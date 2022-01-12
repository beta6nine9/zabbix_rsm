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

#include <ldns/ldns.h>

#include "sysinfo.h"
#include "checks_simple_rsm.h"
#include "zbxserver.h"
#include "comms.h"
#include "base64.h"
#include "md5.h"
#include "threads.h"
#include "log.h"
#include "rsm.h"

#define ZBX_HOST_BUF_SIZE	128
#define ZBX_ERR_BUF_SIZE	8192
#define ZBX_SEND_BUF_SIZE	128
#define ZBX_RDDS_PREVIEW_SIZE	100

#define DEFAULT_RESOLVER_PORT	53
#define DEFAULT_NAMESERVER_PORT	53
#define DEFAULT_RDDS43_PORT	43
#define ZBX_HTTP_RESPONSE_OK	200L

#define XML_PATH_SERVER_ID	0
#define XML_PATH_RESULT_CODE	1

#define XML_VALUE_BUF_SIZE	512

#define EPP_SUCCESS_CODE_GENERAL	"1000"
#define EPP_SUCCESS_CODE_LOGOUT		"1500"

#define COMMAND_LOGIN	"login"
#define COMMAND_INFO	"info"
#define COMMAND_UPDATE	"update"
#define COMMAND_LOGOUT	"logout"

#define UNEXPECTED_LDNS_ERROR		"unexpected LDNS error"
#define UNEXPECTED_LDNS_MEM_ERROR	UNEXPECTED_LDNS_ERROR " (out of memory?)"

#define LDNS_EDNS_NSID		3	/* NSID option code, from RFC5001 */
#define NSID_MAX_LENGTH		127	/* hex representation of NSID must fit into 255 characters */

extern const char	*CONFIG_LOG_FILE;
extern const char	epp_passphrase[128];

#define ZBX_FLAG_IPV4_ENABLED	0x1
#define ZBX_FLAG_IPV6_ENABLED	0x2

#define ZBX_EC_EPP_NOT_IMPLEMENTED	ZBX_EC_EPP_INTERNAL_GENERAL

#define METADATA_FILE_PREFIX	"/tmp/dns-test-metadata"	/* /tmp/dns-test-metadata-<TLD>.bin */

#define GET_PARAM(output_var, param_num)										\
															\
do 															\
{															\
	output_var = get_rparam(request, param_num);									\
} 															\
while (0)

#define GET_PARAM_NEMPTY(output_var, param_num, description)								\
															\
do 															\
{															\
	output_var = get_rparam(request, param_num);									\
															\
	if ('\0' == *output_var)											\
	{														\
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Invalid parameter #%d: %s cannot be empty.",			\
				param_num + 1, description));								\
		goto out;												\
	}														\
} 															\
while (0)

#define GET_PARAM_UINT(output_var, param_num, description)								\
															\
do															\
{															\
	char	*param_str;												\
															\
	param_str = get_rparam(request, param_num);									\
															\
	if (SUCCEED != is_uint31(param_str, &output_var))								\
	{														\
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Invalid parameter #%d: %s.", param_num + 1, description));	\
		goto out;												\
	}														\
}															\
while (0)

typedef struct
{
	const char	*name;
	int		flag;
	ldns_rr_type	rr_type;
}
zbx_ipv_t;

static const zbx_ipv_t	ipvs[] =
{
	{"IPv4",	ZBX_FLAG_IPV4_ENABLED,	LDNS_RR_TYPE_A},
	{"IPv6",	ZBX_FLAG_IPV6_ENABLED,	LDNS_RR_TYPE_AAAA},
	{NULL}
};

/* used in libcurl callback function to store webpage contents in memory */
typedef struct
{
	char	*buf;
	size_t	alloc;
	size_t	offset;
}
curl_data_t;

#define RESOLVER_EXTRAS_NONE	0x0u
#define RESOLVER_EXTRAS_DNSSEC	0x1u

typedef enum
{
	ZBX_INTERNAL_GENERAL,
	ZBX_INTERNAL_IP_UNSUP,
	ZBX_INTERNAL_RES_CATCHALL
}
zbx_internal_error_t;

typedef enum
{
	ZBX_RESOLVER_INTERNAL,
	ZBX_RESOLVER_NOREPLY,
	ZBX_RESOLVER_SERVFAIL,
	ZBX_RESOLVER_NXDOMAIN,
	ZBX_RESOLVER_CATCHALL
}
zbx_resolver_error_t;

typedef enum
{
	ZBX_DNSKEYS_INTERNAL,
	ZBX_DNSKEYS_NOREPLY,
	ZBX_DNSKEYS_NONE,
	ZBX_DNSKEYS_NOADBIT,
	ZBX_DNSKEYS_NXDOMAIN,
	ZBX_DNSKEYS_CATCHALL
}
zbx_dnskeys_error_t;

typedef enum
{
	ZBX_NS_ANSWER_INTERNAL,
	ZBX_NS_ANSWER_ERROR_NOAAFLAG,
	ZBX_NS_ANSWER_ERROR_NODOMAIN
}
zbx_ns_answer_error_t;

typedef enum
{
	ZBX_NS_QUERY_INTERNAL,
	ZBX_NS_QUERY_NOREPLY,		/* only UDP */
	ZBX_NS_QUERY_ECON,		/* only TCP */
	ZBX_NS_QUERY_TO,		/* only TCP */
	ZBX_NS_QUERY_INC_HEADER,
	ZBX_NS_QUERY_INC_QUESTION,
	ZBX_NS_QUERY_INC_ANSWER,
	ZBX_NS_QUERY_INC_AUTHORITY,
	ZBX_NS_QUERY_INC_ADDITIONAL,
	ZBX_NS_QUERY_CATCHALL
}
zbx_ns_query_error_t;

typedef enum
{
	ZBX_EC_DNSSEC_INTERNAL,
	ZBX_EC_DNSSEC_ALGO_UNKNOWN,	/* ldns status: LDNS_STATUS_CRYPTO_UNKNOWN_ALGO */
	ZBX_EC_DNSSEC_ALGO_NOT_IMPL,	/* ldns status: LDNS_STATUS_CRYPTO_ALGO_NOT_IMPL */
	ZBX_EC_DNSSEC_RRSIG_NONE,
	ZBX_EC_DNSSEC_NO_NSEC_IN_AUTH,
	ZBX_EC_DNSSEC_RRSIG_NOTCOVERED,
	ZBX_EC_DNSSEC_RRSIG_NOT_SIGNED,	/* ldns status: LDNS_STATUS_CRYPTO_NO_MATCHING_KEYTAG_DNSKEY */
	ZBX_EC_DNSSEC_SIG_BOGUS,	/* ldns status: LDNS_STATUS_CRYPTO_BOGUS */
	ZBX_EC_DNSSEC_SIG_EXPIRED,	/* ldns status: LDNS_STATUS_CRYPTO_SIG_EXPIRED */
	ZBX_EC_DNSSEC_SIG_NOT_INCEPTED,	/* ldns status: LDNS_STATUS_CRYPTO_SIG_NOT_INCEPTED */
	ZBX_EC_DNSSEC_SIG_EX_BEFORE_IN,	/* ldns status: LDNS_STATUS_CRYPTO_EXPIRATION_BEFORE_INCEPTION */
	ZBX_EC_DNSSEC_NSEC3_ERROR,	/* ldns status: LDNS_STATUS_NSEC3_ERR */
	ZBX_EC_DNSSEC_RR_NOTCOVERED,	/* ldns status: LDNS_STATUS_DNSSEC_NSEC_RR_NOT_COVERED */
	ZBX_EC_DNSSEC_WILD_NOTCOVERED,	/* ldns status: LDNS_STATUS_DNSSEC_NSEC_WILDCARD_NOT_COVERED */
	ZBX_EC_DNSSEC_RRSIG_MISS_RDATA,	/* ldns status: LDNS_STATUS_MISSING_RDATA_FIELDS_RRSIG */
	ZBX_EC_DNSSEC_CATCHALL		/* ldns status: catch all */
}
zbx_dnssec_error_t;

typedef enum
{
	ZBX_EC_RR_CLASS_INTERNAL,
	ZBX_EC_RR_CLASS_CHAOS,
	ZBX_EC_RR_CLASS_HESIOD,
	ZBX_EC_RR_CLASS_CATCHALL
}
zbx_rr_class_error_t;

typedef enum
{
	ZBX_SUBTEST_SUCCESS,
	ZBX_SUBTEST_FAIL
}
zbx_subtest_result_t;

typedef struct
{
	char	*ip;
	int	rtt;
	int	upd;
	char	*nsid;
}
zbx_ns_ip_t;

typedef struct
{
	char		*name;
	char		result;
	zbx_ns_ip_t	*ips;
	size_t		ips_num;
}
zbx_ns_t;

typedef struct
{
	pid_t	pid;
	int	fd;	/* read from this file descriptor */
	int	log_fd;	/* read logs from this file descriptor */
}
writer_thread_t;

#define PACK_NUM_VARS	5
#define PACK_FORMAT	ZBX_FS_SIZE_T "|" ZBX_FS_SIZE_T "|%d|%d|%s"

static const char	*rsm_log_prefixes[] = { "Empty", "Fatal", "Error", "Warning", "Info", "Debug" };

#define rsm_dump(log_fd, fmt, ...)	fprintf(log_fd, ZBX_CONST_STRING(fmt), ##__VA_ARGS__)
#define rsm_errf(log_fd, fmt, ...)	rsm_logf(log_fd, LOG_LEVEL_ERR, ZBX_CONST_STRING(fmt), ##__VA_ARGS__)
#define rsm_warnf(log_fd, fmt, ...)	rsm_logf(log_fd, LOG_LEVEL_WARNING, ZBX_CONST_STRING(fmt), ##__VA_ARGS__)
#define rsm_infof(log_fd, fmt, ...)	rsm_logf(log_fd, LOG_LEVEL_DEBUG, ZBX_CONST_STRING(fmt), ##__VA_ARGS__)

static void	rsm_logf(FILE *log_fd, int level, const char *fmt, ...)
{
	va_list		args;
	char		fmt_buf[ZBX_ERR_BUF_SIZE];
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
			rsm_log_prefixes[level],
			fmt);

	vfprintf(log_fd, fmt_buf, args);

	/* in the future consider uncommenting this for instant log entries */
	/* fflush(log_fd); */
out:
	va_end(args);
}

#define rsm_err(log_fd, text)	rsm_log(log_fd, LOG_LEVEL_ERR, text)
#define rsm_info(log_fd, text)	rsm_log(log_fd, LOG_LEVEL_DEBUG, text)

static void	rsm_log(FILE *log_fd, int level, const char *text)
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
			rsm_log_prefixes[level],
			text);
}

static size_t	pack_values(size_t v1, size_t v2, int v3, int v4, char *nsid, char *buf, size_t buf_size)
{
	return zbx_snprintf(buf, buf_size, PACK_FORMAT, v1, v2, v3, v4, (NULL == nsid) ? "" : nsid);
}

static int	unpack_values(size_t *v1, size_t *v2, int *v3, int *v4, char *nsid, char *buf, FILE *log_fd)
{
	int rv = sscanf(buf, PACK_FORMAT, v1, v2, v3, v4, nsid);

	if (PACK_NUM_VARS == rv + 1)
	{
		nsid[0] = '\0';
	}
	else if (PACK_NUM_VARS != rv)
	{
		rsm_errf(log_fd, "cannot unpack values (unpacked %d, need %d)", rv, PACK_NUM_VARS);

		return FAIL;
	}

	return SUCCEED;
}

static int	zbx_validate_ip(const char *ip, int ipv4_enabled, int ipv6_enabled, ldns_rdf **ip_rdf_out,
		char *is_ipv4)
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

static int	zbx_set_resolver_ns(ldns_resolver *res, const char *name, const char *ip, uint16_t port,
		int ipv4_enabled, int ipv6_enabled, FILE *log_fd, char *err, size_t err_size)
{
	ldns_rdf	*ip_rdf;
	ldns_status	status;

	if (SUCCEED != zbx_validate_ip(ip, ipv4_enabled, ipv6_enabled, &ip_rdf, NULL))
	{
		zbx_snprintf(err, err_size, "invalid or unsupported IP of \"%s\": \"%s\"", name, ip);
		return FAIL;
	}

	ldns_resolver_set_port(res, port);

	status = ldns_resolver_push_nameserver(res, ip_rdf);
	ldns_rdf_deep_free(ip_rdf);

	if (LDNS_STATUS_OK != status)
	{
		zbx_snprintf(err, err_size, "cannot set \"%s\" (%s) as resolver. %s.", name, ip,
				ldns_get_errorstr_by_id(status));
		return FAIL;
	}

	rsm_infof(log_fd, "successfully using %s (%s:%hu)", name, ip, port);
	return SUCCEED;
}

static unsigned char	ip_support(int ipv4_enabled, int ipv6_enabled)
{
	if (0 == ipv4_enabled)
		return 2;	/* IPv6 only, assuming ipv6_enabled and ipv4_enabled cannot be both 0 */

	if (0 == ipv6_enabled)
		return 1;	/* IPv4 only */

	return 0;	/* no preference */
}

static int	zbx_change_resolver(ldns_resolver *res, const char *name, const char *ip, uint16_t port,
		int ipv4_enabled, int ipv6_enabled, FILE *log_fd, char *err, size_t err_size)
{
	ldns_rdf	*pop;

	/* remove current list of nameservers from resolver */
	while (NULL != (pop = ldns_resolver_pop_nameserver(res)))
		ldns_rdf_deep_free(pop);

	return zbx_set_resolver_ns(res, name, ip, port, ipv4_enabled, ipv6_enabled, log_fd, err, err_size);
}

