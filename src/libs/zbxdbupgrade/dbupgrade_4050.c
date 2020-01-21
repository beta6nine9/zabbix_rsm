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

/*
 * NOTE FOR FUTURE OURSELVES
 *
 * Originally our custom patches went into dbupgrade_4040.c.
 * It was agreed to move them to dbupgrade_4050.c during upgrading to Zabbix 4.5.
 * It was also agreed to move them to dbupgrade_5000.c when we move to Zabbix 5.0.
 *
 * When these patches are moved to dbupgrade_5000.c, this reminder should be removed.
 */

/*
 * Some common helpers that can be used as one-liners in patches to avoid copy-pasting.
 *
 * Be careful when implementing new helpers - they have to be generic.
 * If some code is needed only 1-2 times, it doesn't fit here.
 * If some code depends on stuff that is likely to change, it doesn't fit here.
 *
 * If more specific helper is needed, it must be implemented close to the patch that needs it. Specific
 * helpers can be implemented either as functions right before DBpatch_4040xxx(), or as macros inside
 * the DBpatch_4040xxx(). If they're implemented as macros, don't forget to #undef them.
 */

/* checks if this is server; used for skipping patches when running on proxy */
#define ONLY_SERVER()													\
															\
do															\
{															\
	if (0 == (program_type & ZBX_PROGRAM_TYPE_SERVER))								\
	{														\
		return SUCCEED;												\
	}														\
}															\
while (0)

/* checks result of function that returns SUCCEED or FAIL */
#define CHECK_RESULT(CODE)												\
															\
do															\
{															\
	int __result = (CODE);												\
	if (SUCCEED != __result)											\
	{														\
		goto out;												\
	}														\
}															\
while (0)

/* checks result of DBexecute() */
#define CHECK(CODE)													\
															\
do															\
{															\
	int __result = (CODE);												\
	if (ZBX_DB_OK > __result)											\
	{														\
		goto out;												\
	}														\
}															\
while (0)

/* selects single value of zbx_uint64_t type from the database */
#define SELECT_VALUE_UINT64(target_variable, query, ...)								\
															\
do															\
{															\
	DB_RESULT	__result;											\
	DB_ROW		__row;												\
															\
	__result = DBselect(query, __VA_ARGS__);									\
															\
	/* check for errors */												\
	if (NULL == __result)												\
	{														\
		goto out;												\
	}														\
															\
	__row = DBfetch(__result);											\
															\
	/* check if there's at least one row in the resultset */							\
	if (NULL == __row)												\
	{														\
		DBfree_result(__result);										\
		goto out;												\
	}														\
															\
	ZBX_STR2UINT64(target_variable, __row[0]);									\
															\
	__row = DBfetch(__result);											\
															\
	/* check that there are no more rows in the resultset */							\
	if (NULL != __row)												\
	{														\
		DBfree_result(__result);										\
		goto out;												\
	}														\
															\
	DBfree_result(__result);											\
}															\
while (0)

/* gets hostid of the template; status=3 = HOST_STATUS_TEMPLATE */
#define GET_TEMPLATE_ID(hostid, template_host)										\
		SELECT_VALUE_UINT64(hostid, "select hostid from hosts where host='%s' and status=3", template_host)

/* gets itemid of the template's item, status=3 = HOST_STATUS_TEMPLATE */
#define GET_TEMPLATE_ITEM_ID(itemid, template_host, item_key)								\
		SELECT_VALUE_UINT64(											\
				itemid,											\
				"select"										\
					" itemid"									\
				" from"											\
					" items"									\
					" left join hosts on hosts.hostid=items.hostid"					\
				" where"										\
					" hosts.host='%s' and"								\
					" hosts.status=3 and"								\
					" items.key_='%s'",								\
				template_host, item_key)

/* gets itemid of the template's item, status=3 = HOST_STATUS_TEMPLATE */
#define GET_TEMPLATE_ITEM_ID_BY_PATTERN(itemid, template_host, item_key_pattern)					\
		SELECT_VALUE_UINT64(											\
				itemid,											\
				"select"										\
					" itemid"									\
				" from"											\
					" items"									\
					" left join hosts on hosts.hostid=items.hostid"					\
				" where"										\
					" hosts.host='%s' and"								\
					" hosts.status=3 and"								\
					" items.key_ like '%s'",							\
				template_host, item_key_pattern)

/* gets valuemapid of the value map */
#define GET_VALUE_MAP_ID(valuemapid, name)										\
		SELECT_VALUE_UINT64(valuemapid, "select valuemapid from valuemaps where name='%s'", name)

/* gets groupid of the host group */
#define GET_HOST_GROUP_ID(groupid, name)										\
		SELECT_VALUE_UINT64(groupid, "select groupid from hstgrp where name='%s'", name)

/*
 * 5.0 development database patches
 */

#ifndef HAVE_SQLITE3

extern unsigned char	program_type;

static int	DBpatch_4050001(void)
{
	return DBdrop_foreign_key("items", 1);
}

static int	DBpatch_4050002(void)
{
	return DBdrop_index("items", "items_1");
}

static int	DBpatch_4050003(void)
{
	const ZBX_FIELD	field = {"key_", "", NULL, NULL, 2048, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};

	return DBmodify_field_type("items", &field, NULL);
}

static int	DBpatch_4050004(void)
{
#ifdef HAVE_MYSQL
	return DBcreate_index("items", "items_1", "hostid,key_(1021)", 0);
#else
	return DBcreate_index("items", "items_1", "hostid,key_", 0);
#endif
}

static int	DBpatch_4050005(void)
{
	const ZBX_FIELD	field = {"hostid", NULL, "hosts", "hostid", 0, 0, 0, ZBX_FK_CASCADE_DELETE};

	return DBadd_foreign_key("items", 1, &field);
}

static int	DBpatch_4050006(void)
{
	const ZBX_FIELD	field = {"key_", "", NULL, NULL, 2048, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};

	return DBmodify_field_type("item_discovery", &field, NULL);
}

static int	DBpatch_4050007(void)
{
	const ZBX_FIELD	field = {"key_", "", NULL, NULL, 2048, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};

	return DBmodify_field_type("dchecks", &field, NULL);
}

