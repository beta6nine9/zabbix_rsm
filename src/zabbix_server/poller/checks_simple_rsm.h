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

#ifndef ZABBIX_CHECKS_SIMPLE_RSM_H
#define ZABBIX_CHECKS_SIMPLE_RSM_H

#include "dbcache.h"
#include <ldns/ldns.h>

/* internal error codes (do not reflect as service error) */
#define ZBX_EC_DNS_UDP_INTERNAL_GENERAL		-1	/* Internal error */
#define ZBX_EC_DNS_UDP_INTERNAL_RES_CATCHALL	-2	/* DNS UDP - Expecting NOERROR RCODE but got unexpected from local resolver */

#define ZBX_EC_DNS_TCP_INTERNAL_GENERAL		ZBX_EC_DNS_UDP_INTERNAL_GENERAL
#define ZBX_EC_DNS_TCP_INTERNAL_RES_CATCHALL	-3	/* DNS TCP - Expecting NOERROR RCODE but got unexpected from local resolver */

#define ZBX_EC_RDDS43_INTERNAL_GENERAL		-1	/* Internal error */
#define ZBX_EC_RDDS43_INTERNAL_IP_UNSUP		-2	/* RDDS - IP addresses for the hostname are not supported by the IP versions supported by the probe node */
#define ZBX_EC_RDDS43_INTERNAL_RES_CATCHALL	-3	/* RDDS43 - Expecting NOERROR RCODE but got unexpected error when resolving hostname */

#define ZBX_EC_RDDS80_INTERNAL_GENERAL		ZBX_EC_RDDS43_INTERNAL_GENERAL
#define ZBX_EC_RDDS80_INTERNAL_IP_UNSUP		ZBX_EC_RDDS43_INTERNAL_IP_UNSUP
#define ZBX_EC_RDDS80_INTERNAL_RES_CATCHALL	-4	/* RDDS80 - Expecting NOERROR RCODE but got unexpected error when resolving hostname */

#define ZBX_EC_RDAP_INTERNAL_GENERAL		-1	/* Internal error */
#define ZBX_EC_RDAP_INTERNAL_IP_UNSUP		-2	/* RDAP - IP addresses for the hostname are not supported by the IP versions supported by the probe node */
#define ZBX_EC_RDAP_INTERNAL_RES_CATCHALL	-5	/* RDAP - Expecting NOERROR RCODE but got unexpected error when resolving hostname */

#define ZBX_EC_EPP_INTERNAL_GENERAL		-1	/* Internal error */
#define ZBX_EC_EPP_INTERNAL_IP_UNSUP		-2	/* EPP - IP addresses for the hostname are not supported by the IP versions supported by the probe node */

#define ZBX_EC_INTERNAL_LAST			-199	/* -1 :: -199 */

