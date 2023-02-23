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

#define ZBX_PROBESTATUS_LOG_PREFIX	"probestatus"	/* file will be <LOGDIR>/<PROBE>-ZBX_PROBESTATUS_LOG_PREFIX.log */

static char	rsm_validate_host_list(const char *list, char delim)
{
	const char	*p;

	p = list;

	while ('\0' != *p && (0 != isalnum(*p) || '.' == *p || '-' == *p || '_' == *p || ':' == *p || delim == *p))
		p++;

	return *p;
}

int	check_rsm_probe_status(const char *host, const AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char			err[ZBX_ERR_BUF_SIZE],
				*check_mode,
				*ipv4_rootservers,
				*ipv6_rootservers,
				test_status = ZBX_EC_PROBE_UNSUPPORTED;
	const char		*ip;
	zbx_vector_str_t	ips4, ips6;
	ldns_resolver		*res = NULL;
	ldns_rdf		*query_rdf = NULL;
	FILE			*log_fd = NULL;
	unsigned int		extras = RESOLVER_EXTRAS_DNSSEC;
	uint16_t		resolver_port = DEFAULT_RESOLVER_PORT;
	int			i,
				ipv4_enabled = 0,
				ipv6_enabled = 0,
				ipv4_min_servers,
				ipv6_min_servers,
				ipv4_reply_ms,
				ipv6_reply_ms,
				online_delay,
				ok_servers,
				ret = SYSINFO_RET_FAIL;

	zbx_vector_str_create(&ips4);
	zbx_vector_str_create(&ips6);

	/* open log file */
	if (SUCCEED != start_test(&log_fd, NULL, host, NULL, ZBX_PROBESTATUS_LOG_PREFIX, err, sizeof(err)))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, err));
		goto out;
	}

	if (10 != request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
		goto out;
	}

	GET_PARAM_NEMPTY(check_mode      , 0, "mode of the check");
	GET_PARAM_UINT  (ipv4_enabled    , 1, "IPv4 enabled");
	GET_PARAM_UINT  (ipv6_enabled    , 2, "IPv6 enabled");
	GET_PARAM_NEMPTY(ipv4_rootservers, 3, "IPv4 root servers");
	GET_PARAM_NEMPTY(ipv6_rootservers, 4, "IPv6 root servers");
	GET_PARAM_UINT  (ipv4_min_servers, 5, "IPv4 root servers required to be working");
	GET_PARAM_UINT  (ipv6_min_servers, 6, "IPv6 root servers required to be working");
	GET_PARAM_UINT  (ipv4_reply_ms   , 7, "RTT to consider IPv4 root server working");
	GET_PARAM_UINT  (ipv6_reply_ms   , 8, "RTT to consider IPv6 root server working");
	GET_PARAM_UINT  (online_delay    , 9, "seconds to be successful in order to switch from OFFLINE to ONLINE");

	if (0 != strcmp("automatic", check_mode))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "first parameter has to be \"automatic\""));
		goto out;
	}

	/* create query to check the connection */
	if (NULL == (query_rdf = ldns_rdf_new_frm_str(LDNS_RDF_TYPE_DNAME, ".")))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "cannot create DNS request"));
		goto out;
	}

	rsm_infof(log_fd, "IPv4:%s IPv6:%s", 0 == ipv4_enabled ? "DISABLED" : "ENABLED",
			0 == ipv6_enabled ? "DISABLED" : "ENABLED");

	if (0 != ipv4_enabled)
	{
		char	c;

		if ('\0' != (c = rsm_validate_host_list(ipv4_rootservers, ',')))
		{
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "invalid character in IPv4 root servers list: %c", c));
			goto out;
		}

		rsm_get_strings_from_list(&ips4, ipv4_rootservers, ',');

		ok_servers = 0;

		for (i = 0; i < ips4.values_num; i++)
		{
			ip = ips4.values[i];

			if (SUCCEED != rsm_create_resolver(&res, "root server", ip, resolver_port, RSM_UDP, ipv4_enabled,
					ipv6_enabled, extras, RSM_UDP_TIMEOUT, RSM_UDP_RETRY, log_fd, err, sizeof(err)))
			{
				SET_MSG_RESULT(result, zbx_dsprintf(NULL, "cannot instantiate LDNS resolver: %s", err));
				goto out;
			}

			if (SUCCEED == rsm_check_dns_connection(res, query_rdf,
					(CHECK_DNS_CONN_RRSIGS | CHECK_DNS_CONN_RTT),
					ipv4_reply_ms, log_fd, err, sizeof(err)))
			{
				ok_servers++;
			}
			else
				rsm_errf(log_fd, "dns check of root server %s failed: %s", ip, err);

			if (ok_servers == ipv4_min_servers)
			{
				rsm_infof(log_fd, "%d successful results, IPv4 considered working", ok_servers);
				break;
			}
		}

		if (ok_servers != ipv4_min_servers)
		{
			/* IP protocol check failed */
			rsm_warnf(log_fd, "status OFFLINE. IPv4 protocol check failed, %d out of %d root servers"
					" replied successfully, minimum required %d",
					ok_servers, ips4.values_num, ipv4_min_servers);
			test_status = ZBX_EC_PROBE_OFFLINE;
			goto out;
		}
	}

	if (0 != ipv6_enabled)
	{
		char	c;

		if ('\0' != (c = rsm_validate_host_list(ipv6_rootservers, ',')))
		{
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "invalid character in IPv6 root servers list: %c", c));
			goto out;
		}

		rsm_get_strings_from_list(&ips6, ipv6_rootservers, ',');

		ok_servers = 0;

		for (i = 0; i < ips6.values_num; i++)
		{
			ip = ips6.values[i];

			if (SUCCEED != rsm_create_resolver(&res, "root server", ip, resolver_port, RSM_UDP, ipv4_enabled,
					ipv6_enabled, extras, RSM_UDP_TIMEOUT, RSM_UDP_RETRY, log_fd, err, sizeof(err)))
			{
				SET_MSG_RESULT(result, zbx_dsprintf(NULL, "cannot instantiate LDNS resolver: %s", err));
				goto out;
			}

			if (SUCCEED == rsm_check_dns_connection(res, query_rdf,
					(CHECK_DNS_CONN_RRSIGS | CHECK_DNS_CONN_RTT),
					ipv6_reply_ms, log_fd, err, sizeof(err)))
			{
				ok_servers++;
			}
			else
				rsm_errf(log_fd, "dns check of root server %s failed: %s", ip, err);

			if (ok_servers == ipv6_min_servers)
			{
				rsm_infof(log_fd, "%d successful results, IPv6 considered working", ok_servers);
				break;
			}
		}

		if (ok_servers != ipv6_min_servers)
		{
			/* IP protocol check failed */
			rsm_warnf(log_fd, "status OFFLINE. IPv6 protocol check failed, %d out of %d root servers"
					" replied successfully, minimum required %d",
					ok_servers, ips6.values_num, ipv6_min_servers);
			test_status = ZBX_EC_PROBE_OFFLINE;
			goto out;
		}
	}

	test_status = ZBX_EC_PROBE_ONLINE;
