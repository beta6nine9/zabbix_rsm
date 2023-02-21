/*
** Zabbix
** Copyright (C) 2001-2013 Zabbix SIA
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

#include "log.h"
#include "checks_simple_rsm.h"

/* FIXME Currently this error code is missing in specification for RDAP. Hopefully, it will be introduced later. */
#ifdef ZBX_EC_RDAP_NOCODE
#	error "please remove temporary definition of ZBX_EC_RDAP_NOCODE, seems like it was added to the header file"
#else
#	define ZBX_EC_RDAP_NOCODE	ZBX_EC_RDAP_INTERNAL_GENERAL
#endif

ZBX_DEFINE_RESOLVER_ERROR_TO(RDAP)
ZBX_DEFINE_HTTP_PRE_STATUS_ERROR_TO(RDAP)
ZBX_DEFINE_HTTP_ERROR_TO(RDAP)

/* used in libcurl callback function to store webpage contents in memory */
typedef struct
{
	char	*buf;
	size_t	alloc;
	size_t	offset;
}
curl_data_t;

/* store the curl output in memory */
static size_t	curl_memory(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	curl_data_t	*data = (curl_data_t *)userdata;
	size_t		r_size = size * nmemb;

	zbx_strncpy_alloc(&data->buf, &data->alloc, &data->offset, (const char *)ptr, r_size);

	return r_size;
}

static void	create_rdap_json(struct zbx_json *json, const char *ip, int rtt, const char *target,
		const char *testedname, int status)
{
	zbx_json_init(json, 2 * ZBX_KIBIBYTE);

	if (NULL != ip)
		zbx_json_addstring(json, "ip", ip, ZBX_JSON_TYPE_STRING);
	zbx_json_addint64(json, "rtt", rtt);
	zbx_json_addstring(json, "target", target, ZBX_JSON_TYPE_STRING);
	zbx_json_addstring(json, "testedname", testedname, ZBX_JSON_TYPE_STRING);
	zbx_json_addint64(json, "status", status);

	zbx_json_close(json);
}

