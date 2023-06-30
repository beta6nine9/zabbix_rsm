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

#define RSM_RDAP_LOG_PREFIX	"rdap"	/* file will be <LOGDIR>/<PROBE>-<TLD>-RSM_RDAP_LOG_PREFIX.log */

/* FIXME Currently this error code is missing in specification for RDAP. Hopefully, it will be introduced later. */
#ifdef RSM_EC_RDAP_NOCODE
#	error "please remove temporary definition of RSM_EC_RDAP_NOCODE, seems like it was added to the header file"
#else
#	define RSM_EC_RDAP_NOCODE	RSM_EC_RDAP_INTERNAL_GENERAL
#endif

RSM_DEFINE_RESOLVER_ERROR_TO(RDAP)
RSM_DEFINE_HTTP_PRE_STATUS_ERROR_TO(RDAP)
RSM_DEFINE_HTTP_ERROR_TO(RDAP)

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
	writedata_t		request_headers = {NULL, 0, 0}, response = {NULL, 0, 0};
	zbx_vector_str_t	ips;
	struct zbx_json_parse	jp;
	FILE			*log_fd = NULL;
	char			*rsmhost,
				*testedname,
				*base_url,
				*resolver_str,
				*scheme = NULL,
				*domain = NULL,
				*path = NULL,
				*formed_url = NULL,
				*value_str = NULL,
				*transfer_details = NULL,
				err[RSM_ERR_BUF_SIZE],
				is_ipv4,
				query[64],
				resolver_ip[RSM_BUF_SIZE];
	const char		*ip = NULL;
	size_t			value_alloc = 0;
	rsm_http_error_t	ec_http;
	uint16_t		resolver_port;
	int			maxredirs,
				rtt_limit,
				rsmhost_rdap_enabled,
				probe_rdap_enabled,
				ipv4_enabled,
				ipv6_enabled,
				ipv_flags = 0,
				port,
				rtt = RSM_NO_VALUE,
				rv,
				ret = SYSINFO_RET_FAIL;

	zbx_vector_str_create(&ips);

	if (10 != request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
		goto out;
	}

	/* TLD goes first, then RDAP specific parameters, then TLD options, probe options and global settings */
	GET_PARAM_NEMPTY(rsmhost             , 0, "Rsmhost");
	GET_PARAM_NEMPTY(testedname          , 1, "Test domain");
	GET_PARAM_NEMPTY(base_url            , 2, "RDAP service endpoint");
	GET_PARAM_UINT  (maxredirs           , 3, "maximal number of redirections allowed");
	GET_PARAM_UINT  (rtt_limit           , 4, "maximum allowed RTT");
	GET_PARAM_UINT  (rsmhost_rdap_enabled, 5, "RDAP enabled for TLD");
	GET_PARAM_UINT  (probe_rdap_enabled  , 6, "RDAP enabled for probe");
	GET_PARAM_UINT  (ipv4_enabled        , 7, "IPv4 enabled");
	GET_PARAM_UINT  (ipv6_enabled        , 8, "IPv6 enabled");
	GET_PARAM_NEMPTY(resolver_str        , 9, "IP address of local resolver");

	/* open log file */
	if (SUCCEED != start_test(&log_fd, output_fd, host, rsmhost, RSM_RDAP_LOG_PREFIX, err, sizeof(err)))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, err));
		goto out;
	}

	/* print test details */
	rsm_infof(log_fd, "probe_RDAP:%s"
			", RDAP:%s"
			", IPv4:%s"
			", IPv6:%s"
			", base_url:%s"
			", testedname:%s"
			", rtt_limit:%d"
			", maxredirs:%d",
			ENABLED(probe_rdap_enabled),
			ENABLED(rsmhost_rdap_enabled),
			ENABLED(ipv4_enabled),
			ENABLED(ipv6_enabled),
			base_url,
			testedname,
			rtt_limit,
			maxredirs);

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
				ipv6_enabled, RESOLVER_EXTRAS_DNSSEC, RSM_TCP_TIMEOUT, RSM_TCP_RETRY, err, sizeof(err)))
		{
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot create resolver: %s.", err));
			goto out;
		}
	}

	/* from this point item will not become NOTSUPPORTED */
	ret = SYSINFO_RET_OK;

	if (0 == probe_rdap_enabled)
	{
		rsm_info(log_fd, "RDAP disabled on this probe");
		goto out;
	}

	if (0 == rsmhost_rdap_enabled)
	{
		rsm_info(log_fd, "RDAP disabled on this TLD");
		goto out;
	}

	/* skip the test itself in case of two special values in RDAP base URL parameter */

	if (0 == strcmp(base_url, "not listed"))
	{
		rsm_err(log_fd, "The TLD is not listed in the Bootstrap Service Registry for Domain Name Space");
		rtt = RSM_EC_RDAP_NOTLISTED;
		goto out;
	}

	if (0 == strcmp(base_url, "no https"))
	{
		rsm_err(log_fd, "The RDAP base URL obtained from Bootstrap Service Registry for Domain Name Space"
				" does not use HTTPS");
		rtt = RSM_EC_RDAP_NOHTTPS;
		goto out;
	}

	if (0 != ipv4_enabled)
		ipv_flags |= RSM_FLAG_IPV4_ENABLED;
	if (0 != ipv6_enabled)
		ipv_flags |= RSM_FLAG_IPV6_ENABLED;

	/* resolve domain to IPs */
	if (SUCCEED != rsm_resolve_host(res, domain, &ips, ipv_flags, log_fd, &ec_res, err, sizeof(err)))
	{
		rtt = rsm_resolver_error_to_RDAP(ec_res);
		rsm_err(log_fd, err);
		goto out;
	}

	if (0 == ips.values_num)
	{
		rtt = RSM_EC_RDAP_INTERNAL_IP_UNSUP;
		rsm_err(log_fd, "found no IP addresses supported by the Probe");
		goto out;
	}

	/* choose random IP */
	ip = ips.values[rsm_random((size_t)ips.values_num)];

	if (SUCCEED != rsm_validate_ip(ip, ipv4_enabled, ipv6_enabled, NULL, &is_ipv4))
	{
		rtt = RSM_EC_RDAP_INTERNAL_GENERAL;
		rsm_errf(log_fd, "internal error, should not be using unsupported IP %s", ip);
		goto out;
	}

	if ('\0' != *path && path[strlen(path) - 1] == '/')
		zbx_strlcpy(query, "domain", sizeof(query));
	else
		zbx_strlcpy(query, "/domain", sizeof(query));

	if (0 == is_ipv4)
		formed_url = zbx_dsprintf(formed_url, "%s[%s]:%d%s%s/%s", scheme, ip, port, path, query, testedname);
	else
		formed_url = zbx_dsprintf(formed_url, "%s%s:%d%s%s/%s", scheme, ip, port, path, query, testedname);

	rsm_infof(log_fd, "the following URL was generated for the test: %s", formed_url);

	rv = rsm_http_test(domain, formed_url, RSM_TCP_TIMEOUT, maxredirs, &ec_http, &rtt, &request_headers, &response,
			&transfer_details, ipv4_enabled, ipv6_enabled, err, sizeof(err));

	rsm_infof(log_fd, "Request headers:\n%s", ZBX_NULL2STR(request_headers.buf));
	rsm_infof(log_fd, "Transfer details:%s\nBody:\n%s", ZBX_NULL2STR(transfer_details), ZBX_NULL2STR(response.buf));

	if (SUCCEED != rv)
	{
		rtt = rsm_http_error_to_RDAP(ec_http);
		rsm_errf(log_fd, "%s (%d)", err, rtt);
		goto out;
	}

	if (NULL == response.buf || '\0' == *response.buf || SUCCEED != zbx_json_open(response.buf, &jp))
	{
		rtt = RSM_EC_RDAP_EJSON;
		rsm_err(log_fd, "invalid JSON format in response");
		goto out;
	}

	if (SUCCEED != zbx_json_value_by_name_dyn(&jp, "ldhName", &value_str, &value_alloc, NULL))
	{
		rtt = RSM_EC_RDAP_NONAME;
		rsm_err(log_fd, "ldhName member not found in response");
		goto out;
	}

	if (NULL == value_str || 0 != strcmp(value_str, testedname))
	{
		rtt = RSM_EC_RDAP_ENAME;
		rsm_err(log_fd, "ldhName member doesn't match the domain being requested");
		goto out;
	}
out:
	if (SYSINFO_RET_OK == ret && 0 != rsmhost_rdap_enabled && 0 != probe_rdap_enabled)
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

		zbx_json_free(&json);
	}

	if (NULL != res)
	{
		if (0 != ldns_resolver_nameserver_count(res))
			ldns_resolver_deep_free(res);
		else
			ldns_resolver_free(res);
	}

	zbx_free(scheme);
	zbx_free(domain);
	zbx_free(path);
	zbx_free(formed_url);
	zbx_free(value_str);
	zbx_free(request_headers.buf);
	zbx_free(response.buf);
	zbx_free(transfer_details);

	rsm_vector_str_clean_and_destroy(&ips);

	end_test(log_fd, output_fd, result);

	return ret;
}
