#!/usr/bin/perl
#
# - DNS availability test		(data collection)	rsm.dns.udp			(simple, every minute)
#								rsm.dns.tcp			(simple, every 50 minutes)
#								rsm.dns.udp.rtt			(trapper, Proxy)
#								rsm.dns.tcp.rtt			-|-
#								rsm.dns.udp.upd			-|-
#
# - RDDS availability test		(data collection)	rsm.rdds			(simple, every 5 minutes)
#   (also RDDS43 and RDDS80					rsm.rdds.43.ip			(trapper, Proxy)
#   availability at a particular				rsm.rdds.43.rtt			-|-
#   minute)							rsm.rdds.43.upd			-|-
#								rsm.rdds.80.ip			-|-
#								rsm.rdds.80.rtt			-|-
#
# - EPP	availability test		(data collection)	rsm.epp				(simple, every 5 minutes)
# - info RTT							rsm.epp.ip[{$RSM.TLD}]		(trapper, Proxy)
# - login RTT							rsm.epp.rtt[{$RSM.TLD},login]	-|-
# - update RTT							rsm.epp.rtt[{$RSM.TLD},update]	-|-
# - info RTT							rsm.epp.rtt[{$RSM.TLD},info]	-|-
#
# - DNS NS availability			(given minute)		rsm.slv.dns.ns.avail		(trapper, Server)
# - DNS NS monthly availability		(monthly)		rsm.slv.dns.ns.month		-|-
# - DNS monthly resolution RTT		(monthly)		rsm.slv.dns.ns.rtt.udp.month	-|-
# - DNS monthly resolution RTT (TCP)	(monthly, TCP)		rsm.slv.dns.ns.rtt.tcp.month	-|-
# - DNS monthly update time		(monthly)		rsm.slv.dns.ns.upd.month	-|-
# - DNS availability			(given minute)		rsm.slv.dns.avail		-|-
# - DNS rolling week			(rolling week)		rsm.slv.dns.rollweek		-|-
#
# - DNSSEC proper resolution		(given minute)		rsm.slv.dnssec.avail		-|-
# - DNSSEC rolling week			(rolling week)		rsm.slv.dnssec.rollweek		-|-
#
# - RDDS availability			(given minute)		rsm.slv.rdds.avail		-|-
# - RDDS rolling week			(rolling week)		rsm.slv.rdds.rollweek		-|-
# - RDDS43 monthly resolution RTT	(monthly)		rsm.slv.rdds.43.rtt.month	-|-
# - RDDS80 monthly resolution RTT	(monthly)		rsm.slv.rdds.80.rtt.month	-|-
# - RDDS monthly update time		(monthly)		rsm.slv.rdds.upd.month		-|-
#
# - EPP availability			(given minute)		rsm.slv.epp.avail		-|-
# - EPP minutes of downtime		(monthlhy)		rsm.slv.epp.downtime		-|-
# - EPP weekly unavailability		(rolling week)		rsm.slv.epp.rollweek		-|-
# - EPP monthly LOGIN resolution RTT	(monthly)		rsm.slv.epp.rtt.login.month	-|-
# - EPP monthly UPDATE resolution RTT	(monthly)		rsm.slv.epp.rtt.update.month	-|-
# - EPP monthly INFO resolution RTT	(monthly)		rsm.slv.epp.rtt.info.month	-|-

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
use TLD_constants qw(:general :templates :value_types :ec :rsm :slv :config :api);
use TLDs;

sub create_global_macros;
sub create_tld_host($$$$);
sub create_probe_health_tmpl;
sub manage_tld_objects($$$$$);
sub manage_tld_hosts($$);

sub get_nsservers_list($);
sub update_nsservers($$);
sub get_tld_list();
sub get_services($);

my $trigger_rollweek_thresholds = rsm_trigger_rollweek_thresholds;

my $cfg_global_macros = cfg_global_macros;

my ($rsm_groupid, $rsm_hostid);

my ($ns_servers, $root_servers_macros);

my ($main_templateid, $tld_groupid, $tld_type_groupid, $tlds_groupid, $tld_hostid, $probes_groupid, $probes_mon_groupid, $proxy_mon_templateid);

