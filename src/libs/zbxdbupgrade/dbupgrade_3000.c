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

/*
 * 3.0 maintenance database patches
 */

#ifndef HAVE_SQLITE3

#define zbx_db_dyn_escape_string(src)	zbx_db_dyn_escape_string(src, ZBX_SIZE_T_MAX, ZBX_SIZE_T_MAX, ESCAPE_SEQUENCE_ON)

extern unsigned char	program_type;

static int	DBpatch_3000000(void)
{
	return SUCCEED;
}

static int	DBpatch_3000100(void)
{
	zabbix_log(LOG_LEVEL_CRIT, "There is no automatic database upgrade for Phase 1");

	return FAIL;
}

static int	add_hostgroup(const char *name, zbx_uint64_t groupid)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute(
			"insert into groups (groupid,name,internal,flags)"
			" values ('" ZBX_FS_UI64 "','%s','0','0')", groupid, name))
	{
		return FAIL;
	}

	if (ZBX_DB_OK > DBexecute(
			"update ids"
			" set nextid=(select max(groupid) from groups)"
			" where table_name='groups'"
				" and field_name='groupid'"))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_3000101(void)
{
	return add_hostgroup("TLD Probe results", 190);
}

static int	DBpatch_3000102(void)
{
	return add_hostgroup("gTLD Probe results", 200);
}

static int	DBpatch_3000103(void)
{
	return add_hostgroup("ccTLD Probe results", 210);
}

static int	DBpatch_3000104(void)
{
	return add_hostgroup("testTLD Probe results", 220);
}

static int	DBpatch_3000105(void)
{
	return add_hostgroup("otherTLD Probe results", 230);
}

static int	add_right(zbx_uint64_t rightid, zbx_uint64_t usergroupid, zbx_uint64_t groupid)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute(
			"insert into rights (rightid,groupid,permission,id)"
			" values ('" ZBX_FS_UI64 "','" ZBX_FS_UI64 "','2','" ZBX_FS_UI64 "')",
			rightid, usergroupid, groupid))
	{
		return FAIL;
	}

	if (ZBX_DB_OK > DBexecute(
			"update ids"
			" set nextid=(select max(rightid) from rights)"
			" where table_name='rights'"
				" and field_name='rightid'"))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_3000106(void)
{
	return add_right(116, 110, 190);
}

static int	DBpatch_3000107(void)
{
	return add_right(106, 100, 200);
}

static int	DBpatch_3000108(void)
{
	return add_right(117, 110, 120);
}

static int	DBpatch_3000109(void)
{
	return add_right(107, 100, 120);
}

static int	add_hosts_to_group(const char *tld_type, zbx_uint64_t groupid)
{
	DB_RESULT	result;
	DB_ROW		row;
	zbx_uint64_t	hostgroupid;

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	result = DBselect("select max(hostgroupid) from hosts_groups");

	if (NULL == (row = DBfetch(result)))
	{
		DBfree_result(result);
		return FAIL;
	}

	ZBX_STR2UINT64(hostgroupid, row[0]);

	result = DBselect(
			"select h.host"
			" from hosts h,groups g,hosts_groups hg"
			" where h.hostid=hg.hostid"
				" and hg.groupid=g.groupid"
				" and g.name='%s'", tld_type);

	while (NULL != (row = DBfetch(result)))
	{
		DB_RESULT	result2;
		DB_ROW		row2;

		result2 = DBselect(
				"select h.hostid"
				" from hosts h,groups g,hosts_groups hg"
				" where h.hostid=hg.hostid"
					" and hg.groupid=g.groupid"
					" and h.proxy_hostid is not null"
					" and g.name='TLD %s'", row[0]);

		while (NULL != (row2 = DBfetch(result2)))
		{
			zbx_uint64_t	hostid;

			ZBX_STR2UINT64(hostid, row2[0]);

			if (ZBX_DB_OK > DBexecute("insert into hosts_groups (hostgroupid,hostid,groupid) values"
					" ('" ZBX_FS_UI64 "','" ZBX_FS_UI64 "','" ZBX_FS_UI64 "')",
					++hostgroupid, hostid, groupid))
			{
				DBfree_result(result2);
				DBfree_result(result);

				return FAIL;
			}

		}

		DBfree_result(result2);
	}

	DBfree_result(result);

	if (ZBX_DB_OK > DBexecute(
			"update ids"
			" set nextid=(select max(hostgroupid) from hosts_groups)"
			" where table_name='hosts_groups'"
				" and field_name='hostgroupid'"))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_3000110(void)
{
	return add_hosts_to_group("gTLD", 200);
}

static int	DBpatch_3000111(void)
{
	return add_hosts_to_group("ccTLD", 210);
}

static int	DBpatch_3000112(void)
{
	return add_hosts_to_group("testTLD", 220);
}

static int	DBpatch_3000113(void)
{
	return add_hosts_to_group("otherTLD", 230);
}

static int	DBpatch_3000114(void)
{
	return add_hosts_to_group("TLDs", 190);
}

static int	DBpatch_update_trigger_expression(const char *old_expr, const char *new_expr)
{
	char	*old_expr_esc, *new_expr_esc;
	int	ret = SUCCEED;

	old_expr_esc = zbx_db_dyn_escape_string(old_expr);
	new_expr_esc = zbx_db_dyn_escape_string(new_expr);

	if (ZBX_DB_OK > DBexecute("update triggers set expression='%s' where expression='%s'", new_expr_esc, old_expr_esc))
		ret = FAIL;

	zbx_free(old_expr_esc);
	zbx_free(new_expr_esc);

	return ret;
}

static int	DBpatch_3000115(void)
{
	return DBpatch_update_trigger_expression(
			"{100102}<{$RSM.IP4.PROBE.ONLINE}",
			"{100102}<{$RSM.IP4.MIN.PROBE.ONLINE}");
}

static int	DBpatch_3000116(void)
{
	return DBpatch_update_trigger_expression(
			"{100103}<{$RSM.IP6.PROBE.ONLINE}",
			"{100103}<{$RSM.IP6.MIN.PROBE.ONLINE}");
}

static int	DBpatch_3000117(void)
{
	DB_RESULT	result;
	DB_ROW		row;
	int		ret = SUCCEED;

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	result = DBselect(
			"select h.hostid"
			" from hosts h,applications a"
			" where h.status in (0,1)"	/* HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED */
				" and a.hostid=h.hostid"
				" and a.name='Probe status'");

	if (NULL == result)
		return FAIL;

	while (NULL != (row = DBfetch(result)) && SUCCEED == ret)
	{
		zbx_uint64_t		hostid;
		zbx_vector_uint64_t	templateids;
		char			*error;

		ZBX_STR2UINT64(hostid, row[0]);			/* hostid of probe host */
		zbx_vector_uint64_create(&templateids);
		zbx_vector_uint64_reserve(&templateids, 1);
		zbx_vector_uint64_append(&templateids, 10058);	/* hostid of "Template App Zabbix Proxy" */

		if (SUCCEED != (ret = DBcopy_template_elements(hostid, &templateids, &error)))
		{
			zabbix_log(LOG_LEVEL_CRIT, "cannot link template(s) %s", error);
			zbx_free(error);
		}

		zbx_vector_uint64_destroy(&templateids);
	}

	DBfree_result(result);

	return ret;
}

static int	DBpatch_3000118(void)
{
	return add_right(109, 110, 130);
}

static int	DBpatch_3000119(void)
{
	return add_right(119, 110, 110);
}

static int	DBpatch_3000120(void)
{
	return DBpatch_3000117();
}

typedef enum
{
	OP_MESSAGE,
	OP_COMMAND
}
operation_type_t;

typedef enum
{
	OP_MESSAGE_USR
}
opmessage_type_t;

typedef union
{
	zbx_uint64_t	userid;
}
opmessage_data_t;

typedef struct
{
	zbx_uint64_t		id;
	opmessage_type_t	type;
	opmessage_data_t	data;
}
recipient_t;

typedef struct
{
#define MAX_RECIPIENTS	1

	int		default_msg;
	const char	*subject;
	const char	*message;
	zbx_uint64_t	mediatypeid;
	recipient_t	recipients[MAX_RECIPIENTS + 1];

#undef MAX_RECIPIENTS
}
opmessage_t;

typedef enum
{
	OP_COMMAND_CURRENT_HOST
}
opcommand_type_t;

typedef struct
{
	zbx_uint64_t		id;
	opcommand_type_t	type;
}
target_t;

typedef struct
{
#define MAX_TARGETS	1

	int		type;
	int		execute_on;
	const char	*port;
	int		authtype;
	const char	*username;
	const char	*password;
	const char	*publickey;
	const char	*privatekey;
	const char	*command;
	target_t	targets[MAX_TARGETS + 1];

#undef MAX_TARGETS
}
opcommand_t;

typedef union
{
	opmessage_t	opmessage;
	opcommand_t	opcommand;
}
operation_data_t;

typedef struct
{
	zbx_uint64_t		id;
	operation_type_t	type;
	operation_data_t	data;
}
operation_t;

typedef struct
{
	zbx_uint64_t	id;
	int		conditiontype;
	int		op;
	const char	*value;
}
condition_t;

typedef struct
{
#define MAX_OPERATIONS	1
#define MAX_CONDITIONS	5

	zbx_uint64_t	id;
	const char	*name;
	int		esc_period;
	const char	*def_shortdata;
	const char	*def_longdata;
	int		recovery_msg;
	const char	*r_shortdata;
	const char	*r_longdata;
	operation_t	operations[MAX_OPERATIONS + 1];
	condition_t	conditions[MAX_CONDITIONS + 1];

#undef MAX_OPERATIONS
#undef MAX_CONDITIONS
}
action_t;

static int	db_insert_action(const action_t *action)
{
	char	*name_esc = NULL, *def_shortdata_esc = NULL, *def_longdata_esc = NULL, *r_shortdata_esc = NULL,
		*r_longdata_esc = NULL;
	int	ret;

	name_esc = zbx_db_dyn_escape_string(action->name);
	def_shortdata_esc = zbx_db_dyn_escape_string(action->def_shortdata);
	def_longdata_esc = zbx_db_dyn_escape_string(action->def_longdata);
	r_shortdata_esc = zbx_db_dyn_escape_string(action->r_shortdata);
	r_longdata_esc = zbx_db_dyn_escape_string(action->r_longdata);

	ret = DBexecute(
			"insert into actions (actionid,name,esc_period,def_shortdata,def_longdata,"
				"recovery_msg,r_shortdata,r_longdata)"
			" values (" ZBX_FS_UI64 ",'%s',%d,'%s','%s',%d,'%s','%s')",
			action->id, name_esc, action->esc_period, def_shortdata_esc, def_longdata_esc,
			action->recovery_msg, r_shortdata_esc, r_longdata_esc);

	zbx_free(name_esc);
	zbx_free(def_shortdata_esc);
	zbx_free(def_longdata_esc);
	zbx_free(r_shortdata_esc);
	zbx_free(r_longdata_esc);

	return ZBX_DB_OK > ret ? FAIL : SUCCEED;
}

static int	db_insert_opmessage(zbx_uint64_t operationid, const opmessage_t *opmessage)
{
	const recipient_t	*recipient;
	char			*subject_esc = NULL, *message_esc = NULL;
	int			ret;

	subject_esc = zbx_db_dyn_escape_string(opmessage->subject);
	message_esc = zbx_db_dyn_escape_string(opmessage->message);

	ret = DBexecute(
			"insert into opmessage (operationid,default_msg,subject,message,mediatypeid)"
			" values (" ZBX_FS_UI64 ",%d,'%s','%s'," ZBX_FS_UI64 ")",
			operationid, opmessage->default_msg, subject_esc, message_esc, opmessage->mediatypeid);

	zbx_free(subject_esc);
	zbx_free(message_esc);

	if (ZBX_DB_OK > ret)
		return FAIL;

	for (recipient = opmessage->recipients; 0 != recipient->id; recipient++)
	{
		switch (recipient->type)
		{
			case OP_MESSAGE_USR:
				if (ZBX_DB_OK > DBexecute(
						"insert into opmessage_usr (opmessage_usrid,operationid,userid)"
						" values (" ZBX_FS_UI64 "," ZBX_FS_UI64 "," ZBX_FS_UI64 ")",
						recipient->id, operationid, recipient->data.userid))
				{
					return FAIL;
				}
				break;
			default:
				THIS_SHOULD_NEVER_HAPPEN;
				return FAIL;
		}
	}

	return SUCCEED;
}

static int	db_insert_opcommand(zbx_uint64_t operationid, const opcommand_t *opcommand)
{
	const target_t	*target;
	char		*port_esc = NULL, *username_esc = NULL, *password_esc = NULL, *publickey_esc = NULL,
			*privatekey_esc = NULL, *command_esc = NULL;
	int		ret;

	if (0 != opcommand->type)	/* ZBX_SCRIPT_TYPE_CUSTOM_SCRIPT */
	{
		THIS_SHOULD_NEVER_HAPPEN;
		return FAIL;
	}

	port_esc = zbx_db_dyn_escape_string(opcommand->port);
	username_esc = zbx_db_dyn_escape_string(opcommand->username);
	password_esc = zbx_db_dyn_escape_string(opcommand->password);
	publickey_esc = zbx_db_dyn_escape_string(opcommand->publickey);
	privatekey_esc = zbx_db_dyn_escape_string(opcommand->privatekey);
	command_esc = zbx_db_dyn_escape_string(opcommand->command);

	ret = DBexecute(
			"insert into opcommand (operationid,type,scriptid,execute_on,port,authtype,"
				"username,password,publickey,privatekey,command)"
			" values (" ZBX_FS_UI64 ",%d,null,%d,'%s',"
				"%d,'%s','%s','%s','%s','%s')",
			operationid, opcommand->type, opcommand->execute_on, port_esc, opcommand->authtype,
				username_esc, password_esc, publickey_esc, privatekey_esc, command_esc);

	zbx_free(port_esc);
	zbx_free(username_esc);
	zbx_free(password_esc);
	zbx_free(publickey_esc);
	zbx_free(privatekey_esc);
	zbx_free(command_esc);

	if (ZBX_DB_OK > ret)
		return FAIL;

	for (target = opcommand->targets; 0 != target->id; target++)
	{
		switch (target->type)
		{
			case OP_COMMAND_CURRENT_HOST:
				if (ZBX_DB_OK > DBexecute(
						"insert into opcommand_hst (opcommand_hstid,operationid,hostid)"
						" values (" ZBX_FS_UI64 "," ZBX_FS_UI64 ",null)",
						target->id, operationid))
				{
					return FAIL;
				}
				break;
			default:
				THIS_SHOULD_NEVER_HAPPEN;
				return FAIL;
		}
	}

	return SUCCEED;
}

static int	db_insert_condition(zbx_uint64_t actionid, const condition_t *condition)
{
	char	*value_esc = NULL;
	int	ret;

	value_esc = zbx_db_dyn_escape_string(condition->value);

	ret = DBexecute(
			"insert into conditions (conditionid,actionid,conditiontype,operator,value)"
			" values (" ZBX_FS_UI64 "," ZBX_FS_UI64 ",%d,%d,'%s')",
			condition->id, actionid, condition->conditiontype, condition->op, value_esc);

	zbx_free(value_esc);

	return ZBX_DB_OK > ret ? FAIL : SUCCEED;
}

static int	add_actions(const action_t *actions)
{
	const action_t		*action;
	const operation_t	*operation;
	const condition_t	*condition;

	for (action = actions; 0 != action->id; action++)
	{
		if (SUCCEED != db_insert_action(action))
			return FAIL;

		for (operation = action->operations; 0 != operation->id; operation++)
		{
			int	operationtype;

			switch (operation->type)
			{
				case OP_MESSAGE:
					operationtype = 0;	/* OPERATION_TYPE_MESSAGE */
					break;
				case OP_COMMAND:
					operationtype = 1;	/* OPERATION_TYPE_COMMAND */
					break;
				default:
					THIS_SHOULD_NEVER_HAPPEN;
					return FAIL;
			}

			if (ZBX_DB_OK > DBexecute(
					"insert into operations (operationid,actionid,operationtype)"
					" values (" ZBX_FS_UI64 "," ZBX_FS_UI64 ",%d)",
					operation->id, action->id, operationtype))
			{
				return FAIL;
			}

			switch (operation->type)
			{
				case OP_MESSAGE:
					if (SUCCEED != db_insert_opmessage(operation->id, &operation->data.opmessage))
						return FAIL;
					break;
				case OP_COMMAND:
					if (SUCCEED != db_insert_opcommand(operation->id, &operation->data.opcommand))
						return FAIL;
					break;
				default:
					THIS_SHOULD_NEVER_HAPPEN;
					return FAIL;
			}
		}

		for (condition = action->conditions; 0 != condition->id; condition++)
		{
			if (SUCCEED != db_insert_condition(action->id, condition))
				return FAIL;
		}
	}

	if (ZBX_DB_OK > DBexecute(
			"delete from ids"
			" where table_name in ('actions','operations','opmessage','opmessage_usr','conditions')"))
	{
		return FAIL;
	}

	return SUCCEED;
}

static const action_t	actions[] = {
	{110,	"Probes-Mon",		3600,
		"probes#{TRIGGER.STATUS}#{HOST.NAME1}#{ITEM.NAME1}#{ITEM.VALUE1}",	"{EVENT.DATE} {EVENT.TIME} UTC",
		1,
		"probes#{TRIGGER.STATUS}#{HOST.NAME1}#{ITEM.NAME1}#{ITEM.VALUE1}",	"{EVENT.DATE} {EVENT.TIME} UTC",
		{
			{110,	OP_MESSAGE,	{.opmessage = {1,	"",	"",	10,	{
					{110,	OP_MESSAGE_USR,	{.userid = 100}},
					{0}
			}}}},
			{0}
		},
		{
			{110,	16,	7,	""},
			{111,	5,	0,	"1"},
			{112,	4,	5,	"2"},
			{113,	0,	0,	"130"},
			{0}
		}
	},
	{120,	"Central-Server",	3600,
		"system#{TRIGGER.STATUS}#{HOST.NAME1}#{ITEM.NAME1}#{ITEM.VALUE1}",	"{EVENT.DATE} {EVENT.TIME} UTC",
		1,
		"system#{TRIGGER.STATUS}#{HOST.NAME1}#{ITEM.NAME1}#{ITEM.VALUE1}",	"{EVENT.DATE} {EVENT.TIME} UTC",
		{
			{120,	OP_MESSAGE,	{.opmessage = {1,	"",	"",	10,	{
					{120,	OP_MESSAGE_USR,	{.userid = 100}},
					{0}
			}}}},
			{0}
		},
		{
			{120,	16,	7,	""},
			{121,	5,	0,	"1"},
			{122,	4,	5,	"2"},
			{123,	0,	0,	"110"},
			{0}
		}
	},
	{130,	"TLDs",			3600,
		"tld#{TRIGGER.STATUS}#{HOST.NAME1}#{ITEM.NAME1}#{ITEM.VALUE1}",		"{EVENT.DATE} {EVENT.TIME} UTC",
		1,
		"tld#{TRIGGER.STATUS}#{HOST.NAME1}#{ITEM.NAME1}#{ITEM.VALUE1}",		"{EVENT.DATE} {EVENT.TIME} UTC",
		{
			{130,	OP_MESSAGE,	{.opmessage = {1,	"",	"",	10,	{
					{130,	OP_MESSAGE_USR,	{.userid = 100}},
					{0}
			}}}},
			{0}
		},
		{
			{130,	16,	7,	""},
			{131,	5,	0,	"1"},
			{132,	0,	0,	"140"},
			{0}
		}
	},
	{0}
};

static int	DBpatch_3000121(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	return add_actions(actions);
}

static int	DBpatch_3000122(void)
{
	char	*params_esc = NULL;
	int	ret;

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	params_esc = zbx_db_dyn_escape_string("{ALERT.SENDTO}\n{ALERT.SUBJECT}\n{ALERT.MESSAGE}\n");

	ret = DBexecute("update media_type set exec_params='%s' where mediatypeid=10", params_esc);

	zbx_free(params_esc);

	return ZBX_DB_OK > ret ? FAIL : SUCCEED;
}

static int	DBpatch_3000123(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute("update hosts_groups set groupid=110 where hostgroupid=1001"))
		return FAIL;

	return SUCCEED;
}

static int	DBpatch_3000124(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute("update rights set id=130 where rightid=106"))
		return FAIL;

	return SUCCEED;
}

static int	DBpatch_3000125(void)
{
	char	*key_esc = NULL;
	int	ret;

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	key_esc = zbx_db_dyn_escape_string("zabbix[proxy,{$RSM.PROXY_NAME},lastaccess]");

	ret = DBexecute("update items set name='Availability of probe' where key_='%s'", key_esc);

	zbx_free(key_esc);

	return ZBX_DB_OK > ret ? FAIL : SUCCEED;
}

static int	DBpatch_3000126(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute(
			"delete from hosts_groups"
			" where hostid in (select * from ("
				"select hostid from hosts_groups"
				" where groupid=120) as probes)"	/* groupid of "Probes" host group */
			" and groupid<>120"))				/* groupid of "Probes" host group */
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_3000127(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute(
			"delete from hosts_groups"
			" where hostid in (select * from ("
				"select hostid from hosts_groups"
				" where groupid=140) as tlds)"		/* groupid of "TLDs" host group */
			" and groupid not in (140,150,160,170,180)"))	/* groupids of host groups: "TLDs", "gTLD", */
	{								/* "ccTLD", "testTLD" and "otherTLD" */
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_3000128(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute("update functions set parameter='10s' where function='fuzzytime'"))
		return FAIL;

	return SUCCEED;
}

static int	DBpatch_3000129(void)
{
	char	*desc_esc = NULL;
	int	ret;

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	desc_esc = zbx_db_dyn_escape_string("System time on {HOST.HOST} is out of sync with Zabbix Server");

	ret = DBexecute("update triggers set description='%s' where triggerid=13509 or templateid=13509", desc_esc);

	zbx_free(desc_esc);

	return ZBX_DB_OK > ret ? FAIL : SUCCEED;
}

static int	DBpatch_3000130(void)
{
	const ZBX_TABLE table =
			{"lastvalue", "itemid", 0,
				{
					{"itemid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"clock", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
					{"value", "0.0000", NULL, NULL, 0, ZBX_TYPE_FLOAT, ZBX_NOTNULL, 0},
					{0}
				},
				NULL
			};

	return DBcreate_table(&table);
}

static int	DBpatch_3000131(void)
{
	const ZBX_FIELD	field = {"itemid", NULL, "items", "itemid", 0, 0, 0, ZBX_FK_CASCADE_DELETE};

	return DBadd_foreign_key("lastvalue", 1, &field);
}

static int	DBpatch_3000132(void)
{
	return DBpatch_3000122();
}

static int	DBpatch_3000133(void)
{
	if (ZBX_DB_OK > DBexecute(
			"update actions"
			" set r_longdata='{EVENT.RECOVERY.DATE} {EVENT.RECOVERY.TIME} UTC'"
			" where actionid in (100,110,120,130)"))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	move_interface(zbx_uint64_t interfaceid)
{
	DB_RESULT	result;
	DB_ROW		row;
	zbx_uint64_t	nextid;

	if (NULL == (result = DBselect("select max(interfaceid)+1 from interface")))
		return FAIL;

	if (NULL == (row = DBfetch(result)))
	{
		THIS_SHOULD_NEVER_HAPPEN;	/* there is "Zabbix Server" host, 'interface' table can't be empty */
		DBfree_result(result);
		return FAIL;
	}

	ZBX_STR2UINT64(nextid, row[0]);
	DBfree_result(result);

	if (ZBX_DB_OK > DBexecute(
			"insert into interface (interfaceid,hostid,main,type,useip,ip,dns,port,bulk)"
			" select " ZBX_FS_UI64 ",hostid,main,type,useip,ip,dns,port,bulk from interface"
			" where interfaceid=" ZBX_FS_UI64,
			nextid, interfaceid))
	{
		return FAIL;
	}

	if (ZBX_DB_OK > DBexecute(
			"update items set interfaceid=" ZBX_FS_UI64
			" where interfaceid=" ZBX_FS_UI64,
			nextid, interfaceid))
	{
		return FAIL;
	}

	if (ZBX_DB_OK > DBexecute(
			"update interface_discovery set interfaceid=" ZBX_FS_UI64
			" where interfaceid=" ZBX_FS_UI64,
			nextid, interfaceid))
	{
		return FAIL;
	}

	if (ZBX_DB_OK > DBexecute(
			"update interface_discovery set parent_interfaceid=" ZBX_FS_UI64
			" where parent_interfaceid=" ZBX_FS_UI64,
			nextid, interfaceid))
	{
		return FAIL;
	}

	if (ZBX_DB_OK > DBexecute("delete from interface where interfaceid=" ZBX_FS_UI64, interfaceid))
		return FAIL;

	return SUCCEED;
}

static int	reserve_interfaceid(zbx_uint64_t interfaceid)
{
	DB_RESULT	result;
	DB_ROW		row;

	if (NULL == (result = DBselect("select null from interface where interfaceid=" ZBX_FS_UI64, interfaceid)))
		return FAIL;

	row = DBfetch(result);
	DBfree_result(result);
	return NULL == row ? SUCCEED : move_interface(interfaceid);
}

static int	DBpatch_3000134(void)
{
#define DEFAULT_INTERFACE_INSERT								\
	"insert into interface (interfaceid,hostid,main,type,useip,ip,dns,port,bulk)"		\
	" values ('%u','%u','1','1','1','127.0.0.1','','10050','1')"

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (SUCCEED != reserve_interfaceid(2) || ZBX_DB_OK > DBexecute(DEFAULT_INTERFACE_INSERT, 2, 100000))
		return FAIL;

	if (SUCCEED != reserve_interfaceid(3) || ZBX_DB_OK > DBexecute(DEFAULT_INTERFACE_INSERT, 3, 100001))
		return FAIL;

	if (ZBX_DB_OK > DBexecute("delete from ids where table_name='interface'"))
		return FAIL;

	return SUCCEED;

#undef DEFAULT_INTERFACE_INSERT
}

static int	DBpatch_3000135(void)
{
#define RESERVE_GLOBALMACROID									\
		"update globalmacro"								\
		" set globalmacroid=(select nextid from ("					\
			"select max(globalmacroid)+1 as nextid from globalmacro) as tmp)"	\
		" where globalmacroid=%u"

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute(RESERVE_GLOBALMACROID, 56))
		return FAIL;

	if (ZBX_DB_OK > DBexecute(
			"insert into globalmacro (globalmacroid,macro,value)"
			" values (56,'{$MAX_CPU_LOAD}','50')"))
	{
		return FAIL;
	}

	if (ZBX_DB_OK > DBexecute(RESERVE_GLOBALMACROID, 57))
		return FAIL;

	if (ZBX_DB_OK > DBexecute(
			"insert into globalmacro (globalmacroid,macro,value)"
			" values (57,'{$MAX_RUN_PROCESSES}','1500')"))
	{
		return FAIL;
	}

	if (ZBX_DB_OK > DBexecute("delete from ids where table_name='globalmacro'"))
		return FAIL;

	return SUCCEED;

#undef RESERVE_GLOBALMACROID
}

static int	DBpatch_3000136(void)
{
#define RESERVE_HOSTMACROID								\
		"update hostmacro"							\
		" set hostmacroid=(select nextid from ("				\
			"select max(hostmacroid)+1 as nextid from hostmacro) as tmp)"	\
		" where hostmacroid=%u"

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute(RESERVE_HOSTMACROID, 1))
		return FAIL;

	if (ZBX_DB_OK > DBexecute(
			"insert into hostmacro (hostmacroid,hostid,macro,value)"
			" values (1,10057,'{$MAX_CPU_LOAD}','5')"))		/* hostid of "Zabbix Server" host */
	{
		return FAIL;
	}

	if (ZBX_DB_OK > DBexecute(RESERVE_HOSTMACROID, 2))
		return FAIL;

	if (ZBX_DB_OK > DBexecute(
			"insert into hostmacro (hostmacroid,hostid,macro,value)"
			" values (2,10057,'{$MAX_RUN_PROCESSES}','30')"))	/* hostid of "Zabbix Server" host */
	{
		return FAIL;
	}

	if (ZBX_DB_OK > DBexecute("delete from ids where table_name='hostmacro'"))
		return FAIL;

	return SUCCEED;

#undef RESERVE_HOSTMACROID
}

