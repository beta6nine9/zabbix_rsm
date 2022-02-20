/*
** Zabbix
** Copyright (C) 2001-2022 Zabbix SIA
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
#include "dbupgrade.h"
#include "db.h"
#include "log.h"

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
 * 6.0 maintenance database patches
 */

#ifndef HAVE_SQLITE3

static int	DBpatch_6000000(void)
{
	return SUCCEED;
}

/* 6000000, 1 - create {$RSM.PROXY.IP}, {$RSM.PROXY.PORT} macros */
static int	DBpatch_6000000_1(void)
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

/* 6000000, 2 - create provisioning_api_log table */
static int	DBpatch_6000000_2(void)
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

/* 6000000, 3 - remove {$RSM.EPP.ENABLED} and {$RSM.TLD.EPP.ENABLED} macros from rsm.dns[] and rsm.rdds[] items */
static int	DBpatch_6000000_3(void)
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

/* 6000000, 4 - rename {$RSM.TLD.RDDS.43.SERVERS} to {$RSM.TLD.RDDS43.SERVER} */
static int	DBpatch_6000000_4(void)
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

/* 6000000, 5 - rename {$RSM.TLD.RDDS.80.SERVERS} to {$RSM.TLD.RDDS80.URL} */
static int	DBpatch_6000000_5(void)
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

/* 6000000, 6 - update rsm.rdds[] items to use {$RSM.TLD.RDDS43.SERVER} and {$RSM.TLD.RDDS80.URL} */
static int	DBpatch_6000000_6(void)
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

/* 6000000, 7 - split {$RSM.TLD.RDDS.ENABLED} macro into {$RSM.TLD.RDDS43.ENABLED} and {$RSM.TLD.RDDS80.ENABLED} */
static int	DBpatch_6000000_7(void)
{
	int		ret = FAIL;

	DB_RESULT	result = NULL;
	DB_ROW		row;

	ONLY_SERVER();

	result = DBselect("select hostmacroid,hostid,value,type from hostmacro where macro='{$RSM.TLD.RDDS.ENABLED}'");

	if (NULL == result)
		goto out;

#define SQL_UPDATE	"update hostmacro set macro='%s',description='%s' where hostmacroid=%s"
#define SQL_INSERT	"insert into hostmacro set hostmacroid=" ZBX_FS_UI64 ",hostid=%s,macro='%s',value='%s',description='%s',type='%s'"

#define RDDS43_MACRO	"{$RSM.TLD.RDDS43.ENABLED}"
#define RDDS80_MACRO	"{$RSM.TLD.RDDS80.ENABLED}"

#define RDDS43_DESCR	"Indicates whether RDDS43 is enabled on the rsmhost"
#define RDDS80_DESCR	"Indicates whether RDDS80 is enabled on the rsmhost"

	while (NULL != (row = DBfetch(result)))
	{
		const char	*hostmacroid = row[0];
		const char	*hostid      = row[1];
		const char	*value       = row[2];
		const char	*type        = row[3];

		DB_EXEC(SQL_UPDATE, RDDS43_MACRO, RDDS43_DESCR, hostmacroid);
		DB_EXEC(SQL_INSERT, DBget_maxid_num("hostmacro", 1), hostid, RDDS80_MACRO, value, RDDS80_DESCR, type);
	}

#undef RDDS43_MACRO
#undef RDDS80_MACRO

#undef RDDS43_DESCR
#undef RDDS80_DESCR

#undef SQL_UPDATE
#undef SQL_INSERT

	ret = SUCCEED;
out:
	DBfree_result(result);

	return ret;
}

/* 6000000, 8 - replace {$RSM.TLD.RDDS.ENABLED} macro with {$RSM.TLD.RDDS43.ENABLED} and {$RSM.TLD.RDDS80.ENABLED} in rsm.dns[] and rsm.rdds[] item keys */
static int	DBpatch_6000000_8(void)
{
	int	ret = FAIL;

	ONLY_SERVER();

#define FROM	",{$RSM.TLD.RDDS.ENABLED},"
#define TO	",{$RSM.TLD.RDDS43.ENABLED},{$RSM.TLD.RDDS80.ENABLED},"

	DB_EXEC("update items set key_=replace(key_,'%s','%s') where key_ like 'rsm.dns[%%]'", FROM, TO);
	DB_EXEC("update items set key_=replace(key_,'%s','%s') where key_ like 'rsm.rdds[%%]'", FROM, TO);

#undef FROM
#undef TO

	ret = SUCCEED;
out:
	return ret;
}

