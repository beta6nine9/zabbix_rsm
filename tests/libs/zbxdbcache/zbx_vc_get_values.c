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

#include "zbxmocktest.h"
#include "zbxmockdata.h"
#include "zbxmockassert.h"
#include "zbxmockutil.h"

#include "common.h"
#include "valuecache.h"
#include "valuecache_test.h"
#include "mocks/valuecache/valuecache_mock.h"

#include "zbx_vc_common.h"

extern zbx_uint64_t	CONFIG_VALUE_CACHE_SIZE;

void	zbx_vc_test_get_values_setup(zbx_mock_handle_t *handle, zbx_uint64_t *itemid, unsigned char *value_type,
		zbx_timespec_t *ts, int *err, zbx_vector_history_record_t *expected,
				zbx_vector_history_record_t *returned, int *seconds, int *count)
{
	/* perform request */

	*handle = zbx_mock_get_parameter_handle("in.test");
	zbx_vcmock_set_time(*handle, "time");
	zbx_vcmock_set_mode(*handle, "cache mode");

	zbx_vcmock_get_request_params(*handle, itemid, value_type, seconds, count, ts);
	*err = zbx_vc_get_values(*itemid, *value_type, returned, *seconds, *count, ts);
	zbx_vc_flush_stats();
	zbx_mock_assert_result_eq("zbx_vc_get_values() return value", SUCCEED, *err);

	/* validate results */

	zbx_vcmock_read_values(zbx_mock_get_parameter_handle("out.values"), *value_type, expected);
	zbx_vcmock_check_records("Returned values", *value_type,  expected, returned);

	zbx_history_record_vector_clean(returned, *value_type);
	zbx_history_record_vector_clean(expected, *value_type);
}

void	zbx_vc_test_check_result(zbx_uint64_t *cache_hits, zbx_uint64_t *cache_misses)
{
	zbx_uint64_t	expected_hits, expected_misses;

	if (FAIL == is_uint64(zbx_mock_get_parameter_string("out.cache.hits"), &expected_hits))
		fail_msg("Invalid out.cache.hits value");
	zbx_mock_assert_uint64_eq("cache.hits", expected_hits, *cache_hits);

	if (FAIL == is_uint64(zbx_mock_get_parameter_string("out.cache.misses"), &expected_misses))
		fail_msg("Invalid out.cache.misses value");
	zbx_mock_assert_uint64_eq("cache.misses", expected_misses, *cache_misses);
}

void	zbx_mock_test_entry(void **state)
{
	zbx_vc_common_test_func(state, NULL, NULL, zbx_vc_test_check_result, zbx_vc_test_get_values_setup);
}