static int	zbx_create_resolver(ldns_resolver **res, const char *name, const char *ip, uint16_t port, char protocol,
		int ipv4_enabled, int ipv6_enabled, unsigned int extras, int timeout, unsigned char tries, FILE *log_fd,
		char *err, size_t err_size)
{
	struct timeval	tv = {.tv_usec = 0, .tv_sec = timeout};

	if (NULL != *res)
		return zbx_change_resolver(*res, name, ip, port, ipv4_enabled, ipv6_enabled, log_fd, err, err_size);

	/* create a new resolver */
	if (NULL == (*res = ldns_resolver_new()))
	{
		zbx_strlcpy(err, "cannot create new resolver (out of memory)", err_size);
		return FAIL;
	}

	/* push nameserver to it */
	if (SUCCEED != zbx_set_resolver_ns(*res, name, ip, port, ipv4_enabled, ipv6_enabled, log_fd, err, err_size))
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
 * Function: zbx_get_ts_from_host                                             *
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
static int      zbx_get_ts_from_host(const char *host, time_t *ts)
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

/***************************************************************/
/* max_values - maximum possible values starting from 0, e. g. */
/*                                                             */
/* max_values | possible values                                */
/* ----------------------------                                */
/*          2 | 0, 1                                           */
/*          5 | 0, 1, 2, 3, 4                                  */
/***************************************************************/
static size_t	zbx_random(size_t max_values)
{
	zbx_timespec_t	timespec;

	zbx_timespec(&timespec);

	srand((unsigned int)(timespec.sec + timespec.ns));

	return (size_t)rand() % max_values;
}

/***************************************/
/* Return last label of @name. E. g.:  */
/*                                     */
/* [IN] name        | [OUT] last_label */
/* -----------------+----------------- */
/* www.foo.bar.com  | bar.com          */
/* www.foo.bar.com. | com.             */
/*                                     */
/***************************************/
static int	zbx_get_last_label(const char *name, char **last_label, char *err, size_t err_size)
{
	const char	*last_label_start;

	if (NULL == name || '\0' == *name)
	{
		zbx_strlcpy(err, "the test name (PREFIX.TLD) is empty", err_size);
		return FAIL;
	}

	last_label_start = name + strlen(name) - 1;

	while (name != last_label_start && '.' != *last_label_start)
		last_label_start--;

	if (name == last_label_start)
	{
		zbx_snprintf(err, err_size, "cannot get last label from \"%s\"", name);
		return FAIL;
	}

	/* skip the dot */
	last_label_start--;

	if (name == last_label_start)
	{
		zbx_snprintf(err, err_size, "cannot get last label from \"%s\"", name);
		return FAIL;
	}

	while (name != last_label_start && '.' != *last_label_start)
		last_label_start--;

	if (name != last_label_start)
		last_label_start++;

	*last_label = zbx_strdup(*last_label, last_label_start);

	return SUCCEED;
}

static const char	*zbx_covered_to_str(ldns_rr_type covered_type)
{
	switch (covered_type)
	{
		case LDNS_RR_TYPE_DS:
			return "DS";
		case LDNS_RR_TYPE_NSEC:
			return "NSEC";
		case LDNS_RR_TYPE_NSEC3:
			return "NSEC3";
		default:
			return "*UNKNOWN*";
	}
}

static int	zbx_get_covered_rrsigs(const ldns_pkt *pkt, const ldns_rdf *owner, ldns_pkt_section s,
		ldns_rr_type covered_type, ldns_rr_list **result, zbx_dnssec_error_t *dnssec_ec,
		char *err, size_t err_size)
{
	ldns_rr_list	*rrsigs;
	ldns_rr		*rr;
	ldns_rdf	*covered_type_rdf;
	size_t		i, count;
	int		ret = FAIL;

	if (NULL != owner)
	{
		if (NULL == (rrsigs = ldns_pkt_rr_list_by_name_and_type(pkt, owner, LDNS_RR_TYPE_RRSIG, s)))
		{
			char	*owner_str;

			if (NULL == (owner_str = ldns_rdf2str(owner)))
			{
				zbx_snprintf(err, err_size, "ldns_rdf2str() returned NULL");
				*dnssec_ec = ZBX_EC_DNSSEC_INTERNAL;
			}
			else
			{
				zbx_snprintf(err, err_size, "no %s RRSIG records for owner \"%s\" found in reply",
						zbx_covered_to_str(covered_type), owner_str);
				*dnssec_ec = ZBX_EC_DNSSEC_RRSIG_NONE;
			}

			return FAIL;
		}
	}
	else
	{
		if (NULL == (rrsigs = ldns_pkt_rr_list_by_type(pkt, LDNS_RR_TYPE_RRSIG, s)))
		{
			zbx_snprintf(err, err_size, "no %s RRSIG records found in reply",
					zbx_covered_to_str(covered_type));
			*dnssec_ec = ZBX_EC_DNSSEC_RRSIG_NONE;
			return FAIL;
		}
	}

	*result = ldns_rr_list_new();

	count = ldns_rr_list_rr_count(rrsigs);
	for (i = 0; i < count; i++)
	{
		if (NULL == (rr = ldns_rr_list_rr(rrsigs, i)))
		{
			zbx_strlcpy(err, UNEXPECTED_LDNS_MEM_ERROR, err_size);
			*dnssec_ec = ZBX_EC_DNSSEC_INTERNAL;
			goto out;
		}

		if (NULL == (covered_type_rdf = ldns_rr_rrsig_typecovered(rr)))
		{
			zbx_snprintf(err, err_size, "cannot get the type covered of a LDNS_RR_TYPE_RRSIG rr");
			*dnssec_ec = ZBX_EC_DNSSEC_INTERNAL;
			goto out;
		}

		if (ldns_rdf2rr_type(covered_type_rdf) == covered_type &&
				0 == ldns_rr_list_push_rr(*result, ldns_rr_clone(rr)))
		{
			zbx_strlcpy(err, UNEXPECTED_LDNS_MEM_ERROR, err_size);
			*dnssec_ec = ZBX_EC_DNSSEC_INTERNAL;
			goto out;
		}
	}

	ret = SUCCEED;
out:
	if (SUCCEED != ret || 0 == ldns_rr_list_rr_count(*result))
	{
		ldns_rr_list_deep_free(*result);
		*result = NULL;
	}

	if (NULL != rrsigs)
		ldns_rr_list_deep_free(rrsigs);

	return ret;
}

static int	zbx_ldns_rdf_compare(const void *d1, const void *d2)
{
	return ldns_rdf_compare(*(const ldns_rdf **)d1, *(const ldns_rdf **)d2);
}

static void	zbx_get_owners(const ldns_rr_list *rr_list, zbx_vector_ptr_t *owners)
{
	size_t		i, count;

	count = ldns_rr_list_rr_count(rr_list);

	for (i = 0; i < count; i++)
		zbx_vector_ptr_append(owners, ldns_rdf_clone(ldns_rr_owner(ldns_rr_list_rr(rr_list, i))));

	zbx_vector_ptr_sort(owners, zbx_ldns_rdf_compare);
	zbx_vector_ptr_uniq(owners, zbx_ldns_rdf_compare);
}

static void	zbx_destroy_owners(zbx_vector_ptr_t *owners)
{
	int	i;

	for (i = 0; i < owners->values_num; i++)
		ldns_rdf_deep_free((ldns_rdf *)owners->values[i]);

	zbx_vector_ptr_destroy(owners);
}

#define ZBX_EC_DNS_TCP_NS_NOREPLY	ZBX_EC_DNS_TCP_INTERNAL_GENERAL;	/* only UDP */
#define ZBX_EC_DNS_UDP_NS_ECON		ZBX_EC_DNS_UDP_INTERNAL_GENERAL;	/* only TCP */
#define ZBX_EC_DNS_UDP_NS_TO		ZBX_EC_DNS_UDP_INTERNAL_GENERAL;	/* only TCP */

typedef int	(*zbx_ns_query_error_func_t)(zbx_ns_query_error_t);
#define ZBX_DEFINE_ZBX_NS_QUERY_ERROR_TO(__interface)					\
static int	zbx_ns_query_error_to_ ## __interface (zbx_ns_query_error_t err)	\
{											\
	switch (err)									\
	{										\
		case ZBX_NS_QUERY_INTERNAL:						\
			return ZBX_EC_ ## __interface ## _INTERNAL_GENERAL;		\
		case ZBX_NS_QUERY_NOREPLY:						\
			return ZBX_EC_ ## __interface ## _NS_NOREPLY;			\
		case ZBX_NS_QUERY_TO:							\
			return ZBX_EC_ ## __interface ## _NS_TO;			\
		case ZBX_NS_QUERY_ECON:							\
			return ZBX_EC_ ## __interface ## _NS_ECON;			\
		case ZBX_NS_QUERY_INC_HEADER:						\
			return ZBX_EC_ ## __interface ## _HEADER;			\
		case ZBX_NS_QUERY_INC_QUESTION:						\
			return ZBX_EC_ ## __interface ## _QUESTION;			\
		case ZBX_NS_QUERY_INC_ANSWER:						\
			return ZBX_EC_ ## __interface ## _ANSWER;			\
		case ZBX_NS_QUERY_INC_AUTHORITY:					\
			return ZBX_EC_ ## __interface ## _AUTHORITY;			\
		case ZBX_NS_QUERY_INC_ADDITIONAL:					\
			return ZBX_EC_ ## __interface ## _ADDITIONAL;			\
		default:								\
			return ZBX_EC_ ## __interface ## _CATCHALL;			\
	}										\
}

ZBX_DEFINE_ZBX_NS_QUERY_ERROR_TO(DNS_UDP)
ZBX_DEFINE_ZBX_NS_QUERY_ERROR_TO(DNS_TCP)

#undef ZBX_DEFINE_ZBX_NS_QUERY_ERROR_TO

typedef int	(*zbx_dnssec_error_func_t)(zbx_dnssec_error_t);
#define ZBX_DEFINE_ZBX_DNSSEC_ERROR_TO(__interface)					\
static int	zbx_dnssec_error_to_ ## __interface (zbx_dnssec_error_t err)		\
{											\
	switch (err)									\
	{										\
		case ZBX_EC_DNSSEC_INTERNAL:						\
			return ZBX_EC_ ## __interface ## _INTERNAL_GENERAL;		\
		case ZBX_EC_DNSSEC_ALGO_UNKNOWN:					\
			return ZBX_EC_ ## __interface ## _ALGO_UNKNOWN;			\
		case ZBX_EC_DNSSEC_ALGO_NOT_IMPL:					\
			return ZBX_EC_ ## __interface ## _ALGO_NOT_IMPL;		\
		case ZBX_EC_DNSSEC_RRSIG_NONE:						\
			return ZBX_EC_ ## __interface ## _RRSIG_NONE;			\
		case ZBX_EC_DNSSEC_NO_NSEC_IN_AUTH:					\
			return ZBX_EC_ ## __interface ## _NO_NSEC_IN_AUTH;		\
		case ZBX_EC_DNSSEC_RRSIG_NOTCOVERED:					\
			return ZBX_EC_ ## __interface ## _RRSIG_NOTCOVERED;		\
		case ZBX_EC_DNSSEC_RRSIG_NOT_SIGNED:					\
			return ZBX_EC_ ## __interface ## _RRSIG_NOT_SIGNED;		\
		case ZBX_EC_DNSSEC_SIG_BOGUS:						\
			return ZBX_EC_ ## __interface ## _SIG_BOGUS;			\
		case ZBX_EC_DNSSEC_SIG_EXPIRED:						\
			return ZBX_EC_ ## __interface ## _SIG_EXPIRED;			\
		case ZBX_EC_DNSSEC_SIG_NOT_INCEPTED:					\
			return ZBX_EC_ ## __interface ## _SIG_NOT_INCEPTED;		\
		case ZBX_EC_DNSSEC_SIG_EX_BEFORE_IN:					\
			return ZBX_EC_ ## __interface ## _SIG_EX_BEFORE_IN;		\
		case ZBX_EC_DNSSEC_NSEC3_ERROR:						\
			return ZBX_EC_ ## __interface ## _NSEC3_ERROR;			\
		case ZBX_EC_DNSSEC_RR_NOTCOVERED:					\
			return ZBX_EC_ ## __interface ## _RR_NOTCOVERED;		\
		case ZBX_EC_DNSSEC_WILD_NOTCOVERED:					\
			return ZBX_EC_ ## __interface ## _WILD_NOTCOVERED;		\
		case ZBX_EC_DNSSEC_RRSIG_MISS_RDATA:					\
			return ZBX_EC_ ## __interface ## _RRSIG_MISS_RDATA;		\
		default:								\
			return ZBX_EC_ ## __interface ## _DNSSEC_CATCHALL;		\
	}										\
}

ZBX_DEFINE_ZBX_DNSSEC_ERROR_TO(DNS_UDP)
ZBX_DEFINE_ZBX_DNSSEC_ERROR_TO(DNS_TCP)

#undef ZBX_DEFINE_ZBX_DNSSEC_ERROR_TO

typedef int	(*zbx_rr_class_error_func_t)(zbx_rr_class_error_t);
#define ZBX_DEFINE_ZBX_RR_CLASS_ERROR_TO(__interface)					\
static int	zbx_rr_class_error_to_ ## __interface (zbx_rr_class_error_t err)	\
{											\
	switch (err)									\
	{										\
		case ZBX_EC_RR_CLASS_INTERNAL:						\
			return ZBX_EC_ ## __interface ## _INTERNAL_GENERAL;		\
		case ZBX_EC_RR_CLASS_CHAOS:						\
			return ZBX_EC_ ## __interface ## _CLASS_CHAOS;			\
		case ZBX_EC_RR_CLASS_HESIOD:						\
			return ZBX_EC_ ## __interface ## _CLASS_HESIOD;			\
		case ZBX_EC_RR_CLASS_CATCHALL:						\
			return ZBX_EC_ ## __interface ## _CLASS_CATCHALL;		\
		default:								\
			THIS_SHOULD_NEVER_HAPPEN;					\
			return ZBX_EC_ ## __interface ## _INTERNAL_GENERAL;		\
	}										\
}

ZBX_DEFINE_ZBX_RR_CLASS_ERROR_TO(DNS_UDP)
ZBX_DEFINE_ZBX_RR_CLASS_ERROR_TO(DNS_TCP)

#undef ZBX_DEFINE_ZBX_RR_CLASS_ERROR_TO

/* map generic local resolver errors to interface specific ones */

#define ZBX_DEFINE_RESOLVER_ERROR_TO(__interface)					\
static int	zbx_resolver_error_to_ ## __interface (zbx_resolver_error_t err)	\
{											\
	switch (err)									\
	{										\
		case ZBX_RESOLVER_INTERNAL:						\
			return ZBX_EC_ ## __interface ## _INTERNAL_GENERAL;		\
		case ZBX_RESOLVER_NOREPLY:						\
			return ZBX_EC_ ## __interface ## _RES_NOREPLY;			\
		case ZBX_RESOLVER_SERVFAIL:						\
			return ZBX_EC_ ## __interface ## _RES_SERVFAIL;			\
		case ZBX_RESOLVER_NXDOMAIN:						\
			return ZBX_EC_ ## __interface ## _RES_NXDOMAIN;			\
		case ZBX_RESOLVER_CATCHALL:						\
			return ZBX_EC_ ## __interface ## _INTERNAL_RES_CATCHALL;	\
		default:								\
			THIS_SHOULD_NEVER_HAPPEN;					\
			return ZBX_EC_ ## __interface ## _INTERNAL_GENERAL;		\
	}										\
}

ZBX_DEFINE_RESOLVER_ERROR_TO(RDDS43)
ZBX_DEFINE_RESOLVER_ERROR_TO(RDDS80)
ZBX_DEFINE_RESOLVER_ERROR_TO(RDAP)

#undef ZBX_DEFINE_RESOLVER_ERROR_TO

typedef int	(*zbx_dnskeys_error_func_t)(zbx_dnskeys_error_t);
#define ZBX_DEFINE_DNSKEYS_ERROR_TO(__interface)					\
static int	zbx_dnskeys_error_to_ ## __interface (zbx_dnskeys_error_t err)		\
{											\
	switch (err)									\
	{										\
		case ZBX_DNSKEYS_INTERNAL:						\
			return ZBX_EC_ ## __interface ## _INTERNAL_GENERAL;		\
		case ZBX_DNSKEYS_NOREPLY:						\
			return ZBX_EC_ ## __interface ## _RES_NOREPLY;			\
		case ZBX_DNSKEYS_NONE:							\
			return ZBX_EC_ ## __interface ## _DNSKEY_NONE;			\
		case ZBX_DNSKEYS_NOADBIT:						\
			return ZBX_EC_ ## __interface ## _DNSKEY_NOADBIT;		\
		case ZBX_DNSKEYS_NXDOMAIN:						\
			return ZBX_EC_ ## __interface ## _RES_NXDOMAIN;			\
		case ZBX_DNSKEYS_CATCHALL:						\
			return ZBX_EC_ ## __interface ## _INTERNAL_RES_CATCHALL;	\
		default:								\
			THIS_SHOULD_NEVER_HAPPEN;					\
			return ZBX_EC_ ## __interface ## _INTERNAL_GENERAL;		\
	}										\
}

ZBX_DEFINE_DNSKEYS_ERROR_TO(DNS_UDP)
ZBX_DEFINE_DNSKEYS_ERROR_TO(DNS_TCP)

#undef ZBX_DEFINE_DNSKEYS_ERROR_TO

/* map generic name server errors to interface specific ones */

typedef int	(*zbx_ns_answer_error_func_t)(zbx_ns_answer_error_t);
#define ZBX_DEFINE_NS_ANSWER_ERROR_TO(__interface)					\
static int	zbx_ns_answer_error_to_ ## __interface (zbx_ns_answer_error_t err)	\
{											\
	switch (err)									\
	{										\
		case ZBX_NS_ANSWER_INTERNAL:						\
			return ZBX_EC_ ## __interface ## _INTERNAL_GENERAL;		\
		case ZBX_NS_ANSWER_ERROR_NOAAFLAG:					\
			return ZBX_EC_ ## __interface ## _NOAAFLAG;			\
		case ZBX_NS_ANSWER_ERROR_NODOMAIN:					\
			return ZBX_EC_ ## __interface ## _NODOMAIN;			\
		default:								\
			THIS_SHOULD_NEVER_HAPPEN;					\
			return ZBX_EC_ ## __interface ## _INTERNAL_GENERAL;		\
	}										\
}

ZBX_DEFINE_NS_ANSWER_ERROR_TO(DNS_UDP)
ZBX_DEFINE_NS_ANSWER_ERROR_TO(DNS_TCP)

#undef ZBX_DEFINE_NS_ANSWER_ERROR_TO

/* definitions of RCODE 16-23 are missing from ldns library */
/* https://open.nlnetlabs.nl/pipermail/ldns-users/2018-March/000912.html */

typedef int	(*zbx_rcode_not_nxdomain_func_t)(ldns_pkt_rcode);
#define ZBX_DEFINE_ZBX_RCODE_NOT_NXDOMAIN_TO(__interface)			\
static int	zbx_rcode_not_nxdomain_to_ ## __interface (ldns_pkt_rcode rcode)\
{										\
	switch (rcode)								\
	{									\
		case LDNS_RCODE_FORMERR:					\
			return ZBX_EC_ ## __interface ## _RCODE_FORMERR;	\
		case LDNS_RCODE_SERVFAIL:					\
			return ZBX_EC_ ## __interface ## _RCODE_SERVFAIL;	\
		case LDNS_RCODE_NOTIMPL:					\
			return ZBX_EC_ ## __interface ## _RCODE_NOTIMP;		\
		case LDNS_RCODE_REFUSED:					\
			return ZBX_EC_ ## __interface ## _RCODE_REFUSED;	\
		case LDNS_RCODE_YXDOMAIN:					\
			return ZBX_EC_ ## __interface ## _RCODE_YXDOMAIN;	\
		case LDNS_RCODE_YXRRSET:					\
			return ZBX_EC_ ## __interface ## _RCODE_YXRRSET;	\
		case LDNS_RCODE_NXRRSET:					\
			return ZBX_EC_ ## __interface ## _RCODE_NXRRSET;	\
		case LDNS_RCODE_NOTAUTH:					\
			return ZBX_EC_ ## __interface ## _RCODE_NOTAUTH;	\
		case LDNS_RCODE_NOTZONE:					\
			return ZBX_EC_ ## __interface ## _RCODE_NOTZONE;	\
		default:							\
			return ZBX_EC_ ## __interface ## _RCODE_CATCHALL;	\
	}									\
}

ZBX_DEFINE_ZBX_RCODE_NOT_NXDOMAIN_TO(DNS_UDP)
ZBX_DEFINE_ZBX_RCODE_NOT_NXDOMAIN_TO(DNS_TCP)

#undef ZBX_DEFINE_ZBX_RCODE_NOT_NXDOMAIN_TO

typedef struct
{
	zbx_dnskeys_error_func_t	dnskeys_error;
	zbx_ns_answer_error_func_t	ns_answer_error;
	zbx_dnssec_error_func_t		dnssec_error;
	zbx_rr_class_error_func_t	rr_class_error;
	zbx_ns_query_error_func_t	ns_query_error;
	zbx_rcode_not_nxdomain_func_t	rcode_not_nxdomain;
}
zbx_error_functions_t;

const zbx_error_functions_t DNS[] = {
	{
		zbx_dnskeys_error_to_DNS_UDP,
		zbx_ns_answer_error_to_DNS_UDP,
		zbx_dnssec_error_to_DNS_UDP,
		zbx_rr_class_error_to_DNS_UDP,
		zbx_ns_query_error_to_DNS_UDP,
		zbx_rcode_not_nxdomain_to_DNS_UDP
	},
	{
		zbx_dnskeys_error_to_DNS_TCP,
		zbx_ns_answer_error_to_DNS_TCP,
		zbx_dnssec_error_to_DNS_TCP,
		zbx_rr_class_error_to_DNS_TCP,
		zbx_ns_query_error_to_DNS_TCP,
		zbx_rcode_not_nxdomain_to_DNS_TCP
	}
};

#define DNS_PROTO(RES)	ldns_resolver_usevc(RES) ? RSM_TCP : RSM_UDP

static int	zbx_verify_rrsigs(const ldns_pkt *pkt, ldns_rr_type covered_type, const ldns_rr_list *keys,
		const char *ns, const char *ip, zbx_dnssec_error_t *dnssec_ec, char *err, size_t err_size)
{
	zbx_vector_ptr_t	owners;
	ldns_rr_list		*rrset = NULL, *rrsigs = NULL;
	ldns_status		status;
	char			*owner_str, owner_buf[256];
	int			i, ret = FAIL;

	zbx_vector_ptr_create(&owners);

	/* get all RRSIGs just to collect the owners */
	if (SUCCEED != zbx_get_covered_rrsigs(pkt, NULL, LDNS_SECTION_AUTHORITY, covered_type, &rrsigs,
			dnssec_ec, err, err_size))
	{
		goto out;
	}

	zbx_get_owners(rrsigs, &owners);

	if (0 == owners.values_num)
	{
		zbx_snprintf(err, err_size, "no RRSIG records covering %s found at nameserver \"%s\" (%s)",
				zbx_covered_to_str(covered_type), ns, ip);
		*dnssec_ec = ZBX_EC_DNSSEC_RRSIG_NOTCOVERED;
		goto out;
	}

	for (i = 0; i < owners.values_num; i++)
	{
		ldns_rdf	*owner_rdf = (ldns_rdf *)owners.values[i];

		if (NULL == (owner_str = ldns_rdf2str(owner_rdf)))
		{
			zbx_strlcpy(err, UNEXPECTED_LDNS_MEM_ERROR, err_size);
			*dnssec_ec = ZBX_EC_DNSSEC_INTERNAL;
			goto out;
		}

		zbx_strlcpy(owner_buf, owner_str, sizeof(owner_buf));
		zbx_free(owner_str);

		if (NULL != rrset)
		{
			ldns_rr_list_deep_free(rrset);
			rrset = NULL;
		}

		/* collect RRs to verify by owner */
		if (NULL == (rrset = ldns_pkt_rr_list_by_name_and_type(pkt, owner_rdf, covered_type,
				LDNS_SECTION_AUTHORITY)))
		{
			zbx_snprintf(err, err_size, "no %s records covering RRSIG of \"%s\""
					" found at nameserver \"%s\" (%s)",
					zbx_covered_to_str(covered_type), owner_buf, ns, ip);
			*dnssec_ec = ZBX_EC_DNSSEC_RRSIG_NOTCOVERED;
			goto out;
		}

		if (NULL != rrsigs)
		{
			ldns_rr_list_deep_free(rrsigs);
			rrsigs = NULL;
		}

		/* now get RRSIGs of that owner, we know at least one exists */
		if (SUCCEED != zbx_get_covered_rrsigs(pkt, owner_rdf, LDNS_SECTION_AUTHORITY, covered_type, &rrsigs,
				dnssec_ec, err, err_size))
		{
			goto out;
		}

		/* verify RRSIGs */
		if (LDNS_STATUS_OK != (status = ldns_verify(rrset, rrsigs, keys, NULL)))
		{
			const char *error_description;

			/* TODO: these mappings should be checked, some of them */
			/* are never returned by ldns_verify as to ldns 1.7.0   */

			switch (status)
			{
				case LDNS_STATUS_CRYPTO_UNKNOWN_ALGO:
					*dnssec_ec = ZBX_EC_DNSSEC_ALGO_UNKNOWN;
					error_description = "unknown cryptographic algorithm";
					break;
				case LDNS_STATUS_CRYPTO_ALGO_NOT_IMPL:
					*dnssec_ec = ZBX_EC_DNSSEC_ALGO_NOT_IMPL;
					error_description = "cryptographic algorithm not implemented";
					break;
				case LDNS_STATUS_CRYPTO_NO_MATCHING_KEYTAG_DNSKEY:
					*dnssec_ec = ZBX_EC_DNSSEC_RRSIG_NOT_SIGNED;
					error_description = "the RRSIG found is not signed by a DNSKEY";
					break;
				case LDNS_STATUS_CRYPTO_BOGUS:
					*dnssec_ec = ZBX_EC_DNSSEC_SIG_BOGUS;
					error_description = "bogus DNSSEC signature";
					break;
				case LDNS_STATUS_CRYPTO_SIG_EXPIRED:
					*dnssec_ec = ZBX_EC_DNSSEC_SIG_EXPIRED;
					error_description = "DNSSEC signature has expired";
					break;
				case LDNS_STATUS_CRYPTO_SIG_NOT_INCEPTED:
					*dnssec_ec = ZBX_EC_DNSSEC_SIG_NOT_INCEPTED;
					error_description = "DNSSEC signature not incepted yet";
					break;
				case LDNS_STATUS_CRYPTO_EXPIRATION_BEFORE_INCEPTION:
					*dnssec_ec = ZBX_EC_DNSSEC_SIG_EX_BEFORE_IN;
					error_description = "DNSSEC signature has expiration date earlier than inception date";
					break;
				case LDNS_STATUS_NSEC3_ERR:				/* TODO: candidate for removal */
					*dnssec_ec = ZBX_EC_DNSSEC_NSEC3_ERROR;
					error_description = "error in NSEC3 denial of existence";
					break;
				case LDNS_STATUS_DNSSEC_NSEC_RR_NOT_COVERED:		/* TODO: candidate for removal */
					*dnssec_ec = ZBX_EC_DNSSEC_RR_NOTCOVERED;
					error_description = "RR not covered by the given NSEC RRs";
					break;
				case LDNS_STATUS_DNSSEC_NSEC_WILDCARD_NOT_COVERED:	/* TODO: candidate for removal */
					*dnssec_ec = ZBX_EC_DNSSEC_WILD_NOTCOVERED;
					error_description = "wildcard not covered by the given NSEC RRs";
					break;
				case LDNS_STATUS_MISSING_RDATA_FIELDS_RRSIG:
					*dnssec_ec = ZBX_EC_DNSSEC_RRSIG_MISS_RDATA;
					error_description = "RRSIG has too few RDATA fields";
					break;
				default:
					*dnssec_ec = ZBX_EC_DNSSEC_CATCHALL;
					error_description = "malformed DNSSEC response";
			}

			zbx_snprintf(err, err_size, "cannot verify %s RRSIGs of \"%s\": %s"
					" (used %u %s, %u RRSIG and %u DNSKEY RRs). LDNS returned \"%s\"",
					zbx_covered_to_str(covered_type),
					owner_buf,
					error_description,
					(unsigned int)ldns_rr_list_rr_count(rrset),
					zbx_covered_to_str(covered_type),
					(unsigned int)ldns_rr_list_rr_count(rrsigs),
					(unsigned int)ldns_rr_list_rr_count(keys),
					ldns_get_errorstr_by_id(status));

			goto out;
		}
	}

	ret = SUCCEED;
out:
	zbx_destroy_owners(&owners);

	if (NULL != rrset)
		ldns_rr_list_deep_free(rrset);

	if (NULL != rrsigs)
		ldns_rr_list_deep_free(rrsigs);

	return ret;
}

static int	zbx_pkt_section_has_rr_type(const ldns_pkt *pkt, ldns_rr_type t, ldns_pkt_section s)
{
	ldns_rr_list	*rrlist;

	if (NULL == (rrlist = ldns_pkt_rr_list_by_type(pkt, t, s)))
		return FAIL;

	ldns_rr_list_deep_free(rrlist);

	return SUCCEED;
}

static int	zbx_verify_denial_of_existence(const ldns_pkt *pkt, zbx_dnssec_error_t *dnssec_ec, char *err, size_t err_size)
{
	ldns_rr_list	*question = NULL, *rrsigs = NULL, *nsecs = NULL, *nsec3s = NULL;
	ldns_status	status;
	int		ret = FAIL;

	if (NULL == (question = ldns_pkt_rr_list_by_type(pkt, LDNS_RR_TYPE_A, LDNS_SECTION_QUESTION)))
	{
		zbx_snprintf(err, err_size, "cannot obtain query section");
		*dnssec_ec = ZBX_EC_DNSSEC_INTERNAL;
		goto out;
	}

	if (0 == ldns_rr_list_rr_count(question))
	{
		zbx_snprintf(err, err_size, "question section is empty");
		*dnssec_ec = ZBX_EC_DNSSEC_INTERNAL;
		goto out;
	}

	rrsigs = ldns_pkt_rr_list_by_type(pkt, LDNS_RR_TYPE_RRSIG, LDNS_SECTION_AUTHORITY);
	nsecs = ldns_pkt_rr_list_by_type(pkt, LDNS_RR_TYPE_NSEC, LDNS_SECTION_AUTHORITY);
	nsec3s = ldns_pkt_rr_list_by_type(pkt, LDNS_RR_TYPE_NSEC3, LDNS_SECTION_AUTHORITY);

	if (NULL != nsecs)
	{
		if (NULL == rrsigs)
		{
			zbx_snprintf(err, err_size, "missing rrsigs");
			*dnssec_ec = ZBX_EC_DNSSEC_RRSIG_NONE;
			goto out;
		}

		if (LDNS_RCODE_NXDOMAIN == ldns_pkt_get_rcode(pkt))
		{
			status = ldns_dnssec_verify_denial(ldns_rr_list_rr(question, 0), nsecs, rrsigs);
		}
		else
			status = LDNS_STATUS_OK;

		if (LDNS_STATUS_DNSSEC_NSEC_RR_NOT_COVERED == status)
		{
			zbx_snprintf(err, err_size, "RR not covered by the given NSEC RRs");
			*dnssec_ec = ZBX_EC_DNSSEC_RR_NOTCOVERED;
			goto out;
		}
		else if (LDNS_STATUS_DNSSEC_NSEC_WILDCARD_NOT_COVERED == status)
		{
			zbx_snprintf(err, err_size, "wildcard not covered by the given NSEC RRs");
			*dnssec_ec = ZBX_EC_DNSSEC_WILD_NOTCOVERED;
			goto out;
		}
		else if (LDNS_STATUS_OK != status)
		{
			zbx_snprintf(err, err_size, UNEXPECTED_LDNS_ERROR " \"%s\"", ldns_get_errorstr_by_id(status));
			*dnssec_ec = ZBX_EC_DNSSEC_INTERNAL;
			goto out;
		}
	}

	if (NULL != nsec3s)
	{
		if (NULL == rrsigs)
		{
			zbx_snprintf(err, err_size, "missing rrsigs");
			*dnssec_ec = ZBX_EC_DNSSEC_RRSIG_NONE;
			goto out;
		}

		if (LDNS_RCODE_NXDOMAIN == ldns_pkt_get_rcode(pkt))
		{
			status = ldns_dnssec_verify_denial_nsec3(ldns_rr_list_rr(question, 0), nsec3s, rrsigs,
					ldns_pkt_get_rcode(pkt), LDNS_RR_TYPE_A, 1);
		}
		else
			status = LDNS_STATUS_OK;

		if (LDNS_STATUS_DNSSEC_NSEC_RR_NOT_COVERED == status)
		{
			zbx_snprintf(err, err_size, "RR not covered by the given NSEC RRs");
			*dnssec_ec = ZBX_EC_DNSSEC_RR_NOTCOVERED;
			goto out;
		}
		else if (LDNS_STATUS_DNSSEC_NSEC_WILDCARD_NOT_COVERED == status)
		{
			zbx_snprintf(err, err_size, "wildcard not covered by the given NSEC RRs");
			*dnssec_ec = ZBX_EC_DNSSEC_WILD_NOTCOVERED;
			goto out;
		}
		else if (LDNS_STATUS_NSEC3_ERR == status)
		{
			zbx_snprintf(err, err_size, "error in NSEC3 denial of existence proof");
			*dnssec_ec = ZBX_EC_DNSSEC_NSEC3_ERROR;
			goto out;
		}
		else if (LDNS_STATUS_OK != status)
		{
			zbx_snprintf(err, err_size, UNEXPECTED_LDNS_ERROR " \"%s\"", ldns_get_errorstr_by_id(status));
			*dnssec_ec = ZBX_EC_DNSSEC_INTERNAL;
			goto out;
		}
	}

	ret = SUCCEED;
out:
	if (NULL != question)
		ldns_rr_list_deep_free(question);

	if (NULL != rrsigs)
		ldns_rr_list_deep_free(rrsigs);

	if (NULL != nsecs)
		ldns_rr_list_deep_free(nsecs);

	if (NULL != nsec3s)
		ldns_rr_list_deep_free(nsec3s);

	return ret;
}

static void extract_nsid(ldns_rdf *edns_data, char **nsid)
{
	uint8_t	*rdf_data;
	size_t	rdf_size;

	if (NULL == edns_data)
		return;

	rdf_data = ldns_rdf_data(edns_data);
	rdf_size = ldns_rdf_size(edns_data);

	while (4 < rdf_size)	/* 2 bytes for option code, 2 bytes for option length */
	{
		uint16_t	opt_code;
		uint16_t	opt_len;

		opt_code = ldns_read_uint16(rdf_data);
		rdf_size -= sizeof(opt_code);
		rdf_data += sizeof(opt_code);

		opt_len = ldns_read_uint16(rdf_data);
		rdf_size -= sizeof(opt_len);
		rdf_data += sizeof(opt_len);

		if (LDNS_EDNS_NSID == opt_code)
		{
			const char	*hex = "0123456789abcdef";
			uint16_t	i;

			if (NSID_MAX_LENGTH < opt_len)
				opt_len = NSID_MAX_LENGTH;

			*nsid = (char *)zbx_malloc(*nsid, (size_t)(opt_len * 2 + 1));

			for (i = 0; i < opt_len; i++)
			{
				(*nsid)[i * 2 + 0] = hex[rdf_data[i] >> 4];
				(*nsid)[i * 2 + 1] = hex[rdf_data[i] & 15];
			}

			(*nsid)[opt_len * 2] = '\0';
			break;
		}

		rdf_size = opt_len > rdf_size ? 0 : rdf_size - opt_len;
		rdf_data += opt_len;
	}
}

static int	zbx_dns_in_a_query(ldns_pkt **pkt, ldns_resolver *res, const ldns_rdf *testname_rdf, char **nsid,
		zbx_ns_query_error_t *ec, char *err, size_t err_size)
{
	ldns_status	status;
	double		sec = -1;
	ldns_pkt	*query = NULL;
	ldns_rdf	*send_nsid;
	ldns_buffer	*opt_buf;
	int		ret = FAIL;

	opt_buf = ldns_buffer_new(4);	/* size of option code and option size */

	if (NULL == opt_buf)
	{
		zbx_snprintf(err, err_size, "memory error in ldns_buffer_new()");
		*ec = ZBX_NS_QUERY_INTERNAL;
		goto out;
	}

	ldns_buffer_write_u16(opt_buf, LDNS_EDNS_NSID);	/* option code */
	ldns_buffer_write_u16(opt_buf, 0);		/* option size */

	send_nsid = ldns_rdf_new_frm_data(LDNS_RDF_TYPE_NONE, ldns_buffer_position(opt_buf),
			ldns_buffer_begin(opt_buf));

	if (NULL == send_nsid)
	{
		zbx_snprintf(err, err_size, "memory error in ldns_rdf_new_frm_data()");
		*ec = ZBX_NS_QUERY_INTERNAL;
		goto out;
	}

	status = ldns_resolver_prepare_query_pkt(&query, res, testname_rdf, LDNS_RR_TYPE_A, LDNS_RR_CLASS_IN, 0);

	if (LDNS_STATUS_OK != status)
	{
		zbx_snprintf(err, err_size, "cannot create query packet: %s", ldns_get_errorstr_by_id(status));
		goto err;
	}

	ldns_pkt_set_edns_data(query, send_nsid);

	sec = zbx_time();

	status = ldns_resolver_send_pkt(pkt, res, query);

	if (LDNS_STATUS_OK != status)
	{
		zbx_snprintf(err, err_size, "cannot send query: %s", ldns_get_errorstr_by_id(status));
		goto err;
	}

	extract_nsid(ldns_pkt_edns_data(*pkt), nsid);

	ret = SUCCEED;

	goto out;
err:
	switch (status)
	{
		case LDNS_STATUS_ERR:
		case LDNS_STATUS_NETWORK_ERR:
			/* UDP */
			if (!ldns_resolver_usevc(res))
			{
				*ec = ZBX_NS_QUERY_NOREPLY;
			}
			/* TCP */
			else
			{
				struct timeval	tv;
				uint8_t		retry;

				tv = ldns_resolver_timeout(res);
				retry = ldns_resolver_retry(res);

				if (0 <= sec && zbx_time() - sec >= tv.tv_sec * retry)
				{
					*ec = ZBX_NS_QUERY_TO;
				}
				else
				{
					*ec = ZBX_NS_QUERY_ECON;
				}
			}

			break;
		case LDNS_STATUS_WIRE_INCOMPLETE_HEADER:
			*ec = ZBX_NS_QUERY_INC_HEADER;
			break;
		case LDNS_STATUS_WIRE_INCOMPLETE_QUESTION:
			*ec = ZBX_NS_QUERY_INC_QUESTION;
			break;
		case LDNS_STATUS_WIRE_INCOMPLETE_ANSWER:
			*ec = ZBX_NS_QUERY_INC_ANSWER;
			break;
		case LDNS_STATUS_WIRE_INCOMPLETE_AUTHORITY:
			*ec = ZBX_NS_QUERY_INC_AUTHORITY;
			break;
		case LDNS_STATUS_WIRE_INCOMPLETE_ADDITIONAL:
			*ec = ZBX_NS_QUERY_INC_ADDITIONAL;
			break;
		default:
			*ec = ZBX_NS_QUERY_CATCHALL;
	}
out:
	if (NULL != opt_buf)
		ldns_buffer_free(opt_buf);

	if (NULL != query)
		ldns_pkt_free(query);

	return ret;
}

/* Check every RR in rr_set, return  */
/* SUCCEED - all have expected class */
/* FAIL    - otherwise               */
static int	zbx_verify_rr_class(const ldns_rr_list *rr_list, zbx_rr_class_error_t *ec, char *err, size_t err_size)
{
	size_t	i, rr_count;

	rr_count = ldns_rr_list_rr_count(rr_list);

	for (i = 0; i < rr_count; i++)
	{
		ldns_rr		*rr;
		ldns_rr_class	rr_class;

		if (NULL == (rr = ldns_rr_list_rr(rr_list, i)))
		{
			zbx_strlcpy(err, UNEXPECTED_LDNS_MEM_ERROR, err_size);
			*ec = ZBX_EC_RR_CLASS_INTERNAL;
			return FAIL;
		}

		if (LDNS_RR_CLASS_IN != (rr_class = ldns_rr_get_class(rr)))
		{
			char	*class_str;

			class_str = ldns_rr_class2str(rr_class);

			zbx_snprintf(err, err_size, "unexpected RR class, expected IN got %s", class_str);

			zbx_free(class_str);

			switch (rr_class)
			{
				case LDNS_RR_CLASS_CH:
					*ec = ZBX_EC_RR_CLASS_CHAOS;
					break;
				case LDNS_RR_CLASS_HS:
					*ec = ZBX_EC_RR_CLASS_HESIOD;
					break;
				default:
					*ec = ZBX_EC_RR_CLASS_CATCHALL;
					break;
			}

			return FAIL;
		}
	}

	return SUCCEED;
}

static int	zbx_domain_in_question_section(const ldns_pkt *pkt, const char *domain, zbx_ns_answer_error_t *ec,
		char *err, size_t err_size)
{
	ldns_rr_list	*rr_list = NULL;
	const ldns_rdf	*owner_rdf;
	char		*owner = NULL;
	int		ret = FAIL;

	if (NULL == (rr_list = ldns_pkt_rr_list_by_type(pkt, LDNS_RR_TYPE_A, LDNS_SECTION_QUESTION)))
	{
		zbx_strlcpy(err, "no A record in QUESTION section", err_size);
		*ec = ZBX_NS_ANSWER_ERROR_NODOMAIN;
		goto out;
	}

	if (NULL == (owner_rdf = ldns_rr_list_owner(rr_list)))
	{
		zbx_strlcpy(err, "no A RR owner in QUESTION section", err_size);
		*ec = ZBX_NS_ANSWER_ERROR_NODOMAIN;
		goto out;
	}

	if (NULL == (owner = ldns_rdf2str(owner_rdf)))
	{
		zbx_strlcpy(err, UNEXPECTED_LDNS_MEM_ERROR, err_size);
		*ec = ZBX_NS_ANSWER_INTERNAL;
		goto out;
	}

	if (0 != strcasecmp(domain, owner))
	{
		zbx_snprintf(err, err_size, "A RR owner \"%s\" does not match expected \"%s\"", owner, domain);
		*ec = ZBX_NS_ANSWER_ERROR_NODOMAIN;
		goto out;
	}

	ret = SUCCEED;
out:
	zbx_free(owner);

	if (NULL != rr_list)
		ldns_rr_list_deep_free(rr_list);

	return ret;
}

static int	zbx_check_dnssec_no_epp(const ldns_pkt *pkt, const ldns_rr_list *keys, const char *ns, const char *ip,
		zbx_dnssec_error_t *dnssec_ec, char *err, size_t err_size)
{
	int	ret = SUCCEED, auth_has_nsec = 0, auth_has_nsec3 = 0;

	if (SUCCEED != zbx_pkt_section_has_rr_type(pkt, LDNS_RR_TYPE_RRSIG, LDNS_SECTION_ANSWER)
			&&  SUCCEED != zbx_pkt_section_has_rr_type(pkt, LDNS_RR_TYPE_RRSIG, LDNS_SECTION_AUTHORITY)
			&&  SUCCEED != zbx_pkt_section_has_rr_type(pkt, LDNS_RR_TYPE_RRSIG, LDNS_SECTION_ADDITIONAL))
	{
		zbx_strlcpy(err, "no RRSIGs where found in any section", err_size);
		*dnssec_ec = ZBX_EC_DNSSEC_RRSIG_NONE;
		return FAIL;
	}

	if (SUCCEED == zbx_pkt_section_has_rr_type(pkt, LDNS_RR_TYPE_NSEC, LDNS_SECTION_AUTHORITY))
		auth_has_nsec = 1;

	if (SUCCEED == zbx_pkt_section_has_rr_type(pkt, LDNS_RR_TYPE_NSEC3, LDNS_SECTION_AUTHORITY))
		auth_has_nsec3 = 1;

	if (0 == auth_has_nsec && 0 == auth_has_nsec3)
	{
		zbx_strlcpy(err, "no NSEC/NSEC3 RRs were found in the authority section", err_size);
		*dnssec_ec = ZBX_EC_DNSSEC_NO_NSEC_IN_AUTH;
		return FAIL;
	}

	if (1 == auth_has_nsec)
		ret = zbx_verify_rrsigs(pkt, LDNS_RR_TYPE_NSEC, keys, ns, ip, dnssec_ec, err, err_size);

	if (SUCCEED == ret && 1 == auth_has_nsec3)
		ret = zbx_verify_rrsigs(pkt, LDNS_RR_TYPE_NSEC3, keys, ns, ip, dnssec_ec, err, err_size);

	if (SUCCEED == ret || ZBX_EC_DNSSEC_RRSIG_NOT_SIGNED == *dnssec_ec)
	{
		char			err2[ZBX_ERR_BUF_SIZE];
		zbx_dnssec_error_t	dnssec_ec2;

		/* we do not set ret here because we already failed in one of previous function calls */
		if (SUCCEED != zbx_verify_denial_of_existence(pkt, &dnssec_ec2, err2, sizeof(err2)))
		{
			zbx_strlcpy(err, err2, err_size);
			*dnssec_ec = dnssec_ec2;
			ret = FAIL;
		}
	}

	return ret;
}

static int	zbx_get_ns_ip_values(ldns_resolver *res, const char *ns, const char *ip, uint16_t port,
		const ldns_rr_list *keys, const char *testedname, FILE *log_fd, int *rtt, char **nsid, int *upd,
		int ipv4_enabled, int ipv6_enabled, int epp_enabled, char *err, size_t err_size)
{
	char			*host, *last_label = NULL;
	ldns_rdf		*testedname_rdf = NULL, *last_label_rdf = NULL;
	ldns_pkt		*pkt = NULL;
	ldns_rr_list		*nsset = NULL, *all_rr_list = NULL;
	ldns_rr			*rr;
	time_t			now, ts;
	ldns_pkt_rcode		rcode;
	zbx_ns_query_error_t	query_ec;
	zbx_ns_answer_error_t	answer_ec;
	zbx_dnssec_error_t	dnssec_ec;
	zbx_rr_class_error_t	rr_class_ec;
	int			ret = FAIL;

	/* change the resolver */
	if (SUCCEED != zbx_change_resolver(res, ns, ip, port, ipv4_enabled, ipv6_enabled, log_fd, err, err_size))
	{
		*rtt = DNS[DNS_PROTO(res)].ns_query_error(ZBX_NS_QUERY_INTERNAL);
		goto out;
	}

	if (NULL == (testedname_rdf = ldns_rdf_new_frm_str(LDNS_RDF_TYPE_DNAME, testedname)))
	{
		zbx_strlcpy(err, UNEXPECTED_LDNS_MEM_ERROR, err_size);
		*rtt = DNS[DNS_PROTO(res)].ns_query_error(ZBX_NS_QUERY_INTERNAL);
		goto out;
	}

	/* IN A query */
	if (SUCCEED != zbx_dns_in_a_query(&pkt, res, testedname_rdf, nsid, &query_ec, err, err_size))
	{
		*rtt = DNS[DNS_PROTO(res)].ns_query_error(query_ec);
		goto out;
	}

	ldns_pkt_print(log_fd, pkt);

	all_rr_list = ldns_pkt_all(pkt);

	if (SUCCEED != zbx_verify_rr_class(all_rr_list, &rr_class_ec, err, err_size))
	{
		*rtt = DNS[DNS_PROTO(res)].rr_class_error(rr_class_ec);
		goto out;
	}

	/* verify RCODE */
	if (LDNS_RCODE_NOERROR != (rcode = ldns_pkt_get_rcode(pkt)) && LDNS_RCODE_NXDOMAIN != rcode)
	{
		char	*rcode_str;

		rcode_str = ldns_pkt_rcode2str(rcode);
		zbx_snprintf(err, err_size, "expected NXDOMAIN got %s", rcode_str);
		zbx_free(rcode_str);

		*rtt = DNS[DNS_PROTO(res)].rcode_not_nxdomain(rcode);
		goto out;
	}

	if (0 == ldns_pkt_aa(pkt))
	{
		zbx_strlcpy(err, "AA flag is not set in the answer from nameserver", err_size);
		*rtt = DNS[DNS_PROTO(res)].ns_answer_error(ZBX_NS_ANSWER_ERROR_NOAAFLAG);
		goto out;
	}

	if (SUCCEED != zbx_domain_in_question_section(pkt, testedname, &answer_ec, err, err_size))
	{
		*rtt = DNS[DNS_PROTO(res)].ns_answer_error(answer_ec);
		goto out;
	}

	if (0 != epp_enabled)
	{
		/* start referral validation */

		/* the AUTHORITY section should contain at least one NS RR for the last label in  */
		/* PREFIX, e.g. "somedomain" when querying for "blahblah.somedomain.example." */
		if (SUCCEED != zbx_get_last_label(testedname, &last_label, err, err_size))
		{
			*rtt = ZBX_EC_EPP_NOT_IMPLEMENTED;
			goto out;
		}

		if (NULL == (last_label_rdf = ldns_rdf_new_frm_str(LDNS_RDF_TYPE_DNAME, last_label)))
		{
			zbx_strlcpy(err, UNEXPECTED_LDNS_MEM_ERROR, err_size);
			*rtt = ZBX_EC_EPP_NOT_IMPLEMENTED;
			goto out;
		}

		if (NULL == (nsset = ldns_pkt_rr_list_by_name_and_type(pkt, last_label_rdf, LDNS_RR_TYPE_NS,
				LDNS_SECTION_AUTHORITY)))
		{
			zbx_snprintf(err, err_size, "no NS records of \"%s\" at nameserver \"%s\" (%s)", last_label,
					ns, ip);
			*rtt = ZBX_EC_EPP_NOT_IMPLEMENTED;
			goto out;
		}

		/* end referral validation */

		if (NULL != upd)
		{
			/* extract UNIX timestamp of random NS record */

			rr = ldns_rr_list_rr(nsset, zbx_random(ldns_rr_list_rr_count(nsset)));
			host = ldns_rdf2str(ldns_rr_rdf(rr, 0));

			rsm_infof(log_fd, "randomly chose ns %s", host);
			if (SUCCEED != zbx_get_ts_from_host(host, &ts))
			{
				zbx_snprintf(err, err_size, "cannot extract Unix timestamp from %s", host);
				zbx_free(host);
				*upd = ZBX_EC_EPP_NOT_IMPLEMENTED;
				goto out;
			}

			now = time(NULL);

			if (0 > now - ts)
			{
				zbx_snprintf(err, err_size,
						"Unix timestamp of %s is in the future (current: " ZBX_FS_UI64 ")",
						host, now);
				zbx_free(host);
				*upd = ZBX_EC_EPP_NOT_IMPLEMENTED;
				goto out;
			}

			zbx_free(host);

			/* successful update time */
			*upd = (int)(now - ts);
		}

		if (NULL != keys)	/* EPP enabled, DNSSEC enabled */
		{
			if (SUCCEED != zbx_verify_rrsigs(pkt, LDNS_RR_TYPE_DS, keys, ns, ip, &dnssec_ec,
					err, err_size))
			{
				*rtt = DNS[DNS_PROTO(res)].dnssec_error(dnssec_ec);
				goto out;
			}
		}
	}
	else if (NULL != keys)		/* EPP disabled, DNSSEC enabled */
	{
		if (SUCCEED != zbx_check_dnssec_no_epp(pkt, keys, ns, ip, &dnssec_ec, err, err_size))
		{
			*rtt = DNS[DNS_PROTO(res)].dnssec_error(dnssec_ec);
			goto out;
		}
	}

	/* successful rtt */
	*rtt = (int)ldns_pkt_querytime(pkt);

	/* no errors */
	ret = SUCCEED;
out:
	if (NULL != upd)
		rsm_infof(log_fd, "RSM DNS \"%s\" (%s) RTT:%d UPD:%d NSID:%s", ns, ip, *rtt, *upd, ZBX_NULL2STR(*nsid));
	else
		rsm_infof(log_fd, "RSM DNS \"%s\" (%s) RTT:%d NSID:%s", ns, ip, *rtt, ZBX_NULL2STR(*nsid));

	if (NULL != nsset)
		ldns_rr_list_deep_free(nsset);

	if (NULL != all_rr_list)
		ldns_rr_list_deep_free(all_rr_list);

	if (NULL != pkt)
		ldns_pkt_free(pkt);

	if (NULL != testedname_rdf)
		ldns_rdf_deep_free(testedname_rdf);

	if (NULL != last_label_rdf)
		ldns_rdf_deep_free(last_label_rdf);

	if (NULL != last_label)
		zbx_free(last_label);

	return ret;
}

static int	zbx_get_dnskeys(ldns_resolver *res, const char *domain, const char *resolver,
		ldns_rr_list **keys, FILE *pkt_file, zbx_dnskeys_error_t *ec, char *err, size_t err_size)
{
	ldns_pkt	*pkt = NULL;
	ldns_rdf	*domain_rdf = NULL;
	ldns_status	status;
	ldns_pkt_rcode	rcode;
	int		ret = FAIL;

	if (NULL == (domain_rdf = ldns_rdf_new_frm_str(LDNS_RDF_TYPE_DNAME, domain)))
	{
		zbx_strlcpy(err, UNEXPECTED_LDNS_MEM_ERROR, err_size);
		*ec = ZBX_DNSKEYS_INTERNAL;
		goto out;
	}

	/* query domain records */
	status = ldns_resolver_query_status(&pkt, res, domain_rdf, LDNS_RR_TYPE_DNSKEY, LDNS_RR_CLASS_IN,
			LDNS_RD | LDNS_AD);

	if (LDNS_STATUS_OK != status)
	{
		zbx_snprintf(err, err_size, "cannot connect to resolver \"%s\": %s", resolver,
				ldns_get_errorstr_by_id(status));
		*ec = ZBX_DNSKEYS_NOREPLY;
		goto out;
	}

	/* log the packet */
	ldns_pkt_print(pkt_file, pkt);

	/* check the AD bit */
	if (0 == ldns_pkt_ad(pkt))
	{
		zbx_snprintf(err, err_size, "AD bit not present in the answer of \"%s\" from resolver \"%s\"",
				domain, resolver);
		*ec = ZBX_DNSKEYS_NOADBIT;
		goto out;
	}

	if (LDNS_RCODE_NOERROR != (rcode = ldns_pkt_get_rcode(pkt)))
	{
		char    *rcode_str;

		rcode_str = ldns_pkt_rcode2str(rcode);
		zbx_snprintf(err, err_size, "expected NOERROR got %s", rcode_str);
		zbx_free(rcode_str);

		switch (rcode)
		{
			case LDNS_RCODE_NXDOMAIN:
				*ec = ZBX_DNSKEYS_NXDOMAIN;
				break;
			default:
				*ec = ZBX_DNSKEYS_CATCHALL;
		}

		goto out;
	}

	/* get the DNSKEY records */
	if (NULL == (*keys = ldns_pkt_rr_list_by_name_and_type(pkt, domain_rdf, LDNS_RR_TYPE_DNSKEY,
			LDNS_SECTION_ANSWER)))
	{
		zbx_snprintf(err, err_size, "no DNSKEY records of domain \"%s\" from resolver \"%s\"", domain,
				resolver);
		*ec = ZBX_DNSKEYS_NONE;
		goto out;
	}

	ret = SUCCEED;
out:
	if (NULL != domain_rdf)
		ldns_rdf_deep_free(domain_rdf);

	if (NULL != pkt)
		ldns_pkt_free(pkt);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_get_nameservers                                              *
 *                                                                            *
 * Purpose: Parse string "<NS>,<IP> ..." and return list of Name Servers with *
 *          their IPs in zbx_ns_t structure.                                  *
 *                                                                            *
 ******************************************************************************/
static int	zbx_get_nameservers(char *name_servers_list, zbx_ns_t **nss, size_t *nss_num, int ipv4_enabled,
		int ipv6_enabled, FILE *log_fd, char *err, size_t err_size)
{
	char		*ns, *ip, *ns_next;
	size_t		i, j, nss_alloc = 0;
	zbx_ns_t	*ns_entry;

	*nss_num = 0;
	ns = name_servers_list;

	while (NULL != ns)
	{
		if (NULL != (ns_next = strchr(ns, ' ')))
		{
			*ns_next = '\0';
			ns_next++;
		}

		if (NULL == (ip = strchr(ns, ',')))
		{
			zbx_snprintf(err, err_size, "invalid entry \"%s\" in the list of name servers"
					", expected \"<NS>,<IP>\"",
					ns);
			return FAIL;
		}

		*ip = '\0';
		ip++;

		if (SUCCEED != zbx_validate_ip(ip, ipv4_enabled, ipv6_enabled, NULL, NULL))
		{
			rsm_warnf(log_fd, "unsupported IP address \"%s\" in the list of name servers, ignored", ip);
			goto next_ns;
		}

		ns_entry = NULL;

		/* find NS */
		for (i = 0; i < *nss_num; i++)
		{
			if (0 != strcmp(((*nss)[i]).name, ns))
			{
				continue;
			}

			ns_entry = &(*nss)[i];

			for (j = 0; j < ns_entry->ips_num; j++)
			{
				if (0 == strcmp(ns_entry->ips[j].ip, ip))
				{
					goto next_ns;
				}
			}

			break;
		}

		/* add NS */
		if (NULL == ns_entry)
		{
			if (0 == *nss_num)
			{
				nss_alloc = 8;
				*nss = (zbx_ns_t *)zbx_malloc(*nss, nss_alloc * sizeof(zbx_ns_t));
			}
			else if (nss_alloc == *nss_num)
			{
				nss_alloc += 8;
				*nss = (zbx_ns_t *)zbx_realloc(*nss, nss_alloc * sizeof(zbx_ns_t));
			}

			ns_entry = &(*nss)[*nss_num];

			ns_entry->name = zbx_strdup(NULL, ns);
			ns_entry->result = SUCCEED;	/* by default Name Server is considered working */
			ns_entry->ips_num = 0;

			(*nss_num)++;
		}

		/* add IP */
		if (0 == ns_entry->ips_num)
		{
			ns_entry->ips = (zbx_ns_ip_t *)zbx_malloc(NULL, sizeof(zbx_ns_ip_t));
		}
		else
		{
			ns_entry->ips = (zbx_ns_ip_t *)zbx_realloc(ns_entry->ips, (ns_entry->ips_num + 1) * sizeof(zbx_ns_ip_t));
		}

		ns_entry->ips[ns_entry->ips_num].ip = zbx_strdup(NULL, ip);
		ns_entry->ips[ns_entry->ips_num].upd = ZBX_NO_VALUE;
		ns_entry->ips[ns_entry->ips_num].nsid = NULL;

		ns_entry->ips_num++;
next_ns:
		ns = ns_next;
	}

	return SUCCEED;
}

static void	zbx_clean_nss(zbx_ns_t *nss, size_t nss_num)
{
	size_t	i, j;

	for (i = 0; i < nss_num; i++)
	{
		if (0 != nss[i].ips_num)
		{
			for (j = 0; j < nss[i].ips_num; j++)
			{
				zbx_free(nss[i].ips[j].ip);
				zbx_free(nss[i].ips[j].nsid);
			}

			zbx_free(nss[i].ips);
		}

		zbx_free(nss[i].name);
	}
}

static zbx_subtest_result_t	zbx_subtest_result(int rtt, int rtt_limit)
{
	if (ZBX_NO_VALUE == rtt)
		return ZBX_SUBTEST_SUCCESS;

	/* probe knock-down on -1 */
	if (ZBX_EC_DNS_UDP_INTERNAL_GENERAL == rtt)
		zbx_dc_rsm_errors_inc();

	if (rtt <= ZBX_EC_DNS_UDP_INTERNAL_GENERAL && ZBX_EC_INTERNAL_LAST <= rtt)
		return ZBX_SUBTEST_SUCCESS;

	return (0 > rtt || rtt > rtt_limit ? ZBX_SUBTEST_FAIL : ZBX_SUBTEST_SUCCESS);
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

static void	set_dns_test_results(zbx_ns_t *nss, size_t nss_num, int rtt_limit, unsigned int minns,
		unsigned int *nssok, unsigned int *test_status, unsigned int *dnssec_status, int dnssec_enabled,
		FILE *log_fd)
{
	unsigned int	dnssec_nssok = 0;
	size_t		i, j;

	*nssok = 0;

	for (i = 0; i < nss_num; i++)
	{
		int	ns_dnssec_status = SUCCEED;

		for (j = 0; j < nss[i].ips_num; j++)
		{
			/* if a single IP of the Name Server fails, consider the whole Name Server down */
			if (ZBX_SUBTEST_SUCCESS != zbx_subtest_result(nss[i].ips[j].rtt, rtt_limit))
				nss[i].result = FAIL;

			if (dnssec_enabled && (
					(ZBX_EC_DNS_UDP_DNSSEC_FIRST >= nss[i].ips[j].rtt &&
						nss[i].ips[j].rtt >= ZBX_EC_DNS_UDP_DNSSEC_LAST) ||
					(ZBX_EC_DNS_TCP_DNSSEC_FIRST >= nss[i].ips[j].rtt &&
						nss[i].ips[j].rtt >= ZBX_EC_DNS_TCP_DNSSEC_LAST)
			))
			{
				ns_dnssec_status = FAIL;	/* this name server received dnssec error */
			}
		}

		if (SUCCEED == nss[i].result)
			(*nssok)++;

		if (dnssec_enabled)
		{
			if (SUCCEED == ns_dnssec_status)
			{
				rsm_infof(log_fd, "%s: DNSSEC OK", nss[i].name);
				dnssec_nssok++;
			}
			else
				rsm_infof(log_fd, "%s: DNSSEC failed", nss[i].name);
		}
	}

	*test_status = (*nssok >= minns ? 1 : 0);

	if (dnssec_enabled)
		*dnssec_status = (dnssec_nssok >= minns ? 1 : 0);
}

static void	create_dns_json(struct zbx_json *json, zbx_ns_t *nss, size_t nss_num, unsigned int current_mode,
		unsigned int nssok, unsigned int test_status, unsigned int dnssec_status, char protocol,
		const char *testedname, int dnssec_enabled)
{
	size_t	i, j;

	zbx_json_init(json, 2 * ZBX_KIBIBYTE);

	zbx_json_addarray(json, "nsips");

	for (i = 0; i < nss_num; i++)
	{
		for (j = 0; j < nss[i].ips_num; j++)
		{
			zbx_json_addobject(json, NULL);
			zbx_json_addstring(json, "ns", nss[i].name, ZBX_JSON_TYPE_STRING);
			zbx_json_addstring(json, "ip", nss[i].ips[j].ip, ZBX_JSON_TYPE_STRING);
			zbx_json_addstring(json, "nsid", nss[i].ips[j].nsid, ZBX_JSON_TYPE_STRING);
			zbx_json_addstring(json, "protocol", (protocol == RSM_UDP ? "udp" : "tcp"),
					ZBX_JSON_TYPE_STRING);
			zbx_json_addint64(json, "rtt", nss[i].ips[j].rtt);
			zbx_json_close(json);
		}
	}

	zbx_json_close(json);

	zbx_json_addarray(json, "nss");

	for (i = 0; i < nss_num; i++)
	{
		zbx_json_addobject(json, NULL);
		zbx_json_addstring(json, "ns", nss[i].name, ZBX_JSON_TYPE_STRING);
		zbx_json_adduint64(json, "status", (SUCCEED == nss[i].result ? 1 : 0));
		zbx_json_close(json);
	}

	zbx_json_close(json);

	zbx_json_adduint64(json, "nssok", nssok);
	zbx_json_adduint64(json, "mode", current_mode);
	zbx_json_adduint64(json, "status", test_status);
	zbx_json_adduint64(json, "protocol", (protocol == RSM_UDP ? 0 : 1));
	zbx_json_addstring(json, "testedname", testedname, ZBX_JSON_TYPE_STRING);

	if (dnssec_enabled)
		zbx_json_adduint64(json, "dnssecstatus", dnssec_status);

	zbx_json_close(json);
}

static int	metadata_file_exists(const char *rsmhost, int *file_exists, char *err, size_t err_size)
{
	char		*file;
	zbx_stat_t	buf;
	int		ret = SUCCEED;

	file = zbx_dsprintf(NULL, "%s-%s.bin", METADATA_FILE_PREFIX, rsmhost);

	if (0 == zbx_stat(file, &buf))
	{
		*file_exists = S_ISREG(buf.st_mode) ? 1 : 0;
	}
	else if (errno == ENOENT)
	{
		*file_exists = 0;
	}
	else
	{
		zbx_snprintf(err, err_size, "cannot access file \"%s\": %s", file, strerror(errno));
		goto out;
	}

	ret = SUCCEED;
out:
	zbx_free(file);

	return ret;
}

static int	read_metadata(const char *rsmhost, unsigned int *current_mode, int *successful_tests, char *err,
		size_t err_size)
{
	char	*file;
	FILE	*f;
	int	ret = FAIL;

	file = zbx_dsprintf(NULL, "%s-%s.bin", METADATA_FILE_PREFIX, rsmhost);

	if (NULL == (f = fopen(file, "rb")))	/* r for read, b for binary */
	{
		zbx_snprintf(err, err_size, "cannot open metadata file \"%s\": %s", file, strerror(errno));
		goto out;
	}

	if (1 > fread(current_mode, sizeof(*current_mode), 1, f) ||
			1 > fread(successful_tests, sizeof(*successful_tests), 1, f))
	{
		zbx_snprintf(err, err_size, "cannot read metadata from file \"%s\"", file);
		goto out;
	}

	ret = SUCCEED;
out:
	if (NULL != f)
		fclose(f);

	zbx_free(file);

	return ret;
}

static int	write_metadata(const char *rsmhost, unsigned int current_mode, int successful_tests, char *err,
		size_t err_size)
{
	char	*file;
	FILE	*f;
	int	ret = FAIL;

	file = zbx_dsprintf(NULL, "%s-%s.bin", METADATA_FILE_PREFIX, rsmhost);

	if (NULL == (f = fopen(file, "wb")))	/* w for write, b for binary */
	{
		zbx_snprintf(err, err_size, "cannot open metadata file \"%s\": %s", file, strerror(errno));
		goto out;
	}

	if (1 > fwrite(&current_mode, sizeof(current_mode), 1, f) ||
			1 > fwrite(&successful_tests, sizeof(successful_tests), 1, f))
	{
		zbx_snprintf(err, err_size, "cannot write metadata to file \"%s\"", file);
		goto out;
	}

	ret = SUCCEED;
out:
	if (NULL != f)
		fclose(f);

	zbx_free(file);

	return ret;
}

static int	delete_metadata(const char *rsmhost, char *err, size_t err_size)
{
	char	*file;
	int	ret = FAIL;

	file = zbx_dsprintf(NULL, "%s-%s.bin", METADATA_FILE_PREFIX, rsmhost);

	if (0 != unlink(file))
	{
		zbx_snprintf(err, err_size, "cannot delete metadata file \"%s\": %s", file, strerror(errno));
		goto out;
	}

	ret = SUCCEED;
out:
	zbx_free(file);

	return ret;
}

#define CURRENT_MODE_NORMAL		0
#define CURRENT_MODE_CRITICAL_UDP	1
#define CURRENT_MODE_CRITICAL_TCP	2

static int	update_metadata(int file_exists, const char *rsmhost, unsigned int test_status, int test_recover,
		char protocol, unsigned int *current_mode, int *successful_tests, FILE *log_fd, char *err,
		size_t err_size)
{
	if (1 == test_status)
	{
		/* test successful */
		if (CURRENT_MODE_NORMAL != *current_mode)
		{
			/* currently we are in critical mode */
			(*successful_tests)++;

			if (*successful_tests == test_recover)
			{
				/* switch to normal */
				*successful_tests = 0;
				*current_mode = CURRENT_MODE_NORMAL;

				rsm_info(log_fd, "mode changed from critical back to normal for the TLD"
						" due to no errors in the authoritative server tests"
						", will continue using transport protocol according to the algorithm");
			}
		}
	}
	else
	{
		/* test failed */
		*successful_tests = 0;

		if (CURRENT_MODE_NORMAL == *current_mode)
		{
			*current_mode = (RSM_UDP == protocol
					? CURRENT_MODE_CRITICAL_UDP
					: CURRENT_MODE_CRITICAL_TCP);

			rsm_infof(log_fd, "mode changed from normal to critical for the TLD due to errors"
					" in the authoritative server tests, will continue using %s protocol",
					(RSM_UDP == protocol ? "UDP" : "TCP"));
		}
	}

	if (CURRENT_MODE_NORMAL == *current_mode)
	{
		if (1 == file_exists)
		{
			/* delete the file */
			rsm_info(log_fd, "removing the metadata file");

			return delete_metadata(rsmhost, err, err_size);
		}

		return SUCCEED;
	}

	return write_metadata(rsmhost, *current_mode, *successful_tests, err, err_size);
}

/* the value can be in 2 formats:                                                          */
/*   <value>                                                                               */
/*   <value>;<timestamp>:<newvalue>                                                        */
/*                                                                                         */
/* In the latter case the new value gets into effect after specified timestamp has passed. */
int	get_dns_minns_from_value(time_t now, const char *value, int *minns)
{
	const char	*p, *minns_p;
	time_t		ts;

	for (minns_p = value; NULL != (p = strchr(minns_p, ';'));)
	{
		if (1 != sscanf(++p, ZBX_FS_TIME_T, &ts))
			return FAIL;

		if (ts > now)
			break;

		if (NULL == (p = strchr(minns_p, ':')))
			return FAIL;

		minns_p = ++p;
	}

	*minns = atoi(minns_p);

	return SUCCEED;
}

int	check_rsm_dns(zbx_uint64_t hostid, zbx_uint64_t itemid, const char *host, int nextcheck,
		const AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char			err[ZBX_ERR_BUF_SIZE], protocol, *rsmhost, *testprefix, *name_servers_list, *res_ip,
				testedname[ZBX_HOST_BUF_SIZE], *minns_value;
	zbx_dnskeys_error_t	ec_dnskeys;
	ldns_resolver		*res = NULL;
	ldns_rr_list		*keys = NULL;
	FILE			*log_fd;
	zbx_ns_t		*nss = NULL;
	size_t			i, j, nss_num = 0;
	unsigned int		extras, current_mode, test_status, dnssec_status, nssok;
	struct zbx_json		json;
	uint16_t		resolver_port = DEFAULT_RESOLVER_PORT,
				nameserver_port = DEFAULT_NAMESERVER_PORT;
	int			dnssec_enabled,
				rdds43_enabled,
				rdds80_enabled,
				udp_enabled,
				tcp_enabled,
				ipv4_enabled,
				ipv6_enabled,
				udp_rtt_limit,
				tcp_rtt_limit,
				tcp_ratio,
				test_recover_udp,
				test_recover_tcp,
				rdds_enabled,
				rtt_limit,
				successful_tests,
				test_recover,
				minns,
				file_exists = 0,
				epp_enabled = 0,
				ret = SYSINFO_RET_FAIL;

	if (17 != request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "item must contain 17 parameters"));
		return ret;
	}

	/* TLD goes first, then DNS specific parameters, then TLD options, probe options and global settings */
	GET_PARAM_NEMPTY(rsmhost          , 0 , "Rsmhost");
	GET_PARAM_NEMPTY(testprefix       , 1 , "Test prefix");
	GET_PARAM_NEMPTY(name_servers_list, 2 , "List of Name Servers");
	GET_PARAM_UINT  (dnssec_enabled   , 3 , "DNSSEC enabled on rsmhost");
	GET_PARAM_UINT  (rdds43_enabled   , 4 , "RDDS43 enabled on rsmhost");
	GET_PARAM_UINT  (rdds80_enabled   , 5 , "RDDS80 enabled on rsmhost");
	GET_PARAM_UINT  (udp_enabled      , 6 , "DNS UDP enabled");
	GET_PARAM_UINT  (tcp_enabled      , 7 , "DNS TCP enabled");
	GET_PARAM_UINT  (ipv4_enabled     , 8 , "IPv4 enabled");
	GET_PARAM_UINT  (ipv6_enabled     , 9 , "IPv6 enabled");
	GET_PARAM_NEMPTY(res_ip           , 10, "IP address of local resolver");
	GET_PARAM_UINT  (udp_rtt_limit    , 11, "maximum allowed UDP RTT");
	GET_PARAM_UINT  (tcp_rtt_limit    , 12, "maximum allowed TCP RTT");
	GET_PARAM_UINT  (tcp_ratio        , 13, "TCP ratio");
	GET_PARAM_UINT  (test_recover_udp , 14, "successful tests to recover from critical mode (UDP)");
	GET_PARAM_UINT  (test_recover_tcp , 15, "successful tests to recover from critical mode (TCP)");
	GET_PARAM_NEMPTY(minns_value      , 16, "minimum number of working name servers");

	rdds_enabled = (rdds43_enabled || rdds80_enabled);

	if (SUCCEED != get_dns_minns_from_value((time_t)nextcheck, minns_value, &minns))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "unexpected format of parameter #17: %s", minns_value));
		return ret;
	}

	if (SUCCEED != metadata_file_exists(rsmhost, &file_exists, err, sizeof(err)))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, err));
		return ret;
	}

	if (0 == file_exists)
	{
		current_mode = CURRENT_MODE_NORMAL;
		successful_tests = 0;
	}
	else if (SUCCEED != read_metadata(rsmhost, &current_mode, &successful_tests, err, sizeof(err)))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, err));
		return ret;
	}

	/* choose test protocol: if only one is enabled, select that one, otherwise select based on the ratio */
	if (udp_enabled && !tcp_enabled)
	{
		protocol = RSM_UDP;
	}
	else if (tcp_enabled && !udp_enabled)
	{
		protocol = RSM_TCP;
	}
	else if (CURRENT_MODE_NORMAL == current_mode)
	{
		/* Add noise (hostid + itemid) to avoid using TCP by all proxies simultaneously. */
		/* This should balance usage of TCP protocol and avoid abusing the Name Servers. */
		protocol = ((nextcheck / 60 + hostid + itemid) % tcp_ratio) == 0 ? RSM_TCP : RSM_UDP;
	}
	else
	{
		protocol = (current_mode == CURRENT_MODE_CRITICAL_TCP ? RSM_TCP : RSM_UDP);
	}

	if (RSM_UDP == protocol)
	{
		rtt_limit = udp_rtt_limit;
		test_recover = test_recover_udp;
	}
	else
	{
		rtt_limit = tcp_rtt_limit;
		test_recover = test_recover_tcp;
	}

	/* open log file */
	if (NULL == (log_fd = open_item_log(host, rsmhost, ZBX_DNS_LOG_PREFIX, err, sizeof(err))))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, err));
		return ret;
	}

	rsm_info(log_fd, "START TEST");

	rsm_infof(log_fd, "mode: %s, protocol: %s, rtt limit: %d, tcp ratio: %d, minns: %d, UDP: %d, TCP: %d"
			" (for critical mode: successful: %d, required for recovery: %d for UDP, %d for TCP)",
			(CURRENT_MODE_NORMAL == current_mode ? "normal" : "critical"),
			(protocol == RSM_UDP ? "UDP" : "TCP"),
			rtt_limit,
			tcp_ratio,
			minns,
			udp_enabled,
			tcp_enabled,
			successful_tests,
			test_recover_udp,
			test_recover_tcp);

	extras = (dnssec_enabled ? RESOLVER_EXTRAS_DNSSEC : RESOLVER_EXTRAS_NONE);

	/* create resolver */
	if (SUCCEED != zbx_create_resolver(&res, "resolver", res_ip, resolver_port, protocol, ipv4_enabled, ipv6_enabled,
			extras,
			(RSM_UDP == protocol ? RSM_UDP_TIMEOUT : RSM_TCP_TIMEOUT),
			(RSM_UDP == protocol ? RSM_UDP_RETRY : RSM_TCP_RETRY),
			log_fd, err, sizeof(err)))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "cannot create resolver: %s", err));
		goto end;
	}

	/* get list of Name Servers and IPs, by default it will set every Name Server */
	/* as working so if we have no IPs the result of Name Server will be SUCCEED  */
	if (SUCCEED != zbx_get_nameservers(name_servers_list, &nss, &nss_num, ipv4_enabled, ipv6_enabled, log_fd,
			err, sizeof(err)))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, err));
		goto end;
	}

	if (nss_num == 0)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "nothing to do, no Name Servers to test"));
		goto end;
	}

	/* from this point item will not become NOTSUPPORTED */
	ret = SYSINFO_RET_OK;

	/* generate tested name */
	if (0 != strcmp(".", rsmhost))
		zbx_snprintf(testedname, sizeof(testedname), "%s.%s.", testprefix, rsmhost);
	else
		zbx_snprintf(testedname, sizeof(testedname), "%s.", testprefix);

	if (0 != dnssec_enabled && SUCCEED != zbx_get_dnskeys(res, rsmhost, res_ip, &keys, log_fd, &ec_dnskeys,
			err, sizeof(err)))
	{
		/* failed to get DNSKEY records */

		int	res_ec;

		rsm_err(log_fd, err);

		res_ec = DNS[DNS_PROTO(res)].dnskeys_error(ec_dnskeys);

		for (i = 0; i < nss_num; i++)
		{
			for (j = 0; j < nss[i].ips_num; j++)
				nss[i].ips[j].rtt = res_ec;
		}
	}
	else
	{
		size_t		th_num = 0, threads_num = 0;
		int		last_test_failed = 0;
		char		buf[2048];
		pid_t		pid;
		writer_thread_t	*threads = NULL;

		for (i = 0; i < nss_num; i++)
		{
			for (j = 0; j < nss[i].ips_num; j++)
				threads_num++;
		}

		threads = (writer_thread_t *)zbx_calloc(threads, threads_num, sizeof(*threads));
		memset(threads, 0, threads_num * sizeof(*threads));

		fflush(log_fd);

		for (i = 0; i < nss_num; i++)
		{
			for (j = 0; j < nss[i].ips_num; j++)
			{
				int	fd[2];		/* reader and writer fd for data */
				int	log_pipe[2];	/* reader and writer fd for logs */
				int	rv_fd, rv_log_pipe = 0;

				if (0 != last_test_failed)
				{
					nss[i].ips[j].rtt = DNS[DNS_PROTO(res)].ns_query_error(ZBX_NS_QUERY_INTERNAL);

					continue;
				}

				if (-1 == (rv_fd = pipe(fd)) || -1 == (rv_log_pipe = pipe(log_pipe)))
				{
					rsm_errf(log_fd, "cannot create pipe: %s", zbx_strerror(errno));

					if (-1 == rv_log_pipe)
					{
						close(fd[0]);
						close(fd[1]);
					}

					nss[i].ips[j].rtt = DNS[DNS_PROTO(res)].ns_query_error(ZBX_NS_QUERY_INTERNAL);
					last_test_failed = 1;

					continue;
				}

				zbx_child_fork(&pid);

				if (0 > pid)
				{
					rsm_errf(log_fd, "cannot create process: %s", zbx_strerror(errno));

					close(fd[0]);
					close(fd[1]);
					close(log_pipe[0]);
					close(log_pipe[1]);

					nss[i].ips[j].rtt = DNS[DNS_PROTO(res)].ns_query_error(ZBX_NS_QUERY_INTERNAL);
					last_test_failed = 1;

					continue;
				}
				else if (0 == pid)
				{
					/* child */

					FILE	*th_log_fd;

					close(fd[0]);		/* child does not need data reader fd */
					close(log_pipe[0]);	/* child does not need log reader fd */
					fclose(log_fd);		/* child does not need log writer */

					if (NULL == (th_log_fd = fdopen(log_pipe[1], "w")))
					{
						rsm_errf(log_fd, "cannot open log pipe: %s", zbx_strerror(errno));

						nss[i].ips[j].rtt =
							DNS[DNS_PROTO(res)].ns_query_error(ZBX_NS_QUERY_INTERNAL);
					}

					if (NULL != th_log_fd && SUCCEED != zbx_get_ns_ip_values(res,
							nss[i].name,
							nss[i].ips[j].ip,
							nameserver_port,
							keys,
							testedname,
							th_log_fd,
							&nss[i].ips[j].rtt,
							&nss[i].ips[j].nsid,
							(RSM_UDP == protocol &&
									0 != rdds_enabled ? &nss[i].ips[j].upd : NULL),
							ipv4_enabled,
							ipv6_enabled,
							epp_enabled,
							err,
							sizeof(err)))
					{
						rsm_err(th_log_fd, err);
					}

					pack_values(i, j, nss[i].ips[j].rtt, nss[i].ips[j].upd, nss[i].ips[j].nsid,
							buf, sizeof(buf));

					if (-1 == write(fd[1], buf, strlen(buf) + 1))
						rsm_errf(th_log_fd, "cannot write to pipe: %s", zbx_strerror(errno));

					fclose(th_log_fd);
					close(fd[1]);
					close(log_pipe[1]);

					exit(EXIT_SUCCESS);
				}
				else
				{
					/* parent */

					close(fd[1]);		/* parent does not need data writer fd */
					close(log_pipe[1]);	/* parent does not need log writer fd */

					threads[th_num].pid = pid;
					threads[th_num].fd = fd[0];
					threads[th_num].log_fd = log_pipe[0];

					th_num++;
				}
			}
		}

		for (th_num = 0; th_num < threads_num; th_num++)
		{
			ssize_t	bytes;
			int	status;

			if (0 == threads[th_num].pid)
				continue;

			if (-1 != read(threads[th_num].fd, buf, sizeof(buf)))
			{
				int	rtt, upd;
				char	nsid[NSID_MAX_LENGTH * 2 + 1];	/* hex representation + terminating null char */

				unpack_values(&i, &j, &rtt, &upd, nsid, buf, log_fd);

				nss[i].ips[j].rtt = rtt;
				nss[i].ips[j].upd = upd;
				nss[i].ips[j].nsid = zbx_strdup(nss[i].ips[j].nsid, nsid);
			}
			else
				rsm_errf(log_fd, "cannot read from pipe: %s", zbx_strerror(errno));

			while (0 != (bytes = read(threads[th_num].log_fd, buf, sizeof(buf))))
			{
				if (-1 == bytes)
				{
					rsm_errf(log_fd, "cannot read logs from pipe: %s", zbx_strerror(errno));
					break;
				}

				rsm_dump(log_fd, "%.*s", (int)bytes, buf);
			}

			if (0 >= waitpid(threads[th_num].pid, &status, 0))
				rsm_err(log_fd, "error on thread waiting");

			close(threads[th_num].fd);
			close(threads[th_num].log_fd);
		}

		zbx_free(threads);
	}

	set_dns_test_results(nss, nss_num, rtt_limit, minns, &nssok, &test_status, &dnssec_status, dnssec_enabled,
			log_fd);

	create_dns_json(&json, nss, nss_num, current_mode, nssok, test_status, dnssec_status, protocol, testedname,
			dnssec_enabled);

	if (SUCCEED != update_metadata(file_exists, rsmhost, test_status, test_recover, protocol, &current_mode,
			&successful_tests, log_fd, err, sizeof(err)))
	{
		rsm_errf(log_fd, "internal error: %s", err);
	}

	SET_STR_RESULT(result, zbx_strdup(NULL, json.buffer));

	rsm_infof(log_fd, "test result %s", json.buffer);

	zbx_json_free(&json);
