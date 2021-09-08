#!/usr/bin/env perl

use FindBin;
use lib "$FindBin::RealBin/..";

use strict;
use warnings;

use RSM;
use RSMSLV;
use Data::Dumper;

sub main()
{
	my $services;
	my $nsservers;

	parse_cli_opts();
	read_input(\$services, \$nsservers);
	generate_calls($services, $nsservers);
}

sub parse_cli_opts()
{
	parse_opts("tld-pl=s", "services=s", "nsservers=s", "server-id=i");

	setopt("nolog");

	fail("missing option: --tld-pl")    if (!opt("tld-pl"));
	fail("missing option: --services")  if (!opt("services"));
	fail("missing option: --nsservers") if (!opt("nsservers"));
	fail("missing option: --server-id") if (!opt("server-id"));
}

sub read_input($$)
{
	my $services_ref  = shift;
	my $nsservers_ref = shift;

	my $error;

	if (read_file(getopt('services'), $services_ref, \$error) != SUCCESS)
	{
		fail("failed to read file '" . getopt('services') . "': " . $error);
	}
	if (read_file(getopt('nsservers'), $nsservers_ref, \$error) != SUCCESS)
	{
		fail("failed to read file '" . getopt('nsservers') . "': " . $error);
	}
}

sub generate_calls($$$)
{
	my $services  = shift;
	my $nsservers = shift;

	my $tld_pl    = getopt("tld-pl");
	my $server_id = getopt("server-id");

	my @onboard = ();
	my @disable_tld = ();
	my @disable_rdds = ();
	my @disable_rdap = ();

	my %nsip = parse_nsservers($nsservers);

	my $pattern = '^' . join(',', ('"([^"]*)"') x 15);

	foreach my $line (split(/\n/, $services))
	{
		if ($line =~ /$pattern/)
		{
			my $tld                = $1;
			my $tld_type           = $2;
			my $tld_status         = $3;
			my $dns_testprefix     = $4;
			my $rdds_ns_string     = $5;
			my $rdds43_test_prefix = $6;
			my $dnssec_enabled     = $7;
			#my $epp_enabled        = $8; # EPP is not supported
			my $rdds_enabled       = $9;
			my $rdap_enabled       = $10;
			my $rdap_base_url      = $11;
			my $rdap_test_domain   = $12;
			my $rdds43_servers     = $13;
			my $rdds80_servers     = $14;
			my $rdds43_test_domain = $15;

			my $has_rdds_args = 0;
			my $has_rdap_args = 0;

			my $cmd = $tld_pl;

			$cmd .= " --server-id '$server_id'";
			$cmd .= " --tld '$tld'";
			$cmd .= " --type '$tld_type'";
			$cmd .= " --dns-test-prefix '$dns_testprefix'";
			#$cmd .= " --dns-tcp";
			#$cmd .= " --dns-udp";
			#$cmd .= " --dns-minns '2'";

			if (exists($nsip{$tld}{'v4'}))
			{
				$cmd .= " --ipv4";
				$cmd .= " --ns-servers-v4 '$nsip{$tld}{'v4'}'";
			}
			if (exists($nsip{$tld}{'v6'}))
			{
				$cmd .= " --ipv6";
				$cmd .= " --ns-servers-v6 '$nsip{$tld}{'v6'}'";
			}

			if ($dnssec_enabled)
			{
				$cmd .= " --dnssec";
			}

			if ($rdds43_test_prefix || $rdds43_test_domain || $rdds43_servers)
			{
				$has_rdds_args = 1;

				$cmd .= " --rdds-ns-string '$rdds_ns_string'";
				#$cmd .= " --rdds-test-prefix '$rdds43_test_prefix'"; # either prefix or domain must be specified, but not both
				$cmd .= " --rdds43-test-domain '$rdds43_test_domain'";
				$cmd .= " --rdds43-servers '$rdds43_servers'";
			}
			if ($rdds80_servers)
			{
				$has_rdds_args = 1;

				$cmd .= " --rdds80-servers '$rdds80_servers'";
			}
			if ($rdap_base_url || $rdap_test_domain)
			{
				$has_rdap_args = 1;

				$cmd .= " --rdap-base-url '$rdap_base_url'";
				$cmd .= " --rdap-test-domain '$rdap_test_domain'";
			}

			push(@onboard, $cmd);

			if ($tld_status ne '0')
			{
				push(@disable_tld, "$tld_pl --server-id '$server_id' --tld '$tld' --disable");
			}
			if ($rdds_enabled eq '0' && $has_rdds_args)
			{
				push(@disable_rdds, "$tld_pl --server-id '$server_id' --tld '$tld' --disable --rdds");
			}
			if ($rdap_enabled eq '0' && $has_rdap_args)
			{
				push(@disable_rdap, "$tld_pl --server-id '$server_id' --tld '$tld' --disable --rdap");
			}
		}
		else
		{
			fail("unexpected line: '$line'");
		}
	}

	if (@onboard)
	{
		print(join("\n", @onboard) . "\n\n");
	}
	if (@disable_tld)
	{
		print(join("\n", @disable_tld) . "\n\n");
	}
	if (@disable_rdds)
	{
		print(join("\n", @disable_rdds) . "\n\n");
	}
	if (@disable_rdap)
	{
		print(join("\n", @disable_rdap) . "\n\n");
	}


}

sub parse_nsservers($)
{
	my $str = shift;

	my %nsip = ();

	foreach my $line (split(/\n/, $str))
	{
		if ($line =~ /^"([^"]+)","([^"]+)","([^"]+)","([^"]+)"$/)
		{
			my ($tld, $version, $ns, $ip) = ($1, $2, $3, $4);

			push(@{$nsip{$tld}{$version}}, "$ns,$ip");
		}
		else
		{
			fail("unexpected line: '$line'");
		}
	}

	foreach my $tld (keys(%nsip))
	{
		foreach my $version (keys(%{$nsip{$tld}}))
		{
			$nsip{$tld}{$version} = join(' ', @{$nsip{$tld}{$version}});
		}
	}

	return %nsip;
}

main();

__END__

=head1 NAME

generate-tld-pl-calls.pl - generate tld.pl calls.

=head1 SYNOPSIS

generate-tld-pl-calls.pl --tld-pl <filename> --services <filename> --nsservers <filename> --server-id <server-id> [--help]

=head1 OPTIONS

=over 8

=item B<--tld-pl <filename>>

Full path to tld.pl.

=item B<--services <filename>>

Output of --list-services.

=item B<--nsservers <filename>>

Output of --get-nsservers-list.

=item B<--server-id <server-id>>

Output of --get-nsservers-list.

=item B<--help>

Display this help and exit.

=cut