/* DNS UDP error codes */
#define ZBX_EC_DNS_UDP_NS_NOREPLY	-200	/* DNS UDP - No reply from name server */
#define ZBX_EC_DNS_UDP_CLASS_CHAOS	-207	/* DNS UDP - Expecting DNS CLASS IN but got CHAOS */
#define ZBX_EC_DNS_UDP_CLASS_HESIOD	-208	/* DNS UDP - Expecting DNS CLASS IN but got HESIOD */
#define ZBX_EC_DNS_UDP_CLASS_CATCHALL	-209	/* DNS UDP - Expecting DNS CLASS IN but got something different than IN, CHAOS or HESIOD */
#define ZBX_EC_DNS_UDP_HEADER		-210	/* DNS UDP - Header section incomplete */
#define ZBX_EC_DNS_UDP_QUESTION		-211	/* DNS UDP - Question section incomplete */
#define ZBX_EC_DNS_UDP_ANSWER		-212	/* DNS UDP - Answer section incomplete */
#define ZBX_EC_DNS_UDP_AUTHORITY	-213	/* DNS UDP - Authority section incomplete */
#define ZBX_EC_DNS_UDP_ADDITIONAL	-214	/* DNS UDP - Additional section incomplete */
#define ZBX_EC_DNS_UDP_CATCHALL		-215	/* DNS UDP - Malformed DNS response */
#define ZBX_EC_DNS_UDP_NOAAFLAG		-250	/* DNS UDP - Querying for a non existent domain - AA flag not present in response */
#define ZBX_EC_DNS_UDP_NODOMAIN		-251	/* DNS UDP - Querying for a non existent domain - Domain name being queried not present in question section */
/* Error code for every assigned, non private DNS RCODE (with the exception of RCODE/NXDOMAIN) */
/* as per: https://www.iana.org/assignments/dns-parameters/dns-parameters.xhtml */
#define ZBX_EC_DNS_UDP_RCODE_FORMERR	-253	/* DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got FORMERR */
#define ZBX_EC_DNS_UDP_RCODE_SERVFAIL	-254	/* DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got SERVFAIL */
#define ZBX_EC_DNS_UDP_RCODE_NOTIMP	-255	/* DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got NOTIMP */
#define ZBX_EC_DNS_UDP_RCODE_REFUSED	-256	/* DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got REFUSED */
#define ZBX_EC_DNS_UDP_RCODE_YXDOMAIN	-257	/* DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got YXDOMAIN */
#define ZBX_EC_DNS_UDP_RCODE_YXRRSET	-258	/* DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got YXRRSET */
#define ZBX_EC_DNS_UDP_RCODE_NXRRSET	-259	/* DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got NXRRSET */
#define ZBX_EC_DNS_UDP_RCODE_NOTAUTH	-260	/* DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got NOTAUTH */
#define ZBX_EC_DNS_UDP_RCODE_NOTZONE	-261	/* DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got NOTZONE */
#define ZBX_EC_DNS_UDP_RCODE_BADVERS_OR	-262	/* DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got BADVERS or BADSIG */
#define ZBX_EC_DNS_UDP_RCODE_BADKEY	-263	/* DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got BADKEY */
#define ZBX_EC_DNS_UDP_RCODE_BADTIME	-264	/* DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got BADTIME */
#define ZBX_EC_DNS_UDP_RCODE_BADMODE	-265	/* DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got BADMODE */
#define ZBX_EC_DNS_UDP_RCODE_BADNAME	-266	/* DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got BADNAME */
#define ZBX_EC_DNS_UDP_RCODE_BADALG	-267	/* DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got BADALG */
#define ZBX_EC_DNS_UDP_RCODE_BADTRUNC	-268	/* DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got BADTRUNC */
#define ZBX_EC_DNS_UDP_RCODE_BADCOOKIE	-269	/* DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got BADCOOKIE */
#define ZBX_EC_DNS_UDP_RCODE_CATCHALL	-270	/* DNS UDP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got unexpected */
#define ZBX_EC_DNS_UDP_RES_NOREPLY	-400	/* DNS UDP - No server could be reached by local resolver */
/* DNS UDP DNSSEC error codes */
#define ZBX_EC_DNS_UDP_DNSSEC_FIRST	-401	/* NB! This must be the first defined UDP DNSSEC error */
#define ZBX_EC_DNS_UDP_DNSKEY_NONE	-401	/* DNS UDP - The TLD is configured as DNSSEC-enabled, but no DNSKEY was found in the apex */
#define ZBX_EC_DNS_UDP_DNSKEY_NOADBIT	-402	/* DNS UDP - No AD bit from local resolver */
#define ZBX_EC_DNS_UDP_RES_NXDOMAIN	-403	/* DNS UDP - Expecting NOERROR RCODE but got NXDOMAIN from local resolver */
#define ZBX_EC_DNS_UDP_ALGO_UNKNOWN	-405	/* DNS UDP - Unknown cryptographic algorithm */
#define ZBX_EC_DNS_UDP_ALGO_NOT_IMPL	-406	/* DNS UDP - Cryptographic algorithm not implemented */
#define ZBX_EC_DNS_UDP_RRSIG_NONE	-407	/* DNS UDP - No RRSIGs where found in any section, and the TLD has the DNSSEC flag enabled */
#define ZBX_EC_DNS_UDP_NO_NSEC_IN_AUTH	-408	/* DNS UDP - Querying for a non existent domain - No NSEC/NSEC3 RRs were found in the authority section */
#define ZBX_EC_DNS_UDP_RRSIG_NOTCOVERED	-410	/* DNS UDP - The signature does not cover this RRset */
#define ZBX_EC_DNS_UDP_RRSIG_NOT_SIGNED	-414	/* DNS UDP - The RRSIG found is not signed by a DNSKEY from the KEYSET of the TLD */
#define ZBX_EC_DNS_UDP_SIG_BOGUS	-415	/* DNS UDP - Bogus DNSSEC signature */
#define ZBX_EC_DNS_UDP_SIG_EXPIRED	-416	/* DNS UDP - DNSSEC signature has expired */
#define ZBX_EC_DNS_UDP_SIG_NOT_INCEPTED	-417	/* DNS UDP - DNSSEC signature not incepted yet */
#define ZBX_EC_DNS_UDP_SIG_EX_BEFORE_IN	-418	/* DNS UDP - DNSSEC signature has expiration date earlier than inception date */
#define ZBX_EC_DNS_UDP_NSEC3_ERROR	-419	/* DNS UDP - Error in NSEC3 denial of existence proof */
#define ZBX_EC_DNS_UDP_RR_NOTCOVERED	-422	/* DNS UDP - RR not covered by the given NSEC RRs */
#define ZBX_EC_DNS_UDP_WILD_NOTCOVERED	-423	/* DNS UDP - Wildcard not covered by the given NSEC RRs */
#define ZBX_EC_DNS_UDP_RRSIG_MISS_RDATA	-425	/* DNS UDP - The RRSIG has too few RDATA fields */
#define ZBX_EC_DNS_UDP_DNSSEC_CATCHALL	-427	/* DNS UDP - Malformed DNSSEC response */
#define ZBX_EC_DNS_UDP_DNSSEC_LAST	-427	/* NB! This must be the last defined UDP DNSSEC error */
/* DNS TCP error codes */
#define ZBX_EC_DNS_TCP_NS_TO		-600	/* DNS TCP - DNS TCP - Timeout reply from name server */
#define ZBX_EC_DNS_TCP_NS_ECON		-601	/* DNS TCP - Error opening connection to name server */
#define ZBX_EC_DNS_TCP_CLASS_CHAOS	-607	/* DNS TCP - Expecting DNS CLASS IN but got CHAOS */
#define ZBX_EC_DNS_TCP_CLASS_HESIOD	-608	/* DNS TCP - Expecting DNS CLASS IN but got HESIOD */
#define ZBX_EC_DNS_TCP_CLASS_CATCHALL	-609	/* DNS TCP - Expecting DNS CLASS IN but got something different than IN, CHAOS or HESIOD */
#define ZBX_EC_DNS_TCP_HEADER		-610	/* DNS TCP - Header section incomplete */
#define ZBX_EC_DNS_TCP_QUESTION		-611	/* DNS TCP - Question section incomplete */
#define ZBX_EC_DNS_TCP_ANSWER		-612	/* DNS TCP - Answer section incomplete */
#define ZBX_EC_DNS_TCP_AUTHORITY	-613	/* DNS TCP - Authority section incomplete */
#define ZBX_EC_DNS_TCP_ADDITIONAL	-614	/* DNS TCP - Additional section incomplete */
#define ZBX_EC_DNS_TCP_CATCHALL		-615	/* DNS TCP - Malformed DNS response */
#define ZBX_EC_DNS_TCP_NOAAFLAG		-650	/* DNS TCP - Querying for a non existent domain - AA flag not present in response */
#define ZBX_EC_DNS_TCP_NODOMAIN		-651	/* DNS TCP - Querying for a non existent domain - Domain name being queried not present in question section */
/* Error code for every assigned, non private DNS RCODE (with the exception of RCODE/NXDOMAIN) */
/* as per: https://www.iana.org/assignments/dns-parameters/dns-parameters.xhtml */
#define ZBX_EC_DNS_TCP_RCODE_FORMERR	-653	/* DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got FORMERR */
#define ZBX_EC_DNS_TCP_RCODE_SERVFAIL	-654	/* DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got SERVFAIL */
#define ZBX_EC_DNS_TCP_RCODE_NOTIMP	-655	/* DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got NOTIMP */
#define ZBX_EC_DNS_TCP_RCODE_REFUSED	-656	/* DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got REFUSED */
#define ZBX_EC_DNS_TCP_RCODE_YXDOMAIN	-657	/* DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got YXDOMAIN */
#define ZBX_EC_DNS_TCP_RCODE_YXRRSET	-658	/* DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got YXRRSET */
#define ZBX_EC_DNS_TCP_RCODE_NXRRSET	-659	/* DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got NXRRSET */
#define ZBX_EC_DNS_TCP_RCODE_NOTAUTH	-660	/* DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got NOTAUTH */
#define ZBX_EC_DNS_TCP_RCODE_NOTZONE	-661	/* DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got NOTZONE */
#define ZBX_EC_DNS_TCP_RCODE_BADVERS_OR	-662	/* DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got BADVERS or BADSIG */
#define ZBX_EC_DNS_TCP_RCODE_BADKEY	-663	/* DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got BADKEY */
#define ZBX_EC_DNS_TCP_RCODE_BADTIME	-664	/* DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got BADTIME */
#define ZBX_EC_DNS_TCP_RCODE_BADMODE	-665	/* DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got BADMODE */
#define ZBX_EC_DNS_TCP_RCODE_BADNAME	-666	/* DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got BADNAME */
#define ZBX_EC_DNS_TCP_RCODE_BADALG	-667	/* DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got BADALG */
#define ZBX_EC_DNS_TCP_RCODE_BADTRUNC	-668	/* DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got BADTRUNC */
#define ZBX_EC_DNS_TCP_RCODE_BADCOOKIE	-669	/* DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got BADCOOKIE */
#define ZBX_EC_DNS_TCP_RCODE_CATCHALL	-670	/* DNS TCP - Querying for a non existent domain - Expecting NXDOMAIN RCODE but got unexpected */
#define ZBX_EC_DNS_TCP_RES_NOREPLY	-800	/* DNS TCP - No server could be reached by local resolver */
/* DNS TCP DNSSEC error codes */
#define ZBX_EC_DNS_TCP_DNSSEC_FIRST	-801	/* NB! This must be the first defined TCP DNSSEC error */
#define ZBX_EC_DNS_TCP_DNSKEY_NONE	-801	/* DNS TCP - The TLD is configured as DNSSEC-enabled, but no DNSKEY was found in the apex */
#define ZBX_EC_DNS_TCP_DNSKEY_NOADBIT	-802	/* DNS TCP - No AD bit from local resolver */
#define ZBX_EC_DNS_TCP_RES_NXDOMAIN	-803	/* DNS TCP - Expecting NOERROR RCODE but got NXDOMAIN from local resolver */
#define ZBX_EC_DNS_TCP_ALGO_UNKNOWN	-805	/* DNS TCP - Unknown cryptographic algorithm */
#define ZBX_EC_DNS_TCP_ALGO_NOT_IMPL	-806	/* DNS TCP - Cryptographic algorithm not implemented */
#define ZBX_EC_DNS_TCP_RRSIG_NONE	-807	/* DNS TCP - No RRSIGs where found in any section, and the TLD has the DNSSEC flag enabled */
#define ZBX_EC_DNS_TCP_NO_NSEC_IN_AUTH	-808	/* DNS TCP - Querying for a non existent domain - No NSEC/NSEC3 RRs were found in the authority section */
#define ZBX_EC_DNS_TCP_RRSIG_NOTCOVERED	-810	/* DNS TCP - The signature does not cover this RRset */
#define ZBX_EC_DNS_TCP_RRSIG_NOT_SIGNED	-814	/* DNS TCP - The RRSIG found is not signed by a DNSKEY from the KEYSET of the TLD */
#define ZBX_EC_DNS_TCP_SIG_BOGUS	-815	/* DNS TCP - Bogus DNSSEC signature */
#define ZBX_EC_DNS_TCP_SIG_EXPIRED	-816	/* DNS TCP - DNSSEC signature has expired */
#define ZBX_EC_DNS_TCP_SIG_NOT_INCEPTED	-817	/* DNS TCP - DNSSEC signature not incepted yet */
#define ZBX_EC_DNS_TCP_SIG_EX_BEFORE_IN	-818	/* DNS TCP - DNSSEC signature has expiration date earlier than inception date */
#define ZBX_EC_DNS_TCP_NSEC3_ERROR	-819	/* DNS TCP - Error in NSEC3 denial of existence proof */
#define ZBX_EC_DNS_TCP_RR_NOTCOVERED	-822	/* DNS TCP - RR not covered by the given NSEC RRs */
#define ZBX_EC_DNS_TCP_WILD_NOTCOVERED	-823	/* DNS TCP - Wildcard not covered by the given NSEC RRs */
#define ZBX_EC_DNS_TCP_RRSIG_MISS_RDATA	-825	/* DNS TCP - The RRSIG has too few RDATA fields */
#define ZBX_EC_DNS_TCP_DNSSEC_CATCHALL	-827	/* DNS TCP - Malformed DNSSEC response */
#define ZBX_EC_DNS_TCP_DNSSEC_LAST	-827	/* NB! This must be the last defined TCP DNSSEC error */
/* RDDS error codes */
#define ZBX_EC_RDDS43_NONS		-201	/* Whois server returned no NS */
#define ZBX_EC_RDDS80_NOCODE		-206	/* no HTTP status code */
#define ZBX_EC_RDDS43_RES_NOREPLY	-222	/* RDDS43 - No server could be reached by local resolver */
#define ZBX_EC_RDDS43_RES_SERVFAIL	-224	/* RDDS43 - Expecting NOERROR RCODE but got SERVFAIL when resolving hostname */
#define ZBX_EC_RDDS43_RES_NXDOMAIN	-225	/* RDDS43 - Expecting NOERROR RCODE but got NXDOMAIN when resolving hostname */
#define ZBX_EC_RDDS43_TO		-227	/* RDDS43 - Timeout */
#define ZBX_EC_RDDS43_ECON		-228	/* RDDS43 - Error opening connection to server */
#define ZBX_EC_RDDS43_EMPTY		-229	/* RDDS43 - Empty response */
#define ZBX_EC_RDDS80_RES_NOREPLY	-250	/* RDDS80 - No server could be reached by local resolver */
#define ZBX_EC_RDDS80_RES_SERVFAIL	-252	/* RDDS80 - Expecting NOERROR RCODE but got SERVFAIL when resolving hostname */
#define ZBX_EC_RDDS80_RES_NXDOMAIN	-253	/* RDDS80 - Expecting NOERROR RCODE but got NXDOMAIN when resolving hostname */
#define ZBX_EC_RDDS80_TO		-255	/* RDDS80 - Timeout */
#define ZBX_EC_RDDS80_ECON		-256	/* RDDS80 - Error opening connection to server */
#define ZBX_EC_RDDS80_EHTTP		-257	/* RDDS80 - Error in HTTP protocol */
#define ZBX_EC_RDDS80_EHTTPS		-258	/* RDDS80 - Error in HTTPS protocol */
#define ZBX_EC_RDDS80_EMAXREDIRECTS	-259	/* RDDS80 - Maximum HTTP redirects were hit while trying to connect to RDDS server */
#define ZBX_EC_RDDS80_HTTP_BASE		-300	/* RDDS80 - Expecting HTTP status code 200 but got xxx */
/* RDAP error codes */
#define ZBX_EC_RDAP_NOTLISTED		-390	/* The TLD is not listed in the Bootstrap Service Registry for Domain Name Space */
#define ZBX_EC_RDAP_NOHTTPS		-391	/* The RDAP base URL obtained from Bootstrap Service Registry for Domain Name Space does not use HTTPS */
#define ZBX_EC_RDAP_RES_NOREPLY		-400	/* RDAP - No server could be reached by local resolver */
#define ZBX_EC_RDAP_RES_SERVFAIL	-402	/* RDAP - Expecting NOERROR RCODE but got SERVFAIL when resolving hostname */
#define ZBX_EC_RDAP_RES_NXDOMAIN	-403	/* RDAP - Expecting NOERROR RCODE but got NXDOMAIN when resolving hostname */
#define ZBX_EC_RDAP_TO			-405	/* RDAP - Timeout */
#define ZBX_EC_RDAP_ECON		-406	/* RDAP - Error opening connection to server */
#define ZBX_EC_RDAP_EJSON		-407	/* RDAP - Invalid JSON format in response */
#define ZBX_EC_RDAP_NONAME		-408	/* RDAP - ldhName member not found in response */
#define ZBX_EC_RDAP_ENAME		-409	/* RDAP - ldhName member doesn't match query in response */
#define ZBX_EC_RDAP_EHTTP		-413	/* RDAP - Error in HTTP protocol */
#define ZBX_EC_RDAP_EHTTPS		-414	/* RDAP - Error in HTTPS protocol */
#define ZBX_EC_RDAP_EMAXREDIRECTS	-415	/* RDAP - Maximum HTTP redirects were hit while trying to connect to RDAP server */
#define ZBX_EC_RDAP_HTTP_BASE		-500	/* RDAP - Expecting HTTP status code 200 bug got xxx */
/* EPP error codes */
#define ZBX_EC_EPP_NO_IP		-200	/* IP is missing for EPP server */
#define ZBX_EC_EPP_CONNECT		-201	/* cannot connect to EPP server */
#define ZBX_EC_EPP_CRYPT		-202	/* invalid certificate or private key */
#define ZBX_EC_EPP_FIRSTTO		-203	/* first message timeout */
#define ZBX_EC_EPP_FIRSTINVAL		-204	/* first message is invalid */
#define ZBX_EC_EPP_LOGINTO		-205	/* LOGIN command timeout */
#define ZBX_EC_EPP_LOGININVAL		-206	/* invalid reply to LOGIN command */
#define ZBX_EC_EPP_UPDATETO		-207	/* UPDATE command timeout */
#define ZBX_EC_EPP_UPDATEINVAL		-208	/* invalid reply to UPDATE command */
#define ZBX_EC_EPP_INFOTO		-209	/* INFO command timeout */
#define ZBX_EC_EPP_INFOINVAL		-210	/* invalid reply to INFO command */
#define ZBX_EC_EPP_SERVERCERT		-211	/* Server certificate validation failed */