static int	DBpatch_3000137(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute("update functions"
			" set function='avg',parameter='#2'"	/* "Processor load is too high on {HOST.NAME}" trigger */
			" where triggerid=10010"		/* triggerids on "Template OS Linux" template */
			" or triggerid=13541"))			/* and "Zabbix Server" host */
	{
		return FAIL;
	}

	if (ZBX_DB_OK > DBexecute("update triggers"
			" set expression='{12586}>{$MAX_CPU_LOAD}'"
			" where triggerid=10010"))	/* triggerid of "Processor load is too high on {HOST.NAME}" */
							/* trigger from "Template OS Linux" template */
	{
		return FAIL;
	}

	if (ZBX_DB_OK > DBexecute("update triggers"
			" set expression='{12970}>{$MAX_CPU_LOAD}'"
			" where triggerid=13541"))	/* triggerid of "Processor load is too high on {HOST.NAME}" */
							/* trigger from "Zabbix Server" host */
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_3000138(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute("update triggers"
			" set expression='{12555}>{$MAX_RUN_PROCESSES}'"
			" where triggerid=10011"))	/* triggerid of "Too many processes running on {HOST.NAME}" */
							/* trigger from "Template OS Linux" template */
	{
		return FAIL;
	}

	if (ZBX_DB_OK > DBexecute("update triggers"
			" set expression='{12968}>{$MAX_RUN_PROCESSES}'"
			" where triggerid=13539"))	/* triggerid of "Too many processes running on {HOST.NAME}" */
							/* trigger from "Zabbix Server" host */
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_3000139(void)
{
	return SUCCEED;
}

static int	DBpatch_3000140(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute("update config set refresh_unsupported=60"))
		return FAIL;

	return SUCCEED;
}

static int	DBpatch_3000141(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute("insert into rsm_status_map (id,name) values ('7','Activated'),('8','Deactivated')"))
		return FAIL;

	return SUCCEED;
}

static int	DBpatch_3000200(void)
{
	return SUCCEED;
}

static int	DBpatch_3000211(void)
{
	/* this is just a direct paste from data.tmpl, with each line quoted and properly indented */
	static const char	*const data[] = {
		"ROW   |13000    |130       |-1   |Internal error                                                                                                  |",
		"ROW   |13001    |130       |-200 |No reply from RDDS43 server (obsolete)                                                                          |",
		"ROW   |13002    |130       |-201 |Whois server returned no NS                                                                                     |",
		"ROW   |13003    |130       |-202 |No Unix timestamp (obsolete)                                                                                    |",
		"ROW   |13004    |130       |-203 |Invalid Unix timestamp (obsolete)                                                                               |",
		"ROW   |13005    |130       |-204 |No reply from RDDS80 server (obsolete)                                                                          |",
		"ROW   |13006    |130       |-205 |Cannot resolve a Whois host name (obsolete)                                                                     |",
		"ROW   |13007    |130       |-206 |no HTTP status code                                                                                             |",
		"ROW   |13008    |130       |-207 |invalid HTTP status code (obsolete)                                                                             |",
		"ROW   |13009    |130       |-222 |RDDS43 - No reply from local resolver                                                                           |",
		"ROW   |13011    |130       |-224 |RDDS43 - Expecting NOERROR RCODE but got SERVFAIL when resolving hostname                                       |",
		"ROW   |13012    |130       |-225 |RDDS43 - Expecting NOERROR RCODE but got NXDOMAIN when resolving hostname                                       |",
		"ROW   |13013    |130       |-226 |RDDS43 - Expecting NOERROR RCODE but got unexpected error when resolving hostname                               |",
		"ROW   |13014    |130       |-227 |RDDS43 - Timeout                                                                                                |",
		"ROW   |13015    |130       |-228 |RDDS43 - Error opening connection to server                                                                     |",
		"ROW   |13016    |130       |-229 |RDDS43 - Empty response                                                                                         |",
		"ROW   |13017    |130       |-250 |RDDS80 - No reply from local resolver                                                                           |",
		"ROW   |13019    |130       |-252 |RDDS80 - Expecting NOERROR RCODE but got SERVFAIL when resolving hostname                                       |",
		"ROW   |13020    |130       |-253 |RDDS80 - Expecting NOERROR RCODE but got NXDOMAIN when resolving hostname                                       |",
		"ROW   |13021    |130       |-254 |RDDS80 - Expecting NOERROR RCODE but got unexpected error when resolving hostname                               |",
		"ROW   |13022    |130       |-255 |RDDS80 - Timeout                                                                                                |",
		"ROW   |13023    |130       |-256 |RDDS80 - Error opening connection to server                                                                     |",
		"ROW   |13024    |130       |-257 |RDDS80 - Error in HTTP protocol                                                                                 |",
		"ROW   |13025    |130       |-258 |RDDS80 - Error in HTTPS protocol                                                                                |",
		"-- Error code for every assigned HTTP status code (with the exception of HTTP/200)                                                                 ",
		"-- as per: http://www.iana.org/assignments/http-status-codes/http-status-codes.xhtml                                                               ",
		"ROW   |13026    |130       |-300 |RDDS80 - Expecting HTTP status code 200 but got 100                                                             |",
		"ROW   |13027    |130       |-301 |RDDS80 - Expecting HTTP status code 200 but got 101                                                             |",
		"ROW   |13028    |130       |-302 |RDDS80 - Expecting HTTP status code 200 but got 102                                                             |",
		"ROW   |13029    |130       |-303 |RDDS80 - Expecting HTTP status code 200 but got 103                                                             |",
		"ROW   |13030    |130       |-304 |RDDS80 - Expecting HTTP status code 200 but got 201                                                             |",
		"ROW   |13031    |130       |-305 |RDDS80 - Expecting HTTP status code 200 but got 202                                                             |",
		"ROW   |13032    |130       |-306 |RDDS80 - Expecting HTTP status code 200 but got 203                                                             |",
		"ROW   |13033    |130       |-307 |RDDS80 - Expecting HTTP status code 200 but got 204                                                             |",
		"ROW   |13034    |130       |-308 |RDDS80 - Expecting HTTP status code 200 but got 205                                                             |",
		"ROW   |13035    |130       |-309 |RDDS80 - Expecting HTTP status code 200 but got 206                                                             |",
		"ROW   |13036    |130       |-310 |RDDS80 - Expecting HTTP status code 200 but got 207                                                             |",
		"ROW   |13037    |130       |-311 |RDDS80 - Expecting HTTP status code 200 but got 208                                                             |",
		"ROW   |13038    |130       |-312 |RDDS80 - Expecting HTTP status code 200 but got 226                                                             |",
		"ROW   |13039    |130       |-313 |RDDS80 - Expecting HTTP status code 200 but got 300                                                             |",
		"ROW   |13040    |130       |-314 |RDDS80 - Expecting HTTP status code 200 but got 301                                                             |",
		"ROW   |13041    |130       |-315 |RDDS80 - Expecting HTTP status code 200 but got 302                                                             |",
		"ROW   |13042    |130       |-316 |RDDS80 - Expecting HTTP status code 200 but got 303                                                             |",
		"ROW   |13043    |130       |-317 |RDDS80 - Expecting HTTP status code 200 but got 304                                                             |",
		"ROW   |13044    |130       |-318 |RDDS80 - Expecting HTTP status code 200 but got 305                                                             |",
		"ROW   |13045    |130       |-319 |RDDS80 - Expecting HTTP status code 200 but got 306                                                             |",
		"ROW   |13046    |130       |-320 |RDDS80 - Expecting HTTP status code 200 but got 307                                                             |",
		"ROW   |13047    |130       |-321 |RDDS80 - Expecting HTTP status code 200 but got 308                                                             |",
		"ROW   |13048    |130       |-322 |RDDS80 - Expecting HTTP status code 200 but got 400                                                             |",
		"ROW   |13049    |130       |-323 |RDDS80 - Expecting HTTP status code 200 but got 401                                                             |",
		"ROW   |13050    |130       |-324 |RDDS80 - Expecting HTTP status code 200 but got 402                                                             |",
		"ROW   |13051    |130       |-325 |RDDS80 - Expecting HTTP status code 200 but got 403                                                             |",
		"ROW   |13052    |130       |-326 |RDDS80 - Expecting HTTP status code 200 but got 404                                                             |",
		"ROW   |13053    |130       |-327 |RDDS80 - Expecting HTTP status code 200 but got 405                                                             |",
		"ROW   |13054    |130       |-328 |RDDS80 - Expecting HTTP status code 200 but got 406                                                             |",
		"ROW   |13055    |130       |-329 |RDDS80 - Expecting HTTP status code 200 but got 407                                                             |",
		"ROW   |13056    |130       |-330 |RDDS80 - Expecting HTTP status code 200 but got 408                                                             |",
		"ROW   |13057    |130       |-331 |RDDS80 - Expecting HTTP status code 200 but got 409                                                             |",
		"ROW   |13058    |130       |-332 |RDDS80 - Expecting HTTP status code 200 but got 410                                                             |",
		"ROW   |13059    |130       |-333 |RDDS80 - Expecting HTTP status code 200 but got 411                                                             |",
		"ROW   |13060    |130       |-334 |RDDS80 - Expecting HTTP status code 200 but got 412                                                             |",
		"ROW   |13061    |130       |-335 |RDDS80 - Expecting HTTP status code 200 but got 413                                                             |",
		"ROW   |13062    |130       |-336 |RDDS80 - Expecting HTTP status code 200 but got 414                                                             |",
		"ROW   |13063    |130       |-337 |RDDS80 - Expecting HTTP status code 200 but got 415                                                             |",
		"ROW   |13064    |130       |-338 |RDDS80 - Expecting HTTP status code 200 but got 416                                                             |",
		"ROW   |13065    |130       |-339 |RDDS80 - Expecting HTTP status code 200 but got 417                                                             |",
		"ROW   |13066    |130       |-340 |RDDS80 - Expecting HTTP status code 200 but got 421                                                             |",
		"ROW   |13067    |130       |-341 |RDDS80 - Expecting HTTP status code 200 but got 422                                                             |",
		"ROW   |13068    |130       |-342 |RDDS80 - Expecting HTTP status code 200 but got 423                                                             |",
		"ROW   |13069    |130       |-343 |RDDS80 - Expecting HTTP status code 200 but got 424                                                             |",
		"ROW   |13070    |130       |-344 |RDDS80 - Expecting HTTP status code 200 but got 426                                                             |",
		"ROW   |13071    |130       |-345 |RDDS80 - Expecting HTTP status code 200 but got 428                                                             |",
		"ROW   |13072    |130       |-346 |RDDS80 - Expecting HTTP status code 200 but got 429                                                             |",
		"ROW   |13073    |130       |-347 |RDDS80 - Expecting HTTP status code 200 but got 431                                                             |",
		"ROW   |13074    |130       |-348 |RDDS80 - Expecting HTTP status code 200 but got 451                                                             |",
		"ROW   |13075    |130       |-349 |RDDS80 - Expecting HTTP status code 200 but got 500                                                             |",
		"ROW   |13076    |130       |-350 |RDDS80 - Expecting HTTP status code 200 but got 501                                                             |",
		"ROW   |13077    |130       |-351 |RDDS80 - Expecting HTTP status code 200 but got 502                                                             |",
		"ROW   |13078    |130       |-352 |RDDS80 - Expecting HTTP status code 200 but got 503                                                             |",
		"ROW   |13079    |130       |-353 |RDDS80 - Expecting HTTP status code 200 but got 504                                                             |",
		"ROW   |13080    |130       |-354 |RDDS80 - Expecting HTTP status code 200 but got 505                                                             |",
		"ROW   |13081    |130       |-355 |RDDS80 - Expecting HTTP status code 200 but got 506                                                             |",
		"ROW   |13082    |130       |-356 |RDDS80 - Expecting HTTP status code 200 but got 507                                                             |",
		"ROW   |13083    |130       |-357 |RDDS80 - Expecting HTTP status code 200 but got 508                                                             |",
		"ROW   |13084    |130       |-358 |RDDS80 - Expecting HTTP status code 200 but got 510                                                             |",
		"ROW   |13085    |130       |-359 |RDDS80 - Expecting HTTP status code 200 but got 511                                                             |",
		"ROW   |13086    |130       |-360 |RDDS80 - Expecting HTTP status code 200 but got unexpected status code                                          |",
		NULL
	};
	int			i;

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute("delete from mappings where valuemapid=130"))	/* valuemapid of "RSM RDDS rtt" */
		return FAIL;

	for (i = 0; NULL != data[i]; i++)
	{
		zbx_uint64_t	mappingid, valuemapid;
		char		*value = NULL, *newvalue = NULL, *value_esc, *newvalue_esc;

		if (0 == strncmp(data[i], "--", ZBX_CONST_STRLEN("--")))
			continue;

		if (4 != sscanf(data[i], "ROW |" ZBX_FS_UI64 " |" ZBX_FS_UI64 " |%m[^|]|%m[^|]|",
				&mappingid, &valuemapid, &value, &newvalue))
		{
			zabbix_log(LOG_LEVEL_CRIT, "failed to parse the following line:\n%s", data[i]);
			zbx_free(value);
			zbx_free(newvalue);
			return FAIL;
		}

		zbx_rtrim(value, ZBX_WHITESPACE);
		zbx_rtrim(newvalue, ZBX_WHITESPACE);

		/* NOTE: to keep it simple assume that data does not contain sequences "&pipe;", "&eol;" or "&bsn;" */

		value_esc = zbx_db_dyn_escape_string(value);
		newvalue_esc = zbx_db_dyn_escape_string(newvalue);
		zbx_free(value);
		zbx_free(newvalue);

		if (ZBX_DB_OK > DBexecute("insert into mappings (mappingid,valuemapid,value,newvalue)"
				" values (" ZBX_FS_UI64 "," ZBX_FS_UI64 ",'%s','%s')",
				mappingid, valuemapid, value_esc, newvalue_esc))
		{
			zbx_free(value_esc);
			zbx_free(newvalue_esc);
			return FAIL;
		}

		zbx_free(value_esc);
		zbx_free(newvalue_esc);
	}

	return SUCCEED;
}

static int	DBpatch_3000202(void)
{
	/* this is just a direct paste from data.tmpl, with each line quoted and properly indented */
	static const char	*const data[] = {
		"ROW   |12000    |120       |-1   |Internal error                                                                                                  |",
		"ROW   |12001    |120       |-200 |DNS UDP - No reply from name server                                                                             |",
		"ROW   |12002    |120       |-201 |Invalid reply from Name Server (obsolete)                                                                       |",
		"ROW   |12003    |120       |-202 |No UNIX timestamp (obsolete)                                                                                    |",
		"ROW   |12004    |120       |-203 |Invalid UNIX timestamp (obsolete)                                                                               |",
		"ROW   |12005    |120       |-204 |DNSSEC error (obsolete)                                                                                         |",
		"ROW   |12006    |120       |-205 |No reply from resolver (obsolete)                                                                               |",
		"ROW   |12007    |120       |-206 |Keyset is not valid (obsolete)                                                                                  |",
		"ROW   |12008    |120       |-207 |DNS UDP - Expecting DNS CLASS IN but got CHAOS                                                                  |",
		"ROW   |12009    |120       |-208 |DNS UDP - Expecting DNS CLASS IN but got HESIOD                                                                 |",
		"ROW   |12010    |120       |-209 |DNS UDP - Expecting DNS CLASS IN but got something different than IN, CHAOS or HESIOD                           |",
		"ROW   |12011    |120       |-210 |DNS UDP - Header section incomplete                                                                             |",
		"ROW   |12012    |120       |-211 |DNS UDP - Question section incomplete                                                                           |",
		"ROW   |12013    |120       |-212 |DNS UDP - Answer section incomplete                                                                             |",
		"ROW   |12014    |120       |-213 |DNS UDP - Authority section incomplete                                                                          |",
		"ROW   |12015    |120       |-214 |DNS UDP - Additional section incomplete                                                                         |",
		"ROW   |12016    |120       |-215 |DNS UDP - Malformed DNS response                                                                                |",
		"ROW   |12017    |120       |-250 |DNS UDP - Querying for a non existent domain - AA flag not present in response                                  |",
		"ROW   |12018    |120       |-251 |DNS UDP - Querying for a non existent domain - Domain name being queried not present in question section        |",
		"-- Error code for every assigned, non private DNS RCODE (with the exception of RCODE/NXDOMAIN)                                                     ",
		"-- as per: https://www.iana.org/assignments/dns-parameters/dns-parameters.xhtml                                                                    ",
		"ROW   |12019    |120       |-252 |DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got NOERROR                         |",
		"ROW   |12020    |120       |-253 |DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got FORMERR                         |",
		"ROW   |12021    |120       |-254 |DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got SERVFAIL                        |",
		"ROW   |12022    |120       |-255 |DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got NOTIMP                          |",
		"ROW   |12023    |120       |-256 |DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got REFUSED                         |",
		"ROW   |12024    |120       |-257 |DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got YXDOMAIN                        |",
		"ROW   |12025    |120       |-258 |DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got YXRRSET                         |",
		"ROW   |12026    |120       |-259 |DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got NXRRSET                         |",
		"ROW   |12027    |120       |-260 |DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got NOTAUTH                         |",
		"ROW   |12028    |120       |-261 |DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got NOTZONE                         |",
		"ROW   |12029    |120       |-262 |DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got BADVERS or BADSIG               |",
		"ROW   |12030    |120       |-263 |DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got BADKEY                          |",
		"ROW   |12031    |120       |-264 |DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got BADTIME                         |",
		"ROW   |12032    |120       |-265 |DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got BADMODE                         |",
		"ROW   |12033    |120       |-266 |DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got BADNAME                         |",
		"ROW   |12034    |120       |-267 |DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got BADALG                          |",
		"ROW   |12035    |120       |-268 |DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got BADTRUNC                        |",
		"ROW   |12036    |120       |-269 |DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got BADCOOKIE                       |",
		"ROW   |12037    |120       |-270 |DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got unexpected                      |",
		"ROW   |12038    |120       |-400 |DNS UDP - No reply from local resolver                                                                          |",
		"ROW   |12039    |120       |-401 |DNS UDP - No AD bit from local resolver                                                                         |",
		"ROW   |12040    |120       |-402 |DNS UDP - Expecting NOERROR RCODE but got SERVFAIL from local resolver                                          |",
		"ROW   |12041    |120       |-403 |DNS UDP - Expecting NOERROR RCODE but got NXDOMAIN from local resolver                                          |",
		"ROW   |12042    |120       |-404 |DNS UDP - Expecting NOERROR RCODE but got unexpecting from local resolver                                       |",
		"ROW   |12043    |120       |-405 |DNS UDP - Unknown cryptographic algorithm                                                                       |",
		"ROW   |12044    |120       |-406 |DNS UDP - Cryptographic algorithm not implemented                                                               |",
		"ROW   |12045    |120       |-407 |DNS UDP - No RRSIGs where found in any section, and the TLD has the DNSSEC flag enabled                         |",
		"ROW   |12046    |120       |-410 |DNS UDP - The signature does not cover this RRset                                                               |",
		"ROW   |12047    |120       |-414 |DNS UDP - The RRSIG found is not signed by a DNSKEY from the KEYSET of the TLD                                  |",
		"ROW   |12048    |120       |-415 |DNS UDP - Bogus DNSSEC signature                                                                                |",
		"ROW   |12049    |120       |-416 |DNS UDP - DNSSEC signature has expired                                                                          |",
		"ROW   |12050    |120       |-417 |DNS UDP - DNSSEC signature not incepted yet                                                                     |",
		"ROW   |12051    |120       |-418 |DNS UDP - DNSSEC signature has expiration date earlier than inception date                                      |",
		"ROW   |12052    |120       |-419 |DNS UDP - Error in NSEC3 denial of existence proof                                                              |",
		"ROW   |12053    |120       |-421 |DNS UDP - Iterations count for NSEC3 record higher than maximum                                                 |",
		"ROW   |12054    |120       |-422 |DNS UDP - RR not covered by the given NSEC RRs                                                                  |",
		"ROW   |12055    |120       |-423 |DNS UDP - Wildcard not covered by the given NSEC RRs                                                            |",
		"ROW   |12056    |120       |-425 |DNS UDP - The RRSIG has too few RDATA fields                                                                    |",
		"ROW   |12057    |120       |-426 |DNS UDP - The DNSKEY has too few RDATA fields                                                                   |",
		"ROW   |12058    |120       |-427 |DNS UDP - Malformed DNSSEC response                                                                             |",
		"ROW   |12059    |120       |-428 |DNS UDP - The TLD is configured as DNSSEC-enabled, but no DNSKEY was found in the apex                          |",
		"ROW   |12060    |120       |-600 |DNS TCP - Timeout reply from name server                                                                        |",
		"ROW   |12061    |120       |-601 |DNS TCP - Error opening connection to name server                                                               |",
		"ROW   |12062    |120       |-607 |DNS TCP - Expecting DNS CLASS IN but got CHAOS                                                                  |",
		"ROW   |12063    |120       |-608 |DNS TCP - Expecting DNS CLASS IN but got HESIOD                                                                 |",
		"ROW   |12064    |120       |-609 |DNS TCP - Expecting DNS CLASS IN but got something different than IN, CHAOS or HESIOD                           |",
		"ROW   |12065    |120       |-610 |DNS TCP - Header section incomplete                                                                             |",
		"ROW   |12066    |120       |-611 |DNS TCP - Question section incomplete                                                                           |",
		"ROW   |12067    |120       |-612 |DNS TCP - Answer section incomplete                                                                             |",
		"ROW   |12068    |120       |-613 |DNS TCP - Authority section incomplete                                                                          |",
		"ROW   |12069    |120       |-614 |DNS TCP - Additional section incomplete                                                                         |",
		"ROW   |12070    |120       |-615 |DNS TCP - Malformed DNS response                                                                                |",
		"ROW   |12071    |120       |-650 |DNS TCP - Querying for a non existent domain - AA flag not present in response                                  |",
		"ROW   |12072    |120       |-651 |DNS TCP - Querying for a non existent domain - Domain name being queried not present in question section        |",
		"-- Error code for every assigned, non private DNS RCODE (with the exception of RCODE/NXDOMAIN)                                                     ",
		"-- as per: https://www.iana.org/assignments/dns-parameters/dns-parameters.xhtml                                                                    ",
		"ROW   |12073    |120       |-652 |DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got NOERROR                         |",
		"ROW   |12074    |120       |-653 |DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got FORMERR                         |",
		"ROW   |12075    |120       |-654 |DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got SERVFAIL                        |",
		"ROW   |12076    |120       |-655 |DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got NOTIMP                          |",
		"ROW   |12077    |120       |-656 |DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got REFUSED                         |",
		"ROW   |12078    |120       |-657 |DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got YXDOMAIN                        |",
		"ROW   |12079    |120       |-658 |DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got YXRRSET                         |",
		"ROW   |12080    |120       |-659 |DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got NXRRSET                         |",
		"ROW   |12081    |120       |-660 |DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got NOTAUTH                         |",
		"ROW   |12082    |120       |-661 |DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got NOTZONE                         |",
		"ROW   |12083    |120       |-662 |DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got BADVERS or BADSIG               |",
		"ROW   |12084    |120       |-663 |DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got BADKEY                          |",
		"ROW   |12085    |120       |-664 |DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got BADTIME                         |",
		"ROW   |12086    |120       |-665 |DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got BADMODE                         |",
		"ROW   |12087    |120       |-666 |DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got BADNAME                         |",
		"ROW   |12088    |120       |-667 |DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got BADALG                          |",
		"ROW   |12089    |120       |-668 |DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got BADTRUNC                        |",
		"ROW   |12090    |120       |-669 |DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got BADCOOKIE                       |",
		"ROW   |12091    |120       |-670 |DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got unexpected                      |",
		"ROW   |12092    |120       |-800 |DNS TCP - No reply from local resolver                                                                          |",
		"ROW   |12093    |120       |-801 |DNS TCP - No AD bit from local resolver                                                                         |",
		"ROW   |12094    |120       |-802 |DNS TCP - Expecting NOERROR RCODE but got SERVFAIL from local resolver                                          |",
		"ROW   |12095    |120       |-803 |DNS TCP - Expecting NOERROR RCODE but got NXDOMAIN from local resolver                                          |",
		"ROW   |12096    |120       |-804 |DNS TCP - Expecting NOERROR RCODE but got unexpecting from local resolver                                       |",
		"ROW   |12097    |120       |-805 |DNS TCP - Unknown cryptographic algorithm                                                                       |",
		"ROW   |12098    |120       |-806 |DNS TCP - Cryptographic algorithm not implemented                                                               |",
		"ROW   |12099    |120       |-807 |DNS TCP - No RRSIGs where found in any section, and the TLD has the DNSSEC flag enabled                         |",
		"ROW   |12100    |120       |-810 |DNS TCP - The signature does not cover this RRset                                                               |",
		"ROW   |12101    |120       |-814 |DNS TCP - The RRSIG found is not signed by a DNSKEY from the KEYSET of the TLD                                  |",
		"ROW   |12102    |120       |-815 |DNS TCP - Bogus DNSSEC signature                                                                                |",
		"ROW   |12103    |120       |-816 |DNS TCP - DNSSEC signature has expired                                                                          |",
		"ROW   |12104    |120       |-817 |DNS TCP - DNSSEC signature not incepted yet                                                                     |",
		"ROW   |12105    |120       |-818 |DNS TCP - DNSSEC signature has expiration date earlier than inception date                                      |",
		"ROW   |12106    |120       |-819 |DNS TCP - Error in NSEC3 denial of existence proof                                                              |",
		"ROW   |12107    |120       |-821 |DNS TCP - Iterations count for NSEC3 record higher than maximum                                                 |",
		"ROW   |12108    |120       |-822 |DNS TCP - RR not covered by the given NSEC RRs                                                                  |",
		"ROW   |12109    |120       |-823 |DNS TCP - Wildcard not covered by the given NSEC RRs                                                            |",
		"ROW   |12110    |120       |-825 |DNS TCP - The RRSIG has too few RDATA fields                                                                    |",
		"ROW   |12111    |120       |-826 |DNS TCP - The DNSKEY has too few RDATA fields                                                                   |",
		"ROW   |12112    |120       |-827 |DNS TCP - Malformed DNSSEC response                                                                             |",
		"ROW   |12113    |120       |-828 |DNS TCP - The TLD is configured as DNSSEC-enabled, but no DNSKEY was found in the apex                          |",
		NULL
	};
	int			i;

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute("delete from mappings where valuemapid=120"))	/* valuemapid of "RSM DNS rtt" */
		return FAIL;

	for (i = 0; NULL != data[i]; i++)
	{
		zbx_uint64_t	mappingid, valuemapid;
		char		*value = NULL, *newvalue = NULL, *value_esc, *newvalue_esc;

		if (0 == strncmp(data[i], "--", ZBX_CONST_STRLEN("--")))
			continue;

		if (4 != sscanf(data[i], "ROW |" ZBX_FS_UI64 " |" ZBX_FS_UI64 " |%m[^|]|%m[^|]|",
				&mappingid, &valuemapid, &value, &newvalue))
		{
			zabbix_log(LOG_LEVEL_CRIT, "failed to parse the following line:\n%s", data[i]);
			zbx_free(value);
			zbx_free(newvalue);
			return FAIL;
		}

		zbx_rtrim(value, ZBX_WHITESPACE);
		zbx_rtrim(newvalue, ZBX_WHITESPACE);

		/* NOTE: to keep it simple assume that data does not contain sequences "&pipe;", "&eol;" or "&bsn;" */

		value_esc = zbx_db_dyn_escape_string(value);
		newvalue_esc = zbx_db_dyn_escape_string(newvalue);
		zbx_free(value);
		zbx_free(newvalue);

		if (ZBX_DB_OK > DBexecute("insert into mappings (mappingid,valuemapid,value,newvalue)"
				" values (" ZBX_FS_UI64 "," ZBX_FS_UI64 ",'%s','%s')",
				mappingid, valuemapid, value_esc, newvalue_esc))
		{
			zbx_free(value_esc);
			zbx_free(newvalue_esc);
			return FAIL;
		}

		zbx_free(value_esc);
		zbx_free(newvalue_esc);
	}

	return SUCCEED;
}

