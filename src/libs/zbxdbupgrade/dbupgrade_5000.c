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
#include "dbupgrade_macros.h"

/*
 * Some common helpers that can be used as one-liners in patches to avoid copy-pasting.
 *
 * Be careful when implementing new helpers - they have to be generic.
 * If some code is needed only 1-2 times, it doesn't fit here.
 * If some code depends on stuff that is likely to change, it doesn't fit here.
 *
 * If more specific helper is needed, it must be implemented close to the patch that needs it. Specific
 * helpers can be implemented either as functions right before DBpatch_5000xxx(), or as macros inside
 * the DBpatch_5000xxx(). If they're implemented as macros, don't forget to #undef them.
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

extern unsigned char	program_type;

/*
 * 5.0 maintenance database patches
 */

#ifndef HAVE_SQLITE3

static int	DBpatch_5000000(void)
{
	return SUCCEED;
}

static int	DBpatch_5000001(void)
{
	if (0 == (program_type & ZBX_PROGRAM_TYPE_SERVER))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute("delete from profiles where idx in ('web.latest.toggle','web.latest.toggle_other')"))
		return FAIL;

	return SUCCEED;
}

static int	DBpatch_5000002(void)
{
	DB_ROW		row;
	DB_RESULT	result;
	int		ret = SUCCEED;

	if (0 == (program_type & ZBX_PROGRAM_TYPE_SERVER))
		return SUCCEED;

	result = DBselect("select userid from profiles where idx='web.latest.sort' and value_str='lastclock'");

	while (NULL != (row = DBfetch(result)))
	{
		if (ZBX_DB_OK > DBexecute(
			"delete from profiles"
			" where userid='%s'"
				" and idx in ('web.latest.sort','web.latest.sortorder')", row[0]))
		{
			ret = FAIL;
			break;
		}
	}
	DBfree_result(result);

	return ret;
}

/* 5000002, 1 - RSM FY21 */
static int	DBpatch_5000002_1(void)
{
	/* this patch begins RSM FY21 upgrade sequence and has been intentionally left blank */

	return SUCCEED;
}

/* 5000002, 2 - move {$RSM.DNS.AVAIL.MINNS} from globalmacro to hostmacro, rename to {$RSM.TLD.DNS.AVAIL.MINNS} */
static int	DBpatch_5000002_2(void)
{
	int		ret = FAIL;

	DB_RESULT	result_gm = NULL;
	DB_RESULT	result_h = NULL;
	DB_ROW		row;

	zbx_uint64_t	globalmacroid;
	const char	*value;
	const char	*description;
	int		type;

	ONLY_SERVER();

	 /* get global macro */

	result_gm = DBselect("select globalmacroid,value,description,type from globalmacro where macro='{$RSM.DNS.AVAIL.MINNS}'");

	if (NULL == result_gm)
		goto out;

	row = DBfetch(result_gm);

	if (NULL == row)
		goto out;

	ZBX_STR2UINT64(globalmacroid, row[0]);
	value = row[1];
	description = row[2];
	type = atoi(row[3]);

	/* create host macros */

	result_h = DBselect("select hostid from hosts where host like 'Template Rsmhost Config %%'");

	if (NULL == result_h)
		goto out;

	while (NULL != (row = DBfetch(result_h)))
	{
		zbx_uint64_t	hostid;

		ZBX_STR2UINT64(hostid, row[0]);

#define SQL	"insert into hostmacro set hostmacroid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ",macro='%s',value='%s',description='%s',type=%d"
		DB_EXEC(SQL, DBget_maxid_num("hostmacro", 1), hostid, "{$RSM.TLD.DNS.AVAIL.MINNS}", value, description, type);
#undef SQL
	}

	/* delete global macro */

	DB_EXEC("delete from globalmacro where globalmacroid=" ZBX_FS_UI64, globalmacroid);

	ret = SUCCEED;
out:
	DBfree_result(result_gm);
	DBfree_result(result_h);

	return ret;
}

/* 5000002, 3 - delete "rsm.configvalue[RSM.DNS.AVAIL.MINNS]" item */
static int	DBpatch_5000002_3(void)
{
	int	ret = FAIL;

	ONLY_SERVER();

	DB_EXEC("delete"
			" items"
		" from"
			" items"
			" inner join hosts on hosts.hostid = items.hostid"
		" where"
			" hosts.host='Global macro history' and"
			" items.key_='rsm.configvalue[RSM.DNS.AVAIL.MINNS]'");

	ret = SUCCEED;
out:
	return ret;
}

/* 5000002, 4 - replace "{$RSM.DNS.AVAIL.MINNS}" to "{$RSM.TLD.DNS.AVAIL.MINNS}" in item keys (template and hosts) */
static int	DBpatch_5000002_4(void)
{
	int	ret = FAIL;

	ONLY_SERVER();

	DB_EXEC("update items"
		" set key_=replace(key_,'{$RSM.DNS.AVAIL.MINNS}','{$RSM.TLD.DNS.AVAIL.MINNS}')"
		" where key_ like 'rsm.dns[%%]'");

	ret = SUCCEED;
out:
	return ret;
}

