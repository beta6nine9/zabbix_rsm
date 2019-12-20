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
// server delay time in seconds
define('RSM_ROLLWEEK_SHIFT_BACK', 180);

// SLA monitoring start year
define('SLA_MONITORING_START_YEAR',	2014);

// SLA monitoring extra filter value
define('SLA_MONITORING_SLV_FILTER_NON_ZERO',	-1);

// SLA monitoring services
define('RSM_DNS',			0);
define('RSM_DNSSEC',		1);
define('RSM_RDDS',			2);
define('RSM_EPP',			3);
define('RSM_RDAP',			4); // Standalone RDAP

// SLA monitoring macros
define('RSM_SLV_MACRO_EPP_AVAIL',	'{$RSM.SLV.EPP.AVAIL}');
define('RSM_PAGE_SLV',				'{$RSM.ROLLWEEK.THRESHOLDS}');
define('RSM_ROLLWEEK_SECONDS',		'{$RSM.ROLLWEEK.SECONDS}');
define('RSM_MIN_DNS_COUNT',			'{$RSM.DNS.AVAIL.MINNS}');
define('RSM_DNS_UDP_DELAY',			'{$RSM.DNS.UDP.DELAY}');
define('RSM_RDDS_DELAY',			'{$RSM.RDDS.DELAY}');
define('RSM_RDDS_ENABLED',			'{$RSM.RDDS.ENABLED}');
define('RSM_RDAP_ENABLED',			'{$RSM.RDAP.ENABLED}');
define('RSM_TLD_DNSSEC_ENABLED',	'{$RSM.TLD.DNSSEC.ENABLED}');
define('RSM_TLD_EPP_ENABLED',		'{$RSM.TLD.EPP.ENABLED}');
define('RSM_TLD_RDDS_ENABLED',		'{$RSM.TLD.RDDS.ENABLED}');
define('RSM_TLD_RDDS43_ENABLED',	'{$RSM.TLD.RDDS43.ENABLED}');
define('RSM_TLD_RDDS80_ENABLED',	'{$RSM.TLD.RDDS80.ENABLED}');
define('RSM_RDAP_TLD_ENABLED',		'{$RDAP.TLD.ENABLED}');
define('RSM_SLV_DNS_NS_UPD',		'{$RSM.SLV.DNS.NS.UPD}');
define('RSM_DNS_UPDATE_TIME',		'{$RSM.DNS.UPDATE.TIME}');
define('RSM_SLV_EPP_LOGIN',			'{$RSM.SLV.EPP.LOGIN}');
define('RSM_EPP_LOGIN_RTT_LOW',		'{$RSM.EPP.LOGIN.RTT.LOW}');
define('RSM_SLV_EPP_INFO',			'{$RSM.SLV.EPP.INFO}');
define('RSM_EPP_INFO_RTT_LOW',		'{$RSM.EPP.INFO.RTT.LOW}');
define('RSM_SLV_EPP_UPDATE',		'{$RSM.SLV.EPP.UPDATE}');
define('RSM_EPP_UPDATE_RTT_LOW',	'{$RSM.EPP.UPDATE.RTT.LOW}');
define('RDAP_BASE_URL',				'{$RDAP.BASE.URL}');
define('RDDS_ENABLED',				'rdds.enabled');
define('RDAP_ENABLED',				'rdap.enabled');

// SLA monitoring rolling week items keys
define('RSM_SLV_DNS_ROLLWEEK',		'rsm.slv.dns.rollweek');
define('RSM_SLV_DNSSEC_ROLLWEEK',	'rsm.slv.dnssec.rollweek');
define('RSM_SLV_RDDS_ROLLWEEK',		'rsm.slv.rdds.rollweek');
define('RSM_SLV_RDAP_ROLLWEEK',		'rsm.slv.rdap.rollweek');
define('RSM_SLV_EPP_ROLLWEEK',		'rsm.slv.epp.rollweek');

