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

static int	DBpatch_4050012_1(void)
{
	/* this patch begins RSM FY20 upgrade sequence and has been intentionally left blank */

	return SUCCEED;
}

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

static int	DBpatch_4050012_5(void)
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

static int	DBpatch_4050012_6(void)
{
	int		ret = FAIL;

	zbx_uint64_t	groupid_templates;				/* groupid of "Templates" host group */

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
	zbx_uint64_t	itemid_rsm_dns_nssok;				/* itemid of "Number of working Name Servers" item in "Template DNS Test" template */
	zbx_uint64_t	itemid_rsm_dns_ns_discovery;			/* itemid of "Name Servers discovery" item in "Template DNS Test" template */
	zbx_uint64_t	itemid_rsm_dns_nsip_discovery;			/* itemid of "NS-IP pairs discovery" item in "Template DNS Test" template */
	zbx_uint64_t	itemid_rsm_dns_ns_status;			/* itemid of "Status of $1" item prototype in "Template DNS Test" template */
	zbx_uint64_t	itemid_rsm_dns_rtt_tcp;				/* itemid of "RTT of $1,$2 using $3" item prototype in "Template DNS Test" template */
	zbx_uint64_t	itemid_rsm_dns_rtt_udp;				/* itemid of "RTT of $1,$2 using $3" item prototype in "Template DNS Test" template */
	zbx_uint64_t	itemid_rsm_dns_nsid;				/* itemid of "NSID of $1,$2" item prototype in "Template DNS Test" template */
	zbx_uint64_t	itemid_rsm_dns_mode;				/* itemid of "The mode of the Test" item prototype in "Template DNS Test" template */
	zbx_uint64_t	itemid_rsm_dns_protocol;			/* itemid of "Transport protocol of the Test" item prototype in "Template DNS Test" template */

	ONLY_SERVER();

	GET_HOST_GROUP_ID(groupid_templates, "Templates");

	GET_VALUE_MAP_ID(valuemapid_rsm_service_availability, "RSM Service Availability");
	GET_VALUE_MAP_ID(valuemapid_dns_test_mode, "DNS test mode");
	GET_VALUE_MAP_ID(valuemapid_transport_protocol, "Transport protocol");
	GET_VALUE_MAP_ID(valuemapid_rsm_dns_rtt, "RSM DNS rtt");

	hostid = DBget_maxid_num("hosts", 1);

	applicationid_next   = DBget_maxid_num("applications", 2);
	applicationid_dns    = applicationid_next++;
	applicationid_dnssec = applicationid_next++;

	itemid_next                   = DBget_maxid_num("items", 11);
	itemid_dnssec_enabled         = itemid_next++;
	itemid_rsm_dns                = itemid_next++;
	itemid_rsm_dns_nssok          = itemid_next++;
	itemid_rsm_dns_ns_discovery   = itemid_next++;
	itemid_rsm_dns_nsip_discovery = itemid_next++;
	itemid_rsm_dns_ns_status      = itemid_next++;
	itemid_rsm_dns_rtt_tcp        = itemid_next++;
	itemid_rsm_dns_rtt_udp        = itemid_next++;
	itemid_rsm_dns_nsid           = itemid_next++;
	itemid_rsm_dns_mode           = itemid_next++;
	itemid_rsm_dns_protocol       = itemid_next++;

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
			"{$RSM.DNS.UDP.RTT.HIGH},{$RSM.DNS.TCP.RTT.HIGH}]",
		"{$RSM.DNS.UDP.DELAY}", "0", "0",
		ITEM_VALUE_TYPE_TEXT, (zbx_uint64_t)0, "", 0,
		"Master item that performs the test and generates JSON with results."
			" This JSON will be parsed by dependent items. History must be disabled.",
		"30d", (zbx_uint64_t)0);
	DB_EXEC(SQL, itemid_rsm_dns_nssok, ITEM_TYPE_DEPENDENT, hostid,
		"Number of working Name Servers", "rsm.dns.nssok", "0", "90d", "365d",
		ITEM_VALUE_TYPE_UINT64, (zbx_uint64_t)0, "", 0,
		"Number of Name Servers that returned successful results out of those used in the test.",
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
	DB_EXEC(SQL, itemid_rsm_dns_nsid, ITEM_TYPE_DEPENDENT, hostid,
		"NSID of $1,$2", "rsm.dns.nsid[{#NS},{#IP}]", "0", "90d", "0",
		ITEM_VALUE_TYPE_STR, (zbx_uint64_t)0, "", ZBX_FLAG_DISCOVERY_PROTOTYPE,
		"DNS Name Server Identifier of the target Name Server that was tested.",
		"30d", itemid_rsm_dns);
	DB_EXEC(SQL, itemid_rsm_dns_mode, ITEM_TYPE_DEPENDENT, hostid,
		"The mode of the Test", "rsm.dns.mode", "0", "90d", "365d",
		ITEM_VALUE_TYPE_UINT64, valuemapid_dns_test_mode, "", 0,
		"The mode (normal or critical) in which the test was performed.",
		"30d", itemid_rsm_dns);
	DB_EXEC(SQL, itemid_rsm_dns_protocol, ITEM_TYPE_DEPENDENT, hostid,
		"Transport protocol of the Test", "rsm.dns.protocol", "0", "90d", "365d",
		ITEM_VALUE_TYPE_UINT64, valuemapid_transport_protocol, "", 0,
		"Transport protocol (UDP or TCP) that was used during the test.",
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
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_dns   , itemid_rsm_dns_nssok);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_dns   , itemid_rsm_dns_ns_status);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_dns   , itemid_rsm_dns_rtt_tcp);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_dns   , itemid_rsm_dns_rtt_udp);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_dns   , itemid_rsm_dns_nsid);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_dns   , itemid_rsm_dns_mode);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_dns   , itemid_rsm_dns_protocol);
#undef SQL

