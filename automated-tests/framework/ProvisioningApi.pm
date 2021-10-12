package ProvisioningApi;

use strict;
use warnings;

use Exporter qw(import);
our @EXPORT = qw(
	use_probes_pl
	use_tld_pl
	create_probe
	create_tld
	create_tld_probe
	create_tld_probe_nsip
);

use Data::Dumper;
use List::Util qw(max);

use Database;
use Framework;
use Output;

my $USE_PROBES_PL = 1;
my $USE_TLD_PL    = 1;

sub use_probes_pl($)
{
	$USE_PROBES_PL = shift;
}

sub use_tld_pl($)
{
	$USE_TLD_PL = shift;
}

sub create_probe($$$$$$$$)
{
	my $server_id = shift;
	my $probe     = shift;
	my $ip        = shift;
	my $port      = shift;
	my $ipv4      = shift;
	my $ipv6      = shift;
	my $rdds      = shift;
	my $rdap      = shift;

	info("creating probe '$probe'");

	if ($USE_PROBES_PL)
	{
		my @args = ();

		push(@args, "--server-id", $server_id);
		push(@args, "--probe", $probe);
		push(@args, "--add");
		push(@args, "--ip", $ip);
		push(@args, "--port", $port);
		push(@args, "--ipv4") if ($ipv4);
		push(@args, "--ipv6") if ($ipv6);
		push(@args, "--rdds") if ($rdds);
		push(@args, "--rdap") if ($rdap);

		@args = map("'$_'", @args);

		execute("/opt/zabbix/scripts/probes.pl @args");

		return;
	}

	my $hostid_proxy                 = __get_nextid('hosts');
	my $hostid_probe_config_template = __get_nextid('hosts');
	my $hostid_probe_host            = __get_nextid('hosts');
	my $hostid_probe_mon_host        = __get_nextid('hosts');

	__create_host($hostid_proxy                , undef        , "$probe"                      , 6, "");
	__create_host($hostid_probe_config_template, undef        , "Template Probe Config $probe", 3, "Template Probe Config $probe");
	__create_host($hostid_probe_host           , $hostid_proxy, "$probe"                      , 0, "$probe");
	__create_host($hostid_probe_mon_host       , undef        , "$probe - mon"                , 0, "$probe - mon");

	my $interfaceid_proxy          = __get_nextid('interface');
	my $interfaceid_probe_host     = __get_nextid('interface');
	my $interfaceid_probe_mon_host = __get_nextid('interface');

	__create_interface($interfaceid_proxy         , $hostid_proxy         , 0, $ip        , $port);
	__create_interface($interfaceid_probe_host    , $hostid_probe_host    , 1, "127.0.0.1", 10050);
	__create_interface($interfaceid_probe_mon_host, $hostid_probe_mon_host, 1, $ip        , 10050);

	__link_host_to_templateid($hostid_probe_host    , $hostid_probe_config_template);
	__link_host_to_template  ($hostid_probe_host    , "Template Probe Status");
	__link_host_to_template  ($hostid_probe_mon_host, "Template Proxy Health");

	__create_host_group(__get_nextid('hstgrp'), $probe);

	__link_host_to_group($hostid_probe_config_template, "Templates - TLD");
	__link_host_to_group($hostid_probe_host           , "Probes");
	__link_host_to_group($hostid_probe_mon_host       , "Probes - Mon");

	__create_host_macro($hostid_probe_config_template, '{$RSM.IP4.ENABLED}' , $ipv4      , "Indicates whether the probe supports IPv4");
	__create_host_macro($hostid_probe_config_template, '{$RSM.IP6.ENABLED}' , $ipv6      , "Indicates whether the probe supports IPv6");
	__create_host_macro($hostid_probe_config_template, '{$RSM.RESOLVER}'    , "127.0.0.1", "DNS resolver used by the probe");
	__create_host_macro($hostid_probe_config_template, '{$RSM.RDDS.ENABLED}', $rdds      , "Indicates whether the probe supports RDDS protocol");
	__create_host_macro($hostid_probe_config_template, '{$RSM.RDAP.ENABLED}', $rdap      , "Indicates whether the probe supports RDAP protocol");
	__create_host_macro($hostid_probe_config_template, '{$RSM.EPP.ENABLED}' , "0"        , "Indicates whether EPP is enabled on probe");
	__create_host_macro($hostid_probe_mon_host       , '{$RSM.PROXY_NAME}'  , $probe     , "");

	my $itemid_probe_configvalue_ip4  = __get_nextid('items');
	my $itemid_probe_configvalue_ip6  = __get_nextid('items');
	my $itemid_resolver_status        = __get_nextid('items');
	my $itemid_rsm_errors             = __get_nextid('items');
	my $itemid_probe_status_automatic = __get_nextid('items');
	my $itemid_probe_status_manual    = __get_nextid('items');
	my $itemid_probe_online           = __get_nextid('items');
	my $itemid_proxy_lastaccess       = __get_nextid('items');

	my $key_rsm_probe_status_automatic = 'rsm.probe.status[automatic,"{$RSM.IP4.ENABLED}","{$RSM.IP6.ENABLED}","{$RSM.IP4.ROOTSERVERS1}","{$RSM.IP6.ROOTSERVERS1}","{$RSM.IP4.MIN.SERVERS}","{$RSM.IP6.MIN.SERVERS}","{$RSM.IP4.REPLY.MS}","{$RSM.IP6.REPLY.MS}","{$RSM.PROBE.ONLINE.DELAY}"]';
	my $key_resolver_status = 'resolver.status[{$RSM.RESOLVER},{$RESOLVER.STATUS.TIMEOUT},{$RESOLVER.STATUS.TRIES},{$RSM.IP4.ENABLED},{$RSM.IP6.ENABLED}]';

	__create_item_from_template($itemid_probe_configvalue_ip4 , $hostid_probe_host    , __get_itemid("Template Probe Status", 'probe.configvalue[RSM.IP4.ENABLED]'        ), undef                  , undef);
	__create_item_from_template($itemid_probe_configvalue_ip6 , $hostid_probe_host    , __get_itemid("Template Probe Status", 'probe.configvalue[RSM.IP6.ENABLED]'        ), undef                  , undef);
	__create_item_from_template($itemid_resolver_status       , $hostid_probe_host    , __get_itemid("Template Probe Status", $key_resolver_status                        ), $interfaceid_probe_host, undef);
	__create_item_from_template($itemid_rsm_errors            , $hostid_probe_host    , __get_itemid("Template Probe Status", 'rsm.errors'                                ), $interfaceid_probe_host, undef);
	__create_item_from_template($itemid_probe_status_automatic, $hostid_probe_host    , __get_itemid("Template Probe Status", $key_rsm_probe_status_automatic             ), $interfaceid_probe_host, undef);
	__create_item_from_template($itemid_probe_status_manual   , $hostid_probe_host    , __get_itemid("Template Probe Status", 'rsm.probe.status[manual]'                  ), undef                  , undef);
	__create_item_from_template($itemid_probe_online          , $hostid_probe_mon_host, __get_itemid("Template Proxy Health", 'rsm.probe.online'                          ), undef                  , undef);
	__create_item_from_template($itemid_proxy_lastaccess      , $hostid_probe_mon_host, __get_itemid("Template Proxy Health", 'zabbix[proxy,{$RSM.PROXY_NAME},lastaccess]'), undef                  , undef);

	__create_item_rtdata($itemid_probe_configvalue_ip4);
	__create_item_rtdata($itemid_probe_configvalue_ip6);
	__create_item_rtdata($itemid_resolver_status);
	__create_item_rtdata($itemid_rsm_errors);
	__create_item_rtdata($itemid_probe_status_automatic);
	__create_item_rtdata($itemid_probe_status_manual);
	__create_item_rtdata($itemid_probe_online);
	__create_item_rtdata($itemid_proxy_lastaccess);

	__create_item_preproc($itemid_rsm_errors, 1, 10, "", 0);

	my $triggerid_rsm_errors_1           = __get_nextid('triggers');
	my $triggerid_rsm_errors_2           = __get_nextid('triggers');
	my $triggerid_probe_status_automatic = __get_nextid('triggers');
	my $triggerid_probe_status_manual_1  = __get_nextid('triggers');
	my $triggerid_probe_status_manual_2  = __get_nextid('triggers');
	my $triggerid_proxy_lastaccess       = __get_nextid('triggers');

	my $functionid_rsm_errors_1           = __get_nextid('functions');
	my $functionid_rsm_errors_2           = __get_nextid('functions');
	my $functionid_probe_status_automatic = __get_nextid('functions');
	my $functionid_probe_status_manual_1  = __get_nextid('functions');
	my $functionid_probe_status_manual_2  = __get_nextid('functions');
	my $functionid_proxy_lastaccess_1     = __get_nextid('functions');
	my $functionid_proxy_lastaccess_2     = __get_nextid('functions');
	my $functionid_proxy_lastaccess_3     = __get_nextid('functions');
	my $functionid_proxy_lastaccess_4     = __get_nextid('functions');
	my $functionid_proxy_lastaccess_5     = __get_nextid('functions');

	my $trigger_proxy_lastaccess_expression = "{TRIGGER.VALUE}=0 and {$functionid_proxy_lastaccess_1}=0 or\r\n" .
							"{TRIGGER.VALUE}=1 and (\r\n" .
							"    {$functionid_proxy_lastaccess_2}-{$functionid_proxy_lastaccess_3}>1m or\r\n" .
							"    {$functionid_proxy_lastaccess_2}-{$functionid_proxy_lastaccess_4}>2m or\r\n" .
							"    {$functionid_proxy_lastaccess_2}-{$functionid_proxy_lastaccess_5}>3m\r\n" .
							")";
	my $name;

	$name = 'Internal errors happening';
	__create_trigger($triggerid_rsm_errors_1          , "{$functionid_rsm_errors_1}>0"          , $name, 2, __get_triggerid("Template Probe Status", $name), '');
	$name = 'Internal errors happening for {$PROBE.INTERNAL.ERROR.INTERVAL}';
	__create_trigger($triggerid_rsm_errors_2          , "{$functionid_rsm_errors_2}>0"          , $name, 4, __get_triggerid("Template Probe Status", $name), '');
	$name = 'Probe {HOST.NAME} has been disabled by tests';
	__create_trigger($triggerid_probe_status_automatic, "{$functionid_probe_status_automatic}=0", $name, 4, __get_triggerid("Template Probe Status", $name), '');
	$name = 'Probe {HOST.NAME} has been knocked out';
	__create_trigger($triggerid_probe_status_manual_1 , "{$functionid_probe_status_manual_1}=0" , $name, 4, __get_triggerid("Template Probe Status", $name), '');
	$name = 'Probe {HOST.NAME} has been disabled for more than {$RSM.PROBE.MAX.OFFLINE}';
	__create_trigger($triggerid_probe_status_manual_2 , "{$functionid_probe_status_manual_2}=0" , $name, 3, __get_triggerid("Template Probe Status", $name), '');
	$name = 'Probe {$RSM.PROXY_NAME} is unavailable';
	__create_trigger($triggerid_proxy_lastaccess      , $trigger_proxy_lastaccess_expression    , $name, 4, __get_triggerid("Template Proxy Health", $name), '');

	__create_function($functionid_rsm_errors_1          , $itemid_rsm_errors            , $triggerid_rsm_errors_1          , "last"     , "");
	__create_function($functionid_rsm_errors_2          , $itemid_rsm_errors            , $triggerid_rsm_errors_2          , "min"      , '{$PROBE.INTERNAL.ERROR.INTERVAL}');
	__create_function($functionid_probe_status_automatic, $itemid_probe_status_automatic, $triggerid_probe_status_automatic, "last"     , 0);
	__create_function($functionid_probe_status_manual_1 , $itemid_probe_status_manual   , $triggerid_probe_status_manual_1 , "last"     , 0);
	__create_function($functionid_probe_status_manual_2 , $itemid_probe_status_manual   , $triggerid_probe_status_manual_2 , "max"      , '{$RSM.PROBE.MAX.OFFLINE}');
	__create_function($functionid_proxy_lastaccess_1    , $itemid_proxy_lastaccess      , $triggerid_proxy_lastaccess      , "fuzzytime", "2m");
	__create_function($functionid_proxy_lastaccess_2    , $itemid_proxy_lastaccess      , $triggerid_proxy_lastaccess      , "now"      , "");
	__create_function($functionid_proxy_lastaccess_3    , $itemid_proxy_lastaccess      , $triggerid_proxy_lastaccess      , "last"     , "#1");
	__create_function($functionid_proxy_lastaccess_4    , $itemid_proxy_lastaccess      , $triggerid_proxy_lastaccess      , "last"     , "#2");
	__create_function($functionid_proxy_lastaccess_5    , $itemid_proxy_lastaccess      , $triggerid_proxy_lastaccess      , "last"     , "#3");

	__create_trigger_dependency($triggerid_rsm_errors_1, $triggerid_rsm_errors_2);
}