static int	DBpatch_4050010(void)
{
	int		i;
	const char	*values[] = {
			"web.usergroup.filter_users_status", "web.usergroup.filter_user_status",
			"web.usergrps.php.sort", "web.usergroup.sort",
			"web.usergrps.php.sortorder", "web.usergroup.sortorder",
			"web.adm.valuemapping.php.sortorder", "web.valuemap.list.sortorder",
			"web.adm.valuemapping.php.sort", "web.valuemap.list.sort",
			"web.latest.php.sort", "web.latest.sort",
			"web.latest.php.sortorder", "web.latest.sortorder"
		};

	if (0 == (program_type & ZBX_PROGRAM_TYPE_SERVER))
		return SUCCEED;

	for (i = 0; i < (int)ARRSIZE(values); i += 2)
	{
		if (ZBX_DB_OK > DBexecute("update profiles set idx='%s' where idx='%s'", values[i + 1], values[i]))
			return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_4050011(void)
{
#if defined(HAVE_IBM_DB2) || defined(HAVE_POSTGRESQL)
	const char *cast_value_str = "bigint";
#elif defined(HAVE_MYSQL)
	const char *cast_value_str = "unsigned";
#elif defined(HAVE_ORACLE)
	const char *cast_value_str = "number(20)";
#endif

	if (ZBX_DB_OK > DBexecute(
			"update profiles"
			" set value_id=CAST(value_str as %s),"
				" value_str='',"
				" type=1"	/* PROFILE_TYPE_ID */
			" where type=3"	/* PROFILE_TYPE_STR */
				" and (idx='web.latest.filter.groupids' or idx='web.latest.filter.hostids')", cast_value_str))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_4050012(void)
{
	const ZBX_FIELD	field = {"passwd", "", NULL, NULL, 60, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};

	return DBmodify_field_type("users", &field, NULL);
}

static int	DBpatch_4050500(void)
{
	/* this patch begins RSM FY20 upgrade sequence and has been intentionally left blank */

	return SUCCEED;
}

static int	DBpatch_4050501(void)
{
	int	ret = FAIL;

	ONLY_SERVER();

	/* 3 = ITEM_TYPE_SIMPLE */

	CHECK(DBexecute("update items set delay='{$RSM.DNS.UDP.DELAY}' where key_ like 'rsm.dns.udp[%%' and type=3"));
	CHECK(DBexecute("update items set delay='{$RSM.DNS.TCP.DELAY}' where key_ like 'rsm.dns.tcp[%%' and type=3"));
	CHECK(DBexecute("update items set delay='{$RSM.RDDS.DELAY}' where key_ like 'rsm.rdds[%%' and type=3"));
	CHECK(DBexecute("update items set delay='{$RSM.RDAP.DELAY}' where key_ like 'rdap[%%' and type=3"));
	CHECK(DBexecute("update items set delay='{$RSM.EPP.DELAY}' where key_ like 'rsm.epp[%%' and type=3"));

	ret = SUCCEED;
out:
	return ret;
}

typedef struct
{
	const char *const	macro;
	const char *const	description;
}
macro_descr_t;

static int	DBpatch_4050502(void)
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

	ONLY_SERVER();

	for (i = 0; macro_descr[i].macro != NULL; i++)
	{
		ret = DBexecute("update globalmacro set description='%s' where macro='%s' "
				"and description=''", macro_descr[i].description, macro_descr[i].macro);

		if (ZBX_DB_OK > ret)
			zabbix_log(LOG_LEVEL_WARNING, "did not update global macro '%s'", macro_descr[i].macro);
	}

	return SUCCEED;
}

static int	DBpatch_4050503(void)
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

	ONLY_SERVER();

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

static int	DBpatch_4050504(void)
{
	int	ret = FAIL;

	zbx_uint64_t	valuemapid_next;
	zbx_uint64_t	valuemapid_dns_test_mode;		/* valuemapid of "DNS test mode" */
	zbx_uint64_t	valuemapid_transport_protocol;		/* valuemapid of "Transport protocol" */

	zbx_uint64_t	mappingid_next;
	zbx_uint64_t	mappingid_dns_test_mode_normal;		/* mappingid of "Normal" value in "DNS test mode" mapping */
	zbx_uint64_t	mappingid_dns_test_mode_critical;	/* mappingid of "Critical" value in "DNS test mode" mapping */
	zbx_uint64_t	mappingid_transport_protocol_udp;	/* mappingid of "UDP" value in "Transport protocol" mapping */
	zbx_uint64_t	mappingid_transport_protocol_tcp;	/* mappingid of "TCP" value in "Transport protocol" mapping */

	ONLY_SERVER();

	valuemapid_next                  = DBget_maxid_num("valuemaps", 2);
	valuemapid_dns_test_mode         = valuemapid_next++;
	valuemapid_transport_protocol    = valuemapid_next++;

	mappingid_next                   = DBget_maxid_num("mappings", 4);
	mappingid_dns_test_mode_normal   = mappingid_next++;
	mappingid_dns_test_mode_critical = mappingid_next++;
	mappingid_transport_protocol_udp = mappingid_next++;
	mappingid_transport_protocol_tcp = mappingid_next++;

#define INSERT_INTO_VALUEMAPS	"insert into valuemaps set valuemapid=" ZBX_FS_UI64 ",name='%s'"
#define INSERT_INTO_MAPPINGS	"insert into mappings set mappingid=" ZBX_FS_UI64 ",valuemapid=" ZBX_FS_UI64 ",value='%s',newvalue='%s'"

	CHECK(DBexecute(INSERT_INTO_VALUEMAPS, valuemapid_dns_test_mode, "DNS test mode"));
	CHECK(DBexecute(INSERT_INTO_VALUEMAPS, valuemapid_transport_protocol, "Transport protocol"));

	CHECK(DBexecute(INSERT_INTO_MAPPINGS, mappingid_dns_test_mode_normal, valuemapid_dns_test_mode, "0", "Normal"));
	CHECK(DBexecute(INSERT_INTO_MAPPINGS, mappingid_dns_test_mode_critical, valuemapid_dns_test_mode, "1", "Critical"));
	CHECK(DBexecute(INSERT_INTO_MAPPINGS, mappingid_transport_protocol_udp, valuemapid_transport_protocol, "0", "UDP"));
	CHECK(DBexecute(INSERT_INTO_MAPPINGS, mappingid_transport_protocol_tcp, valuemapid_transport_protocol, "1", "TCP"));

#undef INSERT_INTO_VALUEMAPS
#undef INSERT_INTO_MAPPINGS

	ret = SUCCEED;
out:
	return ret;
}

static int	DBpatch_4050505_create_application(zbx_uint64_t applicationid, zbx_uint64_t hostid, const char *name)
{
	return DBexecute("insert into applications set applicationid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ",name='%s',"
			"flags=0",
			applicationid, hostid, name);
}

static int	DBpatch_4050505_create_item(zbx_uint64_t itemid, int type, zbx_uint64_t hostid, const char *name,
		const char *key_, const char *delay, const char *history, const char *trends, int value_type,
		zbx_uint64_t valuemapid, const char *params, int flags, const char *description, const char *lifetime,
		zbx_uint64_t master_itemid)
{
	return DBexecute("insert into items set itemid=" ZBX_FS_UI64 ",type=%d,snmp_community='',snmp_oid='',"
			"hostid=" ZBX_FS_UI64 ",name='%s',key_='%s',delay='%s',history='%s',trends='%s',status=0,"
			"value_type=%d,trapper_hosts='',units='',snmpv3_securityname='',snmpv3_securitylevel=0,"
			"snmpv3_authpassphrase='',snmpv3_privpassphrase='',formula='',logtimefmt='',templateid=NULL,"
			"valuemapid=nullif(" ZBX_FS_UI64 ",0),params='%s',ipmi_sensor='',authtype=0,username='',"
			"password='',publickey='',privatekey='',flags=%d,interfaceid=NULL,port='',description='%s',"
			"inventory_link=0,lifetime='%s',snmpv3_authprotocol=0,snmpv3_privprotocol=0,"
			"snmpv3_contextname='',evaltype=0,jmx_endpoint='',master_itemid=nullif(" ZBX_FS_UI64 ",0),"
			"timeout='3s',url='',query_fields='',posts='',status_codes='200',follow_redirects=1,"
			"post_type=0,http_proxy='',headers='',retrieve_mode=0,request_method=0,output_format=0,"
			"ssl_cert_file='',ssl_key_file='',ssl_key_password='',verify_peer=0,verify_host=0,"
			"allow_traps=0",
			itemid, type, hostid, name, key_, delay, history, trends, value_type, valuemapid, params, flags,
			description, lifetime, master_itemid);
}

static int	DBpatch_4050505_item_to_app(zbx_uint64_t itemappid, zbx_uint64_t applicationid, zbx_uint64_t itemid)
{
	return DBexecute("insert into items_applications set itemappid=" ZBX_FS_UI64 ",applicationid=" ZBX_FS_UI64 ","
			"itemid=" ZBX_FS_UI64,
			itemappid, applicationid, itemid);
}

static int	DBpatch_4050505_item_discovery(zbx_uint64_t itemdiscoveryid, zbx_uint64_t itemid,
		zbx_uint64_t parent_itemid)
{
	return DBexecute("insert into item_discovery set itemdiscoveryid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64 ","
			"parent_itemid=" ZBX_FS_UI64 ",key_='',lastcheck=0,ts_delete=0",
			itemdiscoveryid, itemid, parent_itemid);
}

static int	DBpatch_4050505_item_preproc(zbx_uint64_t item_preprocid, zbx_uint64_t itemid, const char *params,
		int error_handler)
{
	return DBexecute("insert into item_preproc set item_preprocid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64 ",step=1,"
			"type=12,params='%s',error_handler=%d,error_handler_params=''",
			item_preprocid, itemid, params, error_handler);
}

static int	DBpatch_4050505_lld_macro_path(zbx_uint64_t lld_macro_pathid, zbx_uint64_t itemid,
		const char *lld_macro, const char *path)
{
	return DBexecute("insert into lld_macro_path set lld_macro_pathid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64 ","
			"lld_macro='%s',path='%s'",
			lld_macro_pathid, itemid, lld_macro, path);
}

static int	DBpatch_4050505(void)
{
	int		ret = FAIL;

	zbx_uint64_t	groupid_templates;				/* groupid of "Templates" host group */

	zbx_uint64_t	valuemapid_rsm_service_availability;		/* valuemapid of "RSM Service Availability" */
	zbx_uint64_t	valuemapid_dns_test_mode;			/* valuemapid of "DNS test mode" */
	zbx_uint64_t	valuemapid_transport_protocol;			/* valuemapid of "Transport protocol" */
	zbx_uint64_t	valuemapid_rsm_dns_rtt;				/* valuemapid of "RSM DNS rtt" */

	zbx_uint64_t	hostid_template_dns_test;			/* hostid of "Template DNS Test" template */
	zbx_uint64_t	hostgroupid_template_dns;			/* hostgroupid of "Template DNS Test" template in "Templates" host group */

	zbx_uint64_t	applicationid_next;
	zbx_uint64_t	applicationid_dns;				/* applicationid of "DNS" application in "Template DNS Test" template */
	zbx_uint64_t	applicationid_dnssec;				/* applicationid of "DNSSEC" application in "Template DNS Test" template */

	zbx_uint64_t	itemid_next;
	zbx_uint64_t	itemid_dnssec_enabled;				/* itemid of "DNSSEC enabled/disabled" item in "Template DNS Test" template */
	zbx_uint64_t	itemid_rsm_dns;					/* itemid of "DNS Test" item in "Template DNS Test" template */
	zbx_uint64_t	itemid_rsm_dns_nssok;				/* itemid of "Number of working Name Servers" item in "Template DNS Test" template */
	zbx_uint64_t	itemid_rsm_dns_ns_discovery;			/* itemid of "Name Servers discovery" item in "Template DNS Test" template */
	zbx_uint64_t	itemid_rsm_dns_nsip_discovery;			/* itemid of "NS-IP pairs discovery" item in "Template DNS Test" template */
	zbx_uint64_t	itemid_rsm_dns_ns_status;			/* itemid of "Status of $1" item prototype in "Template DNS Test" template */
	zbx_uint64_t	itemid_rsm_dns_rtt_tcp;				/* itemid of "RTT of $1,$2 using $3" item prototype in "Template DNS Test" template */
	zbx_uint64_t	itemid_rsm_dns_rtt_udp;				/* itemid of "RTT of $1,$2 using $3" item prototype in "Template DNS Test" template */
	zbx_uint64_t	itemid_rsm_dns_nsid;				/* itemid of "NSID of $1,$2" item prototype in "Template DNS Test" template */
	zbx_uint64_t	itemid_rsm_dns_mode;				/* itemid of "The mode of the Test" item prototype in "Template DNS Test" template */
	zbx_uint64_t	itemid_rsm_dns_protocol;			/* itemid of "Transport protocol of the Test" item prototype in "Template DNS Test" template */

	zbx_uint64_t	itemappid_next;
	zbx_uint64_t	itemappid_dnssec_enabled;			/* itemappid of "DNSSEC enabled/disabled" item */
	zbx_uint64_t	itemappid_rsm_dns;				/* itemappid of "DNS availability" item */
	zbx_uint64_t	itemappid_rsm_dns_nssok;			/* itemappid of "Number of working Name Servers" item */
	zbx_uint64_t	itemappid_rsm_dns_ns_status;			/* itemappid of "Status of $1" item prototype */
	zbx_uint64_t	itemappid_rsm_dns_rtt_tcp;			/* itemappid of "RTT of $1,$2 using $3" item prototype */
	zbx_uint64_t	itemappid_rsm_dns_rtt_udp;			/* itemappid of "RTT of $1,$2 using $3" item prototype */
	zbx_uint64_t	itemappid_rsm_dns_nsid;				/* itemappid of "NSID of $1,$2" item prototype */
	zbx_uint64_t	itemappid_rsm_dns_mode;				/* itemappid of "The mode of the Test" item prototype */
	zbx_uint64_t	itemappid_rsm_dns_protocol;			/* itemappid of "Transport protocol of the Test" item prototype */

	zbx_uint64_t	itemdiscoveryid_next;
	zbx_uint64_t	itemdiscoveryid_rsm_dns_ns_status;		/* itemdiscoveryid of "Status of $1" item prototype*/
	zbx_uint64_t	itemdiscoveryid_rsm_dns_rtt_tcp;		/* itemdiscoveryid of "RTT of $1,$2 using $3" item prototype*/
	zbx_uint64_t	itemdiscoveryid_rsm_dns_rtt_udp;		/* itemdiscoveryid of "RTT of $1,$2 using $3" item prototype*/
	zbx_uint64_t	itemdiscoveryid_rsm_dns_nsid;			/* itemdiscoveryid of "NSID of $1,$2" item prototype */

	zbx_uint64_t	item_preprocid_next;
	zbx_uint64_t	item_preprocid_rsm_dns_nssok;			/* item_preprocid of "Number of working Name Servers" item */
	zbx_uint64_t	item_preprocid_rsm_dns_ns_discovery;		/* item_preprocid of "Name Servers discovery" item*/
	zbx_uint64_t	item_preprocid_rsm_dns_nsip_discovery;		/* item_preprocid of "NS-IP pairs discovery" item*/
	zbx_uint64_t	item_preprocid_rsm_dns_ns_status;		/* item_preprocid of "Status of $1" item prototype*/
	zbx_uint64_t	item_preprocid_rsm_dns_rtt_tcp;			/* item_preprocid of "RTT of $1,$2 using $3" item prototype*/
	zbx_uint64_t	item_preprocid_rsm_dns_rtt_udp;			/* item_preprocid of "RTT of $1,$2 using $3" item prototype*/
	zbx_uint64_t	item_preprocid_rsm_dns_nsid;			/* item_preprocid of "NSID of $1,$2" item prototype */
	zbx_uint64_t	item_preprocid_rsm_dns_mode;			/* item_preprocid of "The mode of the Test" item prototype */
	zbx_uint64_t	item_preprocid_rsm_dns_protocol;		/* item_preprocid of "Transport protocol of the Test" item prototype */

	zbx_uint64_t	lld_macro_pathid_next;
	zbx_uint64_t	lld_macro_pathid_rsm_dns_ns_discovery_ns;	/* lld_macro_pathid of {#NS} in "Name Servers discovery" item */
	zbx_uint64_t	lld_macro_pathid_rsm_dns_nsip_discovery_ip;	/* lld_macro_pathid of {#IP} in "NS-IP pairs discovery" item */
	zbx_uint64_t	lld_macro_pathid_rsm_dns_nsip_discovery_ns;	/* lld_macro_pathid of {#NS} in "NS-IP pairs discovery" item */

	ONLY_SERVER();

	GET_HOST_GROUP_ID(groupid_templates, "Templates");

	GET_VALUE_MAP_ID(valuemapid_rsm_service_availability, "RSM Service Availability");
	GET_VALUE_MAP_ID(valuemapid_dns_test_mode, "DNS test mode");
	GET_VALUE_MAP_ID(valuemapid_transport_protocol, "Transport protocol");
	GET_VALUE_MAP_ID(valuemapid_rsm_dns_rtt, "RSM DNS rtt");

	hostid_template_dns_test                   = DBget_maxid_num("hosts", 1);

	hostgroupid_template_dns                   = DBget_maxid_num("hosts_groups", 1);

	applicationid_next                         = DBget_maxid_num("applications", 2);
	applicationid_dns                          = applicationid_next++;
	applicationid_dnssec                       = applicationid_next++;

	itemid_next                                = DBget_maxid_num("items", 11);
	itemid_dnssec_enabled                      = itemid_next++;
	itemid_rsm_dns                             = itemid_next++;
	itemid_rsm_dns_nssok                       = itemid_next++;
	itemid_rsm_dns_ns_discovery                = itemid_next++;
	itemid_rsm_dns_nsip_discovery              = itemid_next++;
	itemid_rsm_dns_ns_status                   = itemid_next++;
	itemid_rsm_dns_rtt_tcp                     = itemid_next++;
	itemid_rsm_dns_rtt_udp                     = itemid_next++;
	itemid_rsm_dns_nsid                        = itemid_next++;
	itemid_rsm_dns_mode                        = itemid_next++;
	itemid_rsm_dns_protocol                    = itemid_next++;

	itemappid_next                             = DBget_maxid_num("items_applications", 9);
	itemappid_dnssec_enabled                   = itemappid_next++;
	itemappid_rsm_dns                          = itemappid_next++;
	itemappid_rsm_dns_nssok                    = itemappid_next++;
	itemappid_rsm_dns_ns_status                = itemappid_next++;
	itemappid_rsm_dns_rtt_tcp                  = itemappid_next++;
	itemappid_rsm_dns_rtt_udp                  = itemappid_next++;
	itemappid_rsm_dns_nsid                     = itemappid_next++;
	itemappid_rsm_dns_mode                     = itemappid_next++;
	itemappid_rsm_dns_protocol                 = itemappid_next++;

	itemdiscoveryid_next                       = DBget_maxid_num("item_discovery", 4);
	itemdiscoveryid_rsm_dns_ns_status          = itemdiscoveryid_next++;
	itemdiscoveryid_rsm_dns_rtt_tcp            = itemdiscoveryid_next++;
	itemdiscoveryid_rsm_dns_rtt_udp            = itemdiscoveryid_next++;
	itemdiscoveryid_rsm_dns_nsid               = itemdiscoveryid_next++;

	item_preprocid_next                        = DBget_maxid_num("item_preproc", 9);
	item_preprocid_rsm_dns_nssok               = item_preprocid_next++;
	item_preprocid_rsm_dns_ns_discovery        = item_preprocid_next++;
	item_preprocid_rsm_dns_nsip_discovery      = item_preprocid_next++;
	item_preprocid_rsm_dns_ns_status           = item_preprocid_next++;
	item_preprocid_rsm_dns_rtt_tcp             = item_preprocid_next++;
	item_preprocid_rsm_dns_rtt_udp             = item_preprocid_next++;
	item_preprocid_rsm_dns_nsid                = item_preprocid_next++;
	item_preprocid_rsm_dns_mode                = item_preprocid_next++;
	item_preprocid_rsm_dns_protocol            = item_preprocid_next++;

	lld_macro_pathid_next                      = DBget_maxid_num("lld_macro_path", 3);
	lld_macro_pathid_rsm_dns_ns_discovery_ns   = lld_macro_pathid_next++;
	lld_macro_pathid_rsm_dns_nsip_discovery_ip = lld_macro_pathid_next++;
	lld_macro_pathid_rsm_dns_nsip_discovery_ns = lld_macro_pathid_next++;

#define ITEM_TYPE_SIMPLE		3
#define ITEM_TYPE_CALCULATED		15
#define ITEM_TYPE_DEPENDENT		18

#define ITEM_VALUE_TYPE_FLOAT		0
#define ITEM_VALUE_TYPE_STR		1
#define ITEM_VALUE_TYPE_UINT64		3
#define ITEM_VALUE_TYPE_TEXT		4

#define ZBX_FLAG_DISCOVERY		0x01 /* Discovery rule */
#define ZBX_FLAG_DISCOVERY_PROTOTYPE	0x02 /* Item prototype */

	CHECK(DBexecute("insert into hosts set hostid=" ZBX_FS_UI64 ",created=0,proxy_hostid=NULL,host='%s',status=3,"
			"disable_until=0,error='',available=0,errors_from=0,lastaccess=0,ipmi_authtype=-1,"
			"ipmi_privilege=2,ipmi_username='',ipmi_password='',ipmi_disable_until=0,ipmi_available=0,"
			"snmp_disable_until=0,snmp_available=0,maintenanceid=NULL,maintenance_status=0,"
			"maintenance_type=0,maintenance_from=0,ipmi_errors_from=0,snmp_errors_from=0,ipmi_error='',"
			"snmp_error='',jmx_disable_until=0,jmx_available=0,jmx_errors_from=0,jmx_error='',name='%s',"
			"info_1='',info_2='',flags=0,templateid=NULL,description='',tls_connect=1,tls_accept=1,"
			"tls_issuer='',tls_subject='',tls_psk_identity='',tls_psk='',proxy_address='',auto_compress=1",
			hostid_template_dns_test, "Template DNS Test", "Template DNS Test"));

	CHECK(DBexecute("insert into hosts_groups set hostgroupid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ","
			"groupid=" ZBX_FS_UI64,
			hostgroupid_template_dns, hostid_template_dns_test, groupid_templates));

	CHECK(DBpatch_4050505_create_application(applicationid_dns, hostid_template_dns_test, "DNS"));
	CHECK(DBpatch_4050505_create_application(applicationid_dnssec, hostid_template_dns_test, "DNSSEC"));

	CHECK(DBpatch_4050505_create_item(itemid_dnssec_enabled, ITEM_TYPE_CALCULATED, hostid_template_dns_test,
			"DNSSEC enabled/disabled", "dnssec.enabled", "60", "90d", "365d",
			ITEM_VALUE_TYPE_UINT64, 0, "{$RSM.TLD.DNSSEC.ENABLED}", 0,
			"History of DNSSEC being enabled or disabled.",
			"30d", 0));
	CHECK(DBpatch_4050505_create_item(itemid_rsm_dns, ITEM_TYPE_SIMPLE, hostid_template_dns_test,
			"DNS Test",
			"rsm.dns[{$RSM.TLD},{$RSM.DNS.TESTPREFIX},{$RSM.DNS.NAME.SERVERS},{$RSM.TLD.DNSSEC.ENABLED},"
				"{$RSM.TLD.RDDS.ENABLED},{$RSM.TLD.EPP.ENABLED},{$RSM.TLD.DNS.UDP.ENABLED},"
				"{$RSM.TLD.DNS.TCP.ENABLED},{$RSM.IP4.ENABLED},{$RSM.IP6.ENABLED},{$RSM.RESOLVER},"
				"{$RSM.DNS.UDP.RTT.HIGH},{$RSM.DNS.TCP.RTT.HIGH}]",
			"{$RSM.DNS.UDP.DELAY}", "0", "0",
			ITEM_VALUE_TYPE_TEXT, 0, "", 0,
			"Master item that performs the test and generates JSON with results."
			" This JSON will be parsed by dependent items. History must be disabled.",
			"30d", 0));
	CHECK(DBpatch_4050505_create_item(itemid_rsm_dns_nssok, ITEM_TYPE_DEPENDENT, hostid_template_dns_test,
			"Number of working Name Servers", "rsm.dns.nssok", "0", "90d", "365d",
			ITEM_VALUE_TYPE_UINT64, 0, "", 0,
			"Number of Name Servers that returned successful results out of those used in the test.",
			"30d", itemid_rsm_dns));
	CHECK(DBpatch_4050505_create_item(itemid_rsm_dns_ns_discovery, ITEM_TYPE_DEPENDENT, hostid_template_dns_test,
			"Name Servers discovery", "rsm.dns.ns.discovery", "0", "90d", "0",
			ITEM_VALUE_TYPE_TEXT, 0, "", ZBX_FLAG_DISCOVERY,
			"Discovers Name Servers that were used in DNS test.",
			"1000d", itemid_rsm_dns));
	CHECK(DBpatch_4050505_create_item(itemid_rsm_dns_nsip_discovery, ITEM_TYPE_DEPENDENT, hostid_template_dns_test,
			"NS-IP pairs discovery", "rsm.dns.nsip.discovery", "0", "90d", "0",
			ITEM_VALUE_TYPE_TEXT, 0, "", ZBX_FLAG_DISCOVERY,
			"Discovers Name Servers (NS-IP pairs) that were used in DNS test.",
			"1000d", itemid_rsm_dns));
	CHECK(DBpatch_4050505_create_item(itemid_rsm_dns_ns_status, ITEM_TYPE_DEPENDENT, hostid_template_dns_test,
			"Status of $1", "rsm.dns.ns.status[{#NS}]", "0", "90d", "365d",
			ITEM_VALUE_TYPE_UINT64, valuemapid_rsm_service_availability, "", ZBX_FLAG_DISCOVERY_PROTOTYPE,
			"Status of Name Server: Up (1) or Down (0)."
			" The Name Server is considered to be up if all its IPs returned successful RTTs.",
			"30d", itemid_rsm_dns));
	CHECK(DBpatch_4050505_create_item(itemid_rsm_dns_rtt_tcp, ITEM_TYPE_DEPENDENT, hostid_template_dns_test,
			"RTT of $1,$2 using $3", "rsm.dns.rtt[{#NS},{#IP},tcp]", "0", "90d", "365d",
			ITEM_VALUE_TYPE_FLOAT, valuemapid_rsm_dns_rtt, "", ZBX_FLAG_DISCOVERY_PROTOTYPE,
			"The Round-Time Trip returned when testing specific IP of Name Server using TCP protocol.",
			"30d", itemid_rsm_dns));
	CHECK(DBpatch_4050505_create_item(itemid_rsm_dns_rtt_udp, ITEM_TYPE_DEPENDENT, hostid_template_dns_test,
			"RTT of $1,$2 using $3", "rsm.dns.rtt[{#NS},{#IP},udp]", "0", "90d", "365d",
			ITEM_VALUE_TYPE_FLOAT, valuemapid_rsm_dns_rtt, "", ZBX_FLAG_DISCOVERY_PROTOTYPE,
			"The Round-Time Trip returned when testing specific IP of Name Server using UDP protocol.",
			"30d", itemid_rsm_dns));
	CHECK(DBpatch_4050505_create_item(itemid_rsm_dns_nsid, ITEM_TYPE_DEPENDENT, hostid_template_dns_test,
			"NSID of $1,$2", "rsm.dns.nsid[{#NS},{#IP}]", "0", "90d", "0",
			ITEM_VALUE_TYPE_STR, 0, "", ZBX_FLAG_DISCOVERY_PROTOTYPE,
			"DNS Name Server Identifier of the target Name Server that was tested.",
			"30d", itemid_rsm_dns));
	CHECK(DBpatch_4050505_create_item(itemid_rsm_dns_mode, ITEM_TYPE_DEPENDENT, hostid_template_dns_test,
			"The mode of the Test", "rsm.dns.mode", "0", "90d", "365d",
			ITEM_VALUE_TYPE_UINT64, valuemapid_dns_test_mode, "", 0,
			"The mode (normal or critical) in which the test was performed.",
			"30d", itemid_rsm_dns));
	CHECK(DBpatch_4050505_create_item(itemid_rsm_dns_protocol, ITEM_TYPE_DEPENDENT, hostid_template_dns_test,
			"Transport protocol of the Test", "rsm.dns.protocol", "0", "90d", "365d",
			ITEM_VALUE_TYPE_UINT64, valuemapid_transport_protocol, "", 0,
			"Transport protocol (UDP or TCP) that was used during the test.",
			"30d", itemid_rsm_dns));

	CHECK(DBpatch_4050505_item_to_app(itemappid_dnssec_enabled   , applicationid_dnssec, itemid_dnssec_enabled));
	CHECK(DBpatch_4050505_item_to_app(itemappid_rsm_dns          , applicationid_dns   , itemid_rsm_dns));
	CHECK(DBpatch_4050505_item_to_app(itemappid_rsm_dns_nssok    , applicationid_dns   , itemid_rsm_dns_nssok));
	CHECK(DBpatch_4050505_item_to_app(itemappid_rsm_dns_ns_status, applicationid_dns   , itemid_rsm_dns_ns_status));
	CHECK(DBpatch_4050505_item_to_app(itemappid_rsm_dns_rtt_tcp  , applicationid_dns   , itemid_rsm_dns_rtt_tcp));
	CHECK(DBpatch_4050505_item_to_app(itemappid_rsm_dns_rtt_udp  , applicationid_dns   , itemid_rsm_dns_rtt_udp));
	CHECK(DBpatch_4050505_item_to_app(itemappid_rsm_dns_nsid     , applicationid_dns   , itemid_rsm_dns_nsid));
	CHECK(DBpatch_4050505_item_to_app(itemappid_rsm_dns_mode     , applicationid_dns   , itemid_rsm_dns_mode));
	CHECK(DBpatch_4050505_item_to_app(itemappid_rsm_dns_protocol , applicationid_dns   , itemid_rsm_dns_protocol));

	CHECK(DBpatch_4050505_item_discovery(itemdiscoveryid_rsm_dns_ns_status, itemid_rsm_dns_ns_status,
			itemid_rsm_dns_ns_discovery));
	CHECK(DBpatch_4050505_item_discovery(itemdiscoveryid_rsm_dns_rtt_tcp, itemid_rsm_dns_rtt_tcp,
			itemid_rsm_dns_nsip_discovery));
	CHECK(DBpatch_4050505_item_discovery(itemdiscoveryid_rsm_dns_rtt_udp, itemid_rsm_dns_rtt_udp,
			itemid_rsm_dns_nsip_discovery));
	CHECK(DBpatch_4050505_item_discovery(itemdiscoveryid_rsm_dns_nsid, itemid_rsm_dns_nsid,
			itemid_rsm_dns_nsip_discovery));

	CHECK(DBpatch_4050505_item_preproc(item_preprocid_rsm_dns_nssok, itemid_rsm_dns_nssok,
			"$.nssok", 0));
	CHECK(DBpatch_4050505_item_preproc(item_preprocid_rsm_dns_ns_discovery, itemid_rsm_dns_ns_discovery,
			"$.nss", 0));
	CHECK(DBpatch_4050505_item_preproc(item_preprocid_rsm_dns_nsip_discovery, itemid_rsm_dns_nsip_discovery,
			"$.nsips", 0));
	CHECK(DBpatch_4050505_item_preproc(item_preprocid_rsm_dns_ns_status, itemid_rsm_dns_ns_status,
			"$.nss[?(@.[''ns''] == ''{#NS}'')].status.first()", 1));
	CHECK(DBpatch_4050505_item_preproc(item_preprocid_rsm_dns_rtt_tcp, itemid_rsm_dns_rtt_tcp,
			"$.nsips[?(@.[''ns''] == ''{#NS}'' && @.[''ip''] == ''{#IP}'' && @.[''protocol''] == ''tcp'')].rtt.first()", 1));
	CHECK(DBpatch_4050505_item_preproc(item_preprocid_rsm_dns_rtt_udp, itemid_rsm_dns_rtt_udp,
			"$.nsips[?(@.[''ns''] == ''{#NS}'' && @.[''ip''] == ''{#IP}'' && @.[''protocol''] == ''udp'')].rtt.first()", 1));
	CHECK(DBpatch_4050505_item_preproc(item_preprocid_rsm_dns_nsid, itemid_rsm_dns_nsid,
			"$.nsips[?(@.[''ns''] == ''{#NS}'' && @.[''ip''] == ''{#IP}'')].nsid.first()", 1));
	CHECK(DBpatch_4050505_item_preproc(item_preprocid_rsm_dns_mode, itemid_rsm_dns_mode,
			"$.mode", 0));
	CHECK(DBpatch_4050505_item_preproc(item_preprocid_rsm_dns_protocol, itemid_rsm_dns_protocol,
			"$.protocol", 0));

	CHECK(DBpatch_4050505_lld_macro_path(lld_macro_pathid_rsm_dns_ns_discovery_ns,
			itemid_rsm_dns_ns_discovery, "{#NS}", "$.ns"));
	CHECK(DBpatch_4050505_lld_macro_path(lld_macro_pathid_rsm_dns_nsip_discovery_ip,
			itemid_rsm_dns_nsip_discovery, "{#IP}", "$.ip"));
	CHECK(DBpatch_4050505_lld_macro_path(lld_macro_pathid_rsm_dns_nsip_discovery_ns,
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

	ret = SUCCEED;
out:
	return ret;
}

static int	DBpatch_4050506_create_application(zbx_uint64_t *applicationid, zbx_uint64_t template_applicationid,
		zbx_uint64_t hostid, const char *name)
{
	int	ret = FAIL;

	*applicationid = DBget_maxid_num("applications", 1);

	CHECK(DBexecute("insert into applications set"
				" applicationid=" ZBX_FS_UI64 ","
				" hostid=" ZBX_FS_UI64 ","
				" name='%s',"
				" flags=0",
			*applicationid, hostid, name));

	CHECK(DBexecute("insert into application_template set"
				" application_templateid=" ZBX_FS_UI64 ","
				" applicationid=" ZBX_FS_UI64 ","
				" templateid=" ZBX_FS_UI64,
			DBget_maxid_num("application_template", 1), *applicationid, template_applicationid));

	ret = SUCCEED;
out:
	return ret;
}

static int	DBpatch_4050506_copy_preproc(zbx_uint64_t src_itemid, zbx_uint64_t dst_itemid,
		const char *replacements[][2])
{
	int		ret = FAIL;
	DB_RESULT	result;
	DB_ROW		row;

	result = DBselect("select step,type,params,error_handler,error_handler_params from item_preproc"
				" where itemid=" ZBX_FS_UI64 " order by item_preprocid", src_itemid);

	if (NULL == result)
		goto out;

	while (NULL != (row = DBfetch(result)))
	{
		CHECK(DBexecute("insert into item_preproc set item_preprocid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64 ","
					"step=%s,type=%s,params=\"%s\",error_handler=%s,error_handler_params='%s'",
				DBget_maxid_num("item_preproc", 1), dst_itemid, row[0], row[1], row[2], row[3],
				row[4]));
	}

	DBfree_result(result);
	result = NULL;

	if (NULL != replacements)
	{
		size_t	i;

		for (i = 0; NULL != replacements[i][0]; i++)
		{
			CHECK(DBexecute("update item_preproc set params=replace(params,'%s','%s')"
					" where itemid=" ZBX_FS_UI64,
					replacements[i][0], replacements[i][1], dst_itemid));
		}
	}

	ret = SUCCEED;
out:
	if (NULL != result)
		DBfree_result(result);

	return ret;
}

static int	DBpatch_4050506_copy_lld_macros(zbx_uint64_t src_itemid, zbx_uint64_t dst_itemid)
{
	int		ret = FAIL;
	DB_RESULT	result;
	DB_ROW		row;

	result = DBselect("select lld_macro,path from lld_macro_path"
				" where itemid=" ZBX_FS_UI64 " order by lld_macro_pathid", src_itemid);

	if (NULL == result)
		goto out;

	while (NULL != (row = DBfetch(result)))
	{
		CHECK(DBexecute("insert into lld_macro_path set lld_macro_pathid=" ZBX_FS_UI64 ","
					"itemid=" ZBX_FS_UI64 ",lld_macro='%s',path='%s'",
				DBget_maxid_num("lld_macro_path", 1), dst_itemid, row[0], row[1]));
	}

	ret = SUCCEED;
out:
	DBfree_result(result);

	return ret;
}

static int	DBpatch_4050506_create_item(zbx_uint64_t *new_itemid, zbx_uint64_t templateid, zbx_uint64_t hostid,
		zbx_uint64_t interfaceid, zbx_uint64_t master_itemid, zbx_uint64_t applicationid)
{
	int		ret = FAIL;
	zbx_uint64_t	itemid;

	itemid = DBget_maxid_num("items", 1);

	CHECK(DBexecute("insert into items (itemid,type,snmp_community,snmp_oid,hostid,name,key_,delay,history,"
				"trends,status,value_type,trapper_hosts,units,snmpv3_securityname,snmpv3_securitylevel,"
				"snmpv3_authpassphrase,snmpv3_privpassphrase,formula,logtimefmt,templateid,"
				"valuemapid,params,ipmi_sensor,authtype,username,password,publickey,privatekey,flags,"
				"interfaceid,port,description,inventory_link,lifetime,"
				"snmpv3_authprotocol,snmpv3_privprotocol,snmpv3_contextname,evaltype,jmx_endpoint,"
				"master_itemid,timeout,url,query_fields,posts,status_codes,"
				"follow_redirects,post_type,http_proxy,headers,retrieve_mode,request_method,"
				"output_format,ssl_cert_file,ssl_key_file,ssl_key_password,verify_peer,verify_host,"
				"allow_traps)"
			"select"
				" " ZBX_FS_UI64 ",type,snmp_community,snmp_oid," ZBX_FS_UI64 ",name,key_,delay,history,"
				"trends,status,value_type,trapper_hosts,units,snmpv3_securityname,snmpv3_securitylevel,"
				"snmpv3_authpassphrase,snmpv3_privpassphrase,formula,logtimefmt," ZBX_FS_UI64 ","
				"valuemapid,params,ipmi_sensor,authtype,username,password,publickey,privatekey,flags,"
				"nullif(" ZBX_FS_UI64 ",0),port,description,inventory_link,lifetime,"
				"snmpv3_authprotocol,snmpv3_privprotocol,snmpv3_contextname,evaltype,jmx_endpoint,"
				"nullif(" ZBX_FS_UI64 ",0),timeout,url,query_fields,posts,status_codes,"
				"follow_redirects,post_type,http_proxy,headers,retrieve_mode,request_method,"
				"output_format,ssl_cert_file,ssl_key_file,ssl_key_password,verify_peer,verify_host,"
				"allow_traps"
			" from items"
			" where itemid=" ZBX_FS_UI64,
			itemid, hostid, templateid, interfaceid, master_itemid, templateid));

	if (0 != applicationid)
	{
		CHECK(DBexecute("insert into items_applications set"
				" itemappid=" ZBX_FS_UI64 ",applicationid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64,
				DBget_maxid_num("items_applications", 1), applicationid, itemid));
	}

	CHECK_RESULT(DBpatch_4050506_copy_preproc(templateid, itemid, NULL));

	CHECK_RESULT(DBpatch_4050506_copy_lld_macros(templateid, itemid));

	if (NULL != new_itemid)
	{
		*new_itemid = itemid;
	}

	ret = SUCCEED;
out:
	return ret;
}

static int	DBpatch_4050506_convert_item(zbx_uint64_t *itemid, zbx_uint64_t hostid, const char *key,
		zbx_uint64_t master_itemid, zbx_uint64_t template_itemid, zbx_uint64_t applicationid)
{
	int	ret = FAIL;

	SELECT_VALUE_UINT64(*itemid, "select itemid from items where hostid=" ZBX_FS_UI64 " and key_='%s'", hostid, key);

	CHECK(DBexecute("update"
				" items,"
				" items as template"
			" set"
				" items.type=template.type,"
				" items.name=template.name,"
				" items.key_=template.key_,"
				" items.delay=template.delay,"
				" items.templateid=template.itemid,"
				" items.interfaceid=null,"
				" items.description=template.description,"
				" items.master_itemid=nullif(" ZBX_FS_UI64 ",0),"
				" items.request_method=template.request_method"
			" where"
				" items.itemid=" ZBX_FS_UI64 " and"
				" template.itemid=" ZBX_FS_UI64,
			master_itemid, *itemid, template_itemid));

	CHECK(DBexecute("delete from items_applications where itemid=" ZBX_FS_UI64, *itemid));

	CHECK(DBexecute("insert into items_applications set"
			" itemappid=" ZBX_FS_UI64 ",applicationid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64,
			DBget_maxid_num("items_applications", 1), applicationid, *itemid));

	CHECK_RESULT(DBpatch_4050506_copy_preproc(template_itemid, *itemid, NULL));

	ret = SUCCEED;
out:
	return ret;
}

static int	DBpatch_4050506_create_item_prototype(zbx_uint64_t *new_itemid, zbx_uint64_t templateid,
		zbx_uint64_t hostid, zbx_uint64_t interfaceid, zbx_uint64_t master_itemid, zbx_uint64_t applicationid,
		zbx_uint64_t parent_itemid)
{
	int		ret = FAIL;

	CHECK_RESULT(DBpatch_4050506_create_item(new_itemid, templateid, hostid, interfaceid, master_itemid,
			applicationid));

	CHECK(DBexecute("insert into item_discovery set"
				" itemdiscoveryid=" ZBX_FS_UI64 ","
				" itemid=" ZBX_FS_UI64 ","
				" parent_itemid=" ZBX_FS_UI64 ","
				" key_='',"
				" lastcheck=0,"
				" ts_delete=0",
			DBget_maxid_num("item_discovery", 1), *new_itemid, parent_itemid));

	ret = SUCCEED;
out:
	return ret;
}

static int	DBpatch_4050506_create_item_lld(zbx_uint64_t *new_itemid, const char *key,
		zbx_uint64_t prototype_itemid, zbx_uint64_t applicationid, const char *preproc_replacements[][2])
{
	int	ret = FAIL;

	*new_itemid = DBget_maxid_num("items", 1);

	/* non-default values - templateid=NULL, flags=ZBX_FLAG_DISCOVERY_CREATED, interfaceid=NULL */
	CHECK(DBexecute("insert into items (itemid,type,snmp_community,snmp_oid,hostid,name,key_,delay,history,trends,"
				"status,value_type,trapper_hosts,units,snmpv3_securityname,snmpv3_securitylevel,"
				"snmpv3_authpassphrase,snmpv3_privpassphrase,formula,logtimefmt,templateid,valuemapid,"
				"params,ipmi_sensor,authtype,username,password,publickey,privatekey,flags,interfaceid,"
				"port,description,inventory_link,lifetime,snmpv3_authprotocol,snmpv3_privprotocol,"
				"snmpv3_contextname,evaltype,jmx_endpoint,master_itemid,timeout,url,query_fields,posts,"
				"status_codes,follow_redirects,post_type,http_proxy,headers,retrieve_mode,"
				"request_method,output_format,ssl_cert_file,ssl_key_file,ssl_key_password,verify_peer,"
				"verify_host,allow_traps)"
			" select"
				" " ZBX_FS_UI64 ",type,snmp_community,snmp_oid,hostid,name,'%s',delay,history,trends,"
				"status,value_type,trapper_hosts,units,snmpv3_securityname,snmpv3_securitylevel,"
				"snmpv3_authpassphrase,snmpv3_privpassphrase,formula,logtimefmt,null,valuemapid,"
				"params,ipmi_sensor,authtype,username,password,publickey,privatekey,4,null,"
				"port,description,inventory_link,lifetime,snmpv3_authprotocol,snmpv3_privprotocol,"
				"snmpv3_contextname,evaltype,jmx_endpoint,master_itemid,timeout,url,query_fields,posts,"
				"status_codes,follow_redirects,post_type,http_proxy,headers,retrieve_mode,"
				"request_method,output_format,ssl_cert_file,ssl_key_file,ssl_key_password,verify_peer,"
				"verify_host,allow_traps"
			" from items"
			" where itemid=" ZBX_FS_UI64,
			*new_itemid, key, prototype_itemid));

	CHECK(DBexecute("insert into items_applications set"
			" itemappid=" ZBX_FS_UI64 ",applicationid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64,
			DBget_maxid_num("items_applications", 1), applicationid, *new_itemid));

	CHECK_RESULT(DBpatch_4050506_copy_preproc(prototype_itemid, *new_itemid, preproc_replacements));

	CHECK(DBexecute("insert into item_discovery (itemdiscoveryid,itemid,parent_itemid,key_,lastcheck,ts_delete)"
			" select " ZBX_FS_UI64 "," ZBX_FS_UI64 ",itemid,key_,0,0 from items where itemid=" ZBX_FS_UI64,
			DBget_maxid_num("item_discovery", 1), *new_itemid, prototype_itemid));

	ret = SUCCEED;
out:
	return ret;
}

static int	DBpatch_4050506_convert_item_lld(zbx_uint64_t *itemid, zbx_uint64_t hostid, const char *old_key,
		const char *new_key, zbx_uint64_t prototype_itemid, zbx_uint64_t applicationid,
		const char *preproc_replacements[][2])
{
	int	ret = FAIL;

	SELECT_VALUE_UINT64(*itemid, "select itemid from items where hostid=" ZBX_FS_UI64 " and key_='%s'", hostid, old_key);

	CHECK(DBexecute("update"
				" items,"
				" items as prototype"
			" set"
				" items.type=prototype.type,"
				" items.name=prototype.name,"
				" items.key_='%s',"
				" items.templateid=null,"
				" items.flags=4,"
				" items.description=prototype.description,"
				" items.master_itemid=prototype.master_itemid,"
				" items.request_method=prototype.request_method"
			" where"
				" items.itemid=" ZBX_FS_UI64 " and"
				" prototype.itemid=" ZBX_FS_UI64,
			new_key, *itemid, prototype_itemid));

	CHECK(DBexecute("delete from items_applications where itemid=" ZBX_FS_UI64, *itemid));

	CHECK(DBexecute("insert into items_applications set"
			" itemappid=" ZBX_FS_UI64 ",applicationid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64,
			DBget_maxid_num("items_applications", 1), applicationid, *itemid));

	CHECK_RESULT(DBpatch_4050506_copy_preproc(prototype_itemid, *itemid, preproc_replacements));

	CHECK(DBexecute("insert into item_discovery (itemdiscoveryid,itemid,parent_itemid,key_,lastcheck,ts_delete)"
			" select " ZBX_FS_UI64 "," ZBX_FS_UI64 ",itemid,key_,0,0 from items where itemid=" ZBX_FS_UI64,
			DBget_maxid_num("item_discovery", 1), *itemid, prototype_itemid));

	ret = SUCCEED;
out:
	return ret;
}

static int	DBpatch_4050506(void)
{
	int		ret = FAIL;

	DB_RESULT	result_hostid;
	DB_RESULT	result;
	DB_ROW		row_hostid;
	DB_ROW		row;

	zbx_uint64_t	groupid_tld_probe_resluts;			/* groupid of "TLD Probe results" host group */
	zbx_uint64_t	hostid_template_dns_test;			/* hostid of "Template DNS Test" template */

	zbx_uint64_t	template_applicationid_dns;			/* applicationid of "DNS" application in "Template DNS Test" template */
	zbx_uint64_t	template_applicationid_dnssec;			/* applicationid of "DNSSEC" application in "Template DNS Test" template */

	zbx_uint64_t	template_itemid_dnssec_enabled;			/* itemid of "DNSSEC enabled/disabled" item in "Template DNS Test" template */
	zbx_uint64_t	template_itemid_rsm_dns;			/* itemid of "DNS Test" item in "Template DNS Test" template */
	zbx_uint64_t	template_itemid_rsm_dns_nssok;			/* itemid of "Number of working Name Servers" item in "Template DNS Test" template */
	zbx_uint64_t	template_itemid_rsm_dns_ns_discovery;		/* itemid of "Name Servers discovery" item in "Template DNS Test" template */
	zbx_uint64_t	template_itemid_rsm_dns_nsip_discovery;		/* itemid of "NS-IP pairs discovery" item in "Template DNS Test" template */
	zbx_uint64_t	template_itemid_rsm_dns_ns_status;		/* itemid of "Status of $1" item prototype in "Template DNS Test" template */
	zbx_uint64_t	template_itemid_rsm_dns_nsid;			/* itemid of "NSID of $1,$2" item prototype in "Template DNS Test" template */
	zbx_uint64_t	template_itemid_rsm_dns_rtt_tcp;		/* itemid of "RTT of $1,$2 using $3" item prototype in "Template DNS Test" template */
	zbx_uint64_t	template_itemid_rsm_dns_rtt_udp;		/* itemid of "RTT of $1,$2 using $3" item prototype in "Template DNS Test" template */
	zbx_uint64_t	template_itemid_rsm_dns_mode;			/* itemid of "The mode of the Test" item prototype in "Template DNS Test" template */
	zbx_uint64_t	template_itemid_rsm_dns_protocol;		/* itemid of "Transport protocol of the Test" item prototype in "Template DNS Test" template */

	ONLY_SERVER();

	GET_HOST_GROUP_ID(groupid_tld_probe_resluts, "TLD Probe results");
	GET_TEMPLATE_ID(hostid_template_dns_test, "Template DNS Test");

	SELECT_VALUE_UINT64(template_applicationid_dns,
			"select applicationid from applications where hostid=" ZBX_FS_UI64 " and name='DNS'",
			hostid_template_dns_test);
	SELECT_VALUE_UINT64(template_applicationid_dnssec,
			"select applicationid from applications where hostid=" ZBX_FS_UI64 " and name='DNSSEC'",
			hostid_template_dns_test);

	GET_TEMPLATE_ITEM_ID(template_itemid_dnssec_enabled        , "Template DNS Test", "dnssec.enabled");
	GET_TEMPLATE_ITEM_ID_BY_PATTERN(template_itemid_rsm_dns    , "Template DNS Test", "rsm.dns[%]");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_dns_nssok         , "Template DNS Test", "rsm.dns.nssok");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_dns_ns_discovery  , "Template DNS Test", "rsm.dns.ns.discovery");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_dns_nsip_discovery, "Template DNS Test", "rsm.dns.nsip.discovery");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_dns_ns_status     , "Template DNS Test", "rsm.dns.ns.status[{#NS}]");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_dns_nsid          , "Template DNS Test", "rsm.dns.nsid[{#NS},{#IP}]");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_dns_rtt_tcp       , "Template DNS Test", "rsm.dns.rtt[{#NS},{#IP},tcp]");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_dns_rtt_udp       , "Template DNS Test", "rsm.dns.rtt[{#NS},{#IP},udp]");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_dns_mode          , "Template DNS Test", "rsm.dns.mode");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_dns_protocol      , "Template DNS Test", "rsm.dns.protocol");

	result_hostid = DBselect("select hostid from hosts_groups where groupid=" ZBX_FS_UI64, groupid_tld_probe_resluts);

	if (NULL == result_hostid)
		goto out;

	while (NULL != (row_hostid = DBfetch(result_hostid)))
	{
		zbx_uint64_t	hostid;					/* hostid of "<rsmhost> <probe>" host */
		zbx_uint64_t	interfaceid;				/* interfaceid for items in "<rsmhost> <probe>" host */

		zbx_uint64_t	applicationid_dns;			/* applicationid of "DNS" application */
		zbx_uint64_t	applicationid_dnssec;			/* applicationid of "DNSSEC" application */

		zbx_uint64_t	itemid_dnssec_enabled;			/* itemid of "DNSSEC enabled/disabled" item in "Template DNS Test" template */
		zbx_uint64_t	itemid_rsm_dns;				/* itemid of "DNS Test" item in "Template DNS Test" template */
		zbx_uint64_t	itemid_rsm_dns_ns_discovery;		/* itemid of "Name Servers discovery" item in "Template DNS Test" template */
		zbx_uint64_t	itemid_rsm_dns_nsip_discovery;		/* itemid of "NS-IP pairs discovery" item in "Template DNS Test" template */
		zbx_uint64_t	prototype_itemid_rsm_dns_ns_status;	/* itemid of "Status of $1" item prototype in "Template DNS Test" template */
		zbx_uint64_t	prototype_itemid_rsm_dns_nsid;		/* itemid of "NSID of $1,$2" item prototype in "Template DNS Test" template */
		zbx_uint64_t	prototype_itemid_rsm_dns_rtt_tcp;	/* itemid of "RTT of $1,$2 using $3" item prototype in "Template DNS Test" template */
		zbx_uint64_t	prototype_itemid_rsm_dns_rtt_udp;	/* itemid of "RTT of $1,$2 using $3" item prototype in "Template DNS Test" template */
		zbx_uint64_t	itemid_rsm_dns_mode;			/* itemid of "The mode of the Test" item prototype in "Template DNS Test" template */
		zbx_uint64_t	itemid_rsm_dns_protocol;		/* itemid of "Transport protocol of the Test" item prototype in "Template DNS Test" template */
		zbx_uint64_t	itemid_rsm_dns_nssok;			/* itemid of "Number of working Name Servers" item in "Template DNS Test" template */

		ZBX_STR2UINT64(hostid, row_hostid[0]);

		/* main=INTERFACE_PRIMARY, type=INTERFACE_TYPE_AGENT */
		SELECT_VALUE_UINT64(interfaceid, "select interfaceid from interface"
				" where hostid=" ZBX_FS_UI64 " and main=1 and type=1", hostid);

		/* link "Template DNS Test" template to the host */
		CHECK(DBexecute("insert into hosts_templates set"
				" hosttemplateid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ",templateid=" ZBX_FS_UI64,
				DBget_maxid_num("hosts_templates", 1), hostid, hostid_template_dns_test));

		/* create applications */
		CHECK_RESULT(DBpatch_4050506_create_application(&applicationid_dns   , template_applicationid_dns   , hostid, "DNS"));
		CHECK_RESULT(DBpatch_4050506_create_application(&applicationid_dnssec, template_applicationid_dnssec, hostid, "DNSSEC"));

		/* update dnssec.enabled item */
		CHECK_RESULT(DBpatch_4050506_convert_item(&itemid_dnssec_enabled, hostid, "dnssec.enabled",
				0, template_itemid_dnssec_enabled, applicationid_dnssec));

		/* create "DNS Test" (rsm.dns[...]) master item */
		CHECK_RESULT(DBpatch_4050506_create_item(&itemid_rsm_dns,
				template_itemid_rsm_dns, hostid, interfaceid, 0, applicationid_dns));

		/* create "Name Servers discovery" (rsm.dns.ns.discovery) discovery rule */
		CHECK_RESULT(DBpatch_4050506_create_item(&itemid_rsm_dns_ns_discovery,
				template_itemid_rsm_dns_ns_discovery, hostid, 0, itemid_rsm_dns, 0));

		/* create "NS-IP pairs discovery" (rsm.dns.nsip.discovery) discovery rule */
		CHECK_RESULT(DBpatch_4050506_create_item(&itemid_rsm_dns_nsip_discovery,
				template_itemid_rsm_dns_nsip_discovery, hostid, 0, itemid_rsm_dns, 0));

		/* create "Status of {#NS}" (rsm.dns.ns.status[{#NS}]) item prototype */
		CHECK_RESULT(DBpatch_4050506_create_item_prototype(&prototype_itemid_rsm_dns_ns_status,
				template_itemid_rsm_dns_ns_status, hostid, 0, itemid_rsm_dns, applicationid_dns,
				itemid_rsm_dns_ns_discovery));

		/* create "NSID of {#NS},{#IP}" (rsm.dns.nsid[{#NS},{#IP}]) item prototype */
		CHECK_RESULT(DBpatch_4050506_create_item_prototype(&prototype_itemid_rsm_dns_nsid,
				template_itemid_rsm_dns_nsid, hostid, 0, itemid_rsm_dns, applicationid_dns,
				itemid_rsm_dns_nsip_discovery));

		/* create "RTT of {#NS},{#IP} using tcp" (rsm.dns.rtt[{#NS},{#IP},tcp]) item prototype */
		CHECK_RESULT(DBpatch_4050506_create_item_prototype(&prototype_itemid_rsm_dns_rtt_tcp,
				template_itemid_rsm_dns_rtt_tcp, hostid, 0, itemid_rsm_dns, applicationid_dns,
				itemid_rsm_dns_nsip_discovery));

		/* create "RTT of {#NS},{#IP} using udp" (rsm.dns.rtt[{#NS},{#IP},udp]) item prototype */
		CHECK_RESULT(DBpatch_4050506_create_item_prototype(&prototype_itemid_rsm_dns_rtt_udp,
				template_itemid_rsm_dns_rtt_udp, hostid, 0, itemid_rsm_dns, applicationid_dns,
				itemid_rsm_dns_nsip_discovery));

		/* create "The mode of the Test" (rsm.dns.mode) item */
		CHECK_RESULT(DBpatch_4050506_create_item(&itemid_rsm_dns_mode,
				template_itemid_rsm_dns_mode, hostid, 0, itemid_rsm_dns, applicationid_dns));

		/* create "Transport protocol of the Test" rsm.dns.protocol */
		CHECK_RESULT(DBpatch_4050506_create_item(&itemid_rsm_dns_protocol,
				template_itemid_rsm_dns_protocol, hostid, 0, itemid_rsm_dns, applicationid_dns));

		/* delete rsm.dns.tcp[{$RSM.TLD}] item */
		CHECK(DBexecute("delete from items where key_='rsm.dns.tcp[{$RSM.TLD}]' and hostid=" ZBX_FS_UI64, hostid));

		/* convert rsm.dns.udp[{$RSM.TLD}] item into rsm.dns.nssok */
		CHECK_RESULT(DBpatch_4050506_convert_item(&itemid_rsm_dns_nssok, hostid, "rsm.dns.udp[{$RSM.TLD}]",
				itemid_rsm_dns, template_itemid_rsm_dns_nssok, applicationid_dns));

		/* update <ns> items */

		result = DBselect("select distinct substring_index(substring_index(key_,',',-2),',',1) from items"
					" where hostid=" ZBX_FS_UI64 " and key_ like 'rsm.dns.udp.rtt[%%]'", hostid);

		if (NULL == result)
			goto out;

		while (NULL != (row = DBfetch(result)))
		{
			char		key[MAX_STRING_LEN];
			zbx_uint64_t	itemid;

			const char	*preproc_replacements[][2] = {
				{"{#NS}", row[0]},
				{NULL}
			};

			/* create "Status of <ns>" (rsm.dns.ns.status[<ns>]) items */
			zbx_snprintf(key, sizeof(key), "rsm.dns.ns.status[%s]", row[0]);
			CHECK_RESULT(DBpatch_4050506_create_item_lld(&itemid, key, prototype_itemid_rsm_dns_ns_status,
					applicationid_dns, preproc_replacements));
		}

		DBfree_result(result);
		result = NULL;

		/* update <ns>,<ip> items */

		result = DBselect("select distinct"
					" substring_index(substring_index(key_,',',-2),',',1),"
					" substring_index(substring_index(key_,',',-1),']',1)"
				" from items where hostid=" ZBX_FS_UI64 " and key_ like 'rsm.dns.udp.rtt[%%]'",
				hostid);

		if (NULL == result)
			goto out;

		while (NULL != (row = DBfetch(result)))
		{
			char		old_key[MAX_STRING_LEN];
			char		new_key[MAX_STRING_LEN];
			zbx_uint64_t	itemid;

			const char	*preproc_replacements[][2] = {
				{"{#NS}", row[0]},
				{"{#IP}", row[1]},
				{NULL}
			};

			/* create "NSID of <ns>,<ip>" (rsm.dns.nsid[<ns>,<ip>]) item */
			zbx_snprintf(new_key, sizeof(new_key), "rsm.dns.nsid[%s,%s]", row[0], row[1]);
			CHECK_RESULT(DBpatch_4050506_create_item_lld(&itemid, new_key,
					prototype_itemid_rsm_dns_nsid, applicationid_dns, preproc_replacements));

			/* convert "RTT of <ns>,<ip> using tcp" (rsm.dns.rtt[<ns>,<ip>,tcp]) item */
			zbx_snprintf(old_key, sizeof(old_key), "rsm.dns.tcp.rtt[{$RSM.TLD},%s,%s]", row[0], row[1]);
			zbx_snprintf(new_key, sizeof(new_key), "rsm.dns.rtt[%s,%s,tcp]", row[0], row[1]);
			CHECK_RESULT(DBpatch_4050506_convert_item_lld(&itemid, hostid, old_key, new_key,
					prototype_itemid_rsm_dns_rtt_tcp, applicationid_dns, preproc_replacements));

			/* convert "RTT of <ns>,<ip> using udp" (rsm.dns.rtt[<ns>,<ip>,udp]) item */
			zbx_snprintf(old_key, sizeof(old_key), "rsm.dns.udp.rtt[{$RSM.TLD},%s,%s]", row[0], row[1]);
			zbx_snprintf(new_key, sizeof(new_key), "rsm.dns.rtt[%s,%s,udp]", row[0], row[1]);
			CHECK_RESULT(DBpatch_4050506_convert_item_lld(&itemid, hostid, old_key, new_key,
					prototype_itemid_rsm_dns_rtt_udp, applicationid_dns, preproc_replacements));
		}

		DBfree_result(result);
		result = NULL;

		/* remove old applications */
		CHECK(DBexecute("delete from applications where"
					" hostid=" ZBX_FS_UI64 " and"
					" name in ('DNS (TCP)', 'DNS (UDP)', 'DNS RTT (TCP)', 'DNS RTT (UDP)')",
				hostid));
	}

	ret = SUCCEED;
