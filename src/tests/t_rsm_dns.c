#include "t_rsm.h"
#include "../zabbix_server/poller/checks_simple_rsm.c"

#define DEFAULT_RES_IP		"127.0.0.1"
#define DEFAULT_RES_PORT	53
#define DEFAULT_NS_PORT		53
#define DEFAULT_TESTPREFIX	"www.zz--rsm-monitoring"
#define DEFAULT_RTT_LIMIT	20
#define DEFAULT_MINNS		1
#define EXPECTED_NSS_NUM	1

#define LOG_FILE1	"test1.log"
#define LOG_FILE2	"test2.log"

#define CURRENT_MODE_NORMAL	0

void	zbx_on_exit(int ret)
{
	ZBX_UNUSED(ret);
}

static void	exit_usage(const char *program)
{
	fprintf(stderr, "usage: %s -t <tld> -n <ns> -i <ip> <[-4] [-6]> [-r <res_ip>] [-o <res/_port>] [-p <testprefix>]"
			" [-d] [-c] [-m <seconds>] [-j <file>] [-f] [-h]\n", program);
	fprintf(stderr, "       -t <tld>          TLD to test\n");
	fprintf(stderr, "       -n <ns>           Name Server to test\n");
	fprintf(stderr, "       -i <ip>           IP address of the Name Server to test\n");
	fprintf(stderr, "       -4                enable IPv4\n");
	fprintf(stderr, "       -6                enable IPv6\n");
	fprintf(stderr, "       -r <res_ip>       IP address of resolver to use (default: %s)\n", DEFAULT_RES_IP);
	fprintf(stderr, "       -o <res_port>     port of resolver to use (default: %hu)\n", DEFAULT_RES_PORT);
	fprintf(stderr, "       -s <ns_port>      port of name server to use (default: %hu)\n", DEFAULT_NS_PORT);
	fprintf(stderr, "       -p <testprefix>   domain testprefix to use (default: %s)\n", DEFAULT_TESTPREFIX);
	fprintf(stderr, "       -d                enable DNSSEC\n");
	fprintf(stderr, "       -c                use TCP instead of UDP\n");
	fprintf(stderr, "       -m <seconds>      timeout (default udp:%d tcp:%d)\n", RSM_UDP_TIMEOUT, RSM_TCP_TIMEOUT);
	fprintf(stderr, "       -j <file>         write resulting json to the file\n");
	fprintf(stderr, "       -f                log packets to files (%s, %s) instead of stdout\n", LOG_FILE1, LOG_FILE2);
	fprintf(stderr, "       -h                show this message and quit\n");
	exit(EXIT_FAILURE);
}

