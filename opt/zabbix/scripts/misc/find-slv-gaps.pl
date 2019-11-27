#!/usr/bin/perl

use lib '/opt/zabbix/scripts';

use strict;
use warnings;

use RSM;
use RSMSLV;
use TLD_constants qw(:api :groups);
use Data::Dumper;
use DateTime;

sub main()
{
	my $check_hosts = [];
	my $check_keys = [];

	parse_cli_opts($check_hosts, $check_keys);

	set_slv_config(get_rsm_config());

	db_connect();

	foreach my $host (get_hosts($check_hosts))
	{
		my ($hostid, $host) = @{$host};

		info("checking $host ($hostid)");

		foreach my $item (get_items($hostid, $check_keys))
		{
			my ($itemid, $key, $name, $value_type) = @{$item};

			my $delay;

			$delay = 60  if ($key eq 'rsm.slv.dns.avail');
			$delay = 60  if ($key eq 'rsm.slv.dns.downtime');
			$delay = 60  if ($key eq 'rsm.slv.dns.rollweek');
			$delay = 60  if ($key =~ /^rsm\.slv\.dns\.ns\.avail\[.+,.+\]$/);
			$delay = 60  if ($key =~ /^rsm\.slv\.dns\.ns\.downtime\[.+,.+\]$/);
			$delay = 60  if ($key eq 'rsm.slv.dnssec.avail');
			$delay = 60  if ($key eq 'rsm.slv.dnssec.rollweek');
			$delay = 60  if ($key =~ /^rsm\.slv\.dns\.udp\.rtt\.(performed|failed|pfailed)$/);
			$delay = 60  if ($key =~ /^rsm\.slv\.dns\.tcp\.rtt\.(performed|failed|pfailed)$/);
			$delay = 300 if ($key eq 'rsm.slv.rdds.avail');
			$delay = 300 if ($key eq 'rsm.slv.rdds.downtime');
			$delay = 300 if ($key eq 'rsm.slv.rdds.rollweek');
			$delay = 300 if ($key =~ /^rsm\.slv\.rdds\.rtt\.(performed|failed|pfailed)$/);
			$delay = 300 if ($key eq 'rsm.slv.rdap.avail');
			$delay = 300 if ($key eq 'rsm.slv.rdap.downtime');
			$delay = 300 if ($key eq 'rsm.slv.rdap.rollweek');
			$delay = 300 if ($key =~ /^rsm\.slv\.rdap\.rtt\.(performed|failed|pfailed)$/);

			fail("could not determine item delay (item key: $key)") if (!defined($delay));

			info(". checking $key ($itemid)");

			my @gaps = find_gaps($key, $itemid, get_history_table($value_type), $delay);

			if (@gaps)
			{
				print_gaps(@gaps);
			}
		}
	}

	db_disconnect();

	slv_exit(0);
}

sub get_hosts($)
{
	my $check_hosts = shift;

	my $host_filter = "";
	if (@{$check_hosts})
	{
		$host_filter = join(" or ", ("host=?") x @{$check_hosts});
		$host_filter = " and ($host_filter)";
	}

	my $query = "select" .
			" hosts.hostid," .
			" hosts.host" .
		" from" .
			" hosts" .
			" left join hosts_groups on hosts_groups.hostid=hosts.hostid" .
		" where" .
			" hosts_groups.groupid=?" .
			$host_filter .
		" order by" .
			" hosts.host asc";

	my $rows = db_select($query, [TLDS_GROUPID, @{$check_hosts}]);

	return @{$rows};
}

sub get_items($$)
{
	my $hostid     = shift;
	my $check_keys = shift;

	my $key_filter = "";
	if (@{$check_keys})
	{
		$key_filter = join(" or ", ("key_=?") x @{$check_keys});
		$key_filter = " and ($key_filter)";
	}

	my $query = "select" .
			" itemid," .
			" key_," .
			" name," .
			" value_type" .
		" from" .
			" items" .
		" where" .
			" hostid=?" .
			$key_filter .
		" order by" .
			" key_";

	my $rows = db_select($query, [$hostid, @{$check_keys}]);

	return @{$rows};
}

sub find_gaps($$$)
{
	my $key           = shift;
	my $itemid        = shift;
	my $history_table = shift;
	my $delay         = shift;

	dbg('$key           = ' . $key);
	dbg('$itemid        = ' . $itemid);
	dbg('$history_table = ' . $history_table);
	dbg('$delay         = ' . $delay);

	my $query = "select *" .
		" from (" .
			"select" .
				" \@previous_clock as previous_clock," .
				" \@previous_clock := clock as current_clock," .
				" value," .
				" ns" .
			" from" .
				" (select \@previous_clock := null) tmp," .
				" $history_table" .
			" where" .
				" itemid = ?" .
			" order by" .
				" clock asc" .
			") tmp" .
			" where current_clock - previous_clock <> ?";

	my $rows = db_select($query, [$itemid, $delay]);

	return @{$rows};
}

sub get_history_table($)
{
	my $value_type = shift;

	return 'history'      if ($value_type == ITEM_VALUE_TYPE_FLOAT);
	return 'history_uint' if ($value_type == ITEM_VALUE_TYPE_UINT64);
	return 'history_str'  if ($value_type == ITEM_VALUE_TYPE_STR);

	fail("unhandled value type: $value_type");
}

sub print_gaps
{
	foreach my $gap (@_)
	{
		my ($prev_clock, $curr_clock) = @{$gap};

		info(sprintf("%s | %s | %d", __ts_full($prev_clock), __ts_full($curr_clock), $curr_clock - $prev_clock));
	}
}

sub parse_cli_opts($$)
{
	my $hosts = shift;
	my $keys = shift;

	setopt('stats');
	setopt('nolog');

	parse_opts(
		'host=s' => $hosts,
		'key=s'  => $keys,
	);
}

sub __ts_full($)
{
	my $clock = shift;

	return sprintf("%s (%d)", DateTime->from_epoch('epoch' => $clock) =~ s/T/ /r, $clock);
}

main();

__END__

=head1 NAME

find-slv-gaps.pl - find gaps (and duplicates) in history tables

=head1 SYNOPSIS

find-slv-gaps.pl [--host <host>] [--key <key>] [--debug] [--help]

=head1 OPTIONS

=head2 OPTIONAL OPTIONS

=over 8

=item B<--host <host>>

Handle only specified host. This option can be used multiple times.

=item B<--key <key>>

Handle only specified key. This option can be used multiple times.

=item B<--debug>

Produce insane amount of debug messages.

=item B<--help>

Display this help and exit.

=cut