end:
	if (0 != ISSET_MSG(result))
		rsm_err(log_fd, result->msg);

	rsm_info(log_fd, "END TEST");

	if (0 != nss_num)
	{
		zbx_clean_nss(nss, nss_num);
		zbx_free(nss);
	}

	if (NULL != keys)
		ldns_rr_list_deep_free(keys);

	if (NULL != res)
	{
		if (0 != ldns_resolver_nameserver_count(res))
			ldns_resolver_deep_free(res);
		else
			ldns_resolver_free(res);
	}

	if (NULL != log_fd)
		fclose(log_fd);
out:
	return ret;
}

#undef CURRENT_MODE_NORMAL
#undef CURRENT_MODE_CRITICAL_UDP
#undef CURRENT_MODE_CRITICAL_TCP

static void	zbx_get_rdds43_nss(zbx_vector_str_t *nss, const char *recv_buf, const char *rdds43_ns_string,
		FILE *log_fd)
{
	const char	*p;
	char		ns_buf[ZBX_HOST_BUF_SIZE];
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
			rsm_errf(log_fd, "RSM RDDS internal error, NS buffer too small (%u bytes)"
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

static int	zbx_rdds43_test(const char *request, const char *ip, unsigned short port, int timeout, char **answer,
		int *rtt, char *err, size_t err_size)
{
	zbx_socket_t	s;
	char		send_buf[ZBX_SEND_BUF_SIZE];
	zbx_timespec_t	start, now;
	ssize_t		nbytes;
	int		ret = FAIL;

	zbx_timespec(&start);

	if (SUCCEED != zbx_tcp_connect(&s, NULL, ip, port, timeout, ZBX_TCP_SEC_UNENCRYPTED, NULL, NULL))
	{
		*rtt = (SUCCEED == zbx_alarm_timed_out() ? ZBX_EC_RDDS43_TO : ZBX_EC_RDDS43_ECON);
		zbx_snprintf(err, err_size, "cannot connect: %s", zbx_socket_strerror());
		goto out;
	}

	zbx_snprintf(send_buf, sizeof(send_buf), "%s\r\n", request);

	if (SUCCEED != zbx_tcp_send_raw(&s, send_buf))
	{
		*rtt = (SUCCEED == zbx_alarm_timed_out() ? ZBX_EC_RDDS43_TO : ZBX_EC_RDDS43_ECON);
		zbx_snprintf(err, err_size, "cannot send data: %s", zbx_socket_strerror());
		goto out;
	}

	if (FAIL == (nbytes = zbx_tcp_recv_raw_ext(&s, 0)))	/* timeout is still "active" here */
	{
		*rtt = (SUCCEED == zbx_alarm_timed_out() ? ZBX_EC_RDDS43_TO : ZBX_EC_RDDS43_ECON);
		zbx_snprintf(err, err_size, "cannot receive data: %s", zbx_socket_strerror());
		goto out;
	}

	if (0 == nbytes)
	{
		*rtt = ZBX_EC_RDDS43_EMPTY;
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

/******************************************************************************
 *                                                                            *
 * Function: zbx_resolver_resolve_host                                        *
 *                                                                            *
 * Purpose: resolve specified host to IPs                                     *
 *                                                                            *
 * Parameters: res          - [IN]  resolver object to use for resolving      *
 *             extras       - [IN]  bitmask of optional checks (a combination *
 *                                  of ZBX_RESOLVER_CHECK_* defines)          *
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
static int	zbx_resolver_resolve_host(ldns_resolver *res, const char *host, zbx_vector_str_t *ips, int ipv_flags,
		FILE *log_fd, zbx_resolver_error_t *ec_res, char *err, size_t err_size)
{
	const zbx_ipv_t	*ipv;
	ldns_rdf	*rdf;
	int		ret = FAIL;

	if (NULL == (rdf = ldns_rdf_new_frm_str(LDNS_RDF_TYPE_DNAME, host)))
	{
		zbx_strlcpy(err, UNEXPECTED_LDNS_MEM_ERROR, err_size);
		*ec_res = ZBX_RESOLVER_INTERNAL;
		return ret;
	}

	for (ipv = ipvs; NULL != ipv->name; ipv++)
	{
		ldns_pkt	*pkt;
		ldns_rr_list	*rr_list;
		ldns_pkt_rcode	rcode;
		ldns_status	status;

		status = ldns_resolver_query_status(&pkt, res, rdf, ipv->rr_type, LDNS_RR_CLASS_IN, LDNS_RD);

		if (LDNS_STATUS_OK != status)
		{
			zbx_snprintf(err, err_size, "cannot resolve host \"%s\" to %s address: %s", host, ipv->name,
					ldns_get_errorstr_by_id(status));
			*ec_res = ZBX_RESOLVER_NOREPLY;
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
					*ec_res = ZBX_RESOLVER_SERVFAIL;
					break;
				case LDNS_RCODE_NXDOMAIN:
					*ec_res = ZBX_RESOLVER_NXDOMAIN;
					break;
				default:
					*ec_res = ZBX_RESOLVER_CATCHALL;
			}

			ldns_pkt_free(pkt);
			goto out;
		}

		if (0 != (ipv_flags & ipv->flag) &&
				NULL != (rr_list = ldns_pkt_rr_list_by_type(pkt, ipv->rr_type, LDNS_SECTION_ANSWER)))
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

static void	zbx_delete_unsupported_ips(zbx_vector_str_t *ips, int ipv4_enabled, int ipv6_enabled)
{
	int	i;
	char	is_ipv4;

	for (i = 0; i < ips->values_num; i++)
	{
		if (SUCCEED != zbx_validate_ip(ips->values[i], ipv4_enabled, ipv6_enabled, NULL, &is_ipv4))
		{
			zbx_free(ips->values[i]);
			zbx_vector_str_remove(ips, i--);

			continue;
		}

		if ((0 != is_ipv4 && 0 == ipv4_enabled) || (0 == is_ipv4 && 0 == ipv6_enabled))
		{
			zbx_free(ips->values[i]);
			zbx_vector_str_remove(ips, i--);
		}
	}
}

static char	zbx_validate_host_list(const char *list, char delim)
{
	const char	*p;

	p = list;

	while ('\0' != *p && (0 != isalnum(*p) || '.' == *p || '-' == *p || '_' == *p || ':' == *p || delim == *p))
		p++;

	return *p;
}

static void	zbx_get_strings_from_list(zbx_vector_str_t *strings, char *list, char delim)
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
static int	map_http_code(long http_code)
{
#if ZBX_HTTP_RESPONSE_OK != 200L
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

/* store the curl output in memory */
static size_t	curl_memory(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	curl_data_t	*data = (curl_data_t *)userdata;
	size_t		r_size = size * nmemb;

	zbx_strncpy_alloc(&data->buf, &data->alloc, &data->offset, (const char *)ptr, r_size);

	return r_size;
}

/* discard the curl output (using inline to hide "unused" compiler warning when -Wunused) */
static inline size_t	curl_devnull(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	(void)ptr;
	(void)userdata;

	return size * nmemb;
}

typedef enum
{
	ZBX_EC_PRE_STATUS_ERROR_INTERNAL,
	ZBX_EC_PRE_STATUS_ERROR_TO,
	ZBX_EC_PRE_STATUS_ERROR_ECON,
	ZBX_EC_PRE_STATUS_ERROR_EHTTP,
	ZBX_EC_PRE_STATUS_ERROR_EHTTPS,
	ZBX_EC_PRE_STATUS_ERROR_NOCODE,
	ZBX_EC_PRE_STATUS_ERROR_EMAXREDIRECTS
}
pre_status_error_t;

typedef enum
{
	PRE_HTTP_STATUS_ERROR,
	HTTP_STATUS_ERROR
}
zbx_http_error_type_t;

typedef union
{
	pre_status_error_t	pre_status_error;
	long			response_code;
}
zbx_http_error_data_t;

typedef struct
{
	zbx_http_error_type_t type;
	zbx_http_error_data_t error;
}
zbx_http_error_t;

#define ZBX_FLAG_CURL_VERBOSE	0x1

/* Helper function for Web-based RDDS80 and RDAP checks. Adds host to header, connects to URL obeying timeout and */
/* max redirect settings, stores web page contents using provided callback, checks for OK response and calculates */
/* round-trip time. When function succeeds it returns RTT in milliseconds. When function fails it returns source  */
/* of error in provided RTT parameter. Does not verify certificates.                                              */
static int	zbx_http_test(const char *host, const char *url, long timeout, long maxredirs, zbx_http_error_t *ec_http,
		int *rtt, void *writedata, size_t (*writefunction)(char *, size_t, size_t, void *),
		int curl_flags, char *err, size_t err_size)
{
#ifdef HAVE_LIBCURL
	CURL			*easyhandle;
	CURLcode		curl_err;
	CURLoption		opt;
	char			host_buf[ZBX_HOST_BUF_SIZE];
	double			total_time;
	long			response_code, curlopt_verbose;
	struct curl_slist	*slist = NULL;
#endif
	int			ret = FAIL;

#ifdef HAVE_LIBCURL
	if (NULL == (easyhandle = curl_easy_init()))
	{
		ec_http->type = PRE_HTTP_STATUS_ERROR;
		ec_http->error.pre_status_error = ZBX_EC_PRE_STATUS_ERROR_INTERNAL;

		zbx_strlcpy(err, "cannot init cURL library", err_size);
		goto out;
	}

	zbx_snprintf(host_buf, sizeof(host_buf), "Host: %s", host);
	if (NULL == (slist = curl_slist_append(slist, host_buf)))
	{
		ec_http->type = PRE_HTTP_STATUS_ERROR;
		ec_http->error.pre_status_error = ZBX_EC_PRE_STATUS_ERROR_INTERNAL;

		zbx_strlcpy(err, "cannot generate cURL list of HTTP headers", err_size);
		goto out;
	}

	curlopt_verbose = (0 != (curl_flags & ZBX_FLAG_CURL_VERBOSE) ? 1L : 0L);

	if (CURLE_OK != (curl_err = curl_easy_setopt(easyhandle, opt = CURLOPT_FOLLOWLOCATION, 1L)) ||
			CURLE_OK != (curl_err = curl_easy_setopt(easyhandle, opt = CURLOPT_USERAGENT, "Zabbix " ZABBIX_VERSION)) ||
			CURLE_OK != (curl_err = curl_easy_setopt(easyhandle, opt = CURLOPT_VERBOSE, curlopt_verbose)) ||
			CURLE_OK != (curl_err = curl_easy_setopt(easyhandle, opt = CURLOPT_MAXREDIRS, maxredirs)) ||
			CURLE_OK != (curl_err = curl_easy_setopt(easyhandle, opt = CURLOPT_URL, url)) ||
			CURLE_OK != (curl_err = curl_easy_setopt(easyhandle, opt = CURLOPT_TIMEOUT, timeout)) ||
			CURLE_OK != (curl_err = curl_easy_setopt(easyhandle, opt = CURLOPT_HTTPHEADER, slist)) ||
			CURLE_OK != (curl_err = curl_easy_setopt(easyhandle, opt = CURLOPT_SSL_VERIFYPEER, 0L)) ||
			CURLE_OK != (curl_err = curl_easy_setopt(easyhandle, opt = CURLOPT_SSL_VERIFYHOST, 0L)) ||
			CURLE_OK != (curl_err = curl_easy_setopt(easyhandle, opt = CURLOPT_WRITEDATA, writedata)) ||
			CURLE_OK != (curl_err = curl_easy_setopt(easyhandle, opt = CURLOPT_WRITEFUNCTION, writefunction)))
	{
		ec_http->type = PRE_HTTP_STATUS_ERROR;
		ec_http->error.pre_status_error = ZBX_EC_PRE_STATUS_ERROR_INTERNAL;

		zbx_snprintf(err, err_size, "cannot set cURL option [%d] (%s)", (int)opt, curl_easy_strerror(curl_err));
		goto out;
	}

	if (CURLE_OK != (curl_err = curl_easy_perform(easyhandle)))
	{
		ec_http->type = PRE_HTTP_STATUS_ERROR;

		switch (curl_err)
		{
			case CURLE_OPERATION_TIMEDOUT:
				ec_http->error.pre_status_error = ZBX_EC_PRE_STATUS_ERROR_TO;
				break;
			case CURLE_COULDNT_CONNECT:
				ec_http->error.pre_status_error = ZBX_EC_PRE_STATUS_ERROR_ECON;
				break;
			case CURLE_TOO_MANY_REDIRECTS:
				ec_http->error.pre_status_error = ZBX_EC_PRE_STATUS_ERROR_EMAXREDIRECTS;
				break;
			default:
				if (0 == strncmp(url, "http://", ZBX_CONST_STRLEN("http://")))
					ec_http->error.pre_status_error = ZBX_EC_PRE_STATUS_ERROR_EHTTP;
				else	/* if (0 == strncmp(url, "https://", ZBX_CONST_STRLEN("https://"))) */
					ec_http->error.pre_status_error = ZBX_EC_PRE_STATUS_ERROR_EHTTPS;
		}

		zbx_strlcpy(err, curl_easy_strerror(curl_err), err_size);
		goto out;
	}

	if (CURLE_OK != (curl_err = curl_easy_getinfo(easyhandle, CURLINFO_RESPONSE_CODE, &response_code)))
	{
		ec_http->type = PRE_HTTP_STATUS_ERROR;
		ec_http->error.pre_status_error = ZBX_EC_PRE_STATUS_ERROR_NOCODE;

		zbx_snprintf(err, err_size, "cannot get HTTP response code (%s)", curl_easy_strerror(curl_err));
		goto out;
	}

	if (ZBX_HTTP_RESPONSE_OK != response_code)
	{
		ec_http->type = HTTP_STATUS_ERROR;
		ec_http->error.response_code = response_code;

		zbx_snprintf(err, err_size, "invalid HTTP response code, expected %ld, got %ld", ZBX_HTTP_RESPONSE_OK,
				response_code);
		goto out;
	}

	if (CURLE_OK != (curl_err = curl_easy_getinfo(easyhandle, CURLINFO_TOTAL_TIME, &total_time)))
	{
		ec_http->type = PRE_HTTP_STATUS_ERROR;
		ec_http->error.pre_status_error = ZBX_EC_PRE_STATUS_ERROR_INTERNAL;

		zbx_snprintf(err, err_size, "cannot get HTTP request time (%s)", curl_easy_strerror(curl_err));
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
	ec_http->error.pre_status_error = ZBX_EC_PRE_STATUS_ERROR_INTERNAL;

	zbx_strlcpy(err, "zabbix is not compiled with libcurl support (--with-libcurl)", err_size);
#endif
	return ret;
}

static int	zbx_ec_noerror(int ec)
{
	if (0 <= ec || ZBX_NO_VALUE == ec)
		return SUCCEED;

	return FAIL;
}

static void	zbx_vector_str_clean_and_destroy(zbx_vector_str_t *v)
{
	int	i;

	for (i = 0; i < v->values_num; i++)
		zbx_free(v->values[i]);

	zbx_vector_str_destroy(v);
}

/* FIXME Currently this error code is missing in specification for RDAP. Hopefully, it will be introduced later. */
#ifdef ZBX_EC_RDAP_NOCODE
#	error "please remove temporary definition of ZBX_EC_RDAP_NOCODE, seems like it was added to the header file"
#else
#	define ZBX_EC_RDAP_NOCODE	ZBX_EC_RDAP_INTERNAL_GENERAL
#endif

/* maps generic HTTP errors to RDDS interface specific ones */

#define ZBX_DEFINE_HTTP_PRE_STATUS_ERROR_TO(__interface)					\
static int	zbx_pre_status_error_to_ ## __interface (pre_status_error_t ec_pre_status)	\
{												\
	switch (ec_pre_status)									\
	{											\
		case ZBX_EC_PRE_STATUS_ERROR_INTERNAL:						\
			return ZBX_EC_ ## __interface ## _INTERNAL_GENERAL;			\
		case ZBX_EC_PRE_STATUS_ERROR_TO:						\
			return ZBX_EC_ ## __interface ## _TO;					\
		case ZBX_EC_PRE_STATUS_ERROR_ECON:						\
			return ZBX_EC_ ## __interface ## _ECON;					\
		case ZBX_EC_PRE_STATUS_ERROR_EHTTP:						\
			return ZBX_EC_ ## __interface ## _EHTTP;				\
		case ZBX_EC_PRE_STATUS_ERROR_EHTTPS:						\
			return ZBX_EC_ ## __interface ## _EHTTPS;				\
		case ZBX_EC_PRE_STATUS_ERROR_NOCODE:						\
			return ZBX_EC_ ## __interface ## _NOCODE;				\
		case ZBX_EC_PRE_STATUS_ERROR_EMAXREDIRECTS:					\
			return ZBX_EC_ ## __interface ## _EMAXREDIRECTS;			\
	}											\
	THIS_SHOULD_NEVER_HAPPEN;								\
	return 0;										\
}

ZBX_DEFINE_HTTP_PRE_STATUS_ERROR_TO(RDDS80)
ZBX_DEFINE_HTTP_PRE_STATUS_ERROR_TO(RDAP)

#undef ZBX_DEFINE_HTTP_PRE_STATUS_ERROR_TO

#define ZBX_DEFINE_HTTP_ERROR_TO(__interface)										\
static int	zbx_http_error_to_ ## __interface (zbx_http_error_t ec_http)						\
{															\
	switch (ec_http.type)												\
	{														\
		case PRE_HTTP_STATUS_ERROR:										\
			return zbx_pre_status_error_to_ ## __interface (ec_http.error.pre_status_error);		\
		case HTTP_STATUS_ERROR:											\
			return ZBX_EC_ ## __interface ## _HTTP_BASE - map_http_code(ec_http.error.response_code);	\
	}														\
	THIS_SHOULD_NEVER_HAPPEN;											\
	return 0;													\
}

ZBX_DEFINE_HTTP_ERROR_TO(RDDS80)
ZBX_DEFINE_HTTP_ERROR_TO(RDAP)

#undef ZBX_DEFINE_HTTP_ERROR_TO

/* Splits provided URL into preceding "https://" or "http://", domain name and the rest, frees memory pointed by   */
/* scheme, domain and path pointers and allocates new storage. It is caller responsibility to free them after use. */
static int	zbx_split_url(const char *url, char **scheme, char **domain, int *port, char **path,
		char *err, size_t err_size)
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

static void	create_rdds_json(struct zbx_json *json, const char *ip43, int rtt43, int upd43,
		const char *rdds43_server, const char *rdds43_testedname, const char *ip80, int rtt80,
		const char *rdds80_url, int rdds43_status, int rdds80_status, int rdds_status)
{
	zbx_json_init(json, 2 * ZBX_KIBIBYTE);

	zbx_json_addobject(json, "rdds43");

	zbx_json_addint64(json, "rtt", rtt43);
	if (NULL != ip43)
		zbx_json_addstring(json, "ip", ip43, ZBX_JSON_TYPE_STRING);
	if (ZBX_NO_VALUE != upd43)
		zbx_json_addint64(json, "upd", upd43);
	if (NULL != rdds43_server)
		zbx_json_addstring(json, "target", rdds43_server, ZBX_JSON_TYPE_STRING);
	if (0 != strcmp(rdds43_testedname, ""))
		zbx_json_addstring(json, "testedname", rdds43_testedname, ZBX_JSON_TYPE_STRING);
	zbx_json_addint64(json, "status", rdds43_status);

	zbx_json_close(json);

	zbx_json_addobject(json, "rdds80");

	zbx_json_addint64(json, "rtt", rtt80);
	if (NULL != ip80)
		zbx_json_addstring(json, "ip", ip80, ZBX_JSON_TYPE_STRING);
	if (NULL != rdds80_url)
		zbx_json_addstring(json, "target", rdds80_url, ZBX_JSON_TYPE_STRING);
	zbx_json_addint64(json, "status", rdds80_status);

	zbx_json_close(json);

	zbx_json_addint64(json, "status", rdds_status);
}

int	check_rsm_rdds(const char *host, const AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char			*rsmhost, *rdds43_server, *rdds80_url, *res_ip, *rdds43_ns_string, *answer = NULL,
				is_ipv4, err[ZBX_ERR_BUF_SIZE], *rdds80_domain,
				*scheme = NULL, *domain = NULL, *path = NULL, *formed_url = NULL;
	const char		*rdds43_testedname = NULL, *ip43 = NULL, *ip80 = NULL;
	zbx_vector_str_t	ips43, ips80, nss;
	FILE			*log_fd = NULL;
	ldns_resolver		*res = NULL;
	zbx_resolver_error_t	ec_res;
	time_t			ts, now;
	zbx_http_error_t	ec_http;
	uint16_t		resolver_port = DEFAULT_RESOLVER_PORT,
				rdds43_port = DEFAULT_RDDS43_PORT;
	int			probe_rdds_enabled,
				rdds43_enabled,
				rdds80_enabled,
				ipv4_enabled,
				ipv6_enabled,
				rtt_limit,
				maxredirs,
				rtt43 = ZBX_NO_VALUE,
				upd43 = ZBX_NO_VALUE,
				rtt80 = ZBX_NO_VALUE,
				rdds_enabled,
				epp_enabled = 0,
				probe_epp_enabled = 0,
				ipv_flags = 0,
				curl_flags = 0,
				port,
				ret = SYSINFO_RET_FAIL;
	struct zbx_json		json;

	if (13 != request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "item must contain 13 parameters"));
		return ret;
	}

	zbx_vector_str_create(&ips43);
	zbx_vector_str_create(&ips80);
	zbx_vector_str_create(&nss);

	GET_PARAM_NEMPTY(rsmhost           , 0 ,   "Rsmhost");
	GET_PARAM       (rdds43_server     , 1); /* RDDS43 server      */
	GET_PARAM       (rdds80_url        , 2); /* RDDS80 url         */
	GET_PARAM       (rdds43_testedname , 3); /* RDDS43 test domain */
	GET_PARAM       (rdds43_ns_string  , 4); /* RDDS43 ns string   */
	GET_PARAM_UINT  (probe_rdds_enabled, 5 ,   "RDDS enabled on probe");
	GET_PARAM_UINT  (rdds43_enabled    , 6 ,   "RDDS43 enabled on rsmhost");
	GET_PARAM_UINT  (rdds80_enabled    , 7 ,   "RDDS80 enabled on rsmhost");
	GET_PARAM_UINT  (ipv4_enabled      , 8 ,   "IPv4 enabled");
	GET_PARAM_UINT  (ipv6_enabled      , 9 ,   "IPv6 enabled");
	GET_PARAM_NEMPTY(res_ip            , 10,   "IP address of local resolver");
	GET_PARAM_UINT  (rtt_limit         , 11,   "RTT limit");
	GET_PARAM_UINT  (maxredirs         , 12,   "max redirects");

	rdds_enabled = (rdds43_enabled || rdds80_enabled);

	if (0 != rdds43_enabled)
	{
		if ('\0' == *rdds43_server)
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "macro {$RSM.TLD.RDDS43.SERVER} must be set"));
			goto out;
		}
	}

	if (0 != rdds80_enabled)
	{
		if  ('\0' == *rdds80_url)
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "macro {$RSM.TLD.RDDS80.URL} must be set"));
			goto out;
		}

		if (SUCCEED != zbx_split_url(rdds80_url, &scheme, &domain, &port, &path, err, sizeof(err)))
		{
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "\"%s\": %s", rdds80_url, err));
			goto out;
		}
	}

	/* open log file */
	if (NULL == (log_fd = open_item_log(host, rsmhost, ZBX_RDDS_LOG_PREFIX, err, sizeof(err))))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, err));
		goto out;
	}

	/* create resolver, note: it's used in both RDDS43 and RDDS80 tests */
	if (SUCCEED != zbx_create_resolver(&res, "resolver", res_ip, resolver_port, RSM_TCP, ipv4_enabled, ipv6_enabled,
			RESOLVER_EXTRAS_NONE, RSM_TCP_TIMEOUT, RSM_TCP_RETRY, log_fd, err, sizeof(err)))
	{
		/* exception, item becomes UNSUPPORTED */
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "cannot create resolver: %s", err));
		goto out;
	}

	rsm_info(log_fd, "START TEST");

	/* from this point item will not become NOTSUPPORTED */
	ret = SYSINFO_RET_OK;

	if (0 != ipv4_enabled)
		ipv_flags |= ZBX_FLAG_IPV4_ENABLED;
	if (0 != ipv6_enabled)
		ipv_flags |= ZBX_FLAG_IPV6_ENABLED;

	if (0 == probe_rdds_enabled)
	{
		rsm_info(log_fd, "RDDS disabled on this probe");
		rdds_enabled = 0;
		goto end;
	}

	if (0 == rdds_enabled)
	{
		rsm_info(log_fd, "RDDS disabled on this RSM host");
		goto end;
	}

	if (0 == rdds43_enabled)
		goto rdds80_test;

	/* start RDDS43 test, resolve host to ips */
	if (SUCCEED != zbx_resolver_resolve_host(res, rdds43_server, &ips43, ipv_flags, log_fd, &ec_res,
			err, sizeof(err)))
	{
		rtt43 = zbx_resolver_error_to_RDDS43(ec_res);
		rsm_errf(log_fd, "RDDS43 \"%s\": %s", rdds43_server, err);
	}

	/* if RDDS43 fails we should still process RDDS80 */

	if (SUCCEED == zbx_ec_noerror(rtt43))
	{
		if (0 == ips43.values_num)
		{
			rtt43 = ZBX_EC_RDDS43_INTERNAL_IP_UNSUP;
			rsm_errf(log_fd, "RDDS43 \"%s\": IP address(es) of host not supported by the Probe",
					rdds43_server);
		}
	}

	if (SUCCEED == zbx_ec_noerror(rtt43))
	{
		/* choose random IP */
		ip43 = ips43.values[zbx_random((size_t)ips43.values_num)];

		rsm_infof(log_fd, "start RDDS43 test (ip %s, request \"%s\", expected NS string \"%s\")",
				ip43, rdds43_testedname, rdds43_ns_string);

		if (SUCCEED != zbx_rdds43_test(rdds43_testedname, ip43, rdds43_port, RSM_TCP_TIMEOUT, &answer, &rtt43,
				err, sizeof(err)))
		{
			rsm_errf(log_fd, "RDDS43 of \"%s\" (%s) failed: %s", rdds43_server, ip43, err);
		}
	}

	if (SUCCEED == zbx_ec_noerror(rtt43))
	{
		zbx_get_rdds43_nss(&nss, answer, rdds43_ns_string, log_fd);

		if (0 == nss.values_num)
		{
			rtt43 = ZBX_EC_RDDS43_NONS;
			rsm_errf(log_fd, "no Name Servers found in the output of RDDS43 server \"%s\""
					" (%s) for query \"%s\" (expecting prefix \"%s\")",
					rdds43_server, ip43, rdds43_testedname, rdds43_ns_string);
		}
	}

	if (SUCCEED == zbx_ec_noerror(rtt43))
	{
		if (0 != epp_enabled)
		{
			/* start RDDS UPD test, get timestamp from the host name */
			char	*random_ns;

			/* choose random NS from the output */
			random_ns = nss.values[zbx_random((size_t)nss.values_num)];

			rsm_infof(log_fd, "randomly selected Name Server server \"%s\"", random_ns);

			if (SUCCEED != zbx_get_ts_from_host(random_ns, &ts))
			{
				upd43 = ZBX_EC_RDDS43_INTERNAL_GENERAL;
				rsm_errf(log_fd, "cannot extract Unix timestamp from Name Server \"%s\"", random_ns);
			}

			if (upd43 == ZBX_NO_VALUE)
			{
				now = time(NULL);

				if (0 > now - ts)
				{
					rsm_errf(log_fd, "Unix timestamp of Name Server \"%s\" is in the future"
							" (current: %u)", random_ns, now);
					upd43 = ZBX_EC_RDDS43_INTERNAL_GENERAL;
				}
			}

			if (upd43 == ZBX_NO_VALUE)
			{
				/* successful UPD */
				upd43 = (int)(now - ts);
			}

			rsm_infof(log_fd, "===>\n%.*s\n<=== end RDDS43 test (rtt:%d upd43:%d)",
					ZBX_RDDS_PREVIEW_SIZE, answer, rtt43, upd43);
		}
		else
		{
			rsm_infof(log_fd, "===>\n%.*s\n<=== end RDDS43 test (rtt:%d)",
					ZBX_RDDS_PREVIEW_SIZE, answer, rtt43);
		}
	}

	if (0 == rdds80_enabled)
		goto end;

