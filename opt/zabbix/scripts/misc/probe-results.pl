#!/usr/bin/env perl

use FindBin;
use lib "$FindBin::RealBin/..";

use strict;
use warnings;

use RSM;
use RSMSLV;

setopt('nolog');
setopt('dry-run');

parse_opts('tld=s', 'probe=s', 'from=i', 'till=i');

if (!opt('tld'))
{
	usage("\"--tld\" has to be specified");
}

$tld = getopt('tld');

my $now = time();

my $probe = getopt('probe');
my $from = getopt('from') || cycle_start($now - 120, 300);
my $till = getopt('till') || cycle_end($now - 120, 300);

my $config = get_rsm_config();

set_slv_config($config);

my @server_keys = get_rsm_server_keys($config);
foreach (@server_keys)
{
	$server_key = $_;

	db_connect($server_key);

	if (tld_exists(getopt('tld')) == 0)
	{
		db_disconnect();
		fail("TLD ", getopt('tld'), " does not exist.") if ($server_keys[-1] eq $server_key);
		next;
	}

	last;
}

my @probes;

if ($probe)
{
	push(@probes, $probe);
}
else
{
	my $p = get_probes();

	foreach (keys(%$p))
	{
		push(@probes, $_);
	}
}

foreach my $probe (@probes)
{
	my $host = "$tld $probe";

	my $rows_ref = db_select(
		"select h.clock,h.value,i2.key_".
		" from history_uint h,items i2".
		" where i2.itemid=h.itemid".
	        	" and i2.itemid in".
				" (select i3.itemid".
				" from items i3,hosts ho".
				" where i3.hostid=ho.hostid".
					" and ho.host='$host')".
	        	" and h.clock between $from and $till".
	        " order by h.clock,i2.key_");

	if (scalar(@$rows_ref) != 0)
	{
		print("\n** $probe CYCLES **\n\n");

		printf("%-30s%-70s %s\n", "CLOCK", "ITEM", "VALUE");
		print("------------------------------------------------------------------------------------------------------------\n");

		foreach my $row_ref (@$rows_ref)
		{
			my $clock = $row_ref->[0];
			my $value = $row_ref->[1];
			my $key = $row_ref->[2];

			$key = (length($key) > 60 ? substr($key, 0, 60) . " ..." : $key);

			printf("%s  %-70s %s\n", ts_full($clock), $key, $value);
		}
	}

	my @results;

	foreach my $t ('history', 'history_str')
	{
		$rows_ref = db_select(
			"select h.clock,h.value,i2.key_".
			" from $t h,items i2".
			" where i2.itemid=h.itemid".
				" and i2.itemid in".
					" (select i3.itemid".
					" from items i3,hosts ho".
					" where i3.hostid=ho.hostid".
	                			" and ho.host='$host')".
				" and h.clock between $from and $till".
			" order by h.clock,i2.key_");

		foreach my $row_ref (@$rows_ref)
		{
			my $clock = $row_ref->[0];
			my $value = $row_ref->[1];
			my $key = $row_ref->[2];

			$key = (length($key) > 60 ? substr($key, 0, 60) . " ..." : $key);

			push(@results, [$clock, $key, $value]);
		}
	}

	if (scalar(@results) != 0)
	{
		print("\n** $probe TESTS **\n\n");

		printf("%-30s%-70s %s\n", "CLOCK", "ITEM", "VALUE");
		print("------------------------------------------------------------------------------------------------------------\n");

		foreach my $r (sort {$a->[0] <=> $b->[0] || $a->[2] cmp $b->[2]} (@results))
		{
			printf("%s  %-70s %s\n", ts_full($r->[0]), $r->[1], $r->[2]);
		}
	}
}

db_disconnect();

__END__

=head1 NAME

probe-results.pl - show results from Probes

=head1 SYNOPSIS

probe-results.pl --tld <tld> [--from <clock>] [--till <clock>] [--probe <probe>]

=head1 OPTIONS

=over 8

=item B<--tld> tld

Specify TLD.

=item B<--from> clock

Specify unix timestamp to show results from. If not specified the starting point will be 2 RDDS cycles back.

=item B<--till> clock

Specify unix timestamp to show results till. If not specified the ending point will be 2 RDDS cycles back.

=item B<--probe> probe

Specify Probe.

=item B<--debug>

Run the script in debug mode. This means printing more information.

=item B<--help>

Print a brief help message and exit.

=back

=head1 DESCRIPTION

B<This program> will change the delay between particuar test in the system.

=head1 EXAMPLES

./probe-results.pl --tld tld1

This will display probe results from pre-previous RDDS cycle.

=cut