#define ZBX_EC_PROBE_OFFLINE		0	/* probe in automatic offline mode */
#define ZBX_EC_PROBE_ONLINE		1	/* probe in automatic online mode */
#define ZBX_EC_PROBE_UNSUPPORTED	2	/* internal use only */

#define ZBX_NO_VALUE			-1000	/* no value was obtained during the check, used in the code only */

/* NB! Do not change, these are used as DNS array indexes. */
#define RSM_UDP	0
#define RSM_TCP	1

/* used only in EPP and probe status tests, remove and use item parameters in the future as other checks do */
#define ZBX_MACRO_DNS_RESOLVER		"{$RSM.RESOLVER}"
#define ZBX_MACRO_EPP_LOGIN_RTT		"{$RSM.EPP.LOGIN.RTT.HIGH}"
#define ZBX_MACRO_EPP_UPDATE_RTT	"{$RSM.EPP.UPDATE.RTT.HIGH}"
#define ZBX_MACRO_EPP_INFO_RTT		"{$RSM.EPP.INFO.RTT.HIGH}"
#define ZBX_MACRO_IP4_ENABLED		"{$RSM.IP4.ENABLED}"
#define ZBX_MACRO_IP6_ENABLED		"{$RSM.IP6.ENABLED}"
#define ZBX_MACRO_IP4_MIN_SERVERS	"{$RSM.IP4.MIN.SERVERS}"
#define ZBX_MACRO_IP6_MIN_SERVERS	"{$RSM.IP6.MIN.SERVERS}"
#define ZBX_MACRO_IP4_REPLY_MS		"{$RSM.IP4.REPLY.MS}"
#define ZBX_MACRO_IP6_REPLY_MS		"{$RSM.IP6.REPLY.MS}"
#define ZBX_MACRO_PROBE_ONLINE_DELAY	"{$RSM.PROBE.ONLINE.DELAY}"
#define ZBX_MACRO_EPP_ENABLED		"{$RSM.EPP.ENABLED}"
#define ZBX_MACRO_EPP_USER		"{$RSM.EPP.USER}"
#define ZBX_MACRO_EPP_PASSWD		"{$RSM.EPP.PASSWD}"
#define ZBX_MACRO_EPP_CERT		"{$RSM.EPP.CERT}"
#define ZBX_MACRO_EPP_PRIVKEY		"{$RSM.EPP.PRIVKEY}"
#define ZBX_MACRO_EPP_KEYSALT		"{$RSM.EPP.KEYSALT}"
#define ZBX_MACRO_EPP_COMMANDS		"{$RSM.EPP.COMMANDS}"
#define ZBX_MACRO_EPP_SERVERID		"{$RSM.EPP.SERVERID}"
#define ZBX_MACRO_EPP_TESTPREFIX	"{$RSM.EPP.TESTPREFIX}"
#define ZBX_MACRO_EPP_SERVERCERTMD5	"{$RSM.EPP.SERVERCERTMD5}"
#define ZBX_MACRO_TLD_EPP_ENABLED	"{$RSM.TLD.EPP.ENABLED}"