// SLA monitoring availability items keys
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
define('RSM_SLV_EPP_DOWNTIME',				'rsm.slv.epp.downtime');
define('RSM_SLV_EPP_RTT_LOGIN_PFAILED',		'rsm.slv.epp.rtt.login.pfailed');
define('RSM_SLV_EPP_RTT_LOGIN_FAILED',		'rsm.slv.epp.rtt.login.failed');
define('RSM_SLV_EPP_RTT_LOGIN_MAX',			'rsm.slv.epp.rtt.login.max');
define('RSM_SLV_EPP_RTT_INFO_PFAILED',		'rsm.slv.epp.rtt.info.pfailed');
define('RSM_SLV_EPP_RTT_INFO_FAILED',		'rsm.slv.epp.rtt.info.failed');
define('RSM_SLV_EPP_RTT_INFO_MAX',			'rsm.slv.epp.rtt.info.max');
define('RSM_SLV_EPP_RTT_UPDATE_PFAILED',	'rsm.slv.epp.rtt.update.pfailed');
define('RSM_SLV_EPP_RTT_UPDATE_FAILED',		'rsm.slv.epp.rtt.update.failed');
define('RSM_SLV_EPP_RTT_UPDATE_MAX',		'rsm.slv.epp.rtt.update.max');
define('RSM_SLV_RDDS_AVAIL',				'rsm.slv.rdds.avail');
define('RSM_SLV_RDAP_AVAIL',				'rsm.slv.rdap.avail');
define('RSM_SLV_EPP_AVAIL',					'rsm.slv.epp.avail');
define('RSM_SLV_DNSSEC_AVAIL',				'rsm.slv.dnssec.avail');
define('RSM_SLV_KEY_DNS_NSID',				'rsm.dns.nsid[{#NS},{#IP}]');
define('RSM_SLV_KEY_DNS_PROTOCOL',			'rsm.dns.protocol');
define('RSM_SLV_KEY_DNS_NSSOK',				'rsm.dns.nssok');

// RDAP standalone.
define('RSM_RDAP_STANDALONE', '{$RSM.RDAP.STANDALONE}');

// "RSM Service Availability" value mapping:
define('DOWN',	0);	// Down

// SLA reports graph names
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
define('EPP_SERVICE_AVAILABILITY_GRAPH_1',		'EPP Service Availability – Accumulated minutes of downtime');
define('EPP_SESSION_COMMAND_RTT_GRAPH_1',		'EPP Session-Command RTT – Average');
define('EPP_SESSION_COMMAND_RTT_GRAPH_2',		'EPP Session-Command RTT – Ratio of Failed tests');
define('EPP_TRANSFORM_COMMAND_RTT_GRAPH_1',		'EPP Transform-Command RTT – Average');
define('EPP_TRANSFORM_COMMAND_RTT_GRAPH_2',		'EPP Transform-Command RTT – Ratio of Failed tests');
define('EPP_QUERY_COMMAND_RTT_GRAPH_1',			'EPP Query-Command RTT – Average');
define('EPP_QUERY_COMMAND_RTT_GRAPH_2',			'EPP Query-Command RTT – Ratio of Failed tests');

define('RSM_SLA_SCREEN_TYPE_GRAPH_1',		0);
define('RSM_SLA_SCREEN_TYPE_GRAPH_2',		1);
define('RSM_SLA_SCREEN_TYPE_SCREEN',		2);
define('RSM_SLA_SCREEN_TYPE_GRAPH_3',		3);
define('RSM_SLA_SCREEN_TYPE_GRAPH_4',		4);
define('RSM_SLA_SCREEN_TYPE_GRAPH_5',		5);
define('RSM_SLA_SCREEN_TYPE_GRAPH_6',		6);

// SLA monitoring incident status
define('INCIDENT_ACTIVE',			0);
define('INCIDENT_RESOLVED',			1);
define('INCIDENT_FALSE_POSITIVE',	2);
define('INCIDENT_RESOLVED_NO_DATA',	3);

