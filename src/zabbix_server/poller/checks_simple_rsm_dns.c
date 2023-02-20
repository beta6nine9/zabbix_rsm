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

#include "threads.h"
#include "log.h"
#include "checks_simple_rsm.h"

#define LDNS_EDNS_NSID		3	/* NSID option code, from RFC5001 */
#define NSID_MAX_LENGTH		127	/* hex representation of NSID must fit into 255 characters */

#define DEFAULT_NAMESERVER_PORT	53

#define PACK_NUM_VARS	5
#define PACK_FORMAT	ZBX_FS_SIZE_T "|" ZBX_FS_SIZE_T "|%d|%d|%s"

#define METADATA_FILE_PREFIX	"/tmp/dns-test-metadata"	/* /tmp/dns-test-metadata-<TLD>.bin */

typedef struct
{
	char		*ip;
	unsigned short	port;
	int		rtt;
	int		upd;
	char		*nsid;
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

static void	zbx_get_owners(const ldns_rr_list *rr_list, zbx_vector_ptr_t *owners)
{
	size_t	i, count;

	count = ldns_rr_list_rr_count(rr_list);

	for (i = 0; i < count; i++)
	{
		int		j;
		ldns_rdf	*owner = ldns_rr_owner(ldns_rr_list_rr(rr_list, i));

		if (owner == NULL)
			continue;

		for (j = 0; j < owners->values_num; j++)
		{
			if (ldns_rdf_compare(owner, (const ldns_rdf *)owners->values[j]))
				break;
		}

		if (j == owners->values_num)
			zbx_vector_ptr_append(owners, ldns_rdf_clone(owner));
	}
}

static void	zbx_destroy_owners(zbx_vector_ptr_t *owners)
{
	int	i;

	for (i = 0; i < owners->values_num; i++)
		ldns_rdf_deep_free((ldns_rdf *)owners->values[i]);

	zbx_vector_ptr_destroy(owners);
}

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

static void	extract_nsid(ldns_rdf *edns_data, char **nsid)
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

	/* we want to override the previous ZBX_EC_DNSSEC_RRSIG_NOT_SIGNED error with this one, if it fails */
	if (SUCCEED == ret || ZBX_EC_DNSSEC_RRSIG_NOT_SIGNED == *dnssec_ec)
	{
		char			err2[ZBX_ERR_BUF_SIZE];
		zbx_dnssec_error_t	dnssec_ec2;

		if (SUCCEED != zbx_verify_denial_of_existence(pkt, &dnssec_ec2, err2, sizeof(err2)))
		{
			zbx_strlcpy(err, err2, err_size);
			*dnssec_ec = dnssec_ec2;
			ret = FAIL;
		}
	}

	return ret;
}

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