static int	DBpatch_5000003(void)
{
	DB_RESULT	result;
	int		ret;
	const char	*fields[] = {"subject", "message"};

	result = DBselect("select om.operationid,om.subject,om.message"
			" from opmessage om,operations o,actions a"
			" where om.operationid=o.operationid"
				" and o.actionid=a.actionid"
				" and a.eventsource=0 and o.operationtype=11");

	ret = db_rename_macro(result, "opmessage", "operationid", fields, ARRSIZE(fields), "{EVENT.NAME}",
			"{EVENT.RECOVERY.NAME}");

	DBfree_result(result);

	return ret;
}

static int	DBpatch_5000004(void)
{
	DB_RESULT	result;
	int		ret;
	const char	*fields[] = {"subject", "message"};

	result = DBselect("select mediatype_messageid,subject,message from media_type_message where recovery=1");

	ret = db_rename_macro(result, "media_type_message", "mediatype_messageid", fields, ARRSIZE(fields),
			"{EVENT.NAME}", "{EVENT.RECOVERY.NAME}");

	DBfree_result(result);

	return ret;
}

/* 5000004, 1 - create {$RSM.PROXY.IP}, {$RSM.PROXY.PORT} macros */
static int	DBpatch_5000004_1(void)
{
	int		ret = FAIL;

	DB_RESULT	result = NULL;
	DB_ROW		row;

	ONLY_SERVER();

	result = DBselect("select"
				" hosts.host,"
				"hosts.status,"
				"interface.ip,"
				"interface.port"
			" from"
				" hosts"
				" left join interface on interface.hostid=hosts.hostid"
			" where"
				" hosts.status in (5,6)");

	if (NULL == result)
		goto out;

	while (NULL != (row = DBfetch(result)))
	{
		const char	*host;
		int		status;
		const char	*ip;
		const char	*port;
		zbx_uint64_t	templateid;

		host   = row[0];
		status = atoi(row[1]);
		ip     = row[2];
		port   = row[3];

		if (status != 6)
		{
			zabbix_log(LOG_LEVEL_CRIT, "%s() on line %d: proxy '%s' must be passive (enabled)", __func__, __LINE__, host);		\
			goto out;
		}

		SELECT_VALUE_UINT64(templateid, "select hostid from hosts where host='Template Probe Config %s'", host);

#define SQL	"insert into hostmacro set hostmacroid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ",macro='%s',value='%s',description='%s',type=0"
		DB_EXEC(SQL, DBget_maxid_num("hostmacro", 1), templateid, "{$RSM.PROXY.IP}", ip, "Proxy IP of the proxy");
		DB_EXEC(SQL, DBget_maxid_num("hostmacro", 1), templateid, "{$RSM.PROXY.PORT}", port, "Port of the proxy");
#undef SQL
	}

	ret = SUCCEED;
out:
	DBfree_result(result);

	return ret;
}

/* 5000004, 2 - create provisioning_api_log table */
static int	DBpatch_5000004_2(void)
{
	int	ret = FAIL;

	DB_EXEC("create table provisioning_api_log ("
			"provisioning_api_logid bigint(20) unsigned not null,"
			"clock int(11) not null,"
			"user varchar(100) not null,"
			"interface varchar(8) not null,"
			"identifier varchar(255) not null,"
			"operation varchar(6) not null,"
			"object_type varchar(9) not null,"
			"object_before text default null,"
			"object_after text default null,"
			"remote_addr varchar(45) not null,"
			"x_forwarded_for varchar(255) default null,"
			"primary key (provisioning_api_logid)"
		")");

	ret = SUCCEED;
out:
	return ret;
}

/* 5000004, 3 - remove {$RSM.EPP.ENABLED} and {$RSM.TLD.EPP.ENABLED} macros from rsm.dns[] and rsm.rdds[] items */
static int	DBpatch_5000004_3(void)
{
	int	ret = FAIL;

	ONLY_SERVER();

#define SQL	"update items set key_=replace(key_,',%s','') where key_ like '%s'"
	DB_EXEC(SQL, "{$RSM.TLD.EPP.ENABLED}", "rsm.dns[%]");
	DB_EXEC(SQL, "{$RSM.TLD.EPP.ENABLED}", "rsm.rdds[%]");
	DB_EXEC(SQL, "{$RSM.EPP.ENABLED}"    , "rsm.rdds[%]");
#undef SQL

	ret = SUCCEED;
out:
	return ret;
}