sub create_tld($$$$$$$$$$$$$$)
{
	my $server_id        = shift;
	my $tld              = shift;
	my $dns_test_prefix  = shift;
	my $type             = shift;
	my $dnssec           = shift;
	my $dns_udp          = shift;
	my $dns_tcp          = shift;
	my $ns_servers_v4    = shift;
	my $ns_servers_v6    = shift;
	my $rdds43_servers   = shift;
	my $rdds80_servers   = shift;
	my $rdap_base_url    = shift;
	my $rdap_test_domain = shift;
	my $rdds_test_prefix = shift;

	my $rdds_enabled = $rdds43_servers || $rdds80_servers,
	my $rdap_enabled = $rdap_base_url || $rdap_test_domain;

	info("creating tld '$tld'");

	if ($USE_TLD_PL)
	{
		my @args = ();

		push(@args, "--server-id"       , $server_id       );
		push(@args, "--tld"             , $tld             );
		push(@args, "--dns-test-prefix" , $dns_test_prefix );
		push(@args, "--type"            , $type            );
		push(@args, "--dnssec"                             ) if ($dnssec);
		push(@args, "--dns-udp"                            ) if ($dns_udp);
		push(@args, "--dns-tcp"                            ) if ($dns_tcp);
		push(@args, "--ipv4"                               ) if ($ns_servers_v4);
		push(@args, "--ns-servers-v4"   , $ns_servers_v4   ) if ($ns_servers_v4);
		push(@args, "--ipv6"                               ) if ($ns_servers_v6);
		push(@args, "--ns-servers-v6"   , $ns_servers_v6   ) if ($ns_servers_v6);
		push(@args, "--rdds43-servers"  , $rdds43_servers  ) if ($rdds43_servers);
		push(@args, "--rdds80-servers"  , $rdds80_servers  ) if ($rdds80_servers);
		push(@args, "--rdap-base-url"   , $rdap_base_url   ) if ($rdap_base_url);
		push(@args, "--rdap-test-domain", $rdap_test_domain) if ($rdap_test_domain);
		push(@args, "--rdds-test-prefix", $rdds_test_prefix) if ($rdds_test_prefix);

		@args = map("'$_'", @args);

		execute("/opt/zabbix/scripts/tld.pl @args");

		return;
	}

	my @nsip_list = sort(split(" ", "$ns_servers_v4 $ns_servers_v6"));

	my $hostid_rsmhost_config_template = __get_nextid('hosts');
	my $hostid_rsmhost_host            = __get_nextid('hosts');

	__create_host($hostid_rsmhost_config_template, undef, "Template Rsmhost Config $tld", 3, "Template Rsmhost Config $tld");
	__create_host($hostid_rsmhost_host           , undef, "$tld"                        , 0, "$tld");

	my $interfaceid_tld_host = __get_nextid('interface');

	__create_interface($interfaceid_tld_host, $hostid_rsmhost_host, 1, "127.0.0.1", 10050);

	__link_host_to_templateid($hostid_rsmhost_host, $hostid_rsmhost_config_template);
	__link_host_to_template  ($hostid_rsmhost_host, "Template Config History");
	__link_host_to_template  ($hostid_rsmhost_host, "Template DNS Status");
	__link_host_to_template  ($hostid_rsmhost_host, "Template DNSSEC Status");
	__link_host_to_template  ($hostid_rsmhost_host, "Template RDDS Status");
	__link_host_to_template  ($hostid_rsmhost_host, "Template RDAP Status");

	__create_host_group(__get_nextid('hstgrp'), "TLD $tld");

	__link_host_to_group($hostid_rsmhost_config_template, "Templates - TLD");
	__link_host_to_group($hostid_rsmhost_host           , "TLDs");
	__link_host_to_group($hostid_rsmhost_host           , $type);

	my $dns_ns_servers = join(' ', @nsip_list);
	my $dns_minns = 2;
	my $rdds_ns_string = "Name Server:";

	__create_host_macro($hostid_rsmhost_config_template, '{$RSM.TLD}'                , $tld                    , 'Name of the rsmhost, e. g. "example"');
	__create_host_macro($hostid_rsmhost_config_template, '{$RSM.DNS.TESTPREFIX}'     , $dns_test_prefix        , 'Prefix for DNS tests, e.g. nonexistent');
	if ($rdds_enabled)
	{
		__create_host_macro($hostid_rsmhost_config_template, '{$RSM.RDDS43.TEST.DOMAIN}' , "$rdds_test_prefix.$tld", 'Domain name to use when querying RDDS43 server, e.g. "whois.example"');
	}
	__create_host_macro($hostid_rsmhost_config_template, '{$RSM.RDDS.NS.STRING}'     , $rdds_ns_string         , 'What to look for in RDDS output, e.g. "Name Server:"');
	__create_host_macro($hostid_rsmhost_config_template, '{$RSM.TLD.DNS.UDP.ENABLED}', $dns_udp ? 1 : 0        , 'Indicates whether DNS UDP enabled on the rsmhost');
	__create_host_macro($hostid_rsmhost_config_template, '{$RSM.TLD.DNS.TCP.ENABLED}', $dns_tcp ? 1 : 0        , 'Indicates whether DNS TCP enabled on the rsmhost');
	__create_host_macro($hostid_rsmhost_config_template, '{$RSM.TLD.DNS.AVAIL.MINNS}', $dns_minns              , 'Consider DNS Service availability at a particular time UP if during DNS test more than specified number of Name Servers replied successfully.');
	__create_host_macro($hostid_rsmhost_config_template, '{$RSM.TLD.DNSSEC.ENABLED}' , $dnssec ? 1 : 0         , 'Indicates whether DNSSEC is enabled on the rsmhost');
	__create_host_macro($hostid_rsmhost_config_template, '{$RSM.TLD.RDDS.ENABLED}'   , $rdds43_servers ? 1 : 0 , 'Indicates whether RDDS is enabled on the rsmhost');
	__create_host_macro($hostid_rsmhost_config_template, '{$RSM.TLD.RDDS.43.SERVERS}', lc($rdds43_servers)     , 'List of RDDS43 server host names as candidates for a test');
	__create_host_macro($hostid_rsmhost_config_template, '{$RSM.TLD.RDDS.80.SERVERS}', lc($rdds80_servers)     , 'List of Web Whois server host names as candidates for a test');
	__create_host_macro($hostid_rsmhost_config_template, '{$RSM.TLD.EPP.ENABLED}'    , 0                       , 'Indicates whether EPP is enabled on the rsmhost');
	__create_host_macro($hostid_rsmhost_config_template, '{$RSM.TLD.CONFIG.TIMES}'   , $^T                     , '');
	if ($rdap_enabled)
	{
		__create_host_macro($hostid_rsmhost_config_template, '{$RDAP.BASE.URL}'          , $rdap_base_url          , 'Base URL for RDAP queries, e.g. http://whois.zabbix');
		__create_host_macro($hostid_rsmhost_config_template, '{$RDAP.TEST.DOMAIN}'       , $rdap_test_domain       , 'Test domain for RDAP queries, e.g. whois.zabbix');
	}
	__create_host_macro($hostid_rsmhost_config_template, '{$RDAP.TLD.ENABLED}'       , $rdap_base_url ? 1 : 0  , 'Indicates whether RDAP is enabled on the rsmhost');
	__create_host_macro($hostid_rsmhost_config_template, '{$RSM.DNS.NAME.SERVERS}'   , $dns_ns_servers         , 'List of Name Server (name, IP pairs) to monitor');

	my $itemid_dns_tcp_enabled               = __get_nextid('items');
	my $itemid_dns_udp_enabled               = __get_nextid('items');
	my $itemid_dnssec_enabled                = __get_nextid('items');
	my $itemid_rdap_enabled                  = __get_nextid('items');
	my $itemid_rdds_enabled                  = __get_nextid('items');
	my $itemid_rsm_slv_dns_avail             = __get_nextid('items');
	my $itemid_rsm_slv_dns_downtime          = __get_nextid('items');
	my $itemid_rsm_slv_dns_rollweek          = __get_nextid('items');
	my $itemid_rsm_slv_dns_tcp_rtt_failed    = __get_nextid('items');
	my $itemid_rsm_slv_dns_tcp_rtt_performed = __get_nextid('items');
	my $itemid_rsm_slv_dns_tcp_rtt_pfailed   = __get_nextid('items');
	my $itemid_rsm_slv_dns_udp_rtt_failed    = __get_nextid('items');
	my $itemid_rsm_slv_dns_udp_rtt_performed = __get_nextid('items');
	my $itemid_rsm_slv_dns_udp_rtt_pfailed   = __get_nextid('items');
	my $itemid_rsm_slv_dnssec_avail          = __get_nextid('items');
	my $itemid_rsm_slv_dnssec_rollweek       = __get_nextid('items');
	my $itemid_rsm_slv_rdds_avail            = __get_nextid('items');
	my $itemid_rsm_slv_rdds_downtime         = __get_nextid('items');
	my $itemid_rsm_slv_rdds_rollweek         = __get_nextid('items');
	my $itemid_rsm_slv_rdds_rtt_failed       = __get_nextid('items');
	my $itemid_rsm_slv_rdds_rtt_performed    = __get_nextid('items');
	my $itemid_rsm_slv_rdds_rtt_pfailed      = __get_nextid('items');
	my $itemid_rsm_slv_rdap_avail            = __get_nextid('items');
	my $itemid_rsm_slv_rdap_downtime         = __get_nextid('items');
	my $itemid_rsm_slv_rdap_rollweek         = __get_nextid('items');
	my $itemid_rsm_slv_rdap_rtt_failed       = __get_nextid('items');
	my $itemid_rsm_slv_rdap_rtt_performed    = __get_nextid('items');
	my $itemid_rsm_slv_rdap_rtt_pfailed      = __get_nextid('items');

	my $itemid_rsm_slv_dns_ns_avail    = {};
	my $itemid_rsm_slv_dns_ns_downtime = {};

	foreach my $nsip (@nsip_list)
	{
		$itemid_rsm_slv_dns_ns_avail->{$nsip}    = __get_nextid('items');
		$itemid_rsm_slv_dns_ns_downtime->{$nsip} = __get_nextid('items');
	}

	__create_item_from_template($itemid_dns_tcp_enabled              , $hostid_rsmhost_host, __get_itemid("Template Config History", 'dns.tcp.enabled'              ), undef, undef);
	__create_item_from_template($itemid_dns_udp_enabled              , $hostid_rsmhost_host, __get_itemid("Template Config History", 'dns.udp.enabled'              ), undef, undef);
	__create_item_from_template($itemid_dnssec_enabled               , $hostid_rsmhost_host, __get_itemid("Template Config History", 'dnssec.enabled'               ), undef, undef);
	__create_item_from_template($itemid_rdap_enabled                 , $hostid_rsmhost_host, __get_itemid("Template Config History", 'rdap.enabled'                 ), undef, undef);
	__create_item_from_template($itemid_rdds_enabled                 , $hostid_rsmhost_host, __get_itemid("Template Config History", 'rdds.enabled'                 ), undef, undef);
	__create_item_from_template($itemid_rsm_slv_dns_avail            , $hostid_rsmhost_host, __get_itemid("Template DNS Status"    , 'rsm.slv.dns.avail'            ), undef, undef);
	__create_item_from_template($itemid_rsm_slv_dns_downtime         , $hostid_rsmhost_host, __get_itemid("Template DNS Status"    , 'rsm.slv.dns.downtime'         ), undef, undef);
	__create_item_from_template($itemid_rsm_slv_dns_rollweek         , $hostid_rsmhost_host, __get_itemid("Template DNS Status"    , 'rsm.slv.dns.rollweek'         ), undef, undef);
	__create_item_from_template($itemid_rsm_slv_dns_tcp_rtt_failed   , $hostid_rsmhost_host, __get_itemid("Template DNS Status"    , 'rsm.slv.dns.tcp.rtt.failed'   ), undef, undef);
	__create_item_from_template($itemid_rsm_slv_dns_tcp_rtt_performed, $hostid_rsmhost_host, __get_itemid("Template DNS Status"    , 'rsm.slv.dns.tcp.rtt.performed'), undef, undef);
	__create_item_from_template($itemid_rsm_slv_dns_tcp_rtt_pfailed  , $hostid_rsmhost_host, __get_itemid("Template DNS Status"    , 'rsm.slv.dns.tcp.rtt.pfailed'  ), undef, undef);
	__create_item_from_template($itemid_rsm_slv_dns_udp_rtt_failed   , $hostid_rsmhost_host, __get_itemid("Template DNS Status"    , 'rsm.slv.dns.udp.rtt.failed'   ), undef, undef);
	__create_item_from_template($itemid_rsm_slv_dns_udp_rtt_performed, $hostid_rsmhost_host, __get_itemid("Template DNS Status"    , 'rsm.slv.dns.udp.rtt.performed'), undef, undef);
	__create_item_from_template($itemid_rsm_slv_dns_udp_rtt_pfailed  , $hostid_rsmhost_host, __get_itemid("Template DNS Status"    , 'rsm.slv.dns.udp.rtt.pfailed'  ), undef, undef);
	__create_item_from_template($itemid_rsm_slv_dnssec_avail         , $hostid_rsmhost_host, __get_itemid("Template DNSSEC Status" , 'rsm.slv.dnssec.avail'         ), undef, undef);
	__create_item_from_template($itemid_rsm_slv_dnssec_rollweek      , $hostid_rsmhost_host, __get_itemid("Template DNSSEC Status" , 'rsm.slv.dnssec.rollweek'      ), undef, undef);
	__create_item_from_template($itemid_rsm_slv_rdds_avail           , $hostid_rsmhost_host, __get_itemid("Template RDDS Status"   , 'rsm.slv.rdds.avail'           ), undef, undef);
	__create_item_from_template($itemid_rsm_slv_rdds_downtime        , $hostid_rsmhost_host, __get_itemid("Template RDDS Status"   , 'rsm.slv.rdds.downtime'        ), undef, undef);
	__create_item_from_template($itemid_rsm_slv_rdds_rollweek        , $hostid_rsmhost_host, __get_itemid("Template RDDS Status"   , 'rsm.slv.rdds.rollweek'        ), undef, undef);
	__create_item_from_template($itemid_rsm_slv_rdds_rtt_failed      , $hostid_rsmhost_host, __get_itemid("Template RDDS Status"   , 'rsm.slv.rdds.rtt.failed'      ), undef, undef);
	__create_item_from_template($itemid_rsm_slv_rdds_rtt_performed   , $hostid_rsmhost_host, __get_itemid("Template RDDS Status"   , 'rsm.slv.rdds.rtt.performed'   ), undef, undef);
	__create_item_from_template($itemid_rsm_slv_rdds_rtt_pfailed     , $hostid_rsmhost_host, __get_itemid("Template RDDS Status"   , 'rsm.slv.rdds.rtt.pfailed'     ), undef, undef);
	__create_item_from_template($itemid_rsm_slv_rdap_avail           , $hostid_rsmhost_host, __get_itemid("Template RDAP Status"   , 'rsm.slv.rdap.avail'           ), undef, undef);
	__create_item_from_template($itemid_rsm_slv_rdap_downtime        , $hostid_rsmhost_host, __get_itemid("Template RDAP Status"   , 'rsm.slv.rdap.downtime'        ), undef, undef);
	__create_item_from_template($itemid_rsm_slv_rdap_rollweek        , $hostid_rsmhost_host, __get_itemid("Template RDAP Status"   , 'rsm.slv.rdap.rollweek'        ), undef, undef);
	__create_item_from_template($itemid_rsm_slv_rdap_rtt_failed      , $hostid_rsmhost_host, __get_itemid("Template RDAP Status"   , 'rsm.slv.rdap.rtt.failed'      ), undef, undef);
	__create_item_from_template($itemid_rsm_slv_rdap_rtt_performed   , $hostid_rsmhost_host, __get_itemid("Template RDAP Status"   , 'rsm.slv.rdap.rtt.performed'   ), undef, undef);
	__create_item_from_template($itemid_rsm_slv_rdap_rtt_pfailed     , $hostid_rsmhost_host, __get_itemid("Template RDAP Status"   , 'rsm.slv.rdap.rtt.pfailed'     ), undef, undef);

	foreach my $nsip (@nsip_list)
	{
		__create_item($itemid_rsm_slv_dns_ns_avail->{$nsip}   , 2, $hostid_rsmhost_host, 'DNS NS $1 ($2) availability'    , "rsm.slv.dns.ns.avail[$nsip]"   , 0, 3, '', undef, __get_valuemapid("RSM Service Availability"), '', undef, '');
		__create_item($itemid_rsm_slv_dns_ns_downtime->{$nsip}, 2, $hostid_rsmhost_host, 'DNS minutes of $1 ($2) downtime', "rsm.slv.dns.ns.downtime[$nsip]", 0, 3, '', undef, undef                                       , '', undef, '');
	}

	if (!$rdds43_servers && !$rdds80_servers)
	{
		__disable_item($itemid_rsm_slv_rdds_avail);
		__disable_item($itemid_rsm_slv_rdds_downtime);
		__disable_item($itemid_rsm_slv_rdds_rollweek);
		__disable_item($itemid_rsm_slv_rdds_rtt_failed);
		__disable_item($itemid_rsm_slv_rdds_rtt_performed);
		__disable_item($itemid_rsm_slv_rdds_rtt_pfailed);
	}

	if (!__is_rdap_standalone())
	{
		__disable_item($itemid_rsm_slv_rdap_avail);
		__disable_item($itemid_rsm_slv_rdap_downtime);
		__disable_item($itemid_rsm_slv_rdap_rollweek);
		__disable_item($itemid_rsm_slv_rdap_rtt_failed);
		__disable_item($itemid_rsm_slv_rdap_rtt_performed);
		__disable_item($itemid_rsm_slv_rdap_rtt_pfailed);
	}

	__create_item_rtdata($itemid_dns_tcp_enabled);
	__create_item_rtdata($itemid_dns_udp_enabled);
	__create_item_rtdata($itemid_dnssec_enabled);
	__create_item_rtdata($itemid_rdap_enabled);
	__create_item_rtdata($itemid_rdds_enabled);
	__create_item_rtdata($itemid_rsm_slv_dns_avail);
	__create_item_rtdata($itemid_rsm_slv_dns_downtime);
	__create_item_rtdata($itemid_rsm_slv_dns_rollweek);
	__create_item_rtdata($itemid_rsm_slv_dns_tcp_rtt_failed);
	__create_item_rtdata($itemid_rsm_slv_dns_tcp_rtt_performed);
	__create_item_rtdata($itemid_rsm_slv_dns_tcp_rtt_pfailed);
	__create_item_rtdata($itemid_rsm_slv_dns_udp_rtt_failed);
	__create_item_rtdata($itemid_rsm_slv_dns_udp_rtt_performed);
	__create_item_rtdata($itemid_rsm_slv_dns_udp_rtt_pfailed);
	__create_item_rtdata($itemid_rsm_slv_dnssec_avail);
	__create_item_rtdata($itemid_rsm_slv_dnssec_rollweek);
	__create_item_rtdata($itemid_rsm_slv_rdds_avail);
	__create_item_rtdata($itemid_rsm_slv_rdds_downtime);
	__create_item_rtdata($itemid_rsm_slv_rdds_rollweek);
	__create_item_rtdata($itemid_rsm_slv_rdds_rtt_failed);
	__create_item_rtdata($itemid_rsm_slv_rdds_rtt_performed);
	__create_item_rtdata($itemid_rsm_slv_rdds_rtt_pfailed);
	__create_item_rtdata($itemid_rsm_slv_rdap_avail);
	__create_item_rtdata($itemid_rsm_slv_rdap_downtime);
	__create_item_rtdata($itemid_rsm_slv_rdap_rollweek);
	__create_item_rtdata($itemid_rsm_slv_rdap_rtt_failed);
	__create_item_rtdata($itemid_rsm_slv_rdap_rtt_performed);
	__create_item_rtdata($itemid_rsm_slv_rdap_rtt_pfailed);

	foreach my $nsip (@nsip_list)
	{
		__create_item_rtdata($itemid_rsm_slv_dns_ns_avail->{$nsip});
		__create_item_rtdata($itemid_rsm_slv_dns_ns_downtime->{$nsip});
	}

	my $triggerid_rsm_slv_dns_avail           = __get_nextid('triggers');
	my $triggerid_rsm_slv_dns_downtime        = __get_nextid('triggers');
	my $triggerid_rsm_slv_dns_rollweek_1      = __get_nextid('triggers');
	my $triggerid_rsm_slv_dns_rollweek_2      = __get_nextid('triggers');
	my $triggerid_rsm_slv_dns_rollweek_3      = __get_nextid('triggers');
	my $triggerid_rsm_slv_dns_rollweek_4      = __get_nextid('triggers');
	my $triggerid_rsm_slv_dns_rollweek_5      = __get_nextid('triggers');
	my $triggerid_rsm_slv_dns_tcp_rtt_pfail_1 = __get_nextid('triggers');
	my $triggerid_rsm_slv_dns_tcp_rtt_pfail_2 = __get_nextid('triggers');
	my $triggerid_rsm_slv_dns_tcp_rtt_pfail_3 = __get_nextid('triggers');
	my $triggerid_rsm_slv_dns_tcp_rtt_pfail_4 = __get_nextid('triggers');
	my $triggerid_rsm_slv_dns_tcp_rtt_pfail_5 = __get_nextid('triggers');
	my $triggerid_rsm_slv_dns_udp_rtt_pfail_1 = __get_nextid('triggers');
	my $triggerid_rsm_slv_dns_udp_rtt_pfail_2 = __get_nextid('triggers');
	my $triggerid_rsm_slv_dns_udp_rtt_pfail_3 = __get_nextid('triggers');
	my $triggerid_rsm_slv_dns_udp_rtt_pfail_4 = __get_nextid('triggers');
	my $triggerid_rsm_slv_dns_udp_rtt_pfail_5 = __get_nextid('triggers');
	my $triggerid_rsm_slv_dnssec_avail        = __get_nextid('triggers');
	my $triggerid_rsm_slv_dnssec_rollweek_1   = __get_nextid('triggers');
	my $triggerid_rsm_slv_dnssec_rollweek_2   = __get_nextid('triggers');
	my $triggerid_rsm_slv_dnssec_rollweek_3   = __get_nextid('triggers');
	my $triggerid_rsm_slv_dnssec_rollweek_4   = __get_nextid('triggers');
	my $triggerid_rsm_slv_dnssec_rollweek_5   = __get_nextid('triggers');
	my $triggerid_rsm_slv_rdds_avail          = __get_nextid('triggers');
	my $triggerid_rsm_slv_rdds_downtime_1     = __get_nextid('triggers');
	my $triggerid_rsm_slv_rdds_downtime_2     = __get_nextid('triggers');
	my $triggerid_rsm_slv_rdds_downtime_3     = __get_nextid('triggers');
	my $triggerid_rsm_slv_rdds_downtime_4     = __get_nextid('triggers');
	my $triggerid_rsm_slv_rdds_downtime_5     = __get_nextid('triggers');
	my $triggerid_rsm_slv_rdds_rollweek_1     = __get_nextid('triggers');
	my $triggerid_rsm_slv_rdds_rollweek_2     = __get_nextid('triggers');
	my $triggerid_rsm_slv_rdds_rollweek_3     = __get_nextid('triggers');
	my $triggerid_rsm_slv_rdds_rollweek_4     = __get_nextid('triggers');
	my $triggerid_rsm_slv_rdds_rollweek_5     = __get_nextid('triggers');
	my $triggerid_rsm_slv_rdds_rtt_pfailed_1  = __get_nextid('triggers');
	my $triggerid_rsm_slv_rdds_rtt_pfailed_2  = __get_nextid('triggers');
	my $triggerid_rsm_slv_rdds_rtt_pfailed_3  = __get_nextid('triggers');
	my $triggerid_rsm_slv_rdds_rtt_pfailed_4  = __get_nextid('triggers');
	my $triggerid_rsm_slv_rdds_rtt_pfailed_5  = __get_nextid('triggers');
	my $triggerid_rsm_slv_rdap_avail          = __get_nextid('triggers');
	my $triggerid_rsm_slv_rdap_downtime_1     = __get_nextid('triggers');
	my $triggerid_rsm_slv_rdap_downtime_2     = __get_nextid('triggers');
	my $triggerid_rsm_slv_rdap_downtime_3     = __get_nextid('triggers');
	my $triggerid_rsm_slv_rdap_downtime_4     = __get_nextid('triggers');
	my $triggerid_rsm_slv_rdap_downtime_5     = __get_nextid('triggers');
	my $triggerid_rsm_slv_rdap_rollweek_1     = __get_nextid('triggers');
	my $triggerid_rsm_slv_rdap_rollweek_2     = __get_nextid('triggers');
	my $triggerid_rsm_slv_rdap_rollweek_3     = __get_nextid('triggers');
	my $triggerid_rsm_slv_rdap_rollweek_4     = __get_nextid('triggers');
	my $triggerid_rsm_slv_rdap_rollweek_5     = __get_nextid('triggers');
	my $triggerid_rsm_slv_rdap_rtt_pfailed_1  = __get_nextid('triggers');
	my $triggerid_rsm_slv_rdap_rtt_pfailed_2  = __get_nextid('triggers');
	my $triggerid_rsm_slv_rdap_rtt_pfailed_3  = __get_nextid('triggers');
	my $triggerid_rsm_slv_rdap_rtt_pfailed_4  = __get_nextid('triggers');
	my $triggerid_rsm_slv_rdap_rtt_pfailed_5  = __get_nextid('triggers');

	my $triggerid_rsm_slv_dns_ns_downtime_1 = {};
	my $triggerid_rsm_slv_dns_ns_downtime_2 = {};
	my $triggerid_rsm_slv_dns_ns_downtime_3 = {};
	my $triggerid_rsm_slv_dns_ns_downtime_4 = {};
	my $triggerid_rsm_slv_dns_ns_downtime_5 = {};

	foreach my $nsip (@nsip_list)
	{
		$triggerid_rsm_slv_dns_ns_downtime_1->{$nsip} = __get_nextid('triggers');
		$triggerid_rsm_slv_dns_ns_downtime_2->{$nsip} = __get_nextid('triggers');
		$triggerid_rsm_slv_dns_ns_downtime_3->{$nsip} = __get_nextid('triggers');
		$triggerid_rsm_slv_dns_ns_downtime_4->{$nsip} = __get_nextid('triggers');
		$triggerid_rsm_slv_dns_ns_downtime_5->{$nsip} = __get_nextid('triggers');
	};

	my $functionid_rsm_slv_dns_avail_1         = __get_nextid('functions');
	my $functionid_rsm_slv_dns_avail_2         = __get_nextid('functions');
	my $functionid_rsm_slv_dns_downtime        = __get_nextid('functions');
	my $functionid_rsm_slv_dns_rollweek_1      = __get_nextid('functions');
	my $functionid_rsm_slv_dns_rollweek_2      = __get_nextid('functions');
	my $functionid_rsm_slv_dns_rollweek_3      = __get_nextid('functions');
	my $functionid_rsm_slv_dns_rollweek_4      = __get_nextid('functions');
	my $functionid_rsm_slv_dns_rollweek_5      = __get_nextid('functions');
	my $functionid_rsm_slv_dns_tcp_rtt_pfail_1 = __get_nextid('functions');
	my $functionid_rsm_slv_dns_tcp_rtt_pfail_2 = __get_nextid('functions');
	my $functionid_rsm_slv_dns_tcp_rtt_pfail_3 = __get_nextid('functions');
	my $functionid_rsm_slv_dns_tcp_rtt_pfail_4 = __get_nextid('functions');
	my $functionid_rsm_slv_dns_tcp_rtt_pfail_5 = __get_nextid('functions');
	my $functionid_rsm_slv_dns_udp_rtt_pfail_1 = __get_nextid('functions');
	my $functionid_rsm_slv_dns_udp_rtt_pfail_2 = __get_nextid('functions');
	my $functionid_rsm_slv_dns_udp_rtt_pfail_3 = __get_nextid('functions');
	my $functionid_rsm_slv_dns_udp_rtt_pfail_4 = __get_nextid('functions');
	my $functionid_rsm_slv_dns_udp_rtt_pfail_5 = __get_nextid('functions');
	my $functionid_rsm_slv_dnssec_avail_1      = __get_nextid('functions');
	my $functionid_rsm_slv_dnssec_avail_2      = __get_nextid('functions');
	my $functionid_rsm_slv_dnssec_rollweek_1   = __get_nextid('functions');
	my $functionid_rsm_slv_dnssec_rollweek_2   = __get_nextid('functions');
	my $functionid_rsm_slv_dnssec_rollweek_3   = __get_nextid('functions');
	my $functionid_rsm_slv_dnssec_rollweek_4   = __get_nextid('functions');
	my $functionid_rsm_slv_dnssec_rollweek_5   = __get_nextid('functions');
	my $functionid_rsm_slv_rdds_avail_1        = __get_nextid('functions');
	my $functionid_rsm_slv_rdds_avail_2        = __get_nextid('functions');
	my $functionid_rsm_slv_rdds_downtime_1     = __get_nextid('functions');
	my $functionid_rsm_slv_rdds_downtime_2     = __get_nextid('functions');
	my $functionid_rsm_slv_rdds_downtime_3     = __get_nextid('functions');
	my $functionid_rsm_slv_rdds_downtime_4     = __get_nextid('functions');
	my $functionid_rsm_slv_rdds_downtime_5     = __get_nextid('functions');
	my $functionid_rsm_slv_rdds_rollweek_1     = __get_nextid('functions');
	my $functionid_rsm_slv_rdds_rollweek_2     = __get_nextid('functions');
	my $functionid_rsm_slv_rdds_rollweek_3     = __get_nextid('functions');
	my $functionid_rsm_slv_rdds_rollweek_4     = __get_nextid('functions');
	my $functionid_rsm_slv_rdds_rollweek_5     = __get_nextid('functions');
	my $functionid_rsm_slv_rdds_rtt_pfailed_1  = __get_nextid('functions');
	my $functionid_rsm_slv_rdds_rtt_pfailed_2  = __get_nextid('functions');
	my $functionid_rsm_slv_rdds_rtt_pfailed_3  = __get_nextid('functions');
	my $functionid_rsm_slv_rdds_rtt_pfailed_4  = __get_nextid('functions');
	my $functionid_rsm_slv_rdds_rtt_pfailed_5  = __get_nextid('functions');
	my $functionid_rsm_slv_rdap_avail_1        = __get_nextid('functions');
	my $functionid_rsm_slv_rdap_avail_2        = __get_nextid('functions');
	my $functionid_rsm_slv_rdap_downtime_1     = __get_nextid('functions');
	my $functionid_rsm_slv_rdap_downtime_2     = __get_nextid('functions');
	my $functionid_rsm_slv_rdap_downtime_3     = __get_nextid('functions');
	my $functionid_rsm_slv_rdap_downtime_4     = __get_nextid('functions');
	my $functionid_rsm_slv_rdap_downtime_5     = __get_nextid('functions');
	my $functionid_rsm_slv_rdap_rollweek_1     = __get_nextid('functions');
	my $functionid_rsm_slv_rdap_rollweek_2     = __get_nextid('functions');
	my $functionid_rsm_slv_rdap_rollweek_3     = __get_nextid('functions');
	my $functionid_rsm_slv_rdap_rollweek_4     = __get_nextid('functions');
	my $functionid_rsm_slv_rdap_rollweek_5     = __get_nextid('functions');
	my $functionid_rsm_slv_rdap_rtt_pfailed_1  = __get_nextid('functions');
	my $functionid_rsm_slv_rdap_rtt_pfailed_2  = __get_nextid('functions');
	my $functionid_rsm_slv_rdap_rtt_pfailed_3  = __get_nextid('functions');
	my $functionid_rsm_slv_rdap_rtt_pfailed_4  = __get_nextid('functions');
	my $functionid_rsm_slv_rdap_rtt_pfailed_5  = __get_nextid('functions');

	my $functionid_rsm_slv_dns_ns_downtime_1 = {};
	my $functionid_rsm_slv_dns_ns_downtime_2 = {};
	my $functionid_rsm_slv_dns_ns_downtime_3 = {};
	my $functionid_rsm_slv_dns_ns_downtime_4 = {};
	my $functionid_rsm_slv_dns_ns_downtime_5 = {};

	foreach my $nsip (@nsip_list)
	{
		$functionid_rsm_slv_dns_ns_downtime_1->{$nsip} = __get_nextid('functions');
		$functionid_rsm_slv_dns_ns_downtime_2->{$nsip} = __get_nextid('functions');
		$functionid_rsm_slv_dns_ns_downtime_3->{$nsip} = __get_nextid('functions');
		$functionid_rsm_slv_dns_ns_downtime_4->{$nsip} = __get_nextid('functions');
		$functionid_rsm_slv_dns_ns_downtime_5->{$nsip} = __get_nextid('functions');
	};

	my $name;

	__create_trigger($triggerid_rsm_slv_dns_avail          , "{$functionid_rsm_slv_dns_avail_1}=0"                                   , $name = 'DNS service is down'                                        , 0, __get_triggerid('Template DNS Status'   , $name), "{$functionid_rsm_slv_dns_avail_2}>0");
	__create_trigger($triggerid_rsm_slv_dns_downtime       , "{$functionid_rsm_slv_dns_downtime}>{\$RSM.SLV.DNS.DOWNTIME}"           , $name = 'DNS service was unavailable for at least {ITEM.VALUE1}m'    , 5, __get_triggerid('Template DNS Status'   , $name), '');
	__create_trigger($triggerid_rsm_slv_dns_rollweek_1     , "{$functionid_rsm_slv_dns_rollweek_1}>=10"                              , $name = 'DNS rolling week is over 10%'                               , 2, __get_triggerid('Template DNS Status'   , $name), '');
	__create_trigger($triggerid_rsm_slv_dns_rollweek_2     , "{$functionid_rsm_slv_dns_rollweek_2}>=25"                              , $name = 'DNS rolling week is over 25%'                               , 3, __get_triggerid('Template DNS Status'   , $name), '');
	__create_trigger($triggerid_rsm_slv_dns_rollweek_3     , "{$functionid_rsm_slv_dns_rollweek_3}>=50"                              , $name = 'DNS rolling week is over 50%'                               , 3, __get_triggerid('Template DNS Status'   , $name), '');
	__create_trigger($triggerid_rsm_slv_dns_rollweek_4     , "{$functionid_rsm_slv_dns_rollweek_4}>=75"                              , $name = 'DNS rolling week is over 75%'                               , 4, __get_triggerid('Template DNS Status'   , $name), '');
	__create_trigger($triggerid_rsm_slv_dns_rollweek_5     , "{$functionid_rsm_slv_dns_rollweek_5}>=100"                             , $name = 'DNS rolling week is over 100%'                              , 5, __get_triggerid('Template DNS Status'   , $name), '');
	__create_trigger($triggerid_rsm_slv_dns_tcp_rtt_pfail_1, "{$functionid_rsm_slv_dns_tcp_rtt_pfail_1}>{\$RSM.SLV.DNS.TCP.RTT}*0.1" , $name = 'Ratio of failed DNS TCP tests exceeded 10% of allowed $1%'  , 2, __get_triggerid('Template DNS Status'   , $name), '');
	__create_trigger($triggerid_rsm_slv_dns_tcp_rtt_pfail_2, "{$functionid_rsm_slv_dns_tcp_rtt_pfail_2}>{\$RSM.SLV.DNS.TCP.RTT}*0.25", $name = 'Ratio of failed DNS TCP tests exceeded 25% of allowed $1%'  , 3, __get_triggerid('Template DNS Status'   , $name), '');
	__create_trigger($triggerid_rsm_slv_dns_tcp_rtt_pfail_3, "{$functionid_rsm_slv_dns_tcp_rtt_pfail_3}>{\$RSM.SLV.DNS.TCP.RTT}*0.5" , $name = 'Ratio of failed DNS TCP tests exceeded 50% of allowed $1%'  , 3, __get_triggerid('Template DNS Status'   , $name), '');
	__create_trigger($triggerid_rsm_slv_dns_tcp_rtt_pfail_4, "{$functionid_rsm_slv_dns_tcp_rtt_pfail_4}>{\$RSM.SLV.DNS.TCP.RTT}*0.75", $name = 'Ratio of failed DNS TCP tests exceeded 75% of allowed $1%'  , 4, __get_triggerid('Template DNS Status'   , $name), '');
	__create_trigger($triggerid_rsm_slv_dns_tcp_rtt_pfail_5, "{$functionid_rsm_slv_dns_tcp_rtt_pfail_5}>{\$RSM.SLV.DNS.TCP.RTT}"     , $name = 'Ratio of failed DNS TCP tests exceeded 100% of allowed $1%' , 5, __get_triggerid('Template DNS Status'   , $name), '');
	__create_trigger($triggerid_rsm_slv_dns_udp_rtt_pfail_1, "{$functionid_rsm_slv_dns_udp_rtt_pfail_1}>{\$RSM.SLV.DNS.UDP.RTT}*0.1" , $name = 'Ratio of failed DNS UDP tests exceeded 10% of allowed $1%'  , 2, __get_triggerid('Template DNS Status'   , $name), '');
	__create_trigger($triggerid_rsm_slv_dns_udp_rtt_pfail_2, "{$functionid_rsm_slv_dns_udp_rtt_pfail_2}>{\$RSM.SLV.DNS.UDP.RTT}*0.25", $name = 'Ratio of failed DNS UDP tests exceeded 25% of allowed $1%'  , 3, __get_triggerid('Template DNS Status'   , $name), '');
	__create_trigger($triggerid_rsm_slv_dns_udp_rtt_pfail_3, "{$functionid_rsm_slv_dns_udp_rtt_pfail_3}>{\$RSM.SLV.DNS.UDP.RTT}*0.5" , $name = 'Ratio of failed DNS UDP tests exceeded 50% of allowed $1%'  , 3, __get_triggerid('Template DNS Status'   , $name), '');
	__create_trigger($triggerid_rsm_slv_dns_udp_rtt_pfail_4, "{$functionid_rsm_slv_dns_udp_rtt_pfail_4}>{\$RSM.SLV.DNS.UDP.RTT}*0.75", $name = 'Ratio of failed DNS UDP tests exceeded 75% of allowed $1%'  , 4, __get_triggerid('Template DNS Status'   , $name), '');
	__create_trigger($triggerid_rsm_slv_dns_udp_rtt_pfail_5, "{$functionid_rsm_slv_dns_udp_rtt_pfail_5}>{\$RSM.SLV.DNS.UDP.RTT}"     , $name = 'Ratio of failed DNS UDP tests exceeded 100% of allowed $1%' , 5, __get_triggerid('Template DNS Status'   , $name), '');
	__create_trigger($triggerid_rsm_slv_dnssec_avail       , "{$functionid_rsm_slv_dnssec_avail_1}=0"                                , $name = 'DNSSEC service is down'                                     , 0, __get_triggerid('Template DNSSEC Status', $name), "{$functionid_rsm_slv_dnssec_avail_2}>0");
	__create_trigger($triggerid_rsm_slv_dnssec_rollweek_1  , "{$functionid_rsm_slv_dnssec_rollweek_1}>=10"                           , $name = 'DNSSEC rolling week is over 10%'                            , 2, __get_triggerid('Template DNSSEC Status', $name), '');
	__create_trigger($triggerid_rsm_slv_dnssec_rollweek_2  , "{$functionid_rsm_slv_dnssec_rollweek_2}>=25"                           , $name = 'DNSSEC rolling week is over 25%'                            , 3, __get_triggerid('Template DNSSEC Status', $name), '');
	__create_trigger($triggerid_rsm_slv_dnssec_rollweek_3  , "{$functionid_rsm_slv_dnssec_rollweek_3}>=50"                           , $name = 'DNSSEC rolling week is over 50%'                            , 3, __get_triggerid('Template DNSSEC Status', $name), '');
	__create_trigger($triggerid_rsm_slv_dnssec_rollweek_4  , "{$functionid_rsm_slv_dnssec_rollweek_4}>=75"                           , $name = 'DNSSEC rolling week is over 75%'                            , 4, __get_triggerid('Template DNSSEC Status', $name), '');
	__create_trigger($triggerid_rsm_slv_dnssec_rollweek_5  , "{$functionid_rsm_slv_dnssec_rollweek_5}>=100"                          , $name = 'DNSSEC rolling week is over 100%'                           , 5, __get_triggerid('Template DNSSEC Status', $name), '');
	__create_trigger($triggerid_rsm_slv_rdds_avail         , "{$functionid_rsm_slv_rdds_avail_1}=0"                                  , $name = 'RDDS service is down'                                       , 0, __get_triggerid('Template RDDS Status'  , $name), "{$functionid_rsm_slv_rdds_avail_2}>0");
	__create_trigger($triggerid_rsm_slv_rdds_downtime_1    , "{$functionid_rsm_slv_rdds_downtime_1}>={\$RSM.SLV.RDDS.DOWNTIME}*0.1"  , $name = 'RDDS service was unavailable for 10% of allowed $1 minutes' , 2, __get_triggerid('Template RDDS Status'  , $name), '');
	__create_trigger($triggerid_rsm_slv_rdds_downtime_2    , "{$functionid_rsm_slv_rdds_downtime_2}>={\$RSM.SLV.RDDS.DOWNTIME}*0.25" , $name = 'RDDS service was unavailable for 25% of allowed $1 minutes' , 3, __get_triggerid('Template RDDS Status'  , $name), '');
	__create_trigger($triggerid_rsm_slv_rdds_downtime_3    , "{$functionid_rsm_slv_rdds_downtime_3}>={\$RSM.SLV.RDDS.DOWNTIME}*0.5"  , $name = 'RDDS service was unavailable for 50% of allowed $1 minutes' , 3, __get_triggerid('Template RDDS Status'  , $name), '');
	__create_trigger($triggerid_rsm_slv_rdds_downtime_4    , "{$functionid_rsm_slv_rdds_downtime_4}>={\$RSM.SLV.RDDS.DOWNTIME}*0.75" , $name = 'RDDS service was unavailable for 75% of allowed $1 minutes' , 4, __get_triggerid('Template RDDS Status'  , $name), '');
	__create_trigger($triggerid_rsm_slv_rdds_downtime_5    , "{$functionid_rsm_slv_rdds_downtime_5}>={\$RSM.SLV.RDDS.DOWNTIME}"      , $name = 'RDDS service was unavailable for 100% of allowed $1 minutes', 5, __get_triggerid('Template RDDS Status'  , $name), '');
	__create_trigger($triggerid_rsm_slv_rdds_rollweek_1    , "{$functionid_rsm_slv_rdds_rollweek_1}>=10"                             , $name = 'RDDS rolling week is over 10%'                              , 2, __get_triggerid('Template RDDS Status'  , $name), '');
	__create_trigger($triggerid_rsm_slv_rdds_rollweek_2    , "{$functionid_rsm_slv_rdds_rollweek_2}>=25"                             , $name = 'RDDS rolling week is over 25%'                              , 3, __get_triggerid('Template RDDS Status'  , $name), '');
	__create_trigger($triggerid_rsm_slv_rdds_rollweek_3    , "{$functionid_rsm_slv_rdds_rollweek_3}>=50"                             , $name = 'RDDS rolling week is over 50%'                              , 3, __get_triggerid('Template RDDS Status'  , $name), '');
	__create_trigger($triggerid_rsm_slv_rdds_rollweek_4    , "{$functionid_rsm_slv_rdds_rollweek_4}>=75"                             , $name = 'RDDS rolling week is over 75%'                              , 4, __get_triggerid('Template RDDS Status'  , $name), '');
	__create_trigger($triggerid_rsm_slv_rdds_rollweek_5    , "{$functionid_rsm_slv_rdds_rollweek_5}>=100"                            , $name = 'RDDS rolling week is over 100%'                             , 5, __get_triggerid('Template RDDS Status'  , $name), '');
	__create_trigger($triggerid_rsm_slv_rdds_rtt_pfailed_1 , "{$functionid_rsm_slv_rdds_rtt_pfailed_1}>{\$RSM.SLV.RDDS.RTT}*0.1"     , $name = 'Ratio of failed RDDS tests exceeded 10% of allowed $1%'     , 2, __get_triggerid('Template RDDS Status'  , $name), '');
	__create_trigger($triggerid_rsm_slv_rdds_rtt_pfailed_2 , "{$functionid_rsm_slv_rdds_rtt_pfailed_2}>{\$RSM.SLV.RDDS.RTT}*0.25"    , $name = 'Ratio of failed RDDS tests exceeded 25% of allowed $1%'     , 3, __get_triggerid('Template RDDS Status'  , $name), '');
	__create_trigger($triggerid_rsm_slv_rdds_rtt_pfailed_3 , "{$functionid_rsm_slv_rdds_rtt_pfailed_3}>{\$RSM.SLV.RDDS.RTT}*0.5"     , $name = 'Ratio of failed RDDS tests exceeded 50% of allowed $1%'     , 3, __get_triggerid('Template RDDS Status'  , $name), '');
	__create_trigger($triggerid_rsm_slv_rdds_rtt_pfailed_4 , "{$functionid_rsm_slv_rdds_rtt_pfailed_4}>{\$RSM.SLV.RDDS.RTT}*0.75"    , $name = 'Ratio of failed RDDS tests exceeded 75% of allowed $1%'     , 4, __get_triggerid('Template RDDS Status'  , $name), '');
	__create_trigger($triggerid_rsm_slv_rdds_rtt_pfailed_5 , "{$functionid_rsm_slv_rdds_rtt_pfailed_5}>{\$RSM.SLV.RDDS.RTT}"         , $name = 'Ratio of failed RDDS tests exceeded 100% of allowed $1%'    , 5, __get_triggerid('Template RDDS Status'  , $name), '');
	__create_trigger($triggerid_rsm_slv_rdap_avail         , "{$functionid_rsm_slv_rdap_avail_1}=0"                                  , $name = 'RDAP service is down'                                       , 0, __get_triggerid('Template RDAP Status'  , $name), "{$functionid_rsm_slv_rdap_avail_2}>0");
	__create_trigger($triggerid_rsm_slv_rdap_downtime_1    , "{$functionid_rsm_slv_rdap_downtime_1}>={\$RSM.SLV.RDAP.DOWNTIME}*0.1"  , $name = 'RDAP service was unavailable for 10% of allowed $1 minutes' , 2, __get_triggerid('Template RDAP Status'  , $name), '');
	__create_trigger($triggerid_rsm_slv_rdap_downtime_2    , "{$functionid_rsm_slv_rdap_downtime_2}>={\$RSM.SLV.RDAP.DOWNTIME}*0.25" , $name = 'RDAP service was unavailable for 25% of allowed $1 minutes' , 3, __get_triggerid('Template RDAP Status'  , $name), '');
	__create_trigger($triggerid_rsm_slv_rdap_downtime_3    , "{$functionid_rsm_slv_rdap_downtime_3}>={\$RSM.SLV.RDAP.DOWNTIME}*0.5"  , $name = 'RDAP service was unavailable for 50% of allowed $1 minutes' , 3, __get_triggerid('Template RDAP Status'  , $name), '');
	__create_trigger($triggerid_rsm_slv_rdap_downtime_4    , "{$functionid_rsm_slv_rdap_downtime_4}>={\$RSM.SLV.RDAP.DOWNTIME}*0.75" , $name = 'RDAP service was unavailable for 75% of allowed $1 minutes' , 4, __get_triggerid('Template RDAP Status'  , $name), '');
	__create_trigger($triggerid_rsm_slv_rdap_downtime_5    , "{$functionid_rsm_slv_rdap_downtime_5}>={\$RSM.SLV.RDAP.DOWNTIME}"      , $name = 'RDAP service was unavailable for 100% of allowed $1 minutes', 5, __get_triggerid('Template RDAP Status'  , $name), '');
	__create_trigger($triggerid_rsm_slv_rdap_rollweek_1    , "{$functionid_rsm_slv_rdap_rollweek_1}>=10"                             , $name = 'RDAP rolling week is over 10%'                              , 2, __get_triggerid('Template RDAP Status'  , $name), '');
	__create_trigger($triggerid_rsm_slv_rdap_rollweek_2    , "{$functionid_rsm_slv_rdap_rollweek_2}>=25"                             , $name = 'RDAP rolling week is over 25%'                              , 3, __get_triggerid('Template RDAP Status'  , $name), '');
	__create_trigger($triggerid_rsm_slv_rdap_rollweek_3    , "{$functionid_rsm_slv_rdap_rollweek_3}>=50"                             , $name = 'RDAP rolling week is over 50%'                              , 3, __get_triggerid('Template RDAP Status'  , $name), '');
	__create_trigger($triggerid_rsm_slv_rdap_rollweek_4    , "{$functionid_rsm_slv_rdap_rollweek_4}>=75"                             , $name = 'RDAP rolling week is over 75%'                              , 4, __get_triggerid('Template RDAP Status'  , $name), '');
	__create_trigger($triggerid_rsm_slv_rdap_rollweek_5    , "{$functionid_rsm_slv_rdap_rollweek_5}>=100"                            , $name = 'RDAP rolling week is over 100%'                             , 5, __get_triggerid('Template RDAP Status'  , $name), '');
	__create_trigger($triggerid_rsm_slv_rdap_rtt_pfailed_1 , "{$functionid_rsm_slv_rdap_rtt_pfailed_1}>{\$RSM.SLV.RDAP.RTT}*0.1"     , $name = 'Ratio of failed RDAP tests exceeded 10% of allowed $1%'     , 2, __get_triggerid('Template RDAP Status'  , $name), '');
	__create_trigger($triggerid_rsm_slv_rdap_rtt_pfailed_2 , "{$functionid_rsm_slv_rdap_rtt_pfailed_2}>{\$RSM.SLV.RDAP.RTT}*0.25"    , $name = 'Ratio of failed RDAP tests exceeded 25% of allowed $1%'     , 3, __get_triggerid('Template RDAP Status'  , $name), '');
	__create_trigger($triggerid_rsm_slv_rdap_rtt_pfailed_3 , "{$functionid_rsm_slv_rdap_rtt_pfailed_3}>{\$RSM.SLV.RDAP.RTT}*0.5"     , $name = 'Ratio of failed RDAP tests exceeded 50% of allowed $1%'     , 3, __get_triggerid('Template RDAP Status'  , $name), '');
	__create_trigger($triggerid_rsm_slv_rdap_rtt_pfailed_4 , "{$functionid_rsm_slv_rdap_rtt_pfailed_4}>{\$RSM.SLV.RDAP.RTT}*0.75"    , $name = 'Ratio of failed RDAP tests exceeded 75% of allowed $1%'     , 4, __get_triggerid('Template RDAP Status'  , $name), '');
	__create_trigger($triggerid_rsm_slv_rdap_rtt_pfailed_5 , "{$functionid_rsm_slv_rdap_rtt_pfailed_5}>{\$RSM.SLV.RDAP.RTT}"         , $name = 'Ratio of failed RDAP tests exceeded 100% of allowed $1%'    , 5, __get_triggerid('Template RDAP Status'  , $name), '');

	foreach my $nsip (@nsip_list)
	{
		my ($ns, $ip) = split(',', $nsip);

		__create_trigger($triggerid_rsm_slv_dns_ns_downtime_1->{$nsip}, "{$functionid_rsm_slv_dns_ns_downtime_1->{$nsip}}>{\$RSM.SLV.NS.DOWNTIME}*0.1" , "DNS $ns ($ip) downtime exceeded 10% of allowed \$1 minutes" , 2, undef, '');
		__create_trigger($triggerid_rsm_slv_dns_ns_downtime_2->{$nsip}, "{$functionid_rsm_slv_dns_ns_downtime_2->{$nsip}}>{\$RSM.SLV.NS.DOWNTIME}*0.25", "DNS $ns ($ip) downtime exceeded 25% of allowed \$1 minutes" , 3, undef, '');
		__create_trigger($triggerid_rsm_slv_dns_ns_downtime_3->{$nsip}, "{$functionid_rsm_slv_dns_ns_downtime_3->{$nsip}}>{\$RSM.SLV.NS.DOWNTIME}*0.5" , "DNS $ns ($ip) downtime exceeded 50% of allowed \$1 minutes" , 3, undef, '');
		__create_trigger($triggerid_rsm_slv_dns_ns_downtime_4->{$nsip}, "{$functionid_rsm_slv_dns_ns_downtime_4->{$nsip}}>{\$RSM.SLV.NS.DOWNTIME}*0.75", "DNS $ns ($ip) downtime exceeded 75% of allowed \$1 minutes" , 4, undef, '');
		__create_trigger($triggerid_rsm_slv_dns_ns_downtime_5->{$nsip}, "{$functionid_rsm_slv_dns_ns_downtime_5->{$nsip}}>{\$RSM.SLV.NS.DOWNTIME}"     , "DNS $ns ($ip) downtime exceeded 100% of allowed \$1 minutes", 5, undef, '');
	}

	__create_function($functionid_rsm_slv_dns_avail_1        , $itemid_rsm_slv_dns_avail          , $triggerid_rsm_slv_dns_avail          , 'max' , '#{$RSM.INCIDENT.DNS.FAIL}');
	__create_function($functionid_rsm_slv_dns_avail_2        , $itemid_rsm_slv_dns_avail          , $triggerid_rsm_slv_dns_avail          , 'min' , '#{$RSM.INCIDENT.DNS.RECOVER}');
	__create_function($functionid_rsm_slv_dns_downtime       , $itemid_rsm_slv_dns_downtime       , $triggerid_rsm_slv_dns_downtime       , 'last', 0);
	__create_function($functionid_rsm_slv_dns_rollweek_1     , $itemid_rsm_slv_dns_rollweek       , $triggerid_rsm_slv_dns_rollweek_1     , 'last', 0);
	__create_function($functionid_rsm_slv_dns_rollweek_2     , $itemid_rsm_slv_dns_rollweek       , $triggerid_rsm_slv_dns_rollweek_2     , 'last', 0);
	__create_function($functionid_rsm_slv_dns_rollweek_3     , $itemid_rsm_slv_dns_rollweek       , $triggerid_rsm_slv_dns_rollweek_3     , 'last', 0);
	__create_function($functionid_rsm_slv_dns_rollweek_4     , $itemid_rsm_slv_dns_rollweek       , $triggerid_rsm_slv_dns_rollweek_4     , 'last', 0);
	__create_function($functionid_rsm_slv_dns_rollweek_5     , $itemid_rsm_slv_dns_rollweek       , $triggerid_rsm_slv_dns_rollweek_5     , 'last', 0);
	__create_function($functionid_rsm_slv_dns_tcp_rtt_pfail_1, $itemid_rsm_slv_dns_tcp_rtt_pfailed, $triggerid_rsm_slv_dns_tcp_rtt_pfail_1, 'last', '');
	__create_function($functionid_rsm_slv_dns_tcp_rtt_pfail_2, $itemid_rsm_slv_dns_tcp_rtt_pfailed, $triggerid_rsm_slv_dns_tcp_rtt_pfail_2, 'last', '');
	__create_function($functionid_rsm_slv_dns_tcp_rtt_pfail_3, $itemid_rsm_slv_dns_tcp_rtt_pfailed, $triggerid_rsm_slv_dns_tcp_rtt_pfail_3, 'last', '');
	__create_function($functionid_rsm_slv_dns_tcp_rtt_pfail_4, $itemid_rsm_slv_dns_tcp_rtt_pfailed, $triggerid_rsm_slv_dns_tcp_rtt_pfail_4, 'last', '');
	__create_function($functionid_rsm_slv_dns_tcp_rtt_pfail_5, $itemid_rsm_slv_dns_tcp_rtt_pfailed, $triggerid_rsm_slv_dns_tcp_rtt_pfail_5, 'last', '');
	__create_function($functionid_rsm_slv_dns_udp_rtt_pfail_1, $itemid_rsm_slv_dns_udp_rtt_pfailed, $triggerid_rsm_slv_dns_udp_rtt_pfail_1, 'last', '');
	__create_function($functionid_rsm_slv_dns_udp_rtt_pfail_2, $itemid_rsm_slv_dns_udp_rtt_pfailed, $triggerid_rsm_slv_dns_udp_rtt_pfail_2, 'last', '');
	__create_function($functionid_rsm_slv_dns_udp_rtt_pfail_3, $itemid_rsm_slv_dns_udp_rtt_pfailed, $triggerid_rsm_slv_dns_udp_rtt_pfail_3, 'last', '');
	__create_function($functionid_rsm_slv_dns_udp_rtt_pfail_4, $itemid_rsm_slv_dns_udp_rtt_pfailed, $triggerid_rsm_slv_dns_udp_rtt_pfail_4, 'last', '');
	__create_function($functionid_rsm_slv_dns_udp_rtt_pfail_5, $itemid_rsm_slv_dns_udp_rtt_pfailed, $triggerid_rsm_slv_dns_udp_rtt_pfail_5, 'last', '');
	__create_function($functionid_rsm_slv_dnssec_avail_1     , $itemid_rsm_slv_dnssec_avail       , $triggerid_rsm_slv_dnssec_avail       , 'max' , '#{$RSM.INCIDENT.DNSSEC.FAIL}');
	__create_function($functionid_rsm_slv_dnssec_avail_2     , $itemid_rsm_slv_dnssec_avail       , $triggerid_rsm_slv_dnssec_avail       , 'min' , '#{$RSM.INCIDENT.DNSSEC.RECOVER}');
	__create_function($functionid_rsm_slv_dnssec_rollweek_1  , $itemid_rsm_slv_dnssec_rollweek    , $triggerid_rsm_slv_dnssec_rollweek_1  , 'last', 0);
	__create_function($functionid_rsm_slv_dnssec_rollweek_2  , $itemid_rsm_slv_dnssec_rollweek    , $triggerid_rsm_slv_dnssec_rollweek_2  , 'last', 0);
	__create_function($functionid_rsm_slv_dnssec_rollweek_3  , $itemid_rsm_slv_dnssec_rollweek    , $triggerid_rsm_slv_dnssec_rollweek_3  , 'last', 0);
	__create_function($functionid_rsm_slv_dnssec_rollweek_4  , $itemid_rsm_slv_dnssec_rollweek    , $triggerid_rsm_slv_dnssec_rollweek_4  , 'last', 0);
	__create_function($functionid_rsm_slv_dnssec_rollweek_5  , $itemid_rsm_slv_dnssec_rollweek    , $triggerid_rsm_slv_dnssec_rollweek_5  , 'last', 0);
	__create_function($functionid_rsm_slv_rdds_avail_1       , $itemid_rsm_slv_rdds_avail         , $triggerid_rsm_slv_rdds_avail         , 'max' , '#{$RSM.INCIDENT.RDDS.FAIL}');
	__create_function($functionid_rsm_slv_rdds_avail_2       , $itemid_rsm_slv_rdds_avail         , $triggerid_rsm_slv_rdds_avail         , 'min' , '#{$RSM.INCIDENT.RDDS.RECOVER}');
	__create_function($functionid_rsm_slv_rdds_downtime_1    , $itemid_rsm_slv_rdds_downtime      , $triggerid_rsm_slv_rdds_downtime_1    , 'last', 0);
	__create_function($functionid_rsm_slv_rdds_downtime_2    , $itemid_rsm_slv_rdds_downtime      , $triggerid_rsm_slv_rdds_downtime_2    , 'last', 0);
	__create_function($functionid_rsm_slv_rdds_downtime_3    , $itemid_rsm_slv_rdds_downtime      , $triggerid_rsm_slv_rdds_downtime_3    , 'last', 0);
	__create_function($functionid_rsm_slv_rdds_downtime_4    , $itemid_rsm_slv_rdds_downtime      , $triggerid_rsm_slv_rdds_downtime_4    , 'last', 0);
	__create_function($functionid_rsm_slv_rdds_downtime_5    , $itemid_rsm_slv_rdds_downtime      , $triggerid_rsm_slv_rdds_downtime_5    , 'last', 0);
	__create_function($functionid_rsm_slv_rdds_rollweek_1    , $itemid_rsm_slv_rdds_rollweek      , $triggerid_rsm_slv_rdds_rollweek_1    , 'last', 0);
	__create_function($functionid_rsm_slv_rdds_rollweek_2    , $itemid_rsm_slv_rdds_rollweek      , $triggerid_rsm_slv_rdds_rollweek_2    , 'last', 0);
	__create_function($functionid_rsm_slv_rdds_rollweek_3    , $itemid_rsm_slv_rdds_rollweek      , $triggerid_rsm_slv_rdds_rollweek_3    , 'last', 0);
	__create_function($functionid_rsm_slv_rdds_rollweek_4    , $itemid_rsm_slv_rdds_rollweek      , $triggerid_rsm_slv_rdds_rollweek_4    , 'last', 0);
	__create_function($functionid_rsm_slv_rdds_rollweek_5    , $itemid_rsm_slv_rdds_rollweek      , $triggerid_rsm_slv_rdds_rollweek_5    , 'last', 0);
	__create_function($functionid_rsm_slv_rdds_rtt_pfailed_1 , $itemid_rsm_slv_rdds_rtt_pfailed   , $triggerid_rsm_slv_rdds_rtt_pfailed_1 , 'last', '');
	__create_function($functionid_rsm_slv_rdds_rtt_pfailed_2 , $itemid_rsm_slv_rdds_rtt_pfailed   , $triggerid_rsm_slv_rdds_rtt_pfailed_2 , 'last', '');
	__create_function($functionid_rsm_slv_rdds_rtt_pfailed_3 , $itemid_rsm_slv_rdds_rtt_pfailed   , $triggerid_rsm_slv_rdds_rtt_pfailed_3 , 'last', '');
	__create_function($functionid_rsm_slv_rdds_rtt_pfailed_4 , $itemid_rsm_slv_rdds_rtt_pfailed   , $triggerid_rsm_slv_rdds_rtt_pfailed_4 , 'last', '');
	__create_function($functionid_rsm_slv_rdds_rtt_pfailed_5 , $itemid_rsm_slv_rdds_rtt_pfailed   , $triggerid_rsm_slv_rdds_rtt_pfailed_5 , 'last', '');
	__create_function($functionid_rsm_slv_rdap_avail_1       , $itemid_rsm_slv_rdap_avail         , $triggerid_rsm_slv_rdap_avail         , 'max' , '#{$RSM.INCIDENT.RDAP.FAIL}');
	__create_function($functionid_rsm_slv_rdap_avail_2       , $itemid_rsm_slv_rdap_avail         , $triggerid_rsm_slv_rdap_avail         , 'min' , '#{$RSM.INCIDENT.RDAP.RECOVER}');
	__create_function($functionid_rsm_slv_rdap_downtime_1    , $itemid_rsm_slv_rdap_downtime      , $triggerid_rsm_slv_rdap_downtime_1    , 'last', 0);
	__create_function($functionid_rsm_slv_rdap_downtime_2    , $itemid_rsm_slv_rdap_downtime      , $triggerid_rsm_slv_rdap_downtime_2    , 'last', 0);
	__create_function($functionid_rsm_slv_rdap_downtime_3    , $itemid_rsm_slv_rdap_downtime      , $triggerid_rsm_slv_rdap_downtime_3    , 'last', 0);
	__create_function($functionid_rsm_slv_rdap_downtime_4    , $itemid_rsm_slv_rdap_downtime      , $triggerid_rsm_slv_rdap_downtime_4    , 'last', 0);
	__create_function($functionid_rsm_slv_rdap_downtime_5    , $itemid_rsm_slv_rdap_downtime      , $triggerid_rsm_slv_rdap_downtime_5    , 'last', 0);
	__create_function($functionid_rsm_slv_rdap_rollweek_1    , $itemid_rsm_slv_rdap_rollweek      , $triggerid_rsm_slv_rdap_rollweek_1    , 'last', 0);
	__create_function($functionid_rsm_slv_rdap_rollweek_2    , $itemid_rsm_slv_rdap_rollweek      , $triggerid_rsm_slv_rdap_rollweek_2    , 'last', 0);
	__create_function($functionid_rsm_slv_rdap_rollweek_3    , $itemid_rsm_slv_rdap_rollweek      , $triggerid_rsm_slv_rdap_rollweek_3    , 'last', 0);
	__create_function($functionid_rsm_slv_rdap_rollweek_4    , $itemid_rsm_slv_rdap_rollweek      , $triggerid_rsm_slv_rdap_rollweek_4    , 'last', 0);
	__create_function($functionid_rsm_slv_rdap_rollweek_5    , $itemid_rsm_slv_rdap_rollweek      , $triggerid_rsm_slv_rdap_rollweek_5    , 'last', 0);
	__create_function($functionid_rsm_slv_rdap_rtt_pfailed_1 , $itemid_rsm_slv_rdap_rtt_pfailed   , $triggerid_rsm_slv_rdap_rtt_pfailed_1 , 'last', '');
	__create_function($functionid_rsm_slv_rdap_rtt_pfailed_2 , $itemid_rsm_slv_rdap_rtt_pfailed   , $triggerid_rsm_slv_rdap_rtt_pfailed_2 , 'last', '');
	__create_function($functionid_rsm_slv_rdap_rtt_pfailed_3 , $itemid_rsm_slv_rdap_rtt_pfailed   , $triggerid_rsm_slv_rdap_rtt_pfailed_3 , 'last', '');
	__create_function($functionid_rsm_slv_rdap_rtt_pfailed_4 , $itemid_rsm_slv_rdap_rtt_pfailed   , $triggerid_rsm_slv_rdap_rtt_pfailed_4 , 'last', '');
	__create_function($functionid_rsm_slv_rdap_rtt_pfailed_5 , $itemid_rsm_slv_rdap_rtt_pfailed   , $triggerid_rsm_slv_rdap_rtt_pfailed_5 , 'last', '');

	foreach my $nsip (@nsip_list)
	{
		__create_function($functionid_rsm_slv_dns_ns_downtime_1->{$nsip}, $itemid_rsm_slv_dns_ns_downtime->{$nsip}, $triggerid_rsm_slv_dns_ns_downtime_1->{$nsip}, 'last', '');
		__create_function($functionid_rsm_slv_dns_ns_downtime_2->{$nsip}, $itemid_rsm_slv_dns_ns_downtime->{$nsip}, $triggerid_rsm_slv_dns_ns_downtime_2->{$nsip}, 'last', '');
		__create_function($functionid_rsm_slv_dns_ns_downtime_3->{$nsip}, $itemid_rsm_slv_dns_ns_downtime->{$nsip}, $triggerid_rsm_slv_dns_ns_downtime_3->{$nsip}, 'last', '');
		__create_function($functionid_rsm_slv_dns_ns_downtime_4->{$nsip}, $itemid_rsm_slv_dns_ns_downtime->{$nsip}, $triggerid_rsm_slv_dns_ns_downtime_4->{$nsip}, 'last', '');
		__create_function($functionid_rsm_slv_dns_ns_downtime_5->{$nsip}, $itemid_rsm_slv_dns_ns_downtime->{$nsip}, $triggerid_rsm_slv_dns_ns_downtime_5->{$nsip}, 'last', '');
	}

	__create_trigger_dependency($triggerid_rsm_slv_dns_rollweek_1, $triggerid_rsm_slv_dns_rollweek_2);
	__create_trigger_dependency($triggerid_rsm_slv_dns_rollweek_2, $triggerid_rsm_slv_dns_rollweek_3);
	__create_trigger_dependency($triggerid_rsm_slv_dns_rollweek_3, $triggerid_rsm_slv_dns_rollweek_4);
	__create_trigger_dependency($triggerid_rsm_slv_dns_rollweek_4, $triggerid_rsm_slv_dns_rollweek_5);

	__create_trigger_dependency($triggerid_rsm_slv_dns_tcp_rtt_pfail_1, $triggerid_rsm_slv_dns_tcp_rtt_pfail_2);
	__create_trigger_dependency($triggerid_rsm_slv_dns_tcp_rtt_pfail_2, $triggerid_rsm_slv_dns_tcp_rtt_pfail_3);
	__create_trigger_dependency($triggerid_rsm_slv_dns_tcp_rtt_pfail_3, $triggerid_rsm_slv_dns_tcp_rtt_pfail_4);
	__create_trigger_dependency($triggerid_rsm_slv_dns_tcp_rtt_pfail_4, $triggerid_rsm_slv_dns_tcp_rtt_pfail_5);

	__create_trigger_dependency($triggerid_rsm_slv_dns_udp_rtt_pfail_1, $triggerid_rsm_slv_dns_udp_rtt_pfail_2);
	__create_trigger_dependency($triggerid_rsm_slv_dns_udp_rtt_pfail_2, $triggerid_rsm_slv_dns_udp_rtt_pfail_3);
	__create_trigger_dependency($triggerid_rsm_slv_dns_udp_rtt_pfail_3, $triggerid_rsm_slv_dns_udp_rtt_pfail_4);
	__create_trigger_dependency($triggerid_rsm_slv_dns_udp_rtt_pfail_4, $triggerid_rsm_slv_dns_udp_rtt_pfail_5);

	__create_trigger_dependency($triggerid_rsm_slv_dnssec_rollweek_1, $triggerid_rsm_slv_dnssec_rollweek_2);
	__create_trigger_dependency($triggerid_rsm_slv_dnssec_rollweek_2, $triggerid_rsm_slv_dnssec_rollweek_3);
	__create_trigger_dependency($triggerid_rsm_slv_dnssec_rollweek_3, $triggerid_rsm_slv_dnssec_rollweek_4);
	__create_trigger_dependency($triggerid_rsm_slv_dnssec_rollweek_4, $triggerid_rsm_slv_dnssec_rollweek_5);

	__create_trigger_dependency($triggerid_rsm_slv_rdds_downtime_1, $triggerid_rsm_slv_rdds_downtime_2);
	__create_trigger_dependency($triggerid_rsm_slv_rdds_downtime_2, $triggerid_rsm_slv_rdds_downtime_3);
	__create_trigger_dependency($triggerid_rsm_slv_rdds_downtime_3, $triggerid_rsm_slv_rdds_downtime_4);
	__create_trigger_dependency($triggerid_rsm_slv_rdds_downtime_4, $triggerid_rsm_slv_rdds_downtime_5);

	__create_trigger_dependency($triggerid_rsm_slv_rdds_rollweek_1, $triggerid_rsm_slv_rdds_rollweek_2);
	__create_trigger_dependency($triggerid_rsm_slv_rdds_rollweek_2, $triggerid_rsm_slv_rdds_rollweek_3);
	__create_trigger_dependency($triggerid_rsm_slv_rdds_rollweek_3, $triggerid_rsm_slv_rdds_rollweek_4);
	__create_trigger_dependency($triggerid_rsm_slv_rdds_rollweek_4, $triggerid_rsm_slv_rdds_rollweek_5);

	__create_trigger_dependency($triggerid_rsm_slv_rdds_rtt_pfailed_1, $triggerid_rsm_slv_rdds_rtt_pfailed_2);
	__create_trigger_dependency($triggerid_rsm_slv_rdds_rtt_pfailed_2, $triggerid_rsm_slv_rdds_rtt_pfailed_3);
	__create_trigger_dependency($triggerid_rsm_slv_rdds_rtt_pfailed_3, $triggerid_rsm_slv_rdds_rtt_pfailed_4);
	__create_trigger_dependency($triggerid_rsm_slv_rdds_rtt_pfailed_4, $triggerid_rsm_slv_rdds_rtt_pfailed_5);

	__create_trigger_dependency($triggerid_rsm_slv_rdap_downtime_1, $triggerid_rsm_slv_rdap_downtime_2);
	__create_trigger_dependency($triggerid_rsm_slv_rdap_downtime_2, $triggerid_rsm_slv_rdap_downtime_3);
	__create_trigger_dependency($triggerid_rsm_slv_rdap_downtime_3, $triggerid_rsm_slv_rdap_downtime_4);
	__create_trigger_dependency($triggerid_rsm_slv_rdap_downtime_4, $triggerid_rsm_slv_rdap_downtime_5);

	__create_trigger_dependency($triggerid_rsm_slv_rdap_rollweek_1, $triggerid_rsm_slv_rdap_rollweek_2);
	__create_trigger_dependency($triggerid_rsm_slv_rdap_rollweek_2, $triggerid_rsm_slv_rdap_rollweek_3);
	__create_trigger_dependency($triggerid_rsm_slv_rdap_rollweek_3, $triggerid_rsm_slv_rdap_rollweek_4);
	__create_trigger_dependency($triggerid_rsm_slv_rdap_rollweek_4, $triggerid_rsm_slv_rdap_rollweek_5);

	__create_trigger_dependency($triggerid_rsm_slv_rdap_rtt_pfailed_1, $triggerid_rsm_slv_rdap_rtt_pfailed_2);
	__create_trigger_dependency($triggerid_rsm_slv_rdap_rtt_pfailed_2, $triggerid_rsm_slv_rdap_rtt_pfailed_3);
	__create_trigger_dependency($triggerid_rsm_slv_rdap_rtt_pfailed_3, $triggerid_rsm_slv_rdap_rtt_pfailed_4);
	__create_trigger_dependency($triggerid_rsm_slv_rdap_rtt_pfailed_4, $triggerid_rsm_slv_rdap_rtt_pfailed_5);

	foreach my $nsip (@nsip_list)
	{
		__create_trigger_dependency($triggerid_rsm_slv_dns_ns_downtime_1->{$nsip}, $triggerid_rsm_slv_dns_ns_downtime_2->{$nsip});
		__create_trigger_dependency($triggerid_rsm_slv_dns_ns_downtime_2->{$nsip}, $triggerid_rsm_slv_dns_ns_downtime_3->{$nsip});
		__create_trigger_dependency($triggerid_rsm_slv_dns_ns_downtime_3->{$nsip}, $triggerid_rsm_slv_dns_ns_downtime_4->{$nsip});
		__create_trigger_dependency($triggerid_rsm_slv_dns_ns_downtime_4->{$nsip}, $triggerid_rsm_slv_dns_ns_downtime_5->{$nsip});
	};
}

