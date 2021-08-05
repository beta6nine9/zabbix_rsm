<?php
/*
** Zabbix
** Copyright (C) 2001-2019 Zabbix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/


/*
 * These numbers must be in sync with Backend!
 */

// SLA monitoring start year.
define('SLA_MONITORING_START_YEAR',	2014);

// SLA monitoring extra filter value.
define('SLA_MONITORING_SLV_FILTER_NON_ZERO',	-1);
define('SLA_MONITORING_SLV_FILTER_ANY',			-2);

// SLA monitoring services.
define('RSM_DNS',			0);
define('RSM_DNSSEC',		1);
define('RSM_RDDS',			2);
define('RSM_EPP',			3);
define('RSM_RDAP',			4); // Standalone RDAP

// SLA monitoring macros.
define('RSM_PAGE_SLV',				'{$RSM.ROLLWEEK.THRESHOLDS}');
define('RSM_ROLLWEEK_SECONDS',		'{$RSM.ROLLWEEK.SECONDS}');
define('RSM_DNS_DELAY',				'{$RSM.DNS.DELAY}');
define('RSM_RDDS_DELAY',			'{$RSM.RDDS.DELAY}');
define('RSM_RDDS_ENABLED',			'{$RSM.RDDS.ENABLED}');
define('RSM_RDAP_ENABLED',			'{$RSM.RDAP.ENABLED}');
define('RSM_TLD_DNSSEC_ENABLED',	'{$RSM.TLD.DNSSEC.ENABLED}');
define('RSM_TLD_EPP_ENABLED',		'{$RSM.TLD.EPP.ENABLED}');
define('RSM_TLD_RDDS_ENABLED',		'{$RSM.TLD.RDDS.ENABLED}');
define('RSM_TLD_RDDS43_ENABLED',	'{$RSM.TLD.RDDS43.ENABLED}');
define('RSM_TLD_RDDS80_ENABLED',	'{$RSM.TLD.RDDS80.ENABLED}');
define('RSM_PROBE_AVAIL_LIMIT',		'{$RSM.PROBE.AVAIL.LIMIT}');
define('RSM_RDAP_TLD_ENABLED',		'{$RDAP.TLD.ENABLED}');
define('RSM_SLV_DNS_NS_UPD',		'{$RSM.SLV.DNS.NS.UPD}');
define('RSM_DNS_UPDATE_TIME',		'{$RSM.DNS.UPDATE.TIME}');
define('RDAP_BASE_URL',				'{$RDAP.BASE.URL}');
define('RDDS_ENABLED',				'rdds.enabled');
define('RDAP_ENABLED',				'rdap.enabled');

// Wow often do we save history of Probe ONLINE status.
define('RSM_PROBE_DELAY',			60);	// probe status delay

// SLA monitoring rolling week items keys.
define('RSM_SLV_DNS_ROLLWEEK',		'rsm.slv.dns.rollweek');
define('RSM_SLV_DNSSEC_ROLLWEEK',	'rsm.slv.dnssec.rollweek');
define('RSM_SLV_RDDS_ROLLWEEK',		'rsm.slv.rdds.rollweek');
define('RSM_SLV_RDAP_ROLLWEEK',		'rsm.slv.rdap.rollweek');
define('RSM_SLV_EPP_ROLLWEEK',		'rsm.slv.epp.rollweek');

// Template names.
define('TEMPLATE_NAME_TLD_CONFIG', 'Template Rsmhost Config %s');

// SLA monitoring availability items keys.
define('RSM_SLV_DNS_AVAIL',					'rsm.slv.dns.avail');
define('RSM_SLV_DNS_NS_DOWNTIME',			'rsm.slv.dns.ns.downtime');
define('RSM_SLV_DNS_TCP_RTT_PFAILED',		'rsm.slv.dns.tcp.rtt.pfailed');
define('RSM_SLV_DNS_TCP_RTT_FAILED',		'rsm.slv.dns.tcp.rtt.failed');
define('RSM_SLV_DNS_TCP_RTT_MAX',			'rsm.slv.dns.tcp.rtt.max');
define('RSM_SLV_DNS_UDP_RTT_PFAILED',		'rsm.slv.dns.udp.rtt.pfailed');
define('RSM_SLV_DNS_UDP_RTT_FAILED',		'rsm.slv.dns.udp.rtt.failed');
define('RSM_SLV_DNS_UDP_RTT_MAX',			'rsm.slv.dns.udp.rtt.max');
define('RSM_SLV_DNS_UDP_UPD_PFAILED',		'rsm.slv.dns.udp.upd.pfailed');
define('RSM_SLV_DNS_UDP_UPD_FAILED',		'rsm.slv.dns.udp.upd.failed');
define('RSM_SLV_DNS_UDP_UPD_MAX',			'rsm.slv.dns.udp.upd.max');
define('RSM_SLV_RDDS_DOWNTIME',				'rsm.slv.rdds.downtime');
define('RSM_SLV_RDDS_RTT_PFAILED',			'rsm.slv.rdds.rtt.pfailed');
define('RSM_SLV_RDDS_UPD_FAILED',			'rsm.slv.rdds.upd.failed');
define('RSM_SLV_RDDS_UPD_MAX',				'rsm.slv.rdds.upd.max');
define('RSM_SLV_RDDS_AVAIL',				'rsm.slv.rdds.avail');
define('RSM_SLV_RDAP_AVAIL',				'rsm.slv.rdap.avail');
define('RSM_SLV_EPP_AVAIL',					'rsm.slv.epp.avail');
define('RSM_SLV_DNSSEC_AVAIL',				'rsm.slv.dnssec.avail');