/* 5000004, 4 - rename {$RSM.TLD.RDDS.43.SERVERS} to {$RSM.TLD.RDDS43.SERVER} */
static int	DBpatch_5000004_4(void)
{
	int	ret = FAIL;

	DB_RESULT	result = NULL;
	DB_ROW		row;

	ONLY_SERVER();

	result = DBselect("select hostmacroid,value from hostmacro where macro='{$RSM.TLD.RDDS.43.SERVERS}'");

	if (NULL == result)
		goto out;

	while (NULL != (row = DBfetch(result)))
	{
		const char	*macroid = row[0];
		const char	*value   = row[1];

		if (NULL != strchr(value, ','))
		{
			zabbix_log(LOG_LEVEL_CRIT, "%s() on line %d: macro contains more than one server: '%s' (id: %s)",
					__func__, __LINE__, value, macroid);
			goto out;
		}

		DB_EXEC("update hostmacro set macro='{$RSM.TLD.RDDS43.SERVER}' where hostmacroid=%s", macroid);
	}

	ret = SUCCEED;
out:
	DBfree_result(result);

	return ret;
}

/* 5000004, 5 - rename {$RSM.TLD.RDDS.80.SERVERS} to {$RSM.TLD.RDDS80.URL} */
static int	DBpatch_5000004_5(void)
{
	int	ret = FAIL;

	DB_RESULT	result = NULL;
	DB_ROW		row;

	ONLY_SERVER();

	result = DBselect("select hostmacroid,value from hostmacro where macro='{$RSM.TLD.RDDS.80.SERVERS}'");

	if (NULL == result)
		goto out;

	while (NULL != (row = DBfetch(result)))
	{
		const char	*macroid = row[0];
		const char	*value   = row[1];

		if (NULL != strchr(value, ','))
		{
			zabbix_log(LOG_LEVEL_CRIT, "%s() on line %d: macro contains more than one server: '%s' (id: %s)",
					__func__, __LINE__, value, macroid);
			goto out;
		}

		DB_EXEC("update hostmacro set macro='{$RSM.TLD.RDDS80.URL}',value='http://%s/' where hostmacroid=%s",
				value, macroid);
	}

	ret = SUCCEED;
out:
	DBfree_result(result);

	return ret;
}

/* 5000004, 6 - update rsm.rdds[] items to use {$RSM.TLD.RDDS43.SERVER} and {$RSM.TLD.RDDS80.URL} */
static int	DBpatch_5000004_6(void)
{
	int	ret = FAIL;

	ONLY_SERVER();

#define SQL	"update items set key_=replace(key_,'%s','%s') where key_ like 'rsm.rdds[%%]'"
	DB_EXEC(SQL, "{$RSM.TLD.RDDS.43.SERVERS}", "{$RSM.TLD.RDDS43.SERVER}");
	DB_EXEC(SQL, "{$RSM.TLD.RDDS.80.SERVERS}", "{$RSM.TLD.RDDS80.URL}");
#undef SQL

	ret = SUCCEED;
out:
	return ret;
}

#endif

DBPATCH_START(5000)

/* version, duplicates flag, mandatory flag */

DBPATCH_ADD(5000000, 0, 1)
DBPATCH_ADD(5000001, 0, 0)
DBPATCH_ADD(5000002, 0, 0)
DBPATCH_RSM(5000002, 1, 0, 1)	/* RSM FY21 */
DBPATCH_RSM(5000002, 2, 0, 0)	/* move {$RSM.DNS.AVAIL.MINNS} from globalmacro to hostmacro, rename to {$RSM.TLD.DNS.AVAIL.MINNS} */
DBPATCH_RSM(5000002, 3, 0, 0)	/* delete "rsm.configvalue[RSM.DNS.AVAIL.MINNS]" item */
DBPATCH_RSM(5000002, 4, 0, 0)	/* replace "{$RSM.DNS.AVAIL.MINNS}" to "{$RSM.TLD.DNS.AVAIL.MINNS}" in item keys (template and hosts) */
DBPATCH_ADD(5000003, 0, 0)
DBPATCH_ADD(5000004, 0, 0)
DBPATCH_RSM(5000004, 1, 0, 0)	/* create {$RSM.PROXY.IP}, {$RSM.PROXY.PORT} macros */
DBPATCH_RSM(5000004, 2, 0, 1)	/* create provisioning_api_log table */
DBPATCH_RSM(5000004, 3, 0, 0)	/* remove {$RSM.EPP.ENABLED} and {$RSM.TLD.EPP.ENABLED} macros from rsm.dns[] and rsm.rdds[] items */
DBPATCH_RSM(5000004, 4, 0, 0)	/* rename {$RSM.TLD.RDDS.43.SERVERS} to {$RSM.TLD.RDDS43.SERVER} */
DBPATCH_RSM(5000004, 5, 0, 0)	/* rename {$RSM.TLD.RDDS.80.SERVERS} to {$RSM.TLD.RDDS80.URL} */
DBPATCH_RSM(5000004, 6, 0, 0)	/* update rsm.rdds[] items to use {$RSM.TLD.RDDS43.SERVER} and {$RSM.TLD.RDDS80.URL} */

DBPATCH_END()
