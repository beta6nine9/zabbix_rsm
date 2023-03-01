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

#define RSM_RDDS_LOG_PREFIX	"rdds"	/* file will be <LOGDIR>/<PROBE>-<TLD>-RSM_RDDS_LOG_PREFIX.log */

#define RSM_SEND_BUF_SIZE	128
#define DEFAULT_RDDS43_PORT	43

RSM_DEFINE_RESOLVER_ERROR_TO(RDDS43)
RSM_DEFINE_RESOLVER_ERROR_TO(RDDS80)
RSM_DEFINE_HTTP_PRE_STATUS_ERROR_TO(RDDS80)
RSM_DEFINE_HTTP_ERROR_TO(RDDS80)

static int	ec_noerror(int ec)
{
	if (0 <= ec || RSM_NO_VALUE == ec)
		return SUCCEED;

	return FAIL;
}

/* discard the curl output (using inline to hide "unused" compiler warning when -Wunused) */
static inline size_t	curl_devnull(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	(void)ptr;
	(void)userdata;

	return size * nmemb;
}

static void	create_rdds_json(struct zbx_json *json, const char *ip43, int rtt43, int upd43,
		const char *rdds43_server, const char *rdds43_testedname, const char *ip80, int rtt80,
		const char *rdds80_url, int rdds43_status, int rdds80_status, int rdds_status)
{
	zbx_json_init(json, 2 * ZBX_KIBIBYTE);

	if (RSM_NO_VALUE != rtt43)
	{
		zbx_json_addobject(json, "rdds43");

		zbx_json_addint64(json, "rtt", rtt43);
		if (NULL != ip43)
			zbx_json_addstring(json, "ip", ip43, ZBX_JSON_TYPE_STRING);
		if (RSM_NO_VALUE != upd43)
			zbx_json_addint64(json, "upd", upd43);
		if (NULL != rdds43_server)
			zbx_json_addstring(json, "target", rdds43_server, ZBX_JSON_TYPE_STRING);
		if (0 != strcmp(rdds43_testedname, ""))
			zbx_json_addstring(json, "testedname", rdds43_testedname, ZBX_JSON_TYPE_STRING);
		zbx_json_addint64(json, "status", rdds43_status);

		zbx_json_close(json);
	}

	if (RSM_NO_VALUE != rtt80)
	{
		zbx_json_addobject(json, "rdds80");

		zbx_json_addint64(json, "rtt", rtt80);
		if (NULL != ip80)
			zbx_json_addstring(json, "ip", ip80, ZBX_JSON_TYPE_STRING);
		if (NULL != rdds80_url)
			zbx_json_addstring(json, "target", rdds80_url, ZBX_JSON_TYPE_STRING);
		zbx_json_addint64(json, "status", rdds80_status);

		zbx_json_close(json);
	}

	zbx_json_addint64(json, "status", rdds_status);
}

static int	rdds43_test(const char *request, const char *ip, unsigned short port, int timeout, char **answer,
		int *rtt, char *err, size_t err_size)
{
	zbx_socket_t	s;
	char		send_buf[RSM_SEND_BUF_SIZE];
	zbx_timespec_t	start, now;
	ssize_t		nbytes;
	int		ret = FAIL;

	zbx_timespec(&start);

	if (SUCCEED != zbx_tcp_connect(&s, NULL, ip, port, timeout, ZBX_TCP_SEC_UNENCRYPTED, NULL, NULL))
	{
		*rtt = (SUCCEED == zbx_alarm_timed_out() ? RSM_EC_RDDS43_TO : RSM_EC_RDDS43_ECON);
		zbx_snprintf(err, err_size, "cannot connect: %s", zbx_socket_strerror());
		goto out;
	}

	zbx_snprintf(send_buf, sizeof(send_buf), "%s\r\n", request);

	if (SUCCEED != zbx_tcp_send_raw(&s, send_buf))
	{
		*rtt = (SUCCEED == zbx_alarm_timed_out() ? RSM_EC_RDDS43_TO : RSM_EC_RDDS43_ECON);
		zbx_snprintf(err, err_size, "cannot send data: %s", zbx_socket_strerror());
		goto out;
	}

	if (FAIL == (nbytes = zbx_tcp_recv_raw_ext(&s, 0)))	/* timeout is still "active" here */
	{
		*rtt = (SUCCEED == zbx_alarm_timed_out() ? RSM_EC_RDDS43_TO : RSM_EC_RDDS43_ECON);
		zbx_snprintf(err, err_size, "cannot receive data: %s", zbx_socket_strerror());
		goto out;
	}

	if (0 == nbytes)
	{
		*rtt = RSM_EC_RDDS43_EMPTY;
		zbx_strlcpy(err, "empty response received", err_size);
		goto out;
	}

	ret = SUCCEED;
	zbx_timespec(&now);
	*rtt = (now.sec - start.sec) * 1000 + (now.ns - start.ns) / 1000000;

	if (NULL != answer)
		*answer = zbx_strdup(*answer, s.buffer);
out:
	zbx_tcp_close(&s);	/* takes care of freeing received buffer */

	return ret;
}