sub create_tld_probe($$$$$)
{
	my $tld          = shift;
	my $probe        = shift;
	my $type         = shift;
	my $rdds_enabled = shift;
	my $rdap_enabled = shift;

	if ($USE_TLD_PL)
	{
		return;
	}

	my $hostid = __get_nextid('hosts');

	__create_host($hostid, __get_proxy_hostid($probe), "$tld $probe", 0, "$tld $probe");

	my $interfaceid = __get_nextid('interface');
	__create_interface($interfaceid, $hostid, 1, '127.0.0.1', 10050);

	__link_host_to_template($hostid, "Template Rsmhost Config $tld");
	__link_host_to_template($hostid, "Template DNS Test");
	__link_host_to_template($hostid, "Template RDDS Test");
	__link_host_to_template($hostid, "Template RDAP Test");
	__link_host_to_template($hostid, "Template Probe Config $probe");

	__link_host_to_group($hostid, "TLD $tld");
	__link_host_to_group($hostid, "$probe");
	__link_host_to_group($hostid, "TLD Probe results");
	__link_host_to_group($hostid, "$type Probe results");

	my $itemid_rsm_dns                     = __get_nextid('items'); # 100220
	my $itemid_rsm_dns_mode                = __get_nextid('items'); # 100220  | 100221 | rsm.dns.mode                 |
	my $itemid_rsm_dns_nssok               = __get_nextid('items'); # 100221  | 100222 | rsm.dns.nssok                |
	my $itemid_rsm_dns_protocol            = __get_nextid('items'); # 100222  | 100223 | rsm.dns.protocol             |
	my $itemid_rsm_dns_status              = __get_nextid('items'); # 100223  | 100224 | rsm.dns.status               |
	my $itemid_rsm_dns_testedname          = __get_nextid('items'); # 100225  | 100225 | rsm.dns.testedname           |
	my $itemid_rsm_dnssec_status           = __get_nextid('items'); #         | 100226 | rsm.dnssec.status            |
	my $itemid_rsm_dns_ns_discovery        = __get_nextid('items'); #         | 100227 | rsm.dns.ns.discovery         |
	my $itemid_rsm_dns_nsip_discovery      = __get_nextid('items'); #         | 100228 | rsm.dns.nsip.discovery       |
	my $itemid_rsm_dns_ns_status_prototype = __get_nextid('items'); # 100228  | 100229 | rsm.dns.ns.status[{#NS}]     |
	my $itemid_rsm_dns_nsid_prototype      = __get_nextid('items'); # 100229  | 100230 | rsm.dns.nsid[{#NS},{#IP}]    |
	my $itemid_rsm_dns_rtt_tcp_prototype   = __get_nextid('items'); # 100230  | 100231 | rsm.dns.rtt[{#NS},{#IP},tcp] |
	my $itemid_rsm_dns_rtt_udp_prototype   = __get_nextid('items'); # 100231  | 100232 | rsm.dns.rtt[{#NS},{#IP},udp] |
	my $itemid_rsm_rdds                    = __get_nextid('items'); # 100232
	my $itemid_rsm_rdds_43_ip              = __get_nextid('items'); # 100233
	my $itemid_rsm_rdds_43_rtt             = __get_nextid('items'); # 100234
	my $itemid_rsm_rdds_43_status          = __get_nextid('items'); # 100235
	my $itemid_rsm_rdds_43_target          = __get_nextid('items'); # 100236
	my $itemid_rsm_rdds_43_testedname      = __get_nextid('items'); # 100237
	my $itemid_rsm_rdds_80_ip              = __get_nextid('items'); # 100238
	my $itemid_rsm_rdds_80_rtt             = __get_nextid('items'); # 100239
	my $itemid_rsm_rdds_80_status          = __get_nextid('items'); # 100240
	my $itemid_rsm_rdds_80_target          = __get_nextid('items'); # 100241
	my $itemid_rsm_rdds_status             = __get_nextid('items'); # 100242
	my $itemid_rdap                        = __get_nextid('items'); # 100243
	my $itemid_rdap_ip                     = __get_nextid('items'); # 100244
	my $itemid_rdap_rtt                    = __get_nextid('items'); # 100245
	my $itemid_rdap_status                 = __get_nextid('items'); # 100246
	my $itemid_rdap_target                 = __get_nextid('items'); # 100247
	my $itemid_rdap_testedname             = __get_nextid('items'); # 100248

	my $key_rsm_dns  = 'rsm.dns[{$RSM.TLD},{$RSM.DNS.TESTPREFIX},{$RSM.DNS.NAME.SERVERS},{$RSM.TLD.DNSSEC.ENABLED},{$RSM.TLD.RDDS.ENABLED},{$RSM.TLD.EPP.ENABLED},{$RSM.TLD.DNS.UDP.ENABLED},{$RSM.TLD.DNS.TCP.ENABLED},{$RSM.IP4.ENABLED},{$RSM.IP6.ENABLED},{$RSM.RESOLVER},{$RSM.DNS.UDP.RTT.HIGH},{$RSM.DNS.TCP.RTT.HIGH},{$RSM.DNS.TEST.TCP.RATIO},{$RSM.DNS.TEST.RECOVER.UDP},{$RSM.DNS.TEST.RECOVER.TCP},{$RSM.TLD.DNS.AVAIL.MINNS}]';
	my $key_rsm_rdds = 'rsm.rdds[{$RSM.TLD},{$RSM.TLD.RDDS.43.SERVERS},{$RSM.TLD.RDDS.80.SERVERS},{$RSM.RDDS43.TEST.DOMAIN},{$RSM.RDDS.NS.STRING},{$RSM.RDDS.ENABLED},{$RSM.TLD.RDDS.ENABLED},{$RSM.EPP.ENABLED},{$RSM.TLD.EPP.ENABLED},{$RSM.IP4.ENABLED},{$RSM.IP6.ENABLED},{$RSM.RESOLVER},{$RSM.RDDS.RTT.HIGH},{$RSM.RDDS.MAXREDIRS}]';
	my $key_rdap     = 'rdap[{$RSM.TLD},{$RDAP.TEST.DOMAIN},{$RDAP.BASE.URL},{$RSM.RDDS.MAXREDIRS},{$RSM.RDDS.RTT.HIGH},{$RDAP.TLD.ENABLED},{$RSM.RDAP.ENABLED},{$RSM.IP4.ENABLED},{$RSM.IP6.ENABLED},{$RSM.RESOLVER}]';

	__create_item_from_template($itemid_rsm_dns                    , $hostid, __get_itemid('Template DNS Test' , $key_rsm_dns                  ), $interfaceid, undef          );
	__create_item_from_template($itemid_rsm_dns_mode               , $hostid, __get_itemid('Template DNS Test' , 'rsm.dns.mode'                ), undef       , $itemid_rsm_dns );
	__create_item_from_template($itemid_rsm_dns_nssok              , $hostid, __get_itemid('Template DNS Test' , 'rsm.dns.nssok'               ), undef       , $itemid_rsm_dns );
	__create_item_from_template($itemid_rsm_dns_protocol           , $hostid, __get_itemid('Template DNS Test' , 'rsm.dns.protocol'            ), undef       , $itemid_rsm_dns );
	__create_item_from_template($itemid_rsm_dns_status             , $hostid, __get_itemid('Template DNS Test' , 'rsm.dns.status'              ), undef       , $itemid_rsm_dns );
	__create_item_from_template($itemid_rsm_dns_testedname         , $hostid, __get_itemid('Template DNS Test' , 'rsm.dns.testedname'          ), undef       , $itemid_rsm_dns );
	__create_item_from_template($itemid_rsm_dnssec_status          , $hostid, __get_itemid('Template DNS Test' , 'rsm.dnssec.status'           ), undef       , $itemid_rsm_dns );
	__create_item_from_template($itemid_rsm_dns_ns_discovery       , $hostid, __get_itemid('Template DNS Test' , 'rsm.dns.ns.discovery'        ), undef       , $itemid_rsm_dns );
	__create_item_from_template($itemid_rsm_dns_nsip_discovery     , $hostid, __get_itemid('Template DNS Test' , 'rsm.dns.nsip.discovery'      ), undef       , $itemid_rsm_dns );
	__create_item_from_template($itemid_rsm_dns_ns_status_prototype, $hostid, __get_itemid('Template DNS Test' , 'rsm.dns.ns.status[{#NS}]'    ), undef       , $itemid_rsm_dns );
	__create_item_from_template($itemid_rsm_dns_nsid_prototype     , $hostid, __get_itemid('Template DNS Test' , 'rsm.dns.nsid[{#NS},{#IP}]'   ), undef       , $itemid_rsm_dns );
	__create_item_from_template($itemid_rsm_dns_rtt_tcp_prototype  , $hostid, __get_itemid('Template DNS Test' , 'rsm.dns.rtt[{#NS},{#IP},tcp]'), undef       , $itemid_rsm_dns );
	__create_item_from_template($itemid_rsm_dns_rtt_udp_prototype  , $hostid, __get_itemid('Template DNS Test' , 'rsm.dns.rtt[{#NS},{#IP},udp]'), undef       , $itemid_rsm_dns );
	__create_item_from_template($itemid_rsm_rdds                   , $hostid, __get_itemid('Template RDDS Test', $key_rsm_rdds                 ), $interfaceid, undef           );
	__create_item_from_template($itemid_rsm_rdds_43_ip             , $hostid, __get_itemid('Template RDDS Test', 'rsm.rdds.43.ip'              ), undef       , $itemid_rsm_rdds);
	__create_item_from_template($itemid_rsm_rdds_43_rtt            , $hostid, __get_itemid('Template RDDS Test', 'rsm.rdds.43.rtt'             ), undef       , $itemid_rsm_rdds);
	__create_item_from_template($itemid_rsm_rdds_43_status         , $hostid, __get_itemid('Template RDDS Test', 'rsm.rdds.43.status'          ), undef       , $itemid_rsm_rdds);
	__create_item_from_template($itemid_rsm_rdds_43_target         , $hostid, __get_itemid('Template RDDS Test', 'rsm.rdds.43.target'          ), undef       , $itemid_rsm_rdds);
	__create_item_from_template($itemid_rsm_rdds_43_testedname     , $hostid, __get_itemid('Template RDDS Test', 'rsm.rdds.43.testedname'      ), undef       , $itemid_rsm_rdds);
	__create_item_from_template($itemid_rsm_rdds_80_ip             , $hostid, __get_itemid('Template RDDS Test', 'rsm.rdds.80.ip'              ), undef       , $itemid_rsm_rdds);
	__create_item_from_template($itemid_rsm_rdds_80_rtt            , $hostid, __get_itemid('Template RDDS Test', 'rsm.rdds.80.rtt'             ), undef       , $itemid_rsm_rdds);
	__create_item_from_template($itemid_rsm_rdds_80_status         , $hostid, __get_itemid('Template RDDS Test', 'rsm.rdds.80.status'          ), undef       , $itemid_rsm_rdds);
	__create_item_from_template($itemid_rsm_rdds_80_target         , $hostid, __get_itemid('Template RDDS Test', 'rsm.rdds.80.target'          ), undef       , $itemid_rsm_rdds);
	__create_item_from_template($itemid_rsm_rdds_status            , $hostid, __get_itemid('Template RDDS Test', 'rsm.rdds.status'             ), undef       , $itemid_rsm_rdds);
	__create_item_from_template($itemid_rdap                       , $hostid, __get_itemid('Template RDAP Test', $key_rdap                     ), $interfaceid, undef           );
	__create_item_from_template($itemid_rdap_ip                    , $hostid, __get_itemid('Template RDAP Test', 'rdap.ip'                     ), undef       , $itemid_rdap    );
	__create_item_from_template($itemid_rdap_rtt                   , $hostid, __get_itemid('Template RDAP Test', 'rdap.rtt'                    ), undef       , $itemid_rdap    );
	__create_item_from_template($itemid_rdap_status                , $hostid, __get_itemid('Template RDAP Test', 'rdap.status'                 ), undef       , $itemid_rdap    );
	__create_item_from_template($itemid_rdap_target                , $hostid, __get_itemid('Template RDAP Test', 'rdap.target'                 ), undef       , $itemid_rdap    );
	__create_item_from_template($itemid_rdap_testedname            , $hostid, __get_itemid('Template RDAP Test', 'rdap.testedname'             ), undef       , $itemid_rdap    );

	if (!$rdds_enabled)
	{
		__disable_item($itemid_rsm_rdds);
		__disable_item($itemid_rsm_rdds_43_ip);
		__disable_item($itemid_rsm_rdds_43_rtt);
		__disable_item($itemid_rsm_rdds_43_status);
		__disable_item($itemid_rsm_rdds_43_target);
		__disable_item($itemid_rsm_rdds_43_testedname);
		__disable_item($itemid_rsm_rdds_80_ip);
		__disable_item($itemid_rsm_rdds_80_rtt);
		__disable_item($itemid_rsm_rdds_80_status);
		__disable_item($itemid_rsm_rdds_80_target);
		__disable_item($itemid_rsm_rdds_status);
	}

	if (!$rdap_enabled)
	{
		__disable_item($itemid_rdap);
		__disable_item($itemid_rdap_ip);
		__disable_item($itemid_rdap_rtt);
		__disable_item($itemid_rdap_status);
		__disable_item($itemid_rdap_target);
		__disable_item($itemid_rdap_testedname);
	}

	__create_item_rtdata($itemid_rsm_dns);
	__create_item_rtdata($itemid_rsm_dns_mode);
	__create_item_rtdata($itemid_rsm_dns_nssok);
	__create_item_rtdata($itemid_rsm_dns_protocol);
	__create_item_rtdata($itemid_rsm_dns_status);
	__create_item_rtdata($itemid_rsm_dns_testedname);
	__create_item_rtdata($itemid_rsm_dnssec_status);
	__create_item_rtdata($itemid_rsm_dns_ns_discovery);
	__create_item_rtdata($itemid_rsm_dns_nsip_discovery);
	__create_item_rtdata($itemid_rsm_rdds);
	__create_item_rtdata($itemid_rsm_rdds_43_ip);
	__create_item_rtdata($itemid_rsm_rdds_43_rtt);
	__create_item_rtdata($itemid_rsm_rdds_43_status);
	__create_item_rtdata($itemid_rsm_rdds_43_target);
	__create_item_rtdata($itemid_rsm_rdds_43_testedname);
	__create_item_rtdata($itemid_rsm_rdds_80_ip);
	__create_item_rtdata($itemid_rsm_rdds_80_rtt);
	__create_item_rtdata($itemid_rsm_rdds_80_status);
	__create_item_rtdata($itemid_rsm_rdds_80_target);
	__create_item_rtdata($itemid_rsm_rdds_status);
	__create_item_rtdata($itemid_rdap);
	__create_item_rtdata($itemid_rdap_ip);
	__create_item_rtdata($itemid_rdap_rtt);
	__create_item_rtdata($itemid_rdap_status);
	__create_item_rtdata($itemid_rdap_target);
	__create_item_rtdata($itemid_rdap_testedname);

	__create_item_preproc($itemid_rsm_dns_mode               , 1, 12, '$.mode'                                                                                                   , 0);
	__create_item_preproc($itemid_rsm_dns_nssok              , 1, 12, '$.nssok'                                                                                                  , 0);
	__create_item_preproc($itemid_rsm_dns_protocol           , 1, 12, '$.protocol'                                                                                               , 0);
	__create_item_preproc($itemid_rsm_dns_status             , 1, 12, '$.status'                                                                                                 , 0);
	__create_item_preproc($itemid_rsm_dns_testedname         , 1, 12, '$.testedname'                                                                                             , 0);
	__create_item_preproc($itemid_rsm_dnssec_status          , 1, 12, '$.dnssecstatus'                                                                                           , 1);
	__create_item_preproc($itemid_rsm_dns_ns_discovery       , 1, 12, '$.nss'                                                                                                    , 0);
	__create_item_preproc($itemid_rsm_dns_nsip_discovery     , 1, 12, '$.nsips'                                                                                                  , 0);
	__create_item_preproc($itemid_rsm_dns_ns_status_prototype, 1, 12, '$.nss[?(@.[\'ns\'] == \'{#NS}\')].status.first()'                                                         , 1);
	__create_item_preproc($itemid_rsm_dns_nsid_prototype     , 1, 12, '$.nsips[?(@.[\'ns\'] == \'{#NS}\' && @.[\'ip\'] == \'{#IP}\')].nsid.first()'                              , 1);
	__create_item_preproc($itemid_rsm_dns_rtt_tcp_prototype  , 1, 12, '$.nsips[?(@.[\'ns\'] == \'{#NS}\' && @.[\'ip\'] == \'{#IP}\' && @.[\'protocol\'] == \'tcp\')].rtt.first()', 1);
	__create_item_preproc($itemid_rsm_dns_rtt_udp_prototype  , 1, 12, '$.nsips[?(@.[\'ns\'] == \'{#NS}\' && @.[\'ip\'] == \'{#IP}\' && @.[\'protocol\'] == \'udp\')].rtt.first()', 1);
	__create_item_preproc($itemid_rsm_rdds_43_ip             , 1, 12, '$.rdds43.ip'                                                                                              , 1);
	__create_item_preproc($itemid_rsm_rdds_43_rtt            , 1, 12, '$.rdds43.rtt'                                                                                             , 0);
	__create_item_preproc($itemid_rsm_rdds_43_status         , 1, 12, '$.rdds43.status'                                                                                          , 0);
	__create_item_preproc($itemid_rsm_rdds_43_target         , 1, 12, '$.rdds43.target'                                                                                          , 1);
	__create_item_preproc($itemid_rsm_rdds_43_testedname     , 1, 12, '$.rdds43.testedname'                                                                                      , 1);
	__create_item_preproc($itemid_rsm_rdds_80_ip             , 1, 12, '$.rdds80.ip'                                                                                              , 1);
	__create_item_preproc($itemid_rsm_rdds_80_rtt            , 1, 12, '$.rdds80.rtt'                                                                                             , 0);
	__create_item_preproc($itemid_rsm_rdds_80_status         , 1, 12, '$.rdds80.status'                                                                                          , 0);
	__create_item_preproc($itemid_rsm_rdds_80_target         , 1, 12, '$.rdds80.target'                                                                                          , 1);
	__create_item_preproc($itemid_rsm_rdds_status            , 1, 12, '$.status'                                                                                                 , 0);
	__create_item_preproc($itemid_rdap_ip                    , 1, 12, '$.ip'                                                                                                     , 1);
	__create_item_preproc($itemid_rdap_rtt                   , 1, 12, '$.rtt'                                                                                                    , 0);
	__create_item_preproc($itemid_rdap_status                , 1, 12, '$.status'                                                                                                 , 0);
	__create_item_preproc($itemid_rdap_target                , 1, 12, '$.target'                                                                                                 , 1);
	__create_item_preproc($itemid_rdap_testedname            , 1, 12, '$.testedname'                                                                                             , 1);

	__create_item_discovery($itemid_rsm_dns_ns_status_prototype, $itemid_rsm_dns_ns_discovery  , '');
	__create_item_discovery($itemid_rsm_dns_nsid_prototype     , $itemid_rsm_dns_nsip_discovery, '');
	__create_item_discovery($itemid_rsm_dns_rtt_tcp_prototype  , $itemid_rsm_dns_nsip_discovery, '');
	__create_item_discovery($itemid_rsm_dns_rtt_udp_prototype  , $itemid_rsm_dns_nsip_discovery, '');

	__create_lld_macro_path($itemid_rsm_dns_ns_discovery  ,'{#NS}','$.ns');
	__create_lld_macro_path($itemid_rsm_dns_nsip_discovery,'{#IP}','$.ip');
	__create_lld_macro_path($itemid_rsm_dns_nsip_discovery,'{#NS}','$.ns');
}

