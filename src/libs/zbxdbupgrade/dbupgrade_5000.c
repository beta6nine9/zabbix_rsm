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
	DB_RESULT		result;
	int			ret;
	zbx_field_len_t		fields[] = {
			{"subject", 255},
			{"message", 65535}
	};

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
	DB_RESULT		result;
	int			ret;
	zbx_field_len_t		fields[] = {
			{"subject", 255},
			{"message", 65535}
	};

	result = DBselect("select mediatype_messageid,subject,message from media_type_message where recovery=1");

	ret = db_rename_macro(result, "media_type_message", "mediatype_messageid", fields, ARRSIZE(fields),
			"{EVENT.NAME}", "{EVENT.RECOVERY.NAME}");

	DBfree_result(result);

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

DBPATCH_END()
