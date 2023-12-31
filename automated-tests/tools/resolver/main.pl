#!/usr/bin/perl

use strict;
use warnings;

use FindBin;
use lib "$FindBin::RealBin/..";
use Tools;

use constant PORT => 5053;

my $pid_file   = $ARGV[0];
my $input_file = $ARGV[1];

die("usage $0 <pid file> <input file>")                                  unless ($pid_file);
die("usage $0 <pid file> <input file>")                                  unless ($input_file);
die("usage $0 <pid file> <input file> (invalid input file $input_file)") unless (-r $input_file);

my $config = read_json_file($input_file);

die("config must be a hash") unless (ref($config) eq 'HASH');

foreach my $qname (keys(%{$config}))
{
	die("config{\"$qname\"} must be a hash")                       unless (ref($config->{$qname}) eq 'HASH');
	die("config{\"$qname\"}{\"expected-qtypes\"} must be defined") unless ($config->{$qname}{'expected-qtypes'});
	die("config{\"$qname\"}{\"rcode\"} must be defined")           unless ($config->{$qname}{'rcode'});
	die("config{\"$qname\"}{\"flags\"} must be defined")           unless ($config->{$qname}{'flags'});
	die("config{\"$qname\"}{\"flags\"} must be a hash")            unless (ref($config->{$qname}{'flags'}) eq 'HASH');
}

sub reply_handler
{
	# qname  - query name, e. g. ns1.example.com
	my ($qname, $qclass, $qtype, $peerhost, $query, $conn) = @_;

	if (!exists($config->{$qname}))
	{
		my $qnames = '"' . join('", "', keys(%{$config})) . '"';
		die("unexpected query name \"$qname\", expected one of: $qnames");
	}

	my $query_config = $config->{$qname};

	my $expected_qtype;
	foreach (@{$query_config->{'expected-qtypes'}})
	{
		if ($qtype eq $_)
		{
			$expected_qtype = $_;
			last;
		}
	}

	if (!$expected_qtype)
	{
		die("unexpected query type: \"$qtype\"");
	}

	my (@answer, @authority, @additional, $optionmask);

	if ($query_config->{'sleep'})
	{
		sleep($query_config->{'sleep'});
	}

	if ($qtype eq 'DNSKEY')
	{
		inf("received [$qname] query from $peerhost to ", $conn->{sockhost});

		inf("------------------------------ <QUERY> -----------------------------------");
		$query->print();
		inf("------------------------------ </QUERY> ----------------------------------");

		my $dnskeyrr = get_dnskey_rr($query_config->{'owner'} // $qname);
		push(@answer, $dnskeyrr);

		my $rrsigrr = get_rrsig_rr($query_config->{'owner'} // $qname, $dnskeyrr);
		push(@answer, $rrsigrr);

		# specify EDNS options  { option => value }
		$optionmask =
		{
			nsid => 'foo-ns-id',
		};
	}
	elsif ($qtype eq 'DS')
	{
		my $dsrr = Net::DNS::RR::DS->create(get_dnskey_rr($query_config->{'owner'} // $qname));

		push(@answer, $dsrr);
	}
	elsif ($qtype eq 'A')
	{
		my $addrs = $query_config->{'ipv4-addresses'} // ['127.0.0.1'];

		foreach (@{$addrs})
		{
			push(@answer, Net::DNS::RR->new
				(
					owner   => $query_config->{'owner'} // $qname,
					type    => $qtype,
					address => $_
				)
			);
		}
	}
	elsif ($qtype eq 'AAAA')
	{
		my $addrs = $query_config->{'ipv6-addresses'} // ['::1'];

		foreach (@{$addrs})
		{
			push(@answer, Net::DNS::RR->new
				(
					owner   => $query_config->{'owner'} // $qname,
					type    => $qtype,
					address => $_
				)
			);
		}
	}
	else
	{
		die("handling query type \"$qtype\" is not implemented");
	}

	return ($query_config->{'rcode'}, \@answer, \@authority, \@additional, $query_config->{'flags'}, $optionmask);
}

# name, port, pid_file, verbose, reply_handler, additional options to NameserverCustom
start_dns_server("RESOLVER", PORT, $pid_file, 0, \&reply_handler, {});