// RDAP standalone.
define('RSM_RDAP_STANDALONE', '{$RSM.RDAP.STANDALONE}');

// "RSM Service Availability" value mapping:
define('DOWN',	0);	// Down

// SLA reports graph names.
define('DNS_SERVICE_AVAILABILITY_GRAPH_1',		'DNS Service Availability - Accumulated minutes of downtime');
define('DNS_NS_AVAILABILITY_GRAPH_1',			'DNS NS Availability - [$NS$] Accumulated minutes of downtime');
define('DNS_NS_AVAILABILITY_GRAPH_2',			'DNS NS Availability - [$NS$] UP/DOWN');
define('TCP_DNS_RESOLUTION_RTT_TCP_GRAPH_1',	'DNS TCP Resolution RTT - Average');
define('TCP_DNS_RESOLUTION_RTT_TCP_GRAPH_2',	'DNS TCP Resolution RTT - Ratio of Failed tests');
define('UDP_DNS_RESOLUTION_RTT_UDP_GRAPH_1',	'DNS UDP Resolution RTT - Average');
define('UDP_DNS_RESOLUTION_RTT_UDP_GRAPH_2',	'DNS UDP Resolution RTT - Ratio of Failed tests');
define('DNS_UPDATE_TIME_GRAPH_1',				'DNS UDP Update Time – Average');
define('RDDS_AVAILABILITY_GRAPH_1',				'RDDS Availability – Accumulated minutes of downtime');
define('RDDS_QUERY_RTT_GRAPH_1',				'RDDS Query RTT – Average');
define('RDDS_QUERY_RTT_GRAPH_2',				'RDDS Query RTT – Ratio of Failed tests');
define('RDDS_43_QUERY_RTT_GRAPH_1',				'RDDS 43 Query RTT – Average');
define('RDDS_80_QUERY_RTT_GRAPH_1',				'RDDS 80 Query RTT – Average');
define('RDDS_UPDATE_TIME_GRAPH_1',				'RDDS Update Time – Average');
define('RDDS_UPDATE_TIME_GRAPH_2',				'RDDS Update Time – Ratio of Failed tests');

define('RSM_SLA_SCREEN_TYPE_GRAPH_1',		0);
define('RSM_SLA_SCREEN_TYPE_GRAPH_2',		1);
define('RSM_SLA_SCREEN_TYPE_SCREEN',		2);
define('RSM_SLA_SCREEN_TYPE_GRAPH_3',		3);
define('RSM_SLA_SCREEN_TYPE_GRAPH_4',		4);
define('RSM_SLA_SCREEN_TYPE_GRAPH_5',		5);
define('RSM_SLA_SCREEN_TYPE_GRAPH_6',		6);

// SLA monitoring incident status.
define('INCIDENT_ACTIVE',			0);
define('INCIDENT_RESOLVED',			1);
define('INCIDENT_FALSE_POSITIVE',	2);

// false positive event status.
define('INCIDENT_FLAG_NORMAL',			0);
define('INCIDENT_FLAG_FALSE_POSITIVE',	1);

// SLA monitoring incident status, specific errors: internal and DNSSEC.
define('ZBX_EC_INTERNAL_LAST',			-199);
define('ZBX_EC_DNS_UDP_DNSSEC_FIRST',	-401);	# DNS UDP - The TLD is configured as DNSSEC-enabled, but no DNSKEY was found in the apex
define('ZBX_EC_DNS_UDP_DNSSEC_LAST',	-427);	# DNS UDP - Malformed DNSSEC response
define('ZBX_EC_DNS_TCP_DNSSEC_FIRST',	-801);	# DNS TCP - The TLD is configured as DNSSEC-enabled, but no DNSKEY was found in the apex
define('ZBX_EC_DNS_TCP_DNSSEC_LAST',	-827);	# DNS TCP - Malformed DNSSEC response

