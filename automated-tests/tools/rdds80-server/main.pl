#!/usr/bin/perl

use strict;
use warnings;

use FindBin;
use lib "$FindBin::RealBin/..";
use Tools;

use IO::Socket::INET;
use threads;

use constant RDDS80_PORT => 4380;

my $input_file = $ARGV[0];

die("usage $0 <input file>") unless ($input_file && -r $input_file);

my $config = read_json_file($input_file);

die("\"expected-request\" must be defined") unless ($config->{'expected-request'});
die("\"reply-status\" must be defined")     unless ($config->{'reply-status'});
die("\"reply-headers\" must be defined")    unless ($config->{'reply-headers'});
die("\"reply-headers\" must be a hash")     unless (ref($config->{'reply-headers'}) eq 'HASH');

my $socket = IO::Socket::INET->new(
	LocalHost   => '0.0.0.0',
	LocalPort   =>  RDDS80_PORT,
	Proto       => 'tcp',
	Listen      =>  5,
	Reuse       =>  1
) or die("cannot create socket: $!");

print("Waiting for tcp connect to connect on port " . RDDS80_PORT . "\n");

while (1)
{
	my $client_socket  = $socket->accept();
	my $client_address = $client_socket->peerhost;
	my $client_port    = $client_socket->peerport;

	print("$client_address:$client_port connected\n");

	threads->create(\&connection, $client_socket);
}

$socket->close();

sub connection()
{
	my $client_socket = shift;

	my $data = <$client_socket>;

	$data =~ s/[\r\n]$//g;

	print("received [$data]\n");

	if ($data !~ /$config->{'expected-request'}/)
	{
		printf("Error: expected [%s] got [%s]\n", $config->{'expected-request'}, $data);
		print $client_socket ("error");
		goto OUT;
	}

	my $reply = "$config->{'reply-status'}";

	foreach my $header (keys(%{$config->{'reply-headers'}}))
	{
		$reply .= "\r\n$header";
	}

	$reply .= "\r\n\r\n";

	printf("replying with [%s]\n", $reply);

	print $client_socket ($reply);
OUT:
	$client_socket->close();
}
