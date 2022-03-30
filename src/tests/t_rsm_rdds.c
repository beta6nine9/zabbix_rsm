#include "t_rsm.h"
#include "t_rsm_decl.h"
#include "../zabbix_server/poller/checks_simple_rsm.c"

#define DEFAULT_RES_IP		"127.0.0.1"
#define DEFAULT_RES_PORT	53
#define DEFAULT_RDDS43_PORT	43
#define DEFAULT_RDDS80_PORT	80
#define DEFAULT_TESTPREFIX	"whois.nic"
#define DEFAULT_MAXREDIRS	10
#define DEFAULT_RDDS_NS_STRING	"Name Server:"
#define DEFAULT_RTT_LIMIT	2000

#define LOG_FILE1	"test1.log"
#define LOG_FILE2	"test2.log"

void	zbx_on_exit(int ret)
{
	ZBX_UNUSED(ret);
}

static void	exit_usage(const char *program)
{
	fprintf(stderr, "usage: %s -t <tld> -a <testedname43> -w <testedname80> <[-4] [-6]> [-s whois port]"
			" [-g web-whois port] [-r <res_ip>] [-o <res_port>] [-p <testprefix>] [-e <maxredirs80>]"
			" [-j <file>] [-f] [-h]\n", program);
	fprintf(stderr, "       -t <tld>          TLD to test\n");
	fprintf(stderr, "       -a <testedname43> WHOIS server to use for RDDS43 test\n");
	fprintf(stderr, "       -w <testedname80> WEB-WHOIS URL to use for RDDS80 test\n");
	fprintf(stderr, "       -s <rdds43_port>  WHOIS server port\n");
	fprintf(stderr, "       -g <rdds80_port>  WEB-WHOIS server port\n");
	fprintf(stderr, "       -4                enable IPv4\n");
	fprintf(stderr, "       -6                enable IPv6\n");
	fprintf(stderr, "       -r <res_ip>       IP address of resolver to use (default: %s)\n", DEFAULT_RES_IP);
	fprintf(stderr, "       -o <res_port>     port of resolver to use (default: %hu)\n", DEFAULT_RES_PORT);
	fprintf(stderr, "       -p <testprefix>   TLD prefix to use in RDDS43/RDDS80 tests (default: %s)\n",
			DEFAULT_TESTPREFIX);
	fprintf(stderr, "       -e <maxredirs80>  maximum redirections to use in RDDS80 test (default: %d)\n",
			DEFAULT_MAXREDIRS);
	fprintf(stderr, "       -j <file>         write resulting json to the file\n");
	fprintf(stderr, "       -f                log packets to files (%s, %s) (default: stdout)\n",
			LOG_FILE1, LOG_FILE2);
	fprintf(stderr, "       -h                show this message and quit\n");
	exit(EXIT_FAILURE);
}

int	main(int argc, char *argv[])
{
	char			*tld = NULL, *testedname43 = NULL, *testedname80 = NULL,
				*res_ip = DEFAULT_RES_IP, ipv4_enabled = 0,
				ipv6_enabled = 0, *testprefix = DEFAULT_TESTPREFIX,
				*json_file = NULL, key[8192],
				res_host_buf[ZBX_HOST_BUF_SIZE], rdds43_host_buf[ZBX_HOST_BUF_SIZE],
				rdds80_host_buf[ZBX_HOST_BUF_SIZE];
	int			c, index,
				maxredirs = DEFAULT_MAXREDIRS;
	FILE			*log_fd = stdout;
	uint16_t		res_port = DEFAULT_RES_PORT, rdds43_port = DEFAULT_RDDS43_PORT,
				rdds80_port = DEFAULT_RDDS80_PORT;
	AGENT_REQUEST		request;
	AGENT_RESULT		result;

	opterr = 0;

	while ((c = getopt(argc, argv, "t:a:w:46r:o:s:g:p:e:j:fh")) != -1)
	{
		switch (c)
		{
			case 't':
				tld = optarg;
				break;
			case 'a':
				testedname43 = optarg;
				break;
			case 'w':
				testedname80 = optarg;
				break;
			case 's':
				rdds43_port = atoi(optarg);
				break;
			case 'g':
				rdds80_port = atoi(optarg);
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
			case 'p':
				testprefix = optarg;
				break;
			case 'e':
				maxredirs = atoi(optarg);
				break;
			case 'j':
				json_file = optarg;
				break;
			case 'h':
				exit_usage(argv[0]);
				/* fall through */
			case '?':
				if (optopt == 't' || optopt == 'r' || optopt == 'w' || optopt == 'p' || optopt == 'm')
				{
					fprintf(stderr, "Option -%c requires an argument.\n", optopt);
				}
				else if (isprint(optopt))
				{
					fprintf(stderr, "Unknown option `-%c'.\n", optopt);
				}
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

	if (NULL == testedname43)
	{
		fprintf(stderr, "WHOIS server [-a] must be specified\n");
		exit_usage(argv[0]);
	}

	if (NULL == testedname80)
	{
		fprintf(stderr, "WEB-WHOIS server [-w] must be specified\n");
		exit_usage(argv[0]);
	}

	if (0 == ipv4_enabled && 0 == ipv6_enabled)
	{
		fprintf(stderr, "at least one IP version [-4, -6] must be specified\n");
		exit_usage(argv[0]);
	}

	init_request(&request);

	zbx_snprintf(rdds43_host_buf, sizeof(rdds43_host_buf), "%s;%hu", testedname43, rdds43_port);
	zbx_snprintf(rdds80_host_buf, sizeof(rdds80_host_buf), "%s:%hu", testedname80, rdds80_port);
	zbx_snprintf(res_host_buf,    sizeof(res_host_buf),    "%s;%hu", res_ip,       res_port);

	zbx_snprintf(key, sizeof(key), "rsm.rdds[%s,%s,%s,%s,%s,%d,%d,%d,%d,%d,%s,%d,%d]",
			tld,		/* Rsmhost */
			rdds43_host_buf,/* rdds43_host:port */	
			rdds80_host_buf,/* rdds80_host:port */
			testprefix,
			"Name Server:",	/* {$RSM.RDDS.NS.STRING} */
			1,		/* probe:rdds */
			1,		/* tld:rdds43 */
			1,		/* tld:rdds80 */
			ipv4_enabled,	/* probe:ipv4 */
			ipv6_enabled,	/* probe:ipv6 */
			res_host_buf,	/* resolver ip */
			10000,		/* {$RSM.RDDS.RTT.HIGH} */
			maxredirs
	);

	if (SUCCEED != parse_item_key(key, &request))
	{
		rsm_errf(stderr, "invalid item key format: %s", key);
		exit(-1);
	}

	init_result(&result);

	signal(SIGALRM, alarm_signal_handler);

	check_rsm_rdds("tld1 Probe1", &request, &result, stdout);

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