my %OPTS;
my $rv = GetOptions(\%OPTS,
		    "tld=s",
		    "delete!",
		    "disable!",
		    "type=s",
		    "set-type!",
		    "rdds43-servers=s",
		    "rdds80-servers=s",
		    "dns-test-prefix=s",
		    "rdds-test-prefix=s",
		    "ipv4!",
		    "ipv6!",
		    "dns!",
		    "epp!",
		    "rdds!",
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

usage() if ($OPTS{'help'} or not $rv);

validate_input();
lc_options();

# Expect stuff for EPP
my $exp_timeout = 3;
my $exp_command = '/opt/zabbix/bin/rsm_epp_enc';
my $exp_output;

my $config = get_rsm_config();

pfail("SLV scripts path is not specified. Please check configuration file") unless defined $config->{'slv'}->{'path'};

#### Creating cron objects ####
if (defined($OPTS{'setup-cron'})) {
    create_cron_jobs($config->{'slv'}->{'path'});
    print("cron jobs created successfully\n");
    exit;
}

my $server_key = get_rsm_server_key($OPTS{'server-id'} ? $OPTS{'server-id'} : 1);

my $section = $config->{$server_key};

pfail("Zabbix API URL is not specified. Please check configuration file") unless defined $section->{'za_url'};
pfail("Username for Zabbix API is not specified. Please check configuration file") unless defined $section->{'za_user'};
pfail("Password for Zabbix API is not specified. Please check configuration file") unless defined $section->{'za_password'};

my $result = zbx_connect($section->{'za_url'}, $section->{'za_user'}, $section->{'za_password'}, $OPTS{'verbose'});

if ($result ne true) {
    pfail("Could not connect to Zabbix API. ".$result->{'data'});
}

if (defined($OPTS{'set-type'})) {
    if (set_tld_type($OPTS{'tld'}, $OPTS{'type'}) == true)
    {
	print("${OPTS{'tld'}} set to \"${OPTS{'type'}}\"\n");
    }
    else
    {
	print("${OPTS{'tld'}} is already set to \"${OPTS{'type'}}\"\n");
    }
    exit;
}

#### Manage NS + IP server pairs ####
if (defined($OPTS{'get-nsservers-list'})) {
    my $nsservers;

    if (defined($OPTS{'tld'})) {
	$nsservers->{$OPTS{'tld'}} = get_nsservers_list($OPTS{'tld'});
    }
    else {
	my @tlds = get_tld_list();

	foreach my $tld (@tlds) {
	    my $ns = get_nsservers_list($tld);

	    $nsservers->{$tld} = $ns;
	}
    }

    foreach my $tld (sort keys %{$nsservers}) {
	my $ns = $nsservers->{$tld};
	foreach my $type (sort keys %{$ns}) {
	    foreach my $ns_name (sort keys %{$ns->{$type}}) {
		my @ip_list = @{$ns->{$type}->{$ns_name}};
		foreach my $ip (@ip_list) {
	    	    print $tld.",".$type.",".$ns_name.",".$ip."\n";
		}
	    }
	}
    }
    exit;
}

if (defined($OPTS{'list-services'})) {
    my @tlds = get_tld_list();

    my $report;

    my @columns = ('tld_type', '{$RSM.DNS.TESTPREFIX}', '{$RSM.RDDS.NS.STRING}', '{$RSM.RDDS.TESTPREFIX}',
		    '{$RSM.TLD.DNSSEC.ENABLED}', '{$RSM.TLD.EPP.ENABLED}', '{$RSM.TLD.RDDS.ENABLED}');

    foreach my $tld (@tlds) {
	my $services = get_services($tld);

        $report->{$tld} = $services;
    }

    foreach my $tld (sort keys %{$report}) {
	print $tld.",";

	my $count = 0;

	foreach my $column (@columns) {
	    if (defined($report->{$tld}->{$column})) {
		print $report->{$tld}->{$column};
	    }

	    $count++;

	    print "," if (scalar(@columns) != $count);
	}

	print "\n";
    }

    exit;
}

if (defined($OPTS{'update-nsservers'})) {
    # Possible use dig instead of --ns-servers-v4 and ns-servers-v6
    $ns_servers = get_ns_servers($OPTS{'tld'});
    update_nsservers($OPTS{'tld'}, $ns_servers);
    exit;
}

#### Deleting TLD or TLD objects ####
if (defined($OPTS{'delete'})) {
    manage_tld_objects('delete', $OPTS{'tld'}, $OPTS{'dns'}, $OPTS{'epp'}, $OPTS{'rdds'});
    exit;
}

#### Disabling TLD or TLD objects ####
if (defined($OPTS{'disable'})) {
    manage_tld_objects('disable',$OPTS{'tld'}, $OPTS{'dns'}, $OPTS{'epp'}, $OPTS{'rdds'});
    exit;
}

#### Adding new TLD ####
my $proxies = get_proxies_list();

pfail("Cannot find existing proxies") if (scalar(keys %{$proxies}) == 0);

## Creating all global macros##
## Please check the function to change default values of macros ##
create_global_macros();

## Geting some global macros related to item refresh interval ##
## Values are used as item update interval ##
foreach my $macro (keys %{$cfg_global_macros}) {
    $cfg_global_macros->{$macro} = get_global_macro_value($macro);
    pfail('cannot get global macro ', $macro) unless defined($cfg_global_macros->{$macro});
}


# RSM host is required to have history of global configuration changes #
# There are monitored changes of global macros #

$rsm_groupid = create_group(rsm_group);

if (defined($rsm_groupid)) {
    $rsm_hostid = create_host({'groups' => [{'groupid' => $rsm_groupid}],
			      'host' => rsm_host,
			      'interfaces' => [{'type' => INTERFACE_TYPE_AGENT, 'main' => true, 'useip' => true, 'ip'=> '127.0.0.1', 'dns' => '', 'port' => '10050'}]});

    if (defined($rsm_hostid)) {
        # calculated items, configuration history (TODO: rename host to something like config_history)
	create_rsm_items($rsm_hostid);
    }
    else {
	print "Could not create/update '".rsm_host."' host. Items are not created/updated.\n";
    }
}
else {
    print "Could not create/update '".rsm_group."' host group. RSM host is not created/updated.\n";
}

$ns_servers = get_ns_servers($OPTS{'tld'});

pfail("Could not retrive NS servers for '".$OPTS{'tld'}."' TLD") unless (scalar(keys %{$ns_servers}));

$root_servers_macros = update_root_servers($OPTS{'root-servers'});

unless (defined($root_servers_macros)) {
    print "Could not retrive list of root servers or create global macros\n";
}

$main_templateid = create_main_template($OPTS{'tld'}, $ns_servers);

pfail("Main templateid is not defined") unless defined $main_templateid;

$tld_groupid = create_group('TLD '.$OPTS{'tld'});

pfail $tld_groupid->{'data'} if check_api_error($tld_groupid) eq true;

$tlds_groupid = create_group('TLDs');

pfail $tlds_groupid->{'data'} if check_api_error($tlds_groupid) eq true;

$tld_type_groupid = create_group($OPTS{'type'});

pfail $tld_type_groupid->{'data'} if check_api_error($tld_type_groupid) eq true;

$tld_hostid = create_tld_host($OPTS{'tld'}, $tld_groupid, $tlds_groupid, $tld_type_groupid);

$probes_groupid = create_group('Probes');

pfail $probes_groupid->{'data'} if check_api_error($probes_groupid) eq true;

$probes_mon_groupid = create_group('Probes - Mon');

pfail $probes_mon_groupid->{'data'} if check_api_error($probes_mon_groupid) eq true;

$proxy_mon_templateid = create_probe_health_tmpl();

## Creating TLD hosts for each probe ##

foreach my $proxyid (sort keys %{$proxies}) {
    my $probe_name = $proxies->{$proxyid}->{'host'};

    my $status = HOST_STATUS_MONITORED;

    print $proxyid."\n";
    print $proxies->{$proxyid}->{'host'}."\n";

    my $probe_status = $proxies->{$proxyid}->{'status'};

    if ($probe_status == HOST_STATUS_PROXY_ACTIVE) {
	$status = HOST_STATUS_NOT_MONITORED;
    }

    my $proxy_groupid = create_group($probe_name);

    my $probe_templateid;

    if ($probe_status == HOST_STATUS_PROXY_ACTIVE) {
	$probe_templateid = create_probe_template($probe_name, 0, 0, 0, 0);
    }
    else {
	$probe_templateid = create_probe_template($probe_name);
    }


    my $probe_status_templateid = create_probe_status_template($probe_name, $probe_templateid, $root_servers_macros);

    create_host({'groups' => [{'groupid' => $proxy_groupid}, {'groupid' => $probes_groupid}],
                                          'templates' => [{'templateid' => $probe_status_templateid}],
                                          'host' => $probe_name,
                                          'status' => $status,
                                          'proxy_hostid' => $proxyid,
                                          'interfaces' => [{'type' => 1, 'main' => true, 'useip' => true,
							    'ip'=> '127.0.0.1',
							    'dns' => '', 'port' => '10050'}]
		});

    my $hostid = create_host({'groups' => [{'groupid' => $probes_mon_groupid}],
                                          'templates' => [{'templateid' => $proxy_mon_templateid}],
                                          'host' => $probe_name.' - mon',
                                          'status' => $status,
                                          'interfaces' => [{'type' => 1, 'main' => true, 'useip' => true,
                                                            'ip'=> $proxies->{$proxyid}->{'interface'}->{'ip'},
                                                            'dns' => 'tt', 'port' => '10050'}]
            		    });

    create_macro('{$RSM.PROXY_NAME}', $probe_name, $hostid, 1);

    create_host({'groups' => [{'groupid' => $tld_groupid}, {'groupid' => $proxy_groupid}],
                                          'templates' => [{'templateid' => $main_templateid}, {'templateid' => $probe_templateid}],
                                          'host' => $OPTS{'tld'}.' '.$probe_name,
                                          'status' => $status,
                                          'proxy_hostid' => $proxyid,
                                          'interfaces' => [{'type' => 1, 'main' => true, 'useip' => true, 'ip'=> '127.0.0.1', 'dns' => '', 'port' => '10050'}]});
}

create_probe_status_host($probes_mon_groupid);

exit;

########### FUNCTIONS ###############

sub get_ns_servers {
    my $tld = shift;

    if ($OPTS{'ns-servers-v4'} or $OPTS{'ns-servers-v6'}) {
	if ($OPTS{'ns-servers-v4'} and ($OPTS{'ipv4'} == 1 or $OPTS{'update-nsservers'})) {
	    my @nsservers = split(/\s/, $OPTS{'ns-servers-v4'});
	    foreach my $ns (@nsservers) {
		next if ($ns eq '');

		my @entries = split(/,/, $ns);

		my $exists = 0;
		foreach my $ip (@{$ns_servers->{$entries[0]}{'v4'}}) {
		    if ($ip eq $entries[1]) {
			$exists = 1;
			last;
		    }
		}

		push(@{$ns_servers->{$entries[0]}{'v4'}}, $entries[1]) unless ($exists);
	    }
	}

	if ($OPTS{'ns-servers-v6'} and ($OPTS{'ipv6'} == 1 or $OPTS{'update-nsservers'})) {
	    my @nsservers = split(/\s/, $OPTS{'ns-servers-v6'});
	    foreach my $ns (@nsservers) {
		next if ($ns eq '');

		my @entries = split(/,/, $ns);

		my $exists = 0;
		foreach my $ip (@{$ns_servers->{$entries[0]}{'v6'}}) {
		    if ($ip eq $entries[1]) {
			$exists = 1;
			last;
		    }
		}

		push(@{$ns_servers->{$entries[0]}{'v6'}}, $entries[1]) unless ($exists);
	    }
	}
    } else {
	my $nsservers = `dig $tld NS +short`;
	my @nsservers = split(/\n/,$nsservers);

	foreach (my $i = 0;$i<=$#nsservers; $i++) {
	    if ($OPTS{'ipv4'} == 1) {
		my $ipv4 = `dig $nsservers[$i] A +short`;
		my @ipv4 = split(/\n/, $ipv4);

		@{$ns_servers->{$nsservers[$i]}{'v4'}} = @ipv4 if scalar @ipv4;
	    }

	    if ($OPTS{'ipv6'} == 1) {
		my $ipv6 = `dig $nsservers[$i] AAAA +short` if $OPTS{'ipv6'};
		my @ipv6 = split(/\n/, $ipv6);

		@{$ns_servers->{$nsservers[$i]}{'v6'}} = @ipv6 if scalar @ipv6;
	    }
	}
    }

    return $ns_servers;
}

sub create_item_dns_rtt {
    my $ns_name = shift;
    my $ip = shift;
    my $templateid = shift;
    my $template_name = shift;
    my $proto = shift;
    my $ipv = shift;

    pfail("undefined template ID passed to create_item_dns_rtt()") unless ($templateid);
    pfail("no protocol parameter specified to create_item_dns_rtt()") unless ($proto);

    my $proto_lc = lc($proto);
    my $proto_uc = uc($proto);

    my $item_key = 'rsm.dns.'.$proto_lc.'.rtt[{$RSM.TLD},'.$ns_name.','.$ip.']';

    my $options = {'name' => 'DNS RTT of $2 ($3) ('.$proto_uc.')',
                                              'key_'=> $item_key,
                                              'hostid' => $templateid,
                                              'applications' => [get_application_id('DNS RTT ('.$proto_uc.')', $templateid)],
                                              'type' => 2, 'value_type' => 0,
					      'status' => ITEM_STATUS_ACTIVE,
                                              'valuemapid' => rsm_value_mappings->{'rsm_dns'}};

    create_item($options);

    return;
}

sub create_slv_item {
    my $name = shift;
    my $key = shift;
    my $hostid = shift;
    my $value_type = shift;
    my $applicationids = shift;

    my $options;
    if ($value_type == VALUE_TYPE_AVAIL)
    {
	$options = {'name' => $name,
                                              'key_'=> $key,
                                              'hostid' => $hostid,
                                              'type' => 2, 'value_type' => 3,
					      'applications' => $applicationids,
					    'status' => ITEM_STATUS_ACTIVE,
					      'valuemapid' => rsm_value_mappings->{'rsm_avail'}};
    }
    elsif ($value_type == VALUE_TYPE_NUM)
    {
	$options = {'name' => $name,
                                              'key_'=> $key,
                                              'hostid' => $hostid,
                                              'type' => 2, 'value_type' => 3,
					    'status' => ITEM_STATUS_ACTIVE,
					      'applications' => $applicationids};
    }
    elsif ($value_type == VALUE_TYPE_PERC) {
	$options = {'name' => $name,
                                              'key_'=> $key,
                                              'hostid' => $hostid,
                                              'type' => 2, 'value_type' => 0,
                                              'applications' => $applicationids,
					    'status' => ITEM_STATUS_ACTIVE,
					      'units' => '%'};
    }
    else {
	pfail("Unknown value type $value_type.");
    }


    return create_item($options);
}

sub create_item_dns_udp_upd {
    my $ns_name = shift;
    my $ip = shift;
    my $templateid = shift;
    my $template_name = shift;

    my $proto_uc = 'UDP';

    my $options = {'name' => 'DNS update time of $2 ($3)',
                                              'key_'=> 'rsm.dns.udp.upd[{$RSM.TLD},'.$ns_name.','.$ip.']',
                                              'hostid' => $templateid,
                                              'applications' => [get_application_id('DNS RTT ('.$proto_uc.')', $templateid)],
                                              'type' => 2, 'value_type' => 0,
                                              'valuemapid' => rsm_value_mappings->{'rsm_dns'},
		                              'status' => (defined($OPTS{'epp-servers'}) ? 0 : 1)};
    return create_item($options);
}

sub create_items_dns {
    my $templateid = shift;
    my $template_name = shift;

    my $proto = 'tcp';
    my $proto_uc = uc($proto);
    my $item_key = 'rsm.dns.'.$proto.'[{$RSM.TLD}]';

    my $options = {'name' => 'Number of working DNS Name Servers of $1 ('.$proto_uc.')',
                                              'key_'=> $item_key,
                                              'hostid' => $templateid,
                                              'applications' => [get_application_id('DNS ('.$proto_uc.')', $templateid)],
                                              'type' => 3, 'value_type' => 3,
                                              'delay' => $cfg_global_macros->{'{$RSM.DNS.TCP.DELAY}'}};

    create_item($options);

    $proto = 'udp';
    $proto_uc = uc($proto);
    $item_key = 'rsm.dns.'.$proto.'[{$RSM.TLD}]';

    $options = {'name' => 'Number of working DNS Name Servers of $1 ('.$proto_uc.')',
                                              'key_'=> $item_key,
                                              'hostid' => $templateid,
                                              'applications' => [get_application_id('DNS ('.$proto_uc.')', $templateid)],
                                              'type' => 3, 'value_type' => 3,
                                              'delay' => $cfg_global_macros->{'{$RSM.DNS.UDP.DELAY}'}, 'valuemapid' => rsm_value_mappings->{'rsm_dns'}};

    create_item($options);
}

sub create_items_rdds {
    my $templateid = shift;
    my $template_name = shift;

    my $applicationid_43 = get_application_id('RDDS43', $templateid);
    my $applicationid_80 = get_application_id('RDDS80', $templateid);

    my $item_key = 'rsm.rdds.43.ip[{$RSM.TLD}]';

    my $options = {'name' => 'RDDS43 IP of $1',
                                              'key_'=> $item_key,
                                              'hostid' => $templateid,
                                              'applications' => [$applicationid_43],
                                              'type' => 2, 'value_type' => 1,
                                              'valuemapid' => rsm_value_mappings->{'rsm_rdds_rttudp'}};
    create_item($options);

    $item_key = 'rsm.rdds.43.rtt[{$RSM.TLD}]';

    $options = {'name' => 'RDDS43 RTT of $1',
                                              'key_'=> $item_key,
                                              'hostid' => $templateid,
                                              'applications' => [$applicationid_43],
                                              'type' => 2, 'value_type' => 0,
                                              'valuemapid' => rsm_value_mappings->{'rsm_rdds_rttudp'}};
    create_item($options);

    if (defined($OPTS{'epp-servers'})) {
	$item_key = 'rsm.rdds.43.upd[{$RSM.TLD}]';

	$options = {'name' => 'RDDS43 update time of $1',
		    'key_'=> $item_key,
		    'hostid' => $templateid,
		    'applications' => [$applicationid_43],
		    'type' => 2, 'value_type' => 0,
		    'valuemapid' => rsm_value_mappings->{'rsm_rdds_rttudp'},
		    'status' => 0};
	create_item($options);
    }

    $item_key = 'rsm.rdds.80.ip[{$RSM.TLD}]';

    $options = {'name' => 'RDDS80 IP of $1',
                                              'key_'=> $item_key,
                                              'hostid' => $templateid,
                                              'applications' => [$applicationid_80],
                                              'type' => 2, 'value_type' => 1};
    create_item($options);

    $item_key = 'rsm.rdds.80.rtt[{$RSM.TLD}]';

    $options = {'name' => 'RDDS80 RTT of $1',
                                              'key_'=> $item_key,
                                              'hostid' => $templateid,
                                              'applications' => [$applicationid_80],
                                              'type' => 2, 'value_type' => 0,
                                              'valuemapid' => rsm_value_mappings->{'rsm_rdds_rttudp'}};
    create_item($options);

    $item_key = 'rsm.rdds[{$RSM.TLD},"'.$OPTS{'rdds43-servers'}.'","'.$OPTS{'rdds80-servers'}.'"]';

    $options = {'name' => 'RDDS availability',
                                              'key_'=> $item_key,
                                              'hostid' => $templateid,
                                              'applications' => [get_application_id('RDDS', $templateid)],
                                              'type' => 3, 'value_type' => 3,
					      'delay' => $cfg_global_macros->{'{$RSM.RDDS.DELAY}'},
                                              'valuemapid' => rsm_value_mappings->{'rsm_rdds_avail'}};
    create_item($options);
}

sub create_items_epp {
    my $templateid = shift;
    my $template_name = shift;

    my $applicationid = get_application_id('EPP', $templateid);

    my ($item_key, $options);

    $item_key = 'rsm.epp[{$RSM.TLD},"'.$OPTS{'epp-servers'}.'"]';

    $options = {'name' => 'EPP service availability at $1 ($2)',
		'key_'=> $item_key,
		'hostid' => $templateid,
		'applications' => [$applicationid],
		'type' => 3, 'value_type' => 3,
		'delay' => $cfg_global_macros->{'{$RSM.EPP.DELAY}'}, 'valuemapid' => rsm_value_mappings->{'rsm_avail'}};

    create_item($options);

    $item_key = 'rsm.epp.ip[{$RSM.TLD}]';

    $options = {'name' => 'EPP IP of $1',
		'key_'=> $item_key,
		'hostid' => $templateid,
		'applications' => [$applicationid],
		'type' => 2, 'value_type' => 1};

    create_item($options);

    $item_key = 'rsm.epp.rtt[{$RSM.TLD},login]';

    $options = {'name' => 'EPP $2 command RTT of $1',
		'key_'=> $item_key,
		'hostid' => $templateid,
		'applications' => [$applicationid],
		'type' => 2, 'value_type' => 0,
		'valuemapid' => rsm_value_mappings->{'rsm_epp'}};

    create_item($options);

    $item_key = 'rsm.epp.rtt[{$RSM.TLD},update]';

    $options = {'name' => 'EPP $2 command RTT of $1',
		'key_'=> $item_key,
		'hostid' => $templateid,
		'applications' => [$applicationid],
		'type' => 2, 'value_type' => 0,
		'valuemapid' => rsm_value_mappings->{'rsm_epp'}};

    create_item($options);

    $item_key = 'rsm.epp.rtt[{$RSM.TLD},info]';

    $options = {'name' => 'EPP $2 command RTT of $1',
		'key_'=> $item_key,
		'hostid' => $templateid,
		'applications' => [$applicationid],
		'type' => 2, 'value_type' => 0,
		'valuemapid' => rsm_value_mappings->{'rsm_epp'}};

    create_item($options);
}


sub trim
{
    $_[0] =~ s/^\s*//g;
    $_[0] =~ s/\s*$//g;
}

sub get_sensdata
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

sub exp_get_keysalt
{
    my $self = shift;

    if ($self->match() =~ m/^([^\s]+\|[^\s]+)/)
    {
	$exp_output = $1;
    }
}

sub get_encrypted_passwd
{
    my $keysalt = shift;
    my $passphrase = shift;
    my $passwd = shift;

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

sub get_encrypted_privkey
{
    my $keysalt = shift;
    my $passphrase = shift;
    my $file = shift;

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

sub read_file {
    my $file = shift;

    my $contents = do {
	local $/ = undef;
	open my $fh, "<", $file or pfail("could not open $file: $!");
	<$fh>;
    };

    return $contents;
}

sub get_md5 {
    my $file = shift;

    my $contents = do {
        local $/ = undef;
        open(my $fh, "<", $file) or pfail("cannot open $file: $!");
        <$fh>;
    };

    my $index = index($contents, "-----BEGIN CERTIFICATE-----");
    pfail("specified file $file does not contain line \"-----BEGIN CERTIFICATE-----\"") if ($index == -1);

    return md5_hex(substr($contents, $index));
}

sub create_main_template {
    my $tld = shift;
    my $ns_servers = shift;

    my $template_name = 'Template '.$tld;

    my $templateid = create_template($template_name);

    pfail("Could not create main template for '".$tld."' TLD. ".$templateid->{'data'}) if check_api_error($templateid) eq true;

    my $delay = 300;
    my $appid = get_application_id('Configuration', $templateid);
    my ($options, $key);

    foreach my $m ('RSM.IP4.ENABLED', 'RSM.IP6.ENABLED') {
        $key = 'probe.configvalue['.$m.']';

        $options = {'name' => 'Value of $1 variable',
                    'key_'=> $key,
                    'hostid' => $templateid,
                    'applications' => [$appid],
                    'params' => '{$'.$m.'}',
                    'delay' => $delay,
                    'type' => ITEM_TYPE_CALCULATED, 'value_type' => ITEM_VALUE_TYPE_UINT64};

        my $itemid = create_item($options);

	print $itemid->{'data'}."\n" if check_api_error($itemid) eq true;
    }

    foreach my $ns_name (sort keys %{$ns_servers}) {
	print $ns_name."\n";

        my @ipv4 = defined($ns_servers->{$ns_name}{'v4'}) ? @{$ns_servers->{$ns_name}{'v4'}} : undef;
	my @ipv6 = defined($ns_servers->{$ns_name}{'v6'}) ? @{$ns_servers->{$ns_name}{'v6'}} : undef;

        foreach (my $i_ipv4 = 0; $i_ipv4 <= $#ipv4; $i_ipv4++) {
	    next unless defined $ipv4[$i_ipv4];
	    print "	--v4     $ipv4[$i_ipv4]\n";

            create_item_dns_rtt($ns_name, $ipv4[$i_ipv4], $templateid, $template_name, "tcp", '4');
	    create_item_dns_rtt($ns_name, $ipv4[$i_ipv4], $templateid, $template_name, "udp", '4');
	    if (defined($OPTS{'epp-servers'})) {
    		create_item_dns_udp_upd($ns_name, $ipv4[$i_ipv4], $templateid);
    	    }
        }

	foreach (my $i_ipv6 = 0; $i_ipv6 <= $#ipv6; $i_ipv6++) {
	    next unless defined $ipv6[$i_ipv6];
    	    print "	--v6     $ipv6[$i_ipv6]\n";

	    create_item_dns_rtt($ns_name, $ipv6[$i_ipv6], $templateid, $template_name, "tcp", '6');
    	    create_item_dns_rtt($ns_name, $ipv6[$i_ipv6], $templateid, $template_name, "udp", '6');
	    if (defined($OPTS{'epp-servers'})) {
    		create_item_dns_udp_upd($ns_name, $ipv6[$i_ipv6], $templateid);
	    }
        }
    }

    create_items_dns($templateid, $template_name);
    create_items_rdds($templateid, $template_name) if (defined($OPTS{'rdds43-servers'}));
    create_items_epp($templateid, $template_name) if (defined($OPTS{'epp-servers'}));

    create_macro('{$RSM.TLD}', $tld, $templateid);
    create_macro('{$RSM.DNS.TESTPREFIX}', $OPTS{'dns-test-prefix'}, $templateid);
    create_macro('{$RSM.RDDS.TESTPREFIX}', $OPTS{'rdds-test-prefix'}, $templateid) if (defined($OPTS{'rdds-test-prefix'}));
    create_macro('{$RSM.RDDS.NS.STRING}', defined($OPTS{'rdds-ns-string'}) ? $OPTS{'rdds-ns-string'} : cfg_default_rdds_ns_string, $templateid);
    create_macro('{$RSM.TLD.DNSSEC.ENABLED}', defined($OPTS{'dnssec'}) ? 1 : 0, $templateid, true);
    create_macro('{$RSM.TLD.RDDS.ENABLED}', defined($OPTS{'rdds43-servers'}) ? 1 : 0, $templateid, true);
    create_macro('{$RSM.TLD.EPP.ENABLED}', defined($OPTS{'epp-servers'}) ? 1 : 0, $templateid, true);

    if ($OPTS{'epp-servers'})
    {
	my $m = '{$RSM.EPP.KEYSALT}';
	my $keysalt = get_global_macro_value($m);
	pfail('cannot get macro ', $m) unless defined($keysalt);
	trim($keysalt);
	pfail("global macro $m must conatin |") unless ($keysalt =~ m/\|/);

	if ($OPTS{'epp-commands'}) {
	    create_macro('{$RSM.EPP.COMMANDS}', $OPTS{'epp-commands'}, $templateid, 1);
	} else {
	    create_macro('{$RSM.EPP.COMMANDS}', '/opt/test-sla/epp-commands/'.$tld, $templateid);
	}
	create_macro('{$RSM.EPP.USER}', $OPTS{'epp-user'}, $templateid, 1);
	create_macro('{$RSM.EPP.CERT}', encode_base64(read_file($OPTS{'epp-cert'}), ''),  $templateid, 1);
	create_macro('{$RSM.EPP.SERVERID}', $OPTS{'epp-serverid'}, $templateid, 1);
	create_macro('{$RSM.EPP.TESTPREFIX}', $OPTS{'epp-test-prefix'}, $templateid, 1);
	create_macro('{$RSM.EPP.SERVERCERTMD5}', get_md5($OPTS{'epp-servercert'}), $templateid, 1);

	my $passphrase = get_sensdata("Enter EPP secret key passphrase: ");
	my $passwd = get_sensdata("Enter EPP password: ");
	create_macro('{$RSM.EPP.PASSWD}', get_encrypted_passwd($keysalt, $passphrase, $passwd), $templateid, 1);
	$passwd = undef;
	create_macro('{$RSM.EPP.PRIVKEY}', get_encrypted_privkey($keysalt, $passphrase, $OPTS{'epp-privkey'}), $templateid, 1);
	$passphrase = undef;

	print("EPP data saved successfully.\n");
    }

    return $templateid;
}

sub create_all_slv_ns_items {
    my $ns_name = shift;
    my $ip = shift;
    my $hostid = shift;

    create_slv_item('% of successful monthly DNS resolution RTT (UDP): $1 ($2)', 'rsm.slv.dns.ns.rtt.udp.month['.$ns_name.','.$ip.']', $hostid, VALUE_TYPE_PERC, [get_application_id(APP_SLV_MONTHLY, $hostid)]);
    create_slv_item('% of successful monthly DNS resolution RTT (TCP): $1 ($2)', 'rsm.slv.dns.ns.rtt.tcp.month['.$ns_name.','.$ip.']', $hostid, VALUE_TYPE_PERC, [get_application_id(APP_SLV_MONTHLY, $hostid)]);
    create_slv_item('% of successful monthly DNS update time: $1 ($2)', 'rsm.slv.dns.ns.upd.month['.$ns_name.','.$ip.']', $hostid, VALUE_TYPE_PERC, [get_application_id(APP_SLV_MONTHLY, $hostid)]) if (defined($OPTS{'epp-servers'}));
    create_slv_item('DNS NS availability: $1 ($2)', 'rsm.slv.dns.ns.avail['.$ns_name.','.$ip.']', $hostid, VALUE_TYPE_AVAIL, [get_application_id(APP_SLV_PARTTEST, $hostid)]);
    create_slv_item('DNS NS minutes of downtime: $1 ($2)', 'rsm.slv.dns.ns.downtime['.$ns_name.','.$ip.']', $hostid, VALUE_TYPE_NUM, [get_application_id(APP_SLV_CURMON, $hostid)]);
    create_slv_item('DNS NS probes that returned results: $1 ($2)', 'rsm.slv.dns.ns.results['.$ns_name.','.$ip.']', $hostid, VALUE_TYPE_NUM, [get_application_id(APP_SLV_CURMON, $hostid)]);
    create_slv_item('DNS NS probes that returned positive results: $1 ($2)', 'rsm.slv.dns.ns.positive['.$ns_name.','.$ip.']', $hostid, VALUE_TYPE_NUM, [get_application_id(APP_SLV_CURMON, $hostid)]);
    create_slv_item('DNS NS positive results by SLA: $1 ($2)', 'rsm.slv.dns.ns.sla['.$ns_name.','.$ip.']', $hostid, VALUE_TYPE_NUM, [get_application_id(APP_SLV_CURMON, $hostid)]);
    create_slv_item('% of monthly DNS NS availability: $1 ($2)', 'rsm.slv.dns.ns.month['.$ns_name.','.$ip.']', $hostid, VALUE_TYPE_PERC, [get_application_id(APP_SLV_MONTHLY, $hostid)]);
}

sub create_slv_ns_items {
    my $ns_servers = shift;
    my $hostid = shift;

    foreach my $ns_name (sort keys %{$ns_servers}) {
        my @ipv4 = defined($ns_servers->{$ns_name}{'v4'}) ? @{$ns_servers->{$ns_name}{'v4'}} : undef;
	my @ipv6 = defined($ns_servers->{$ns_name}{'v6'}) ? @{$ns_servers->{$ns_name}{'v6'}} : undef;

        foreach (my $i_ipv4 = 0; $i_ipv4 <= $#ipv4; $i_ipv4++) {
	    next unless defined $ipv4[$i_ipv4];

# todo phase 1: DNS NS are not currently used
#	    create_all_slv_ns_items($ns_name, $ipv4[$i_ipv4], $hostid);
        }

	foreach (my $i_ipv6 = 0; $i_ipv6 <= $#ipv6; $i_ipv6++) {
	    next unless defined $ipv6[$i_ipv6];

# todo phase 1: DNS NS are not currently used
#	    create_all_slv_ns_items($ns_name, $ipv6[$i_ipv6], $hostid);
        }
    }
}

sub create_slv_items {
    my $ns_servers = shift;
    my $hostid = shift;
    my $host_name = shift;

    create_slv_ns_items($ns_servers, $hostid);

    create_slv_item('DNS availability', 'rsm.slv.dns.avail', $hostid, VALUE_TYPE_AVAIL, [get_application_id(APP_SLV_PARTTEST, $hostid)]);
    create_slv_item('DNS minutes of downtime', 'rsm.slv.dns.downtime', $hostid, VALUE_TYPE_NUM, [get_application_id(APP_SLV_CURMON, $hostid)]);

    my $options;

    create_avail_trigger('DNS', $host_name);

    create_slv_item('DNS weekly unavailability', 'rsm.slv.dns.rollweek', $hostid, VALUE_TYPE_PERC, [get_application_id(APP_SLV_ROLLWEEK, $hostid)]);

    my $depend_down;
    my $created;

    foreach my $position (sort keys %{$trigger_rollweek_thresholds}) {
	my $threshold = $trigger_rollweek_thresholds->{$position}->{'threshold'};
	my $priority = $trigger_rollweek_thresholds->{$position}->{'priority'};
        next if ($threshold eq 0);

        my $result = create_rollweek_trigger('DNS', $host_name, $threshold, $priority, \$created);

	my $triggerid = $result->{'triggerids'}[0];

        if ($created && defined($depend_down)) {
            add_dependency($triggerid, $depend_down);
        }

        $depend_down = $triggerid;
    }

    undef($depend_down);

    if (defined($OPTS{'dnssec'})) {
	create_slv_item('DNSSEC availability', 'rsm.slv.dnssec.avail', $hostid, VALUE_TYPE_AVAIL, [get_application_id(APP_SLV_PARTTEST, $hostid)]);

	create_avail_trigger('DNSSEC', $host_name);

	create_slv_item('DNSSEC weekly unavailability', 'rsm.slv.dnssec.rollweek', $hostid, VALUE_TYPE_PERC, [get_application_id(APP_SLV_ROLLWEEK, $hostid)]);

        my $depend_down;
	my $created;

	foreach my $position (sort keys %{$trigger_rollweek_thresholds}) {
    	    my $threshold = $trigger_rollweek_thresholds->{$position}->{'threshold'};
    	    my $priority = $trigger_rollweek_thresholds->{$position}->{'priority'};
    	    next if ($threshold eq 0);

	    my $result = create_rollweek_trigger('DNSSEC', $host_name, $threshold, $priority, \$created);

    	    my $triggerid = $result->{'triggerids'}[0];

	    if ($created && defined($depend_down)) {
    	        add_dependency($triggerid, $depend_down);
    	    }

    	    $depend_down = $triggerid;
        }

	undef($depend_down);
    }


    if (defined($OPTS{'rdds43-servers'})) {
	create_slv_item('RDDS availability', 'rsm.slv.rdds.avail', $hostid, VALUE_TYPE_AVAIL, [get_application_id(APP_SLV_PARTTEST, $hostid)]);
	create_slv_item('RDDS minutes of downtime', 'rsm.slv.rdds.downtime', $hostid, VALUE_TYPE_NUM, [get_application_id(APP_SLV_CURMON, $hostid)]);

	create_avail_trigger('RDDS', $host_name);

	create_slv_item('RDDS weekly unavailability', 'rsm.slv.rdds.rollweek', $hostid, VALUE_TYPE_PERC, [get_application_id(APP_SLV_ROLLWEEK, $hostid)]);

        my $depend_down;
	my $created;

	foreach my $position (sort keys %{$trigger_rollweek_thresholds}) {
    	    my $threshold = $trigger_rollweek_thresholds->{$position}->{'threshold'};
    	    my $priority = $trigger_rollweek_thresholds->{$position}->{'priority'};
    	    next if ($threshold eq 0);

	    my $result = create_rollweek_trigger('RDDS', $host_name, $threshold, $priority, \$created);

    	    my $triggerid = $result->{'triggerids'}[0];

	    if ($created && defined($depend_down)) {
    	        add_dependency($triggerid, $depend_down);
    	    }

    	    $depend_down = $triggerid;
        }

	undef($depend_down);


# todo phase 1: DNS NS are not currently used
#	create_slv_item('% of successful monthly RDDS43 resolution RTT', 'rsm.slv.rdds.43.rtt.month', $hostid, VALUE_TYPE_PERC, [get_application_id(APP_SLV_MONTHLY, $hostid)]);
#	create_slv_item('% of successful monthly RDDS80 resolution RTT', 'rsm.slv.rdds.80.rtt.month', $hostid, VALUE_TYPE_PERC, [get_application_id(APP_SLV_MONTHLY, $hostid)]);
#	create_slv_item('% of successful monthly RDDS update time', 'rsm.slv.rdds.upd.month', $hostid, VALUE_TYPE_PERC, [get_application_id(APP_SLV_MONTHLY, $hostid)]) if (defined($OPTS{'epp-servers'}));
    }

    if (defined($OPTS{'epp-servers'})) {
	create_slv_item('EPP availability', 'rsm.slv.epp.avail', $hostid, VALUE_TYPE_AVAIL, [get_application_id(APP_SLV_PARTTEST, $hostid)]);
	create_slv_item('EPP minutes of downtime', 'rsm.slv.epp.downtime', $hostid, VALUE_TYPE_NUM, [get_application_id(APP_SLV_CURMON, $hostid)]);
	create_slv_item('EPP weekly unavailability', 'rsm.slv.epp.rollweek', $hostid, VALUE_TYPE_PERC, [get_application_id(APP_SLV_ROLLWEEK, $hostid)]);

# todo phase 1: DNS NS are not currently used
#	create_slv_item('% of successful monthly EPP LOGIN resolution RTT', 'rsm.slv.epp.rtt.login.month', $hostid, VALUE_TYPE_PERC, [get_application_id(APP_SLV_MONTHLY, $hostid)]);
#	create_slv_item('% of successful monthly EPP UPDATE resolution RTT', 'rsm.slv.epp.rtt.update.month', $hostid, VALUE_TYPE_PERC, [get_application_id(APP_SLV_MONTHLY, $hostid)]);
#	create_slv_item('% of successful monthly EPP INFO resolution RTT', 'rsm.slv.epp.rtt.info.month', $hostid, VALUE_TYPE_PERC, [get_application_id(APP_SLV_MONTHLY, $hostid)]);

	create_avail_trigger('EPP', $host_name);

        my $depend_down;
	my $created;

	foreach my $position (sort keys %{$trigger_rollweek_thresholds}) {
    	    my $threshold = $trigger_rollweek_thresholds->{$position}->{'threshold'};
    	    my $priority = $trigger_rollweek_thresholds->{$position}->{'priority'};
    	    next if ($threshold eq 0);

	    my $result = create_rollweek_trigger('EPP', $host_name, $threshold, $priority, \$created);

    	    my $triggerid = $result->{'triggerids'}[0];

	    if ($created && defined($depend_down)) {
    	        add_dependency($triggerid, $depend_down);
    	    }

    	    $depend_down = $triggerid;
        }

	undef($depend_down);
    }
}

# calculated items, configuration history (TODO: rename host to something like config_history)
sub create_rsm_items {
    my $hostid = shift;

    my $options;
    my $appid = get_application_id('Configuration', $hostid);

    my $macros = {
		&TIME_MINUTE => [
			'RSM.INCIDENT.DNS.FAIL',
    			'RSM.INCIDENT.DNS.RECOVER',
		        'RSM.INCIDENT.DNSSEC.FAIL',
		        'RSM.INCIDENT.DNSSEC.RECOVER',
		        'RSM.INCIDENT.RDDS.FAIL',
		        'RSM.INCIDENT.RDDS.RECOVER',
		        'RSM.INCIDENT.EPP.FAIL',
		        'RSM.INCIDENT.EPP.RECOVER',
		        'RSM.DNS.UDP.DELAY',
		        'RSM.RDDS.DELAY',
		        'RSM.EPP.DELAY',
		        'RSM.DNS.UDP.RTT.HIGH',
		        'RSM.DNS.AVAIL.MINNS',
		        'RSM.DNS.ROLLWEEK.SLA',
		        'RSM.RDDS.ROLLWEEK.SLA',
		        'RSM.EPP.ROLLWEEK.SLA'
		],
		&TIME_DAY => [
			'RSM.SLV.DNS.UDP.RTT',
		        'RSM.SLV.DNS.TCP.RTT',
		        'RSM.SLV.NS.AVAIL',
		        'RSM.SLV.RDDS43.RTT',
		        'RSM.SLV.RDDS80.RTT',
		        'RSM.SLV.RDDS.UPD',
		        'RSM.SLV.DNS.NS.UPD',
		        'RSM.SLV.EPP.LOGIN',
		        'RSM.SLV.EPP.UPDATE',
		        'RSM.SLV.EPP.INFO'
		]};

    foreach my $delay (keys %{$macros}) {
	foreach my $macro (@{$macros->{$delay}}) {
	    $options = {'name' => '$1 value',
                   'key_'=> 'rsm.configvalue['.$macro.']',
                   'hostid' => $hostid,
                   'applications' => [$appid],
                   'params' => '{$'.$macro.'}',
                   'delay' => $delay,
                   'type' => ITEM_TYPE_CALCULATED, 'value_type' => ITEM_VALUE_TYPE_UINT64};

    	    my $itemid = create_item($options);

	    pfail($itemid->{'data'}) if check_api_error($itemid) eq true;
	}
    }
}

sub usage {
    my ($opt_name, $opt_value) = @_;

    my $cfg_default_rdds_ns_string = cfg_default_rdds_ns_string;

    print <<EOF;

    Usage: $0 [options]

Required options

        --tld=STRING
                TLD name
        --dns-test-prefix=STRING
                domain test prefix for DNS monitoring (specify '*randomtld*' for root servers monitoring)

Other options
        --delete
                delete specified TLD
        --disable
                disable specified TLD
	--list-services
		list services of each TLD, the output is comma-separated list:
                <TLD>,<TLD-TYPE>,<RDDS.DNS.TESTPREFIX>,<RDDS.NS.STRING>,<RDDS.TESTPREFIX>,<TLD.DNSSEC.ENABLED>,<TLD.EPP.ENABLED>,<TLD.RDDS.ENABLED>
	--get-nsservers-list
		CSV formatted list of NS + IP server pairs for specified TLD
	--update-nsservers
		update all NS + IP pairs for specified TLD. --ns-servers-v4 or/and --ns-servers-v6 is mandatory in this case
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
		(default: get the list from local resolver)
        --ns-servers-v6=STRING
                list of IPv6 name servers separated by space (name and IP separated by comma): "NAME,IP[ NAME,IP2 ...]"
		(default: get the list from local resolver)
        --rdds43-servers=STRING
                list of RDDS43 servers separated by comma: "NAME1,NAME2,..."
        --rdds80-servers=STRING
                list of RDDS80 servers separated by comma: "NAME1,NAME2,..."
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
		(default: $cfg_default_rdds_ns_string)
        --root-servers=STRING
                list of IPv4 and IPv6 root servers separated by comma and semicolon: "v4IP1[,v4IP2,...][;v6IP1[,v6IP2,...]]"
                (default: taken from DNS)
        --server-id=STRING
                ID of Zabbix server (default: 1)
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
	--rdds
		Action with RDDS
		(default: no)
        --help
                display this message
EOF
exit(1);
}

sub validate_input {
    my $msg = "";

    return if (defined($OPTS{'setup-cron'}));

    $msg  = "TLD must be specified (--tld)\n" if (!defined($OPTS{'tld'}) and !defined($OPTS{'get-nsservers-list'}) and !defined($OPTS{'list-services'}));

    if (!defined($OPTS{'delete'}) and !defined($OPTS{'disable'}) and !defined($OPTS{'get-nsservers-list'}) and !defined($OPTS{'update-nsservers'}) and !defined($OPTS{'list-services'}))
    {
	    if (!defined($OPTS{'type'}))
	    {
		    $msg .= "type (--type) of TLD must be specified: @{[TLD_TYPE_G]}, @{[TLD_TYPE_CC]}, @{[TLD_TYPE_OTHER]} or @{[TLD_TYPE_TEST]}\n";
	    }
	    elsif ($OPTS{'type'} ne TLD_TYPE_G and $OPTS{'type'} ne TLD_TYPE_CC and $OPTS{'type'} ne TLD_TYPE_OTHER and $OPTS{'type'} ne TLD_TYPE_TEST)
	    {
		    $msg .= "invalid TLD type \"${OPTS{'type'}}\", type must be one of: @{[TLD_TYPE_G]}, @{[TLD_TYPE_CC]}, @{[TLD_TYPE_OTHER]} or @{[TLD_TYPE_TEST]}\n";
	    }
    }

    if (defined($OPTS{'set-type'}))
    {
	    unless ($msg eq "") {
		    print($msg);
		    usage();
	    }
	    return;
    }

    $msg .= "at least one IPv4 or IPv6 must be enabled (--ipv4 or --ipv6)\n" if (!defined($OPTS{'delete'}) and !defined($OPTS{'disable'})
										and !defined($OPTS{'ipv4'}) and !defined($OPTS{'ipv6'})
										and !defined($OPTS{'get-nsservers-list'}) and !defined($OPTS{'update-nsservers'})
										and !defined($OPTS{'list-services'}));
    $msg .= "DNS test prefix must be specified (--dns-test-prefix)\n" if (!defined($OPTS{'delete'}) and !defined($OPTS{'disable'}) and !defined($OPTS{'dns-test-prefix'})
									    and !defined($OPTS{'get-nsservers-list'}) and !defined($OPTS{'update-nsservers'})
									    and !defined($OPTS{'list-services'}));
    $msg .= "RDDS test prefix must be specified (--rdds-test-prefix)\n" if ((defined($OPTS{'rdds43-servers'}) and !defined($OPTS{'rdds-test-prefix'})) or
									    (defined($OPTS{'rdds80-servers'}) and !defined($OPTS{'rdds-test-prefix'})));
    $msg .= "none or both --rdds43-servers and --rdds80-servers must be specified\n" if ((defined($OPTS{'rdds43-servers'}) and !defined($OPTS{'rdds80-servers'})) or
											 (defined($OPTS{'rdds80-servers'}) and !defined($OPTS{'rdds43-servers'})));

    if ($OPTS{'epp-servers'}) {
	$msg .= "EPP user must be specified (--epp-user)\n" unless ($OPTS{'epp-user'});
	$msg .= "EPP Client certificate file must be specified (--epp-cert)\n" unless ($OPTS{'epp-cert'});
	$msg .= "EPP Client private key file must be specified (--epp-privkey)\n" unless ($OPTS{'epp-privkey'});
	$msg .= "EPP server ID must be specified (--epp-serverid)\n" unless ($OPTS{'epp-serverid'});
	$msg .= "EPP domain test prefix must be specified (--epp-test-prefix)\n" unless ($OPTS{'epp-serverid'});
	$msg .= "EPP Server certificate file must be specified (--epp-servercert)\n" unless ($OPTS{'epp-servercert'});
    }

    $OPTS{'ipv4'} = 0 if (defined($OPTS{'update-nsservers'}));
    $OPTS{'ipv6'} = 0 if (defined($OPTS{'update-nsservers'}));

    $OPTS{'dns'} = 0 unless defined $OPTS{'dns'};
    $OPTS{'rdds'} = 0 unless defined $OPTS{'rdds'};
    $OPTS{'epp'} = 0 unless defined $OPTS{'epp'};

    unless ($msg eq "") {
	print($msg);
	usage();
    }
}

sub lc_options {
    foreach my $key (keys(%OPTS))
    {
	foreach ("tld", "rdds43-servers", "rdds80-servers=s", "epp-servers", "ns-servers-v4", "ns-servers-v6")
	{
	    $OPTS{$_} = lc($OPTS{$_}) if ($key eq $_);
	}
    }
}

sub add_default_actions() {

}

sub create_global_macros() {
    create_macro('{$RSM.IP4.MIN.PROBE.ONLINE}', 2, undef);
    create_macro('{$RSM.IP6.MIN.PROBE.ONLINE}', 2, undef);

    create_macro('{$RSM.PROBE.MAX.OFFLINE}', '1h', undef);

    create_macro('{$RSM.IP4.MIN.SERVERS}', 4, undef);
    create_macro('{$RSM.IP6.MIN.SERVERS}', 4, undef);
    create_macro('{$RSM.IP4.REPLY.MS}', 500, undef);
    create_macro('{$RSM.IP6.REPLY.MS}', 500, undef);

    create_macro('{$RSM.DNS.TCP.RTT.LOW}', 1500, undef);
    create_macro('{$RSM.DNS.TCP.RTT.HIGH}', 7500, undef);
    create_macro('{$RSM.DNS.UDP.RTT.LOW}', 500, undef);
    create_macro('{$RSM.DNS.UDP.RTT.HIGH}', 2500, undef);
    create_macro('{$RSM.DNS.UDP.DELAY}', 60, undef);
    create_macro('{$RSM.DNS.TCP.DELAY}', 60, undef);
    create_macro('{$RSM.DNS.UPDATE.TIME}', 3600, undef);
    create_macro('{$RSM.DNS.PROBE.ONLINE}', 2, undef);
    create_macro('{$RSM.DNS.AVAIL.MINNS}', 2, undef);
    create_macro('{$RSM.DNS.ROLLWEEK.SLA}', 60, undef);

    create_macro('{$RSM.RDDS.RTT.LOW}', 2000, undef);
    create_macro('{$RSM.RDDS.RTT.HIGH}', 10000, undef);
    create_macro('{$RSM.RDDS.DELAY}', 60, undef);
    create_macro('{$RSM.RDDS.UPDATE.TIME}', 3600, undef);
    create_macro('{$RSM.RDDS.PROBE.ONLINE}', 2, undef);
    create_macro('{$RSM.RDDS.ROLLWEEK.SLA}', 60, undef);
    create_macro('{$RSM.RDDS.MAXREDIRS}', 10, undef);

    create_macro('{$RSM.EPP.DELAY}', 60, undef);
    create_macro('{$RSM.EPP.LOGIN.RTT.LOW}', 4000, undef);
    create_macro('{$RSM.EPP.LOGIN.RTT.HIGH}', 20000, undef);
    create_macro('{$RSM.EPP.UPDATE.RTT.LOW}', 4000, undef);
    create_macro('{$RSM.EPP.UPDATE.RTT.HIGH}', 20000, undef);
    create_macro('{$RSM.EPP.INFO.RTT.LOW}', 2000, undef);
    create_macro('{$RSM.EPP.INFO.RTT.HIGH}', 10000, undef);
    create_macro('{$RSM.EPP.PROBE.ONLINE}', 2, undef);
    create_macro('{$RSM.EPP.ROLLWEEK.SLA}', 60, undef);

    create_macro('{$RSM.PROBE.ONLINE.DELAY}', 60, undef);

    create_macro('{$RSM.TRIG.DOWNCOUNT}', '#1', undef);
    create_macro('{$RSM.TRIG.UPCOUNT}', '#3', undef);

    create_macro('{$RSM.INCIDENT.DNS.FAIL}', '3', undef);
    create_macro('{$RSM.INCIDENT.DNS.RECOVER}', '3', undef);
    create_macro('{$RSM.INCIDENT.DNSSEC.FAIL}', '3', undef);
    create_macro('{$RSM.INCIDENT.DNSSEC.RECOVER}', '3', undef);
    create_macro('{$RSM.INCIDENT.RDDS.FAIL}', '2', undef);
    create_macro('{$RSM.INCIDENT.RDDS.RECOVER}', '2', undef);
    create_macro('{$RSM.INCIDENT.EPP.FAIL}', '2', undef);
    create_macro('{$RSM.INCIDENT.EPP.RECOVER}', '2', undef);

    create_macro('{$RSM.SLV.DNS.UDP.RTT}', 99, undef);
    create_macro('{$RSM.SLV.DNS.TCP.RTT}', 99, undef);
    create_macro('{$RSM.SLV.NS.AVAIL}', 99, undef);
    create_macro('{$RSM.SLV.RDDS43.RTT}', 99, undef);
    create_macro('{$RSM.SLV.RDDS80.RTT}', 99, undef);
    create_macro('{$RSM.SLV.RDDS.UPD}', 99, undef);
    create_macro('{$RSM.SLV.DNS.NS.UPD}', 99, undef);
    create_macro('{$RSM.SLV.EPP.LOGIN}', 99, undef);
    create_macro('{$RSM.SLV.EPP.UPDATE}', 99, undef);
    create_macro('{$RSM.SLV.EPP.INFO}', 99, undef);

    create_macro('{$RSM.ROLLWEEK.THRESHOLDS}', RSM_ROLLWEEK_THRESHOLDS, undef);
    create_macro('{$RSM.ROLLWEEK.SECONDS}', 7200, undef);
    create_macro('{$RSM.PROBE.AVAIL.LIMIT}', '60', undef); # For finding unreachable probes. Probes are considered unreachable if last access time is over this limit of seconds.
}

sub create_tld_host($$$$) {
    my $tld_name = shift;
    my $tld_groupid = shift;
    my $tlds_groupid = shift;
    my $tld_type_groupid = shift;

    my $tld_hostid = create_host({'groups' => [{'groupid' => $tld_groupid}, {'groupid' => $tlds_groupid}, {'groupid' => $tld_type_groupid}],
                              'host' => $tld_name,
                              'interfaces' => [{'type' => INTERFACE_TYPE_AGENT, 'main' => true, 'useip' => true, 'ip'=> '127.0.0.1', 'dns' => '', 'port' => '10050'}]});

    pfail $tld_hostid->{'data'} if check_api_error($tld_hostid) eq true;

    create_slv_items($ns_servers, $tld_hostid, $tld_name);

    return $tld_hostid;
}

sub create_probe_health_tmpl() {
    my $host_name = 'Template Proxy Health';
    my $templateid = create_template($host_name, LINUX_TEMPLATEID);

    my $item_key = 'zabbix[proxy,{$RSM.PROXY_NAME},lastaccess]';

    my $options = {'name' => 'Availability of $2 Probe',
                                          'key_'=> $item_key,
                                          'hostid' => $templateid,
                                          'applications' => [get_application_id('Probe Availability', $templateid)],
                                          'type' => 5, 'value_type' => 3,
                                          'units' => 'unixtime', delay => '60'};

    create_item($options);

    $options = { 'description' => 'Probe {$RSM.PROXY_NAME} is unavailable',
                     'expression' => '{'.$host_name.':'.$item_key.'.fuzzytime(2m)}=0',
                    'priority' => '4',
            };

    create_trigger($options, $host_name);

    # todo phase 1: make sure this is in phase 2
    $options = {'name' => 'Probe main status',
		'key_'=> 'rsm.probe.online',
		'hostid' => $templateid,
		'applications' => [get_application_id('Probe Availability', $templateid)],
		'type' => 2, 'value_type' => 3,
		'valuemapid' => rsm_value_mappings->{'rsm_probe'}};

    create_item($options);

    return $templateid;
}

sub manage_tld_objects($$$$$) {
    my $action = shift;
    my $tld = shift;
    my $dns = shift;
    my $epp = shift;
    my $rdds = shift;

    my $types = {'dns' => $dns, 'epp' => $epp, 'rdds' => $rdds};

    my $main_temlateid;

    my @tld_hostids;

    print "Trying to $action '$tld' TLD\n";

    print "Getting main host of the TLD: ";
    my $main_hostid = get_host($tld, false);

    if (scalar(%{$main_hostid})) {
        $main_hostid = $main_hostid->{'hostid'};
	print "success\n";
    }
    else {
        print "Could not find '$tld' host\n";
        exit;
    }

    print "Getting main template of the TLD: ";
    my $tld_template = get_template('Template '.$tld, false, true);

    if (scalar(%{$tld_template})) {
        $main_templateid = $tld_template->{'templateid'};
	print "success\n";
    }
    else {
        print "Could not find 'Template .$tld' template\n";
        exit;
    }

    foreach my $host (@{$tld_template->{'hosts'}}) {
	push @tld_hostids, $host->{'hostid'};
    }


    if ($dns eq true and $epp eq true and $rdds eq true) {
	print "You have choosed all possible options. Trying to $action TLD.\n";

	my @tmp_hostids;
	my @hostids_arr;

	push @tmp_hostids, {'hostid' => $main_hostid};

	foreach my $hostid (@tld_hostids) {
                push @tmp_hostids, {'hostid' => $hostid};
		push @hostids_arr, $hostid;
        }

	if ($action eq 'disable') {
	    my $result = disable_hosts(\@tmp_hostids);

	    if (scalar(%{$result})) {
		compare_arrays(\@hostids_arr, \@{$result->{'hostids'}});
	    }
	    else {
		print "An error happened while removing hosts!\n";
	    }

	    exit;
	}

	if ($action eq 'delete') {
	    remove_hosts( \@tmp_hostids );
	    remove_templates([ $main_templateid ]);

	    my $hostgroupid = get_host_group('TLD '.$tld, false);
	    $hostgroupid = $hostgroupid->{'groupid'};
	    remove_hostgroups( [ $hostgroupid ] );
	    return;
	}
    }

    foreach my $type (keys %{$types}) {
	next if $types->{$type} eq false;

	my @itemids;

	my $template_items = get_items_like($main_templateid, $type, true);
	my $host_items = get_items_like($main_hostid, $type, false);

	if (scalar(%{$template_items})) {
	    foreach my $itemid (%{$template_items}) {
		push @itemids, $itemid;
	    }
	}
	else {
	    print "Could not find $type related items on the template level\n";
	}

	if (scalar(%{$host_items})) {
	    foreach my $itemid (%{$host_items}) {
		push @itemids, $itemid;
	    }
	}
	else {
	    print "Could not find $type related items on host level\n";
	}

	if ($action eq 'disable' and scalar(@itemids)) {
	    disable_items(\@itemids);
	}

	if ($action eq 'delete' and scalar(@itemids)) {
	    remove_items(\@itemids);
#	    remove_applications_by_items(\@itemids);
	}
    }
}

sub compare_arrays($$) {
    my $array_A = shift;
    my $array_B = shift;

    my @result;

    foreach my $a (@{$array_A}) {
	my $found = false;
	foreach $b (@{$array_B}) {
	    $found = true if $a eq $b;
	}

	push @result, $a if $found eq false;
    }

    return @result;
}

sub get_tld_list() {
    my $tlds = get_host_group('TLDs', true);

    my @result;

    foreach my $tld (@{$tlds->{'hosts'}}) {
	push @result, $tld->{'name'};
    }

    return @result;
}

sub get_nsservers_list($) {
    my $TLD = shift;
    my $result;

    my $templateid = get_template('Template '.$TLD, false, false);

    return unless defined $templateid->{'templateid'};

    $templateid = $templateid->{'templateid'};

    my $items = get_items_like($templateid, 'rsm.dns.tcp.rtt', true);

    foreach my $itemid (keys %{$items}) {
	next if $items->{$itemid}->{'status'} == ITEM_STATUS_DISABLED;

	my $name = $items->{$itemid}->{'key_'};
	my $ip = $items->{$itemid}->{'key_'};

	$ip =~s/.+\,.+\,(.+)\]$/$1/;
	$name =~s/.+\,(.+)\,.+\]$/$1/;

	if ($ip=~/\d*\.\d*\.\d*\.\d+/) {
	    push @{$result->{'v4'}->{$name}}, $ip;
	}
	else {
	    push @{$result->{'v6'}->{$name}}, $ip;
	}
    }

    return $result;
}

sub update_nsservers($$) {
    my $TLD = shift;
    my $new_ns_servers = shift;

    return unless defined $new_ns_servers;

    my $old_ns_servers = get_nsservers_list($TLD);

    return unless defined $old_ns_servers;

    my @to_be_added = ();
    my @to_be_removed = ();

    foreach my $new_nsname (keys %{$new_ns_servers}) {
	my $new_ns = $new_ns_servers->{$new_nsname};

	foreach my $proto (keys %{$new_ns}) {
	    my $new_ips = $new_ns->{$proto};
	    foreach my $new_ip (@{$new_ips}) {
		    my $need_to_add = true;

		    if (defined($old_ns_servers->{$proto}) and defined($old_ns_servers->{$proto}->{$new_nsname})) {
			foreach my $old_ip (@{$old_ns_servers->{$proto}->{$new_nsname}}) {
			    $need_to_add = false if $old_ip eq $new_ip;
			}
		    }

		    if ($need_to_add == true) {
			my $ns_ip;
			$ns_ip->{$new_ip}->{'ns'} = $new_nsname;
			$ns_ip->{$new_ip}->{'proto'} = $proto;
			push @to_be_added, $ns_ip;
		    }
	    }

	}
    }

    foreach my $proto (keys %{$old_ns_servers}) {
	my $old_ns = $old_ns_servers->{$proto};
	foreach my $old_nsname (keys %{$old_ns}) {
	    foreach my $old_ip (@{$old_ns->{$old_nsname}}) {
		my $need_to_remove = false;

		if (defined($new_ns_servers->{$old_nsname}->{$proto})) {
		    $need_to_remove = true;

		    foreach my $new_ip (@{$new_ns_servers->{$old_nsname}->{$proto}}) {
		    	    $need_to_remove = false if $new_ip eq $old_ip;
		        }
		}
		else {
		    $need_to_remove = true;
		}

		if ($need_to_remove == true) {
		    my $ns_ip;

		    $ns_ip->{$old_ip} = $old_nsname;

		    push @to_be_removed, $ns_ip;
		}
	    }
	}
    }

    add_new_ns($TLD, \@to_be_added) if scalar(@to_be_added);
    disable_old_ns($TLD, \@to_be_removed) if scalar(@to_be_removed);
}

sub add_new_ns($) {
    my $TLD = shift;
    my $ns_servers = shift;

    my $main_templateid = get_template('Template '.$TLD, false, false);

    return unless defined $main_templateid->{'templateid'};

    $main_templateid = $main_templateid->{'templateid'};

    my $main_hostid = get_host($TLD, false);

    return unless defined $main_hostid->{'hostid'};

    $main_hostid = $main_hostid->{'hostid'};

    my $macro_value = get_host_macro($main_templateid, '{$RSM.TLD.DNSSEC.ENABLED}');

    $OPTS{'dnssec'} = true if (defined($macro_value) and $macro_value->{'value'} eq true);

    $macro_value = get_host_macro($main_templateid, '{$RSM.TLD.EPP.ENABLED}');

    $OPTS{'epp-servers'} = true if (defined($macro_value) and $macro_value->{'value'} eq true);

    foreach my $ns_ip (@$ns_servers) {
	foreach my $ip (keys %{$ns_ip}) {
	    my $proto = $ns_ip->{$ip}->{'proto'};
	    my $ns = $ns_ip->{$ip}->{'ns'};

	    $proto=~s/v(\d)/$1/;

	    create_item_dns_rtt($ns, $ip, $main_templateid, 'Template '.$TLD, 'tcp', $proto);
	    create_item_dns_rtt($ns, $ip, $main_templateid, 'Template '.$TLD, 'udp', $proto);

    	    create_all_slv_ns_items($ns, $ip, $main_hostid);
	}
    }
}

sub disable_old_ns($) {
    my $TLD = shift;
    my $ns_servers = shift;

    my @itemids;

    my $main_templateid = get_template('Template '.$TLD, false, false);

    return unless defined $main_templateid->{'templateid'};

    $main_templateid = $main_templateid->{'templateid'};

    my $main_hostid = get_host($TLD, false);

    return unless defined $main_hostid->{'hostid'};

    $main_hostid = $main_hostid->{'hostid'};

    foreach my $ns (@$ns_servers) {
	foreach my $ip (keys %{$ns}) {
	    my $ns_name = $ns->{$ip};
	    my $item_key = ','.$ns_name.','.$ip.']';

	    my $items = get_items_like($main_templateid, $item_key, true);

	    my @tmp_items = keys %{$items};

	    push @itemids, @tmp_items;

	    $item_key = '['.$ns_name.','.$ip.']';

	    $items = get_items_like($main_hostid, $item_key, false);

    	    @tmp_items = keys %{$items};

    	    push @itemids, @tmp_items;
	}
    }

    if (scalar(@itemids) > 0) {
	my $triggers = get_triggers_by_items(\@itemids);

        my @triggerids = keys %{$triggers};

#	disable_triggers(\@triggerids) if scalar @triggerids;

    	disable_items(\@itemids);
    }
}

sub get_services($) {
    my $tld = shift;

    my @tld_types = [TLD_TYPE_G, TLD_TYPE_CC, TLD_TYPE_OTHER, TLD_TYPE_TEST];

    my $result;

    my $main_templateid = get_template('Template '.$tld, false, false);

    my $macros = get_host_macro($main_templateid, undef);

    my $tld_host = get_host($tld, true);

    foreach my $group (@{$tld_host->{'groups'}}) {
	my $name = $group->{'name'};
	$result->{'tld_type'} = $name if (grep(/\b$name\b/, @tld_types));
    }

    foreach my $macro (@{$macros}) {
	my $name = $macro->{'macro'};
	my $value = $macro->{'value'};

	$result->{$name} = $value;
    }

    return $result;
}

sub create_avail_trigger($$) {
	my $service = shift;
	my $host_name = shift;

	my $service_lc = lc($service);

	# NB! Configuration trigger that is used in PHP and C code to detect incident!
	# priority must be set to 0!
	my $options =
	{
		'description' => $service.' service is down',
		'expression' => '({TRIGGER.VALUE}=0 and '.
			'{'.$host_name.':rsm.slv.'.$service_lc.'.avail.max(#{$RSM.INCIDENT.'.$service.'.FAIL})}=0) or '.
			'({TRIGGER.VALUE}=1 and '.
			'{'.$host_name.':rsm.slv.'.$service_lc.'.avail.count(#{$RSM.INCIDENT.'.$service.'.RECOVER},0,"eq")}>0)',
		'priority' => '0'
	};

	return create_trigger($options, $host_name);
}

sub create_rollweek_trigger($$$$$) {
	my $service = shift;
	my $host_name = shift;
	my $threshold = shift;
	my $priority = shift;
	my $created_ref = shift;

	my $service_lc = lc($service);

        my $options =
	{
		'description' => $service.' rolling week is over '.$threshold.'%',
		'expression' => '{'.$host_name.':rsm.slv.'.$service_lc.'.rollweek.last(0)}>='.$threshold,
		'priority' => $priority
	};

	return create_trigger($options, $host_name, $created_ref);
}
