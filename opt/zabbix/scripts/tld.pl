#!/usr/bin/perl
#
# Script to manage TLDs in Zabbix.

use strict;
use warnings;

use Path::Tiny;
use lib path($0)->parent->realpath()->stringify();

use Zabbix;
use Getopt::Long;
use MIME::Base64;
use Digest::MD5 qw(md5_hex);
use Expect;
use Data::Dumper;
use RSM;
use RSMSLV;
use TLD_constants qw(:general :templates :groups :value_types :ec :slv :config :api);
use TLDs;
use Text::CSV_XS;
use JSON;
use Pusher;

# Expect stuff for EPP
my $exp_timeout = 3;
my $exp_command = '/opt/zabbix/bin/rsm_epp_enc';
my $exp_output;

my $trigger_thresholds = RSM_TRIGGER_THRESHOLDS;
my $cfg_global_macros = CFG_GLOBAL_MACROS;

################################################################################
# main
################################################################################

sub main()
{
	my $config = get_rsm_config();

	pfail("cannot find 'path' in 'slv' section of rsm config file") unless defined $config->{'slv'}{'path'};

	init_cli_opts(get_rsm_local_id($config));

	if (opt('setup-cron'))
	{
		create_cron_jobs($config->{'slv'}{'path'});
		print("cron jobs created successfully\n");

		return;
	}

	my $server_key = opt('server-id') ? get_rsm_server_key(getopt('server-id')) : get_rsm_local_key($config);
	init_zabbix_api($config, $server_key);

	# expect "registry" monitoring target
	my $target = get_global_macro_value('{$RSM.MONITORING.TARGET}');
	if (!defined($target))
	{
		pfail('cannot find global macro {$RSM.MONITORING.TARGET}');
	}

	if ($target ne MONITORING_TARGET_REGISTRY)
	{
		pfail("expected monitoring target \"${\MONITORING_TARGET_REGISTRY}\", but got \"$target\",".
			" if you'd like to change it, please run:".
			"\n\n/opt/zabbix/scripts/change-macro.pl".
			" --macro '{\$RSM.MONITORING.TARGET}'".
			" --value '${\MONITORING_TARGET_REGISTRY}'");
	}

	# get global macros required by this script
	foreach my $macro (keys %{$cfg_global_macros})
	{
		$cfg_global_macros->{$macro} = get_global_macro_value($macro);
		pfail('cannot get global macro ', $macro) unless defined($cfg_global_macros->{$macro});
	}

	if (opt('set-type'))
	{
		set_type();
	}
	elsif (opt('list-services'))
	{
		list_services(getopt('tld'));
	}
	elsif (opt('get-nsservers-list'))
	{
		list_ns_servers(getopt('tld'));
	}
	elsif (opt('update-nsservers'))
	{
		my $opt_ns_servers = getopt_ns_servers();

		my $config_templateid = get_template_id(TEMPLATE_RSMHOST_CONFIG_PREFIX . getopt('tld'));

		print_ns_changes($config_templateid, $opt_ns_servers);

		update_ns_servers($config_templateid, $opt_ns_servers, $config);
	}
	elsif (opt('delete'))
	{
		manage_tld_objects('delete', getopt('tld'), getopt('dns'), getopt('dns-udp'), getopt('dns-tcp'),
				getopt('dnssec'), getopt('epp'), getopt('rdds'), getopt('rdap'));
	}
	elsif (opt('disable'))
	{
		manage_tld_objects('disable', getopt('tld'), getopt('dns'), getopt('dns-udp'), getopt('dns-tcp'),
				getopt('dnssec'), getopt('epp'), getopt('rdds'), getopt('rdap'));
	}
	else
	{
		add_new_tld($config);
	}
}

sub init_cli_opts($)
{
	my $default_server_id = shift;

	my %OPTS;
	my $rv = GetOptions(\%OPTS,
			"tld=s",
			"delete!",
			"disable!",
			"type=s",
			"set-type!",
			"rdds43-servers=s",
			"rdds80-servers=s",
			"rdap-base-url=s",
			"rdap-test-domain=s",
			"dns-test-prefix=s",
			"rdds-test-prefix=s",
			"ipv4!",
			"ipv6!",
			"dns!",
			"dns-tcp",
			"dns-udp",
			"epp!",
			"rdds!",
			"rdap!",
			"dnssec!",
			"epp-servers=s",
			"epp-user=s",
			"epp-cert=s",
			"epp-privkey=s",
			"epp-commands=s",
			"epp-serverid=s",
			"epp-test-prefix=s",
			"epp-servercert=s",
			"ns-servers-v4=s",
			"ns-servers-v6=s",
			"rdds-ns-string=s",
			"root-servers=s",
			"server-id=s",
			"get-nsservers-list!",
			"update-nsservers!",
			"list-services!",
			"setup-cron!",
			"verbose!",
			"quiet!",
			"help|?");

	if (!$rv || !%OPTS || $OPTS{'help'})
	{
		__usage($default_server_id);
	}

	override_opts(\%OPTS);
	setopt('nolog');

	validate_input($default_server_id);
	lc_options();
}

