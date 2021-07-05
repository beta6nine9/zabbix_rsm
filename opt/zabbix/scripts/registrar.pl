#!/usr/bin/perl
#
# - RDDS availability test		(data collection)	rsm.rdds			(simple, every 5 minutes)
#   (also RDDS43 and RDDS80					rsm.rdds.43.ip			(trapper, Proxy)
#   availability at a particular				rsm.rdds.43.rtt			-|-
#   minute)							rsm.rdds.43.upd			-|-
#								rsm.rdds.80.ip			-|-
#								rsm.rdds.80.rtt			-|-
#
# - RDDS availability			(given minute)		rsm.slv.rdds.avail		-|-
# - RDDS rolling week			(rolling week)		rsm.slv.rdds.rollweek		-|-
# - RDDS43 monthly resolution RTT	(monthly)		rsm.slv.rdds.43.rtt.month	-|-
# - RDDS80 monthly resolution RTT	(monthly)		rsm.slv.rdds.80.rtt.month	-|-
# - RDDS monthly update time		(monthly)		rsm.slv.rdds.upd.month		-|-

BEGIN
{
	our $MYDIR = $0; $MYDIR =~ s,(.*)/.*,$1,; $MYDIR = '.' if ($MYDIR eq $0);
}
use lib $MYDIR;

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
use TLD_constants qw(:general :templates :groups :value_types :ec :slv :config :api);
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

	pfail("SLV scripts path is not specified. Please check configuration file") unless defined $config->{'slv'}{'path'};

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
		manage_registrar('delete', getopt('rr-id'), getopt('rdds'), getopt('rdap'));
	}
	elsif (opt('disable'))
	{
		manage_registrar('disable', getopt('rr-id'), getopt('rdds'), getopt('rdap'));
	}
	else
	{
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
			"delete!",
			"disable!",
			"rdds!",
			"rdap!",
			"list-services!",
			"rdds43-servers=s",
			"rdds80-servers=s",
			"rdap-base-url=s",
			"rdap-test-domain=s",
			"rdds-ns-string=s",
			"root-servers=s",
			"rdds43-test-domain=s",
			"verbose!",
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
		pfail("expected monitoring target \"${\MONITORING_TARGET_REGISTRAR}\", but got \"$target\", if you'd like to change it, please run:".
			"\n\n/opt/zabbix/scripts/change-macro.pl --macro '{\$RSM.MONITORING.TARGET}' --value '${\MONITORING_TARGET_REGISTRAR}'");
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
		'rdds43_servers',
		'rdds80_servers',
	);

	my %rsmhosts = get_registrar_list();

	if (defined($rsmhost))
	{
		%rsmhosts = ($rsmhost => $rsmhosts{$rsmhost});
	}

	my @rows = ();

	foreach my $rsmhost (sort(keys(%rsmhosts)))
	{
		my $services = get_services($server_key, $rsmhost);

		my @row = ();

		push(@row, $rsmhost);                      # Registrar ID
		push(@row, $rsmhosts{$rsmhost}{'name'});   # Registrar name
		push(@row, $rsmhosts{$rsmhost}{'family'}); # Registrar family
		push(@row, map($services->{$_}, @columns));

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

sub get_services($$)
{
	my $server_key = shift;
	my $rsmhost    = shift;

	my $result;

	# get template id, list of macros

	my $template = get_template("Template $rsmhost", true, false);
	pfail("Registrar \"$rsmhost\" does not exist on \"$server_key\"") unless ($template->{'templateid'});

	# store macros

	map { $result->{$_->{'macro'}} = $_->{'value'} } @{$template->{'macros'}};

	# get status (enabled, disabled)

	my $tld_host = get_host($rsmhost, true);

	$result->{'status'} = $tld_host->{'status'};

	# get RDDS43 and RDDS80 servers

	my $items = get_items_like($template->{'templateid'}, 'rsm.rdds[', true);

	if (%{$items} && (values(%{$items}))[0]{'status'} == 0)
	{
		(values(%{$items}))[0]{'key_'} =~ /,"(\S+)","(\S+)"]/;

		$result->{'rdds43_servers'} = $1;
		$result->{'rdds80_servers'} = $2;
	}

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

	my $rsmhost_template = get_template('Template ' . $rsmhost, false, true);
	pfail("cannot find template \"Template $rsmhost\"") unless %{$rsmhost_template};

	my $main_hostid = $main_host->{'hostid'};
	my $main_templateid = $rsmhost_template->{'templateid'};

	print("Requested to $action '$rsmhost'\n");
	print("Main hostid of the Registrar: $main_hostid\n");
	print("Main templateid of the Registrar: $main_templateid\n");

	my @hostids = (
		$main_hostid,
		map($_->{'hostid'}, @{$rsmhost_template->{'hosts'}})
	);

	# get the list of currently enabled services
	my %enabled_services;
	foreach my $service ('rdds', 'rdap')
	{
		my $macro = $service eq 'rdap' ? '{$RDAP.TLD.ENABLED}' : '{$RSM.TLD.' . uc($service) . '.ENABLED}';

		my $macro_value = get_host_macro($main_templateid, $macro);

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
			remove_templates([$main_templateid]);

			my $hostgroupid = get_host_group('TLD ' . $rsmhost, false, false);
			$hostgroupid = $hostgroupid->{'groupid'};
			remove_hostgroups([$hostgroupid]);
		}

		return;
	}

	my $service = $rdds ? 'rdds' : 'rdap';
	my $template_items = get_items_like($main_templateid, $service, true);
	my $host_items = get_items_like($main_hostid, $service, false);

	if ($rdds)
	{
		my $service_enabled_itemkey = "$service.enabled";

		my @service_enabled_itemid = grep { $template_items->{$_}{'key_'} eq $service_enabled_itemkey } keys(%{$template_items});
		if (!@service_enabled_itemid)
		{
			pfail("failed to find $service_enabled_itemkey item");
		}

		delete($template_items->{$service_enabled_itemid[0]});
	}

	my $template_item_ids = [keys(%{$template_items})];
	my $host_items_ids = [keys(%{$host_items})];

	if (scalar(@{$template_item_ids}) == 0 && $service ne 'rdap') # RDAP doesn't have items in "Template $tld"
	{
		print("Could not find $service related items on the template level\n");
	}

	if (scalar(@{$host_items_ids}) == 0)
	{
		print("Could not find $service related items on host level\n");
	}

	my @itemids = (@{$template_item_ids}, @{$host_items_ids});

	# print(Dumper(\@itemids));
	# print(Dumper($host_items_ids));

	if (scalar(@itemids))
	{
		my $macro = $service eq 'rdap' ? '{$RDAP.TLD.ENABLED}' : '{$RSM.TLD.' . uc($service) . '.ENABLED}';

		create_macro($macro, 0, $main_templateid, true);

		if ($action eq 'disable')
		{
			disable_items(\@itemids);
		}
		else # $action is 'delete'
		{
			remove_items(\@itemids);
		}
	}

	if ($action eq 'disable' && $service eq 'rdap')
	{
		set_linked_items_enabled('rdap[', $rsmhost, 0);
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
	my $root_servers_macros = update_root_servers(getopt('root-servers'));
	print("Could not retrive list of root servers or create global macros\n") unless (defined($root_servers_macros));

	my $main_templateid = create_main_template(getopt('rr-id'));
	pfail("Main templateid is not defined") unless defined $main_templateid;

	my $rsmhost_groupid = really(create_group('TLD ' . getopt('rr-id')));

	create_rsmhost();

	my $proxy_mon_templateid = create_probe_health_tmpl();

	create_tld_hosts_on_probes($root_servers_macros, $proxy_mon_templateid, $rsmhost_groupid, $main_templateid);
}

