package TLD_constants;

use strict;
use warnings;
use base 'Exporter';

use constant true => 1;
use constant false => 0;

use constant MONITORING_TARGET_REGISTRY  => "registry";
use constant MONITORING_TARGET_REGISTRAR => "registrar";

use constant LINUX_TEMPLATEID			=> 10001;	# Template "Template OS Linux by Zabbix agent"
use constant APP_ZABBIX_PROXY_TEMPLATEID	=> 10058;	# Template "Template App Zabbix Proxy"
use constant PROBE_ERRORS_TEMPLATEID		=> 99990;	# Template "Template Probe Errors"
use constant RDAP_TEMPLATEID			=> 99980;	# Template "Template RDAP"

use constant TEMPLATES_TLD_GROUPID		=> 240;		# Host group "Templates - TLD"
use constant PROBES_GROUPID			=> 120;		# Host group "Probes"
use constant PROBES_MON_GROUPID			=> 130;		# Host group "Probes - Mon"
use constant TLDS_GROUPID			=> 140;		# Host group "TLDs"
use constant TLD_PROBE_RESULTS_GROUPID		=> 190;		# Host group "TLD Probe results"

use constant VALUE_TYPE_AVAIL => 0;
use constant VALUE_TYPE_PERC  => 1;
use constant VALUE_TYPE_NUM   => 2;

use constant HOST_STATUS_MONITORED     => 0;
use constant HOST_STATUS_NOT_MONITORED => 1;

use constant HOST_STATUS_PROXY_ACTIVE  => 5;
use constant HOST_STATUS_PROXY_PASSIVE => 6;

use constant HOST_ENCRYPTION_PSK => 2;

use constant ITEM_STATUS_ACTIVE   => 0;
use constant ITEM_STATUS_DISABLED => 1;

use constant TRIGGER_STATUS_ENABLED  => 0;
use constant TRIGGER_STATUS_DISABLED => 1;

use constant INTERFACE_TYPE_AGENT => 1;

use constant DEFAULT_MAIN_INTERFACE => {
	'type'	=> INTERFACE_TYPE_AGENT,
	'main'	=> true,
	'useip'	=> true,
	'ip'	=> '127.0.0.1',
	'dns'	=> '',
	'port'	=> '10050'
};

use constant ITEM_VALUE_TYPE_FLOAT  => 0;
use constant ITEM_VALUE_TYPE_STR    => 1;
use constant ITEM_VALUE_TYPE_LOG    => 2;
use constant ITEM_VALUE_TYPE_UINT64 => 3;
use constant ITEM_VALUE_TYPE_TEXT   => 4;

use constant ITEM_TYPE_ZABBIX        => 0;
use constant ITEM_TYPE_TRAPPER       => 2;
use constant ITEM_TYPE_SIMPLE        => 3;
use constant ITEM_TYPE_INTERNAL      => 5;
use constant ITEM_TYPE_ZABBIX_ACTIVE => 7;
use constant ITEM_TYPE_AGGREGATE     => 8;
use constant ITEM_TYPE_EXTERNAL      => 10;
use constant ITEM_TYPE_CALCULATED    => 15;

use constant ZBX_EC_INTERNAL_FIRST => -1;
use constant ZBX_EC_INTERNAL_LAST  => -199;

# define ranges of DNSSEC error codes of DNS UDP/TCP
use constant ZBX_EC_DNS_UDP_DNSSEC_FIRST => -401;	# DNS UDP - The TLD is configured as DNSSEC-enabled, but no DNSKEY was found in the apex
use constant ZBX_EC_DNS_UDP_DNSSEC_LAST  => -427;	# DNS UDP - Malformed DNSSEC response
use constant ZBX_EC_DNS_TCP_DNSSEC_FIRST => -801;	# DNS TCP - The TLD is configured as DNSSEC-enabled, but no DNSKEY was found in the apex
use constant ZBX_EC_DNS_TCP_DNSSEC_LAST  => -827;	# DNS TCP - Malformed DNSSEC response

use constant CFG_DEFAULT_RDDS_NS_STRING => 'Name Server:';

use constant PROBE_KEY_ONLINE => 'rsm.probe.online';

use constant CONFIGVALUE_DNS_UDP_RTT_HIGH_ITEMID => 100011;	# itemid of rsm.configvalue[RSM.DNS.UDP.RTT.HIGH] item

use constant RSM_VALUE_MAPPINGS => {
	'service_state'   => 1,
	'rsm_dns_rtt'     => 120,
	'rsm_rdds_rtt'    => 130,
	'rsm_rdds_result' => 140,
	'rsm_epp_rtt'     => 150,
	'rsm_epp_result'  => 160,
	'rsm_avail'       => 110,
	'rsm_probe'       => 100
};

# 'RSM RDDS result' value mappings
use constant RDDS_DOWN    => 0;
use constant RDDS_UP      => 1;
use constant RDDS_43_ONLY => 2;
use constant RDDS_80_ONLY => 3;

use constant AUDIT_RESOURCE_INCIDENT => 32;

