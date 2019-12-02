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

#include "common.h"
#include "db.h"
#include "dbupgrade.h"
#include "log.h"

extern unsigned char	program_type;

/*
 * 4.4 maintenance database patches
 */

#ifndef HAVE_SQLITE3

static int	DBpatch_4040000(void)
{
	return SUCCEED;
}

static int	DBpatch_4040300(void)
{
	/* this patch begins RSM FY20 upgrade sequence and has been intentionally left blank */

	return SUCCEED;
}

static int	DBpatch_4040301(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute(
		"update items"
		" set delay='{$RSM.DNS.UDP.DELAY}'"
		" where key_ like 'rsm.dns.udp[%%'"
		" and type=%d",
		ITEM_TYPE_SIMPLE))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_4040302(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute(
		"update items"
		" set delay='{$RSM.DNS.TCP.DELAY}'"
		" where key_ like 'rsm.dns.tcp[%%'"
		" and type=%d",
		ITEM_TYPE_SIMPLE))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_4040303(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute(
		"update items"
		" set delay='{$RSM.RDDS.DELAY}'"
		" where key_ like 'rsm.rdds[%%'"
		" and type=%d",
		ITEM_TYPE_SIMPLE))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_4040304(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute(
		"update items"
		" set delay='{$RSM.RDAP.DELAY}'"
		" where key_ like 'rdap[%%'"
		" and type=%d",
		ITEM_TYPE_SIMPLE))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_4040305(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute(
		"update items"
		" set delay='{$RSM.EPP.DELAY}'"
		" where key_ like 'rsm.epp[%%'"
		" and type=%d",
		ITEM_TYPE_SIMPLE))
	{
		return FAIL;
	}

	return SUCCEED;
}
typedef struct
{
	const char *const	macro;
	const char *const	description;
}
macro_descr_t;