rdds80_test:
	rsm_infof(log_fd, "start RDDS80 test (url %s)", rdds80_url);

	/* start RDDS80 test, resolve domain to ips */
	if (SUCCEED != zbx_resolver_resolve_host(res, domain, &ips80, ipv_flags, log_fd, &ec_res, err, sizeof(err)))
	{
		rtt80 = zbx_resolver_error_to_RDDS80(ec_res);
		rsm_errf(log_fd, "RDDS80 \"%s\": %s", domain, err);
		goto end;
	}

	if (0 == ips80.values_num)
	{
		rtt80 = ZBX_EC_RDDS80_INTERNAL_IP_UNSUP;
		rsm_errf(log_fd, "RDDS80 \"%s\": IP address(es) of host not supported by the Probe", rdds80_url);
		goto end;
	}

	/* choose random IP */
	ip80 = ips80.values[zbx_random((size_t)ips80.values_num)];

	if (SUCCEED != zbx_validate_ip(ip80, ipv4_enabled, ipv6_enabled, NULL, &is_ipv4))
	{
		rtt80 = ZBX_EC_RDDS80_INTERNAL_GENERAL;
		rsm_errf(log_fd, "internal error, selected unsupported IP of \"%s\": \"%s\"", rdds80_url, ip80);
		goto end;
	}

	if (0 == is_ipv4)
		formed_url = zbx_dsprintf(formed_url, "%s[%s]:%d%s", scheme, ip80, port, path);
	else
		formed_url = zbx_dsprintf(formed_url, "%s%s:%d%s", scheme, ip80, port, path);

	rsm_infof(log_fd, "domain \"%s\" was resolved to %s, using URL \"%s\".", domain, ip80, formed_url);

	if (SUCCEED != zbx_http_test(domain, formed_url, RSM_TCP_TIMEOUT, maxredirs, &ec_http, &rtt80, NULL,
			curl_devnull, curl_flags, err, sizeof(err)))
	{
		rtt80 = zbx_http_error_to_RDDS80(ec_http);
		rsm_errf(log_fd, "RDDS80 of \"%s\" (%s) failed: %s (%d)", rdds80_url, formed_url, err, rtt80);
	}

	rsm_infof(log_fd, "end RDDS80 test (rtt:%d)", rtt80);