use constant RSM_TRIGGER_THRESHOLDS => {
	'1' => {'threshold' => '10', 'priority' => 2},
	'2' => {'threshold' => '25', 'priority' => 3},
	'3' => {'threshold' => '50', 'priority' => 3},
	'4' => {'threshold' => '75', 'priority' => 4},
	'5' => {'threshold' => '100', 'priority' => 5}
};

use constant CFG_GLOBAL_MACROS => {
	'{$RSM.DNS.UDP.DELAY}' => '', 
	'{$RSM.DNS.TCP.DELAY}' => '', 
	'{$RSM.RDDS.DELAY}' => '', 
	'{$RSM.EPP.DELAY}' => '',
	'{$RSM.RDAP.STANDALONE}' => ''
};

use constant CFG_PROBE_STATUS_DELAY => 60;

use constant APP_SLV_MONTHLY  => 'SLV monthly';
use constant APP_SLV_ROLLWEEK => 'SLV rolling week';
use constant APP_SLV_PARTTEST => 'SLV particular test';
use constant APP_SLV_CURMON   => 'SLV current month';

use constant TLD_TYPE_G     => 'gTLD';
use constant TLD_TYPE_CC    => 'ccTLD';
use constant TLD_TYPE_OTHER => 'otherTLD';
use constant TLD_TYPE_TEST  => 'testTLD';

use constant TLD_TYPE_GROUPIDS	=> {
	TLD_TYPE_G,	150,	# Host group "gTLD"
	TLD_TYPE_CC,	160,	# Host group "ccTLD"
	TLD_TYPE_TEST,	170,	# Host group "testTLD"
	TLD_TYPE_OTHER,	180	# Host group "otherTLD"
};

use constant TLD_TYPE_PROBE_RESULTS_GROUPIDS	=> {
	TLD_TYPE_G,	200,	# Host group "gTLD Probe results"
	TLD_TYPE_CC,	210,	# Host group "ccTLD Probe results"
	TLD_TYPE_TEST,	220,	# Host group "testTLD Probe results"
	TLD_TYPE_OTHER,	230	# Host group "otherTLD Probe results"
};

use constant CFG_MACRO_DESCRIPTION => {
	'{$RSM.RDDS.NS.STRING}' => 'What to look for in RDDS output, e.g. "Name Server:"',
	'{$RSM.TLD}' => 'Name of this TLD, e.g. "zabbix"',
	'{$RSM.RDDS.TESTPREFIX}' => 'Prefix for RDDS tests of this TLD, e.g. "whois"',
	'{$RSM.IP4.ENABLED}' => 'Indicates whether the probe supports IPv4',
	'{$RSM.IP6.ENABLED}' => 'Indicates whether the probe supports IPv6',
	'{$RSM.RDAP.ENABLED}' => 'Indicates whether the probe supports RDAP protocol',
	'{$RSM.DNS.TESTPREFIX}' => 'Prefix for DNS tests, e.g. nonexistent',
	'{$RSM.RESOLVER}' => 'DNS resolver used by the probe',
	'{$RDAP.TEST.DOMAIN}' => 'Test domain for RDAP queries, e.g. whois.zabbix',
	'{$RSM.TLD.RDDS.ENABLED}' => 'Indicates whether RDDS is enabled for this TLD',
	'{$RSM.EPP.ENABLED}' => 'Indicates whether EPP is enabled on probe',
	'{$RDAP.BASE.URL}' => 'Base URL for RDAP queries, e.g. http://whois.zabbix ',
	'{$RSM.TLD.EPP.ENABLED}' => 'Indicates whether EPP is enabled for this TLD',
	'{$RSM.RDDS.ENABLED}' => 'Indicates whether the probe supports RDDS protocol',
	'{$RDAP.TLD.ENABLED}' => 'Indicates whether RDAP is enabled for this TLD',
	'{$RSM.TLD.DNSSEC.ENABLED}' => 'Indicates whether DNSSEC is enabled for this TLD',
};