sub validate_input($)
{
	my $default_server_id = shift;

	if (opt('setup-cron'))
	{
		return;
	}

	my $msg = "";

	if (!opt('tld') && !opt('get-nsservers-list') && !opt('list-services'))
	{
		$msg .= "TLD must be specified (--tld)\n";
	}

	if (!opt('delete') && !opt('disable') && !opt('get-nsservers-list') && !opt('update-nsservers') && !opt('list-services'))
	{
		if (!opt('type'))
		{
			$msg .= "type (--type) of TLD must be specified: @{[TLD_TYPE_G]}, @{[TLD_TYPE_CC]}, @{[TLD_TYPE_OTHER]} or @{[TLD_TYPE_TEST]}\n";
		}
		elsif (getopt('type') ne TLD_TYPE_G && getopt('type') ne TLD_TYPE_CC && getopt('type') ne TLD_TYPE_OTHER && getopt('type') ne TLD_TYPE_TEST)
		{
			my $type = getopt('type');
			$msg .= "invalid TLD type \"$type\", type must be one of: @{[TLD_TYPE_G]}, @{[TLD_TYPE_CC]}, @{[TLD_TYPE_OTHER]} or @{[TLD_TYPE_TEST]}\n";
		}
		elsif (!opt('ns-servers-v4') && !opt('ns-servers-v6'))
		{
			$msg .= "at least one of the --ns-servers-v4,--ns-servers-v6 options must be specified\n";
		}
	}

	if (opt('update-nsservers') && (!opt('ns-servers-v4') && !opt('ns-servers-v6')))
	{
		$msg .= "--update-nsservers requires at least --ns-servers-v4 and/or --ns-servers-v6\n";
	}

	if (opt('set-type'))
	{
		if ($msg)
		{
			chomp($msg);
			__usage($default_server_id, $msg);
		}
		return;
	}

	if (!opt('delete') && !opt('disable'))
	{
		if (!opt('ipv4') && !opt('ipv6') && !opt('get-nsservers-list') && !opt('update-nsservers') && !opt('list-services'))
		{
			$msg .= "at least one IPv4 or IPv6 must be enabled (--ipv4 or --ipv6)\n";
		}

		if (!opt('dns-test-prefix') && !opt('get-nsservers-list') && !opt('update-nsservers') && !opt('list-services'))
		{
			$msg .= "DNS test prefix must be specified (--dns-test-prefix)\n";
		}
	}

	if (opt('delete') && (opt('dns-tcp') || opt('dns-udp')))
	{
		$msg .= "--dns-tcp, --dns-udp are not compatible with --delete\n";
	}

	if (opt('disable') && opt('dns-tcp') && opt('dns-udp'))
	{
		$msg .= "only one of --dns-tcp, --dns-udp can be used with --disable\n";
	}

	if ((opt('rdds43-servers') && !opt('rdds80-servers')) ||
			(opt('rdds80-servers') && !opt('rdds43-servers')))
	{
		$msg .= "none or both --rdds43-servers and --rdds80-servers must be specified\n";
	}

	if (opt('rdds43-servers') && !opt('rdds-test-prefix'))
	{
		$msg .= "--rdds-test-prefix must be specified\n";
	}

	if ((opt('rdap-base-url') && !opt('rdap-test-domain')) ||
			(opt('rdap-test-domain') && !opt('rdap-base-url')))
	{
		$msg .= "none or both --rdap-base-url and --rdap-test-domain must be specified\n";
	}

	if (opt('epp-servers'))
	{
		$msg .= "EPP user must be specified (--epp-user)\n" unless (getopt('epp-user'));
		$msg .= "EPP Client certificate file must be specified (--epp-cert)\n" unless (getopt('epp-cert'));
		$msg .= "EPP Client private key file must be specified (--epp-privkey)\n" unless (getopt('epp-privkey'));
		$msg .= "EPP server ID must be specified (--epp-serverid)\n" unless (getopt('epp-serverid'));
		$msg .= "EPP domain test prefix must be specified (--epp-test-prefix)\n" unless (getopt('epp-serverid'));
		$msg .= "EPP Server certificate file must be specified (--epp-servercert)\n" unless (getopt('epp-servercert'));
	}

	if (!opt('delete') && !opt('disable'))
	{
		if (opt('dns') && !opt('dns-tcp') && !opt('dns-udp'))
		{
			setopt('dns-tcp');
			setopt('dns-udp');
		}
		elsif (opt('dns-tcp') || opt('dns-udp'))
		{
			setopt('dns');
		}
		elsif (!opt('dns') && !opt('dns-tcp') && !opt('dns-udp'))
		{
			setopt('dns');
			setopt('dns-tcp');
			setopt('dns-udp');
		}
	}

	setopt('dns'    , 0) unless opt('dns');
	setopt('dnssec' , 0) unless opt('dnssec');
	setopt('rdds'   , 0) unless opt('rdds');
	setopt('epp'    , 0) unless opt('epp');
	setopt('rdap'   , 0) unless opt('rdap');
	setopt('dns-udp', 0) unless opt('dns-udp');
	setopt('dns-tcp', 0) unless opt('dns-tcp');

	if ($msg)
	{
		chomp($msg);
		__usage($default_server_id, $msg);
	}
}

sub lc_options()
{
	my @options_to_lowercase = (
		"tld",
		"rdds43-servers",
		"rdds80-servers",
		"epp-servers",
		"ns-servers-v4",
		"ns-servers-v6"
	);

	foreach my $option (@options_to_lowercase)
	{
		if (opt($option))
		{
			setopt($option, lc(getopt($option)));
		}
	}
}

sub init_zabbix_api($$)
{
	my $config     = shift;
	my $server_key = shift;

	my $section = $config->{$server_key};

	pfail("no 'za_url' in '$server_key' section of rsm config file") unless defined $section->{'za_url'};
	pfail("no 'za_user' in '$server_key' section of rsm config file") unless defined $section->{'za_user'};
	pfail("no 'za_password' in '$server_key' section of rsm config file") unless defined $section->{'za_password'};

	my $attempts = 3;
	my $result;
	my $error;

	RELOGIN:
	$result = zbx_connect($section->{'za_url'}, $section->{'za_user'}, $section->{'za_password'}, getopt('verbose'));

	if ($result ne true)
	{
		pfail("cannot connect to Zabbix API. " . $result->{'data'});
	}

	# make sure we re-login in case of session invalidation
	my $tld_probe_results_groupid = create_group('TLD Probe results');

	$error = get_api_error($tld_probe_results_groupid);

	if (defined($error))
	{
		if (zbx_need_relogin($tld_probe_results_groupid) eq true)
		{
			goto RELOGIN if (--$attempts);
		}

		pfail($error);
	}
}

################################################################################
# set type
################################################################################