end:
	if (0 != ISSET_MSG(result))
		rsm_err(log_fd, result->msg);

	rsm_info(log_fd, "END TEST");

	if (SYSINFO_RET_OK == ret && 0 != rdds_enabled)
	{
		int	rdds43_status, rdds80_status;

		switch (zbx_subtest_result(rtt43, rtt_limit))
		{
			case ZBX_SUBTEST_SUCCESS:
				rdds43_status = 1;	/* up */
				break;
			default:	/* ZBX_SUBTEST_FAIL */
				rdds43_status = 0;	/* down */
		}

		switch (zbx_subtest_result(rtt80, rtt_limit))
		{
			case ZBX_SUBTEST_SUCCESS:
				rdds80_status = 1;	/* up */
				break;
			default:	/* ZBX_SUBTEST_FAIL */
				rdds80_status = 0;	/* down */
		}

		create_rdds_json(&json, ip43, rtt43, upd43, rdds43_server, rdds43_testedname, ip80, rtt80, rdds80_url,
				rdds43_status, rdds80_status, (rdds43_status && rdds80_status));

		SET_STR_RESULT(result, zbx_strdup(NULL, json.buffer));

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

	zbx_free(answer);

	if (NULL != log_fd)
		fclose(log_fd);
out:
	zbx_free(scheme);
	zbx_free(domain);
	zbx_free(path);
	zbx_free(formed_url);

	zbx_vector_str_clean_and_destroy(&nss);
	zbx_vector_str_clean_and_destroy(&ips80);
	zbx_vector_str_clean_and_destroy(&ips43);

	return ret;
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

int	check_rsm_rdap(const char *host, const AGENT_REQUEST *request, AGENT_RESULT *result)
{
	ldns_resolver		*res = NULL;
	zbx_resolver_error_t	ec_res;
	curl_data_t		data = {NULL, 0, 0};
	zbx_vector_str_t	ips;
	struct zbx_json_parse	jp;
	FILE			*log_fd;
	char			*rsmhost, *testedname, *base_url, *res_ip, *scheme = NULL,
				*domain = NULL, *path = NULL, *formed_url = NULL, *value_str = NULL,
				err[ZBX_ERR_BUF_SIZE], is_ipv4, query[64];
	const char		*ip = NULL;
	size_t			value_alloc = 0;
	zbx_http_error_t	ec_http;
	uint16_t		resolver_port = DEFAULT_RESOLVER_PORT;
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
	GET_PARAM_NEMPTY(res_ip       , 9, "IP address of local resolver");

	if (SUCCEED != zbx_split_url(base_url, &scheme, &domain, &port, &path, err, sizeof(err)))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "\"%s\": %s", base_url, err));
		goto out;
	}

	/* open log file */
	if (NULL == (log_fd = open_item_log(host, rsmhost, ZBX_RDAP_LOG_PREFIX, err, sizeof(err))))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, err));
		goto out;
	}

	zbx_vector_str_create(&ips);

	rsm_info(log_fd, "START TEST");

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

	/* create resolver */
	if (SUCCEED != zbx_create_resolver(&res, "resolver", res_ip, resolver_port, RSM_TCP, ipv4_enabled, ipv6_enabled,
			RESOLVER_EXTRAS_DNSSEC, RSM_TCP_TIMEOUT, RSM_TCP_RETRY, log_fd, err, sizeof(err)))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot create resolver: %s.", err));
		goto out;
	}

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
	if (SUCCEED != zbx_resolver_resolve_host(res, domain, &ips, ipv_flags, log_fd, &ec_res, err, sizeof(err)))
	{
		rtt = zbx_resolver_error_to_RDAP(ec_res);
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
	ip = ips.values[zbx_random((size_t)ips.values_num)];

	if (SUCCEED != zbx_validate_ip(ip, ipv4_enabled, ipv6_enabled, NULL, &is_ipv4))
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

	if (SUCCEED != zbx_http_test(domain, formed_url, RSM_TCP_TIMEOUT, maxredirs, &ec_http, &rtt, &data,
			curl_memory, curl_flags, err, sizeof(err)))
	{
		rtt = zbx_http_error_to_RDAP(ec_http);
		rsm_errf(log_fd, "test of \"%s\" (%s) failed: %s (%d)", base_url, formed_url, err, rtt);
		goto end;
	}

	rsm_infof(log_fd, "got response ===>\n%.*s\n<===", ZBX_RDDS_PREVIEW_SIZE, ZBX_NULL2STR(data.buf));

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

	rsm_info(log_fd, "END TEST");

	if (SYSINFO_RET_OK == ret && ZBX_NO_VALUE != rtt)
	{
		int		subtest_result;
		struct zbx_json	json;

		switch (zbx_subtest_result(rtt, rtt_limit))
		{
			case ZBX_SUBTEST_SUCCESS:
				subtest_result = 1;	/* up */
				break;
			default:	/* ZBX_SUBTEST_FAIL */
				subtest_result = 0;	/* down */
		}

		create_rdap_json(&json, ip, rtt, base_url, testedname, subtest_result);

		SET_STR_RESULT(result, zbx_strdup(NULL, json.buffer));

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

	zbx_vector_str_clean_and_destroy(&ips);

	fclose(log_fd);
out:
	zbx_free(scheme);
	zbx_free(domain);
	zbx_free(path);
	zbx_free(formed_url);

	return ret;
}

static int	epp_recv_buf(SSL *ssl, void *buf, int num)
{
	void	*p;
	int	read, ret = FAIL;

	if (1 > num)
		goto out;

	p = buf;

	while (0 < num)
	{
		if (0 >= (read = SSL_read(ssl, p, num)))
			goto out;

		p = (char *)p + read;
		num -= read;
	}

	ret = SUCCEED;
out:
	return ret;
}

static int	epp_recv_message(SSL *ssl, char **data, size_t *data_len, FILE *log_fd)
{
	unsigned int	message_size;
	int		ret = FAIL;

	if (NULL == data || NULL != *data)
	{
		THIS_SHOULD_NEVER_HAPPEN;
		exit(EXIT_FAILURE);
	}

	/* receive header */
	if (SUCCEED != epp_recv_buf(ssl, &message_size, sizeof(message_size)))
		goto out;

	*data_len = ntohl(message_size) - sizeof(message_size);
	*data = (char *)malloc(*data_len);

	/* receive body */
	if (SUCCEED != epp_recv_buf(ssl, *data, (int)*data_len - 1))
		goto out;

	(*data)[*data_len - 1] = '\0';

	rsm_infof(log_fd, "received message ===>\n%s\n<===", *data);

	ret = SUCCEED;
out:
	if (SUCCEED != ret && NULL != *data)
	{
		free(*data);
		*data = NULL;
	}

	return ret;
}

static int	epp_send_buf(SSL *ssl, const void *buf, int num)
{
	const void	*p;
	int		written, ret = FAIL;

	if (1 > num)
		goto out;

	p = buf;

	while (0 < num)
	{
		if (0 >= (written = SSL_write(ssl, p, num)))
			goto out;

		p = (const char *)p + written;
		num -= written;
	}

	ret = SUCCEED;
out:
	return ret;
}

static int	epp_send_message(SSL *ssl, const char *data, size_t data_size, FILE *log_fd)
{
	int		ret = FAIL;
	unsigned int	message_size;

	message_size = htonl((unsigned int)(data_size + sizeof(message_size)));

	/* send header */
	if (SUCCEED != epp_send_buf(ssl, &message_size, sizeof(message_size)))
		goto out;

	/* send body */
	if (SUCCEED != epp_send_buf(ssl, data, (int)data_size))
		goto out;

	rsm_infof(log_fd, "sent message ===>\n%s\n<===", data);

	ret = SUCCEED;
out:
	return ret;
}

static int	get_xml_value(const char *data, int xml_path, char *xml_value, size_t xml_value_size)
{
	const char	*p_start, *p_end, *start_tag, *end_tag;
	int		ret = FAIL;

	switch (xml_path)
	{
		case XML_PATH_SERVER_ID:
			start_tag = "<svID>";
			end_tag = "</svID>";
			break;
		case XML_PATH_RESULT_CODE:
			start_tag = "<result code=\"";
			end_tag = "\">";
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			exit(EXIT_FAILURE);
	}

	if (NULL == (p_start = zbx_strcasestr(data, start_tag)))
		goto out;

	p_start += strlen(start_tag);

	if (NULL == (p_end = zbx_strcasestr(p_start, end_tag)))
		goto out;

	zbx_strlcpy(xml_value, p_start, MIN((size_t)(p_end - p_start + 1), xml_value_size));

	ret = SUCCEED;
out:
	return ret;
}

static int	get_tmpl(const char *epp_commands, const char *command, char **tmpl)
{
	char	buf[256];
	size_t	tmpl_alloc = 512, tmpl_offset = 0;
	int	f, nbytes, ret = FAIL;

	zbx_snprintf(buf, sizeof(buf), "%s/%s.tmpl", epp_commands, command);

	if (-1 == (f = zbx_open(buf, O_RDONLY)))
		goto out;

	*tmpl = (char *)zbx_malloc(*tmpl, tmpl_alloc);

	while (0 < (nbytes = zbx_read(f, buf, sizeof(buf), "")))
		zbx_strncpy_alloc(tmpl, &tmpl_alloc, &tmpl_offset, buf, (size_t)nbytes);

	if (-1 == nbytes)
	{
		zbx_free(*tmpl);
		goto out;
	}

	ret = SUCCEED;
out:
	if (-1 != f)
		close(f);

	return ret;
}

static int	get_first_message(SSL *ssl, int *res, FILE *log_fd, const char *epp_serverid, char *err, size_t err_size)
{
	char	xml_value[XML_VALUE_BUF_SIZE], *data = NULL;
	size_t	data_len;
	int	ret = FAIL;

	if (SUCCEED != epp_recv_message(ssl, &data, &data_len, log_fd))
	{
		zbx_strlcpy(err, "cannot receive first message from server", err_size);
		*res = ZBX_EC_EPP_FIRSTTO;
		goto out;
	}

	if (SUCCEED != get_xml_value(data, XML_PATH_SERVER_ID, xml_value, sizeof(xml_value)))
	{
		zbx_snprintf(err, err_size, "no Server ID in first message from server");
		*res = ZBX_EC_EPP_FIRSTINVAL;
		goto out;
	}

	if (0 != strcmp(epp_serverid, xml_value))
	{
		zbx_snprintf(err, err_size, "invalid Server ID in the first message from server: \"%s\""
				" (expected \"%s\")", xml_value, epp_serverid);
		*res = ZBX_EC_EPP_FIRSTINVAL;
		goto out;
	}

	ret = SUCCEED;
out:
	if (NULL != data)
		free(data);

	return ret;
}

static void	zbx_tmpl_replace(char **tmpl, const char *variable, const char *value)
{
	const char	*p;
	size_t		variable_size, l_pos, r_pos;

	variable_size = strlen(variable);

	while (NULL != (p = strstr(*tmpl, variable)))
	{
		l_pos = (size_t)(p - *tmpl);
		r_pos = l_pos + variable_size - 1;

		zbx_replace_string(tmpl, (size_t)(p - *tmpl), &r_pos, value);
	}
}

static int	command_login(const char *epp_commands, const char *name, SSL *ssl, int *rtt, FILE *log_fd,
		const char *epp_user, const char *epp_passwd, char *err, size_t err_size)
{
	char		*tmpl = NULL, xml_value[XML_VALUE_BUF_SIZE], *data = NULL;
	size_t		data_len;
	zbx_timespec_t	start, end;
	int		ret = FAIL;

	if (SUCCEED != get_tmpl(epp_commands, name, &tmpl))
	{
		zbx_snprintf(err, err_size, "cannot load template \"%s\"", name);
		*rtt = ZBX_EC_EPP_INTERNAL_GENERAL;
		goto out;
	}

	zbx_tmpl_replace(&tmpl, "{TMPL_EPP_USER}", epp_user);
	zbx_tmpl_replace(&tmpl, "{TMPL_EPP_PASSWD}", epp_passwd);

	zbx_timespec(&start);

	if (SUCCEED != epp_send_message(ssl, tmpl, strlen(tmpl), log_fd))
	{
		zbx_snprintf(err, err_size, "cannot send command \"%s\"", name);
		*rtt = ZBX_EC_EPP_LOGINTO;
		goto out;
	}

	if (SUCCEED != epp_recv_message(ssl, &data, &data_len, log_fd))
	{
		zbx_snprintf(err, err_size, "cannot receive reply to command \"%s\"", name);
		*rtt = ZBX_EC_EPP_LOGINTO;
		goto out;
	}

	if (SUCCEED != get_xml_value(data, XML_PATH_RESULT_CODE, xml_value, sizeof(xml_value)))
	{
		zbx_snprintf(err, err_size, "no result code in reply");
		*rtt = ZBX_EC_EPP_LOGININVAL;
		goto out;
	}

	if (0 != strcmp(EPP_SUCCESS_CODE_GENERAL, xml_value))
	{
		zbx_snprintf(err, err_size, "invalid result code in reply to \"%s\": \"%s\" (expected \"%s\")",
				name, xml_value, EPP_SUCCESS_CODE_GENERAL);
		*rtt = ZBX_EC_EPP_LOGININVAL;
		goto out;
	}

	zbx_timespec(&end);
	*rtt = (end.sec - start.sec) * 1000 + (end.ns - start.ns) / 1000000;

	ret = SUCCEED;
out:
	zbx_free(data);
	zbx_free(tmpl);

	return ret;
}

static int	command_update(const char *epp_commands, const char *name, SSL *ssl, int *rtt, FILE *log_fd,
		const char *epp_testprefix, const char *domain, char *err, size_t err_size)
{
	char		*tmpl = NULL, xml_value[XML_VALUE_BUF_SIZE], *data = NULL, tsbuf[32], buf[ZBX_HOST_BUF_SIZE];
	size_t		data_len;
	time_t		now;
	zbx_timespec_t	start, end;
	int		ret = FAIL;

	if (SUCCEED != get_tmpl(epp_commands, name, &tmpl))
	{
		zbx_snprintf(err, err_size, "cannot load template \"%s\"", name);
		*rtt = ZBX_EC_EPP_INTERNAL_GENERAL;
		goto out;
	}

	time(&now);
	zbx_snprintf(tsbuf, sizeof(tsbuf), "%llu", (unsigned long long)now);

	zbx_snprintf(buf, sizeof(buf), "%s.%s", epp_testprefix, domain);

	zbx_tmpl_replace(&tmpl, "{TMPL_DOMAIN}", buf);
	zbx_tmpl_replace(&tmpl, "{TMPL_TIMESTAMP}", tsbuf);

	zbx_timespec(&start);

	if (SUCCEED != epp_send_message(ssl, tmpl, strlen(tmpl), log_fd))
	{
		zbx_snprintf(err, err_size, "cannot send command \"%s\"", name);
		*rtt = ZBX_EC_EPP_UPDATETO;
		goto out;
	}

	if (SUCCEED != epp_recv_message(ssl, &data, &data_len, log_fd))
	{
		zbx_snprintf(err, err_size, "cannot receive reply to command \"%s\"", name);
		*rtt = ZBX_EC_EPP_UPDATETO;
		goto out;
	}

	if (SUCCEED != get_xml_value(data, XML_PATH_RESULT_CODE, xml_value, sizeof(xml_value)))
	{
		zbx_snprintf(err, err_size, "no result code in reply");
		*rtt = ZBX_EC_EPP_UPDATEINVAL;
		goto out;
	}

	if (0 != strcmp(EPP_SUCCESS_CODE_GENERAL, xml_value))
	{
		zbx_snprintf(err, err_size, "invalid result code in reply to \"%s\": \"%s\" (expected \"%s\")",
				name, xml_value, EPP_SUCCESS_CODE_GENERAL);
		*rtt = ZBX_EC_EPP_UPDATEINVAL;
		goto out;
	}

	zbx_timespec(&end);
	*rtt = (end.sec - start.sec) * 1000 + (end.ns - start.ns) / 1000000;

	ret = SUCCEED;
out:
	zbx_free(data);
	zbx_free(tmpl);

	return ret;
}

static int	command_info(const char *epp_commands, const char *name, SSL *ssl, int *rtt, FILE *log_fd,
		const char *epp_testprefix, const char *domain, char *err, size_t err_size)
{
	char		*tmpl = NULL, xml_value[XML_VALUE_BUF_SIZE], *data = NULL, buf[ZBX_HOST_BUF_SIZE];
	size_t		data_len;
	zbx_timespec_t	start, end;
	int		ret = FAIL;

	if (SUCCEED != get_tmpl(epp_commands, name, &tmpl))
	{
		zbx_snprintf(err, err_size, "cannot load template \"%s\"", name);
		*rtt = ZBX_EC_EPP_INTERNAL_GENERAL;
		goto out;
	}

	zbx_snprintf(buf, sizeof(buf), "%s.%s", epp_testprefix, domain);

	zbx_tmpl_replace(&tmpl, "{TMPL_DOMAIN}", buf);

	zbx_timespec(&start);

	if (SUCCEED != epp_send_message(ssl, tmpl, strlen(tmpl), log_fd))
	{
		zbx_snprintf(err, err_size, "cannot send command \"%s\"", name);
		*rtt = ZBX_EC_EPP_INFOTO;
		goto out;
	}

	if (SUCCEED != epp_recv_message(ssl, &data, &data_len, log_fd))
	{
		zbx_snprintf(err, err_size, "cannot receive reply to command \"%s\"", name);
		*rtt = ZBX_EC_EPP_INFOTO;
		goto out;
	}

	if (SUCCEED != get_xml_value(data, XML_PATH_RESULT_CODE, xml_value, sizeof(xml_value)))
	{
		zbx_snprintf(err, err_size, "no result code in reply");
		*rtt = ZBX_EC_EPP_INFOINVAL;
		goto out;
	}

	if (0 != strcmp(EPP_SUCCESS_CODE_GENERAL, xml_value))
	{
		zbx_snprintf(err, err_size, "invalid result code in reply to \"%s\": \"%s\" (expected \"%s\")",
				name, xml_value, EPP_SUCCESS_CODE_GENERAL);
		*rtt = ZBX_EC_EPP_INFOINVAL;
		goto out;
	}

	zbx_timespec(&end);
	*rtt = (end.sec - start.sec) * 1000 + (end.ns - start.ns) / 1000000;

	ret = SUCCEED;
out:
	zbx_free(data);
	zbx_free(tmpl);

	return ret;
}

static int	command_logout(const char *epp_commands, const char *name, SSL *ssl, FILE *log_fd, char *err, size_t err_size)
{
	char	*tmpl = NULL, xml_value[XML_VALUE_BUF_SIZE], *data = NULL;
	size_t	data_len;
	int	ret = FAIL;

	if (SUCCEED != get_tmpl(epp_commands, name, &tmpl))
	{
		zbx_snprintf(err, err_size, "cannot load template \"%s\"", name);
		goto out;
	}

	if (SUCCEED != epp_send_message(ssl, tmpl, strlen(tmpl), log_fd))
	{
		zbx_snprintf(err, err_size, "cannot send command \"%s\"", name);
		goto out;
	}

	if (SUCCEED != epp_recv_message(ssl, &data, &data_len, log_fd))
	{
		zbx_snprintf(err, err_size, "cannot receive reply to command \"%s\"", name);
		goto out;
	}

	if (SUCCEED != get_xml_value(data, XML_PATH_RESULT_CODE, xml_value, sizeof(xml_value)))
	{
		zbx_snprintf(err, err_size, "no result code in reply");
		goto out;
	}

	if (0 != strcmp(EPP_SUCCESS_CODE_LOGOUT, xml_value))
	{
		zbx_snprintf(err, err_size, "invalid result code in reply to \"%s\": \"%s\" (expected \"%s\")",
				name, xml_value, EPP_SUCCESS_CODE_LOGOUT);
		goto out;
	}

	ret = SUCCEED;
out:
	zbx_free(data);
	zbx_free(tmpl);

	return ret;
}

static int	zbx_ssl_attach_cert(SSL *ssl, char *cert, int cert_len, int *rtt, char *err, size_t err_size)
{
	BIO	*bio = NULL;
	X509	*x509 = NULL;
	int	ret = FAIL;

	if (NULL == (bio = BIO_new_mem_buf(cert, cert_len)))
	{
		*rtt = ZBX_EC_EPP_INTERNAL_GENERAL;
		zbx_strlcpy(err, "out of memory", err_size);
		goto out;
	}

	if (NULL == (x509 = PEM_read_bio_X509(bio, NULL, NULL, NULL)))
	{
		*rtt = ZBX_EC_EPP_CRYPT;
		zbx_ssl_get_error(err, err_size);
		goto out;
	}

	if (1 != SSL_use_certificate(ssl, x509))
	{
		*rtt = ZBX_EC_EPP_CRYPT;
		zbx_ssl_get_error(err, err_size);
		goto out;
	}

	ret = SUCCEED;
out:
	if (NULL != x509)
		X509_free(x509);

	if (NULL != bio)
		BIO_free(bio);

	return ret;
}

static int	zbx_ssl_attach_privkey(SSL *ssl, char *privkey, size_t privkey_len, int *rtt, char *err, size_t err_size)
{
	BIO	*bio = NULL;
	RSA	*rsa = NULL;
	int	ret = FAIL;

	if (NULL == (bio = BIO_new_mem_buf(privkey, (int)privkey_len)))
	{
		*rtt = ZBX_EC_EPP_INTERNAL_GENERAL;
		zbx_strlcpy(err, "out of memory", err_size);
		goto out;
	}

	if (NULL == (rsa = PEM_read_bio_RSAPrivateKey(bio, NULL, NULL, NULL)))
	{
		*rtt = ZBX_EC_EPP_CRYPT;
		zbx_ssl_get_error(err, err_size);
		goto out;
	}

	if (1 != SSL_use_RSAPrivateKey(ssl, rsa))
	{
		*rtt = ZBX_EC_EPP_CRYPT;
		zbx_ssl_get_error(err, err_size);
		goto out;
	}

	ret = SUCCEED;
out:
	if (NULL != rsa)
		RSA_free(rsa);

	if (NULL != bio)
		BIO_free(bio);

	return ret;
}

static char	*zbx_parse_time(char *str, size_t str_size, int *i)
{
	char	*p_end;
	char	c;
	size_t	block_size = 0;
	int	rv;

	p_end = str;

	while ('\0' != *p_end && block_size++ < str_size)
		p_end++;

	if (str == p_end)
		return NULL;

	c = *p_end;
	*p_end = '\0';

	rv = sscanf(str, "%d", i);
	*p_end = c;

	if (1 != rv)
		return NULL;


	return p_end;
}

static int	zbx_parse_asn1time(ASN1_TIME *asn1time, time_t *time, char *err, size_t err_size)
{
	struct tm	tm;
	char		buf[15], *p;
	int		ret = FAIL;

	if (V_ASN1_UTCTIME == asn1time->type && 13 == asn1time->length && 'Z' == asn1time->data[12])
	{
		memcpy(buf + 2, asn1time->data, (size_t)asn1time->length - 1);

		if ('5' <= asn1time->data[0])
		{
			buf[0] = '1';
			buf[1] = '9';
		}
		else
		{
			buf[0] = '2';
			buf[1] = '0';
		}
	}
	else if (V_ASN1_GENERALIZEDTIME == asn1time->type && 15 == asn1time->length && 'Z' == asn1time->data[14])
	{
		memcpy(buf, asn1time->data, (size_t)asn1time->length - 1);
	}
	else
	{
		zbx_strlcpy(err, "unknown date format", err_size);
		goto out;
	}

	buf[14] = '\0';

	memset(&tm, 0, sizeof(tm));

	/* year */
	if (NULL == (p = zbx_parse_time(buf, 4, &tm.tm_year)) || '\0' == *p)
	{
		zbx_strlcpy(err, "invalid year", err_size);
		goto out;
	}

	/* month */
	if (NULL == (p = zbx_parse_time(p, 2, &tm.tm_mon)) || '\0' == *p)
	{
		zbx_strlcpy(err, "invalid month", err_size);
		goto out;
	}

	/* day of month */
	if (NULL == (p = zbx_parse_time(p, 2, &tm.tm_mday)) || '\0' == *p)
	{
		zbx_strlcpy(err, "invalid day of month", err_size);
		goto out;
	}

	/* hours */
	if (NULL == (p = zbx_parse_time(p, 2, &tm.tm_hour)) || '\0' == *p)
	{
		zbx_strlcpy(err, "invalid hours", err_size);
		goto out;
	}

	/* minutes */
	if (NULL == (p = zbx_parse_time(p, 2, &tm.tm_min)) || '\0' == *p)
	{
		zbx_strlcpy(err, "invalid minutes", err_size);
		goto out;
	}

	/* seconds */
	if (NULL == (p = zbx_parse_time(p, 2, &tm.tm_sec)) || '\0' != *p)
	{
		zbx_strlcpy(err, "invalid seconds", err_size);
		goto out;
	}

	tm.tm_year -= 1900;
	tm.tm_mon -= 1;

	*time = timegm(&tm);

	ret = SUCCEED;
out:
	return ret;
}

static int	zbx_get_cert_md5(X509 *cert, char **md5, char *err, size_t err_size)
{
	char		*data;
	BIO		*bio;
	long		len;
	size_t		sz, i;
	md5_state_t	state;
	md5_byte_t	hash[MD5_DIGEST_SIZE];
	int		ret = FAIL;

	if (NULL == (bio = BIO_new(BIO_s_mem())))
	{
		zbx_strlcpy(err, "out of memory", err_size);
		goto out;
	}

	if (1 != PEM_write_bio_X509(bio, cert))
	{
		zbx_strlcpy(err, "internal OpenSSL error while parsing server certificate", err_size);
		goto out;
	}

	len = BIO_get_mem_data(bio, &data);	/* "data" points to the cert data (no need to free), len - its length */

	zbx_md5_init(&state);
	zbx_md5_append(&state, (const md5_byte_t *)data, (int)len);
	zbx_md5_finish(&state, hash);

	sz = MD5_DIGEST_SIZE * 2 + 1;
	*md5 = (char *)zbx_malloc(*md5, sz);

	for (i = 0; i < MD5_DIGEST_SIZE; i++)
		zbx_snprintf(&(*md5)[i << 1], sz - (i << 1), "%02x", hash[i]);

	ret = SUCCEED;
out:
	if (NULL != bio)
		BIO_free(bio);

	return ret;
}

static int	zbx_validate_cert(X509 *cert, const char *md5_macro, int *rtt, char *err, size_t err_size)
{
	time_t	now, not_before, not_after;
	char	*md5 = NULL;
	int	ret = FAIL;

	/* get certificate validity dates */
	if (SUCCEED != zbx_parse_asn1time(X509_get_notBefore(cert), &not_before, err, err_size))
	{
		*rtt = ZBX_EC_EPP_SERVERCERT;
		goto out;
	}

	if (SUCCEED != zbx_parse_asn1time(X509_get_notAfter(cert), &not_after, err, err_size))
	{
		*rtt = ZBX_EC_EPP_SERVERCERT;
		goto out;
	}

	now = time(NULL);
	if (now > not_after)
	{
		*rtt = ZBX_EC_EPP_SERVERCERT;
		zbx_strlcpy(err, "the certificate has expired", err_size);
		goto out;
	}

	if (now < not_before)
	{
		*rtt = ZBX_EC_EPP_SERVERCERT;
		zbx_strlcpy(err, "the validity date is in the future", err_size);
		goto out;
	}

	if (SUCCEED != zbx_get_cert_md5(cert, &md5, err, err_size))
	{
		*rtt = ZBX_EC_EPP_INTERNAL_GENERAL;
		goto out;
	}

	if (0 != strcmp(md5_macro, md5))
	{
		*rtt = ZBX_EC_EPP_SERVERCERT;
		zbx_snprintf(err, err_size, "MD5 sum set in a macro (%s) differs from what we got (%s)", md5_macro, md5);
		goto out;
	}

	ret = SUCCEED;
out:
	zbx_free(md5);

	return ret;
}

int	check_rsm_epp(const char *host, const AGENT_REQUEST *request, AGENT_RESULT *result)
{
	ldns_resolver		*res = NULL;
	zbx_resolver_error_t	ec_res;
	char			*domain, err[ZBX_ERR_BUF_SIZE], *value_str = NULL, *res_ip = NULL,
				*secretkey_enc_b64 = NULL, *secretkey_salt_b64 = NULL, *epp_passwd_enc_b64 = NULL,
				*epp_passwd_salt_b64 = NULL, *epp_privkey_enc_b64 = NULL, *epp_privkey_salt_b64 = NULL,
				*epp_user = NULL, *epp_passwd = NULL, *epp_privkey = NULL, *epp_cert_b64 = NULL,
				*epp_cert = NULL, *epp_commands = NULL, *epp_serverid = NULL, *epp_testprefix = NULL,
				*epp_servercertmd5 = NULL;
	unsigned short		epp_port = 700;
	X509			*epp_server_x509 = NULL;
	const SSL_METHOD	*method;
	const char		*ip = NULL, *random_host;
	SSL_CTX			*ctx = NULL;
	SSL			*ssl = NULL;
	FILE			*log_fd = NULL;
	zbx_socket_t		sock;
	zbx_vector_str_t	epp_hosts, epp_ips;
	unsigned int		extras;
	uint16_t		resolver_port = DEFAULT_RESOLVER_PORT;
	int			rv, epp_cert_size, rtt, rtt1 = ZBX_NO_VALUE, rtt2 = ZBX_NO_VALUE,
				rtt3 = ZBX_NO_VALUE, ipv4_enabled = 0, ipv6_enabled = 0, ret = SYSINFO_RET_FAIL;

	zbx_vector_str_create(&epp_hosts);
	zbx_vector_str_create(&epp_ips);

	if (2 != request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "item must contain 2 parameters"));
		return SYSINFO_RET_FAIL;
	}

	domain = get_rparam(request, 0);

	if ('\0' == *domain)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "first parameter missing"));
		return SYSINFO_RET_FAIL;
	}

	/* open log file */
	if (NULL == (log_fd = open_item_log(host, domain, ZBX_EPP_LOG_PREFIX, err, sizeof(err))))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, err));
		return SYSINFO_RET_FAIL;
	}

	rsm_info(log_fd, "START TEST");

	if ('\0' == *epp_passphrase)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "EPP passphrase was not provided when starting proxy"
				" (restart proxy with --rsm option)"));
		goto out;
	}

	/* get EPP servers list */
	value_str = zbx_strdup(value_str, get_rparam(request, 1));

	if ('\0' == *value_str)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "second key parameter missing"));
		goto out;
	}

	zbx_get_strings_from_list(&epp_hosts, value_str, ',');

	if (0 == epp_hosts.values_num)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "cannot get EPP hosts from key parameter"));
		goto out;
	}

	/* TODO: make sure the service is enabled on TLD and Probe */

	/* TODO: get certificate, service ID, RT MD5, password and salt, client */
	/* private key and salt, EPP passphrase and salt and other things */

	/* TODO: find out if RESOLVER_EXTRAS_DNSSEC is correct choice */
	extras = RESOLVER_EXTRAS_DNSSEC;

	/* create resolver */
	if (SUCCEED != zbx_create_resolver(&res, "resolver", res_ip, resolver_port, RSM_TCP, ipv4_enabled, ipv6_enabled,
			extras, RSM_TCP_TIMEOUT, RSM_TCP_RETRY, log_fd, err, sizeof(err)))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "cannot create resolver: %s", err));
		goto out;
	}

	/* from this point item will not become NOTSUPPORTED */
	ret = SYSINFO_RET_OK;

	if (SUCCEED != rsm_ssl_init())
	{
		rtt1 = rtt2 = rtt3 = ZBX_EC_EPP_INTERNAL_GENERAL;
		rsm_err(log_fd, "cannot initialize SSL library");
		goto out;
	}

	/* set SSLv2 client hello, also announce SSLv3 and TLSv1 */
	method = SSLv23_client_method();

	/* create a new SSL context */
	if (NULL == (ctx = SSL_CTX_new(method)))
	{
		rtt1 = rtt2 = rtt3 = ZBX_EC_EPP_INTERNAL_GENERAL;
		rsm_err(log_fd, "cannot create a new SSL context structure");
		goto out;
	}

	/* disabling SSLv2 will leave v3 and TSLv1 for negotiation */
	SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2);

	/* create new SSL connection state object */
	if (NULL == (ssl = SSL_new(ctx)))
	{
		rtt1 = rtt2 = rtt3 = ZBX_EC_EPP_INTERNAL_GENERAL;
		rsm_err(log_fd, "cannot create a new SSL context structure");
		goto out;
	}

	/* choose random host */
	random_host = epp_hosts.values[zbx_random((size_t)epp_hosts.values_num)];

	/* resolve host to ips: TODO! error handler functions not implemented (see NULLs below) */
	if (SUCCEED != zbx_resolver_resolve_host(res, random_host, &epp_ips,
			(0 != ipv4_enabled ? ZBX_FLAG_IPV4_ENABLED : 0) | (0 != ipv6_enabled ? ZBX_FLAG_IPV6_ENABLED : 0),
			log_fd, &ec_res, err, sizeof(err)))
	{
		rtt1 = rtt2 = rtt3 = (ZBX_RESOLVER_NOREPLY != ec_res ? ZBX_EC_EPP_NO_IP : ZBX_EC_EPP_INTERNAL_GENERAL);
		rsm_errf(log_fd, "\"%s\": %s", random_host, err);
		goto out;
	}

	zbx_delete_unsupported_ips(&epp_ips, ipv4_enabled, ipv6_enabled);

	if (0 == epp_ips.values_num)
	{
		rtt1 = rtt2 = rtt3 = ZBX_EC_RDAP_INTERNAL_IP_UNSUP;
		rsm_errf(log_fd, "EPP \"%s\": IP address(es) of host not supported by this probe", random_host);
		goto out;
	}

	/* choose random IP */
	ip = epp_ips.values[zbx_random((size_t)epp_ips.values_num)];

	/* make the underlying TCP socket connection */
	if (SUCCEED != zbx_tcp_connect(&sock, NULL, ip, epp_port, RSM_TCP_TIMEOUT,
			ZBX_TCP_SEC_UNENCRYPTED, NULL, NULL))
	{
		rtt1 = rtt2 = rtt3 = ZBX_EC_EPP_CONNECT;
		rsm_errf(log_fd, "cannot connect to EPP server %s:%d", ip, epp_port);
		goto out;
	}

	/* attach the socket descriptor to SSL session */
	if (1 != SSL_set_fd(ssl, sock.socket))
	{
		rtt1 = rtt2 = rtt3 = ZBX_EC_EPP_INTERNAL_GENERAL;
		rsm_err(log_fd, "cannot attach TCP socket to SSL session");
		goto out;
	}

	str_base64_decode_dyn(epp_cert_b64, (int)strlen(epp_cert_b64), &epp_cert, &epp_cert_size);

	if (SUCCEED != zbx_ssl_attach_cert(ssl, epp_cert, epp_cert_size, &rtt, err, sizeof(err)))
	{
		rtt1 = rtt2 = rtt3 = rtt;
		rsm_errf(log_fd, "cannot attach client certificate to SSL session: %s", err);
		goto out;
	}

	if (SUCCEED != decrypt_ciphertext(epp_passphrase, strlen(epp_passphrase), secretkey_enc_b64,
			strlen(secretkey_enc_b64), secretkey_salt_b64, strlen(secretkey_salt_b64), epp_privkey_enc_b64,
			strlen(epp_privkey_enc_b64), epp_privkey_salt_b64, strlen(epp_privkey_salt_b64), &epp_privkey,
			err, sizeof(err)))
	{
		rtt1 = rtt2 = rtt3 = ZBX_EC_EPP_INTERNAL_GENERAL;
		rsm_errf(log_fd, "cannot decrypt client private key: %s", err);
		goto out;
	}

	rv = zbx_ssl_attach_privkey(ssl, epp_privkey, strlen(epp_privkey), &rtt, err, sizeof(err));

	memset(epp_privkey, 0, strlen(epp_privkey));
	zbx_free(epp_privkey);

	if (SUCCEED != rv)
	{
		rtt1 = rtt2 = rtt3 = rtt;
		rsm_errf(log_fd, "cannot attach client private key to SSL session: %s", err);
		goto out;
	}

	/* try to SSL-connect, returns 1 on success */
	if (1 != SSL_connect(ssl))
	{
		rtt1 = rtt2 = rtt3 = ZBX_EC_EPP_INTERNAL_GENERAL;
		zbx_ssl_get_error(err, sizeof(err));
		rsm_errf(log_fd, "cannot build an SSL connection to %s:%d: %s", ip, epp_port, err);
		goto out;
	}

	/* get the remote certificate into the X509 structure */
	if (NULL == (epp_server_x509 = SSL_get_peer_certificate(ssl)))
	{
		rtt1 = rtt2 = rtt3 = ZBX_EC_EPP_SERVERCERT;
		rsm_errf(log_fd, "cannot get Server certificate from %s:%d", ip, epp_port);
		goto out;
	}

	if (SUCCEED != zbx_validate_cert(epp_server_x509, epp_servercertmd5, &rtt, err, sizeof(err)))
	{
		rtt1 = rtt2 = rtt3 = rtt;
		rsm_errf(log_fd, "Server certificate validation failed: %s", err);
		goto out;
	}

	rsm_info(log_fd, "Server certificate validation successful");

	rsm_infof(log_fd, "start EPP test (ip %s)", ip);

	if (SUCCEED != get_first_message(ssl, &rv, log_fd, epp_serverid, err, sizeof(err)))
	{
		rtt1 = rtt2 = rtt3 = rv;
		rsm_err(log_fd, err);
		goto out;
	}

	if (SUCCEED != decrypt_ciphertext(epp_passphrase, strlen(epp_passphrase), secretkey_enc_b64,
			strlen(secretkey_enc_b64), secretkey_salt_b64, strlen(secretkey_salt_b64), epp_passwd_enc_b64,
			strlen(epp_passwd_enc_b64), epp_passwd_salt_b64, strlen(epp_passwd_salt_b64), &epp_passwd,
			err, sizeof(err)))
	{
		rtt1 = rtt2 = rtt3 = ZBX_EC_EPP_INTERNAL_GENERAL;
		rsm_errf(log_fd, "cannot decrypt EPP password: %s", err);
		goto out;
	}

	rv = command_login(epp_commands, COMMAND_LOGIN, ssl, &rtt1, log_fd, epp_user, epp_passwd, err, sizeof(err));

	memset(epp_passwd, 0, strlen(epp_passwd));
	zbx_free(epp_passwd);

	if (SUCCEED != rv)
	{
		rtt2 = rtt3 = rtt1;
		rsm_err(log_fd, err);
		goto out;
	}

	if (SUCCEED != command_update(epp_commands, COMMAND_UPDATE, ssl, &rtt2, log_fd, epp_testprefix, domain,
			err, sizeof(err)))
	{
		rtt3 = rtt2;
		rsm_err(log_fd, err);
		goto out;
	}

	if (SUCCEED != command_info(epp_commands, COMMAND_INFO, ssl, &rtt3, log_fd, epp_testprefix, domain, err,
			sizeof(err)))
	{
		rsm_err(log_fd, err);
		goto out;
	}

	/* logout command errors should not affect the test results */
	if (SUCCEED != command_logout(epp_commands, COMMAND_LOGOUT, ssl, log_fd, err, sizeof(err)))
		rsm_err(log_fd, err);

	rsm_infof(log_fd, "end EPP test (ip %s):SUCCESS", ip);