// false positive event status
define('INCIDENT_FLAG_NORMAL',			0);
define('INCIDENT_FLAG_FALSE_POSITIVE',	1);

// SLA monitoring incident status
define('ZBX_EC_INTERNAL_LAST',		-199);
define('ZBX_EC_DNS_UDP_RES_NOADBIT',	-401);
define('ZBX_EC_DNS_UDP_DNSKEY_NONE',	-428);

// SLA monitoring calculated items keys
define('CALCULATED_ITEM_DNS_FAIL',				'rsm.configvalue[RSM.INCIDENT.DNS.FAIL]');
define('CALCULATED_ITEM_DNSSEC_FAIL',			'rsm.configvalue[RSM.INCIDENT.DNSSEC.FAIL]');
define('CALCULATED_ITEM_RDDS_FAIL',				'rsm.configvalue[RSM.INCIDENT.RDDS.FAIL]');
define('CALCULATED_ITEM_RDAP_FAIL',				'rsm.configvalue[RSM.INCIDENT.RDAP.FAIL]');
define('CALCULATED_ITEM_EPP_FAIL',				'rsm.configvalue[RSM.INCIDENT.EPP.FAIL]');
define('CALCULATED_ITEM_DNS_DELAY',				'rsm.configvalue[RSM.DNS.UDP.DELAY]');
define('CALCULATED_ITEM_RDDS_DELAY',			'rsm.configvalue[RSM.RDDS.DELAY]');
define('CALCULATED_ITEM_RDAP_DELAY',			'rsm.configvalue[RSM.RDAP.DELAY]');
define('CALCULATED_ITEM_EPP_DELAY',				'rsm.configvalue[RSM.EPP.DELAY]');
define('CALCULATED_ITEM_DNS_AVAIL_MINNS',		'rsm.configvalue[RSM.DNS.AVAIL.MINNS]');
define('CALCULATED_ITEM_DNS_UDP_RTT_HIGH',		'rsm.configvalue[RSM.DNS.UDP.RTT.HIGH]');
define('CALCULATED_ITEM_SLV_DNS_NS_RTT_UDP',	'rsm.configvalue[RSM.SLV.DNS.UDP.RTT]');
define('CALCULATED_ITEM_SLV_DNS_NS_RTT_TCP',	'rsm.configvalue[RSM.SLV.DNS.TCP.RTT]');
define('CALCULATED_ITEM_SLV_DNS_NS_UPD',		'rsm.configvalue[RSM.SLV.DNS.NS.UPD]');
define('CALCULATED_ITEM_SLV_DNS_NS',			'rsm.configvalue[RSM.SLV.NS.DOWNTIME]');
define('CALCULATED_ITEM_SLV_RDDS43_RTT',		'rsm.configvalue[RSM.SLV.RDDS43.RTT]');
define('CALCULATED_ITEM_SLV_RDDS80_RTT',		'rsm.configvalue[RSM.SLV.RDDS80.RTT]');
define('CALCULATED_ITEM_SLV_RDDS_UPD',			'rsm.configvalue[RSM.SLV.RDDS.UPD]');
define('CALCULATED_ITEM_SLV_EPP_INFO',			'rsm.configvalue[RSM.SLV.EPP.INFO]');
define('CALCULATED_ITEM_SLV_EPP_LOGIN',			'rsm.configvalue[RSM.SLV.EPP.LOGIN]');
define('CALCULATED_ITEM_EPP_UPDATE',			'rsm.configvalue[RSM.SLV.EPP.UPDATE]');
define('CALCULATED_DNS_ROLLWEEK_SLA',			'rsm.configvalue[RSM.DNS.ROLLWEEK.SLA]');
define('CALCULATED_RDDS_ROLLWEEK_SLA',			'rsm.configvalue[RSM.RDDS.ROLLWEEK.SLA]');
define('CALCULATED_RDAP_ROLLWEEK_SLA',			'rsm.configvalue[RSM.RDAP.ROLLWEEK.SLA]');
define('CALCULATED_EPP_ROLLWEEK_SLA',			'rsm.configvalue[RSM.EPP.ROLLWEEK.SLA]');

