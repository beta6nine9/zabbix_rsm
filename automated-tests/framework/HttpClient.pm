package HttpClient;

use strict;
use warnings;

use Exporter qw(import);
our @EXPORT = qw(
	http_request
);

use HTTP::Request;
use LWP::UserAgent;
use MIME::Base64;

use Data::Dumper;
use Output;

sub http_request($$$$)
{
	my $url     = shift;
	my $method  = shift;
	my $auth    = shift;
	my $payload = shift;

	info('sending request (method: "' . $method . '"; url: "' . $url . '")');
	if (defined($payload))
	{
		info('payload:');
		info($_) foreach (split(/\n/, $payload));
	}

	my $user_agent = LWP::UserAgent->new();
	my $request = HTTP::Request->new($method, $url, undef, $payload);

	if (defined($auth))
	{
		$request->header('Authorization' => 'Basic ' . encode_base64($auth->{'username'} . ':' . $auth->{'password'}));
	}

	my $response = $user_agent->simple_request($request);

	my $code = $response->code();
	my $type = $response->header('content-type');
	my $body = $response->content();

	info('received (status code: ' . $code . '):');
	info($_) foreach (split(/\n/, $body));

	return $code, $type, $body;
}

1;
