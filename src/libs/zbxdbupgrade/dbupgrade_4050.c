/*
** Zabbix
** Copyright (C) 2001-2021 Zabbix SIA
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
#include "zbxalgo.h"
#include "../zbxalgo/vectorimpl.h"

/*
 * Some common helpers that can be used as one-liners in patches to avoid copy-pasting.
 *
 * Be careful when implementing new helpers - they have to be generic.
 * If some code is needed only 1-2 times, it doesn't fit here.
 * If some code depends on stuff that is likely to change, it doesn't fit here.
 *
 * If more specific helper is needed, it must be implemented close to the patch that needs it. Specific
 * helpers can be implemented either as functions right before DBpatch_4050xxx(), or as macros inside
 * the DBpatch_4050xxx(). If they're implemented as macros, don't forget to #undef them.
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
#define CHECK(CODE)													\
															\
do															\
{															\
	int	__result = (CODE);											\
	if (SUCCEED != __result)											\
	{														\
		zabbix_log(LOG_LEVEL_CRIT, "%s() on line %d: got unexpected result", __func__, __LINE__);		\
		goto out;												\
	}														\
}															\
while (0)

/* checks result of DBexecute() */
#define DB_EXEC(...)													\
															\
do															\
{															\
	int	__result = DBexecute(__VA_ARGS__);									\
	if (ZBX_DB_OK > __result)											\
	{														\
		zabbix_log(LOG_LEVEL_CRIT, "%s() on line %d: got unexpected result", __func__, __LINE__);		\
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
		zabbix_log(LOG_LEVEL_CRIT, "%s() on line %d: query failed", __func__, __LINE__);			\
		goto out;												\
	}														\
															\
	__row = DBfetch(__result);											\
															\
	/* check if there's at least one row in the resultset */							\
	if (NULL == __row)												\
	{														\
		zabbix_log(LOG_LEVEL_CRIT, "%s() on line %d: query did not return any rows", __func__, __LINE__);	\
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
		zabbix_log(LOG_LEVEL_CRIT, "%s() on line %d: query returned more than one row", __func__, __LINE__);	\
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

/* gets applicationid of the template's application, host.status=3 = HOST_STATUS_TEMPLATE */
#define GET_TEMPLATE_APPLICATION_ID(applicationid, template_host, application_name)					\
		SELECT_VALUE_UINT64(											\
				applicationid,										\
				"select"										\
					" applications.applicationid"							\
				" from"											\
					" applications"									\
					" left join hosts on hosts.hostid=applications.hostid"				\
				" where"										\
					" hosts.host='%s' and"								\
					" hosts.status=3 and"								\
					" applications.name='%s'",							\
				template_host, application_name)							\

/* gets applicationid of the host's application */
#define GET_HOST_APPLICATION_ID(applicationid, host_id, application_name)						\
		SELECT_VALUE_UINT64(											\
				applicationid,										\
				"select"										\
					" applicationid"								\
				" from"											\
					" applications"									\
				" where"										\
					" hostid=" ZBX_FS_UI64 " and"							\
					" name='%s'",									\
				host_id, application_name)								\

/* gets itemid of the template's item, host.status=3 = HOST_STATUS_TEMPLATE */
#define GET_TEMPLATE_ITEM_ID(itemid, template_host, item_key)								\
		SELECT_VALUE_UINT64(											\
				itemid,											\
				"select"										\
					" items.itemid"									\
				" from"											\
					" items"									\
					" left join hosts on hosts.hostid=items.hostid"					\
				" where"										\
					" hosts.host='%s' and"								\
					" hosts.status=3 and"								\
					" items.key_='%s'",								\
				template_host, item_key)

/* gets itemid of the template's item, host.status=3 = HOST_STATUS_TEMPLATE */
#define GET_TEMPLATE_ITEM_ID_BY_PATTERN(itemid, template_host, item_key_pattern)					\
		SELECT_VALUE_UINT64(											\
				itemid,											\
				"select"										\
					" items.itemid"									\
				" from"											\
					" items"									\
					" left join hosts on hosts.hostid=items.hostid"					\
				" where"										\
					" hosts.host='%s' and"								\
					" hosts.status=3 and"								\
					" items.key_ like '%s'",							\
				template_host, item_key_pattern)

/* gets itemid of the host's item */
#define GET_HOST_ITEM_ID(itemid, host_id, item_key)									\
		SELECT_VALUE_UINT64(											\
				itemid,											\
				"select"										\
					" itemid"									\
				" from"											\
					" items"									\
				" where"										\
					" hostid=" ZBX_FS_UI64 " and"							\
					" key_='%s'",									\
				host_id, item_key)

/* gets itemid of the host's item */
#define GET_HOST_ITEM_ID_BY_PATTERN(itemid, host_id, item_key_pattern)							\
		SELECT_VALUE_UINT64(											\
				itemid,											\
				"select"										\
					" itemid"									\
				" from"											\
					" items"									\
				" where"										\
					" hostid=" ZBX_FS_UI64 " and"							\
					" key_ like '%s'",								\
				host_id, item_key_pattern)

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

static int	DBpatch_4050000(void)
{
	int	ret = FAIL;

	DB_EXEC("alter table dbversion add column mandatory_rsm int(11) not null default '0' after mandatory");
	DB_EXEC("alter table dbversion add column optional_rsm  int(11) not null default '0' after optional");

	ret = SUCCEED;
out:
	return ret;
}

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

/* 4050012, 1 - RSM FY20 */
static int	DBpatch_4050012_1(void)
{
	/* this patch begins RSM FY20 upgrade sequence and has been intentionally left blank */

	return SUCCEED;
}

/* 4050012, 2 - set delay as macro for rsm.dns.*, rsm.rdds*, rsm.rdap* and rsm.epp* items items */
static int	DBpatch_4050012_2(void)
{
	int	ret = FAIL;

	ONLY_SERVER();

	/* 3 = ITEM_TYPE_SIMPLE */

	DB_EXEC("update items set delay='{$RSM.DNS.UDP.DELAY}' where key_ like 'rsm.dns.udp[%%' and type=3");
	DB_EXEC("update items set delay='{$RSM.DNS.TCP.DELAY}' where key_ like 'rsm.dns.tcp[%%' and type=3");
	DB_EXEC("update items set delay='{$RSM.RDDS.DELAY}' where key_ like 'rsm.rdds[%%' and type=3");
	DB_EXEC("update items set delay='{$RSM.RDAP.DELAY}' where key_ like 'rdap[%%' and type=3");
	DB_EXEC("update items set delay='{$RSM.EPP.DELAY}' where key_ like 'rsm.epp[%%' and type=3");

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

/* 4050012, 3 - set global macro descriptions */
static int	DBpatch_4050012_3(void)
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

/* 4050012, 4 - set host macro descriptions */
static int	DBpatch_4050012_4(void)
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
			"{$RSM.RDDS43.TEST.DOMAIN}",
			"Domain name to use when querying RDDS43 server, e.g. \"whois.example\""
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

/* 4050012, 5 - disable "db watchdog" internal items */
static int	DBpatch_4050012_5(void)
{
	int	ret;

	ONLY_SERVER();

	ret = DBexecute("update items set status=%d where key_ like 'zabbix[process,db watchdog,%%' "
			"and type=%d", ITEM_STATUS_DISABLED, ITEM_TYPE_INTERNAL);

	if (ZBX_DB_OK <= ret)
		zabbix_log(LOG_LEVEL_WARNING, "disabled %d db watchdog items", ret);

	return SUCCEED;
}

/* 4050012, 6 - upgrade "TLDs" action (upgrade process to Zabbix 4.x failed to upgrade it) */
static int	DBpatch_4050012_6(void)
{
	int		ret = FAIL;
	const char	*command = "/opt/zabbix/scripts/tlds-notification.pl --send-to \\'zabbix alert\\' --event-id \\'{EVENT.RECOVERY.ID}\\' &";

	ONLY_SERVER();

	/* enable "TLDs" action */
	DB_EXEC("update actions set status=0 where actionid=130");

	/* add recovery operation */
	DB_EXEC("insert into operations set operationid=131,actionid=130,operationtype=1,esc_period='0',"
			"esc_step_from=1,esc_step_to=1,evaltype=0,recovery=1");
	DB_EXEC("insert into opcommand set operationid=131,type=0,scriptid=NULL,execute_on=1,port='',"
			"authtype=0,username='',password='',publickey='',privatekey='',command='%s'", command);
	DB_EXEC("insert into opcommand_hst set opcommand_hstid=131,operationid=131,hostid=NULL");

	ret = SUCCEED;
out:
	return ret;
}

/* 4050012, 7 - add "DNS test mode" and "Transport protocol" value mappings */
static int	DBpatch_4050012_7(void)
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

	/* It was decided to setup monitoring from scratch rather than upgrade all the configuration, */
	/* therefore this patch may be obsolete and there's no guarantee that it works correctly.     */
	goto out;

	ONLY_SERVER();

	valuemapid_next                  = DBget_maxid_num("valuemaps", 2);
	valuemapid_dns_test_mode         = valuemapid_next++;
	valuemapid_transport_protocol    = valuemapid_next++;

	mappingid_next                   = DBget_maxid_num("mappings", 4);
	mappingid_dns_test_mode_normal   = mappingid_next++;
	mappingid_dns_test_mode_critical = mappingid_next++;
	mappingid_transport_protocol_udp = mappingid_next++;
	mappingid_transport_protocol_tcp = mappingid_next++;

#define SQL	"insert into valuemaps set valuemapid=" ZBX_FS_UI64 ",name='%s'"
	DB_EXEC(SQL, valuemapid_dns_test_mode, "DNS test mode");
	DB_EXEC(SQL, valuemapid_transport_protocol, "Transport protocol");
#undef SQL

#define SQL	"insert into mappings set mappingid=" ZBX_FS_UI64 ",valuemapid=" ZBX_FS_UI64 ",value='%s',newvalue='%s'"
	DB_EXEC(SQL, mappingid_dns_test_mode_normal, valuemapid_dns_test_mode, "0", "Normal");
	DB_EXEC(SQL, mappingid_dns_test_mode_critical, valuemapid_dns_test_mode, "1", "Critical");
	DB_EXEC(SQL, mappingid_transport_protocol_udp, valuemapid_transport_protocol, "0", "UDP");
	DB_EXEC(SQL, mappingid_transport_protocol_tcp, valuemapid_transport_protocol, "1", "TCP");
#undef SQL

	ret = SUCCEED;
out:
	return ret;
}

/* 4050012, 8 - add DNS test related global macros */
static int	DBpatch_4050012_8(void)
{
	int	ret = FAIL;

	zbx_uint64_t	globalmacroid_next;
	zbx_uint64_t	globalmacroid_rsm_dns_test_tcp_ratio;	/* globalmacroid of "{$RSM.DNS.TEST.TCP.RATIO}" global macro */
	zbx_uint64_t	globalmacroid_rsm_dns_test_recover;	/* globalmacroid of "{$RSM.DNS.TEST.RECOVER}" global macro */

	/* It was decided to setup monitoring from scratch rather than upgrade all the configuration, */
	/* therefore this patch may be obsolete and there's no guarantee that it works correctly.     */
	goto out;

	ONLY_SERVER();

	globalmacroid_next                   = DBget_maxid_num("globalmacro", 2);
	globalmacroid_rsm_dns_test_tcp_ratio = globalmacroid_next++;
	globalmacroid_rsm_dns_test_recover   = globalmacroid_next++;

#define SQL	"insert into globalmacro set globalmacroid=" ZBX_FS_UI64 ",macro='%s',value='%s',description='%s'"
	DB_EXEC(SQL, globalmacroid_rsm_dns_test_tcp_ratio, "{$RSM.DNS.TEST.TCP.RATIO}", "10",
		"The ratio (calculated against current time) of using TCP protocol instead of UDP when in normal mode of a DNS Test.");
	DB_EXEC(SQL, globalmacroid_rsm_dns_test_recover, "{$RSM.DNS.TEST.RECOVER}", "3",
		"Number of subsequently successful DNS Test results to switch from critical to normal mode.");
#undef SQL

	ret = SUCCEED;
out:
	return ret;
}

/* 4050012, 9 - add "Template Config History" template */
static int	DBpatch_4050012_9(void)
{
	int		ret = FAIL;

	zbx_uint64_t	groupid_templates;		/* groupid of "Templates" host group */

	zbx_uint64_t	hostid;				/* hostid of "Template Config History" template */

	zbx_uint64_t	applicationid_next;
	zbx_uint64_t	applicationid_dns;		/* applicationid of "DNS" application */
	zbx_uint64_t	applicationid_dnssec;		/* applicationid of "DNSSEC" application */
	zbx_uint64_t	applicationid_rdap;		/* applicationid of "RDAP" application */
	zbx_uint64_t	applicationid_rdds;		/* applicationid of "RDDS" application */

	zbx_uint64_t	itemid_next;
	zbx_uint64_t	itemid_dns_tcp_enabled;		/* itemid of "dns.tcp.enabled" item */
	zbx_uint64_t	itemid_dns_udp_enabled;		/* itemid of "dns.udp.enabled" item */
	zbx_uint64_t	itemid_dnssec_enabled;		/* itemid of "dnssec.enabled" item */
	zbx_uint64_t	itemid_rdap_enabled;		/* itemid of "rdap.enabled" item */
	zbx_uint64_t	itemid_rdds_enabled;		/* itemid of "rdds.enabled" item */

	/* It was decided to setup monitoring from scratch rather than upgrade all the configuration, */
	/* therefore this patch may be obsolete and there's no guarantee that it works correctly.     */
	goto out;

	ONLY_SERVER();

	GET_HOST_GROUP_ID(groupid_templates, "Templates");

	hostid = DBget_maxid_num("hosts", 1);

	applicationid_next   = DBget_maxid_num("applications", 4);
	applicationid_dns    = applicationid_next++;
	applicationid_dnssec = applicationid_next++;
	applicationid_rdap   = applicationid_next++;
	applicationid_rdds   = applicationid_next++;

	itemid_next            = DBget_maxid_num("items", 5);
	itemid_dns_tcp_enabled = itemid_next++;
	itemid_dns_udp_enabled = itemid_next++;
	itemid_dnssec_enabled  = itemid_next++;
	itemid_rdap_enabled    = itemid_next++;
	itemid_rdds_enabled    = itemid_next++;

	/* status 3 = HOST_STATUS_TEMPLATE */
	DB_EXEC("insert into hosts set"
			" hostid=" ZBX_FS_UI64 ",created=0,proxy_hostid=NULL,host='%s',status=%d,disable_until=0,"
			"error='',available=0,errors_from=0,lastaccess=0,ipmi_authtype=-1,ipmi_privilege=2,"
			"ipmi_username='',ipmi_password='',ipmi_disable_until=0,ipmi_available=0,snmp_disable_until=0,"
			"snmp_available=0,maintenanceid=NULL,maintenance_status=0,maintenance_type=0,"
			"maintenance_from=0,ipmi_errors_from=0,snmp_errors_from=0,ipmi_error='',snmp_error='',"
			"jmx_disable_until=0,jmx_available=0,jmx_errors_from=0,jmx_error='',name='%s',info_1='',"
			"info_2='',flags=0,templateid=NULL,description='',tls_connect=1,tls_accept=1,tls_issuer='',"
			"tls_subject='',tls_psk_identity='',tls_psk='',proxy_address='',auto_compress=1",
		hostid, "Template Config History", 3, "Template Config History");

	DB_EXEC("insert into hosts_groups set hostgroupid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ",groupid=" ZBX_FS_UI64,
		DBget_maxid_num("hosts_groups", 1), hostid, groupid_templates);

#define SQL	"insert into applications set applicationid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ",name='%s',flags=0"
	DB_EXEC(SQL, applicationid_dns   , hostid, "DNS");
	DB_EXEC(SQL, applicationid_dnssec, hostid, "DNSSEC");
	DB_EXEC(SQL, applicationid_rdap  , hostid, "RDAP");
	DB_EXEC(SQL, applicationid_rdds  , hostid, "RDDS");
#undef SQL

#define SQL	"insert into items set itemid=" ZBX_FS_UI64 ",type=%d,snmp_community='',snmp_oid='',"			\
		"hostid=" ZBX_FS_UI64 ",name='%s',key_='%s',delay='%s',history='90d',trends='365d',status=0,"		\
		"value_type=%d,trapper_hosts='',units='',snmpv3_securityname='',snmpv3_securitylevel=0,"		\
		"snmpv3_authpassphrase='',snmpv3_privpassphrase='',formula='',logtimefmt='',templateid=NULL,"		\
		"valuemapid=NULL,params='%s',ipmi_sensor='',authtype=0,username='',password='',publickey='',"		\
		"privatekey='',flags=0,interfaceid=NULL,port='',description='%s',inventory_link=0,lifetime='30d',"	\
		"snmpv3_authprotocol=0,snmpv3_privprotocol=0,snmpv3_contextname='',evaltype=0,jmx_endpoint='',"		\
		"master_itemid=NULL,timeout='3s',url='',query_fields='',posts='',status_codes='200',"			\
		"follow_redirects=1,post_type=0,http_proxy='',headers='',retrieve_mode=0,request_method=0,"		\
		"output_format=0,ssl_cert_file='',ssl_key_file='',ssl_key_password='',verify_peer=0,verify_host=0,"	\
		"allow_traps=0"
	/* type 15 = ITEM_TYPE_CALCULATED */
	/* value_type 3 = ITEM_VALUE_TYPE_UINT64 */
	DB_EXEC(SQL, itemid_dns_tcp_enabled, 15, hostid, "DNS TCP enabled/disabled", "dns.tcp.enabled", "1m",
		3, "{$RSM.TLD.DNS.TCP.ENABLED}", "History of DNS TCP being enabled or disabled.");
	DB_EXEC(SQL, itemid_dns_udp_enabled, 15, hostid, "DNS UDP enabled/disabled", "dns.udp.enabled", "1m",
		3, "{$RSM.TLD.DNS.UDP.ENABLED}", "History of DNS UDP being enabled or disabled.");
	DB_EXEC(SQL, itemid_dnssec_enabled , 15, hostid, "DNSSEC enabled/disabled" , "dnssec.enabled" , "1m",
		3, "{$RSM.TLD.DNSSEC.ENABLED}" , "History of DNSSEC being enabled or disabled.");
	DB_EXEC(SQL, itemid_rdap_enabled   , 15, hostid, "RDAP enabled/disabled"   , "rdap.enabled"   , "1m",
		3, "{$RDAP.TLD.ENABLED}"       , "History of RDAP being enabled or disabled.");
	DB_EXEC(SQL, itemid_rdds_enabled   , 15, hostid, "RDDS enabled/disabled"   , "rdds.enabled"   , "1m",
		3, "{$RSM.TLD.RDDS.ENABLED}"   , "History of RDDS being enabled or disabled.");
#undef SQL

#define SQL	"insert into items_applications set itemappid=" ZBX_FS_UI64 ",applicationid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_dns   , itemid_dns_tcp_enabled);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_dns   , itemid_dns_udp_enabled);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_dnssec, itemid_dnssec_enabled);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_rdap  , itemid_rdap_enabled);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_rdds  , itemid_rdds_enabled);
#undef SQL

	ret = SUCCEED;
out:
	return ret;
}

/* 4050012, 10 - add "Template DNS Test" template */
static int	DBpatch_4050012_10(void)
{
	int		ret = FAIL;

	zbx_uint64_t	groupid_templates;				/* groupid of "Templates" host group */

	zbx_uint64_t	valuemapid_service_state;			/* valuemapid of "Service state" */
	zbx_uint64_t	valuemapid_rsm_service_availability;		/* valuemapid of "RSM Service Availability" */
	zbx_uint64_t	valuemapid_dns_test_mode;			/* valuemapid of "DNS test mode" */
	zbx_uint64_t	valuemapid_transport_protocol;			/* valuemapid of "Transport protocol" */
	zbx_uint64_t	valuemapid_rsm_dns_rtt;				/* valuemapid of "RSM DNS rtt" */

	zbx_uint64_t	hostid;						/* hostid of "Template DNS Test" template */

	zbx_uint64_t	applicationid_next;
	zbx_uint64_t	applicationid_dns;				/* applicationid of "DNS" application in "Template DNS Test" template */
	zbx_uint64_t	applicationid_dnssec;				/* applicationid of "DNSSEC" application in "Template DNS Test" template */

	zbx_uint64_t	itemid_next;
	zbx_uint64_t	itemid_dnssec_enabled;				/* itemid of "DNSSEC enabled/disabled" item in "Template DNS Test" template */
	zbx_uint64_t	itemid_rsm_dns;					/* itemid of "DNS Test" item in "Template DNS Test" template */
	zbx_uint64_t	itemid_rsm_dns_mode;				/* itemid of "The mode of the Test" item prototype in "Template DNS Test" template */
	zbx_uint64_t	itemid_rsm_dns_nssok;				/* itemid of "Number of working Name Servers" item in "Template DNS Test" template */
	zbx_uint64_t	itemid_rsm_dns_protocol;			/* itemid of "Transport protocol of the Test" item prototype in "Template DNS Test" template */
	zbx_uint64_t	itemid_rsm_dns_status;				/* itemid of "Status of a DNS Test" item in "Template DNS Test" template */
	zbx_uint64_t	itemid_rsm_dns_ns_discovery;			/* itemid of "Name Servers discovery" item in "Template DNS Test" template */
	zbx_uint64_t	itemid_rsm_dns_nsip_discovery;			/* itemid of "NS-IP pairs discovery" item in "Template DNS Test" template */
	zbx_uint64_t	itemid_rsm_dns_ns_status;			/* itemid of "Status of $1" item prototype in "Template DNS Test" template */
	zbx_uint64_t	itemid_rsm_dns_nsid;				/* itemid of "NSID of $1,$2" item prototype in "Template DNS Test" template */
	zbx_uint64_t	itemid_rsm_dns_rtt_tcp;				/* itemid of "RTT of $1,$2 using $3" item prototype in "Template DNS Test" template */
	zbx_uint64_t	itemid_rsm_dns_rtt_udp;				/* itemid of "RTT of $1,$2 using $3" item prototype in "Template DNS Test" template */

	/* It was decided to setup monitoring from scratch rather than upgrade all the configuration, */
	/* therefore this patch may be obsolete and there's no guarantee that it works correctly.     */
	goto out;

	ONLY_SERVER();

	GET_HOST_GROUP_ID(groupid_templates, "Templates");

	GET_VALUE_MAP_ID(valuemapid_service_state, "Service state");
	GET_VALUE_MAP_ID(valuemapid_rsm_service_availability, "RSM Service Availability");
	GET_VALUE_MAP_ID(valuemapid_dns_test_mode, "DNS test mode");
	GET_VALUE_MAP_ID(valuemapid_transport_protocol, "Transport protocol");
	GET_VALUE_MAP_ID(valuemapid_rsm_dns_rtt, "RSM DNS rtt");

	hostid = DBget_maxid_num("hosts", 1);

	applicationid_next   = DBget_maxid_num("applications", 2);
	applicationid_dns    = applicationid_next++;
	applicationid_dnssec = applicationid_next++;

	itemid_next                   = DBget_maxid_num("items", 12);
	itemid_dnssec_enabled         = itemid_next++;
	itemid_rsm_dns                = itemid_next++;
	itemid_rsm_dns_mode           = itemid_next++;
	itemid_rsm_dns_nssok          = itemid_next++;
	itemid_rsm_dns_protocol       = itemid_next++;
	itemid_rsm_dns_status         = itemid_next++;
	itemid_rsm_dns_ns_discovery   = itemid_next++;
	itemid_rsm_dns_nsip_discovery = itemid_next++;
	itemid_rsm_dns_ns_status      = itemid_next++;
	itemid_rsm_dns_nsid           = itemid_next++;
	itemid_rsm_dns_rtt_tcp        = itemid_next++;
	itemid_rsm_dns_rtt_udp        = itemid_next++;

	/* status 3 = HOST_STATUS_TEMPLATE */
	DB_EXEC("insert into hosts set hostid=" ZBX_FS_UI64 ",created=0,proxy_hostid=NULL,host='%s',status=%d,"
			"disable_until=0,error='',available=0,errors_from=0,lastaccess=0,ipmi_authtype=-1,"
			"ipmi_privilege=2,ipmi_username='',ipmi_password='',ipmi_disable_until=0,ipmi_available=0,"
			"snmp_disable_until=0,snmp_available=0,maintenanceid=NULL,maintenance_status=0,"
			"maintenance_type=0,maintenance_from=0,ipmi_errors_from=0,snmp_errors_from=0,ipmi_error='',"
			"snmp_error='',jmx_disable_until=0,jmx_available=0,jmx_errors_from=0,jmx_error='',name='%s',"
			"info_1='',info_2='',flags=0,templateid=NULL,description='',tls_connect=1,tls_accept=1,"
			"tls_issuer='',tls_subject='',tls_psk_identity='',tls_psk='',proxy_address='',auto_compress=1",
		hostid, "Template DNS Test", 3, "Template DNS Test");

	DB_EXEC("insert into hosts_groups set hostgroupid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ",groupid=" ZBX_FS_UI64,
		DBget_maxid_num("hosts_groups", 1), hostid, groupid_templates);

#define SQL	"insert into applications set applicationid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ",name='%s',flags=0"
	DB_EXEC(SQL, applicationid_dns   , hostid, "DNS");
	DB_EXEC(SQL, applicationid_dnssec, hostid, "DNSSEC");
#undef SQL

#define SQL	"insert into items set itemid=" ZBX_FS_UI64 ",type=%d,snmp_community='',snmp_oid='',"			\
		"hostid=" ZBX_FS_UI64 ",name='%s',key_='%s',delay='%s',history='%s',trends='%s',status=0,"		\
		"value_type=%d,trapper_hosts='',units='',snmpv3_securityname='',snmpv3_securitylevel=0,"		\
		"snmpv3_authpassphrase='',snmpv3_privpassphrase='',formula='',logtimefmt='',templateid=NULL,"		\
		"valuemapid=nullif(" ZBX_FS_UI64 ",0),params='%s',ipmi_sensor='',authtype=0,username='',password='',"	\
		"publickey='',privatekey='',flags=%d,interfaceid=NULL,port='',description='%s',inventory_link=0,"	\
		"lifetime='%s',snmpv3_authprotocol=0,snmpv3_privprotocol=0,snmpv3_contextname='',evaltype=0,"		\
		"jmx_endpoint='',master_itemid=nullif(" ZBX_FS_UI64 ",0),timeout='3s',url='',query_fields='',posts='',"	\
		"status_codes='200',follow_redirects=1,post_type=0,http_proxy='',headers='',retrieve_mode=0,"		\
		"request_method=0,output_format=0,ssl_cert_file='',ssl_key_file='',ssl_key_password='',verify_peer=0,"	\
		"verify_host=0,allow_traps=0"

#define ITEM_TYPE_SIMPLE		3
#define ITEM_TYPE_CALCULATED		15
#define ITEM_TYPE_DEPENDENT		18

#define ITEM_VALUE_TYPE_FLOAT		0
#define ITEM_VALUE_TYPE_STR		1
#define ITEM_VALUE_TYPE_UINT64		3
#define ITEM_VALUE_TYPE_TEXT		4

#define ZBX_FLAG_DISCOVERY		0x01	/* Discovery rule */
#define ZBX_FLAG_DISCOVERY_PROTOTYPE	0x02	/* Item prototype */

	/* DB_EXEC(SQL, itemid, type, hostid,			*/
	/* 		name, key_, delay, history, trends,	*/
	/* 		value_type, valuemapid, params, flags,	*/
	/* 		description,				*/
	/* 		lifetime, master_itemid);		*/
	DB_EXEC(SQL, itemid_dnssec_enabled, ITEM_TYPE_CALCULATED, hostid,
		"DNSSEC enabled/disabled", "dnssec.enabled", "60", "90d", "365d",
		ITEM_VALUE_TYPE_UINT64, (zbx_uint64_t)0, "{$RSM.TLD.DNSSEC.ENABLED}", 0,
		"History of DNSSEC being enabled or disabled.",
		"30d", (zbx_uint64_t)0);
	DB_EXEC(SQL, itemid_rsm_dns, ITEM_TYPE_SIMPLE, hostid,
		"DNS Test",
		"rsm.dns[{$RSM.TLD},{$RSM.DNS.TESTPREFIX},{$RSM.DNS.NAME.SERVERS},{$RSM.TLD.DNSSEC.ENABLED},"
			"{$RSM.TLD.RDDS.ENABLED},{$RSM.TLD.EPP.ENABLED},{$RSM.TLD.DNS.UDP.ENABLED},"
			"{$RSM.TLD.DNS.TCP.ENABLED},{$RSM.IP4.ENABLED},{$RSM.IP6.ENABLED},{$RSM.RESOLVER},"
			"{$RSM.DNS.UDP.RTT.HIGH},{$RSM.DNS.TCP.RTT.HIGH},{$RSM.DNS.TEST.TCP.RATIO},"
			"{$RSM.DNS.TEST.RECOVER},{$RSM.DNS.AVAIL.MINNS}]",
		"{$RSM.DNS.UDP.DELAY}", "0", "0",
		ITEM_VALUE_TYPE_TEXT, (zbx_uint64_t)0, "", 0,
		"Master item that performs the test and generates JSON with results."
			" This JSON will be parsed by dependent items. History must be disabled.",
		"30d", (zbx_uint64_t)0);
	DB_EXEC(SQL, itemid_rsm_dns_mode, ITEM_TYPE_DEPENDENT, hostid,
		"The mode of the Test", "rsm.dns.mode", "0", "90d", "365d",
		ITEM_VALUE_TYPE_UINT64, valuemapid_dns_test_mode, "", 0,
		"The mode (normal or critical) in which the test was performed.",
		"30d", itemid_rsm_dns);
	DB_EXEC(SQL, itemid_rsm_dns_nssok, ITEM_TYPE_DEPENDENT, hostid,
		"Number of working Name Servers", "rsm.dns.nssok", "0", "90d", "365d",
		ITEM_VALUE_TYPE_UINT64, (zbx_uint64_t)0, "", 0,
		"Number of Name Servers that returned successful results out of those used in the test.",
		"30d", itemid_rsm_dns);
	DB_EXEC(SQL, itemid_rsm_dns_protocol, ITEM_TYPE_DEPENDENT, hostid,
		"Transport protocol of the Test", "rsm.dns.protocol", "0", "90d", "365d",
		ITEM_VALUE_TYPE_UINT64, valuemapid_transport_protocol, "", 0,
		"Transport protocol (UDP or TCP) that was used during the test.",
		"30d", itemid_rsm_dns);
	DB_EXEC(SQL, itemid_rsm_dns_status, ITEM_TYPE_DEPENDENT, hostid,
		"Status of a DNS Test", "rsm.dns.status", "0", "90d", "365d",
		ITEM_VALUE_TYPE_UINT64, valuemapid_service_state, "", 0,
		"Status of a DNS Test, one of 1 (Up) or 0 (Down).",
		"30d", itemid_rsm_dns);
	DB_EXEC(SQL, itemid_rsm_dns_ns_discovery, ITEM_TYPE_DEPENDENT, hostid,
		"Name Servers discovery", "rsm.dns.ns.discovery", "0", "90d", "0",
		ITEM_VALUE_TYPE_TEXT, (zbx_uint64_t)0, "", ZBX_FLAG_DISCOVERY,
		"Discovers Name Servers that were used in DNS test.",
		"1000d", itemid_rsm_dns);
	DB_EXEC(SQL, itemid_rsm_dns_nsip_discovery, ITEM_TYPE_DEPENDENT, hostid,
		"NS-IP pairs discovery", "rsm.dns.nsip.discovery", "0", "90d", "0",
		ITEM_VALUE_TYPE_TEXT, (zbx_uint64_t)0, "", ZBX_FLAG_DISCOVERY,
		"Discovers Name Servers (NS-IP pairs) that were used in DNS test.",
		"1000d", itemid_rsm_dns);
	DB_EXEC(SQL, itemid_rsm_dns_ns_status, ITEM_TYPE_DEPENDENT, hostid,
		"Status of $1", "rsm.dns.ns.status[{#NS}]", "0", "90d", "365d",
		ITEM_VALUE_TYPE_UINT64, valuemapid_rsm_service_availability, "", ZBX_FLAG_DISCOVERY_PROTOTYPE,
		"Status of Name Server: Up (1) or Down (0)."
			" The Name Server is considered to be up if all its IPs returned successful RTTs.",
		"30d", itemid_rsm_dns);
	DB_EXEC(SQL, itemid_rsm_dns_nsid, ITEM_TYPE_DEPENDENT, hostid,
		"NSID of $1,$2", "rsm.dns.nsid[{#NS},{#IP}]", "0", "90d", "0",
		ITEM_VALUE_TYPE_STR, (zbx_uint64_t)0, "", ZBX_FLAG_DISCOVERY_PROTOTYPE,
		"DNS Name Server Identifier of the target Name Server that was tested.",
		"30d", itemid_rsm_dns);
	DB_EXEC(SQL, itemid_rsm_dns_rtt_tcp, ITEM_TYPE_DEPENDENT, hostid,
		"RTT of $1,$2 using $3", "rsm.dns.rtt[{#NS},{#IP},tcp]", "0", "90d", "365d",
		ITEM_VALUE_TYPE_FLOAT, valuemapid_rsm_dns_rtt, "", ZBX_FLAG_DISCOVERY_PROTOTYPE,
		"The Round-Time Trip returned when testing specific IP of Name Server using TCP protocol.",
		"30d", itemid_rsm_dns);
	DB_EXEC(SQL, itemid_rsm_dns_rtt_udp, ITEM_TYPE_DEPENDENT, hostid,
		"RTT of $1,$2 using $3", "rsm.dns.rtt[{#NS},{#IP},udp]", "0", "90d", "365d",
		ITEM_VALUE_TYPE_FLOAT, valuemapid_rsm_dns_rtt, "", ZBX_FLAG_DISCOVERY_PROTOTYPE,
		"The Round-Time Trip returned when testing specific IP of Name Server using UDP protocol.",
		"30d", itemid_rsm_dns);

#undef ITEM_TYPE_SIMPLE
#undef ITEM_TYPE_CALCULATED
#undef ITEM_TYPE_DEPENDENT

#undef ITEM_VALUE_TYPE_FLOAT
#undef ITEM_VALUE_TYPE_STR
#undef ITEM_VALUE_TYPE_UINT64
#undef ITEM_VALUE_TYPE_TEXT

#undef ZBX_FLAG_DISCOVERY
#undef ZBX_FLAG_DISCOVERY_PROTOTYPE

#undef SQL

#define SQL	"insert into items_applications set itemappid=" ZBX_FS_UI64 ",applicationid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_dnssec, itemid_dnssec_enabled);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_dns   , itemid_rsm_dns);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_dns   , itemid_rsm_dns_mode);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_dns   , itemid_rsm_dns_nssok);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_dns   , itemid_rsm_dns_protocol);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_dns   , itemid_rsm_dns_status);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_dns   , itemid_rsm_dns_ns_status);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_dns   , itemid_rsm_dns_nsid);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_dns   , itemid_rsm_dns_rtt_tcp);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_dns   , itemid_rsm_dns_rtt_udp);
#undef SQL

#define SQL	"insert into item_discovery set itemdiscoveryid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64 ","		\
		"parent_itemid=" ZBX_FS_UI64 ",key_='',lastcheck=0,ts_delete=0"
	DB_EXEC(SQL, DBget_maxid_num("item_discovery", 1), itemid_rsm_dns_ns_status, itemid_rsm_dns_ns_discovery);
	DB_EXEC(SQL, DBget_maxid_num("item_discovery", 1), itemid_rsm_dns_nsid     , itemid_rsm_dns_nsip_discovery);
	DB_EXEC(SQL, DBget_maxid_num("item_discovery", 1), itemid_rsm_dns_rtt_tcp  , itemid_rsm_dns_nsip_discovery);
	DB_EXEC(SQL, DBget_maxid_num("item_discovery", 1), itemid_rsm_dns_rtt_udp  , itemid_rsm_dns_nsip_discovery);
#undef SQL

#define SQL	"insert into item_preproc set"										\
		" item_preprocid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64 ",step=%d,type=%d,params='%s',"			\
		"error_handler=%d,error_handler_params=''"
	/* type 12 = ZBX_PREPROC_JSONPATH */
	DB_EXEC(SQL, DBget_maxid_num("item_preproc", 1), itemid_rsm_dns_mode, 1, 12,
			"$.mode", 0);
	DB_EXEC(SQL, DBget_maxid_num("item_preproc", 1), itemid_rsm_dns_nssok, 1, 12,
			"$.nssok", 0);
	DB_EXEC(SQL, DBget_maxid_num("item_preproc", 1), itemid_rsm_dns_protocol, 1, 12,
			"$.protocol", 0);
	DB_EXEC(SQL, DBget_maxid_num("item_preproc", 1), itemid_rsm_dns_status, 1, 12,
			"$.status", 0);
	DB_EXEC(SQL, DBget_maxid_num("item_preproc", 1), itemid_rsm_dns_ns_discovery, 1, 12,
			"$.nss", 0);
	DB_EXEC(SQL, DBget_maxid_num("item_preproc", 1), itemid_rsm_dns_nsip_discovery, 1, 12,
			"$.nsips", 0);
	DB_EXEC(SQL, DBget_maxid_num("item_preproc", 1), itemid_rsm_dns_ns_status, 1, 12,
			"$.nss[?(@.[''ns''] == ''{#NS}'')].status.first()", 1);
	DB_EXEC(SQL, DBget_maxid_num("item_preproc", 1), itemid_rsm_dns_nsid, 1, 12,
			"$.nsips[?(@.[''ns''] == ''{#NS}'' && @.[''ip''] == ''{#IP}'')].nsid.first()", 1);
	DB_EXEC(SQL, DBget_maxid_num("item_preproc", 1), itemid_rsm_dns_rtt_tcp, 1, 12,
			"$.nsips[?(@.[''ns''] == ''{#NS}'' && @.[''ip''] == ''{#IP}'' && @.[''protocol''] == ''tcp'')].rtt.first()", 1);
	DB_EXEC(SQL, DBget_maxid_num("item_preproc", 1), itemid_rsm_dns_rtt_udp, 1, 12,
			"$.nsips[?(@.[''ns''] == ''{#NS}'' && @.[''ip''] == ''{#IP}'' && @.[''protocol''] == ''udp'')].rtt.first()", 1);
#undef SQL

#define SQL	"insert into lld_macro_path set lld_macro_pathid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64 ",lld_macro='%s',path='%s'"
	DB_EXEC(SQL, DBget_maxid_num("lld_macro_path", 1), itemid_rsm_dns_ns_discovery  , "{#NS}", "$.ns");
	DB_EXEC(SQL, DBget_maxid_num("lld_macro_path", 1), itemid_rsm_dns_nsip_discovery, "{#IP}", "$.ip");
	DB_EXEC(SQL, DBget_maxid_num("lld_macro_path", 1), itemid_rsm_dns_nsip_discovery, "{#NS}", "$.ns");
#undef SQL

	ret = SUCCEED;
out:
	return ret;
}

/* 4050012, 11 - add "Template RDDS Test" template */
static int	DBpatch_4050012_11(void)
{
	int		ret = FAIL;

	zbx_uint64_t	groupid_templates;		/* groupid of "Templates" host group */
	zbx_uint64_t	valuemapid_rsm_rdds_rtt;	/* valuemapid of "RSM RDDS rtt" value map */
	zbx_uint64_t	valuemapid_rsm_rdds_result;	/* valuemapid of "RSM RDDS result" value map */

	zbx_uint64_t	hostid;				/* hostid of "Template RDDS Test" template */

	zbx_uint64_t	applicationid_next;
	zbx_uint64_t	applicationid_rdds;		/* applicationid of "RDDS" application */
	zbx_uint64_t	applicationid_rdds43;		/* applicationid of "RDDS43" application */
	zbx_uint64_t	applicationid_rdds80;		/* applicationid of "RDDS80" application */

	zbx_uint64_t	itemid_next;
	zbx_uint64_t	itemid_rsm_rdds;		/* itemid of "rsm.rdds[]" item */
	zbx_uint64_t	itemid_rsm_rdds43_ip;		/* itemid of "rsm.rdds.43.ip" item */
	zbx_uint64_t	itemid_rsm_rdds43_rtt;		/* itemid of "rsm.rdds.43.rtt" item */
	zbx_uint64_t	itemid_rsm_rdds_status;		/* itemid of "rsm.rdds.status" item */
	zbx_uint64_t	itemid_rsm_rdds80_ip;		/* itemid of "rsm.rdds.80.ip" item */
	zbx_uint64_t	itemid_rsm_rdds80_rtt;		/* itemid of "rsm.rdds.80.rtt" item */

	/* It was decided to setup monitoring from scratch rather than upgrade all the configuration, */
	/* therefore this patch may be obsolete and there's no guarantee that it works correctly.     */
	goto out;

	ONLY_SERVER();

	GET_HOST_GROUP_ID(groupid_templates, "Templates");
	GET_VALUE_MAP_ID(valuemapid_rsm_rdds_rtt, "RSM RDDS rtt");
	GET_VALUE_MAP_ID(valuemapid_rsm_rdds_result, "RSM RDDS result");

	hostid = DBget_maxid_num("hosts", 1);

	applicationid_next   = DBget_maxid_num("applications", 3);
	applicationid_rdds   = applicationid_next++;
	applicationid_rdds43 = applicationid_next++;
	applicationid_rdds80 = applicationid_next++;

	itemid_next            = DBget_maxid_num("items", 6);
	itemid_rsm_rdds        = itemid_next++;
	itemid_rsm_rdds43_ip   = itemid_next++;
	itemid_rsm_rdds43_rtt  = itemid_next++;
	itemid_rsm_rdds_status = itemid_next++;
	itemid_rsm_rdds80_ip   = itemid_next++;
	itemid_rsm_rdds80_rtt  = itemid_next++;

	/* status 3 = HOST_STATUS_TEMPLATE */
	DB_EXEC("insert into hosts set hostid=" ZBX_FS_UI64 ",created=0,proxy_hostid=NULL,host='%s',status=%d,"
			"disable_until=0,error='',available=0,errors_from=0,lastaccess=0,ipmi_authtype=-1,"
			"ipmi_privilege=2,ipmi_username='',ipmi_password='',ipmi_disable_until=0,ipmi_available=0,"
			"snmp_disable_until=0,snmp_available=0,maintenanceid=NULL,maintenance_status=0,"
			"maintenance_type=0,maintenance_from=0,ipmi_errors_from=0,snmp_errors_from=0,ipmi_error='',"
			"snmp_error='',jmx_disable_until=0,jmx_available=0,jmx_errors_from=0,jmx_error='',name='%s',"
			"info_1='',info_2='',flags=0,templateid=NULL,description='',tls_connect=1,tls_accept=1,"
			"tls_issuer='',tls_subject='',tls_psk_identity='',tls_psk='',proxy_address='',auto_compress=1",
		hostid, "Template RDDS Test", 3, "Template RDDS Test");

	DB_EXEC("insert into hosts_groups set hostgroupid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ",groupid=" ZBX_FS_UI64,
		DBget_maxid_num("hosts_groups", 1), hostid, groupid_templates);

#define SQL	"insert into applications set applicationid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ",name='%s',flags=0"
	DB_EXEC(SQL, applicationid_rdds  , hostid, "RDDS");
	DB_EXEC(SQL, applicationid_rdds43, hostid, "RDDS43");
	DB_EXEC(SQL, applicationid_rdds80, hostid, "RDDS80");
#undef SQL

#define SQL	"insert into items set itemid=" ZBX_FS_UI64 ",type=%d,snmp_community='',snmp_oid='',"			\
		"hostid=" ZBX_FS_UI64 ",name='%s',key_='%s',delay='%s',history='%s',trends='%s',status=0,"		\
		"value_type=%d,trapper_hosts='',units='',snmpv3_securityname='',snmpv3_securitylevel=0,"		\
		"snmpv3_authpassphrase='',snmpv3_privpassphrase='',formula='',logtimefmt='',templateid=NULL,"		\
		"valuemapid=nullif(" ZBX_FS_UI64 ",0),params='',ipmi_sensor='',authtype=0,username='',password='',"	\
		"publickey='',privatekey='',flags=0,interfaceid=NULL,port='',description='%s',inventory_link=0,"	\
		"lifetime='30d',snmpv3_authprotocol=0,snmpv3_privprotocol=0,snmpv3_contextname='',evaltype=0,"		\
		"jmx_endpoint='',master_itemid=nullif(" ZBX_FS_UI64 ",0),timeout='3s',url='',query_fields='',posts='',"	\
		"status_codes='200',follow_redirects=1,post_type=0,http_proxy='',headers='',retrieve_mode=0,"		\
		"request_method=0,output_format=0,ssl_cert_file='',ssl_key_file='',ssl_key_password='',verify_peer=0,"	\
		"verify_host=0,allow_traps=0"
	/* type 3 = ITEM_TYPE_SIMPLE */
	/* type 18 = ITEM_TYPE_DEPENDENT */
	/* value_type 0 = ITEM_VALUE_TYPE_FLOAT */
	/* value_type 1 = ITEM_VALUE_TYPE_STR */
	/* value_type 3 = ITEM_VALUE_TYPE_UINT64 */
	/* value_type 4 = ITEM_VALUE_TYPE_TEXT */
	/* DB_EXEC(SQL, itemid, type, hostid, name,			*/
	/* 		key,						*/
	/* 		delay, history, trends, value_type, valuemapid,	*/
	/* 		description,					*/
	/* 		master_itemid);					*/
	DB_EXEC(SQL, itemid_rsm_rdds, 3, hostid, "RDDS Test",
		"rsm.rdds[{$RSM.TLD},{$RSM.TLD.RDDS.43.SERVERS},{$RSM.TLD.RDDS.80.SERVERS},"
			"{$RSM.RDDS43.TEST.DOMAIN},{$RSM.RDDS.NS.STRING},{$RSM.RDDS.ENABLED},"
			"{$RSM.TLD.RDDS.ENABLED},{$RSM.EPP.ENABLED},{$RSM.TLD.EPP.ENABLED},{$RSM.IP4.ENABLED},"
			"{$RSM.IP6.ENABLED},{$RSM.RESOLVER},{$RSM.RDDS.RTT.HIGH},{$RSM.RDDS.MAXREDIRS}]",
		"{$RSM.RDDS.DELAY}", "0", "0", 4, (zbx_uint64_t)0,
		"Master item that performs the RDDS test and generates JSON with results. This JSON will be"
			" parsed by dependent items. History must be disabled.",
		(zbx_uint64_t)0);
	DB_EXEC(SQL, itemid_rsm_rdds43_ip, 18, hostid, "RDDS43 IP",
		"rsm.rdds.43.ip",
		"0", "90d", "0", 1, (zbx_uint64_t)0,
		"IP address for RDDS43 test",
		itemid_rsm_rdds);
	DB_EXEC(SQL, itemid_rsm_rdds43_rtt, 18, hostid, "RDDS43 RTT",
		"rsm.rdds.43.rtt",
		"0", "90d", "365d", 0, valuemapid_rsm_rdds_rtt,
		"RTT value for RDDS43 test",
		itemid_rsm_rdds);
	DB_EXEC(SQL, itemid_rsm_rdds_status, 18, hostid, "Status of RDDS Test",
		"rsm.rdds.status",
		"0", "90d", "365d", 3, valuemapid_rsm_rdds_result,
		"Status of RDDS Test: 0 (Down), 1 (Up), 2 (RDDS43 only) or 3 (RDDS80 only).",
		itemid_rsm_rdds);
	DB_EXEC(SQL, itemid_rsm_rdds80_ip, 18, hostid, "RDDS80 IP",
		"rsm.rdds.80.ip",
		"0", "90d", "0", 1, (zbx_uint64_t)0,
		"IP address for RDDS80 test",
		itemid_rsm_rdds);
	DB_EXEC(SQL, itemid_rsm_rdds80_rtt, 18, hostid, "RDDS80 RTT",
		"rsm.rdds.80.rtt",
		"0", "90d", "365d", 0, valuemapid_rsm_rdds_rtt,
		"RTT value for RDDS80 test",
		itemid_rsm_rdds);
#undef SQL

#define SQL	"insert into items_applications set itemappid=" ZBX_FS_UI64 ",applicationid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_rdds  , itemid_rsm_rdds);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_rdds43, itemid_rsm_rdds43_ip);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_rdds43, itemid_rsm_rdds43_rtt);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_rdds  , itemid_rsm_rdds_status);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_rdds80, itemid_rsm_rdds80_ip);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_rdds80, itemid_rsm_rdds80_rtt);
#undef SQL

#define SQL	"insert into item_preproc set"										\
		" item_preprocid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64 ",step=%d,type=%d,params='%s',"			\
		"error_handler=%d,error_handler_params=''"
	/* type 12 = ZBX_PREPROC_JSONPATH */
	DB_EXEC(SQL, DBget_maxid_num("item_preproc", 1), itemid_rsm_rdds43_ip  , 1, 12, "$.rdds43.ip" , 1);
	DB_EXEC(SQL, DBget_maxid_num("item_preproc", 1), itemid_rsm_rdds43_rtt , 1, 12, "$.rdds43.rtt", 0);
	DB_EXEC(SQL, DBget_maxid_num("item_preproc", 1), itemid_rsm_rdds_status, 1, 12, "$.status"    , 0);
	DB_EXEC(SQL, DBget_maxid_num("item_preproc", 1), itemid_rsm_rdds80_ip  , 1, 12, "$.rdds80.ip" , 1);
	DB_EXEC(SQL, DBget_maxid_num("item_preproc", 1), itemid_rsm_rdds80_rtt , 1, 12, "$.rdds80.rtt", 0);
#undef SQL

	ret = SUCCEED;
out:
	return ret;
}

/* 4050012, 12 - rename "Template RDAP" to "Template RDAP Test" */
static int	DBpatch_4050012_12(void)
{
	int	ret = FAIL;

	/* It was decided to setup monitoring from scratch rather than upgrade all the configuration, */
	/* therefore this patch may be obsolete and there's no guarantee that it works correctly.     */
	goto out;

	ONLY_SERVER();

	DB_EXEC("update hosts set host='Template RDAP Test',name='Template RDAP Test' where host='Template RDAP'");

	ret = SUCCEED;
out:
	return ret;
}

/* 4050012, 13 - add "Template DNS Status" template */
static int	DBpatch_4050012_13(void)
{
	int		ret = FAIL;

	zbx_uint64_t	groupid_templates;				/* groupid of "Templates" host group */
	zbx_uint64_t	valuemapid_rsm_service_availability;		/* valuemapid of "RSM Service Availability" value map */

	zbx_uint64_t	hostid;						/* hostid of "Template DNS Status" template */

	zbx_uint64_t	applicationid_next;
	zbx_uint64_t	applicatinoid_slv_current_month;		/* applicationid of "SLV current month" application */
	zbx_uint64_t	applicatinoid_slv_particular_test;		/* applicationid of "SLV particular test" application */
	zbx_uint64_t	applicatinoid_slv_rolling_week;			/* applicationid of "SLV rolling week" application */

	zbx_uint64_t	itemid_next;
	zbx_uint64_t	itemid_rsm_slv_dns_avail;			/* itemid of "DNS availability" item */
	zbx_uint64_t	itemid_rsm_slv_dns_downtime;			/* itemid of "DNS minutes of downtime" item */
	zbx_uint64_t	itemid_rsm_slv_dns_rollweek;			/* itemid of "DNS weekly unavailability" item */
	zbx_uint64_t	itemid_rsm_slv_dns_tcp_rtt_failed;		/* itemid of "Number of failed monthly DNS TCP tests" item */
	zbx_uint64_t	itemid_rsm_slv_dns_tcp_rtt_performed;		/* itemid of "Number of performed monthly DNS TCP tests" item */
	zbx_uint64_t	itemid_rsm_slv_dns_tcp_rtt_pfailed;		/* itemid of "Ratio of failed monthly DNS TCP tests" item */
	zbx_uint64_t	itemid_rsm_slv_dns_udp_rtt_failed;		/* itemid of "Number of failed monthly DNS UDP tests" item */
	zbx_uint64_t	itemid_rsm_slv_dns_udp_rtt_performed;		/* itemid of "Number of performed monthly DNS UDP tests" item */
	zbx_uint64_t	itemid_rsm_slv_dns_udp_rtt_pfailed;		/* itemid of "Ratio of failed monthly DNS UDP tests" item */
	zbx_uint64_t	itemid_rsm_dns_nsip_discovery;			/* itemid of "NS-IP pairs discovery" item */
	zbx_uint64_t	itemid_rsm_slv_dns_ns_avail_ns_ip;		/* itemid of "DNS NS $1 ($2) availability" item */
	zbx_uint64_t	itemid_rsm_slv_dns_ns_downtime_ns_ip;		/* itemid of "DNS minutes of $1 ($2) downtime" item */

	zbx_uint64_t	triggerid_next;
	zbx_uint64_t	triggerid_service_down;				/* triggerid of "DNS service is down" trigger */
	zbx_uint64_t	triggerid_downtime_over_100;			/* triggerid of "DNS service was unavailable for at least {ITEM.VALUE1}m" trigger */
	zbx_uint64_t	triggerid_rollweek_over_10;			/* triggerid of "DNS rolling week is over 10%" trigger */
	zbx_uint64_t	triggerid_rollweek_over_25;			/* triggerid of "DNS rolling week is over 25%" trigger */
	zbx_uint64_t	triggerid_rollweek_over_50;			/* triggerid of "DNS rolling week is over 50%" trigger */
	zbx_uint64_t	triggerid_rollweek_over_75;			/* triggerid of "DNS rolling week is over 75%" trigger */
	zbx_uint64_t	triggerid_rollweek_over_100;			/* triggerid of "DNS rolling week is over 100%" trigger */
	zbx_uint64_t	triggerid_tcp_rtt_pfailed_over_10;		/* triggerid of "Ratio of failed DNS TCP tests exceeded 10% of allowed $1%" trigger */
	zbx_uint64_t	triggerid_tcp_rtt_pfailed_over_25;		/* triggerid of "Ratio of failed DNS TCP tests exceeded 25% of allowed $1%" trigger */
	zbx_uint64_t	triggerid_tcp_rtt_pfailed_over_50;		/* triggerid of "Ratio of failed DNS TCP tests exceeded 50% of allowed $1%" trigger */
	zbx_uint64_t	triggerid_tcp_rtt_pfailed_over_75;		/* triggerid of "Ratio of failed DNS TCP tests exceeded 75% of allowed $1%" trigger */
	zbx_uint64_t	triggerid_tcp_rtt_pfailed_over_100;		/* triggerid of "Ratio of failed DNS TCP tests exceeded 100% of allowed $1%" trigger */
	zbx_uint64_t	triggerid_udp_rtt_pfailed_over_10;		/* triggerid of "Ratio of failed DNS UDP tests exceeded 10% of allowed $1%" trigger */
	zbx_uint64_t	triggerid_udp_rtt_pfailed_over_25;		/* triggerid of "Ratio of failed DNS UDP tests exceeded 25% of allowed $1%" trigger */
	zbx_uint64_t	triggerid_udp_rtt_pfailed_over_50;		/* triggerid of "Ratio of failed DNS UDP tests exceeded 50% of allowed $1%" trigger */
	zbx_uint64_t	triggerid_udp_rtt_pfailed_over_75;		/* triggerid of "Ratio of failed DNS UDP tests exceeded 75% of allowed $1%" trigger */
	zbx_uint64_t	triggerid_udp_rtt_pfailed_over_100;		/* triggerid of "Ratio of failed DNS UDP tests exceeded 100% of allowed $1%" trigger */
	zbx_uint64_t	triggerid_ns_ip_downtime_over_10;		/* triggerid of "DNS {#NS} ({#IP}) downtime exceeded 10% of allowed $1 minutes" trigger */
	zbx_uint64_t	triggerid_ns_ip_downtime_over_25;		/* triggerid of "DNS {#NS} ({#IP}) downtime exceeded 25% of allowed $1 minutes" trigger */
	zbx_uint64_t	triggerid_ns_ip_downtime_over_50;		/* triggerid of "DNS {#NS} ({#IP}) downtime exceeded 50% of allowed $1 minutes" trigger */
	zbx_uint64_t	triggerid_ns_ip_downtime_over_75;		/* triggerid of "DNS {#NS} ({#IP}) downtime exceeded 75% of allowed $1 minutes" trigger */
	zbx_uint64_t	triggerid_ns_ip_downtime_over_100;		/* triggerid of "DNS {#NS} ({#IP}) downtime exceeded 100% of allowed $1 minutes" trigger */

	zbx_uint64_t	functionid_next;
	zbx_uint64_t	functionid_service_down_1;			/* functionid for "DNS service is down" trigger */
	zbx_uint64_t	functionid_service_down_2;			/* functionid for "DNS service is down" trigger */
	zbx_uint64_t	functionid_downtime_over_100;			/* functionid for "DNS service was unavailable for at least {ITEM.VALUE1}m" trigger */
	zbx_uint64_t	functionid_rollweek_over_10;			/* functionid for "DNS rolling week is over 10%" trigger */
	zbx_uint64_t	functionid_rollweek_over_25;			/* functionid for "DNS rolling week is over 25%" trigger */
	zbx_uint64_t	functionid_rollweek_over_50;			/* functionid for "DNS rolling week is over 50%" trigger */
	zbx_uint64_t	functionid_rollweek_over_75;			/* functionid for "DNS rolling week is over 75%" trigger */
	zbx_uint64_t	functionid_rollweek_over_100;			/* functionid for "DNS rolling week is over 100%" trigger */
	zbx_uint64_t	functionid_tcp_rtt_pfailed_over_10;		/* functionid for "Ratio of failed DNS TCP tests exceeded 10% of allowed $1%" trigger */
	zbx_uint64_t	functionid_tcp_rtt_pfailed_over_25;		/* functionid for "Ratio of failed DNS TCP tests exceeded 25% of allowed $1%" trigger */
	zbx_uint64_t	functionid_tcp_rtt_pfailed_over_50;		/* functionid for "Ratio of failed DNS TCP tests exceeded 50% of allowed $1%" trigger */
	zbx_uint64_t	functionid_tcp_rtt_pfailed_over_75;		/* functionid for "Ratio of failed DNS TCP tests exceeded 75% of allowed $1%" trigger */
	zbx_uint64_t	functionid_tcp_rtt_pfailed_over_100;		/* functionid for "Ratio of failed DNS TCP tests exceeded 100% of allowed $1%" trigger */
	zbx_uint64_t	functionid_udp_rtt_pfailed_over_10;		/* functionid for "Ratio of failed DNS UDP tests exceeded 10% of allowed $1%" trigger */
	zbx_uint64_t	functionid_udp_rtt_pfailed_over_25;		/* functionid for "Ratio of failed DNS UDP tests exceeded 25% of allowed $1%" trigger */
	zbx_uint64_t	functionid_udp_rtt_pfailed_over_50;		/* functionid for "Ratio of failed DNS UDP tests exceeded 50% of allowed $1%" trigger */
	zbx_uint64_t	functionid_udp_rtt_pfailed_over_75;		/* functionid for "Ratio of failed DNS UDP tests exceeded 75% of allowed $1%" trigger */
	zbx_uint64_t	functionid_udp_rtt_pfailed_over_100;		/* functionid for "Ratio of failed DNS UDP tests exceeded 100% of allowed $1%" trigger */
	zbx_uint64_t	functionid_ns_ip_downtime_over_10;		/* functionid for "DNS {#NS} ({#IP}) downtime exceeded 10% of allowed $1 minutes" trigger */
	zbx_uint64_t	functionid_ns_ip_downtime_over_25;		/* functionid for "DNS {#NS} ({#IP}) downtime exceeded 25% of allowed $1 minutes" trigger */
	zbx_uint64_t	functionid_ns_ip_downtime_over_50;		/* functionid for "DNS {#NS} ({#IP}) downtime exceeded 50% of allowed $1 minutes" trigger */
	zbx_uint64_t	functionid_ns_ip_downtime_over_75;		/* functionid for "DNS {#NS} ({#IP}) downtime exceeded 75% of allowed $1 minutes" trigger */
	zbx_uint64_t	functionid_ns_ip_downtime_over_100;		/* functionid for "DNS {#NS} ({#IP}) downtime exceeded 100% of allowed $1 minutes" trigger */

	/* It was decided to setup monitoring from scratch rather than upgrade all the configuration, */
	/* therefore this patch may be obsolete and there's no guarantee that it works correctly.     */
	goto out;

	ONLY_SERVER();

	GET_HOST_GROUP_ID(groupid_templates, "Templates");
	GET_VALUE_MAP_ID(valuemapid_rsm_service_availability, "RSM Service Availability");

	hostid = DBget_maxid_num("hosts", 1);

	applicationid_next                = DBget_maxid_num("applications", 3);
	applicatinoid_slv_current_month   = applicationid_next++;
	applicatinoid_slv_particular_test = applicationid_next++;
	applicatinoid_slv_rolling_week    = applicationid_next++;

	itemid_next                          = DBget_maxid_num("items", 12);
	itemid_rsm_slv_dns_avail             = itemid_next++;
	itemid_rsm_slv_dns_downtime          = itemid_next++;
	itemid_rsm_slv_dns_rollweek          = itemid_next++;
	itemid_rsm_slv_dns_tcp_rtt_failed    = itemid_next++;
	itemid_rsm_slv_dns_tcp_rtt_performed = itemid_next++;
	itemid_rsm_slv_dns_tcp_rtt_pfailed   = itemid_next++;
	itemid_rsm_slv_dns_udp_rtt_failed    = itemid_next++;
	itemid_rsm_slv_dns_udp_rtt_performed = itemid_next++;
	itemid_rsm_slv_dns_udp_rtt_pfailed   = itemid_next++;
	itemid_rsm_dns_nsip_discovery        = itemid_next++;
	itemid_rsm_slv_dns_ns_avail_ns_ip    = itemid_next++;
	itemid_rsm_slv_dns_ns_downtime_ns_ip = itemid_next++;

	triggerid_next                     = DBget_maxid_num("triggers", 22);
	triggerid_service_down             = triggerid_next++;
	triggerid_downtime_over_100        = triggerid_next++;
	triggerid_rollweek_over_10         = triggerid_next++;
	triggerid_rollweek_over_25         = triggerid_next++;
	triggerid_rollweek_over_50         = triggerid_next++;
	triggerid_rollweek_over_75         = triggerid_next++;
	triggerid_rollweek_over_100        = triggerid_next++;
	triggerid_tcp_rtt_pfailed_over_10  = triggerid_next++;
	triggerid_tcp_rtt_pfailed_over_25  = triggerid_next++;
	triggerid_tcp_rtt_pfailed_over_50  = triggerid_next++;
	triggerid_tcp_rtt_pfailed_over_75  = triggerid_next++;
	triggerid_tcp_rtt_pfailed_over_100 = triggerid_next++;
	triggerid_udp_rtt_pfailed_over_10  = triggerid_next++;
	triggerid_udp_rtt_pfailed_over_25  = triggerid_next++;
	triggerid_udp_rtt_pfailed_over_50  = triggerid_next++;
	triggerid_udp_rtt_pfailed_over_75  = triggerid_next++;
	triggerid_udp_rtt_pfailed_over_100 = triggerid_next++;
	triggerid_ns_ip_downtime_over_10   = triggerid_next++;
	triggerid_ns_ip_downtime_over_25   = triggerid_next++;
	triggerid_ns_ip_downtime_over_50   = triggerid_next++;
	triggerid_ns_ip_downtime_over_75   = triggerid_next++;
	triggerid_ns_ip_downtime_over_100  = triggerid_next++;

	functionid_next                     = DBget_maxid_num("functions", 23);
	functionid_service_down_1           = functionid_next++;
	functionid_service_down_2           = functionid_next++;
	functionid_downtime_over_100        = functionid_next++;
	functionid_rollweek_over_10         = functionid_next++;
	functionid_rollweek_over_25         = functionid_next++;
	functionid_rollweek_over_50         = functionid_next++;
	functionid_rollweek_over_75         = functionid_next++;
	functionid_rollweek_over_100        = functionid_next++;
	functionid_tcp_rtt_pfailed_over_10  = functionid_next++;
	functionid_tcp_rtt_pfailed_over_25  = functionid_next++;
	functionid_tcp_rtt_pfailed_over_50  = functionid_next++;
	functionid_tcp_rtt_pfailed_over_75  = functionid_next++;
	functionid_tcp_rtt_pfailed_over_100 = functionid_next++;
	functionid_udp_rtt_pfailed_over_10  = functionid_next++;
	functionid_udp_rtt_pfailed_over_25  = functionid_next++;
	functionid_udp_rtt_pfailed_over_50  = functionid_next++;
	functionid_udp_rtt_pfailed_over_75  = functionid_next++;
	functionid_udp_rtt_pfailed_over_100 = functionid_next++;
	functionid_ns_ip_downtime_over_10   = functionid_next++;
	functionid_ns_ip_downtime_over_25   = functionid_next++;
	functionid_ns_ip_downtime_over_50   = functionid_next++;
	functionid_ns_ip_downtime_over_75   = functionid_next++;
	functionid_ns_ip_downtime_over_100  = functionid_next++;

#define SQL	"insert into hosts set hostid=" ZBX_FS_UI64 ",created=0,proxy_hostid=NULL,host='%s',status=%d,"		\
		"disable_until=0,error='',available=0,errors_from=0,lastaccess=0,ipmi_authtype=-1,ipmi_privilege=2,"	\
		"ipmi_username='',ipmi_password='',ipmi_disable_until=0,ipmi_available=0,snmp_disable_until=0,"		\
		"snmp_available=0,maintenanceid=NULL,maintenance_status=0,maintenance_type=0,maintenance_from=0,"	\
		"ipmi_errors_from=0,snmp_errors_from=0,ipmi_error='',snmp_error='',jmx_disable_until=0,"		\
		"jmx_available=0,jmx_errors_from=0,jmx_error='',name='%s',info_1='',info_2='',flags=0,templateid=NULL,"	\
		"description='%s',tls_connect=1,tls_accept=1,tls_issuer='',tls_subject='',tls_psk_identity='',"		\
		"tls_psk='',proxy_address='',auto_compress=1"
	/* status 3 = HOST_STATUS_TEMPLATE */
	DB_EXEC(SQL, hostid, "Template DNS Status", 3, "Template DNS Status", "DNS SLV items and triggers for linking to <RSMHOST> hosts");
#undef SQL

#define SQL	"insert into hosts_groups set hostgroupid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ",groupid=" ZBX_FS_UI64
	DB_EXEC(SQL, DBget_maxid_num("hosts_groups", 1), hostid, groupid_templates);
#undef SQL

#define SQL	"insert into applications set applicationid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ",name='%s',flags=0"
	DB_EXEC(SQL, applicatinoid_slv_current_month  , hostid, "SLV current month");
	DB_EXEC(SQL, applicatinoid_slv_particular_test, hostid, "SLV particular test");
	DB_EXEC(SQL, applicatinoid_slv_rolling_week   , hostid, "SLV rolling week");
#undef SQL

#define SQL	"insert into items set itemid=" ZBX_FS_UI64 ",type=%d,snmp_community='',snmp_oid='',"			\
		"hostid=" ZBX_FS_UI64 ",name='%s',key_='%s',delay='0',history='90d',trends='%s',status=0,"		\
		"value_type=%d,trapper_hosts='',units='%s',snmpv3_securityname='',snmpv3_securitylevel=0,"		\
		"snmpv3_authpassphrase='',snmpv3_privpassphrase='',formula='',logtimefmt='',templateid=NULL,"		\
		"valuemapid=nullif(" ZBX_FS_UI64 ",0),params='',ipmi_sensor='',authtype=0,username='',password='',"	\
		"publickey='',privatekey='',flags=%d,interfaceid=NULL,port='',description='%s',inventory_link=0,"	\
		"lifetime='%s',snmpv3_authprotocol=0,snmpv3_privprotocol=0,snmpv3_contextname='',evaltype=0,"		\
		"jmx_endpoint='',master_itemid=NULL,timeout='3s',url='',query_fields='',posts='',status_codes='200',"	\
		"follow_redirects=1,post_type=0,http_proxy='',headers='',retrieve_mode=0,request_method=0,"		\
		"output_format=0,ssl_cert_file='',ssl_key_file='',ssl_key_password='',verify_peer=0,verify_host=0,"	\
		"allow_traps=0"
	/* type 2 = ITEM_TYPE_TRAPPER */
	/* value_type 0 = ITEM_VALUE_TYPE_FLOAT */
	/* value_type 3 = ITEM_VALUE_TYPE_UINT64 */
	/* value_type 4 = ITEM_VALUE_TYPE_TEXT */
	/* flags 0 = ZBX_FLAG_DISCOVERY_NORMAL */
	/* flags 1 = ZBX_FLAG_DISCOVERY */
	/* flags 2 = ZBX_FLAG_DISCOVERY_PROTOTYPE */
	/* DB_EXEC(SQL, itemid, type, hostid, name,						*/
	/*	key_, trends, value_type, units, valuemapid, flags, description, lifetime);	*/
	DB_EXEC(SQL, itemid_rsm_slv_dns_avail, 2, hostid, "DNS availability",
		"rsm.slv.dns.avail", "365d", 3, "", valuemapid_rsm_service_availability, 0, "", "30d");
	DB_EXEC(SQL, itemid_rsm_slv_dns_downtime, 2, hostid, "DNS minutes of downtime",
		"rsm.slv.dns.downtime", "365d", 3, "", (zbx_uint64_t)0, 0, "", "30d");
	DB_EXEC(SQL, itemid_rsm_slv_dns_rollweek, 2, hostid, "DNS weekly unavailability",
		"rsm.slv.dns.rollweek", "365d", 0, "%", (zbx_uint64_t)0, 0, "", "30d");
	DB_EXEC(SQL, itemid_rsm_slv_dns_tcp_rtt_failed, 2, hostid, "Number of failed monthly DNS TCP tests",
		"rsm.slv.dns.tcp.rtt.failed", "365d", 3, "", (zbx_uint64_t)0, 0, "", "30d");
	DB_EXEC(SQL, itemid_rsm_slv_dns_tcp_rtt_performed, 2, hostid, "Number of performed monthly DNS TCP tests",
		"rsm.slv.dns.tcp.rtt.performed", "365d", 3, "", (zbx_uint64_t)0, 0, "", "30d");
	DB_EXEC(SQL, itemid_rsm_slv_dns_tcp_rtt_pfailed, 2, hostid, "Ratio of failed monthly DNS TCP tests",
		"rsm.slv.dns.tcp.rtt.pfailed", "365d", 0, "%", (zbx_uint64_t)0, 0, "", "30d");
	DB_EXEC(SQL, itemid_rsm_slv_dns_udp_rtt_failed, 2, hostid, "Number of failed monthly DNS UDP tests",
		"rsm.slv.dns.udp.rtt.failed", "365d", 3, "", (zbx_uint64_t)0, 0, "", "30d");
	DB_EXEC(SQL, itemid_rsm_slv_dns_udp_rtt_performed, 2, hostid, "Number of performed monthly DNS UDP tests",
		"rsm.slv.dns.udp.rtt.performed", "365d", 3, "", (zbx_uint64_t)0, 0, "", "30d");
	DB_EXEC(SQL, itemid_rsm_slv_dns_udp_rtt_pfailed, 2, hostid, "Ratio of failed monthly DNS UDP tests",
		"rsm.slv.dns.udp.rtt.pfailed", "365d", 0, "%", (zbx_uint64_t)0, 0, "", "30d");
	DB_EXEC(SQL, itemid_rsm_dns_nsip_discovery, 2, hostid, "NS-IP pairs discovery",
		"rsm.dns.nsip.discovery", "0", 4, "", (zbx_uint64_t)0, 1, "Discovers Name Servers (NS-IP pairs).", "1000d");
	DB_EXEC(SQL, itemid_rsm_slv_dns_ns_avail_ns_ip, 2, hostid, "DNS NS $1 ($2) availability",
		"rsm.slv.dns.ns.avail[{#NS},{#IP}]", "365d", 3, "", valuemapid_rsm_service_availability, 2, "", "30d");
	DB_EXEC(SQL, itemid_rsm_slv_dns_ns_downtime_ns_ip, 2, hostid, "DNS minutes of $1 ($2) downtime",
		"rsm.slv.dns.ns.downtime[{#NS},{#IP}]", "365d", 3, "", (zbx_uint64_t)0, 2, "", "30d");
#undef SQL

#define SQL	"insert into items_applications set itemappid=" ZBX_FS_UI64 ",applicationid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicatinoid_slv_particular_test, itemid_rsm_slv_dns_avail);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicatinoid_slv_current_month  , itemid_rsm_slv_dns_downtime);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicatinoid_slv_rolling_week   , itemid_rsm_slv_dns_rollweek);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicatinoid_slv_current_month  , itemid_rsm_slv_dns_tcp_rtt_failed);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicatinoid_slv_current_month  , itemid_rsm_slv_dns_tcp_rtt_performed);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicatinoid_slv_current_month  , itemid_rsm_slv_dns_tcp_rtt_pfailed);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicatinoid_slv_current_month  , itemid_rsm_slv_dns_udp_rtt_failed);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicatinoid_slv_current_month  , itemid_rsm_slv_dns_udp_rtt_performed);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicatinoid_slv_current_month  , itemid_rsm_slv_dns_udp_rtt_pfailed);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicatinoid_slv_particular_test, itemid_rsm_slv_dns_ns_avail_ns_ip);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicatinoid_slv_current_month  , itemid_rsm_slv_dns_ns_downtime_ns_ip);
#undef SQL

#define SQL	"insert into item_discovery set itemdiscoveryid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64 ",parent_itemid=" ZBX_FS_UI64 ",key_='',lastcheck=0,ts_delete=0"
	DB_EXEC(SQL, DBget_maxid_num("item_discovery", 1), itemid_rsm_slv_dns_ns_avail_ns_ip   , itemid_rsm_dns_nsip_discovery);
	DB_EXEC(SQL, DBget_maxid_num("item_discovery", 1), itemid_rsm_slv_dns_ns_downtime_ns_ip, itemid_rsm_dns_nsip_discovery);
#undef SQL

#define SQL	"insert into triggers set triggerid=" ZBX_FS_UI64 ","								\
		"expression='({TRIGGER.VALUE}=0 and {" ZBX_FS_UI64 "}=0) or ({TRIGGER.VALUE}=1 and {" ZBX_FS_UI64 "}>0)',"	\
		"description='%s',url='',status=0,value=0,priority=%d,lastchange=0,comments='',error='',"			\
		"templateid=NULL,type=0,state=0,flags=%d,recovery_mode=0,recovery_expression='',correlation_mode=0,"		\
		"correlation_tag='',manual_close=0,opdata=''"
	/* priority 0 = TRIGGER_SEVERITY_NOT_CLASSIFIED */
	/* flags 0x00 = ZBX_FLAG_DISCOVERY_NORMAL */
	DB_EXEC(SQL, triggerid_service_down, functionid_service_down_1, functionid_service_down_2, "DNS service is down", 0, 0);
#undef SQL

#define SQL	"insert into triggers set triggerid=" ZBX_FS_UI64 ","							\
		"expression='{" ZBX_FS_UI64 "}%s',"									\
		"description='%s',url='',status=0,value=0,priority=%d,lastchange=0,comments='',error='',"		\
		"templateid=NULL,type=0,state=0,flags=%d,recovery_mode=0,recovery_expression='',correlation_mode=0,"	\
		"correlation_tag='',manual_close=0,opdata=''"
	/* priority 0 = TRIGGER_SEVERITY_NOT_CLASSIFIED */
	/* priority 2 = TRIGGER_SEVERITY_WARNING */
	/* priority 3 = TRIGGER_SEVERITY_AVERAGE */
	/* priority 4 = TRIGGER_SEVERITY_HIGH */
	/* priority 5 = TRIGGER_SEVERITY_DISASTER */
	/* flags 0x00 = ZBX_FLAG_DISCOVERY_NORMAL */
	/* flags 0x02 = ZBX_FLAG_DISCOVERY_CHILD */
	DB_EXEC(SQL, triggerid_downtime_over_100, functionid_downtime_over_100, ">{$RSM.SLV.DNS.DOWNTIME}",
		"DNS service was unavailable for at least {ITEM.VALUE1}m", 5, 0);
	DB_EXEC(SQL, triggerid_rollweek_over_10, functionid_rollweek_over_10, ">=10",
		"DNS rolling week is over 10%", 2, 0);
	DB_EXEC(SQL, triggerid_rollweek_over_25, functionid_rollweek_over_25, ">=25",
		"DNS rolling week is over 25%", 3, 0);
	DB_EXEC(SQL, triggerid_rollweek_over_50, functionid_rollweek_over_50, ">=50",
		"DNS rolling week is over 50%", 3, 0);
	DB_EXEC(SQL, triggerid_rollweek_over_75, functionid_rollweek_over_75, ">=75",
		"DNS rolling week is over 75%", 4, 0);
	DB_EXEC(SQL, triggerid_rollweek_over_100, functionid_rollweek_over_100, ">=100",
		"DNS rolling week is over 100%", 5, 0);
	DB_EXEC(SQL, triggerid_tcp_rtt_pfailed_over_10, functionid_tcp_rtt_pfailed_over_10, ">{$RSM.SLV.DNS.TCP.RTT}*0.1",
		"Ratio of failed DNS TCP tests exceeded 10% of allowed $1%", 2, 0);
	DB_EXEC(SQL, triggerid_tcp_rtt_pfailed_over_25, functionid_tcp_rtt_pfailed_over_25, ">{$RSM.SLV.DNS.TCP.RTT}*0.25",
		"Ratio of failed DNS TCP tests exceeded 25% of allowed $1%", 3, 0);
	DB_EXEC(SQL, triggerid_tcp_rtt_pfailed_over_50, functionid_tcp_rtt_pfailed_over_50, ">{$RSM.SLV.DNS.TCP.RTT}*0.5",
		"Ratio of failed DNS TCP tests exceeded 50% of allowed $1%", 3, 0);
	DB_EXEC(SQL, triggerid_tcp_rtt_pfailed_over_75, functionid_tcp_rtt_pfailed_over_75, ">{$RSM.SLV.DNS.TCP.RTT}*0.75",
		"Ratio of failed DNS TCP tests exceeded 75% of allowed $1%", 4, 0);
	DB_EXEC(SQL, triggerid_tcp_rtt_pfailed_over_100, functionid_tcp_rtt_pfailed_over_100, ">{$RSM.SLV.DNS.TCP.RTT}",
		"Ratio of failed DNS TCP tests exceeded 100% of allowed $1%", 5, 0);
	DB_EXEC(SQL, triggerid_udp_rtt_pfailed_over_10, functionid_udp_rtt_pfailed_over_10, ">{$RSM.SLV.DNS.UDP.RTT}*0.1",
		"Ratio of failed DNS UDP tests exceeded 10% of allowed $1%", 2, 0);
	DB_EXEC(SQL, triggerid_udp_rtt_pfailed_over_25, functionid_udp_rtt_pfailed_over_25, ">{$RSM.SLV.DNS.UDP.RTT}*0.25",
		"Ratio of failed DNS UDP tests exceeded 25% of allowed $1%", 3, 0);
	DB_EXEC(SQL, triggerid_udp_rtt_pfailed_over_50, functionid_udp_rtt_pfailed_over_50, ">{$RSM.SLV.DNS.UDP.RTT}*0.5",
		"Ratio of failed DNS UDP tests exceeded 50% of allowed $1%", 3, 0);
	DB_EXEC(SQL, triggerid_udp_rtt_pfailed_over_75, functionid_udp_rtt_pfailed_over_75, ">{$RSM.SLV.DNS.UDP.RTT}*0.75",
		"Ratio of failed DNS UDP tests exceeded 75% of allowed $1%", 4, 0);
	DB_EXEC(SQL, triggerid_udp_rtt_pfailed_over_100, functionid_udp_rtt_pfailed_over_100, ">{$RSM.SLV.DNS.UDP.RTT}",
		"Ratio of failed DNS UDP tests exceeded 100% of allowed $1%", 5, 0);
	DB_EXEC(SQL, triggerid_ns_ip_downtime_over_10, functionid_ns_ip_downtime_over_10, ">{$RSM.SLV.NS.DOWNTIME}*0.1",
		"DNS {#NS} ({#IP}) downtime exceeded 10% of allowed $1 minutes", 2, 2);
	DB_EXEC(SQL, triggerid_ns_ip_downtime_over_25, functionid_ns_ip_downtime_over_25, ">{$RSM.SLV.NS.DOWNTIME}*0.25",
		"DNS {#NS} ({#IP}) downtime exceeded 25% of allowed $1 minutes", 3, 2);
	DB_EXEC(SQL, triggerid_ns_ip_downtime_over_50, functionid_ns_ip_downtime_over_50, ">{$RSM.SLV.NS.DOWNTIME}*0.5",
		"DNS {#NS} ({#IP}) downtime exceeded 50% of allowed $1 minutes", 3, 2);
	DB_EXEC(SQL, triggerid_ns_ip_downtime_over_75, functionid_ns_ip_downtime_over_75, ">{$RSM.SLV.NS.DOWNTIME}*0.75",
		"DNS {#NS} ({#IP}) downtime exceeded 75% of allowed $1 minutes", 4, 2);
	DB_EXEC(SQL, triggerid_ns_ip_downtime_over_100, functionid_ns_ip_downtime_over_100, ">{$RSM.SLV.NS.DOWNTIME}",
		"DNS {#NS} ({#IP}) downtime exceeded 100% of allowed $1 minutes", 5, 2);
#undef SQL

#define SQL	"insert into functions set functionid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64 ",triggerid=" ZBX_FS_UI64 ",name='%s',parameter='%s'"
	DB_EXEC(SQL, functionid_service_down_1          , itemid_rsm_slv_dns_avail            , triggerid_service_down            , "max"  , "#{$RSM.INCIDENT.DNS.FAIL}");
	DB_EXEC(SQL, functionid_service_down_2          , itemid_rsm_slv_dns_avail            , triggerid_service_down            , "count", "#{$RSM.INCIDENT.DNS.RECOVER},0,\"eq\"");
	DB_EXEC(SQL, functionid_downtime_over_100       , itemid_rsm_slv_dns_downtime         , triggerid_downtime_over_100       , "last" , "0");
	DB_EXEC(SQL, functionid_rollweek_over_10        , itemid_rsm_slv_dns_rollweek         , triggerid_rollweek_over_10        , "last" , "0");
	DB_EXEC(SQL, functionid_rollweek_over_25        , itemid_rsm_slv_dns_rollweek         , triggerid_rollweek_over_25        , "last" , "0");
	DB_EXEC(SQL, functionid_rollweek_over_50        , itemid_rsm_slv_dns_rollweek         , triggerid_rollweek_over_50        , "last" , "0");
	DB_EXEC(SQL, functionid_rollweek_over_75        , itemid_rsm_slv_dns_rollweek         , triggerid_rollweek_over_75        , "last" , "0");
	DB_EXEC(SQL, functionid_rollweek_over_100       , itemid_rsm_slv_dns_rollweek         , triggerid_rollweek_over_100       , "last" , "0");
	DB_EXEC(SQL, functionid_tcp_rtt_pfailed_over_10 , itemid_rsm_slv_dns_tcp_rtt_pfailed  , triggerid_tcp_rtt_pfailed_over_10 , "last" , "");
	DB_EXEC(SQL, functionid_tcp_rtt_pfailed_over_25 , itemid_rsm_slv_dns_tcp_rtt_pfailed  , triggerid_tcp_rtt_pfailed_over_25 , "last" , "");
	DB_EXEC(SQL, functionid_tcp_rtt_pfailed_over_50 , itemid_rsm_slv_dns_tcp_rtt_pfailed  , triggerid_tcp_rtt_pfailed_over_50 , "last" , "");
	DB_EXEC(SQL, functionid_tcp_rtt_pfailed_over_75 , itemid_rsm_slv_dns_tcp_rtt_pfailed  , triggerid_tcp_rtt_pfailed_over_75 , "last" , "");
	DB_EXEC(SQL, functionid_tcp_rtt_pfailed_over_100, itemid_rsm_slv_dns_tcp_rtt_pfailed  , triggerid_tcp_rtt_pfailed_over_100, "last" , "");
	DB_EXEC(SQL, functionid_udp_rtt_pfailed_over_10 , itemid_rsm_slv_dns_udp_rtt_pfailed  , triggerid_udp_rtt_pfailed_over_10 , "last" , "");
	DB_EXEC(SQL, functionid_udp_rtt_pfailed_over_25 , itemid_rsm_slv_dns_udp_rtt_pfailed  , triggerid_udp_rtt_pfailed_over_25 , "last" , "");
	DB_EXEC(SQL, functionid_udp_rtt_pfailed_over_50 , itemid_rsm_slv_dns_udp_rtt_pfailed  , triggerid_udp_rtt_pfailed_over_50 , "last" , "");
	DB_EXEC(SQL, functionid_udp_rtt_pfailed_over_75 , itemid_rsm_slv_dns_udp_rtt_pfailed  , triggerid_udp_rtt_pfailed_over_75 , "last" , "");
	DB_EXEC(SQL, functionid_udp_rtt_pfailed_over_100, itemid_rsm_slv_dns_udp_rtt_pfailed  , triggerid_udp_rtt_pfailed_over_100, "last" , "");
	DB_EXEC(SQL, functionid_ns_ip_downtime_over_10  , itemid_rsm_slv_dns_ns_downtime_ns_ip, triggerid_ns_ip_downtime_over_10  , "last" , "");
	DB_EXEC(SQL, functionid_ns_ip_downtime_over_25  , itemid_rsm_slv_dns_ns_downtime_ns_ip, triggerid_ns_ip_downtime_over_25  , "last" , "");
	DB_EXEC(SQL, functionid_ns_ip_downtime_over_50  , itemid_rsm_slv_dns_ns_downtime_ns_ip, triggerid_ns_ip_downtime_over_50  , "last" , "");
	DB_EXEC(SQL, functionid_ns_ip_downtime_over_75  , itemid_rsm_slv_dns_ns_downtime_ns_ip, triggerid_ns_ip_downtime_over_75  , "last" , "");
	DB_EXEC(SQL, functionid_ns_ip_downtime_over_100 , itemid_rsm_slv_dns_ns_downtime_ns_ip, triggerid_ns_ip_downtime_over_100 , "last" , "");
#undef SQL

#define SQL "insert into trigger_depends set triggerdepid=" ZBX_FS_UI64 ",triggerid_down=" ZBX_FS_UI64 ",triggerid_up=" ZBX_FS_UI64
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_rollweek_over_10       , triggerid_rollweek_over_25);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_rollweek_over_25       , triggerid_rollweek_over_50);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_rollweek_over_50       , triggerid_rollweek_over_75);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_rollweek_over_75       , triggerid_rollweek_over_100);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_tcp_rtt_pfailed_over_10, triggerid_tcp_rtt_pfailed_over_25);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_tcp_rtt_pfailed_over_25, triggerid_tcp_rtt_pfailed_over_50);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_tcp_rtt_pfailed_over_50, triggerid_tcp_rtt_pfailed_over_75);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_tcp_rtt_pfailed_over_75, triggerid_tcp_rtt_pfailed_over_100);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_udp_rtt_pfailed_over_10, triggerid_udp_rtt_pfailed_over_25);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_udp_rtt_pfailed_over_25, triggerid_udp_rtt_pfailed_over_50);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_udp_rtt_pfailed_over_50, triggerid_udp_rtt_pfailed_over_75);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_udp_rtt_pfailed_over_75, triggerid_udp_rtt_pfailed_over_100);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_ns_ip_downtime_over_10 , triggerid_ns_ip_downtime_over_25);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_ns_ip_downtime_over_25 , triggerid_ns_ip_downtime_over_50);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_ns_ip_downtime_over_50 , triggerid_ns_ip_downtime_over_75);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_ns_ip_downtime_over_75 , triggerid_ns_ip_downtime_over_100);
#undef SQL

	ret = SUCCEED;
out:
	return ret;
}

/* 4050012, 14 - add "Template DNSSEC Status" template */
static int	DBpatch_4050012_14(void)
{
	int		ret = FAIL;

	zbx_uint64_t	groupid_templates;				/* groupid of "Templates" host group */
	zbx_uint64_t	valuemapid_rsm_service_availability;		/* valuemapid of "RSM Service Availability" value map */

	zbx_uint64_t	hostid;						/* hostid of "Template DNSSEC Status" template */

	zbx_uint64_t	itemid_next;
	zbx_uint64_t	itemid_avail;					/* itemid of ""DNSSEC availability" item */
	zbx_uint64_t	itemid_rollweek;				/* itemid of ""DNSSEC weekly unavailability" item */

	zbx_uint64_t	triggerid_next;
	zbx_uint64_t	triggerid_service_down;				/* triggerid of "DNSSEC service is down" trigger */
	zbx_uint64_t	triggerid_rollweek_over_10;			/* triggerid of "DNSSEC rolling week is over 10%" trigger */
	zbx_uint64_t	triggerid_rollweek_over_25;			/* triggerid of "DNSSEC rolling week is over 25%" trigger */
	zbx_uint64_t	triggerid_rollweek_over_50;			/* triggerid of "DNSSEC rolling week is over 50%" trigger */
	zbx_uint64_t	triggerid_rollweek_over_75;			/* triggerid of "DNSSEC rolling week is over 75%" trigger */
	zbx_uint64_t	triggerid_rollweek_over_100;			/* triggerid of "DNSSEC rolling week is over 100%" trigger */

	zbx_uint64_t	functionid_next;
	zbx_uint64_t	functionid_service_down_1;			/* functionid for "DNSSEC service is down" trigger */
	zbx_uint64_t	functionid_service_down_2;			/* functionid for "DNSSEC service is down" trigger */
	zbx_uint64_t	functionid_rollweek_over_10;			/* functionid for "DNSSEC rolling week is over 10%" trigger */
	zbx_uint64_t	functionid_rollweek_over_25;			/* functionid for "DNSSEC rolling week is over 25%" trigger */
	zbx_uint64_t	functionid_rollweek_over_50;			/* functionid for "DNSSEC rolling week is over 50%" trigger */
	zbx_uint64_t	functionid_rollweek_over_75;			/* functionid for "DNSSEC rolling week is over 75%" trigger */
	zbx_uint64_t	functionid_rollweek_over_100;			/* functionid for "DNSSEC rolling week is over 100%" trigger */

	/* It was decided to setup monitoring from scratch rather than upgrade all the configuration, */
	/* therefore this patch may be obsolete and there's no guarantee that it works correctly.     */
	goto out;

	ONLY_SERVER();

	GET_HOST_GROUP_ID(groupid_templates, "Templates");
	GET_VALUE_MAP_ID(valuemapid_rsm_service_availability, "RSM Service Availability");

	hostid = DBget_maxid_num("hosts", 1);

	itemid_next     = DBget_maxid_num("items", 2);
	itemid_avail    = itemid_next++;
	itemid_rollweek = itemid_next++;

	triggerid_next              = DBget_maxid_num("triggers", 6);
	triggerid_service_down      = triggerid_next++;
	triggerid_rollweek_over_10  = triggerid_next++;
	triggerid_rollweek_over_25  = triggerid_next++;
	triggerid_rollweek_over_50  = triggerid_next++;
	triggerid_rollweek_over_75  = triggerid_next++;
	triggerid_rollweek_over_100 = triggerid_next++;

	functionid_next              = DBget_maxid_num("functions", 7);
	functionid_service_down_1    = functionid_next++;
	functionid_service_down_2    = functionid_next++;
	functionid_rollweek_over_10  = functionid_next++;
	functionid_rollweek_over_25  = functionid_next++;
	functionid_rollweek_over_50  = functionid_next++;
	functionid_rollweek_over_75  = functionid_next++;
	functionid_rollweek_over_100 = functionid_next++;

	/* status 3 = HOST_STATUS_TEMPLATE */
	DB_EXEC("insert into hosts set hostid=" ZBX_FS_UI64 ",created=0,proxy_hostid=NULL,host='%s',status=%d,"
			"disable_until=0,error='',available=0,errors_from=0,lastaccess=0,ipmi_authtype=-1,"
			"ipmi_privilege=2,ipmi_username='',ipmi_password='',ipmi_disable_until=0,ipmi_available=0,"
			"snmp_disable_until=0,snmp_available=0,maintenanceid=NULL,maintenance_status=0,"
			"maintenance_type=0,maintenance_from=0,ipmi_errors_from=0,snmp_errors_from=0,ipmi_error='',"
			"snmp_error='',jmx_disable_until=0,jmx_available=0,jmx_errors_from=0,jmx_error='',name='%s',"
			"info_1='',info_2='',flags=0,templateid=NULL,description='%s',tls_connect=1,tls_accept=1,"
			"tls_issuer='',tls_subject='',tls_psk_identity='',tls_psk='',proxy_address='',auto_compress=1",
		hostid, "Template DNSSEC Status", 3, "Template DNSSEC Status",
		"DNSSEC SLV items and triggers for linking to <RSMHOST> hosts");

	DB_EXEC("insert into hosts_groups set hostgroupid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ",groupid=" ZBX_FS_UI64,
		DBget_maxid_num("hosts_groups", 1), hostid, groupid_templates);

#define SQL	"insert into items set itemid=" ZBX_FS_UI64 ",type=%d,snmp_community='',snmp_oid='',"			\
			"hostid=" ZBX_FS_UI64 ",name='%s',key_='%s',delay='0',history='90d',trends='365d',status=0,"	\
			"value_type=%d,trapper_hosts='',units='%s',snmpv3_securityname='',snmpv3_securitylevel=0,"	\
			"snmpv3_authpassphrase='',snmpv3_privpassphrase='',formula='',logtimefmt='',templateid=NULL,"	\
			"valuemapid=nullif(" ZBX_FS_UI64 ",0),params='',ipmi_sensor='',authtype=0,username='',"		\
			"password='',publickey='',privatekey='',flags=0,interfaceid=NULL,port='',description='',"	\
			"inventory_link=0,lifetime='30d',snmpv3_authprotocol=0,snmpv3_privprotocol=0,"			\
			"snmpv3_contextname='',evaltype=0,jmx_endpoint='',master_itemid=NULL,timeout='3s',url='',"	\
			"query_fields='',posts='',status_codes='200',follow_redirects=1,post_type=0,http_proxy='',"	\
			"headers='',retrieve_mode=0,request_method=0,output_format=0,ssl_cert_file='',ssl_key_file='',"	\
			"ssl_key_password='',verify_peer=0,verify_host=0,allow_traps=0"
	/* type 2 = ITEM_TYPE_TRAPPER */
	/* value_type 0 = ITEM_VALUE_TYPE_FLOAT */
	/* value_type 3 = ITEM_VALUE_TYPE_UINT64 */
	DB_EXEC(SQL, itemid_avail, 2, hostid, "DNSSEC availability", "rsm.slv.dnssec.avail", 3, "" , valuemapid_rsm_service_availability);
	DB_EXEC(SQL, itemid_rollweek, 2, hostid, "DNSSEC weekly unavailability", "rsm.slv.dnssec.rollweek", 0, "%", (zbx_uint64_t)0);
#undef SQL

	/* priority 0 = TRIGGER_SEVERITY_NOT_CLASSIFIED */
	DB_EXEC("insert into triggers set triggerid=" ZBX_FS_UI64 ","
			"expression='({TRIGGER.VALUE}=0 and {" ZBX_FS_UI64 "}=0) or ({TRIGGER.VALUE}=1 and {" ZBX_FS_UI64 "}>0)',"
			"description='%s',url='',status=0,value=0,priority=%d,lastchange=0,comments='',error='',"
			"templateid=NULL,type=0,state=0,flags=0,recovery_mode=0,recovery_expression='',"
			"correlation_mode=0,correlation_tag='',manual_close=0,opdata=''",
		triggerid_service_down, functionid_service_down_1, functionid_service_down_2, "DNSSEC service is down", 0);

#define SQL	"insert into triggers set triggerid=" ZBX_FS_UI64 ",expression='{" ZBX_FS_UI64 "}%s',description='%s',"	\
			"url='',status=0,value=0,priority=%d,lastchange=0,comments='',error='',templateid=NULL,type=0,"	\
			"state=0,flags=0,recovery_mode=0,recovery_expression='',correlation_mode=0,correlation_tag='',"	\
			"manual_close=0,opdata=''"
	/* priority 2 = TRIGGER_SEVERITY_WARNING */
	/* priority 3 = TRIGGER_SEVERITY_AVERAGE */
	/* priority 4 = TRIGGER_SEVERITY_HIGH */
	/* priority 5 = TRIGGER_SEVERITY_DISASTER */
	DB_EXEC(SQL, triggerid_rollweek_over_10 , functionid_rollweek_over_10 , ">=10" , "DNSSEC rolling week is over 10%" , 2);
	DB_EXEC(SQL, triggerid_rollweek_over_25 , functionid_rollweek_over_25 , ">=25" , "DNSSEC rolling week is over 25%" , 3);
	DB_EXEC(SQL, triggerid_rollweek_over_50 , functionid_rollweek_over_50 , ">=50" , "DNSSEC rolling week is over 50%" , 3);
	DB_EXEC(SQL, triggerid_rollweek_over_75 , functionid_rollweek_over_75 , ">=75" , "DNSSEC rolling week is over 75%" , 4);
	DB_EXEC(SQL, triggerid_rollweek_over_100, functionid_rollweek_over_100, ">=100", "DNSSEC rolling week is over 100%", 5);
#undef SQL

#define SQL	"insert into functions set functionid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64 ",triggerid=" ZBX_FS_UI64 ",name='%s',parameter='%s'"
	DB_EXEC(SQL, functionid_service_down_1   , itemid_avail   , triggerid_service_down     , "max"  , "#{$RSM.INCIDENT.DNSSEC.FAIL}");
	DB_EXEC(SQL, functionid_service_down_2   , itemid_avail   , triggerid_service_down     , "count", "#{$RSM.INCIDENT.DNSSEC.RECOVER},0,\"eq\"");
	DB_EXEC(SQL, functionid_rollweek_over_10 , itemid_rollweek, triggerid_rollweek_over_10 , "last" , "0");
	DB_EXEC(SQL, functionid_rollweek_over_25 , itemid_rollweek, triggerid_rollweek_over_25 , "last" , "0");
	DB_EXEC(SQL, functionid_rollweek_over_50 , itemid_rollweek, triggerid_rollweek_over_50 , "last" , "0");
	DB_EXEC(SQL, functionid_rollweek_over_75 , itemid_rollweek, triggerid_rollweek_over_75 , "last" , "0");
	DB_EXEC(SQL, functionid_rollweek_over_100, itemid_rollweek, triggerid_rollweek_over_100, "last" , "0");
#undef SQL

#define SQL	"insert into trigger_depends set triggerdepid=" ZBX_FS_UI64 ",triggerid_down=" ZBX_FS_UI64 ",triggerid_up=" ZBX_FS_UI64
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_rollweek_over_10, triggerid_rollweek_over_25);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_rollweek_over_25, triggerid_rollweek_over_50);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_rollweek_over_50, triggerid_rollweek_over_75);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_rollweek_over_75, triggerid_rollweek_over_100);
#undef SQL

	ret = SUCCEED;
out:
	return ret;
}

/* 4050012, 15 - add "Template RDAP Status" template */
static int	DBpatch_4050012_15(void)
{
	int		ret = FAIL;

	zbx_uint64_t	groupid_templates;				/* groupid of "Templates" host group */
	zbx_uint64_t	valuemapid_rsm_service_availability;		/* valuemapid of "RSM Service Availability" value map */

	zbx_uint64_t	hostid;						/* hostid of "Template RDAP Status" template */

	zbx_uint64_t	itemid_next;
	zbx_uint64_t	itemid_avail;					/* itemid of "RDAP availability" item */
	zbx_uint64_t	itemid_downtime;				/* itemid of "RDAP minutes of downtime" item */
	zbx_uint64_t	itemid_rollweek;				/* itemid of "RDAP weekly unavailability" item */
	zbx_uint64_t	itemid_rtt_failed;				/* itemid of "Number of failed monthly RDAP queries" item */
	zbx_uint64_t	itemid_rtt_performed;				/* itemid of "Number of performed monthly RDAP queries" item */
	zbx_uint64_t	itemid_rtt_pfailed;				/* itemid of "Ratio of failed monthly RDAP queries" item */

	zbx_uint64_t	triggerid_next;
	zbx_uint64_t	triggerid_service_down;				/* triggerid of "RDAP service is down" trigger */
	zbx_uint64_t	triggerid_downtime_over_10;			/* triggerid of "RDAP service was unavailable for 10% of allowed $1 minutes" trigger */
	zbx_uint64_t	triggerid_downtime_over_25;			/* triggerid of "RDAP service was unavailable for 25% of allowed $1 minutes" trigger */
	zbx_uint64_t	triggerid_downtime_over_50;			/* triggerid of "RDAP service was unavailable for 50% of allowed $1 minutes" trigger */
	zbx_uint64_t	triggerid_downtime_over_75;			/* triggerid of "RDAP service was unavailable for 75% of allowed $1 minutes" trigger */
	zbx_uint64_t	triggerid_downtime_over_100;			/* triggerid of "RDAP service was unavailable for 100% of allowed $1 minutes" trigger */
	zbx_uint64_t	triggerid_rollweek_over_10;			/* triggerid of "RDAP rolling week is over 10%" trigger */
	zbx_uint64_t	triggerid_rollweek_over_25;			/* triggerid of "RDAP rolling week is over 25%" trigger */
	zbx_uint64_t	triggerid_rollweek_over_50;			/* triggerid of "RDAP rolling week is over 50%" trigger */
	zbx_uint64_t	triggerid_rollweek_over_75;			/* triggerid of "RDAP rolling week is over 75%" trigger */
	zbx_uint64_t	triggerid_rollweek_over_100;			/* triggerid of "RDAP rolling week is over 100%" trigger */
	zbx_uint64_t	triggerid_rtt_pfailed_over_10;			/* triggerid of "Ratio of failed RDAP tests exceeded 10% of allowed $1%" trigger */
	zbx_uint64_t	triggerid_rtt_pfailed_over_25;			/* triggerid of "Ratio of failed RDAP tests exceeded 25% of allowed $1%" trigger */
	zbx_uint64_t	triggerid_rtt_pfailed_over_50;			/* triggerid of "Ratio of failed RDAP tests exceeded 50% of allowed $1%" trigger */
	zbx_uint64_t	triggerid_rtt_pfailed_over_75;			/* triggerid of "Ratio of failed RDAP tests exceeded 75% of allowed $1%" trigger */
	zbx_uint64_t	triggerid_rtt_pfailed_over_100;			/* triggerid of "Ratio of failed RDAP tests exceeded 100% of allowed $1%" trigger */

	zbx_uint64_t	functionid_next;
	zbx_uint64_t	functionid_service_down_1;			/* functionid for "RDAP service is down" trigger */
	zbx_uint64_t	functionid_service_down_2;			/* functionid for "RDAP service is down" trigger */
	zbx_uint64_t	functionid_downtime_over_10;			/* functionid for "RDAP service was unavailable for 10% of allowed $1 minutes" trigger */
	zbx_uint64_t	functionid_downtime_over_25;			/* functionid for "RDAP service was unavailable for 25% of allowed $1 minutes" trigger */
	zbx_uint64_t	functionid_downtime_over_50;			/* functionid for "RDAP service was unavailable for 50% of allowed $1 minutes" trigger */
	zbx_uint64_t	functionid_downtime_over_75;			/* functionid for "RDAP service was unavailable for 75% of allowed $1 minutes" trigger */
	zbx_uint64_t	functionid_downtime_over_100;			/* functionid for "RDAP service was unavailable for 100% of allowed $1 minutes" trigger */
	zbx_uint64_t	functionid_rollweek_over_10;			/* functionid for "RDAP rolling week is over 10%" trigger */
	zbx_uint64_t	functionid_rollweek_over_25;			/* functionid for "RDAP rolling week is over 25%" trigger */
	zbx_uint64_t	functionid_rollweek_over_50;			/* functionid for "RDAP rolling week is over 50%" trigger */
	zbx_uint64_t	functionid_rollweek_over_75;			/* functionid for "RDAP rolling week is over 75%" trigger */
	zbx_uint64_t	functionid_rollweek_over_100;			/* functionid for "RDAP rolling week is over 100%" trigger */
	zbx_uint64_t	functionid_rtt_pfailed_over_10;			/* functionid for "Ratio of failed RDAP tests exceeded 10% of allowed $1%" trigger */
	zbx_uint64_t	functionid_rtt_pfailed_over_25;			/* functionid for "Ratio of failed RDAP tests exceeded 25% of allowed $1%" trigger */
	zbx_uint64_t	functionid_rtt_pfailed_over_50;			/* functionid for "Ratio of failed RDAP tests exceeded 50% of allowed $1%" trigger */
	zbx_uint64_t	functionid_rtt_pfailed_over_75;			/* functionid for "Ratio of failed RDAP tests exceeded 75% of allowed $1%" trigger */
	zbx_uint64_t	functionid_rtt_pfailed_over_100;		/* functionid for "Ratio of failed RDAP tests exceeded 100% of allowed $1%" trigger */

	/* It was decided to setup monitoring from scratch rather than upgrade all the configuration, */
	/* therefore this patch may be obsolete and there's no guarantee that it works correctly.     */
	goto out;

	ONLY_SERVER();

	GET_HOST_GROUP_ID(groupid_templates, "Templates");
	GET_VALUE_MAP_ID(valuemapid_rsm_service_availability, "RSM Service Availability");

	hostid = DBget_maxid_num("hosts", 1);

	itemid_next          = DBget_maxid_num("items", 6);
	itemid_avail         = itemid_next++;
	itemid_downtime      = itemid_next++;
	itemid_rollweek      = itemid_next++;
	itemid_rtt_failed    = itemid_next++;
	itemid_rtt_performed = itemid_next++;
	itemid_rtt_pfailed   = itemid_next++;

	triggerid_next                 = DBget_maxid_num("triggers", 16);
	triggerid_service_down         = triggerid_next++;
	triggerid_downtime_over_10     = triggerid_next++;
	triggerid_downtime_over_25     = triggerid_next++;
	triggerid_downtime_over_50     = triggerid_next++;
	triggerid_downtime_over_75     = triggerid_next++;
	triggerid_downtime_over_100    = triggerid_next++;
	triggerid_rollweek_over_10     = triggerid_next++;
	triggerid_rollweek_over_25     = triggerid_next++;
	triggerid_rollweek_over_50     = triggerid_next++;
	triggerid_rollweek_over_75     = triggerid_next++;
	triggerid_rollweek_over_100    = triggerid_next++;
	triggerid_rtt_pfailed_over_10  = triggerid_next++;
	triggerid_rtt_pfailed_over_25  = triggerid_next++;
	triggerid_rtt_pfailed_over_50  = triggerid_next++;
	triggerid_rtt_pfailed_over_75  = triggerid_next++;
	triggerid_rtt_pfailed_over_100 = triggerid_next++;

	functionid_next                 = DBget_maxid_num("functions", 17);
	functionid_service_down_1       = functionid_next++;
	functionid_service_down_2       = functionid_next++;
	functionid_downtime_over_10     = functionid_next++;
	functionid_downtime_over_25     = functionid_next++;
	functionid_downtime_over_50     = functionid_next++;
	functionid_downtime_over_75     = functionid_next++;
	functionid_downtime_over_100    = functionid_next++;
	functionid_rollweek_over_10     = functionid_next++;
	functionid_rollweek_over_25     = functionid_next++;
	functionid_rollweek_over_50     = functionid_next++;
	functionid_rollweek_over_75     = functionid_next++;
	functionid_rollweek_over_100    = functionid_next++;
	functionid_rtt_pfailed_over_10  = functionid_next++;
	functionid_rtt_pfailed_over_25  = functionid_next++;
	functionid_rtt_pfailed_over_50  = functionid_next++;
	functionid_rtt_pfailed_over_75  = functionid_next++;
	functionid_rtt_pfailed_over_100 = functionid_next++;

	/* status 3 = HOST_STATUS_TEMPLATE */
	DB_EXEC("insert into hosts set hostid=" ZBX_FS_UI64 ",created=0,proxy_hostid=NULL,host='%s',status=%d,"
			"disable_until=0,error='',available=0,errors_from=0,lastaccess=0,ipmi_authtype=-1,"
			"ipmi_privilege=2,ipmi_username='',ipmi_password='',ipmi_disable_until=0,ipmi_available=0,"
			"snmp_disable_until=0,snmp_available=0,maintenanceid=NULL,maintenance_status=0,"
			"maintenance_type=0,maintenance_from=0,ipmi_errors_from=0,snmp_errors_from=0,ipmi_error='',"
			"snmp_error='',jmx_disable_until=0,jmx_available=0,jmx_errors_from=0,jmx_error='',name='%s',"
			"info_1='',info_2='',flags=0,templateid=NULL,description='%s',tls_connect=1,tls_accept=1,"
			"tls_issuer='',tls_subject='',tls_psk_identity='',tls_psk='',proxy_address='',auto_compress=1",
		hostid, "Template RDAP Status", 3, "Template RDAP Status",
		"RDAP SLV items and triggers for linking to <RSMHOST> hosts");

	DB_EXEC("insert into hosts_groups set hostgroupid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ",groupid=" ZBX_FS_UI64,
		DBget_maxid_num("hosts_groups", 1), hostid, groupid_templates);

#define SQL	"insert into items set itemid=" ZBX_FS_UI64 ",type=%d,snmp_community='',snmp_oid='',"			\
			"hostid=" ZBX_FS_UI64 ",name='%s',key_='%s',delay='0',history='90d',trends='365d',status=0,"	\
			"value_type=%d,trapper_hosts='',units='%s',snmpv3_securityname='',snmpv3_securitylevel=0,"	\
			"snmpv3_authpassphrase='',snmpv3_privpassphrase='',formula='',logtimefmt='',templateid=NULL,"	\
			"valuemapid=nullif(" ZBX_FS_UI64 ",0),params='',ipmi_sensor='',authtype=0,username='',"		\
			"password='',publickey='',privatekey='',flags=0,interfaceid=NULL,port='',description='',"	\
			"inventory_link=0,lifetime='30d',snmpv3_authprotocol=0,snmpv3_privprotocol=0,"			\
			"snmpv3_contextname='',evaltype=0,jmx_endpoint='',master_itemid=NULL,timeout='3s',url='',"	\
			"query_fields='',posts='',status_codes='200',follow_redirects=1,post_type=0,http_proxy='',"	\
			"headers='',retrieve_mode=0,request_method=0,output_format=0,ssl_cert_file='',ssl_key_file='',"	\
			"ssl_key_password='',verify_peer=0,verify_host=0,allow_traps=0"
	/* type 2 = ITEM_TYPE_TRAPPER */
	/* value_type 0 = ITEM_VALUE_TYPE_FLOAT */
	/* value_type 3 = ITEM_VALUE_TYPE_UINT64 */
	DB_EXEC(SQL, itemid_avail, 2, hostid, "RDAP availability", "rsm.slv.rdap.avail", 3, "" , valuemapid_rsm_service_availability);
	DB_EXEC(SQL, itemid_downtime, 2, hostid, "RDAP minutes of downtime", "rsm.slv.rdap.downtime", 3, "" , (zbx_uint64_t)0);
	DB_EXEC(SQL, itemid_rollweek, 2, hostid, "RDAP weekly unavailability", "rsm.slv.rdap.rollweek", 0, "%", (zbx_uint64_t)0);
	DB_EXEC(SQL, itemid_rtt_failed, 2, hostid, "Number of failed monthly RDAP queries", "rsm.slv.rdap.rtt.failed", 3, "" , (zbx_uint64_t)0);
	DB_EXEC(SQL, itemid_rtt_performed, 2, hostid, "Number of performed monthly RDAP queries", "rsm.slv.rdap.rtt.performed", 3, "" , (zbx_uint64_t)0);
	DB_EXEC(SQL, itemid_rtt_pfailed, 2, hostid, "Ratio of failed monthly RDAP queries", "rsm.slv.rdap.rtt.pfailed", 0, "%", (zbx_uint64_t)0);
#undef SQL

	/* priority 0 = TRIGGER_SEVERITY_NOT_CLASSIFIED */
	DB_EXEC("insert into triggers set triggerid=" ZBX_FS_UI64 ","
			"expression='({TRIGGER.VALUE}=0 and {" ZBX_FS_UI64 "}=0) or ({TRIGGER.VALUE}=1 and {" ZBX_FS_UI64 "}>0)',"
			"description='%s',url='',status=0,value=0,priority=%d,lastchange=0,comments='',error='',"
			"templateid=NULL,type=0,state=0,flags=0,recovery_mode=0,recovery_expression='',"
			"correlation_mode=0,correlation_tag='',manual_close=0,opdata=''",
		triggerid_service_down, functionid_service_down_1, functionid_service_down_2, "RDAP service is down", 0);

#define SQL	"insert into triggers set triggerid=" ZBX_FS_UI64 ",expression='{" ZBX_FS_UI64 "}%s',description='%s',"	\
			"url='',status=0,value=0,priority=%d,lastchange=0,comments='',error='',templateid=NULL,type=0,"	\
			"state=0,flags=0,recovery_mode=0,recovery_expression='',correlation_mode=0,correlation_tag='',"	\
			"manual_close=0,opdata=''"
	/* priority 2 = TRIGGER_SEVERITY_WARNING */
	/* priority 3 = TRIGGER_SEVERITY_AVERAGE */
	/* priority 4 = TRIGGER_SEVERITY_HIGH */
	/* priority 5 = TRIGGER_SEVERITY_DISASTER */
	DB_EXEC(SQL, triggerid_downtime_over_10, functionid_downtime_over_10, ">={$RSM.SLV.RDAP.DOWNTIME}*0.1",
		"RDAP service was unavailable for 10% of allowed $1 minutes", 2);
	DB_EXEC(SQL, triggerid_downtime_over_25, functionid_downtime_over_25, ">={$RSM.SLV.RDAP.DOWNTIME}*0.25",
		"RDAP service was unavailable for 25% of allowed $1 minutes", 3);
	DB_EXEC(SQL, triggerid_downtime_over_50, functionid_downtime_over_50, ">={$RSM.SLV.RDAP.DOWNTIME}*0.5",
		"RDAP service was unavailable for 50% of allowed $1 minutes", 3);
	DB_EXEC(SQL, triggerid_downtime_over_75, functionid_downtime_over_75, ">={$RSM.SLV.RDAP.DOWNTIME}*0.75",
		"RDAP service was unavailable for 75% of allowed $1 minutes", 4);
	DB_EXEC(SQL, triggerid_downtime_over_100, functionid_downtime_over_100, ">={$RSM.SLV.RDAP.DOWNTIME}",
		"RDAP service was unavailable for 100% of allowed $1 minutes", 5);
	DB_EXEC(SQL, triggerid_rollweek_over_10, functionid_rollweek_over_10, ">=10",
		"RDAP rolling week is over 10%", 2);
	DB_EXEC(SQL, triggerid_rollweek_over_25, functionid_rollweek_over_25, ">=25",
		"RDAP rolling week is over 25%", 3);
	DB_EXEC(SQL, triggerid_rollweek_over_50, functionid_rollweek_over_50, ">=50",
		"RDAP rolling week is over 50%", 3);
	DB_EXEC(SQL, triggerid_rollweek_over_75, functionid_rollweek_over_75, ">=75",
		"RDAP rolling week is over 75%", 4);
	DB_EXEC(SQL, triggerid_rollweek_over_100, functionid_rollweek_over_100, ">=100",
		"RDAP rolling week is over 100%", 5);
	DB_EXEC(SQL, triggerid_rtt_pfailed_over_10, functionid_rtt_pfailed_over_10, ">{$RSM.SLV.RDAP.RTT}*0.1",
		"Ratio of failed RDAP tests exceeded 10% of allowed $1%", 2);
	DB_EXEC(SQL, triggerid_rtt_pfailed_over_25, functionid_rtt_pfailed_over_25, ">{$RSM.SLV.RDAP.RTT}*0.25",
		"Ratio of failed RDAP tests exceeded 25% of allowed $1%", 3);
	DB_EXEC(SQL, triggerid_rtt_pfailed_over_50, functionid_rtt_pfailed_over_50, ">{$RSM.SLV.RDAP.RTT}*0.5",
		"Ratio of failed RDAP tests exceeded 50% of allowed $1%", 3);
	DB_EXEC(SQL, triggerid_rtt_pfailed_over_75, functionid_rtt_pfailed_over_75, ">{$RSM.SLV.RDAP.RTT}*0.75",
		"Ratio of failed RDAP tests exceeded 75% of allowed $1%", 4);
	DB_EXEC(SQL, triggerid_rtt_pfailed_over_100, functionid_rtt_pfailed_over_100, ">{$RSM.SLV.RDAP.RTT}",
		"Ratio of failed RDAP tests exceeded 100% of allowed $1%", 5);
#undef SQL

#define SQL	"insert into functions set functionid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64 ",triggerid=" ZBX_FS_UI64 ",name='%s',parameter='%s'"
	DB_EXEC(SQL, functionid_service_down_1      , itemid_avail      , triggerid_service_down        , "max"  , "#{$RSM.INCIDENT.RDAP.FAIL}");
	DB_EXEC(SQL, functionid_service_down_2      , itemid_avail      , triggerid_service_down        , "count", "#{$RSM.INCIDENT.RDAP.RECOVER},0,\"eq\"");
	DB_EXEC(SQL, functionid_downtime_over_10    , itemid_downtime   , triggerid_downtime_over_10    , "last" , "0");
	DB_EXEC(SQL, functionid_downtime_over_25    , itemid_downtime   , triggerid_downtime_over_25    , "last" , "0");
	DB_EXEC(SQL, functionid_downtime_over_50    , itemid_downtime   , triggerid_downtime_over_50    , "last" , "0");
	DB_EXEC(SQL, functionid_downtime_over_75    , itemid_downtime   , triggerid_downtime_over_75    , "last" , "0");
	DB_EXEC(SQL, functionid_downtime_over_100   , itemid_downtime   , triggerid_downtime_over_100   , "last" , "0");
	DB_EXEC(SQL, functionid_rollweek_over_10    , itemid_rollweek   , triggerid_rollweek_over_10    , "last" , "0");
	DB_EXEC(SQL, functionid_rollweek_over_25    , itemid_rollweek   , triggerid_rollweek_over_25    , "last" , "0");
	DB_EXEC(SQL, functionid_rollweek_over_50    , itemid_rollweek   , triggerid_rollweek_over_50    , "last" , "0");
	DB_EXEC(SQL, functionid_rollweek_over_75    , itemid_rollweek   , triggerid_rollweek_over_75    , "last" , "0");
	DB_EXEC(SQL, functionid_rollweek_over_100   , itemid_rollweek   , triggerid_rollweek_over_100   , "last" , "0");
	DB_EXEC(SQL, functionid_rtt_pfailed_over_10 , itemid_rtt_pfailed, triggerid_rtt_pfailed_over_10 , "last" , "");
	DB_EXEC(SQL, functionid_rtt_pfailed_over_25 , itemid_rtt_pfailed, triggerid_rtt_pfailed_over_25 , "last" , "");
	DB_EXEC(SQL, functionid_rtt_pfailed_over_50 , itemid_rtt_pfailed, triggerid_rtt_pfailed_over_50 , "last" , "");
	DB_EXEC(SQL, functionid_rtt_pfailed_over_75 , itemid_rtt_pfailed, triggerid_rtt_pfailed_over_75 , "last" , "");
	DB_EXEC(SQL, functionid_rtt_pfailed_over_100, itemid_rtt_pfailed, triggerid_rtt_pfailed_over_100, "last" , "");
#undef SQL

#define SQL	"insert into trigger_depends set triggerdepid=" ZBX_FS_UI64 ",triggerid_down=" ZBX_FS_UI64 ",triggerid_up=" ZBX_FS_UI64
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_downtime_over_10   , triggerid_downtime_over_25);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_downtime_over_25   , triggerid_downtime_over_50);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_downtime_over_50   , triggerid_downtime_over_75);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_downtime_over_75   , triggerid_downtime_over_100);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_rollweek_over_10   , triggerid_rollweek_over_25);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_rollweek_over_25   , triggerid_rollweek_over_50);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_rollweek_over_50   , triggerid_rollweek_over_75);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_rollweek_over_75   , triggerid_rollweek_over_100);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_rtt_pfailed_over_10, triggerid_rtt_pfailed_over_25);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_rtt_pfailed_over_25, triggerid_rtt_pfailed_over_50);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_rtt_pfailed_over_50, triggerid_rtt_pfailed_over_75);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_rtt_pfailed_over_75, triggerid_rtt_pfailed_over_100);
#undef SQL

	ret = SUCCEED;
out:
	return ret;
}

/* 4050012, 16 - add "Template RDDS Status" template */
static int	DBpatch_4050012_16(void)
{
	int		ret = FAIL;

	zbx_uint64_t	groupid_templates;				/* groupid of "Templates" host group */
	zbx_uint64_t	valuemapid_rsm_service_availability;		/* valuemapid of "RSM Service Availability" value map */

	zbx_uint64_t	hostid;						/* hostid of "Template RDDS Status" template */

	zbx_uint64_t	itemid_next;
	zbx_uint64_t	itemid_avail;					/* itemid of "RDDS availability" item */
	zbx_uint64_t	itemid_downtime;				/* itemid of "RDDS minutes of downtime" item */
	zbx_uint64_t	itemid_rollweek;				/* itemid of "RDDS weekly unavailability" item */
	zbx_uint64_t	itemid_rtt_failed;				/* itemid of "Number of failed monthly RDDS queries" item */
	zbx_uint64_t	itemid_rtt_performed;				/* itemid of "Number of performed monthly RDDS queries" item */
	zbx_uint64_t	itemid_rtt_pfailed;				/* itemid of "Ratio of failed monthly RDDS queries" item */

	zbx_uint64_t	triggerid_next;
	zbx_uint64_t	triggerid_service_down;				/* triggerid of "RDDS service is down" trigger */
	zbx_uint64_t	triggerid_downtime_over_10;			/* triggerid of "RDDS service was unavailable for 10% of allowed $1 minutes" trigger */
	zbx_uint64_t	triggerid_downtime_over_25;			/* triggerid of "RDDS service was unavailable for 25% of allowed $1 minutes" trigger */
	zbx_uint64_t	triggerid_downtime_over_50;			/* triggerid of "RDDS service was unavailable for 50% of allowed $1 minutes" trigger */
	zbx_uint64_t	triggerid_downtime_over_75;			/* triggerid of "RDDS service was unavailable for 75% of allowed $1 minutes" trigger */
	zbx_uint64_t	triggerid_downtime_over_100;			/* triggerid of "RDDS service was unavailable for 100% of allowed $1 minutes" trigger */
	zbx_uint64_t	triggerid_rollweek_over_10;			/* triggerid of "RDDS rolling week is over 10%" trigger */
	zbx_uint64_t	triggerid_rollweek_over_25;			/* triggerid of "RDDS rolling week is over 25%" trigger */
	zbx_uint64_t	triggerid_rollweek_over_50;			/* triggerid of "RDDS rolling week is over 50%" trigger */
	zbx_uint64_t	triggerid_rollweek_over_75;			/* triggerid of "RDDS rolling week is over 75%" trigger */
	zbx_uint64_t	triggerid_rollweek_over_100;			/* triggerid of "RDDS rolling week is over 100%" trigger */
	zbx_uint64_t	triggerid_rtt_pfailed_over_10;			/* triggerid of "Ratio of failed RDDS tests exceeded 10% of allowed $1%" trigger */
	zbx_uint64_t	triggerid_rtt_pfailed_over_25;			/* triggerid of "Ratio of failed RDDS tests exceeded 25% of allowed $1%" trigger */
	zbx_uint64_t	triggerid_rtt_pfailed_over_50;			/* triggerid of "Ratio of failed RDDS tests exceeded 50% of allowed $1%" trigger */
	zbx_uint64_t	triggerid_rtt_pfailed_over_75;			/* triggerid of "Ratio of failed RDDS tests exceeded 75% of allowed $1%" trigger */
	zbx_uint64_t	triggerid_rtt_pfailed_over_100;			/* triggerid of "Ratio of failed RDDS tests exceeded 100% of allowed $1%" trigger */

	zbx_uint64_t	functionid_next;
	zbx_uint64_t	functionid_service_down_1;			/* functionid for "RDDS service is down" trigger */
	zbx_uint64_t	functionid_service_down_2;			/* functionid for "RDDS service is down" trigger */
	zbx_uint64_t	functionid_downtime_over_10;			/* functionid for "RDDS service was unavailable for 10% of allowed $1 minutes" trigger */
	zbx_uint64_t	functionid_downtime_over_25;			/* functionid for "RDDS service was unavailable for 25% of allowed $1 minutes" trigger */
	zbx_uint64_t	functionid_downtime_over_50;			/* functionid for "RDDS service was unavailable for 50% of allowed $1 minutes" trigger */
	zbx_uint64_t	functionid_downtime_over_75;			/* functionid for "RDDS service was unavailable for 75% of allowed $1 minutes" trigger */
	zbx_uint64_t	functionid_downtime_over_100;			/* functionid for "RDDS service was unavailable for 100% of allowed $1 minutes" trigger */
	zbx_uint64_t	functionid_rollweek_over_10;			/* functionid for "RDDS rolling week is over 10%" trigger */
	zbx_uint64_t	functionid_rollweek_over_25;			/* functionid for "RDDS rolling week is over 25%" trigger */
	zbx_uint64_t	functionid_rollweek_over_50;			/* functionid for "RDDS rolling week is over 50%" trigger */
	zbx_uint64_t	functionid_rollweek_over_75;			/* functionid for "RDDS rolling week is over 75%" trigger */
	zbx_uint64_t	functionid_rollweek_over_100;			/* functionid for "RDDS rolling week is over 100%" trigger */
	zbx_uint64_t	functionid_rtt_pfailed_over_10;			/* functionid for "Ratio of failed RDDS tests exceeded 10% of allowed $1%" trigger */
	zbx_uint64_t	functionid_rtt_pfailed_over_25;			/* functionid for "Ratio of failed RDDS tests exceeded 25% of allowed $1%" trigger */
	zbx_uint64_t	functionid_rtt_pfailed_over_50;			/* functionid for "Ratio of failed RDDS tests exceeded 50% of allowed $1%" trigger */
	zbx_uint64_t	functionid_rtt_pfailed_over_75;			/* functionid for "Ratio of failed RDDS tests exceeded 75% of allowed $1%" trigger */
	zbx_uint64_t	functionid_rtt_pfailed_over_100;		/* functionid for "Ratio of failed RDDS tests exceeded 100% of allowed $1%" trigger */

	/* It was decided to setup monitoring from scratch rather than upgrade all the configuration, */
	/* therefore this patch may be obsolete and there's no guarantee that it works correctly.     */
	goto out;

	ONLY_SERVER();

	GET_HOST_GROUP_ID(groupid_templates, "Templates");
	GET_VALUE_MAP_ID(valuemapid_rsm_service_availability, "RSM Service Availability");

	hostid = DBget_maxid_num("hosts", 1);

	itemid_next          = DBget_maxid_num("items", 6);
	itemid_avail         = itemid_next++;
	itemid_downtime      = itemid_next++;
	itemid_rollweek      = itemid_next++;
	itemid_rtt_failed    = itemid_next++;
	itemid_rtt_performed = itemid_next++;
	itemid_rtt_pfailed   = itemid_next++;

	triggerid_next                 = DBget_maxid_num("triggers", 16);
	triggerid_service_down         = triggerid_next++;
	triggerid_downtime_over_10     = triggerid_next++;
	triggerid_downtime_over_25     = triggerid_next++;
	triggerid_downtime_over_50     = triggerid_next++;
	triggerid_downtime_over_75     = triggerid_next++;
	triggerid_downtime_over_100    = triggerid_next++;
	triggerid_rollweek_over_10     = triggerid_next++;
	triggerid_rollweek_over_25     = triggerid_next++;
	triggerid_rollweek_over_50     = triggerid_next++;
	triggerid_rollweek_over_75     = triggerid_next++;
	triggerid_rollweek_over_100    = triggerid_next++;
	triggerid_rtt_pfailed_over_10  = triggerid_next++;
	triggerid_rtt_pfailed_over_25  = triggerid_next++;
	triggerid_rtt_pfailed_over_50  = triggerid_next++;
	triggerid_rtt_pfailed_over_75  = triggerid_next++;
	triggerid_rtt_pfailed_over_100 = triggerid_next++;

	functionid_next                 = DBget_maxid_num("functions", 17);
	functionid_service_down_1       = functionid_next++;
	functionid_service_down_2       = functionid_next++;
	functionid_downtime_over_10     = functionid_next++;
	functionid_downtime_over_25     = functionid_next++;
	functionid_downtime_over_50     = functionid_next++;
	functionid_downtime_over_75     = functionid_next++;
	functionid_downtime_over_100    = functionid_next++;
	functionid_rollweek_over_10     = functionid_next++;
	functionid_rollweek_over_25     = functionid_next++;
	functionid_rollweek_over_50     = functionid_next++;
	functionid_rollweek_over_75     = functionid_next++;
	functionid_rollweek_over_100    = functionid_next++;
	functionid_rtt_pfailed_over_10  = functionid_next++;
	functionid_rtt_pfailed_over_25  = functionid_next++;
	functionid_rtt_pfailed_over_50  = functionid_next++;
	functionid_rtt_pfailed_over_75  = functionid_next++;
	functionid_rtt_pfailed_over_100 = functionid_next++;

	/* status 3 = HOST_STATUS_TEMPLATE */
	DB_EXEC("insert into hosts set hostid=" ZBX_FS_UI64 ",created=0,proxy_hostid=NULL,host='%s',status=%d,"
			"disable_until=0,error='',available=0,errors_from=0,lastaccess=0,ipmi_authtype=-1,"
			"ipmi_privilege=2,ipmi_username='',ipmi_password='',ipmi_disable_until=0,ipmi_available=0,"
			"snmp_disable_until=0,snmp_available=0,maintenanceid=NULL,maintenance_status=0,"
			"maintenance_type=0,maintenance_from=0,ipmi_errors_from=0,snmp_errors_from=0,ipmi_error='',"
			"snmp_error='',jmx_disable_until=0,jmx_available=0,jmx_errors_from=0,jmx_error='',name='%s',"
			"info_1='',info_2='',flags=0,templateid=NULL,description='%s',tls_connect=1,tls_accept=1,"
			"tls_issuer='',tls_subject='',tls_psk_identity='',tls_psk='',proxy_address='',auto_compress=1",
		hostid, "Template RDDS Status", 3, "Template RDDS Status",
		"RDDS SLV items and triggers for linking to <RSMHOST> hosts");

	DB_EXEC("insert into hosts_groups set hostgroupid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ",groupid=" ZBX_FS_UI64,
			DBget_maxid_num("hosts_groups", 1), hostid, groupid_templates);

#define SQL	"insert into items set itemid=" ZBX_FS_UI64 ",type=%d,snmp_community='',snmp_oid='',"			\
			"hostid=" ZBX_FS_UI64 ",name='%s',key_='%s',delay='0',history='90d',trends='365d',status=0,"	\
			"value_type=%d,trapper_hosts='',units='%s',snmpv3_securityname='',snmpv3_securitylevel=0,"	\
			"snmpv3_authpassphrase='',snmpv3_privpassphrase='',formula='',logtimefmt='',templateid=NULL,"	\
			"valuemapid=nullif(" ZBX_FS_UI64 ",0),params='',ipmi_sensor='',authtype=0,username='',"		\
			"password='',publickey='',privatekey='',flags=0,interfaceid=NULL,port='',description='',"	\
			"inventory_link=0,lifetime='30d',snmpv3_authprotocol=0,snmpv3_privprotocol=0,"			\
			"snmpv3_contextname='',evaltype=0,jmx_endpoint='',master_itemid=NULL,timeout='3s',url='',"	\
			"query_fields='',posts='',status_codes='200',follow_redirects=1,post_type=0,http_proxy='',"	\
			"headers='',retrieve_mode=0,request_method=0,output_format=0,ssl_cert_file='',ssl_key_file='',"	\
			"ssl_key_password='',verify_peer=0,verify_host=0,allow_traps=0"
	/* type 2 = ITEM_TYPE_TRAPPER */
	/* value_type 0 = ITEM_VALUE_TYPE_FLOAT */
	/* value_type 3 = ITEM_VALUE_TYPE_UINT64 */
	DB_EXEC(SQL, itemid_avail, 2, hostid, "RDDS availability", "rsm.slv.rdds.avail", 3, "", valuemapid_rsm_service_availability);
	DB_EXEC(SQL, itemid_downtime, 2, hostid, "RDDS minutes of downtime", "rsm.slv.rdds.downtime", 3, "", (zbx_uint64_t)0);
	DB_EXEC(SQL, itemid_rollweek, 2, hostid, "RDDS weekly unavailability", "rsm.slv.rdds.rollweek", 0, "%", (zbx_uint64_t)0);
	DB_EXEC(SQL, itemid_rtt_failed, 2, hostid, "Number of failed monthly RDDS queries", "rsm.slv.rdds.rtt.failed", 3, "", (zbx_uint64_t)0);
	DB_EXEC(SQL, itemid_rtt_performed, 2, hostid, "Number of performed monthly RDDS queries", "rsm.slv.rdds.rtt.performed", 3, "", (zbx_uint64_t)0);
	DB_EXEC(SQL, itemid_rtt_pfailed, 2, hostid, "Ratio of failed monthly RDDS queries", "rsm.slv.rdds.rtt.pfailed", 0, "%", (zbx_uint64_t)0);
#undef SQL

	/* priority 0 = TRIGGER_SEVERITY_NOT_CLASSIFIED */
	DB_EXEC("insert into triggers set triggerid=" ZBX_FS_UI64 ","
			"expression='({TRIGGER.VALUE}=0 and {" ZBX_FS_UI64 "}=0) or ({TRIGGER.VALUE}=1 and {" ZBX_FS_UI64 "}>0)',"
			"description='%s',url='',status=0,value=0,priority=%d,lastchange=0,comments='',error='',"
			"templateid=NULL,type=0,state=0,flags=0,recovery_mode=0,recovery_expression='',"
			"correlation_mode=0,correlation_tag='',manual_close=0,opdata=''",
		triggerid_service_down, functionid_service_down_1, functionid_service_down_2, "RDDS service is down", 0);

#define SQL	"insert into triggers set triggerid=" ZBX_FS_UI64 ",expression='{" ZBX_FS_UI64 "}%s',description='%s',"	\
			"url='',status=0,value=0,priority=%d,lastchange=0,comments='',error='',templateid=NULL,type=0,"	\
			"state=0,flags=0,recovery_mode=0,recovery_expression='',correlation_mode=0,correlation_tag='',"	\
			"manual_close=0,opdata=''"
	/* priority 2 = TRIGGER_SEVERITY_WARNING */
	/* priority 3 = TRIGGER_SEVERITY_AVERAGE */
	/* priority 4 = TRIGGER_SEVERITY_HIGH */
	/* priority 5 = TRIGGER_SEVERITY_DISASTER */
	DB_EXEC(SQL, triggerid_downtime_over_10, functionid_downtime_over_10, ">={$RSM.SLV.RDDS.DOWNTIME}*0.1",
		"RDDS service was unavailable for 10% of allowed $1 minutes", 2);
	DB_EXEC(SQL, triggerid_downtime_over_25, functionid_downtime_over_25, ">={$RSM.SLV.RDDS.DOWNTIME}*0.25",
		"RDDS service was unavailable for 25% of allowed $1 minutes", 3);
	DB_EXEC(SQL, triggerid_downtime_over_50, functionid_downtime_over_50, ">={$RSM.SLV.RDDS.DOWNTIME}*0.5",
		"RDDS service was unavailable for 50% of allowed $1 minutes", 3);
	DB_EXEC(SQL, triggerid_downtime_over_75, functionid_downtime_over_75, ">={$RSM.SLV.RDDS.DOWNTIME}*0.75",
		"RDDS service was unavailable for 75% of allowed $1 minutes", 4);
	DB_EXEC(SQL, triggerid_downtime_over_100, functionid_downtime_over_100, ">={$RSM.SLV.RDDS.DOWNTIME}",
		"RDDS service was unavailable for 100% of allowed $1 minutes", 5);
	DB_EXEC(SQL, triggerid_rollweek_over_10, functionid_rollweek_over_10, ">=10",
		"RDDS rolling week is over 10%", 2);
	DB_EXEC(SQL, triggerid_rollweek_over_25, functionid_rollweek_over_25, ">=25",
		"RDDS rolling week is over 25%", 3);
	DB_EXEC(SQL, triggerid_rollweek_over_50, functionid_rollweek_over_50, ">=50",
		"RDDS rolling week is over 50%", 3);
	DB_EXEC(SQL, triggerid_rollweek_over_75, functionid_rollweek_over_75, ">=75",
		"RDDS rolling week is over 75%", 4);
	DB_EXEC(SQL, triggerid_rollweek_over_100, functionid_rollweek_over_100, ">=100",
		"RDDS rolling week is over 100%", 5);
	DB_EXEC(SQL, triggerid_rtt_pfailed_over_10, functionid_rtt_pfailed_over_10, ">{$RSM.SLV.RDDS.RTT}*0.1",
		"Ratio of failed RDDS tests exceeded 10% of allowed $1%", 2);
	DB_EXEC(SQL, triggerid_rtt_pfailed_over_25, functionid_rtt_pfailed_over_25, ">{$RSM.SLV.RDDS.RTT}*0.25",
		"Ratio of failed RDDS tests exceeded 25% of allowed $1%", 3);
	DB_EXEC(SQL, triggerid_rtt_pfailed_over_50, functionid_rtt_pfailed_over_50, ">{$RSM.SLV.RDDS.RTT}*0.5",
		"Ratio of failed RDDS tests exceeded 50% of allowed $1%", 3);
	DB_EXEC(SQL, triggerid_rtt_pfailed_over_75, functionid_rtt_pfailed_over_75, ">{$RSM.SLV.RDDS.RTT}*0.75",
		"Ratio of failed RDDS tests exceeded 75% of allowed $1%", 4);
	DB_EXEC(SQL, triggerid_rtt_pfailed_over_100, functionid_rtt_pfailed_over_100, ">{$RSM.SLV.RDDS.RTT}",
		"Ratio of failed RDDS tests exceeded 100% of allowed $1%", 5);
#undef SQL

#define SQL	"insert into functions set functionid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64 ",triggerid=" ZBX_FS_UI64 ",name='%s',parameter='%s'"
	DB_EXEC(SQL, functionid_service_down_1      , itemid_avail      , triggerid_service_down        , "max"  , "#{$RSM.INCIDENT.RDDS.FAIL}");
	DB_EXEC(SQL, functionid_service_down_2      , itemid_avail      , triggerid_service_down        , "count", "#{$RSM.INCIDENT.RDDS.RECOVER},0,\"eq\"");
	DB_EXEC(SQL, functionid_downtime_over_10    , itemid_downtime   , triggerid_downtime_over_10    , "last" , "0");
	DB_EXEC(SQL, functionid_downtime_over_25    , itemid_downtime   , triggerid_downtime_over_25    , "last" , "0");
	DB_EXEC(SQL, functionid_downtime_over_50    , itemid_downtime   , triggerid_downtime_over_50    , "last" , "0");
	DB_EXEC(SQL, functionid_downtime_over_75    , itemid_downtime   , triggerid_downtime_over_75    , "last" , "0");
	DB_EXEC(SQL, functionid_downtime_over_100   , itemid_downtime   , triggerid_downtime_over_100   , "last" , "0");
	DB_EXEC(SQL, functionid_rollweek_over_10    , itemid_rollweek   , triggerid_rollweek_over_10    , "last" , "0");
	DB_EXEC(SQL, functionid_rollweek_over_25    , itemid_rollweek   , triggerid_rollweek_over_25    , "last" , "0");
	DB_EXEC(SQL, functionid_rollweek_over_50    , itemid_rollweek   , triggerid_rollweek_over_50    , "last" , "0");
	DB_EXEC(SQL, functionid_rollweek_over_75    , itemid_rollweek   , triggerid_rollweek_over_75    , "last" , "0");
	DB_EXEC(SQL, functionid_rollweek_over_100   , itemid_rollweek   , triggerid_rollweek_over_100   , "last" , "0");
	DB_EXEC(SQL, functionid_rtt_pfailed_over_10 , itemid_rtt_pfailed, triggerid_rtt_pfailed_over_10 , "last" , "");
	DB_EXEC(SQL, functionid_rtt_pfailed_over_25 , itemid_rtt_pfailed, triggerid_rtt_pfailed_over_25 , "last" , "");
	DB_EXEC(SQL, functionid_rtt_pfailed_over_50 , itemid_rtt_pfailed, triggerid_rtt_pfailed_over_50 , "last" , "");
	DB_EXEC(SQL, functionid_rtt_pfailed_over_75 , itemid_rtt_pfailed, triggerid_rtt_pfailed_over_75 , "last" , "");
	DB_EXEC(SQL, functionid_rtt_pfailed_over_100, itemid_rtt_pfailed, triggerid_rtt_pfailed_over_100, "last" , "");
#undef SQL

#define SQL	"insert into trigger_depends set triggerdepid=" ZBX_FS_UI64 ",triggerid_down=" ZBX_FS_UI64 ",triggerid_up=" ZBX_FS_UI64
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_downtime_over_10   , triggerid_downtime_over_25);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_downtime_over_25   , triggerid_downtime_over_50);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_downtime_over_50   , triggerid_downtime_over_75);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_downtime_over_75   , triggerid_downtime_over_100);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_rollweek_over_10   , triggerid_rollweek_over_25);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_rollweek_over_25   , triggerid_rollweek_over_50);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_rollweek_over_50   , triggerid_rollweek_over_75);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_rollweek_over_75   , triggerid_rollweek_over_100);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_rtt_pfailed_over_10, triggerid_rtt_pfailed_over_25);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_rtt_pfailed_over_25, triggerid_rtt_pfailed_over_50);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_rtt_pfailed_over_50, triggerid_rtt_pfailed_over_75);
	DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_rtt_pfailed_over_75, triggerid_rtt_pfailed_over_100);
#undef SQL

	ret = SUCCEED;
out:
	return ret;
}

/* 4050012, 17 - add "Template Probe Status" template */
static int	DBpatch_4050012_17(void)
{
	int		ret = FAIL;

	zbx_uint64_t	groupid_templates;				/* groupid of "Templates" host group */
	zbx_uint64_t	valuemapid_service_state;			/* valuemapid of "Service state" value map */
	zbx_uint64_t	valuemapid_rsm_probe_status;			/* valuemapid of "RSM Probe status" value map */

	zbx_uint64_t	hostid;						/* hostid of "Template Probe Status" template */

	zbx_uint64_t	applicationid_next;
	zbx_uint64_t	applicationid_configuration;			/* applicationid of "Configuration" application */
	zbx_uint64_t	applicationid_internal_errors;			/* applicationid of "Internal errors" application */
	zbx_uint64_t	applicationid_probe_status;			/* applicationid of "Probe status" application */

	zbx_uint64_t	itemid_next;
	zbx_uint64_t	itemid_probe_configvalue_rsm_ip4_enabled;	/* itemid of "probe.configvalue[RSM.IP4.ENABLED]" item */
	zbx_uint64_t	itemid_probe_configvalue_rsm_ip6_enabled;	/* itemid of "probe.configvalue[RSM.IP6.ENABLED]" item */
	zbx_uint64_t	itemid_resolver_status;				/* itemid of "resolver.status[...]" item */
	zbx_uint64_t	itemid_rsm_errors;				/* itemid of "rsm.errors" item */
	zbx_uint64_t	itemid_rsm_probe_status_automatic;		/* itemid of "rsm.probe.status[automatic,...]" item */
	zbx_uint64_t	itemid_rsm_probe_status_manual;			/* itemid of "rsm.probe.status[manual]" item */

	zbx_uint64_t	triggerid_next;
	zbx_uint64_t	triggerid_int_err_1;				/* triggerid of "Internal errors happening" trigger */
	zbx_uint64_t	triggerid_int_err_2;				/* triggerid of "Internal errors happening for ..." trigger */
	zbx_uint64_t	triggerid_probe_disabled_1;			/* triggerid of "Probe <host> has been disabled by tests" trigger */
	zbx_uint64_t	triggerid_probe_disabled_2;			/* triggerid of "Probe <host> has been disabled for more than ..." trigger */
	zbx_uint64_t	triggerid_probe_knocked_out;			/* triggerid of "Probe <host> has been knocked out" trigger */

	zbx_uint64_t	functionid_next;
	zbx_uint64_t	functionid_int_err_1;				/* functiond for "Internal errors happening" trigger */
	zbx_uint64_t	functionid_int_err_2;				/* functiond for "Internal errors happening for ..." trigger */
	zbx_uint64_t	functionid_probe_disabled_1;			/* functiond for "Probe <host> has been disabled by tests" trigger */
	zbx_uint64_t	functionid_probe_disabled_2;			/* functiond for "Probe <host> has been disabled for more than ..." trigger */
	zbx_uint64_t	functionid_probe_knocked_out;			/* functiond for "Probe <host> has been knocked out" trigger */

	/* It was decided to setup monitoring from scratch rather than upgrade all the configuration, */
	/* therefore this patch may be obsolete and there's no guarantee that it works correctly.     */
	goto out;

	ONLY_SERVER();

	GET_HOST_GROUP_ID(groupid_templates, "Templates");
	GET_VALUE_MAP_ID(valuemapid_service_state, "Service state");
	GET_VALUE_MAP_ID(valuemapid_rsm_probe_status, "RSM Probe status");

	hostid = DBget_maxid_num("hosts", 1);

	applicationid_next            = DBget_maxid_num("applications", 3);
	applicationid_configuration   = applicationid_next++;
	applicationid_internal_errors = applicationid_next++;
	applicationid_probe_status    = applicationid_next++;

	itemid_next                              = DBget_maxid_num("items", 6);
	itemid_probe_configvalue_rsm_ip4_enabled = itemid_next++;
	itemid_probe_configvalue_rsm_ip6_enabled = itemid_next++;
	itemid_resolver_status                   = itemid_next++;
	itemid_rsm_errors                        = itemid_next++;
	itemid_rsm_probe_status_automatic        = itemid_next++;
	itemid_rsm_probe_status_manual           = itemid_next++;

	triggerid_next              = DBget_maxid_num("triggers", 5);
	triggerid_int_err_1         = triggerid_next++;
	triggerid_int_err_2         = triggerid_next++;
	triggerid_probe_disabled_1  = triggerid_next++;
	triggerid_probe_disabled_2  = triggerid_next++;
	triggerid_probe_knocked_out = triggerid_next++;

	functionid_next              = DBget_maxid_num("functions", 5);
	functionid_int_err_1         = functionid_next++;
	functionid_int_err_2         = functionid_next++;
	functionid_probe_disabled_1  = functionid_next++;
	functionid_probe_disabled_2  = functionid_next++;
	functionid_probe_knocked_out = functionid_next++;

	/* status 3 = HOST_STATUS_TEMPLATE */
	DB_EXEC("insert into hosts set hostid=" ZBX_FS_UI64 ",created=0,proxy_hostid=NULL,host='%s',status=%d,"
			"disable_until=0,error='',available=0,errors_from=0,lastaccess=0,ipmi_authtype=-1,"
			"ipmi_privilege=2,ipmi_username='',ipmi_password='',ipmi_disable_until=0,ipmi_available=0,"
			"snmp_disable_until=0,snmp_available=0,maintenanceid=NULL,maintenance_status=0,"
			"maintenance_type=0,maintenance_from=0,ipmi_errors_from=0,snmp_errors_from=0,ipmi_error='',"
			"snmp_error='',jmx_disable_until=0,jmx_available=0,jmx_errors_from=0,jmx_error='',name='%s',"
			"info_1='',info_2='',flags=0,templateid=NULL,description='',tls_connect=1,tls_accept=1,"
			"tls_issuer='',tls_subject='',tls_psk_identity='',tls_psk='',proxy_address='',auto_compress=1",
		hostid, "Template Probe Status", 3, "Template Probe Status");

	DB_EXEC("insert into hosts_groups set hostgroupid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ",groupid=" ZBX_FS_UI64,
		DBget_maxid_num("hosts_groups", 1), hostid, groupid_templates);

#define SQL	"insert into applications set applicationid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ",name='%s',flags=0"
	DB_EXEC(SQL, applicationid_configuration  , hostid, "Configuration");
	DB_EXEC(SQL, applicationid_internal_errors, hostid, "Internal errors");
	DB_EXEC(SQL, applicationid_probe_status   , hostid, "Probe status");
#undef SQL

#define SQL	"insert into items set itemid=" ZBX_FS_UI64 ",type=%d,snmp_community='',snmp_oid='',"			\
		"hostid=" ZBX_FS_UI64 ",name='%s',key_='%s',delay='%s',history='90d',trends='365d',status=0,"		\
		"value_type=%d,trapper_hosts='',units='',snmpv3_securityname='',snmpv3_securitylevel=0,"		\
		"snmpv3_authpassphrase='',snmpv3_privpassphrase='',formula='',logtimefmt='',templateid=NULL,"		\
		"valuemapid=nullif(" ZBX_FS_UI64 ",0),params='%s',ipmi_sensor='',authtype=0,username='',password='',"	\
		"publickey='',privatekey='',flags=0,interfaceid=NULL,port='',description='',inventory_link=0,"		\
		"lifetime='30d',snmpv3_authprotocol=0,snmpv3_privprotocol=0,snmpv3_contextname='',evaltype=0,"		\
		"jmx_endpoint='',master_itemid=NULL,timeout='3s',url='',query_fields='',posts='',status_codes='200',"	\
		"follow_redirects=1,post_type=0,http_proxy='',headers='',retrieve_mode=0,request_method=0,"		\
		"output_format=0,ssl_cert_file='',ssl_key_file='',ssl_key_password='',verify_peer=0,verify_host=0,"	\
		"allow_traps=0"
	/* type 2 = ITEM_TYPE_TRAPPER */
	/* type 3 = ITEM_TYPE_SIMPLE */
	/* type 15 = ITEM_TYPE_CALCULATED */
	/* value_type 0 = ITEM_VALUE_TYPE_FLOAT */
	/* value_type 3 = ITEM_VALUE_TYPE_UINT64 */
	/* DB_EXEC(SQL, itemid, type, hostid, name,			*/
	/* 		key_,						*/
	/* 		delay, value_type, valuemapid, params);		*/
	DB_EXEC(SQL, itemid_probe_configvalue_rsm_ip4_enabled, 15, hostid, "Value of $1 variable",
		"probe.configvalue[RSM.IP4.ENABLED]",
		"300", 3, (zbx_uint64_t)0, "{$RSM.IP4.ENABLED}");
	DB_EXEC(SQL, itemid_probe_configvalue_rsm_ip6_enabled, 15, hostid, "Value of $1 variable",
		"probe.configvalue[RSM.IP6.ENABLED]",
		"300", 3, (zbx_uint64_t)0, "{$RSM.IP6.ENABLED}");
	DB_EXEC(SQL, itemid_resolver_status, 3, hostid, "Local resolver status ($1)",
		"resolver.status[{$RSM.RESOLVER},{$RESOLVER.STATUS.TIMEOUT},{$RESOLVER.STATUS.TRIES},"
			"{$RSM.IP4.ENABLED},{$RSM.IP6.ENABLED}]",
		"60", 3, valuemapid_service_state, "");
	DB_EXEC(SQL, itemid_rsm_errors, 3, hostid, "Internal error rate",
		"rsm.errors",
		"60", 0, (zbx_uint64_t)0, "");
	DB_EXEC(SQL, itemid_rsm_probe_status_automatic, 3, hostid, "Probe status ($1)",
		"rsm.probe.status[automatic,\"{$RSM.IP4.ROOTSERVERS1}\",\"{$RSM.IP6.ROOTSERVERS1}\"]",
		"60", 3, valuemapid_rsm_probe_status, "");
	DB_EXEC(SQL, itemid_rsm_probe_status_manual, 2, hostid, "Probe status ($1)",
		"rsm.probe.status[manual]",
		"0", 3, valuemapid_rsm_probe_status, "");
#undef SQL

#define SQL	"insert into items_applications set itemappid=" ZBX_FS_UI64 ",applicationid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_configuration  , itemid_probe_configvalue_rsm_ip4_enabled);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_configuration  , itemid_probe_configvalue_rsm_ip6_enabled);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_probe_status   , itemid_resolver_status);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_internal_errors, itemid_rsm_errors);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_probe_status   , itemid_rsm_probe_status_automatic);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_probe_status   , itemid_rsm_probe_status_manual);
#undef SQL

#define SQL	"insert into triggers set triggerid=" ZBX_FS_UI64 ",expression='{" ZBX_FS_UI64 "}%s',description='%s',"	\
			"url='',status=0,value=0,priority=%d,lastchange=0,comments='',error='',templateid=NULL,type=0,"	\
			"state=0,flags=0,recovery_mode=0,recovery_expression='',correlation_mode=0,correlation_tag='',"	\
			"manual_close=0,opdata=''"
	/* priority 2 = TRIGGER_SEVERITY_WARNING */
	/* priority 3 = TRIGGER_SEVERITY_AVERAGE */
	/* priority 4 = TRIGGER_SEVERITY_HIGH */
	DB_EXEC(SQL, triggerid_int_err_1, functionid_int_err_1, ">0",
		"Internal errors happening", 2);
	DB_EXEC(SQL, triggerid_int_err_2, functionid_int_err_2, ">0",
		"Internal errors happening for {$PROBE.INTERNAL.ERROR.INTERVAL}", 4);
	DB_EXEC(SQL, triggerid_probe_disabled_1, functionid_probe_disabled_1, "=0",
		"Probe {HOST.NAME} has been disabled by tests", 4);
	DB_EXEC(SQL, triggerid_probe_disabled_2, functionid_probe_disabled_2, "=0",
		"Probe {HOST.NAME} has been disabled for more than {$RSM.PROBE.MAX.OFFLINE}", 3);
	DB_EXEC(SQL, triggerid_probe_knocked_out, functionid_probe_knocked_out, "=0",
		"Probe {HOST.NAME} has been knocked out", 4);
#undef SQL

#define SQL	"insert into functions set functionid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64 ",triggerid=" ZBX_FS_UI64 ",name='%s',parameter='%s'"
	DB_EXEC(SQL, functionid_int_err_1, itemid_rsm_errors, triggerid_int_err_1,
		"last", "");
	DB_EXEC(SQL, functionid_int_err_2, itemid_rsm_errors, triggerid_int_err_2,
		"min", "{$PROBE.INTERNAL.ERROR.INTERVAL}");
	DB_EXEC(SQL, functionid_probe_disabled_1, itemid_rsm_probe_status_automatic, triggerid_probe_disabled_1,
		"last", "0");
	DB_EXEC(SQL, functionid_probe_disabled_2, itemid_rsm_probe_status_manual, triggerid_probe_disabled_2,
		"max", "{$RSM.PROBE.MAX.OFFLINE}");
	DB_EXEC(SQL, functionid_probe_knocked_out, itemid_rsm_probe_status_manual, triggerid_probe_knocked_out,
		"last", "0");
#undef SQL

	DB_EXEC("insert into trigger_depends set"
			" triggerdepid=" ZBX_FS_UI64 ",triggerid_down=" ZBX_FS_UI64 ",triggerid_up=" ZBX_FS_UI64,
		DBget_maxid_num("trigger_depends", 1), triggerid_int_err_1, triggerid_int_err_2);

	ret = SUCCEED;
out:
	return ret;
}

static int	DBpatch_4050012_18_create_application(zbx_uint64_t *applicationid, zbx_uint64_t template_applicationid,
		zbx_uint64_t hostid, const char *name)
{
	int	ret = FAIL;

	*applicationid = DBget_maxid_num("applications", 1);

	DB_EXEC("insert into applications set applicationid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ",name='%s',flags=0",
		*applicationid, hostid, name);

	DB_EXEC("insert into application_template set"
			" application_templateid=" ZBX_FS_UI64 ","
			"applicationid=" ZBX_FS_UI64 ","
			"templateid=" ZBX_FS_UI64,
		DBget_maxid_num("application_template", 1), *applicationid, template_applicationid);

	ret = SUCCEED;
out:
	return ret;
}

static int	DBpatch_4050012_18_copy_preproc(zbx_uint64_t src_itemid, zbx_uint64_t dst_itemid,
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
		DB_EXEC("insert into item_preproc set item_preprocid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64 ",step=%s,"
				"type=%s,params=\"%s\",error_handler=%s,error_handler_params='%s'",
			DBget_maxid_num("item_preproc", 1), dst_itemid, row[0], row[1], row[2], row[3],
			row[4]);
	}

	DBfree_result(result);
	result = NULL;

	if (NULL != replacements)
	{
		size_t	i;

		for (i = 0; NULL != replacements[i][0]; i++)
		{
			DB_EXEC("update item_preproc set params=replace(params,'%s','%s') where itemid=" ZBX_FS_UI64,
				replacements[i][0], replacements[i][1], dst_itemid);
		}
	}

	ret = SUCCEED;
out:
	if (NULL != result)
		DBfree_result(result);

	return ret;
}

static int	DBpatch_4050012_18_copy_lld_macros(zbx_uint64_t src_itemid, zbx_uint64_t dst_itemid)
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
		DB_EXEC("insert into lld_macro_path set"
				" lld_macro_pathid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64 ",lld_macro='%s',path='%s'",
			DBget_maxid_num("lld_macro_path", 1), dst_itemid, row[0], row[1]);
	}

	ret = SUCCEED;
out:
	DBfree_result(result);

	return ret;
}

static int	DBpatch_4050012_18_create_item(zbx_uint64_t *new_itemid, zbx_uint64_t templateid, zbx_uint64_t hostid,
		zbx_uint64_t interfaceid, zbx_uint64_t master_itemid, zbx_uint64_t applicationid)
{
	int		ret = FAIL;

	*new_itemid = DBget_maxid_num("items", 1);

	DB_EXEC("insert into items (itemid,type,snmp_community,snmp_oid,hostid,name,key_,delay,history,trends,"
			"status,value_type,trapper_hosts,units,snmpv3_securityname,snmpv3_securitylevel,"
			"snmpv3_authpassphrase,snmpv3_privpassphrase,formula,logtimefmt,templateid,valuemapid,"
			"params,ipmi_sensor,authtype,username,password,publickey,privatekey,flags,"
			"interfaceid,port,description,inventory_link,lifetime,snmpv3_authprotocol,"
			"snmpv3_privprotocol,snmpv3_contextname,evaltype,jmx_endpoint,master_itemid,"
			"timeout,url,query_fields,posts,status_codes,follow_redirects,post_type,http_proxy,headers,"
			"retrieve_mode,request_method,output_format,ssl_cert_file,ssl_key_file,ssl_key_password,"
			"verify_peer,verify_host,allow_traps)"
		"select"
			" " ZBX_FS_UI64 ",type,snmp_community,snmp_oid," ZBX_FS_UI64 ",name,key_,delay,history,trends,"
			"status,value_type,trapper_hosts,units,snmpv3_securityname,snmpv3_securitylevel,"
			"snmpv3_authpassphrase,snmpv3_privpassphrase,formula,logtimefmt," ZBX_FS_UI64 ",valuemapid,"
			"params,ipmi_sensor,authtype,username,password,publickey,privatekey,flags,"
			"nullif(" ZBX_FS_UI64 ",0),port,description,inventory_link,lifetime,snmpv3_authprotocol,"
			"snmpv3_privprotocol,snmpv3_contextname,evaltype,jmx_endpoint,nullif(" ZBX_FS_UI64 ",0),"
			"timeout,url,query_fields,posts,status_codes,follow_redirects,post_type,http_proxy,headers,"
			"retrieve_mode,request_method,output_format,ssl_cert_file,ssl_key_file,ssl_key_password,"
			"verify_peer,verify_host,allow_traps"
		" from items"
		" where itemid=" ZBX_FS_UI64,
		*new_itemid, hostid, templateid, interfaceid, master_itemid, templateid);

	if (0 != applicationid)
	{
		DB_EXEC("insert into items_applications set"
				" itemappid=" ZBX_FS_UI64 ",applicationid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64,
			DBget_maxid_num("items_applications", 1), applicationid, *new_itemid);
	}

	CHECK(DBpatch_4050012_18_copy_preproc(templateid, *new_itemid, NULL));

	CHECK(DBpatch_4050012_18_copy_lld_macros(templateid, *new_itemid));

	ret = SUCCEED;
out:
	return ret;
}

static int	DBpatch_4050012_18_convert_item(zbx_uint64_t *itemid, zbx_uint64_t hostid, const char *key,
		zbx_uint64_t master_itemid, zbx_uint64_t template_itemid, zbx_uint64_t applicationid)
{
	int	ret = FAIL;

	SELECT_VALUE_UINT64(*itemid, "select itemid from items where hostid=" ZBX_FS_UI64 " and key_='%s'", hostid, key);

	DB_EXEC("update"
			" items,"
			"items as template"
		" set"
			" items.type=template.type,"
			"items.name=template.name,"
			"items.key_=template.key_,"
			"items.delay=template.delay,"
			"items.templateid=template.itemid,"
			"items.interfaceid=null,"
			"items.description=template.description,"
			"items.master_itemid=nullif(" ZBX_FS_UI64 ",0),"
			"items.request_method=template.request_method"
		" where"
			" items.itemid=" ZBX_FS_UI64 " and"
			" template.itemid=" ZBX_FS_UI64,
		master_itemid, *itemid, template_itemid);

	DB_EXEC("delete from items_applications where itemid=" ZBX_FS_UI64, *itemid);

	DB_EXEC("insert into items_applications set"
			" itemappid=" ZBX_FS_UI64 ",applicationid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64,
		DBget_maxid_num("items_applications", 1), applicationid, *itemid);

	CHECK(DBpatch_4050012_18_copy_preproc(template_itemid, *itemid, NULL));

	ret = SUCCEED;
out:
	return ret;
}

static int	DBpatch_4050012_18_create_item_prototype(zbx_uint64_t *new_itemid, zbx_uint64_t templateid,
		zbx_uint64_t hostid, zbx_uint64_t interfaceid, zbx_uint64_t master_itemid, zbx_uint64_t applicationid,
		zbx_uint64_t parent_itemid)
{
	int		ret = FAIL;

	CHECK(DBpatch_4050012_18_create_item(new_itemid, templateid, hostid, interfaceid, master_itemid,
			applicationid));

	DB_EXEC("insert into item_discovery set"
			" itemdiscoveryid=" ZBX_FS_UI64 ","
			"itemid=" ZBX_FS_UI64 ","
			"parent_itemid=" ZBX_FS_UI64 ","
			"key_='',"
			"lastcheck=0,"
			"ts_delete=0",
		DBget_maxid_num("item_discovery", 1), *new_itemid, parent_itemid);

	ret = SUCCEED;
out:
	return ret;
}

static int	DBpatch_4050012_18_create_item_lld(zbx_uint64_t *new_itemid, const char *key,
		zbx_uint64_t prototype_itemid, zbx_uint64_t applicationid, const char *preproc_replacements[][2])
{
	int	ret = FAIL;

	*new_itemid = DBget_maxid_num("items", 1);

	/* non-default values - templateid=NULL, flags=ZBX_FLAG_DISCOVERY_CREATED, interfaceid=NULL */
	DB_EXEC("insert into items (itemid,type,snmp_community,snmp_oid,hostid,name,key_,delay,history,trends,status,"
			"value_type,trapper_hosts,units,snmpv3_securityname,snmpv3_securitylevel,snmpv3_authpassphrase,"
			"snmpv3_privpassphrase,formula,logtimefmt,templateid,valuemapid,params,ipmi_sensor,authtype,"
			"username,password,publickey,privatekey,flags,interfaceid,port,description,inventory_link,"
			"lifetime,snmpv3_authprotocol,snmpv3_privprotocol,snmpv3_contextname,evaltype,jmx_endpoint,"
			"master_itemid,timeout,url,query_fields,posts,status_codes,follow_redirects,post_type,"
			"http_proxy,headers,retrieve_mode,request_method,output_format,ssl_cert_file,ssl_key_file,"
			"ssl_key_password,verify_peer,verify_host,allow_traps)"
		" select"
			" " ZBX_FS_UI64 ",type,snmp_community,snmp_oid,hostid,name,'%s',delay,history,trends,status,"
			"value_type,trapper_hosts,units,snmpv3_securityname,snmpv3_securitylevel,snmpv3_authpassphrase,"
			"snmpv3_privpassphrase,formula,logtimefmt,null,valuemapid,params,ipmi_sensor,authtype,"
			"username,password,publickey,privatekey,4,null,port,description,inventory_link,"
			"lifetime,snmpv3_authprotocol,snmpv3_privprotocol,snmpv3_contextname,evaltype,jmx_endpoint,"
			"master_itemid,timeout,url,query_fields,posts,status_codes,follow_redirects,post_type,"
			"http_proxy,headers,retrieve_mode,request_method,output_format,ssl_cert_file,ssl_key_file,"
			"ssl_key_password,verify_peer,verify_host,allow_traps"
		" from items"
		" where itemid=" ZBX_FS_UI64,
		*new_itemid, key, prototype_itemid);

	DB_EXEC("insert into items_applications set"
			" itemappid=" ZBX_FS_UI64 ",applicationid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64,
		DBget_maxid_num("items_applications", 1), applicationid, *new_itemid);

	CHECK(DBpatch_4050012_18_copy_preproc(prototype_itemid, *new_itemid, preproc_replacements));

	DB_EXEC("insert into item_discovery (itemdiscoveryid,itemid,parent_itemid,key_,lastcheck,ts_delete)"
			" select " ZBX_FS_UI64 "," ZBX_FS_UI64 ",itemid,key_,0,0 from items where itemid=" ZBX_FS_UI64,
		DBget_maxid_num("item_discovery", 1), *new_itemid, prototype_itemid);

	ret = SUCCEED;
out:
	return ret;
}

static int	DBpatch_4050012_18_convert_item_lld(zbx_uint64_t *itemid, zbx_uint64_t hostid, const char *old_key,
		const char *new_key, zbx_uint64_t prototype_itemid, zbx_uint64_t applicationid,
		const char *preproc_replacements[][2])
{
	int	ret = FAIL;

	SELECT_VALUE_UINT64(*itemid, "select itemid from items where hostid=" ZBX_FS_UI64 " and key_='%s'", hostid, old_key);

	DB_EXEC("update"
			" items,"
			"items as prototype"
		" set"
			" items.type=prototype.type,"
			"items.name=prototype.name,"
			"items.key_='%s',"
			"items.templateid=null,"
			"items.flags=4,"
			"items.description=prototype.description,"
			"items.master_itemid=prototype.master_itemid,"
			"items.request_method=prototype.request_method"
		" where"
			" items.itemid=" ZBX_FS_UI64 " and"
			" prototype.itemid=" ZBX_FS_UI64,
		new_key, *itemid, prototype_itemid);

	DB_EXEC("delete from items_applications where itemid=" ZBX_FS_UI64, *itemid);

	DB_EXEC("insert into items_applications set"
			" itemappid=" ZBX_FS_UI64 ",applicationid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64,
		DBget_maxid_num("items_applications", 1), applicationid, *itemid);

	CHECK(DBpatch_4050012_18_copy_preproc(prototype_itemid, *itemid, preproc_replacements));

	DB_EXEC("insert into item_discovery (itemdiscoveryid,itemid,parent_itemid,key_,lastcheck,ts_delete)"
			" select " ZBX_FS_UI64 "," ZBX_FS_UI64 ",itemid,key_,0,0 from items where itemid=" ZBX_FS_UI64,
		DBget_maxid_num("item_discovery", 1), *itemid, prototype_itemid);

	ret = SUCCEED;
out:
	return ret;
}

/* 4050012, 18 - convert "<rshmost> <probe>" hosts to use "Template DNS Test" template */
static int	DBpatch_4050012_18(void)
{
	int		ret = FAIL;

	DB_RESULT	result_hostid = NULL;
	DB_RESULT	result = NULL;
	DB_ROW		row;

	zbx_uint64_t	groupid_tld_probe_resluts;			/* groupid of "TLD Probe results" host group */
	zbx_uint64_t	hostid_template_dns_test;			/* hostid of "Template DNS Test" template */

	zbx_uint64_t	template_applicationid_dns;			/* applicationid of "DNS" application in "Template DNS Test" template */
	zbx_uint64_t	template_applicationid_dnssec;			/* applicationid of "DNSSEC" application in "Template DNS Test" template */

	zbx_uint64_t	template_itemid_dnssec_enabled;			/* itemid of "DNSSEC enabled/disabled" item in "Template DNS Test" template */
	zbx_uint64_t	template_itemid_rsm_dns;			/* itemid of "DNS Test" item in "Template DNS Test" template */
	zbx_uint64_t	template_itemid_rsm_dns_status;			/* itemid of "Status of a DNS Test" item in "Template DNS Test" template */
	zbx_uint64_t	template_itemid_rsm_dns_nssok;			/* itemid of "Number of working Name Servers" item in "Template DNS Test" template */
	zbx_uint64_t	template_itemid_rsm_dns_ns_discovery;		/* itemid of "Name Servers discovery" item in "Template DNS Test" template */
	zbx_uint64_t	template_itemid_rsm_dns_nsip_discovery;		/* itemid of "NS-IP pairs discovery" item in "Template DNS Test" template */
	zbx_uint64_t	template_itemid_rsm_dns_ns_status;		/* itemid of "Status of $1" item prototype in "Template DNS Test" template */
	zbx_uint64_t	template_itemid_rsm_dns_nsid;			/* itemid of "NSID of $1,$2" item prototype in "Template DNS Test" template */
	zbx_uint64_t	template_itemid_rsm_dns_rtt_tcp;		/* itemid of "RTT of $1,$2 using $3" item prototype in "Template DNS Test" template */
	zbx_uint64_t	template_itemid_rsm_dns_rtt_udp;		/* itemid of "RTT of $1,$2 using $3" item prototype in "Template DNS Test" template */
	zbx_uint64_t	template_itemid_rsm_dns_mode;			/* itemid of "The mode of the Test" item prototype in "Template DNS Test" template */
	zbx_uint64_t	template_itemid_rsm_dns_protocol;		/* itemid of "Transport protocol of the Test" item prototype in "Template DNS Test" template */

	/* It was decided to setup monitoring from scratch rather than upgrade all the configuration, */
	/* therefore this patch may be obsolete and there's no guarantee that it works correctly.     */
	goto out;

	ONLY_SERVER();

	GET_HOST_GROUP_ID(groupid_tld_probe_resluts, "TLD Probe results");
	GET_TEMPLATE_ID(hostid_template_dns_test, "Template DNS Test");

	GET_TEMPLATE_APPLICATION_ID(template_applicationid_dns   , "Template DNS Test", "DNS");
	GET_TEMPLATE_APPLICATION_ID(template_applicationid_dnssec, "Template DNS Test", "DNSSEC");

	GET_TEMPLATE_ITEM_ID(template_itemid_dnssec_enabled        , "Template DNS Test", "dnssec.enabled");
	GET_TEMPLATE_ITEM_ID_BY_PATTERN(template_itemid_rsm_dns    , "Template DNS Test", "rsm.dns[%]");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_dns_status        , "Template DNS Test", "rsm.dns.status");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_dns_nssok         , "Template DNS Test", "rsm.dns.nssok");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_dns_ns_discovery  , "Template DNS Test", "rsm.dns.ns.discovery");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_dns_nsip_discovery, "Template DNS Test", "rsm.dns.nsip.discovery");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_dns_ns_status     , "Template DNS Test", "rsm.dns.ns.status[{#NS}]");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_dns_nsid          , "Template DNS Test", "rsm.dns.nsid[{#NS},{#IP}]");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_dns_rtt_tcp       , "Template DNS Test", "rsm.dns.rtt[{#NS},{#IP},tcp]");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_dns_rtt_udp       , "Template DNS Test", "rsm.dns.rtt[{#NS},{#IP},udp]");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_dns_mode          , "Template DNS Test", "rsm.dns.mode");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_dns_protocol      , "Template DNS Test", "rsm.dns.protocol");

	result_hostid = DBselect("select"
				" hosts_groups.hostid,"
				"interface.interfaceid"
			" from"
				" hosts_groups"
				" left join interface on interface.hostid=hosts_groups.hostid"
			" where"
				" hosts_groups.groupid=" ZBX_FS_UI64 " and"
				" interface.main=1 and"			/* INTERFACE_PRIMARY */
				" interface.type=1",			/* INTERFACE_TYPE_AGENT */
			groupid_tld_probe_resluts);

	if (NULL == result_hostid)
		goto out;

	while (NULL != (row = DBfetch(result_hostid)))
	{
		zbx_uint64_t	hostid;					/* hostid of "<rsmhost> <probe>" host */
		zbx_uint64_t	interfaceid;				/* interfaceid for items in "<rsmhost> <probe>" host */

		zbx_uint64_t	applicationid_dns;			/* applicationid of "DNS" application */
		zbx_uint64_t	applicationid_dnssec;			/* applicationid of "DNSSEC" application */

		zbx_uint64_t	itemid_dnssec_enabled;			/* itemid of "DNSSEC enabled/disabled" item */
		zbx_uint64_t	itemid_rsm_dns;				/* itemid of "DNS Test" item */
		zbx_uint64_t	itemid_rsm_dns_ns_discovery;		/* itemid of "Name Servers discovery" item */
		zbx_uint64_t	itemid_rsm_dns_nsip_discovery;		/* itemid of "NS-IP pairs discovery" item */
		zbx_uint64_t	prototype_itemid_rsm_dns_ns_status;	/* itemid of "Status of $1" item prototype */
		zbx_uint64_t	prototype_itemid_rsm_dns_nsid;		/* itemid of "NSID of $1,$2" item prototype */
		zbx_uint64_t	prototype_itemid_rsm_dns_rtt_tcp;	/* itemid of "RTT of $1,$2 using $3" item prototype */
		zbx_uint64_t	prototype_itemid_rsm_dns_rtt_udp;	/* itemid of "RTT of $1,$2 using $3" item prototype */
		zbx_uint64_t	itemid_rsm_dns_mode;			/* itemid of "The mode of the Test" item prototype */
		zbx_uint64_t	itemid_rsm_dns_protocol;		/* itemid of "Transport protocol of the Test" item prototype */
		zbx_uint64_t	itemid_rsm_dns_status;			/* itemid of "Status of a DNS Test" item */
		zbx_uint64_t	itemid_rsm_dns_nssok;			/* itemid of "Number of working Name Servers" item */

		ZBX_STR2UINT64(hostid, row[0]);
		ZBX_STR2UINT64(interfaceid, row[1]);

		/* link "Template DNS Test" template to the host */
		DB_EXEC("insert into hosts_templates set"
				" hosttemplateid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ",templateid=" ZBX_FS_UI64,
			DBget_maxid_num("hosts_templates", 1), hostid, hostid_template_dns_test);

		/* create applications */
		CHECK(DBpatch_4050012_18_create_application(&applicationid_dns   , template_applicationid_dns   , hostid, "DNS"));
		CHECK(DBpatch_4050012_18_create_application(&applicationid_dnssec, template_applicationid_dnssec, hostid, "DNSSEC"));

		/* update dnssec.enabled item */
		CHECK(DBpatch_4050012_18_convert_item(&itemid_dnssec_enabled, hostid, "dnssec.enabled",
				0, template_itemid_dnssec_enabled, applicationid_dnssec));

		/* create "DNS Test" (rsm.dns[...]) master item */
		CHECK(DBpatch_4050012_18_create_item(&itemid_rsm_dns,
				template_itemid_rsm_dns, hostid, interfaceid, 0, applicationid_dns));

		/* create "Name Servers discovery" (rsm.dns.ns.discovery) discovery rule */
		CHECK(DBpatch_4050012_18_create_item(&itemid_rsm_dns_ns_discovery,
				template_itemid_rsm_dns_ns_discovery, hostid, 0, itemid_rsm_dns, 0));

		/* create "NS-IP pairs discovery" (rsm.dns.nsip.discovery) discovery rule */
		CHECK(DBpatch_4050012_18_create_item(&itemid_rsm_dns_nsip_discovery,
				template_itemid_rsm_dns_nsip_discovery, hostid, 0, itemid_rsm_dns, 0));

		/* create "Status of {#NS}" (rsm.dns.ns.status[{#NS}]) item prototype */
		CHECK(DBpatch_4050012_18_create_item_prototype(&prototype_itemid_rsm_dns_ns_status,
				template_itemid_rsm_dns_ns_status, hostid, 0, itemid_rsm_dns, applicationid_dns,
				itemid_rsm_dns_ns_discovery));

		/* create "NSID of {#NS},{#IP}" (rsm.dns.nsid[{#NS},{#IP}]) item prototype */
		CHECK(DBpatch_4050012_18_create_item_prototype(&prototype_itemid_rsm_dns_nsid,
				template_itemid_rsm_dns_nsid, hostid, 0, itemid_rsm_dns, applicationid_dns,
				itemid_rsm_dns_nsip_discovery));

		/* create "RTT of {#NS},{#IP} using tcp" (rsm.dns.rtt[{#NS},{#IP},tcp]) item prototype */
		CHECK(DBpatch_4050012_18_create_item_prototype(&prototype_itemid_rsm_dns_rtt_tcp,
				template_itemid_rsm_dns_rtt_tcp, hostid, 0, itemid_rsm_dns, applicationid_dns,
				itemid_rsm_dns_nsip_discovery));

		/* create "RTT of {#NS},{#IP} using udp" (rsm.dns.rtt[{#NS},{#IP},udp]) item prototype */
		CHECK(DBpatch_4050012_18_create_item_prototype(&prototype_itemid_rsm_dns_rtt_udp,
				template_itemid_rsm_dns_rtt_udp, hostid, 0, itemid_rsm_dns, applicationid_dns,
				itemid_rsm_dns_nsip_discovery));

		/* create "The mode of the Test" (rsm.dns.mode) item */
		CHECK(DBpatch_4050012_18_create_item(&itemid_rsm_dns_mode,
				template_itemid_rsm_dns_mode, hostid, 0, itemid_rsm_dns, applicationid_dns));

		/* create "Transport protocol of the Test" rsm.dns.protocol */
		CHECK(DBpatch_4050012_18_create_item(&itemid_rsm_dns_protocol,
				template_itemid_rsm_dns_protocol, hostid, 0, itemid_rsm_dns, applicationid_dns));

		/* create "Status of a DNS Test" rsm.dns.status */
		CHECK(DBpatch_4050012_18_create_item(&itemid_rsm_dns_status,
				template_itemid_rsm_dns_status, hostid, 0, itemid_rsm_dns, applicationid_dns));

		/* delete rsm.dns.tcp[{$RSM.TLD}] item */
		DB_EXEC("delete from items where key_='rsm.dns.tcp[{$RSM.TLD}]' and hostid=" ZBX_FS_UI64, hostid);

		/* convert rsm.dns.udp[{$RSM.TLD}] item into rsm.dns.nssok */
		CHECK(DBpatch_4050012_18_convert_item(&itemid_rsm_dns_nssok, hostid, "rsm.dns.udp[{$RSM.TLD}]",
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
			CHECK(DBpatch_4050012_18_create_item_lld(&itemid, key, prototype_itemid_rsm_dns_ns_status,
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
			CHECK(DBpatch_4050012_18_create_item_lld(&itemid, new_key,
					prototype_itemid_rsm_dns_nsid, applicationid_dns, preproc_replacements));

			/* convert "RTT of <ns>,<ip> using tcp" (rsm.dns.rtt[<ns>,<ip>,tcp]) item */
			zbx_snprintf(old_key, sizeof(old_key), "rsm.dns.tcp.rtt[{$RSM.TLD},%s,%s]", row[0], row[1]);
			zbx_snprintf(new_key, sizeof(new_key), "rsm.dns.rtt[%s,%s,tcp]", row[0], row[1]);
			CHECK(DBpatch_4050012_18_convert_item_lld(&itemid, hostid, old_key, new_key,
					prototype_itemid_rsm_dns_rtt_tcp, applicationid_dns, preproc_replacements));

			/* convert "RTT of <ns>,<ip> using udp" (rsm.dns.rtt[<ns>,<ip>,udp]) item */
			zbx_snprintf(old_key, sizeof(old_key), "rsm.dns.udp.rtt[{$RSM.TLD},%s,%s]", row[0], row[1]);
			zbx_snprintf(new_key, sizeof(new_key), "rsm.dns.rtt[%s,%s,udp]", row[0], row[1]);
			CHECK(DBpatch_4050012_18_convert_item_lld(&itemid, hostid, old_key, new_key,
					prototype_itemid_rsm_dns_rtt_udp, applicationid_dns, preproc_replacements));
		}

		DBfree_result(result);
		result = NULL;

		/* remove old applications */
		DB_EXEC("delete from applications where"
				" hostid=" ZBX_FS_UI64 " and"
				" name in ('DNS (TCP)', 'DNS (UDP)', 'DNS RTT (TCP)', 'DNS RTT (UDP)')",
			hostid);
	}

	ret = SUCCEED;
out:
	DBfree_result(result);
	DBfree_result(result_hostid);

	return ret;
}

/* 4050012, 19 - convert "<rshmost> <probe>" hosts to use "Template RDDS Test" template */
static int	DBpatch_4050012_19(void)
{
	int		ret = FAIL;

	DB_RESULT	result = NULL;
	DB_ROW		row;

	zbx_uint64_t	groupid_tld_probe_resluts;			/* groupid of "TLD Probe results" host group */
	zbx_uint64_t	hostid_template_rdds_test;			/* hostid of "Template RDDS Test" template */

	zbx_uint64_t	template_applicationid_rdds;			/* applicationid of "RDDS" application in "Template RDDS Test" template */
	zbx_uint64_t	template_applicationid_rdds43;			/* applicationid of "RDDS43" application in "Template RDDS Test" template */
	zbx_uint64_t	template_applicationid_rdds80;			/* applicationid of "RDDS80" application in "Template RDDS Test" template */

	zbx_uint64_t	template_itemid_rsm_rdds;			/* itemid of "RDDS Test" item in "Template RDDS Test" template */
	zbx_uint64_t	template_itemid_rsm_rdds_status;		/* itemid of "Status of RDDS Test" item in "Template RDDS Test" template */
	zbx_uint64_t	template_itemid_rsm_rdds43_ip;			/* itemid of "RDDS43 IP" item in "Template RDDS Test" template */
	zbx_uint64_t	template_itemid_rsm_rdds43_rtt;			/* itemid of "RDDS43 RTT" item in "Template RDDS Test" template */
	zbx_uint64_t	template_itemid_rsm_rdds80_ip;			/* itemid of "RDDS80 IP" item in "Template RDDS Test" template */
	zbx_uint64_t	template_itemid_rsm_rdds80_rtt;			/* itemid of "RDDS80 RTT" item in "Template RDDS Test" template */

	/* It was decided to setup monitoring from scratch rather than upgrade all the configuration, */
	/* therefore this patch may be obsolete and there's no guarantee that it works correctly.     */
	goto out;

	ONLY_SERVER();

	GET_HOST_GROUP_ID(groupid_tld_probe_resluts, "TLD Probe results");
	GET_TEMPLATE_ID(hostid_template_rdds_test, "Template RDDS Test");

	GET_TEMPLATE_APPLICATION_ID(template_applicationid_rdds  , "Template RDDS Test", "RDDS");
	GET_TEMPLATE_APPLICATION_ID(template_applicationid_rdds43, "Template RDDS Test", "RDDS43");
	GET_TEMPLATE_APPLICATION_ID(template_applicationid_rdds80, "Template RDDS Test", "RDDS80");

	GET_TEMPLATE_ITEM_ID_BY_PATTERN(template_itemid_rsm_rdds, "Template RDDS Test", "rsm.rdds[%]");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_rdds_status, "Template RDDS Test", "rsm.rdds.status");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_rdds43_ip  , "Template RDDS Test", "rsm.rdds.43.ip");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_rdds43_rtt , "Template RDDS Test", "rsm.rdds.43.rtt");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_rdds80_ip  , "Template RDDS Test", "rsm.rdds.80.ip");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_rdds80_rtt , "Template RDDS Test", "rsm.rdds.80.rtt");

	result = DBselect("select"
				" hosts_groups.hostid,"
				"interface.interfaceid,"
				"items.status"
			" from"
				" hosts_groups"
				" left join interface on interface.hostid=hosts_groups.hostid"
				" left join items on items.hostid=hosts_groups.hostid"
			" where"
				" hosts_groups.groupid=" ZBX_FS_UI64 " and"
				" interface.main=1 and"			/* INTERFACE_PRIMARY */
				" interface.type=1 and"			/* INTERFACE_TYPE_AGENT */
				" items.key_ like 'rsm.rdds[%%]'",
			groupid_tld_probe_resluts);

	if (NULL == result)
		goto out;

	while (NULL != (row = DBfetch(result)))
	{
		zbx_uint64_t	hostid;					/* hostid of "<rsmhost> <probe>" host */
		zbx_uint64_t	interfaceid;				/* interfaceid for items in "<rsmhost> <probe>" host */
		const char	*status;				/* status of the tests (enabled/disabled) */

		zbx_uint64_t	applicationid_rdds;			/* applicationid of "RDDS" aplication */
		zbx_uint64_t	applicationid_rdds43;			/* applicationid of "RDDS43" application */
		zbx_uint64_t	applicationid_rdds80;			/* applicationid of "RDDS80" application */

		zbx_uint64_t	itemid_rsm_rdds;			/* itemid of "RDDS Test" item */
		zbx_uint64_t	itemid_rsm_rdds_status;			/* itemid of "Status of RDDS Test" item */
		zbx_uint64_t	itemid_rsm_rdds43_ip;			/* itemid of "RDDS43 IP" item */
		zbx_uint64_t	itemid_rsm_rdds43_rtt;			/* itemid of "RDDS43 RTT" item */
		zbx_uint64_t	itemid_rsm_rdds80_ip;			/* itemid of "RDDS80 IP" item */
		zbx_uint64_t	itemid_rsm_rdds80_rtt;			/* itemid of "RDDS80 RTT" item */

		ZBX_STR2UINT64(hostid, row[0]);
		ZBX_STR2UINT64(interfaceid, row[1]);
		status = row[2];

		GET_HOST_APPLICATION_ID(applicationid_rdds  , hostid, "RDDS");
		GET_HOST_APPLICATION_ID(applicationid_rdds43, hostid, "RDDS43");
		GET_HOST_APPLICATION_ID(applicationid_rdds80, hostid, "RDDS80");

		GET_HOST_ITEM_ID_BY_PATTERN(itemid_rsm_rdds_status, hostid, "rsm.rdds[%]");
		GET_HOST_ITEM_ID(itemid_rsm_rdds43_ip , hostid, "rsm.rdds.43.ip[{$RSM.TLD}]");
		GET_HOST_ITEM_ID(itemid_rsm_rdds43_rtt, hostid, "rsm.rdds.43.rtt[{$RSM.TLD}]");
		GET_HOST_ITEM_ID(itemid_rsm_rdds80_ip , hostid, "rsm.rdds.80.ip[{$RSM.TLD}]");
		GET_HOST_ITEM_ID(itemid_rsm_rdds80_rtt, hostid, "rsm.rdds.80.rtt[{$RSM.TLD}]");

		/* link "Template RDDS Test" template to the host */
		DB_EXEC("insert into hosts_templates set"
				" hosttemplateid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ",templateid=" ZBX_FS_UI64,
			DBget_maxid_num("hosts_templates", 1), hostid, hostid_template_rdds_test);

		/* remove old links between host applications and template applications */
#define SQL	"delete from application_template where applicationid=" ZBX_FS_UI64
		DB_EXEC(SQL, applicationid_rdds);
		DB_EXEC(SQL, applicationid_rdds43);
		DB_EXEC(SQL, applicationid_rdds80);
#undef SQL

		/* link applications to the template */
#define SQL	"insert into application_template set"									\
		" application_templateid=" ZBX_FS_UI64 ",applicationid=" ZBX_FS_UI64 ",templateid=" ZBX_FS_UI64
		DB_EXEC(SQL, DBget_maxid_num("application_template", 1), applicationid_rdds  , template_applicationid_rdds);
		DB_EXEC(SQL, DBget_maxid_num("application_template", 1), applicationid_rdds43, template_applicationid_rdds43);
		DB_EXEC(SQL, DBget_maxid_num("application_template", 1), applicationid_rdds80, template_applicationid_rdds80);
#undef SQL

		itemid_rsm_rdds = DBget_maxid_num("items", 1);

		DB_EXEC("insert into items (itemid,type,snmp_community,snmp_oid,hostid,name,key_,delay,history,"
				"trends,status,value_type,trapper_hosts,units,snmpv3_securityname,snmpv3_securitylevel,"
				"snmpv3_authpassphrase,snmpv3_privpassphrase,formula,logtimefmt,templateid,"
				"valuemapid,params,ipmi_sensor,authtype,username,password,publickey,privatekey,flags,"
				"interfaceid,port,description,inventory_link,lifetime,snmpv3_authprotocol,"
				"snmpv3_privprotocol,snmpv3_contextname,evaltype,jmx_endpoint,master_itemid,timeout,"
				"url,query_fields,posts,status_codes,follow_redirects,post_type,http_proxy,headers,"
				"retrieve_mode,request_method,output_format,ssl_cert_file,ssl_key_file,"
				"ssl_key_password,verify_peer,verify_host,allow_traps)"
			" select"
				" " ZBX_FS_UI64 ",type,snmp_community,snmp_oid," ZBX_FS_UI64 ",name,key_,delay,history,"
				"trends,%s,value_type,trapper_hosts,units,snmpv3_securityname,snmpv3_securitylevel,"
				"snmpv3_authpassphrase,snmpv3_privpassphrase,formula,logtimefmt," ZBX_FS_UI64 ","
				"valuemapid,params,ipmi_sensor,authtype,username,password,publickey,privatekey,flags,"
				ZBX_FS_UI64 ",port,description,inventory_link,lifetime,snmpv3_authprotocol,"
				"snmpv3_privprotocol,snmpv3_contextname,evaltype,jmx_endpoint,master_itemid,timeout,"
				"url,query_fields,posts,status_codes,follow_redirects,post_type,http_proxy,headers,"
				"retrieve_mode,request_method,output_format,ssl_cert_file,ssl_key_file,"
				"ssl_key_password,verify_peer,verify_host,allow_traps"
			" from items"
			" where itemid=" ZBX_FS_UI64,
			itemid_rsm_rdds, hostid, status, template_itemid_rsm_rdds, interfaceid, template_itemid_rsm_rdds);

		DB_EXEC("insert into items_applications set"
					" itemappid=" ZBX_FS_UI64 ",applicationid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64,
				DBget_maxid_num("items_applications", 1), applicationid_rdds, itemid_rsm_rdds);

#define SQL	"update"												\
			" items,"											\
			"items as template"										\
		" set"													\
			" items.type=template.type,"									\
			"items.name=template.name,"									\
			"items.key_=template.key_,"									\
			"items.delay=template.delay,"									\
			"items.trends=template.trends,"									\
			"items.templateid=template.itemid,"								\
			"items.valuemapid=template.valuemapid,"								\
			"items.interfaceid=template.interfaceid,"							\
			"items.description=template.description,"							\
			"items.master_itemid=" ZBX_FS_UI64 ","								\
			"items.request_method=template.request_method"							\
		" where"												\
			" items.itemid=" ZBX_FS_UI64 " and"								\
			" template.itemid=" ZBX_FS_UI64
		DB_EXEC(SQL, itemid_rsm_rdds, itemid_rsm_rdds_status, template_itemid_rsm_rdds_status);
		DB_EXEC(SQL, itemid_rsm_rdds, itemid_rsm_rdds43_ip  , template_itemid_rsm_rdds43_ip);
		DB_EXEC(SQL, itemid_rsm_rdds, itemid_rsm_rdds43_rtt , template_itemid_rsm_rdds43_rtt);
		DB_EXEC(SQL, itemid_rsm_rdds, itemid_rsm_rdds80_ip  , template_itemid_rsm_rdds80_ip);
		DB_EXEC(SQL, itemid_rsm_rdds, itemid_rsm_rdds80_rtt , template_itemid_rsm_rdds80_rtt);
#undef SQL

#define SQL	"insert into item_preproc (item_preprocid,itemid,step,type,params,error_handler,error_handler_params)"	\
		"select " ZBX_FS_UI64 "," ZBX_FS_UI64 ",step,type,params,error_handler,error_handler_params"		\
		" from item_preproc where itemid=" ZBX_FS_UI64
		DB_EXEC(SQL, DBget_maxid_num("item_preproc", 1), itemid_rsm_rdds       , template_itemid_rsm_rdds);
		DB_EXEC(SQL, DBget_maxid_num("item_preproc", 1), itemid_rsm_rdds_status, template_itemid_rsm_rdds_status);
		DB_EXEC(SQL, DBget_maxid_num("item_preproc", 1), itemid_rsm_rdds43_ip  , template_itemid_rsm_rdds43_ip);
		DB_EXEC(SQL, DBget_maxid_num("item_preproc", 1), itemid_rsm_rdds43_rtt , template_itemid_rsm_rdds43_rtt);
		DB_EXEC(SQL, DBget_maxid_num("item_preproc", 1), itemid_rsm_rdds80_ip  , template_itemid_rsm_rdds80_ip);
		DB_EXEC(SQL, DBget_maxid_num("item_preproc", 1), itemid_rsm_rdds80_rtt , template_itemid_rsm_rdds80_rtt);
#undef SQL
	}

	ret = SUCCEED;
out:
	DBfree_result(result);

	return ret;
}

/* 4050012, 20 - set RDAP master item value_type to the text type */
static int	DBpatch_4050012_20(void)
{
	int	ret = FAIL;

	/* It was decided to setup monitoring from scratch rather than upgrade all the configuration, */
	/* therefore this patch may be obsolete and there's no guarantee that it works correctly.     */
	goto out;

	ONLY_SERVER();

	/* 4 = ITEM_VALUE_TYPE_TEXT */
	DB_EXEC("update items set name='RDAP Test',value_type=4,history='0',trends='0' where key_ like 'rdap[%%'");

	ret = SUCCEED;
out:
	return ret;
}

/* 4050012, 21 - set RDAP calculated items to be dependent items */
static int	DBpatch_4050012_21(void)
{
	int	ret = FAIL;

	/* It was decided to setup monitoring from scratch rather than upgrade all the configuration, */
	/* therefore this patch may be obsolete and there's no guarantee that it works correctly.     */
	goto out;

	ONLY_SERVER();

	/* 18 = ITEM_TYPE_DEPENDENT */
	DB_EXEC("update items as i1 inner join items as i2 on i1.hostid=i2.hostid set"
			" i1.type=18,i1.master_itemid=i2.itemid where i1.key_ in ('rdap.ip','rdap.rtt') and"
			" i2.key_ like 'rdap[%%'");

	ret = SUCCEED;
out:
	return ret;
}

static int	DBpatch_4050012_22_insert_item_preproc(const char *item_key, const char *item_preproc_param)
{
	int		ret = FAIL;
	DB_RESULT	result;
	DB_ROW		row;

	result = DBselect("select itemid from items where key_='%s'", item_key);

	if (NULL == result)
		goto out;

	while (NULL != (row = DBfetch(result)))
	{
		zbx_uint64_t	itemid;

		ZBX_STR2UINT64(itemid, row[0]);

		/* 12 = ZBX_PREPROC_JSONPATH */
		DB_EXEC("insert into item_preproc set item_preprocid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64 ",step=1,"
				"type=12,params='%s',error_handler=0",
			DBget_maxid_num("item_preproc", 1), itemid, item_preproc_param);
	}

	ret = SUCCEED;
out:
	DBfree_result(result);

	return ret;
}

/* 4050012, 22 - add item_preproc to RDAP ip and rtt items */
static int	DBpatch_4050012_22(void)
{
	int	ret = FAIL;

	/* It was decided to setup monitoring from scratch rather than upgrade all the configuration, */
	/* therefore this patch may be obsolete and there's no guarantee that it works correctly.     */
	goto out;

	ONLY_SERVER();

	CHECK(DBpatch_4050012_22_insert_item_preproc("rdap.ip", "$.rdap.ip"));
	CHECK(DBpatch_4050012_22_insert_item_preproc("rdap.rtt", "$.rdap.rtt"));

	ret = SUCCEED;
out:
	return ret;
}

/* 4050012, 23 - convert "<rsmhost>" hosts to use "Template DNS Status" template */
static int	DBpatch_4050012_23(void)
{
	int		ret = FAIL;

	DB_RESULT	result = NULL;
	DB_RESULT	result2 = NULL;
	DB_ROW		row;

	zbx_uint64_t	tlds_groupid;
	zbx_uint64_t	template_hostid;

	zbx_uint64_t	template_applicationid_slv_particular_test;	/* applicationid of "SLV particular test" application in "Template DNS Status" template */
	zbx_uint64_t	template_applicationid_slv_current_month;	/* applicationid of "SLV current month" application in "Template DNS Status" template */
	zbx_uint64_t	template_applicationid_slv_rolling_week;	/* applicationid of "SLV rolling week" application in "Template DNS Status" template */

	zbx_uint64_t	template_itemid_rsm_slv_dns_avail;		/* itemid of "DNS availability" item in "Template DNS Status" template */
	zbx_uint64_t	template_itemid_rsm_slv_dns_downtime;		/* itemid of "DNS minutes of downtime" item in "Template DNS Status" template */
	zbx_uint64_t	template_itemid_rsm_slv_dns_rollweek;		/* itemid of "DNS weekly unavailability" item in "Template DNS Status" template */
	zbx_uint64_t	template_itemid_rsm_slv_dns_udp_rtt_performed;	/* itemid of "Number of performed monthly DNS UDP tests" item in "Template DNS Status" template */
	zbx_uint64_t	template_itemid_rsm_slv_dns_udp_rtt_failed;	/* itemid of "Number of failed monthly DNS UDP tests" item in "Template DNS Status" template */
	zbx_uint64_t	template_itemid_rsm_slv_dns_udp_rtt_pfailed;	/* itemid of "Ratio of failed monthly DNS UDP tests" item in "Template DNS Status" template */
	zbx_uint64_t	template_itemid_rsm_slv_dns_tcp_rtt_performed;	/* itemid of "Number of performed monthly DNS TCP tests" item in "Template DNS Status" template */
	zbx_uint64_t	template_itemid_rsm_slv_dns_tcp_rtt_failed;	/* itemid of "Number of failed monthly DNS TCP tests" item in "Template DNS Status" template */
	zbx_uint64_t	template_itemid_rsm_slv_dns_tcp_rtt_pfailed;	/* itemid of "Ratio of failed monthly DNS TCP tests" item in "Template DNS Status" template */
	zbx_uint64_t	template_itemid_rsm_dns_nsip_discovery;		/* itemid of "NS-IP pairs discovery" discovery rule in "Template DNS Status" template */
	zbx_uint64_t	template_itemid_rsm_slv_dns_ns_avail_ns_ip;	/* itemid of "DNS NS $1 ($2) availability" item prototype in "Template DNS Status" template */
	zbx_uint64_t	template_itemid_rsm_slv_dns_ns_downtime_ns_ip;	/* itemid of "DNS minutes of $1 ($2) downtime" item prototype in "Template DNS Status" template */

	zbx_uint64_t	temlpate_triggerid_downtime_over_10;		/* triggerid of "DNS {#NS} ({#IP}) downtime exceeded 10% of allowed $1 minutes" trigger prototype in "Template DNS Status" template */
	zbx_uint64_t	temlpate_triggerid_downtime_over_25;		/* triggerid of "DNS {#NS} ({#IP}) downtime exceeded 25% of allowed $1 minutes" trigger prototype in "Template DNS Status" template */
	zbx_uint64_t	temlpate_triggerid_downtime_over_50;		/* triggerid of "DNS {#NS} ({#IP}) downtime exceeded 50% of allowed $1 minutes" trigger prototype in "Template DNS Status" template */
	zbx_uint64_t	temlpate_triggerid_downtime_over_75;		/* triggerid of "DNS {#NS} ({#IP}) downtime exceeded 75% of allowed $1 minutes" trigger prototype in "Template DNS Status" template */
	zbx_uint64_t	temlpate_triggerid_downtime_over_100;		/* triggerid of "DNS {#NS} ({#IP}) downtime exceeded 100% of allowed $1 minutes" trigger prototype in "Template DNS Status" template */

	/* It was decided to setup monitoring from scratch rather than upgrade all the configuration, */
	/* therefore this patch may be obsolete and there's no guarantee that it works correctly.     */
	goto out;

	ONLY_SERVER();

	GET_HOST_GROUP_ID(tlds_groupid, "TLDs");
	GET_TEMPLATE_ID(template_hostid, "Template DNS Status");

	GET_TEMPLATE_APPLICATION_ID(template_applicationid_slv_particular_test, "Template DNS Status", "SLV particular test");
	GET_TEMPLATE_APPLICATION_ID(template_applicationid_slv_current_month  , "Template DNS Status", "SLV current month");
	GET_TEMPLATE_APPLICATION_ID(template_applicationid_slv_rolling_week   , "Template DNS Status", "SLV rolling week");

	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_slv_dns_avail            , "Template DNS Status", "rsm.slv.dns.avail");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_slv_dns_downtime         , "Template DNS Status", "rsm.slv.dns.downtime");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_slv_dns_rollweek         , "Template DNS Status", "rsm.slv.dns.rollweek");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_slv_dns_udp_rtt_performed, "Template DNS Status", "rsm.slv.dns.udp.rtt.performed");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_slv_dns_udp_rtt_failed   , "Template DNS Status", "rsm.slv.dns.udp.rtt.failed");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_slv_dns_udp_rtt_pfailed  , "Template DNS Status", "rsm.slv.dns.udp.rtt.pfailed");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_slv_dns_tcp_rtt_performed, "Template DNS Status", "rsm.slv.dns.tcp.rtt.performed");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_slv_dns_tcp_rtt_failed   , "Template DNS Status", "rsm.slv.dns.tcp.rtt.failed");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_slv_dns_tcp_rtt_pfailed  , "Template DNS Status", "rsm.slv.dns.tcp.rtt.pfailed");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_dns_nsip_discovery       , "Template DNS Status", "rsm.dns.nsip.discovery");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_slv_dns_ns_avail_ns_ip   , "Template DNS Status", "rsm.slv.dns.ns.avail[{#NS},{#IP}]");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_slv_dns_ns_downtime_ns_ip, "Template DNS Status", "rsm.slv.dns.ns.downtime[{#NS},{#IP}]");

#define SQL	"select"												\
			" triggers.triggerid"										\
		" from"													\
			" hosts"											\
			" left join items on items.hostid=hosts.hostid"							\
			" left join functions on functions.itemid=items.itemid"						\
			" left join triggers on triggers.triggerid=functions.triggerid"					\
		" where"												\
			" hosts.host='Template DNS Status' and"								\
			" triggers.expression like '%s'"
	SELECT_VALUE_UINT64(temlpate_triggerid_downtime_over_10 , SQL, "{%}>{$RSM.SLV.NS.DOWNTIME}*0.1");
	SELECT_VALUE_UINT64(temlpate_triggerid_downtime_over_25 , SQL, "{%}>{$RSM.SLV.NS.DOWNTIME}*0.25");
	SELECT_VALUE_UINT64(temlpate_triggerid_downtime_over_50 , SQL, "{%}>{$RSM.SLV.NS.DOWNTIME}*0.5");
	SELECT_VALUE_UINT64(temlpate_triggerid_downtime_over_75 , SQL, "{%}>{$RSM.SLV.NS.DOWNTIME}*0.75");
	SELECT_VALUE_UINT64(temlpate_triggerid_downtime_over_100, SQL, "{%}>{$RSM.SLV.NS.DOWNTIME}");
#undef SQL

	/* get hostid of all <rsmhost> hosts */
	result = DBselect("select hostid from hosts_groups where groupid=" ZBX_FS_UI64, tlds_groupid);

	if (NULL == result)
		goto out;

	while (NULL != (row = DBfetch(result)))
	{
		zbx_uint64_t	hostid;

		zbx_uint64_t	applicationid_slv_particular_test;		/* applicationid of "SLV particular test" application */
		zbx_uint64_t	applicationid_slv_current_month;		/* applicationid of "SLV current month" application */
		zbx_uint64_t	applicationid_slv_rolling_week;			/* applicationid of "SLV rolling week" application */

		zbx_uint64_t	itemid_next;
		zbx_uint64_t	itemid_rsm_dns_nsip_discovery;			/* itemid of "NS-IP pairs discovery" discovery rule */
		zbx_uint64_t	itemid_rsm_slv_dns_ns_avail_ns_ip;		/* itemid of "DNS NS $1 ($2) availability" item prototype */
		zbx_uint64_t	itemid_rsm_slv_dns_ns_downtime_ns_ip;		/* itemid of "DNS minutes of $1 ($2) downtime" item prototype */

		zbx_uint64_t	triggerid_next;
		zbx_uint64_t	triggerid_downtime_over_10;			/* triggerid of "DNS {#NS} ({#IP}) downtime exceeded 10% of allowed $1 minutes" trigger prototype */
		zbx_uint64_t	triggerid_downtime_over_25;			/* triggerid of "DNS {#NS} ({#IP}) downtime exceeded 25% of allowed $1 minutes" trigger prototype */
		zbx_uint64_t	triggerid_downtime_over_50;			/* triggerid of "DNS {#NS} ({#IP}) downtime exceeded 50% of allowed $1 minutes" trigger prototype */
		zbx_uint64_t	triggerid_downtime_over_75;			/* triggerid of "DNS {#NS} ({#IP}) downtime exceeded 75% of allowed $1 minutes" trigger prototype */
		zbx_uint64_t	triggerid_downtime_over_100;			/* triggerid of "DNS {#NS} ({#IP}) downtime exceeded 100% of allowed $1 minutes" trigger prototype */

		zbx_uint64_t	functionid_next;
		zbx_uint64_t	functionid_downtime_over_10;			/* functionid for "DNS {#NS} ({#IP}) downtime exceeded 10% of allowed $1 minutes" trigger prototype */
		zbx_uint64_t	functionid_downtime_over_25;			/* functionid for "DNS {#NS} ({#IP}) downtime exceeded 25% of allowed $1 minutes" trigger prototype */
		zbx_uint64_t	functionid_downtime_over_50;			/* functionid for "DNS {#NS} ({#IP}) downtime exceeded 50% of allowed $1 minutes" trigger prototype */
		zbx_uint64_t	functionid_downtime_over_75;			/* functionid for "DNS {#NS} ({#IP}) downtime exceeded 75% of allowed $1 minutes" trigger prototype */
		zbx_uint64_t	functionid_downtime_over_100;			/* functionid for "DNS {#NS} ({#IP}) downtime exceeded 100% of allowed $1 minutes" trigger prototype */

		ZBX_STR2UINT64(hostid, row[0]);

		GET_HOST_APPLICATION_ID(applicationid_slv_particular_test, hostid, "SLV particular test");
		GET_HOST_APPLICATION_ID(applicationid_slv_current_month  , hostid, "SLV current month");
		GET_HOST_APPLICATION_ID(applicationid_slv_rolling_week   , hostid, "SLV rolling week");

		itemid_next                          = DBget_maxid_num("items", 3);
		itemid_rsm_dns_nsip_discovery        = itemid_next++;
		itemid_rsm_slv_dns_ns_avail_ns_ip    = itemid_next++;
		itemid_rsm_slv_dns_ns_downtime_ns_ip = itemid_next++;

		triggerid_next              = DBget_maxid_num("triggers", 5);
		triggerid_downtime_over_10  = triggerid_next++;
		triggerid_downtime_over_25  = triggerid_next++;
		triggerid_downtime_over_50  = triggerid_next++;
		triggerid_downtime_over_75  = triggerid_next++;
		triggerid_downtime_over_100 = triggerid_next++;

		functionid_next              = DBget_maxid_num("functions", 5);
		functionid_downtime_over_10  = functionid_next++;
		functionid_downtime_over_25  = functionid_next++;
		functionid_downtime_over_50  = functionid_next++;
		functionid_downtime_over_75  = functionid_next++;
		functionid_downtime_over_100 = functionid_next++;

#define SQL	"insert into hosts_templates set hosttemplateid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ",templateid=" ZBX_FS_UI64
		DB_EXEC(SQL, DBget_maxid_num("hosts_templates", 1), hostid, template_hostid);
#undef SQL

#define SQL	"insert into application_template set application_templateid=" ZBX_FS_UI64 ",applicationid=" ZBX_FS_UI64 ",templateid=" ZBX_FS_UI64
		DB_EXEC(SQL, DBget_maxid_num("application_template", 1), applicationid_slv_particular_test, template_applicationid_slv_particular_test);
		DB_EXEC(SQL, DBget_maxid_num("application_template", 1), applicationid_slv_current_month  , template_applicationid_slv_current_month);
		DB_EXEC(SQL, DBget_maxid_num("application_template", 1), applicationid_slv_rolling_week   , template_applicationid_slv_rolling_week);
#undef SQL

#define SQL	"update items set templateid=" ZBX_FS_UI64 ",request_method=0 where hostid=" ZBX_FS_UI64 " and key_='%s'"
		DB_EXEC(SQL, template_itemid_rsm_slv_dns_avail            , hostid, "rsm.slv.dns.avail");
		DB_EXEC(SQL, template_itemid_rsm_slv_dns_downtime         , hostid, "rsm.slv.dns.downtime");
		DB_EXEC(SQL, template_itemid_rsm_slv_dns_rollweek         , hostid, "rsm.slv.dns.rollweek");
		DB_EXEC(SQL, template_itemid_rsm_slv_dns_udp_rtt_performed, hostid, "rsm.slv.dns.udp.rtt.performed");
		DB_EXEC(SQL, template_itemid_rsm_slv_dns_udp_rtt_failed   , hostid, "rsm.slv.dns.udp.rtt.failed");
		DB_EXEC(SQL, template_itemid_rsm_slv_dns_udp_rtt_pfailed  , hostid, "rsm.slv.dns.udp.rtt.pfailed");
		DB_EXEC(SQL, template_itemid_rsm_slv_dns_tcp_rtt_performed, hostid, "rsm.slv.dns.tcp.rtt.performed");
		DB_EXEC(SQL, template_itemid_rsm_slv_dns_tcp_rtt_failed   , hostid, "rsm.slv.dns.tcp.rtt.failed");
		DB_EXEC(SQL, template_itemid_rsm_slv_dns_tcp_rtt_pfailed  , hostid, "rsm.slv.dns.tcp.rtt.pfailed");
#undef SQL

		/* LLD of NS-IP pairs: create items (discovery rule, item prototypes) */
#define SQL	"insert into items (itemid,type,snmp_community,snmp_oid,hostid,name,key_,delay,history,trends,"		\
			"status,value_type,trapper_hosts,units,snmpv3_securityname,snmpv3_securitylevel,"		\
			"snmpv3_authpassphrase,snmpv3_privpassphrase,formula,logtimefmt,templateid,valuemapid,"		\
			"params,ipmi_sensor,authtype,username,password,publickey,privatekey,flags,interfaceid,port,"	\
			"description,inventory_link,lifetime,snmpv3_authprotocol,snmpv3_privprotocol,"			\
			"snmpv3_contextname,evaltype,jmx_endpoint,master_itemid,timeout,url,query_fields,posts,"	\
			"status_codes,follow_redirects,post_type,http_proxy,headers,retrieve_mode,request_method,"	\
			"output_format,ssl_cert_file,ssl_key_file,ssl_key_password,verify_peer,verify_host,"		\
			"allow_traps)"											\
		" select"												\
			" " ZBX_FS_UI64 ",type,snmp_community,snmp_oid," ZBX_FS_UI64 ",name,key_,delay,history,trends,"	\
			"status,value_type,trapper_hosts,units,snmpv3_securityname,snmpv3_securitylevel,"		\
			"snmpv3_authpassphrase,snmpv3_privpassphrase,formula,logtimefmt," ZBX_FS_UI64 ",valuemapid,"	\
			"params,ipmi_sensor,authtype,username,password,publickey,privatekey,flags,interfaceid,port,"	\
			"description,inventory_link,lifetime,snmpv3_authprotocol,snmpv3_privprotocol,"			\
			"snmpv3_contextname,evaltype,jmx_endpoint,master_itemid,timeout,url,query_fields,posts,"	\
			"status_codes,follow_redirects,post_type,http_proxy,headers,retrieve_mode,request_method,"	\
			"output_format,ssl_cert_file,ssl_key_file,ssl_key_password,verify_peer,verify_host,"		\
			"allow_traps"											\
		" from items"												\
		" where itemid=" ZBX_FS_UI64
#define CREATE(itemid, templateid)	DB_EXEC(SQL, itemid, hostid, templateid, templateid)
		CREATE(itemid_rsm_dns_nsip_discovery       , template_itemid_rsm_dns_nsip_discovery);
		CREATE(itemid_rsm_slv_dns_ns_avail_ns_ip   , template_itemid_rsm_slv_dns_ns_avail_ns_ip);
		CREATE(itemid_rsm_slv_dns_ns_downtime_ns_ip, template_itemid_rsm_slv_dns_ns_downtime_ns_ip);
#undef CREATE
#undef SQL

		/* LLD of NS-IP pairs: link item prototypes to discovery rules */
#define SQL	"insert into item_discovery set itemdiscoveryid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64 ","		\
		"parent_itemid=" ZBX_FS_UI64 ",key_='',lastcheck=0,ts_delete=0"
	DB_EXEC(SQL, DBget_maxid_num("item_discovery", 1), itemid_rsm_slv_dns_ns_avail_ns_ip   , itemid_rsm_dns_nsip_discovery);
	DB_EXEC(SQL, DBget_maxid_num("item_discovery", 1), itemid_rsm_slv_dns_ns_downtime_ns_ip, itemid_rsm_dns_nsip_discovery);
#undef SQL

		/* LLD of NS-IP pairs: link items to their prototypes */
#define MIGRATE(key_pattern, prototype_key, prototype_itemid)								\
															\
do															\
{															\
	/* flags 0x04 = ZBX_FLAG_DISCOVERY_CREATED */									\
	DB_EXEC("update"												\
			" items,"											\
			" items as prototype"										\
		" set"													\
			" items.name=prototype.name,"									\
			"items.templateid=null,"									\
			"items.flags=4,"										\
			"items.request_method=prototype.request_method"							\
		" where"												\
			" prototype.itemid=" ZBX_FS_UI64 " and"								\
			" items.hostid=prototype.hostid and"								\
			" items.itemid<>prototype.itemid and"								\
			" items.key_ like '%s'",									\
		prototype_itemid, key_pattern);										\
															\
	result2 = DBselect("select itemid"										\
			" from items"											\
			" where"											\
				" hostid=" ZBX_FS_UI64 " and"								\
				" itemid<>" ZBX_FS_UI64 " and"								\
				" key_ like '%s'",									\
			hostid, prototype_itemid, key_pattern);								\
															\
	if (NULL == result2)												\
		goto out;												\
															\
	while (NULL != (row = DBfetch(result2)))									\
	{														\
		zbx_uint64_t	itemid;											\
															\
		ZBX_STR2UINT64(itemid, row[0]);										\
															\
		DB_EXEC("insert into item_discovery set"								\
				" itemdiscoveryid=" ZBX_FS_UI64 ","							\
				"itemid=" ZBX_FS_UI64 ","								\
				"parent_itemid=" ZBX_FS_UI64 ","							\
				"key_='%s',"										\
				"lastcheck=0,"										\
				"ts_delete=0",										\
			DBget_maxid_num("item_discovery", 1), itemid, prototype_itemid, prototype_key);			\
	}														\
															\
	DBfree_result(result2);												\
	result2 = NULL;													\
															\
	/* TODO: handle disabled ns-ip pairs */										\
}															\
while (0)
		MIGRATE("rsm.slv.dns.ns.avail[%,%]"   , "rsm.slv.dns.ns.avail[{#NS},{#IP}]"   , itemid_rsm_slv_dns_ns_avail_ns_ip);
		MIGRATE("rsm.slv.dns.ns.downtime[%,%]", "rsm.slv.dns.ns.downtime[{#NS},{#IP}]", itemid_rsm_slv_dns_ns_downtime_ns_ip);
#undef MIGRATE

		/* LLD of NS-IP pairs: create trigger prototypes */
#define SQL	"insert into triggers (triggerid,expression,description,url,status,value,priority,lastchange,"		\
			"comments,error,templateid,type,state,flags,recovery_mode,recovery_expression,"			\
			"correlation_mode,correlation_tag,manual_close,opdata)"						\
		" select"												\
			" " ZBX_FS_UI64 ",'{" ZBX_FS_UI64 "}%s',description,url,status,value,priority,lastchange,"	\
			"comments,error," ZBX_FS_UI64 ",type,state,flags,recovery_mode,recovery_expression,"		\
			"correlation_mode,correlation_tag,manual_close,opdata"						\
		" from triggers"											\
		" where triggerid=" ZBX_FS_UI64
#define CREATE(triggerid, functionid, comparison, template_triggerid)							\
		DB_EXEC(SQL, triggerid, functionid, comparison, template_triggerid, template_triggerid)
		CREATE(triggerid_downtime_over_10 , functionid_downtime_over_10 , ">{$RSM.SLV.NS.DOWNTIME}*0.1" , temlpate_triggerid_downtime_over_10);
		CREATE(triggerid_downtime_over_25 , functionid_downtime_over_25 , ">{$RSM.SLV.NS.DOWNTIME}*0.25", temlpate_triggerid_downtime_over_25);
		CREATE(triggerid_downtime_over_50 , functionid_downtime_over_50 , ">{$RSM.SLV.NS.DOWNTIME}*0.5" , temlpate_triggerid_downtime_over_50);
		CREATE(triggerid_downtime_over_75 , functionid_downtime_over_75 , ">{$RSM.SLV.NS.DOWNTIME}*0.75", temlpate_triggerid_downtime_over_75);
		CREATE(triggerid_downtime_over_100, functionid_downtime_over_100, ">{$RSM.SLV.NS.DOWNTIME}"     , temlpate_triggerid_downtime_over_100);
#undef CREATE
#undef SQL

		/* LLD of NS-IP pairs: create functions for trigger prototypes */
#define SQL	"insert into functions set functionid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64 ",triggerid=" ZBX_FS_UI64 ",name='last',parameter=''"
		DB_EXEC(SQL, functionid_downtime_over_10 , itemid_rsm_slv_dns_ns_downtime_ns_ip, triggerid_downtime_over_10);
		DB_EXEC(SQL, functionid_downtime_over_25 , itemid_rsm_slv_dns_ns_downtime_ns_ip, triggerid_downtime_over_25);
		DB_EXEC(SQL, functionid_downtime_over_50 , itemid_rsm_slv_dns_ns_downtime_ns_ip, triggerid_downtime_over_50);
		DB_EXEC(SQL, functionid_downtime_over_75 , itemid_rsm_slv_dns_ns_downtime_ns_ip, triggerid_downtime_over_75);
		DB_EXEC(SQL, functionid_downtime_over_100, itemid_rsm_slv_dns_ns_downtime_ns_ip, triggerid_downtime_over_100);
#undef SQL

		/* LLD of NS-IP pairs: create dependencies between trigger prototypes */
#define SQL	"insert into trigger_depends set triggerdepid=" ZBX_FS_UI64 ",triggerid_down=" ZBX_FS_UI64 ",triggerid_up=" ZBX_FS_UI64
		DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_downtime_over_10, triggerid_downtime_over_25);
		DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_downtime_over_25, triggerid_downtime_over_50);
		DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_downtime_over_50, triggerid_downtime_over_75);
		DB_EXEC(SQL, DBget_maxid_num("trigger_depends", 1), triggerid_downtime_over_75, triggerid_downtime_over_100);
#undef SQL

		/* LLD of NS-IP pairs: link triggers to their prototypes */
#define MIGRATE(expression_pattern, prototype_triggerid)								\
															\
do															\
{															\
	/* flags 0x00 = ZBX_FLAG_DISCOVERY_NORMAL */									\
	result2 = DBselect("select"											\
				" triggers.triggerid"									\
			" from"												\
				" triggers"										\
				" left join functions on functions.triggerid=triggers.triggerid"			\
				" left join items on items.itemid=functions.itemid"					\
			" where"											\
				" items.hostid=" ZBX_FS_UI64 " and"							\
				" triggers.flags=0 and"									\
				" triggers.expression like '%s'",							\
			hostid, expression_pattern);									\
															\
	if (NULL == result2)												\
		goto out;												\
															\
	while (NULL != (row = DBfetch(result2)))									\
	{														\
		zbx_uint64_t	triggerid;										\
															\
		ZBX_STR2UINT64(triggerid, row[0]);									\
															\
		/* flags 0x04 = ZBX_FLAG_DISCOVERY_CREATED */								\
		DB_EXEC("update triggers set flags=4 where triggerid=" ZBX_FS_UI64, triggerid);				\
															\
		DB_EXEC("insert into trigger_discovery set triggerid=" ZBX_FS_UI64 ",parent_triggerid=" ZBX_FS_UI64,	\
			triggerid, prototype_triggerid);								\
	}														\
															\
	DBfree_result(result2);												\
	result2 = NULL;													\
}															\
while (0)
		MIGRATE("{%}>{$RSM.SLV.NS.DOWNTIME}*0.1" , triggerid_downtime_over_10);
		MIGRATE("{%}>{$RSM.SLV.NS.DOWNTIME}*0.25", triggerid_downtime_over_25);
		MIGRATE("{%}>{$RSM.SLV.NS.DOWNTIME}*0.5" , triggerid_downtime_over_50);
		MIGRATE("{%}>{$RSM.SLV.NS.DOWNTIME}*0.75", triggerid_downtime_over_75);
		MIGRATE("{%}>{$RSM.SLV.NS.DOWNTIME}"     , triggerid_downtime_over_100);
#undef MIGRATE
	}

	ret = SUCCEED;
out:
	DBfree_result(result2);
	DBfree_result(result);

	return ret;
}

/* 4050012, 24 - convert "<rsmhost>" hosts to use "Template DNSSEC Status" template */
static int	DBpatch_4050012_24(void)
{
	int		ret = FAIL;

	DB_RESULT	result = NULL;
	DB_ROW		row;

	zbx_uint64_t	tlds_groupid;
	zbx_uint64_t	template_hostid;

	zbx_uint64_t	template_itemid_rsm_slv_dnssec_avail;		/* itemid of "DNSSEC availability" item in "Template DNSSEC Status" template */
	zbx_uint64_t	template_itemid_rsm_slv_dnssec_rollweek;	/* itemid of "DNSSEC weekly unavailability" item in "Template DNSSEC Status" template */

	/* It was decided to setup monitoring from scratch rather than upgrade all the configuration, */
	/* therefore this patch may be obsolete and there's no guarantee that it works correctly.     */
	goto out;

	ONLY_SERVER();

	GET_HOST_GROUP_ID(tlds_groupid, "TLDs");
	GET_TEMPLATE_ID(template_hostid, "Template DNSSEC Status");

	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_slv_dnssec_avail   , "Template DNSSEC Status", "rsm.slv.dnssec.avail");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_slv_dnssec_rollweek, "Template DNSSEC Status", "rsm.slv.dnssec.rollweek");

	/* get hostid of all <rsmhost> hosts */
	result = DBselect("select hostid from hosts_groups where groupid=" ZBX_FS_UI64, tlds_groupid);

	if (NULL == result)
		goto out;

	while (NULL != (row = DBfetch(result)))
	{
		zbx_uint64_t	hostid;

		ZBX_STR2UINT64(hostid, row[0]);

#define SQL	"insert into hosts_templates set hosttemplateid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ",templateid=" ZBX_FS_UI64
		DB_EXEC(SQL, DBget_maxid_num("hosts_templates", 1), hostid, template_hostid);
#undef SQL

#define SQL	"update items set templateid=" ZBX_FS_UI64 ",request_method=0 where hostid=" ZBX_FS_UI64 " and key_='%s'"
		DB_EXEC(SQL, template_itemid_rsm_slv_dnssec_avail   , hostid, "rsm.slv.dnssec.avail");
		DB_EXEC(SQL, template_itemid_rsm_slv_dnssec_rollweek, hostid, "rsm.slv.dnssec.rollweek");
#undef SQL
	}

	ret = SUCCEED;
out:
	DBfree_result(result);

	return ret;
}

/* 4050012, 25 - convert "<rsmhost>" hosts to use "Template RDAP Status" template */
static int	DBpatch_4050012_25(void)
{
	int		ret = FAIL;

	DB_RESULT	result = NULL;
	DB_ROW		row;

	zbx_uint64_t	tlds_groupid;
	zbx_uint64_t	template_hostid;

	zbx_uint64_t	template_itemid_rsm_slv_rdap_avail;		/* itemid of "RDAP availability" item in "Template RDAP Status" template */
	zbx_uint64_t	template_itemid_rsm_slv_rdap_downtime;		/* itemid of "RDAP minutes of downtime" item in "Template RDAP Status" template */
	zbx_uint64_t	template_itemid_rsm_slv_rdap_rollweek;		/* itemid of "RDAP weekly unavailability" item in "Template RDAP Status" template */
	zbx_uint64_t	template_itemid_rsm_slv_rdap_rtt_performed;	/* itemid of "Number of performed monthly RDAP queries" item in "Template RDAP Status" template */
	zbx_uint64_t	template_itemid_rsm_slv_rdap_rtt_failed;	/* itemid of "Number of failed monthly RDAP queries" item in "Template RDAP Status" template */
	zbx_uint64_t	template_itemid_rsm_slv_rdap_rtt_pfailed;	/* itemid of "Ratio of failed monthly RDAP queries" item in "Template RDAP Status" template */

	/* It was decided to setup monitoring from scratch rather than upgrade all the configuration, */
	/* therefore this patch may be obsolete and there's no guarantee that it works correctly.     */
	goto out;

	ONLY_SERVER();

	GET_HOST_GROUP_ID(tlds_groupid, "TLDs");
	GET_TEMPLATE_ID(template_hostid, "Template RDAP Status");

	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_slv_rdap_avail        , "Template RDAP Status", "rsm.slv.rdap.avail");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_slv_rdap_downtime     , "Template RDAP Status", "rsm.slv.rdap.downtime");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_slv_rdap_rollweek     , "Template RDAP Status", "rsm.slv.rdap.rollweek");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_slv_rdap_rtt_performed, "Template RDAP Status", "rsm.slv.rdap.rtt.performed");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_slv_rdap_rtt_failed   , "Template RDAP Status", "rsm.slv.rdap.rtt.failed");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_slv_rdap_rtt_pfailed  , "Template RDAP Status", "rsm.slv.rdap.rtt.pfailed");

	/* get hostid of all <rsmhost> hosts */
	result = DBselect("select hostid from hosts_groups where groupid=" ZBX_FS_UI64, tlds_groupid);

	if (NULL == result)
		goto out;

	while (NULL != (row = DBfetch(result)))
	{
		zbx_uint64_t	hostid;

		ZBX_STR2UINT64(hostid, row[0]);

#define SQL	"insert into hosts_templates set hosttemplateid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ",templateid=" ZBX_FS_UI64
		DB_EXEC(SQL, DBget_maxid_num("hosts_templates", 1), hostid, template_hostid);
#undef SQL

#define SQL	"update items set templateid=" ZBX_FS_UI64 ",request_method=0 where hostid=" ZBX_FS_UI64 " and key_='%s'"
		DB_EXEC(SQL, template_itemid_rsm_slv_rdap_avail        , hostid, "rsm.slv.rdap.avail");
		DB_EXEC(SQL, template_itemid_rsm_slv_rdap_downtime     , hostid, "rsm.slv.rdap.downtime");
		DB_EXEC(SQL, template_itemid_rsm_slv_rdap_rollweek     , hostid, "rsm.slv.rdap.rollweek");
		DB_EXEC(SQL, template_itemid_rsm_slv_rdap_rtt_performed, hostid, "rsm.slv.rdap.rtt.performed");
		DB_EXEC(SQL, template_itemid_rsm_slv_rdap_rtt_failed   , hostid, "rsm.slv.rdap.rtt.failed");
		DB_EXEC(SQL, template_itemid_rsm_slv_rdap_rtt_pfailed  , hostid, "rsm.slv.rdap.rtt.pfailed");
#undef SQL
	}

	ret = SUCCEED;
out:
	DBfree_result(result);

	return ret;
}

/* 4050012, 26 - convert "<rsmhost>" hosts to use "Template RDDS Status" template */
static int	DBpatch_4050012_26(void)
{
	int		ret = FAIL;

	DB_RESULT	result = NULL;
	DB_ROW		row;

	zbx_uint64_t	tlds_groupid;
	zbx_uint64_t	template_hostid;

	zbx_uint64_t	template_itemid_rsm_slv_rdds_avail;		/* itemid of "RDDS availability" item in "Template RDDS Status" template */
	zbx_uint64_t	template_itemid_rsm_slv_rdds_downtime;		/* itemid of "RDDS minutes of downtime" item in "Template RDDS Status" template */
	zbx_uint64_t	template_itemid_rsm_slv_rdds_rollweek;		/* itemid of "RDDS weekly unavailability" item in "Template RDDS Status" template */
	zbx_uint64_t	template_itemid_rsm_slv_rdds_rtt_performed;	/* itemid of "Number of performed monthly RDDS queries" item in "Template RDDS Status" template */
	zbx_uint64_t	template_itemid_rsm_slv_rdds_rtt_failed;	/* itemid of "Number of failed monthly RDDS queries" item in "Template RDDS Status" template */
	zbx_uint64_t	template_itemid_rsm_slv_rdds_rtt_pfailed;	/* itemid of "Ratio of failed monthly RDDS queries" item in "Template RDDS Status" template */

	/* It was decided to setup monitoring from scratch rather than upgrade all the configuration, */
	/* therefore this patch may be obsolete and there's no guarantee that it works correctly.     */
	goto out;

	ONLY_SERVER();

	GET_HOST_GROUP_ID(tlds_groupid, "TLDs");
	GET_TEMPLATE_ID(template_hostid, "Template RDDS Status");

	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_slv_rdds_avail        , "Template RDDS Status", "rsm.slv.rdds.avail");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_slv_rdds_downtime     , "Template RDDS Status", "rsm.slv.rdds.downtime");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_slv_rdds_rollweek     , "Template RDDS Status", "rsm.slv.rdds.rollweek");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_slv_rdds_rtt_performed, "Template RDDS Status", "rsm.slv.rdds.rtt.performed");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_slv_rdds_rtt_failed   , "Template RDDS Status", "rsm.slv.rdds.rtt.failed");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_slv_rdds_rtt_pfailed  , "Template RDDS Status", "rsm.slv.rdds.rtt.pfailed");

	/* get hostid of all <rsmhost> hosts */
	result = DBselect("select hostid from hosts_groups where groupid=" ZBX_FS_UI64, tlds_groupid);

	if (NULL == result)
		goto out;

	while (NULL != (row = DBfetch(result)))
	{
		zbx_uint64_t	hostid;

		ZBX_STR2UINT64(hostid, row[0]);

#define SQL	"insert into hosts_templates set hosttemplateid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ",templateid=" ZBX_FS_UI64
		DB_EXEC(SQL, DBget_maxid_num("hosts_templates", 1), hostid, template_hostid);
#undef SQL

#define SQL	"update items set templateid=" ZBX_FS_UI64 ",request_method=0 where hostid=" ZBX_FS_UI64 " and key_='%s'"
		DB_EXEC(SQL, template_itemid_rsm_slv_rdds_avail        , hostid, "rsm.slv.rdds.avail");
		DB_EXEC(SQL, template_itemid_rsm_slv_rdds_downtime     , hostid, "rsm.slv.rdds.downtime");
		DB_EXEC(SQL, template_itemid_rsm_slv_rdds_rollweek     , hostid, "rsm.slv.rdds.rollweek");
		DB_EXEC(SQL, template_itemid_rsm_slv_rdds_rtt_performed, hostid, "rsm.slv.rdds.rtt.performed");
		DB_EXEC(SQL, template_itemid_rsm_slv_rdds_rtt_failed   , hostid, "rsm.slv.rdds.rtt.failed");
		DB_EXEC(SQL, template_itemid_rsm_slv_rdds_rtt_pfailed  , hostid, "rsm.slv.rdds.rtt.pfailed");
#undef SQL
	}

	ret = SUCCEED;
out:
	DBfree_result(result);

	return ret;
}

/* 4050012, 27 - convert "<probe>" hosts to use "Template Probe Status" template */
static int	DBpatch_4050012_27(void)
{
	int		ret = FAIL;

	DB_RESULT	result = NULL;
	DB_ROW		row;

	zbx_uint64_t	probes_groupid;
	zbx_uint64_t	template_hostid;
	zbx_uint64_t	template_applicationid_internal_errors;
	zbx_uint64_t	template_applicationid_probe_status;
	zbx_uint64_t	template_applicationid_configuration;
	zbx_uint64_t	template_itemid_probe_configvalue_rsm_ip4_enabled;
	zbx_uint64_t	template_itemid_probe_configvalue_rsm_ip6_enabled;

	zbx_uint64_t	count;

	/* It was decided to setup monitoring from scratch rather than upgrade all the configuration, */
	/* therefore this patch may be obsolete and there's no guarantee that it works correctly.     */
	goto out;

	ONLY_SERVER();

	GET_HOST_GROUP_ID(probes_groupid, "Probes");
	GET_TEMPLATE_ID(template_hostid, "Template Probe Status");

	GET_TEMPLATE_APPLICATION_ID(template_applicationid_internal_errors, "Template Probe Status", "Internal errors");
	GET_TEMPLATE_APPLICATION_ID(template_applicationid_probe_status   , "Template Probe Status", "Probe status");
	GET_TEMPLATE_APPLICATION_ID(template_applicationid_configuration  , "Template Probe Status", "Configuration");

	GET_TEMPLATE_ITEM_ID(template_itemid_probe_configvalue_rsm_ip4_enabled, "Template Probe Status", "probe.configvalue[RSM.IP4.ENABLED]");
	GET_TEMPLATE_ITEM_ID(template_itemid_probe_configvalue_rsm_ip6_enabled, "Template Probe Status", "probe.configvalue[RSM.IP6.ENABLED]");

	/* get hostid and host of all <probe> hosts */
	result = DBselect("select"
				" hosts.hostid,"
				"hosts.host"
			" from"
				" hosts"
				" left join hosts_groups on hosts_groups.hostid=hosts.hostid"
			" where"
				" hosts_groups.groupid=" ZBX_FS_UI64,
			probes_groupid);

	if (NULL == result)
		goto out;

	while (NULL != (row = DBfetch(result)))
	{
		zbx_uint64_t	probe_hostid;
		const char	*probe_host;
		zbx_uint64_t	applicationid_internal_errors;
		zbx_uint64_t	applicationid_probe_status;
		zbx_uint64_t	applicationid_configuration;
		zbx_uint64_t	itemid;					/* itemid of "probe.configvalue[..]" items */

		ZBX_STR2UINT64(probe_hostid, row[0]);
		probe_host = row[1];

		/* <probe> hosts already have these applications */
		GET_HOST_APPLICATION_ID(applicationid_internal_errors, probe_hostid, "Internal errors");
		GET_HOST_APPLICATION_ID(applicationid_probe_status   , probe_hostid, "Probe status");

		/* create "Configuration" application on <probe> host */
		applicationid_configuration = DBget_maxid_num("applications", 1);
		DB_EXEC("insert into applications"
				" set applicationid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ",name='%s',flags=0",
			applicationid_configuration, probe_hostid, "Configuration");

		/* unlink "Template <probe> Status" template from the probe host*/
		DB_EXEC("delete"
				" hosts_templates"
			" from"
				" hosts_templates"
				" left join hosts as templates on templates.hostid=hosts_templates.templateid"
			" where"
				" hosts_templates.hostid=" ZBX_FS_UI64 " and"
				" templates.host='Template %s Status'",
			probe_hostid, probe_host);

		/* link "Template Probe Status" to the <probe> host */
		DB_EXEC("insert into hosts_templates set"
				" hosttemplateid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ",templateid=" ZBX_FS_UI64,
			DBget_maxid_num("hosts_templates", 1), probe_hostid, template_hostid);

		/* remove old links between host applications and template applications */
#define SQL	"delete from application_template where applicationid=" ZBX_FS_UI64
		DB_EXEC(SQL, applicationid_internal_errors);
		DB_EXEC(SQL, applicationid_probe_status);
#undef SQL

		/* link host's applications with template's applications */
#define SQL	"insert into application_template set"									\
		" application_templateid=" ZBX_FS_UI64 ",applicationid=" ZBX_FS_UI64 ",templateid=" ZBX_FS_UI64
		DB_EXEC(SQL, DBget_maxid_num("application_template", 1), applicationid_internal_errors, template_applicationid_internal_errors);
		DB_EXEC(SQL, DBget_maxid_num("application_template", 1), applicationid_probe_status   , template_applicationid_probe_status);
		DB_EXEC(SQL, DBget_maxid_num("application_template", 1), applicationid_configuration  , template_applicationid_configuration);
#undef SQL

		/* move probe.configvalue[..] items from one "<rsmhost> <probe>" host to "<probe>" host, keep the history */
#define MIGRATE(key, template_itemid)											\
															\
do															\
{															\
		/* hosts.status 0 = HOST_STATUS_MONITORED */								\
		/* hosts.status 1 = HOST_STATUS_NOT_MONITORED */							\
		SELECT_VALUE_UINT64(											\
			itemid,												\
			"select"											\
				" items.itemid"										\
			" from"												\
				" items"										\
				" left join hosts on hosts.hostid=items.hostid"						\
			" where"											\
				" items.templateid is not null and"							\
				" items.key_='%s' and"									\
				" hosts.status in (0,1) and"								\
				" hosts.host like '%% %s'"								\
			" order by"											\
				" hosts.status asc"									\
			" limit 1",											\
			key, probe_host);										\
															\
		DB_EXEC("update items set hostid=" ZBX_FS_UI64 " where itemid=" ZBX_FS_UI64, probe_hostid, itemid);	\
															\
		DB_EXEC("update items_applications set applicationid=" ZBX_FS_UI64 " where itemid=" ZBX_FS_UI64,	\
			applicationid_configuration, itemid);								\
															\
		DB_EXEC("update"											\
				" items as item,"									\
				"items as template"									\
			" set"												\
				" item.delay=template.delay,"								\
				"item.templateid=template.itemid,"							\
				"item.request_method=template.request_method"						\
			" where"											\
				" item.itemid=" ZBX_FS_UI64 " and"							\
				" template.itemid=" ZBX_FS_UI64,							\
			itemid, template_itemid);									\
}															\
while (0)
		MIGRATE("probe.configvalue[RSM.IP4.ENABLED]", template_itemid_probe_configvalue_rsm_ip4_enabled);
		MIGRATE("probe.configvalue[RSM.IP6.ENABLED]", template_itemid_probe_configvalue_rsm_ip6_enabled);
#undef MIGRATE
	}

	/* delete probe.confivalue[..] items from "<rsmhost> <probe>" hosts, leave those that were moved to "<probe>" hosts */
#define SQL	"delete from items where key_='%s' and hostid<>" ZBX_FS_UI64 " and templateid<>" ZBX_FS_UI64
	DB_EXEC(SQL, "probe.configvalue[RSM.IP4.ENABLED]", template_hostid, template_itemid_probe_configvalue_rsm_ip4_enabled);
	DB_EXEC(SQL, "probe.configvalue[RSM.IP6.ENABLED]", template_hostid, template_itemid_probe_configvalue_rsm_ip6_enabled);
#undef SQL

	/* link rest of the items to the "Template Probe Status" template */
#define SQL	"update"												\
			" items as item,"										\
			"items as template"										\
		" set"													\
			" item.delay=template.delay,"									\
			"item.templateid=template.itemid,"								\
			"item.request_method=template.request_method"							\
		" where"												\
			" item.templateid is not null and"								\
			" item.key_='%s' and"										\
			" template.hostid=" ZBX_FS_UI64 " and"								\
			" template.key_='%s'"
#define MIGRATE(key)	DB_EXEC(SQL, key, template_hostid, key)
	MIGRATE("resolver.status[{$RSM.RESOLVER},{$RESOLVER.STATUS.TIMEOUT},{$RESOLVER.STATUS.TRIES},{$RSM.IP4.ENABLED},{$RSM.IP6.ENABLED}]");
	MIGRATE("rsm.probe.status[automatic,\"{$RSM.IP4.ROOTSERVERS1}\",\"{$RSM.IP6.ROOTSERVERS1}\"]");
	MIGRATE("rsm.probe.status[manual]");
	MIGRATE("rsm.errors");
#undef MIGRATE
#undef SQL

	/* make sure that "Configuration" application on "<rsmhost> <probe>" hosts isn't used anymore */
	SELECT_VALUE_UINT64(
		count,
		"select"
			" count(*)"
		" from"
			" items_applications"
			" left join applications on applications.applicationid=items_applications.applicationid"
			" left join hosts_groups on hosts_groups.hostid=applications.hostid"
			" left join hstgrp on hstgrp.groupid=hosts_groups.groupid"
		" where"
			" applications.name='%s' and"
			" hstgrp.name='%s'",
		"Configuration", "TLD Probe results");	/* workaround for "requires at least one argument in a variadic macro" warning */

	if (0 != count)
	{
		zabbix_log(LOG_LEVEL_CRIT, "%s() on line %d: found item(s) that are linked to the 'Configuration' application", __func__, __LINE__);
		goto out;
	}

	/* remove "Configuration" application from "<rsmhost> <probe>" hosts */
	DB_EXEC("delete"
			" applications"
		" from"
			" applications"
			" left join hosts_groups on hosts_groups.hostid=applications.hostid"
			" left join hstgrp on hstgrp.groupid=hosts_groups.groupid"
		" where"
			" applications.name='Configuration' and"
			" hstgrp.name='TLD Probe results'");

	ret = SUCCEED;
out:
	DBfree_result(result);

	return ret;
}

/* 4050012, 28 - convert "<rsmhost>" hosts to use "Template Config History" template */
static int	DBpatch_4050012_28(void)
{
	int		ret = FAIL;

	DB_RESULT	result = NULL;
	DB_ROW		row;

	zbx_uint64_t	tlds_groupid;
	zbx_uint64_t	template_hostid;

	zbx_uint64_t	template_itemid_dns_tcp_enabled;	/* itemid of "DNS TCP enabled/disabled" item in "Template Config History" template */
	zbx_uint64_t	template_itemid_dns_udp_enabled;	/* itemid of "DNS UDP enabled/disabled" item in "Template Config History" template */
	zbx_uint64_t	template_itemid_dnssec_enabled;		/* itemid of "DNSSEC enabled/disabled" item in "Template Config History" template */
	zbx_uint64_t	template_itemid_rdap_enabled;		/* itemid of "RDAP enabled/disabled" item in "Template Config History" template */
	zbx_uint64_t	template_itemid_rdds_enabled;		/* itemid of "RDDS enabled/disabled" item in "Template Config History" template */

	/* It was decided to setup monitoring from scratch rather than upgrade all the configuration, */
	/* therefore this patch may be obsolete and there's no guarantee that it works correctly.     */
	goto out;

	ONLY_SERVER();

	GET_HOST_GROUP_ID(tlds_groupid, "TLDs");
	GET_TEMPLATE_ID(template_hostid, "Template Config History");

	GET_TEMPLATE_ITEM_ID(template_itemid_dns_tcp_enabled, "Template Config History", "dns.tcp.enabled");
	GET_TEMPLATE_ITEM_ID(template_itemid_dns_udp_enabled, "Template Config History", "dns.udp.enabled");
	GET_TEMPLATE_ITEM_ID(template_itemid_dnssec_enabled , "Template Config History", "dnssec.enabled");
	GET_TEMPLATE_ITEM_ID(template_itemid_rdap_enabled   , "Template Config History", "rdap.enabled");
	GET_TEMPLATE_ITEM_ID(template_itemid_rdds_enabled   , "Template Config History", "rdds.enabled");

	/* get hostid of all <rsmhost> hosts */
	result = DBselect("select hostid from hosts_groups where groupid=" ZBX_FS_UI64, tlds_groupid);

	if (NULL == result)
		goto out;

	while (NULL != (row = DBfetch(result)))
	{
		zbx_uint64_t	hostid;

		ZBX_STR2UINT64(hostid, row[0]);

#define SQL	"insert into hosts_templates set hosttemplateid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ",templateid=" ZBX_FS_UI64
		DB_EXEC(SQL, DBget_maxid_num("hosts_templates", 1), hostid, template_hostid);
#undef SQL

#define SQL	"insert into items (itemid,type,snmp_community,snmp_oid,hostid,name,key_,delay,history,trends,"		\
			"status,value_type,trapper_hosts,units,snmpv3_securityname,snmpv3_securitylevel,"		\
			"snmpv3_authpassphrase,snmpv3_privpassphrase,formula,logtimefmt,templateid,valuemapid,"		\
			"params,ipmi_sensor,authtype,username,password,publickey,privatekey,flags,interfaceid,port,"	\
			"description,inventory_link,lifetime,snmpv3_authprotocol,snmpv3_privprotocol,"			\
			"snmpv3_contextname,evaltype,jmx_endpoint,master_itemid,timeout,url,query_fields,posts,"	\
			"status_codes,follow_redirects,post_type,http_proxy,headers,retrieve_mode,request_method,"	\
			"output_format,ssl_cert_file,ssl_key_file,ssl_key_password,verify_peer,verify_host,"		\
			"allow_traps)"											\
		" select"												\
			" " ZBX_FS_UI64 ",type,snmp_community,snmp_oid," ZBX_FS_UI64 ",name,key_,delay,history,trends,"	\
			"status,value_type,trapper_hosts,units,snmpv3_securityname,snmpv3_securitylevel,"		\
			"snmpv3_authpassphrase,snmpv3_privpassphrase,formula,logtimefmt," ZBX_FS_UI64 ",valuemapid,"	\
			"params,ipmi_sensor,authtype,username,password,publickey,privatekey,flags,interfaceid,port,"	\
			"description,inventory_link,lifetime,snmpv3_authprotocol,snmpv3_privprotocol,"			\
			"snmpv3_contextname,evaltype,jmx_endpoint,master_itemid,timeout,url,query_fields,posts,"	\
			"status_codes,follow_redirects,post_type,http_proxy,headers,retrieve_mode,request_method,"	\
			"output_format,ssl_cert_file,ssl_key_file,ssl_key_password,verify_peer,verify_host,"		\
			"allow_traps"											\
		" from items"												\
		" where itemid=" ZBX_FS_UI64
#define CREATE(templateid)	DB_EXEC(SQL, DBget_maxid_num("items", 1), hostid, templateid, templateid)
		CREATE(template_itemid_dns_tcp_enabled);
		CREATE(template_itemid_dns_udp_enabled);
		CREATE(template_itemid_dnssec_enabled);
		CREATE(template_itemid_rdap_enabled);
		CREATE(template_itemid_rdds_enabled);
#undef CREATE
#undef SQL
	}

	ret = SUCCEED;
out:
	DBfree_result(result);

	return ret;
}

/* 4050012, 29 - convert "Template <rsmhost>" templates into "Template Rsmhost Config <rsmhost>", link to "<rsmhost>" hosts */
static int	DBpatch_4050012_29(void)
{
	int		ret = FAIL;

	DB_RESULT	result = NULL;
	DB_ROW		row;

	/* It was decided to setup monitoring from scratch rather than upgrade all the configuration, */
	/* therefore this patch may be obsolete and there's no guarantee that it works correctly.     */
	goto out;

	ONLY_SERVER();

	/* status 0 = HOST_STATUS_MONITORED */
	/* status 1 = HOST_STATUS_NOT_MONITORED */
	/* status 3 = HOST_STATUS_TEMPLATE */
	result = DBselect("select"
				" rsmhosts.hostid,"
				"rsmhosts.host,"
				"templates.hostid,"
				"nsip_items.nsip_list,"
				"trim('\"' from substring_index(substring_index(rdds_items.key_,',',-2),',',1)) as rdds43_servers,"
				"trim('\"' from substring_index(substring_index(rdds_items.key_,',',-1),']',1)) as rdds80_servers"
			" from"
				" hosts as rsmhosts"
				" left join hosts_groups on hosts_groups.hostid=rsmhosts.hostid"
				" left join hstgrp on hstgrp.groupid=hosts_groups.groupid"
				","
				" hosts as templates"
				" left join ("
					"select"
						" hostid,"
						"group_concat("
							"substring_index(substring_index(key_,',',-2),']',1)"
							" order by itemid asc"
							" separator ' '"
						") as nsip_list"
					" from items"
					" where key_ like 'rsm.dns.udp.rtt[%%,%%]'"
					" group by hostid"
				") as nsip_items on nsip_items.hostid=templates.hostid"
				" left join items as rdds_items on rdds_items.hostid=templates.hostid and rdds_items.key_ like 'rsm.rdds[%%,%%,%%]'"
			" where"
				" hstgrp.name='TLDs' and"
				" rsmhosts.status in (0,1) and"
				" templates.status=3 and"
				" templates.host=concat('Template ', rsmhosts.host)");

	if (NULL == result)
		goto out;

	while (NULL != (row = DBfetch(result)))
	{
		zbx_uint64_t	rsmhost_hostid;
		const char	*rsmhost_host;
		zbx_uint64_t	template_hostid;
		const char	*nsip_list;
		const char	*rdds43_servers;
		const char	*rdds80_servers;
		zbx_uint64_t	count;

		ZBX_STR2UINT64(rsmhost_hostid, row[0]);
		rsmhost_host = row[1];
		ZBX_STR2UINT64(template_hostid, row[2]);
		nsip_list = row[3];
		rdds43_servers = row[4];
		rdds80_servers = row[5];

		/* add missing macros */
#define SQL	"insert into hostmacro set hostmacroid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ",macro='%s',value='%s',description='%s'"
		DB_EXEC(SQL, DBget_maxid_num("hostmacro", 1), template_hostid, "{$RSM.DNS.NAME.SERVERS}", nsip_list,
			"List of Name Server (name, IP pairs) to monitor");
		DB_EXEC(SQL, DBget_maxid_num("hostmacro", 1), template_hostid, "{$RSM.TLD.DNS.UDP.ENABLED}", "1",
			"Indicates whether DNS UDP enabled for this TLD");
		DB_EXEC(SQL, DBget_maxid_num("hostmacro", 1), template_hostid, "{$RSM.TLD.DNS.TCP.ENABLED}", "1",
			"Indicates whether DNS TCP enabled for this TLD");
		DB_EXEC(SQL, DBget_maxid_num("hostmacro", 1), template_hostid, "{$RSM.TLD.RDDS.43.SERVERS}", rdds43_servers,
			"List of RDDS43 server host names as candidates for a test");
		DB_EXEC(SQL, DBget_maxid_num("hostmacro", 1), template_hostid, "{$RSM.TLD.RDDS.80.SERVERS}", rdds80_servers,
			"List of Web Whois server host names as candidates for a test");
#undef SQL

		/* delete "rdds.enabled" items from "<rsmhost> <probe>" hosts that are linked to a template */
		DB_EXEC("delete"
				" items"
			" from"
				" items"
				" left join items as template_items on template_items.itemid=items.templateid"
				" left join hosts as templates on templates.hostid=template_items.hostid"
			" where"
				" templates.hostid=" ZBX_FS_UI64 " and"
				" template_items.key_='rdds.enabled'",
			template_hostid);

		/* make sure that there are no items that have not been migrated */
		SELECT_VALUE_UINT64(
			count,
			"select"
				" count(*)"
			" from"
				" items"
				" left join items as template_items on template_items.itemid=items.templateid"
			" where"
				" template_items.hostid=" ZBX_FS_UI64,
			template_hostid);

		if (0 != count)
		{
			zabbix_log(LOG_LEVEL_CRIT, "%s() on line %d: found item(s) that are linked to the template", __func__, __LINE__);
			goto out;
		}

		/* delete template's items */
		DB_EXEC("delete from items where hostid=" ZBX_FS_UI64, template_hostid);

		/* make sure that there are no applications that have not been migrated */
		SELECT_VALUE_UINT64(
			count,
			"select"
				" count(*)"
			" from"
				" applications"
				" inner join application_template on application_template.templateid=applications.applicationid"
			" where"
				" applications.hostid=" ZBX_FS_UI64,
			template_hostid);

		if (0 != count)
		{
			zabbix_log(LOG_LEVEL_CRIT, "%s() on line %d: found application(s) that are linked to the template", __func__, __LINE__);
			goto out;
		}

		/* delete template's applications */
		DB_EXEC("delete from applications where applications.hostid=" ZBX_FS_UI64, template_hostid);

		/* link template to <rsmhost> host */
		DB_EXEC("insert into hosts_templates set hosttemplateid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ",templateid=" ZBX_FS_UI64,
			DBget_maxid_num("hosts_templates", 1), rsmhost_hostid, template_hostid);

		/* rename the template */
		DB_EXEC("update hosts set"
				" host='Template Rsmhost Config %s',"
				"name='Template Rsmhost Config %s'"
			" where hostid=" ZBX_FS_UI64,
			rsmhost_host, rsmhost_host, template_hostid);
	}

	ret = SUCCEED;
out:
	DBfree_result(result);

	return ret;
}

static int	DBpatch_4050012_30_delete_template(zbx_uint64_t template_hostid)
{
	int		ret = FAIL;
	zbx_uint64_t	count;

	/* make sure that there are no items that have not been migrated */
	SELECT_VALUE_UINT64(
		count,
		"select"
			" count(*)"
		" from"
			" items as probe_items"
			" left join items as template_items on template_items.itemid=probe_items.templateid"
		" where"
			" template_items.hostid=" ZBX_FS_UI64,
		template_hostid);

	if (0 != count)
	{
		zabbix_log(LOG_LEVEL_CRIT, "%s() on line %d: found item(s) that are linked to the template", __func__, __LINE__);
		goto out;
	}

	/* delete template's items */
	DB_EXEC("delete from items where hostid=" ZBX_FS_UI64, template_hostid);

	/* make sure that there are no applications that have not been migrated */
	SELECT_VALUE_UINT64(
		count,
		"select"
			" count(*)"
		" from"
			" applications"
			" inner join application_template on application_template.templateid=applications.applicationid"
		" where"
			" applications.hostid=" ZBX_FS_UI64,
		template_hostid);

	/* delete template's applications */
	DB_EXEC("delete from applications where applications.hostid=" ZBX_FS_UI64, template_hostid);

	if (0 != count)
	{
		zabbix_log(LOG_LEVEL_CRIT, "%s() on line %d: found application(s) that are linked to the template", __func__, __LINE__);
		goto out;
	}

	/* delete the template */
	DB_EXEC("delete from hosts where hostid=" ZBX_FS_UI64, template_hostid);

	ret = SUCCEED;
out:
	return ret;
}

/* 4050012, 30 - delete "Template <probe> Status" templates */
static int	DBpatch_4050012_30(void)
{
	int		ret = FAIL;

	DB_RESULT	result = NULL;
	DB_ROW		row;

	/* It was decided to setup monitoring from scratch rather than upgrade all the configuration, */
	/* therefore this patch may be obsolete and there's no guarantee that it works correctly.     */
	goto out;

	ONLY_SERVER();

	/* get hostid of "Template <probe> Status" templates */
	result = DBselect("select"
				" templates.hostid"
			" from"
				" hosts as probe_hosts"
				" left join hosts_groups on hosts_groups.hostid=probe_hosts.hostid"
				" left join hstgrp on hstgrp.groupid=hosts_groups.groupid"
				" left join hosts as templates on templates.host=concat('Template ', probe_hosts.host, ' Status')"
			" where "
				" hstgrp.name='Probes'");

	if (NULL == result)
		goto out;

	while (NULL != (row = DBfetch(result)))
	{
		zbx_uint64_t	template_hostid;

		ZBX_STR2UINT64(template_hostid, row[0]);

		CHECK(DBpatch_4050012_30_delete_template(template_hostid));
	}

	ret = SUCCEED;
out:
	DBfree_result(result);

	return ret;
}

/* 4050012, 31 - delete "Template Probe Errors" templates */
static int	DBpatch_4050012_31(void)
{
	int		ret = FAIL;
	zbx_uint64_t	template_hostid;

	/* It was decided to setup monitoring from scratch rather than upgrade all the configuration, */
	/* therefore this patch may be obsolete and there's no guarantee that it works correctly.     */
	goto out;

	ONLY_SERVER();

	GET_TEMPLATE_ID(template_hostid, "Template Probe Errors");

	DB_EXEC("delete"
			" triggers"
		" from"
			" triggers"
			" left join functions on functions.triggerid=triggers.triggerid"
			" left join items on items.itemid=functions.itemid"
		" where"
			" items.hostid=" ZBX_FS_UI64,
		template_hostid);

	CHECK(DBpatch_4050012_30_delete_template(template_hostid));

	ret = SUCCEED;
out:
	return ret;
}

/* 4050012, 32 - rename "Template <probe>" template into "Template Probe Config <probe>", link to "<probe>" hosts */
static int	DBpatch_4050012_32(void)
{
	int		ret = FAIL;

	DB_RESULT	result = NULL;
	DB_ROW		row;

	/* It was decided to setup monitoring from scratch rather than upgrade all the configuration, */
	/* therefore this patch may be obsolete and there's no guarantee that it works correctly.     */
	goto out;

	ONLY_SERVER();

	/* get hostid of "Template <probe> Status" templates */
	result = DBselect("select"
				" templates.hostid,"
				"probe_hosts.hostid,"
				"probe_hosts.host"
			" from"
				" hosts as probe_hosts"
				" left join hosts_groups on hosts_groups.hostid=probe_hosts.hostid"
				" left join hstgrp on hstgrp.groupid=hosts_groups.groupid"
				" left join hosts as templates on templates.host=concat('Template ', probe_hosts.host)"
			" where "
				" hstgrp.name='Probes'");

	if (NULL == result)
		goto out;

	while (NULL != (row = DBfetch(result)))
	{
		zbx_uint64_t	template_hostid;
		zbx_uint64_t	probe_hostid;
		const char	*probe_host;

		ZBX_STR2UINT64(template_hostid, row[0]);
		ZBX_STR2UINT64(probe_hostid, row[1]);
		probe_host = row[2];

		DB_EXEC("update hosts set"
				" host='Template Probe Config %s',"
				"name='Template Probe Config %s'"
			" where hostid=" ZBX_FS_UI64,
			probe_host, probe_host, template_hostid);

		DB_EXEC("insert into hosts_templates set hosttemplateid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ",templateid=" ZBX_FS_UI64,
			DBget_maxid_num("hosts_templates", 1), probe_hostid, template_hostid);
	}

	ret = SUCCEED;
out:
	DBfree_result(result);

	return ret;
}

static int	DBpatch_4050014(void)
{
	DB_ROW		row;
	DB_RESULT	result;
	int		ret = SUCCEED;
	char		*sql = NULL, *name = NULL, *name_esc;
	size_t		sql_alloc = 0, sql_offset = 0;

	if (0 == (program_type & ZBX_PROGRAM_TYPE_SERVER))
		return SUCCEED;

	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	result = DBselect(
			"select wf.widget_fieldid,wf.name"
			" from widget_field wf,widget w"
			" where wf.widgetid=w.widgetid"
				" and w.type='navtree'"
				" and wf.name like 'map.%%' or wf.name like 'mapid.%%'"
			);

	while (NULL != (row = DBfetch(result)))
	{
		if (0 == strncmp(row[1], "map.", 4))
		{
			name = zbx_dsprintf(name, "navtree.%s", row[1] + 4);
		}
		else
		{
			name = zbx_dsprintf(name, "navtree.sys%s", row[1]);
		}

		name_esc = DBdyn_escape_string_len(name, 255);

		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"update widget_field set name='%s' where widget_fieldid=%s;\n", name_esc, row[0]);

		zbx_free(name_esc);

		if (SUCCEED != (ret = DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset)))
			goto out;
	}

	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	if (16 < sql_offset && ZBX_DB_OK > DBexecute("%s", sql))
		ret = FAIL;
out:
	DBfree_result(result);
	zbx_free(sql);
	zbx_free(name);

	return ret;
}

static int	DBpatch_4050015(void)
{
	DB_RESULT		result;
	DB_ROW			row;
	zbx_uint64_t		time_period_id, every;
	int			invalidate = 0;
	const ZBX_TABLE		*timeperiods;
	const ZBX_FIELD		*field;

	if (NULL != (timeperiods = DBget_table("timeperiods")) &&
			NULL != (field = DBget_field(timeperiods, "every")))
	{
		ZBX_STR2UINT64(every, field->default_value);
	}
	else
	{
		THIS_SHOULD_NEVER_HAPPEN;
		return FAIL;
	}

	result = DBselect("select timeperiodid from timeperiods where every=0");

	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(time_period_id, row[0]);

		zabbix_log(LOG_LEVEL_WARNING, "Invalid maintenance time period found: "ZBX_FS_UI64
				", changing \"every\" to "ZBX_FS_UI64, time_period_id, every);
		invalidate = 1;
	}

	DBfree_result(result);

	if (0 != invalidate &&
			ZBX_DB_OK > DBexecute("update timeperiods set every=1 where timeperiodid!=0 and every=0"))
		return FAIL;

	return SUCCEED;
}

static int	DBpatch_4050016(void)
{
	const ZBX_TABLE	table =
			{"media_type_message", "mediatype_messageid", 0,
				{
					{"mediatype_messageid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"mediatypeid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"eventsource", NULL, NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
					{"recovery", NULL, NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
					{"subject", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
					{"message", "", NULL, NULL, 0, ZBX_TYPE_SHORTTEXT, ZBX_NOTNULL, 0},
					{0}
				},
				NULL
			};

	return DBcreate_table(&table);
}

static int	DBpatch_4050017(void)
{
	const ZBX_FIELD	field = {"mediatypeid", NULL, "media_type", "mediatypeid", 0, 0, 0, ZBX_FK_CASCADE_DELETE};

	return DBadd_foreign_key("media_type_message", 1, &field);
}

static int	DBpatch_4050018(void)
{
	return DBcreate_index("media_type_message", "media_type_message_1", "mediatypeid,eventsource,recovery", 1);
}

static int	DBpatch_4050019(void)
{
	const ZBX_FIELD	field = {"default_msg", "1", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	return DBset_default("opmessage", &field);
}

static int	DBpatch_4050020(void)
{
	DB_ROW		row;
	DB_RESULT	result;
	zbx_uint64_t	operationid;
	int		ret = SUCCEED, res, col;
	char		*subject, *message;

	if (0 == (program_type & ZBX_PROGRAM_TYPE_SERVER))
		return SUCCEED;

	result = DBselect(
			"select m.operationid,o.recovery,a.def_shortdata,a.def_longdata,a.r_shortdata,a.r_longdata,"
			"a.ack_shortdata,a.ack_longdata from opmessage m"
			" join operations o on m.operationid=o.operationid"
			" left join actions a on o.actionid=a.actionid"
			" where m.default_msg='1' and o.recovery in (0,1,2)");

	while (NULL != (row = DBfetch(result)))
	{
		col = 2 + (atoi(row[1]) * 2);
		subject = DBdyn_escape_string(row[col]);
		message = DBdyn_escape_string(row[col + 1]);
		ZBX_DBROW2UINT64(operationid, row[0]);

		res = DBexecute("update opmessage set subject='%s',message='%s',default_msg='0'"
				" where operationid=" ZBX_FS_UI64, subject, message, operationid);

		zbx_free(subject);
		zbx_free(message);

		if (ZBX_DB_OK > res)
		{
			ret = FAIL;
			break;
		}
	}
	DBfree_result(result);

	return ret;
}

static int	DBpatch_4050021(void)
{
	char	*messages[3][3][4] =
			{
				{
					{
						"Problem started at {EVENT.TIME} on {EVENT.DATE}\n"
						"Problem name: {EVENT.NAME}\n"
						"Host: {HOST.NAME}\n"
						"Severity: {EVENT.SEVERITY}\n"
						"Operational data: {EVENT.OPDATA}\n"
						"Original problem ID: {EVENT.ID}\n"
						"{TRIGGER.URL}"
						,
						"<b>Problem started</b> at {EVENT.TIME} on {EVENT.DATE}<br>"
						"<b>Problem name:</b> {EVENT.NAME}<br>"
						"<b>Host:</b> {HOST.NAME}<br>"
						"<b>Severity:</b> {EVENT.SEVERITY}<br>"
						"<b>Operational data:</b> {EVENT.OPDATA}<br>"
						"<b>Original problem ID:</b> {EVENT.ID}<br>"
						"{TRIGGER.URL}"
						,
						"{EVENT.SEVERITY}: {EVENT.NAME}\n"
						"Host: {HOST.NAME}\n"
						"{EVENT.DATE} {EVENT.TIME}"
						,
						"Problem: {EVENT.NAME}"
					},
					{
						"Problem has been resolved at "
						"{EVENT.RECOVERY.TIME} on {EVENT.RECOVERY.DATE}\n"
						"Problem name: {EVENT.NAME}\n"
						"Host: {HOST.NAME}\n"
						"Severity: {EVENT.SEVERITY}\n"
						"Original problem ID: {EVENT.ID}\n"
						"{TRIGGER.URL}"
						,
						"<b>Problem has been resolved</b> at {EVENT.RECOVERY.TIME} on "
						"{EVENT.RECOVERY.DATE}<br>"
						"<b>Problem name:</b> {EVENT.NAME}<br>"
						"<b>Host:</b> {HOST.NAME}<br>"
						"<b>Severity:</b> {EVENT.SEVERITY}<br>"
						"<b>Original problem ID:</b> {EVENT.ID}<br>"
						"{TRIGGER.URL}"
						,
						"RESOLVED: {EVENT.NAME}\n"
						"Host: {HOST.NAME}\n"
						"{EVENT.DATE} {EVENT.TIME}"
						,
						"Resolved: {EVENT.NAME}"
					},
					{
						"{USER.FULLNAME} {EVENT.UPDATE.ACTION} problem at "
						"{EVENT.UPDATE.DATE} {EVENT.UPDATE.TIME}.\n"
						"{EVENT.UPDATE.MESSAGE}\n"
						"\n"
						"Current problem status is {EVENT.STATUS}, acknowledged: "
						"{EVENT.ACK.STATUS}."
						,
						"<b>{USER.FULLNAME} {EVENT.UPDATE.ACTION} problem</b> at "
						"{EVENT.UPDATE.DATE} {EVENT.UPDATE.TIME}.<br>"
						"{EVENT.UPDATE.MESSAGE}<br>"
						"<br>"
						"<b>Current problem status:</b> {EVENT.STATUS}<br>"
						"<b>Acknowledged:</b> {EVENT.ACK.STATUS}."
						,
						"{USER.FULLNAME} {EVENT.UPDATE.ACTION} problem at "
						"{EVENT.UPDATE.DATE} {EVENT.UPDATE.TIME}"
						,
						"Updated problem: {EVENT.NAME}"
					}
				},
				{
					{
						"Discovery rule: {DISCOVERY.RULE.NAME}\n"
						"\n"
						"Device IP: {DISCOVERY.DEVICE.IPADDRESS}\n"
						"Device DNS: {DISCOVERY.DEVICE.DNS}\n"
						"Device status: {DISCOVERY.DEVICE.STATUS}\n"
						"Device uptime: {DISCOVERY.DEVICE.UPTIME}\n"
						"\n"
						"Device service name: {DISCOVERY.SERVICE.NAME}\n"
						"Device service port: {DISCOVERY.SERVICE.PORT}\n"
						"Device service status: {DISCOVERY.SERVICE.STATUS}\n"
						"Device service uptime: {DISCOVERY.SERVICE.UPTIME}"
						,
						"<b>Discovery rule:</b> {DISCOVERY.RULE.NAME}<br>"
						"<br>"
						"<b>Device IP:</b> {DISCOVERY.DEVICE.IPADDRESS}<br>"
						"<b>Device DNS:</b> {DISCOVERY.DEVICE.DNS}<br>"
						"<b>Device status:</b> {DISCOVERY.DEVICE.STATUS}<br>"
						"<b>Device uptime:</b> {DISCOVERY.DEVICE.UPTIME}<br>"
						"<br>"
						"<b>Device service name:</b> {DISCOVERY.SERVICE.NAME}<br>"
						"<b>Device service port:</b> {DISCOVERY.SERVICE.PORT}<br>"
						"<b>Device service status:</b> {DISCOVERY.SERVICE.STATUS}<br>"
						"<b>Device service uptime:</b> {DISCOVERY.SERVICE.UPTIME}"
						,
						"Discovery: {DISCOVERY.DEVICE.STATUS} {DISCOVERY.DEVICE.IPADDRESS}"
						,
						"Discovery: {DISCOVERY.DEVICE.STATUS} {DISCOVERY.DEVICE.IPADDRESS}"
					},
					{NULL, NULL, NULL, NULL},
					{NULL, NULL, NULL, NULL}
				},
				{
					{
						"Host name: {HOST.HOST}\n"
						"Host IP: {HOST.IP}\n"
						"Agent port: {HOST.PORT}"
						,
						"<b>Host name:</b> {HOST.HOST}<br>"
						"<b>Host IP:</b> {HOST.IP}<br>"
						"<b>Agent port:</b> {HOST.PORT}"
						,
						"Autoregistration: {HOST.HOST}\n"
						"Host IP: {HOST.IP}\n"
						"Agent port: {HOST.PORT}"
						,
						"Autoregistration: {HOST.HOST}"
					},
					{NULL, NULL, NULL, NULL},
					{NULL, NULL, NULL, NULL}
				}
			};
	int		ret = SUCCEED, res;
	DB_ROW		row;
	DB_RESULT	result;
	zbx_uint64_t	mediatypeid, mediatypemessageid = 1;
	int		content_type, i, k;
	char		*msg_esc = NULL, *subj_esc = NULL;

	if (0 == (program_type & ZBX_PROGRAM_TYPE_SERVER))
		return SUCCEED;

	result = DBselect("select mediatypeid,type,content_type from media_type");

	while (NULL != (row = DBfetch(result)))
	{
		ZBX_DBROW2UINT64(mediatypeid, row[0]);

		switch (atoi(row[1]))
		{
			case MEDIA_TYPE_SMS:
				content_type = 2;
				break;
			case MEDIA_TYPE_EMAIL:
				content_type = atoi(row[2]);
				break;
			default:
				content_type = 0;
		}

		for (i = 0; 2 >= i; i++)
		{
			for (k = 0; 2 >= k; k++)
			{
				if (NULL != messages[i][k][0])
				{
					msg_esc = DBdyn_escape_string(messages[i][k][content_type]);
					subj_esc = content_type == 2 ? NULL : DBdyn_escape_string(messages[i][k][3]);

					res = DBexecute(
							"insert into media_type_message"
							" (mediatype_messageid,mediatypeid,eventsource,recovery,"
							"subject,message)"
							" values (" ZBX_FS_UI64 "," ZBX_FS_UI64 ",%i,%i,'%s','%s')",
							mediatypemessageid++, mediatypeid, i, k,
							ZBX_NULL2EMPTY_STR(subj_esc), msg_esc);

					zbx_free(msg_esc);
					zbx_free(subj_esc);

					if (ZBX_DB_OK > res)
					{
						ret = FAIL;
						goto out;
					}
				}
			}
		}
	}
out:
	DBfree_result(result);

	return ret;
}

static int	DBpatch_4050022(void)
{
	return DBdrop_field("actions", "def_shortdata");
}

static int	DBpatch_4050023(void)
{
	return DBdrop_field("actions", "def_longdata");
}

static int	DBpatch_4050024(void)
{
	return DBdrop_field("actions", "r_shortdata");
}

static int	DBpatch_4050025(void)
{
	return DBdrop_field("actions", "r_longdata");
}

static int	DBpatch_4050026(void)
{
	return DBdrop_field("actions", "ack_shortdata");
}

static int	DBpatch_4050027(void)
{
	return DBdrop_field("actions", "ack_longdata");
}

static int	DBpatch_4050028(void)
{
	const ZBX_TABLE table =
		{"module", "moduleid", 0,
			{
				{"moduleid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"id", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
				{"relative_path", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
				{"status", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
				{"config", "", NULL, NULL, 0, ZBX_TYPE_SHORTTEXT, ZBX_NOTNULL, 0},
				{0}
			},
			NULL
		};

	return DBcreate_table(&table);
}

static int	DBpatch_4050030(void)
{
	return SUCCEED;
}

static int	DBpatch_4050031(void)
{
	const ZBX_TABLE table =
			{"task_data", "taskid", 0,
				{
					{"taskid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"type", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
					{"data", "", NULL, NULL, 0, ZBX_TYPE_SHORTTEXT, ZBX_NOTNULL, 0},
					{"parent_taskid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{0}
				},
				NULL
			};

	return DBcreate_table(&table);
}

static int	DBpatch_4050032(void)
{
	const ZBX_FIELD	field = {"taskid", NULL, "task", "taskid", 0, 0, 0, ZBX_FK_CASCADE_DELETE};

	return DBadd_foreign_key("task_data", 1, &field);
}

static int	DBpatch_4050033(void)
{
	const ZBX_TABLE	table =
			{"task_result", "taskid", 0,
				{
					{"taskid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"status", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
					{"parent_taskid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"info", "", NULL, NULL, 0, ZBX_TYPE_SHORTTEXT, ZBX_NOTNULL, 0},
					{0}
				},
				NULL
			};

	return DBcreate_table(&table);
}

static int	DBpatch_4050034(void)
{
	return DBcreate_index("task_result", "task_result_1", "parent_taskid", 0);
}

static int	DBpatch_4050035(void)
{
	const ZBX_FIELD	field = {"taskid", NULL, "task", "taskid", 0, 0, 0, ZBX_FK_CASCADE_DELETE};

	return DBadd_foreign_key("task_result", 1, &field);
}

static int	DBpatch_4050036(void)
{
	const ZBX_FIELD	field = {"note", "0", NULL, NULL, 128, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};

	return DBrename_field("auditlog", "details", &field);
}

static int	DBpatch_4050037(void)
{
	const ZBX_FIELD	field = {"note", "", NULL, NULL, 128, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};

	return DBset_default("auditlog", &field);
}

static int	DBpatch_4050038(void)
{
	return DBcreate_index("auditlog", "auditlog_3", "resourcetype,resourceid", 0);
}

static int	DBpatch_4050039(void)
{
	int		i;
	const char	*values[] = {
			"web.usergroup.filter_users_status", "web.usergroup.filter_user_status",
			"web.usergrps.php.sort", "web.usergroup.sort",
			"web.usergrps.php.sortorder", "web.usergroup.sortorder",
			"web.adm.valuemapping.php.sortorder", "web.valuemap.list.sortorder",
			"web.adm.valuemapping.php.sort", "web.valuemap.list.sort",
			"web.latest.php.sort", "web.latest.sort",
			"web.latest.php.sortorder", "web.latest.sortorder",
			"web.paging.lastpage", "web.pager.entity",
			"web.paging.page", "web.pager.page",
			"web.auditlogs.filter.active", "web.auditlog.filter.active",
			"web.auditlogs.filter.action", "web.auditlog.filter.action",
			"web.auditlogs.filter.alias", "web.auditlog.filter.alias",
			"web.auditlogs.filter.resourcetype", "web.auditlog.filter.resourcetype",
			"web.auditlogs.filter.from", "web.auditlog.filter.from",
			"web.auditlogs.filter.to", "web.auditlog.filter.to"
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

static int	DBpatch_4050040(void)
{
	const ZBX_FIELD	field = {"resourceid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, 0, 0};

	return DBdrop_default("auditlog", &field);
}

static int	DBpatch_4050041(void)
{
	const ZBX_FIELD	field = {"resourceid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, 0, 0};

	return DBdrop_not_null("auditlog", &field);
}

static int	DBpatch_4050042(void)
{
	if (0 == (program_type & ZBX_PROGRAM_TYPE_SERVER))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute("update auditlog set resourceid=null where resourceid=0"))
		return FAIL;

	return SUCCEED;
}

static int	DBpatch_4050043(void)
{
	if (0 == (program_type & ZBX_PROGRAM_TYPE_SERVER))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute("delete from profiles where idx='web.screens.graphid'"))
		return FAIL;

	return SUCCEED;
}

static int	DBpatch_4050044(void)
{
	const ZBX_TABLE table =
		{"interface_snmp", "interfaceid", 0,
			{
				{"interfaceid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"version", "2", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
				{"bulk", "1", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
				{"community", "", NULL, NULL, 64, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
				{"securityname", "", NULL, NULL, 64, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
				{"securitylevel", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
				{"authpassphrase", "", NULL, NULL, 64, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
				{"privpassphrase", "", NULL, NULL, 64, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
				{"authprotocol", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
				{"privprotocol", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
				{"contextname", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
				{0}
			},
			NULL
		};

	return DBcreate_table(&table);
}

static int	DBpatch_4050045(void)
{
	const ZBX_FIELD	field = {"interfaceid", NULL, "interface", "interfaceid", 0, 0, 0, ZBX_FK_CASCADE_DELETE};

	return DBadd_foreign_key("interface_snmp", 1, &field);
}

typedef struct
{
	zbx_uint64_t	interfaceid;
	char		*community;
	char		*securityname;
	char		*authpassphrase;
	char		*privpassphrase;
	char		*contextname;
	unsigned char	securitylevel;
	unsigned char	authprotocol;
	unsigned char	privprotocol;
	unsigned char	version;
	unsigned char	bulk;
	zbx_uint64_t	item_interfaceid;
	char		*item_port;
	unsigned char	skip;
}
dbu_snmp_if_t;

typedef struct
{
	zbx_uint64_t	interfaceid;
	zbx_uint64_t	hostid;
	char		*ip;
	char		*dns;
	char		*port;
	unsigned char	type;
	unsigned char	main;
	unsigned char	useip;
}
dbu_interface_t;

ZBX_PTR_VECTOR_DECL(dbu_interface, dbu_interface_t);
ZBX_PTR_VECTOR_IMPL(dbu_interface, dbu_interface_t);
ZBX_PTR_VECTOR_DECL(dbu_snmp_if, dbu_snmp_if_t);
ZBX_PTR_VECTOR_IMPL(dbu_snmp_if, dbu_snmp_if_t);

static void	db_interface_free(dbu_interface_t interface)
{
	zbx_free(interface.ip);
	zbx_free(interface.dns);
	zbx_free(interface.port);
}

static void	db_snmpinterface_free(dbu_snmp_if_t snmp)
{
	zbx_free(snmp.community);
	zbx_free(snmp.securityname);
	zbx_free(snmp.authpassphrase);
	zbx_free(snmp.privpassphrase);
	zbx_free(snmp.contextname);
	zbx_free(snmp.item_port);
}

static int	db_snmp_if_cmp(const dbu_snmp_if_t *snmp1, const dbu_snmp_if_t *snmp2)
{
#define ZBX_RETURN_IF_NOT_EQUAL_STR(s1, s2)	\
	if (0 != (ret = strcmp(s1, s2)))	\
		return ret;

	int	ret;

	ZBX_RETURN_IF_NOT_EQUAL(snmp1->securitylevel, snmp2->securitylevel);
	ZBX_RETURN_IF_NOT_EQUAL(snmp1->authprotocol, snmp2->authprotocol);
	ZBX_RETURN_IF_NOT_EQUAL(snmp1->privprotocol, snmp2->privprotocol);
	ZBX_RETURN_IF_NOT_EQUAL(snmp1->version, snmp2->version);
	ZBX_RETURN_IF_NOT_EQUAL(snmp1->bulk, snmp2->bulk);
	ZBX_RETURN_IF_NOT_EQUAL_STR(snmp1->community, snmp2->community);
	ZBX_RETURN_IF_NOT_EQUAL_STR(snmp1->securityname, snmp2->securityname);
	ZBX_RETURN_IF_NOT_EQUAL_STR(snmp1->authpassphrase, snmp2->authpassphrase);
	ZBX_RETURN_IF_NOT_EQUAL_STR(snmp1->privpassphrase, snmp2->privpassphrase);
	ZBX_RETURN_IF_NOT_EQUAL_STR(snmp1->contextname, snmp2->contextname);

	return 0;

#undef ZBX_RETURN_IF_NOT_EQUAL_STR
}

static int	db_snmp_if_newid_cmp(const dbu_snmp_if_t *snmp1, const dbu_snmp_if_t *snmp2)
{
	ZBX_RETURN_IF_NOT_EQUAL(snmp1->interfaceid, snmp2->interfaceid);

	return db_snmp_if_cmp(snmp1,snmp2);
}

static int	db_snmp_new_if_find(const dbu_snmp_if_t *snmp, const zbx_vector_dbu_snmp_if_t *snmp_new_ifs,
		const zbx_vector_dbu_interface_t *interfaces, const char *if_port)
{
	int		i, index;
	dbu_interface_t	id, *interface;

	for (i = snmp_new_ifs->values_num - 1; i >= 0 &&
			snmp->item_interfaceid == snmp_new_ifs->values[i].item_interfaceid; i--)
	{
		if (0 != db_snmp_if_cmp(snmp, &snmp_new_ifs->values[i]))
			continue;

		if ('\0' != *snmp->item_port && 0 != strcmp(snmp->item_port, snmp_new_ifs->values[i].item_port))
			continue;

		id.interfaceid = snmp_new_ifs->values[i].interfaceid;
		index = zbx_vector_dbu_interface_bsearch(interfaces, id, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
		interface = &interfaces->values[index];

		if ('\0' == *snmp->item_port && 0 != strcmp(if_port, interface->port))
			continue;

		return i;
	}

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: DBpatch_load_data                                                *
 *                                                                            *
 * Purpose: loading a set of unique combination of snmp data within a single  *
 *          interface and associated interface data                           *
 *                                                                            *
 * Parameters: snmp_ifs     - [OUT] snmp data linked with existing interfaces *
 *             new_ifs      - [OUT] new interfaces for snmp data              *
 *             snmp_new_ifs - [OUT] snmp data associated with new interfaces  *
 *                                                                            *
 ******************************************************************************/
static void	DBpatch_load_data(zbx_vector_dbu_snmp_if_t *snmp_ifs, zbx_vector_dbu_interface_t *new_ifs,
		zbx_vector_dbu_snmp_if_t *snmp_new_ifs)
{
#define ITEM_TYPE_SNMPv1	1
#define ITEM_TYPE_SNMPv2c	4
#define ITEM_TYPE_SNMPv3	6

	DB_RESULT	result;
	DB_ROW		row;
	int		index;

	result = DBselect(
			"select distinct "
				"i.interfaceid,"
				"i.type,"
				"f.bulk,"
				"i.snmp_community,"
				"i.snmpv3_securityname,"
				"i.snmpv3_securitylevel,"
				"i.snmpv3_authpassphrase,"
				"i.snmpv3_privpassphrase,"
				"i.snmpv3_authprotocol,"
				"i.snmpv3_privprotocol,"
				"i.snmpv3_contextname,"
				"i.port,"
				"i.hostid,"
				"f.type,"
				"f.useip,"
				"f.ip,"
				"f.dns,"
				"f.port"
			" from items i"
				" join hosts h on i.hostid=h.hostid"
				" join interface f on i.interfaceid=f.interfaceid"
			" where i.type in (%d,%d,%d)"
				" and h.status in (0,1)"
			" order by i.interfaceid asc,i.type asc,i.port asc,i.snmp_community asc",
			ITEM_TYPE_SNMPv1, ITEM_TYPE_SNMPv2c, ITEM_TYPE_SNMPv3);

	while (NULL != (row = DBfetch(result)))
	{
		dbu_interface_t	interface;
		dbu_snmp_if_t	snmp;
		unsigned char	item_type;
		const char 	*if_port;

		ZBX_DBROW2UINT64(snmp.item_interfaceid, row[0]);
		ZBX_STR2UCHAR(item_type, row[1]);
		ZBX_STR2UCHAR(snmp.bulk, row[2]);
		snmp.community = zbx_strdup(NULL, row[3]);
		snmp.securityname = zbx_strdup(NULL, row[4]);
		ZBX_STR2UCHAR(snmp.securitylevel, row[5]);
		snmp.authpassphrase = zbx_strdup(NULL, row[6]);
		snmp.privpassphrase = zbx_strdup(NULL, row[7]);
		ZBX_STR2UCHAR(snmp.authprotocol, row[8]);
		ZBX_STR2UCHAR(snmp.privprotocol, row[9]);
		snmp.contextname = zbx_strdup(NULL, row[10]);
		snmp.item_port = zbx_strdup(NULL, row[11]);
		snmp.skip = 0;
		if_port = row[17];

		if (ITEM_TYPE_SNMPv1 == item_type)
			snmp.version = ZBX_IF_SNMP_VERSION_1;
		else if (ITEM_TYPE_SNMPv2c == item_type)
			snmp.version = ZBX_IF_SNMP_VERSION_2;
		else
			snmp.version = ZBX_IF_SNMP_VERSION_3;

		snmp.interfaceid = snmp.item_interfaceid;
		index = FAIL;

		if (('\0' == *snmp.item_port || 0 == strcmp(snmp.item_port, if_port)) &&
				FAIL == (index = zbx_vector_dbu_snmp_if_bsearch(snmp_ifs, snmp,
						ZBX_DEFAULT_UINT64_COMPARE_FUNC)))
		{
			zbx_vector_dbu_snmp_if_append(snmp_ifs, snmp);
			continue;
		}
		else if (FAIL != index && 0 == db_snmp_if_newid_cmp(&snmp_ifs->values[index], &snmp))
		{
			db_snmpinterface_free(snmp);
			continue;
		}
		else if (0 < snmp_new_ifs->values_num &&
				FAIL != (index = db_snmp_new_if_find(&snmp, snmp_new_ifs, new_ifs, if_port)))
		{
			snmp.skip = 1;
			snmp.interfaceid = snmp_new_ifs->values[index].interfaceid;
			zbx_vector_dbu_snmp_if_append(snmp_new_ifs, snmp);
			continue;
		}

		snmp.interfaceid = DBget_maxid("interface");

		zbx_vector_dbu_snmp_if_append(snmp_new_ifs, snmp);

		interface.interfaceid = snmp.interfaceid;
		ZBX_DBROW2UINT64(interface.hostid, row[12]);
		interface.main = 0;
		ZBX_STR2UCHAR(interface.type, row[13]);
		ZBX_STR2UCHAR(interface.useip, row[14]);
		interface.ip = zbx_strdup(NULL, row[15]);
		interface.dns = zbx_strdup(NULL, row[16]);

		if ('\0' != *snmp.item_port)
			interface.port = zbx_strdup(NULL, snmp.item_port);
		else
			interface.port = zbx_strdup(NULL, if_port);

		zbx_vector_dbu_interface_append(new_ifs, interface);
	}
	DBfree_result(result);

#undef ITEM_TYPE_SNMPv1
#undef ITEM_TYPE_SNMPv2c
#undef ITEM_TYPE_SNMPv3
}

static void	DBpatch_load_empty_if(zbx_vector_dbu_snmp_if_t *snmp_def_ifs)
{
	DB_RESULT	result;
	DB_ROW		row;

	result = DBselect(
			"select h.interfaceid,h.bulk"
			" from interface h"
			" where h.type=2 and h.interfaceid not in ("
				"select interfaceid"
				" from interface_snmp)");

	while (NULL != (row = DBfetch(result)))
	{
		dbu_snmp_if_t	snmp;

		ZBX_DBROW2UINT64(snmp.interfaceid, row[0]);
		ZBX_STR2UCHAR(snmp.bulk, row[1]);
		snmp.version = ZBX_IF_SNMP_VERSION_2;
		snmp.community = zbx_strdup(NULL, "{$SNMP_COMMUNITY}");
		snmp.securityname = zbx_strdup(NULL, "");
		snmp.securitylevel = 0;
		snmp.authpassphrase = zbx_strdup(NULL, "");
		snmp.privpassphrase = zbx_strdup(NULL, "");
		snmp.authprotocol = 0;
		snmp.privprotocol = 0;
		snmp.contextname = zbx_strdup(NULL, "");
		snmp.item_port = zbx_strdup(NULL, "");
		snmp.skip = 0;
		snmp.item_interfaceid = 0;

		zbx_vector_dbu_snmp_if_append(snmp_def_ifs, snmp);
	}
	DBfree_result(result);
}

static int	DBpatch_snmp_if_save(zbx_vector_dbu_snmp_if_t *snmp_ifs)
{
	zbx_db_insert_t	db_insert_snmp_if;
	int		i, ret;

	zbx_db_insert_prepare(&db_insert_snmp_if, "interface_snmp", "interfaceid", "version", "bulk", "community",
			"securityname", "securitylevel", "authpassphrase", "privpassphrase", "authprotocol",
			"privprotocol", "contextname", NULL);

	for (i = 0; i < snmp_ifs->values_num; i++)
	{
		dbu_snmp_if_t	*s = &snmp_ifs->values[i];

		if (0 != s->skip)
			continue;

		zbx_db_insert_add_values(&db_insert_snmp_if, s->interfaceid, s->version, s->bulk, s->community,
				s->securityname, s->securitylevel, s->authpassphrase, s->privpassphrase, s->authprotocol,
				s->privprotocol, s->contextname);
	}

	ret = zbx_db_insert_execute(&db_insert_snmp_if);
	zbx_db_insert_clean(&db_insert_snmp_if);

	return ret;
}

static int	DBpatch_interface_create(zbx_vector_dbu_interface_t *interfaces)
{
	zbx_db_insert_t	db_insert_interfaces;
	int		i, ret;

	zbx_db_insert_prepare(&db_insert_interfaces, "interface", "interfaceid", "hostid", "main", "type", "useip",
			"ip", "dns", "port", NULL);

	for (i = 0; i < interfaces->values_num; i++)
	{
		dbu_interface_t	*interface = &interfaces->values[i];

		zbx_db_insert_add_values(&db_insert_interfaces, interface->interfaceid,
				interface->hostid, interface->main, interface->type, interface->useip, interface->ip,
				interface->dns, interface->port);
	}

	ret = zbx_db_insert_execute(&db_insert_interfaces);
	zbx_db_insert_clean(&db_insert_interfaces);

	return ret;
}

static int	DBpatch_items_update(zbx_vector_dbu_snmp_if_t *snmp_ifs)
{
#define ITEM_TYPE_SNMPv1	1
#define ITEM_TYPE_SNMPv2c	4
#define ITEM_TYPE_SNMPv3	6
#define ITEM_TYPE_SNMP		20

	int	i, ret = SUCCEED;
	char	*sql;
	size_t	sql_alloc = snmp_ifs->values_num * ZBX_KIBIBYTE / 3 , sql_offset = 0;

	sql = (char *)zbx_malloc(NULL, sql_alloc);
	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	for (i = 0; i < snmp_ifs->values_num && SUCCEED == ret; i++)
	{
		dbu_snmp_if_t	*s = &snmp_ifs->values[i];

		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
#ifdef HAVE_ORACLE
				"update items i set type=%d, interfaceid=" ZBX_FS_UI64
				" where exists (select 1 from hosts h"
					" where i.hostid=h.hostid and"
					" i.type in (%d,%d,%d) and h.status <> 3 and"
					" i.interfaceid=" ZBX_FS_UI64 " and"
					" (('%s' is null and i.snmp_community is null) or"
						" i.snmp_community='%s') and"
					" (('%s' is null and i.snmpv3_securityname is null) or"
						" i.snmpv3_securityname='%s') and"
					" i.snmpv3_securitylevel=%d and"
					" (('%s' is null and i.snmpv3_authpassphrase is null) or"
						" i.snmpv3_authpassphrase='%s') and"
					" (('%s' is null and i.snmpv3_privpassphrase is null) or"
						" i.snmpv3_privpassphrase='%s') and"
					" i.snmpv3_authprotocol=%d and"
					" i.snmpv3_privprotocol=%d and"
					" (('%s' is null and i.snmpv3_contextname is null) or"
						" i.snmpv3_contextname='%s') and"
					" (('%s' is null and i.port is null) or"
						" i.port='%s'));\n",
				ITEM_TYPE_SNMP, s->interfaceid, ITEM_TYPE_SNMPv1, ITEM_TYPE_SNMPv2c, ITEM_TYPE_SNMPv3,
				s->item_interfaceid, s->community, s->community, s->securityname, s->securityname,
				(int)s->securitylevel, s->authpassphrase, s->authpassphrase, s->privpassphrase,
				s->privpassphrase, (int)s->authprotocol, (int)s->privprotocol, s->contextname,
				s->contextname, s->item_port, s->item_port);

#else
#	ifdef HAVE_MYSQL
				"update items i, hosts h set i.type=%d, i.interfaceid=" ZBX_FS_UI64
#	else
				"update items i set type=%d, interfaceid=" ZBX_FS_UI64 " from hosts h"
#	endif
				" where i.hostid=h.hostid and"
					" type in (%d,%d,%d) and h.status <> 3 and"
					" interfaceid=" ZBX_FS_UI64 " and"
					" snmp_community='%s' and"
					" snmpv3_securityname='%s' and"
					" snmpv3_securitylevel=%d and"
					" snmpv3_authpassphrase='%s' and"
					" snmpv3_privpassphrase='%s' and"
					" snmpv3_authprotocol=%d and"
					" snmpv3_privprotocol=%d and"
					" snmpv3_contextname='%s' and"
					" port='%s';\n",
				ITEM_TYPE_SNMP, s->interfaceid,
				ITEM_TYPE_SNMPv1, ITEM_TYPE_SNMPv2c, ITEM_TYPE_SNMPv3,
				s->item_interfaceid, s->community, s->securityname, (int)s->securitylevel,
				s->authpassphrase, s->privpassphrase, (int)s->authprotocol, (int)s->privprotocol,
				s->contextname, s->item_port);
#endif
		ret = DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
	}

	if (SUCCEED == ret)
	{
		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

		if (16 < sql_offset && ZBX_DB_OK > DBexecute("%s", sql))
			ret = FAIL;
	}

	zbx_free(sql);

	return ret;

#undef ITEM_TYPE_SNMPv1
#undef ITEM_TYPE_SNMPv2c
#undef ITEM_TYPE_SNMPv3
#undef ITEM_TYPE_SNMP
}

static int	DBpatch_items_type_update(void)
{
#define ITEM_TYPE_SNMPv1	1
#define ITEM_TYPE_SNMPv2c	4
#define ITEM_TYPE_SNMPv3	6
#define ITEM_TYPE_SNMP		20

	if (ZBX_DB_OK > DBexecute("update items set type=%d where type in (%d,%d,%d)", ITEM_TYPE_SNMP,
			ITEM_TYPE_SNMPv1, ITEM_TYPE_SNMPv2c, ITEM_TYPE_SNMPv3))
	{
		return FAIL;
	}

	return SUCCEED;

#undef ITEM_TYPE_SNMPv1
#undef ITEM_TYPE_SNMPv2c
#undef ITEM_TYPE_SNMPv3
#undef ITEM_TYPE_SNMP
}

/******************************************************************************
 *                                                                            *
 * Function: DBpatch_4050046                                                  *
 *                                                                            *
 * Purpose: migration snmp data from 'items' table to 'interface_snmp' new    *
 *          table linked with 'interface' table, except interface links for   *
 *          discovered hosts and parent host interface                        *
 *                                                                            *
 * Return value: SUCCEED - the operation has completed successfully           *
 *               FAIL    - the operation has failed                           *
 *                                                                            *
 ******************************************************************************/
static int	DBpatch_4050046(void)
{
	zbx_vector_dbu_interface_t	new_ifs;
	zbx_vector_dbu_snmp_if_t	snmp_ifs, snmp_new_ifs, snmp_def_ifs;
	int				ret = FAIL;

	zbx_vector_dbu_snmp_if_create(&snmp_ifs);
	zbx_vector_dbu_snmp_if_create(&snmp_new_ifs);
	zbx_vector_dbu_snmp_if_create(&snmp_def_ifs);
	zbx_vector_dbu_interface_create(&new_ifs);

	DBpatch_load_data(&snmp_ifs, &new_ifs, &snmp_new_ifs);

	while (1)
	{
		if (0 < snmp_ifs.values_num && SUCCEED != DBpatch_snmp_if_save(&snmp_ifs))
			break;

		if (0 < new_ifs.values_num && SUCCEED != DBpatch_interface_create(&new_ifs))
			break;

		if (0 < snmp_new_ifs.values_num && SUCCEED != DBpatch_snmp_if_save(&snmp_new_ifs))
			break;

		DBpatch_load_empty_if(&snmp_def_ifs);

		if (0 < snmp_def_ifs.values_num && SUCCEED != DBpatch_snmp_if_save(&snmp_def_ifs))
			break;

		if (0 < snmp_new_ifs.values_num && SUCCEED != DBpatch_items_update(&snmp_new_ifs))
			break;

		if (SUCCEED != DBpatch_items_type_update())
			break;

		ret = SUCCEED;
		break;
	}

	zbx_vector_dbu_interface_clear_ext(&new_ifs, db_interface_free);
	zbx_vector_dbu_interface_destroy(&new_ifs);
	zbx_vector_dbu_snmp_if_clear_ext(&snmp_ifs, db_snmpinterface_free);
	zbx_vector_dbu_snmp_if_destroy(&snmp_ifs);
	zbx_vector_dbu_snmp_if_clear_ext(&snmp_new_ifs, db_snmpinterface_free);
	zbx_vector_dbu_snmp_if_destroy(&snmp_new_ifs);
	zbx_vector_dbu_snmp_if_clear_ext(&snmp_def_ifs, db_snmpinterface_free);
	zbx_vector_dbu_snmp_if_destroy(&snmp_def_ifs);

	return ret;
}

static int	db_if_cmp(const dbu_interface_t *if1, const dbu_interface_t *if2)
{
#define ZBX_RETURN_IF_NOT_EQUAL_STR(s1, s2)	\
	if (0 != (ret = strcmp(s1, s2)))	\
		return ret;

	int	ret;

	ZBX_RETURN_IF_NOT_EQUAL(if1->hostid, if2->hostid);
	ZBX_RETURN_IF_NOT_EQUAL(if1->type, if2->type);
	ZBX_RETURN_IF_NOT_EQUAL(if1->main, if2->main);
	ZBX_RETURN_IF_NOT_EQUAL(if1->useip, if2->useip);
	ZBX_RETURN_IF_NOT_EQUAL_STR(if1->ip, if2->ip);
	ZBX_RETURN_IF_NOT_EQUAL_STR(if1->dns, if2->dns);
	ZBX_RETURN_IF_NOT_EQUAL_STR(if1->port, if2->port);

	return 0;

#undef ZBX_RETURN_IF_NOT_EQUAL_STR
}

static zbx_uint64_t	db_if_find(const dbu_interface_t *interface, dbu_snmp_if_t *snmp,
		zbx_vector_dbu_interface_t *interfaces, zbx_vector_dbu_snmp_if_t *snmp_ifs)
{
	int	i;

	for (i = interfaces->values_num - 1; i >= 0 &&
			interface->hostid == interfaces->values[i].hostid; i--)
	{
		if (0 != db_if_cmp(interface, &interfaces->values[i]))
			continue;

		if (0 != db_snmp_if_cmp(snmp, &snmp_ifs->values[i]))
			continue;

		return interfaces->values[i].interfaceid;
	}

	return 0;
}

static void	db_if_link(zbx_uint64_t if_slave, zbx_uint64_t if_master, zbx_vector_uint64_pair_t *if_links)
{
	zbx_uint64_pair_t	pair = {if_slave, if_master};

	zbx_vector_uint64_pair_append(if_links, pair);
}

/******************************************************************************
 *                                                                            *
 * Function: DBpatch_if_load_data                                             *
 *                                                                            *
 * Purpose: loading all unlinked interfaces, snmp data and hostid of host     *
 *          prototype for discovered hosts                                    *
 *                                                                            *
 * Parameters: new_ifs      - [OUT] new interfaces to be created on master    *
 *                                  hosts                                     *
 *             snmp_new_ifs - [OUT] snmp data associated with new interfaces  *
 *             if_links     - [OUT] set of pairs for discovered host          *
 *                                  interfaceid and parent interfaceid of     *
 *                                  parent host                               *
 *                                                                            *
 * Comments: When host is created by lld the parent host interfaces are       *
 *           copied over to the discovered hosts. Previous patch could have   *
 *           created new SNMP interfaces on discovered hosts, which must be   *
 *           linked to the corresponding interfaces (created if necessary) to *
 *           the parent host.                                                 *
 *                                                                            *
 ******************************************************************************/
static void	DBpatch_if_load_data(zbx_vector_dbu_interface_t *new_ifs, zbx_vector_dbu_snmp_if_t *snmp_new_ifs,
		zbx_vector_uint64_pair_t *if_links)
{
	DB_RESULT	result;
	DB_ROW		row;

	result = DBselect(
			"select hreal.hostid,"
				"i.interfaceid,"
				"i.main,"
				"i.type,"
				"i.useip,"
				"i.ip,"
				"i.dns,"
				"i.port,"
				"s.version,"
				"s.bulk,"
				"s.community,"
				"s.securityname,"
				"s.securitylevel,"
				"s.authpassphrase,"
				"s.privpassphrase,"
				"s.authprotocol,"
				"s.privprotocol,"
				"s.contextname"
			" from interface i"
			" left join interface_discovery d on i.interfaceid=d.interfaceid"
			" join interface_snmp s on i.interfaceid=s.interfaceid"
			" join hosts hdisc on i.hostid=hdisc.hostid"
			" join host_discovery hd on hdisc.hostid=hd.hostid"
			" join hosts hproto on hd.parent_hostid=hproto.hostid"
			" join host_discovery hdd on hd.parent_hostid=hdd.hostid"
			" join items drule on drule.itemid=hdd.parent_itemid"
			" join hosts hreal on drule.hostid=hreal.hostid"
			" where"
				" i.type=2 and"
				" hdisc.flags=4 and"
				" drule.flags=1 and"
				" hproto.flags=2 and"
				" hreal.status in (1,0) and"
				" d.interfaceid is null"
			" order by drule.hostid asc, i.interfaceid asc");

	while (NULL != (row = DBfetch(result)))
	{
		dbu_interface_t		interface;
		dbu_snmp_if_t		snmp;
		zbx_uint64_t		if_parentid;

		ZBX_DBROW2UINT64(interface.hostid, row[0]);
		ZBX_DBROW2UINT64(interface.interfaceid , row[1]);
		ZBX_STR2UCHAR(interface.main, row[2]);
		ZBX_STR2UCHAR(interface.type, row[3]);
		ZBX_STR2UCHAR(interface.useip, row[4]);
		interface.ip = zbx_strdup(NULL, row[5]);
		interface.dns = zbx_strdup(NULL, row[6]);
		interface.port = zbx_strdup(NULL, row[7]);

		ZBX_STR2UCHAR(snmp.version, row[8]);
		ZBX_STR2UCHAR(snmp.bulk, row[9]);
		snmp.community = zbx_strdup(NULL, row[10]);
		snmp.securityname = zbx_strdup(NULL, row[11]);
		ZBX_STR2UCHAR(snmp.securitylevel, row[12]);
		snmp.authpassphrase = zbx_strdup(NULL, row[13]);
		snmp.privpassphrase = zbx_strdup(NULL, row[14]);
		ZBX_STR2UCHAR(snmp.authprotocol, row[15]);
		ZBX_STR2UCHAR(snmp.privprotocol, row[16]);
		snmp.contextname = zbx_strdup(NULL, row[17]);
		snmp.item_port = NULL;
		snmp.skip = 0;
		snmp.item_interfaceid = 0;

		if (0 < new_ifs->values_num &&
				0 != (if_parentid = db_if_find(&interface, &snmp, new_ifs, snmp_new_ifs)))
		{
			db_if_link(interface.interfaceid, if_parentid, if_links);
			db_snmpinterface_free(snmp);
			db_interface_free(interface);
			continue;
		}

		if_parentid = DBget_maxid("interface");
		db_if_link(interface.interfaceid, if_parentid, if_links);
		interface.interfaceid = if_parentid;
		snmp.interfaceid = if_parentid;
		zbx_vector_dbu_interface_append(new_ifs, interface);
		zbx_vector_dbu_snmp_if_append(snmp_new_ifs, snmp);
	}
	DBfree_result(result);
}

static int	DBpatch_interface_discovery_save(zbx_vector_uint64_pair_t *if_links)
{
	zbx_db_insert_t	db_insert_if_links;
	int		i, ret;

	zbx_db_insert_prepare(&db_insert_if_links, "interface_discovery", "interfaceid", "parent_interfaceid", NULL);

	for (i = 0; i < if_links->values_num; i++)
	{
		zbx_uint64_pair_t	*l = &if_links->values[i];

		zbx_db_insert_add_values(&db_insert_if_links, l->first, l->second);
	}

	ret = zbx_db_insert_execute(&db_insert_if_links);
	zbx_db_insert_clean(&db_insert_if_links);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: DBpatch_4050047                                                  *
 *                                                                            *
 * Purpose: recovery links between the interfaceid of discovered host and     *
 *          parent interfaceid from parent host                               *
 *                                                                            *
 * Return value: SUCCEED - the operation has completed successfully           *
 *               FAIL    - the operation has failed                           *
 *                                                                            *
 ******************************************************************************/
static int	DBpatch_4050047(void)
{
	zbx_vector_dbu_interface_t	new_ifs;
	zbx_vector_dbu_snmp_if_t	snmp_new_ifs;
	zbx_vector_uint64_pair_t	if_links;
	int				ret = FAIL;

	zbx_vector_dbu_snmp_if_create(&snmp_new_ifs);
	zbx_vector_dbu_interface_create(&new_ifs);
	zbx_vector_uint64_pair_create(&if_links);

	DBpatch_if_load_data(&new_ifs, &snmp_new_ifs, &if_links);

	while (1)
	{
		if (0 < new_ifs.values_num && SUCCEED != DBpatch_interface_create(&new_ifs))
			break;

		if (0 < snmp_new_ifs.values_num && SUCCEED != DBpatch_snmp_if_save(&snmp_new_ifs))
			break;

		if (0 < if_links.values_num && SUCCEED != DBpatch_interface_discovery_save(&if_links))
			break;

		ret = SUCCEED;
		break;
	}

	zbx_vector_uint64_pair_destroy(&if_links);
	zbx_vector_dbu_interface_clear_ext(&new_ifs, db_interface_free);
	zbx_vector_dbu_interface_destroy(&new_ifs);
	zbx_vector_dbu_snmp_if_clear_ext(&snmp_new_ifs, db_snmpinterface_free);
	zbx_vector_dbu_snmp_if_destroy(&snmp_new_ifs);

	return ret;
}

static int	DBpatch_4050048(void)
{
	return DBdrop_field("interface", "bulk");
}

static int	DBpatch_4050049(void)
{
	return DBdrop_field("items", "snmp_community");
}

static int	DBpatch_4050050(void)
{
	return DBdrop_field("items", "snmpv3_securityname");
}

static int	DBpatch_4050051(void)
{
	return DBdrop_field("items", "snmpv3_securitylevel");
}

static int	DBpatch_4050052(void)
{
	return DBdrop_field("items", "snmpv3_authpassphrase");
}

static int	DBpatch_4050053(void)
{
	return DBdrop_field("items", "snmpv3_privpassphrase");
}

static int	DBpatch_4050054(void)
{
	return DBdrop_field("items", "snmpv3_authprotocol");
}

static int	DBpatch_4050055(void)
{
	return DBdrop_field("items", "snmpv3_privprotocol");
}

static int	DBpatch_4050056(void)
{
	return DBdrop_field("items", "snmpv3_contextname");
}

static int	DBpatch_4050057(void)
{
	return DBdrop_field("items", "port");
}

static int	DBpatch_4050058(void)
{
	const ZBX_FIELD	field = {"type", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	return DBadd_field("globalmacro", &field);
}

static int	DBpatch_4050059(void)
{
	const ZBX_FIELD	field = {"type", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	return DBadd_field("hostmacro", &field);
}

static int	DBpatch_4050060(void)
{
	const ZBX_FIELD	field = {"compression_status", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	return DBadd_field("config", &field);
}

static int	DBpatch_4050061(void)
{
	const ZBX_FIELD	field = {"compression_availability", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	return DBadd_field("config", &field);
}

static int	DBpatch_4050062(void)
{
	const ZBX_FIELD	field = {"compress_older", "7d", NULL, NULL, 32, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};

	return DBadd_field("config", &field);
}

static int	DBpatch_4050063(void)
{
	DB_ROW		row;
	DB_RESULT	result;
	zbx_uint64_t	profileid, userid, idx2;
	int		ret = SUCCEED, value_int, i;
	const char	*profile = "web.problem.filter.severities";

	if (0 == (program_type & ZBX_PROGRAM_TYPE_SERVER))
		return SUCCEED;

	result = DBselect(
			"select profileid,userid,value_int"
			" from profiles"
			" where idx='web.problem.filter.severity'");

	while (NULL != (row = DBfetch(result)))
	{
		ZBX_DBROW2UINT64(profileid, row[0]);

		if (0 == (value_int = atoi(row[2])))
		{
			if (ZBX_DB_OK > DBexecute("delete from profiles where profileid=" ZBX_FS_UI64, profileid))
			{
				ret = FAIL;
				break;
			}

			continue;
		}

		if (ZBX_DB_OK > DBexecute("update profiles set idx='%s'"
				" where profileid=" ZBX_FS_UI64, profile, profileid))
		{
			ret = FAIL;
			break;
		}

		ZBX_DBROW2UINT64(userid, row[1]);
		idx2 = 0;

		for (i = value_int + 1; i < 6; i++)
		{
			if (ZBX_DB_OK > DBexecute("insert into profiles (profileid,userid,idx,idx2,value_id,value_int,"
					"type) values (" ZBX_FS_UI64 "," ZBX_FS_UI64 ",'%s'," ZBX_FS_UI64 ",0,%d,2)",
					DBget_maxid("profiles"), userid, profile, ++idx2, i))
			{
				ret = FAIL;
				break;
			}
		}
	}
	DBfree_result(result);

	return ret;
}

static int	DBpatch_4050064(void)
{
	if (ZBX_DB_OK > DBexecute("update profiles set value_int=1 where idx='web.layout.mode' and value_int=2"))
		return FAIL;

	return SUCCEED;
}

static int	DBpatch_4050065(void)
{
	const ZBX_FIELD	field = {"value", "0.0000", NULL, NULL, 0, ZBX_TYPE_FLOAT, ZBX_NOTNULL, 0};

	if (0 != (program_type & ZBX_PROGRAM_TYPE_SERVER))
		return SUCCEED;

	return DBmodify_field_type("history", &field, &field);
}

static int	DBpatch_4050066(void)
{
	const ZBX_FIELD	field = {"value_min", "0.0000", NULL, NULL, 0, ZBX_TYPE_FLOAT, ZBX_NOTNULL, 0};

	if (0 != (program_type & ZBX_PROGRAM_TYPE_SERVER))
		return SUCCEED;

	return DBmodify_field_type("trends", &field, &field);
}

static int	DBpatch_4050067(void)
{
	const ZBX_FIELD	field = {"value_avg", "0.0000", NULL, NULL, 0, ZBX_TYPE_FLOAT, ZBX_NOTNULL, 0};

	if (0 != (program_type & ZBX_PROGRAM_TYPE_SERVER))
		return SUCCEED;

	return DBmodify_field_type("trends", &field, &field);
}

static int	DBpatch_4050068(void)
{
	const ZBX_FIELD	field = {"value_max", "0.0000", NULL, NULL, 0, ZBX_TYPE_FLOAT, ZBX_NOTNULL, 0};

	if (0 != (program_type & ZBX_PROGRAM_TYPE_SERVER))
		return SUCCEED;

	return DBmodify_field_type("trends", &field, &field);
}

static int	DBpatch_4050069(void)
{
	const ZBX_FIELD	field = {"yaxismin", "0", NULL, NULL, 0, ZBX_TYPE_FLOAT, ZBX_NOTNULL, 0};

	return DBmodify_field_type("graphs", &field, &field);
}

static int	DBpatch_4050070(void)
{
	const ZBX_FIELD	field = {"yaxismax", "100", NULL, NULL, 0, ZBX_TYPE_FLOAT, ZBX_NOTNULL, 0};

	return DBmodify_field_type("graphs", &field, &field);
}

static int	DBpatch_4050071(void)
{
	const ZBX_FIELD	field = {"percent_left", "0", NULL, NULL, 0, ZBX_TYPE_FLOAT, ZBX_NOTNULL, 0};

	return DBmodify_field_type("graphs", &field, &field);
}

static int	DBpatch_4050072(void)
{
	const ZBX_FIELD	field = {"percent_right", "0", NULL, NULL, 0, ZBX_TYPE_FLOAT, ZBX_NOTNULL, 0};

	return DBmodify_field_type("graphs", &field, &field);
}

static int	DBpatch_4050073(void)
{
	const ZBX_FIELD	field = {"goodsla", "99.9", NULL, NULL, 0, ZBX_TYPE_FLOAT, ZBX_NOTNULL, 0};

	return DBmodify_field_type("services", &field, &field);
}

static int	DBpatch_4050074(void)
{
	int		i;
	const char	*values[] = {
			"web.latest.groupid", "web.latest.hostid", "web.latest.graphid", "web..groupid",
			"web..hostid", "web.view.groupid", "web.view.hostid", "web.view.graphid",
			"web.config.groupid", "web.config.hostid", "web.templates.php.groupid", "web.cm.groupid",
			"web.httpmon.php.sort", "web.httpmon.php.sortorder", "web.avail_report.0.hostid",
			"web.avail_report.0.groupid", "web.graphs.filter.to", "web.graphs.filter.from", "web.graphs.filter.active"
		};

	if (0 == (program_type & ZBX_PROGRAM_TYPE_SERVER))
		return SUCCEED;

	for (i = 0; i < (int)ARRSIZE(values); i++)
	{
		if (ZBX_DB_OK > DBexecute("delete from profiles where idx='%s'", values[i]))
			return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_4050075(void)
{
	return DBdrop_field("config", "dropdown_first_entry");
}

static int	DBpatch_4050076(void)
{
	return DBdrop_field("config", "dropdown_first_remember");
}

static int	DBpatch_4050077(void)
{
	const ZBX_FIELD	field = {"message", "", NULL, NULL, 2048, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};

	return DBmodify_field_type("acknowledges", &field, NULL);
}

static int	DBpatch_4050078(void)
{
	const ZBX_FIELD	field = {"write_clock", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	return DBadd_field("proxy_history", &field);
}

static int	DBpatch_4050079(void)
{
	const ZBX_FIELD	field = {"instanceid", "", NULL, NULL, 32, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};

	return DBadd_field("config", &field);
}

static int	DBpatch_4050080(void)
{
	const ZBX_FIELD	old_field = {"script", "", NULL, NULL, 0, ZBX_TYPE_SHORTTEXT, ZBX_NOTNULL, 0};
	const ZBX_FIELD	field = {"script", "", NULL, NULL, 0, ZBX_TYPE_TEXT, ZBX_NOTNULL, 0};

	return DBmodify_field_type("media_type", &field, &old_field);
}

static int	DBpatch_4050081(void)
{
	const ZBX_FIELD	old_field = {"oldvalue", "", NULL, NULL, 0, ZBX_TYPE_SHORTTEXT, ZBX_NOTNULL, 0};
	const ZBX_FIELD	field = {"oldvalue", "", NULL, NULL, 0, ZBX_TYPE_TEXT, ZBX_NOTNULL, 0};

	return DBmodify_field_type("auditlog_details", &field, &old_field);
}

static int	DBpatch_4050082(void)
{
	const ZBX_FIELD	old_field = {"newvalue", "", NULL, NULL, 0, ZBX_TYPE_SHORTTEXT, ZBX_NOTNULL, 0};
	const ZBX_FIELD	field = {"newvalue", "", NULL, NULL, 0, ZBX_TYPE_TEXT, ZBX_NOTNULL, 0};

	return DBmodify_field_type("auditlog_details", &field, &old_field);
}

static int	DBpatch_4050083(void)
{
	const ZBX_FIELD	field = {"saml_auth_enabled", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	return DBadd_field("config", &field);
}

static int	DBpatch_4050084(void)
{
	const ZBX_FIELD	field = {"saml_idp_entityid", "", NULL, NULL, 1024, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};

	return DBadd_field("config", &field);
}

static int	DBpatch_4050085(void)
{
	const ZBX_FIELD	field = {"saml_sso_url", "", NULL, NULL, 2048, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};

	return DBadd_field("config", &field);
}

static int	DBpatch_4050086(void)
{
	const ZBX_FIELD	field = {"saml_slo_url", "", NULL, NULL, 2048, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};

	return DBadd_field("config", &field);
}

static int	DBpatch_4050087(void)
{
	const ZBX_FIELD	field = {"saml_username_attribute", "", NULL, NULL, 128, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};

	return DBadd_field("config", &field);
}

static int	DBpatch_4050088(void)
{
	const ZBX_FIELD	field = {"saml_sp_entityid", "", NULL, NULL, 1024, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};

	return DBadd_field("config", &field);
}

static int	DBpatch_4050089(void)
{
	const ZBX_FIELD	field = {"saml_nameid_format", "", NULL, NULL, 2048, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};

	return DBadd_field("config", &field);
}

static int	DBpatch_4050090(void)
{
	const ZBX_FIELD	field = {"saml_sign_messages", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	return DBadd_field("config", &field);
}

static int	DBpatch_4050091(void)
{
	const ZBX_FIELD	field = {"saml_sign_assertions", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	return DBadd_field("config", &field);
}

static int	DBpatch_4050092(void)
{
	const ZBX_FIELD	field = {"saml_sign_authn_requests", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	return DBadd_field("config", &field);
}

static int	DBpatch_4050093(void)
{
	const ZBX_FIELD	field = {"saml_sign_logout_requests", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	return DBadd_field("config", &field);
}

static int	DBpatch_4050094(void)
{
	const ZBX_FIELD	field = {"saml_sign_logout_responses", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	return DBadd_field("config", &field);
}

static int	DBpatch_4050095(void)
{
	const ZBX_FIELD	field = {"saml_encrypt_nameid", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	return DBadd_field("config", &field);
}

static int	DBpatch_4050096(void)
{
	const ZBX_FIELD	field = {"saml_encrypt_assertions", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	return DBadd_field("config", &field);
}

static int	DBpatch_4050097(void)
{
	const ZBX_FIELD	field = {"saml_case_sensitive", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	return DBadd_field("config", &field);
}

static int	DBpatch_4050098(void)
{
	const ZBX_TABLE	table =
		{"lld_override", "lld_overrideid", 0,
			{
				{"lld_overrideid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"itemid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"name", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
				{"step", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
				{"evaltype", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
				{"formula", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
				{"stop", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
				{0}
			},
			NULL
		};

	return DBcreate_table(&table);
}

static int	DBpatch_4050099(void)
{
	const ZBX_FIELD	field = {"itemid", NULL, "items", "itemid", 0, 0, 0, ZBX_FK_CASCADE_DELETE};

	return DBadd_foreign_key("lld_override", 1, &field);
}

static int	DBpatch_4050100(void)
{
	return DBcreate_index("lld_override", "lld_override_1", "itemid,name", 1);
}

static int	DBpatch_4050101(void)
{
	const ZBX_TABLE	table =
		{"lld_override_condition", "lld_override_conditionid", 0,
			{
				{"lld_override_conditionid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"lld_overrideid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"operator", "8", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
				{"macro", "", NULL, NULL, 64, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
				{"value", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
				{0}
			},
			NULL
		};

	return DBcreate_table(&table);
}

static int	DBpatch_4050102(void)
{
	const ZBX_FIELD	field = {"lld_overrideid", NULL, "lld_override", "lld_overrideid", 0, 0, 0,
			ZBX_FK_CASCADE_DELETE};

	return DBadd_foreign_key("lld_override_condition", 1, &field);
}

static int	DBpatch_4050103(void)
{
	return DBcreate_index("lld_override_condition", "lld_override_condition_1", "lld_overrideid", 0);
}

static int	DBpatch_4050104(void)
{
	const ZBX_TABLE	table =
		{"lld_override_operation", "lld_override_operationid", 0,
			{
				{"lld_override_operationid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"lld_overrideid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"operationobject", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
				{"operator", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
				{"value", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
				{0}
			},
			NULL
		};

	return DBcreate_table(&table);
}

static int	DBpatch_4050105(void)
{
	const ZBX_FIELD	field = {"lld_overrideid", NULL, "lld_override", "lld_overrideid", 0, 0, 0,
			ZBX_FK_CASCADE_DELETE};

	return DBadd_foreign_key("lld_override_operation", 1, &field);
}

static int	DBpatch_4050106(void)
{
	return DBcreate_index("lld_override_operation", "lld_override_operation_1", "lld_overrideid", 0);
}

static int	DBpatch_4050107(void)
{
	const ZBX_TABLE	table =
		{"lld_override_opstatus", "lld_override_operationid", 0,
			{
				{"lld_override_operationid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"status", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
				{0}
			},
			NULL
		};

	return DBcreate_table(&table);
}

static int	DBpatch_4050108(void)
{
	const ZBX_FIELD	field = {"lld_override_operationid", NULL, "lld_override_operation", "lld_override_operationid",
			0, 0, 0, ZBX_FK_CASCADE_DELETE};

	return DBadd_foreign_key("lld_override_opstatus", 1, &field);
}

static int	DBpatch_4050109(void)
{
	const ZBX_TABLE	table =
		{"lld_override_opdiscover", "lld_override_operationid", 0,
			{
				{"lld_override_operationid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"discover", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
				{0}
			},
			NULL
		};

	return DBcreate_table(&table);
}

static int	DBpatch_4050110(void)
{
	const ZBX_FIELD	field = {"lld_override_operationid", NULL, "lld_override_operation", "lld_override_operationid",
			0, 0, 0, ZBX_FK_CASCADE_DELETE};

	return DBadd_foreign_key("lld_override_opdiscover", 1, &field);
}

static int	DBpatch_4050111(void)
{
	const ZBX_TABLE	table =
		{"lld_override_opperiod", "lld_override_operationid", 0,
			{
				{"lld_override_operationid", NULL, "lld_override_operation", "lld_override_operationid",
						0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"delay", "0", NULL, NULL, 1024, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
				{0}
			},
			NULL
		};

	return DBcreate_table(&table);
}

static int	DBpatch_4050112(void)
{
	const ZBX_FIELD	field = {"lld_override_operationid", NULL, "lld_override_operation", "lld_override_operationid",
			0, 0, 0, ZBX_FK_CASCADE_DELETE};

	return DBadd_foreign_key("lld_override_opperiod", 1, &field);
}

static int	DBpatch_4050113(void)
{
	const ZBX_TABLE	table =
		{"lld_override_ophistory", "lld_override_operationid", 0,
			{
				{"lld_override_operationid", NULL, "lld_override_operation", "lld_override_operationid",
						0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"history", "90d", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
				{0}
			},
			NULL
		};

	return DBcreate_table(&table);
}

static int	DBpatch_4050114(void)
{
	const ZBX_FIELD	field = {"lld_override_operationid", NULL, "lld_override_operation", "lld_override_operationid",
			0, 0, 0, ZBX_FK_CASCADE_DELETE};

	return DBadd_foreign_key("lld_override_ophistory", 1, &field);
}

static int	DBpatch_4050115(void)
{
	const ZBX_TABLE	table =
		{"lld_override_optrends", "lld_override_operationid", 0,
			{
				{"lld_override_operationid", NULL, "lld_override_operation", "lld_override_operationid",
						0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"trends", "365d", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
				{0}
			},
			NULL
		};

	return DBcreate_table(&table);
}

static int	DBpatch_4050116(void)
{
	const ZBX_FIELD	field = {"lld_override_operationid", NULL, "lld_override_operation", "lld_override_operationid",
			0, 0, 0, ZBX_FK_CASCADE_DELETE};

	return DBadd_foreign_key("lld_override_optrends", 1, &field);
}

static int	DBpatch_4050117(void)
{
	const ZBX_TABLE	table =
		{"lld_override_opseverity", "lld_override_operationid", 0,
			{
				{"lld_override_operationid", NULL, "lld_override_operation", "lld_override_operationid",
						0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"severity", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
				{0}
			},
			NULL
		};

	return DBcreate_table(&table);
}

static int	DBpatch_4050118(void)
{
	const ZBX_FIELD	field = {"lld_override_operationid", NULL, "lld_override_operation", "lld_override_operationid",
			0, 0, 0, ZBX_FK_CASCADE_DELETE};

	return DBadd_foreign_key("lld_override_opseverity", 1, &field);
}

static int	DBpatch_4050119(void)
{
	const ZBX_TABLE	table =
		{"lld_override_optag", "lld_override_optagid", 0,
			{
				{"lld_override_optagid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"lld_override_operationid", NULL, "lld_override_operation", "lld_override_operationid",
						0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"tag", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
				{"value", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
				{0}
			},
			NULL
		};

	return DBcreate_table(&table);
}

static int	DBpatch_4050120(void)
{
	const ZBX_FIELD	field = {"lld_override_operationid", NULL, "lld_override_operation", "lld_override_operationid",
			0, 0, 0, ZBX_FK_CASCADE_DELETE};

	return DBadd_foreign_key("lld_override_optag", 1, &field);
}

static int	DBpatch_4050121(void)
{
	return DBcreate_index("lld_override_optag", "lld_override_optag_1", "lld_override_operationid", 0);
}

static int	DBpatch_4050122(void)
{
	const ZBX_TABLE table =
		{"lld_override_optemplate", "lld_override_optemplateid", 0,
			{
				{"lld_override_optemplateid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"lld_override_operationid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"templateid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{0}
			},
			NULL
		};

	return DBcreate_table(&table);
}

static int	DBpatch_4050123(void)
{
	const ZBX_FIELD	field = {"lld_override_operationid", NULL, "lld_override_operation", "lld_override_operationid",
			0, 0, 0, ZBX_FK_CASCADE_DELETE};

	return DBadd_foreign_key("lld_override_optemplate", 1, &field);
}

static int	DBpatch_4050124(void)
{
	return DBcreate_index("lld_override_optemplate", "lld_override_optemplate_1",
			"lld_override_operationid,templateid", 1);
}

static int	DBpatch_4050125(void)
{
	const ZBX_FIELD	field = {"templateid", NULL, "hosts", "hostid", 0, 0, 0, 0};

	return DBadd_foreign_key("lld_override_optemplate", 2, &field);
}

static int	DBpatch_4050126(void)
{
	return DBcreate_index("lld_override_optemplate", "lld_override_optemplate_2", "templateid", 0);
}

static int	DBpatch_4050127(void)
{
	const ZBX_TABLE	table =
		{"lld_override_opinventory", "lld_override_operationid", 0,
			{
				{"lld_override_operationid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
				{"inventory_mode", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
				{0}
			},
			NULL
		};

	return DBcreate_table(&table);
}

static int	DBpatch_4050128(void)
{
	const ZBX_FIELD	field = {"lld_override_operationid", NULL, "lld_override_operation", "lld_override_operationid",
			0, 0, 0, ZBX_FK_CASCADE_DELETE};

	return DBadd_foreign_key("lld_override_opinventory", 1, &field);
}

static int	DBpatch_4050129(void)
{
	const ZBX_FIELD field = {"discover", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	return DBadd_field("items", &field);
}

static int	DBpatch_4050130(void)
{
	const ZBX_FIELD field = {"discover", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	return DBadd_field("triggers", &field);
}

static int	DBpatch_4050131(void)
{
	const ZBX_FIELD field = {"discover", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	return DBadd_field("hosts", &field);
}

static int	DBpatch_4050132(void)
{
	const ZBX_FIELD field = {"discover", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	return DBadd_field("graphs", &field);
}

static int	DBpatch_4050133(void)
{
	const ZBX_FIELD field = {"lastcheck", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	return DBadd_field("trigger_discovery", &field);
}

static int	DBpatch_4050134(void)
{
	const ZBX_FIELD field = {"ts_delete", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	return DBadd_field("trigger_discovery", &field);
}

static int	DBpatch_4050135(void)
{
	const ZBX_FIELD field = {"lastcheck", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	return DBadd_field("graph_discovery", &field);
}

static int	DBpatch_4050136(void)
{
	const ZBX_FIELD field = {"ts_delete", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0};

	return DBadd_field("graph_discovery", &field);
}

#endif

DBPATCH_START(4050)

/* version, duplicates flag, mandatory flag */

DBPATCH_ADD(4050000, 0, 1)	/* add mandatory_rsm and optional_rsm fields to the dbversion table */
DBPATCH_ADD(4050001, 0, 1)
DBPATCH_ADD(4050002, 0, 1)
DBPATCH_ADD(4050003, 0, 1)
DBPATCH_ADD(4050004, 0, 1)
DBPATCH_ADD(4050005, 0, 1)
DBPATCH_ADD(4050006, 0, 1)
DBPATCH_ADD(4050007, 0, 1)
DBPATCH_ADD(4050011, 0, 1)
DBPATCH_ADD(4050012, 0, 1)
DBPATCH_RSM(4050012, 1, 0, 1)	/* RSM FY20 */
DBPATCH_RSM(4050012, 2, 0, 1)	/* set delay as macro for rsm.dns.*, rsm.rdds*, rsm.rdap* and rsm.epp* items items */
DBPATCH_RSM(4050012, 3, 0, 0)	/* set global macro descriptions */
DBPATCH_RSM(4050012, 4, 0, 0)	/* set host macro descriptions */
DBPATCH_RSM(4050012, 5, 0, 0)	/* disable "db watchdog" internal items */
DBPATCH_RSM(4050012, 6, 0, 0)	/* upgrade "TLDs" action (upgrade process to Zabbix 4.x failed to upgrade it) */
DBPATCH_RSM(4050012, 7, 0, 0)	/* add "DNS test mode" and "Transport protocol" value mappings */
DBPATCH_RSM(4050012, 8, 0, 0)	/* add DNS test related global macros */
DBPATCH_RSM(4050012, 9, 0, 0)	/* add "Template Config History" template */
DBPATCH_RSM(4050012, 10, 0, 0)	/* add "Template DNS Test" template */
DBPATCH_RSM(4050012, 11, 0, 0)	/* add "Template RDDS Test" template */
DBPATCH_RSM(4050012, 12, 0, 0)	/* rename "Template RDAP" to "Template RDAP Test" */
DBPATCH_RSM(4050012, 13, 0, 0)	/* add "Template DNS Status" template */
DBPATCH_RSM(4050012, 14, 0, 0)	/* add "Template DNSSEC Status" template */
DBPATCH_RSM(4050012, 15, 0, 0)	/* add "Template RDAP Status" template */
DBPATCH_RSM(4050012, 16, 0, 0)	/* add "Template RDDS Status" template */
DBPATCH_RSM(4050012, 17, 0, 0)	/* add "Template Probe Status" template */
DBPATCH_RSM(4050012, 18, 0, 0)	/* convert "<rshmost> <probe>" hosts to use "Template DNS Test" template */
DBPATCH_RSM(4050012, 19, 0, 0)	/* convert "<rshmost> <probe>" hosts to use "Template RDDS Test" template */
DBPATCH_RSM(4050012, 20, 0, 0)	/* set RDAP master item value_type to the text type */
DBPATCH_RSM(4050012, 21, 0, 0)	/* set RDAP calculated items to be dependent items */
DBPATCH_RSM(4050012, 22, 0, 0)	/* add item_preproc to RDAP ip and rtt items */
DBPATCH_RSM(4050012, 23, 0, 0)	/* convert "<rsmhost>" hosts to use "Template DNS Status" template */
DBPATCH_RSM(4050012, 24, 0, 0)	/* convert "<rsmhost>" hosts to use "Template DNSSEC Status" template */
DBPATCH_RSM(4050012, 25, 0, 0)	/* convert "<rsmhost>" hosts to use "Template RDAP Status" template */
DBPATCH_RSM(4050012, 26, 0, 0)	/* convert "<rsmhost>" hosts to use "Template RDDS Status" template */
DBPATCH_RSM(4050012, 27, 0, 0)	/* convert "<probe>" hosts to use "Template Probe Status" template */
DBPATCH_RSM(4050012, 28, 0, 0)	/* convert "<rsmhost>" hosts to use "Template Config History" template */
DBPATCH_RSM(4050012, 29, 0, 0)	/* convert "Template <rsmhost>" templates into "Template Rsmhost Config <rsmhost>", link to "<rsmhost>" hosts */
DBPATCH_RSM(4050012, 30, 0, 0)	/* delete "Template <probe> Status" templates */
DBPATCH_RSM(4050012, 31, 0, 0)	/* delete "Template Probe Errors" templates */
DBPATCH_RSM(4050012, 32, 0, 0)	/* rename "Template <probe>" template into "Template Probe Config <probe>", link to "<probe>" hosts */
DBPATCH_ADD(4050014, 0, 1)
DBPATCH_ADD(4050015, 0, 1)
DBPATCH_ADD(4050016, 0, 1)
DBPATCH_ADD(4050017, 0, 1)
DBPATCH_ADD(4050018, 0, 1)
DBPATCH_ADD(4050019, 0, 1)
DBPATCH_ADD(4050020, 0, 1)
DBPATCH_ADD(4050021, 0, 1)
DBPATCH_ADD(4050022, 0, 1)
DBPATCH_ADD(4050023, 0, 1)
DBPATCH_ADD(4050024, 0, 1)
DBPATCH_ADD(4050025, 0, 1)
DBPATCH_ADD(4050026, 0, 1)
DBPATCH_ADD(4050027, 0, 1)
DBPATCH_ADD(4050028, 0, 1)
DBPATCH_ADD(4050030, 0, 1)
DBPATCH_ADD(4050031, 0, 1)
DBPATCH_ADD(4050032, 0, 1)
DBPATCH_ADD(4050033, 0, 1)
DBPATCH_ADD(4050034, 0, 1)
DBPATCH_ADD(4050035, 0, 1)
DBPATCH_ADD(4050036, 0, 1)
DBPATCH_ADD(4050037, 0, 1)
DBPATCH_ADD(4050038, 0, 1)
DBPATCH_ADD(4050039, 0, 1)
DBPATCH_ADD(4050040, 0, 1)
DBPATCH_ADD(4050041, 0, 1)
DBPATCH_ADD(4050042, 0, 1)
DBPATCH_ADD(4050043, 0, 0)
DBPATCH_ADD(4050044, 0, 1)
DBPATCH_ADD(4050045, 0, 1)
DBPATCH_ADD(4050046, 0, 1)
DBPATCH_ADD(4050047, 0, 1)
DBPATCH_ADD(4050048, 0, 1)
DBPATCH_ADD(4050049, 0, 1)
DBPATCH_ADD(4050050, 0, 1)
DBPATCH_ADD(4050051, 0, 1)
DBPATCH_ADD(4050052, 0, 1)
DBPATCH_ADD(4050053, 0, 1)
DBPATCH_ADD(4050054, 0, 1)
DBPATCH_ADD(4050055, 0, 1)
DBPATCH_ADD(4050056, 0, 1)
DBPATCH_ADD(4050057, 0, 1)
DBPATCH_ADD(4050058, 0, 1)
DBPATCH_ADD(4050059, 0, 1)
DBPATCH_ADD(4050060, 0, 1)
DBPATCH_ADD(4050061, 0, 1)
DBPATCH_ADD(4050062, 0, 1)
DBPATCH_ADD(4050063, 0, 1)
DBPATCH_ADD(4050064, 0, 1)
DBPATCH_ADD(4050065, 0, 1)
DBPATCH_ADD(4050066, 0, 1)
DBPATCH_ADD(4050067, 0, 1)
DBPATCH_ADD(4050068, 0, 1)
DBPATCH_ADD(4050069, 0, 1)
DBPATCH_ADD(4050070, 0, 1)
DBPATCH_ADD(4050071, 0, 1)
DBPATCH_ADD(4050072, 0, 1)
DBPATCH_ADD(4050073, 0, 1)
DBPATCH_ADD(4050074, 0, 1)
DBPATCH_ADD(4050075, 0, 1)
DBPATCH_ADD(4050076, 0, 1)
DBPATCH_ADD(4050077, 0, 1)
DBPATCH_ADD(4050078, 0, 1)
DBPATCH_ADD(4050079, 0, 1)
DBPATCH_ADD(4050080, 0, 1)
DBPATCH_ADD(4050081, 0, 1)
DBPATCH_ADD(4050082, 0, 1)
DBPATCH_ADD(4050083, 0, 1)
DBPATCH_ADD(4050084, 0, 1)
DBPATCH_ADD(4050085, 0, 1)
DBPATCH_ADD(4050086, 0, 1)
DBPATCH_ADD(4050087, 0, 1)
DBPATCH_ADD(4050088, 0, 1)
DBPATCH_ADD(4050089, 0, 1)
DBPATCH_ADD(4050090, 0, 1)
DBPATCH_ADD(4050091, 0, 1)
DBPATCH_ADD(4050092, 0, 1)
DBPATCH_ADD(4050093, 0, 1)
DBPATCH_ADD(4050094, 0, 1)
DBPATCH_ADD(4050095, 0, 1)
DBPATCH_ADD(4050096, 0, 1)
DBPATCH_ADD(4050097, 0, 1)
DBPATCH_ADD(4050098, 0, 1)
DBPATCH_ADD(4050099, 0, 1)
DBPATCH_ADD(4050100, 0, 1)
DBPATCH_ADD(4050101, 0, 1)
DBPATCH_ADD(4050102, 0, 1)
DBPATCH_ADD(4050103, 0, 1)
DBPATCH_ADD(4050104, 0, 1)
DBPATCH_ADD(4050105, 0, 1)
DBPATCH_ADD(4050106, 0, 1)
DBPATCH_ADD(4050107, 0, 1)
DBPATCH_ADD(4050108, 0, 1)
DBPATCH_ADD(4050109, 0, 1)
DBPATCH_ADD(4050110, 0, 1)
DBPATCH_ADD(4050111, 0, 1)
DBPATCH_ADD(4050112, 0, 1)
DBPATCH_ADD(4050113, 0, 1)
DBPATCH_ADD(4050114, 0, 1)
DBPATCH_ADD(4050115, 0, 1)
DBPATCH_ADD(4050116, 0, 1)
DBPATCH_ADD(4050117, 0, 1)
DBPATCH_ADD(4050118, 0, 1)
DBPATCH_ADD(4050119, 0, 1)
DBPATCH_ADD(4050120, 0, 1)
DBPATCH_ADD(4050121, 0, 1)
DBPATCH_ADD(4050122, 0, 1)
DBPATCH_ADD(4050123, 0, 1)
DBPATCH_ADD(4050124, 0, 1)
DBPATCH_ADD(4050125, 0, 1)
DBPATCH_ADD(4050126, 0, 1)
DBPATCH_ADD(4050127, 0, 1)
DBPATCH_ADD(4050128, 0, 1)
DBPATCH_ADD(4050129, 0, 1)
DBPATCH_ADD(4050130, 0, 1)
DBPATCH_ADD(4050131, 0, 1)
DBPATCH_ADD(4050132, 0, 1)
DBPATCH_ADD(4050133, 0, 1)
DBPATCH_ADD(4050134, 0, 1)
DBPATCH_ADD(4050135, 0, 1)
DBPATCH_ADD(4050136, 0, 1)

DBPATCH_END()