static int	DBpatch_4040306(void)
{
	/* this patch function has been generated automatically by extract_macros.pl */

	static macro_descr_t	macro_descr[] =
	{
		{
			"{$RSM.SLV.RDDS.RTT}",
			"Maximum allowed ratio of RDDS43 and RDDS80 queries with RTT above"
			" {$RSM.RDDS.RTT.LOW}"
		},
		{
			"{$RSM.DNS.UDP.RTT.LOW}",
			"Consider DNS UDP RTT unsuccessful if it is over specified time. T"
			"his parameter is used by scripts later when processing collected "
			"data."
		},
		{
			"{$RSM.IP6.ROOTSERVERS1}",
			"List of IPv6 root servers for getting automatic Probe status (Onl"
			"ine/Offline), if Probe supports IPv6."
		},
		{
			"{$RSM.EPP.LOGIN.RTT.LOW}",
			"Consider EPP-Session RTT unsuccessful if it is over specified tim"
			"e. This parameter is used by scripts later when processing collec"
			"ted data."
		},
		{
			"{$RSM.RDAP.PROBE.ONLINE}",
			"Consider RDAP Service availability at a particular time unconditi"
			"onally UP, if there was less than specified number of RDAP-enable"
			"d online probes at the time of the test cycle."
		},
		{
			"{$RSM.INCIDENT.RDDS.RECOVER}",
			"Number of subsequently successful RDDS Availability tests to clos"
			"e an incident."
		},
		{
			"{$RSM.INCIDENT.EPP.RECOVER}",
			"Number of subsequently successful EPP Availability tests to close"
			" an incident."
		},
		{
			"{$RSM.EPP.LOGIN.RTT.HIGH}",
			"When performing particular EPP-Session test on a Probe node consi"
			"der the target down if RTT was over specified time."
		},
		{
			"{$RSM.INCIDENT.RDAP.FAIL}",
			"Number of subsequently failed RDAP Availability tests to start an"
			" incident."
		},
		{
			"{$PROBE.INTERNAL.ERROR.INTERVAL}",
			"The amount of time probe needs to generate internal errors for be"
			"fore it is considered to be broken and is put offline, used in \""
			"Internal errors happening for {$PROBE.INTERNAL.ERROR.INTERVAL}\" "
			"trigger. Value can be easily changed on individual Zabbix server "
			"in \"Template Probe Errors\" template or fine-tuned for individua"
			"l hosts. Note that time units are supported (\"s\" for seconds, "
			"\"m\" for minutes, \"h\" for hours, \"d\" for days and \"w\" for "
			"weeks)."
		},
		{
			"{$RSM.RDAP.STANDALONE}",
			"This parameter stores timestamp of switch to Standalone RDAP. 0 o"
			"r values larger than current timestamp mean the system considers "
			"RDAP to be part of RDDS service."
		},
		{
			"{$RSM.EPP.UPDATE.RTT.LOW}",
			"Consider EPP-Transform RTT unsuccessful if it is over specified t"
			"ime. This parameter is used by scripts later when processing coll"
			"ected data."
		},
		{
			"{$RSM.INCIDENT.DNSSEC.RECOVER}",
			"Number of subsequently successful DNSSEC Availability tests to cl"
			"ose an incident."
		},
		{
			"{$RSM.SLV.DNS.DOWNTIME}",
			"Maximum allowed downtime of DNS service per month"
		},
		{
			"{$RSM.DNS.TCP.RTT.LOW}",
			"Consider DNS TCP RTT unsuccessful if it is over specified time. T"
			"his parameter is used by scripts later when processing collected "
			"data."
		},
		{
			"{$RSM.RDDS.PROBE.ONLINE}",
			"Consider RDDS Service availability at a particular time unconditi"
			"onally UP, if there was less than specified number of RDDS-enable"
			"d online probes at the time of the test cycle."
		},
		{
			"{$RSM.RDAP.RTT.LOW}",
			"Consider RDAP RTT unsuccessful if it is over specified time. This"
			" parameter is used by scripts later when processing collected dat"
			"a."
		},
		{
			"{$RSM.IP6.MIN.SERVERS}",
			"When testing Probe status consider it Offline if number of succes"
			"sfully tested IPv6 servers was less than specified, if Probe supp"
			"orts IPv6."
		},
		{
			"{$RSM.RDDS.MAXREDIRS}",
			"Maximum redirects to perform on the Probe node during RDDS80 test"
			" (cURL option CURLOPT_MAXREDIRS)."
		},
		{
			"{$RSM.DNS.TCP.DELAY}",
			"DNS TCP test cycle period (Update interval of item rsm.dns.tcp[{$"
			"RSM.TLD}]). NB! This must be checked before adding first TLD!"
		},
		{
			"{$RSM.RDAP.MAXREDIRS}",
			"Maximum redirects to perform on the Probe node during RDAP test ("
			"cURL option CURLOPT_MAXREDIRS)"
		},
		{
			"{$RSM.RDDS.UPDATE.TIME}",
			"Maximum RDDS update time to consider it successful."
		},
		{
			"{$RSM.IP4.MIN.PROBE.ONLINE}",
			"Fire a trigger if system contains less than specified number of I"
			"Pv4 online Probe nodes."
		},
		{
			"{$RSM.RDDS.RTT.LOW}",
			"Consider RDDS RTT unsuccessful if it is over specified time. This"
			" parameter is used by scripts later when processing collected dat"
			"a."
		},
		{
			"{$RSM.IP6.MIN.PROBE.ONLINE}",
			"Fire a trigger if system contains less than specified number of I"
			"Pv6 online Probe nodes."
		},
		{
			"{$RSM.INCIDENT.EPP.FAIL}",
			"Number of subsequently failed EPP Availability tests to start an "
			"incident."
		},
		{
			"{$RSM.INCIDENT.DNS.FAIL}",
			"Number of subsequently failed DNS Availability tests to start an "
			"incident."
		},
		{
			"{$RSM.EPP.DELAY}",
			"NB! This must be checked before adding first TLD! EPP test cycle "
			"period (Update interval of item rsm.epp[{$RSM.TLD},])."
		},
		{
			"{$RSM.INCIDENT.DNSSEC.FAIL}",
			"Number of subsequently failed DNSSEC Availability tests to start "
			"an incident."
		},
		{
			"{$RSM.EPP.INFO.RTT.LOW}",
			"Consider EPP-Query RTT unsuccessful if it is over specified time."
			" This parameter is used by scripts later when processing collecte"
			"d data."
		},
		{
			"{$RSM.EPP.KEYSALT}",
			"EPP Key Salt"
		},
		{
			"{$RSM.DNS.ROLLWEEK.SLA}",
			"Maximum (100%) DNS/DNSSEC rolling week threshold."
		},
		{
			"{$RSM.INCIDENT.RDAP.RECOVER}",
			"Number of subsequently successful RDAP Availability tests to clos"
			"e an incident."
		},
		{
			"{$RSM.INCIDENT.RDDS.FAIL}",
			"Number of subsequently failed RDDS Availability tests to start an"
			" incident."
		},
		{
			"{$RSM.DNS.PROBE.ONLINE}",
			"Consider DNS/DNSSEC Service availability at a particular time unc"
			"onditionally UP, if there was less than specified number of onlin"
			"e probes at the time of the test cycle."
		},
		{
			"{$RSM.PROBE.MAX.OFFLINE}",
			"Fire a trigger if Probe has been manually disabled for more than "
			"specified time."
		},
		{
			"{$RSM.DNS.TCP.RTT.HIGH}",
			"When performing particular DNS TCP test on a Probe node consider "
			"Name Server down if RTT was over specified time."
		},
		{
			"{$RSM.SLV.DNS.UDP.RTT}",
			"Maximum allowed ratio of DNS UDP queries with RTT above {$RSM.DNS"
			".UDP.RTT.LOW}"
		},
		{
			"{$RESOLVER.STATUS.TIMEOUT}",
			"Timeout when getting resolver status, used in item resolver.statu"
			"s[{$RSM.RESOLVER},{$RESOLVER.STATUS.TIMEOUT},{$RESOLVER.STATUS.TR"
			"IES},{$RSM.IP4.ENABLED},{$RSM.IP6.ENABLED}]"
		},
		{
			"{$RSM.RDAP.RTT.HIGH}",
			"When performing particular RDAP test on a Probe node consider the"
			" target down if RTT was over specified time."
		},
		{
			"{$RSM.SLV.RDDS80.RTT}",
			"Maximum allowed ratio of RDDS80 queries with RTT above {$RSM.RDDS"
			".RTT.LOW}"
		},
		{
			"{$RSM.SLV.DNS.TCP.RTT}",
			"Maximum allowed ratio of DNS TCP queries with RTT above {$RSM.DNS"
			".TCP.RTT.LOW}"
		},
		{
			"{$RSM.IP6.REPLY.MS}",
			"When testing Probe status consider IPv6 server successful if RTT "
			"is below specified time, otherwise unsuccessful, if Probe support"
			"s IPv6."
		},
		{
			"{$RSM.PROBE.ONLINE.DELAY}",
			"How many seconds the check rsm.probe.status[automatic,\"{$RSM.IP4"
			".ROOTSERVERS1}\",\"{$RSM.IP6.ROOTSERVERS1}\"] must be successful "
			"in order to switch from OFFLINE to ONLINE."
		},
		{
			"{$RSM.DNS.AVAIL.MINNS}",
			"Consider DNS Service availability at a particular time UP if duri"
			"ng DNS test more than specified number of Name Servers replied su"
			"ccessfully."
		},
		{
			"{$RSM.RDDS.RTT.HIGH}",
			"When performing particular RDDS test on a Probe node consider the"
			" target down if RTT was over specified time."
		},
		{
			"{$RSM.SLV.RDAP.RTT}",
			"Maximum allowed ratio of RDAP queries with RTT above {$RSM.RDAP.R"
			"TT.LOW}"
		},
		{
			"{$RSM.PROBE.AVAIL.LIMIT}",
			"Maximum time from the last time Probe was available to Zabbix ser"
			"ver. If this is over limit the Probe will be considered Offline. "
			"This parameter is related to zabbix[proxy,{$RSM.PROXY_NAME},lasta"
			"ccess] item."
		},
		{
			"{$RSM.EPP.PROBE.ONLINE}",
			"Consider EPP Service availability at a particular time unconditio"
			"nally UP, if there was less than specified number of EPP-enabled "
			"online probes at the time of the test cycle."
		},
		{
			"{$RSM.DNS.UDP.DELAY}",
			"NB! This must be checked before adding first TLD! DNS UDP test cy"
			"cle period (Update interval of item rsm.dns.udp[{$RSM.TLD}])."
		},
		{
			"{$RSM.ROLLWEEK.THRESHOLDS}",
			"Thresholds for the SLA Monitoring->Rolling week status->Exceeding"
			" or equal to drop-down."
		},
		{
			"{$RSM.ROLLWEEK.SECONDS}",
			"Rolling week period. Use different from 604800 only in test purpo"
			"ses."
		},
		{
			"{$RSM.RDDS.DELAY}",
			"NB! This must be checked before adding first TLD! RDDS test cycle"
			" period (Update interval of item rsm.rdds[{$RSM.TLD},,])."
		},
		{
			"{$RSM.SLV.DNS.NS.UPD}",
			"Part of Compliance, part of Phase 2."
		},
		{
			"{$RESOLVER.STATUS.TRIES}",
			"Maximum number of tries when checking resolver status, used in it"
			"em resolver.status[{$RSM.RESOLVER},{$RESOLVER.STATUS.TIMEOUT},{$R"
			"ESOLVER.STATUS.TRIES},{$RSM.IP4.ENABLED},{$RSM.IP6.ENABLED}]"
		},
		{
			"{$RSM.IP4.MIN.SERVERS}",
			"When testing Probe status consider it Offline if number of succes"
			"sfully tested IPv4 servers was less than specified."
		},
		{
			"{$RSM.SLV.RDAP.DOWNTIME}",
			"Maximum allowed downtime of RDAP service per month"
		},
		{
			"{$RSM.RDDS.ROLLWEEK.SLA}",
			"Maximum (100%) RDDS rolling week threshold."
		},
		{
			"{$RSM.RDAP.ROLLWEEK.SLA}",
			"Maximum (100%) RDAP rolling week threshold."
		},
		{
			"{$RSM.SLV.RDDS.DOWNTIME}",
			"Maximum allowed downtime of RDDS service per month"
		},
		{
			"{$RSM.SLV.RDDS43.RTT}",
			"Maximum allowed ratio of RDDS43 queries with RTT above {$RSM.RDDS"
			".RTT.LOW}"
		},
		{
			"{$RSM.MONITORING.TARGET}",
			"* empty string - unknown; * \"registry\" - Monitoring target is T"
			"LD; * \"registrar\" - Monitoring target is Registrar;"
		},
		{
			"{$RSM.SLV.NS.DOWNTIME}",
			"Maximum allowed downtime of DNS NS per month"
		},
		{
			"{$RSM.EPP.INFO.RTT.HIGH}",
			"When performing particular EPP-Query test on a Probe node conside"
			"r the target down if RTT was over specified time."
		},
		{
			"{$RSM.EPP.UPDATE.RTT.HIGH}",
			"When performing particular EPP-Transform test on a Probe node con"
			"sider the target down if RTT was over specified time."
		},
		{
			"{$RSM.EPP.ROLLWEEK.SLA}",
			"Maximum (100%) EPP rolling week threshold."
		},
		{
			"{$RSM.IP4.REPLY.MS}",
			"When testing Probe status consider IPv4 server successful if RTT "
			"is below specified time, otherwise unsuccessful."
		},
		{
			"{$RSM.DNS.UPDATE.TIME}",
			"Maximum DNS update time to consider it successful."
		},
		{
			"{$RSM.DNS.UDP.RTT.HIGH}",
			"When performing particular DNS UDP test on a Probe node consider "
			"Name Server down if RTT was over specified time."
		},
		{
			"{$RSM.RDAP.DELAY}",
			"RDAP test cycle period (Update interval of item rdap[{$RSM.TLD},{"
			"$RDAP.TEST.DOMAIN},{$RDAP.BASE.URL},{$RSM.RDDS.MAXREDIRS},{$RSM.R"
			"DDS.RTT.HIGH},{$RDAP.TLD.ENABLED},{$RSM.RDAP.ENABLED},{$RSM.IP4.E"
			"NABLED},{$RSM.IP6.ENABLED},{$RSM.RESOLVER}])"
		},
		{
			"{$RSM.INCIDENT.DNS.RECOVER}",
			"Number of subsequently successful DNS Availability tests to close"
			" an incident."
		},
		{
			"{$RSM.IP4.ROOTSERVERS1}",
			"List of IPv4 root servers for getting automatic Probe status (Onl"
			"ine/Offline)."
		},
		{ NULL }
	};
	int	i, ret;

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	for (i = 0; macro_descr[i].macro != NULL; i++)
	{
		ret = DBexecute("update globalmacro set description='%s' where macro='%s' "
				"and description=''", macro_descr[i].description, macro_descr[i].macro);

		if (ZBX_DB_OK > ret)
			zabbix_log(LOG_LEVEL_WARNING, "did not update global macro '%s'", macro_descr[i].macro);
	}

	return SUCCEED;
}

