#include "t_rsm.h"
#include "../zabbix_server/poller/checks_simple_rsm.c"

#define DEFAULT_RES_IP		"127.0.0.1"
#define DEFAULT_TESTPREFIX	"www.zz--rsm-monitoring"
#define DEFAULT_RTT_LIMIT	20
#define EXPECTED_NSS_NUM	1

#define LOG_FILE1	"test1.log"
#define LOG_FILE2	"test2.log"

#define CURRENT_MODE_NORMAL	0

void	zbx_on_exit(int ret)
{
	ZBX_UNUSED(ret);
}

void	exit_usage(const char *program)
{
	fprintf(stderr, "usage: %s -t <tld> -n <ns> -i <ip> <[-4] [-6]> [-r <res_ip>] [-p <testprefix>] [-d] [-g] [-f] [-h]\n", program);
	fprintf(stderr, "       -t <tld>          TLD to test\n");
	fprintf(stderr, "       -n <ns>           Name Server to test\n");
	fprintf(stderr, "       -i <ip>           IP address of the Name Server to test\n");
	fprintf(stderr, "       -4                enable IPv4\n");
	fprintf(stderr, "       -6                enable IPv6\n");
	fprintf(stderr, "       -r <res_ip>       IP address of resolver to use (default: %s)\n", DEFAULT_RES_IP);
	fprintf(stderr, "       -p <testprefix>   domain testprefix to use (default: %s)\n", DEFAULT_TESTPREFIX);
	fprintf(stderr, "       -d                enable DNSSEC\n");
	fprintf(stderr, "       -c                use TCP instead of UDP\n");
	fprintf(stderr, "       -g                ignore errors, try to finish the test\n");
	fprintf(stderr, "       -f                log packets to files (%s, %s) instead of stdout\n", LOG_FILE1, LOG_FILE2);
	fprintf(stderr, "       -h                show this message and quit\n");
	exit(EXIT_FAILURE);
}