#define RSM_DEFAULT_LOGDIR		"/var/log"		/* if Zabbix log dir is undefined */
#define ZBX_DNS_LOG_PREFIX		"dns"			/* file will be <LOGDIR>/<PROBE>-<TLD>-ZBX_DNS_LOG_PREFIX-<udp|tcp>.log */
#define ZBX_RDDS_LOG_PREFIX		"rdds"			/* file will be <LOGDIR>/<PROBE>-<TLD>-ZBX_RDDS_LOG_PREFIX.log */
#define ZBX_RDAP_LOG_PREFIX		"rdap"			/* file will be <LOGDIR>/<PROBE>-<TLD>-ZBX_RDAP_LOG_PREFIX.log */
#define ZBX_EPP_LOG_PREFIX		"epp"			/* file will be <LOGDIR>/<PROBE>-<TLD>-ZBX_EPP_LOG_PREFIX.log */
#define ZBX_PROBESTATUS_LOG_PREFIX	"probestatus"		/* file will be <LOGDIR>/<PROBE>-ZBX_PROBESTATUS_LOG_PREFIX.log */
#define ZBX_RESOLVERSTATUS_LOG_PREFIX	"resolverstatus"	/* file will be <LOGDIR>/<PROBE>-ZBX_RESOLVERSTATUS_LOG_PREFIX.log */

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
	ZBX_INTERNAL_GENERAL,
	ZBX_INTERNAL_IP_UNSUP,
	ZBX_INTERNAL_RES_CATCHALL
}
zbx_internal_error_t;

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
	ZBX_EC_RR_CLASS_INTERNAL,
	ZBX_EC_RR_CLASS_CHAOS,
	ZBX_EC_RR_CLASS_HESIOD,
	ZBX_EC_RR_CLASS_CATCHALL
}
zbx_rr_class_error_t;

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

