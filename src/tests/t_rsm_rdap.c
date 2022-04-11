#include "../zabbix_server/poller/checks_simple_rsm.c"
#include "t_rsm_decl.h"
#include "t_rsm.h"

#define DEFAULT_RES_PORT	53
#define DEFAULT_MAXREDIRS	10

void	zbx_on_exit(int ret)
{
	ZBX_UNUSED(ret);
}

static void	exit_usage(const char *program)
{
	fprintf(stderr, "usage: %s -r <ip> [-o <res_port>] -u <base_url> -d <testedname> <[-4 -6]> [-j <file>] [-h]\n",
			program);
	fprintf(stderr, "       -r <res_ip>       local resolver IP\n");
	fprintf(stderr, "       -o <res_port>     port of resolver to use (default: %hu)\n", DEFAULT_RES_PORT);
	fprintf(stderr, "       -u <base_url>     RDAP service endpoint\n");
	fprintf(stderr, "       -d <testedname>   domain name to use in RDAP query\n");
	fprintf(stderr, "       -4                enable IPv4\n");
	fprintf(stderr, "       -6                enable IPv6\n");
	fprintf(stderr, "       -h                show this message and quit\n");
	fprintf(stderr, "       -j <file>         write resulting json to the file\n");
	exit(EXIT_FAILURE);
}

int	main(int argc, char *argv[])
{
	int		c, index,
			maxredirs = DEFAULT_MAXREDIRS,
			res_port = DEFAULT_RES_PORT;
	char		*testedname = NULL, *base_url = NULL, *res_ip = NULL,
			ipv4_enabled = 0, ipv6_enabled = 0, *json_file = NULL,
			key[8192],
			res_host_buf[ZBX_HOST_BUF_SIZE];
	AGENT_REQUEST	request;
	AGENT_RESULT	result;

	while ((c = getopt(argc, argv, "r:o:u:d:46j:h")) != -1)
	{
		switch (c)
		{
			case 'r':
				res_ip = optarg;
				break;
			case 'o':
				res_port = atoi(optarg);
				break;
			case 'u':
				base_url = optarg;
				break;
			case 'd':
				testedname = optarg;
				break;
			case '4':
				ipv4_enabled = 1;
				break;
			case '6':
				ipv6_enabled = 1;
				break;
			case 'j':
				json_file = optarg;
				break;
			case 'h':
				exit_usage(argv[0]);
				/* fall through */
			case '?':
				if (optopt == 'r' || optopt == 'u' || optopt == 'd')
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

	if (NULL == res_ip || '\0' == *res_ip)
	{
		fprintf(stderr, "Name Server IP [-r] must be specified\n");
		exit_usage(argv[0]);
	}

	if (NULL == base_url || '\0' == *base_url)
	{
		fprintf(stderr, "Base URL [-u] must be specified\n");
		exit_usage(argv[0]);
	}

	if (NULL == testedname || '\0' == *testedname)
	{
		fprintf(stderr, "Test domain [-d] must be specified\n");
		exit_usage(argv[0]);
	}

	if (0 == ipv4_enabled && 0 == ipv6_enabled)
	{
		fprintf(stderr, "at least one IP version [-4, -6] must be specified\n");
		exit_usage(argv[0]);
	}

	zbx_snprintf(res_host_buf,  sizeof(res_host_buf),  "%s;%d", res_ip, res_port);

	printf("IP: %s, URL: %s, Test domain: %s\n", res_host_buf, base_url, testedname);

	zbx_snprintf(key, sizeof(key), "rsm.rdds[%s,%s,%s,%d,%d,%d,%d,%d,%d,%s]",
			"example",	/* Rsmhost */
			testedname,	/* test domain */
			base_url,	/* RDAP service endpoint */
			maxredirs,	/* maximal number of redirections allowed */
			10000,		/* {$RSM.RDAP.RTT.HIGH} */
			1,		/* RDAP enabled for TLD */
			1,		/* RDAP enabled for probe */
			ipv4_enabled,	/* IPv4 enabled */
			ipv6_enabled,	/* IPv6 enabled */
			res_host_buf	/* IP address of local resolver */
	);

	if (SUCCEED != parse_item_key(key, &request))
	{
		rsm_errf(stderr, "invalid item key format: %s", key);
		exit(EXIT_FAILURE);
	}

	init_result(&result);

	signal(SIGALRM, alarm_signal_handler);

	check_rsm_rdap("example Probe1", &request, &result, stdout);

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

		rsm_infof(stdout, "writing to %s...", json_file);

		rv = write_json_status(json_file, result.text, &error);

		zbx_free(error);

		if (rv != SUCCEED)
			exit(EXIT_FAILURE);
	}

	exit(EXIT_SUCCESS);
}