static int	DBpatch_4040307(void)
{
	/* this patch function has been generated automatically by extract_macros.pl */

	static macro_descr_t	macro_descr[] =
	{
		{
			"{$RSM.TLD.EPP.ENABLED}",
			"Indicates whether EPP is enabled for this TLD"
		},
		{
			"{$RSM.RDDS.NS.STRING}",
			"What to look for in RDDS output, e.g. \"Name Server:\""
		},
		{
			"{$RDAP.TLD.ENABLED}",
			"Indicates whether RDAP is enabled for this TLD"
		},
		{
			"{$RSM.TLD.DNSSEC.ENABLED}",
			"Indicates whether DNSSEC is enabled for this TLD"
		},
		{
			"{$RSM.RDDS.ENABLED}",
			"Indicates whether the probe supports RDDS protocol"
		},
		{
			"{$RDAP.BASE.URL}",
			"Base URL for RDAP queries, e.g. http://whois.zabbix"
		},
		{
			"{$RDAP.TEST.DOMAIN}",
			"Test domain for RDAP queries, e.g. whois.zabbix"
		},
		{
			"{$RSM.IP6.ENABLED}",
			"Indicates whether the probe supports IPv6"
		},
		{
			"{$RSM.TLD}",
			"Name of this TLD, e.g. \"zabbix\""
		},
		{
			"{$RSM.TLD.RDDS.ENABLED}",
			"Indicates whether RDDS is enabled for this TLD"
		},
		{
			"{$RSM.RDAP.ENABLED}",
			"Indicates whether the probe supports RDAP protocol"
		},
		{
			"{$RSM.IP4.ENABLED}",
			"Indicates whether the probe supports IPv4"
		},
		{
			"{$RSM.DNS.TESTPREFIX}",
			"Prefix for DNS tests, e.g. nonexistent"
		},
		{
			"{$RSM.RDDS.TESTPREFIX}",
			"Prefix for RDDS tests of this TLD, e.g. \"whois\""
		},
		{
			"{$RSM.RESOLVER}",
			"DNS resolver used by the probe"
		},
		{
			"{$RSM.EPP.ENABLED}",
			"Indicates whether EPP is enabled on probe"
		},
		{ NULL }
	};
	int	i, ret;

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	for (i = 0; macro_descr[i].macro != NULL; i++)
	{
		ret = DBexecute("update hostmacro hm set description='%s' where hm.macro='%s' and"
				" hm.description='' and exists (select * from hosts h where"
				" hm.hostid=h.hostid and h.status=%d)",
				macro_descr[i].description, macro_descr[i].macro, HOST_STATUS_TEMPLATE);

		if (ZBX_DB_OK > ret)
			zabbix_log(LOG_LEVEL_WARNING, "did not update template macro '%s'", macro_descr[i].macro);
	}

	return SUCCEED;
}