sub set_type()
{
	my $tld  = getopt('tld');
	my $type = getopt('type');

	if (set_tld_type($tld, $type, TLD_TYPE_PROBE_RESULTS_GROUPIDS->{$type}) == true)
	{
		print("$tld set to \"$type\"\n");
	}
	else
	{
		print("$tld is already set to \"$type\"\n");
	}
}

################################################################################
# list services for a single RSMHOST or all RSMHOSTs
################################################################################

sub list_services(;$)
{
	my $server_key = shift;
	my $rsmhost    = shift; # optional

	# NB! Keep @columns in sync with __usage()!
	my @columns = (
		'type',
		'status',
		'{$RSM.DNS.TESTPREFIX}',
		'{$RSM.RDDS.NS.STRING}',
		'{$RSM.RDDS.TESTPREFIX}',
		'{$RSM.TLD.DNSSEC.ENABLED}',
		'{$RSM.TLD.EPP.ENABLED}',
		'{$RSM.TLD.RDDS.ENABLED}',
		'{$RDAP.TLD.ENABLED}',
		'{$RDAP.BASE.URL}',
		'{$RDAP.TEST.DOMAIN}',
		'{$RSM.TLD.RDDS.43.SERVERS}',
		'{$RSM.TLD.RDDS.80.SERVERS}',
	);

	my @rsmhosts = ($rsmhost // get_tld_list());

	my @rows = ();

	foreach my $rsmhost (sort(@rsmhosts))
	{
		my @row = ();
		my $config = get_rsmhost_config($rsmhost);

		push(@row, $rsmhost);
		push(@row, map($config->{$_} // "", @columns));
		push(@rows, \@row);
	}

	# all fields in a CSV must be double-quoted, even if empty
	my $csv = Text::CSV_XS->new({binary => 1, auto_diag => 1, always_quote => 1, eol => "\n"});

	$csv->print(*STDOUT, $_) foreach (@rows);
}

sub get_tld_list()
{
	my $tlds = get_host_group('TLDs', true, false);

	return map($_->{'host'}, @{$tlds->{'hosts'}});
}

sub get_rsmhost_config($)
{
	my $rsmhost = shift;

	my @tld_types = (TLD_TYPE_G, TLD_TYPE_CC, TLD_TYPE_OTHER, TLD_TYPE_TEST);
	my $result;

	my $config_templateid = get_template_id(TEMPLATE_RSMHOST_CONFIG_PREFIX . $rsmhost);
	my $macros = get_host_macro($config_templateid, undef);
	my $rsmhost_host = get_host($rsmhost, true);

	$result->{'status'} = $rsmhost_host->{'status'};

	foreach my $group (@{$rsmhost_host->{'groups'}})
	{
		my $name = $group->{'name'};
		foreach my $tld_type (@tld_types)
		{
			if ($name eq $tld_type)
			{
				$result->{'type'} = $name;
				last;
			}
		}
	}

	foreach my $m (@{$macros})
	{
		$result->{$m->{'macro'}} = $m->{'value'};
	}

	return $result;
}

################################################################################
# manage NS + IP server pairs
################################################################################

sub list_ns_servers(;$)
{
	my $rsmhost = shift; # optional

	# all fields in a CSV must be double-quoted, even if empty
	my $csv = Text::CSV_XS->new({binary => 1, auto_diag => 1, always_quote => 1, eol => "\n"});

	my @rsmhosts = ($rsmhost // get_tld_list());

	foreach my $rsmhost (sort(@rsmhosts))
	{
		my $config_templateid = get_template_id(TEMPLATE_RSMHOST_CONFIG_PREFIX . $rsmhost);

		my $ns_servers = get_ns_servers($config_templateid);
		my @ns_types = keys(%{$ns_servers});

		foreach my $type (sort(@ns_types))
		{
			my @ns_names = keys(%{$ns_servers->{$type}});

			foreach my $ns_name (sort(@ns_names))
			{
				foreach my $ip (@{$ns_servers->{$type}{$ns_name}})
				{
					$csv->print(*STDOUT, [$rsmhost, $type, $ns_name // "", $ip // ""]);
				}
			}
		}
	}
}

sub print_ns_changes($$)
{
	my $config_templateid = shift;
	my $opt_ns_servers    = shift;

	my $ns_servers = get_ns_servers($config_templateid);

	foreach my $opt_nsname (keys(%{$opt_ns_servers}))
	{
		my $opt_ns = $opt_ns_servers->{$opt_nsname};

		foreach my $proto (keys %{$opt_ns})
		{
			my $opt_ips = $opt_ns->{$proto};
			foreach my $opt_ip (@{$opt_ips})
			{
				my $need_to_add = true;

				if (defined($ns_servers->{$proto}) and defined($ns_servers->{$proto}{$opt_nsname}))
				{
					foreach my $ip (@{$ns_servers->{$proto}{$opt_nsname}})
					{
						$need_to_add = false if ($ip eq $opt_ip);
					}
				}

				if ($need_to_add == true)
				{
					print("add\t: $opt_nsname ($opt_ip)\n");
				}
			}

		}
	}

	foreach my $proto (keys %{$ns_servers})
	{
		my $ns = $ns_servers->{$proto};
		foreach my $nsname (keys %{$ns})
		{
			foreach my $ip (@{$ns->{$nsname}})
			{
				my $need_to_remove = false;

				if (defined($opt_ns_servers->{$nsname}{$proto}))
				{
					$need_to_remove = true;

					foreach my $opt_ip (@{$opt_ns_servers->{$nsname}{$proto}})
					{
						$need_to_remove = false if ($opt_ip eq $ip);
					}
				}
				else
				{
					$need_to_remove = true;
				}

				if ($need_to_remove == true)
				{
					print("disable\t: $nsname ($ip)\n");
				}
			}
		}
	}
}

sub get_ns_servers($)
{
	my $config_templateid = shift;

	my $macro = get_host_macro($config_templateid, '{$RSM.DNS.NAME.SERVERS}');

	my $result = {};

	return $result unless (defined($macro));

	# <ns1>,<ip1> <ns2>,<ip2> ...
	foreach my $nsip (split(' ', $macro->{'value'}))
	{
		my ($ns, $ip) = split(',', $nsip);

		if ($ip =~ /\d*\.\d*\.\d*\.\d+/)
		{
			push(@{$result->{'v4'}{$ns}}, $ip);
		}
		else
		{
			push(@{$result->{'v6'}{$ns}}, $ip);
		}
	}

	return $result;
}

# locates and returns macro value from the result as returned by API
sub __get_macro_value_from_result($$)
{
	my $result = shift;
	my $macro  = shift;

	my $macro_hash_ref = (grep { $_->{'macro'} eq $macro } @{$result->{'macros'}})[0];

	return $macro_hash_ref->{'value'};
}

################################################################################
# delete or disable RSMHOST or its objects
################################################################################

sub manage_tld_objects($$$$$$$)
{
	my $action  = shift;
	my $tld     = shift;
	my $dns     = shift;
	my $dns_udp = shift;
	my $dns_tcp = shift;
	my $dnssec  = shift;
	my $epp     = shift;
	my $rdds    = shift;
	my $rdap    = shift;

	my $types = {
		'dns'		=> $dns,
		'dns-udp'	=> $dns_udp,
		'dns-tcp'	=> $dns_tcp,
		'dnssec'	=> $dnssec,
		'epp'		=> $epp,
		'rdds'		=> $rdds
	};

	my $config_templateid;

	$types->{'rdap'} = $rdap if (__is_rdap_standalone());

	print("Getting main host of the TLD: ");
	my $main_hostid = get_host($tld, false);

	if (scalar(%{$main_hostid}))
	{
		$main_hostid = $main_hostid->{'hostid'};
		print("$main_hostid\n");
	}
	else
	{
		pfail("cannot find host \"$tld\"");
	}

	print("Getting main template of the TLD: ");
	my $tld_template = get_template(TEMPLATE_RSMHOST_CONFIG_PREFIX . $tld, true, true);

	if (scalar(%{$tld_template}))
	{
		$config_templateid = $tld_template->{'templateid'};
		print("$config_templateid\n");
	}
	else
	{
		pfail("cannot find template \"" . TEMPLATE_RSMHOST_CONFIG_PREFIX . "$tld\"");
	}

	my @tld_hostids;

	my @affected_services;
	my $total_services = scalar(keys(%{$types}));
	foreach my $s (keys(%{$types}))
	{
		push(@affected_services, $s) if ($types->{$s} eq true);
	}

	if (scalar(@affected_services) == 0)
	{
		foreach my $s (keys(%{$types}))
		{
			$types->{$s} = true;
		}
	}

	print("Requested to $action '$tld'");
	if (scalar(@affected_services) != 0 && scalar(@affected_services) != $total_services)
	{
		print(" (", join(',', @affected_services), ")");
	}
	print("\n");

	foreach my $host (@{$tld_template->{'hosts'}})
	{
		push(@tld_hostids, $host->{'hostid'});
	}

	# This condition checks if all services selected. This means we need to either check dns, epp, rdds
	# before switch to Standalone RDAP or dns, epp, rdds, rdap after the switch.
	if ($types->{'dns'} eq true and $types->{'epp'} eq true and $types->{'rdds'} eq true and
			(__is_rdap_standalone() ? ($types->{'rdap'} eq true) : 1))
	{
		my @tmp_hostids;
		my @hostids_arr;

		foreach my $hostid (@tld_hostids)
		{
			push(@tmp_hostids, {'hostid' => $hostid});
			push(@hostids_arr, $hostid);
		}

		if ($action eq 'disable')
		{
			generate_report($tld, time(), 1);

			my $result = disable_hosts(\@tmp_hostids);

			if (!$result || !%{$result})
			{
				pfail("an error occurred while disabling hosts");
			}

			exit;
		}

		if ($action eq 'delete')
		{
			remove_hosts(\@hostids_arr);
			remove_templates([$config_templateid]);

			my $hostgroupid = get_host_group('TLD ' . $tld, false, false);
			$hostgroupid = $hostgroupid->{'groupid'};
			remove_hostgroups([$hostgroupid]);

			return;
		}
	}

	foreach my $type (keys %{$types})
	{
		next if ($types->{$type} eq false);

		if ($type eq 'dnssec')
		{
			really(create_macro('{$RSM.TLD.DNSSEC.ENABLED}', 0, $config_templateid, true));
			next;
		}
		elsif ($type eq 'dns-udp')
		{
			if (__get_macro_value_from_result($tld_template, '{$RSM.TLD.DNS.TCP.ENABLED}') eq "0")
			{
				pfail("cannot disable DNS UDP because DNS TCP is already disabled");
			}
			really(create_macro('{$RSM.TLD.DNS.UDP.ENABLED}', 0, $config_templateid, true));
			next;
		}
		elsif ($type eq 'dns-tcp')
		{
			if (__get_macro_value_from_result($tld_template, '{$RSM.TLD.DNS.UDP.ENABLED}') eq "0")
			{
				pfail("cannot disable DNS TCP because DNS UDP is already disabled");
			}
			really(create_macro('{$RSM.TLD.DNS.TCP.ENABLED}', 0, $config_templateid, true));
			next;
		}

		my $macro = $type eq 'rdap' ? '{$RDAP.TLD.ENABLED}' : '{$RSM.TLD.' . uc($type) . '.ENABLED}';

		create_macro($macro, 0, $config_templateid, true);

		my $items = get_items_like($main_hostid, $type, false);
		my @itemids = map { $items->{$_}->{'key_'} ne "$type.enabled" ? $_ : () } keys(%{$items});

		if (scalar(@itemids) > 0)
		{
			$action eq 'disable' ? disable_items(\@itemids) : remove_items(\@itemids);
		}
		else
		{
			print("cannot find $type items on host $tld ($main_hostid)\n");
		}

		set_linked_items_status($type eq 'rdds' ? 'rsm.rdds[' : 'rdap[', $tld, 0) if ($action eq 'disable');
		set_linked_items_status('rdap[', $tld, 0) if (!__is_rdap_standalone() && $action eq 'disable');
	}
}

################################################################################
# add or update RSMHOST
################################################################################

sub add_new_tld($)
{
	my $config = shift;

	my $proxies = get_proxies_list();

	pfail("please add at least one probe first using probes.pl") unless (%{$proxies});

	update_root_server_macros(getopt('root-servers'));

	my $config_templateid = create_rsmhost_template(getopt('tld'), $config);
	my $rsmhost_groupid = really(create_group('TLD ' . getopt('tld')));

	create_rsmhost($config_templateid, getopt('tld'), getopt('type'));

	my $proxy_mon_templateid = create_probe_health_tmpl();

	create_tld_hosts_on_probes($proxy_mon_templateid, $rsmhost_groupid, $config_templateid, $proxies);
}

sub getopt_ns_servers()
{
	my $ns_servers;

	# just in case, the input should have been validated by now
	unless (opt('ns-servers-v4') or opt('ns-servers-v6'))
	{
		pfail("option --ns-servers-v4 and/or --ns-servers-v6 required for this invocation");
	}

	if (getopt('ns-servers-v4') && getopt('ipv4') == 1)
	{
		my @ns_servers = split(/\s/, getopt('ns-servers-v4'));
		foreach my $ns (@ns_servers)
		{
			next if ($ns eq '');

			my @entries = split(/,/, $ns);

			pfail("incorrect Name Server format: expected \"<NAME>,<IP>\" got \"$ns\"") unless ($entries[0] && $entries[1]);

			my $exists = 0;
			foreach my $ip (@{$ns_servers->{$entries[0]}{'v4'}})
			{
				if ($ip eq $entries[1])
				{
					$exists = 1;
					last;
				}
			}

			push(@{$ns_servers->{$entries[0]}{'v4'}}, $entries[1]) unless ($exists);
		}
	}

	if (getopt('ns-servers-v6') && getopt('ipv6') == 1)
	{
		my @ns_servers = split(/\s/, getopt('ns-servers-v6'));
		foreach my $ns (@ns_servers)
		{
			next if ($ns eq '');

			my @entries = split(/,/, $ns);

			my $exists = 0;
			foreach my $ip (@{$ns_servers->{$entries[0]}{'v6'}})
			{
				if ($ip eq $entries[1])
				{
					$exists = 1;
					last;
				}
			}

			push(@{$ns_servers->{$entries[0]}{'v6'}}, $entries[1]) unless ($exists);
		}
	}

	return $ns_servers;
}

sub update_ns_servers($$$)
{
	my $config_templateid = shift;
	my $opt_ns_servers    = shift;
	my $config            = shift;

	my $macro_value = '';
	my $data = [];

	foreach my $ns_name (sort keys %{$opt_ns_servers})
	{
		print $ns_name . "\n";

		my @ipv4 = defined($opt_ns_servers->{$ns_name}{'v4'}) ? @{$opt_ns_servers->{$ns_name}{'v4'}} : undef;
		my @ipv6 = defined($opt_ns_servers->{$ns_name}{'v6'}) ? @{$opt_ns_servers->{$ns_name}{'v6'}} : undef;

		for (my $i_ipv4 = 0; $i_ipv4 <= $#ipv4; $i_ipv4++)
		{
			next unless defined $ipv4[$i_ipv4];
			print("	--v4     $ipv4[$i_ipv4]\n");

			$macro_value .= " " unless ($macro_value eq '');
			$macro_value .= "$ns_name,$ipv4[$i_ipv4]";

			push(@{$data}, {'{#NS}' => $ns_name, '{#IP}' => $ipv4[$i_ipv4]});
		}

		for (my $i_ipv6 = 0; $i_ipv6 <= $#ipv6; $i_ipv6++)
		{
			next unless defined $ipv6[$i_ipv6];
			print("	--v6     $ipv6[$i_ipv6]\n");

			$macro_value .= " " unless ($macro_value eq '');
			$macro_value .= "$ns_name,$ipv6[$i_ipv6]";

			push(@{$data}, {'{#NS}' => $ns_name, '{#IP}' => $ipv6[$i_ipv6]});
		}
	}

	really(create_macro('{$RSM.DNS.NAME.SERVERS}', $macro_value, $config_templateid, 1));

	my $json = [{
		'host'  => getopt('tld'),
		'key'   => 'rsm.dns.nsip.discovery',
		'clock' => time(),
		'value' => encode_json($data),
	}];

	# timeout = 10, attempts = 5
	push_to_trapper($config->{'slv'}{'zserver'}, $config->{'slv'}{'zport'}, 10, 5, $json);
}

sub create_rsmhost_template($$)
{
	my $rsmhost = shift;
	my $config = shift;

	my $config_templateid = really(create_template(TEMPLATE_RSMHOST_CONFIG_PREFIX . $rsmhost));

	my $opt_ns_servers = getopt_ns_servers();

	update_ns_servers($config_templateid, $opt_ns_servers, $config);

	really(create_macro('{$RSM.TLD}', $rsmhost, $config_templateid));
	really(create_macro('{$RSM.DNS.TESTPREFIX}', getopt('dns-test-prefix'), $config_templateid, 1));
	really(create_macro('{$RSM.RDDS.TESTPREFIX}', getopt('rdds-test-prefix'), $config_templateid, 1)) if (opt('rdds-test-prefix'));
	really(create_macro('{$RSM.RDDS.NS.STRING}', opt('rdds-ns-string') ? getopt('rdds-ns-string') : CFG_DEFAULT_RDDS_NS_STRING, $config_templateid, 1));
	really(create_macro('{$RSM.TLD.DNS.UDP.ENABLED}', getopt('dns-udp'), $config_templateid, 1));
	really(create_macro('{$RSM.TLD.DNS.TCP.ENABLED}', getopt('dns-tcp'), $config_templateid, 1));
	really(create_macro('{$RSM.TLD.DNSSEC.ENABLED}', getopt('dnssec'), $config_templateid, 1));
	really(create_macro('{$RSM.TLD.RDDS.ENABLED}', opt('rdds43-servers') ? 1 : 0, $config_templateid, 1));
	really(create_macro('{$RSM.TLD.RDDS.43.SERVERS}', getopt('rdds43-servers') // '', $config_templateid, 1));
	really(create_macro('{$RSM.TLD.RDDS.80.SERVERS}', getopt('rdds80-servers') // '', $config_templateid, 1));
	really(create_macro('{$RSM.TLD.EPP.ENABLED}', opt('epp-servers') ? 1 : 0, $config_templateid, 1));

	if (opt('rdap-base-url') && opt('rdap-test-domain'))
	{
		really(create_macro('{$RDAP.BASE.URL}', getopt('rdap-base-url'), $config_templateid, 1));
		really(create_macro('{$RDAP.TEST.DOMAIN}', getopt('rdap-test-domain'), $config_templateid, 1));
		really(create_macro('{$RDAP.TLD.ENABLED}', 1, $config_templateid, 1));
	}
	else
	{
		really(create_macro('{$RDAP.TLD.ENABLED}', 0, $config_templateid, 1));
	}

	if (getopt('epp-servers'))
	{
		my ($buf, $error);

		if (read_file(getopt('epp-cert'), \$buf, \$error) != SUCCESS)
		{
			pfail("cannot read file \"", getopt('epp-cert'), "\": $error");
		}

		my $m = '{$RSM.EPP.KEYSALT}';
		my $keysalt = get_global_macro_value($m);
		pfail('cannot get macro ', $m) unless defined($keysalt);
		trim($keysalt);
		pfail("global macro $m must conatin |") unless ($keysalt =~ m/\|/);

		if (getopt('epp-commands'))
		{
			really(create_macro('{$RSM.EPP.COMMANDS}', getopt('epp-commands'), $config_templateid, 1));
		}
		else
		{
			really(create_macro('{$RSM.EPP.COMMANDS}', '/opt/test-sla/epp-commands/' . $rsmhost, $config_templateid));
		}
		really(create_macro('{$RSM.EPP.USER}', getopt('epp-user'), $config_templateid, 1));
		really(create_macro('{$RSM.EPP.CERT}', encode_base64($buf, ''),  $config_templateid, 1));
		really(create_macro('{$RSM.EPP.SERVERID}', getopt('epp-serverid'), $config_templateid, 1));
		really(create_macro('{$RSM.EPP.TESTPREFIX}', getopt('epp-test-prefix'), $config_templateid, 1));
		really(create_macro('{$RSM.EPP.SERVERCERTMD5}', get_md5(getopt('epp-servercert')), $config_templateid, 1));

		my $passphrase = get_sensdata("Enter EPP secret key passphrase: ");
		my $passwd = get_sensdata("Enter EPP password: ");
		really(create_macro('{$RSM.EPP.PASSWD}', get_encrypted_passwd($keysalt, $passphrase, $passwd), $config_templateid, 1));
		$passwd = undef;
		really(create_macro('{$RSM.EPP.PRIVKEY}', get_encrypted_privkey($keysalt, $passphrase, getopt('epp-privkey')), $config_templateid, 1));
		$passphrase = undef;

		print("EPP data saved successfully.\n");
	}

	return $config_templateid;
}

sub get_sensdata($)
{
	my $prompt = shift;

	my $sensdata;

	print($prompt);
	system('stty', '-echo');
	chop($sensdata = <STDIN>);
	system('stty', 'echo');
	print("\n");

	return $sensdata;
}

sub exp_get_keysalt($)
{
	my $self = shift;

	if ($self->match() =~ m/^([^\s]+\|[^\s]+)/)
	{
		$exp_output = $1;
	}
}

sub get_encrypted_passwd($$$)
{
	my $keysalt    = shift;
	my $passphrase = shift;
	my $passwd     = shift;

	my @params = split('\|', $keysalt);

	pfail("$keysalt: invalid keysalt") unless (scalar(@params) == 2);

	push(@params, '-n');

	my $exp = new Expect or pfail("cannot create Expect object");
	$exp->raw_pty(1);
	$exp->spawn($exp_command, @params) or pfail("cannot spawn $exp_command: $!");

	$exp->send("$passphrase\n");
	$exp->send("$passwd\n");

	print("");
	$exp->expect($exp_timeout, [qr/.*\n/, \&exp_get_keysalt]);

	$exp->soft_close();

	pfail("$exp_command returned error") unless ($exp_output and $exp_output =~ m/\|/);

	my $ret = $exp_output;
	$exp_output = undef;

	return $ret;
}

sub get_encrypted_privkey($$$)
{
	my $keysalt    = shift;
	my $passphrase = shift;
	my $file       = shift;

	my @params = split('\|', $keysalt);

	pfail("$keysalt: invalid keysalt") unless (scalar(@params) == 2);

	push(@params, '-n', '-f', $file);

	my $exp = new Expect or pfail("cannot create Expect object");
	$exp->raw_pty(1);
	$exp->spawn($exp_command, @params) or pfail("cannot spawn $exp_command: $!");

	$exp->send("$passphrase\n");

	print("");
	$exp->expect($exp_timeout, [qr/.*\n/, \&exp_get_keysalt]);

	$exp->soft_close();

	pfail("$exp_command returned error") unless ($exp_output and $exp_output =~ m/\|/);

	my $ret = $exp_output;
	$exp_output = undef;

	return $ret;
}

sub get_md5($)
{
	my $file = shift;

	my $contents = do
	{
		local $/ = undef;
		open(my $fh, "<", $file) or pfail("cannot open $file: $!");
		<$fh>;
	};

	my $index = index($contents, "-----BEGIN CERTIFICATE-----");
	pfail("specified file $file does not contain line \"-----BEGIN CERTIFICATE-----\"") if ($index == -1);

	return md5_hex(substr($contents, $index));
}

sub create_dependent_trigger_chain($$$$)
{
	my $host_name       = shift;
	my $service_or_nsip = shift;
	my $fun             = shift;
	my $thresholds_ref  = shift;

	my $depend_down;
	my $created;

	foreach my $k (sort keys %{$thresholds_ref})
	{
		my $threshold = $thresholds_ref->{$k}{'threshold'};
		my $priority = $thresholds_ref->{$k}{'priority'};
		next if ($threshold eq 0);

		my $result = &$fun($service_or_nsip, $host_name, $threshold, $priority, \$created);

		my $triggerid = $result->{'triggerids'}[0];

		if ($created && defined($depend_down))
		{
			add_dependency($triggerid, $depend_down);
		}

		$depend_down = $triggerid;
	}
}

sub __is_rdap_standalone()
{
	return $cfg_global_macros->{'{$RSM.RDAP.STANDALONE}'} != 0 &&
			time() >= $cfg_global_macros->{'{$RSM.RDAP.STANDALONE}'};
}

sub create_rsmhost($$$)
{
	my $config_templateid = shift;
	my $tld_name          = shift;
	my $tld_type          = shift;

	my $tld_hostid = really(create_host({
		'groups'     => [
			{'groupid' => TLDS_GROUPID},
			{'groupid' => TLD_TYPE_GROUPIDS->{$tld_type}}
		],
		'templates' => [
			{'templateid' => $config_templateid},
			{'templateid' => CONFIG_HISTORY_TEMPLATEID}
		],
		'host'       => $tld_name,
		'status'     => HOST_STATUS_MONITORED,
		'interfaces' => [DEFAULT_MAIN_INTERFACE]
	}));

	link_status_templates($tld_hostid, $tld_name);
}

sub link_status_templates($$)
{
	my $hostid    = shift;
	my $host_name = shift;

	my $all_templates;

	if (!__is_rdap_standalone())	# we haven't switched to RDAP yet
	{
		$all_templates = link_template_to_host($hostid, RDDS_STATUS_TEMPLATEID, $all_templates);

		# link RDAP template in advance
		if (opt('rdap-base-url'))
		{
			$all_templates = link_template_to_host($hostid, RDAP_STATUS_TEMPLATEID, $all_templates);
		}
	}
	else
	{
		# after the switch, RDDS and RDAP are opt-in
		if (opt('rdap-base-url'))
		{
			$all_templates = link_template_to_host($hostid, RDAP_STATUS_TEMPLATEID, $all_templates);
		}

		if (opt('rdds43-servers') || opt('rdds80-servers'))
		{
			$all_templates = link_template_to_host($hostid, RDDS_STATUS_TEMPLATEID, $all_templates);
		}
	}

	$all_templates = link_template_to_host($hostid, DNS_STATUS_TEMPLATEID, $all_templates);

	if (getopt('dnssec'))
	{
		$all_templates = link_template_to_host($hostid, DNSSEC_STATUS_TEMPLATEID, $all_templates);
	}
}

sub create_tld_hosts_on_probes($$$$)
{
	my $proxy_mon_templateid = shift;
	my $rsmhost_groupid      = shift;
	my $config_templateid    = shift;
	my $proxies              = shift;

	# TODO Revise this part because it is creating entities (e.g. "<Probe>", "<Probe> - mon" hosts) which should
	# have been created already by preceeding probes.pl execution. At least move the code to one place and reuse it.

	foreach my $proxyid (sort(keys(%{$proxies})))
	{
		# skip disabled probes
		next unless ($proxies->{$proxyid}{'status'} == HOST_STATUS_PROXY_PASSIVE);

		my $probe_name = $proxies->{$proxyid}{'host'};

		print("$proxyid\n$probe_name\n");

		my $probe_groupid = really(create_group($probe_name));
		my $probe_templateid;
		my $status;

		$probe_templateid = create_probe_template($probe_name);
		$status = HOST_STATUS_MONITORED;

		really(create_host({
			'groups' => [
				{'groupid' => PROBES_GROUPID}
			],
			'templates' => [
				{'templateid' => PROBE_STATUS_TEMPLATEID},
				{'templateid' => APP_ZABBIX_PROXY_TEMPLATEID},
				{'templateid' => $probe_templateid}
			],
			'host'         => $probe_name,
			'status'       => $status,
			'proxy_hostid' => $proxyid,
			'interfaces'   => [DEFAULT_MAIN_INTERFACE]
		}));

		my $hostid = really(create_host({
			'groups' => [
				{'groupid' => PROBES_MON_GROUPID}
			],
			'templates' => [
				{'templateid' => $proxy_mon_templateid}
			],
			'host' => "$probe_name - mon",
			'status' => $status,
			'interfaces' => [
				{
					'type'  => INTERFACE_TYPE_AGENT,
					'main'  => true,
					'useip' => true,
					'ip'    => $proxies->{$proxyid}{'interface'}{'ip'},
					'dns'   => '',
					'port'  => '10050'
				}
			]
		}));

		really(create_macro('{$RSM.PROXY_NAME}', $probe_name, $hostid, 1));

		#  TODO: add the host above to more host groups: "TLD Probe Results" and/or "gTLD Probe Results" and perhaps others

		really(create_host({
			'groups' => [
				{'groupid' => $rsmhost_groupid},
				{'groupid' => $probe_groupid},
				{'groupid' => TLD_PROBE_RESULTS_GROUPID},
				{'groupid' => TLD_TYPE_PROBE_RESULTS_GROUPIDS->{getopt('type')}}
			],
			'templates' => [
				{'templateid' => $config_templateid},
				{'templateid' => DNS_TEST_TEMPLATEID},
				{'templateid' => RDAP_TEST_TEMPLATEID},
				{'templateid' => $probe_templateid},
				{'templateid' => RDDS_TEST_TEMPLATEID}
			],
			'host'         => getopt('tld') . ' ' . $probe_name,
			'status'       => $status,
			'proxy_hostid' => $proxyid,
			'interfaces'   => [DEFAULT_MAIN_INTERFACE]
		}));
	}

	set_linked_items_status('rdap[', getopt('tld'), opt('rdap-base-url') && opt('rdap-test-domain'));
	set_linked_items_status('rsm.rdds[', getopt('tld'), opt('rdds43-servers'));
}

sub set_linked_items_status($$$)
{
	my $like    = shift;
	my $tld     = shift;
	my $enabled = shift;

	my $template = TEMPLATE_RSMHOST_CONFIG_PREFIX . $tld;
	my $result = get_template($template, false, true);	# do not select macros, select hosts

	pfail("$tld template \"$template\" does not exist") if (keys(%{$result}) == 0);

	foreach my $host_ref (@{$result->{'hosts'}})
	{
		my $hostid = $host_ref->{'hostid'};

		my $result2 = really(get_items_like($hostid, $like, false));	# not a template

		my @items = keys(%{$result2});

		$enabled ? enable_items(\@items) : disable_items(\@items);
	}
}

sub really($)
{
	my $api_result = shift;

	pfail($api_result->{'data'}) if (check_api_error($api_result) == true);

	return $api_result;
}

sub __usage($;$)
{
	my $default_server_id = shift;
	my $error_message     = shift;

	if ($error_message)
	{
		print($error_message, "\n\n");
	}

	print <<EOF;
Usage: $0 [options]

Required options

        --tld=STRING
                TLD name
        --dns-test-prefix=STRING
                domain test prefix for DNS monitoring (specify '*randomtld*' for root servers monitoring)

Other options
        --delete
                delete specified TLD or TLD services specified by: --dns, --rdds, --rdap, --epp
                if none or all services specified - will delete the whole TLD
        --disable
                disable specified TLD or TLD services specified by: --dns, --rdds, --rdap, --epp
                if none or all services specified - will disable the whole TLD
        --list-services
                list services of each TLD, the output is comma-separated list:
                <TLD>,<TLD-TYPE>,<TLD-STATUS>,<RDDS.DNS.TESTPREFIX>,<RDDS.NS.STRING>,<RDDS.TESTPREFIX>,
                <TLD.DNSSEC.ENABLED>,<TLD.EPP.ENABLED>,<TLD.RDDS.ENABLED>,<TLD.RDAP.ENABLED>,
                <RDAP.BASE.URL>,<RDAP.TEST.DOMAIN>,<RDDS43.SERVERS>,<RDDS80.SERVERS>
        --get-nsservers-list
                CSV formatted list of NS + IP server pairs for specified TLD:
                <TLD>,<IP-VERSION>,<NAME-SERVER>,<IP>
        --update-nsservers
                update all NS + IP pairs for specified TLD.
        --type=STRING
                Type of TLD. Possible values: @{[TLD_TYPE_G]}, @{[TLD_TYPE_CC]}, @{[TLD_TYPE_OTHER]}, @{[TLD_TYPE_TEST]}.
        --set-type
                set specified TLD type and exit
        --ipv4
                enable IPv4
                (default: disabled)
        --ipv6
                enable IPv6
                (default: disabled)
        --dnssec
                enable DNSSEC in DNS tests
                (default: disabled)
        --ns-servers-v4=STRING
                list of IPv4 name servers separated by space (name and IP separated by comma): "NAME,IP[ NAME,IP2 ...]"
        --ns-servers-v6=STRING
                list of IPv6 name servers separated by space (name and IP separated by comma): "NAME,IP[ NAME,IP2 ...]"
        --rdds43-servers=STRING
                list of RDDS43 servers separated by comma: "NAME1,NAME2,..."
        --rdds80-servers=STRING
                list of RDDS80 servers separated by comma: "NAME1,NAME2,..."
        --rdap-base-url=STRING
                RDAP service endpoint, e.g. "http://rdap.nic.cz"
                Specify "not listed" to get error -390, e. g. --rdap-base-url="not listed"
                Specify "no https" to get error -391, e. g. --rdap-base-url="no https"
        --rdap-test-domain=STRING
                test domain for RDAP queries
        --epp-servers=STRING
                list of EPP servers separated by comma: "NAME1,NAME2,..."
        --epp-user
                specify EPP username
        --epp-cert
                path to EPP Client certificates file
        --epp-servercert
                path to EPP Server certificates file
        --epp-privkey
                path to EPP Client private key file (unencrypted)
        --epp-serverid
                specify expected EPP Server ID string in reply
        --epp-test-prefix=STRING
                this string represents DOMAIN (in DOMAIN.TLD) to use in EPP commands
        --epp-commands
                path to a directory on the Probe Node containing EPP command templates
                (default: /opt/test-sla/epp-commands/TLD)
        --rdds-ns-string=STRING
                name server prefix in the WHOIS output
                (default: "${\CFG_DEFAULT_RDDS_NS_STRING}")
        --root-servers=STRING
                list of IPv4 and IPv6 root servers separated by comma and semicolon: "v4IP1[,v4IP2,...][;v6IP1[,v6IP2,...]]"
        --server-id=STRING
                ID of Zabbix server $default_server_id
        --rdds-test-prefix=STRING
                domain test prefix for RDDS monitoring (needed only if rdds servers specified)
        --setup-cron
                create cron jobs and exit
        --epp
                Action with EPP
                (default: no)
        --dns
                Action with DNS
                (default: no)
        --dns-udp
                Action with DNS UDP
                (default: no; this option is mutually exclusive with --dns-tcp when used with --disable)
        --dns-tcp
                Action with DNS TCP
                (default: no; this option is mutually exclusive with --dns-udp when used with --disable)
        --rdds
                Action with RDDS
                (default: no)
        --rdap
                Action with RDAP
                (only effective after switch to Standalone RDAP, default: no)
        --help
                display this message
EOF
	exit(1);
}

main();