int	main(int argc, char *argv[])
{
	char		err[256], pack_buf[2048], nsid_unpacked[NSID_MAX_LENGTH * 2 + 1], *res_ip = DEFAULT_RES_IP,
			*tld = NULL, *ns = NULL, *ns_ip = NULL, proto = RSM_UDP, *nsid = NULL, *ns_with_ip = NULL,
			ipv4_enabled = 0, ipv6_enabled = 0, *testprefix = DEFAULT_TESTPREFIX,
			testedname[ZBX_HOST_BUF_SIZE], dnssec_enabled = 0, ignore_err = 0, log_to_file = 0;
	int		c, index, rtt, rtt_unpacked, upd_unpacked, unpacked_values_num, nssok, test_status;
	ldns_resolver	*res = NULL;
	ldns_rr_list	*keys = NULL;
	FILE		*log_fd = stdout;
	unsigned int	extras;
	size_t		size_one_unpacked, size_two_unpacked, nss_num = 0;
	zbx_ns_t	*nss = NULL;
	struct zbx_json	json;

	opterr = 0;

	while ((c = getopt (argc, argv, "t:n:i:46r:p:dcgfh")) != -1)
	{
		switch (c)
		{
			case 't':
				tld = optarg;
				break;
			case 'n':
				ns = optarg;
				break;
			case 'i':
				ns_ip = optarg;
				break;
			case '4':
				ipv4_enabled = 1;
				break;
			case '6':
				ipv6_enabled = 1;
				break;
			case 'r':
				res_ip = optarg;
				break;
			case 'p':
				testprefix = optarg;
				break;
			case 'd':
				dnssec_enabled = 1;
				break;
			case 'c':
				proto = RSM_TCP;
				break;
			case 'g':
				ignore_err = 1;
				break;
			case 'f':
				log_to_file = 1;
				break;
			case 'h':
				exit_usage(argv[0]);
				/* fall through */
			case '?':
				if (optopt == 't' || optopt == 'n' || optopt == 'i' || optopt == 'r' || optopt == 'p')
					fprintf(stderr, "Option -%c requires an argument.\n", optopt);
				else if (isprint (optopt))
					fprintf(stderr, "Unknown option `-%c'.\n", optopt);
				else
					fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
				exit(EXIT_FAILURE);
			default:
				abort();
		}
	}

	for (index = optind; index < argc; index++)
		printf("Non-option argument %s\n", argv[index]);

	if (NULL == tld)
	{
		fprintf(stderr, "tld [-t] must be specified\n");
		exit_usage(argv[0]);
	}

	if (NULL == ns)
	{
		fprintf(stderr, "Name Server [-n] must be specified\n");
		exit_usage(argv[0]);
	}

	if (NULL == ns_ip)
	{
		fprintf(stderr, "Name Server IP [-i] must be specified\n");
		exit_usage(argv[0]);
	}

	if (0 == ipv4_enabled && 0 == ipv6_enabled)
	{
		fprintf(stderr, "at least one IP version [-4, -6] must be specified\n");
		exit_usage(argv[0]);
	}

	rsm_infof(log_fd, "tld:%s, ns:%s, ip:%s, res:%s, testprefix:%s", tld, ns, ns_ip, res_ip, testprefix);

	extras = (dnssec_enabled ? RESOLVER_EXTRAS_DNSSEC : RESOLVER_EXTRAS_NONE);

	/* create resolver */
	if (SUCCEED != zbx_create_resolver(&res, "resolver", res_ip, proto, ipv4_enabled, ipv6_enabled, extras,
			(RSM_UDP == proto ? RSM_UDP_TIMEOUT : RSM_TCP_TIMEOUT),
			(RSM_UDP == proto ? RSM_UDP_RETRY : RSM_TCP_RETRY),
			log_fd, err, sizeof(err)))
	{
		rsm_errf(stderr, "cannot create resolver: %s", err);
		goto out;
	}

	ns_with_ip = zbx_malloc(NULL, strlen(ns) + 1 + strlen(ns_ip) + 1);
	zbx_strlcpy(ns_with_ip, ns, sizeof(ns_with_ip));
	strcat(ns_with_ip, ",");
	strcat(ns_with_ip, ns_ip);

	if (SUCCEED != zbx_get_nameservers(ns_with_ip, &nss, &nss_num, ipv4_enabled, ipv6_enabled, log_fd, err,
			sizeof(err)))
	{
		rsm_errf(stderr, "cannot get namservers: %s", err);
		goto out;
	}

	if (EXPECTED_NSS_NUM != nss_num)
	{
		rsm_errf(stderr, "unexpected number of nameservers: %d", nss_num);
	}

	if (0 != dnssec_enabled)
	{
		zbx_dnskeys_error_t	dnskeys_ec;

		if (log_to_file != 0)
		{
			if (NULL == (log_fd = fopen(LOG_FILE1, "w")))
			{
				rsm_errf(stderr, "cannot open file \"%s\" for writing: %s", LOG_FILE1, strerror(errno));
				exit(EXIT_FAILURE);
			}
		}

		if (SUCCEED != zbx_get_dnskeys(res, tld, res_ip, &keys, log_fd, &dnskeys_ec, err, sizeof(err)))
		{
			rsm_errf(stderr, "%s (error=%d)", err, DNS[DNS_PROTO(res)].dnskeys_error(dnskeys_ec));
			if (0 == ignore_err)
				goto out;
		}
	}

	if (log_to_file != 0)
	{
		if (0 != fclose(log_fd))
		{
			rsm_errf(stderr, "cannot close file %s: %s", LOG_FILE1, strerror(errno));
			goto out;
		}

		if (NULL == (log_fd = fopen(LOG_FILE2, "w")))
		{
			rsm_errf(stderr, "cannot open file \"%s\" for writing: %s", LOG_FILE2, strerror(errno));
			goto out;
		}
	}

	/* generate tested name */
	if (0 != strcmp(".", tld))
		zbx_snprintf(testedname, sizeof(testedname), "%s.%s.", testprefix, tld);
	else
		zbx_snprintf(testedname, sizeof(testedname), "%s.", testprefix);

	if (SUCCEED != zbx_get_ns_ip_values(res, ns, ns_ip, keys, testedname, log_fd, &rtt, &nsid, NULL,
			ipv4_enabled, ipv6_enabled, 0, err, sizeof(err)))
	{
		rsm_err(stderr, err);
		if (0 == ignore_err)
			goto out;
	}

	/* we have nsid, now test that it works with packing/unpacking */

	pack_values(0, 0, rtt, 0, nsid, pack_buf, sizeof(pack_buf));

	if (SUCCEED != unpack_values(&size_one_unpacked, &size_two_unpacked, &rtt_unpacked, &upd_unpacked,
			nsid_unpacked, pack_buf, stderr))
	{
		goto out;
	}

	/* test json */
	nss[0].ips[0].rtt = rtt_unpacked;
	nss[0].ips[0].nsid = zbx_strdup(NULL, nsid);
	nss[0].ips[0].upd = upd_unpacked;

	set_nss_results(nss, nss_num, DEFAULT_RTT_LIMIT, 2, &nssok, &test_status);

	create_rsm_dns_json(&json, nss, nss_num, CURRENT_MODE_NORMAL, nssok, test_status, proto, testedname);

	printf("OK (RTT:%d)\n", rtt_unpacked);
	printf("OK (NSID:%s)\n", nsid);
	printf("OK, json: %s\n", json.buffer);

	zbx_json_free(&json);

out:
	if (log_to_file != 0)
	{
		if (0 != fclose(log_fd))
			rsm_errf(stderr, "cannot close log file: %s", strerror(errno));
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

	if (0 != nss_num)
	{
		zbx_clean_nss(nss, nss_num);
		zbx_free(nss);
	}

	zbx_free(ns_with_ip);
	zbx_free(nsid);

	exit(EXIT_SUCCESS);
}