sub create_tld_probe_nsip($$$$)
{
	my $tld           = shift;
	my $probe         = shift;
	my $ns_servers_v4 = shift;
	my $ns_servers_v6 = shift;

	if ($USE_TLD_PL)
	{
		return;
	}

	my @nsip_list = sort(split(" ", "$ns_servers_v4 $ns_servers_v6"));

	my $key_rsm_dns = 'rsm.dns[{$RSM.TLD},{$RSM.DNS.TESTPREFIX},{$RSM.DNS.NAME.SERVERS},{$RSM.TLD.DNSSEC.ENABLED},{$RSM.TLD.RDDS.ENABLED},{$RSM.TLD.EPP.ENABLED},{$RSM.TLD.DNS.UDP.ENABLED},{$RSM.TLD.DNS.TCP.ENABLED},{$RSM.IP4.ENABLED},{$RSM.IP6.ENABLED},{$RSM.RESOLVER},{$RSM.DNS.UDP.RTT.HIGH},{$RSM.DNS.TCP.RTT.HIGH},{$RSM.DNS.TEST.TCP.RATIO},{$RSM.DNS.TEST.RECOVER.UDP},{$RSM.DNS.TEST.RECOVER.TCP},{$RSM.TLD.DNS.AVAIL.MINNS}]';

	my $itemid_rsm_dns                     = __get_itemid("$tld $probe", $key_rsm_dns                  );
	my $itemid_rsm_dns_ns_status_prototype = __get_itemid("$tld $probe", 'rsm.dns.ns.status[{#NS}]'    );
	my $itemid_rsm_dns_nsid_prototype      = __get_itemid("$tld $probe", 'rsm.dns.nsid[{#NS},{#IP}]'   );
	my $itemid_rsm_dns_rtt_tcp_prototype   = __get_itemid("$tld $probe", 'rsm.dns.rtt[{#NS},{#IP},tcp]');
	my $itemid_rsm_dns_rtt_udp_prototype   = __get_itemid("$tld $probe", 'rsm.dns.rtt[{#NS},{#IP},udp]');

	my $itemid_rsm_dns_ns_status = {};
	my $itemid_rsm_dns_nsid      = {};
	my $itemid_rsm_dns_rtt_tcp   = {};
	my $itemid_rsm_dns_rtt_udp   = {};

	foreach my $nsip (@nsip_list)
	{
		my ($ns, $ip) = split(',', $nsip);

		if (exists($itemid_rsm_dns_ns_status->{$ns}))
		{
			next;
		}

		$itemid_rsm_dns_ns_status->{$ns} = __get_nextid('items');

		__create_item_from_lld($itemid_rsm_dns_ns_status->{$ns}, $itemid_rsm_dns_ns_status_prototype, $itemid_rsm_dns, {'{#NS}' => $ns});
		__create_item_rtdata($itemid_rsm_dns_ns_status->{$ns});
		__create_item_preproc($itemid_rsm_dns_ns_status->{$ns}, 1, 12, "\$.nss[?(@.['ns'] == '$ns')].status.first()", 1);
		__create_item_discovery($itemid_rsm_dns_ns_status->{$ns}, $itemid_rsm_dns_ns_status_prototype, 'rsm.dns.ns.status[{#NS}]');
	}


	foreach my $nsip (@nsip_list)
	{
		my ($ns, $ip) = split(',', $nsip);

		$itemid_rsm_dns_nsid->{$nsip} = __get_nextid('items');

		__create_item_from_lld($itemid_rsm_dns_nsid->{$nsip}, $itemid_rsm_dns_nsid_prototype, $itemid_rsm_dns, {'{#NS}' => $ns, '{#IP}' => $ip});
		__create_item_rtdata($itemid_rsm_dns_nsid->{$nsip});
		__create_item_preproc($itemid_rsm_dns_nsid->{$nsip}, 1, 12, "\$.nsips[?(@.['ns'] == '$ns' && @.['ip'] == '$ip')].nsid.first()", 1);
		__create_item_discovery($itemid_rsm_dns_nsid->{$nsip}, $itemid_rsm_dns_nsid_prototype, 'rsm.dns.nsid[{#NS},{#IP}]');
	}

	foreach my $nsip (@nsip_list)
	{
		my ($ns, $ip) = split(',', $nsip);

		$itemid_rsm_dns_rtt_tcp->{$nsip} = __get_nextid('items');

		__create_item_from_lld($itemid_rsm_dns_rtt_tcp->{$nsip}, $itemid_rsm_dns_rtt_tcp_prototype, $itemid_rsm_dns, {'{#NS}' => $ns, '{#IP}' => $ip});
		__create_item_rtdata($itemid_rsm_dns_rtt_tcp->{$nsip});
		__create_item_preproc($itemid_rsm_dns_rtt_tcp->{$nsip}, 1, 12, "\$.nsips[?(@.['ns'] == '$ns' && @.['ip'] == '$ip' && @.['protocol'] == 'tcp')].rtt.first()", 1);
		__create_item_discovery($itemid_rsm_dns_rtt_tcp->{$nsip}, $itemid_rsm_dns_rtt_tcp_prototype, 'rsm.dns.rtt[{#NS},{#IP},tcp]');
	}

	foreach my $nsip (@nsip_list)
	{
		my ($ns, $ip) = split(',', $nsip);

		$itemid_rsm_dns_rtt_udp->{$nsip} = __get_nextid('items');

		__create_item_from_lld($itemid_rsm_dns_rtt_udp->{$nsip}, $itemid_rsm_dns_rtt_udp_prototype, $itemid_rsm_dns, {'{#NS}' => $ns, '{#IP}' => $ip});
		__create_item_rtdata($itemid_rsm_dns_rtt_udp->{$nsip});
		__create_item_preproc($itemid_rsm_dns_rtt_udp->{$nsip}, 1, 12, "\$.nsips[?(@.['ns'] == '$ns' && @.['ip'] == '$ip' && @.['protocol'] == 'udp')].rtt.first()", 1);
		__create_item_discovery($itemid_rsm_dns_rtt_udp->{$nsip}, $itemid_rsm_dns_rtt_udp_prototype, 'rsm.dns.rtt[{#NS},{#IP},udp]');
	}
}