static int	DBpatch_3000203(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute("insert into mappings (mappingid,valuemapid,value,newvalue) values"
			" (11002,110,'2','Up-inconclusive-no-data'),"
			" (11003,110,'3','Up-inconclusive-no-probes')"))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_3000204(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute("insert into rsm_status_map (id,name) values"
			" (9,'Up-inconclusive-no-data'),"
			" (10,'Up-inconclusive-no-probes')"))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_3000205(void)
{
#define RESERVE_GLOBALMACROID									\
		"update globalmacro"								\
		" set globalmacroid=(select nextid from ("					\
			"select max(globalmacroid)+1 as nextid from globalmacro) as tmp)"	\
		" where globalmacroid=%u"

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute(RESERVE_GLOBALMACROID, 66))
		return FAIL;

	if (ZBX_DB_OK > DBexecute(
			"insert into globalmacro (globalmacroid,macro,value)"
			" values (66,'{$PROBE.INTERNAL.ERROR.INTERVAL}','5m')"))
	{
		return FAIL;
	}

	if (ZBX_DB_OK > DBexecute("delete from ids where table_name='globalmacro'"))
		return FAIL;

	return SUCCEED;

#undef RESERVE_GLOBALMACROID
}

static int	DBpatch_3000206(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	/* create "Template Probe Errors" */
	if (ZBX_DB_OK > DBexecute(
			"insert into hosts (hostid,proxy_hostid,host,status,"
				"ipmi_authtype,ipmi_privilege,ipmi_username,ipmi_password,name,flags,templateid,"
				"description,tls_connect,tls_accept,tls_issuer,tls_subject,tls_psk_identity,tls_psk)"
			" values ('99990',NULL,'Template Probe Errors','3',"
				"'0','2','','','Template Probe Errors','0',NULL,"
				"'','1','1','','','','')"))
	{
		return FAIL;
	}

	/* add it to "Templates" host group */
	if (ZBX_DB_OK > DBexecute("insert into hosts_groups (hostgroupid,hostid,groupid) values ('999','99990','1')"))
		return FAIL;

	/* add "Internal errors" application */
	if (ZBX_DB_OK > DBexecute(
			"insert into applications (applicationid,hostid,name,flags)"
			" values ('999','99990','Internal errors','0')"))
	{
		return FAIL;
	}

	/* add "Internal error rate" item */
	if (ZBX_DB_OK > DBexecute(
			"insert into items (itemid,type,snmp_community,snmp_oid,hostid,name,key_,delay,history,trends,"
				"status,value_type,trapper_hosts,units,multiplier,delta,"
				"snmpv3_securityname,snmpv3_securitylevel,snmpv3_authpassphrase,snmpv3_privpassphrase,"
				"formula,logtimefmt,templateid,valuemapid,delay_flex,params,ipmi_sensor,data_type,"
				"authtype,username,password,publickey,privatekey,flags,interfaceid,port,description,"
				"inventory_link,lifetime,snmpv3_authprotocol,snmpv3_privprotocol,snmpv3_contextname,evaltype)"
			" values ('99990','3','','','99990','Internal error rate','rsm.errors','60','90','365',"
				"'0','0','','','0','1',"
				"'','0','','',"
				"'1','',NULL,NULL,'','','','0',"
				"'0','','','','','0',NULL,'','',"
				"'0','30','0','0','','0')"))
	{
		return FAIL;
	}

	/* put item into application */
	if (ZBX_DB_OK > DBexecute(
			"insert into items_applications (itemappid,applicationid,itemid)"
			" values ('99990','999','99990')"))
	{
		return FAIL;
	}

	/* add triggers... */
	if (ZBX_DB_OK > DBexecute(
			"insert into triggers (triggerid,expression,description,url,status,priority,comments,templateid,type,flags)"
			" values"
				" ('99990','{99990}>0','Internal errors happening for {$PROBE.INTERNAL.ERROR.INTERVAL}','','0','4','',NULL,'0','0'),"
				" ('99991','{99991}>0','Internal errors happening','','0','2','',NULL,'0','0')"))
	{
		return FAIL;
	}

	/* ...and trigger functions */
	if (ZBX_DB_OK > DBexecute(
			"insert into functions (functionid,itemid,triggerid,function,parameter)"
			" values"
				" ('99990','99990','99990','min','{$PROBE.INTERNAL.ERROR.INTERVAL}'),"
				" ('99991','99990','99991','last','')"))
	{
		return FAIL;
	}

	/* add dependency */

#define RESERVE_TRIGGERDEPID									\
		"update trigger_depends"							\
		" set triggerdepid=(select nextid from ("					\
			"select max(triggerdepid)+1 as nextid from trigger_depends) as tmp)"	\
		" where triggerdepid=%u"

	if (ZBX_DB_OK > DBexecute(RESERVE_TRIGGERDEPID, 1))
		return FAIL;

	if (ZBX_DB_OK > DBexecute(
			"insert into trigger_depends (triggerdepid,triggerid_down,triggerid_up)"
			" values ('1','99991','99990')"))
	{
		return FAIL;
	}

	if (ZBX_DB_OK > DBexecute("delete from ids where table_name='trigger_depends'"))
		return FAIL;

#undef RESERVE_TRIGGERDEPID

	return SUCCEED;
}

static int	DBpatch_3000210(void)
{
	DB_RESULT	result;
	DB_ROW		row;
	int		ret = SUCCEED;

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	result = DBselect("select h.hostid from hosts h,hosts_groups hg where h.hostid=hg.hostid and hg.groupid=120");

	if (NULL == result)
		return FAIL;

	while (NULL != (row = DBfetch(result)) && SUCCEED == ret)
	{
		zbx_uint64_t		hostid;
		zbx_vector_uint64_t	templateids;
		char			*error;

		ZBX_STR2UINT64(hostid, row[0]);			/* hostid of probe host */
		zbx_vector_uint64_create(&templateids);
		zbx_vector_uint64_reserve(&templateids, 1);
		zbx_vector_uint64_append(&templateids, 99990);	/* hostid of "Template Probe Errors" */

		if (SUCCEED != (ret = DBcopy_template_elements(hostid, &templateids, &error)))
		{
			zabbix_log(LOG_LEVEL_CRIT, "cannot link template(s) %s", error);
			zbx_free(error);
		}

		zbx_vector_uint64_destroy(&templateids);
	}

	DBfree_result(result);

	return ret;
}

static const action_t	two_more_actions[] = {
	{115,	"Probes",		3600,
		"probes#{TRIGGER.STATUS}#{HOST.NAME1}#{ITEM.NAME1}#{ITEM.VALUE1}",	"{EVENT.DATE} {EVENT.TIME} UTC",
		1,
		"probes#{TRIGGER.STATUS}#{HOST.NAME1}#{ITEM.NAME1}#{ITEM.VALUE1}",	"{EVENT.RECOVERY.DATE} {EVENT.RECOVERY.TIME} UTC",
		{
			{115,	OP_MESSAGE,	{.opmessage = {1,	"",	"",	10,
				{
					{115,	OP_MESSAGE_USR,	{.userid = 100}},
					{0}
				}
			}}},
			{0}
		},
		{
			{115,	16,	7,	""},
			{116,	5,	0,	"1"},
			{117,	4,	5,	"2"},
			{118,	0,	0,	"120"},
			{0}
		}
	},
	{140,	"Probes-Knockout",	3600,
		"probes#{TRIGGER.STATUS}#{HOST.NAME1}#{ITEM.NAME1}#{ITEM.VALUE1}",	"{EVENT.DATE} {EVENT.TIME} UTC",
		1,
		"probes#{TRIGGER.STATUS}#{HOST.NAME1}#{ITEM.NAME1}#{ITEM.VALUE1}",	"{EVENT.RECOVERY.DATE} {EVENT.RECOVERY.TIME} UTC",
		{
			{140,	OP_COMMAND,	{.opcommand = {0,	1,	"",	0,	"",	"",	"",	"",
				"/opt/zabbix/scripts/probe-manual.pl --probe \'{HOST.HOST}\' --set 0",
				{
					{140,	OP_COMMAND_CURRENT_HOST},
					{0}
				}
			}}},
			{0}
		},
		{
			{140,	16,	7,	""},
			{141,	5,	0,	"1"},
			{142,	4,	5,	"2"},
			{143,	0,	0,	"120"},
			{144,	2,	0,	"99990"},
			{0}
		}
	},
	{0}
};

static int	DBpatch_3000208(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	return add_actions(two_more_actions);
}

static int	DBpatch_3000212(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute("delete from mappings where mappingid in (12053,12057,12107,12111)"))
		return FAIL;

	return SUCCEED;
}

static int	DBpatch_3000213(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute("insert into valuemaps (valuemapid,name) values ('135','RSM RDAP rtt')"))
		return FAIL;

	return SUCCEED;
}

static int	DBpatch_3000214(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute(
			"insert into hosts ("
				"hostid,proxy_hostid,host,status,ipmi_authtype,ipmi_privilege,"
				"ipmi_username,ipmi_password,name,flags,templateid,description,"
				"tls_connect,tls_accept,tls_issuer,tls_subject,tls_psk_identity,tls_psk"
			")"
			" values ("
				"'99980',NULL,'Template RDAP','3','0','2',"
				"'','','Template RDAP','0',NULL,'',"
				"'1','1','','','',''"
			")"))
	{
		return FAIL;
	}

	if (ZBX_DB_OK > DBexecute(
			"insert into applications (applicationid,hostid,name,flags) values ('998','99980','RDAP','0')"))
	{
		return FAIL;
	}

	if (ZBX_DB_OK > DBexecute(
			"insert into items ("
				"itemid,type,snmp_community,snmp_oid,hostid,name,"
				"key_,"
				"delay,history,trends,status,value_type,trapper_hosts,units,multiplier,delta,"
				"snmpv3_securityname,snmpv3_securitylevel,snmpv3_authpassphrase,snmpv3_privpassphrase,"
				"formula,logtimefmt,templateid,valuemapid,delay_flex,params,ipmi_sensor,data_type,"
				"authtype,username,password,publickey,privatekey,flags,interfaceid,port,"
				"description,"
				"inventory_link,lifetime,snmpv3_authprotocol,snmpv3_privprotocol,snmpv3_contextname,evaltype"
			")"
			" values ("
				"'99980','3','','','99980','RDAP availability',"
				"'rdap["
					"{$RSM.TLD},"
					"{$RDAP.TEST.DOMAIN},"
					"{$RDAP.BASE.URL},"
					"{$RSM.RDDS.MAXREDIRS},"
					"{$RSM.RDDS.RTT.HIGH},"
					"{$RDAP.TLD.ENABLED},"
					"{$RSM.RDDS.ENABLED},"
					"{$RSM.IP4.ENABLED},"
					"{$RSM.IP6.ENABLED},"
					"{$RSM.RESOLVER}"
					"]',"
				"'300','7','365','0','3','','','0','0',"
				"'','0','','',"
				"'1','',NULL,'1','','','','0',"
				"'0','','','','','0',NULL,'',"
				"'Status of Registration Data Access Protocol service.',"
				"'0','0','0','0','','0'"
			"),"
			"("
				"'99981','2','','','99980','RDAP IP',"
				"'rdap.ip',"
				"'0','7','365','0','1','','','0','0',"
				"'','0','','',"
				"'1','',NULL,NULL,'','','','0',"
				"'0','','','','','0',NULL,'',"
				"'IP address of Registration Data Access Protocol service provider domain used to perform the test.',"
				"'0','0','0','0','','0'"
			"),"
			"("
				"'99982','2','','','99980','RDAP RTT',"
				"'rdap.rtt',"
				"'0','7','365','0','0','','ms','0','0',"
				"'','0','','',"
				"'1','',NULL,'135','','','','0',"
				"'0','','','','','0',NULL,'',"
				"'Round-Trip Time of Registration Data Access Protocol service test.',"
				"'0','0','0','0','','0'"
			"),"
			"("
				"'99983','15','','','99980','RDAP enabled/disabled','rdap.enabled','60','7','365',"
				"'0','3','','','0','0',"
				"'','0','','',"
				"'1','',NULL,NULL,'','{$RDAP.TLD.ENABLED}','','0',"
				"'0','','','','','0',NULL,'',"
				"'History of Registration Data Access Protocol being enabled or disabled.',"
				"'0','0','0','0','','0'"
			")"))
	{
		return FAIL;
	}

	if (ZBX_DB_OK > DBexecute(
			"insert into items_applications (itemappid,applicationid,itemid)"
			" values ('99980','998','99980'),('99981','998','99981'),('99982','998','99982'),(99983,998,99983)"))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_3000215(void)
{
	/* this is just a direct paste from data.tmpl, with each line quoted and properly indented */
	static const char	*const data[] = {
		"ROW   |13500    |135       |-1   |Internal error                                                                                                  |",
		"ROW   |13501    |135       |-100 |The TLD is not listed in the Bootstrap Service Registry for Domain Name Space                                   |",
		"ROW   |13502    |135       |-101 |The RDAP base URL obtained from Bootstrap Service Registry for Domain Name Space does not use HTTPS             |",
		"ROW   |13503    |135       |-200 |RDAP - No reply from local resolver                                                                             |",
		"ROW   |13504    |135       |-201 |RDAP - No AD bit from local resolver                                                                            |",
		"ROW   |13505    |135       |-202 |RDAP - Expecting NOERROR RCODE but got SERVFAIL when resolving hostname                                         |",
		"ROW   |13506    |135       |-203 |RDAP - Expecting NOERROR RCODE but got NXDOMAIN when resolving hostname                                         |",
		"ROW   |13507    |135       |-204 |RDAP - Expecting NOERROR RCODE but got unexpected error when resolving hostname                                 |",
		"ROW   |13508    |135       |-205 |RDAP - Timeout                                                                                                  |",
		"ROW   |13509    |135       |-206 |RDAP - Error opening connection to server                                                                       |",
		"ROW   |13510    |135       |-207 |RDAP - Invalid JSON format in response                                                                          |",
		"ROW   |13511    |135       |-208 |RDAP - ldhName member not found in response                                                                     |",
		"ROW   |13512    |135       |-209 |RDAP - ldhName member doesn't match query in response                                                           |",
		"ROW   |13513    |135       |-213 |RDAP - Error in HTTP protocol                                                                                   |",
		"ROW   |13514    |135       |-214 |RDAP - Error in HTTPS protocol                                                                                  |",
		"-- Error code for every assigned HTTP status code (with the exception of HTTP/200)                                                                 ",
		"-- as per: http://www.iana.org/assignments/http-status-codes/http-status-codes.xhtml                                                               ",
		"ROW   |13515    |135       |-250 |RDAP - Expecting HTTP status code 200 but got 100                                                               |",
		"ROW   |13516    |135       |-251 |RDAP - Expecting HTTP status code 200 but got 101                                                               |",
		"ROW   |13517    |135       |-252 |RDAP - Expecting HTTP status code 200 but got 102                                                               |",
		"ROW   |13518    |135       |-253 |RDAP - Expecting HTTP status code 200 but got 103                                                               |",
		"ROW   |13519    |135       |-254 |RDAP - Expecting HTTP status code 200 but got 201                                                               |",
		"ROW   |13520    |135       |-255 |RDAP - Expecting HTTP status code 200 but got 202                                                               |",
		"ROW   |13521    |135       |-256 |RDAP - Expecting HTTP status code 200 but got 203                                                               |",
		"ROW   |13522    |135       |-257 |RDAP - Expecting HTTP status code 200 but got 204                                                               |",
		"ROW   |13523    |135       |-258 |RDAP - Expecting HTTP status code 200 but got 205                                                               |",
		"ROW   |13524    |135       |-259 |RDAP - Expecting HTTP status code 200 but got 206                                                               |",
		"ROW   |13525    |135       |-260 |RDAP - Expecting HTTP status code 200 but got 207                                                               |",
		"ROW   |13526    |135       |-261 |RDAP - Expecting HTTP status code 200 but got 208                                                               |",
		"ROW   |13527    |135       |-262 |RDAP - Expecting HTTP status code 200 but got 226                                                               |",
		"ROW   |13528    |135       |-263 |RDAP - Expecting HTTP status code 200 but got 300                                                               |",
		"ROW   |13529    |135       |-264 |RDAP - Expecting HTTP status code 200 but got 301                                                               |",
		"ROW   |13530    |135       |-265 |RDAP - Expecting HTTP status code 200 but got 302                                                               |",
		"ROW   |13531    |135       |-266 |RDAP - Expecting HTTP status code 200 but got 303                                                               |",
		"ROW   |13532    |135       |-267 |RDAP - Expecting HTTP status code 200 but got 304                                                               |",
		"ROW   |13533    |135       |-268 |RDAP - Expecting HTTP status code 200 but got 305                                                               |",
		"ROW   |13534    |135       |-269 |RDAP - Expecting HTTP status code 200 but got 306                                                               |",
		"ROW   |13535    |135       |-270 |RDAP - Expecting HTTP status code 200 but got 307                                                               |",
		"ROW   |13536    |135       |-271 |RDAP - Expecting HTTP status code 200 but got 308                                                               |",
		"ROW   |13537    |135       |-272 |RDAP - Expecting HTTP status code 200 but got 400                                                               |",
		"ROW   |13538    |135       |-273 |RDAP - Expecting HTTP status code 200 but got 401                                                               |",
		"ROW   |13539    |135       |-274 |RDAP - Expecting HTTP status code 200 but got 402                                                               |",
		"ROW   |13540    |135       |-275 |RDAP - Expecting HTTP status code 200 but got 403                                                               |",
		"ROW   |13541    |135       |-276 |RDAP - Expecting HTTP status code 200 but got 404                                                               |",
		"ROW   |13542    |135       |-277 |RDAP - Expecting HTTP status code 200 but got 405                                                               |",
		"ROW   |13543    |135       |-278 |RDAP - Expecting HTTP status code 200 but got 406                                                               |",
		"ROW   |13544    |135       |-279 |RDAP - Expecting HTTP status code 200 but got 407                                                               |",
		"ROW   |13545    |135       |-280 |RDAP - Expecting HTTP status code 200 but got 408                                                               |",
		"ROW   |13546    |135       |-281 |RDAP - Expecting HTTP status code 200 but got 409                                                               |",
		"ROW   |13547    |135       |-282 |RDAP - Expecting HTTP status code 200 but got 410                                                               |",
		"ROW   |13548    |135       |-283 |RDAP - Expecting HTTP status code 200 but got 411                                                               |",
		"ROW   |13549    |135       |-284 |RDAP - Expecting HTTP status code 200 but got 412                                                               |",
		"ROW   |13550    |135       |-285 |RDAP - Expecting HTTP status code 200 but got 413                                                               |",
		"ROW   |13551    |135       |-286 |RDAP - Expecting HTTP status code 200 but got 414                                                               |",
		"ROW   |13552    |135       |-287 |RDAP - Expecting HTTP status code 200 but got 415                                                               |",
		"ROW   |13553    |135       |-288 |RDAP - Expecting HTTP status code 200 but got 416                                                               |",
		"ROW   |13554    |135       |-289 |RDAP - Expecting HTTP status code 200 but got 417                                                               |",
		"ROW   |13555    |135       |-290 |RDAP - Expecting HTTP status code 200 but got 421                                                               |",
		"ROW   |13556    |135       |-291 |RDAP - Expecting HTTP status code 200 but got 422                                                               |",
		"ROW   |13557    |135       |-292 |RDAP - Expecting HTTP status code 200 but got 423                                                               |",
		"ROW   |13558    |135       |-293 |RDAP - Expecting HTTP status code 200 but got 424                                                               |",
		"ROW   |13559    |135       |-294 |RDAP - Expecting HTTP status code 200 but got 426                                                               |",
		"ROW   |13560    |135       |-295 |RDAP - Expecting HTTP status code 200 but got 428                                                               |",
		"ROW   |13561    |135       |-296 |RDAP - Expecting HTTP status code 200 but got 429                                                               |",
		"ROW   |13562    |135       |-297 |RDAP - Expecting HTTP status code 200 but got 431                                                               |",
		"ROW   |13563    |135       |-298 |RDAP - Expecting HTTP status code 200 but got 451                                                               |",
		"ROW   |13564    |135       |-299 |RDAP - Expecting HTTP status code 200 but got 500                                                               |",
		"ROW   |13565    |135       |-300 |RDAP - Expecting HTTP status code 200 but got 501                                                               |",
		"ROW   |13566    |135       |-301 |RDAP - Expecting HTTP status code 200 but got 502                                                               |",
		"ROW   |13567    |135       |-302 |RDAP - Expecting HTTP status code 200 but got 503                                                               |",
		"ROW   |13568    |135       |-303 |RDAP - Expecting HTTP status code 200 but got 504                                                               |",
		"ROW   |13569    |135       |-304 |RDAP - Expecting HTTP status code 200 but got 505                                                               |",
		"ROW   |13570    |135       |-305 |RDAP - Expecting HTTP status code 200 but got 506                                                               |",
		"ROW   |13571    |135       |-306 |RDAP - Expecting HTTP status code 200 but got 507                                                               |",
		"ROW   |13572    |135       |-307 |RDAP - Expecting HTTP status code 200 but got 508                                                               |",
		"ROW   |13573    |135       |-308 |RDAP - Expecting HTTP status code 200 but got 510                                                               |",
		"ROW   |13574    |135       |-309 |RDAP - Expecting HTTP status code 200 but got 511                                                               |",
		"ROW   |13575    |135       |-310 |RDAP - Expecting HTTP status code 200 but got unexpected status code                                            |",
		NULL
	};
	int			i;

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	for (i = 0; NULL != data[i]; i++)
	{
		zbx_uint64_t	mappingid, valuemapid;
		char		*value = NULL, *newvalue = NULL, *value_esc, *newvalue_esc;

		if (0 == strncmp(data[i], "--", ZBX_CONST_STRLEN("--")))
			continue;

		if (4 != sscanf(data[i], "ROW |" ZBX_FS_UI64 " |" ZBX_FS_UI64 " |%m[^|]|%m[^|]|",
				&mappingid, &valuemapid, &value, &newvalue))
		{
			zabbix_log(LOG_LEVEL_CRIT, "failed to parse the following line:\n%s", data[i]);
			zbx_free(value);
			zbx_free(newvalue);
			return FAIL;
		}

		zbx_rtrim(value, ZBX_WHITESPACE);
		zbx_rtrim(newvalue, ZBX_WHITESPACE);

		/* NOTE: to keep it simple assume that data does not contain sequences "&pipe;", "&eol;" or "&bsn;" */

		value_esc = zbx_db_dyn_escape_string(value);
		newvalue_esc = zbx_db_dyn_escape_string(newvalue);
		zbx_free(value);
		zbx_free(newvalue);

		if (ZBX_DB_OK > DBexecute("insert into mappings (mappingid,valuemapid,value,newvalue)"
				" values (" ZBX_FS_UI64 "," ZBX_FS_UI64 ",'%s','%s')",
				mappingid, valuemapid, value_esc, newvalue_esc))
		{
			zbx_free(value_esc);
			zbx_free(newvalue_esc);
			return FAIL;
		}

		zbx_free(value_esc);
		zbx_free(newvalue_esc);
	}

	return SUCCEED;
}

static int	DBpatch_3000216(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute("insert into rsm_test_type (id,name) values ('7','rdap')"))
		return FAIL;

	return SUCCEED;
}