static void	get_rdds43_nss(zbx_vector_str_t *nss, const char *recv_buf, const char *rdds43_ns_string,
		FILE *log_fd)
{
	const char	*p;
	char		ns_buf[RSM_BUF_SIZE];
	size_t		rdds43_ns_string_size, ns_buf_len;

	p = recv_buf;
	rdds43_ns_string_size = strlen(rdds43_ns_string);

	while (NULL != (p = zbx_strcasestr(p, rdds43_ns_string)))
	{
		p += rdds43_ns_string_size;

		while (0 != isblank(*p))
			p++;

		if (0 == isalnum(*p))
			continue;

		ns_buf_len = 0;
		while ('\0' != *p && 0 == isspace(*p) && ns_buf_len < sizeof(ns_buf))
			ns_buf[ns_buf_len++] = *p++;

		if (sizeof(ns_buf) == ns_buf_len)
		{
			/* internal error, ns buffer not enough */
			rsm_errf(log_fd, "internal error, name server buffer too small (%u bytes)"
					" for host in \"%.*s...\"", sizeof(ns_buf), sizeof(ns_buf), p);
			continue;
		}

		ns_buf[ns_buf_len] = '\0';
		zbx_vector_str_append(nss, zbx_strdup(NULL, ns_buf));
	}

	if (0 != nss->values_num)
	{
		zbx_vector_str_sort(nss, ZBX_DEFAULT_STR_COMPARE_FUNC);
		zbx_vector_str_uniq(nss, ZBX_DEFAULT_STR_COMPARE_FUNC);
	}
}