sub __db_exec($$)
{
	my $sql    = shift;
	my $params = shift;

	$sql =~ s/ *= */=/g;

	db_exec($sql, $params);
}

sub __create_host($$$$$)
{
	my $hostid       = shift;
	my $proxy_hostid = shift;
	my $host         = shift;
	my $status       = shift;
	my $name         = shift;

	my $sql = "insert into hosts set " .
			"hostid             = ?," .
			"created            = 0," .
			"proxy_hostid       = ?," .
			"host               = ?," .
			"status             = ?," .
			"disable_until      = 0," .
			"error              = ''," .
			"available          = 0," .
			"errors_from        = 0," .
			"lastaccess         = 0," .
			"ipmi_authtype      = -1," .
			"ipmi_privilege     = 2," .
			"ipmi_username      = ''," .
			"ipmi_password      = ''," .
			"ipmi_disable_until = 0," .
			"ipmi_available     = 0," .
			"snmp_disable_until = 0," .
			"snmp_available     = 0," .
			"maintenanceid      = NULL," .
			"maintenance_status = 0," .
			"maintenance_type   = 0," .
			"maintenance_from   = 0," .
			"ipmi_errors_from   = 0," .
			"snmp_errors_from   = 0," .
			"ipmi_error         = ''," .
			"snmp_error         = ''," .
			"jmx_disable_until  = 0," .
			"jmx_available      = 0," .
			"jmx_errors_from    = 0," .
			"jmx_error          = ''," .
			"name               = ?," .
			"info_1             = ''," .
			"info_2             = ''," .
			"flags              = 0," .
			"templateid         = NULL," .
			"description        = ''," .
			"tls_connect        = 1," .
			"tls_accept         = 1," .
			"tls_issuer         = ''," .
			"tls_subject        = ''," .
			"tls_psk_identity   = ''," .
			"tls_psk            = ''," .
			"proxy_address      = ''," .
			"auto_compress      = 1," .
			"discover           = 0";
	my $params = [$hostid, $proxy_hostid, $host, $status, $name];

	__db_exec($sql, $params);
}