static int	DBpatch_4040308_create_application(zbx_uint64_t applicationid, zbx_uint64_t hostid, const char *name)
{
	return DBexecute("insert into applications set applicationid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ",name='%s',"
			"flags=0",
			applicationid, hostid, name);
}

static int	DBpatch_4040308_create_item(zbx_uint64_t itemid, int type, zbx_uint64_t hostid, const char *name,
		const char *key_, const char *delay, const char *history, const char *trends, int value_type,
		zbx_uint64_t valuemapid, const char *params, int flags, const char* description, const char *lifetime,
		zbx_uint64_t master_itemid)
{
	return DBexecute("insert into items set itemid=" ZBX_FS_UI64 ",type=%d,snmp_community='',snmp_oid='',"
			"hostid=" ZBX_FS_UI64 ",name='%s',key_='%s',delay='%s',history='%s',trends='%s',status=0,"
			"value_type=%d,trapper_hosts='',units='',snmpv3_securityname='',snmpv3_securitylevel=0,"
			"snmpv3_authpassphrase='',snmpv3_privpassphrase='',formula='',logtimefmt='',templateid=NULL,"
			"valuemapid=nullif(" ZBX_FS_UI64 ",0),params='%s',ipmi_sensor='',authtype=0,username='',password='',publickey='',"
			"privatekey='',flags=%d,interfaceid=NULL,port='',description='%s',inventory_link=0,"
			"lifetime='%s',snmpv3_authprotocol=0,snmpv3_privprotocol=0,snmpv3_contextname='',evaltype=0,"
			"jmx_endpoint='',master_itemid=nullif(" ZBX_FS_UI64 ",0),timeout='3s',url='',query_fields='',"
			"posts='',status_codes='200',follow_redirects=1,post_type=0,http_proxy='',headers='',"
			"retrieve_mode=0,request_method=0,output_format=0,ssl_cert_file='',ssl_key_file='',"
			"ssl_key_password='',verify_peer=0,verify_host=0,allow_traps=0",
			itemid, type, hostid, name, key_, delay, history, trends, value_type, valuemapid, params, flags,
			description, lifetime, master_itemid);
}