out:
	if (0 != ISSET_MSG(result))
	{
		rsm_err(log_fd, result->msg);
	}
	else
	{
		/* TODO: save result: ip, rtt1, rtt2, rtt3 */
	}

	rsm_info(log_fd, "END TEST");

	zbx_free(epp_servercertmd5);
	zbx_free(epp_testprefix);
	zbx_free(epp_serverid);
	zbx_free(epp_commands);
	zbx_free(epp_user);
	zbx_free(epp_cert);
	zbx_free(epp_cert_b64);
	zbx_free(epp_privkey_salt_b64);
	zbx_free(epp_privkey_enc_b64);
	zbx_free(epp_passwd_salt_b64);
	zbx_free(epp_passwd_enc_b64);
	zbx_free(secretkey_salt_b64);
	zbx_free(secretkey_enc_b64);

	if (NULL != epp_server_x509)
		X509_free(epp_server_x509);

	if (NULL != ssl)
	{
		SSL_shutdown(ssl);
		SSL_free(ssl);
	}

	if (NULL != ctx)
		SSL_CTX_free(ctx);

	zbx_tcp_close(&sock);

	zbx_free(value_str);
	zbx_free(res_ip);

	zbx_vector_str_clean_and_destroy(&epp_ips);
	zbx_vector_str_clean_and_destroy(&epp_hosts);

	if (NULL != log_fd)
		fclose(log_fd);

	return ret;
}