sub __create_item($$$$$$$$$$$$$)
{
	my $itemid      = shift;
	my $type        = shift;
	my $hostid      = shift;
	my $name        = shift;
	my $key         = shift;
	my $delay       = shift;
	my $value_type  = shift;
	my $units       = shift;
	my $templateid  = shift;
	my $valuemapid  = shift;
	my $params      = shift;
	my $interfaceid = shift;
	my $description = shift;

	my $sql = "insert into items set " .
			"itemid           = ?," .
			"type             = ?," .
			"snmp_oid         = ''," .
			"hostid           = ?," .
			"name             = ?," .
			"key_             = ?," .
			"delay            = ?," .
			"history          = '90d'," .
			"trends           = '365d'," .
			"status           = 0," .
			"value_type       = ?," .
			"trapper_hosts    = ''," .
			"units            = ?," .
			"formula          = ''," .
			"logtimefmt       = ''," .
			"templateid       = ?," .
			"valuemapid       = ?," .
			"params           = ?," .
			"ipmi_sensor      = ''," .
			"authtype         = 0," .
			"username         = ''," .
			"password         = ''," .
			"publickey        = ''," .
			"privatekey       = ''," .
			"flags            = 0," .
			"interfaceid      = ?," .
			"description      = ?," .
			"inventory_link   = 0," .
			"lifetime         = '30d'," .
			"evaltype         = 0," .
			"jmx_endpoint     = ''," .
			"master_itemid    = NULL," .
			"timeout          = '3s'," .
			"url              = ''," .
			"query_fields     = ''," .
			"posts            = ''," .
			"status_codes     = '200'," .
			"follow_redirects = 1," .
			"post_type        = 0," .
			"http_proxy       = ''," .
			"headers          = ''," .
			"retrieve_mode    = 0," .
			"request_method   = 0," .
			"output_format    = 0," .
			"ssl_cert_file    = ''," .
			"ssl_key_file     = ''," .
			"ssl_key_password = ''," .
			"verify_peer      = 0," .
			"verify_host      = 0," .
			"allow_traps      = 0," .
			"discover         = 0";
	my $query_params = [$itemid, $type, $hostid, $name, $key, $delay, $value_type, $units, $templateid, $valuemapid, $params, $interfaceid, $description];

	__db_exec($sql, $query_params);
}