#define SQL	"insert into item_discovery set itemdiscoveryid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64 ","		\
		"parent_itemid=" ZBX_FS_UI64 ",key_='',lastcheck=0,ts_delete=0"
	DB_EXEC(SQL, DBget_maxid_num("item_discovery", 1), itemid_rsm_dns_ns_status, itemid_rsm_dns_ns_discovery);
	DB_EXEC(SQL, DBget_maxid_num("item_discovery", 1), itemid_rsm_dns_rtt_tcp  , itemid_rsm_dns_nsip_discovery);
	DB_EXEC(SQL, DBget_maxid_num("item_discovery", 1), itemid_rsm_dns_rtt_udp  , itemid_rsm_dns_nsip_discovery);
	DB_EXEC(SQL, DBget_maxid_num("item_discovery", 1), itemid_rsm_dns_nsid     , itemid_rsm_dns_nsip_discovery);
#undef SQL

#define SQL	"insert into item_preproc set"										\
		" item_preprocid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64 ",step=%d,type=%d,params='%s',"			\
		"error_handler=%d,error_handler_params=''"
	/* type 12 = ZBX_PREPROC_JSONPATH */
	DB_EXEC(SQL, DBget_maxid_num("item_preproc", 1), itemid_rsm_dns_nssok, 1, 12,
			"$.nssok", 0);
	DB_EXEC(SQL, DBget_maxid_num("item_preproc", 1), itemid_rsm_dns_ns_discovery, 1, 12,
			"$.nss", 0);
	DB_EXEC(SQL, DBget_maxid_num("item_preproc", 1), itemid_rsm_dns_nsip_discovery, 1, 12,
			"$.nsips", 0);
	DB_EXEC(SQL, DBget_maxid_num("item_preproc", 1), itemid_rsm_dns_ns_status, 1, 12,
			"$.nss[?(@.[''ns''] == ''{#NS}'')].status.first()", 1);
	DB_EXEC(SQL, DBget_maxid_num("item_preproc", 1), itemid_rsm_dns_rtt_tcp, 1, 12,
			"$.nsips[?(@.[''ns''] == ''{#NS}'' && @.[''ip''] == ''{#IP}'' && @.[''protocol''] == ''tcp'')].rtt.first()", 1);
	DB_EXEC(SQL, DBget_maxid_num("item_preproc", 1), itemid_rsm_dns_rtt_udp, 1, 12,
			"$.nsips[?(@.[''ns''] == ''{#NS}'' && @.[''ip''] == ''{#IP}'' && @.[''protocol''] == ''udp'')].rtt.first()", 1);
	DB_EXEC(SQL, DBget_maxid_num("item_preproc", 1), itemid_rsm_dns_nsid, 1, 12,
			"$.nsips[?(@.[''ns''] == ''{#NS}'' && @.[''ip''] == ''{#IP}'')].nsid.first()", 1);
	DB_EXEC(SQL, DBget_maxid_num("item_preproc", 1), itemid_rsm_dns_mode, 1, 12,
			"$.mode", 0);
	DB_EXEC(SQL, DBget_maxid_num("item_preproc", 1), itemid_rsm_dns_protocol, 1, 12,
			"$.protocol", 0);
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