sub create_main_template($)
{
	my $rsmhost = shift;

	my $template_name = 'Template ' . $rsmhost;

	my $templateid = really(create_template($template_name));

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

	create_items_rdds($templateid, $template_name);

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

	really(create_macro('{$RSM.TLD}', $rsmhost, $templateid));
	really(create_macro('{$RSM.RDDS43.TEST.DOMAIN}', getopt('rdds43-test-domain'), $templateid, 1)) if (opt('rdds43-test-domain'));
	really(create_macro('{$RSM.RDDS.NS.STRING}', opt('rdds-ns-string') ? getopt('rdds-ns-string') : CFG_DEFAULT_RDDS_NS_STRING, $templateid, 1));
	really(create_macro('{$RSM.TLD.RDDS.ENABLED}', opt('rdds43-servers') ? 1 : 0, $templateid, 1));
	really(create_macro('{$RSM.TLD.EPP.ENABLED}', 0, $templateid)); # required by rsm.rdds[] metric

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

	return $templateid;
}

sub create_items_rdds($$)
{
	my $templateid    = shift;
	my $template_name = shift;

	if (opt('rdds43-servers'))
	{
		my $applicationid = get_application_id('RDDS43', $templateid);

		really(create_item({
			'name'         => 'RDDS43 IP of $1',
			'key_'         => 'rsm.rdds.43.ip[{$RSM.TLD}]',
			'status'       => ITEM_STATUS_ACTIVE,
			'hostid'       => $templateid,
			'applications' => [$applicationid],
			'type'         => ITEM_TYPE_TRAPPER,
			'value_type'   => ITEM_VALUE_TYPE_STR
		}));

		really(create_item({
			'name'         => 'RDDS43 RTT of $1',
			'key_'         => 'rsm.rdds.43.rtt[{$RSM.TLD}]',
			'status'       => ITEM_STATUS_ACTIVE,
			'hostid'       => $templateid,
			'applications' => [$applicationid],
			'type'         => ITEM_TYPE_TRAPPER,
			'value_type'   => ITEM_VALUE_TYPE_FLOAT,
			'valuemapid'   => RSM_VALUE_MAPPINGS->{'rsm_rdds_rtt'}
		}));

		really(create_item({
			'name'         => 'RDDS43 target',
			'key_'         => 'rsm.rdds.43.target',
			'status'       => ITEM_STATUS_ACTIVE,
			'hostid'       => $templateid,
			'applications' => [$applicationid],
			'type'         => ITEM_TYPE_TRAPPER,
			'value_type'   => ITEM_VALUE_TYPE_STR
		}));

		really(create_item({
			'name'         => 'RDDS43 tested name',
			'key_'         => 'rsm.rdds.43.testedname',
			'status'       => ITEM_STATUS_ACTIVE,
			'hostid'       => $templateid,
			'applications' => [$applicationid],
			'type'         => ITEM_TYPE_TRAPPER,
			'value_type'   => ITEM_VALUE_TYPE_STR
		}));
	}

	if (opt('rdds80-servers'))
	{
		my $applicationid = get_application_id('RDDS80', $templateid);

		really(create_item({
			'name'         => 'RDDS80 IP of $1',
			'key_'         => 'rsm.rdds.80.ip[{$RSM.TLD}]',
			'status'       => ITEM_STATUS_ACTIVE,
			'hostid'       => $templateid,
			'applications' => [$applicationid],
			'type'         => ITEM_TYPE_TRAPPER,
			'value_type'   => ITEM_VALUE_TYPE_STR
		}));

		really(create_item({
			'name'         => 'RDDS80 RTT of $1',
			'key_'         => 'rsm.rdds.80.rtt[{$RSM.TLD}]',
			'status'       => ITEM_STATUS_ACTIVE,
			'hostid'       => $templateid,
			'applications' => [$applicationid],
			'type'         => ITEM_TYPE_TRAPPER,
			'value_type'   => ITEM_VALUE_TYPE_FLOAT,
			'valuemapid'   => RSM_VALUE_MAPPINGS->{'rsm_rdds_rtt'}
		}));

		really(create_item({
			'name'         => 'RDDS80 target',
			'key_'         => 'rsm.rdds.80.target',
			'status'       => ITEM_STATUS_ACTIVE,
			'hostid'       => $templateid,
			'applications' => [$applicationid],
			'type'         => ITEM_TYPE_TRAPPER,
			'value_type'   => ITEM_VALUE_TYPE_STR
		}));
	}

	if (opt('rdds43-servers') || opt('rdds80-servers'))
	{
		# disable old items to keep the history, if value of rdds43-servers and/or rdds80-servers has changed
		my @old_rdds_availability_items = keys(%{get_items_like($templateid, 'rsm.rdds[', true)});
		disable_items(\@old_rdds_availability_items);

		# create new item (or update/enable existing item)
		really(create_item({
			'name'         => 'RDDS availability',
			'key_'         => 'rsm.rdds[{$RSM.TLD},"' . getopt('rdds43-servers') . '","' . getopt('rdds80-servers') . '"]',
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

sub create_rsmhost()
{
	my $rr_id     = getopt('rr-id');
	my $rr_name   = getopt('rr-name');
	my $rr_family = getopt('rr-family'); # TODO: save family

	my $rsmhostid = really(create_host({
		'groups'     => [
			{'groupid' => TLDS_GROUPID},
			{'groupid' => TLD_TYPE_GROUPIDS->{${\TLD_TYPE_G}}}
		],
		'host'       => $rr_id,
		'info_1'     => $rr_name,
		'info_2'     => $rr_family,
		'status'     => HOST_STATUS_MONITORED,
		'interfaces' => [DEFAULT_MAIN_INTERFACE]
	}));

	create_slv_items($rsmhostid, $rr_id);
}

sub create_rdds_or_rdap_slv_items($$$;$)
{
	my $hostid       = shift;
	my $host_name    = shift;
	my $service      = shift;
	my $item_status  = shift;

	$item_status = ITEM_STATUS_ACTIVE unless (defined($item_status));

	($service ne "RDDS" and $service ne "RDAP") and pfail("Internal error, invalid service '$service' while creating SLV items");

	my $service_lc = lc($service);

	create_slv_item("$service availability",          "rsm.slv.$service_lc.avail",    $hostid, VALUE_TYPE_AVAIL, [get_application_id(APP_SLV_PARTTEST, $hostid)]);
	create_slv_item("$service minutes of downtime",   "rsm.slv.$service_lc.downtime", $hostid, VALUE_TYPE_NUM,   [get_application_id(APP_SLV_CURMON, $hostid)]);
	create_slv_item("$service weekly unavailability", "rsm.slv.$service_lc.rollweek", $hostid, VALUE_TYPE_PERC,  [get_application_id(APP_SLV_ROLLWEEK, $hostid)]);

	create_avail_trigger($service, $host_name);
	create_dependent_trigger_chain($host_name, $service, \&create_downtime_trigger, $trigger_thresholds);
	create_dependent_trigger_chain($host_name, $service, \&create_rollweek_trigger, $trigger_thresholds);

	create_slv_item("Number of performed monthly $service queries", "rsm.slv.$service_lc.rtt.performed", $hostid, VALUE_TYPE_NUM,  [get_application_id(APP_SLV_CURMON, $hostid)]);
	create_slv_item("Number of failed monthly $service queries"   , "rsm.slv.$service_lc.rtt.failed"   , $hostid, VALUE_TYPE_NUM,  [get_application_id(APP_SLV_CURMON, $hostid)]);
	create_slv_item("Ratio of failed monthly $service queries"    , "rsm.slv.$service_lc.rtt.pfailed"  , $hostid, VALUE_TYPE_PERC, [get_application_id(APP_SLV_CURMON, $hostid)]);

	create_dependent_trigger_chain($host_name, $service, \&create_ratio_of_failed_tests_trigger, $trigger_thresholds);
}

sub __is_rdap_standalone()
{
	return  $cfg_global_macros->{'{$RSM.RDAP.STANDALONE}'} != 0 &&
			time() >= $cfg_global_macros->{'{$RSM.RDAP.STANDALONE}'};
}

sub create_slv_items($$)
{
	my $hostid    = shift;
	my $host_name = shift;

	if (!__is_rdap_standalone()) # we haven't switched to RDAP yet
	{
		# create RDDS items which may also include RDAP check results
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

sub create_slv_item($$$$$)
{
	my $name           = shift;
	my $key            = shift;
	my $hostid         = shift;
	my $value_type     = shift;
	my $applicationids = shift;

	if ($value_type == VALUE_TYPE_AVAIL)
	{
		return really(create_item({
			'name'         => $name,
			'key_'         => $key,
			'status'       => ITEM_STATUS_ACTIVE,
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
			'status'       => ITEM_STATUS_ACTIVE,
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
			'status'       => ITEM_STATUS_ACTIVE,
			'hostid'       => $hostid,
			'type'         => ITEM_TYPE_TRAPPER,
			'value_type'   => ITEM_VALUE_TYPE_FLOAT,
			'applications' => $applicationids,
			'units'        => '%'
		}));
	}

	pfail("Unknown value type $value_type.");
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

	if ($service eq 'RDDS')
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
				{'groupid' => TLD_TYPE_PROBE_RESULTS_GROUPIDS->{${\TLD_TYPE_G}}}
			],
			'templates' => [
				{'templateid' => $main_templateid},
				{'templateid' => RDAP_TEMPLATEID},
				{'templateid' => $probe_templateid}
			],
			'host'         => getopt('rr-id') . ' ' . $probe_name,
			'status'       => $status,
			'proxy_hostid' => $proxyid,
			'interfaces'   => [DEFAULT_MAIN_INTERFACE]
		}));
	}

	if (opt('rdap-base-url') && opt('rdap-test-domain'))
	{
		set_linked_items_enabled('rdap[', getopt('rr-id'), 1);
	}
	else
	{
		set_linked_items_enabled('rdap[', getopt('rr-id'), 0);
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
		my $result2 = really(get_items_like($host_ref->{'hostid'}, $like, false));	# not a template

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
                (default: taken from DNS)
        --rdds43-test-domain=STRING
                test domain for RDDS monitoring (needed only if rdds servers specified)
        --rdds
                Action with RDDS
                (only effective after switch to Standalone RDAP, default: no)
        --rdap
                Action with RDAP
                (only effective after switch to Standalone RDAP, default: no)
        --help
                display this message
EOF
	exit(1);
}

main();