sub __create_item_from_template($$$$$)
{
	my $itemid        = shift;
	my $hostid        = shift;
	my $templateid    = shift;
	my $interfaceid   = shift;
	my $master_itemid = shift;

	my $sql = "insert into items (" .
				"itemid,type,snmp_oid,hostid,name,key_,delay,history,trends,status,value_type," .
				"trapper_hosts,units,formula,logtimefmt,templateid,valuemapid,params,ipmi_sensor," .
				"authtype,username,password,publickey,privatekey,flags,interfaceid,description," .
				"inventory_link,lifetime,evaltype,jmx_endpoint,master_itemid,timeout,url,query_fields," .
				"posts,status_codes,follow_redirects,post_type,http_proxy,headers,retrieve_mode," .
				"request_method,output_format,ssl_cert_file,ssl_key_file,ssl_key_password,verify_peer," .
				"verify_host,allow_traps,discover" .
			") select" .
				" ?,type,snmp_oid,?,name,key_,delay,history,trends,status,value_type," .
				"trapper_hosts,units,formula,logtimefmt,?,valuemapid,params,ipmi_sensor," .
				"authtype,username,password,publickey,privatekey,flags,?,description," .
				"inventory_link,lifetime,evaltype,jmx_endpoint,?,timeout,url,query_fields," .
				"posts,status_codes,follow_redirects,post_type,http_proxy,headers,retrieve_mode," .
				"request_method,output_format,ssl_cert_file,ssl_key_file,ssl_key_password,verify_peer," .
				"verify_host,allow_traps,discover" .
			" from items where itemid=?";
	my $params = [$itemid, $hostid, $templateid, $interfaceid, $master_itemid, $templateid];

	__db_exec($sql, $params);
}