int	check_rsm_rdds(const char *host, const AGENT_REQUEST *request, AGENT_RESULT *result, FILE *output_fd)
{
	char			*rsmhost,
				*rdds43_server_str,
				*rdds80_url,
				*resolver_str,
				*rdds43_ns_string,
				*answer = NULL,
				*scheme = NULL,
				*domain = NULL,
				*path = NULL,
				*formed_url = NULL,
				is_ipv4, err[RSM_ERR_BUF_SIZE],
				rdds43_server[RSM_BUF_SIZE],
				resolver_ip[RSM_BUF_SIZE];
	const char		*rdds43_testedname = NULL,
				*ip43 = NULL,
				*ip80 = NULL;
	zbx_vector_str_t	ips43,
				ips80,
				nss;
	FILE			*log_fd = NULL;
	ldns_resolver		*res = NULL;
	rsm_resolver_error_t	ec_res;
	time_t			ts, now;
	rsm_http_error_t	ec_http;
	struct zbx_json		json;
	uint16_t		resolver_port,
				rdds43_port;
	int			probe_rdds_enabled,
				rsmhost_rdds43_enabled,
				rsmhost_rdds80_enabled,
				ipv4_enabled,
				ipv6_enabled,
				rtt_limit,
				maxredirs,
				rtt43 = RSM_NO_VALUE,
				upd43 = RSM_NO_VALUE,
				rtt80 = RSM_NO_VALUE,
				epp_enabled = 0,
				ipv_flags = 0,
				curl_flags = 0,
				port,
				ret = SYSINFO_RET_FAIL;

	zbx_vector_str_create(&ips43);
	zbx_vector_str_create(&ips80);
	zbx_vector_str_create(&nss);

	if (13 != request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "item must contain 13 parameters"));
		goto out;
	}

	GET_PARAM_NEMPTY(rsmhost               , 0 ,   "Rsmhost");
	GET_PARAM       (rdds43_server_str     , 1); /* RDDS43 server      */
	GET_PARAM       (rdds80_url            , 2); /* RDDS80 url         */
	GET_PARAM       (rdds43_testedname     , 3); /* RDDS43 test domain */
	GET_PARAM       (rdds43_ns_string      , 4); /* RDDS43 ns string   */
	GET_PARAM_UINT  (probe_rdds_enabled    , 5 ,   "RDDS enabled on probe");
	GET_PARAM_UINT  (rsmhost_rdds43_enabled, 6 ,   "RDDS43 enabled on rsmhost");
	GET_PARAM_UINT  (rsmhost_rdds80_enabled, 7 ,   "RDDS80 enabled on rsmhost");
	GET_PARAM_UINT  (ipv4_enabled          , 8 ,   "IPv4 enabled");
	GET_PARAM_UINT  (ipv6_enabled          , 9 ,   "IPv6 enabled");
	GET_PARAM_NEMPTY(resolver_str          , 10,   "IP address of local resolver");
	GET_PARAM_UINT  (rtt_limit             , 11,   "RTT limit");
	GET_PARAM_UINT  (maxredirs             , 12,   "max redirects");

	/* open log file */
	if (SUCCEED != start_test(&log_fd, output_fd, host, rsmhost, RSM_RDDS_LOG_PREFIX, err, sizeof(err)))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, err));
		goto out;
	}

	if (0 != rsmhost_rdds43_enabled)
	{
		if ('\0' == *rdds43_server_str)
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "macro {$RSM.TLD.RDDS43.SERVER} must be set"));
			goto out;
		}
	}

	if (0 != rsmhost_rdds80_enabled)
	{
		if  ('\0' == *rdds80_url)
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "macro {$RSM.TLD.RDDS80.URL} must be set"));
			goto out;
		}

		if (SUCCEED != rsm_split_url(rdds80_url, &scheme, &domain, &port, &path, err, sizeof(err)))
		{
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "\"%s\": %s", rdds80_url, err));
			goto out;
		}
	}

	/* print test details */
	rsm_infof(log_fd, "probe_RDDS:%s"
			", RDDS43:%s"
			", RDDS80:%s"
			", IPv4:%s"
			", IPv6:%s"
			"%s%s%s%s"
			"%s%s%s%s"
			"%s%s%s%s"
			", resolver:%s"
			", rtt_limit:%d"
			", maxredirs:%d",
			ENABLED(probe_rdds_enabled),
			ENABLED(rsmhost_rdds43_enabled),
			ENABLED(rsmhost_rdds80_enabled),
			ENABLED(ipv4_enabled),
			ENABLED(ipv6_enabled),
			/* rdds43_testedname */
			(rsmhost_rdds43_enabled ? ", " : ""),
			(rsmhost_rdds43_enabled ? "RDDS43_testedname" : ""),
			(rsmhost_rdds43_enabled ? ":" : ""),
			(rsmhost_rdds43_enabled ? rdds43_testedname : ""),
			/* rdds43_ns_string */
			(rsmhost_rdds43_enabled ? ", " : ""),
			(rsmhost_rdds43_enabled ? "RDDS43_ns_string" : ""),
			(rsmhost_rdds43_enabled ? ":" : ""),
			(rsmhost_rdds43_enabled ? rdds43_ns_string : ""),
			/* rdds80_url */
			(rsmhost_rdds80_enabled ? ", " : ""),
			(rsmhost_rdds80_enabled ? "RDDS80_url" : ""),
			(rsmhost_rdds80_enabled ? ":" : ""),
			(rsmhost_rdds80_enabled ? rdds80_url : ""),
			resolver_str,
			rtt_limit,
			maxredirs);

	get_host_and_port_from_str(resolver_str, ';', resolver_ip, sizeof(resolver_ip), &resolver_port,
			DEFAULT_RESOLVER_PORT);

	get_host_and_port_from_str(rdds43_server_str, ';', rdds43_server, sizeof(rdds43_server), &rdds43_port,
			DEFAULT_RDDS43_PORT);

	/* create resolver, note: it's used in both RDDS43 and RDDS80 tests */
	if (SUCCEED != rsm_create_resolver(&res, "resolver", resolver_ip, resolver_port, RSM_TCP, ipv4_enabled,
			ipv6_enabled, RESOLVER_EXTRAS_NONE, RSM_TCP_TIMEOUT, RSM_TCP_RETRY, err, sizeof(err)))
	{
		/* exception, item becomes UNSUPPORTED */
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "cannot create resolver: %s", err));
		goto out;
	}

	/* from this point item will not become NOTSUPPORTED */
	ret = SYSINFO_RET_OK;

	if (0 != ipv4_enabled)
		ipv_flags |= RSM_FLAG_IPV4_ENABLED;
	if (0 != ipv6_enabled)
		ipv_flags |= RSM_FLAG_IPV6_ENABLED;

	if (0 == probe_rdds_enabled)
	{
		rsm_info(log_fd, "RDDS disabled on this probe");
		goto out;
	}

	if (0 == rsmhost_rdds43_enabled && 0 == rsmhost_rdds80_enabled)
	{
		rsm_info(log_fd, "RDDS disabled on this RSM host");
		goto out;
	}

	if (0 != rsmhost_rdds43_enabled)
	{
		rsm_infof(log_fd, "start RDDS43 test (server %s)", rdds43_server);

		/* start RDDS43 test, resolve host to ips */
		if (SUCCEED != rsm_resolve_host(res, rdds43_server, &ips43, ipv_flags, log_fd, &ec_res,
				err, sizeof(err)))
		{
			rtt43 = rsm_resolver_error_to_RDDS43(ec_res);
			rsm_err(log_fd, err);
		}

		/* if RDDS43 fails we should still process RDDS80 */

		if (SUCCEED == ec_noerror(rtt43))
		{
			if (0 == ips43.values_num)
			{
				rtt43 = RSM_EC_RDDS43_INTERNAL_IP_UNSUP;
				rsm_err(log_fd, "found no IP addresses supported by the Probe");
			}
		}

		if (SUCCEED == ec_noerror(rtt43))
		{
			int	rv;

			/* choose random IP */
			ip43 = ips43.values[rsm_random((size_t)ips43.values_num)];

			rsm_infof(log_fd, "the following details will be used in the test:"
					" ip:%s, request:%s, name server prefix:\"%s\"",
					ip43, rdds43_testedname, rdds43_ns_string);

			rv = rdds43_test(rdds43_testedname, ip43, rdds43_port, RSM_TCP_TIMEOUT, &answer, &rtt43, err,
					sizeof(err));

			if (answer != NULL)
				rsm_infof(log_fd, "response ===>\n%s\n<===", answer);

			if (rv != SUCCEED)
				rsm_err(log_fd, err);
		}

		if (SUCCEED == ec_noerror(rtt43))
		{
			get_rdds43_nss(&nss, answer, rdds43_ns_string, log_fd);

			if (0 == nss.values_num)
			{
				rtt43 = RSM_EC_RDDS43_NONS;
				rsm_err(log_fd, "no Name Servers found in the output");
			}
		}

		if (SUCCEED == ec_noerror(rtt43))
		{
			if (0 != epp_enabled)
			{
				/* start RDDS UPD test, get timestamp from the host name */
				char	*random_ns;

				/* choose random NS from the output */
				random_ns = nss.values[rsm_random((size_t)nss.values_num)];

				rsm_infof(log_fd, "randomly selected Name Server server \"%s\"", random_ns);

				if (SUCCEED != rsm_get_ts_from_host(random_ns, &ts))
				{
					upd43 = RSM_EC_RDDS43_INTERNAL_GENERAL;
					rsm_errf(log_fd, "cannot extract Unix timestamp from Name Server \"%s\"",
							random_ns);
				}

				if (upd43 == RSM_NO_VALUE)
				{
					now = time(NULL);

					if (0 > now - ts)
					{
						rsm_errf(log_fd, "Unix timestamp of Name Server \"%s\" is in the future"
								" (current: %u)", random_ns, now);
						upd43 = RSM_EC_RDDS43_INTERNAL_GENERAL;
					}
				}

				if (upd43 == RSM_NO_VALUE)
				{
					/* successful UPD */
					upd43 = (int)(now - ts);
				}
			}
		}
	}

	if (0 != rsmhost_rdds80_enabled)
	{
		rsm_infof(log_fd, "start RDDS80 test (url %s)", rdds80_url);

		/* start RDDS80 test, resolve domain to ips */
		if (SUCCEED != rsm_resolve_host(res, domain, &ips80, ipv_flags, log_fd, &ec_res, err, sizeof(err)))
		{
			rtt80 = rsm_resolver_error_to_RDDS80(ec_res);
			rsm_err(log_fd, err);
			goto out;
		}

		if (0 == ips80.values_num)
		{
			rtt80 = RSM_EC_RDDS80_INTERNAL_IP_UNSUP;
			rsm_err(log_fd, "found no IP addresses supported by the Probe");
			goto out;
		}

		/* choose random IP */
		ip80 = ips80.values[rsm_random((size_t)ips80.values_num)];

		if (SUCCEED != rsm_validate_ip(ip80, ipv4_enabled, ipv6_enabled, NULL, &is_ipv4))
		{
			rtt80 = RSM_EC_RDDS80_INTERNAL_GENERAL;
			rsm_errf(log_fd, "internal error, should not be using unsupported IP %s", ip80);
			goto out;
		}

		if (0 == is_ipv4)
			formed_url = zbx_dsprintf(formed_url, "%s[%s]:%d%s", scheme, ip80, port, path);
		else
			formed_url = zbx_dsprintf(formed_url, "%s%s:%d%s", scheme, ip80, port, path);

		rsm_infof(log_fd, "the following URL was generated for the test: %s", formed_url);

		if (SUCCEED != rsm_http_test(domain, formed_url, RSM_TCP_TIMEOUT, maxredirs, &ec_http, &rtt80, NULL,
						curl_devnull, curl_flags, err, sizeof(err)))
		{
			rtt80 = rsm_http_error_to_RDDS80(ec_http);
			rsm_errf(log_fd, "%s (%d)", err, rtt80);
		}

		rsm_infof(log_fd, "end RDDS80 test (rtt:%d)", rtt80);
	}
