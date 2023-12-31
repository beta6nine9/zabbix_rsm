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

#define RSM_RESOLVERSTATUS_LOG_PREFIX	"resolverstatus"	/* file will be <LOGDIR>/<PROBE>-RSM_RESOLVERSTATUS_LOG_PREFIX.log */

int	check_rsm_resolver_status(const char *host, const AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char		*resolver_ip,
			err[RSM_ERR_BUF_SIZE];
	ldns_resolver	*res = NULL;
	ldns_rdf	*query_rdf = NULL;
	FILE		*log_fd = NULL;
	unsigned int	extras;
	uint16_t	resolver_port = DEFAULT_RESOLVER_PORT;
	int		timeout,
			tries,
			ipv4_enabled,
			ipv6_enabled,
			test_status = 0,
			ret = SYSINFO_RET_FAIL;

	if (5 != request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
		goto out;
	}

	/* resolver-specific parameters */
	GET_PARAM_NEMPTY(resolver_ip , 0, "IP address of local resolver");
	GET_PARAM_UINT  (timeout     , 1, "timeout in seconds");
	GET_PARAM_UINT  (tries       , 2, "maximum number of tries");
	GET_PARAM_UINT  (ipv4_enabled, 3, "IPv4 enabled");
	GET_PARAM_UINT  (ipv6_enabled, 4, "IPv6 enabled");

	/* open log file */
	if (SUCCEED != start_test(&log_fd, NULL, host, NULL, RSM_RESOLVERSTATUS_LOG_PREFIX, err, sizeof(err)))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, err));
		goto out;
	}

	/* print test details */
	rsm_infof(log_fd, "IPv4:%s"
			", IPv6:%s"
			", timeout:%d"
			", tries:%d",
			ENABLED(ipv4_enabled),
			ENABLED(ipv6_enabled),
			timeout,
			tries);

	extras = RESOLVER_EXTRAS_DNSSEC;

	/* create resolver */
	if (SUCCEED != rsm_create_resolver(&res, "resolver", resolver_ip, resolver_port, RSM_UDP, ipv4_enabled,
			ipv6_enabled, extras, RSM_UDP_TIMEOUT, RSM_UDP_RETRY, err, sizeof(err)))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot create resolver: %s.", err));
		goto out;
	}

	/* create query to check the connection */
	if (NULL == (query_rdf = ldns_rdf_new_frm_str(LDNS_RDF_TYPE_DNAME, ".")))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "cannot create DNS request"));
		goto out;
	}

	/* from this point item will not become NOTSUPPORTED */
	ret = SYSINFO_RET_OK;

	while (tries--)
	{
		if (SUCCEED == rsm_soa_query(res, query_rdf, RSM_SOA_QUERY_RECURSIVE, 0, log_fd, err, sizeof(err)))
			break;

		if (!tries)
		{
			rsm_errf(log_fd, "dns check of local resolver %s failed: %s", resolver_ip, err);
			goto out;
		}

		/* will try again */
		rsm_errf(log_fd, "dns check of local resolver %s failed: %s, will try %d more time%s",
				resolver_ip, err, tries, (1 == tries ? "" : "s"));
	}

	test_status = 1;
out:

	if (SYSINFO_RET_OK == ret)
	{
		rsm_infof(log_fd, "status of \"%s\": %d", resolver_ip, test_status);

		SET_UI64_RESULT(result, test_status);

		/* knock-down the probe if local resolver non-functional */
		if (0 == test_status)
			rsm_dc_errors_inc();
	}

	if (NULL != query_rdf)
		ldns_rdf_deep_free(query_rdf);

	if (NULL != res)
	{
		if (0 != ldns_resolver_nameserver_count(res))
			ldns_resolver_deep_free(res);
		else
			ldns_resolver_free(res);
	}

	end_test(log_fd, NULL, result);

	return ret;
}