static int	DBpatch_3000217(void)
{
	DB_RESULT		result;
	DB_ROW			row;
	zbx_vector_uint64_t	templateids;
	zbx_uint64_t		hostmacroid, itemid;
	int			i;
	int			ret = FAIL;

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	zbx_vector_uint64_create(&templateids);
	zbx_vector_uint64_reserve(&templateids, 1);

	result = DBselect("select max(hostmacroid)+1 from hostmacro");

	if (NULL == result)
		goto out;

	if (NULL == (row = DBfetch(result)))
	{
		DBfree_result(result);
		goto out;
	}

	ZBX_STR2UINT64(hostmacroid, row[0]);

	DBfree_result(result);

	result = DBselect("select max(itemid)+1 from items");

	if (NULL == result)
		goto out;

	if (NULL == (row = DBfetch(result)))
	{
		DBfree_result(result);
		goto out;
	}

	ZBX_STR2UINT64(itemid, row[0]);

	DBfree_result(result);

	/* select templates "Template <TLD>" */
	result = DBselect(
			"select distinct templateid"
			" from hosts_templates"
			" where hostid in ("
					"select hostid"
					" from hosts_groups"
					" where groupid=190"	/* "TLD Probe results" */
				")"
				" and templateid not in ("
						"select templateid"
						" from hosts_templates"
						" where hostid in ("
								"select templateid"
								" from hosts_templates"
								" where hostid in ("
										"select hostid"
										" from hosts_groups"
										" where groupid=120)"	/* exclude "Probes" */
							")"
					")"
				" and templateid not in (99980)");	/* exclude "Template RDAP" */

	if (NULL == result)
	{
		ret = SUCCEED;
		goto out;
	}

	while (NULL != (row = DBfetch(result)))
	{
		zbx_uint64_t	templateid;

		ZBX_STR2UINT64(templateid, row[0]);	/* hostid of "Template <TLD>" */

		zbx_vector_uint64_append(&templateids, templateid);
	}

	DBfree_result(result);

	for (i = 0; i < templateids.values_num; i++)
	{
		zbx_vector_uint64_t	hostids;
		zbx_uint64_t		templated_itemid;
		int			j;

		templated_itemid = itemid;

		if (ZBX_DB_OK > DBexecute(
				"insert into hostmacro (hostmacroid,hostid,macro,value)"
				" values (" ZBX_FS_UI64 "," ZBX_FS_UI64 ",'{$RDAP.TLD.ENABLED}','0')",
				hostmacroid++, templateids.values[i]))
		{
			goto out;
		}

		if (ZBX_DB_OK > DBexecute(
				"insert into items ("
					"itemid,type,snmp_community,snmp_oid,hostid,name,key_,"
					"delay,history,trends,status,value_type,trapper_hosts,units,multiplier,delta,"
					"snmpv3_securityname,snmpv3_securitylevel,snmpv3_authpassphrase,"
					"snmpv3_privpassphrase,formula,logtimefmt,templateid,valuemapid,delay_flex,"
					"params,ipmi_sensor,data_type,authtype,username,password,publickey,privatekey,"
					"flags,interfaceid,port,description,inventory_link,lifetime,snmpv3_authprotocol,"
					"snmpv3_privprotocol,snmpv3_contextname,evaltype"
				")"
				" values ("
					ZBX_FS_UI64 ",'15','',''," ZBX_FS_UI64 ",'RDDS enabled/disabled',"
					"'rdds.enabled','60','7','365',"
					"'0','3','','','0','0',"
					"'','0','','',"
					"'1','',NULL,NULL,'','{$RSM.TLD.RDDS.ENABLED}','','0',"
					"'0','','','','','0',NULL,'',"
					"'History of Registration Data Directory Service being enabled or disabled.',"
					"'0','0','0','0','','0'"
				")",
				itemid++, templateids.values[i]))
		{
			goto out;
		}

		zbx_vector_uint64_create(&hostids);
		zbx_vector_uint64_reserve(&hostids, 1);

		/* select hosts "<TLD> <Probe>" hosts, for this particular TLD */
		result = DBselect(
				"select h.hostid"
				" from hosts_templates ht, hosts h"
				" where h.hostid=ht.hostid"
					" and ht.templateid=" ZBX_FS_UI64,
				templateids.values[i]);

		if (NULL == result)
		{
			zabbix_log(LOG_LEVEL_CRIT, "no \"<TLD> <Probe>\" hosts found");
			zbx_vector_uint64_destroy(&hostids);
			goto out;
		}

		while (NULL != (row = DBfetch(result)))
		{
			zbx_uint64_t	hostid;

			ZBX_STR2UINT64(hostid, row[0]);     /* hostid of "<TLD> <Probe>" */

			zbx_vector_uint64_append(&hostids, hostid);
		}

		DBfree_result(result);

		for (j = 0; j < hostids.values_num; j++)
		{
			if (ZBX_DB_OK > DBexecute(
					"insert into items ("
						"itemid,type,snmp_community,snmp_oid,hostid,name,key_,"
						"delay,history,trends,status,value_type,trapper_hosts,units,multiplier,delta,"
						"snmpv3_securityname,snmpv3_securitylevel,snmpv3_authpassphrase,"
						"snmpv3_privpassphrase,formula,logtimefmt,templateid,valuemapid,delay_flex,"
						"params,ipmi_sensor,data_type,authtype,username,password,publickey,privatekey,"
						"flags,interfaceid,port,description,inventory_link,lifetime,snmpv3_authprotocol,"
						"snmpv3_privprotocol,snmpv3_contextname,evaltype"
					")"
					" values ("
						ZBX_FS_UI64 ",'15','',''," ZBX_FS_UI64 ",'RDDS enabled/disabled',"
						"'rdds.enabled','60','7','365',"
						"'0','3','','','0','0',"
						"'','0','','',"
						"'1',''," ZBX_FS_UI64 ",NULL,'','{$RSM.TLD.RDDS.ENABLED}','','0',"
						"'0','','','','','0',NULL,'',"
						"'History of Registration Data Directory Service being enabled or disabled.',"
						"'0','0','0','0','','0'"
					")",
					itemid++, hostids.values[j], templated_itemid))
			{
				zbx_vector_uint64_destroy(&hostids);
				goto out;
			}
		}

		zbx_vector_uint64_destroy(&hostids);
	}

	if (ZBX_DB_OK > DBexecute("delete from ids where table_name='hostmacro'"))
		goto out;

	if (ZBX_DB_OK > DBexecute("delete from ids where table_name='items'"))
		goto out;

	ret = SUCCEED;
out:
	zbx_vector_uint64_destroy(&templateids);

	return ret;
}

static int	add_mappings(const char *const data[])
{
	int	i;

	for (i = 0; NULL != data[i]; i++)
	{
		zbx_uint64_t	mappingid, valuemapid;
		char		*value = NULL, *newvalue = NULL, *value_esc, *newvalue_esc;

		if (0 == strncmp(data[i], "--", ZBX_CONST_STRLEN("--")))
			continue;

		if (4 != sscanf(data[i], "ROW |" ZBX_FS_UI64 " |" ZBX_FS_UI64 " |%m[^|]|%m[^|]|",
				&mappingid, &valuemapid, &value, &newvalue))
		{
			zabbix_log(LOG_LEVEL_CRIT, "failed to parse the following line:\n%s", data[i]);
			zbx_free(value);
			zbx_free(newvalue);
			return FAIL;
		}

		zbx_rtrim(value, ZBX_WHITESPACE);
		zbx_rtrim(newvalue, ZBX_WHITESPACE);

		/* NOTE: to keep it simple assume that data does not contain sequences "&pipe;", "&eol;" or "&bsn;" */

		value_esc = zbx_db_dyn_escape_string(value);
		newvalue_esc = zbx_db_dyn_escape_string(newvalue);
		zbx_free(value);
		zbx_free(newvalue);

		if (ZBX_DB_OK > DBexecute("insert into mappings (mappingid,valuemapid,value,newvalue)"
				" values (" ZBX_FS_UI64 "," ZBX_FS_UI64 ",'%s','%s')",
				mappingid, valuemapid, value_esc, newvalue_esc))
		{
			zbx_free(value_esc);
			zbx_free(newvalue_esc);
			return FAIL;
		}

		zbx_free(value_esc);
		zbx_free(newvalue_esc);
	}

	return SUCCEED;
}

static int	DBpatch_3000218(void)
{
	/* this is just a direct paste from data.tmpl, with each line quoted and properly indented */
	static const char	*const data[] = {
		"ROW   |12053    |120       |-408 |DNS UDP - Querying for a non existent domain - No NSEC/NSEC3 RRs were found in the authority section            |",
		"ROW   |12107    |120       |-808 |DNS TCP - Querying for a non existent domain - No NSEC/NSEC3 RRs were found in the authority section            |",
		NULL
	};

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	return add_mappings(data);
}

static int	DBpatch_3000219(void)
{
	DB_RESULT		result;
	DB_ROW			row;
	zbx_vector_uint64_t	templateids;
	zbx_uint64_t		itemid;
	int			i;
	int			ret = FAIL;

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	zbx_vector_uint64_create(&templateids);
	zbx_vector_uint64_reserve(&templateids, 1);

	result = DBselect("select max(itemid)+1 from items");

	if (NULL == result)
		goto out;

	if (NULL == (row = DBfetch(result)))
	{
		DBfree_result(result);
		goto out;
	}

	ZBX_STR2UINT64(itemid, row[0]);

	DBfree_result(result);

	/* select templates "Template <TLD>" */
	result = DBselect(
			"select distinct templateid"
			" from hosts_templates"
			" where hostid in ("
					"select hostid"
					" from hosts_groups"
					" where groupid=190"	/* "TLD Probe results" */
				")"
				" and templateid not in ("
						"select templateid"
						" from hosts_templates"
						" where hostid in ("
								"select templateid"
								" from hosts_templates"
								" where hostid in ("
										"select hostid"
										" from hosts_groups"
										" where groupid=120)"	/* exclude "Probes" */
							")"
					")"
				" and templateid not in (99980)");	/* exclude "Template RDAP" */

	if (NULL == result)
	{
		ret = SUCCEED;
		goto out;
	}

	while (NULL != (row = DBfetch(result)))
	{
		zbx_uint64_t	templateid;

		ZBX_STR2UINT64(templateid, row[0]);	/* hostid of "Template <TLD>" */

		zbx_vector_uint64_append(&templateids, templateid);
	}

	DBfree_result(result);

	for (i = 0; i < templateids.values_num; i++)
	{
		zbx_vector_uint64_t	hostids;
		zbx_uint64_t		templated_itemid;
		int			j;

		templated_itemid = itemid;

		if (ZBX_DB_OK > DBexecute(
				"insert into items ("
					"itemid,type,snmp_community,snmp_oid,hostid,name,key_,"
					"delay,history,trends,status,value_type,trapper_hosts,units,multiplier,delta,"
					"snmpv3_securityname,snmpv3_securitylevel,snmpv3_authpassphrase,"
					"snmpv3_privpassphrase,formula,logtimefmt,templateid,valuemapid,delay_flex,"
					"params,ipmi_sensor,data_type,authtype,username,password,publickey,privatekey,"
					"flags,interfaceid,port,description,inventory_link,lifetime,snmpv3_authprotocol,"
					"snmpv3_privprotocol,snmpv3_contextname,evaltype"
				")"
				" values ("
					ZBX_FS_UI64 ",'15','',''," ZBX_FS_UI64 ",'DNSSEC enabled/disabled',"
					"'dnssec.enabled','60','7','365',"
					"'0','3','','','0','0',"
					"'','0','','',"
					"'1','',NULL,NULL,'','{$RSM.TLD.DNSSEC.ENABLED}','','0',"
					"'0','','','','','0',NULL,'',"
					"'History of Registration Data Directory Service being enabled or disabled.',"
					"'0','0','0','0','','0'"
				")",
				itemid++, templateids.values[i]))
		{
			goto out;
		}

		zbx_vector_uint64_create(&hostids);
		zbx_vector_uint64_reserve(&hostids, 1);

		/* select hosts "<TLD> <Probe>" hosts, for this particular TLD */
		result = DBselect(
				"select h.hostid"
				" from hosts_templates ht, hosts h"
				" where h.hostid=ht.hostid"
					" and ht.templateid=" ZBX_FS_UI64,
				templateids.values[i]);

		if (NULL == result)
		{
			zabbix_log(LOG_LEVEL_CRIT, "no \"<TLD> <Probe>\" hosts found");
			zbx_vector_uint64_destroy(&hostids);
			goto out;
		}

		while (NULL != (row = DBfetch(result)))
		{
			zbx_uint64_t	hostid;

			ZBX_STR2UINT64(hostid, row[0]);     /* hostid of "<TLD> <Probe>" */

			zbx_vector_uint64_append(&hostids, hostid);
		}

		DBfree_result(result);

		for (j = 0; j < hostids.values_num; j++)
		{
			if (ZBX_DB_OK > DBexecute(
					"insert into items ("
						"itemid,type,snmp_community,snmp_oid,hostid,name,key_,"
						"delay,history,trends,status,value_type,trapper_hosts,units,multiplier,delta,"
						"snmpv3_securityname,snmpv3_securitylevel,snmpv3_authpassphrase,"
						"snmpv3_privpassphrase,formula,logtimefmt,templateid,valuemapid,delay_flex,"
						"params,ipmi_sensor,data_type,authtype,username,password,publickey,privatekey,"
						"flags,interfaceid,port,description,inventory_link,lifetime,snmpv3_authprotocol,"
						"snmpv3_privprotocol,snmpv3_contextname,evaltype"
					")"
					" values ("
						ZBX_FS_UI64 ",'15','',''," ZBX_FS_UI64 ",'DNSSEC enabled/disabled',"
						"'dnssec.enabled','60','7','365',"
						"'0','3','','','0','0',"
						"'','0','','',"
						"'1',''," ZBX_FS_UI64 ",NULL,'','{$RSM.TLD.DNSSEC.ENABLED}','','0',"
						"'0','','','','','0',NULL,'',"
						"'History of Registration Data Directory Service being enabled or disabled.',"
						"'0','0','0','0','','0'"
					")",
					itemid++, hostids.values[j], templated_itemid))
			{
				zbx_vector_uint64_destroy(&hostids);
				goto out;
			}
		}

		zbx_vector_uint64_destroy(&hostids);
	}

	if (ZBX_DB_OK > DBexecute("delete from ids where table_name='items'"))
		goto out;

	ret = SUCCEED;
out:
	zbx_vector_uint64_destroy(&templateids);

	return ret;
}

static int	DBpatch_3000220(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute("update items set units='' where key_='rdap.rtt'"))
		return FAIL;

	return SUCCEED;
}

static int	DBpatch_3000221(void)
{
	/* this is just a direct paste from data.tmpl, with each line quoted and properly indented */
	static const char	*const data[] = {
		"ROW   |13576    |130       |-259 |RDDS80 - Maximum HTTP redirects were hit while trying to connect to RDAP server                                 |",
		"ROW   |13577    |135       |-215 |RDAP - Maximum HTTP redirects were hit while trying to connect to RDAP server                                   |",
		NULL
	};

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute("delete from mappings where mappingid in (13040,13041,13042,13529,13530,13531)"))
	{
		return FAIL;
	}

	return add_mappings(data);
}

static int	DBpatch_3000222(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute("update mappings set newvalue='DNS UDP - Expecting NOERROR RCODE but got unexpected"
			" from local resolver' where mappingid=%u", 12042))
	{
		return FAIL;
	}

	if (ZBX_DB_OK > DBexecute("update mappings set newvalue='DNS TCP - Expecting NOERROR RCODE but got unexpected"
			" from local resolver' where mappingid=%u", 12096))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_3000223(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute("update mappings set newvalue='RDDS80 - Maximum HTTP redirects were hit while trying"
			" to connect to RDDS server' where mappingid=%u", 13576))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	template_is_linked_to_host(const char *templateid, const char *hostid)
{
	DB_RESULT	result;
	DB_ROW		row;
	int		ret = FAIL;

	result = DBselect("select 1 from hosts_templates where templateid=%s and hostid=%s", templateid, hostid);

	if (NULL != (row = DBfetch(result)))
		ret = SUCCEED;

	DBfree_result(result);

	return ret;
}

static int	DBpatch_3000224(void)
{
	DB_RESULT	result;
	DB_ROW		row;
	int		ret = SUCCEED;

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	result = DBselect("select h.hostid from hosts_groups hg,hosts h where hg.hostid=h.hostid and hg.groupid=190");

	if (NULL == result)
		return FAIL;

	while (NULL != (row = DBfetch(result)) && SUCCEED == ret)
	{
		zbx_uint64_t		hostid;
		zbx_vector_uint64_t	templateids;
		char			*error;

		if (SUCCEED == template_is_linked_to_host("99980", row[0]))
			continue;	/* already linked */

		ZBX_STR2UINT64(hostid, row[0]);			/* hostid of probe host */
		zbx_vector_uint64_create(&templateids);
		zbx_vector_uint64_reserve(&templateids, 1);
		zbx_vector_uint64_append(&templateids, 99980);	/* hostid of "Template RDAP" */

		if (SUCCEED != (ret = DBcopy_template_elements(hostid, &templateids, &error)))
		{
			zabbix_log(LOG_LEVEL_CRIT, "cannot link template(s) %s", error);
			zbx_free(error);
		}

		zbx_vector_uint64_destroy(&templateids);
	}

	DBfree_result(result);

	return ret;
}

static int	DBpatch_3000225(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	/* "Zabbix Server" host macro {$MAX_PROCESSES} */
	if (ZBX_DB_OK > DBexecute("update hostmacro set value='1500' where hostmacroid=3"))
		return FAIL;

	return SUCCEED;
}

static int	DBpatch_3000226(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	/* disable 'rdap[%' items on hosts where RDAP disabled on a TLD level */
	if (ZBX_DB_OK > DBexecute(
			"update items"
			" set status=1"
			" where key_ like 'rdap[%%'"
				" and hostid in ("
					"select hostid"
					" from hosts_templates"
					" where templateid in ("
						"select hostid"
						" from hostmacro"
						" where macro='{$RDAP.TLD.ENABLED}'"
							" and value=0"
						")"
					")"))
	{
		return FAIL;
	}

	/* disable 'rdap[%' items on hosts where RDAP disabled on a Probe level */
	if (ZBX_DB_OK > DBexecute(
			"update items"
			" set status=1"
			" where key_ like 'rdap[%%'"
				" and hostid in ("
					"select hostid"
					" from hosts_templates"
					" where templateid in ("
						"select hostid"
						" from hostmacro"
						" where macro='{$RSM.RDDS.ENABLED}'"
							" and value=0"
						")"
					")"))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_3000227(void)
{
	/* this is just a direct paste from data.tmpl, with each line quoted and properly indented */
	static const char	*const data[] = {
		"ROW   |13018    |130       |-2   |RDDS - IP addresses for the hostname are not supported by the IP versions supported by the probe node           |",
		"ROW   |13578    |135       |-2   |RDAP - IP addresses for the hostname are not supported by the IP versions supported by the probe node           |",
		"ROW   |15013    |150       |-2   |EPP - IP addresses for the hostname are not supported by the IP versions supported by the probe node            |",
		NULL
	};

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	/* DNS UDP -428 */
	if (ZBX_DB_OK > DBexecute("delete from mappings where mappingid=12059"))
		return FAIL;

	/* DNS UDP -401 */
	if (ZBX_DB_OK > DBexecute("update mappings set newvalue='DNS UDP - The TLD is configured as DNSSEC-enabled, but"
			" no DNSKEY was found in the apex' where mappingid=12039"))
	{
		return FAIL;
	}

	/* DNS TCP -828 */
	if (ZBX_DB_OK > DBexecute("delete from mappings where mappingid=12113"))
		return FAIL;

	/* DNS TCP -801 */
	if (ZBX_DB_OK > DBexecute("update mappings set newvalue='DNS TCP - The TLD is configured as DNSSEC-enabled, but"
			" no DNSKEY was found in the apex' where mappingid=12093"))
	{
		return FAIL;
	}

	/* DNS UDP -402 */
	if (ZBX_DB_OK > DBexecute("update mappings set newvalue='DNS UDP - No AD bit from local resolver'"
			" where mappingid=12040"))
	{
		return FAIL;
	}

	/* DNS TCP -802 */
	if (ZBX_DB_OK > DBexecute("update mappings set newvalue='DNS TCP - No AD bit from local resolver'"
			" where mappingid=12094"))
	{
		return FAIL;
	}

	/* RDDS -2, RDAP -2, EPP -2 */
	if (SUCCEED != add_mappings(data))
		return FAIL;

	return SUCCEED;
}

static int	DBpatch_3000228(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	/* DNS UDP -404 => -2 */
	if (ZBX_DB_OK > DBexecute("update mappings set value='-2' where mappingid=12042"))
		return FAIL;

	/* DNS TCP -804 => -3 */
	if (ZBX_DB_OK > DBexecute("update mappings set value='-3' where mappingid=12096"))
		return FAIL;

	/* RDDS43 - => -226 => -3 */
	if (ZBX_DB_OK > DBexecute("update mappings set value='-3' where mappingid=13013"))
		return FAIL;

	/* RDDS80 - => -254 => -4 */
	if (ZBX_DB_OK > DBexecute("update mappings set value='-4' where mappingid=13021"))
		return FAIL;

	/* RDAP - => -204 => -5 */
	if (ZBX_DB_OK > DBexecute("update mappings set value='-5' where mappingid=13507"))
		return FAIL;

	/* DNS UDP -400 */
	if (ZBX_DB_OK > DBexecute("update mappings set newvalue='DNS UDP - No server could be reached by local resolver'"
			" where mappingid=12038"))
	{
		return FAIL;
	}

	/* DNS TCP -800 */
	if (ZBX_DB_OK > DBexecute("update mappings set newvalue='DNS TCP - No server could be reached by local resolver'"
			" where mappingid=12092"))
	{
		return FAIL;
	}

	/* RDDS43 -222 */
	if (ZBX_DB_OK > DBexecute("update mappings set newvalue='RDDS43 - No server could be reached by local resolver'"
			" where mappingid=13009"))
	{
		return FAIL;
	}

	/* RDDS80 -250 */
	if (ZBX_DB_OK > DBexecute("update mappings set newvalue='RDDS80 - No server could be reached by local resolver'"
			" where mappingid=13017"))
	{
		return FAIL;
	}

	/* RDAP -200 */
	if (ZBX_DB_OK > DBexecute("update mappings set newvalue='RDAP - No server could be reached by local resolver'"
			" where mappingid=13503"))
	{
		return FAIL;
	}

	/* RDAP -201 */
	if (ZBX_DB_OK > DBexecute("delete from mappings where mappingid=13504"))
		return FAIL;

	return SUCCEED;
}

static int	DBpatch_3000229(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute(
			"update mappings"
			" set value=value-200"
			" where valuemapid=135"
				" and convert(value,integer) between -215 and -200"))
	{
		return FAIL;
	}

	if (ZBX_DB_OK > DBexecute(
			"update mappings"
			" set value=value-250"
			" where valuemapid=135"
				" and convert(value,integer)<-249"))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_3000230(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute(
			"update mappings"
			" set value=value+250"
			" where valuemapid=135"
				" and convert(value,integer) between -665 and -650"))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	add_globalmacros(const char *const data[])
{
	int	i;

	for (i = 0; NULL != data[i]; i++)
	{
		zbx_uint64_t	globalmacroid;
		char		*macro = NULL, *value = NULL, *macro_esc, *value_esc;

		if (0 == strncmp(data[i], "--", ZBX_CONST_STRLEN("--")))
			continue;

		if (3 != sscanf(data[i], "ROW |" ZBX_FS_UI64 " |%m[^|]|%m[^|]|",
				&globalmacroid, &macro, &value))
		{
			zabbix_log(LOG_LEVEL_CRIT, "failed to parse the following line:\n%s", data[i]);
			zbx_free(macro);
			zbx_free(value);
			return FAIL;
		}

		zbx_rtrim(macro, ZBX_WHITESPACE);
		zbx_rtrim(value, ZBX_WHITESPACE);

		/* NOTE: to keep it simple assume that data does not contain sequences "&pipe;", "&eol;" or "&bsn;" */

		macro_esc = zbx_db_dyn_escape_string(macro);
		value_esc = zbx_db_dyn_escape_string(value);
		zbx_free(macro);
		zbx_free(value);

		if (ZBX_DB_OK > DBexecute("insert into globalmacro (globalmacroid,macro,value)"
				" values (" ZBX_FS_UI64 ",'%s','%s')",
				globalmacroid, macro_esc, value_esc))
		{
			zbx_free(macro_esc);
			zbx_free(value_esc);
			return FAIL;
		}

		zbx_free(macro_esc);
		zbx_free(value_esc);
	}

	return SUCCEED;
}

static int	DBpatch_3000231(void)
{
	DB_RESULT		result;
	DB_ROW			row;
	zbx_vector_uint64_t	templateids;
	zbx_uint64_t		next_itemid, next_itemappid;
	int			i;

	static const char	*const data[] = {
		"ROW   |100          |{$RESOLVER.STATUS.TIMEOUT}    |5                  |",
		"ROW   |101          |{$RESOLVER.STATUS.TRIES}      |3                  |",
		NULL
	};

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	zbx_vector_uint64_create(&templateids);
	zbx_vector_uint64_reserve(&templateids, 1);

	/* select all "Template <Probe> Status" hosts */
	result = DBselect("select h.hostid from hosts h,hosts_groups hg where h.hostid=hg.hostid and h.host like 'Template %% Status' and hg.groupid=240");

	while (NULL != (row = DBfetch(result)))
	{
		zbx_uint64_t	templateid;

		ZBX_STR2UINT64(templateid, row[0]);	/* hostid of "Template <Probe> Status" */

		zbx_vector_uint64_append(&templateids, templateid);
	}

	DBfree_result(result);

	next_itemid = DBget_maxid_num("items", templateids.values_num * 2);			/* items for Template and host it is linked to */
	next_itemappid = DBget_maxid_num("items_applications", templateids.values_num * 2);	/* items_applications for Template and host it is linked to */

	for (i = 0; i < templateids.values_num; i++)
	{
		zbx_uint64_t	templateid, hostid, templated_itemid, applicationid;

		templateid = templateids.values[i];
		templated_itemid = next_itemid;

		if (ZBX_DB_OK > DBexecute(
			"insert into items ("
				"itemid,type,snmp_community,snmp_oid,hostid,name,"
				"key_,"
				"delay,history,trends,status,value_type,trapper_hosts,units,multiplier,delta,"
				"snmpv3_securityname,snmpv3_securitylevel,snmpv3_authpassphrase,snmpv3_privpassphrase,"
				"formula,logtimefmt,templateid,valuemapid,delay_flex,params,ipmi_sensor,data_type,"
				"authtype,username,password,publickey,privatekey,flags,interfaceid,port,"
				"description,"
				"inventory_link,lifetime,snmpv3_authprotocol,snmpv3_privprotocol,snmpv3_contextname,evaltype"
			")"
			" values ("
				ZBX_FS_UI64 ",'3','',''," ZBX_FS_UI64 ",'Local resolver status ($1)',"
				"'resolver.status["
					"{$RSM.RESOLVER},"
					"{$RESOLVER.STATUS.TIMEOUT},"
					"{$RESOLVER.STATUS.TRIES},"
					"{$RSM.IP4.ENABLED},"
					"{$RSM.IP6.ENABLED}"
					"]',"
				"'60','7','365','0','3','','','0','0',"
				"'','0','','',"
				"'1','',NULL,'1','','','','0',"
				"'0','','','','','0',NULL,'',"
				"'Status of Local resolver.',"
				"'0','0','0','0','','0'"
			")",
			next_itemid++, templateids.values[i]))
		{
			return FAIL;
		}

		result = DBselect(
				"select applicationid"
				" from items_applications"
				" where itemid in"
					" (select itemid"
					" from items"
					" where key_='rsm.probe.status[manual]'"
						" and hostid=" ZBX_FS_UI64
					")",
				templateids.values[i]);

		if (NULL == (row = DBfetch(result)))
		{
			zabbix_log(LOG_LEVEL_CRIT, "Zabbix configuration in the database is corrupted");
			return FAIL;
		}

		ZBX_STR2UINT64(applicationid, row[0]);	/* application of "Template <Probe> Status" */

		if (ZBX_DB_OK > DBexecute(
				"insert into items_applications (itemappid,applicationid,itemid)"
				" values (" ZBX_FS_UI64 "," ZBX_FS_UI64 "," ZBX_FS_UI64 ")",
				next_itemappid++, applicationid, templated_itemid))

		{
			return FAIL;
		}

		DBfree_result(result);

		result = DBselect("select hostid from hosts_templates where templateid=" ZBX_FS_UI64, templateid);

		if (NULL == (row = DBfetch(result)))
		{
			zabbix_log(LOG_LEVEL_CRIT, "Zabbix configuration in the database is corrupted");
			return FAIL;
		}

		ZBX_STR2UINT64(hostid, row[0]);	/* hostid of "<Probe>" */

		DBfree_result(result);

		if (ZBX_DB_OK > DBexecute(
			"insert into items ("
				"itemid,type,snmp_community,snmp_oid,hostid,name,"
				"key_,"
				"delay,history,trends,status,value_type,trapper_hosts,units,multiplier,delta,"
				"snmpv3_securityname,snmpv3_securitylevel,snmpv3_authpassphrase,snmpv3_privpassphrase,"
				"formula,logtimefmt,templateid,valuemapid,delay_flex,params,ipmi_sensor,data_type,"
				"authtype,username,password,publickey,privatekey,flags,interfaceid,port,"
				"description,"
				"inventory_link,lifetime,snmpv3_authprotocol,snmpv3_privprotocol,snmpv3_contextname,evaltype"
			")"
			" values ("
				ZBX_FS_UI64 ",'3','',''," ZBX_FS_UI64 ",'Local resolver status ($1)',"
				"'resolver.status["
					"{$RSM.RESOLVER},"
					"{$RESOLVER.STATUS.TIMEOUT},"
					"{$RESOLVER.STATUS.TRIES},"
					"{$RSM.IP4.ENABLED},"
					"{$RSM.IP6.ENABLED}"
					"]',"
				"'60','90','365','0','3','','','0','0',"
				"'','0','','',"
				"'1',''," ZBX_FS_UI64 ",'1','','','','0',"
				"'0','','','','','0',NULL,'',"
				"'Status of Local resolver.',"
				"'0','0','0','0','','0'"
			")",
			next_itemid++, hostid, templated_itemid))
		{
			return FAIL;
		}

		result = DBselect(
				"select applicationid"
				" from items_applications"
				" where itemid in"
					" (select itemid"
					" from items"
					" where key_='rsm.probe.status[manual]'"
						" and hostid=" ZBX_FS_UI64
					")",
				hostid);

		if (NULL == (row = DBfetch(result)))
		{
			zabbix_log(LOG_LEVEL_CRIT, "Zabbix configuration in the database is corrupted");
			return FAIL;
		}

		ZBX_STR2UINT64(applicationid, row[0]);	/* application of "Template <Probe> Status" */

		if (ZBX_DB_OK > DBexecute(
				"insert into items_applications (itemappid,applicationid,itemid)"
				" values (" ZBX_FS_UI64 "," ZBX_FS_UI64 "," ZBX_FS_UI64 ")",
				next_itemappid++, applicationid, next_itemid - 1))

		{
			return FAIL;
		}

		DBfree_result(result);
	}

	zbx_vector_uint64_destroy(&templateids);

	add_globalmacros(data);

	if (ZBX_DB_OK > DBexecute("delete from ids where table_name='items'"))
		return FAIL;

	if (ZBX_DB_OK > DBexecute("delete from ids where table_name='items_applications'"))
		return FAIL;

	return SUCCEED;
}

static int	DBpatch_3000232(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute("update mappings set value='-390' where mappingid=13501"))
		return FAIL;

	if (ZBX_DB_OK > DBexecute("update mappings set value='-391' where mappingid=13502"))
		return FAIL;

	return SUCCEED;
}

