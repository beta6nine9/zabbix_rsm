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

/* gets groupid of the host group */
#define GET_HOST_GROUP_ID(groupid, name)										\
		SELECT_VALUE_UINT64(groupid, "select groupid from hstgrp where name='%s'", name)

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

DBPATCH_END()
