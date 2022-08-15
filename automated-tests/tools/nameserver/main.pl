#!/usr/bin/perl

use strict;
use warnings;

use FindBin;
use lib "$FindBin::RealBin/..";
use Tools;

use Net::DNS::SEC;
use Net::DNS::RR::RRSIG;

use constant PORT => 5054;

my $pid_file   = $ARGV[0];
my $input_file = $ARGV[1];

die("usage $0 <pid file> <input file> (no pid file)") unless ($pid_file);
die("usage $0 <pid file> <input file> (no input file), args: ", join(',', @ARGV)) unless ($input_file);

if (! -r $input_file)
{
	$input_file = "$FindBin::RealBin/../../test-cases/simple-check/$input_file";
}

die("usage $0 <pid file> <input file> (invalid input file $input_file)") unless (-r $input_file);

my $config = read_json_file($input_file);

die("\"owner\" must be defined") unless ($config->{'owner'});
die("\"rcode\" must be defined") unless ($config->{'rcode'});
die("\"flags\" must be defined") unless ($config->{'flags'});
die("\"flags\" must be a hash")  unless (ref($config->{'flags'}) eq 'HASH');

sub reply_handler
{
	# qname  - query name, e. g. ns1.example.com
	my ($qname, $qclass, $qtype, $peerhost, $query, $conn) = @_;

	if ($qtype ne 'A')
	{
		die("unexpected query type \"$qtype\", expected A");
	}

	my (@answer, @authority, @additional);

	inf("Received [$qname] query from $peerhost to ", $conn->{sockhost});

	inf("------------------------------ <QUERY> -----------------------------------");
	$query->print();
	inf("------------------------------ </QUERY> ----------------------------------");

	if ($config->{'sleep'})
	{
		sleep($config->{'sleep'});
	}

	# if ($qname eq EXISTING_DOMAIN)

	# ANSWER section
	push(@answer, Net::DNS::RR->new
		(
			owner   => $config->{'owner'},
			ttl     => 86400,
			class   => 'IN',
			type    => 'A',
			address => '127.0.0.2'
		)
	);

	# AUTHORITY section
	my $keypath = "$FindBin::RealBin/Krsa.$config->{'owner'}.+010+36026.private";
	die("cannot find key file \"$keypath\"") unless (-r $keypath);

	my $rr = Net::DNS::RR->new
	(
		owner   => $config->{'owner'},
		name    => $config->{'owner'},
		ttl     => 86400,
		class   => 'IN',
		type    => 'NSEC',
	);

	push(@authority, $rr);

	push(@authority, Net::DNS::RR::RRSIG->create([$rr], $keypath));

	# specify EDNS options  { option => value }
	my $optionmask =
	{
		nsid => 'foo-ns-id',
	};

	return ($config->{'rcode'}, \@answer, \@authority, \@additional, $config->{'flags'}, $optionmask);
}

# name, port, pid_file, verbose, reply_handler, additional options to NameserverCustom
start_dns_server("NAMESERVER", PORT, $pid_file, 0, \&reply_handler, {
		'OverrideOwner' => $config->{'override-owner'},
		'OverrideReply' => $config->{'override-reply'},
	}
);
