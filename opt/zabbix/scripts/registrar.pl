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
	elsif (opt('disable'))
	{
		cmd_disable();
	}
	elsif (opt('delete'))
	{
		cmd_delete();
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
	my $rsmhost = getopt('rr-id');

	my ($code, $json) = http_get($rsmhost);

	if (defined($rsmhost))
	{
		$json = [$json];
	}

	$json = [grep($_->{'centralServer'} == getopt('server-id'), @{$json})];
	$json = [sort { $a->{'registrar'} <=> $b->{'registrar'} } @{$json}];

	my $csv_printer = Text::CSV_XS->new({'binary' => 1, 'auto_diag' => 1, 'always_quote' => 1, 'eol' => "\n"});

	foreach my $data (@{$json})
	{
		my %services = map { $_->{'service'} => $_->{'enabled'} } @{$data->{'servicesStatus'}};

		my $output = [
			$data->{'registrar'},                                                        # registrar id
			$data->{'registrarName'},                                                    # registrar name
			$data->{'registrarFamily'},                                                  # registrar family
			$services{'rdap'} || $services{'rdds43'} || $services{'rdds80'} ? 0 : 1,     # status
			$data->{'rddsParameters'}{'rdds43NsString'} // '',                           # {$RSM.RDDS.NS.STRING}
			$data->{'rddsParameters'}{'rdds43TestedDomain'} // '',                       # {$RSM.RDDS43.TEST.DOMAIN}
			$services{'rdds43'} || $services{'rdds80'} ? 1 : 0,                          # {$RSM.TLD.RDDS.ENABLED}
			$services{'rdap'} ? 1 : 0,                                                   # {$RDAP.TLD.ENABLED}
			$data->{'rddsParameters'}{'rdapUrl'} // '',                                  # {$RDAP.BASE.URL}
			$data->{'rddsParameters'}{'rdapTestedDomain'} // '',                         # {$RDAP.TEST.DOMAIN}
			$data->{'rddsParameters'}{'rdds43Server'} // '',                             # {$RSM.TLD.RDDS.43.SERVERS}
			($data->{'rddsParameters'}{'rdds80Url'} // '') =~ s!^https?://(.*?)/?$!$1!r, # {$RSM.TLD.RDDS.80.SERVERS}
		];

		$csv_printer->print(*STDOUT, $output);
	}
}

sub cmd_disable()
{
	my $rsmhost = getopt('rr-id');

	my ($code, $json) = http_get($rsmhost);

	my $rr_name   = $json->{'registrarName'};
	my $rr_family = $json->{'registrarFamily'};

	($code, $json) = http_put(
		$rsmhost,
		{
			"registrarName" => $rr_name,
			"registrarFamily" => $rr_family,
			"servicesStatus" => [
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
	my $rsmhost = getopt('rr-id');

	my ($code, $json) = http_delete($rsmhost);

	foreach my $line (@{$json->{'details'}{'info'}})
	{
		print($line . "\n");
	}
	print($json->{'title'} . "\n");
}

sub cmd_onboard()
{
	my $rsmhost = getopt('rr-id');

	my $config = {
		"registrarName" => getopt('rr-name'),
		"registrarFamily" => getopt('rr-family'),
		'servicesStatus' => [
			{ 'service' => 'rdds43', 'enabled' => opt('rdds43-servers') ? JSON_TRUE : JSON_FALSE },
			{ 'service' => 'rdds80', 'enabled' => opt('rdds80-servers') ? JSON_TRUE : JSON_FALSE },
			{ 'service' => 'rdap'  , 'enabled' => opt('rdap-base-url')  ? JSON_TRUE : JSON_FALSE },
		],
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

	return ($provisioning_api_config->{'url'} =~ s!^(.*?)/?$!$1!r) . '/registrars' . (defined($rsmhost) ? '/' . $rsmhost : '');
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
			'delete',
			'disable',
			'rr-id=s',
			'rr-name=s',
			'rr-family=s',
			'rdds43-servers=s',
			'rdds80-servers=s',
			'rdap-base-url=s',
			'rdap-test-domain=s',
			'rdds43-test-domain=s',
			'rdds-ns-string=s',
			'nolog',
			'help');

	if (!$rv || !%OPTS || $OPTS{'help'})
	{
		__usage();
	}

	override_opts(\%OPTS);

	validate_input();
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
		# --server-id <server-id> --list-services [--rr-id <id>]

		delete($opts{'server-id'});
		delete($opts{'list-services'});
		delete($opts{'rr-id'});
	}
	elsif (opt('disable'))
	{
		# --server-id <server-id> --disable --rr-id <id>

		require_input(\$error, 'rr-id');

		delete($opts{'server-id'});
		delete($opts{'disable'});
		delete($opts{'rr-id'});
	}
	elsif (opt('delete'))
	{
		# --server-id <server-id> --delete --rr-id <id>

		require_input(\$error, 'rr-id');

		delete($opts{'server-id'});
		delete($opts{'delete'});
		delete($opts{'rr-id'});
	}
	else
	{
		# --server-id <server-id> --rr-id <id> --rr-name <name> --rr-family <family>
		# [--rdds43-servers <server> --rdds43-test-domain <domain> --rdds-ns-string <ns-string> --rdds80-servers <server>]
		# [--rdap-base-url <url> --rdap-test-domain <domain>]

		require_input(\$error, 'rr-id');
		require_input(\$error, 'rr-name');
		require_input(\$error, 'rr-family');

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
		delete($opts{'rr-id'});
		delete($opts{'rr-name'});
		delete($opts{'rr-family'});
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
