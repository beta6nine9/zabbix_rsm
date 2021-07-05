#!/usr/bin/perl
#
# Script to manage TLDs in Zabbix.

use strict;
use warnings;

use Path::Tiny;
use lib path($0)->parent->realpath()->stringify();

use Zabbix;
use Getopt::Long;
use List::Util qw(first);
use MIME::Base64;
use Digest::MD5 qw(md5_hex);
use Expect;
use Data::Dumper;
use RSM;
use RSMSLV;
use TLD_constants qw(:general :templates :groups :value_types :ec :slv :config :api);
use TLDs;
use Text::CSV_XS;

# Expect stuff for EPP
my $exp_timeout = 3;
my $exp_command = '/opt/zabbix/bin/rsm_epp_enc';
my $exp_output;

my $trigger_thresholds = RSM_TRIGGER_THRESHOLDS;
my $cfg_global_macros = CFG_GLOBAL_MACROS;

my $ns_servers;

################################################################################
# main
################################################################################

sub main()
{
	my $config = get_rsm_config();

	pfail("SLV scripts path is not specified. Please check configuration file") unless defined $config->{'slv'}{'path'};

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
		pfail("expected monitoring target \"${\MONITORING_TARGET_REGISTRY}\", but got \"$target\", if you'd like to change it, please run:".
			"\n\n/opt/zabbix/scripts/change-macro.pl --macro '{\$RSM.MONITORING.TARGET}' --value '${\MONITORING_TARGET_REGISTRY}'");
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
		list_services($server_key, getopt('tld'));
	}
	elsif (opt('get-nsservers-list'))
	{
		list_nsservers($server_key, getopt('tld'));
	}
	elsif (opt('update-nsservers'))
	{
		$ns_servers = get_ns_servers(getopt('tld'));
		update_nsservers($server_key, getopt('tld'), $ns_servers);
	}
	elsif (opt('delete'))
	{
		manage_tld_objects('delete', getopt('tld'), getopt('dns'), getopt('dnssec'),
				getopt('epp'), getopt('rdds'), getopt('rdap'));
	}
	elsif (opt('disable'))
	{
		manage_tld_objects('disable', getopt('tld'), getopt('dns'), getopt('dnssec'),
				getopt('epp'), getopt('rdds'), getopt('rdap'));
	}
	else
	{
		add_new_tld();
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
			"rdds43-test-domain=s",
			"ipv4!",
			"ipv6!",
			"dns!",
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

	if ((opt('rdds43-servers') && !opt('rdds80-servers')) ||
			(opt('rdds80-servers') && !opt('rdds43-servers')))
	{
		$msg .= "none or both --rdds43-servers and --rdds80-servers must be specified\n";
	}

	if (opt('rdds43-servers'))
	{
		if (!opt('rdds-test-prefix') && !opt('rdds43-test-domain'))
		{
			$msg .= "--rdds-test-prefix or --rdds43-test-domain must be specified\n";
		}
		if (opt('rdds-test-prefix') && opt('rdds43-test-domain'))
		{
			$msg .= "only one of --rdds-test-prefix and --rdds43-test-domain must be specified\n";
		}
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

	# why on Earth? Do not do this.
	#getopt('ipv4') = 0 if (opt('update-nsservers'));
	#getopt('ipv6') = 0 if (opt('update-nsservers'));

	setopt('dns'   , 0) unless opt('dns');
	setopt('dnssec', 0) unless opt('dnssec');
	setopt('rdds'  , 0) unless opt('rdds');
	setopt('epp'   , 0) unless opt('epp');
	setopt('rdap'  , 0) unless opt('rdap');

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

	pfail("Zabbix API URL is not specified. Please check configuration file") unless defined $section->{'za_url'};
	pfail("Username for Zabbix API is not specified. Please check configuration file") unless defined $section->{'za_user'};
	pfail("Password for Zabbix API is not specified. Please check configuration file") unless defined $section->{'za_password'};

	my $attempts = 3;
	my $result;
	my $error;

	RELOGIN:
	$result = zbx_connect($section->{'za_url'}, $section->{'za_user'}, $section->{'za_password'}, getopt('verbose'));

	if ($result ne true)
	{
		pfail("Could not connect to Zabbix API. " . $result->{'data'});
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

sub list_services($;$)
{
	my $server_key = shift;
	my $rsmhost    = shift; # optional

	# NB! Keep @columns in sync with __usage()!
	my @columns = (
		'tld_type',
		'tld_status',
		'{$RSM.DNS.TESTPREFIX}',
		'{$RSM.RDDS.NS.STRING}',
		'rdds43_test_prefix',
		'{$RSM.TLD.DNSSEC.ENABLED}',
		'{$RSM.TLD.EPP.ENABLED}',
		'{$RSM.TLD.RDDS.ENABLED}',
		'{$RDAP.TLD.ENABLED}',
		'{$RDAP.BASE.URL}',
		'{$RDAP.TEST.DOMAIN}',
		'rdds43_servers',
		'rdds80_servers',
		'{$RSM.RDDS43.TEST.DOMAIN}',
	);

	my @rsmhosts = ($rsmhost // get_tld_list());

	my @rows = ();

	foreach my $rsmhost (sort(@rsmhosts))
	{
		my $services = get_services($server_key, $rsmhost);

		my @row = ();

		push(@row, $rsmhost);
		push(@row, map($services->{$_}, @columns));

		push(@rows, \@row);
	}

	# convert undefs to empty strings
	@rows = map([map($_ // "", @{$_})], @rows);

	# all fields in a CSV must be double-quoted, even if empty
	my $csv = Text::CSV_XS->new({binary => 1, auto_diag => 1, always_quote => 1, eol => "\n"});

	$csv->print(*STDOUT, $_) foreach (@rows);
}

sub get_tld_list()
{
	my $tlds = get_host_group('TLDs', true, false);

	return map($_->{'host'}, @{$tlds->{'hosts'}});
}

sub get_services($$)
{
	my $server_key = shift;
	my $rsmhost    = shift;

	my %tld_types = (
		TLD_TYPE_G    , undef,
		TLD_TYPE_CC   , undef,
		TLD_TYPE_OTHER, undef,
		TLD_TYPE_TEST , undef,
	);

	my $result;

	# get template id, list of macros

	my $template = get_template("Template $rsmhost", true, false);
	pfail("TLD \"$rsmhost\" does not exist on \"$server_key\"") unless ($template->{'templateid'});

	# store macros

	map { $result->{$_->{'macro'}} = $_->{'value'} } @{$template->{'macros'}};

	# get status (enabled, disabled), type (gTLD, ccTLD, ...)

	my $tld_host = get_host($rsmhost, true);

	$result->{'tld_status'} = $tld_host->{'status'};
	$result->{'tld_type'} = first { exists($tld_types{$_}) } map($_->{'name'}, @{$tld_host->{'groups'}});

	# get RDDS43 test prefix

	if (defined($result->{'{$RSM.RDDS43.TEST.DOMAIN}'}))
	{
		$result->{'rdds43_test_prefix'} = $result->{'{$RSM.RDDS43.TEST.DOMAIN}'};
		$result->{'rdds43_test_prefix'} =~ s/^(.+)\.[^.]+$/$1/ if ($rsmhost ne ".");
	}

	# get RDDS43 and RDDS80 servers

	my $items = get_items_like($template->{'templateid'}, 'rsm.rdds[', true);

	return $result if (0 == scalar(keys(%{$items})));

	# skip disabled items
	foreach my $itemid (keys(%{$items}))
	{
		next if ($items->{$itemid}{'status'} != ITEM_STATUS_ACTIVE);

		$items->{$itemid}{'key_'} =~ /,"(\S+)","(\S+)"]/;

		$result->{'rdds43_servers'} = $1;
		$result->{'rdds80_servers'} = $2;

		last;
	}

	return $result;
}

################################################################################
# manage NS + IP server pairs
################################################################################

sub list_nsservers($;$)
{
	my $server_key = shift;
	my $rsmhost    = shift; # optional

	# all fields in a CSV must be double-quoted, even if empty
	my $csv = Text::CSV_XS->new({binary => 1, auto_diag => 1, always_quote => 1, eol => "\n"});

	my @rsmhosts = ($rsmhost // get_tld_list());

	foreach my $rsmhost (sort(@rsmhosts))
	{
		my $nsservers = get_nsservers_list($server_key, $rsmhost);
		my @ns_types = keys(%{$nsservers});

		foreach my $type (sort(@ns_types))
		{
			my @ns_names = keys(%{$nsservers->{$type}});

			foreach my $ns_name (sort(@ns_names))
			{
				foreach my $ip (@{$nsservers->{$type}{$ns_name}})
				{
					$csv->print(*STDOUT, [$rsmhost, $type, $ns_name // "", $ip // ""]);
				}
			}
		}
	}
}

sub update_nsservers($$$)
{
	my $server_key     = shift;
	my $TLD            = shift;
	my $new_ns_servers = shift;

	# allow disabling all the NSs
	#return unless defined $new_ns_servers;

	my $old_ns_servers = get_nsservers_list($server_key, $TLD);

	# allow adding NSs on an empty set
	#return unless defined $old_ns_servers;

	my @to_be_added = ();
	my @to_be_removed = ();

	foreach my $new_nsname (keys %{$new_ns_servers})
	{
		my $new_ns = $new_ns_servers->{$new_nsname};

		foreach my $proto (keys %{$new_ns})
		{
			my $new_ips = $new_ns->{$proto};
			foreach my $new_ip (@{$new_ips})
			{
				my $need_to_add = true;

				if (defined($old_ns_servers->{$proto}) and defined($old_ns_servers->{$proto}{$new_nsname}))
				{
					foreach my $old_ip (@{$old_ns_servers->{$proto}{$new_nsname}})
					{
						$need_to_add = false if $old_ip eq $new_ip;
					}
				}

				if ($need_to_add == true)
				{
					my $ns_ip;
					$ns_ip->{$new_ip}{'ns'} = $new_nsname;
					$ns_ip->{$new_ip}{'proto'} = $proto;
					push(@to_be_added, $ns_ip);

					print("add\t: $new_nsname ($new_ip)\n");
				}
			}

		}
	}

	foreach my $proto (keys %{$old_ns_servers})
	{
		my $old_ns = $old_ns_servers->{$proto};
		foreach my $old_nsname (keys %{$old_ns})
		{
			foreach my $old_ip (@{$old_ns->{$old_nsname}})
			{
				my $need_to_remove = false;

				if (defined($new_ns_servers->{$old_nsname}{$proto}))
				{
					$need_to_remove = true;

					foreach my $new_ip (@{$new_ns_servers->{$old_nsname}{$proto}})
					{
						$need_to_remove = false if $new_ip eq $old_ip;
					}
				}
				else
				{
					$need_to_remove = true;
				}

				if ($need_to_remove == true)
				{
					my $ns_ip;

					$ns_ip->{$old_ip} = $old_nsname;

					push(@to_be_removed, $ns_ip);

					print("disable\t: $old_nsname ($old_ip)\n");
				}
			}
		}
	}

	add_new_ns($TLD, \@to_be_added) if scalar(@to_be_added);
	disable_old_ns($TLD, \@to_be_removed) if scalar(@to_be_removed);
}

sub get_nsservers_list($$)
{
	my $server_key = shift;
	my $TLD        = shift;

	my $result;

	my $templateid = get_template('Template ' . $TLD, false, false);

	pfail("TLD \"$TLD\" does not exist on \"$server_key\"") unless ($templateid->{'templateid'});

	$templateid = $templateid->{'templateid'};

	my $items = get_items_like($templateid, 'rsm.dns.tcp.rtt', true);

	foreach my $itemid (keys %{$items})
	{
		next if $items->{$itemid}{'status'} == ITEM_STATUS_DISABLED;

		my $name = $items->{$itemid}{'key_'};
		my $ip = $items->{$itemid}{'key_'};

		$ip =~ s/.+\,.+\,(.+)\]$/$1/;
		$name =~ s/.+\,(.+)\,.+\]$/$1/;

		if ($ip=~/\d*\.\d*\.\d*\.\d+/)
		{
			push(@{$result->{'v4'}{$name}}, $ip);
		}
		else
		{
			push(@{$result->{'v6'}{$name}}, $ip);
		}
	}

	return $result;
}

sub add_new_ns($)
{
	my $TLD        = shift;
	my $ns_servers = shift;

	my $main_templateid = get_template('Template ' . $TLD, false, false);

	return unless defined $main_templateid->{'templateid'};

	$main_templateid = $main_templateid->{'templateid'};

	my $main_hostid = get_host($TLD, false);

	return unless defined $main_hostid->{'hostid'};

	$main_hostid = $main_hostid->{'hostid'};

	my $macro_value = get_host_macro($main_templateid, '{$RSM.TLD.DNSSEC.ENABLED}');

	setopt('dnssec', 1) if (defined($macro_value) and $macro_value->{'value'} eq true);

	$macro_value = get_host_macro($main_templateid, '{$RSM.TLD.EPP.ENABLED}');

	setopt('epp-servers', 1) if (defined($macro_value) and $macro_value->{'value'} eq true);

	foreach my $ns_ip (@$ns_servers)
	{
		foreach my $ip (keys %{$ns_ip})
		{
			my $proto = $ns_ip->{$ip}{'proto'};
			my $ns = $ns_ip->{$ip}{'ns'};

			$proto=~s/v(\d)/$1/;

			create_item_dns_rtt($ns, $ip, $main_templateid, 'tcp', $proto);
			create_item_dns_rtt($ns, $ip, $main_templateid, 'udp', $proto);

			create_all_slv_ns_items($ns, $ip, $main_hostid, $TLD);
		}
	}
}

sub disable_old_ns($)
{
	my $TLD        = shift;
	my $ns_servers = shift;

	my @itemids;

	my $main_templateid = get_template('Template ' . $TLD, false, false);

	return unless defined $main_templateid->{'templateid'};

	$main_templateid = $main_templateid->{'templateid'};

	my $main_hostid = get_host($TLD, false);

	return unless defined $main_hostid->{'hostid'};

	$main_hostid = $main_hostid->{'hostid'};

	foreach my $ns (@$ns_servers)
	{
		foreach my $ip (keys %{$ns})
		{
			my $ns_name = $ns->{$ip};
			my $item_key = ',' . $ns_name . ',' . $ip . ']';

			my $items = get_items_like($main_templateid, $item_key, true);

			my @tmp_items = keys %{$items};

			push(@itemids, @tmp_items);

			$item_key = '[' . $ns_name . ',' . $ip . ']';

			$items = get_items_like($main_hostid, $item_key, false);

			@tmp_items = keys %{$items};

			push(@itemids, @tmp_items);
		}
	}

	if (@itemids)
	{
		my $triggers = get_triggers_by_items(\@itemids);

		my @triggerids = keys %{$triggers};

		#disable_triggers(\@triggerids) if scalar @triggerids;

		disable_items(\@itemids);
	}
}

################################################################################
# delete or disable RSMHOST or its objects
################################################################################

sub manage_tld_objects($$$$$$$)
{
	my $action = shift;
	my $tld    = shift;
	my $dns    = shift;
	my $dnssec = shift;
	my $epp    = shift;
	my $rdds   = shift;
	my $rdap   = shift;

	my $types = {
		'dns'    => $dns,
		'dnssec' => $dnssec,
		'epp'    => $epp,
		'rdds'   => $rdds
	};

	my $main_templateid;

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
	my $tld_template = get_template('Template ' . $tld, false, true);

	if (scalar(%{$tld_template}))
	{
		$main_templateid = $tld_template->{'templateid'};
		print("$main_templateid\n");
	}
	else
	{
		pfail("cannot find template \"Template .$tld\"");
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

		push(@tmp_hostids, {'hostid' => $main_hostid});

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
				pfail("an error occurred while disabling hosts!");
			}

			exit;
		}

		if ($action eq 'delete')
		{
			remove_hosts(\@hostids_arr);
			remove_hosts([$main_hostid]);
			remove_templates([$main_templateid]);

			my $hostgroupid = get_host_group('TLD ' . $tld, false, false);
			$hostgroupid = $hostgroupid->{'groupid'};
			remove_hostgroups([$hostgroupid]);
			return;
		}
	}

	foreach my $type (keys %{$types})
	{
		next if $types->{$type} eq false;

		if ($type eq 'dnssec')
		{
			really(create_macro('{$RSM.TLD.DNSSEC.ENABLED}', 0, $main_templateid, true)) if ($types->{$type} eq true);
			next;
		}

		my @itemids;

		my $template_items = get_items_like($main_templateid, $type, true);
		my $host_items = get_items_like($main_hostid, $type, false);

		if ($type eq 'rdds')
		{
			my $service_enabled_itemkey = "$type.enabled";

			my @service_enabled_itemid = grep { $template_items->{$_}{'key_'} eq $service_enabled_itemkey } keys(%{$template_items});
			if (!@service_enabled_itemid)
			{
				pfail("failed to find $service_enabled_itemkey item");
			}

			delete($template_items->{$service_enabled_itemid[0]});
		}

		if (scalar(keys(%{$template_items})))
		{
			push(@itemids, keys(%{$template_items}));
		}
		elsif ($type ne 'rdap') # RDAP doesn't have items in "Template $tld"
		{
			print("Could not find $type related items on the template level\n");
		}

		if (scalar(keys(%{$host_items})))
		{
			push(@itemids, keys(%{$host_items}));
		}
		else
		{
			print("Could not find $type related items on host level\n");
		}

		if (scalar(@itemids))
		{
			my $macro = $type eq 'rdap' ? '{$RDAP.TLD.ENABLED}' : '{$RSM.TLD.' . uc($type) . '.ENABLED}';

			create_macro($macro, 0, $main_templateid, true);

			if ($action eq 'disable')
			{
				disable_items(\@itemids);
			}
			else # $action is 'delete'
			{
				remove_items(\@itemids);
				# remove_applications_by_items(\@itemids);
			}
		}

		if ($action eq 'disable' && $type eq 'rdap')
		{
			set_linked_items_enabled('rdap[', $tld, 0);
		}
	}
}

################################################################################
# add or update RSMHOST
################################################################################

sub add_new_tld()
{
	$ns_servers = get_ns_servers(getopt('tld'));
	pfail("Could not retrieve NS servers for '" . getopt('tld') . "' TLD") unless (scalar(keys %{$ns_servers}));

	my $root_servers_macros = update_root_servers(getopt('root-servers'));
	print("Could not retrieve list of root servers or create global macros\n") unless (defined($root_servers_macros));

	my $main_templateid = create_main_template(getopt('tld'), $ns_servers);
	pfail("Main templateid is not defined") unless defined $main_templateid;

	my $rsmhost_groupid = really(create_group('TLD ' . getopt('tld')));

	create_rsmhost();

	my $proxy_mon_templateid = create_probe_health_tmpl();

	create_tld_hosts_on_probes($root_servers_macros, $proxy_mon_templateid, $rsmhost_groupid, $main_templateid);
}

sub get_ns_servers($)
{
	my $tld = shift;

	my $ns_servers;

	# just in case, the input should have been validated by now
	unless (opt('ns-servers-v4') or opt('ns-servers-v6'))
	{
		pfail("option --ns-servers-v4 and/or --ns-servers-v6 required for this invocation");
	}

	if (getopt('ns-servers-v4') and (getopt('ipv4') == 1 or getopt('update-nsservers')))
	{
		my @nsservers = split(/\s/, getopt('ns-servers-v4'));
		foreach my $ns (@nsservers)
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

	if (getopt('ns-servers-v6') and (getopt('ipv6') or getopt('update-nsservers')))
	{
		my @nsservers = split(/\s/, getopt('ns-servers-v6'));
		foreach my $ns (@nsservers)
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

sub create_main_template($$)
{
	my $tld        = shift;
	my $ns_servers = shift;

	my $templateid = really(create_template('Template ' . $tld));

	my $delay = 300;
	my $appid = get_application_id('Configuration', $templateid);

	foreach my $m ('RSM.IP4.ENABLED', 'RSM.IP6.ENABLED')
	{
		really(create_item({
			'name'         => 'Value of $1 variable',
			'key_'         => 'probe.configvalue[' . $m . ']',
			'status'       => ITEM_STATUS_ACTIVE,
			'hostid'       => $templateid,
			'applications' => [$appid],
			'params'       => '{$' . $m . '}',
			'delay'        => $delay,
			'type'         => ITEM_TYPE_CALCULATED,
			'value_type'   => ITEM_VALUE_TYPE_UINT64
		}));
	}

	foreach my $ns_name (sort keys %{$ns_servers})
	{
		print $ns_name . "\n";

		my @ipv4 = defined($ns_servers->{$ns_name}{'v4'}) ? @{$ns_servers->{$ns_name}{'v4'}} : undef;
		my @ipv6 = defined($ns_servers->{$ns_name}{'v6'}) ? @{$ns_servers->{$ns_name}{'v6'}} : undef;

		for (my $i_ipv4 = 0; $i_ipv4 <= $#ipv4; $i_ipv4++)
		{
			next unless defined $ipv4[$i_ipv4];
			print("	--v4     $ipv4[$i_ipv4]\n");

			create_item_dns_rtt($ns_name, $ipv4[$i_ipv4], $templateid, "tcp", '4');
			create_item_dns_rtt($ns_name, $ipv4[$i_ipv4], $templateid, "udp", '4');
			if (opt('epp-servers'))
			{
				create_item_dns_udp_upd($ns_name, $ipv4[$i_ipv4], $templateid);
			}
		}

		for (my $i_ipv6 = 0; $i_ipv6 <= $#ipv6; $i_ipv6++)
		{
			next unless defined $ipv6[$i_ipv6];
			print("	--v6     $ipv6[$i_ipv6]\n");

			create_item_dns_rtt($ns_name, $ipv6[$i_ipv6], $templateid, "tcp", '6');
			create_item_dns_rtt($ns_name, $ipv6[$i_ipv6], $templateid, "udp", '6');
			if (opt('epp-servers'))
			{
				create_item_dns_udp_upd($ns_name, $ipv6[$i_ipv6], $templateid);
			}
		}
	}

	create_items_dns($templateid);
	create_items_rdds($templateid);
	create_items_epp($templateid) if (opt('epp-servers'));

	my $rdds43_test_domain;
	if (opt('rdds-test-prefix'))
	{
		if (getopt('tld') eq ".")
		{
			$rdds43_test_domain = getopt('rdds-test-prefix');
		}
		else
		{
			$rdds43_test_domain = sprintf('%s.%s', getopt('rdds-test-prefix'), getopt('tld'));
		}
	}
	elsif (opt('rdds43-test-domain'))
	{
		$rdds43_test_domain = getopt('rdds43-test-domain');
	}

	# NB! Macros {$RSM.TLD.RDDS.ENABLED} and {$RDAP.TLD.ENABLED} reflect different information depending
	# in Standalone RDAP:
	#
	# if RDAP is standalone:
	#   {$RSM.TLD.RDDS.ENABLED} tells if RDDS SERVICE is enabled
	#   {$RDAP.TLD.ENABLED}     tells if RDAP SERVICE is enabled
	#
	# if RDAP is NOT standalone:
	#   {$RSM.TLD.RDDS.ENABLED} tells if RDDS43/RDDS80 subservices of RDDS SERVICE are enabled
	#   {$RDAP.TLD.ENABLED}     tells if RDAP subservice of RDDS SERVICE is enabled

	really(create_macro('{$RSM.TLD}', $tld, $templateid));
	really(create_macro('{$RSM.DNS.TESTPREFIX}', getopt('dns-test-prefix'), $templateid, 1));
	really(create_macro('{$RSM.RDDS43.TEST.DOMAIN}', $rdds43_test_domain, $templateid, 1)) if (defined($rdds43_test_domain));
	really(create_macro('{$RSM.RDDS.NS.STRING}', opt('rdds-ns-string') ? getopt('rdds-ns-string') : CFG_DEFAULT_RDDS_NS_STRING, $templateid, 1));
	really(create_macro('{$RSM.TLD.DNSSEC.ENABLED}', getopt('dnssec'), $templateid, 1));
	really(create_macro('{$RSM.TLD.RDDS.ENABLED}', opt('rdds43-servers') ? 1 : 0, $templateid, 1));
	really(create_macro('{$RSM.TLD.EPP.ENABLED}', opt('epp-servers') ? 1 : 0, $templateid, 1));

	if (opt('rdap-base-url') && opt('rdap-test-domain'))
	{
		really(create_macro('{$RDAP.BASE.URL}', getopt('rdap-base-url'), $templateid, 1));
		really(create_macro('{$RDAP.TEST.DOMAIN}', getopt('rdap-test-domain'), $templateid, 1));
		really(create_macro('{$RDAP.TLD.ENABLED}', 1, $templateid, 1));
	}
	else
	{
		really(create_macro('{$RDAP.TLD.ENABLED}', 0, $templateid, 1));
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
			really(create_macro('{$RSM.EPP.COMMANDS}', getopt('epp-commands'), $templateid, 1));
		}
		else
		{
			really(create_macro('{$RSM.EPP.COMMANDS}', '/opt/test-sla/epp-commands/' . $tld, $templateid));
		}
		really(create_macro('{$RSM.EPP.USER}', getopt('epp-user'), $templateid, 1));
		really(create_macro('{$RSM.EPP.CERT}', encode_base64($buf, ''),  $templateid, 1));
		really(create_macro('{$RSM.EPP.SERVERID}', getopt('epp-serverid'), $templateid, 1));
		really(create_macro('{$RSM.EPP.TESTPREFIX}', getopt('epp-test-prefix'), $templateid, 1));
		really(create_macro('{$RSM.EPP.SERVERCERTMD5}', get_md5(getopt('epp-servercert')), $templateid, 1));

		my $passphrase = get_sensdata("Enter EPP secret key passphrase: ");
		my $passwd = get_sensdata("Enter EPP password: ");
		really(create_macro('{$RSM.EPP.PASSWD}', get_encrypted_passwd($keysalt, $passphrase, $passwd), $templateid, 1));
		$passwd = undef;
		really(create_macro('{$RSM.EPP.PRIVKEY}', get_encrypted_privkey($keysalt, $passphrase, getopt('epp-privkey')), $templateid, 1));
		$passphrase = undef;

		print("EPP data saved successfully.\n");
	}

	return $templateid;
}

sub create_item_dns_rtt($$$$$)
{
	my $ns_name       = shift;
	my $ip            = shift;
	my $templateid    = shift;
	my $proto         = shift;
	my $ipv           = shift;

	pfail("undefined template ID passed to create_item_dns_rtt()") unless ($templateid);
	pfail("no protocol parameter specified to create_item_dns_rtt()") unless ($proto);

	my $proto_lc = lc($proto);
	my $proto_uc = uc($proto);

	my $item_key = 'rsm.dns.' . $proto_lc . '.rtt[{$RSM.TLD},' . $ns_name . ',' . $ip . ']';

	really(create_item({
		'name'         => 'DNS RTT of $2 ($3) (' . $proto_uc . ')',
		'key_'         => $item_key,
		'status'       => ITEM_STATUS_ACTIVE,
		'hostid'       => $templateid,
		'applications' => [get_application_id('DNS RTT (' . $proto_uc . ')', $templateid)],
		'type'         => ITEM_TYPE_TRAPPER,
		'value_type'   => ITEM_VALUE_TYPE_FLOAT,
		'valuemapid'   => RSM_VALUE_MAPPINGS->{'rsm_dns_rtt'}
	}));
}

sub create_slv_item($$$$$;$)
{
	my $name           = shift;
	my $key            = shift;
	my $hostid         = shift;
	my $value_type     = shift;
	my $applicationids = shift;
	my $item_status    = shift;

	$item_status = ITEM_STATUS_ACTIVE unless (defined($item_status));

	if ($value_type == VALUE_TYPE_AVAIL)
	{
		return really(create_item({
			'name'         => $name,
			'key_'         => $key,
			'status'       => $item_status,
			'hostid'       => $hostid,
			'type'         => ITEM_TYPE_TRAPPER,
			'value_type'   => ITEM_VALUE_TYPE_UINT64,
			'applications' => $applicationids,
			'valuemapid'   => RSM_VALUE_MAPPINGS->{'rsm_avail'}
		}));
	}

	if ($value_type == VALUE_TYPE_NUM)
	{
		return really(create_item({
			'name'         => $name,
			'key_'         => $key,
			'status'       => $item_status,
			'hostid'       => $hostid,
			'type'         => ITEM_TYPE_TRAPPER,
			'value_type'   => ITEM_VALUE_TYPE_UINT64,
			'applications' => $applicationids
		}));
	}

	if ($value_type == VALUE_TYPE_PERC)
	{
		return really(create_item({
			'name'         => $name,
			'key_'         => $key,
			'status'       => $item_status,
			'hostid'       => $hostid,
			'type'         => ITEM_TYPE_TRAPPER,
			'value_type'   => ITEM_VALUE_TYPE_FLOAT,
			'applications' => $applicationids,
			'units'        => '%'
		}));
	}

	pfail("Unknown value type $value_type.");
}

sub create_item_dns_udp_upd($$$)
{
	my $ns_name       = shift;
	my $ip            = shift;
	my $templateid    = shift;

	my $proto_uc = 'UDP';

	return really(create_item({
		'name'         => 'DNS update time of $2 ($3)',
		'key_'         => 'rsm.dns.udp.upd[{$RSM.TLD},' . $ns_name . ',' . $ip . ']',
		'status'       => (opt('epp-servers') ? ITEM_STATUS_ACTIVE : ITEM_STATUS_DISABLED),
		'hostid'       => $templateid,
		'applications' => [get_application_id('DNS RTT (' . $proto_uc . ')', $templateid)],
		'type'         => ITEM_TYPE_TRAPPER,
		'value_type'   => ITEM_VALUE_TYPE_FLOAT,
		'valuemapid'   => RSM_VALUE_MAPPINGS->{'rsm_dns_rtt'}
	}));
}

sub create_items_dns($)
{
	my $templateid    = shift;

	my $proto = 'tcp';
	my $proto_uc = uc($proto);
	my $item_key = 'rsm.dns.' . $proto . '[{$RSM.TLD}]';

	really(create_item({
		'name'         => 'Number of working DNS Name Servers of $1 (' . $proto_uc . ')',
		'key_'         => $item_key,
		'status'       => ITEM_STATUS_ACTIVE,
		'hostid'       => $templateid,
		'applications' => [get_application_id('DNS (' . $proto_uc . ')', $templateid)],
		'type'         => ITEM_TYPE_SIMPLE,
		'value_type'   => ITEM_VALUE_TYPE_UINT64,
		'delay'        => $cfg_global_macros->{'{$RSM.DNS.TCP.DELAY}'}
	}));

	$proto = 'udp';
	$proto_uc = uc($proto);
	$item_key = 'rsm.dns.' . $proto . '[{$RSM.TLD}]';

	really(create_item({
		'name'         => 'Number of working DNS Name Servers of $1 (' . $proto_uc . ')',
		'key_'         => $item_key,
		'status'       => ITEM_STATUS_ACTIVE,
		'hostid'       => $templateid,
		'applications' => [get_application_id('DNS (' . $proto_uc . ')', $templateid)],
		'type'         => ITEM_TYPE_SIMPLE,
		'value_type'   => ITEM_VALUE_TYPE_UINT64,
		'delay'        => $cfg_global_macros->{'{$RSM.DNS.UDP.DELAY}'}
	}));

	# this item is added in any case
	really(create_item({
		'name'       => 'DNSSEC enabled/disabled',
		'key_'       => 'dnssec.enabled',
		'status'     => ITEM_STATUS_ACTIVE,
		'hostid'     => $templateid,
		'params'     => '{$RSM.TLD.DNSSEC.ENABLED}',
		'delay'      => 60,
		'type'       => ITEM_TYPE_CALCULATED,
		'value_type' => ITEM_VALUE_TYPE_UINT64
	}));
}

sub create_items_rdds($)
{
	my $templateid    = shift;

	if (opt('rdds43-servers'))
	{
		my $applicationid_43 = get_application_id('RDDS43', $templateid);
		my $applicationid_80 = get_application_id('RDDS80', $templateid);

		my $item_key = 'rsm.rdds.43.ip[{$RSM.TLD}]';

		really(create_item({
			'name'         => 'RDDS43 IP of $1',
			'key_'         => $item_key,
			'status'       => ITEM_STATUS_ACTIVE,
			'hostid'       => $templateid,
			'applications' => [$applicationid_43],
			'type'         => ITEM_TYPE_TRAPPER,
			'value_type'   => ITEM_VALUE_TYPE_STR
		}));

		$item_key = 'rsm.rdds.43.rtt[{$RSM.TLD}]';

		really(create_item({
			'name'         => 'RDDS43 RTT of $1',
			'key_'         => $item_key,
			'status'       => ITEM_STATUS_ACTIVE,
			'hostid'       => $templateid,
			'applications' => [$applicationid_43],
			'type'         => ITEM_TYPE_TRAPPER,
			'value_type'   => ITEM_VALUE_TYPE_FLOAT,
			'valuemapid'   => RSM_VALUE_MAPPINGS->{'rsm_rdds_rtt'}
		}));

		if (opt('epp-servers'))
		{
			$item_key = 'rsm.rdds.43.upd[{$RSM.TLD}]';

			really(create_item({
				'name'         => 'RDDS43 update time of $1',
				'key_'         => $item_key,
				'status'       => ITEM_STATUS_ACTIVE,
				'hostid'       => $templateid,
				'applications' => [$applicationid_43],
				'type'         => ITEM_TYPE_TRAPPER,
				'value_type'   => ITEM_VALUE_TYPE_FLOAT,
				'valuemapid'   => RSM_VALUE_MAPPINGS->{'rsm_rdds_rtt'}
			}));
		}

		really(create_item({
			'name'         => 'RDDS43 target',
			'key_'         => 'rsm.rdds.43.target',
			'status'       => ITEM_STATUS_ACTIVE,
			'hostid'       => $templateid,
			'applications' => [$applicationid_43],
			'type'         => ITEM_TYPE_TRAPPER,
			'value_type'   => ITEM_VALUE_TYPE_STR
		}));

		really(create_item({
			'name'         => 'RDDS43 tested name',
			'key_'         => 'rsm.rdds.43.testedname',
			'status'       => ITEM_STATUS_ACTIVE,
			'hostid'       => $templateid,
			'applications' => [$applicationid_43],
			'type'         => ITEM_TYPE_TRAPPER,
			'value_type'   => ITEM_VALUE_TYPE_STR
		}));

		$item_key = 'rsm.rdds.80.ip[{$RSM.TLD}]';

		really(create_item({
			'name'         => 'RDDS80 IP of $1',
			'key_'         => $item_key,
			'status'       => ITEM_STATUS_ACTIVE,
			'hostid'       => $templateid,
			'applications' => [$applicationid_80],
			'type'         => ITEM_TYPE_TRAPPER,
			'value_type'   => ITEM_VALUE_TYPE_STR
		}));

		$item_key = 'rsm.rdds.80.rtt[{$RSM.TLD}]';

		really(create_item({
			'name'         => 'RDDS80 RTT of $1',
			'key_'         => $item_key,
			'status'       => ITEM_STATUS_ACTIVE,
			'hostid'       => $templateid,
			'applications' => [$applicationid_80],
			'type'         => ITEM_TYPE_TRAPPER,
			'value_type'   => ITEM_VALUE_TYPE_FLOAT,
			'valuemapid'   => RSM_VALUE_MAPPINGS->{'rsm_rdds_rtt'}
		}));

		really(create_item({
			'name'         => 'RDDS80 target',
			'key_'         => 'rsm.rdds.80.target',
			'status'       => ITEM_STATUS_ACTIVE,
			'hostid'       => $templateid,
			'applications' => [$applicationid_80],
			'type'         => ITEM_TYPE_TRAPPER,
			'value_type'   => ITEM_VALUE_TYPE_STR
		}));

		$item_key = 'rsm.rdds[{$RSM.TLD},"' . getopt('rdds43-servers') . '","' . getopt('rdds80-servers') . '"]';

		# disable old items to keep the history, if value of rdds43-servers and/or rdds80-servers has changed
		my @old_rdds_availability_items = keys(%{get_items_like($templateid, 'rsm.rdds[', true)});
		disable_items(\@old_rdds_availability_items);

		# create new item (or update/enable existing item)
		really(create_item({
			'name'         => 'RDDS availability',
			'key_'         => $item_key,
			'status'       => ITEM_STATUS_ACTIVE,
			'hostid'       => $templateid,
			'applications' => [get_application_id('RDDS', $templateid)],
			'type'         => ITEM_TYPE_SIMPLE,
			'value_type'   => ITEM_VALUE_TYPE_UINT64,
			'delay'        => $cfg_global_macros->{'{$RSM.RDDS.DELAY}'},
			'valuemapid'   => RSM_VALUE_MAPPINGS->{'rsm_rdds_result'}
		}));
	}

	# this item is added in any case
	really(create_item({
		'name'       => 'RDDS enabled/disabled',
		'key_'       => 'rdds.enabled',
		'status'     => ITEM_STATUS_ACTIVE,
		'hostid'     => $templateid,
		'params'     => '{$RSM.TLD.RDDS.ENABLED}',
		'delay'      => 60,
		'type'       => ITEM_TYPE_CALCULATED,
		'value_type' => ITEM_VALUE_TYPE_UINT64
	}));
}

sub create_items_epp($)
{
	my $templateid    = shift;

	my $applicationid = get_application_id('EPP', $templateid);

	really(create_item({
		'name'         => 'EPP service availability at $1 ($2)',
		'key_'         => 'rsm.epp[{$RSM.TLD},"' . getopt('epp-servers') . '"]',
		'status'       => ITEM_STATUS_ACTIVE,
		'hostid'       => $templateid,
		'applications' => [$applicationid],
		'type'         => ITEM_TYPE_SIMPLE,
		'value_type'   => ITEM_VALUE_TYPE_UINT64,
		'delay'        => $cfg_global_macros->{'{$RSM.EPP.DELAY}'},
		'valuemapid'   => RSM_VALUE_MAPPINGS->{'rsm_epp_result'}
	}));

	really(create_item({
		'name'         => 'EPP IP of $1',
		'key_'         => 'rsm.epp.ip[{$RSM.TLD}]',
		'status'       => ITEM_STATUS_ACTIVE,
		'hostid'       => $templateid,
		'applications' => [$applicationid],
		'type'         => ITEM_TYPE_TRAPPER,
		'value_type'   => ITEM_VALUE_TYPE_STR
	}));

	really(create_item({
		'name'         => 'EPP $2 command RTT of $1',
		'key_'         => 'rsm.epp.rtt[{$RSM.TLD},login]',
		'status'       => ITEM_STATUS_ACTIVE,
		'hostid'       => $templateid,
		'applications' => [$applicationid],
		'type'         => ITEM_TYPE_TRAPPER,
		'value_type'   => ITEM_VALUE_TYPE_FLOAT,
		'valuemapid'   => RSM_VALUE_MAPPINGS->{'rsm_epp_rtt'}
	}));

	really(create_item({
		'name'         => 'EPP $2 command RTT of $1',
		'key_'         => 'rsm.epp.rtt[{$RSM.TLD},update]',
		'status'       => ITEM_STATUS_ACTIVE,
		'hostid'       => $templateid,
		'applications' => [$applicationid],
		'type'         => ITEM_TYPE_TRAPPER,
		'value_type'   => ITEM_VALUE_TYPE_FLOAT,
		'valuemapid'   => RSM_VALUE_MAPPINGS->{'rsm_epp_rtt'}
	}));

	really(create_item({
		'name'         => 'EPP $2 command RTT of $1',
		'key_'         => 'rsm.epp.rtt[{$RSM.TLD},info]',
		'status'       => ITEM_STATUS_ACTIVE,
		'hostid'       => $templateid,
		'applications' => [$applicationid],
		'type'         => ITEM_TYPE_TRAPPER,
		'value_type'   => ITEM_VALUE_TYPE_FLOAT,
		'valuemapid'   => RSM_VALUE_MAPPINGS->{'rsm_epp_rtt'}
	}));
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

sub create_all_slv_ns_items($$$$)
{
	my $ns_name   = shift;
	my $ip        = shift;
	my $hostid    = shift;
	my $host_name = shift;

	create_slv_item("DNS NS $ns_name ($ip) availability", "rsm.slv.dns.ns.avail[$ns_name,$ip]",
			$hostid, VALUE_TYPE_AVAIL, [get_application_id(APP_SLV_PARTTEST, $hostid)]);

	create_slv_item("DNS minutes of $ns_name ($ip) downtime", "rsm.slv.dns.ns.downtime[$ns_name,$ip]",
			$hostid, VALUE_TYPE_NUM, [get_application_id(APP_SLV_CURMON, $hostid)]);

	create_dependent_trigger_chain($host_name, "$ns_name,$ip", \&create_dns_ns_downtime_trigger, $trigger_thresholds);

	#create_slv_item('% of successful monthly DNS resolution RTT (UDP): $1 ($2)', 'rsm.slv.dns.ns.rtt.udp.month[' . $ns_name . ',' . $ip . ']', $hostid, VALUE_TYPE_PERC, [get_application_id(APP_SLV_MONTHLY, $hostid)]);
	#create_slv_item('% of successful monthly DNS resolution RTT (TCP): $1 ($2)', 'rsm.slv.dns.ns.rtt.tcp.month[' . $ns_name . ',' . $ip . ']', $hostid, VALUE_TYPE_PERC, [get_application_id(APP_SLV_MONTHLY, $hostid)]);
	#create_slv_item('% of successful monthly DNS update time: $1 ($2)', 'rsm.slv.dns.ns.upd.month[' . $ns_name . ',' . $ip . ']', $hostid, VALUE_TYPE_PERC, [get_application_id(APP_SLV_MONTHLY, $hostid)]) if (opt('epp-servers'));
	#create_slv_item('DNS NS availability: $1 ($2)', 'rsm.slv.dns.ns.avail[' . $ns_name . ',' . $ip . ']', $hostid, VALUE_TYPE_AVAIL, [get_application_id(APP_SLV_PARTTEST, $hostid)]);
	#create_slv_item('DNS NS minutes of downtime: $1 ($2)', 'rsm.slv.dns.ns.downtime[' . $ns_name . ',' . $ip . ']', $hostid, VALUE_TYPE_NUM, [get_application_id(APP_SLV_CURMON, $hostid)]);
	#create_slv_item('DNS NS probes that returned results: $1 ($2)', 'rsm.slv.dns.ns.results[' . $ns_name . ',' . $ip . ']', $hostid, VALUE_TYPE_NUM, [get_application_id(APP_SLV_CURMON, $hostid)]);
	#create_slv_item('DNS NS probes that returned positive results: $1 ($2)', 'rsm.slv.dns.ns.positive[' . $ns_name . ',' . $ip . ']', $hostid, VALUE_TYPE_NUM, [get_application_id(APP_SLV_CURMON, $hostid)]);
	#create_slv_item('DNS NS positive results by SLA: $1 ($2)', 'rsm.slv.dns.ns.sla[' . $ns_name . ',' . $ip . ']', $hostid, VALUE_TYPE_NUM, [get_application_id(APP_SLV_CURMON, $hostid)]);
	#create_slv_item('% of monthly DNS NS availability: $1 ($2)', 'rsm.slv.dns.ns.month[' . $ns_name . ',' . $ip . ']', $hostid, VALUE_TYPE_PERC, [get_application_id(APP_SLV_MONTHLY, $hostid)]);
}

sub create_slv_ns_items($$$)
{
	my $ns_servers = shift;
	my $hostid     = shift;
	my $host_name  = shift;

	foreach my $ns_name (sort keys %{$ns_servers})
	{
		my @ipv4 = defined($ns_servers->{$ns_name}{'v4'}) ? @{$ns_servers->{$ns_name}{'v4'}} : undef;
		my @ipv6 = defined($ns_servers->{$ns_name}{'v6'}) ? @{$ns_servers->{$ns_name}{'v6'}} : undef;

		for (my $i_ipv4 = 0; $i_ipv4 <= $#ipv4; $i_ipv4++)
		{
			next unless defined $ipv4[$i_ipv4];

			create_all_slv_ns_items($ns_name, $ipv4[$i_ipv4], $hostid, $host_name);
		}

		for (my $i_ipv6 = 0; $i_ipv6 <= $#ipv6; $i_ipv6++)
		{
			next unless defined $ipv6[$i_ipv6];

			create_all_slv_ns_items($ns_name, $ipv6[$i_ipv6], $hostid, $host_name);
		}
	}
}

sub create_rdds_or_rdap_slv_items($$$;$)
{
	my $hostid       = shift;
	my $host_name    = shift;
	my $service      = shift;
	my $item_status  = shift;

	$item_status = ITEM_STATUS_ACTIVE unless (defined($item_status));

	pfail("Internal error, invalid service '$service', expected RDDS or RDAP") unless ($service eq "RDDS" || $service eq "RDAP");

	my $service_lc = lc($service);

	create_slv_item("$service availability"         , "rsm.slv.$service_lc.avail"   , $hostid, VALUE_TYPE_AVAIL, [get_application_id(APP_SLV_PARTTEST, $hostid)]);
	create_slv_item("$service minutes of downtime"  , "rsm.slv.$service_lc.downtime", $hostid, VALUE_TYPE_NUM  , [get_application_id(APP_SLV_CURMON, $hostid)]);
	create_slv_item("$service weekly unavailability", "rsm.slv.$service_lc.rollweek", $hostid, VALUE_TYPE_PERC , [get_application_id(APP_SLV_ROLLWEEK, $hostid)]);

	create_avail_trigger($service, $host_name);
	create_dependent_trigger_chain($host_name, $service, \&create_downtime_trigger, $trigger_thresholds);
	create_dependent_trigger_chain($host_name, $service, \&create_rollweek_trigger, $trigger_thresholds);

	create_slv_item("Number of performed monthly $service queries", "rsm.slv.$service_lc.rtt.performed", $hostid, VALUE_TYPE_NUM , [get_application_id(APP_SLV_CURMON, $hostid)]);
	create_slv_item("Number of failed monthly $service queries"   , "rsm.slv.$service_lc.rtt.failed"   , $hostid, VALUE_TYPE_NUM , [get_application_id(APP_SLV_CURMON, $hostid)]);
	create_slv_item("Ratio of failed monthly $service queries"    , "rsm.slv.$service_lc.rtt.pfailed"  , $hostid, VALUE_TYPE_PERC, [get_application_id(APP_SLV_CURMON, $hostid)]);

	create_dependent_trigger_chain($host_name, $service, \&create_ratio_of_failed_tests_trigger, $trigger_thresholds);
}

sub __is_rdap_standalone()
{
	return  $cfg_global_macros->{'{$RSM.RDAP.STANDALONE}'} != 0 &&
			time() >= $cfg_global_macros->{'{$RSM.RDAP.STANDALONE}'};
}

sub create_slv_items($$$)
{
	my $ns_servers = shift;
	my $hostid     = shift;
	my $host_name  = shift;

	create_slv_ns_items($ns_servers, $hostid, $host_name);

	create_slv_item('DNS availability', 'rsm.slv.dns.avail', $hostid, VALUE_TYPE_AVAIL, [get_application_id(APP_SLV_PARTTEST, $hostid)]);
	create_slv_item('DNS minutes of downtime', 'rsm.slv.dns.downtime', $hostid, VALUE_TYPE_NUM, [get_application_id(APP_SLV_CURMON, $hostid)]);
	create_slv_item('DNS weekly unavailability', 'rsm.slv.dns.rollweek', $hostid, VALUE_TYPE_PERC, [get_application_id(APP_SLV_ROLLWEEK, $hostid)]);

	create_avail_trigger('DNS', $host_name);
	create_dns_downtime_trigger($host_name, 5);
	create_dependent_trigger_chain($host_name, 'DNS', \&create_rollweek_trigger, $trigger_thresholds);

	if (opt('dnssec'))
	{
		create_slv_item('DNSSEC availability', 'rsm.slv.dnssec.avail', $hostid, VALUE_TYPE_AVAIL, [get_application_id(APP_SLV_PARTTEST, $hostid)]);
		create_slv_item('DNSSEC weekly unavailability', 'rsm.slv.dnssec.rollweek', $hostid, VALUE_TYPE_PERC, [get_application_id(APP_SLV_ROLLWEEK, $hostid)]);

		create_avail_trigger('DNSSEC', $host_name);
		create_dependent_trigger_chain($host_name, 'DNSSEC', \&create_rollweek_trigger, $trigger_thresholds);
	}

	if (opt('epp-servers'))
	{
		create_slv_item('EPP availability', 'rsm.slv.epp.avail', $hostid, VALUE_TYPE_AVAIL, [get_application_id(APP_SLV_PARTTEST, $hostid)]);
		create_slv_item('EPP minutes of downtime', 'rsm.slv.epp.downtime', $hostid, VALUE_TYPE_NUM, [get_application_id(APP_SLV_CURMON, $hostid)]);
		create_slv_item('EPP weekly unavailability', 'rsm.slv.epp.rollweek', $hostid, VALUE_TYPE_PERC, [get_application_id(APP_SLV_ROLLWEEK, $hostid)]);

		create_avail_trigger('EPP', $host_name);
		#create_dependent_trigger_chain($host_name, 'EPP', \&create_downtime_trigger, $trigger_thresholds);
	}

	create_slv_item('Number of performed monthly DNS UDP tests', 'rsm.slv.dns.udp.rtt.performed', $hostid, VALUE_TYPE_NUM , [get_application_id(APP_SLV_CURMON, $hostid)]);
	create_slv_item('Number of failed monthly DNS UDP tests'   , 'rsm.slv.dns.udp.rtt.failed'   , $hostid, VALUE_TYPE_NUM , [get_application_id(APP_SLV_CURMON, $hostid)]);
	create_slv_item('Ratio of failed monthly DNS UDP tests'    , 'rsm.slv.dns.udp.rtt.pfailed'  , $hostid, VALUE_TYPE_PERC, [get_application_id(APP_SLV_CURMON, $hostid)]);
	create_slv_item('Number of performed monthly DNS TCP tests', 'rsm.slv.dns.tcp.rtt.performed', $hostid, VALUE_TYPE_NUM , [get_application_id(APP_SLV_CURMON, $hostid)]);
	create_slv_item('Number of failed monthly DNS TCP tests'   , 'rsm.slv.dns.tcp.rtt.failed'   , $hostid, VALUE_TYPE_NUM , [get_application_id(APP_SLV_CURMON, $hostid)]);
	create_slv_item('Ratio of failed monthly DNS TCP tests'    , 'rsm.slv.dns.tcp.rtt.pfailed'  , $hostid, VALUE_TYPE_PERC, [get_application_id(APP_SLV_CURMON, $hostid)]);

	create_dependent_trigger_chain($host_name, 'DNS UDP', \&create_ratio_of_failed_tests_trigger, $trigger_thresholds);
	create_dependent_trigger_chain($host_name, 'DNS TCP', \&create_ratio_of_failed_tests_trigger, $trigger_thresholds);

	if (opt('rdds43-servers') || opt('rdds80-servers') || opt('rdap-base-url'))
	{
		if (!__is_rdap_standalone())
		{
			# we haven't switched to RDAP yet, create RDDS items which may also include RDAP check results
			create_rdds_or_rdap_slv_items($hostid, $host_name, "RDDS");

			# create future RDAP items, it's ok for them to be active
			create_rdds_or_rdap_slv_items($hostid, $host_name, "RDAP") if (opt('rdap-base-url'));
		}
		else
		{
			# after the switch, RDDS and RDAP item sets are opt-in
			create_rdds_or_rdap_slv_items($hostid, $host_name, "RDAP") if (opt('rdap-base-url'));
			create_rdds_or_rdap_slv_items($hostid, $host_name, "RDDS") if (opt('rdds43-servers') || opt('rdds80-servers'));
		}
	}
}

sub create_rsmhost()
{
	my $tld_name = getopt('tld');
	my $tld_type = getopt('type');

	my $tld_hostid = really(create_host({
		'groups'     => [
			{'groupid' => TLDS_GROUPID},
			{'groupid' => TLD_TYPE_GROUPIDS->{$tld_type}}
		],
		'host'       => $tld_name,
		'status'     => HOST_STATUS_MONITORED,
		'interfaces' => [DEFAULT_MAIN_INTERFACE]
	}));

	create_slv_items($ns_servers, $tld_hostid, $tld_name);
}

sub create_avail_trigger($$)
{
	my $service   = shift;
	my $host_name = shift;

	my $service_lc = lc($service);
	my $expression = '';

	$expression .= "({TRIGGER.VALUE}=0 and ";
	$expression .= "{$host_name:rsm.slv.$service_lc.avail.max(#{\$RSM.INCIDENT.$service.FAIL})}=" . DOWN . ") or ";
	$expression .= "({TRIGGER.VALUE}=1 and ";
	$expression .= "{$host_name:rsm.slv.$service_lc.avail.count(#{\$RSM.INCIDENT.$service.RECOVER}," . DOWN . ",\"eq\")}>0)";

	# NB! Configuration trigger that is used in PHP and C code to detect incident!
	# priority must be set to 0!
	my $options = {
		'description' => "$service service is down",
		'expression'  => $expression,
		'priority'    => 0
	};

	really(create_trigger($options, $host_name));
}

sub create_dns_downtime_trigger($$)
{
	my $host_name = shift;
	my $priority  = shift;

	my $service_lc = lc('DNS');

	my $options = {
		'description' => 'DNS service was unavailable for at least {ITEM.VALUE1}m',
		'expression'  => '{' . $host_name . ':rsm.slv.' . $service_lc . '.downtime.last(0)}>{$RSM.SLV.DNS.DOWNTIME}',
		'priority'    => $priority
	};

	really(create_trigger($options, $host_name));
}

sub create_downtime_trigger($$$$$)
{
	my $service     = shift;
	my $host_name   = shift;
	my $threshold   = shift;
	my $priority    = shift;
	my $created_ref = shift;

	my $service_lc = lc($service);

	my $threshold_str = '';

	if ($threshold < 100)
	{
		$threshold_str = "*" . ($threshold * 0.01);
	}

	my $options = {
		'description' => $service . ' service was unavailable for ' . $threshold . '% of allowed $1 minutes',
		'expression'  => '{' . $host_name . ':rsm.slv.' . $service_lc . '.downtime.last(0)}>={$RSM.SLV.' . $service . '.DOWNTIME}' . $threshold_str,
		'priority'    => $priority
	};

	really(create_trigger($options, $host_name, $created_ref));
}

sub create_dns_ns_downtime_trigger($$$$$)
{
	my $nsip        = shift;
	my $host_name   = shift;
	my $threshold   = shift;
	my $priority    = shift;
	my $created_ref = shift;

	my $nsipname = $nsip;
	$nsipname =~ s/,/ (/;
	$nsipname .= ')';

	my $threshold_str = '';

	if ($threshold < 100)
	{
		$threshold_str = "*" . ($threshold * 0.01);
	}

	my $options = {
		'description' => 'DNS ' . $nsipname . ' downtime exceeded ' . $threshold . '% of allowed $1 minutes',
		'expression'  => '{' . $host_name . ':rsm.slv.dns.ns.downtime[' . $nsip . '].last()}>{$RSM.SLV.NS.DOWNTIME}' . $threshold_str,
		'priority'    => $priority
	};

	really(create_trigger($options, $host_name, $created_ref));
}

sub create_rollweek_trigger($$$$$)
{
	my $service     = shift;
	my $host_name   = shift;
	my $threshold   = shift;
	my $priority    = shift;
	my $created_ref = shift;

	my $service_lc = lc($service);

	my $options = {
		'description' => $service . ' rolling week is over ' . $threshold . '%',
		'expression'  => '{' . $host_name . ':rsm.slv.' . $service_lc . '.rollweek.last(0)}>=' . $threshold,
		'priority'    => $priority
	};

	really(create_trigger($options, $host_name, $created_ref));
}

sub create_ratio_of_failed_tests_trigger($$$$$)
{
	my $service     = shift;
	my $host_name   = shift;
	my $threshold   = shift;
	my $priority    = shift;
	my $created_ref = shift;

	my $item_key;
	my $macro;

	if ($service eq 'DNS UDP')
	{
		$item_key = 'rsm.slv.dns.udp.rtt.pfailed';
		$macro = '{$RSM.SLV.DNS.UDP.RTT}';
	}
	elsif ($service eq 'DNS TCP')
	{
		$item_key = 'rsm.slv.dns.tcp.rtt.pfailed';
		$macro = '{$RSM.SLV.DNS.TCP.RTT}';
	}
	elsif ($service eq 'RDDS')
	{
		$item_key = 'rsm.slv.rdds.rtt.pfailed';
		$macro = '{$RSM.SLV.RDDS.RTT}';
	}
	elsif ($service eq 'RDAP')
	{
		$item_key = 'rsm.slv.rdap.rtt.pfailed';
		$macro = '{$RSM.SLV.RDAP.RTT}';
	}
	else
	{
		fail("Unknown service '$service'");
	}

	my $expression;

	if ($threshold == 100)
	{
		$expression = "{$host_name:$item_key.last()}>$macro";
	}
	else
	{
		my $threshold_perc = $threshold / 100;
		$expression = "{$host_name:$item_key.last()}>$macro*$threshold_perc";
	}

	my $options = {
		'description' => "Ratio of failed $service tests exceeded $threshold% of allowed \$1%",
		'expression'  => $expression,
		'priority'    => $priority
	};

	really(create_trigger($options, $host_name, $created_ref));
}

sub create_tld_hosts_on_probes($$$$)
{
	my $root_servers_macros  = shift;
	my $proxy_mon_templateid = shift;
	my $rsmhost_groupid      = shift;
	my $main_templateid      = shift;

	my $proxies = get_proxies_list();
	pfail("Cannot find existing proxies") unless (%{$proxies});

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

		if ($proxies->{$proxyid}{'status'} == HOST_STATUS_PROXY_ACTIVE)	# probe is "disabled"
		{
			$probe_templateid = create_probe_template($probe_name, 0, 0, 0, 0);
			$status = HOST_STATUS_NOT_MONITORED;
		}
		else
		{
			$probe_templateid = create_probe_template($probe_name);
			$status = HOST_STATUS_MONITORED;
		}

		my $probe_status_templateid = create_probe_status_template($probe_name, $probe_templateid, $root_servers_macros);

		really(create_host({
			'groups' => [
				{'groupid' => PROBES_GROUPID}
			],
			'templates' => [
				{'templateid' => $probe_status_templateid},
				{'templateid' => APP_ZABBIX_PROXY_TEMPLATEID},
				{'templateid' => PROBE_ERRORS_TEMPLATEID}
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
				{'templateid' => $main_templateid},
				{'templateid' => RDAP_TEMPLATEID},
				{'templateid' => $probe_templateid}
			],
			'host'         => getopt('tld') . ' ' . $probe_name,
			'status'       => $status,
			'proxy_hostid' => $proxyid,
			'interfaces'   => [DEFAULT_MAIN_INTERFACE]
		}));
	}

	if (opt('rdap-base-url') && opt('rdap-test-domain'))
	{
		set_linked_items_enabled('rdap[', getopt('tld'), 1);
	}
	else
	{
		set_linked_items_enabled('rdap[', getopt('tld'), 0);
	}
}

sub set_linked_items_enabled($$$)
{
	my $like    = shift;
	my $tld     = shift;
	my $enabled = shift;

	my $template = 'Template ' . $tld;
	my $result = get_template($template, false, true);	# do not select macros, select hosts

	pfail("$tld template \"$template\" does not exist") if (keys(%{$result}) == 0);

	foreach my $host_ref (@{$result->{'hosts'}})
	{
		my $hostid = $host_ref->{'hostid'};

		my $result2 = really(get_items_like($hostid, $like, false));	# not a template

		my @items = keys(%{$result2});

		if ($enabled)
		{
			enable_items(\@items);
		}
		else
		{
			disable_items(\@items);
		}
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
                <TLD>,<TLD-TYPE>,<TLD-STATUS>,<RDDS.DNS.TESTPREFIX>,<RDDS.NS.STRING>,<RDDS43.TEST.PREFIX>,
                <TLD.DNSSEC.ENABLED>,<TLD.EPP.ENABLED>,<TLD.RDDS.ENABLED>,<TLD.RDAP.ENABLED>,
                <RDAP.BASE.URL>,<RDAP.TEST.DOMAIN>,<RDDS43.SERVERS>,<RDDS80.SERVERS>,<RDDS43.TEST.DOMAIN>
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
                (default: taken from DNS)
        --server-id=STRING
                ID of Zabbix server $default_server_id
        --rdds-test-prefix=STRING
                domain test prefix for RDDS monitoring (needed only if rdds servers specified)
        --rdds43-test-domain=STRING
                test domain for RDDS monitoring (needed only if rdds servers specified)
        --setup-cron
                create cron jobs and exit
        --epp
                Action with EPP
                (default: no)
        --dns
                Action with DNS
                (default: no)
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