// Number of test cycles to show before and after incident recovery event.
define('DISPLAY_CYCLES_AFTER_RECOVERY',		6); // (including recovery event)
define('DISPLAY_CYCLES_BEFORE_RECOVERY',	3);

// SLA monitoring probe status items keys
define('PROBE_KEY_ONLINE',			'rsm.probe.online');
define('PROBE_DNS_UDP_ITEM',		'rsm.dns.udp[{$RSM.TLD}]');
define('PROBE_DNS_UDP_ITEM_RTT',	'rsm.dns.udp.rtt[');
define('PROBE_RDDS_ITEM',			'rsm.rdds[');
define('PROBE_EPP_RESULT',			'rsm.epp[');
define('PROBE_EPP_IP',				'rsm.epp.ip[{$RSM.TLD}]');
define('PROBE_EPP_UPDATE',			'rsm.epp.rtt[{$RSM.TLD},update]');
define('PROBE_EPP_INFO',			'rsm.epp.rtt[{$RSM.TLD},info]');
define('PROBE_EPP_LOGIN',			'rsm.epp.rtt[{$RSM.TLD},login]');
define('PROBE_RDDS43_IP',			'rsm.rdds.43.ip[{$RSM.TLD}]');
define('PROBE_RDDS43_RTT',			'rsm.rdds.43.rtt[{$RSM.TLD}]');
define('PROBE_RDDS80_IP',			'rsm.rdds.80.ip[{$RSM.TLD}]');
define('PROBE_RDDS80_RTT',			'rsm.rdds.80.rtt[{$RSM.TLD}]');
//define('PROBE_RDAP_IP',			'rsm.rdds.rdap.ip[{$RSM.TLD}]');  // deprecated
//define('PROBE_RDAP_RTT',			'rsm.rdds.rdap.rtt[{$RSM.TLD}]'); // deprecated
define('PROBE_RDAP_ITEM',			'rdap[');
define('PROBE_RDAP_IP',				'rdap.ip');
define('PROBE_RDAP_RTT',			'rdap.rtt');

// SLA monitoring NS names
define('NS_NO_RESULT',	0);
define('NS_DOWN',		1);
define('NS_UP',			2);

// SLA monitoring probe status
define('PROBE_OFFLINE',	-1);
define('PROBE_DOWN',	0);
define('PROBE_UP',		1);

// NameServer status
define('NAMESERVER_DOWN',	0);
define('NAMESERVER_UP',		1);

// SLA monitoring "rsm" host name
define('RSM_HOST',	'Global macro history');

// SLA monitoring TLDs group
define('RSM_TLDS_GROUP',	'TLDs');

// TLD types
define('RSM_CC_TLD_GROUP',		'ccTLD');
define('RSM_G_TLD_GROUP',		'gTLD');
define('RSM_OTHER_TLD_GROUP',	'otherTLD');
define('RSM_TEST_GROUP',		'testTLD');

define('RSM_RDDS_SUBSERVICE_RDDS', 'RDDS43/80');
define('RSM_RDDS_SUBSERVICE_RDAP', 'RDAP');

define('PROBES_MON_GROUPID',	130);
define('RSM_SERVICE_AVAIL_VALUE_MAP', 110);
define('RSM_DNS_RTT_ERRORS_VALUE_MAP', 120);
define('RSM_RDDS_RTT_ERRORS_VALUE_MAP', 130);

define('RSM_MONITORING_TARGET', '{$RSM.MONITORING.TARGET}');
define('MONITORING_TARGET_REGISTRY', 'registry');
define('MONITORING_TARGET_REGISTRAR', 'registrar');
