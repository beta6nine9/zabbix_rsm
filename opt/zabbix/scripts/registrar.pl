#!/usr/bin/env perl
#
# Script to manage Registrars in Zabbix.

use FindBin;
use lib $FindBin::RealBin;

use strict;
use warnings;

use Zabbix;
use Getopt::Long;
use MIME::Base64;
use Digest::MD5 qw(md5_hex);
use Expect;
use Data::Dumper;
use RSM;
use RSMSLV;
use TLD_constants qw(:general :templates :groups :ec :config :api);
use TLDs;
use Text::CSV_XS;

my $trigger_thresholds = RSM_TRIGGER_THRESHOLDS;
my $cfg_global_macros  = CFG_GLOBAL_MACROS;

################################################################################
# main
################################################################################

sub main()
{
	my $config = get_rsm_config();

	init_cli_opts(get_rsm_local_id($config));

	my $server_key = opt('server-id') ? get_rsm_server_key(getopt('server-id')) : get_rsm_local_key($config);
	init_zabbix_api($config, $server_key);

	init_macros_and_validate_env();

	if (opt('list-services'))
	{
		list_services($server_key, getopt('rr-id'));
	}
	elsif (opt('delete'))
	{
		update_rsmhost_config_times(getopt('rr-id'));
		manage_registrar('delete', getopt('rr-id'), getopt('rdds'), getopt('rdap'));
	}
	elsif (opt('disable'))
	{
		update_rsmhost_config_times(getopt('rr-id'));
		manage_registrar('disable', getopt('rr-id'), getopt('rdds'), getopt('rdap'));
	}
	else
	{
		update_rsmhost_config_times(getopt('rr-id'));
		add_new_registrar();
	}
}

sub init_cli_opts($)
{
	my $default_server_id = shift;

	my %OPTS;
	my $rv = GetOptions(\%OPTS,
			"rr-id=s",
			"rr-name=s",
			"rr-family=s",
			"server-id=s",
			"delete",
			"disable",
			"rdds",
			"rdap",
			"list-services",
			"rdds43-servers=s",
			"rdds80-servers=s",
			"rdap-base-url=s",
			"rdap-test-domain=s",
			"rdds-ns-string=s",
			"root-servers=s",
			"rdds43-test-domain=s",
			"debug",
			"help|?");

	if (!$rv || !%OPTS || $OPTS{'help'})
	{
		__usage($default_server_id);
	}

	override_opts(\%OPTS);

	validate_cli_opts($default_server_id);
}

sub validate_cli_opts($)
{
	my $default_server_id = shift;

	my $msg = "";

	# --list-services doesn't require other options

	if (opt('list-services'))
	{
		return;
	}

	# --delete and --disable require only --rr-id

	if (opt('delete') || opt('disable'))
	{
		if (!opt('rr-id'))
		{
			__usage($default_server_id, "Registrar ID must be specified (--rr-id)");
		}

		return;
	}

	# when creating or editing registrar, --rr-id, --rr-name and --rr-family are required

	if (!opt('rr-id'))
	{
		$msg .= "Registrar ID must be specified (--rr-id)\n";
	}
	if (!opt('rr-name'))
	{
		$msg .= "Registrar name must be specified (--rr-name)\n";
	}
	if (!opt('rr-family'))
	{
		$msg .= "Registrar family must be specified (--rr-family)\n";
	}

	# generic rules

	if (!opt('rdds43-servers') && !opt('rdds80-servers') && !opt('rdap-base-url'))
	{
		$msg .= "--rdds43-servers, --rdds80-servers and/or --rdap-base-url must be specified\n";
	}

	# rules for RDDS

	if (opt('rdds43-servers') || opt('rdds80-servers'))
	{
		if (!opt('rdds43-servers') || !opt('rdds80-servers'))
		{
			$msg .= "none or both --rdds43-servers and --rdds80-servers must be specified\n";
		}
		if (!opt('rdds43-test-domain'))
		{
			# this might be needed only for RDDS43, but must be double-checked, if RDDS43 and RDDS80 are separated
			$msg .= "--rdds43-test-domain must be specified\n";
		}
	}

	# rules for RDAP

	if (opt('rdap-base-url') || opt('rdap-test-domain'))
	{
		if (!opt('rdap-base-url') || !opt('rdap-test-domain'))
		{
			$msg .= "none or both --rdap-base-url and --rdap-test-domain must be specified\n";
		}
	}

	# if any option is missing, print error message and quit

	if ($msg)
	{
		chomp($msg);
		__usage($default_server_id, $msg);
	}
}

