#include "t_rsm.h"
#include "../zabbix_server/poller/checks_simple_rsm.c"

#define DEFAULT_MAXREDIRS	10
#define DEFAULT_RTT_LIMIT	20

void	zbx_on_exit(int ret)
{
	ZBX_UNUSED(ret);
}

void	exit_usage(const char *program)
{
	fprintf(stderr, "usage: %s -r <ip> -u <base_url> -d <test_domain> [-h]\n", program);
	fprintf(stderr, "       -r <res_ip>       local resolver IP\n");
	fprintf(stderr, "       -u <base_url>     RDAP service endpoint\n");
	fprintf(stderr, "       -d <test_domain>  testing domain to make RDAP query\n");
	fprintf(stderr, "       -h                show this message and quit\n");
	exit(EXIT_FAILURE);
}

int	main(int argc, char *argv[])
{
	zbx_vector_str_t	ips;
	zbx_resolver_error_t	ec_res;
	curl_data_t		data = {NULL, 0, 0};
	zbx_http_error_t	ec_http;
	int			c, index, port, rtt_limit = DEFAULT_RTT_LIMIT, rtt = ZBX_NO_VALUE, ipv4_enabled = 1,
				ipv6_enabled = 1, maxredirs = DEFAULT_MAXREDIRS, ipv_flags = 0, curl_flags = 0;
	struct zbx_json_parse	jp;
	ldns_resolver		*res = NULL;
	char			*test_domain, *base_url, is_ipv4,  rdap_prefix[64], *res_ip = NULL, *proto = NULL,
				*domain_part = NULL, *prefix = NULL, *full_url = NULL, *value_str = NULL,
				err[ZBX_ERR_BUF_SIZE];
	const char		*ip = NULL;
	size_t			value_alloc = 0;

	zbx_vector_str_create(&ips);

	while ((c = getopt (argc, argv, "r:u:d:h")) != -1)
	{
		switch (c)
		{
			case 'r':
				res_ip = optarg;
				break;
			case 'u':
				base_url = optarg;
				break;
			case 'd':
				test_domain = optarg;
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

	if (NULL == test_domain || '\0' == *test_domain)
	{
		fprintf(stderr, "Test domain [-d] must be specified\n");
		exit_usage(argv[0]);
	}

	printf("IP: %s, URL: %s , Test domain: %s\n", res_ip, base_url, test_domain);

	if (SUCCEED != zbx_create_resolver(&res, "resolver", res_ip, RSM_TCP, ipv4_enabled, ipv6_enabled,
			RESOLVER_EXTRAS_DNSSEC, RSM_TCP_TIMEOUT, RSM_TCP_RETRY, stderr, err, sizeof(err)))
	{
		rsm_errf(stderr, "cannot create resolver: %s", err);
		goto out;
	}

	if (0 == strcmp(base_url, "not listed"))
	{
		rsm_err(stderr, "The TLD is not listed in the Bootstrap Service Registry for Domain Name Space");
		rtt = ZBX_EC_RDAP_NOTLISTED;
		goto out;
	}

	if (0 == strcmp(base_url, "no https"))
	{
		rsm_err(stderr, "The RDAP base URL obtained from Bootstrap Service Registry for Domain Name Space"
				" does not use HTTPS");
		rtt = ZBX_EC_RDAP_NOHTTPS;
		goto out;
	}

	ipv_flags |= ZBX_FLAG_IPV4_ENABLED;
	ipv_flags |= ZBX_FLAG_IPV6_ENABLED;

	if (SUCCEED != zbx_split_url(base_url, &proto, &domain_part, &port, &prefix, err, sizeof(err)))
	{
		rtt = ZBX_EC_RDAP_INTERNAL_GENERAL;
		rsm_errf(stderr, "RDAP \"%s\": %s", base_url, err);
		goto out;
	}

	/* resolve host to IPs */
	if (SUCCEED != zbx_resolver_resolve_host(res, domain_part, &ips, ipv_flags, stderr, &ec_res, err, sizeof(err)))
	{
		rtt = zbx_resolver_error_to_RDAP(ec_res);
		rsm_errf(stderr, "RDAP \"%s\": %s", base_url, err);
		goto out;
	}

	if (0 == ips.values_num)
	{
		rtt = ZBX_EC_RDAP_INTERNAL_IP_UNSUP;
		rsm_errf(stderr, "RDAP \"%s\": IP address(es) of host \"%s\" are not supported by the Probe",
				base_url, domain_part);
		goto out;
	}

	/* choose random IP */
	ip = ips.values[zbx_random(ips.values_num)];

	if (SUCCEED != zbx_validate_ip(ip, ipv4_enabled, ipv6_enabled, NULL, &is_ipv4))
	{
		rtt = ZBX_EC_RDAP_INTERNAL_GENERAL;
		rsm_errf(stderr, "internal error, selected unsupported IP of \"%s\": \"%s\"", domain_part, ip);
		goto out;
	}

	if ('\0' != *prefix && prefix[strlen(prefix) - 1] == '/')
		zbx_strlcpy(rdap_prefix, "domain", sizeof(rdap_prefix));
	else
		zbx_strlcpy(rdap_prefix, "/domain", sizeof(rdap_prefix));

	if (0 == is_ipv4)
	{
		full_url = zbx_dsprintf(full_url, "%s[%s]:%d%s%s/%s", proto, ip, port, prefix, rdap_prefix,
				test_domain);
	}
	else
	{
		full_url = zbx_dsprintf(full_url, "%s%s:%d%s%s/%s", proto, ip, port, prefix, rdap_prefix, test_domain);
	}

	/* base_url example: http://whois.springbank */
	/* full_url example: http://172.19.0.2:80/domain/whois.springbank */

	rsm_infof(stderr, "the domain in base URL \"%s\" was resolved to %s, using full URL \"%s\".",
			base_url, ip, full_url);

	if (SUCCEED != zbx_http_test(domain_part, full_url, RSM_TCP_TIMEOUT, maxredirs, &ec_http, &rtt, &data,
			curl_memory, curl_flags, err, sizeof(err)))
	{
		rtt = zbx_http_error_to_RDAP(ec_http);
		rsm_errf(stderr, "test of \"%s\" (%s) failed: %s (%d)", base_url, full_url, err, rtt);
		goto out;
	}

	rsm_infof(stderr, "got response ===>\n%.*s\n<===", ZBX_RDDS_PREVIEW_SIZE, ZBX_NULL2STR(data.buf));

	if (NULL == data.buf || '\0' == *data.buf || SUCCEED != zbx_json_open(data.buf, &jp))
	{
		rtt = ZBX_EC_RDAP_EJSON;
		rsm_errf(stderr, "invalid JSON format in response of \"%s\" (%s)", base_url, ip);
		goto out;
	}

	if (SUCCEED != zbx_json_value_by_name_dyn(&jp, "ldhName", &value_str, &value_alloc, NULL))
	{
		rtt = ZBX_EC_RDAP_NONAME;
		rsm_errf(stderr, "ldhName member not found in response of \"%s\" (%s)", base_url, ip);
		goto out;
	}

	if (NULL == value_str || 0 != strcmp(value_str, test_domain))
	{
		rtt = ZBX_EC_RDAP_ENAME;
		rsm_errf(stderr, "ldhName member doesn't match query in response of \"%s\" (%s)", base_url, ip);
		goto out;
	}

	printf("end test of \"%s\" (%s) (rtt:%d)\n", base_url, ZBX_NULL2STR(ip), rtt);
out:
	if (ZBX_NO_VALUE != rtt)
	{
		int		subtest_result = 0;
		struct zbx_json	json;

		switch (zbx_subtest_result(rtt, rtt_limit))
		{
			case ZBX_SUBTEST_SUCCESS:
				subtest_result = 1;
				break;
			case ZBX_SUBTEST_FAIL:
				subtest_result = 0;
		}

		create_rsm_rdap_json(&json, ip, rtt, subtest_result);
		printf("OK, json: %s\n", json.buffer);
		zbx_json_free(&json);
	}

	if (NULL != res)
	{
		if (0 != ldns_resolver_nameserver_count(res))
			ldns_resolver_deep_free(res);
		else
			ldns_resolver_free(res);
	}

	zbx_free(proto);
	zbx_free(domain_part);
	zbx_free(prefix);
	zbx_free(full_url);
	zbx_free(value_str);
	zbx_free(data.buf);

	zbx_vector_str_clean_and_destroy(&ips);

	exit(EXIT_SUCCESS);
}