typedef enum
{
	RSM_SUBTEST_SUCCESS,
	RSM_SUBTEST_FAIL
}
rsm_subtest_result_t;

int	check_rsm_dns(zbx_uint64_t hostid, zbx_uint64_t itemid, const char *host, int nextcheck,
		const AGENT_REQUEST *request, AGENT_RESULT *result, FILE *output_fd);
int	check_rsm_rdds(const char *host, const AGENT_REQUEST *request, AGENT_RESULT *result, FILE *output_fd);
int	check_rsm_rdap(const char *host, const AGENT_REQUEST *request, AGENT_RESULT *result, FILE *output_fd);
int	check_rsm_epp(const char *host, const AGENT_REQUEST *request, AGENT_RESULT *result);
int	check_rsm_probe_status(const char *host, const AGENT_REQUEST *request, AGENT_RESULT *result);
int	check_rsm_resolver_status(const char *host, const AGENT_REQUEST *request, AGENT_RESULT *result);

int	zbx_validate_ip(const char *ip, int ipv4_enabled, int ipv6_enabled, ldns_rdf **ip_rdf_out, char *is_ipv4);
FILE	*open_item_log(const char *host, const char *tld, const char *name, char *err, size_t err_size);
void	zbx_get_strings_from_list(zbx_vector_str_t *strings, char *list, char delim);
int	zbx_create_resolver(ldns_resolver **res, const char *name, const char *ip, uint16_t port, char protocol,
		int ipv4_enabled, int ipv6_enabled, unsigned int extras, int timeout, unsigned char tries, FILE *log_fd,
		char *err, size_t err_size);