int	check_rsm_rdap(const char *host, const AGENT_REQUEST *request, AGENT_RESULT *result, FILE *output_fd)
{
	ldns_resolver		*res = NULL;
	rsm_resolver_error_t	ec_res;
	curl_data_t		data = {NULL, 0, 0};
	zbx_vector_str_t	ips;
	struct zbx_json_parse	jp;
	FILE			*log_fd;
	char			*rsmhost, *testedname, *base_url, *resolver_str, *scheme = NULL,
				*domain = NULL, *path = NULL, *formed_url = NULL, *value_str = NULL,
				err[ZBX_ERR_BUF_SIZE], is_ipv4, query[64],
				resolver_ip[ZBX_HOST_BUF_SIZE];
	const char		*ip = NULL;
	size_t			value_alloc = 0;
	rsm_http_error_t	ec_http;
	uint16_t		resolver_port;
	int			maxredirs, rtt_limit, tld_enabled, probe_enabled, ipv4_enabled, ipv6_enabled,
				ipv_flags = 0, curl_flags = 0, port, rtt = ZBX_NO_VALUE, ret = SYSINFO_RET_FAIL;

	if (10 != request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
		goto out;
	}

	/* TLD goes first, then RDAP specific parameters, then TLD options, probe options and global settings */
	GET_PARAM_NEMPTY(rsmhost      , 0, "Rsmhost");
	GET_PARAM_NEMPTY(testedname   , 1, "Test domain");
	GET_PARAM_NEMPTY(base_url     , 2, "RDAP service endpoint");
	GET_PARAM_UINT  (maxredirs    , 3, "maximal number of redirections allowed");
	GET_PARAM_UINT  (rtt_limit    , 4, "maximum allowed RTT");
	GET_PARAM_UINT  (tld_enabled  , 5, "RDAP enabled for TLD");
	GET_PARAM_UINT  (probe_enabled, 6, "RDAP enabled for probe");
	GET_PARAM_UINT  (ipv4_enabled , 7, "IPv4 enabled");
	GET_PARAM_UINT  (ipv6_enabled , 8, "IPv6 enabled");
	GET_PARAM_NEMPTY(resolver_str , 9, "IP address of local resolver");

	/* open log file */
	if (NULL == output_fd)
	{
		if (NULL == (log_fd = open_item_log(host, rsmhost, ZBX_RDDS_LOG_PREFIX, err, sizeof(err))))
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, err));
			goto out;
		}
	}
	else
		log_fd = output_fd;

	if (0 == probe_enabled)
	{
		rsm_info(log_fd, "RDAP disabled on this probe");
		ret = SYSINFO_RET_OK;
		goto out;
	}

	if (0 == tld_enabled)
	{
		rsm_info(log_fd, "RDAP disabled on this TLD");
		ret = SYSINFO_RET_OK;
		goto out;
	}

	zbx_vector_str_create(&ips);

	if (SUCCEED != str_in_list("not listed,no https", base_url, ','))
	{
		if (SUCCEED != rsm_split_url(base_url, &scheme, &domain, &port, &path, err, sizeof(err)))
		{
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "\"%s\": %s", base_url, err));
			goto out;
		}

		get_host_and_port_from_str(resolver_str, ';', resolver_ip, sizeof(resolver_ip), &resolver_port,
				DEFAULT_RESOLVER_PORT);

		/* create resolver */
		if (SUCCEED != rsm_create_resolver(&res, "resolver", resolver_ip, resolver_port, RSM_TCP, ipv4_enabled,
				ipv6_enabled, RESOLVER_EXTRAS_DNSSEC, RSM_TCP_TIMEOUT, RSM_TCP_RETRY, log_fd,
				err, sizeof(err)))
		{
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot create resolver: %s.", err));
			goto out;
		}
	}

	start_test(log_fd);

	/* from this point item will not become NOTSUPPORTED */
	ret = SYSINFO_RET_OK;

	/* skip the test itself in case of two special values in RDAP base URL parameter */

	if (0 == strcmp(base_url, "not listed"))
	{
		rsm_err(log_fd, "The TLD is not listed in the Bootstrap Service Registry for Domain Name Space");
		rtt = ZBX_EC_RDAP_NOTLISTED;
		goto end;
	}

	if (0 == strcmp(base_url, "no https"))
	{
		rsm_err(log_fd, "The RDAP base URL obtained from Bootstrap Service Registry for Domain Name Space"
				" does not use HTTPS");
		rtt = ZBX_EC_RDAP_NOHTTPS;
		goto end;
	}

	if (0 != ipv4_enabled)
		ipv_flags |= ZBX_FLAG_IPV4_ENABLED;
	if (0 != ipv6_enabled)
		ipv_flags |= ZBX_FLAG_IPV6_ENABLED;

	/* resolve domain to IPs */
	if (SUCCEED != rsm_resolve_host(res, domain, &ips, ipv_flags, log_fd, &ec_res, err, sizeof(err)))
	{
		rtt = rsm_resolver_error_to_RDAP(ec_res);
		rsm_errf(log_fd, "trying to resolve \"%s\": %s", domain, err);
		goto end;
	}

	if (0 == ips.values_num)
	{
		rtt = ZBX_EC_RDAP_INTERNAL_IP_UNSUP;
		rsm_errf(log_fd, "IP address(es) of host \"%s\" are not supported on this Probe", domain);
		goto end;
	}

	/* choose random IP */
	ip = ips.values[rsm_random((size_t)ips.values_num)];

	if (SUCCEED != rsm_validate_ip(ip, ipv4_enabled, ipv6_enabled, NULL, &is_ipv4))
	{
		rtt = ZBX_EC_RDAP_INTERNAL_GENERAL;
		rsm_errf(log_fd, "internal error, selected unsupported IP of \"%s\": \"%s\"", domain, ip);
		goto end;
	}

	if ('\0' != *path && path[strlen(path) - 1] == '/')
		zbx_strlcpy(query, "domain", sizeof(query));
	else
		zbx_strlcpy(query, "/domain", sizeof(query));

	if (0 == is_ipv4)
		formed_url = zbx_dsprintf(formed_url, "%s[%s]:%d%s%s/%s", scheme, ip, port, path, query, testedname);
	else
		formed_url = zbx_dsprintf(formed_url, "%s%s:%d%s%s/%s", scheme, ip, port, path, query, testedname);

	rsm_infof(log_fd, "domain \"%s\" was resolved to %s, using URL \"%s\".", domain, ip, formed_url);

	if (SUCCEED != rsm_http_test(domain, formed_url, RSM_TCP_TIMEOUT, maxredirs, &ec_http, &rtt, &data,
			curl_memory, curl_flags, err, sizeof(err)))
	{
		rtt = rsm_http_error_to_RDAP(ec_http);
		rsm_errf(log_fd, "test of \"%s\" (%s) failed: %s (%d)", base_url, formed_url, err, rtt);
		goto end;
	}

	rsm_infof(log_fd, "got response ===>\n%.*s\n<===", RESPONSE_PREVIEW_SIZE, ZBX_NULL2STR(data.buf));

	if (NULL == data.buf || '\0' == *data.buf || SUCCEED != zbx_json_open(data.buf, &jp))
	{
		rtt = ZBX_EC_RDAP_EJSON;
		rsm_errf(log_fd, "invalid JSON format in response of \"%s\" (%s)", base_url, ip);
		goto end;
	}

	if (SUCCEED != zbx_json_value_by_name_dyn(&jp, "ldhName", &value_str, &value_alloc, NULL))
	{
		rtt = ZBX_EC_RDAP_NONAME;
		rsm_errf(log_fd, "ldhName member not found in response of \"%s\" (%s)", base_url, ip);
		goto end;
	}

	if (NULL == value_str || 0 != strcmp(value_str, testedname))
	{
		rtt = ZBX_EC_RDAP_ENAME;
		rsm_errf(log_fd, "ldhName member doesn't match query in response of \"%s\" (%s)", base_url, ip);
		goto end;
	}

	rsm_infof(log_fd, "end test of \"%s\" (%s) (rtt:%d)", base_url, ZBX_NULL2STR(ip), rtt);