out:
	DBfree_result(result);
	DBfree_result(result_hostid);

	return ret;
}

static int	DBpatch_4050507(void)
{
	int	ret;

	ONLY_SERVER();

	ret = DBexecute("update items set status=%d where key_ like 'zabbix[process,db watchdog,%%' "
			"and type=%d", ITEM_STATUS_DISABLED, ITEM_TYPE_INTERNAL);

	if (ZBX_DB_OK <= ret)
		zabbix_log(LOG_LEVEL_WARNING, "disabled %d db watchdog items", ret);

	return SUCCEED;
}

static int    DBpatch_4050508(void)
{
	int		ret = FAIL;
	const char	*command = "/opt/zabbix/scripts/tlds-notification.pl --send-to \\'zabbix alert\\' --event-id \\'{EVENT.RECOVERY.ID}\\' &";

	ONLY_SERVER();

	/* enable "TLDs" action */

	CHECK(DBexecute("update actions set status=0 where actionid=130"));

	/* add recovery operation */

	CHECK(DBexecute("insert into operations set operationid=131,actionid=130,operationtype=1,esc_period='0',"
			"esc_step_from=1,esc_step_to=1,evaltype=0,recovery=1"));
	CHECK(DBexecute("insert into opcommand set operationid=131,type=0,scriptid=NULL,execute_on=1,port='',"
			"authtype=0,username='',password='',publickey='',privatekey='',command='%s'", command));
	CHECK(DBexecute("insert into opcommand_hst set opcommand_hstid=131,operationid=131,hostid=NULL"));

	ret = SUCCEED;
out:
	return ret;
}