sub __create_item_from_lld($$$$)
{
	my $itemid           = shift;
	my $prototype_itemid = shift;
	my $master_itemid    = shift;
	my $replacements     = shift;

	my $item_key = db_select_value("select key_ from items where itemid=?", [$prototype_itemid]);

	foreach my $key (keys(%{$replacements}))
	{
		$item_key =~ s/$key/$replacements->{$key}/g;
	}

	my $sql = "insert into items (" .
				"itemid,type,snmp_oid,hostid,name,key_,delay,history,trends,status,value_type," .
				"trapper_hosts,units,formula,logtimefmt,templateid,valuemapid,params,ipmi_sensor," .
				"authtype,username,password,publickey,privatekey,flags,interfaceid,description," .
				"inventory_link,lifetime,evaltype,jmx_endpoint,master_itemid,timeout,url,query_fields," .
				"posts,status_codes,follow_redirects,post_type,http_proxy,headers,retrieve_mode," .
				"request_method,output_format,ssl_cert_file,ssl_key_file,ssl_key_password,verify_peer," .
				"verify_host,allow_traps,discover" .
			") select" .
				"?,type,snmp_oid,hostid,name,?,delay,history,trends,status,value_type," .
				"trapper_hosts,units,formula,logtimefmt,?,valuemapid,params,ipmi_sensor," .
				"authtype,username,password,publickey,privatekey,?,interfaceid,description," .
				"inventory_link,lifetime,evaltype,jmx_endpoint,?,timeout,url,query_fields," .
				"posts,status_codes,follow_redirects,post_type,http_proxy,headers,retrieve_mode," .
				"request_method,output_format,ssl_cert_file,ssl_key_file,ssl_key_password,verify_peer," .
				"verify_host,allow_traps,discover" .
			" from items where itemid=?";
	my $params = [$itemid, $item_key, undef, 4, $master_itemid, $prototype_itemid];

	__db_exec($sql, $params);
}

sub __create_interface($$$$$)
{
	my $interfaceid = shift;
	my $hostid      = shift;
	my $type        = shift;
	my $ip          = shift;
	my $port        = shift;

	my $sql = "insert into interface set " .
			"interfaceid = ?," .
			"hostid      = ?," .
			"main        = 1," . # default: 0
			"type        = ?," .
			"useip       = 1," .
			"ip          = ?," .
			"dns         = ''," .
			"port        = ?";
	my $params = [$interfaceid, $hostid, $type, $ip, $port];

	__db_exec($sql, $params);
}

sub __create_host_group($$)
{
	my $groupid = shift;
	my $name    = shift;

	my $sql = "insert into hstgrp set " .
			"groupid  = ?," .
			"name     = ?," .
			"internal = 0," .
			"flags    = 0";
	my $params = [$groupid, $name];

	__db_exec($sql, $params);
}

sub __create_host_macro($$$$)
{
	my $hostid      = shift;
	my $macro       = shift;
	my $value       = shift;
	my $description = shift;

	my $sql = "insert into hostmacro set " .
			"hostmacroid = ?," .
			"hostid      = ?," .
			"macro       = ?," .
			"value       = ?," .
			"description = ?," .
			"type        = 0";
	my $params = [__get_nextid('hostmacro'), $hostid, $macro, $value, $description];

	__db_exec($sql, $params);
}

sub __create_item_rtdata($)
{
	my $itemid = shift;

	my $sql = "insert into item_rtdata set " .
			"itemid      = ?," .
			"lastlogsize = 0," .
			"state       = 0," .
			"mtime       = 0," .
			"error       = ''";
	my $params = [$itemid];

	__db_exec($sql, $params);
}

sub __create_item_preproc($$$$$)
{
	my $itemid        = shift;
	my $step          = shift;
	my $type          = shift;
	my $params        = shift;
	my $error_handler = shift;

	my $sql = "insert into item_preproc set " .
			"item_preprocid       = ?," .
			"itemid               = ?," .
			"step                 = ?," .
			"type                 = ?," .
			"params               = ?," .
			"error_handler        = ?," .
			"error_handler_params = ''";
	my $query_params = [__get_nextid('item_preproc'), $itemid, $step, $type, $params, $error_handler];

	__db_exec($sql, $query_params);
}

sub __create_item_discovery($$$)
{
	my $itemid        = shift;
	my $parent_itemid = shift;
	my $key           = shift;

	my $sql = "insert into item_discovery set " .
			"itemdiscoveryid = ?," .
			"itemid          = ?," .
			"parent_itemid   = ?," .
			"key_            = ?," .
			"lastcheck       = 0," .
			"ts_delete       = 0";
	my $params = [__get_nextid('item_discovery'), $itemid, $parent_itemid, $key];

	__db_exec($sql, $params);
}

sub __create_lld_macro_path($$$)
{
	my $itemid    = shift;
	my $lld_macro = shift;
	my $path      = shift;

	my $sql = "insert into lld_macro_path set " .
			"lld_macro_pathid = ?," .
			"itemid           = ?," .
			"lld_macro        = ?," .
			"path             = ?";
	my $params = [__get_nextid('lld_macro_path'), $itemid, $lld_macro, $path];

	__db_exec($sql, $params);
}

sub __create_trigger($$$$$$)
{
	my $triggerid           = shift;
	my $expression          = shift;
	my $description         = shift;
	my $priority            = shift;
	my $templateid          = shift;
	my $recovery_expression = shift;

	my $recovery_mode = length($recovery_expression) ? 1 : 0;

	my $sql = "insert into triggers set " .
			"triggerid           = ?," .
			"expression          = ?," .
			"description         = ?," .
			"url                 = ''," .
			"status              = 0," .
			"value               = 0," .
			"priority            = ?," .
			"lastchange          = 0," .
			"comments            = ''," .
			"error               = ''," .
			"templateid          = ?," .
			"type                = 0," .
			"state               = 0," .
			"flags               = 0," .
			"recovery_mode       = ?," .
			"recovery_expression = ?," .
			"correlation_mode    = 0," .
			"correlation_tag     = ''," .
			"manual_close        = 0," .
			"opdata              = ''," .
			"discover            = 0";
	my $params = [$triggerid, $expression, $description, $priority, $templateid, $recovery_mode, $recovery_expression];

	__db_exec($sql, $params);
}

sub __create_function($$$$$)
{
	my $functionid = shift;
	my $itemid     = shift;
	my $triggerid  = shift;
	my $name       = shift;
	my $parameter  = shift;

	my $sql = "insert into functions set " .
			"functionid = ?," .
			"itemid     = ?," .
			"triggerid  = ?," .
			"name       = ?," .
			"parameter  = ?";
	my $params = [$functionid, $itemid, $triggerid, $name, $parameter];

	__db_exec($sql, $params);
}

sub __create_trigger_dependency($$)
{
	my $triggerid_down = shift;
	my $triggerid_up   = shift;

	my $sql = "insert into trigger_depends set " .
			"triggerdepid   = ?," .
			"triggerid_down = ?," .
			"triggerid_up   = ?";
	my $params = [__get_nextid('trigger_depends'), $triggerid_down, $triggerid_up];

	__db_exec($sql, $params);
}

sub __link_host_to_template($$)
{
	my $hostid   = shift;
	my $template = shift;

	my $templateid = db_select_value("select hostid from hosts where host=?", [$template]);

	__link_host_to_templateid($hostid, $templateid);
}

sub __link_host_to_templateid($$)
{
	my $hostid     = shift;
	my $templateid = shift;

	my $sql = "insert into hosts_templates set " .
			"hosttemplateid = ?," .
			"hostid         = ?," .
			"templateid     = ?";
	my $params = [__get_nextid('hosts_templates'), $hostid, $templateid];

	__db_exec($sql, $params);
}

sub __link_host_to_group($$)
{
	my $hostid = shift;
	my $group  = shift;

	my $groupid = db_select_value("select groupid from hstgrp where name=?", [$group]);

	__link_host_to_groupid($hostid, $groupid);
}

sub __link_host_to_groupid($$)
{
	my $hostid  = shift;
	my $groupid = shift;

	my $sql = "insert into hosts_groups set " .
			"hostgroupid = ?," .
			"hostid      = ?," .
			"groupid     = ?";
	my $params = [__get_nextid('hosts_groups'), $hostid, $groupid];

	__db_exec($sql, $params);
}

sub __disable_item($)
{
	my $itemid = shift;

	db_exec("update items set status=1 where itemid=?", [$itemid]);
}

sub __get_proxy_hostid($)
{
	my $host = shift;

	return db_select_value("select hostid from hosts where host=? and status=6", [$host]);
}

sub __get_itemid($$)
{
	my $host = shift;
	my $key  = shift;

	my $sql = "select items.itemid from items inner join hosts on hosts.hostid=items.hostid where hosts.host=? and items.key_=?";
	my $params = [$host, $key];

	return db_select_value($sql, $params);
}

sub __get_valuemapid($)
{
	my $name = shift;

	return db_select_value("select valuemapid from valuemaps where name=?", [$name]);
}

sub __get_triggerid($$)
{
	my $host = shift;
	my $name = shift;

	my $sql = "select" .
			" triggers.triggerid" .
		" from" .
			" hosts" .
			" inner join items on items.hostid=hosts.hostid" .
			" inner join functions on functions.itemid=items.itemid" .
			" inner join triggers on triggers.expression like concat('%{',functions.functionid,'}%')" .
		" where" .
			" hosts.host=? and" .
			" triggers.description=?" .
		" group by" .
			" triggers.triggerid";
	my $params = [$host, $name];

	return db_select_value($sql, $params);
}

sub __get_nextid($)
{
	my $table = shift;

	my $id_field = {
		'auditlog'        => 'auditid',
		'functions'       => 'functionid',
		'hostmacro'       => 'hostmacroid',
		'hosts'           => 'hostid',
		'hosts_groups'    => 'hostgroupid',
		'hosts_templates' => 'hosttemplateid',
		'hstgrp'          => 'groupid',
		'interface'       => 'interfaceid',
		'item_preproc'    => 'item_preprocid',
		'items'           => 'itemid',
		'trigger_depends' => 'triggerdepid',
		'triggers'        => 'triggerid',
		'item_discovery'  => 'itemdiscoveryid',
		'lld_macro_path'  => 'lld_macro_pathid',
	};

	fail("unknown table: '$table'") if (!exists($id_field->{$table}));

	my $max_id_1 = db_select("select nextid from ids where table_name=?", [$table]);
	my $max_id_2 = db_select("select max($id_field->{$table}) from $table");
	my $max_id = max($max_id_1->[0][0] // 0, $max_id_2->[0][0] // 0);

	my $next_id = $max_id + 1;

	my $sql = "insert into ids set table_name=?,field_name=?,nextid=? on duplicate key update nextid=values(nextid)";
	my $params = [$table, $id_field->{$table}, $next_id];
	db_exec($sql, $params);

	return $next_id;
}

sub __is_rdap_standalone()
{
	my $ts = db_select_value("select value from globalmacro where macro=?", ['{$RSM.RDAP.STANDALONE}']);

	return $ts && time() >= $ts ? 1 : 0;
}

1;