// SLA monitoring calculated items keys.
define('CALCULATED_ITEM_DNS_FAIL',				'rsm.configvalue[RSM.INCIDENT.DNS.FAIL]');
define('CALCULATED_ITEM_DNSSEC_FAIL',			'rsm.configvalue[RSM.INCIDENT.DNSSEC.FAIL]');
define('CALCULATED_ITEM_RDDS_FAIL',				'rsm.configvalue[RSM.INCIDENT.RDDS.FAIL]');
define('CALCULATED_ITEM_RDAP_FAIL',				'rsm.configvalue[RSM.INCIDENT.RDAP.FAIL]');
define('CALCULATED_ITEM_EPP_FAIL',				'rsm.configvalue[RSM.INCIDENT.EPP.FAIL]');
define('CALCULATED_ITEM_DNS_DELAY',				'rsm.configvalue[RSM.DNS.DELAY]');
define('CALCULATED_ITEM_RDDS_DELAY',			'rsm.configvalue[RSM.RDDS.DELAY]');
define('CALCULATED_ITEM_RDAP_DELAY',			'rsm.configvalue[RSM.RDAP.DELAY]');
define('CALCULATED_ITEM_EPP_DELAY',				'rsm.configvalue[RSM.EPP.DELAY]');
define('CALCULATED_ITEM_DNS_UDP_RTT_HIGH',		'rsm.configvalue[RSM.DNS.UDP.RTT.HIGH]');
define('CALCULATED_ITEM_DNS_TCP_RTT_HIGH',		'rsm.configvalue[RSM.DNS.TCP.RTT.HIGH]');
define('CALCULATED_ITEM_RDDS_RTT_HIGH',			'rsm.configvalue[RSM.RDDS.RTT.HIGH]');
define('CALCULATED_ITEM_RDAP_RTT_HIGH',			'rsm.configvalue[RSM.RDAP.RTT.HIGH]');
define('CALCULATED_ITEM_SLV_DNS_NS_RTT_UDP',	'rsm.configvalue[RSM.SLV.DNS.UDP.RTT]');
define('CALCULATED_ITEM_SLV_DNS_NS_RTT_TCP',	'rsm.configvalue[RSM.SLV.DNS.TCP.RTT]');
define('CALCULATED_ITEM_SLV_DNS_NS_UPD',		'rsm.configvalue[RSM.SLV.DNS.NS.UPD]');
define('CALCULATED_ITEM_SLV_DNS_NS',			'rsm.configvalue[RSM.SLV.NS.DOWNTIME]');
define('CALCULATED_ITEM_SLV_RDDS43_RTT',		'rsm.configvalue[RSM.SLV.RDDS43.RTT]');
define('CALCULATED_ITEM_SLV_RDDS80_RTT',		'rsm.configvalue[RSM.SLV.RDDS80.RTT]');
define('CALCULATED_ITEM_SLV_RDDS_UPD',			'rsm.configvalue[RSM.SLV.RDDS.UPD]');
define('CALCULATED_DNS_ROLLWEEK_SLA',			'rsm.configvalue[RSM.DNS.ROLLWEEK.SLA]');
define('CALCULATED_RDDS_ROLLWEEK_SLA',			'rsm.configvalue[RSM.RDDS.ROLLWEEK.SLA]');
define('CALCULATED_RDAP_ROLLWEEK_SLA',			'rsm.configvalue[RSM.RDAP.ROLLWEEK.SLA]');
define('CALCULATED_EPP_ROLLWEEK_SLA',			'rsm.configvalue[RSM.EPP.ROLLWEEK.SLA]');
define('CALCULATED_PROBE_RSM_IP4_ENABLED',		'probe.configvalue[RSM.IP4.ENABLED]');
define('CALCULATED_PROBE_RSM_IP6_ENABLED',		'probe.configvalue[RSM.IP6.ENABLED]');

// Number of test cycles to show before and after incident recovery event.
define('DISPLAY_CYCLES_BEFORE_RECOVERY',	4);
define('DISPLAY_CYCLES_AFTER_RECOVERY',		6);

