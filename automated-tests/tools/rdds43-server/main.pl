#!/usr/bin/perl

use strict;
use warnings;

use FindBin;
use lib "$FindBin::RealBin/..";
use Tools;

use constant RDDS43_PORT => 4343;

my $pid_file   = $ARGV[0];
my $input_file = $ARGV[1];

die("usage $0 <pid file> <input file>") unless ($pid_file);
die("usage $0 <pid file> <input file>") unless ($input_file);

if (! -r $input_file)
{
	$input_file = "$FindBin::RealBin/../../test-cases/simple-check/$input_file";
}

die("usage $0 <pid file> <input file> (invalid input file $input_file)") unless (-r $input_file);

my $config = read_json_file($input_file);
die("\"expected-domain\" must be defined")  unless ($config->{'expected-domain'});
die("\"rdds43-ns-string\" must be defined") unless ($config->{'rdds43-ns-string'});
die("\"name-servers\" must be defined")     unless ($config->{'name-servers'});
die("\"name-servers\" must be an array")    unless (ref($config->{'name-servers'}) eq 'ARRAY');

sub reply_handler()
{
	my $client_socket = shift;

	my $data = <$client_socket>;

	$data =~ s/[\r\n]$//g;

	print("received [$data]\n");

	if ($data ne $config->{'expected-domain'})
	{
		printf("Error: expected [%s] got [%s]\n", $config->{'expected-domain'}, $data);
		print $client_socket ("error");
		goto OUT;
	}

	my $reply = "";

	foreach my $ns (@{$config->{'name-servers'}})
	{
		$reply .= "\n$config->{'rdds43-ns-string'} $ns";
	}

	print $client_socket ($reply);
OUT:
	$client_socket->close();
}

start_tcp_server("RDDS43 SERVER", RDDS43_PORT, $pid_file, \&reply_handler);
