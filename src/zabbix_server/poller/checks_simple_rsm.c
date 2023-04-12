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

#include "sysinfo.h"
#include "checks_simple_rsm.h"
#include "zbxserver.h"
#include "comms.h"
#include "log.h"
#include "rsm.h"

#define RSM_HTTP_RESPONSE_OK	200L

typedef struct
{
	const char	*name;
	int		flag;
	ldns_rr_type	rr_type;
	const char	*resolve_reason;
}
ipv_t;

static const ipv_t	ipvs[] =
{
	{"IPv4",	RSM_FLAG_IPV4_ENABLED,	LDNS_RR_TYPE_A,		"resolve a host to IPv4 addresses"},
	{"IPv6",	RSM_FLAG_IPV6_ENABLED,	LDNS_RR_TYPE_AAAA,	"resolve a host to IPv6 addresses"},
	{NULL}
};

static const char	*log_prefixes[] = { "Empty", "Fatal", "Error", "Warning", "Info", "Debug" };

void	rsm_logf(FILE *log_fd, int level, const char *fmt, ...)
{
	va_list		args;
	char		fmt_buf[RSM_ERR_BUF_SIZE];
	struct timeval	current_time;
	struct tm	*tm;
	long		ms;

	va_start(args, fmt);

	/* fall back to regular Zabbix log */
	if (NULL == log_fd)
	{
		zbx_vsnprintf(fmt_buf, sizeof(fmt_buf), fmt, args);
		__zbx_zabbix_log(level, "%s", fmt_buf);
		goto out;
	}

	if (level > LOG_LEVEL_TRACE)
		level = LOG_LEVEL_TRACE;

	gettimeofday(&current_time, NULL);
	tm = localtime(&current_time.tv_sec);
	ms = current_time.tv_usec / 1000;

	zbx_snprintf(fmt_buf, sizeof(fmt_buf), "%6d:%.4d%.2d%.2d:%.2d%.2d%.2d.%03ld %s: %s\n",
			getpid(),
			tm->tm_year + 1900,
			tm->tm_mon + 1,
			tm->tm_mday,
			tm->tm_hour,
			tm->tm_min,
			tm->tm_sec,
			ms,
			log_prefixes[level],
			fmt);

	vfprintf(log_fd, fmt_buf, args);

	/* for instant log entries */
	fflush(log_fd);
out:
	va_end(args);
}

void	rsm_log(FILE *log_fd, int level, const char *text)
{
	struct timeval	current_time;
	struct tm	*tm;
	long		ms;

	/* fall back to regular Zabbix log */
	if (NULL == log_fd)
	{
		__zbx_zabbix_log(level, "%s", text);
		return;
	}

	if (level > LOG_LEVEL_TRACE)
		level = LOG_LEVEL_TRACE;

	gettimeofday(&current_time, NULL);
	tm = localtime(&current_time.tv_sec);
	ms = current_time.tv_usec / 1000;

	fprintf(log_fd, "%6d:%.4d%.2d%.2d:%.2d%.2d%.2d.%03ld %s: %s\n",
			getpid(),
			tm->tm_year + 1900,
			tm->tm_mon + 1,
			tm->tm_mday,
			tm->tm_hour,
			tm->tm_min,
			tm->tm_sec,
			ms,
			log_prefixes[level],
			text);
}

static const char	*get_probe_from_host(const char *host)
{
	const char	*p;

	if (NULL != (p = strchr(host, ' ')))
		return p + 1;

	return host;
}

/******************************************************************************
 *                                                                            *
 * Function: open_item_log                                                    *
 *                                                                            *
 * Purpose: Open log file for simple check                                    *
 *                                                                            *
 * Parameters: host     - [IN]  name of the host: <Probe> or <TLD Probe>      *
 *             tld      - [IN]  NULL in case of probe/resolver status checks  *
 *             name     - [IN]  name of the test: dns, rdds, epp, probestatus *
 *             err      - [OUT] buffer for error message                      *
 *             err_size - [IN]  size of err buffer                            *
 *                                                                            *
 * Return value: file descriptor in case of success, NULL otherwise           *
 *                                                                            *
 ******************************************************************************/
static FILE	*open_item_log(const char *host, const char *tld, const char *name, char *err, size_t err_size)
{
	FILE		*fd;
	char		*file_name;
	const char	*p = NULL, *probe;

	if (NULL == CONFIG_LOG_FILE)
	{
		zbx_strlcpy(err, "zabbix log file configuration parameter (LogFile) is not set", err_size);
		return NULL;
	}

	p = CONFIG_LOG_FILE + strlen(CONFIG_LOG_FILE) - 1;

	while (CONFIG_LOG_FILE != p && '/' != *p)
		p--;

	if (CONFIG_LOG_FILE == p)
		file_name = zbx_strdup(NULL, RSM_DEFAULT_LOGDIR);
	else
		file_name = zbx_dsprintf(NULL, "%.*s", (int)(p - CONFIG_LOG_FILE), CONFIG_LOG_FILE);

	probe = get_probe_from_host(host);

	if (NULL != tld)
	{
		file_name = zbx_strdcatf(file_name, "/%s-%s-%s.log", probe, tld, name);
	}
	else
		file_name = zbx_strdcatf(file_name, "/%s-%s.log", probe, name);

	if (NULL == (fd = fopen(file_name, "a")))
		zbx_snprintf(err, err_size, "cannot open log file \"%s\". %s.", file_name, strerror(errno));

	zbx_free(file_name);

	return fd;
}