static int	DBpatch_3000233(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	/* {$PROBE.INTERNAL.ERROR.INTERVAL}=1m */
	if (ZBX_DB_OK > DBexecute(
			"update globalmacro"
			" set value='1m'"
			" where globalmacroid=66"))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_3000234(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	/* delete obsoleted records */
	if (ZBX_DB_OK > DBexecute(
			"delete l"
			" from lastvalue as l"
			" left join items i"
				" on i.itemid = l.itemid"
			" where i.itemid is null"))
	{
		return FAIL;
	}

	/* add constraint */
	if (ZBX_DB_OK > DBexecute(
			"alter table `lastvalue`"
			" add constraint `c_lastvalue_1`"
				" foreign key (`itemid`) references `items` (`itemid`)"
			" on delete cascade"))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_3000235(void)
{
	DB_RESULT		result;
	DB_ROW			row;
	zbx_vector_uint64_t	templateids;
	zbx_uint64_t		next_triggerid, next_functionid;
	int			i;

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	zbx_vector_uint64_create(&templateids);
	zbx_vector_uint64_reserve(&templateids, 1);

	/* select all "Template <Probe> Status" hosts */
	result = DBselect("select h.hostid from hosts h,hosts_groups hg where h.hostid=hg.hostid and h.host like 'Template %% Status' and hg.groupid=240");

	while (NULL != (row = DBfetch(result)))
	{
		zbx_uint64_t	templateid;

		ZBX_STR2UINT64(templateid, row[0]);	/* hostid of "Template <Probe> Status" */

		zbx_vector_uint64_append(&templateids, templateid);
	}

	DBfree_result(result);

	next_triggerid = DBget_maxid_num("triggers", templateids.values_num * 2);	/* triggers for Template and host it is linked to */
	next_functionid = DBget_maxid_num("functions", templateids.values_num * 2);	/* functions for Template and host it is linked to */

	for (i = 0; i < templateids.values_num; i++)
	{
		zbx_uint64_t	templateid, hostid, templated_itemid, itemid, templated_functionid, functionid,
				templated_triggerid, triggerid;

		templateid = templateids.values[i];

		templated_triggerid = next_triggerid++;
		triggerid = next_triggerid++;
		templated_functionid = next_functionid++;
		functionid = next_functionid++;

		result = DBselect("select hostid from hosts_templates where templateid=" ZBX_FS_UI64, templateid);

		if (NULL == (row = DBfetch(result)))
		{
			zabbix_log(LOG_LEVEL_CRIT, "Zabbix configuration in the database is corrupted");
			return FAIL;
		}

		ZBX_STR2UINT64(hostid, row[0]);	/* hostid of "<Probe>" */

		DBfree_result(result);

		result = DBselect("select itemid from items where hostid=" ZBX_FS_UI64 " and key_='rsm.probe.status[manual]' and templateid is null", templateid);

		if (NULL == (row = DBfetch(result)))
		{
			zabbix_log(LOG_LEVEL_CRIT, "Zabbix configuration in the database is corrupted");
			return FAIL;
		}

		ZBX_STR2UINT64(templated_itemid, row[0]);	/* itemid of "Template <Probe> Status" */

		DBfree_result(result);

		result = DBselect("select itemid from items where hostid=" ZBX_FS_UI64 " and key_='rsm.probe.status[manual]' and templateid is not null", hostid);

		if (NULL == (row = DBfetch(result)))
		{
			zabbix_log(LOG_LEVEL_CRIT, "Zabbix configuration in the database is corrupted");
			return FAIL;
		}

		ZBX_STR2UINT64(itemid, row[0]);	/* itemid of "<Probe>" */

		DBfree_result(result);

		/* add triggers... */
		if (ZBX_DB_OK > DBexecute(
				"insert into triggers (triggerid,expression,description,url,status,priority,comments,templateid,type,flags)"
				" values"
					" (" ZBX_FS_UI64 ",'{" ZBX_FS_UI64 "}=0','Probe {HOST.NAME} has been knocked out','','0','4','',NULL,'0','0'),"
					" (" ZBX_FS_UI64 ",'{" ZBX_FS_UI64 "}=0','Probe {HOST.NAME} has been knocked out','','0','4',''," ZBX_FS_UI64 ",'0','0')",
				templated_triggerid, templated_functionid,
				triggerid, functionid, templated_triggerid))
		{
			return FAIL;
		}

		/* and trigger functions */
		if (ZBX_DB_OK > DBexecute(
				"insert into functions (functionid,itemid,triggerid,function,parameter)"
				" values"
					" (" ZBX_FS_UI64 "," ZBX_FS_UI64 "," ZBX_FS_UI64 ",'last','0'),"
					" (" ZBX_FS_UI64 "," ZBX_FS_UI64 "," ZBX_FS_UI64 ",'last','0')",
				templated_functionid, templated_itemid, templated_triggerid,
				functionid, itemid, triggerid))
		{
			return FAIL;
		}

	}

	zbx_vector_uint64_destroy(&templateids);

	if (ZBX_DB_OK > DBexecute("delete from ids where table_name='triggers'"))
		return FAIL;

	if (ZBX_DB_OK > DBexecute("delete from ids where table_name='functions'"))
		return FAIL;

	return SUCCEED;
}

static int	DBpatch_3000236(void)
{
	return DBpatch_3000226();
}

static int      DBpatch_3000237(void)
{
	if (ZBX_DB_OK > DBexecute("update mappings set newvalue='DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got NOERROR (obsolete)' where mappingid=12019"))
		return FAIL;

	if (ZBX_DB_OK > DBexecute("update mappings set newvalue='DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got NOERROR (obsolete)' where mappingid=12073"))
		return FAIL;

	return SUCCEED;
}

static int	DBpatch_3000238(void)
{
	if (ZBX_DB_OK > DBexecute("alter table lastvalue modify column value double(24,4) not null default 0.0"))
		return FAIL;

	return SUCCEED;
}

static int	DBpatch_3000300(void)
{
	return SUCCEED;
}

static int	DBpatch_3000301(void)
{
	const ZBX_TABLE table =
			{"lastvalue_str", "itemid", 0,
				{
					{"itemid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"clock", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
					{"value", "", NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
					{0}
				},
				NULL
			};

	if (SUCCEED != DBcreate_table(&table))
		return FAIL;

	/* add constraint */
	if (ZBX_DB_OK > DBexecute(
			"alter table `lastvalue_str`"
			" add constraint `c_lastvalue_str_1`"
				" foreign key (`itemid`) references `items` (`itemid`)"
			" on delete cascade"))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_3000302(void)
{
	return DBpatch_3000237();
}

static int	DBpatch_3000303(void)
{
	return DBpatch_3000238();
}

static int	move_ids(const char *table_name, const char *idfield, zbx_uint64_t id, int count)
{
	DB_RESULT	result;
	DB_ROW		row;

	result = DBselect("select %s from %s where %s>=" ZBX_FS_UI64 " order by %s desc",
			idfield, table_name, idfield, id, idfield);

	while (NULL != (row = DBfetch(result)))
	{
		if (ZBX_DB_OK > DBexecute("update %s set %s=%d where %s=%s",
				table_name, idfield, atoi(row[0]) + count, idfield, row[0]))
		{
			return FAIL;
		}
	}

	DBfree_result(result);

	if (ZBX_DB_OK > DBexecute("delete from ids where table_name='%s'", table_name))
		return FAIL;

	return SUCCEED;
}

static int	DBpatch_3000304(void)
{
	int		i;
	zbx_uint64_t	globalmacroid = 102;	/* use 102, 103 and 104 */
	const char	*macros[][2] = {
		{"{$RSM.SLV.RDDS.RTT}", "5"},
		{"{$RSM.SLV.DNS.DOWNTIME}", "0"},
		{"{$RSM.SLV.RDDS.DOWNTIME}", "864"},
		{NULL, NULL}
	};

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	for (i = 0; macros[i][0] != NULL; i++)
	{
		if (ZBX_DB_OK > DBexecute("delete from globalmacro where macro='%s'", macros[i][0]))
			return FAIL;
	}

	if (SUCCEED != move_ids("globalmacro", "globalmacroid", globalmacroid, 3))
		return FAIL;

	for (i = 0; macros[i][0] != NULL; i++)
	{
		if (ZBX_DB_OK > DBexecute(
				"insert into globalmacro (globalmacroid,macro,value)"
				" values"
				" (" ZBX_FS_UI64 ",'%s','%s')", globalmacroid++, macros[i][0], macros[i][1]))
		{
			return FAIL;
		}
	}

	if (ZBX_DB_OK > DBexecute(
			"update globalmacro"
			" set value='5'"
			" where macro in ("
				"'{$RSM.SLV.RDDS43.RTT}',"
				"'{$RSM.SLV.RDDS80.RTT}',"
				"'{$RSM.SLV.DNS.UDP.RTT}',"
				"'{$RSM.SLV.DNS.TCP.RTT}')"))
	{
		return FAIL;
	}

	if (ZBX_DB_OK > DBexecute("update globalmacro set value='432' where macro='{$RSM.SLV.NS.AVAIL}'"))
		return FAIL;

	return SUCCEED;
}

static int	create_dns_downtime_trigger(const char *hostid)
{
	DB_RESULT	result;
	DB_ROW		row;
	zbx_uint64_t	triggerid, functionid;
	const char	*itemkey = "rsm.slv.dns.downtime";

	triggerid = DBget_maxid("triggers");
	functionid = DBget_maxid("functions");

	if (ZBX_DB_OK > DBexecute(
			"insert into triggers (triggerid,expression,description,"
				"url,status,priority,comments,templateid,type,flags)"
			"values (" ZBX_FS_UI64 ",'{" ZBX_FS_UI64 "}>{$RSM.SLV.DNS.DOWNTIME}','DNS service was unavailable for at least {ITEM.VALUE1}m',"
				"'', '0', '5', '', NULL, '0', '0')",
			triggerid, functionid))
	{
		return FAIL;
	}

	result = DBselect("select itemid from items where key_='%s' and hostid='%s'", itemkey, hostid);

	if (NULL == (row = DBfetch(result)))
	{
		zabbix_log(LOG_LEVEL_CRIT, "item key \"%s\" not found at TLD host %s", itemkey, hostid);
		return FAIL;
	}

	if (ZBX_DB_OK > DBexecute(
			"insert into functions (functionid,itemid,triggerid,function,parameter) values"
			" (" ZBX_FS_UI64 ", %s," ZBX_FS_UI64 ",'last','0')",
			functionid, row[0], triggerid))
	{
		return FAIL;
	}

	DBfree_result(result);

	return SUCCEED;
}

static int	DBpatch_3000305(void)
{
	DB_RESULT	result;
	DB_ROW		row;

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	result = DBselect("select h.hostid, h.host from hosts h inner join hosts_groups hg on h.hostid=hg.hostid"
				" where hg.groupid=140");

	while (NULL != (row = DBfetch(result)))
	{
		create_dns_downtime_trigger(row[0]);
	}

	DBfree_result(result);

	return SUCCEED;
}

static int	create_rdds_downtime_trigger(const char *hostid, const char *percent, const char *coefficient,
		const char *priority, zbx_uint64_t *triggerid)
{
	DB_RESULT	result;
	DB_ROW		row;
	zbx_uint64_t	functionid;
	const char	*itemkey = "rsm.slv.rdds.downtime";

	*triggerid = DBget_maxid("triggers");
	functionid = DBget_maxid("functions");

	if (ZBX_DB_OK > DBexecute(
			"insert into triggers (triggerid,expression,description,"
				"url,status,priority,comments,templateid,type,flags)"
			"values (" ZBX_FS_UI64 ", '{" ZBX_FS_UI64 "}>={$RSM.SLV.RDDS.DOWNTIME}%s',"
				"'RDDS service was unavailable for %s of allowed $1 minutes',"
				"'', '0', '%s', '', NULL, '0', '0')",
			*triggerid, functionid, coefficient, percent, priority))
	{
		return FAIL;
	}

	result = DBselect("select itemid from items where key_='%s' and hostid='%s'", itemkey, hostid);

	if (NULL == (row = DBfetch(result)))
	{
		zabbix_log(LOG_LEVEL_CRIT, "item key \"%s\" not found at TLD host %s", itemkey, hostid);
		return FAIL;
	}

	if (ZBX_DB_OK > DBexecute(
			"insert into functions (functionid,itemid,triggerid,function,parameter) values"
			" (" ZBX_FS_UI64 ", %s," ZBX_FS_UI64 ",'last','0')",
			functionid, row[0], *triggerid))
	{
		return FAIL;
	}

	DBfree_result(result);

	return SUCCEED;
}

static int	create_trigger_dependency(zbx_uint64_t triggerid, zbx_uint64_t dependid)
{
	if (ZBX_DB_OK > DBexecute(
			"insert into trigger_depends (triggerdepid,triggerid_down,triggerid_up)"
			" values (" ZBX_FS_UI64 ", " ZBX_FS_UI64 ", " ZBX_FS_UI64 ")",
			DBget_maxid("trigger_depends"), dependid, triggerid))
	{
		return FAIL;
	}

	return SUCCEED;
}

/* percent, coefficient, priority */
const char	*trigger_params[][3] = {
	{"10%",		"*0.1",		"2"},
	{"25%",		"*0.25",	"3"},
	{"50%",		"*0.5",		"3"},
	{"75%",		"*0.75",	"4"},
	{"100%",	"",		"5"}
};

static int	create_dependent_rdds_trigger_chain(const char *hostid)
{
	zbx_uint64_t	triggerid = 0, dependid = 0;
	size_t		i;

	for (i = 0; i < sizeof(trigger_params) / sizeof(*trigger_params); i++)
	{
		const char	*percent     = trigger_params[i][0];
		const char	*coefficient = trigger_params[i][1];
		const char	*priority    = trigger_params[i][2];

		if (SUCCEED != create_rdds_downtime_trigger(hostid, percent, coefficient, priority, &triggerid))
			return FAIL;

		if (0 != triggerid && 0 != dependid)
		{
			if (SUCCEED != create_trigger_dependency(triggerid, dependid))
				return FAIL;
		}

		dependid = triggerid;
	}

	return SUCCEED;
}

static int	tld_rdds_enabled(const char *tld, int *rdds_enabled)
{
	DB_RESULT	result;
	DB_ROW		row;

	result = DBselect(
			"select max(value)"
			" from hostmacro hm,hosts h"
			" where hm.hostid=h.hostid"
				" and h.host='Template %s'"
				" and (hm.macro='{$RSM.TLD.RDDS.ENABLED}' or hm.macro='{$RDAP.TLD.ENABLED}')",
			tld);

	if (NULL == (row = DBfetch(result)))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot determine if RDDS is enabled on TLD %s", row[1]);
		return FAIL;
	}

	*rdds_enabled = atoi(row[0]);

	DBfree_result(result);

	return SUCCEED;
}

static int	DBpatch_3000306(void)
{
	DB_RESULT	result;
	DB_ROW		row;

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	result = DBselect("select h.hostid,h.host from hosts h inner join hosts_groups hg on h.hostid=hg.hostid"
				" where hg.groupid=140");

	while (NULL != (row = DBfetch(result)))
	{
		int	rdds_enabled;

		if (SUCCEED != tld_rdds_enabled(row[1], &rdds_enabled))
			return FAIL;

		if (0 == rdds_enabled)
			continue;

		if (SUCCEED != create_dependent_rdds_trigger_chain(row[0]))
			return FAIL;
	}

	DBfree_result(result);

	return SUCCEED;
}

static int extract_string_part(char **out, char ch, const char *str, size_t *index, size_t length)
{
	size_t	strbegin, part_length;

	strbegin = *index;

	if (*index >= length)
		return FAIL;

	for (;;)
	{
		if (*index >= length)
			return FAIL;

		if (str[*index] == ch)
			break;

		(*index)++;
	}

	part_length = *index - strbegin;

	if (1 > part_length)
		return FAIL;

	*out = (char *)malloc(part_length + 1);
	memcpy(*out, str + strbegin, part_length);
	(*out)[part_length] = 0;

	return SUCCEED;
}

static int extract_nsip_pair_from_rtt_item_key(const char *probe_item_key, char **ns, char **ip)
{
	size_t	i, probe_item_key_len;

	if (NULL == probe_item_key || 0 == probe_item_key[0])
		return FAIL;

	probe_item_key_len = strlen(probe_item_key);

	i = strlen("rsm.dns.udp.rtt[{$RSM.TLD},");

	if (SUCCEED != extract_string_part(ns, ',', probe_item_key, &i, probe_item_key_len))
		return FAIL;

	i++;

	if (SUCCEED != extract_string_part(ip, ']', probe_item_key, &i, probe_item_key_len))
		return FAIL;

	return SUCCEED;
}

static int	create_slv_dns_ns_avail_item(const char *tld, const char *hostid, char *ns, char *ip)
{
	zbx_uint64_t	itemid;

	itemid = DBget_maxid("items");

	if (ZBX_DB_OK > DBexecute(
		"insert into items (itemid,type,snmp_community,snmp_oid,hostid,"
			"name,key_,delay,history,trends,"
			"status,value_type,trapper_hosts,units,multiplier,delta,"
			"snmpv3_securityname,snmpv3_securitylevel,snmpv3_authpassphrase,snmpv3_privpassphrase,"
			"formula,logtimefmt,templateid,valuemapid,delay_flex,params,ipmi_sensor,data_type,"
			"authtype,username,password,publickey,privatekey,flags,interfaceid,port,description,"
			"inventory_link,lifetime,snmpv3_authprotocol,snmpv3_privprotocol,snmpv3_contextname,evaltype)"
		" values ('" ZBX_FS_UI64 "','2','','','%s',"
			"'DNS NS %s (%s) availability','rsm.slv.dns.ns.avail[%s,%s]','60','90','365',"
			"'0','0','','','0','0',"
			"'','0','','',"
			"'1','',NULL,'110','','','','0',"
			"'0','','','','','0',NULL,'','',"
			"'0','30','0','0','','0')",
			itemid, hostid, ns, ip, ns, ip))
	{
		return FAIL;
	}

	if (ZBX_DB_OK > DBexecute(
		"insert into items_applications (itemappid,applicationid,itemid) VALUES ('" ZBX_FS_UI64 "',"
			"(SELECT applicationid FROM applications WHERE hostid='%s' AND name='SLV particular test'),"
			ZBX_FS_UI64")", DBget_maxid("items_applications"), hostid, itemid))
	{
		return FAIL;
	}

	ZBX_UNUSED(tld);

	return SUCCEED;
}

static int	create_slv_dns_ns_downtime_item(const char *tld, const char *hostid, char *ns, char *ip)
{
	zbx_uint64_t	itemid;

	itemid = DBget_maxid("items");

	if (ZBX_DB_OK > DBexecute(
		"insert into items (itemid,type,snmp_community,snmp_oid,hostid,"
			"name,key_,delay,history,trends,"
			"status,value_type,trapper_hosts,units,multiplier,delta,"
			"snmpv3_securityname,snmpv3_securitylevel,snmpv3_authpassphrase,snmpv3_privpassphrase,"
			"formula,logtimefmt,templateid,valuemapid,delay_flex,params,ipmi_sensor,data_type,"
			"authtype,username,password,publickey,privatekey,flags,interfaceid,port,description,"
			"inventory_link,lifetime,snmpv3_authprotocol,snmpv3_privprotocol,snmpv3_contextname,evaltype)"
		" values ('" ZBX_FS_UI64 "','2','','','%s',"
			"'DNS minutes of %s (%s) downtime','rsm.slv.dns.ns.downtime[%s,%s]','60','90','365',"
			"'0','0','','','0','0',"
			"'','0','','',"
			"'1','',NULL,NULL,'','','','0',"
			"'0','','','','','0',NULL,'','',"
			"'0','30','0','0','','0')",
			itemid, hostid, ns, ip, ns, ip))
	{
		return FAIL;
	}

	if (ZBX_DB_OK > DBexecute(
		"insert into items_applications (itemappid,applicationid,itemid) VALUES ('" ZBX_FS_UI64 "',"
			"(SELECT applicationid FROM applications WHERE hostid='%s' AND name='SLV current month'),"
			ZBX_FS_UI64")", DBget_maxid("items_applications"), hostid, itemid))
	{
		return FAIL;
	}

	ZBX_UNUSED(tld);

	return SUCCEED;
}

static int	foreach_probe_nsip_pair(const char *tld, const char *hostid,
		int (*func)(const char *tld, const char *hostid, char *ns, char *ip))
{
	DB_RESULT	result;
	DB_ROW		row;
	char	 	*ns, *ip;

	if (NULL == tld || 0 == tld[0] || NULL == hostid || NULL == func)
		return FAIL;

	result = DBselect("select distinct key_ from items i"
				" left join hosts h on i.hostid=h.hostid"
				" where i.key_ like 'rsm.dns.udp.rtt%%'"
				" and h.name='Template %s'", tld);

	while (NULL != (row = DBfetch(result)))
	{
		ns = NULL;
		ip = NULL;

		if (SUCCEED != extract_nsip_pair_from_rtt_item_key(row[0], &ns, &ip))
			return FAIL;

		if (SUCCEED != func(tld, hostid, ns, ip))
			return FAIL;
	}

	DBfree_result(result);

	return SUCCEED;
}

static int	create_slv_rtt_item(zbx_uint64_t hostid, zbx_uint64_t itemid, int item_type, int item_value_type,
		const char *item_name, const char *item_key, const char *item_units, zbx_uint64_t itemappid,
		zbx_uint64_t applicationid)
{
	if (ZBX_DB_OK > DBexecute(
			"insert into items (itemid,type,snmp_community,snmp_oid,hostid,name,key_,delay,history,trends,"
				"status,value_type,trapper_hosts,units,multiplier,delta,"
				"snmpv3_securityname,snmpv3_securitylevel,snmpv3_authpassphrase,snmpv3_privpassphrase,"
				"formula,error,lastlogsize,logtimefmt,templateid,valuemapid,delay_flex,params,ipmi_sensor,data_type,"
				"authtype,username,password,publickey,privatekey,mtime,flags,interfaceid,port,description,"
				"inventory_link,lifetime,snmpv3_authprotocol,snmpv3_privprotocol,state,snmpv3_contextname,evaltype)"
			" values (" ZBX_FS_UI64 ",%d,'',''," ZBX_FS_UI64 ",'%s','%s','0','90','365',"
				"'0',%d,'','%s','0','0',"
				"'','0','','',"
				"'1','','0','',NULL,NULL,'','','','0',"
				"'0','','','','','0','0',NULL,'','',"
				"'0','30','0','0','0','','0')",
			itemid, item_type, hostid, item_name, item_key, item_value_type, item_units))
	{
		return FAIL;
	}

	if (ZBX_DB_OK > DBexecute(
			"insert into items_applications (itemappid,applicationid,itemid) values (" ZBX_FS_UI64 ","
			ZBX_FS_UI64 "," ZBX_FS_UI64 ")", itemappid, applicationid, itemid))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	create_ratio_of_failed_tests_triggers(zbx_uint64_t itemid, const char *service, const char *macro)
{
	size_t		i;
	zbx_uint64_t	prev_triggerid = 0;

	for (i = 0; i < sizeof(trigger_params) / sizeof(*trigger_params); i++)
	{
		const char	*percent     = trigger_params[i][0];
		const char	*coefficient = trigger_params[i][1];
		const char	*priority    = trigger_params[i][2];

		zbx_uint64_t	functionid = DBget_maxid_num("functions", 1);
		zbx_uint64_t	triggerid  = DBget_maxid_num("triggers", 1);

		if (ZBX_DB_OK > DBexecute("insert into triggers (triggerid,expression,description,url,status,value,"
				"priority,lastchange,comments,error,templateid,type,state,flags) values"
				" (" ZBX_FS_UI64 ",'{" ZBX_FS_UI64 "}>%s%s',"
				"'Ratio of failed %s tests exceeded %s of allowed $1%%','',0,0,%s,0,'','',NULL,0,0,0)",
				triggerid, functionid, macro, coefficient, service, percent, priority))
		{
			return FAIL;
		}

		if (ZBX_DB_OK > DBexecute("insert into functions (functionid,itemid,triggerid,function,parameter) "
				" values (" ZBX_FS_UI64 "," ZBX_FS_UI64 "," ZBX_FS_UI64 ",'last','')",
				functionid, itemid, triggerid))
		{
			return FAIL;
		}

		if (0 != prev_triggerid)
		{
			if (SUCCEED != create_trigger_dependency(triggerid, prev_triggerid))
			{
				return FAIL;
			}
		}

		prev_triggerid = triggerid;
	}

	return SUCCEED;
}

static int	DBpatch_3000307(void)
{
	int		ret = FAIL;
	DB_RESULT	hosts_result;
	DB_ROW		hosts_row;

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	/* get hostid of all hosts that have status = HOST_STATUS_MONITORED and are in "TLDs" group */
	hosts_result = DBselect(
			"select h.hostid"
			" from hosts h,hosts_groups hg"
			" where h.status=0 and hg.hostid=h.hostid and hg.groupid=140");

	while (NULL != (hosts_row = DBfetch(hosts_result)))
	{
		DB_RESULT	result;
		DB_ROW		row;
		zbx_uint64_t	hostid;		/* ID of current host */
		zbx_uint64_t	applicationid;	/* ID of "SLV current month" application on current host */
		zbx_uint64_t	next_itemid;	/* ID of next row in items table */
		zbx_uint64_t	next_itemappid;	/* ID of next row in items_applications table */
		zbx_uint64_t	itemid_udp_performed;
		zbx_uint64_t	itemid_udp_failed;
		zbx_uint64_t	itemid_udp_pfailed;
		zbx_uint64_t	itemid_tcp_performed;
		zbx_uint64_t	itemid_tcp_failed;
		zbx_uint64_t	itemid_tcp_pfailed;

		ZBX_STR2UINT64(hostid, hosts_row[0]);

		/* get ID of "SLV current month" application on current host */

		result = DBselect("select applicationid from applications where hostid=" ZBX_FS_UI64 " and"
				" name='SLV current month'", hostid);

		if (NULL == (row = DBfetch(result)))
		{
			DBfree_result(result);
			goto out;
		}

		ZBX_STR2UINT64(applicationid, row[0]);

		DBfree_result(result);

		/* reserve 6 IDs in "items" and "items_applications" tables */

		next_itemid = DBget_maxid_num("items", 6);
		next_itemappid = DBget_maxid_num("items_applications", 6);

		itemid_udp_performed = next_itemid++;
		itemid_udp_failed    = next_itemid++;
		itemid_udp_pfailed   = next_itemid++;
		itemid_tcp_performed = next_itemid++;
		itemid_tcp_failed    = next_itemid++;
		itemid_tcp_pfailed   = next_itemid++;

		/* create items and link them to "SLV current month" application */

		if (SUCCEED != create_slv_rtt_item(hostid, itemid_udp_performed, 2, 3, "Number of performed monthly DNS UDP tests",
				"rsm.slv.dns.udp.rtt.performed", "", next_itemappid++, applicationid))
		{
			goto out;
		}
		if (SUCCEED != create_slv_rtt_item(hostid, itemid_udp_failed, 2, 3, "Number of failed monthly DNS UDP tests",
				"rsm.slv.dns.udp.rtt.failed", "", next_itemappid++, applicationid))
		{
			goto out;
		}
		if (SUCCEED != create_slv_rtt_item(hostid, itemid_udp_pfailed, 2, 0, "Ratio of failed monthly DNS UDP tests",
				"rsm.slv.dns.udp.rtt.pfailed", "%", next_itemappid++, applicationid))
		{
			goto out;
		}
		if (SUCCEED != create_slv_rtt_item(hostid, itemid_tcp_performed, 2, 3, "Number of performed monthly DNS TCP tests",
				"rsm.slv.dns.tcp.rtt.performed", "", next_itemappid++, applicationid))
		{
			goto out;
		}
		if (SUCCEED != create_slv_rtt_item(hostid, itemid_tcp_failed, 2, 3, "Number of failed monthly DNS TCP tests",
				"rsm.slv.dns.tcp.rtt.failed", "", next_itemappid++, applicationid))
		{
			goto out;
		}
		if (SUCCEED != create_slv_rtt_item(hostid, itemid_tcp_pfailed, 2, 0, "Ratio of failed monthly DNS TCP tests",
				"rsm.slv.dns.tcp.rtt.pfailed", "%", next_itemappid++, applicationid))
		{
			goto out;
		}

		if (SUCCEED != create_ratio_of_failed_tests_triggers(itemid_udp_pfailed, "DNS UDP", "{$RSM.SLV.DNS.UDP.RTT}"))
		{
			goto out;
		}
		if (SUCCEED != create_ratio_of_failed_tests_triggers(itemid_tcp_pfailed, "DNS TCP", "{$RSM.SLV.DNS.TCP.RTT}"))
		{
			goto out;
		}
	}

	ret = SUCCEED;
out:
	DBfree_result(hosts_result);

	return ret;
}

static int	DBpatch_3000308(void)
{
	int		ret = FAIL;
	DB_RESULT	hosts_result;
	DB_ROW		hosts_row;

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	/* get hostid of all hosts that have status = HOST_STATUS_MONITORED,*/
	/* are in "TLDs" group and have either RDDS or RDAP enabled */
	hosts_result = DBselect(
			"select distinct hosts.hostid"
			" from hosts"
				" left join hosts_groups on hosts_groups.hostid=hosts.hostid"
				" left join hosts as templates on templates.host=concat('Template ',hosts.host)"
				" left join hostmacro on hostmacro.hostid=templates.hostid"
			" where hosts.status=0 and"
				" hosts_groups.groupid=140 and"
				" hostmacro.macro in ('{$RSM.TLD.RDDS.ENABLED}','{$RDAP.TLD.ENABLED}') and"
				" hostmacro.value='1'");

	while (NULL != (hosts_row = DBfetch(hosts_result)))
	{
		DB_RESULT	result;
		DB_ROW		row;
		zbx_uint64_t	hostid;		/* ID of current host */
		zbx_uint64_t	applicationid;	/* ID of "SLV current month" application on current host */
		zbx_uint64_t	next_itemid;	/* ID of next row in items table */
		zbx_uint64_t	next_itemappid;	/* ID of next row in items_applications table */
		zbx_uint64_t	itemid_performed;
		zbx_uint64_t	itemid_failed;
		zbx_uint64_t	itemid_pfailed;

		ZBX_STR2UINT64(hostid, hosts_row[0]);

		/* get ID of "SLV current month" application on current host */

		result = DBselect("select applicationid from applications where hostid=" ZBX_FS_UI64 " and"
				" name='SLV current month'", hostid);

		if (NULL == (row = DBfetch(result)))
		{
			DBfree_result(result);
			goto out;
		}

		ZBX_STR2UINT64(applicationid, row[0]);

		DBfree_result(result);

		/* reserve 3 IDs in "items" and "items_applications" tables */

		next_itemid = DBget_maxid_num("items", 3);
		next_itemappid = DBget_maxid_num("items_applications", 3);

		itemid_performed = next_itemid++;
		itemid_failed    = next_itemid++;
		itemid_pfailed   = next_itemid++;

		/* create items and link them to "SLV current month" application */

		if (SUCCEED != create_slv_rtt_item(hostid, itemid_performed, 2, 3, "Number of performed monthly RDDS queries",
				"rsm.slv.rdds.rtt.performed", "", next_itemappid++, applicationid))
		{
			goto out;
		}
		if (SUCCEED != create_slv_rtt_item(hostid, itemid_failed, 2, 3, "Number of failed monthly RDDS queries",
				"rsm.slv.rdds.rtt.failed", "", next_itemappid++, applicationid))
		{
			goto out;
		}
		if (SUCCEED != create_slv_rtt_item(hostid, itemid_pfailed, 2, 0, "Ratio of failed monthly RDDS queries",
				"rsm.slv.rdds.rtt.pfailed", "%", next_itemappid++, applicationid))
		{
			goto out;
		}

		if (SUCCEED != create_ratio_of_failed_tests_triggers(itemid_pfailed, "RDDS", "{$RSM.SLV.RDDS.RTT}"))
		{
			goto out;
		}
	}

	ret = SUCCEED;
out:
	DBfree_result(hosts_result);

	return ret;
}

static int	DBpatch_3000309(void)
{
	const ZBX_TABLE table =
			{"sla_reports", "hostid,year,month", 0,
				{
					{"hostid", NULL, "hosts", "hostid", 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"year", NULL, NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
					{"month", NULL, NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
					{"report", NULL, NULL, NULL, 0, ZBX_TYPE_TEXT, ZBX_NOTNULL, 0},
					{0}
				},
				NULL
			};

	if (SUCCEED != DBcreate_table(&table))
		return FAIL;

	/* add constraint */
	if (ZBX_DB_OK > DBexecute(
			"alter table `sla_reports`"
			" add constraint `c_sla_reports_1`"
				" foreign key (`hostid`) references `hosts` (`hostid`)"
			" on delete cascade"))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_3000310(void)
{
	DB_RESULT	result;
	DB_ROW		row;

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	result = DBselect("select h.host,h.hostid from hosts h inner join hosts_groups hg on h.hostid=hg.hostid"
				" where hg.groupid=140");

	while (NULL != (row = DBfetch(result)))
	{
		if (SUCCEED != foreach_probe_nsip_pair(row[0], row[1], &create_slv_dns_ns_downtime_item))
			return FAIL;
	}

	DBfree_result(result);

	return SUCCEED;
}

static int	DBpatch_3000311(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute("update globalmacro set macro='{$RSM.SLV.NS.DOWNTIME}'"
				" where macro='{$RSM.SLV.NS.AVAIL}'"))
	{
		return FAIL;
	}

	if (ZBX_DB_OK > DBexecute("update items set"
				" key_='rsm.configvalue[RSM.SLV.NS.DOWNTIME]',"
				" params='{$RSM.SLV.NS.DOWNTIME}'"
				" where key_='rsm.configvalue[RSM.SLV.NS.AVAIL]'"))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	create_dns_ns_downtime_trigger(const char *itemid, const char *nsip,
				const char *percent, const char *coefficient, const char *priority,
				zbx_uint64_t *triggerid)
{
	zbx_uint64_t	functionid;

	*triggerid = DBget_maxid("triggers");
	functionid = DBget_maxid("functions");

	if (ZBX_DB_OK > DBexecute(
			"insert into triggers (triggerid,expression,description,"
				"url,status,priority,comments,templateid,type,flags)"
			"values (" ZBX_FS_UI64 ", '{" ZBX_FS_UI64 "}>={$RSM.SLV.NS.DOWNTIME}%s',"
				"'DNS NS %s was unavailable for %s of allowed $1 minutes',"
				"'', '0', '%s', '', NULL, '0', '0')",
			*triggerid, functionid, coefficient, nsip, percent, priority))
	{
		return FAIL;
	}

	if (ZBX_DB_OK > DBexecute(
			"insert into functions (functionid,itemid,triggerid,function,parameter) values"
			" (" ZBX_FS_UI64 ", %s," ZBX_FS_UI64 ",'last','0')",
			functionid, itemid, *triggerid))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	create_dependent_dns_ns_trigger_chain(const char *itemid, const char *nsip)
{
	zbx_uint64_t	triggerid = 0, dependid = 0;
	size_t		i;

	for (i = 0; i < sizeof(trigger_params) / sizeof(*trigger_params); i++)
	{
		const char	*percent     = trigger_params[i][0];
		const char	*coefficient = trigger_params[i][1];
		const char	*priority    = trigger_params[i][2];

		if (SUCCEED != create_dns_ns_downtime_trigger(itemid, nsip, percent, coefficient,
				priority, &triggerid))
		{
			return FAIL;
		}

		if (0 != triggerid && 0 != dependid)
		{
			if (SUCCEED != create_trigger_dependency(triggerid, dependid))
				return FAIL;
		}

		dependid = triggerid;
	}

	return SUCCEED;
}

static int	DBpatch_3000312(void)
{
	DB_RESULT	result;
	DB_ROW		row;
	char		*itemkey;
	size_t		prefixlen, itemkeylen;
	const char	*key_prefix = "rsm.slv.dns.ns.downtime[";

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	prefixlen = strlen(key_prefix);

	result = DBselect(
			"select i.itemid,i.key_"
			" from items i"
			" left join hosts_groups hg on hg.hostid=i.hostid"
			" where i.key_ like '%s%%'"
				" and hg.groupid=140", key_prefix);

	while (NULL != (row = DBfetch(result)))
	{
		itemkey = zbx_strdup(NULL, row[1]);
		itemkeylen = strlen(itemkey);

		if (itemkeylen < (prefixlen + 2)) /* +2 for closing ] and at least on char inside */
		{
			zabbix_log(LOG_LEVEL_CRIT, "bogus item key too short");
			return FAIL;
		}

		itemkey[itemkeylen - 1] = 0; /* overwrite closing ] */

		create_dependent_dns_ns_trigger_chain(row[0], itemkey + prefixlen);

		zbx_free(itemkey);
	}

	return SUCCEED;
}

static int	DBpatch_3000313(void)
{
	DB_RESULT	result;
	DB_ROW		row;

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	result = DBselect("select h.host,h.hostid from hosts h inner join hosts_groups hg on h.hostid=hg.hostid"
				" where hg.groupid=140");

	while (NULL != (row = DBfetch(result)))
	{
		if (SUCCEED != foreach_probe_nsip_pair(row[0], row[1], &create_slv_dns_ns_avail_item))
			return FAIL;
	}

	DBfree_result(result);

	return SUCCEED;
}

static int	DBpatch_3000314(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute(
			"update auditlog"
			" set resourceid=substring_index(details,':',1)"
			" where resourcetype=32 and resourceid=0"))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_3000315(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute(
			"update items set value_type=3 where key_ like 'rsm.slv.dns.ns.%%' and value_type=0"
	))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_3000316(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	/* update global macro */
	if (ZBX_DB_OK > DBexecute("update globalmacro set value = 3600 where macro = '{$RSM.DNS.TCP.DELAY}'"))
	{
		return FAIL;
	}

	/* update item update intervals */
	if (ZBX_DB_OK > DBexecute("update items set delay = 3600 where key_ like 'rsm.dns.tcp[%%]'"))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_3000317(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute("update items set delay = 60 where hostid = 100000"))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	add_global_macro_history_item(zbx_uint64_t itemid, zbx_uint64_t itemappid, const char *macro)
{
	/* constant IDs are from data.tmpl */

	if (ZBX_DB_OK > DBexecute(
			"insert into items (itemid,type,snmp_community,snmp_oid,hostid,name,key_,delay,history,trends,"
				"status,value_type,trapper_hosts,units,multiplier,delta,"
				"snmpv3_securityname,snmpv3_securitylevel,snmpv3_authpassphrase,snmpv3_privpassphrase,"
				"formula,logtimefmt,templateid,valuemapid,delay_flex,params,ipmi_sensor,data_type,"
				"authtype,username,password,publickey,privatekey,flags,interfaceid,port,description,"
				"inventory_link,lifetime,snmpv3_authprotocol,snmpv3_privprotocol,snmpv3_contextname,evaltype)"
			" values (" ZBX_FS_UI64 ",15,'','',100000,'$1 value','rsm.configvalue[%s]',60,7,365,"
				"0,3,'','',0,0,"
				"'',0,'','',"
				"1,'',NULL,NULL,'','{$%s}','',0,"
				"0,'','','','',0,NULL,'','',"
				"0,0,0,0,'',0)",
			itemid, macro, macro))
	{
		return FAIL;
	}

	if (ZBX_DB_OK > DBexecute(
			"insert into items_applications (itemappid,applicationid,itemid)"
			" values (" ZBX_FS_UI64 ",1000," ZBX_FS_UI64 ")",
			itemappid, itemid))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_3000318(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	/* constant IDs are from data.tmpl */

	if (FAIL == add_global_macro_history_item(100026, 100026, "RSM.SLV.DNS.DOWNTIME"))
	{
		return FAIL;
	}
	if (FAIL == add_global_macro_history_item(100027, 100027, "RSM.DNS.TCP.RTT.LOW"))
	{
		return FAIL;
	}
	if (FAIL == add_global_macro_history_item(100028, 100028, "RSM.DNS.UDP.RTT.LOW"))
	{
		return FAIL;
	}
	if (FAIL == add_global_macro_history_item(100029, 100029, "RSM.SLV.RDDS.DOWNTIME"))
	{
		return FAIL;
	}
	if (FAIL == add_global_macro_history_item(100030, 100030, "RSM.SLV.RDDS.RTT"))
	{
		return FAIL;
	}
	if (FAIL == add_global_macro_history_item(100031, 100031, "RSM.RDDS.RTT.LOW"))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_3000319(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute(
			"update items"
			" set name='Ratio of failed monthly RDDS queries'"
			" where name like 'Ratio of failed monthly RDDS queries %%'"))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_3000320(void)
{
	/* patch for both server and proxy */

	if (ZBX_DB_OK > DBexecute(
			"create table rsmhost_dns_ns_log ("
				"itemid bigint(20) unsigned not null,"
				"clock int(11) not null,"
				"action int(11) not null,"
				"primary key (itemid,clock),"
				"constraint c_rsmhost_dns_ns_log_1 foreign key (itemid) references items (itemid) on delete cascade"
			")"))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_3000321(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute(
			"insert into rsmhost_dns_ns_log"
			" select"
				" itemid,"
				"1546300800," /* 2019-01-01 00:00:00 UTC */
				"case status"
					" when 0 then 1" /* enable */
					" when 1 then 2" /* disable */
				" end"
			" from items"
			" where key_ like 'rsm.slv.dns.ns.downtime[%%,%%]'"))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_3000322(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

#define CHECK(CODE) do {                \
	int result = (CODE);            \
	if (ZBX_DB_OK > result)         \
	{                               \
		return FAIL;            \
	}                               \
} while (0)

	/* set item's "Update interval" to 0 seconds for ITEM_TYPE_TRAPPER items */
	CHECK(DBexecute("update items set delay=0 where type=2"));

	/* set item's "Store value" to "As is" for *.rtt.performed, *.rtt.failed and *.rtt.pfailed items */
	CHECK(DBexecute("update items set delta=0 where key_ like 'rsm.slv.%%.rtt.%%'"));

	return SUCCEED;

#undef CHECK
}

static int	DBpatch_3000323(void)
{
	/* patch for both server and proxy */

	if (ZBX_DB_OK > DBexecute(
			"alter table hosts"
			" add column created int(11) not null default '0' after hostid"))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_3000324(void)
{
	const char	*command = "/opt/zabbix/scripts/tlds-notification.pl --send-to \\'zabbix alert\\' --event-id \\'{EVENT.ID}\\' &";

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

#define CHECK(CODE) do {                \
	int result = (CODE);            \
	if (ZBX_DB_OK > result)         \
	{                               \
		return FAIL;            \
	}                               \
} while (0)

	/* delete action condition "Trigger value = PROBLEM" */
	CHECK(DBexecute("delete from conditions where conditionid=131"));

	/* delete data of current "Send message" operation type */
	CHECK(DBexecute("delete from opmessage_usr where operationid=130"));
	CHECK(DBexecute("delete from opmessage where operationid=130"));

	/* update operation type to "Remote command" */
	CHECK(DBexecute("update operations set operationtype=1 where operationid=130"));

	/* unset action's "recovery message", clear message subjects and bodies */
	CHECK(DBexecute("update actions set def_shortdata='',def_longdata='',recovery_msg=0,r_shortdata='',r_longdata='' where actionid=130"));

	/* create custom script that needs to be executed on the server */
	CHECK(DBexecute("insert into opcommand set operationid=130,type=0,scriptid=NULL,execute_on=1,port='',"
			"authtype=0,username='',password='',publickey='',privatekey='',"
			"command='%s'", command));
	CHECK(DBexecute("insert into opcommand_hst set opcommand_hstid=130,operationid=130,hostid=NULL"));

	return SUCCEED;

#undef CHECK
}

static int	DBpatch_3000400(void)
{
	return SUCCEED;
}

static int	DBpatch_3000401(void)
{
#define RESERVE_GLOBALMACROID									\
		"update globalmacro"								\
		" set globalmacroid=(select nextid from ("					\
			"select max(globalmacroid)+1 as nextid from globalmacro) as tmp)"	\
		" where globalmacroid=%u"

	DB_RESULT	result;
	DB_ROW		row;
	const char	*monitoring_target;

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute(RESERVE_GLOBALMACROID, 105))
		return FAIL;

	/* get hostid of all hosts that have status = HOST_STATUS_MONITORED and are in "TLDs" group */
	result = DBselect("select count(*) from hosts h,hosts_groups hg where hg.hostid=h.hostid and hg.groupid=140");

	if (NULL == (row = DBfetch(result)))
	{
		/* this means the above SQL is faulty, which shouldn't happen */
		monitoring_target = NULL;
	}
	else if (atoi(row[0]) == 0)
	{
		/* new or empty installation, monitoring target unknown */
		monitoring_target = "";
	}
	else
	{
		/* existing installation, monitoring target is "Registry" */
		monitoring_target = "registry";
	}

	DBfree_result(result);

	if (NULL == monitoring_target)
	{
		zabbix_log(LOG_LEVEL_CRIT, "error while trying to list hosts of hostgroup with ID 140");
		return FAIL;
	}

	if (ZBX_DB_OK > DBexecute(
			"insert into globalmacro (globalmacroid,macro,value)"
			" values (105,'{$RSM.MONITORING.TARGET}','%s')",
			monitoring_target))
	{
		return FAIL;
	}

	return SUCCEED;

#undef RESERVE_GLOBALMACROID
}

static int	DBpatch_3000402(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute("update usrgrp set name = 'Read-only user' where usrgrpid = 100"))
	{
		return FAIL;
	}
	if (ZBX_DB_OK > DBexecute("update usrgrp set name = 'Power user' where usrgrpid = 110"))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_3000403(void)
{
	/* patch for both server and proxy */

	if (ZBX_DB_OK > DBexecute(
			"alter table hosts"
			" add column info_1 varchar(128) collate utf8_bin not null default '' after name,"
			" add column info_2 varchar(128) collate utf8_bin not null default '' after info_1"))
	{
		return FAIL;
	}

	return SUCCEED;
}

static zbx_uint64_t	get_applicationid(zbx_uint64_t hostid, const char *name)
{
	zbx_uint64_t	applicationid = 0;
	DB_RESULT	result;
	DB_ROW		row;

	result = DBselect("select applicationid from applications where hostid=" ZBX_FS_UI64 " and name='%s'",
			hostid, name);

	if (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(applicationid, row[0]);
	}

	DBfree_result(result);

	return applicationid;
}

static int	create_standalone_rdap_item(zbx_uint64_t itemid, zbx_uint64_t hostid, const char *name, const char *key,
		int value_type, const char *units, zbx_uint64_t value_map_id,
		zbx_uint64_t applicationid, zbx_uint64_t itemappid)
{
#define CHECK(CODE) do {                \
	int result = (CODE);            \
	if (ZBX_DB_OK > result)         \
	{                               \
		return FAIL;            \
	}                               \
} while (0)

	char	value_map_id_str[ZBX_MAX_UINT64_LEN + 1];

	if (0 == value_map_id)
	{
		zbx_snprintf(value_map_id_str, sizeof(value_map_id_str), "NULL");
	}
	else
	{
		zbx_snprintf(value_map_id_str, sizeof(value_map_id_str), ZBX_FS_UI64, value_map_id);
	}

	CHECK(DBexecute("insert into items (itemid,type,snmp_community,snmp_oid,hostid,name,key_,delay,history,trends,"
				"status,value_type,trapper_hosts,units,multiplier,delta,"
				"snmpv3_securityname,snmpv3_securitylevel,snmpv3_authpassphrase,snmpv3_privpassphrase,"
				"formula,error,lastlogsize,logtimefmt,templateid,valuemapid,delay_flex,params,"
				"ipmi_sensor,data_type,authtype,username,password,publickey,privatekey,mtime,flags,"
				"interfaceid,port,description,inventory_link,lifetime,snmpv3_authprotocol,"
				"snmpv3_privprotocol,state,snmpv3_contextname,evaltype)"
			" values (" ZBX_FS_UI64 ",2,'',''," ZBX_FS_UI64 ",'%s','%s',0,90,365,"
				"1,%d,'','%s',0,0,"
				"'',0,'','',"
				"'','',0,'',NULL,%s,'','',"
				"'',0,0,'','','','',0,0,"
				"NULL,'','',0,'30',0,"
				"0,0,'',0)",
			itemid, hostid, name, key, value_type, units, value_map_id_str));

	CHECK(DBexecute("insert into items_applications (itemappid,applicationid,itemid)"
			" values (" ZBX_FS_UI64 "," ZBX_FS_UI64 "," ZBX_FS_UI64 ")",
			itemappid, applicationid, itemid));

	return SUCCEED;

#undef CHECK
}

static int	create_avail_trigger(zbx_uint64_t itemid, const char *service, const char *fail_macro, const char *recover_macro)
{
#define CHECK(CODE) do {                \
	int result = (CODE);            \
	if (ZBX_DB_OK > result)         \
	{                               \
		return FAIL;            \
	}                               \
} while (0)

	zbx_uint64_t	triggerid    = DBget_maxid_num("triggers", 1);
	zbx_uint64_t	functionid_1 = DBget_maxid_num("functions", 1);
	zbx_uint64_t	functionid_2 = DBget_maxid_num("functions", 1);

	char	expression[MAX_STRING_LEN];
	char	description[MAX_STRING_LEN];
	char	fail_param[MAX_STRING_LEN];
	char	recover_param[MAX_STRING_LEN];

	zbx_snprintf(expression, sizeof(expression),
			"({TRIGGER.VALUE}=0 and {" ZBX_FS_UI64 "}=0) or"
			" ({TRIGGER.VALUE}=1 and {" ZBX_FS_UI64 "}>0)",
			functionid_1, functionid_2);
	zbx_snprintf(description, sizeof(description), "%s service is down", service);
	zbx_snprintf(fail_param, sizeof(fail_param), "#%s", fail_macro);
	zbx_snprintf(recover_param, sizeof(recover_param), "#%s,0,\"eq\"", recover_macro);

	CHECK(DBexecute("insert into triggers (triggerid,expression,description,url,status,value,"
				"priority,lastchange,comments,error,templateid,type,state,flags)"
			" values (" ZBX_FS_UI64 ",'%s','%s','',0,0,0,0,'','',NULL,0,0,0)",
			triggerid, expression, description));
	CHECK(DBexecute("insert into functions (functionid,itemid,triggerid,function,parameter)"
			" values (" ZBX_FS_UI64 "," ZBX_FS_UI64 "," ZBX_FS_UI64 ",'%s','%s')",
			functionid_1, itemid, triggerid, "max", fail_param));
	CHECK(DBexecute("insert into functions (functionid,itemid,triggerid,function,parameter)"
			" values (" ZBX_FS_UI64 "," ZBX_FS_UI64 "," ZBX_FS_UI64 ",'%s','%s')",
			functionid_2, itemid, triggerid, "count", recover_param));

	return SUCCEED;

#undef CHECK
}

static int	create_downtime_triggers(zbx_uint64_t itemid, const char *service, const char *downtime_macro)
{
#define CHECK(CODE) do {                \
	int result = (CODE);            \
	if (ZBX_DB_OK > result)         \
	{                               \
		return FAIL;            \
	}                               \
} while (0)

	size_t		i;
	zbx_uint64_t	prev_triggerid = 0;

	for (i = 0; i < sizeof(trigger_params) / sizeof(*trigger_params); i++)
	{
		const char	*percent     = trigger_params[i][0];
		const char	*coefficient = trigger_params[i][1];
		const char	*priority    = trigger_params[i][2];

		zbx_uint64_t	triggerid  = DBget_maxid_num("triggers", 1);
		zbx_uint64_t	functionid = DBget_maxid_num("functions", 1);
		char		expression[MAX_STRING_LEN];
		char		description[MAX_STRING_LEN];

		zbx_snprintf(expression, sizeof(expression),
				"{" ZBX_FS_UI64 "}>=%s%s", functionid, downtime_macro, coefficient);
		zbx_snprintf(description, sizeof(description),
				"%s service was unavailable for %s of allowed $1 minutes", service, percent);

		CHECK(DBexecute("insert into triggers (triggerid,expression,description,url,status,value,"
				"priority,lastchange,comments,error,templateid,type,state,flags)"
				" values (" ZBX_FS_UI64 ",'%s','%s','',0,0,%s,0,'','',NULL,0,0,0)",
				triggerid, expression, description, priority));
		CHECK(DBexecute("insert into functions (functionid,itemid,triggerid,function,parameter)"
				" values (" ZBX_FS_UI64 "," ZBX_FS_UI64 "," ZBX_FS_UI64 ",'%s','%s')",
				functionid, itemid, triggerid, "last", "0"));

		if (0 != prev_triggerid)
		{
			if (SUCCEED != create_trigger_dependency(triggerid, prev_triggerid))
			{
				return FAIL;
			}
		}

		prev_triggerid = triggerid;
	}

	return SUCCEED;

#undef CHECK
}

static int	create_rollweek_triggers(zbx_uint64_t itemid, const char *service)
{
#define CHECK(CODE) do {                \
	int result = (CODE);            \
	if (ZBX_DB_OK > result)         \
	{                               \
		return FAIL;            \
	}                               \
} while (0)

	size_t		i;
	zbx_uint64_t	prev_triggerid = 0;

	for (i = 0; i < sizeof(trigger_params) / sizeof(*trigger_params); i++)
	{
		const char	*percent     = trigger_params[i][0];
		const char	*priority    = trigger_params[i][2];

		zbx_uint64_t	triggerid  = DBget_maxid_num("triggers", 1);
		zbx_uint64_t	functionid = DBget_maxid_num("functions", 1);
		char		expression[MAX_STRING_LEN];
		char		description[MAX_STRING_LEN];

		zbx_snprintf(expression, sizeof(expression), "{" ZBX_FS_UI64 "}>=%s", functionid, percent);
		zbx_snprintf(description, sizeof(description), "%s rolling week is over %s", service, percent);

		CHECK(DBexecute("insert into triggers (triggerid,expression,description,url,status,value,"
				"priority,lastchange,comments,error,templateid,type,state,flags)"
				" values (" ZBX_FS_UI64 ",'%s','%s','',0,0,%s,0,'','',NULL,0,0,0)",
				triggerid, expression, description, priority));
		CHECK(DBexecute("insert into functions (functionid,itemid,triggerid,function,parameter)"
				" values (" ZBX_FS_UI64 "," ZBX_FS_UI64 "," ZBX_FS_UI64 ",'%s','%s')",
				functionid, itemid, triggerid, "last", "0"));

		if (0 != prev_triggerid)
		{
			if (SUCCEED != create_trigger_dependency(triggerid, prev_triggerid))
			{
				return FAIL;
			}
		}

		prev_triggerid = triggerid;
	}

	return SUCCEED;

#undef CHECK
}

static int	DBpatch_3000500(void)
{
	return SUCCEED;
}

static int	DBpatch_3000501(void)
{
	int		ret = FAIL;
	DB_RESULT	hosts_result;
	DB_ROW		hosts_row;

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

#define CREATE_GLOBALMACRO(globalmacroid, macro, value)                                 \
do {                                                                                    \
	if (ZBX_DB_OK > DBexecute(                                                      \
			"update globalmacro set globalmacroid=("                        \
				"select nextid from ("                                  \
					"select max(globalmacroid)+1 as nextid"         \
					" from globalmacro"                             \
				") as tmp"                                              \
			") where globalmacroid=%u", globalmacroid))                     \
	{                                                                               \
		return FAIL;                                                            \
	}                                                                               \
	if (ZBX_DB_OK > DBexecute(                                                      \
			"insert into globalmacro (globalmacroid,macro,value)"           \
			" values (%u,'%s','%s')",                                       \
			globalmacroid, macro, value))                                   \
	{                                                                               \
		return FAIL;                                                            \
	}                                                                               \
} while (0)

	CREATE_GLOBALMACRO(106, "{$RSM.INCIDENT.RDAP.FAIL}"   , "2");
	CREATE_GLOBALMACRO(107, "{$RSM.INCIDENT.RDAP.RECOVER}", "2");
	CREATE_GLOBALMACRO(108, "{$RSM.RDAP.DELAY}"           , "300");
	CREATE_GLOBALMACRO(109, "{$RSM.RDAP.MAXREDIRS}"       , "10");
	CREATE_GLOBALMACRO(110, "{$RSM.RDAP.PROBE.ONLINE}"    , "10");
	CREATE_GLOBALMACRO(111, "{$RSM.RDAP.ROLLWEEK.SLA}"    , "1440");
	CREATE_GLOBALMACRO(112, "{$RSM.RDAP.RTT.HIGH}"        , "10000");
	CREATE_GLOBALMACRO(113, "{$RSM.RDAP.RTT.LOW}"         , "5000");
	CREATE_GLOBALMACRO(114, "{$RSM.RDAP.STANDALONE}"      , "0");
	CREATE_GLOBALMACRO(115, "{$RSM.SLV.RDAP.DOWNTIME}"    , "864");
	CREATE_GLOBALMACRO(116, "{$RSM.SLV.RDAP.RTT}"         , "5");

#undef CREATE_GLOBALMACRO

	/* get hostid of all hosts that are in "TLDs" group */
	hosts_result = DBselect(
			"select hosts.hostid"
			" from hosts"
			" left join hosts_groups on hosts_groups.hostid=hosts.hostid"
			" where hosts_groups.groupid=140");

#define CHECK(CODE) do {                \
	int result = (CODE);            \
	if (SUCCEED != result)          \
	{                               \
		goto out;               \
	}                               \
} while (0)

	while (NULL != (hosts_row = DBfetch(hosts_result)))
	{
		zbx_uint64_t	hostid;

		zbx_uint64_t	appid_current_month;
		zbx_uint64_t	appid_particular_test;
		zbx_uint64_t	appid_rolling_week;

		zbx_uint64_t	itemid_base;
		zbx_uint64_t	itemid_avail;
		zbx_uint64_t	itemid_downtime;
		zbx_uint64_t	itemid_rollweek;
		zbx_uint64_t	itemid_rtt_failed;
		zbx_uint64_t	itemid_rtt_performed;
		zbx_uint64_t	itemid_rtt_pfailed;

		zbx_uint64_t	itemappid_base;
		zbx_uint64_t	itemappid_avail;
		zbx_uint64_t	itemappid_downtime;
		zbx_uint64_t	itemappid_rollweek;
		zbx_uint64_t	itemappid_rtt_failed;
		zbx_uint64_t	itemappid_rtt_performed;
		zbx_uint64_t	itemappid_rtt_pfailed;

		ZBX_STR2UINT64(hostid, hosts_row[0]);

		appid_current_month   = get_applicationid(hostid, "SLV current month");
		appid_particular_test = get_applicationid(hostid, "SLV particular test");
		appid_rolling_week    = get_applicationid(hostid, "SLV rolling week");

		if (0 == appid_current_month || 0 == appid_particular_test || 0 == appid_rolling_week)
		{
			goto out;
		}

		itemid_base             = DBget_maxid_num("items", 6);
		itemid_avail            = itemid_base++;
		itemid_downtime         = itemid_base++;
		itemid_rollweek         = itemid_base++;
		itemid_rtt_failed       = itemid_base++;
		itemid_rtt_performed    = itemid_base++;
		itemid_rtt_pfailed      = itemid_base++;

		itemappid_base          = DBget_maxid_num("items_applications", 6);
		itemappid_avail         = itemappid_base++;
		itemappid_downtime      = itemappid_base++;
		itemappid_rollweek      = itemappid_base++;
		itemappid_rtt_failed    = itemappid_base++;
		itemappid_rtt_performed = itemappid_base++;
		itemappid_rtt_pfailed   = itemappid_base++;

		CHECK(create_standalone_rdap_item(itemid_avail, hostid,
				"RDAP availability", "rsm.slv.rdap.avail", 3, "", 110,
				appid_particular_test, itemappid_avail));
		CHECK(create_standalone_rdap_item(itemid_downtime, hostid,
				"RDAP minutes of downtime", "rsm.slv.rdap.downtime", 3, "", 0,
				appid_current_month, itemappid_downtime));
		CHECK(create_standalone_rdap_item(itemid_rollweek, hostid,
				"RDAP weekly unavailability", "rsm.slv.rdap.rollweek", 0, "%", 0,
				appid_rolling_week, itemappid_rollweek));
		CHECK(create_standalone_rdap_item(itemid_rtt_failed, hostid,
				"Number of failed monthly RDAP queries", "rsm.slv.rdap.rtt.failed", 3, "", 0,
				appid_current_month, itemappid_rtt_failed));
		CHECK(create_standalone_rdap_item(itemid_rtt_performed, hostid,
				"Number of performed monthly RDAP queries", "rsm.slv.rdap.rtt.performed", 3, "", 0,
				appid_current_month, itemappid_rtt_performed));
		CHECK(create_standalone_rdap_item(itemid_rtt_pfailed, hostid,
				"Ratio of failed monthly RDAP queries", "rsm.slv.rdap.rtt.pfailed", 0, "%", 0,
				appid_current_month, itemappid_rtt_pfailed));

		CHECK(create_avail_trigger(itemid_avail, "RDAP", "{$RSM.INCIDENT.RDAP.FAIL}", "{$RSM.INCIDENT.RDAP.RECOVER}"));
		CHECK(create_downtime_triggers(itemid_downtime, "RDAP", "{$RSM.SLV.RDAP.DOWNTIME}"));
		CHECK(create_rollweek_triggers(itemid_rollweek, "RDAP"));
		CHECK(create_ratio_of_failed_tests_triggers(itemid_rtt_pfailed, "RDAP", "{$RSM.SLV.RDAP.RTT}"));
	}

#undef CHECK

	ret = SUCCEED;
out:
	DBfree_result(hosts_result);

	return ret;
}

static int	DBpatch_3000502(void)
{
	int		ret = FAIL;
	DB_RESULT	result;
	DB_ROW		row;

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	result = DBselect("select hostid, value from hostmacro where macro = '{$RSM.RDDS.ENABLED}'");

	while (NULL != (row = DBfetch(result)))
	{
		if (ZBX_DB_OK > DBexecute("insert into hostmacro (hostmacroid,hostid,macro,value)"
				" values (" ZBX_FS_UI64 ",%s,'%s','%s')",
				DBget_maxid_num("hostmacro", 1), row[0], "{$RSM.RDAP.ENABLED}", row[1]))
		{
			goto out;
		}
	}

	ret = SUCCEED;
out:
	DBfree_result(result);

	return ret;
}

static int	DBpatch_3000503(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute("update items set"
			" key_=replace(key_,'{$RSM.RDDS.','{$RSM.RDAP.')"
			" where key_ like 'rdap[%%]'"))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_3000504(void)
{
	DB_RESULT	result = NULL;
	DB_ROW		row;
	zbx_uint64_t	cat_id;
	int		ret = FAIL;

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	/* Add necessary extra RDAP macros. Constant IDs are from data.tmpl */

	if (FAIL == add_global_macro_history_item(100032, 100032, "RSM.INCIDENT.RDAP.FAIL"))
	{
		goto out;
	}
	if (FAIL == add_global_macro_history_item(100033, 100033, "RSM.INCIDENT.RDAP.RECOVER"))
	{
		goto out;
	}
	if (FAIL == add_global_macro_history_item(100034, 100034, "RSM.RDAP.DELAY"))
	{
		goto out;
	}
	if (FAIL == add_global_macro_history_item(100035, 100035, "RSM.RDAP.MAXREDIRS"))
	{
		goto out;
	}
	if (FAIL == add_global_macro_history_item(100036, 100036, "RSM.RDAP.PROBE.ONLINE"))
	{
		goto out;
	}
	if (FAIL == add_global_macro_history_item(100037, 100037, "RSM.RDAP.ROLLWEEK.SLA"))
	{
		goto out;
	}
	if (FAIL == add_global_macro_history_item(100038, 100038, "RSM.RDAP.RTT.HIGH"))
	{
		goto out;
	}
	if (FAIL == add_global_macro_history_item(100039, 100039, "RSM.RDAP.RTT.LOW"))
	{
		goto out;
	}
	if (FAIL == add_global_macro_history_item(100040, 100040, "RSM.SLV.RDAP.DOWNTIME"))
	{
		goto out;
	}
	if (FAIL == add_global_macro_history_item(100041, 100041, "RSM.SLV.RDAP.RTT"))
	{
		goto out;
	}

	/* add 'rdap' service category for data export */

	if (NULL == (result = DBselect("select id from rsm_service_category where name='rdap'")))
	{
		goto out;
	}

	if (NULL == (row = DBfetch(result)))
	{
		if (ZBX_DB_OK > DBexecute("insert into rsm_service_category values(6,'rdap')"))
		{
			goto out;
		}
	}
	else
	{
		ZBX_STR2UINT64(cat_id, row[0]);
		if (6 != cat_id)
		{
			zabbix_log(LOG_LEVEL_ERR, "'rdap' service category exists, but it's not 6");
			goto out;
		}
	}

	ret = SUCCEED;
out:
	DBfree_result(result);
	return ret;
}

static int	DBpatch_3000505(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (ZBX_DB_OK > DBexecute("update items set type=5 where type=0 and key_='zabbix[host,agent,available]'"))
		return FAIL;

	return SUCCEED;
}

static int	DBpatch_3000506(void)
{
	int		ret = FAIL;

	zbx_uint64_t	templateid_template_rdap  = 99980;
	zbx_uint64_t	itemid_rdap_target        = 99984;
	zbx_uint64_t	itemid_rdap_testedname    = 99985;
	zbx_uint64_t	itemappid_rdap_target     = 99984;
	zbx_uint64_t	itemappid_rdap_testedname = 99985;
	zbx_uint64_t	applicationid_rdap        = 998;

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

#define CHECK(CODE) do {		\
	int __result = (CODE);		\
	if (ZBX_DB_OK > __result)	\
	{				\
		goto out;		\
	}				\
} while (0)

#define SQL	"insert into items set itemid=" ZBX_FS_UI64 ",type=%d,snmp_community='',snmp_oid='',"			\
		"hostid=" ZBX_FS_UI64 ",name='%s',key_='%s',delay=0,history=%d,trends=365,status=0,value_type=%d,"	\
		"trapper_hosts='',units='',multiplier=0,delta=0,snmpv3_securityname='',snmpv3_securitylevel=0,"		\
		"snmpv3_authpassphrase='',snmpv3_privpassphrase='',formula='%s',error='',lastlogsize=0,logtimefmt='',"	\
		"templateid=NULL,valuemapid=NULL,delay_flex='',params='',ipmi_sensor='',data_type=0,authtype=0,"	\
		"username='',password='',publickey='',privatekey='',mtime=0,flags=0,interfaceid=NULL,port='',"		\
		"description='%s',inventory_link=0,lifetime='30',snmpv3_authprotocol=0,snmpv3_privprotocol=0,state=0,"	\
		"snmpv3_contextname='',evaltype=0"
	/* type 2 = ITEM_TYPE_TRAPPER */
	/* value_type 1 = ITEM_VALUE_TYPE_STR */
	CHECK(DBexecute(SQL, itemid_rdap_target, 2, templateid_template_rdap, "RDAP target", "rdap.target",
			7, 1, "1", "The base URL used in Registration Data Access Protocol service provider test."));
	CHECK(DBexecute(SQL, itemid_rdap_testedname, 2, templateid_template_rdap, "RDAP tested name", "rdap.testedname",
			7, 1, "1", "The domain name used in Registration Data Access Protocol service provider test."));
#undef SQL

#define SQL	"insert into items_applications set itemappid=" ZBX_FS_UI64 ",applicationid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64
	CHECK(DBexecute(SQL, itemappid_rdap_target, applicationid_rdap, itemid_rdap_target));
	CHECK(DBexecute(SQL, itemappid_rdap_testedname, applicationid_rdap, itemid_rdap_testedname));
#undef SQL

#undef CHECK

	ret = SUCCEED;
out:
	return ret;
}

static int	DBpatch_3000507(void)
{
	DB_RESULT	result = NULL;
	DB_ROW		row;
	int		ret = FAIL;

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	/* Loop through all "Template <RSMHOST>" templates that have RDDS43 and RDDS80 applications. */
	/* Having these applications indicates that RSMHOST has had RDDS enabled at some point of time. */
	/* status 3 = HOST_STATUS_TEMPLATE */
	result = DBselect("select"
				" hosts.hostid,"
				"app_rdds43.applicationid,"
				"app_rdds80.applicationid,"
				"hostmacro.value"
			" from"
				" hosts"
				" left join applications as app_rdds43 on app_rdds43.hostid=hosts.hostid"
				" left join applications as app_rdds80 on app_rdds80.hostid=hosts.hostid"
				" left join hostmacro on hostmacro.hostid=hosts.hostid"
			" where"
				" hosts.status=3 and"
				" app_rdds43.name='RDDS43' and"
				" app_rdds80.name='RDDS80' and"
				" hostmacro.macro='{$RSM.TLD.RDDS.ENABLED}'");

	if (NULL == result)
		goto out;

	while (NULL != (row = DBfetch(result)))
	{
		zbx_uint64_t	hostid;
		zbx_uint64_t	applicationid_rdds43;
		zbx_uint64_t	applicationid_rdds80;
		int		status;
		zbx_uint64_t	itemid_next;
		zbx_uint64_t	itemid_rdds43_target;
		zbx_uint64_t	itemid_rdds43_testedname;
		zbx_uint64_t	itemid_rdds80_target;

		ZBX_STR2UINT64(hostid, row[0]);
		ZBX_STR2UINT64(applicationid_rdds43, row[1]);
		ZBX_STR2UINT64(applicationid_rdds80, row[2]);

		if (0 == strcmp(row[3], "0"))
			/* ITEM_STATUS_DISABLED */
			status = 1;
		else if (0 == strcmp(row[3], "1"))
			/* ITEM_STATUS_ACTIVE */
			status = 0;
		else
			/* unexpected value of the macro */
			goto out;

		itemid_next              = DBget_maxid_num("items", 3);
		itemid_rdds43_target     = itemid_next++;
		itemid_rdds43_testedname = itemid_next++;
		itemid_rdds80_target     = itemid_next++;

#define CHECK(CODE) do {		\
	int __result = (CODE);		\
	if (ZBX_DB_OK > __result)	\
	{				\
		goto out;		\
	}				\
} while (0)

#define SQL	"insert into items set itemid=" ZBX_FS_UI64 ",type=%d,snmp_community='',snmp_oid='',"			\
		"hostid=" ZBX_FS_UI64 ",name='%s',key_='%s',delay=0,history=90,trends=%d,status=%d,value_type=%d,"	\
		"trapper_hosts='',units='',multiplier=0,delta=0,snmpv3_securityname='',snmpv3_securitylevel=0,"		\
		"snmpv3_authpassphrase='',snmpv3_privpassphrase='',formula='%s',error='',lastlogsize=0,logtimefmt='',"	\
		"templateid=NULL,valuemapid=NULL,delay_flex='',params='',ipmi_sensor='',data_type=0,authtype=0,"	\
		"username='',password='',publickey='',privatekey='',mtime=0,flags=0,interfaceid=NULL,port='',"		\
		"description='',inventory_link=0,lifetime='30',snmpv3_authprotocol=0,snmpv3_privprotocol=0,state=0,"	\
		"snmpv3_contextname='',evaltype=0"
		/* type 2 = ITEM_TYPE_TRAPPER */
		/* value_type 1 = ITEM_VALUE_TYPE_STR */
		CHECK(DBexecute(SQL, itemid_rdds43_target    , 2, hostid, "RDDS43 target"     , "rsm.rdds.43.target"    , 0, status, 1, "1"));
		CHECK(DBexecute(SQL, itemid_rdds43_testedname, 2, hostid, "RDDS43 tested name", "rsm.rdds.43.testedname", 0, status, 1, "1"));
		CHECK(DBexecute(SQL, itemid_rdds80_target    , 2, hostid, "RDDS80 target"     , "rsm.rdds.80.target"    , 0, status, 1, "1"));
#undef SQL

#define SQL	"insert into items_applications set itemappid=" ZBX_FS_UI64 ",applicationid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64
		CHECK(DBexecute(SQL, DBget_maxid_num("items_applications", 1), applicationid_rdds43, itemid_rdds43_target));
		CHECK(DBexecute(SQL, DBget_maxid_num("items_applications", 1), applicationid_rdds43, itemid_rdds43_testedname));
		CHECK(DBexecute(SQL, DBget_maxid_num("items_applications", 1), applicationid_rdds80, itemid_rdds80_target));
#undef SQL

#undef CHECK
	}

	ret = SUCCEED;
out:
	DBfree_result(result);

	return ret;
}

static int	DBpatch_3000508(void)
{
	int		ret = FAIL;
	DB_RESULT	result = NULL;
	DB_ROW		row;

	zbx_uint64_t	templateid_template_rdap        = 99980;
	zbx_uint64_t	template_itemid_rdap_target     = 99984;
	zbx_uint64_t	template_itemid_rdap_testedname = 99985;
	zbx_uint64_t	template_applicationid_rdap     = 998;

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	result = DBselect("select"
				" hosts.hostid,"
				"applications.applicationid"
			" from"
				" hosts"
				" left join hosts_templates on hosts_templates.hostid=hosts.hostid"
				" left join applications on applications.hostid=hosts.hostid"
				" left join application_template on application_template.applicationid=applications.applicationid"
			" where"
				" hosts_templates.templateid=" ZBX_FS_UI64 " and"
				" application_template.templateid=" ZBX_FS_UI64,
			templateid_template_rdap, template_applicationid_rdap);

	if (NULL == result)
		goto out;

	while (NULL != (row = DBfetch(result)))
	{
		zbx_uint64_t	hostid;
		zbx_uint64_t	applicationid;
		zbx_uint64_t	itemid_next;
		zbx_uint64_t	itemid_rdap_target;
		zbx_uint64_t	itemid_rdap_testedname;

		ZBX_STR2UINT64(hostid, row[0]);
		ZBX_STR2UINT64(applicationid, row[1]);

		itemid_next            = DBget_maxid_num("items", 2);
		itemid_rdap_target     = itemid_next++;
		itemid_rdap_testedname = itemid_next++;

#define CHECK(CODE) do {		\
	int __result = (CODE);		\
	if (ZBX_DB_OK > __result)	\
	{				\
		goto out;		\
	}				\
} while (0)

#define SQL	"insert into items (itemid,type,snmp_community,snmp_oid,hostid,name,key_,delay,history,trends,"		\
			"status,value_type,trapper_hosts,units,multiplier,delta,snmpv3_securityname,"			\
			"snmpv3_securitylevel,snmpv3_authpassphrase,snmpv3_privpassphrase,formula,error,lastlogsize,"	\
			"logtimefmt,templateid,valuemapid,delay_flex,params,ipmi_sensor,data_type,authtype,"		\
			"username,password,publickey,privatekey,mtime,flags,interfaceid,port,description,"		\
			"inventory_link,lifetime,snmpv3_authprotocol,snmpv3_privprotocol,state,snmpv3_contextname,"	\
			"evaltype)"											\
		" select"												\
			" " ZBX_FS_UI64 ",type,snmp_community,snmp_oid," ZBX_FS_UI64 ",name,key_,delay,history,trends,"	\
			"status,value_type,trapper_hosts,units,multiplier,delta,snmpv3_securityname,"			\
			"snmpv3_securitylevel,snmpv3_authpassphrase,snmpv3_privpassphrase,formula,error,lastlogsize,"	\
			"logtimefmt," ZBX_FS_UI64 ",valuemapid,delay_flex,params,ipmi_sensor,data_type,authtype,"	\
			"username,password,publickey,privatekey,mtime,flags,interfaceid,port,description,"		\
			"inventory_link,lifetime,snmpv3_authprotocol,snmpv3_privprotocol,state,snmpv3_contextname,"	\
			"evaltype"											\
		" from items"												\
		" where itemid=" ZBX_FS_UI64
		CHECK(DBexecute(SQL, itemid_rdap_target, hostid, template_itemid_rdap_target,
				template_itemid_rdap_target));
		CHECK(DBexecute(SQL, itemid_rdap_testedname, hostid, template_itemid_rdap_testedname,
				template_itemid_rdap_testedname));
#undef SQL

#define SQL	"insert into items_applications set itemappid=" ZBX_FS_UI64 ",applicationid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64
		CHECK(DBexecute(SQL, DBget_maxid_num("items_applications", 1), applicationid, itemid_rdap_target));
		CHECK(DBexecute(SQL, DBget_maxid_num("items_applications", 1), applicationid, itemid_rdap_testedname));
#undef SQL

#undef CHECK
	}

	ret = SUCCEED;
out:
	DBfree_result(result);

	return ret;
}

static int	DBpatch_3000509(void)
{
	DB_RESULT	result = NULL;
	DB_RESULT	result2 = NULL;
	DB_ROW		row;
	int		ret = FAIL;

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	/* loop through all "Template <RSMHOST>" templates that have listed items */
	/* status 3 = HOST_STATUS_TEMPLATE */
	result = DBselect("select"
				" hosts.hostid,"
				"items_rdds43_target.itemid,"
				"items_rdds43_testedname.itemid,"
				"items_rdds80_target.itemid"
			" from"
				" hosts"
				" left join items as items_rdds43_target on items_rdds43_target.hostid=hosts.hostid"
				" left join items as items_rdds43_testedname on items_rdds43_testedname.hostid=hosts.hostid"
				" left join items as items_rdds80_target on items_rdds80_target.hostid=hosts.hostid"
			" where"
				" hosts.status=3 and"
				" items_rdds43_target.key_='rsm.rdds.43.target' and"
				" items_rdds43_testedname.key_='rsm.rdds.43.testedname' and"
				" items_rdds80_target.key_='rsm.rdds.80.target'");

	if (NULL == result)
		goto out;

	while (NULL != (row = DBfetch(result)))
	{
		zbx_uint64_t	template_hostid;
		zbx_uint64_t	template_itemid_rdds43_target;
		zbx_uint64_t	template_itemid_rdds43_testedname;
		zbx_uint64_t	template_itemid_rdds80_target;

		ZBX_STR2UINT64(template_hostid, row[0]);
		ZBX_STR2UINT64(template_itemid_rdds43_target, row[1]);
		ZBX_STR2UINT64(template_itemid_rdds43_testedname, row[2]);
		ZBX_STR2UINT64(template_itemid_rdds80_target, row[3]);

		result2 = DBselect("select"
					" hosts.hostid,"
					"app_rdds43.applicationid,"
					"app_rdds80.applicationid"
				" from"
					" hosts"
					" left join hosts_templates on hosts_templates.hostid=hosts.hostid"
					" left join applications as app_rdds43 on app_rdds43.hostid=hosts.hostid"
					" left join applications as app_rdds80 on app_rdds80.hostid=hosts.hostid"
				" where"
					" hosts_templates.templateid=" ZBX_FS_UI64 " and"
					" app_rdds43.name='RDDS43' and"
					" app_rdds80.name='RDDS80'",
				template_hostid);

		if (NULL == result2)
			goto out;

		while (NULL != (row = DBfetch(result2)))
		{
			zbx_uint64_t	hostid;
			zbx_uint64_t	applicationid_rdds43;
			zbx_uint64_t	applicationid_rdds80;
			zbx_uint64_t	itemid_next;
			zbx_uint64_t	itemid_rdds43_target;
			zbx_uint64_t	itemid_rdds43_testedname;
			zbx_uint64_t	itemid_rdds80_target;

			ZBX_STR2UINT64(hostid, row[0]);
			ZBX_STR2UINT64(applicationid_rdds43, row[1]);
			ZBX_STR2UINT64(applicationid_rdds80, row[2]);

			itemid_next              = DBget_maxid_num("items", 3);
			itemid_rdds43_target     = itemid_next++;
			itemid_rdds43_testedname = itemid_next++;
			itemid_rdds80_target     = itemid_next++;

#define CHECK(CODE) do {		\
	int __result = (CODE);		\
	if (ZBX_DB_OK > __result)	\
	{				\
		goto out;		\
	}				\
} while (0)

#define SQL	"insert into items (itemid,type,snmp_community,snmp_oid,hostid,name,key_,delay,history,trends,"		\
			"status,value_type,trapper_hosts,units,multiplier,delta,snmpv3_securityname,"			\
			"snmpv3_securitylevel,snmpv3_authpassphrase,snmpv3_privpassphrase,formula,error,lastlogsize,"	\
			"logtimefmt,templateid,valuemapid,delay_flex,params,ipmi_sensor,data_type,authtype,"		\
			"username,password,publickey,privatekey,mtime,flags,interfaceid,port,description,"		\
			"inventory_link,lifetime,snmpv3_authprotocol,snmpv3_privprotocol,state,snmpv3_contextname,"	\
			"evaltype)"											\
		" select"												\
			" " ZBX_FS_UI64 ",type,snmp_community,snmp_oid," ZBX_FS_UI64 ",name,key_,delay,history,trends,"	\
			"status,value_type,trapper_hosts,units,multiplier,delta,snmpv3_securityname,"			\
			"snmpv3_securitylevel,snmpv3_authpassphrase,snmpv3_privpassphrase,formula,error,lastlogsize,"	\
			"logtimefmt," ZBX_FS_UI64 ",valuemapid,delay_flex,params,ipmi_sensor,data_type,authtype,"	\
			"username,password,publickey,privatekey,mtime,flags,interfaceid,port,description,"		\
			"inventory_link,lifetime,snmpv3_authprotocol,snmpv3_privprotocol,state,snmpv3_contextname,"	\
			"evaltype"											\
		" from items"												\
		" where itemid=" ZBX_FS_UI64
		CHECK(DBexecute(SQL, itemid_rdds43_target, hostid, template_itemid_rdds43_target,
				template_itemid_rdds43_target));
		CHECK(DBexecute(SQL, itemid_rdds43_testedname, hostid, template_itemid_rdds43_testedname,
				template_itemid_rdds43_testedname));
		CHECK(DBexecute(SQL, itemid_rdds80_target, hostid, template_itemid_rdds80_target,
				template_itemid_rdds80_target));
#undef SQL

#define SQL	"insert into items_applications set itemappid=" ZBX_FS_UI64 ",applicationid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64
		CHECK(DBexecute(SQL, DBget_maxid_num("items_applications", 1), applicationid_rdds43, itemid_rdds43_target));
		CHECK(DBexecute(SQL, DBget_maxid_num("items_applications", 1), applicationid_rdds43, itemid_rdds43_testedname));
		CHECK(DBexecute(SQL, DBget_maxid_num("items_applications", 1), applicationid_rdds80, itemid_rdds80_target));
#undef SQL

#undef CHECK
		}

		DBfree_result(result2);
		result2 = NULL;
	}

	ret = SUCCEED;
out:
	DBfree_result(result2);
	DBfree_result(result);
	return ret;
}

static int	DBpatch_3000510(void)
{
	const ZBX_TABLE table =
			{"rsm_target", "id", 0,
				{
					{"id", NULL, NULL, NULL, 0, ZBX_TYPE_UINT, ZBX_NOTNULL, 0},
					{"name", NULL, NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
					{0}
				},
				NULL
			};

	if (SUCCEED != DBcreate_table(&table))
		return FAIL;

	/* add auto_increment `id` */
	if (ZBX_DB_OK > DBexecute(
			"alter table `rsm_target`"
			" modify `id` bigint(20) unsigned NOT NULL AUTO_INCREMENT"))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_3000511(void)
{
	const ZBX_TABLE table =
			{"rsm_testedname", "id", 0,
				{
					{"id", NULL, NULL, NULL, 0, ZBX_TYPE_UINT, ZBX_NOTNULL, 0},
					{"name", NULL, NULL, NULL, 255, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
					{0}
				},
				NULL
			};

	if (SUCCEED != DBcreate_table(&table))
		return FAIL;

	/* add auto_increment `id` */
	if (ZBX_DB_OK > DBexecute(
			"alter table `rsm_testedname`"
			" modify `id` bigint(20) unsigned NOT NULL AUTO_INCREMENT"))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_3000512(void)
{
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

#define SQL	"update"												\
			" hostmacro"											\
			" left join hostmacro as hostmacro2 on hostmacro2.hostid=hostmacro.hostid"			\
		" set"													\
			" hostmacro.macro='{$RSM.RDDS43.TEST.DOMAIN}',"							\
			"hostmacro.value=concat(hostmacro.value,'.',hostmacro2.value)"					\
		" where"												\
			" hostmacro.macro='{$RSM.RDDS.TESTPREFIX}' and"							\
			" hostmacro2.macro='{$RSM.TLD}'"
	if (ZBX_DB_OK > DBexecute(SQL))
	{
		return FAIL;
	}
#undef SQL

	return SUCCEED;
}

static int	DBpatch_3000513(void)
{
	/* run on both server and proxy */

	if (ZBX_DB_OK > DBexecute(
			"alter table sla_reports"
			" change column report report_xml text collate utf8_bin not null default ''"))
	{
		return FAIL;
	}

	if (ZBX_DB_OK > DBexecute(
			"alter table sla_reports"
			" add column report_json text collate utf8_bin not null default '' after report_xml"))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_3000514(void)
{
	int		ret = FAIL;

	zbx_uint64_t	hostid_probe_statuses            = 100001;
	zbx_uint64_t	applicationid_probe_availability = 1001;

	DB_RESULT	result;
	DB_ROW		row;

	zbx_uint64_t	itemid_rdap_online_probes;
	zbx_uint64_t	itemid_rdap_total_probes;
	zbx_uint64_t	itemappid_rdap_online_probes;
	zbx_uint64_t	itemappid_rdap_total_probes;

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	if (NULL == (result = DBselect("select max(itemid)+1 from items")))
		return FAIL;

	if (NULL == (row = DBfetch(result)))
	{
		THIS_SHOULD_NEVER_HAPPEN;	/* there is "Zabbix Server" host, 'interface' table can't be empty */
		DBfree_result(result);
		return FAIL;
	}

	ZBX_STR2UINT64(itemid_rdap_online_probes, row[0]);
	DBfree_result(result);

	if (NULL == (result = DBselect("select max(itemappid)+1 from items_applications")))
		return FAIL;

	if (NULL == (row = DBfetch(result)))
	{
		THIS_SHOULD_NEVER_HAPPEN;	/* there is "Zabbix Server" host, 'interface' table can't be empty */
		DBfree_result(result);
		return FAIL;
	}

	ZBX_STR2UINT64(itemappid_rdap_online_probes, row[0]);
	DBfree_result(result);

	itemid_rdap_total_probes     = itemid_rdap_online_probes + 1;
	itemappid_rdap_total_probes  = itemappid_rdap_online_probes + 1;

#define CHECK(CODE) do {		\
	int __result = (CODE);		\
	if (ZBX_DB_OK > __result)	\
	{				\
		goto out;		\
	}				\
} while (0)

#define SQL	"insert into items set itemid=" ZBX_FS_UI64 ",type=%d,snmp_community='',snmp_oid='',"			\
		"hostid=" ZBX_FS_UI64 ",name='%s',key_='%s',delay=%d,history=%d,trends=365,status=0,value_type=%d,"	\
		"trapper_hosts='',units='',multiplier=0,delta=0,snmpv3_securityname='',snmpv3_securitylevel=0,"		\
		"snmpv3_authpassphrase='',snmpv3_privpassphrase='',formula='%s',error='',lastlogsize=0,logtimefmt='',"	\
		"templateid=NULL,valuemapid=NULL,delay_flex='',params='',ipmi_sensor='',data_type=0,authtype=0,"	\
		"username='',password='',publickey='',privatekey='',mtime=0,flags=0,interfaceid=NULL,port='',"		\
		"description='',inventory_link=0,lifetime='30',snmpv3_authprotocol=0,snmpv3_privprotocol=0,state=0,"	\
		"snmpv3_contextname='',evaltype=0"
	/* type 10 = ITEM_TYPE_EXTERNAL */
	/* value_type 3 = ITEM_VALUE_TYPE_UINT64 */
	CHECK(DBexecute(SQL, itemid_rdap_online_probes, 10, hostid_probe_statuses,
			"Number of online probes for RDAP tests", "online.nodes.pl[online,rdap]", 60, 7, 3, "1"));
	CHECK(DBexecute(SQL, itemid_rdap_total_probes, 10, hostid_probe_statuses,
			"Total number of probes for RDAP tests", "online.nodes.pl[total,rdap]", 60, 7, 3, "1"));
#undef SQL

#define SQL	"insert into items_applications set itemappid=" ZBX_FS_UI64 ",applicationid=" ZBX_FS_UI64 ",itemid=" ZBX_FS_UI64
	CHECK(DBexecute(SQL, itemappid_rdap_online_probes, applicationid_probe_availability, itemid_rdap_online_probes));
	CHECK(DBexecute(SQL, itemappid_rdap_total_probes, applicationid_probe_availability, itemid_rdap_total_probes));
#undef SQL

	CHECK(DBexecute("delete from ids where table_name='items' or table_name='items_applications'"));

#undef CHECK

	ret = SUCCEED;
out:
	return ret;
}

static int	DBpatch_3000515(void)
{
	zbx_uint64_t	itemid, triggerid, functionid;
	DB_RESULT	result;
	DB_ROW		row;
	int		ret = FAIL;

	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		return SUCCEED;

	triggerid = DBget_maxid("triggers");
	functionid = DBget_maxid("functions");

	result = DBselect("select itemid from items where hostid=100001 and key_='online.nodes.pl[online,rdap]'");

	if (NULL == (row = DBfetch(result)))
	{
		zabbix_log(LOG_LEVEL_CRIT, "Zabbix configuration in the database is corrupted");
		goto out;
	}

	ZBX_STR2UINT64(itemid, row[0]);	/* itemid of "<Probe>" */

#define CHECK(CODE) do {		\
	int __result = (CODE);		\
	if (ZBX_DB_OK > __result)	\
	{				\
		goto out;		\
	}				\
} while (0)

	CHECK(DBexecute("insert into triggers (triggerid,expression,description,url,status,value,"
				"priority,lastchange,comments,error,templateid,type,state,flags)"
			" values (" ZBX_FS_UI64 ",'{" ZBX_FS_UI64 "}<{$RSM.RDAP.PROBE.ONLINE}','%s',"
				"'',0,0,5,0,'','',NULL,0,0,0)",
			triggerid, functionid, "Number of RDAP-enabled online probes is less than $1"));
	CHECK(DBexecute("insert into functions (functionid,itemid,triggerid,function,parameter)"
			" values (" ZBX_FS_UI64 "," ZBX_FS_UI64 "," ZBX_FS_UI64 ",'%s','')",
			functionid, itemid, triggerid, "last"));
#undef CHECK

	ret = SUCCEED;
out:
	DBfree_result(result);

	return ret;
}

static int	DBpatch_3000516(void)
{
#define SQL	"alter table host_inventory"										\
			" modify column name varchar(128) collate utf8_bin not null default '',"			\
			" modify column alias varchar(128) collate utf8_bin not null default '',"			\
			" modify column os varchar(128) collate utf8_bin not null default '',"				\
			" modify column os_short varchar(128) collate utf8_bin not null default ''"
	if (ZBX_DB_OK > DBexecute(SQL))
	{
		return FAIL;
	}
#undef SQL

	return SUCCEED;
}

#endif

DBPATCH_START(3000)

/* version, duplicates flag, mandatory flag */

DBPATCH_ADD(3000000, 0, 1)
DBPATCH_ADD(3000100, 0, 1)	/* Phase 1 */
DBPATCH_ADD(3000101, 0, 1)	/* new group "TLD Probe results" */
DBPATCH_ADD(3000102, 0, 1)	/* new group "gTLD Probe results" */
DBPATCH_ADD(3000103, 0, 1)	/* new group "ccTLD Probe results" */
DBPATCH_ADD(3000104, 0, 1)	/* new group "testTLD Probe results" */
DBPATCH_ADD(3000105, 0, 1)	/* new group "otherTLD Probe results" */
DBPATCH_ADD(3000106, 0, 1)	/* read permissions on "TLD Probe results" host group for "Technical services users" */
DBPATCH_ADD(3000107, 0, 1)	/* read permissions on "gTLD Probe results" host group for "EBERO users" */
DBPATCH_ADD(3000108, 0, 1)	/* read permissions on "Probes" host group for "Technical services users" */
DBPATCH_ADD(3000109, 0, 1)	/* read permissions on "Probes" host group for "EBERO users" */
DBPATCH_ADD(3000110, 0, 1)	/* add "gTLD" hosts to "gTLD Probe results" host group */
DBPATCH_ADD(3000111, 0, 1)	/* add "ccTLD" hosts to "ccTLD Probe results" host group */
DBPATCH_ADD(3000112, 0, 1)	/* add "testTLD" hosts to "testTLD Probe results" host group */
DBPATCH_ADD(3000113, 0, 1)	/* add "otherTLD" hosts to "otherTLD Probe results" host group */
DBPATCH_ADD(3000114, 0, 1)	/* add all TLD hosts to "TLD Probe results" host group */
DBPATCH_ADD(3000115, 0, 0)	/* fix trigger expression for minimum online IPv4 enabled probe number */
DBPATCH_ADD(3000116, 0, 0)	/* fix trigger expression for minimum online IPv6 enabled probe number */
DBPATCH_ADD(3000117, 0, 0)	/* link "Template App Zabbix Proxy" to all probe hosts */
DBPATCH_ADD(3000118, 0, 0)	/* read permissions on "Probes - Mon" host group for "Technical services users" */
DBPATCH_ADD(3000119, 0, 0)	/* read permissions on "Mon" host group for "Technical services users" */
DBPATCH_ADD(3000120, 0, 0)	/* link "Template App Zabbix Proxy" to all probe hosts (again) */
DBPATCH_ADD(3000121, 0, 0)	/* new actions: "Probes-Mon", "Central-Server", "TLDs" */
DBPATCH_ADD(3000122, 0, 0)	/* parameters for "Script" media type */
DBPATCH_ADD(3000123, 0, 0)	/* move "Probe statuses" host from "Probes - Mon" group to "Mon" group */
DBPATCH_ADD(3000124, 0, 1)	/* change read permissions on "Probes" host group to "Probes - Mon" host group for "EBERO users" */
DBPATCH_ADD(3000125, 0, 0)	/* drop "$2" and fixed capitalization in "zabbix[proxy,{$RSM.PROXY_NAME},lastaccess]" item name */
DBPATCH_ADD(3000126, 0, 0)	/* remove "<probe>" hosts from "<probe>" host group */
DBPATCH_ADD(3000127, 0, 0)	/* remove "<TLD>" hosts from "TLD <TLD>" host group */
DBPATCH_ADD(3000128, 0, 0)	/* adjust allowed system time difference between Zabbix Server and other hosts */
DBPATCH_ADD(3000129, 0, 0)	/* rename corresponding trigger */
DBPATCH_ADD(3000130, 0, 1)	/* create lastvalue table */
DBPATCH_ADD(3000131, 0, 1)	/* add itemid constraint to lastvalue */
DBPATCH_ADD(3000132, 0, 0)	/* delete carriage returns in parameters of "Script" media type */
DBPATCH_ADD(3000133, 0, 1)	/* use recovery event (instead of problem event) date and time in recovery message */
DBPATCH_ADD(3000134, 0, 0)	/* add missing interfaces for "Global macro history" and "Probe statuses" hosts */
DBPATCH_ADD(3000135, 0, 0)	/* add global "{$MAX_CPU_LOAD}" and "{$MAX_RUN_PROCESSES}" macros */
DBPATCH_ADD(3000136, 0, 0)	/* add "{$MAX_CPU_LOAD}" and "{$MAX_RUN_PROCESSES}" macros on "Zabbix Server" host */
DBPATCH_ADD(3000137, 0, 0)	/* update "Processor load is too high on {HOST.NAME}" trigger in "Template OS Linux" template and "Zabbix Server" host */
DBPATCH_ADD(3000138, 0, 0)	/* update "Too many processes running on {HOST.NAME}" trigger in "Template OS Linux" template and "Zabbix Server" host */
DBPATCH_ADD(3000139, 0, 0)	/* unsuccessful attempt to unlink and link updated "Template OS Linux" template to all hosts it is currently linked to (except "Zabbix Server") */
DBPATCH_ADD(3000140, 0, 0)	/* lowered "Refresh unsupported items" interval to 60 seconds */
DBPATCH_ADD(3000141, 0, 0)	/* added "Activated" and "Deactivated" false positive statuses to "statusMaps.csv" */
DBPATCH_ADD(3000200, 0, 0)	/* Phase 2 */
DBPATCH_ADD(3000202, 0, 0)	/* update "RSM DNS rtt" value mapping with new DNS test error codes */
DBPATCH_ADD(3000203, 0, 0)	/* add "Up-inconclusive-no-data" and "Up-inconclusive-no-probes" to "RSM Service Availability" value mapping */
DBPATCH_ADD(3000204, 0, 0)	/* add "Up-inconclusive-no-data" and "Up-inconclusive-no-probes" to data export "statusMaps" catalog */
DBPATCH_ADD(3000205, 0, 0)	/* add {$PROBE.INTERNAL.ERROR.INTERVAL} global macro */
DBPATCH_ADD(3000206, 0, 0)	/* create "Template Probe Errors" template with "Internal error rate" item and triggers */
DBPATCH_ADD(3000208, 0, 0)	/* new actions: "Probes", "Probes-Knockout" */
DBPATCH_ADD(3000210, 0, 0)	/* link "Template Probe Errors" template to all probe hosts */
DBPATCH_ADD(3000211, 0, 0)	/* update "RSM RDDS rtt" value mapping with new RDDS43 and RDDS80 test error codes */
DBPATCH_ADD(3000212, 0, 0)	/* remove obsoleted DNS error codes: -421,-426,-821,-826 */
DBPATCH_ADD(3000213, 0, 0)	/* add "RSM RDAP rtt" value mapping (without error codes) */
DBPATCH_ADD(3000214, 0, 0)	/* create "Template RDAP" template with rdap[...], rdap.ip and rdap.rtt items, RDAP application; place template into "Templates" host group */
DBPATCH_ADD(3000215, 0, 0)	/* add RDAP error codes into "RSM RDAP rtt" value mapping */
DBPATCH_ADD(3000216, 0, 0)	/* add new test type to "testTypes" catalog */
DBPATCH_ADD(3000217, 0, 0)	/* add macro {$RDAP.TLD.ENABLED}=0 and item rdds.enabled to all "Template <TLD>" and hosts it is linked to */
DBPATCH_ADD(3000218, 0, 0)	/* add value mappings for new DNS error codes: -408, -808 */
DBPATCH_ADD(3000219, 0, 0)	/* add item dnssec.enabled to all "Template <TLD>" and hosts it is linked to */
DBPATCH_ADD(3000220, 0, 0)	/* remove 'ms' units from item rdap.rtt */
DBPATCH_ADD(3000221, 0, 0)	/* remove 6 obsoleted value mappings add 2 new errors related to hitting max HTTP redirects */
DBPATCH_ADD(3000222, 0, 0)	/* fix value mapping typo 'unexpecting' => 'unexpected' */
DBPATCH_ADD(3000223, 0, 0)	/* fix value mapping typo 'RDAP' => 'RDDS' */
DBPATCH_ADD(3000224, 0, 0)	/* link "Template RDAP" template to all probe hosts */
DBPATCH_ADD(3000225, 0, 0)	/* change "Zabbix server" macro value {$MAX_PROCESSES}=1500 (was 300) */
DBPATCH_ADD(3000226, 0, 0)	/* disable "RDAP availability" items on hosts where RDAP is disabled */
DBPATCH_ADD(3000227, 0, 0)	/* reorganize error codes: part 1 */
DBPATCH_ADD(3000228, 0, 0)	/* reorganize error codes: part 2 */
DBPATCH_ADD(3000229, 0, 0)	/* reorganize error codes: part 3 (add -200 and -250 to RDAP service error codes) */
DBPATCH_ADD(3000230, 0, 0)	/* fix previous patch 3000229: RDAP error codes -400 :: -415 */
DBPATCH_ADD(3000231, 0, 0)	/* add item resolver.status[...] to templates "Template <PROBE> status" */
DBPATCH_ADD(3000232, 0, 0)	/* replace error codes -100 and -101 with -390 and -391 */
DBPATCH_ADD(3000233, 0, 0)	/* change global macro value {$PROBE.INTERNAL.ERROR.INTERVAL}=5m (was 1m) */
DBPATCH_ADD(3000234, 0, 0)	/* add constraint on lastvalue table to delete obsoleted itemids */
DBPATCH_ADD(3000235, 0, 0)	/* add trigger for item rsm.probe.status[manual] to alert on Probe knock out */
DBPATCH_ADD(3000236, 0, 0)	/* disable "RDAP availability" items on hosts where RDAP is disabled (again) */
DBPATCH_ADD(3000237, 0, 0)	/* mark DNS errors -252, -652 in mappings as obsoleted */
DBPATCH_ADD(3000238, 0, 0)	/* increase "value" field of "lastvalue" table by double(24,4) to accept bigint values */
DBPATCH_ADD(3000300, 0, 0)	/* Phase 3 */
DBPATCH_ADD(3000301, 0, 0)	/* add lastvalue_str table */
DBPATCH_ADD(3000302, 0, 0)	/* mark DNS errors -252, -652 in mappings as obsoleted (again, for those started from Phase 3) */
DBPATCH_ADD(3000303, 0, 0)	/* increase "value" field of "lastvalue" table by double(24,4) to accept bigint values (again, for those started from Phase 3) */
DBPATCH_ADD(3000304, 0, 0)	/* update and add new RSM.SLV.* macros */
DBPATCH_ADD(3000305, 0, 0)	/* add DNS downtime trigger to existing tld hosts */
DBPATCH_ADD(3000306, 0, 0)	/* add RDDS downtime triggers to existing tld hosts */
DBPATCH_ADD(3000307, 0, 0)	/* add "DNS Resolution RTT (performed/failed/pfailed)" items to existing tld hosts */
DBPATCH_ADD(3000308, 0, 0)	/* add "RDDS Resolution RTT (performed/failed/pfailed)" items to existing tld hosts */
DBPATCH_ADD(3000309, 0, 0)	/* create sla_reports table */
DBPATCH_ADD(3000310, 0, 0)	/* add rsm.slv.dns.ns.downtime to tld hosts */
DBPATCH_ADD(3000311, 0, 0)	/* rename macro RSM.SLV.NS.AVAIL into RSM.SLV.NS.DOWNTIME */
DBPATCH_ADD(3000312, 0, 0)	/* add nameserver downtime triggers to tld hosts */
DBPATCH_ADD(3000313, 0, 0)	/* add nameserver availability items to tld hosts */
DBPATCH_ADD(3000314, 0, 0)	/* fill auditlog.resourceid for "Marked/Unmarked as false positive" logs */
DBPATCH_ADD(3000315, 0, 0)	/* fix item value_type for rsm.slv.dns.ns.* to uint */
DBPATCH_ADD(3000316, 0, 0)	/* set RSM.DNS.TCP.DELAY macro to 1h, set update interval of rsm.dns.tcp[%] items to 1h */
DBPATCH_ADD(3000317, 0, 0)	/* set update interval of items in "Global macro history" host to 60 seconds */
DBPATCH_ADD(3000318, 0, 0)	/* add new items to "Global macro history" host */
DBPATCH_ADD(3000319, 0, 0)	/* remove trailing spaces from "Ratio of failed monthly RDDS queries" item names */
DBPATCH_ADD(3000320, 0, 1)	/* create rsmhost_dns_ns_log table */
DBPATCH_ADD(3000321, 0, 0)	/* fill rsmhost_dns_ns_log table */
DBPATCH_ADD(3000322, 0, 0)	/* fix delta field for 'rsm.slv.%.rtt.%' items */
DBPATCH_ADD(3000323, 0, 1)	/* add 'created' field to the 'hosts' table */
DBPATCH_ADD(3000324, 0, 0)	/* changed "TLDs" action to a remote commnad that calls tlds-notification.pl */
DBPATCH_ADD(3000400, 0, 0)	/* Phase 3, version 1.4.0 */
DBPATCH_ADD(3000401, 0, 0)	/* add macro {$RSM.MONITORING.TARGET} with empty string as value (unknown) or "registry" */
DBPATCH_ADD(3000402, 0, 0)	/* rename "EBERO users" user group to "Read-only user", "Technical services users" to "Power user" */
DBPATCH_ADD(3000403, 0, 1)	/* add columns "info_1" and "info_2" to the "hosts" table */
DBPATCH_ADD(3000500, 0, 0)	/* Phase 4, version 2.0.0 */
DBPATCH_ADD(3000501, 0, 0)	/* add macros, items and triggers for Standalone RDAP */
DBPATCH_ADD(3000502, 0, 0)	/* add {$RSM.RDAP.ENABLED} macro on probes */
DBPATCH_ADD(3000503, 0, 0)	/* replace {$RSM.RDDS.*} with {$RSM.RDAP.*} in rdap[] keys */
DBPATCH_ADD(3000504, 0, 0)	/* add RDAP-related macros to Global macro history */
DBPATCH_ADD(3000505, 0, 0)	/* fixed the type of 'zabbix[host,agent,available]' items */
DBPATCH_ADD(3000506, 0, 0)	/* add "rdap.target" and "rdap.testedname" items to "Template RDAP" template */
DBPATCH_ADD(3000507, 0, 0)	/* add "rsm.rdds.43.target", "rsm.rdds.43.testedname" and "rsm.rdds.80.target" items to "Template <RSMHOST>" templates where needed */
DBPATCH_ADD(3000508, 0, 0)	/* add "rdap.target" and "rdap.testedname" items to hosts that use "Template RDAP" template */
DBPATCH_ADD(3000509, 0, 0)	/* add "rsm.rdds.43.target", "rsm.rdds.43.testedname" and "rsm.rdds.80.target" items to hosts that use "Template <RSMHOST>" */
DBPATCH_ADD(3000510, 0, 0)	/* create table rsm_target for Data Export */
DBPATCH_ADD(3000511, 0, 0)	/* create table rsm_testedname for Data Export */
DBPATCH_ADD(3000512, 0, 0)	/* rename {$RSM.RDDS.TESTPREFIX} to {$RSM.RDDS43.TEST.DOMAIN} */
DBPATCH_ADD(3000513, 0, 0)	/* rename report column in sla_reports to report_xml, add report_json column */
DBPATCH_ADD(3000514, 0, 0)	/* add items for online/total probes with RDAP enabled to host "Probe statuses" */
DBPATCH_ADD(3000515, 0, 0)	/* add trigger for number of RDAP-enabled online probes to host "Probe statuses" */
DBPATCH_ADD(3000516, 0, 1)	/* modify "hosts_inventory" table to avoid "Row size too large" MariaDB error */

DBPATCH_END()