int	main(int argc, char *argv[])
{
	char		err[256], pack_buf[2048], nsid_unpacked[NSID_MAX_LENGTH * 2 + 1], *res_ip = DEFAULT_RES_IP,
			*tld = NULL, *ns = NULL, *ns_ip = NULL, proto = RSM_UDP, *nsid = NULL, *ns_with_ip = NULL,
			ipv4_enabled = 0, ipv6_enabled = 0, *testprefix = DEFAULT_TESTPREFIX,
			testedname[ZBX_HOST_BUF_SIZE], dnssec_enabled = 0,
			*json_file = NULL;
	int		c, index, rtt, rtt_unpacked, upd_unpacked, minns = DEFAULT_MINNS, timeout = -1;
	ldns_resolver	*res = NULL;
	ldns_rr_list	*keys = NULL;
	FILE		*log_fd = stdout;
	unsigned int	extras, nssok, test_status, dnssec_status;
	size_t		size_one_unpacked, size_two_unpacked, nss_num = 0;
	zbx_ns_t	*nss = NULL;
	struct zbx_json	json;
	uint16_t	res_port = DEFAULT_RES_PORT, ns_port = DEFAULT_NS_PORT;

	opterr = 0;

	while ((c = getopt(argc, argv, "t:n:i:46r:o:s:p:dcm:j:fh")) != -1)
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
			case 'o':
				res_port = atoi(optarg);
				break;
			case 's':
				ns_port = atoi(optarg);
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
			case 'm':
				timeout = atoi(optarg);
				break;
			case 'j':
				json_file = optarg;
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

	if (-1 == timeout)
	{
		timeout = (RSM_UDP == proto ? RSM_UDP_TIMEOUT : RSM_TCP_TIMEOUT);
	}

	rsm_infof(log_fd, "tld:%s ns:%s ip:%s res:%s testprefix:%s timeout:%d", tld, ns, ns_ip, res_ip, testprefix, timeout);

	extras = (dnssec_enabled ? RESOLVER_EXTRAS_DNSSEC : RESOLVER_EXTRAS_NONE);

	/* create resolver */
	if (SUCCEED != zbx_create_resolver(&res, "resolver", res_ip, res_port, proto, ipv4_enabled, ipv6_enabled, extras,
			timeout,
			(RSM_UDP == proto ? RSM_UDP_RETRY : RSM_TCP_RETRY),
			log_fd, err, sizeof(err)))
	{
		rsm_errf(stderr, "cannot create resolver: %s", err);
		exit(EXIT_FAILURE);
	}

	ns_with_ip = zbx_malloc(NULL, strlen(ns) + 1 + strlen(ns_ip) + 1);
	zbx_strlcpy(ns_with_ip, ns, strlen(ns) + 1);
	strcat(ns_with_ip, ",");
	strcat(ns_with_ip, ns_ip);

	if (SUCCEED != zbx_get_nameservers(ns_with_ip, &nss, &nss_num, ipv4_enabled, ipv6_enabled, log_fd, err,
			sizeof(err)))
	{
		rsm_errf(stderr, "cannot get namservers: %s", err);
		exit(EXIT_FAILURE);
	}

	if (EXPECTED_NSS_NUM != nss_num)
	{
		rsm_errf(stderr, "unexpected number of nameservers: %d", nss_num);
		exit(EXIT_FAILURE);
	}

	if (0 != dnssec_enabled)
	{
		zbx_dnskeys_error_t	dnskeys_ec;

		if (SUCCEED != zbx_get_dnskeys(res, tld, res_ip, &keys, log_fd, &dnskeys_ec, err, sizeof(err)))
		{
			rsm_errf(stderr, "%s (error=%d)", err, DNS[DNS_PROTO(res)].dnskeys_error(dnskeys_ec));
		}
	}

	/* generate tested name */
	if (0 != strcmp(".", tld))
		zbx_snprintf(testedname, sizeof(testedname), "%s.%s.", testprefix, tld);
	else
		zbx_snprintf(testedname, sizeof(testedname), "%s.", testprefix);

	if (SUCCEED != zbx_get_ns_ip_values(res, ns, ns_ip, ns_port, keys, testedname, log_fd, &rtt, &nsid, NULL,
			ipv4_enabled, ipv6_enabled, 0, err, sizeof(err)))
	{
		rsm_err(stderr, err);
	}

	/* we have nsid, now test that it works with packing/unpacking */

	pack_values(0, 0, rtt, 0, nsid, pack_buf, sizeof(pack_buf));

	if (SUCCEED != unpack_values(&size_one_unpacked, &size_two_unpacked, &rtt_unpacked, &upd_unpacked,
			nsid_unpacked, pack_buf, stderr))
	{
		exit(EXIT_FAILURE);
	}

	/* test json */
	nss[0].ips[0].rtt = rtt_unpacked;
	nss[0].ips[0].nsid = zbx_strdup(NULL, (nsid ? nsid : ""));
	nss[0].ips[0].upd = upd_unpacked;

	set_dns_test_results(nss, nss_num, DEFAULT_RTT_LIMIT, minns, &nssok, &test_status, &dnssec_status,
			dnssec_enabled, stdout);

	create_dns_json(&json, nss, nss_num, CURRENT_MODE_NORMAL, nssok, test_status, dnssec_status, proto, testedname,
			dnssec_enabled);

	printf("OK (RTT:%d)\n", rtt_unpacked);
	printf("OK (NSID:%s)\n", nsid);
	printf("OK, json: %s\n", json.buffer);

	if (json_file)
	{
		char	*error = NULL;
		int	rv;

		rsm_infof(log_fd, "writing to %s...", json_file);

		rv = write_json_status(json_file, json.buffer, &error);

		zbx_free(error);

		if (rv != SUCCEED)
			exit(EXIT_FAILURE);
	}

	zbx_json_free(&json);

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