int	start_test(FILE **log_fd, FILE *output_fd, const char *probe, const char *rsmhost, const char *suffix,
		char *err, size_t err_size)
{
	if (NULL == output_fd)
	{
		if (NULL == (*log_fd = open_item_log(probe, rsmhost, suffix, err, err_size)))
		{
			return FAIL;
		}
	}
	else
		*log_fd = output_fd;

	rsm_info(*log_fd, ">>> START TEST <<<");

	return SUCCEED;
}

void	end_test(FILE *log_fd, FILE *output_fd, AGENT_RESULT *result)
{
	/* nothing to do if the log file wasn't even opened */
	if (NULL == log_fd)
		return;

	if (0 != ISSET_MSG(result))
	{
		rsm_errf(log_fd, "Could not perform test: %s", result->msg);
	}
	else if (0 != ISSET_TEXT(result))
	{
		rsm_infof(log_fd, "Test result: %s", result->text);
	}
	else if (0 != ISSET_UI64(result))
	{
		rsm_infof(log_fd, "Test result: " ZBX_FS_UI64, result->ui64);
	}
	else
	{
		/* this should never be possible */
		rsm_err(log_fd, "INTERNAL ERROR: no result at the end of RSM test!");
		__zbx_zabbix_log(LOG_LEVEL_CRIT, "%s", "INTERNAL ERROR: no result at the end of RSM test!");
		exit(EXIT_FAILURE);
	}

	rsm_info(log_fd, ">>> END TEST <<<");

	/* no need to close the stdout file descriptor */
	if (log_fd == output_fd)
		return;

	fclose(log_fd);
}

int	rsm_validate_ip(const char *ip, int ipv4_enabled, int ipv6_enabled, ldns_rdf **ip_rdf_out, char *is_ipv4)
{
	ldns_rdf	*ip_rdf;

	if (0 != ipv4_enabled && NULL != (ip_rdf = ldns_rdf_new_frm_str(LDNS_RDF_TYPE_A, ip)))	/* try IPv4 */
	{
		if (NULL != is_ipv4)
			*is_ipv4 = 1;
	}
	else if (0 != ipv6_enabled && NULL != (ip_rdf = ldns_rdf_new_frm_str(LDNS_RDF_TYPE_AAAA, ip)))	/* try IPv6 */
	{
		if (NULL != is_ipv4)
			*is_ipv4 = 0;
	}
	else
		return FAIL;

	if (NULL != ip_rdf_out)
		*ip_rdf_out = ldns_rdf_clone(ip_rdf);

	ldns_rdf_deep_free(ip_rdf);

	return SUCCEED;
}

void	get_host_and_port_from_str(const char *str, char delim, char *host, size_t host_size, unsigned short *port,
		unsigned short default_port)
{
	char	*str_copy, *p;

	str_copy = zbx_strdup(NULL, str);

	if (NULL == (p = strchr(str_copy, delim)))
	{
		*port = default_port;
	}
	else
	{
		*p = '\0';
		p++;
		*port = (unsigned short)atoi(p);
	}

	zbx_snprintf(host, host_size, "%s", str_copy);

	zbx_free(str_copy);
}

rsm_subtest_result_t	rsm_subtest_result(int rtt, int rtt_limit)
{
	if (RSM_NO_VALUE == rtt)
		return RSM_SUBTEST_SUCCESS;

	/* knock-down the probe if we are hitting internal errors */
	if (RSM_EC_DNS_UDP_INTERNAL_GENERAL == rtt)
		rsm_dc_errors_inc();

	if (rtt <= RSM_EC_DNS_UDP_INTERNAL_GENERAL && RSM_EC_INTERNAL_LAST <= rtt)
		return RSM_SUBTEST_SUCCESS;

	return (0 > rtt || rtt > rtt_limit ? RSM_SUBTEST_FAIL : RSM_SUBTEST_SUCCESS);
}

static int	set_resolver(ldns_resolver *res, const char *name, const char *ip, uint16_t port, int ipv4_enabled,
		int ipv6_enabled, char *err, size_t err_size)
{
	ldns_rdf	*ip_rdf;
	ldns_status	status;

	if (SUCCEED != rsm_validate_ip(ip, ipv4_enabled, ipv6_enabled, &ip_rdf, NULL))
	{
		zbx_snprintf(err, err_size, "invalid or unsupported IP of \"%s\": \"%s\"", name, ip);
		return FAIL;
	}

	ldns_resolver_set_port(res, port);

	status = ldns_resolver_push_nameserver(res, ip_rdf);
	ldns_rdf_deep_free(ip_rdf);

	if (LDNS_STATUS_OK != status)
	{
		zbx_snprintf(err, err_size, "cannot set \"%s\" (%s, port:%hu) as resolver. %s.", name, ip, port,
				ldns_get_errorstr_by_id(status));
		return FAIL;
	}

	return SUCCEED;
}