static int	DBpatch_4040308_item_to_app(zbx_uint64_t itemappid, zbx_uint64_t applicationid, zbx_uint64_t itemid)
{
	return DBexecute("insert into items_applications set itemappid=" ZBX_FS_UI64 ",applicationid=" ZBX_FS_UI64 ","
			"itemid=" ZBX_FS_UI64,
			itemappid, applicationid, itemid);
}

static int	DBpatch_4040308_item_discovery(zbx_uint64_t itemdiscoveryid, zbx_uint64_t itemid,
		zbx_uint64_t parent_itemid)
{
	return DBexecute("insert into item_discovery set itemdiscoveryid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64 ","
			"parent_itemid=" ZBX_FS_UI64 ",key_='',lastcheck=0,ts_delete=0",
			itemdiscoveryid, itemid, parent_itemid);
}

static int	DBpatch_4040308_item_preproc(zbx_uint64_t item_preprocid, zbx_uint64_t itemid, const char *params,
		int error_handler)
{
	return DBexecute("insert into item_preproc set item_preprocid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64 ",step=1,"
			"type=12,params='%s',error_handler=%d,error_handler_params=''",
			item_preprocid, itemid, params, error_handler);
}

static int	DBpatch_4040308_lld_macro_path(zbx_uint64_t lld_macro_pathid, zbx_uint64_t itemid,
		const char *lld_macro, const char *path)
{
	return DBexecute("insert into lld_macro_path set lld_macro_pathid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64 ","
			"lld_macro='%s',path='%s'",
			lld_macro_pathid, itemid, lld_macro, path);
}

