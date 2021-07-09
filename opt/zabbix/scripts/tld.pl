#!/usr/bin/env perl

use FindBin;
use lib $FindBin::RealBin;

use strict;
use warnings;

use RSM;
use RSMSLV;
use TLD_constants qw(:config);
use Getopt::Long;
use Data::Dumper;
use JSON::XS;
use Text::CSV_XS;
use HTTP::Request;
use LWP::UserAgent;
use MIME::Base64;

use constant DNS_MINNS_DEFAULT		=> 2;
use constant DNS_MINNS_OFFSET_MINUTES	=> 15;

use constant HTTP_STATUS_OK		=> 200;
use constant HTTP_STATUS_NOT_FOUND	=> 404;

use constant JSON_TRUE			=> $Types::Serialiser::true;
use constant JSON_FALSE			=> $Types::Serialiser::false;

my $provisioning_api_config;

sub main($)
{
	my $ARGV = shift;

	init_cli_opts();

	info('command line:');
	info(join(' ', map(/\s/ ? "'$_'" : $_, ($0, @{$ARGV}))));

	$provisioning_api_config = get_rsm_config()->{'provisioning_api'};

	if (opt('list-services'))
	{
		cmd_list_services();
	}
	elsif (opt('get-nsservers-list'))
	{
		cmd_get_nsservers_list();
	}
	elsif (opt('disable'))
	{
		cmd_disable();
	}
	elsif (opt('delete'))
	{
		cmd_delete();
	}
	elsif (opt('update-nsservers'))
	{
		cmd_update_nsservers();
	}
	else
	{
		cmd_onboard();
	}
}

sub pfail($)
{
	my $message = shift;

	print(STDERR 'Error: ' . $message . "\n");

	fail($message);
}