// SLA monitoring probe status items keys.
define('PROBE_KEY_ONLINE',			'rsm.probe.online');
define('PROBE_DNS_UDP_ITEM',		'rsm.dns.udp[{$RSM.TLD}]');
define('PROBE_DNS_UDP_ITEM_RTT',	'rsm.dns.udp.rtt[');
define('PROBE_DNS_TEST',			'rsm.dns[{$RSM.TLD},{$RSM.DNS.TESTPREFIX},{$RSM.DNS.NAME.SERVERS},{$RSM.TLD.DNSSEC.ENABLED},{$RSM.TLD.RDDS.ENABLED},{$RSM.TLD.EPP.ENABLED},{$RSM.TLD.DNS.UDP.ENABLED},{$RSM.TLD.DNS.TCP.ENABLED},{$RSM.IP4.ENABLED},{$RSM.IP6.ENABLED},{$RSM.RESOLVER},{$RSM.DNS.UDP.RTT.HIGH},{$RSM.DNS.TCP.RTT.HIGH}]');
define('PROBE_DNS_TCP_RTT',			'rsm.dns.rtt[{#NS},{#IP},tcp]');
define('PROBE_DNS_UDP_RTT',			'rsm.dns.rtt[{#NS},{#IP},udp]');
define('PROBE_DNS_NSID',			'rsm.dns.nsid[{#NS},{#IP}]');
define('PROBE_DNS_PROTOCOL',		'rsm.dns.protocol');
define('PROBE_DNS_NSSOK',			'rsm.dns.nssok');
define('PROBE_DNS_STATUS',			'rsm.dns.status');
define('PROBE_DNS_NS_STATUS',		'rsm.dns.ns.status[{#NS}]');
define('PROBE_DNSSEC_STATUS',		'rsm.dnssec.status');
define('PROBE_EPP_RESULT',			'rsm.epp[');
define('PROBE_EPP_IP',				'rsm.epp.ip[{$RSM.TLD}]');
define('PROBE_EPP_UPDATE',			'rsm.epp.rtt[{$RSM.TLD},update]');
define('PROBE_EPP_INFO',			'rsm.epp.rtt[{$RSM.TLD},info]');
define('PROBE_EPP_LOGIN',			'rsm.epp.rtt[{$RSM.TLD},login]');
define('PROBE_RDDS_STATUS',			'rsm.rdds.status');
define('PROBE_RDDS43_STATUS',		'rsm.rdds.43.status');
define('PROBE_RDDS43_IP',			'rsm.rdds.43.ip');
define('PROBE_RDDS43_RTT',			'rsm.rdds.43.rtt');
define('PROBE_RDDS43_TARGET',		'rsm.rdds.43.target');
define('PROBE_RDDS43_TESTEDNAME',	'rsm.rdds.43.testedname');
define('PROBE_RDDS80_STATUS',		'rsm.rdds.80.status');
define('PROBE_RDDS80_IP',			'rsm.rdds.80.ip');
define('PROBE_RDDS80_RTT',			'rsm.rdds.80.rtt');
define('PROBE_RDDS80_TARGET',		'rsm.rdds.80.target');
define('PROBE_RDAP_STATUS',			'rdap.status');
define('PROBE_RDAP_IP',				'rdap.ip');
define('PROBE_RDAP_RTT',			'rdap.rtt');
define('PROBE_RDAP_TARGET',			'rdap.target');
define('PROBE_RDAP_TESTEDNAME',		'rdap.testedname');

// SLA monitoring NS names.
define('NS_NO_RESULT',	0);
define('NS_DOWN',		1);
define('NS_UP',			2);

// SLA monitoring probe status.
define('PROBE_OFFLINE',	-1);
define('PROBE_DOWN',	0);
define('PROBE_UP',		1);

// NameServer status.
define('NAMESERVER_DOWN',	0);
define('NAMESERVER_UP',		1);

// SLA monitoring "rsm" host name.
define('RSM_HOST',	'Global macro history');

// SLA monitoring TLDs group.
define('RSM_TLDS_GROUP',	'TLDs');

// TLD types.
define('RSM_CC_TLD_GROUP',		'ccTLD');
define('RSM_G_TLD_GROUP',		'gTLD');
define('RSM_OTHER_TLD_GROUP',	'otherTLD');
define('RSM_TEST_GROUP',		'testTLD');

define('RSM_RDDS_SUBSERVICE_RDDS',	'RDDS43/80');
define('RSM_RDDS_SUBSERVICE_RDAP',	'RDAP');

// Value maps used for special purpose.
define('PROBES_MON_GROUPID',					130);
define('RSM_SERVICE_AVAIL_VALUE_MAP',			110);
define('RSM_DNS_TRANSPORT_PROTOCOL_VALUE_MAP',	162);
define('RSM_DNS_RTT_ERRORS_VALUE_MAP',			120);
define('RSM_RDDS_RTT_ERRORS_VALUE_MAP',			130);

define('RSM_MONITORING_TARGET',			'{$RSM.MONITORING.TARGET}');
define('MONITORING_TARGET_REGISTRY',	'registry');
define('MONITORING_TARGET_REGISTRAR',	'registrar');

define('AUDIT_RESOURCE_INCIDENT',	100001);

define('UP_INCONCLUSIVE_RECONFIG',	4);

// Salt used for switching frontends
// static string can be replaced with environment variable getenv('RSM_SECRET_KEY')
define('RSM_SECRET_KEY',		'An0KXLtNTwCGd2FUeKqUsJ#X0#6N%B=OVZ(sfsB&dQEx6aVte2^ZXTset&!%l4f#');