our @EXPORT_OK = qw(
	true
	false
	MONITORING_TARGET_REGISTRY
	MONITORING_TARGET_REGISTRAR
	LINUX_TEMPLATEID
	APP_ZABBIX_PROXY_TEMPLATEID
	PROBE_ERRORS_TEMPLATEID
	RDAP_TEMPLATEID
	CONFIGVALUE_DNS_UDP_RTT_HIGH_ITEMID
	TEMPLATES_TLD_GROUPID
	PROBES_GROUPID
	PROBES_MON_GROUPID
	TLDS_GROUPID
	TLD_PROBE_RESULTS_GROUPID
	TLD_TYPE_GROUPIDS
	TLD_TYPE_PROBE_RESULTS_GROUPIDS
	VALUE_TYPE_AVAIL
	VALUE_TYPE_PERC
	VALUE_TYPE_NUM
	ZBX_EC_INTERNAL_FIRST
	ZBX_EC_INTERNAL_LAST
	ZBX_EC_DNS_UDP_DNSSEC_FIRST
	ZBX_EC_DNS_UDP_DNSSEC_LAST
	ZBX_EC_DNS_TCP_DNSSEC_FIRST
	ZBX_EC_DNS_TCP_DNSSEC_LAST
	RSM_VALUE_MAPPINGS CFG_PROBE_STATUS_DELAY
	PROBE_KEY_ONLINE
	CFG_DEFAULT_RDDS_NS_STRING RSM_TRIGGER_THRESHOLDS CFG_GLOBAL_MACROS
	HOST_STATUS_MONITORED HOST_STATUS_NOT_MONITORED HOST_STATUS_PROXY_ACTIVE HOST_STATUS_PROXY_PASSIVE HOST_ENCRYPTION_PSK ITEM_STATUS_ACTIVE
	ITEM_STATUS_DISABLED INTERFACE_TYPE_AGENT DEFAULT_MAIN_INTERFACE TRIGGER_STATUS_DISABLED TRIGGER_STATUS_ENABLED
	ITEM_VALUE_TYPE_FLOAT ITEM_VALUE_TYPE_STR ITEM_VALUE_TYPE_LOG ITEM_VALUE_TYPE_UINT64 ITEM_VALUE_TYPE_TEXT
	ITEM_TYPE_ZABBIX ITEM_TYPE_TRAPPER ITEM_TYPE_SIMPLE ITEM_TYPE_INTERNAL ITEM_TYPE_ZABBIX_ACTIVE ITEM_TYPE_AGGREGATE ITEM_TYPE_EXTERNAL ITEM_TYPE_CALCULATED
	APP_SLV_MONTHLY APP_SLV_ROLLWEEK APP_SLV_PARTTEST APP_SLV_CURMON TLD_TYPE_G TLD_TYPE_CC TLD_TYPE_OTHER TLD_TYPE_TEST
	RDDS_DOWN RDDS_UP RDDS_43_ONLY RDDS_80_ONLY AUDIT_RESOURCE_INCIDENT CFG_MACRO_DESCRIPTION
);

our %EXPORT_TAGS = (
	general => [ qw(true false) ],
	templates => [ qw(
			LINUX_TEMPLATEID
			APP_ZABBIX_PROXY_TEMPLATEID
			PROBE_ERRORS_TEMPLATEID
			RDAP_TEMPLATEID) ],
	groups => [ qw(
			TEMPLATES_TLD_GROUPID
			PROBES_GROUPID
			PROBES_MON_GROUPID
			TLDS_GROUPID
			TLD_PROBE_RESULTS_GROUPID
			TLD_TYPE_GROUPIDS
			TLD_TYPE_PROBE_RESULTS_GROUPIDS) ],
	items => [ qw(PROBE_KEY_ONLINE CONFIGVALUE_DNS_UDP_RTT_HIGH_ITEMID) ],
	value_types => [ qw(VALUE_TYPE_AVAIL VALUE_TYPE_PERC VALUE_TYPE_NUM) ],
	ec => [ qw(
			ZBX_EC_INTERNAL_FIRST
			ZBX_EC_INTERNAL_LAST
			ZBX_EC_DNS_UDP_DNSSEC_FIRST
			ZBX_EC_DNS_UDP_DNSSEC_LAST
			ZBX_EC_DNS_TCP_DNSSEC_FIRST
			ZBX_EC_DNS_TCP_DNSSEC_LAST) ],
	api => [ qw(HOST_STATUS_MONITORED HOST_STATUS_NOT_MONITORED HOST_STATUS_PROXY_ACTIVE HOST_STATUS_PROXY_PASSIVE ITEM_STATUS_ACTIVE
			ITEM_STATUS_DISABLED INTERFACE_TYPE_AGENT DEFAULT_MAIN_INTERFACE
			ITEM_VALUE_TYPE_FLOAT ITEM_VALUE_TYPE_STR ITEM_VALUE_TYPE_LOG ITEM_VALUE_TYPE_UINT64 ITEM_VALUE_TYPE_TEXT
			ITEM_TYPE_ZABBIX ITEM_TYPE_TRAPPER ITEM_TYPE_SIMPLE ITEM_TYPE_INTERNAL ITEM_TYPE_ZABBIX_ACTIVE
			ITEM_TYPE_AGGREGATE ITEM_TYPE_EXTERNAL ITEM_TYPE_CALCULATED
			TRIGGER_STATUS_DISABLED TRIGGER_STATUS_ENABLED
			MONITORING_TARGET_REGISTRY MONITORING_TARGET_REGISTRAR)],
	config => [ qw(CFG_PROBE_STATUS_DELAY CFG_DEFAULT_RDDS_NS_STRING RSM_VALUE_MAPPINGS RSM_TRIGGER_THRESHOLDS
			CFG_GLOBAL_MACROS TLD_TYPE_G TLD_TYPE_CC TLD_TYPE_OTHER TLD_TYPE_TEST
			RDDS_DOWN RDDS_UP RDDS_43_ONLY RDDS_80_ONLY AUDIT_RESOURCE_INCIDENT CFG_MACRO_DESCRIPTION) ],
	slv => [ qw(APP_SLV_MONTHLY APP_SLV_ROLLWEEK APP_SLV_PARTTEST APP_SLV_CURMON) ],
	tls => [ qw(HOST_ENCRYPTION_PSK) ]
);

1;