size_t	rsm_random(size_t max_values);
int	zbx_resolver_resolve_host(ldns_resolver *res, const char *host, zbx_vector_str_t *ips, int ipv_flags,
		FILE *log_fd, zbx_resolver_error_t *ec_res, char *err, size_t err_size);
void	rsm_vector_str_clean_and_destroy(zbx_vector_str_t *v);
void	get_host_and_port_from_str(const char *str, char delim, char *host, size_t host_size, unsigned short *port,
		unsigned short default_port);
int	zbx_change_resolver(ldns_resolver *res, const char *name, const char *ip, uint16_t port, int ipv4_enabled,
		int ipv6_enabled, FILE *log_fd, char *err, size_t err_size);
int	zbx_get_ts_from_host(const char *host, time_t *ts);

int	zbx_http_test(const char *host, const char *url, long timeout, long maxredirs, zbx_http_error_t *ec_http,
		int *rtt, void *writedata, size_t (*writefunction)(char *, size_t, size_t, void *),
		int curl_flags, char *err, size_t err_size);
int	map_http_code(long http_code);
int	rsm_split_url(const char *url, char **scheme, char **domain, int *port, char **path, char *err, size_t err_size);

rsm_subtest_result_t	rsm_subtest_result(int rtt, int rtt_limit);

