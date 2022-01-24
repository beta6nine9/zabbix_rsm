#!/usr/bin/perl

use strict;
use warnings;

use FindBin;
use lib "$FindBin::RealBin/..";
use Tools;

use Net::DNS::SEC;
use Net::DNS::RR::RRSIG;

use constant PORT => 5053;

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
die("\"expected-qname\" must be defined")  unless ($config->{'expected-qname'});
die("\"expected-qtypes\" must be defined") unless ($config->{'expected-qtypes'});
die("\"rcode\" must be defined")           unless ($config->{'rcode'});

sub reply_handler
{
	# qname  - query name, e. g. ns1.example.com
	my ($qname, $qclass, $qtype, $peerhost, $query, $conn) = @_;

	if ($qname ne $config->{'expected-qname'})
	{
		die("unexpected query name \"$qname\", expected \"$config->{'expected-qname'}\"");
	}

	my $expected_qtype;
	foreach (@{$config->{'expected-qtypes'}})
	{
		if ($qtype eq $_)
		{
			$expected_qtype = $_;
			last;
		}
	}

	die("unexpected query type: \"$qtype\"") unless ($expected_qtype);

	my ($rcode, @answer, @authority, @additional, $headermask, $optionmask);

	if ($qtype eq 'DNSKEY')
	{
		print("Received [$qname] query from $peerhost to " . $conn->{sockhost} . ":\n");

		print("------------------------------ <QUERY> -----------------------------------\n");
		$query->print();
		print("------------------------------ </QUERY> ----------------------------------\n");

		my @rrsetref =
		(
			Net::DNS::RR->new
			(
				owner   => $qname,
				ttl     => 86400,
				class   => 'IN',
				type    => 'A',
				address => '127.0.0.1'
			)
		);

		my $keypath = "$FindBin::RealBin/Krsa.example.+010+36026.private";

		die("cannot find key file \"$keypath\"") unless (-r $keypath);

		my $rr = Net::DNS::RR::RRSIG->create(\@rrsetref, $keypath);

		push(@answer, $rr);

		push(@answer, Net::DNS::RR->new
			(
				"$qname DNSKEY 256 3 10 (AwEAAbaRT4gWfhOU7LLzG44c/IhhjrWxO/mYU8v6EQSWrlqRlShQ9PX3 p7tM4SvlGcv91JX60f5irAkg+1w2veeG6vnURcf+EbeY2Hu5LYfJ8QJV 3wySDu562CsY0CHaeYpRcFH2mqg/hqFntcQRWJyUy/H1XHfmkR6J2vFZ M9pA2V9t)"
			)
		);

		# mark the answer authoritative (by setting the 'aa' flag)
		$headermask =
		{
			aa => 1,
			ad => 1,
			rd => 0,
			#		tc => 0,
			#		do => 0,
		};

		# specify EDNS options  { option => value }
		$optionmask =
		{
			nsid => 'foo-ns-id',
		};
	}
	elsif ($qtype eq 'A')
	{
		my $rr = Net::DNS::RR->new
		(
			owner   => 'example',
			type    => 'A',
			address => '127.0.0.1'
		);

		push(@answer, $rr);
	}
	elsif ($qtype eq 'AAAA')
	{
		my $rr = Net::DNS::RR->new
		(
			owner   => 'example',
			type    => 'AAAA',
			address => '::1'
		);

		push(@answer, $rr);
	}
	else
	{
		die("unsupported query type: \"$qtype\"");
	}

	$rcode = $config->{'rcode'};

	return ($rcode, \@answer, \@authority, \@additional, $headermask, $optionmask);
}

# name, port, pid_file, verbose, reply_handler, additional options to NameserverCustom
start_dns_server("RESOLVER", PORT, $pid_file, 0, \&reply_handler, {});