end:
	if (0 != ISSET_MSG(result))
		rsm_err(log_fd, result->msg);

	end_test(log_fd);

	if (SYSINFO_RET_OK == ret && ZBX_NO_VALUE != rtt)
	{
		int		subtest_result;
		struct zbx_json	json;

		switch (rsm_subtest_result(rtt, rtt_limit))
		{
			case RSM_SUBTEST_SUCCESS:
				subtest_result = 1;	/* up */
				break;
			default:	/* RSM_SUBTEST_FAIL */
				subtest_result = 0;	/* down */
		}

		create_rdap_json(&json, ip, rtt, base_url, testedname, subtest_result);

		SET_TEXT_RESULT(result, zbx_strdup(NULL, json.buffer));

		rsm_infof(log_fd, "%s", json.buffer);

		zbx_json_free(&json);
	}

	if (NULL != res)
	{
		if (0 != ldns_resolver_nameserver_count(res))
			ldns_resolver_deep_free(res);
		else
			ldns_resolver_free(res);
	}

	zbx_free(value_str);
	zbx_free(data.buf);

	rsm_vector_str_clean_and_destroy(&ips);

	if (NULL == output_fd && NULL != log_fd)
		fclose(log_fd);
out:
	zbx_free(scheme);
	zbx_free(domain);
	zbx_free(path);
	zbx_free(formed_url);

	return ret;
}