static int	DBpatch_4040308(void)
{
	int		ret = FAIL;

	zbx_uint64_t	groupid_templates;			/* groupid of "Templates" host group */
	zbx_uint64_t	hostid_template_dns;			/* hostid of "Template DNS" template */
	zbx_uint64_t	hostgroupid_template_dns;		/* hostgroupid of "Template DNS" template in "Templates" host group */
	zbx_uint64_t	valuemapid_rsm_service_availability;	/* valuemapid of "RSM Service Availability" */

	zbx_uint64_t	applicationid_next;
	zbx_uint64_t	applicationid_dns;			/* applicationid of "DNS" application in "Template DNS" template */
	zbx_uint64_t	applicationid_dnssec;			/* applicationid of "DNSSEC" application in "Template DNS" template */

	zbx_uint64_t	itemid_next;
	zbx_uint64_t	itemid_dnssec_enabled;			/* itemid of "DNSSEC enabled/disabled" item in "Template DNS" template */
	zbx_uint64_t	itemid_rsm_dns;				/* itemid of "DNS availability" item in "Template DNS" template */
	zbx_uint64_t	itemid_rsm_dns_nssok;			/* itemid of "Number of working Name Servers" item in "Template DNS" template */
	zbx_uint64_t	itemid_rsm_dns_ns_discovery;		/* itemid of "Name Servers discovery" item in "Template DNS" template */
	zbx_uint64_t	itemid_rsm_dns_nsip_discovery;		/* itemid of "NS-IP pairs discovery" item in "Template DNS" template */
	zbx_uint64_t	itemid_rsm_dns_ns_status;		/* itemid of "Status of $1" item prototype in "Template DNS" template */
	zbx_uint64_t	itemid_rsm_dns_rtt_tcp;			/* itemid of "RTT of $1,$2 using $3" item prototype in "Template DNS" template */
	zbx_uint64_t	itemid_rsm_dns_rtt_udp;			/* itemid of "RTT of $1,$2 using $3" item prototype in "Template DNS" template */
	zbx_uint64_t	itemid_rsm_dns_nsid;			/* itemid of "NSID of $1,$2" item prototype in "Template DNS" template */

	zbx_uint64_t	itemappid_next;
	zbx_uint64_t	itemappid_dnssec_enabled;		/* itemappid of "DNSSEC enabled/disabled" item */
	zbx_uint64_t	itemappid_rsm_dns;			/* itemappid of "DNS availability" item */
	zbx_uint64_t	itemappid_rsm_dns_nssok;		/* itemappid of "Number of working Name Servers" item */
	zbx_uint64_t	itemappid_rsm_dns_ns_status;		/* itemappid of "Status of $1" item prototype */
	zbx_uint64_t	itemappid_rsm_dns_rtt_tcp;		/* itemappid of "RTT of $1,$2 using $3" item prototype */
	zbx_uint64_t	itemappid_rsm_dns_rtt_udp;		/* itemappid of "RTT of $1,$2 using $3" item prototype */
	zbx_uint64_t	itemappid_rsm_dns_nsid;			/* itemappid of "NSID of $1,$2" item prototype */

	zbx_uint64_t	itemdiscoveryid_next;
	zbx_uint64_t	itemdiscoveryid_rsm_dns_ns_status;	/* itemdiscoveryid of "Status of $1" item prototype*/
	zbx_uint64_t	itemdiscoveryid_rsm_dns_rtt_tcp;	/* itemdiscoveryid of "RTT of $1,$2 using $3" item prototype*/
	zbx_uint64_t	itemdiscoveryid_rsm_dns_rtt_udp;	/* itemdiscoveryid of "RTT of $1,$2 using $3" item prototype*/
	zbx_uint64_t	itemdiscoveryid_rsm_dns_nsid;		/* itemdiscoveryid of "NSID of $1,$2" item prototype */

	zbx_uint64_t	item_preprocid_next;
	zbx_uint64_t	item_preprocid_rsm_dns_nssok;		/* item_preprocid of "Number of working Name Servers" item */
	zbx_uint64_t	item_preprocid_rsm_dns_ns_discovery;	/* item_preprocid of "Name Servers discovery" item*/
	zbx_uint64_t	item_preprocid_rsm_dns_nsip_discovery;	/* item_preprocid of "NS-IP pairs discovery" item*/
	zbx_uint64_t	item_preprocid_rsm_dns_ns_status;	/* item_preprocid of "Status of $1" item prototype*/
	zbx_uint64_t	item_preprocid_rsm_dns_rtt_tcp;		/* item_preprocid of "RTT of $1,$2 using $3" item prototype*/
	zbx_uint64_t	item_preprocid_rsm_dns_rtt_udp;		/* item_preprocid of "RTT of $1,$2 using $3" item prototype*/
	zbx_uint64_t	item_preprocid_rsm_dns_nsid;		/* item_preprocid of "NSID of $1,$2" item prototype */

	zbx_uint64_t	lld_macro_pathid_next;
	zbx_uint64_t	lld_macro_pathid_rsm_dns_ns_discovery_ns;	/* lld_macro_pathid of {#NS} in "Name Servers discovery" item */
	zbx_uint64_t	lld_macro_pathid_rsm_dns_nsip_discovery_ip;	/* lld_macro_pathid of {#IP} in "NS-IP pairs discovery" item */
	zbx_uint64_t	lld_macro_pathid_rsm_dns_nsip_discovery_ns;	/* lld_macro_pathid of {#NS} in "NS-IP pairs discovery" item */

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	groupid_templates                           = 1;
	hostid_template_dns                         = 99970;
	hostgroupid_template_dns                    = DBget_maxid_num("hosts_groups", 1);
	valuemapid_rsm_service_availability         = 110;

	applicationid_next                          = DBget_maxid_num("applications", 2);
	applicationid_dns                           = applicationid_next++;
	applicationid_dnssec                        = applicationid_next++;

	itemid_next                                 = DBget_maxid_num("items", 9);
	itemid_dnssec_enabled                       = itemid_next++;
	itemid_rsm_dns                              = itemid_next++;
	itemid_rsm_dns_nssok                        = itemid_next++;
	itemid_rsm_dns_ns_discovery                 = itemid_next++;
	itemid_rsm_dns_nsip_discovery               = itemid_next++;
	itemid_rsm_dns_ns_status                    = itemid_next++;
	itemid_rsm_dns_rtt_tcp                      = itemid_next++;
	itemid_rsm_dns_rtt_udp                      = itemid_next++;
	itemid_rsm_dns_nsid                         = itemid_next++;

	itemappid_next                              = DBget_maxid_num("items_applications", 7);
	itemappid_dnssec_enabled                    = itemappid_next++;
	itemappid_rsm_dns                           = itemappid_next++;
	itemappid_rsm_dns_nssok                     = itemappid_next++;
	itemappid_rsm_dns_ns_status                 = itemappid_next++;
	itemappid_rsm_dns_rtt_tcp                   = itemappid_next++;
	itemappid_rsm_dns_rtt_udp                   = itemappid_next++;
	itemappid_rsm_dns_nsid                      = itemappid_next++;

	itemdiscoveryid_next                        = DBget_maxid_num("item_discovery", 4);
	itemdiscoveryid_rsm_dns_ns_status           = itemdiscoveryid_next++;
	itemdiscoveryid_rsm_dns_rtt_tcp             = itemdiscoveryid_next++;
	itemdiscoveryid_rsm_dns_rtt_udp             = itemdiscoveryid_next++;
	itemdiscoveryid_rsm_dns_nsid                = itemdiscoveryid_next++;

	item_preprocid_next                         = DBget_maxid_num("item_preproc", 7);
	item_preprocid_rsm_dns_nssok                = item_preprocid_next++;
	item_preprocid_rsm_dns_ns_discovery         = item_preprocid_next++;
	item_preprocid_rsm_dns_nsip_discovery       = item_preprocid_next++;
	item_preprocid_rsm_dns_ns_status            = item_preprocid_next++;
	item_preprocid_rsm_dns_rtt_tcp              = item_preprocid_next++;
	item_preprocid_rsm_dns_rtt_udp              = item_preprocid_next++;
	item_preprocid_rsm_dns_nsid                 = item_preprocid_next++;

	lld_macro_pathid_next                       = DBget_maxid_num("lld_macro_path", 3);
	lld_macro_pathid_rsm_dns_ns_discovery_ns    = lld_macro_pathid_next++;
	lld_macro_pathid_rsm_dns_nsip_discovery_ip  = lld_macro_pathid_next++;
	lld_macro_pathid_rsm_dns_nsip_discovery_ns  = lld_macro_pathid_next++;

#define ITEM_TYPE_SIMPLE		3
#define ITEM_TYPE_CALCULATED		15
#define ITEM_TYPE_DEPENDENT		18

#define ITEM_VALUE_TYPE_FLOAT		0
#define ITEM_VALUE_TYPE_STR		1
#define ITEM_VALUE_TYPE_UINT64		3
#define ITEM_VALUE_TYPE_TEXT		4

#define ZBX_FLAG_DISCOVERY		0x01 /* Discovery rule */
#define ZBX_FLAG_DISCOVERY_PROTOTYPE	0x02 /* Item prototype */

#define CHECK(CODE) do {                \
	int result = (CODE);            \
	if (ZBX_DB_OK > result)         \
	{                               \
		goto out;               \
	}                               \
} while (0)

	CHECK(DBexecute("insert into hosts set hostid=" ZBX_FS_UI64 ",created=0,proxy_hostid=NULL,host='%s',status=3,"
			"disable_until=0,error='',available=0,errors_from=0,lastaccess=0,ipmi_authtype=-1,"
			"ipmi_privilege=2,ipmi_username='',ipmi_password='',ipmi_disable_until=0,ipmi_available=0,"
			"snmp_disable_until=0,snmp_available=0,maintenanceid=NULL,maintenance_status=0,"
			"maintenance_type=0,maintenance_from=0,ipmi_errors_from=0,snmp_errors_from=0,ipmi_error='',"
			"snmp_error='',jmx_disable_until=0,jmx_available=0,jmx_errors_from=0,jmx_error='',name='%s',"
			"info_1='',info_2='',flags=0,templateid=NULL,description='',tls_connect=1,tls_accept=1,"
			"tls_issuer='',tls_subject='',tls_psk_identity='',tls_psk='',proxy_address='',auto_compress=1",
			hostid_template_dns, "Template DNS", "Template DNS"));

	CHECK(DBexecute("insert into hosts_groups set hostgroupid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ","
			"groupid=" ZBX_FS_UI64,
			hostgroupid_template_dns, hostid_template_dns, groupid_templates));

	CHECK(DBpatch_4040308_create_application(applicationid_dns, hostid_template_dns, "DNS"));
	CHECK(DBpatch_4040308_create_application(applicationid_dnssec, hostid_template_dns, "DNSSEC"));

	CHECK(DBpatch_4040308_create_item(itemid_dnssec_enabled, ITEM_TYPE_CALCULATED, hostid_template_dns,
			"DNSSEC enabled/disabled", "dnssec.enabled", "60", "90d", "365d",
			ITEM_VALUE_TYPE_UINT64, 0, "{$RSM.TLD.DNSSEC.ENABLED}", 0,
			"History of DNSSEC being enabled or disabled.",
			"30d", 0));
	CHECK(DBpatch_4040308_create_item(itemid_rsm_dns, ITEM_TYPE_SIMPLE, hostid_template_dns,
			"DNS availability", "rsm.dns[{$RSM.TLD}]", "{$RSM.DNS.UDP.DELAY}", "0", "0",
			ITEM_VALUE_TYPE_TEXT, 0, "", 0,
			"Master item that performs the test and generates JSON with results."
			" This JSON will be parsed by dependent items. History must be disabled.",
			"30d", 0));
	CHECK(DBpatch_4040308_create_item(itemid_rsm_dns_nssok, ITEM_TYPE_DEPENDENT, hostid_template_dns,
			"Number of working Name Servers", "rsm.dns.nssok", "0", "90d", "365d",
			ITEM_VALUE_TYPE_UINT64, 0, "", 0,
			"Number of Name Servers that returned successful results out of those used in the test.",
			"30d", itemid_rsm_dns));
	CHECK(DBpatch_4040308_create_item(itemid_rsm_dns_ns_discovery, ITEM_TYPE_DEPENDENT, hostid_template_dns,
			"Name Servers discovery", "rsm.dns.ns.discovery", "0", "90d", "0",
			ITEM_VALUE_TYPE_TEXT, 0, "", ZBX_FLAG_DISCOVERY,
			"Discovers Name Servers that were used in DNS test.",
			"1000d", itemid_rsm_dns));
	CHECK(DBpatch_4040308_create_item(itemid_rsm_dns_nsip_discovery, ITEM_TYPE_DEPENDENT, hostid_template_dns,
			"NS-IP pairs discovery", "rsm.dns.nsip.discovery", "0", "90d", "0",
			ITEM_VALUE_TYPE_TEXT, 0, "", ZBX_FLAG_DISCOVERY,
			"Discovers Name Servers (NS-IP pairs) that were used in DNS test.",
			"1000d", itemid_rsm_dns));
	CHECK(DBpatch_4040308_create_item(itemid_rsm_dns_ns_status, ITEM_TYPE_DEPENDENT, hostid_template_dns,
			"Status of $1", "rsm.dns.ns.status[{#NS}]", "0", "90d", "365d",
			ITEM_VALUE_TYPE_UINT64, valuemapid_rsm_service_availability, "", ZBX_FLAG_DISCOVERY_PROTOTYPE,
			"",
			"30d", itemid_rsm_dns));
	CHECK(DBpatch_4040308_create_item(itemid_rsm_dns_rtt_tcp, ITEM_TYPE_DEPENDENT, hostid_template_dns,
			"RTT of $1,$2 using $3", "rsm.dns.rtt[{#NS},{#IP},tcp]", "0", "90d", "365d",
			ITEM_VALUE_TYPE_FLOAT, 0, "", ZBX_FLAG_DISCOVERY_PROTOTYPE,
			"",
			"30d", itemid_rsm_dns));
	CHECK(DBpatch_4040308_create_item(itemid_rsm_dns_rtt_udp, ITEM_TYPE_DEPENDENT, hostid_template_dns,
			"RTT of $1,$2 using $3", "rsm.dns.rtt[{#NS},{#IP},udp]", "0", "90d", "365d",
			ITEM_VALUE_TYPE_FLOAT, 0, "", ZBX_FLAG_DISCOVERY_PROTOTYPE,
			"",
			"30d", itemid_rsm_dns));
	CHECK(DBpatch_4040308_create_item(itemid_rsm_dns_nsid, ITEM_TYPE_DEPENDENT, hostid_template_dns,
			"NSID of $1,$2", "rsm.dns.nsid[{#NS},{#IP}]", "0", "90d", "0",
			ITEM_VALUE_TYPE_STR, 0, "", ZBX_FLAG_DISCOVERY_PROTOTYPE,
			"",
			"30d", itemid_rsm_dns));

	CHECK(DBpatch_4040308_item_to_app(itemappid_dnssec_enabled   , applicationid_dnssec, itemid_dnssec_enabled));
	CHECK(DBpatch_4040308_item_to_app(itemappid_rsm_dns          , applicationid_dns   , itemid_rsm_dns));
	CHECK(DBpatch_4040308_item_to_app(itemappid_rsm_dns_nssok    , applicationid_dns   , itemid_rsm_dns_nssok));
	CHECK(DBpatch_4040308_item_to_app(itemappid_rsm_dns_ns_status, applicationid_dns   , itemid_rsm_dns_ns_status));
	CHECK(DBpatch_4040308_item_to_app(itemappid_rsm_dns_rtt_tcp  , applicationid_dns   , itemid_rsm_dns_rtt_tcp));
	CHECK(DBpatch_4040308_item_to_app(itemappid_rsm_dns_rtt_udp  , applicationid_dns   , itemid_rsm_dns_rtt_udp));
	CHECK(DBpatch_4040308_item_to_app(itemappid_rsm_dns_nsid     , applicationid_dns   , itemid_rsm_dns_nsid));

	CHECK(DBpatch_4040308_item_discovery(itemdiscoveryid_rsm_dns_ns_status, itemid_rsm_dns_ns_status,
			itemid_rsm_dns_ns_discovery));
	CHECK(DBpatch_4040308_item_discovery(itemdiscoveryid_rsm_dns_rtt_tcp, itemid_rsm_dns_rtt_tcp,
			itemid_rsm_dns_nsip_discovery));
	CHECK(DBpatch_4040308_item_discovery(itemdiscoveryid_rsm_dns_rtt_udp, itemid_rsm_dns_rtt_udp,
			itemid_rsm_dns_nsip_discovery));
	CHECK(DBpatch_4040308_item_discovery(itemdiscoveryid_rsm_dns_nsid, itemid_rsm_dns_nsid,
			itemid_rsm_dns_nsip_discovery));

	CHECK(DBpatch_4040308_item_preproc(item_preprocid_rsm_dns_nssok, itemid_rsm_dns_nssok,
			"$.nssok", 0));
	CHECK(DBpatch_4040308_item_preproc(item_preprocid_rsm_dns_ns_discovery, itemid_rsm_dns_ns_discovery,
			"$.nss", 0));
	CHECK(DBpatch_4040308_item_preproc(item_preprocid_rsm_dns_nsip_discovery, itemid_rsm_dns_nsip_discovery,
			"$.nsips", 0));
	CHECK(DBpatch_4040308_item_preproc(item_preprocid_rsm_dns_ns_status, itemid_rsm_dns_ns_status,
			"$.nss[?(@.['ns'] == '{#NS}')].status.first()", 0));
	CHECK(DBpatch_4040308_item_preproc(item_preprocid_rsm_dns_rtt_tcp, itemid_rsm_dns_rtt_tcp,
			"$.nsips[?(@.['ns'] == '{#NS}' && @.['ip'] == '{#IP}' && @.['protocol'] == 'tcp')].rtt.first()", 1));
	CHECK(DBpatch_4040308_item_preproc(item_preprocid_rsm_dns_rtt_udp, itemid_rsm_dns_rtt_udp,
			"$.nsips[?(@.['ns'] == '{#NS}' && @.['ip'] == '{#IP}' && @.['protocol'] == 'udp')].rtt.first()", 1));
	CHECK(DBpatch_4040308_item_preproc(item_preprocid_rsm_dns_nsid, itemid_rsm_dns_nsid,
			"$.nsips[?(@.['ns'] == '{#NS}' && @.['ip'] == '{#IP}')].nsid.first()", 0));

	CHECK(DBpatch_4040308_lld_macro_path(lld_macro_pathid_rsm_dns_ns_discovery_ns,
			itemid_rsm_dns_ns_discovery, "{#NS}", "$.ns"));
	CHECK(DBpatch_4040308_lld_macro_path(lld_macro_pathid_rsm_dns_nsip_discovery_ip,
			itemid_rsm_dns_nsip_discovery, "{#IP}", "$.ip"));
	CHECK(DBpatch_4040308_lld_macro_path(lld_macro_pathid_rsm_dns_nsip_discovery_ns,
			itemid_rsm_dns_nsip_discovery, "{#NS}", "$.ns"));