out:
	if (SYSINFO_RET_OK == ret && (0 != rsmhost_rdds43_enabled || 0 != rsmhost_rdds80_enabled) && 0 != probe_rdds_enabled)
	{
		int	rdds43_status, rdds80_status;

		switch (rsm_subtest_result(rtt43, rtt_limit))
		{
			case RSM_SUBTEST_SUCCESS:
				rdds43_status = 1;	/* up */
				break;
			default:	/* RSM_SUBTEST_FAIL */
				rdds43_status = 0;	/* down */
		}

		switch (rsm_subtest_result(rtt80, rtt_limit))
		{
			case RSM_SUBTEST_SUCCESS:
				rdds80_status = 1;	/* up */
				break;
			default:	/* RSM_SUBTEST_FAIL */
				rdds80_status = 0;	/* down */
		}

		create_rdds_json(&json, ip43, rtt43, upd43, rdds43_server, rdds43_testedname, ip80, rtt80, rdds80_url,
				rdds43_status, rdds80_status, (rdds43_status && rdds80_status));

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

	zbx_free(answer);
	zbx_free(scheme);
	zbx_free(domain);
	zbx_free(path);
	zbx_free(formed_url);

	rsm_vector_str_clean_and_destroy(&nss);
	rsm_vector_str_clean_and_destroy(&ips80);
	rsm_vector_str_clean_and_destroy(&ips43);

	end_test(log_fd, output_fd, result);

	return ret;
}