out:
	if (0 != ISSET_MSG(result))
		rsm_err(log_fd, result->msg);

	/* The value @online_delay controlls how many seconds must the check be successful in order */
	/* to switch from OFFLINE to ONLINE. This is why we keep last online time in the cache.     */
	if (ZBX_EC_PROBE_UNSUPPORTED != test_status)
	{
		ret = SYSINFO_RET_OK;

		if (ZBX_EC_PROBE_OFFLINE == test_status)
		{
			DCset_probe_online_since(0);
		}
		else if (ZBX_EC_PROBE_ONLINE == test_status && ZBX_EC_PROBE_OFFLINE == DCget_probe_last_status())
		{
			time_t	probe_online_since, now;

			probe_online_since = DCget_probe_online_since();
			now = time(NULL);

			if (0 == DCget_probe_online_since())
			{
				DCset_probe_online_since(now);
			}
			else
			{
				if (now - probe_online_since < online_delay)
				{
					rsm_warnf(log_fd, "probe status successful for % seconds, still OFFLINE",
							now - probe_online_since);
					test_status = ZBX_EC_PROBE_OFFLINE;
				}
				else
				{
					rsm_warnf(log_fd, "probe status successful for % seconds, changing to ONLINE",
							now - probe_online_since);
				}
			}
		}

		SET_UI64_RESULT(result, test_status);
	}
	else
	{
		ret = SYSINFO_RET_FAIL;
		DCset_probe_online_since(0);
	}

	DCset_probe_last_status(test_status);

	if (NULL != res)
	{
		if (0 != ldns_resolver_nameserver_count(res))
			ldns_resolver_deep_free(res);
		else
			ldns_resolver_free(res);
	}

	rsm_vector_str_clean_and_destroy(&ips6);
	rsm_vector_str_clean_and_destroy(&ips4);

	if (NULL != query_rdf)
		ldns_rdf_deep_free(query_rdf);

	end_test(log_fd, NULL);

	return ret;
}
