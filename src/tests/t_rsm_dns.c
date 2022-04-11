#include "../zabbix_server/poller/checks_simple_rsm.c"
#include "t_rsm_decl.h"
#include "t_rsm.h"

#define DEFAULT_RES_IP		"127.0.0.1"
#define DEFAULT_RES_PORT	53
#define DEFAULT_NS_PORT		53
#define DEFAULT_TESTPREFIX	"www.zz--rsm-monitoring"

#define LOG_FILE1	"test1.log"
#define LOG_FILE2	"test2.log"

void	zbx_on_exit(int ret)
{
	ZBX_UNUSED(ret);
}

static void	exit_usage(const char *program)
{
	fprintf(stderr, "usage: %s -t <tld> -n <ns> -i <ip> <[-4] [-6]> [-r <res_ip>] [-o <res/_port>] [-p <testprefix>]"
			" [-d] [-c] [-j <file>] [-f] [-h]\n", program);
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
	fprintf(stderr, "       -j <file>         write resulting json to the file\n");
	fprintf(stderr, "       -f                log packets to files (%s, %s) instead of stdout\n", LOG_FILE1, LOG_FILE2);
	fprintf(stderr, "       -h                show this message and quit\n");
	exit(EXIT_FAILURE);
}

int	main(int argc, char *argv[])
{
	char		*tld = NULL, *ns = NULL, *ns_ip = NULL, proto = RSM_UDP,
			ipv4_enabled = 0, ipv6_enabled = 0,
			testedname[ZBX_HOST_BUF_SIZE], dnssec_enabled = 0,
			*json_file = NULL,
			res_host_buf[ZBX_HOST_BUF_SIZE],
			nsip_buf[ZBX_HOST_BUF_SIZE],
			key[8192];
	const char	*res_ip = DEFAULT_RES_IP,
			*testprefix = DEFAULT_TESTPREFIX;
	int		c, index;
	FILE		*log_fd = stdout;
	int		res_port = DEFAULT_RES_PORT, ns_port = DEFAULT_NS_PORT;
	AGENT_REQUEST	request;
	AGENT_RESULT	result;

	opterr = 0;

	while ((c = getopt(argc, argv, "t:n:i:46r:o:s:p:dcj:fh")) != -1)
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

	zbx_snprintf(res_host_buf, sizeof(res_host_buf), "%s;%d",    res_ip, res_port);
	zbx_snprintf(nsip_buf,     sizeof(nsip_buf),     "%s,%s;%d", ns, ns_ip, ns_port);
	zbx_snprintf(testedname,   sizeof(testedname),   "%s.%s.",   testprefix, tld);

	rsm_infof(log_fd, "tld:%s ns:%s ip:%s res:%s testprefix:%s", tld, ns, ns_ip, res_ip, testprefix);

	init_request(&request);

	zbx_snprintf(key, sizeof(key), "rsm.dns[%s,%s,\"%s\",%d,%d,%d,%d,%d,%d,%d,%s,%d,%d,%d,%d,%d,%d]",
			tld, /* Rsmhost */
			testprefix, /* Test prefix */
			nsip_buf, /* List of Name Servers */
			dnssec_enabled, /* DNSSEC enabled on rsmhost */
			1, /* RDDS43 enabled on rsmhost */
			1, /* RDDS80 enabled on rsmhost */
			(RSM_UDP == proto), /* DNS UDP enabled */
			(RSM_TCP == proto), /* DNS TCP enabled */
			ipv4_enabled, /* IPv4 enabled */
			ipv6_enabled, /* IPv6 enabled */
			res_host_buf, /* IP address of local resolver */
			5000, /* maximum allowed UDP RTT */
			10000, /* maximum allowed TCP RTT */
			10, /* TCP ratio */
			2, /* successful tests to recover from critical mode (UDP) */
			2, /* successful tests to recover from critical mode (TCP) */
			1       /* minimum number of working name servers */
	);

	if (SUCCEED != parse_item_key(key, &request))
	{
		rsm_errf(stderr, "invalid item key format: %s", key);
		exit(-1);
	}

	init_result(&result);

	signal(SIGALRM, alarm_signal_handler);

	check_rsm_dns(0, 0, "tld1 Probe1", 0, &request, &result, stdout);

	if (ISSET_MSG(&result))
	{
		printf("FAILED: %s\n",  result.msg);
		exit(EXIT_FAILURE);
	}

	printf("OK: %s\n",  result.text);

	if (json_file)
	{
		char	*error = NULL;
		int	rv;

		rsm_infof(log_fd, "writing to %s...", json_file);

		rv = write_json_status(json_file, result.text, &error);

		zbx_free(error);

		if (rv != SUCCEED)
			exit(EXIT_FAILURE);
	}

	exit(EXIT_SUCCESS);
}