#define DNS_PROTO(RES)	ldns_resolver_usevc(RES) ? RSM_TCP : RSM_UDP

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

			rr = ldns_rr_list_rr(nsset, rsm_random(ldns_rr_list_rr_count(nsset)));
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
						"Unix timestamp of %s is in the future (current: %ld)",
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
		int ipv6_enabled, unsigned short default_port, FILE *log_fd, char *err, size_t err_size)
{
	char		*ns, *ip, *ns_next, ip_buf[INTERFACE_IP_LEN_MAX];
	size_t		i, j, nss_alloc = 0;
	zbx_ns_t	*ns_entry;
	unsigned short	port;

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
					", expected \"<NS>,<IP>[;<PORT>]\"",
					ns);
			return FAIL;
		}

		*ip = '\0';
		ip++;

		get_host_and_port_from_str(ip, ';', ip_buf, sizeof(ip_buf), &port, default_port);

		if (SUCCEED != zbx_validate_ip(ip_buf, ipv4_enabled, ipv6_enabled, NULL, NULL))
		{
			rsm_warnf(log_fd, "unsupported IP address \"%s\" in the list of name servers, ignored", ip_buf);
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
				if (0 == strcmp(ns_entry->ips[j].ip, ip) && ns_entry->ips[j].port == port)
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

		ns_entry->ips[ns_entry->ips_num].ip = zbx_strdup(NULL, ip_buf);
		ns_entry->ips[ns_entry->ips_num].port = port;
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
FILE	*open_item_log(const char *host, const char *tld, const char *name, char *err, size_t err_size)
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

/**
 * previously, there was only DNS status (0/1) of the Name Server:
 *
 * 0: DNS_DOWN
 * 1: DNS_UP
 *
 * later we added DNSSEC status and the values changed:
 *
 * value | DNS status | DNSSEC status
 * ------|------------|--------------
 *  0    | Old Down   |
 *  1    | Old Up     |
 *  2    | Down       | Disabled
 *  3    | Down       | Down
 *  4    | Down       | Up
 *  5    | Up         | Disabled
 * -5----|-Up---------|-Down-           <-- not possible
 *  6    | Up         | Up
 */

#define DNS_DOWN_DNSSEC_DISABLED	2;
#define DNS_DOWN_DNSSEC_DOWN		3;
#define DNS_DOWN_DNSSEC_UP		4;
#define DNS_UP_DNSSEC_DISABLED		5;
#define DNS_UP_DNSSEC_UP		6;

static void	set_dns_test_results(zbx_ns_t *nss, size_t nss_num, int rtt_limit, unsigned int minns,
		unsigned int *dns_status, unsigned int *dnssec_status, FILE *log_fd)
{
	unsigned int	dns_nssok = 0, dnssec_nssok = 0;
	size_t		i, j;

	for (i = 0; i < nss_num; i++)
	{
		int	ip_dns_result = SUCCEED, ip_dnssec_result = SUCCEED;

		for (j = 0; j < nss[i].ips_num; j++)
		{
			/* if a single IP of the Name Server fails, consider the whole Name Server down */
			if (RSM_SUBTEST_SUCCESS != rsm_subtest_result(nss[i].ips[j].rtt, rtt_limit))
				ip_dns_result = FAIL;

			if (dnssec_status != NULL && (
					(ZBX_EC_DNS_UDP_DNSSEC_FIRST >= nss[i].ips[j].rtt &&
						nss[i].ips[j].rtt >= ZBX_EC_DNS_UDP_DNSSEC_LAST) ||
					(ZBX_EC_DNS_TCP_DNSSEC_FIRST >= nss[i].ips[j].rtt &&
						nss[i].ips[j].rtt >= ZBX_EC_DNS_TCP_DNSSEC_LAST)
			))
			{
				ip_dnssec_result = FAIL;	/* this name server failed dnssec check */
			}
		}

		/* Name Server status (minding all its IPs) */
		if (ip_dns_result == FAIL)
		{
			/* DNS DOWN, DNSSEC varies */
			if (dnssec_status == NULL)
			{
				nss[i].result = DNS_DOWN_DNSSEC_DISABLED;
			}
			else if (ip_dnssec_result == FAIL)
			{
				nss[i].result = DNS_DOWN_DNSSEC_DOWN;
			}
			else
				nss[i].result = DNS_DOWN_DNSSEC_UP;
		}
		else
		{
			/* DNS UP, DNSSEC varies */
			dns_nssok++;

			if (dnssec_status == NULL)
			{
				nss[i].result = DNS_UP_DNSSEC_DISABLED;
			}
			else
				nss[i].result = DNS_UP_DNSSEC_UP;
		}

		if (dnssec_status != NULL)
		{
			if (SUCCEED == ip_dnssec_result)
			{
				rsm_infof(log_fd, "%s: DNSSEC OK", nss[i].name);
				dnssec_nssok++;
			}
			else
				rsm_infof(log_fd, "%s: DNSSEC failed", nss[i].name);
		}
	}

	*dns_status = (dns_nssok >= minns ? 1 : 0);

	if (dnssec_status != NULL)
		*dnssec_status = (dnssec_nssok >= minns ? 1 : 0);
}

static void	create_dns_json(struct zbx_json *json, zbx_ns_t *nss, size_t nss_num, unsigned int current_mode,
		unsigned int dns_status, const unsigned int *dnssec_status, char protocol, const char *testedname)
{
	size_t	i, j;

	zbx_json_init(json, 2 * ZBX_KIBIBYTE);

	zbx_json_addarray(json, "nsips");

	for (i = 0; i < nss_num; i++)
	{
		for (j = 0; j < nss[i].ips_num; j++)
		{
			zbx_json_addobject(json, NULL);
			zbx_json_addstring(json, "ns"      , nss[i].name, ZBX_JSON_TYPE_STRING);
			zbx_json_addstring(json, "ip"      , nss[i].ips[j].ip, ZBX_JSON_TYPE_STRING);
			zbx_json_addstring(json, "nsid"    , nss[i].ips[j].nsid, ZBX_JSON_TYPE_STRING);
			zbx_json_addstring(json, "protocol", (protocol == RSM_UDP ? "udp" : "tcp"),
					ZBX_JSON_TYPE_STRING);
			zbx_json_addint64(json, "rtt"      , nss[i].ips[j].rtt);
			zbx_json_close(json);
		}
	}

	zbx_json_close(json);

	zbx_json_addarray(json, "nss");

	for (i = 0; i < nss_num; i++)
	{
		zbx_json_addobject(json, NULL);
		zbx_json_addstring(json, "ns", nss[i].name, ZBX_JSON_TYPE_STRING);
		zbx_json_adduint64(json, "status", nss[i].result);
		zbx_json_close(json);
	}

	zbx_json_close(json);

	zbx_json_adduint64(json, "mode", current_mode);
	zbx_json_adduint64(json, "status", dns_status);
	zbx_json_adduint64(json, "protocol", (protocol == RSM_UDP ? 0 : 1));
	zbx_json_addstring(json, "testedname", testedname, ZBX_JSON_TYPE_STRING);

	if (dnssec_status != NULL)
		zbx_json_adduint64(json, "dnssecstatus", *dnssec_status);

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

static int	update_metadata(int file_exists, const char *rsmhost, unsigned int dns_status, int test_recover,
		char protocol, unsigned int *current_mode, int *successful_tests, FILE *log_fd, char *err,
		size_t err_size)
{
	if (1 == dns_status)
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
static int	get_dns_minns_from_value(time_t now, const char *value, unsigned int *minns)
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

	*minns = (unsigned int)atoi(minns_p);

	return SUCCEED;
}

int	check_rsm_dns(zbx_uint64_t hostid, zbx_uint64_t itemid, const char *host, int nextcheck,
		const AGENT_REQUEST *request, AGENT_RESULT *result, FILE *output_fd)
{
	char			err[ZBX_ERR_BUF_SIZE], protocol, *rsmhost, *testprefix, *name_servers_list,
				*resolver_str,
				resolver_ip[ZBX_HOST_BUF_SIZE],
				testedname[ZBX_HOST_BUF_SIZE], *minns_value;
	zbx_dnskeys_error_t	ec_dnskeys;
	ldns_resolver		*res = NULL;
	ldns_rr_list		*keys = NULL;
	FILE			*log_fd;
	zbx_ns_t		*nss = NULL;
	size_t			i, j, nss_num = 0;
	unsigned int		extras,
				current_mode,
				dns_status,
				dnssec_status,
				minns;
	struct zbx_json		json;
	uint16_t		resolver_port;
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
	GET_PARAM_NEMPTY(resolver_str     , 10, "IP address of local resolver");
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
		protocol = (((zbx_uint64_t)nextcheck / 60 + hostid + itemid) % (zbx_uint64_t)tcp_ratio) == 0 ? RSM_TCP : RSM_UDP;
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
	if (NULL == output_fd)
	{
		if (NULL == (log_fd = open_item_log(host, rsmhost, ZBX_DNS_LOG_PREFIX, err, sizeof(err))))
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, err));
			return ret;
		}
	}
	else
		log_fd = output_fd;
		
	start_test(log_fd);

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

	get_host_and_port_from_str(resolver_str, ';', resolver_ip, sizeof(resolver_ip), &resolver_port,
			DEFAULT_RESOLVER_PORT);

	/* create resolver */
	if (SUCCEED != zbx_create_resolver(&res, "resolver", resolver_ip, resolver_port, protocol, ipv4_enabled,
			ipv6_enabled, extras,
			(RSM_UDP == protocol ? RSM_UDP_TIMEOUT : RSM_TCP_TIMEOUT),
			(RSM_UDP == protocol ? RSM_UDP_RETRY   : RSM_TCP_RETRY),
			log_fd, err, sizeof(err)))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "cannot create resolver: %s", err));
		goto end;
	}

	/* get list of Name Servers and IPs, by default it will set every Name Server */
	/* as working so if we have no IPs the result of Name Server will be SUCCEED  */
	if (SUCCEED != zbx_get_nameservers(name_servers_list, &nss, &nss_num, ipv4_enabled, ipv6_enabled,
			DEFAULT_NAMESERVER_PORT, log_fd, err, sizeof(err)))
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

	if (0 != dnssec_enabled && SUCCEED != zbx_get_dnskeys(res, rsmhost, resolver_ip, &keys, log_fd, &ec_dnskeys,
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
							nss[i].ips[j].port,
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

	set_dns_test_results(nss, nss_num, rtt_limit, minns, &dns_status,
			(dnssec_enabled ? &dnssec_status : NULL), log_fd);

	create_dns_json(&json, nss, nss_num, current_mode, dns_status,
			(dnssec_enabled ? &dnssec_status : NULL), protocol, testedname);

	if (SUCCEED != update_metadata(file_exists, rsmhost, dns_status, test_recover, protocol, &current_mode,
			&successful_tests, log_fd, err, sizeof(err)))
	{
		rsm_errf(log_fd, "internal error: %s", err);
	}

	SET_TEXT_RESULT(result, zbx_strdup(NULL, json.buffer));

	rsm_infof(log_fd, "test result %s", json.buffer);

	zbx_json_free(&json);
end:
	if (0 != ISSET_MSG(result))
		rsm_err(log_fd, result->msg);

	end_test(log_fd);

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

	if (NULL == output_fd && NULL != log_fd)
		fclose(log_fd);
out:
	return ret;
}

#undef CURRENT_MODE_NORMAL
#undef CURRENT_MODE_CRITICAL_UDP
#undef CURRENT_MODE_CRITICAL_TCP
