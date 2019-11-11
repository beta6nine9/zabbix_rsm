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
	zabbix_log(LOG_LEVEL_CRIT, "There is no automatic database upgrade");

	return FAIL;
}

static int	DBpatch_4040301(void)
{
	/* type=3 -> type=simple check */
	if (ZBX_DB_OK > DBexecute(
		"update items"
		" set delay='{$RSM.DNS.UDP.DELAY}'"
		" where key_ like 'rsm.dns.udp[%%'"
		" and type=3"))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_4040302(void)
{
	/* type=3 -> type=simple check */
	if (ZBX_DB_OK > DBexecute(
		"update items"
		" set delay='{$RSM.DNS.TCP.DELAY}'"
		" where key_ like 'rsm.dns.tcp[%%'"
		" and type=3"))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_4040303(void)
{
	/* type=3 -> type=simple check */
	if (ZBX_DB_OK > DBexecute(
		"update items"
		" set delay='{$RSM.RDDS.DELAY}'"
		" where key_ like 'rsm.rdds[%%'"
		" and type=3"))
	{
		return FAIL;
	}

	return SUCCEED;
}

static int	DBpatch_4040304(void)
{
	/* type=3 -> type=simple check */
	if (ZBX_DB_OK > DBexecute(
		"update items"
		" set delay='{$RSM.RDAP.DELAY}'"
		" where key_ like 'rdap[%%'"
		" and type=3"))
	{
		return FAIL;
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

DBPATCH_END()