sub cmd_list_services()
{
	my $rsmhost = getopt('tld');

	my ($code, $json) = http_get($rsmhost);

	if (defined($rsmhost))
	{
		$json = [$json];
	}

	$json = [grep($_->{'centralServer'} == getopt('server-id'), @{$json})];
	$json = [sort { $a->{'tld'} cmp $b->{'tld'} } @{$json}];

	my $csv_printer = Text::CSV_XS->new({'binary' => 1, 'auto_diag' => 1, 'always_quote' => 1, 'eol' => "\n"});

	foreach my $data (@{$json})
	{
		my %services = map { $_->{'service'} => $_->{'enabled'} } @{$data->{'servicesStatus'}};

		my $rdds43_test_prefix;
		if ($services{'rdds43'})
		{
			$rdds43_test_prefix = $data->{'rddsParameters'}{'rdds43TestedDomain'};
			$rdds43_test_prefix =~ s/^(.+)\.[^.]+$/$1/ if ($data->{'tld'} ne '.');
		}

		my $output = [
			$data->{'tld'},                                                              # tld
			$data->{'tldType'},                                                          # type
			$services{'dnsUDP'} || $services{'dnsTCP'} ? 0 : 1,                          # status
			$data->{'dnsParameters'}{'nsTestPrefix'},                                    # {$RSM.DNS.TESTPREFIX}
			$data->{'rddsParameters'}{'rdds43NsString'} // '',                           # {$RSM.RDDS.NS.STRING}
			$rdds43_test_prefix // '',                                                   # $rdds43_test_prefix
			$data->{'dnsParameters'}{'dnssecEnabled'} ? 1 : 0,                           # {$RSM.TLD.DNSSEC.ENABLED}
			'',                                                                          # {$RSM.TLD.EPP.ENABLED}
			$services{'rdds43'} || $services{'rdds80'} ? 1 : 0,                          # {$RSM.TLD.RDDS.ENABLED}
			$services{'rdap'} ? 1 : 0,                                                   # {$RDAP.TLD.ENABLED}
			$data->{'rddsParameters'}{'rdapUrl'} // '',                                  # {$RDAP.BASE.URL}
			$data->{'rddsParameters'}{'rdapTestedDomain'} // '',                         # {$RDAP.TEST.DOMAIN}
			$data->{'rddsParameters'}{'rdds43Server'} // '',                             # {$RSM.TLD.RDDS.43.SERVERS}
			($data->{'rddsParameters'}{'rdds80Url'} // '') =~ s!^https?://(.*?)/?$!$1!r, # {$RSM.TLD.RDDS.80.SERVERS}
			$data->{'rddsParameters'}{'rdds43TestedDomain'} // '',                       # {$RSM.RDDS43.TEST.DOMAIN}
			$data->{'dnsParameters'}{'minNs'},                                           # dns_minns
		];

		$csv_printer->print(*STDOUT, $output);
	}
}

sub cmd_get_nsservers_list()
{
	my $rsmhost = getopt('tld');

	my ($code, $json) = http_get($rsmhost);

	if (defined($rsmhost))
	{
		$json = [$json];
	}

	$json = [grep($_->{'centralServer'} == getopt('server-id'), @{$json})];
	$json = [sort { $a->{'tld'} cmp $b->{'tld'} } @{$json}];

	my $csv_printer = Text::CSV_XS->new({'binary' => 1, 'auto_diag' => 1, 'always_quote' => 1, 'eol' => "\n"});

	foreach my $data (@{$json})
	{
		my $nsip_lists = {};

		foreach my $nsip (@{$data->{'dnsParameters'}{'nsIps'}})
		{
			my $ip_version = $nsip->{'ip'} =~ /^\d+\.\d+\.\d+\.\d+$/ ? 'ipv4' : 'ipv6';
			push(@{$nsip_lists->{$ip_version}{$nsip->{'ns'}}}, $nsip->{'ip'});
		}

		foreach my $ip_version (sort(keys(%{$nsip_lists})))
		{
			foreach my $ns (sort(keys(%{$nsip_lists->{$ip_version}})))
			{
				foreach my $ip (sort(@{$nsip_lists->{$ip_version}{$ns}}))
				{
					my $output = [$data->{'tld'}, $ip_version, $ns, $ip];
					$csv_printer->print(*STDOUT, $output);
				}
			}
		}
	}
}

sub cmd_disable()
{
	my $rsmhost = getopt('tld');

	my ($code, $json) = http_get($rsmhost);

	my $tld_type = $json->{'tldType'};

	($code, $json) = http_put(
		$rsmhost,
		{
			"tldType" => $tld_type,
			"servicesStatus" => [
				{ "service" => "dnsUDP", "enabled" => JSON_FALSE },
				{ "service" => "dnsTCP", "enabled" => JSON_FALSE },
				{ "service" => "rdds43", "enabled" => JSON_FALSE },
				{ "service" => "rdds80", "enabled" => JSON_FALSE },
				{ "service" => "rdap"  , "enabled" => JSON_FALSE },
			],
			'centralServer' => getopt('server-id'),
		}
	);

	foreach my $line (@{$json->{'details'}{'info'}})
	{
		print($line . "\n");
	}
	print($json->{'title'} . "\n");
}

sub cmd_delete()
{
	my $rsmhost = getopt('tld');

	my ($code, $json) = http_delete($rsmhost);

	foreach my $line (@{$json->{'details'}{'info'}})
	{
		print($line . "\n");
	}
	print($json->{'title'} . "\n");
}

sub cmd_update_nsservers()
{
	my $rsmhost = getopt('tld');

	my ($code, $json) = http_get($rsmhost);

	my %services = map { $_->{'service'} => $_->{'enabled'} } @{$json->{'servicesStatus'}};

	my $config = {
		'tldType' => $json->{'tldType'},
		'servicesStatus' => [
			{ 'service' => 'dnsUDP', 'enabled' => $services{'dnsUDP'} ? JSON_TRUE : JSON_FALSE },
			{ 'service' => 'dnsTCP', 'enabled' => $services{'dnsTCP'} ? JSON_TRUE : JSON_FALSE },
			{ 'service' => 'rdds43', 'enabled' => $services{'rdds43'} ? JSON_TRUE : JSON_FALSE },
			{ 'service' => 'rdds80', 'enabled' => $services{'rdds80'} ? JSON_TRUE : JSON_FALSE },
			{ 'service' => 'rdap'  , 'enabled' => $services{'rdap'}   ? JSON_TRUE : JSON_FALSE },
		],
		'dnsParameters' => {
			'nsIps'         => build_nsip_list(),
			'dnssecEnabled' => $json->{'dnsParameters'}{'dnssecEnabled'} ? JSON_TRUE : JSON_FALSE,
			'nsTestPrefix'  => $json->{'dnsParameters'}{'nsTestPrefix'},
			'minNs'         => $json->{'dnsParameters'}{'minNs'},
		},
		'centralServer' => getopt('server-id'),
	};

	if ($services{'rdds43'})
	{
		$config->{'rddsParameters'}{'rdds43Server'}       = $json->{'rddsParameters'}('rdds43Server');
		$config->{'rddsParameters'}{'rdds43TestedDomain'} = $json->{'rddsParameters'}{'rdds43TestedDomain'};
		$config->{'rddsParameters'}{'rdds43NsString'}     = $json->{'rddsParameters'}{'rdds43NsString'};
	}

	if ($services{'rdds80'})
	{
		$config->{'rddsParameters'}{'rdds80Url'}          = $json->{'rddsParameters'}{'rdds80Url'};
	}

	if ($services{'rdap'})
	{
		$config->{'rddsParameters'}{'rdapUrl'}            = $json->{'rddsParameters'}{'rdapUrl'};
		$config->{'rddsParameters'}{'rdapTestedDomain'}   = $json->{'rddsParameters'}{'rdapTestedDomain'};
	}

	($code, $json) = http_put($rsmhost, $config);

	foreach my $line (@{$json->{'details'}{'info'}})
	{
		print($line . "\n");
	}
	print($json->{'title'} . "\n");
}

sub cmd_onboard()
{
	my $rsmhost = getopt('tld');

	my $config = {
		'tldType' => getopt('type'),
		'servicesStatus' => [
			{ 'service' => 'dnsUDP', 'enabled' => JSON_TRUE },
			{ 'service' => 'dnsTCP', 'enabled' => JSON_TRUE },
			{ 'service' => 'rdds43', 'enabled' => opt('rdds43-servers') ? JSON_TRUE : JSON_FALSE },
			{ 'service' => 'rdds80', 'enabled' => opt('rdds80-servers') ? JSON_TRUE : JSON_FALSE },
			{ 'service' => 'rdap'  , 'enabled' => opt('rdap-base-url')  ? JSON_TRUE : JSON_FALSE },
		],
		'dnsParameters' => {
			'nsIps'         => build_nsip_list(),
			'dnssecEnabled' => opt('dnssec') ? JSON_TRUE : JSON_FALSE,
			'nsTestPrefix'  => getopt('dns-test-prefix'),
			'minNs'         => DNS_MINNS_DEFAULT,
		},
		'centralServer' => getopt('server-id'),
	};

	if (opt('rdds43-servers'))
	{
		$config->{'rddsParameters'}{'rdds43Server'}       = getopt('rdds43-servers');
		$config->{'rddsParameters'}{'rdds43TestedDomain'} = getopt('rdds43-test-domain');
		$config->{'rddsParameters'}{'rdds43NsString'}     = getopt('rdds-ns-string') // CFG_DEFAULT_RDDS_NS_STRING;
	}

	if (opt('rdds80-servers'))
	{
		$config->{'rddsParameters'}{'rdds80Url'}          = 'http://' . getopt('rdds80-servers') . '/';
	}

	if (opt('rdap-base-url'))
	{
		$config->{'rddsParameters'}{'rdapUrl'}            = getopt('rdap-base-url');
		$config->{'rddsParameters'}{'rdapTestedDomain'}   = getopt('rdap-test-domain');
	}

	my ($code, $json) = http_put($rsmhost, $config);

	foreach my $line (@{$json->{'details'}{'info'}})
	{
		print($line . "\n");
	}
	print($json->{'title'} . "\n");
}

sub build_nsip_list()
{
	my @nsip_list = ();
	push(@nsip_list, split(/\s+/, getopt('ns-servers-v4'))) if (opt('ipv4') && getopt('ns-servers-v4'));
	push(@nsip_list, split(/\s+/, getopt('ns-servers-v6'))) if (opt('ipv6') && getopt('ns-servers-v6'));
	@nsip_list = sort(cmp_nsip @nsip_list);

	my @result = ();

	foreach my $nsip (@nsip_list)
	{
		if ($nsip !~ /^(.+),(.+)$/)
		{
			pfail('invalid format of ns,ip pair: "' . $nsip . '"');
		}

		push(@result, {'ns' => $1, 'ip' => $2});
	}

	return [@result];
}

sub cmp_nsip($$)
{
	my ($a_ns, $a_ip) = split(/,/, shift);
	my ($b_ns, $b_ip) = split(/,/, shift);

	# compare hostnames

	if ($a_ns ne $b_ns)
	{
		return $a_ns cmp $b_ns;
	}

	# compare ip versions (put ipv4 before ipv6)

	my $a_is_ipv4 = ($a_ip =~ /^\d+\.\d+\.\d+\.\d+$/) ? 1 : 0;
	my $b_is_ipv4 = ($b_ip =~ /^\d+\.\d+\.\d+\.\d+$/) ? 1 : 0;

	if ($a_is_ipv4 != $b_is_ipv4)
	{
		return $b_is_ipv4 - $a_is_ipv4;
	}

	# compare ip addresses

	return $a_ip cmp $b_ip;
}

sub http_get($)
{
	my $rsmhost = shift;

	return http_request($rsmhost, 'GET', undef);
}

sub http_delete($)
{
	my $rsmhost = shift;

	return http_request($rsmhost, 'DELETE', undef);
}

sub http_put($$)
{
	my $rsmhost = shift;
	my $json    = shift;

	return http_request($rsmhost, 'PUT', $json);
}

sub http_request($$$)
{
	my $rsmhost = shift;
	my $method  = shift;
	my $json    = shift;

	my $url     = get_url($rsmhost);
	my $payload = defined($json) ? encode_json($json) : undef;

	info('sending request (method: "' . $method . '"; url: "' . $url . '")');
	if (defined($payload))
	{
		info('payload:');
		info($_) foreach (split(/\n/, $payload));
	}

	my $user_agent = LWP::UserAgent->new();
	my $request = HTTP::Request->new($method, $url, undef, $payload);

	$request->header('Authorization' => 'Basic ' . encode_base64(get_username($method) . ':' . get_password($method)));

	my $response = $user_agent->simple_request($request);

	my $code = $response->code();
	my $type = $response->header('content-type');
	my $body = $response->content();

	info('received (status code: ' . $code . '):');
	info($_) foreach (split(/\n/, $body));

	http_validate_response($rsmhost, $code, $type);

	$json = JSON::XS->new()->boolean_values(0, 1)->decode($body);

	return $code, $json;
}

sub http_validate_response($$$)
{
	my $rsmhost      = shift;
	my $status_code  = shift;
	my $content_type = shift;

	if (!defined($content_type) || $content_type ne 'application/json')
	{
		pfail('unexpected content type: "' . ($content_type // 'undef') . '"');
	}
	if ($status_code == HTTP_STATUS_NOT_FOUND)
	{
		pfail('monitored object not found: "' . ($rsmhost // 'undef') . '"');
	}
	if ($status_code != HTTP_STATUS_OK)
	{
		pfail('unexpected status code: "' . ($status_code // 'undef') . '"');
	}
}

sub get_url($)
{
	my $rsmhost = shift;

	return ($provisioning_api_config->{'url'} =~ s!^(.*?)/?$!$1!r) . '/tlds' . (defined($rsmhost) ? '/' . $rsmhost : '');
}

sub get_username($)
{
	my $method = shift;

	return $provisioning_api_config->{'readonly_username'}  if ($method eq 'GET');
	return $provisioning_api_config->{'readwrite_username'} if ($method eq 'DELETE');
	return $provisioning_api_config->{'readwrite_username'} if ($method eq 'PUT');

	pfail('unhandled request method: "' . $method . '"');
}

sub get_password($)
{
	my $method = shift;

	return $provisioning_api_config->{'readonly_password'}  if ($method eq 'GET');
	return $provisioning_api_config->{'readwrite_password'} if ($method eq 'DELETE');
	return $provisioning_api_config->{'readwrite_password'} if ($method eq 'PUT');

	pfail('unhandled request method: "' . $method . '"');
}

sub init_cli_opts()
{
	my %OPTS;
	my $rv = GetOptions(\%OPTS,
			'server-id=i',
			'list-services',
			'get-nsservers-list',
			'update-nsservers',
			'delete',
			'disable',
			'tld=s',
			'type=s',
			'rdds43-servers=s',
			'rdds80-servers=s',
			'rdap-base-url=s',
			'rdap-test-domain=s',
			'dns-test-prefix=s',
			'rdds43-test-domain=s',
			'rdds-ns-string=s',
			'ipv4',
			'ipv6',
			'dnssec',
			'ns-servers-v4=s',
			'ns-servers-v6=s',
			'nolog',
			'help');

	if (!$rv || !%OPTS || $OPTS{'help'})
	{
		__usage();
	}

	override_opts(\%OPTS);

	validate_input();
	lc_options();
}

sub validate_input()
{
	# %opts is for reporting unexpected options, e.g., if --dnssec is specified with --disabled
	my %opts = map { $_ => undef } optkeys();
	delete($opts{'nolog'});

	# $error is for building a list of error messages
	my $error = '';

	require_input(\$error, 'server-id');

	if (opt('list-services'))
	{
		# --server-id <server-id> --list-services [--tld <tld>]

		delete($opts{'server-id'});
		delete($opts{'list-services'});
		delete($opts{'tld'});
	}
	elsif (opt('get-nsservers-list'))
	{
		# --server-id <server-id> --get-nsservers-list [--tld <tld>]

		delete($opts{'server-id'});
		delete($opts{'get-nsservers-list'});
		delete($opts{'tld'});
	}
	elsif (opt('disable'))
	{
		# --server-id <server-id> --disable --tld <tld>

		require_input(\$error, 'tld');

		delete($opts{'server-id'});
		delete($opts{'disable'});
		delete($opts{'tld'});
	}
	elsif (opt('delete'))
	{
		# --server-id <server-id> --delete --tld <tld>

		require_input(\$error, 'tld');

		delete($opts{'server-id'});
		delete($opts{'delete'});
		delete($opts{'tld'});
	}
	elsif (opt('update-nsservers'))
	{
		# --server-id <server-id> --update-nsservers --tld <tld> [--ipv4 --ns-servers-v4 <list>] [--ipv6 --ns-servers-v6]

		require_input(\$error, 'tld');

		if (!opt('ipv4') && !opt('ipv6'))
		{
			$error .= "Missing option: --ipv4 and/or --ipv6\n";
		}
		if (opt('ipv4') || opt('ns-servers-v4'))
		{
			require_input(\$error, 'ipv4');
			require_input(\$error, 'ns-servers-v4');
		}
		if (opt('ipv6') || opt('ns-servers-v6'))
		{
			require_input(\$error, 'ipv6');
			require_input(\$error, 'ns-servers-v6');
		}

		delete($opts{'server-id'});
		delete($opts{'update-nsservers'});
		delete($opts{'tld'});
		delete($opts{'ipv4'});
		delete($opts{'ns-servers-v4'});
		delete($opts{'ipv6'});
		delete($opts{'ns-servers-v6'});
	}
	else
	{
		# --server-id <server-id> --tld <tld> --dns-test-prefix <dns-test-prefix> --type <type>
		# [--dnssec]
		# [--ipv4 --ns-servers-v4 <list>]
		# [--ipv6 --ns-servers-v6 <list>]
		# [--rdds43-servers <server> --rdds43-test-domain <domain> --rdds-ns-string <ns-string> --rdds80-servers <server>]
		# [--rdap-base-url <url> --rdap-test-domain <domain>]

		require_input(\$error, 'tld');
		require_input(\$error, 'dns-test-prefix');
		require_input(\$error, 'type');

		if (!opt('ipv4') && !opt('ipv6'))
		{
			$error .= "Missing option: --ipv4 and/or --ipv6\n";
		}
		if (opt('ipv4') || opt('ns-servers-v4'))
		{
			require_input(\$error, 'ipv4');
			require_input(\$error, 'ns-servers-v4');
		}
		if (opt('ipv6') || opt('ns-servers-v6'))
		{
			require_input(\$error, 'ipv6');
			require_input(\$error, 'ns-servers-v6');
		}
		if (opt('rdds43-servers') || opt('rdds43-test-domain') || opt('rdds80-servers'))
		{
			require_input(\$error, 'rdds43-servers');
			require_input(\$error, 'rdds43-test-domain');
			require_input(\$error, 'rdds80-servers');
		}
		if (opt('rdap-base-url') || opt('rdap-test-domain'))
		{
			require_input(\$error, 'rdap-base-url');
			require_input(\$error, 'rdap-test-domain');
		}

		delete($opts{'server-id'});
		delete($opts{'tld'});
		delete($opts{'dns-test-prefix'});
		delete($opts{'type'});
		delete($opts{'dnssec'});
		delete($opts{'ipv4'});
		delete($opts{'ns-servers-v4'});
		delete($opts{'ipv6'});
		delete($opts{'ns-servers-v6'});
		delete($opts{'rdds43-servers'});
		delete($opts{'rdds43-test-domain'});
		delete($opts{'rdds80-servers'});
		delete($opts{'rdds-ns-string'});
		delete($opts{'rdap-base-url'});
		delete($opts{'rdap-test-domain'});
	}

	if (%opts)
	{
		$error .= "Unexpected options: " . join(', ', map("--$_", keys(%opts)));
	}

	if ($error)
	{
		__usage($error);
	}
}

sub require_input($$)
{
	my $messages_ref = shift;
	my $option       = shift;

	if (!opt($option))
	{
		${$messages_ref} .= 'Missing option: --' . $option . "\n";
	}
}

sub lc_options()
{
	my @options_to_lowercase = (
		"tld",
		"rdds43-servers",
		"rdds80-servers",
		"ns-servers-v4",
		"ns-servers-v6",
	);

	foreach my $option (@options_to_lowercase)
	{
		if (opt($option))
		{
			setopt($option, lc(getopt($option)));
		}
	}
}

sub __usage($)
{
	my $error_message = shift;

	if ($error_message)
	{
		print($error_message, "\n\n");
	}

	print <<EOF;
Usage: $0 [options]

Required options

	--tld=STRING
		TLD name
	--dns-test-prefix=STRING
		domain test prefix for DNS monitoring

Other options
	--delete
		delete specified TLD or TLD services specified by: --dns, --rdds, --rdap
		if none or all services specified - will delete the whole TLD
	--disable
		disable specified TLD or TLD services specified by: --dns, --rdds, --rdap
		if none or all services specified - will disable the whole TLD
	--list-services
		list services of each TLD, the output is comma-separated list:
		<TLD>,<TLD-TYPE>,<TLD-STATUS>,<DNS.TESTPREFIX>,<RDDS.NS.STRING>,<RDDS43.TEST.PREFIX>,
		<TLD.DNSSEC.ENABLED>,<TLD.EPP.ENABLED>,<TLD.RDDS.ENABLED>,<TLD.RDAP.ENABLED>,
		<RDAP.BASE.URL>,<RDAP.TEST.DOMAIN>,<RDDS43.SERVERS>,<RDDS80.SERVERS>,<RDDS43.TEST.DOMAIN>,<DNS.MINNS>
	--get-nsservers-list
		CSV formatted list of NS + IP server pairs for specified TLD:
		<TLD>,<IP-VERSION>,<NAME-SERVER>,<IP>
	--update-nsservers
		update all NS + IP pairs for specified TLD.
	--type=STRING
		Type of TLD. Possible values: @{[TLD_TYPE_G]}, @{[TLD_TYPE_CC]}, @{[TLD_TYPE_OTHER]}, @{[TLD_TYPE_TEST]}.
	--ipv4
		enable IPv4
		(default: disabled)
	--ipv6
		enable IPv6
		(default: disabled)
	--dnssec
		enable DNSSEC in DNS tests
		(default: disabled)
	--ns-servers-v4=STRING
		list of IPv4 name servers separated by space (name and IP separated by comma): "NAME,IP[ NAME,IP2 ...]"
	--ns-servers-v6=STRING
		list of IPv6 name servers separated by space (name and IP separated by comma): "NAME,IP[ NAME,IP2 ...]"
	--rdds43-servers=STRING
		list of RDDS43 servers separated by comma: "NAME1,NAME2,..."
	--rdds80-servers=STRING
		list of RDDS80 servers separated by comma: "NAME1,NAME2,..."
	--rdap-base-url=STRING
		RDAP service endpoint, e.g. "http://rdap.nic.cz"
		Specify "not listed" to get error -390, e. g. --rdap-base-url="not listed"
		Specify "no https" to get error -391, e. g. --rdap-base-url="no https"
	--rdap-test-domain=STRING
		test domain for RDAP queries
	--rdds-ns-string=STRING
		name server prefix in the WHOIS output
		(default: "${\CFG_DEFAULT_RDDS_NS_STRING}")
	--server-id=STRING
		ID of Zabbix server
	--rdds43-test-domain=STRING
		test domain for RDDS monitoring (needed only if rdds servers specified)
	--nolog
		print output to stdout and stderr instead of a log file
	--help
		display this message
EOF
	exit(1);
}

main([@ARGV]);