#define	CHECK_DNS_CONN_RRSIGS		0x1u
#define	CHECK_DNS_CONN_RTT		0x2u
#define	CHECK_DNS_CONN_RECURSIVE	0x4u

static int	zbx_check_dns_connection(const ldns_resolver *res, ldns_rdf *query_rdf, unsigned int flags,
		int reply_ms, FILE *log_fd, char *err, size_t err_size)
{
	ldns_pkt	*pkt = NULL;
	ldns_rr_list	*rrset = NULL;
	uint16_t	query_flags = 0;
	int		ret = FAIL;

	if (0 != (flags & CHECK_DNS_CONN_RECURSIVE))
		query_flags = LDNS_RD;

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

	if (0 != (flags & CHECK_DNS_CONN_RRSIGS) &&
			NULL == (rrset = ldns_pkt_rr_list_by_type(pkt, LDNS_RR_TYPE_RRSIG, LDNS_SECTION_ANSWER)))
	{
		zbx_strlcpy(err, "no RRSIG records found", err_size);
		goto out;
	}

	if (0 != (flags & CHECK_DNS_CONN_RTT) && ldns_pkt_querytime(pkt) > (uint32_t)reply_ms)
	{
		zbx_snprintf(err, err_size, "query RTT %d over limit (%d)", ldns_pkt_querytime(pkt), reply_ms);
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

int	check_rsm_probe_status(const char *host, const AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char			err[ZBX_ERR_BUF_SIZE], ips4_init = 0, ips6_init = 0;
	const char		*ip;
	zbx_vector_str_t	ips4, ips6;
	ldns_resolver		*res = NULL;
	ldns_rdf		*query_rdf = NULL;
	FILE			*log_fd = NULL;
	unsigned int		extras = RESOLVER_EXTRAS_DNSSEC;
	uint16_t		resolver_port = DEFAULT_RESOLVER_PORT;
	int			i, ipv4_enabled = 0, ipv6_enabled = 0, ipv4_min_servers, ipv6_min_servers, ipv4_reply_ms,
				ipv6_reply_ms, online_delay, ok_servers, ret;
	char			*check_mode, *ipv4_rootservers, *ipv6_rootservers, status = ZBX_EC_PROBE_UNSUPPORTED;

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
		return SYSINFO_RET_FAIL;
	}

	/* open probestatus log file */
	if (NULL == (log_fd = open_item_log(host, NULL, ZBX_PROBESTATUS_LOG_PREFIX, err, sizeof(err))))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, err));
		return SYSINFO_RET_FAIL;
	}

	rsm_info(log_fd, "START TEST");

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

		zbx_vector_str_create(&ips4);
		ips4_init = 1;

		if ('\0' != (c = zbx_validate_host_list(ipv4_rootservers, ',')))
		{
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "invalid character in IPv4 root servers list: %c", c));
			goto out;
		}

		zbx_get_strings_from_list(&ips4, ipv4_rootservers, ',');

		ok_servers = 0;

		for (i = 0; i < ips4.values_num; i++)
		{
			ip = ips4.values[i];

			if (SUCCEED != zbx_create_resolver(&res, "root server", ip, resolver_port, RSM_UDP, ipv4_enabled,
					ipv6_enabled, extras, RSM_UDP_TIMEOUT, RSM_UDP_RETRY, log_fd, err, sizeof(err)))
			{
				SET_MSG_RESULT(result, zbx_dsprintf(NULL, "cannot instantiate LDNS resolver: %s", err));
				goto out;
			}

			if (SUCCEED == zbx_check_dns_connection(res, query_rdf,
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
			status = ZBX_EC_PROBE_OFFLINE;
			goto out;
		}
	}

	if (0 != ipv6_enabled)
	{
		char	c;

		zbx_vector_str_create(&ips6);
		ips6_init = 1;

		if ('\0' != (c = zbx_validate_host_list(ipv6_rootservers, ',')))
		{
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "invalid character in IPv6 root servers list: %c", c));
			goto out;
		}

		zbx_get_strings_from_list(&ips6, ipv6_rootservers, ',');

		ok_servers = 0;

		for (i = 0; i < ips6.values_num; i++)
		{
			ip = ips6.values[i];

			if (SUCCEED != zbx_create_resolver(&res, "root server", ip, resolver_port, RSM_UDP, ipv4_enabled,
					ipv6_enabled, extras, RSM_UDP_TIMEOUT, RSM_UDP_RETRY, log_fd, err, sizeof(err)))
			{
				SET_MSG_RESULT(result, zbx_dsprintf(NULL, "cannot instantiate LDNS resolver: %s", err));
				goto out;
			}

			if (SUCCEED == zbx_check_dns_connection(res, query_rdf,
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
			status = ZBX_EC_PROBE_OFFLINE;
			goto out;
		}
	}

	status = ZBX_EC_PROBE_ONLINE;
out:
	if (0 != ISSET_MSG(result))
		rsm_err(log_fd, result->msg);

	/* The value @online_delay controlls how many seconds must the check be successful in order */
	/* to switch from OFFLINE to ONLINE. This is why we keep last online time in the cache.     */
	if (ZBX_EC_PROBE_UNSUPPORTED != status)
	{
		ret = SYSINFO_RET_OK;

		if (ZBX_EC_PROBE_OFFLINE == status)
		{
			DCset_probe_online_since(0);
		}
		else if (ZBX_EC_PROBE_ONLINE == status && ZBX_EC_PROBE_OFFLINE == DCget_probe_last_status())
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
					status = ZBX_EC_PROBE_OFFLINE;
				}
				else
				{
					rsm_warnf(log_fd, "probe status successful for % seconds, changing to ONLINE",
							now - probe_online_since);
				}
			}
		}

		SET_UI64_RESULT(result, status);
	}
	else
	{
		ret = SYSINFO_RET_FAIL;
		DCset_probe_online_since(0);
	}

	DCset_probe_last_status(status);

	rsm_info(log_fd, "END TEST");

	if (NULL != res)
	{
		if (0 != ldns_resolver_nameserver_count(res))
			ldns_resolver_deep_free(res);
		else
			ldns_resolver_free(res);
	}

	if (0 != ips6_init)
		zbx_vector_str_clean_and_destroy(&ips6);

	if (0 != ips4_init)
		zbx_vector_str_clean_and_destroy(&ips4);

	if (NULL != query_rdf)
		ldns_rdf_deep_free(query_rdf);

	if (NULL != log_fd)
		fclose(log_fd);

	return ret;
}