/* 6000000, 9 - split rdds.enabled item into rdds43.enabled and rdds80.enabled */
static int	DBpatch_6000000_9(void)
{
	int		ret = FAIL;

	DB_RESULT	result = NULL;
	DB_ROW		row;

	zbx_uint64_t	rdds43_templateid;
	zbx_uint64_t	rdds80_templateid;

	ONLY_SERVER();

	/* update rdds43.enabled template item */

	SELECT_VALUE_UINT64(rdds43_templateid, "select itemid from items where templateid is null and key_='%s'", "rdds.enabled");

#define SQL	"update"												\
			" items"											\
		" set"													\
			" name='RDDS43 enabled/disabled',"								\
			"key_='rdds43.enabled',"									\
			"params='{$RSM.TLD.RDDS43.ENABLED}',"								\
			"description='History of RDDS43 being enabled or disabled.'"					\
		" where"												\
			" itemid=" ZBX_FS_UI64

	DB_EXEC(SQL, rdds43_templateid);

#undef SQL

	/* create rdds80.enabled template item */

	rdds80_templateid = DBget_maxid_num("items", 1);

#define SQL	"insert into items ("											\
			"itemid,type,snmp_oid,hostid,name,key_,delay,history,trends,status,value_type,"			\
			"trapper_hosts,units,formula,logtimefmt,templateid,valuemapid,params,ipmi_sensor,authtype,"	\
			"username,password,publickey,privatekey,flags,interfaceid,description,inventory_link,lifetime,"	\
			"evaltype,jmx_endpoint,master_itemid,timeout,url,query_fields,posts,status_codes,"		\
			"follow_redirects,post_type,http_proxy,headers,retrieve_mode,request_method,output_format,"	\
			"ssl_cert_file,ssl_key_file,ssl_key_password,verify_peer,verify_host,allow_traps,discover"	\
		")"													\
		" select"												\
			" " ZBX_FS_UI64 ",type,snmp_oid,hostid,'%s','%s',delay,history,trends,status,value_type,"	\
			"trapper_hosts,units,formula,logtimefmt,templateid,valuemapid,'%s',ipmi_sensor,authtype,"	\
			"username,password,publickey,privatekey,flags,interfaceid,'%s',inventory_link,lifetime,"	\
			"evaltype,jmx_endpoint,master_itemid,timeout,url,query_fields,posts,status_codes,"		\
			"follow_redirects,post_type,http_proxy,headers,retrieve_mode,request_method,output_format,"	\
			"ssl_cert_file,ssl_key_file,ssl_key_password,verify_peer,verify_host,allow_traps,discover"	\
		" from items where itemid=" ZBX_FS_UI64

	DB_EXEC(SQL, rdds80_templateid, "DDS80 enabled/disabled", "rdds80.enabled", "{$RSM.TLD.RDDS80.ENABLED}",
		"History of RDDS80 being enabled or disabled.", rdds43_templateid);

#undef SQL

	/* update rdds43.enabled items */

#define SQL	"update"												\
			" items"											\
		" set"													\
			" name='RDDS43 enabled/disabled',"								\
			" key_='rdds43.enabled',"									\
			" params='{$RSM.TLD.RDDS43.ENABLED}',"								\
			" description='History of RDDS43 being enabled or disabled.'"					\
		" where"												\
			" templateid=" ZBX_FS_UI64

	DB_EXEC(SQL, rdds43_templateid);

#undef SQL

	/* create rdds80.enabled items */

	result = DBselect("select itemid from items where templateid=" ZBX_FS_UI64, rdds43_templateid);

	if (NULL == result)
		goto out;

#define SQL_ITEMS													\
		"insert into items ("											\
			"itemid,type,snmp_oid,hostid,name,key_,delay,history,trends,status,value_type,"			\
			"trapper_hosts,units,formula,logtimefmt,templateid,valuemapid,params,ipmi_sensor,authtype,"	\
			"username,password,publickey,privatekey,flags,interfaceid,description,inventory_link,lifetime,"	\
			"evaltype,jmx_endpoint,master_itemid,timeout,url,query_fields,posts,status_codes,"		\
			"follow_redirects,post_type,http_proxy,headers,retrieve_mode,request_method,output_format,"	\
			"ssl_cert_file,ssl_key_file,ssl_key_password,verify_peer,verify_host,allow_traps,discover"	\
		")"													\
		" select"												\
			" " ZBX_FS_UI64 ",type,snmp_oid,hostid,'%s','%s',delay,history,trends,status,value_type,"	\
			"trapper_hosts,units,formula,logtimefmt," ZBX_FS_UI64 ",valuemapid,'%s',ipmi_sensor,authtype,"	\
			"username,password,publickey,privatekey,flags,interfaceid,'%s',inventory_link,lifetime,"	\
			"evaltype,jmx_endpoint,master_itemid,timeout,url,query_fields,posts,status_codes,"		\
			"follow_redirects,post_type,http_proxy,headers,retrieve_mode,request_method,output_format,"	\
			"ssl_cert_file,ssl_key_file,ssl_key_password,verify_peer,verify_host,allow_traps,discover"	\
		" from items where itemid=" ZBX_FS_UI64

#define SQL_RTDATA													\
		"insert into item_rtdata (itemid,lastlogsize,state,mtime,error)"					\
		"select " ZBX_FS_UI64 ",lastlogsize,state,mtime,error from item_rtdata where itemid=" ZBX_FS_UI64

#define SQL_HISTORY													\
		"insert into history_uint (itemid,clock,value,ns)"							\
		"select " ZBX_FS_UI64 ",clock,value,ns from history_uint where itemid=" ZBX_FS_UI64

	while (NULL != (row = DBfetch(result)))
	{
		zbx_uint64_t	rdds43_itemid;
		zbx_uint64_t	rdds80_itemid;

		ZBX_STR2UINT64(rdds43_itemid, row[0]);
		rdds80_itemid = DBget_maxid_num("items", 1);

		/* copy item */
		DB_EXEC(SQL_ITEMS,
			rdds80_itemid,
			"RDDS80 enabled/disabled",
			"rdds80.enabled",
			rdds80_templateid,
			"{$RSM.TLD.RDDS80.ENABLED}",
			"History of RDDS80 being enabled or disabled.",
			rdds43_itemid);

		/* copy rtdata */
		DB_EXEC(SQL_RTDATA, rdds80_itemid, rdds43_itemid);

		/* copy history */
		DB_EXEC(SQL_HISTORY, rdds80_itemid, rdds43_itemid);
	}

#undef SQL_ITEMS
#undef SQL_RTDATA
#undef SQL_HISTORY

	ret = SUCCEED;
out:
	DBfree_result(result);

	return ret;
}

