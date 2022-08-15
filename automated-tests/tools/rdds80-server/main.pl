#!/usr/bin/perl

use strict;
use warnings;

use FindBin;
use lib "$FindBin::RealBin/..";
use Tools;

use constant RDDS80_PORT => 4380;

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

die("\"expected-request\" must be defined") unless ($config->{'expected-request'});
die("\"reply-status\" must be defined")     unless ($config->{'reply-status'});
die("\"reply-headers\" must be defined")    unless ($config->{'reply-headers'});
die("\"reply-headers\" must be a hash")     unless (ref($config->{'reply-headers'}) eq 'HASH');

sub reply_handler()
{
	my $client_socket = shift;

	my $data = <$client_socket>;

	if (!defined($data))
	{
		err("remote connection closed without sending anything");
		print $client_socket ("error");
		goto OUT;
	}

	$data =~ s/[\r\n]$//g;

	inf("received [$data]");

	if ($data !~ /$config->{'expected-request'}/)
	{
		err(sprintf("expected [%s] got [%s]", $config->{'expected-request'}, $data));
		print $client_socket ("error");
		goto OUT;
	}

	if ($config->{'sleep'})
	{
		sleep($config->{'sleep'});
	}

	my $reply = "HTTP/1.1 $config->{'reply-status'}";

	foreach my $header (keys(%{$config->{'reply-headers'}}))
	{
		$reply .= "\r\n$header";
	}

	$reply .= "\r\n\r\n";

	inf("replying with [$reply]");

	print $client_socket ($reply);
OUT:
	$client_socket->close();
}

start_tcp_server("RDDS80SERVER", RDDS80_PORT, $pid_file, \&reply_handler);