#endif

DBPATCH_START(4050)

/* version, duplicates flag, mandatory flag */

DBPATCH_ADD(4050001, 0, 1)
DBPATCH_ADD(4050002, 0, 1)
DBPATCH_ADD(4050003, 0, 1)
DBPATCH_ADD(4050004, 0, 1)
DBPATCH_ADD(4050005, 0, 1)
DBPATCH_ADD(4050006, 0, 1)
DBPATCH_ADD(4050007, 0, 1)
DBPATCH_ADD(4050010, 0, 1)
DBPATCH_ADD(4050011, 0, 1)
DBPATCH_ADD(4050012, 0, 1)
DBPATCH_ADD(4050500, 0, 1)	/* RSM FY20 */
DBPATCH_ADD(4050501, 0, 1)	/* set delay as macro for rsm.dns.*, rsm.rdds*, rsm.rdap* and rsm.epp* items items */
DBPATCH_ADD(4050502, 0, 0)	/* set global macro descriptions */
DBPATCH_ADD(4050503, 0, 0)	/* set host macro descriptions */
DBPATCH_ADD(4050504, 0, 0)	/* add "DNS test mode" and "Transport protocol" value mappings */
DBPATCH_ADD(4050505, 0, 0)	/* add "Template DNS Test" template */
DBPATCH_ADD(4050506, 0, 0)	/* convert hosts to use "Template DNS Test" template */
DBPATCH_ADD(4050507, 0, 0)	/* disable "db watchdog" internal items */
DBPATCH_ADD(4050508, 0, 0)	/* upgrade "TLDs" action (upgrade process to Zabbix 4.x failed to upgrade it) */

DBPATCH_END()