/* 6000000, 10 - replace obsoleted positional macros $1 and $2 in item names */
static int	DBpatch_6000000_10(void)
{
	int		ret = FAIL;

	DB_RESULT	result = NULL;
	DB_ROW		row;

	ONLY_SERVER();

#define SQL	"update items"								\
		" set name=replace("							\
			"name"								\
			", '%s'"							\
			", substring_index("						\
				"substring_index("					\
					"regexp_substr(key_, '(?<=\\\\[).*(?=\\\\])')"	\
				", ',', %d)"						\
			", ',', -1)"							\
		")"									\
		" where key_ like 'rsm.configvalue[%%'"					\
			" or key_ like 'probe.configvalue[%%'"				\
			" or key_ like 'resolver.status[%%'"				\
			" or key_ like 'rsm.probe.status[%%'"				\
			" or key_ like 'rsm.slv.dns.ns.avail[%%'"			\
			" or key_ like 'rsm.slv.dns.ns.downtime[%%'"

	/* replace positional macros $1 and $2 */
	DB_EXEC(SQL, "$1", 1);
	DB_EXEC(SQL, "$2", 2);

#undef SQL

	/* make sure we handled everything */
	result = DBselect("select count(*) from items where name like '%%$1%%' or name like '%%$2%%'");

	if (NULL == result)
		goto out;

	if (NULL == (row = DBfetch(result)))
		goto out;

	if (0 != atoi(row[0]))
	{
		zabbix_log(LOG_LEVEL_CRIT, "%s() on line %d: positional macros left after trying to replace them",
				__func__, __LINE__);
		goto out;
	}

	ret = SUCCEED;
out:
	DBfree_result(result);

	return ret;
}

#endif

DBPATCH_START(6000)

/* version, duplicates flag, mandatory flag */

DBPATCH_ADD(6000000, 0, 1)
DBPATCH_RSM(6000000, 1, 0, 0)	/* create {$RSM.PROXY.IP}, {$RSM.PROXY.PORT} macros */
DBPATCH_RSM(6000000, 2, 0, 1)	/* create provisioning_api_log table */
DBPATCH_RSM(6000000, 3, 0, 0)	/* remove {$RSM.EPP.ENABLED} and {$RSM.TLD.EPP.ENABLED} macros from rsm.dns[] and rsm.rdds[] items */
DBPATCH_RSM(6000000, 4, 0, 0)	/* rename {$RSM.TLD.RDDS.43.SERVERS} to {$RSM.TLD.RDDS43.SERVER} */
DBPATCH_RSM(6000000, 5, 0, 0)	/* rename {$RSM.TLD.RDDS.80.SERVERS} to {$RSM.TLD.RDDS80.URL} */
DBPATCH_RSM(6000000, 6, 0, 0)	/* update rsm.rdds[] items to use {$RSM.TLD.RDDS43.SERVER} and {$RSM.TLD.RDDS80.URL} */
DBPATCH_RSM(6000000, 7, 0, 0)	/* split {$RSM.TLD.RDDS.ENABLED} macro into {$RSM.TLD.RDDS43.ENABLED} and {$RSM.TLD.RDDS80.ENABLED} */
DBPATCH_RSM(6000000, 8, 0, 0)	/* replace {$RSM.TLD.RDDS.ENABLED} macro with {$RSM.TLD.RDDS43.ENABLED} and {$RSM.TLD.RDDS80.ENABLED} in rsm.dns[] and rsm.rdds[] item keys */
DBPATCH_RSM(6000000, 9, 0, 0)	/* split rdds.enabled item into rdds43.enabled and rdds80.enabled */
DBPATCH_RSM(6000000, 10, 0, 0)	/* replace obsoleted positional macros $1 and $2 in item names */

DBPATCH_END()
