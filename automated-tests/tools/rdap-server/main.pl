#!/usr/bin/perl

use strict;
use warnings;

use FindBin;
use lib "$FindBin::RealBin/..";
use Tools;

use IO::Socket::INET;
use threads;

use constant RDAP_PORT => 4380;

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
die("\"reply-body\" must be defined")       unless (defined($config->{'reply-body'}));

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
		print("sleeping for $config->{'sleep'}...\n");
		sleep($config->{'sleep'});
	}

	my $reply = "HTTP/1.1 $config->{'reply-status'}";

	foreach my $key (keys(%{$config->{'reply-headers'}}))
	{
		$reply .= "\r\n$key: " . $config->{'reply-headers'}{$key};
	}

	$reply .= "\r\n\r\n$config->{'reply-body'}";

	inf("replying with [$reply]");

	print $client_socket ($reply);
OUT:
	$client_socket->close();
}

start_tcp_server("RDAP SERVER", RDAP_PORT, $pid_file, \&reply_handler);
