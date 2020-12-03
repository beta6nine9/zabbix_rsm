#!/usr/bin/perl

use FindBin;
use lib "$FindBin::RealBin/..";

use strict;
use warnings;

use RSM;
use RSMSLV;
use Data::Dumper;

parse_opts('from=i', 'period=i', 'service=s', 'probe=s');

# do not write any logs
setopt('nolog');

if (opt('debug'))
{
	dbg("command-line parameters:");
	dbg("$_ => ", getopt($_)) foreach (optkeys());
}

set_slv_config(get_rsm_config());

db_connect();

__validate_input();

my $opt_from = getopt('from');

if (defined($opt_from))
{
	$opt_from = truncate_from($opt_from);	# use the whole minute
	dbg("option \"from\" truncated to the start of a minute: $opt_from") if ($opt_from != getopt('from'));
}

my %services;
if (opt('service'))
{
	$services{lc(getopt('service'))} = undef;
}
else
{
	foreach my $service ('dns', 'dnssec', 'rdds', 'epp')
	{
		$services{$service} = undef;
	}
}

foreach my $service (keys(%services))
{
	$services{$service}{'delay'} = get_dns_delay()  if ($service eq 'dns');
	$services{$service}{'delay'} = get_dns_delay()  if ($service eq 'dnssec');
	$services{$service}{'delay'} = get_rdds_delay() if ($service eq 'rdds');
	$services{$service}{'delay'} = get_epp_delay()  if ($service eq 'epp');
}

my ($check_from, $check_till);

$check_from = $opt_from;
$check_till = $check_from + getopt('period') * PROBE_DELAY - 1;

if ($check_till > time())
{
	fail("specified period (", selected_period($check_from, $check_till), ") is in the future");
}

my ($from, $till) = get_real_services_period(\%services, $check_from, $check_till);

if (!$from)
{
	info("no full test periods within specified time range: ", selected_period($check_from, $check_till));
	exit(0);
}

dbg(sprintf("getting probe statuses for period: %s", selected_period($from, $till)));

my $all_probes_ref;

if (opt('probe'))
{
	$all_probes_ref = get_probes(undef, getopt('probe'));
}
else
{
	$all_probes_ref = get_probes(undef);
}

probe_online_at_init();

print("Status of Probes at ", ts_str(getopt('from')), "\n");
print("---------------------------------------\n");
foreach my $probe (keys(%$all_probes_ref))
{
	if (probe_online_at($probe, getopt('from'), PROBE_DELAY);
	{
		print("$probe: Online\n");
	}
	else
	{
		print("$probe: Offline\n");
	}
}

sub __validate_input
{
	if (!opt('from') || !opt('period'))
	{
		usage();
	}

	if (opt('service'))
	{
		if (getopt('service') ne 'dns' and getopt('service') ne 'dnssec' and getopt('service') ne 'rdds' and getopt('service') ne 'epp')
		{
			print("Error: \"", getopt('service'), "\" - unknown service\n");
			usage();
		}
	}

	if (opt('probe'))
	{
		my $probe = getopt('probe');

		my $probes_ref = get_probes();
		my $valid = 0;

		foreach my $name (keys(%$probes_ref))
		{
			if ($name eq $probe)
			{
				$valid = 1;
				last;
			}
		}

		if ($valid == 0)
		{
			print("Error: unknown probe \"$probe\"\n");
			print("\nAvailable probes:\n");
			foreach my $name (keys(%$probes_ref))
			{
				print("  $name\n");
			}
			exit(E_FAIL);
		}
        }
}


__END__

=head1 NAME

probe-avail.pl - get information about Probe availability at a specified period

=head1 SYNOPSIS

probe-avail.pl --from <timestamp> --period <minutes> [--service <dns|dnssec|rdds|epp>] [--probe <probe>] [--debug] [--help]

=head1 OPTIONS

=over 8

=item B<--from> timestamp

Specify Unix timestamp within the oldest test cycle to handle in this run. You don't need to specify the
first second of the test cycle, any timestamp within it will work. Number of test cycles to handle within
this run can be specified using option --period otherwise all completed test cycles available in the
database up till now will be handled.

=item B<--period> minutes

Specify number minutes of the period to handle during this run. The first cycle to handle can be specified
using options --from or --continue (continue from the last time when --continue was used) (see below).

=item B<--service> service

Process only specified service. Service must be one of: dns, dnssec, rdds or epp.

=item B<--probe> name

Process only specified probe.

=item B<--debug>

Run the script in debug mode. This means printing more information.

=item B<--help>

Print a brief help message and exit.

=back

=head1 DESCRIPTION

B<This program> will print information about Probe availability at a specified period.

=head1 EXAMPLES

./probe-avail.pl --from 1443015000 --period 10

This will output Probe availability for all service tests that fall under period 23.09.2015 16:30:00-16:40:00 .

=cut