sub init_macros_and_validate_env()
{
	# expect "registrar" monitoring target
	my $target = get_global_macro_value('{$RSM.MONITORING.TARGET}');
	if (!defined($target))
	{
		pfail('cannot find global macro {$RSM.MONITORING.TARGET}');
	}

	if ($target ne MONITORING_TARGET_REGISTRAR)
	{
		pfail("expected monitoring target \"${\MONITORING_TARGET_REGISTRAR}\", but got \"$target\",".
			" if you'd like to change it, please run:".
			"\n\n/opt/zabbix/scripts/change-macro.pl".
			" --macro '{\$RSM.MONITORING.TARGET}'".
			" --value '${\MONITORING_TARGET_REGISTRAR}'");
	}

	# get global macros related to this script
	foreach my $macro (keys(%{$cfg_global_macros}))
	{
		$cfg_global_macros->{$macro} = get_global_macro_value($macro);
		pfail('cannot get global macro ', $macro) unless defined($cfg_global_macros->{$macro});
	}

	if (!__is_rdap_standalone() && (opt('rdds') || opt('rdap')))
	{
		pfail('--rdds, --rdap are only supported after switch to Standalone RDAP')
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
	$result = zbx_connect($section->{'za_url'}, $section->{'za_user'}, $section->{'za_password'}, getopt('debug'));

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
# list services for a single RSMHOST or all RSMHOSTs
################################################################################

sub list_services($;$)
{
	my $server_key = shift;
	my $rsmhost    = shift; # optional

	# NB! Keep @columns in sync with __usage()!
	my @columns = (
		'status',
		'{$RSM.RDDS.NS.STRING}',
		'{$RSM.RDDS43.TEST.DOMAIN}',
		'{$RSM.TLD.RDDS.ENABLED}',
		'{$RDAP.TLD.ENABLED}',
		'{$RDAP.BASE.URL}',
		'{$RDAP.TEST.DOMAIN}',
		'{$RSM.TLD.RDDS.43.SERVERS}',
		'{$RSM.TLD.RDDS.80.SERVERS}',
	);

	my %rsmhosts = get_registrar_list();

	if (defined($rsmhost))
	{
		%rsmhosts = ($rsmhost => $rsmhosts{$rsmhost});
	}

	my @rows = ();

	foreach my $rsmhost (sort(keys(%rsmhosts)))
	{
		my $config = get_rsmhost_config($server_key, $rsmhost);

		my @row = ();

		push(@row, $rsmhost);                      # Registrar ID
		push(@row, $rsmhosts{$rsmhost}{'name'});   # Registrar name
		push(@row, $rsmhosts{$rsmhost}{'family'}); # Registrar family

		push(@row, map($config->{$_} // "", @columns));

		push(@rows, \@row);
	}

	# convert undefs to empty strings
	@rows = map([map($_ // "", @{$_})], @rows);

	# all fields in a CSV must be double-quoted, even if empty
	my $csv = Text::CSV_XS->new({binary => 1, auto_diag => 1, always_quote => 1, eol => "\n"});

	$csv->print(*STDOUT, $_) foreach (@rows);
}

sub get_registrar_list()
{
	my $registrars = get_host_group('TLDs', true, false, ['info_1', 'info_2']);

	my %result;

	foreach my $host (@{$registrars->{'hosts'}})
	{
		$result{$host->{'host'}} = {
			'name'   => $host->{'info_1'},
			'family' => $host->{'info_2'}
		};
	}

	return %result;
}

sub get_rsmhost_config($$)
{
	my $server_key = shift;
	my $rsmhost    = shift;

	my $result;

	my $config_templateid = get_template_id(TEMPLATE_RSMHOST_CONFIG_PREFIX . $rsmhost);
	my $macros = get_host_macro($config_templateid, undef);
	my $rsmhost_host = get_host($rsmhost, true);

	# save rsmhost status (enabled, disabled)
	$result->{'status'} = $rsmhost_host->{'status'};

	# and macros
	map { $result->{$_->{'macro'}} = $_->{'value'} } @{$macros};

	return $result;
}

################################################################################
# delete or disable RSMHOST
################################################################################

sub manage_registrar($$$$)
{
	my $action  = shift;
	my $rsmhost = shift;
	my $rdds    = shift;
	my $rdap    = shift;

	if (!__is_rdap_standalone())
	{
		# before switch to Standalone RDAP treat both services like one
		# and delete or disable hosts without touching specific items or macros
		$rdds = $rdap = 1;
	}
	elsif (!$rdds && !$rdap)
	{
		# after the switch - delete or disable hosts if no specific services
		# provided in the command line
		$rdds = $rdap = 1;
	}

	my $main_host = get_host($rsmhost, false);
	pfail("cannot find host \"$rsmhost\"") unless %{$main_host};

	my $rsmhost_template = get_template(TEMPLATE_RSMHOST_CONFIG_PREFIX . $rsmhost, false, true);
	pfail("cannot find template \"" . TEMPLATE_RSMHOST_CONFIG_PREFIX . "$rsmhost\"") unless %{$rsmhost_template};

	my $main_hostid = $main_host->{'hostid'};
	my $config_templateid = $rsmhost_template->{'templateid'};

	print("Requested to $action '$rsmhost'\n");
	print("Main hostid of the Registrar: $main_hostid\n");
	print("Main templateid of the Registrar: $config_templateid\n");

	my @hostids = (
		$main_hostid,
		map($_->{'hostid'}, @{$rsmhost_template->{'hosts'}})
	);

	# get the list of currently enabled services
	my %enabled_services;
	foreach my $service ('rdds', 'rdap')
	{
		my $macro = $service eq 'rdap' ? '{$RDAP.TLD.ENABLED}' : '{$RSM.TLD.' . uc($service) . '.ENABLED}';

		my $macro_value = get_host_macro($config_templateid, $macro);

		$enabled_services{$service} = 1 if (defined($macro_value) && $macro_value->{'value'});
	}

	# filter out what's requested to be disabled, the host will be disabled later if no enabled services left
	if ($action eq 'disable')
	{
		delete($enabled_services{'rdds'}) if ($rdds);
		delete($enabled_services{'rdap'}) if ($rdap);
	}

	if ($rdds && $rdap)
	{
		if ($action eq 'disable')
		{
			my @hostids_for_api = map({'hostid' => $_}, @hostids);

			my $result = disable_hosts(\@hostids_for_api);

			if (%{$result})
			{
				if (!compare_arrays(\@hostids, \@{$result->{'hostids'}}))
				{
					pfail("en error occurred while disabling hosts!");
				}
			}
			else
			{
				pfail("en error occurred while disabling hosts!");
			}
		}
		elsif ($action eq 'delete')
		{
			remove_hosts(\@hostids);
			remove_templates([$config_templateid]);

			my $hostgroupid = get_host_group('TLD ' . $rsmhost, false, false);
			$hostgroupid = $hostgroupid->{'groupid'};
			remove_hostgroups([$hostgroupid]);
		}

		return;
	}

	my $service = $rdds ? 'rdds' : 'rdap';

	my $macro = $service eq 'rdap' ? '{$RDAP.TLD.ENABLED}' : '{$RSM.TLD.' . uc($service) . '.ENABLED}';

	create_macro($macro, 0, $config_templateid, true);

	# get items on "<rsmhost>" host
	my $rsmhost_items = get_host_items($main_hostid);
	set_service_items_status($rsmhost_items, RDDS_STATUS_TEMPLATEID, 0);
	set_service_items_status($rsmhost_items, RDAP_STATUS_TEMPLATEID, 0);

	# get "<rsmhost> <probe>" hostids
	my $probe_hostids = get_template(TEMPLATE_RSMHOST_CONFIG_PREFIX . $tld, false, true);
	$probe_hostids = $probe_hostids->{'hosts'};
	$probe_hostids = [grep($_->{'host'} ne $tld, @{$probe_hostids})];
	$probe_hostids = [map($_->{'hostid'}, @{$probe_hostids})];

	# get items on all "<rsmhost> <probe>" hosts
	my $probe_items;
	foreach my $hostid (@{$probe_hostids})
	{
		push(@{$probe_items}, @{get_host_items($hostid)});
	}

	# disable RDDS and/or RDAP items
	if ($rdds)
	{
		set_service_items_status($rsmhost_items, RDDS_STATUS_TEMPLATEID, 0);
		set_service_items_status($probe_items, RDDS_TEST_TEMPLATEID, 0);
	}
	if ($rdap)
	{
		set_service_items_status($rsmhost_items, RDAP_STATUS_TEMPLATEID, 0);
		set_service_items_status($probe_items, RDAP_TEST_TEMPLATEID, 0);
	}

	# disable host if no enabled services left
	if ($action eq 'disable' && scalar(keys(%enabled_services)) == 0)
	{
		my @hostids_for_api = map({'hostid' => $_}, @hostids);

		my $result = disable_hosts(\@hostids_for_api);

		if (%{$result})
		{
			if (!compare_arrays(\@hostids, \@{$result->{'hostids'}}))
			{
				pfail("en error occurred while disabling hosts!");
			}
		}
		else
		{
			pfail("en error occurred while disabling hosts!");
		}
	}
}

sub compare_arrays($$)
{
	# TODO: what if $a = [1, 2, 3], $b = [1, 2]?
	# TODO: what if $a = [1, 2, 3], $b = [1, 2, 2, 3]?

	my $array_A = shift;
	my $array_B = shift;

	my %values = map { $_ => undef } @{$array_A};

	foreach my $value (@{$array_B})
	{
		if (!exists($values{$value}))
		{
			return 0;
		}
	}

	return 1;
}

################################################################################
# add or update RSMHOST
################################################################################

sub add_new_registrar()
{
	my $proxies = get_proxies_list();

	pfail("please add at least one probe first using probes.pl") unless (%{$proxies});

	update_root_server_macros(getopt('root-servers'));

	my $config_templateid = create_main_template(getopt('rr-id'));
	my $rsmhost_groupid = really(create_group('TLD ' . getopt('rr-id')));

	create_rsmhost($config_templateid, getopt('rr-id'), getopt('rr-name'), getopt('rr-family'));

	create_rsmhosts_on_probes($rsmhost_groupid, $config_templateid, $proxies);
}

sub create_main_template($)
{
	my $rsmhost = shift;

	my $template_name = TEMPLATE_RSMHOST_CONFIG_PREFIX . $rsmhost;

	my $template = get_template(TEMPLATE_RSMHOST_CONFIG_PREFIX . $rsmhost, 1, 0);
	my $templateid;

	my $new_rsmhost = !%{$template};

	if ($new_rsmhost)
	{
		$templateid = really(create_template($template_name));
	}
	else
	{
		$templateid = $template->{'templateid'};
	}

	my $rdds_ns_string = opt('rdds-ns-string') ? getopt('rdds-ns-string') : CFG_DEFAULT_RDDS_NS_STRING;

	really(create_macro('{$RSM.TLD}'                , $rsmhost                      , $templateid));
	really(create_macro('{$RSM.RDDS43.TEST.DOMAIN}' , getopt('rdds43-test-domain')  , $templateid, 1)) if (opt('rdds43-test-domain'));
	really(create_macro('{$RSM.RDDS.NS.STRING}'     , $rdds_ns_string               , $templateid, 1));
	really(create_macro('{$RSM.TLD.RDDS.ENABLED}'   , opt('rdds43-servers') ? 1 : 0 , $templateid, 1));
	really(create_macro('{$RSM.TLD.RDDS.43.SERVERS}', getopt('rdds43-servers') // '', $templateid, 1));
	really(create_macro('{$RSM.TLD.RDDS.80.SERVERS}', getopt('rdds80-servers') // '', $templateid, 1));
	really(create_macro('{$RSM.TLD.EPP.ENABLED}'    , 0                             , $templateid)); # required by rsm.rdds[] metric
	really(create_macro('{$RSM.TLD.CONFIG.TIMES}'   , $^T                           , $templateid, 1)) if ($new_rsmhost);

	if (opt('rdap-base-url') && opt('rdap-test-domain'))
	{
		really(create_macro('{$RDAP.BASE.URL}'   , getopt('rdap-base-url')   , $templateid, 1));
		really(create_macro('{$RDAP.TEST.DOMAIN}', getopt('rdap-test-domain'), $templateid, 1));
		really(create_macro('{$RDAP.TLD.ENABLED}', 1                         , $templateid, 1));
	}
	else
	{
		really(create_macro('{$RDAP.TLD.ENABLED}', 0, $templateid, 1));
	}

	return $templateid;
}

sub __is_rdap_standalone()
{
	return $cfg_global_macros->{'{$RSM.RDAP.STANDALONE}'} != 0 &&
			time() >= $cfg_global_macros->{'{$RSM.RDAP.STANDALONE}'};
}

sub create_rsmhost($$$$)
{
	my $config_templateid = shift;
	my $rr_id             = shift;
	my $rr_name           = shift;
	my $rr_family         = shift;

	my $rsmhostid = really(create_host({
		'groups'     => [
			{'groupid' => TLDS_GROUPID},
			{'groupid' => TLD_TYPE_GROUPIDS->{${\TLD_TYPE_G}}}
		],
		'templates' => [
			{'templateid' => $config_templateid},
			{'templateid' => CONFIG_HISTORY_TEMPLATEID},
			{'templateid' => RDDS_STATUS_TEMPLATEID},
			{'templateid' => RDAP_STATUS_TEMPLATEID},
		],
		'host'       => $rr_id,
		'info_1'     => $rr_name,
		'info_2'     => $rr_family,
		'status'     => HOST_STATUS_MONITORED,
		'interfaces' => [DEFAULT_MAIN_INTERFACE]
	}));

	my $rsmhost_items = get_host_items($rsmhostid);

	if (__is_rdap_standalone())
	{
		set_service_items_status($rsmhost_items, RDDS_STATUS_TEMPLATEID, opt('rdds43-servers') || opt('rdds80-servers'));
		set_service_items_status($rsmhost_items, RDAP_STATUS_TEMPLATEID, opt('rdap-base-url'));
	}
	else
	{
		set_service_items_status($rsmhost_items, RDDS_STATUS_TEMPLATEID, opt('rdds43-servers') || opt('rdds80-servers') || opt('rdap-base-url'));
		set_service_items_status($rsmhost_items, RDAP_STATUS_TEMPLATEID, 0);
	}
}

sub create_rsmhosts_on_probes($$$)
{
	my $rsmhost_groupid      = shift;
	my $config_templateid    = shift;
	my $proxies              = shift;

	# TODO: Revise this part because it is creating entities (e.g. "<Probe>", "<Probe> - mon" hosts) which should
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
				{'templateid' => PROXY_HEALTH_TEMPLATEID}
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

		my $rsmhost_probe_hostid = really(create_host({
			'groups' => [
				{'groupid' => $rsmhost_groupid},
				{'groupid' => $probe_groupid},
				{'groupid' => TLD_PROBE_RESULTS_GROUPID},
				{'groupid' => TLD_TYPE_PROBE_RESULTS_GROUPIDS->{${\TLD_TYPE_G}}}
			],
			'templates' => [
				{'templateid' => $config_templateid},
				{'templateid' => RDDS_TEST_TEMPLATEID},
				{'templateid' => RDAP_TEST_TEMPLATEID},
				{'templateid' => $probe_templateid}
			],
			'host'         => getopt('rr-id') . ' ' . $probe_name,
			'status'       => $status,
			'proxy_hostid' => $proxyid,
			'interfaces'   => [DEFAULT_MAIN_INTERFACE]
		}));

		my $rsmhost_probe_items = get_host_items($rsmhost_probe_hostid);

		my $probe_config = get_template(TEMPLATE_PROBE_CONFIG_PREFIX . $proxies->{$proxyid}{'host'}, 1, 0);
		my %probe_config_macros = map(($_->{'macro'} => $_->{'value'}), @{$probe_config->{'macros'}});
		my $probe_rdds = $probe_config_macros{'{$RSM.RDDS.ENABLED}'};
		my $probe_rdap = $probe_config_macros{'{$RSM.RDAP.ENABLED}'};

		set_service_items_status($rsmhost_probe_items, RDDS_TEST_TEMPLATEID, $probe_rdds && (opt('rdds43-servers') || opt('rdds80-servers')));
		set_service_items_status($rsmhost_probe_items, RDAP_TEST_TEMPLATEID, $probe_rdap && opt('rdap-base-url'));
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

        --rr-id=STRING
                Registrar ID
        --rr-name=STRING
                Registrar name
        --rr-family=STRING
                Registrar family

Other options

        --server-id=STRING
                ID of Zabbix server (default: $default_server_id)
        --delete
                delete specified Registrar or Registrar's services specified by: --rdds, --rdap
                (services supported only after switch to Standalone RDAP)
        --disable
                disable specified Registrar or Registrar's services specified by: --rdds, --rdap
                (services supported only after switch to Standalone RDAP)
        --list-services
                list services of each Regstrar, the output is comma-separated list:
                <RR-ID>,<RR-NAME>,<RR-FAMILY>,<RR-STATUS>,<RDDS.NS.STRING>,<RDDS43.TEST.DOMAIN>,
                <RDDS.ENABLED>,<RDAP.ENABLED>,<RDAP.BASE.URL>,<RDAP.TEST.DOMAIN>,
		<RDDS43.SERVERS>,<RDDS80.SERVERS>
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
        --rdds-ns-string=STRING
                name server prefix in the WHOIS output
                (default: "${\CFG_DEFAULT_RDDS_NS_STRING}")
        --root-servers=STRING
                list of IPv4 and IPv6 root servers separated by comma and semicolon: "v4IP1[,v4IP2,...][;v6IP1[,v6IP2,...]]"
        --rdds43-test-domain=STRING
                test domain for RDDS monitoring (needed only if rdds servers specified)
        --rdds
                Action with RDDS
                (only effective after switch to Standalone RDAP, default: no)
        --rdap
                Action with RDAP
                (only effective after switch to Standalone RDAP, default: no)
	--debug
		print every Zabbix API request and response, useful for troubleshooting
        --help
                display this message
EOF
	exit(1);
}

main();
