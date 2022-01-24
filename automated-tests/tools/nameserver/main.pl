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

die("\"flags\" must be defined") unless ($config->{'flags'});
die("\"owner\" must be defined") unless ($config->{'owner'});

sub reply_handler
{
	# qname  - query name, e. g. ns1.example.com
	my ($qname, $qclass, $qtype, $peerhost, $query, $conn) = @_;

	if ($qtype ne 'A')
	{
		die("unexpected query type \"$qtype\", expected A");
	}

	my ($rcode, @answer, @authority, @additional);

	my $rr;

	print("Received [$qname] query from $peerhost to " . $conn->{sockhost} . "\n");

	print("------------------------------ <QUERY> -----------------------------------\n");
	$query->print();
	print("------------------------------ </QUERY> ----------------------------------\n");

	if ($config->{'sleep'})
	{
		sleep($config->{'sleep'});
	}

	#	if ($qname eq EXISTING_DOMAIN)
#	{
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

		my @rrsetref =
		(
			Net::DNS::RR->new
			(
				owner   => $config->{'owner'},
				ttl     => 86400,
				class   => 'IN',
				type    => 'A',
				address => '127.0.0.2'
			)
		);

		my $keypath = "$FindBin::RealBin/Krsa.$config->{'owner'}.+010+36026.private";
		my $sigrr = Net::DNS::RR::RRSIG->create(\@rrsetref, $keypath);

		push(@answer, $sigrr);

		# AUTHORITY section
		my $name = $config->{'owner'};
		my $type = 'NSEC';
		my @attr = qw(nxtdname typelist);
		my @hash = ("ns1.$config->{'owner'}.", q(A NS NSEC RRSIG SOA));

		my $hash = {};
		@{$hash}{@attr} = @hash;

		$rr = Net::DNS::RR->new(
			name  => $name,
			type  => $type,
			ttl   => 86400,
			owner => $name,
			%$hash
		);

		push(@authority, $rr);

		$rcode = "NOERROR";
#		$rcode = undef;
#	}
#	else
#	{
#		print("Warning: Non-existing domain!\n");
#		$rcode = "NXDOMAIN";
#	}

	# specify EDNS options  { option => value }
	my $optionmask =
	{
		nsid => 'foo-ns-id',
	};

	return ($rcode, \@answer, \@authority, \@additional, $config->{'flags'}, $optionmask);
}

# name, port, pid_file, verbose, reply_handler, additional options to NameserverCustom
start_dns_server("NAMESERVER", PORT, $pid_file, 0, \&reply_handler, {
		'OverrideOwner' => $config->{'override-owner'}
	}
);