int	rsm_change_resolver(ldns_resolver *res, const char *name, const char *ip, uint16_t port, int ipv4_enabled,
		int ipv6_enabled, char *err, size_t err_size)
{
	ldns_rdf	*pop;

	/* remove current list of nameservers from resolver */
	while (NULL != (pop = ldns_resolver_pop_nameserver(res)))
		ldns_rdf_deep_free(pop);

	return set_resolver(res, name, ip, port, ipv4_enabled, ipv6_enabled, err, err_size);
}

static unsigned char	ip_support(int ipv4_enabled, int ipv6_enabled)
{
	if (0 == ipv4_enabled)
		return 2;	/* IPv6 only, assuming ipv6_enabled and ipv4_enabled cannot be both 0 */

	if (0 == ipv6_enabled)
		return 1;	/* IPv4 only */

	return 0;	/* no preference */
}

int	rsm_create_resolver(ldns_resolver **res, const char *name, const char *ip, uint16_t port, char protocol,
		int ipv4_enabled, int ipv6_enabled, unsigned int extras, int timeout, unsigned char tries, char *err,
		size_t err_size)
{
	struct timeval	tv = {.tv_usec = 0, .tv_sec = timeout};

	if (NULL != *res)
		return rsm_change_resolver(*res, name, ip, port, ipv4_enabled, ipv6_enabled, err, err_size);

	/* create a new resolver */
	if (NULL == (*res = ldns_resolver_new()))
	{
		zbx_strlcpy(err, "cannot create new resolver (out of memory)", err_size);
		return FAIL;
	}

	/* push nameserver to it */
	if (SUCCEED != set_resolver(*res, name, ip, port, ipv4_enabled, ipv6_enabled, err, err_size))
		return FAIL;

	/* set timeout of one try */
	ldns_resolver_set_timeout(*res, tv);

	/* set number of tries */
	ldns_resolver_set_retry(*res, tries);

	/* set DNSSEC if needed */
	if (0 != (extras & RESOLVER_EXTRAS_NONE))
		ldns_resolver_set_dnssec(*res, 0);
	else if (0 != (extras & RESOLVER_EXTRAS_DNSSEC))
		ldns_resolver_set_dnssec(*res, 1);

	/* unset the CD flag */
	ldns_resolver_set_dnssec_cd(*res, false);

	/* use TCP or UDP */
	ldns_resolver_set_usevc(*res, (RSM_UDP == protocol ? false : true));

	/* set IP version support */
	ldns_resolver_set_ip6(*res, ip_support(ipv4_enabled, ipv6_enabled));

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: rsm_get_ts_from_host                                             *
 *                                                                            *
 * Purpose: Extract the Unix timestamp from the host name. Expected format of *
 *          the host: ns<optional digits><DOT or DASH><Unix timestamp>.       *
 *          Examples: ns2-1376488934.example.com.                             *
 *                    ns1.1376488934.example.com.                             *
 * Return value: SUCCEED - if host name correctly formatted and timestamp     *
 *               extracted, FAIL - otherwise                                  *
 *                                                                            *
 * Author: Vladimir Levijev                                                   *
 *                                                                            *
 ******************************************************************************/
int	rsm_get_ts_from_host(const char *host, time_t *ts)
{
	const char	*p, *p2;

	p = host;

	if (0 != strncmp("ns", p, 2))
		return FAIL;

	p += 2;

	while (0 != isdigit(*p))
		p++;

	if ('.' != *p && '-' != *p)
		return FAIL;

	p++;
	p2 = p;

	while (0 != isdigit(*p2))
		p2++;

	if ('.' != *p2)
		return FAIL;

	if (p2 == p || '0' == *p)
		return FAIL;

	if (1 != sscanf(p, ZBX_FS_TIME_T, ts))
		return FAIL;

	return SUCCEED;
}

size_t	rsm_random(size_t max_values)
{
	zbx_timespec_t	timespec;

	zbx_timespec(&timespec);

	srand((unsigned int)(timespec.sec + timespec.ns));

	return (size_t)rand() % max_values;
}

void	rsm_print_nameserver(FILE *log_fd, const ldns_resolver *res, const char *details)
{
	char	*name;

	if (0 == ldns_resolver_nameserver_count(res))
	{
		/* this should never be possible */
		rsm_err(log_fd, "INTERNAL ERROR: attempt to print nameserver while zero found!");
		__zbx_zabbix_log(LOG_LEVEL_CRIT, "%s", "INTERNAL ERROR: attempt to print nameserver while zero found!");
		exit(EXIT_FAILURE);
	}

	if (1 != ldns_resolver_nameserver_count(res))
	{
		/* this should never be possible */
		rsm_err(log_fd, "INTERNAL ERROR: attempt to print nameserver while more than one found!");
		__zbx_zabbix_log(LOG_LEVEL_CRIT, "%s", "INTERNAL ERROR: attempt to print nameserver while more than one found!");
		exit(EXIT_FAILURE);
	}

	name = ldns_rdf2str(ldns_resolver_nameservers(res)[0]);

	rsm_infof(log_fd, "making DNS query to %s:%u to %s", name, ldns_resolver_port(res), details);

	zbx_free(name);
}

/******************************************************************************
 *                                                                            *
 * Function: rsm_resolve_host                                                 *
 *                                                                            *
 * Purpose: resolve specified host to IPs                                     *
 *                                                                            *
 * Parameters: res          - [IN]  resolver object to use for resolving      *
 *             extras       - [IN]  bitmask of optional checks (a combination *
 *                                  of RSM_RESOLVER_CHECK_* defines)          *
 *             host         - [IN]  host name to resolve                      *
 *             ips          - [OUT] IPs resolved from specified host          *
 *             ipv_flags    - [IN]  mask of supported and enabled IP versions *
 *             log_fd       - [IN]  print resolved packets to specified file  *
 *                                  descriptor, cannot be NULL                *
 *             ec_res       - [OUT] resolver error code                       *
 *             err          - [OUT] in case of error, write the error string  *
 *                                  to specified buffer                       *
 *             err_size     - [IN]  error buffer size                         *
 *                                                                            *
 * Return value: SUCCEED - host resolved successfully                         *
 *               FAIL - otherwise                                             *
 *                                                                            *
 ******************************************************************************/
int	rsm_resolve_host(ldns_resolver *res, const char *host, zbx_vector_str_t *ips, int ipv_flags,
		FILE *log_fd, rsm_resolver_error_t *ec_res, char *err, size_t err_size)
{
	const ipv_t	*ipv;
	ldns_rdf	*rdf;
	int		ret = FAIL;

	if (NULL == (rdf = ldns_rdf_new_frm_str(LDNS_RDF_TYPE_DNAME, host)))
	{
		zbx_strlcpy(err, UNEXPECTED_LDNS_MEM_ERROR, err_size);
		*ec_res = RSM_RESOLVER_INTERNAL;
		return ret;
	}

	for (ipv = ipvs; NULL != ipv->name; ipv++)
	{
		ldns_pkt	*pkt;
		ldns_rr_list	*rr_list;
		ldns_pkt_rcode	rcode;
		ldns_status	status;

		if (0 == (ipv_flags & ipv->flag))
			continue;

		rsm_print_nameserver(log_fd, res, ipv->resolve_reason);

		status = ldns_resolver_query_status(&pkt, res, rdf, ipv->rr_type, LDNS_RR_CLASS_IN, LDNS_RD);

		if (LDNS_STATUS_OK != status)
		{
			zbx_snprintf(err, err_size, "cannot resolve host \"%s\" to %s address: %s", host, ipv->name,
					ldns_get_errorstr_by_id(status));
			*ec_res = RSM_RESOLVER_NOREPLY;
			goto out;
		}

		ldns_pkt_print(log_fd, pkt);

		if (LDNS_RCODE_NOERROR != (rcode = ldns_pkt_get_rcode(pkt)))
		{
			char	*rcode_str;

			rcode_str = ldns_pkt_rcode2str(rcode);
			zbx_snprintf(err, err_size, "expected NOERROR got %s", rcode_str);
			zbx_free(rcode_str);

			switch (rcode)
			{
				case LDNS_RCODE_SERVFAIL:
					*ec_res = RSM_RESOLVER_SERVFAIL;
					break;
				case LDNS_RCODE_NXDOMAIN:
					*ec_res = RSM_RESOLVER_NXDOMAIN;
					break;
				default:
					*ec_res = RSM_RESOLVER_CATCHALL;
			}

			ldns_pkt_free(pkt);
			goto out;
		}

		if (NULL != (rr_list = ldns_pkt_rr_list_by_type(pkt, ipv->rr_type, LDNS_SECTION_ANSWER)))
		{
			size_t	rr_count, i;

			rr_count = ldns_rr_list_rr_count(rr_list);

			for (i = 0; i < rr_count; i++)
				zbx_vector_str_append(ips, ldns_rdf2str(ldns_rr_a_address(ldns_rr_list_rr(rr_list, i))));

			ldns_rr_list_deep_free(rr_list);
		}

		ldns_pkt_free(pkt);
	}

	if (0 != ips->values_num)
	{
		zbx_vector_str_sort(ips, ZBX_DEFAULT_STR_COMPARE_FUNC);
		zbx_vector_str_uniq(ips, ZBX_DEFAULT_STR_COMPARE_FUNC);
	}

	ret = SUCCEED;
out:
	ldns_rdf_deep_free(rdf);

	return ret;
}

void	rsm_get_strings_from_list(zbx_vector_str_t *strings, char *list, char delim)
{
	char	*p, *p_end;

	if (NULL == list || '\0' == *list)
		return;

	p = list;
	while ('\0' != *p && delim == *p)
		p++;

	if ('\0' == *p)
		return;

	do
	{
		p_end = strchr(p, delim);
		if (NULL != p_end)
			*p_end = '\0';

		zbx_vector_str_append(strings, zbx_strdup(NULL, p));

		if (NULL != p_end)
		{
			*p_end = delim;

			while ('\0' != *p_end && delim == *p_end)
				p_end++;

			if ('\0' == *p_end)
				p_end = NULL;
			else
				p = p_end;
		}
	}
	while (NULL != p_end);
}

/* maps HTTP status codes ommitting status 200 and unassigned according to   */
/* http://www.iana.org/assignments/http-status-codes/http-status-codes.xhtml */
int	map_http_code(long http_code)
{
#if RSM_HTTP_RESPONSE_OK != 200L
#	error "Mapping of HTTP statuses to error codes is based on assumption that status 200 is not an error."
#endif

	switch (http_code)
	{
		case 100L:	/* Continue */
			return 0;
		case 101L:	/* Switching Protocols */
			return 1;
		case 102L:	/* Processing */
			return 2;
		case 103L:	/* Early Hints */
			return 3;
		case 200L:	/* OK */
			THIS_SHOULD_NEVER_HAPPEN;
			exit(EXIT_FAILURE);
		case 201L:	/* Created */
			return 4;
		case 202L:	/* Accepted */
			return 5;
		case 203L:	/* Non-Authoritative Information */
			return 6;
		case 204L:	/* No Content */
			return 7;
		case 205L:	/* Reset Content */
			return 8;
		case 206L:	/* Partial Content */
			return 9;
		case 207L:	/* Multi-Status */
			return 10;
		case 208L:	/* Already Reported */
			return 11;
		case 226L:	/* IM Used */
			return 12;
		case 300L:	/* Multiple Choices */
			return 13;
		/* 17.10.2018: 301, 302 and 303 were obsoleted because we follow redirects */
		case 304L:	/* Not Modified */
			return 17;
		case 305L:	/* Use Proxy */
			return 18;
		case 306L:	/* (Unused) */
			return 19;
		case 307L:	/* Temporary Redirect */
			return 20;
		case 308L:	/* Permanent Redirect */
			return 21;
		case 400L:	/* Bad Request */
			return 22;
		case 401L:	/* Unauthorized */
			return 23;
		case 402L:	/* Payment Required */
			return 24;
		case 403L:	/* Forbidden */
			return 25;
		case 404L:	/* Not Found */
			return 26;
		case 405L:	/* Method Not Allowed */
			return 27;
		case 406L:	/* Not Acceptable */
			return 28;
		case 407L:	/* Proxy Authentication Required */
			return 29;
		case 408L:	/* Request Timeout */
			return 30;
		case 409L:	/* Conflict */
			return 31;
		case 410L:	/* Gone */
			return 32;
		case 411L:	/* Length Required */
			return 33;
		case 412L:	/* Precondition Failed */
			return 34;
		case 413L:	/* Payload Too Large */
			return 35;
		case 414L:	/* URI Too Long */
			return 36;
		case 415L:	/* Unsupported Media Type */
			return 37;
		case 416L:	/* Range Not Satisfiable */
			return 38;
		case 417L:	/* Expectation Failed */
			return 39;
		case 421L:	/* Misdirected Request */
			return 40;
		case 422L:	/* Unprocessable Entity */
			return 41;
		case 423L:	/* Locked */
			return 42;
		case 424L:	/* Failed Dependency */
			return 43;
		case 426L:	/* Upgrade Required */
			return 44;
		case 428L:	/* Precondition Required */
			return 45;
		case 429L:	/* Too Many Requests */
			return 46;
		case 431L:	/* Request Header Fields Too Large */
			return 47;
		case 451L:	/* Unavailable For Legal Reasons */
			return 48;
		case 500L:	/* Internal Server Error */
			return 49;
		case 501L:	/* Not Implemented */
			return 50;
		case 502L:	/* Bad Gateway */
			return 51;
		case 503L:	/* Service Unavailable */
			return 52;
		case 504L:	/* Gateway Timeout */
			return 53;
		case 505L:	/* HTTP Version Not Supported */
			return 54;
		case 506L:	/* Variant Also Negotiates */
			return 55;
		case 507L:	/* Insufficient Storage */
			return 56;
		case 508L:	/* Loop Detected */
			return 57;
		case 510L:	/* Not Extended */
			return 58;
		case 511L:	/* Network Authentication Required */
			return 59;
		default:	/* catch-all for newly assigned HTTP status codes that may not have an association */
			return 60;
	}
}

/* callback for curl to store the response body */
static size_t	response_function(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	writedata_t	*data = (writedata_t *)userdata;
	size_t		r_size = size * nmemb;

	zbx_strncpy_alloc(&data->buf, &data->alloc, &data->offset, (const char *)ptr, r_size);

	return r_size;
}

/* this is a pointer to a user-passed writedata_t object */
static writedata_t	*debugdata;

static int	request_headers_function(CURL *handle, curl_infotype type, char *data, size_t size, void *userp)
{
	ZBX_UNUSED(handle);
	ZBX_UNUSED(userp);

	if (CURLINFO_HEADER_OUT == type)
		zbx_strncpy_alloc(&debugdata->buf, &debugdata->alloc, &debugdata->offset, data, size);

	return 0;
}

/* allocates memory and returns a buffer to the details */
static char	*get_curl_details(CURL *easyhandle)
{
	char		*string, *output = NULL;
	long		number;
	double		precision;
	CURLcode	curl_err;

#define GET_DETAIL(FIELD, name, var, fmt)										\
	do														\
	{														\
		if (CURLE_OK != (curl_err = curl_easy_getinfo(easyhandle, CURLINFO_ ## FIELD, &var)))			\
		{													\
			output = zbx_strdcatf(output, "\n  Error: cannot get \"%s\" from response (%s)",		\
					name, curl_easy_strerror(curl_err));						\
			return output;											\
		}													\
		output = zbx_strdcatf(output, "\n  %s=" fmt, name, var);						\
	}														\
	while (0)

	GET_DETAIL(CONTENT_TYPE      , "content_type"      , string   , "%s");
	GET_DETAIL(RESPONSE_CODE     , "http_code"         , number   , "%ld");
	GET_DETAIL(LOCAL_IP          , "local_ip"          , string   , "%s");
	GET_DETAIL(LOCAL_PORT        , "local_port"        , number   , "%ld");
	GET_DETAIL(REDIRECT_COUNT    , "num_redirects"     , number   , "%ld");
	GET_DETAIL(PRIMARY_IP        , "remote_ip"         , string   , "%s");
	GET_DETAIL(PRIMARY_PORT      , "remote_port"       , number   , "%ld");
	GET_DETAIL(SIZE_DOWNLOAD     , "size_download"     , precision, "%.0f");
	GET_DETAIL(HEADER_SIZE       , "size_header"       , number   , "%ld");
	GET_DETAIL(APPCONNECT_TIME   , "time_appconnect"   , precision, "%.2f");
	GET_DETAIL(CONNECT_TIME      , "time_connect"      , precision, "%.2f");
	GET_DETAIL(NAMELOOKUP_TIME   , "time_namelookup"   , precision, "%.2f");
	GET_DETAIL(PRETRANSFER_TIME  , "time_pretransfer"  , precision, "%.2f");
	GET_DETAIL(REDIRECT_TIME     , "time_redirect"     , precision, "%.2f");
	GET_DETAIL(STARTTRANSFER_TIME, "time_starttransfer", precision, "%.2f");
	GET_DETAIL(TOTAL_TIME        , "time_total"        , precision, "%.3f");
	GET_DETAIL(EFFECTIVE_URL     , "url_effective"     , string   , "%s");

#undef GET_DETAIL

	return output;
}

/* Helper function for Web-based RDDS80 and RDAP checks. Adds host to header, connects to URL obeying timeout and */
/* max redirect settings, stores web page contents using provided callback, checks for OK response and calculates */
/* round-trip time. When function succeeds it returns RTT in milliseconds. When function fails it returns source  */
/* of error in provided RTT parameter. Does not verify certificates.                                              */
int	rsm_http_test(const char *host, const char *url, long timeout, long maxredirs, rsm_http_error_t *ec_http, int *rtt,
		writedata_t *request_headers, void *response, char **transfer_details, char *err, size_t err_size)
{
#ifdef HAVE_LIBCURL
	CURL			*easyhandle;
	CURLcode		curl_err;
	CURLoption		opt;
	char			host_buf[RSM_BUF_SIZE];
	double			total_time;
	long			response_code;
	struct curl_slist	*slist = NULL;
#endif
	int			ret = FAIL;

#ifdef HAVE_LIBCURL
	/* point it to the user passed object to be used in the request_headers_function() callback */
	debugdata = request_headers;

	if (NULL == (easyhandle = curl_easy_init()))
	{
		ec_http->type = PRE_HTTP_STATUS_ERROR;
		ec_http->error.pre_status_error = RSM_EC_PRE_STATUS_ERROR_INTERNAL;

		zbx_strlcpy(err, "cannot init cURL library", err_size);
		goto out;
	}

	zbx_snprintf(host_buf, sizeof(host_buf), "Host: %s", host);
	if (NULL == (slist = curl_slist_append(slist, host_buf)))
	{
		ec_http->type = PRE_HTTP_STATUS_ERROR;
		ec_http->error.pre_status_error = RSM_EC_PRE_STATUS_ERROR_INTERNAL;

		zbx_strlcpy(err, "cannot generate cURL list of HTTP headers", err_size);
		goto out;
	}

	if (CURLE_OK != (curl_err = curl_easy_setopt(easyhandle, opt = CURLOPT_FOLLOWLOCATION, 1L)) ||
			CURLE_OK != (curl_err = curl_easy_setopt(easyhandle, opt = CURLOPT_USERAGENT, "Zabbix " ZABBIX_VERSION)) ||
			CURLE_OK != (curl_err = curl_easy_setopt(easyhandle, opt = CURLOPT_VERBOSE, 1L)) || /* this must be turned on for debugfunction */
			CURLE_OK != (curl_err = curl_easy_setopt(easyhandle, opt = CURLOPT_MAXREDIRS, maxredirs)) ||
			CURLE_OK != (curl_err = curl_easy_setopt(easyhandle, opt = CURLOPT_URL, url)) ||
			CURLE_OK != (curl_err = curl_easy_setopt(easyhandle, opt = CURLOPT_TIMEOUT, timeout)) ||
			CURLE_OK != (curl_err = curl_easy_setopt(easyhandle, opt = CURLOPT_HTTPHEADER, slist)) ||
			CURLE_OK != (curl_err = curl_easy_setopt(easyhandle, opt = CURLOPT_SSL_VERIFYPEER, 0L)) ||
			CURLE_OK != (curl_err = curl_easy_setopt(easyhandle, opt = CURLOPT_SSL_VERIFYHOST, 0L)) ||
			CURLE_OK != (curl_err = curl_easy_setopt(easyhandle, opt = CURLOPT_WRITEDATA, response)) ||
			CURLE_OK != (curl_err = curl_easy_setopt(easyhandle, opt = CURLOPT_WRITEFUNCTION, response_function)) ||
			CURLE_OK != (curl_err = curl_easy_setopt(easyhandle, opt = CURLOPT_DEBUGFUNCTION, request_headers_function)))
	{
		ec_http->type = PRE_HTTP_STATUS_ERROR;
		ec_http->error.pre_status_error = RSM_EC_PRE_STATUS_ERROR_INTERNAL;

		zbx_snprintf(err, err_size, "cannot set cURL option [%d] (%s)", (int)opt, curl_easy_strerror(curl_err));
		goto out;
	}

	if (CURLE_OK != (curl_err = curl_easy_perform(easyhandle)))
	{
		ec_http->type = PRE_HTTP_STATUS_ERROR;

		switch (curl_err)
		{
			case CURLE_OPERATION_TIMEDOUT:
				ec_http->error.pre_status_error = RSM_EC_PRE_STATUS_ERROR_TO;
				break;
			case CURLE_COULDNT_CONNECT:
				ec_http->error.pre_status_error = RSM_EC_PRE_STATUS_ERROR_ECON;
				break;
			case CURLE_TOO_MANY_REDIRECTS:
				ec_http->error.pre_status_error = RSM_EC_PRE_STATUS_ERROR_EMAXREDIRECTS;
				break;
			default:
				if (0 == strncmp(url, "http://", ZBX_CONST_STRLEN("http://")))
					ec_http->error.pre_status_error = RSM_EC_PRE_STATUS_ERROR_EHTTP;
				else	/* if (0 == strncmp(url, "https://", ZBX_CONST_STRLEN("https://"))) */
					ec_http->error.pre_status_error = RSM_EC_PRE_STATUS_ERROR_EHTTPS;
		}

		zbx_strlcpy(err, curl_easy_strerror(curl_err), err_size);
		goto out;
	}

	*transfer_details = get_curl_details(easyhandle);

	/* total time */
	if (CURLE_OK != (curl_err = curl_easy_getinfo(easyhandle, CURLINFO_TOTAL_TIME, &total_time)))
	{
		ec_http->type = PRE_HTTP_STATUS_ERROR;
		ec_http->error.pre_status_error = RSM_EC_PRE_STATUS_ERROR_INTERNAL;

		zbx_snprintf(err, err_size, "cannot get HTTP request time (%s)", curl_easy_strerror(curl_err));
		goto out;
	}

	/* HTTP status code */
	if (CURLE_OK != (curl_err = curl_easy_getinfo(easyhandle, CURLINFO_RESPONSE_CODE, &response_code)))
	{
		ec_http->type = PRE_HTTP_STATUS_ERROR;
		ec_http->error.pre_status_error = RSM_EC_PRE_STATUS_ERROR_NOCODE;

		zbx_snprintf(err, err_size, "cannot get HTTP response code (%s)", curl_easy_strerror(curl_err));
		goto out;
	}

	if (RSM_HTTP_RESPONSE_OK != response_code)
	{
		ec_http->type = HTTP_STATUS_ERROR;
		ec_http->error.response_code = response_code;

		zbx_snprintf(err, err_size, "invalid HTTP response code, expected %ld, got %ld", RSM_HTTP_RESPONSE_OK,
				response_code);
		goto out;
	}

	*rtt = (int)(total_time * 1000);	/* expected in ms */

	ret = SUCCEED;
out:
	if (NULL != slist)
		curl_slist_free_all(slist);

	if (NULL != easyhandle)
		curl_easy_cleanup(easyhandle);
#else
	ec_http->type = PRE_HTTP_STATUS_ERROR;
	ec_http->error.pre_status_error = RSM_EC_PRE_STATUS_ERROR_INTERNAL;

	zbx_strlcpy(err, "zabbix is not compiled with libcurl support (--with-libcurl)", err_size);
#endif
	return ret;
}

void	rsm_vector_str_clean_and_destroy(zbx_vector_str_t *v)
{
	int	i;

	for (i = 0; i < v->values_num; i++)
		zbx_free(v->values[i]);

	zbx_vector_str_destroy(v);
}

/* Splits provided URL into preceding "https://" or "http://", domain name and the rest, frees memory pointed by   */
/* scheme, domain and path pointers and allocates new storage. It is caller responsibility to free them after use. */
int	rsm_split_url(const char *url, char **scheme, char **domain, int *port, char **path, char *err, size_t err_size)
{
	const char	*tmp;

	if (0 == strncmp(url, "https://", ZBX_CONST_STRLEN("https://")))
	{
		*scheme = zbx_strdup(*scheme, "https://");
		url += ZBX_CONST_STRLEN("https://");
		*port = 443;
	}
	else if (0 == strncmp(url, "http://", ZBX_CONST_STRLEN("http://")))
	{
		*scheme = zbx_strdup(*scheme, "http://");
		url += ZBX_CONST_STRLEN("http://");
		*port = 80;
	}
	else
	{
		zbx_snprintf(err, err_size, "unrecognized scheme in URL \"%s\"", url);
		return FAIL;
	}

	if (NULL != (tmp = strchr(url, ':')))
	{
		size_t	len = (size_t)(tmp - url);

		if (0 == isdigit(*(tmp + 1)))
		{
			zbx_snprintf(err, err_size, "invalid port in URL \"%s\"", url);
			return FAIL;
		}

		zbx_free(*domain);
		*domain = (char *)zbx_malloc(*domain, len + 1);
		memcpy(*domain, url, len);
		(*domain)[len] = '\0';

		url = tmp + 1;

		/* override port and move forward */
		*port = atoi(url);

		/* and move forward */
		while (*url != '\0' && *url != '/')
			url++;

		*path = zbx_strdup(*path, url);
	}
	else if (NULL != (tmp = strchr(url, '/')))
	{
		size_t	len = (size_t)(tmp - url);

		zbx_free(*domain);
		*domain = (char *)zbx_malloc(*domain, len + 1);
		memcpy(*domain, url, len);
		(*domain)[len] = '\0';
		*path = zbx_strdup(*path, tmp);
	}
	else
	{
		*domain = zbx_strdup(*domain, url);
		*path = zbx_strdup(*path, "");
	}

	return SUCCEED;
}

int	rsm_soa_query(const ldns_resolver *res, ldns_rdf *query_rdf, unsigned int flags, int reply_ms, FILE *log_fd,
		char *err, size_t err_size)
{
	ldns_pkt	*pkt = NULL;
	ldns_rr_list	*rrset = NULL;
	uint16_t	query_flags = 0;
	int		ret = FAIL;

	if (0 != (flags & RSM_SOA_QUERY_RECURSIVE))
		query_flags = LDNS_RD;

	rsm_print_nameserver(log_fd, res, "get resource records of type SOA");

	if (NULL == (pkt = ldns_resolver_query(res, query_rdf, LDNS_RR_TYPE_SOA, LDNS_RR_CLASS_IN, query_flags)))
	{
		zbx_strlcpy(err, "cannot connect to host", err_size);
		goto out;
	}

	ldns_pkt_print(log_fd, pkt);

	if (NULL == (rrset = ldns_pkt_rr_list_by_type(pkt, LDNS_RR_TYPE_SOA, LDNS_SECTION_ANSWER)))
	{
		zbx_strlcpy(err, "no SOA records found", err_size);
		goto out;
	}

	ldns_rr_list_deep_free(rrset);
	rrset = NULL;

	if (0 != (flags & RSM_SOA_QUERY_RRSIGS) &&
			NULL == (rrset = ldns_pkt_rr_list_by_type(pkt, LDNS_RR_TYPE_RRSIG, LDNS_SECTION_ANSWER)))
	{
		zbx_strlcpy(err, "no RRSIG records found", err_size);
		goto out;
	}

	if (0 != (flags & RSM_SOA_QUERY_RTT) && ldns_pkt_querytime(pkt) > (uint32_t)reply_ms)
	{
		zbx_snprintf(err, err_size, "query RTT %u over limit (%d)", ldns_pkt_querytime(pkt), reply_ms);
		goto out;
	}

	/* target succeeded */
	ret = SUCCEED;
out:
	if (NULL != rrset)
		ldns_rr_list_deep_free(rrset);

	if (NULL != pkt)
		ldns_pkt_free(pkt);

	return ret;
}