static int	DBpatch_4050012_7_create_application(zbx_uint64_t *applicationid, zbx_uint64_t template_applicationid,
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

static int	DBpatch_4050012_7_copy_preproc(zbx_uint64_t src_itemid, zbx_uint64_t dst_itemid,
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

static int	DBpatch_4050012_7_copy_lld_macros(zbx_uint64_t src_itemid, zbx_uint64_t dst_itemid)
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

static int	DBpatch_4050012_7_create_item(zbx_uint64_t *new_itemid, zbx_uint64_t templateid, zbx_uint64_t hostid,
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

	CHECK(DBpatch_4050012_7_copy_preproc(templateid, *new_itemid, NULL));

	CHECK(DBpatch_4050012_7_copy_lld_macros(templateid, *new_itemid));

	ret = SUCCEED;
out:
	return ret;
}

static int	DBpatch_4050012_7_convert_item(zbx_uint64_t *itemid, zbx_uint64_t hostid, const char *key,
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

	CHECK(DBpatch_4050012_7_copy_preproc(template_itemid, *itemid, NULL));

	ret = SUCCEED;
out:
	return ret;
}

static int	DBpatch_4050012_7_create_item_prototype(zbx_uint64_t *new_itemid, zbx_uint64_t templateid,
		zbx_uint64_t hostid, zbx_uint64_t interfaceid, zbx_uint64_t master_itemid, zbx_uint64_t applicationid,
		zbx_uint64_t parent_itemid)
{
	int		ret = FAIL;

	CHECK(DBpatch_4050012_7_create_item(new_itemid, templateid, hostid, interfaceid, master_itemid,
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

static int	DBpatch_4050012_7_create_item_lld(zbx_uint64_t *new_itemid, const char *key,
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

	CHECK(DBpatch_4050012_7_copy_preproc(prototype_itemid, *new_itemid, preproc_replacements));

	DB_EXEC("insert into item_discovery (itemdiscoveryid,itemid,parent_itemid,key_,lastcheck,ts_delete)"
			" select " ZBX_FS_UI64 "," ZBX_FS_UI64 ",itemid,key_,0,0 from items where itemid=" ZBX_FS_UI64,
		DBget_maxid_num("item_discovery", 1), *new_itemid, prototype_itemid);

	ret = SUCCEED;
out:
	return ret;
}

static int	DBpatch_4050012_7_convert_item_lld(zbx_uint64_t *itemid, zbx_uint64_t hostid, const char *old_key,
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

	CHECK(DBpatch_4050012_7_copy_preproc(prototype_itemid, *itemid, preproc_replacements));

	DB_EXEC("insert into item_discovery (itemdiscoveryid,itemid,parent_itemid,key_,lastcheck,ts_delete)"
			" select " ZBX_FS_UI64 "," ZBX_FS_UI64 ",itemid,key_,0,0 from items where itemid=" ZBX_FS_UI64,
		DBget_maxid_num("item_discovery", 1), *itemid, prototype_itemid);

	ret = SUCCEED;
out:
	return ret;
}

static int	DBpatch_4050012_7(void)
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

	GET_TEMPLATE_APPLICATION_ID(template_applicationid_dns   , "Template DNS Test", "DNS");
	GET_TEMPLATE_APPLICATION_ID(template_applicationid_dnssec, "Template DNS Test", "DNSSEC");

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
		zbx_uint64_t	itemid_rsm_dns_nssok;			/* itemid of "Number of working Name Servers" item */

		ZBX_STR2UINT64(hostid, row[0]);
		ZBX_STR2UINT64(interfaceid, row[1]);

		/* link "Template DNS Test" template to the host */
		DB_EXEC("insert into hosts_templates set"
				" hosttemplateid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ",templateid=" ZBX_FS_UI64,
			DBget_maxid_num("hosts_templates", 1), hostid, hostid_template_dns_test);

		/* create applications */
		CHECK(DBpatch_4050012_7_create_application(&applicationid_dns   , template_applicationid_dns   , hostid, "DNS"));
		CHECK(DBpatch_4050012_7_create_application(&applicationid_dnssec, template_applicationid_dnssec, hostid, "DNSSEC"));

		/* update dnssec.enabled item */
		CHECK(DBpatch_4050012_7_convert_item(&itemid_dnssec_enabled, hostid, "dnssec.enabled",
				0, template_itemid_dnssec_enabled, applicationid_dnssec));

		/* create "DNS Test" (rsm.dns[...]) master item */
		CHECK(DBpatch_4050012_7_create_item(&itemid_rsm_dns,
				template_itemid_rsm_dns, hostid, interfaceid, 0, applicationid_dns));

		/* create "Name Servers discovery" (rsm.dns.ns.discovery) discovery rule */
		CHECK(DBpatch_4050012_7_create_item(&itemid_rsm_dns_ns_discovery,
				template_itemid_rsm_dns_ns_discovery, hostid, 0, itemid_rsm_dns, 0));

		/* create "NS-IP pairs discovery" (rsm.dns.nsip.discovery) discovery rule */
		CHECK(DBpatch_4050012_7_create_item(&itemid_rsm_dns_nsip_discovery,
				template_itemid_rsm_dns_nsip_discovery, hostid, 0, itemid_rsm_dns, 0));

		/* create "Status of {#NS}" (rsm.dns.ns.status[{#NS}]) item prototype */
		CHECK(DBpatch_4050012_7_create_item_prototype(&prototype_itemid_rsm_dns_ns_status,
				template_itemid_rsm_dns_ns_status, hostid, 0, itemid_rsm_dns, applicationid_dns,
				itemid_rsm_dns_ns_discovery));

		/* create "NSID of {#NS},{#IP}" (rsm.dns.nsid[{#NS},{#IP}]) item prototype */
		CHECK(DBpatch_4050012_7_create_item_prototype(&prototype_itemid_rsm_dns_nsid,
				template_itemid_rsm_dns_nsid, hostid, 0, itemid_rsm_dns, applicationid_dns,
				itemid_rsm_dns_nsip_discovery));

		/* create "RTT of {#NS},{#IP} using tcp" (rsm.dns.rtt[{#NS},{#IP},tcp]) item prototype */
		CHECK(DBpatch_4050012_7_create_item_prototype(&prototype_itemid_rsm_dns_rtt_tcp,
				template_itemid_rsm_dns_rtt_tcp, hostid, 0, itemid_rsm_dns, applicationid_dns,
				itemid_rsm_dns_nsip_discovery));

		/* create "RTT of {#NS},{#IP} using udp" (rsm.dns.rtt[{#NS},{#IP},udp]) item prototype */
		CHECK(DBpatch_4050012_7_create_item_prototype(&prototype_itemid_rsm_dns_rtt_udp,
				template_itemid_rsm_dns_rtt_udp, hostid, 0, itemid_rsm_dns, applicationid_dns,
				itemid_rsm_dns_nsip_discovery));

		/* create "The mode of the Test" (rsm.dns.mode) item */
		CHECK(DBpatch_4050012_7_create_item(&itemid_rsm_dns_mode,
				template_itemid_rsm_dns_mode, hostid, 0, itemid_rsm_dns, applicationid_dns));

		/* create "Transport protocol of the Test" rsm.dns.protocol */
		CHECK(DBpatch_4050012_7_create_item(&itemid_rsm_dns_protocol,
				template_itemid_rsm_dns_protocol, hostid, 0, itemid_rsm_dns, applicationid_dns));

		/* delete rsm.dns.tcp[{$RSM.TLD}] item */
		DB_EXEC("delete from items where key_='rsm.dns.tcp[{$RSM.TLD}]' and hostid=" ZBX_FS_UI64, hostid);

		/* convert rsm.dns.udp[{$RSM.TLD}] item into rsm.dns.nssok */
		CHECK(DBpatch_4050012_7_convert_item(&itemid_rsm_dns_nssok, hostid, "rsm.dns.udp[{$RSM.TLD}]",
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
			CHECK(DBpatch_4050012_7_create_item_lld(&itemid, key, prototype_itemid_rsm_dns_ns_status,
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
			CHECK(DBpatch_4050012_7_create_item_lld(&itemid, new_key,
					prototype_itemid_rsm_dns_nsid, applicationid_dns, preproc_replacements));

			/* convert "RTT of <ns>,<ip> using tcp" (rsm.dns.rtt[<ns>,<ip>,tcp]) item */
			zbx_snprintf(old_key, sizeof(old_key), "rsm.dns.tcp.rtt[{$RSM.TLD},%s,%s]", row[0], row[1]);
			zbx_snprintf(new_key, sizeof(new_key), "rsm.dns.rtt[%s,%s,tcp]", row[0], row[1]);
			CHECK(DBpatch_4050012_7_convert_item_lld(&itemid, hostid, old_key, new_key,
					prototype_itemid_rsm_dns_rtt_tcp, applicationid_dns, preproc_replacements));

			/* convert "RTT of <ns>,<ip> using udp" (rsm.dns.rtt[<ns>,<ip>,udp]) item */
			zbx_snprintf(old_key, sizeof(old_key), "rsm.dns.udp.rtt[{$RSM.TLD},%s,%s]", row[0], row[1]);
			zbx_snprintf(new_key, sizeof(new_key), "rsm.dns.rtt[%s,%s,udp]", row[0], row[1]);
			CHECK(DBpatch_4050012_7_convert_item_lld(&itemid, hostid, old_key, new_key,
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

static int	DBpatch_4050012_8(void)
{
	int	ret;

	ONLY_SERVER();

	ret = DBexecute("update items set status=%d where key_ like 'zabbix[process,db watchdog,%%' "
			"and type=%d", ITEM_STATUS_DISABLED, ITEM_TYPE_INTERNAL);

	if (ZBX_DB_OK <= ret)
		zabbix_log(LOG_LEVEL_WARNING, "disabled %d db watchdog items", ret);

	return SUCCEED;
}

static int	DBpatch_4050012_9(void)
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

static int	DBpatch_4050012_10(void)
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

static int	DBpatch_4050012_11(void)
{
	int		ret = FAIL;

	zbx_uint64_t	groupid_templates;		/* groupid of "Templates" host group */
	zbx_uint64_t	valuemapid_rsm_rdds_rtt;	/* valuemapid of "RSM RDDS rtt" value map */
	zbx_uint64_t	valuemapid_rsm_rdds_result;	/* valuemapid of "RSM RDDS result" value map */                 /* TODO: check the name of the valuemap */

	zbx_uint64_t	hostid;				/* hostid of "Template RDDS Test" template */

	zbx_uint64_t	applicationid_next;
	zbx_uint64_t	applicationid_rdds;		/* applicationid of "RDDS" application */
	zbx_uint64_t	applicationid_rdds43;		/* applicationid of "RDDS43" application */
	zbx_uint64_t	applicationid_rdds80;		/* applicationid of "RDDS80" application */

	zbx_uint64_t	itemid_next;
	zbx_uint64_t	itemid_rsm_rdds;		/* itemid of "rsm.rdds[]" item */
	zbx_uint64_t	itemid_rsm_rdds_status;		/* itemid of "rsm.rdds.status" item */                          /* TODO: check key, variable's name */
	zbx_uint64_t	itemid_rsm_rdds43_ip;		/* itemid of "rsm.rdds.43.ip" item */
	zbx_uint64_t	itemid_rsm_rdds43_rtt;		/* itemid of "rsm.rdds.43.rtt" item */
	zbx_uint64_t	itemid_rsm_rdds80_ip;		/* itemid of "rsm.rdds.80.ip" item */
	zbx_uint64_t	itemid_rsm_rdds80_rtt;		/* itemid of "rsm.rdds.80.rtt" item */

	ONLY_SERVER();

	GET_HOST_GROUP_ID(groupid_templates, "Templates");
	GET_VALUE_MAP_ID(valuemapid_rsm_rdds_rtt, "RSM RDDS rtt");
	GET_VALUE_MAP_ID(valuemapid_rsm_rdds_result, "RSM RDDS result");                                                /* TODO: check the name of the valuemap */

	hostid = DBget_maxid_num("hosts", 1);

	applicationid_next   = DBget_maxid_num("applications", 3);
	applicationid_rdds   = applicationid_next++;
	applicationid_rdds43 = applicationid_next++;
	applicationid_rdds80 = applicationid_next++;

	itemid_next            = DBget_maxid_num("items", 6);
	itemid_rsm_rdds        = itemid_next++;
	itemid_rsm_rdds_status = itemid_next++;
	itemid_rsm_rdds43_ip   = itemid_next++;
	itemid_rsm_rdds43_rtt  = itemid_next++;
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
			"{$RSM.RDDS.TESTPREFIX},{$RSM.RDDS.NS.STRING},{$RSM.RDDS.ENABLED},"
			"{$RSM.TLD.RDDS.ENABLED},{$RSM.EPP.ENABLED},{$RSM.TLD.EPP.ENABLED},{$RSM.IP4.ENABLED},"
			"{$RSM.IP6.ENABLED},{$RSM.RESOLVER},{$RSM.RDDS.RTT.HIGH},{$RSM.RDDS.MAXREDIRS}]",
		"{$RSM.RDDS.DELAY}", "0", "0", 4, (zbx_uint64_t)0,
		"Master item that performs the RDDS test and generates JSON with results. This JSON will be"
			" parsed by dependent items. History must be disabled.",
		(zbx_uint64_t)0);
	DB_EXEC(SQL, itemid_rsm_rdds_status, 18, hostid, "RDDS status",                                                 /* TODO: check values */
		"rsm.rdds.status",                                                                                      /* TODO: check values */
		"0", "90d", "0", 3, valuemapid_rsm_rdds_result,                                                         /* TODO: check values */
		"Status of the RDDS",                                                                                   /* TODO: check values */
		itemid_rsm_rdds);                                                                                       /* TODO: check values */
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
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_rdds  , itemid_rsm_rdds_status);           /* TODO: check applicationid */
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_rdds43, itemid_rsm_rdds43_ip);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_rdds43, itemid_rsm_rdds43_rtt);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_rdds80, itemid_rsm_rdds80_ip);
	DB_EXEC(SQL, DBget_maxid_num("items_applications", 1), applicationid_rdds80, itemid_rsm_rdds80_rtt);
#undef SQL

#define SQL	"insert into item_preproc set"										\
		" item_preprocid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64 ",step=%d,type=%d,params='%s',"			\
		"error_handler=%d,error_handler_params=''"
	/* type 12 = ZBX_PREPROC_JSONPATH */
	DB_EXEC(SQL, DBget_maxid_num("item_preproc", 1), itemid_rsm_rdds_status, 1, 12, "$.status"    , 0);             /* TODO: check values */
	DB_EXEC(SQL, DBget_maxid_num("item_preproc", 1), itemid_rsm_rdds43_ip  , 1, 12, "$.rdds43.ip" , 1);
	DB_EXEC(SQL, DBget_maxid_num("item_preproc", 1), itemid_rsm_rdds43_rtt , 1, 12, "$.rdds43.rtt", 0);
	DB_EXEC(SQL, DBget_maxid_num("item_preproc", 1), itemid_rsm_rdds80_ip  , 1, 12, "$.rdds80.ip" , 1);
	DB_EXEC(SQL, DBget_maxid_num("item_preproc", 1), itemid_rsm_rdds80_rtt , 1, 12, "$.rdds80.rtt", 0);
#undef SQL

	ret = SUCCEED;
out:
	return ret;
}

static int	DBpatch_4050012_12(void)
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

static int	DBpatch_4050012_13(void)
{
	int	ret = FAIL;

	ONLY_SERVER();

	DB_EXEC("update hosts set host='Template RDAP Test',name='Template RDAP Test' where host='Template RDAP'");

	ret = SUCCEED;
out:
	return ret;
}

static int	DBpatch_4050012_14(void)
{
	int	ret = FAIL;

	ONLY_SERVER();

	/* 4 = ITEM_VALUE_TYPE_TEXT */
	DB_EXEC("update items set name='RDAP Test',value_type=4,history='0',trends='0' where key_ like 'rdap[%%'");

	ret = SUCCEED;
out:
	return ret;
}

static int	DBpatch_4050012_15(void)
{
	int	ret = FAIL;

	ONLY_SERVER();

	/* 18 = ITEM_TYPE_DEPENDENT */
	DB_EXEC("update items as i1 inner join items as i2 on i1.hostid=i2.hostid set"
			" i1.type=18,i1.master_itemid=i2.itemid where i1.key_ in ('rdap.ip','rdap.rtt') and"
			" i2.key_ like 'rdap[%%'");

	ret = SUCCEED;
out:
	return ret;
}

static int	db_insert_rdap_item_preproc(const char *item_key, const char *item_preproc_param)
{
	DB_RESULT		result;
	DB_ROW			row;
	zbx_vector_uint64_t	rdap_itemids;
	int			i, ret = FAIL;
	zbx_uint64_t		item_preprocid_next;

	zbx_vector_uint64_create(&rdap_itemids);

	result = DBselect("select itemid from items where key_='%s'", item_key);

	while (NULL != (row = DBfetch(result)))
	{
		zbx_uint64_t	rdap_itemid;

		ZBX_STR2UINT64(rdap_itemid, row[0]);
		zbx_vector_uint64_append(&rdap_itemids, rdap_itemid);
	}

	DBfree_result(result);

	item_preprocid_next = DBget_maxid_num("item_preproc", rdap_itemids.values_num);

	for (i = 0; i < rdap_itemids.values_num; i++)
	{
		/* 12 = ZBX_PREPROC_JSONPATH */
		DB_EXEC("insert into item_preproc set item_preprocid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64 ",step=1,"
				"type=12,params='%s',error_handler=0",
			item_preprocid_next, rdap_itemids.values[i], item_preproc_param);
		item_preprocid_next++;
	}

	ret = SUCCEED;
out:
	zbx_vector_uint64_destroy(&rdap_itemids);

	return ret;
}

static int	DBpatch_4050012_16(void)
{

	int	ret = FAIL;

	ONLY_SERVER();

	CHECK(db_insert_rdap_item_preproc("rdap.ip", "$.rdap.ip"));
	CHECK(db_insert_rdap_item_preproc("rdap.rtt", "$.rdap.rtt"));

	ret = SUCCEED;
out:
	return ret;
}

#endif

static int	DBpatch_4050012_17(void)
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

static int	DBpatch_4050012_18(void)
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

static int	DBpatch_4050012_19(void)
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

static int	DBpatch_4050012_20(void)
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
	zbx_uint64_t	template_itemid_rsm_rdds_status;		/* itemid of "RDDS Status" item in "Template RDDS Test" template */            /* TODO: check item's name */
	zbx_uint64_t	template_itemid_rsm_rdds43_ip;			/* itemid of "RDDS43 IP" item in "Template RDDS Test" template */
	zbx_uint64_t	template_itemid_rsm_rdds43_rtt;			/* itemid of "RDDS43 RTT" item in "Template RDDS Test" template */
	zbx_uint64_t	template_itemid_rsm_rdds80_ip;			/* itemid of "RDDS80 IP" item in "Template RDDS Test" template */
	zbx_uint64_t	template_itemid_rsm_rdds80_rtt;			/* itemid of "RDDS80 RTT" item in "Template RDDS Test" template */

	ONLY_SERVER();

	GET_HOST_GROUP_ID(groupid_tld_probe_resluts, "TLD Probe results");
	GET_TEMPLATE_ID(hostid_template_rdds_test, "Template RDDS Test");

	GET_TEMPLATE_APPLICATION_ID(template_applicationid_rdds  , "Template RDDS Test", "RDDS");
	GET_TEMPLATE_APPLICATION_ID(template_applicationid_rdds43, "Template RDDS Test", "RDDS43");
	GET_TEMPLATE_APPLICATION_ID(template_applicationid_rdds80, "Template RDDS Test", "RDDS80");

	GET_TEMPLATE_ITEM_ID_BY_PATTERN(template_itemid_rsm_rdds, "Template RDDS Test", "rsm.rdds[%]");
	GET_TEMPLATE_ITEM_ID(template_itemid_rsm_rdds_status, "Template RDDS Test", "rsm.rdds.status");                 /* TODO: check values */
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
		zbx_uint64_t	itemid_rsm_rdds_status;			/* itemid of "RDDS Status" item */              /* TODO: check item's name */
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

static int	DBpatch_4050012_21(void)
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

		/* link "Template Probe Status" to the <probe> host */
		DB_EXEC("insert into hosts_templates set"
				" hosttemplateid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ",templateid=" ZBX_FS_UI64,
			DBget_maxid_num("hosts_templates", 1), probe_hostid, template_hostid);

		/* link host's applications with template's applications */
#define SQL	"insert into application_template set"									\
		" application_templateid=" ZBX_FS_UI64 ",applicationid=" ZBX_FS_UI64 ",templateid=" ZBX_FS_UI64
		DB_EXEC(SQL, DBget_maxid_num("application_template", 1), applicationid_internal_errors, template_applicationid_internal_errors);
		DB_EXEC(SQL, DBget_maxid_num("application_template", 1), applicationid_probe_status   , template_applicationid_probe_status);
		DB_EXEC(SQL, DBget_maxid_num("application_template", 1), applicationid_configuration  , template_applicationid_configuration);
#undef SQL

		/* move probe.configvalue[..] items from one "<tld> <probe>" host to "<probe>" host, keep the history */
#define MIGRATE(key, template_itemid)											\
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
			itemid, template_itemid);
		MIGRATE("probe.configvalue[RSM.IP4.ENABLED]", template_itemid_probe_configvalue_rsm_ip4_enabled);
		MIGRATE("probe.configvalue[RSM.IP6.ENABLED]", template_itemid_probe_configvalue_rsm_ip6_enabled);
#undef MIGRATE
	}

	/* delete probe.confivalue[..] items from "<tld> <probe>" hosts, leave those that were moved to "<probe>" hosts */
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

	ret = SUCCEED;
out:
	DBfree_result(result);

	return ret;
}

static int	DBpatch_4050012_22(void)
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
DBPATCH_ADD(4050010, 0, 1)
DBPATCH_ADD(4050011, 0, 1)
DBPATCH_ADD(4050012, 0, 1)
DBPATCH_RSM(4050012, 1, 0, 1)	/* RSM FY20 */
DBPATCH_RSM(4050012, 2, 0, 1)	/* set delay as macro for rsm.dns.*, rsm.rdds*, rsm.rdap* and rsm.epp* items items */
DBPATCH_RSM(4050012, 3, 0, 0)	/* set global macro descriptions */
DBPATCH_RSM(4050012, 4, 0, 0)	/* set host macro descriptions */
DBPATCH_RSM(4050012, 5, 0, 0)	/* add "DNS test mode" and "Transport protocol" value mappings */
DBPATCH_RSM(4050012, 6, 0, 0)	/* add "Template DNS Test" template */
DBPATCH_RSM(4050012, 7, 0, 0)	/* convert hosts to use "Template DNS Test" template */
DBPATCH_RSM(4050012, 8, 0, 0)	/* disable "db watchdog" internal items */
DBPATCH_RSM(4050012, 9, 0, 0)	/* upgrade "TLDs" action (upgrade process to Zabbix 4.x failed to upgrade it) */
DBPATCH_RSM(4050012, 10, 0, 0)	/* add "Template Config History" template */
DBPATCH_RSM(4050012, 11, 0, 0)	/* add "Template RDDS Test" template */
DBPATCH_RSM(4050012, 12, 0, 0)	/* add "Template Probe Status" template */
DBPATCH_RSM(4050012, 13, 0, 0)	/* rename "Template RDAP" to "Template RDAP Test" */
DBPATCH_RSM(4050012, 14, 0, 0)	/* set RDAP master item value_type to the text type */
DBPATCH_RSM(4050012, 15, 0, 0)	/* set RDAP calculated items to be dependent items */
DBPATCH_RSM(4050012, 16, 0, 0)	/* add item_preproc to RDAP ip and rtt items */
DBPATCH_RSM(4050012, 17, 0, 0)	/* add "Template DNSSEC Status" template */
DBPATCH_RSM(4050012, 18, 0, 0)	/* add "Template RDAP Status" template */
DBPATCH_RSM(4050012, 19, 0, 0)	/* add "Template RDDS Status" template */
DBPATCH_RSM(4050012, 20, 0, 0)	/* convert "<rsmhost> <probe>" hosts to use "Template RDDS Test" template */
DBPATCH_RSM(4050012, 21, 0, 0)	/* convert "<probe>" hosts to use "Template Probe Status" template */
DBPATCH_RSM(4050012, 22, 0, 0)	/* add "Template DNS Status" template */

DBPATCH_END()