#undef ITEM_TYPE_SIMPLE
#undef ITEM_TYPE_CALCULATED
#undef ITEM_TYPE_DEPENDENT

#undef ITEM_VALUE_TYPE_FLOAT
#undef ITEM_VALUE_TYPE_STR
#undef ITEM_VALUE_TYPE_UINT64
#undef ITEM_VALUE_TYPE_TEXT

#undef ZBX_FLAG_DISCOVERY
#undef ZBX_FLAG_DISCOVERY_PROTOTYPE

#undef CHECK

	ret = SUCCEED;
out:
	return ret;
}

#endif

DBPATCH_START(4040)

/* version, duplicates flag, mandatory flag */

DBPATCH_ADD(4040000, 0, 1)
DBPATCH_ADD(4040300, 0, 1)	/* RSM FY20 */
DBPATCH_ADD(4040301, 0, 1)
DBPATCH_ADD(4040302, 0, 1)
DBPATCH_ADD(4040303, 0, 1)
DBPATCH_ADD(4040304, 0, 1)
DBPATCH_ADD(4040305, 0, 1)
DBPATCH_ADD(4040306, 0, 0)
DBPATCH_ADD(4040307, 0, 0)
DBPATCH_ADD(4040308, 0, 0)	/* add "Template DNS" template */

DBPATCH_END()