void	start_test(FILE *log_fd);
void	end_test(FILE *log_fd);

#define rsm_dump(log_fd, fmt, ...)	fprintf(log_fd, ZBX_CONST_STRING(fmt), ##__VA_ARGS__)
#define rsm_errf(log_fd, fmt, ...)	rsm_logf(log_fd, LOG_LEVEL_ERR, ZBX_CONST_STRING(fmt), ##__VA_ARGS__)
#define rsm_warnf(log_fd, fmt, ...)	rsm_logf(log_fd, LOG_LEVEL_WARNING, ZBX_CONST_STRING(fmt), ##__VA_ARGS__)
#define rsm_infof(log_fd, fmt, ...)	rsm_logf(log_fd, LOG_LEVEL_DEBUG, ZBX_CONST_STRING(fmt), ##__VA_ARGS__)

#define rsm_err(log_fd, text)	rsm_log(log_fd, LOG_LEVEL_ERR, text)
#define rsm_info(log_fd, text)	rsm_log(log_fd, LOG_LEVEL_DEBUG, text)

void	rsm_log(FILE *log_fd, int level, const char *text);
void	rsm_logf(FILE *log_fd, int level, const char *fmt, ...);

extern const char	*CONFIG_LOG_FILE;

#define ZBX_HOST_BUF_SIZE	128
#define ZBX_ERR_BUF_SIZE	8192
#define DEFAULT_RESOLVER_PORT	53

