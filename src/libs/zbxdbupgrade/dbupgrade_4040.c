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

DBPATCH_END()
