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
#include "db.h"
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

/* gets groupid of the host group */
#define GET_HOST_GROUP_ID(groupid, name)										\
		SELECT_VALUE_UINT64(groupid, "select groupid from hstgrp where name='%s'", name)

#define zbx_db_dyn_escape_string(src)	zbx_db_dyn_escape_string(src, ZBX_SIZE_T_MAX, ZBX_SIZE_T_MAX, ESCAPE_SEQUENCE_ON)

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
	const ZBX_TABLE	table =
			{"provisioning_api_log", "provisioning_api_logid", 0,
				{
					{"provisioning_api_logid", NULL, NULL, NULL, 0  , ZBX_TYPE_ID  , ZBX_NOTNULL, 0},
					{"clock"                 , NULL, NULL, NULL, 0  , ZBX_TYPE_INT , ZBX_NOTNULL, 0},
					{"user"                  , NULL, NULL, NULL, 100, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
					{"interface"             , NULL, NULL, NULL, 8  , ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
					{"identifier"            , NULL, NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
					{"operation"             , NULL, NULL, NULL, 6  , ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
					{"object_type"           , NULL, NULL, NULL, 9  , ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
					{"object_before"         , NULL, NULL, NULL, 0  , ZBX_TYPE_TEXT, 0          , 0},
					{"object_after"          , NULL, NULL, NULL, 0  , ZBX_TYPE_TEXT, 0          , 0},
					{"remote_addr"           , NULL, NULL, NULL, 45 , ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
					{"x_forwarded_for"       , NULL, NULL, NULL, 255, ZBX_TYPE_CHAR, 0          , 0},
					{0}
				},
				NULL
			};

	return DBcreate_table(&table);
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

	DB_EXEC(SQL, rdds80_templateid, "RDDS80 enabled/disabled", "rdds80.enabled", "{$RSM.TLD.RDDS80.ENABLED}",
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

/* 6000000, 11 - register Provisioning API module and create its users */
static int	DBpatch_6000000_11(void)
{
	int		ret = FAIL;

	zbx_uint64_t	userid_ro, userid_rw, roleid, usrgrpid, id_ro, id_rw, moduleid;

	userid_ro = DBget_maxid_num("users"       , 2);
	userid_rw = userid_ro + 1;
	usrgrpid  = DBget_maxid_num("usrgrp"      , 1);
	id_ro     = DBget_maxid_num("users_groups", 2);
	id_rw     = id_ro + 1;
	moduleid  = DBget_maxid_num("module"      , 1);

	ONLY_SERVER();

	SELECT_VALUE_UINT64(roleid, "select roleid from role where name='%s'", "Super admin role");

#define SQL	"insert into users set userid=" ZBX_FS_UI64 ",username='%s',passwd='%s',autologout=0,roleid=" ZBX_FS_UI64
	DB_EXEC(SQL, userid_ro, "provisioning-api-readonly",  "5f4dcc3b5aa765d61d8327deb882cf99", roleid);
	DB_EXEC(SQL, userid_rw, "provisioning-api-readwrite", "5f4dcc3b5aa765d61d8327deb882cf99", roleid);
#undef SQL

#define SQL	"insert into usrgrp set usrgrpid=" ZBX_FS_UI64 ",name='%s',gui_access=3,users_status=0,debug_mode=0"
	DB_EXEC(SQL, usrgrpid, "Provisioning API");
#undef SQL

#define SQL	"insert into users_groups set id=" ZBX_FS_UI64 ",usrgrpid=" ZBX_FS_UI64 ",userid=" ZBX_FS_UI64
	DB_EXEC(SQL, id_ro, usrgrpid, userid_ro);
	DB_EXEC(SQL, id_rw, usrgrpid, userid_rw);
#undef SQL

#define SQL "insert into module set moduleid=" ZBX_FS_UI64 ",id='%s',relative_path='%s',status=1,config='[]'"
	DB_EXEC(SQL, moduleid, "RSM Provisioning API", "RsmProvisioningApi");
#undef SQL

	ret = SUCCEED;
out:
	return ret;
}

/* 6000000, 12 - create a template for storing value maps */
static int	DBpatch_6000000_12(void)
{
	int		ret = FAIL;

	DB_RESULT	valuemap_result = NULL;
	DB_ROW		valuemap_row;
	DB_RESULT	mapping_result = NULL;
	DB_ROW		mapping_row;

	zbx_uint64_t	groupid;
	zbx_uint64_t	hostid;
	const char	*template_name = "Template Value Maps";
	char		*template_uuid = NULL;
	char		*valuemap_name = NULL;
	char		*valuemap_uuid = NULL;
	char		*old_value = NULL;
	char		*new_value = NULL;

	ONLY_SERVER();

	GET_HOST_GROUP_ID(groupid, "Templates");
	hostid = DBget_maxid_num("hosts", 1);
	template_uuid = zbx_gen_uuid4(template_name);

	/* status 3 = HOST_STATUS_TEMPLATE */

	DB_EXEC("insert into hosts set"
			" hostid=" ZBX_FS_UI64 ",created=0,proxy_hostid=NULL,host='%s',status=%d,lastaccess=0,"
			"ipmi_authtype=-1,ipmi_privilege=2,ipmi_username='',ipmi_password='',maintenanceid=NULL,"
			"maintenance_status=0,maintenance_type=0,maintenance_from=0,name='%s',info_1='',info_2='',"
			"flags=0,templateid=NULL,description='',tls_connect=1,tls_accept=1,tls_issuer='',"
			"tls_subject='',tls_psk_identity='',tls_psk='',proxy_address='',auto_compress=1,discover=0,"
			"custom_interfaces=0,uuid='%s'",
		hostid, template_name, HOST_STATUS_TEMPLATE, template_name, template_uuid);

	DB_EXEC("insert into hosts_groups set"
			" hostgroupid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ",groupid=" ZBX_FS_UI64,
		DBget_maxid_num("hosts_groups", 1), hostid, groupid);

	valuemap_result = DBselect("select valuemapid,name from valuemaps_tmp order by valuemapid");

	if (NULL == valuemap_result)
		goto out;

	while (NULL != (valuemap_row = DBfetch(valuemap_result)))
	{
		zbx_uint64_t	valuemapid_old;
		zbx_uint64_t	valuemapid_new;
		unsigned int	sortorder = 0;

		ZBX_STR2UINT64(valuemapid_old, valuemap_row[0]);
		valuemap_name = DBdyn_escape_string(valuemap_row[1]);

		valuemapid_new = DBget_maxid_num("valuemap", 1);
		valuemap_uuid = zbx_gen_uuid4(valuemap_name);

#define SQL	"insert into valuemap set valuemapid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ",name='%s',uuid='%s'"
		DB_EXEC(SQL, valuemapid_new, hostid, valuemap_name, valuemap_uuid);
#undef SQL

		mapping_result = DBselect("select value,newvalue from mappings_tmp where valuemapid=" ZBX_FS_UI64 " order by mappingid", valuemapid_old);

		if (NULL == mapping_result)
			goto out;

		while (NULL != (mapping_row = DBfetch(mapping_result)))
		{
			old_value = DBdyn_escape_string(mapping_row[0]);
			new_value = DBdyn_escape_string(mapping_row[1]);

			/* type 0 = VALUEMAP_MAPPING_TYPE_EQUAL */

			DB_EXEC("insert into valuemap_mapping set"
					" valuemap_mappingid=" ZBX_FS_UI64 ",valuemapid=" ZBX_FS_UI64 ",value='%s',"
					"newvalue='%s',type=0,sortorder=%d",
				DBget_maxid_num("valuemap_mapping", 1), valuemapid_new, old_value, new_value, sortorder++);

			zbx_free(old_value);
			zbx_free(new_value);
		}

		zbx_free(valuemap_name);
		zbx_free(valuemap_uuid);
	}

	DB_EXEC("drop table valuemaps_tmp");
	DB_EXEC("drop table mappings_tmp");

	ret = SUCCEED;
out:
	DBfree_result(valuemap_result);
	DBfree_result(mapping_result);
	zbx_free(template_uuid);
	zbx_free(valuemap_name);
	zbx_free(valuemap_uuid);
	zbx_free(old_value);
	zbx_free(new_value);

	return ret;
}

/* 6000000, 13 - enable show_technical_errors */
static int	DBpatch_6000000_13(void)
{
	int	ret = FAIL;

	DB_EXEC("update config set show_technical_errors=1");

	ret = SUCCEED;
out:
	return ret;
}

/* 6000000, 14 - reset items.lifetime and items.request_method to default values */
static int	DBpatch_6000000_14(void)
{
	int	ret = FAIL;

	DB_EXEC("update items set lifetime=default(lifetime) where lifetime<>default(lifetime)");
	DB_EXEC("update items set request_method=default(request_method) where request_method<>default(request_method)");

	ret = SUCCEED;
out:
	return ret;
}

/* 6000000, 15 - add missing macros - {$RDAP.BASE.URL}, {$RDAP.TEST.DOMAIN}, {$RSM.RDDS43.TEST.DOMAIN} */
static int	DBpatch_6000000_15(void)
{
	int		ret = FAIL;

	DB_RESULT	result = NULL;
	DB_ROW		row;
	unsigned int 	i;

	const char	*macros[][2] = {
		{"{$RDAP.BASE.URL}"         , "Base URL for RDAP queries, e.g. http://whois.zabbix"},
		{"{$RDAP.TEST.DOMAIN}"      , "Test domain for RDAP queries, e.g. whois.zabbix"},
		{"{$RSM.RDDS43.TEST.DOMAIN}", "Domain name to use when querying RDDS43 server, e.g. \"whois.example\""},
	};

	ONLY_SERVER();

	for (i = 0; i < sizeof(macros) / sizeof(*macros); i++)
	{
		const char	*macro = macros[i][0];
		const char	*description = macros[i][1];

		result = DBselect("select"
					" hosts.hostid"
				" from"
					" hosts"
					" left join hostmacro on"
						" hostmacro.hostid=hosts.hostid and"
						" hostmacro.macro='%s'"
				" where"
					" hosts.host like 'Template Rsmhost Config %%' and"
					" hostmacro.hostmacroid is null",
				macro);

		if (NULL == result)
			goto out;

		while (NULL != (row = DBfetch(result)))
		{
			zbx_uint64_t	hostid;

			ZBX_STR2UINT64(hostid, row[0]);

#define SQL	"insert into hostmacro set hostmacroid=" ZBX_FS_UI64 ",hostid=" ZBX_FS_UI64 ",macro='%s',value='',description='%s',type=0"
			DB_EXEC(SQL, DBget_maxid_num("hostmacro", 1), hostid, macro, description);
#undef SQL
		}

		DBfree_result(result);
		result = NULL;
	}

	ret = SUCCEED;
out:
	DBfree_result(result);

	return ret;
}

/* 6000000, 16 - create table rsm_false_positive, this is required by frontend */
static int	DBpatch_6000000_16(void)
{
	const ZBX_TABLE	table =
			{"rsm_false_positive", "rsm_false_positiveid", 0,
				{
					{"rsm_false_positiveid", NULL, NULL, NULL, 0  , ZBX_TYPE_ID  , ZBX_NOTNULL, 0},
					{"userid"              , NULL, NULL, NULL, 0  , ZBX_TYPE_ID  , ZBX_NOTNULL, 0},
					{"eventid"             , NULL, NULL, NULL, 0  , ZBX_TYPE_ID  , ZBX_NOTNULL, 0},
					{"clock"               , NULL, NULL, NULL, 0  , ZBX_TYPE_INT , ZBX_NOTNULL, 0},
					{"status"              , NULL, NULL, NULL, 0  , ZBX_TYPE_INT , ZBX_NOTNULL, 0},
					{0}
				},
				NULL
			};

	return DBcreate_table(&table);
}

/* 6000000, 17 - add userid index to table rsm_false_positive */
static int	DBpatch_6000000_17(void)
{
	return DBcreate_index("rsm_false_positive", "rsm_false_positive_1", "userid", 0);
}

/* 6000000, 18 - add userid foreign key to table rsm_false_positive */
static int	DBpatch_6000000_18(void)
{
	const ZBX_FIELD field = {"userid", NULL, "users", "userid", 0, 0, 0, 0};

	return DBadd_foreign_key("rsm_false_positive", 1, &field);
}

/* 6000000, 19 - add eventid index to table rsm_false_positive */
static int	DBpatch_6000000_19(void)
{
	return DBcreate_index("rsm_false_positive", "rsm_false_positive_2", "eventid", 0);
}

/* 6000000, 20 - add eventid foreign key to table rsm_false_positive */
static int	DBpatch_6000000_20(void)
{
	const ZBX_FIELD field = {"eventid", NULL, "events", "eventid", 0, 0, 0, ZBX_FK_CASCADE_DELETE};

	return DBadd_foreign_key("rsm_false_positive", 2, &field);
}

/* 6000000, 21 - drop column events.false_positive */
static int	DBpatch_6000000_21(void)
{
	return DBdrop_field("events", "false_positive");
}

/* 6000000, 22 - add settings for "Read-only user" and "Power user" roles */
static int	DBpatch_6000000_22(void)
{
	/* this is just a direct paste from data.tmpl, with each line quoted and properly indented */
	static const char	*const data[] = {
		"ROW   |10000      |100   |0   |ui.monitoring.hosts   |1        |         |NULL          |NULL           |",
		"ROW   |10001      |100   |0   |ui.default_access     |0        |         |NULL          |NULL           |",
		"ROW   |10002      |100   |0   |services.read         |0        |         |NULL          |NULL           |",
		"ROW   |10003      |100   |0   |services.write        |0        |         |NULL          |NULL           |",
		"ROW   |10004      |100   |0   |modules.default_access|0        |         |NULL          |NULL           |",
		"ROW   |10005      |100   |0   |api.access            |0        |         |NULL          |NULL           |",
		"ROW   |10006      |100   |0   |actions.default_access|0        |         |NULL          |NULL           |",
		"ROW   |10007      |100   |2   |modules.module.0      |0        |         |1             |NULL           |",
		"ROW   |10008      |110   |0   |ui.monitoring.hosts   |1        |         |NULL          |NULL           |",
		"ROW   |10009      |110   |0   |ui.default_access     |0        |         |NULL          |NULL           |",
		"ROW   |10010      |110   |0   |services.read         |0        |         |NULL          |NULL           |",
		"ROW   |10011      |110   |0   |services.write        |0        |         |NULL          |NULL           |",
		"ROW   |10012      |110   |0   |modules.default_access|0        |         |NULL          |NULL           |",
		"ROW   |10013      |110   |0   |api.access            |0        |         |NULL          |NULL           |",
		"ROW   |10014      |110   |0   |actions.default_access|0        |         |NULL          |NULL           |",
		"ROW   |10015      |110   |2   |modules.module.0      |0        |         |1             |NULL           |",
		NULL
	};
	int			i;

	ONLY_SERVER();

	for (i = 0; NULL != data[i]; i++)
	{
		zbx_uint64_t	role_ruleid, roleid;
		char		*name = NULL, *value_str = NULL, *value_moduleid = NULL, *name_esc;
		int		type, value_int;

		if (0 == strncmp(data[i], "--", ZBX_CONST_STRLEN("--")))
			continue;

		if (7 != sscanf(data[i], "ROW |" ZBX_FS_UI64 " |" ZBX_FS_UI64 " |%d |%m[^|]|%d |%m[^|]|%m[^|]|",
				&role_ruleid, &roleid, &type, &name, &value_int, &value_str, &value_moduleid))
		{
			zabbix_log(LOG_LEVEL_CRIT, "failed to parse the following line:\n%s", data[i]);
			zbx_free(name);
			zbx_free(value_str);
			zbx_free(value_moduleid);
			return FAIL;
		}

		/* this one is unused */
		zbx_free(value_str);

		zbx_rtrim(name, ZBX_WHITESPACE);
		zbx_rtrim(value_moduleid, ZBX_WHITESPACE);

		/* NOTE: to keep it simple assume that data does not contain sequences "&pipe;", "&eol;" or "&bsn;" */

		name_esc = zbx_db_dyn_escape_string(name);
		zbx_free(name);

		if (ZBX_DB_OK > DBexecute(
				"insert into role_rule (role_ruleid,roleid,type,name,value_int,value_moduleid)"
				" values (" ZBX_FS_UI64 "," ZBX_FS_UI64 ",%d,'%s',%d,%s)",
				role_ruleid, roleid, type, name_esc, value_int, value_moduleid))
		{
			zbx_free(name_esc);
			zbx_free(value_moduleid);
			return FAIL;
		}

		zbx_free(name_esc);
		zbx_free(value_moduleid);
	}

	if (ZBX_DB_OK > DBexecute("delete from ids where table_name='role_rule'"))
		return FAIL;

	return SUCCEED;
}

/* 6000000, 23 add script "DNSViz webhook" */
static int	DBpatch_6000000_23(void)
{
	zbx_uint64_t	scriptid;
	int		ret = FAIL;

	ONLY_SERVER();

	scriptid = DBget_maxid_num("scripts", 1);

	DB_EXEC(
			"insert into scripts"
			" set scriptid=" ZBX_FS_UI64 ",name='DNSViz webhook',command='%s',host_access=2,description=''"
			",type=5,execute_on=2,timeout='1m',scope=1,authtype=0",
			scriptid,
			"try {\r\n"
			"    var req = new HttpRequest(), response, script_params = JSON.parse(value), uri_params = {}, payload;\r\n"
			"\r\n"
			"    // Set up headers.\r\n"
			"    req.addHeader(''Content-Type: application/x-www-form-urlencoded; charset=UTF-8'');\r\n"
			"    req.addHeader(''Cache-Control: no-cache'');\r\n"
			"    req.addHeader(''Connection: keep-alive'');\r\n"
			"    req.addHeader(''Pragma: no-cache'');\r\n"
			"\r\n"
			"    // These ones are important in order to get HTTP status code 200 instead of 403.\r\n"
			"    req.addHeader(''Referer: '' + script_params.url);\r\n"
			"    req.addHeader(''X-Requested-With: XMLHttpRequest'');\r\n"
			"\r\n"
			"    // Form elements.\r\n"
			"    payload = ''force_ancestor=.&analysis_type=0&perspective=server'';\r\n"
			"\r\n"
			"    Zabbix.log(3, ''[ DNSViz webhook ] POST \"'' + script_params.url + ''\" with \"'' + payload + ''\"'');\r\n"
			"\r\n"
			"    // Perform the POST request.\r\n"
			"    response = req.post(script_params.url, payload);\r\n"
			"\r\n"
			"    Zabbix.log(3, ''[ DNSViz webhook ] Responded with code: '' + req.getStatus() + ''. Response: '' + response);\r\n"
			"\r\n"
			"    if (req.getStatus() !== 200) {\r\n"
			"        throw response.error;\r\n"
			"    }\r\n"
			"\r\n"
			"    return ''OK'';\r\n"
			"}\r\n"
			"catch (error) {\r\n"
			"    Zabbix.log(3, ''[ DNSViz webhook ] Sending failed. Error: '' + error);\r\n"
			"    throw ''Failed with error: '' + error;\r\n"
			"}");

	DB_EXEC(
			"insert into script_param"
			" set script_paramid=" ZBX_FS_UI64 ",scriptid=" ZBX_FS_UI64 ",name='url'"
				",value='https://dnsviz.net/d/{$RSM.DNS.TESTPREFIX}.{$RSM.TLD}/analyze/'",
			DBget_maxid_num("script_param", 1), scriptid);

	DB_EXEC("delete from ids where table_name='scripts'");
	DB_EXEC("delete from ids where table_name='script_param'");

	ret = SUCCEED;
out:
	return ret;
}

/* 6000000, 24 - add action "Create DNSViz report" for DNSSEC accidents */
static int	DBpatch_6000000_24(void)
{
	zbx_uint64_t	actionid, operationid, scriptid;
	int		ret = FAIL;

	ONLY_SERVER();

	actionid    = DBget_maxid_num("actions", 1);
	operationid = DBget_maxid_num("operations", 1);

	SELECT_VALUE_UINT64(scriptid, "select scriptid from scripts where name='%s'", "DNSViz webhook");

	DB_EXEC(
			"insert into actions"
			" set actionid=" ZBX_FS_UI64 ",name='Create DNSViz report',eventsource=0,evaltype=0,status=1"
				",esc_period='1h',pause_suppressed=1,notify_if_canceled=1",
			actionid);

	DB_EXEC(
			"insert into conditions"
			" set conditionid=" ZBX_FS_UI64 ",actionid=" ZBX_FS_UI64 ",conditiontype=3,operator=2"
			",value='DNSSEC service is down'",
			DBget_maxid_num("conditions", 1), actionid);

	DB_EXEC(
			"insert into operations"
			" set operationid=" ZBX_FS_UI64 ",actionid=" ZBX_FS_UI64 ",operationtype=1"
				",esc_period='0',esc_step_from=1,esc_step_to=1,evaltype=0,recovery=0",
			operationid, actionid);

	DB_EXEC(
			"insert into opcommand"
			" set operationid=" ZBX_FS_UI64 ",scriptid=" ZBX_FS_UI64,
			operationid, scriptid);

	DB_EXEC(
			"insert into opcommand_hst"
			" set opcommand_hstid=" ZBX_FS_UI64 ",operationid=" ZBX_FS_UI64,
			DBget_maxid_num("opcommand_hst", 1), operationid);

	DB_EXEC("delete from ids where table_name='actions'");
	DB_EXEC("delete from ids where table_name='conditions'");
	DB_EXEC("delete from ids where table_name='operations'");
	DB_EXEC("delete from ids where table_name='opcommand'");
	DB_EXEC("delete from ids where table_name='opcommand_hst'");

	ret = SUCCEED;
out:
	return ret;
}

/* 6000000, 25 - create host group "Value maps" */
static int	DBpatch_6000000_25(void)
{
	zbx_uint64_t	groupid;
	int		ret = FAIL;

	ONLY_SERVER();

	groupid = DBget_maxid_num("hstgrp", 1);

	DB_EXEC(
			"insert into hstgrp (groupid,name,internal,flags,uuid)"
			" values (" ZBX_FS_UI64 ",'%s',%d,%d,'%s')",
			groupid, "Value Maps", 0, 0, "5f022f8b797d44c69dbbcecc1e7fcd30");

	DB_EXEC("delete from ids where table_name='hstgrp'");

	ret = SUCCEED;
out:
	return ret;
}

/* 6000000, 26 - add "Value Maps" host group to "Template Value Maps" */
static int	DBpatch_6000000_26(void)
{
	zbx_uint64_t	hostgroupid, hostid, groupid;
	int		ret = FAIL;

	ONLY_SERVER();

	hostgroupid = DBget_maxid_num("hosts_groups", 1);

	SELECT_VALUE_UINT64(groupid, "select groupid from hstgrp where name='%s'", "Value Maps");
	SELECT_VALUE_UINT64(hostid,  "select hostid from hosts where name='%s'", "Template Value Maps");

	DB_EXEC(
			"insert into hosts_groups (hostgroupid,hostid,groupid)"
			" values (" ZBX_FS_UI64 "," ZBX_FS_UI64 "," ZBX_FS_UI64 ")",
			hostgroupid, hostid, groupid);

	DB_EXEC("delete from ids where table_name='hosts_groups'");

	ret = SUCCEED;
out:
	return ret;
}

/* 6000000, 27 - allow "Power user" and "Read-only user" user groups to access "Value Maps" host group */
static int	DBpatch_6000000_27(void)
{
	zbx_uint64_t	rightid, hstgrp_groupid;
	int		ret = FAIL;

	ONLY_SERVER();

	rightid = DBget_maxid_num("rights", 2);

	SELECT_VALUE_UINT64(hstgrp_groupid, "select groupid from hstgrp where name='%s'", "Value Maps");

	DB_EXEC(
			"insert into rights (rightid,groupid,permission,id)"
			" values (" ZBX_FS_UI64 ",%d,%d," ZBX_FS_UI64 ")",
			rightid, 100, 2, hstgrp_groupid);

	DB_EXEC(
			"insert into rights (rightid,groupid,permission,id)"
			" values (" ZBX_FS_UI64 ",%d,%d," ZBX_FS_UI64 ")",
			++rightid, 110, 2, hstgrp_groupid);

	DB_EXEC("delete from ids where table_name='rights'");

	ret = SUCCEED;
out:
	return ret;
}

static int	DBpatch_6000001(void)
{
	if (0 == (program_type & ZBX_PROGRAM_TYPE_SERVER))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute("delete from profiles where idx='web.auditlog.filter.action' and value_int=-1"))
		return FAIL;

	return SUCCEED;
}

static int	DBpatch_6000002(void)
{
	if (0 == (program_type & ZBX_PROGRAM_TYPE_SERVER))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute("update profiles set idx='web.auditlog.filter.actions' where"
			" idx='web.auditlog.filter.action'"))
	{
		return FAIL;
	}

	return SUCCEED;
}

#define HTTPSTEP_ITEM_TYPE_RSPCODE	0
#define HTTPSTEP_ITEM_TYPE_TIME		1
#define HTTPSTEP_ITEM_TYPE_IN		2
#define HTTPSTEP_ITEM_TYPE_LASTSTEP	3
#define HTTPSTEP_ITEM_TYPE_LASTERROR	4

static int	DBpatch_6000003(void)
{
	DB_ROW		row;
	DB_RESULT	result;
	int		ret = SUCCEED;
	char		*sql = NULL;
	size_t		sql_alloc = 0, sql_offset = 0, out_alloc = 0;
	char		*out = NULL;

	if (ZBX_PROGRAM_TYPE_SERVER != program_type)
		return SUCCEED;

	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	result = DBselect(
			"select hi.itemid,hi.type,ht.name"
			" from httptestitem hi,httptest ht"
			" where hi.httptestid=ht.httptestid");

	while (SUCCEED == ret && NULL != (row = DBfetch(result)))
	{
		zbx_uint64_t	itemid;
		char		*esc;
		size_t		out_offset = 0;
		unsigned char	type;

		ZBX_STR2UINT64(itemid, row[0]);
		ZBX_STR2UCHAR(type, row[1]);

		switch (type)
		{
			case HTTPSTEP_ITEM_TYPE_IN:
				zbx_snprintf_alloc(&out, &out_alloc, &out_offset,
						"Download speed for scenario \"%s\".", row[2]);
				break;
			case HTTPSTEP_ITEM_TYPE_LASTSTEP:
				zbx_snprintf_alloc(&out, &out_alloc, &out_offset,
						"Failed step of scenario \"%s\".", row[2]);
				break;
			case HTTPSTEP_ITEM_TYPE_LASTERROR:
				zbx_snprintf_alloc(&out, &out_alloc, &out_offset,
						"Last error message of scenario \"%s\".", row[2]);
				break;
		}
		esc = DBdyn_escape_field("items", "name", out);
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update items set name='%s' where itemid="
				ZBX_FS_UI64 ";\n", esc, itemid);
		zbx_free(esc);

		ret = DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
	}
	DBfree_result(result);

	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	if (SUCCEED == ret && 16 < sql_offset)
	{
		if (ZBX_DB_OK > DBexecute("%s", sql))
			ret = FAIL;
	}

	zbx_free(sql);
	zbx_free(out);

	return ret;
}

static int	DBpatch_6000004(void)
{
	DB_ROW		row;
	DB_RESULT	result;
	int		ret = SUCCEED;
	char		*sql = NULL;
	size_t		sql_alloc = 0, sql_offset = 0, out_alloc = 0;
	char		*out = NULL;

	if (ZBX_PROGRAM_TYPE_SERVER != program_type)
		return SUCCEED;

	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	result = DBselect(
			"select hi.itemid,hi.type,hs.name,ht.name"
			" from httpstepitem hi,httpstep hs,httptest ht"
			" where hi.httpstepid=hs.httpstepid"
				" and hs.httptestid=ht.httptestid");

	while (SUCCEED == ret && NULL != (row = DBfetch(result)))
	{
		zbx_uint64_t	itemid;
		char		*esc;
		size_t		out_offset = 0;
		unsigned char	type;

		ZBX_STR2UINT64(itemid, row[0]);
		ZBX_STR2UCHAR(type, row[1]);

		switch (type)
		{
			case HTTPSTEP_ITEM_TYPE_IN:
				zbx_snprintf_alloc(&out, &out_alloc, &out_offset,
						"Download speed for step \"%s\" of scenario \"%s\".", row[2], row[3]);
				break;
			case HTTPSTEP_ITEM_TYPE_TIME:
				zbx_snprintf_alloc(&out, &out_alloc, &out_offset,
						"Response time for step \"%s\" of scenario \"%s\".", row[2], row[3]);
				break;
			case HTTPSTEP_ITEM_TYPE_RSPCODE:
				zbx_snprintf_alloc(&out, &out_alloc, &out_offset,
						"Response code for step \"%s\" of scenario \"%s\".", row[2], row[3]);
				break;
		}

		esc = DBdyn_escape_field("items", "name", out);
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update items set name='%s' where itemid="
				ZBX_FS_UI64 ";\n", esc, itemid);
		zbx_free(esc);

		ret = DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
	}
	DBfree_result(result);

	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	if (SUCCEED == ret && 16 < sql_offset)
	{
		if (ZBX_DB_OK > DBexecute("%s", sql))
			ret = FAIL;
	}

	zbx_free(sql);
	zbx_free(out);

	return ret;
}

#undef HTTPSTEP_ITEM_TYPE_RSPCODE
#undef HTTPSTEP_ITEM_TYPE_TIME
#undef HTTPSTEP_ITEM_TYPE_IN
#undef HTTPSTEP_ITEM_TYPE_LASTSTEP
#undef HTTPSTEP_ITEM_TYPE_LASTERROR

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
DBPATCH_RSM(6000000, 11, 0, 0)	/* register Provisioning API module and create its users */
DBPATCH_RSM(6000000, 12, 0, 0)	/* create a template for storing value maps */
DBPATCH_RSM(6000000, 13, 0, 0)	/* enable show_technical_errors */
DBPATCH_RSM(6000000, 14, 0, 0)	/* reset items.lifetime and items.request_method to default values */
DBPATCH_RSM(6000000, 15, 0, 0)	/* add missing macros - {$RDAP.BASE.URL}, {$RDAP.TEST.DOMAIN}, {$RSM.RDDS43.TEST.DOMAIN} */
DBPATCH_RSM(6000000, 16, 0, 1)	/* create table rsm_false_positive, this is required by frontend */
DBPATCH_RSM(6000000, 17, 0, 0)	/* add userid index to table rsm_false_positive */
DBPATCH_RSM(6000000, 18, 0, 0)	/* add userid foreign key to table rsm_false_positive */
DBPATCH_RSM(6000000, 19, 0, 0)	/* add eventid index to table rsm_false_positive */
DBPATCH_RSM(6000000, 20, 0, 0)	/* add eventid foreign key to table rsm_false_positive */
DBPATCH_RSM(6000000, 21, 0, 0)	/* drop column events.false_positive */
DBPATCH_RSM(6000000, 22, 0, 1)  /* add settings for "Read-only user" and "Power user" roles */
DBPATCH_RSM(6000000, 23, 0, 0)  /* add script "DNSViz webhook" */
DBPATCH_RSM(6000000, 24, 0, 0)  /* add action "Create DNSViz report" for DNSSEC accidents */
DBPATCH_RSM(6000000, 25, 0, 0)  /* create host group "Value Maps" */
DBPATCH_RSM(6000000, 26, 0, 0)  /* add "Value Maps" host group to "Template Value Maps" */
DBPATCH_RSM(6000000, 27, 0, 0)  /* allow "Power user" and "Read-only user" user groups to access "Value Maps" host group */
DBPATCH_ADD(6000001, 0, 0)
DBPATCH_ADD(6000002, 0, 0)
DBPATCH_ADD(6000003, 0, 0)
DBPATCH_ADD(6000004, 0, 0)

DBPATCH_END()