#define RESOLVER_EXTRAS_NONE	0x0u
#define RESOLVER_EXTRAS_DNSSEC	0x1u

#define ZBX_FLAG_IPV4_ENABLED	0x1
#define ZBX_FLAG_IPV6_ENABLED	0x2

#define ZBX_EC_EPP_NOT_IMPLEMENTED	ZBX_EC_EPP_INTERNAL_GENERAL

#define RSM_UDP_TIMEOUT	3	/* seconds */
#define RSM_UDP_RETRY	1
#define RSM_TCP_TIMEOUT	11	/* seconds (SLA: 5 times higher than max (2)) */
#define RSM_TCP_RETRY	1

#define ZBX_EC_DNS_TCP_NS_NOREPLY	ZBX_EC_DNS_TCP_INTERNAL_GENERAL;	/* only UDP */
#define ZBX_EC_DNS_UDP_NS_ECON		ZBX_EC_DNS_UDP_INTERNAL_GENERAL;	/* only TCP */
#define ZBX_EC_DNS_UDP_NS_TO		ZBX_EC_DNS_UDP_INTERNAL_GENERAL;	/* only TCP */

#define UNEXPECTED_LDNS_ERROR		"unexpected LDNS error"
#define UNEXPECTED_LDNS_MEM_ERROR	UNEXPECTED_LDNS_ERROR " (out of memory?)"

#define RESPONSE_PREVIEW_SIZE	100

typedef int	(*zbx_ns_query_error_func_t)(zbx_ns_query_error_t);
typedef int	(*zbx_ns_answer_error_func_t)(zbx_ns_answer_error_t);
typedef int	(*zbx_dnskeys_error_func_t)(zbx_dnskeys_error_t);
typedef int	(*zbx_dnssec_error_func_t)(zbx_dnssec_error_t);
typedef int	(*zbx_rr_class_error_func_t)(zbx_rr_class_error_t);
typedef int	(*zbx_rcode_not_nxdomain_func_t)(ldns_pkt_rcode);

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

#endif