int	check_rsm_resolver_status(const char *host, const AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char		*res_ip, err[ZBX_ERR_BUF_SIZE];
	ldns_resolver	*res = NULL;
	ldns_rdf	*query_rdf = NULL;
	FILE		*log_fd = NULL;
	unsigned int	extras;
	uint16_t	resolver_port;
	int		timeout, tries, ipv4_enabled, ipv6_enabled, status = 0, ret = SYSINFO_RET_FAIL;

	if (5 != request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
		goto out;
	}

	/* TLD goes first, then RDAP specific parameters, then TLD options, probe options and global settings */
	GET_PARAM_NEMPTY(res_ip      , 0, "IP address of local resolver");
	GET_PARAM_UINT  (timeout     , 1, "timeout in seconds");
	GET_PARAM_UINT  (tries       , 2, "maximum number of tries");
	GET_PARAM_UINT  (ipv4_enabled, 3, "IPv4 enabled");
	GET_PARAM_UINT  (ipv6_enabled, 4, "IPv6 enabled");

	/* open log file */
	if (NULL == (log_fd = open_item_log(host, NULL, ZBX_RESOLVERSTATUS_LOG_PREFIX, err, sizeof(err))))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, err));
		goto out;
	}

	extras = RESOLVER_EXTRAS_DNSSEC;

	/* create resolver */
	if (SUCCEED != zbx_create_resolver(&res, "resolver", res_ip, resolver_port, RSM_UDP, ipv4_enabled, ipv6_enabled,
			extras, RSM_UDP_TIMEOUT, RSM_UDP_RETRY, log_fd, err, sizeof(err)))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot create resolver: %s.", err));
		goto end;
	}

	/* create query to check the connection */
	if (NULL == (query_rdf = ldns_rdf_new_frm_str(LDNS_RDF_TYPE_DNAME, ".")))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "cannot create DNS request"));
		goto end;
	}

	/* from this point item will not become NOTSUPPORTED */
	ret = SYSINFO_RET_OK;

	rsm_info(log_fd, "START TEST");

	rsm_infof(log_fd, "IPv4:%s IPv6:%s", 0 == ipv4_enabled ? "DISABLED" : "ENABLED",
			0 == ipv6_enabled ? "DISABLED" : "ENABLED");

	while (tries--)
	{
		if (SUCCEED == zbx_check_dns_connection(res, query_rdf, CHECK_DNS_CONN_RECURSIVE, 0, log_fd,
				err, sizeof(err)))
		{
			break;
		}

		if (!tries)
		{
			rsm_errf(log_fd, "dns check of local resolver %s failed: %s", res_ip, err);
			goto end;
		}

		/* will try again */
		rsm_errf(log_fd, "dns check of local resolver %s failed: %s, will try %d more time%s",
				res_ip, err, tries, (tries == 1 ? "" : "s"));
	}

	status = 1;
end:
	if (0 != ISSET_MSG(result))
		rsm_err(log_fd, result->msg);

	if (SYSINFO_RET_OK == ret)
	{
		rsm_infof(log_fd, "status of \"%s\": %d", res_ip, status);

		rsm_info(log_fd, "END TEST");

		SET_UI64_RESULT(result, status);

		/* knock-down the probe if local resolver non-functional */
		if (0 == status)
			zbx_dc_rsm_errors_inc();
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

	if (NULL != log_fd)
		fclose(log_fd);
out:
	return ret;
}
