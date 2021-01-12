#!/usr/bin/env perl

use FindBin;
use lib "$FindBin::RealBin/..";

use strict;
use warnings;

use RSM;
use RSMSLV;
use TLD_constants qw(:api :groups);
use Data::Dumper;
use DateTime;
use List::Util qw(min max);

sub main()
{
	my ($item, $host, $from, $till, $delay) = parse_cli_opts();

	__log("Item  - %s", $item);
	__log("Host  - %s", $host // "");
	__log("From  - %s", __ts_full($from));
	__log("Till  - %s", __ts_full($till));
	__log("Delay - %s", $delay);

	my $config = get_rsm_config();
	set_slv_config($config);

	my $server_key = get_rsm_server_key(getopt('server-id'));

	db_connect($server_key);

	my ($value_type, @itemids) = get_itemids($item, $host);
	my $itemids = join(',', @itemids);
	my $history_table = get_history_table($value_type);

	my $count_total = 0;           # number of history entries
	my $count_duplicates = 0;      # number of history entries that have duplicates
	my $count_removable_lines = 0; # number of history entries that need to be deleted
	my $count_removed_lines = 0;   # number of history entries that were deleted

	for (my $clock = $from; $clock <= $till; $clock += $delay)
	{
		if ($clock % (60 * 60) == 0)
		{
			__log("Checking from: %s", __ts_full($clock));
		}

		my %history;
		my @duplicates;

		my $rows = db_select("select itemid,clock,value,ns from $history_table where clock=$clock and itemid in ($itemids)");

		foreach my $row (@{$rows})
		{
			my ($itemid, $clock, $value, $ns) = @{$row};

			if (!exists($history{$itemid}{$clock}{$value}))
			{
				$history{$itemid}{$clock}{$value} = {
					'itemid' => $itemid,
					'clock'  => $clock,
					'value'  => $value,
					'ns'     => [],
					'count'  => 0,
				}
			}

			my $history_entry = $history{$itemid}{$clock}{$value};

			if ($history_entry->{'count'} == 1)
			{
				push(@duplicates, $history_entry);
			}

			$history_entry->{'count'}++;
			push(@{$history_entry->{'ns'}}, $ns);
		}

		for my $item (@duplicates)
		{
			my $itemid = $item->{'itemid'};
			my $clock  = $item->{'clock'};
			my $value  = $item->{'value'};
			my $ns     = $item->{'ns'};
			my $count  = $item->{'count'};
			my $limit  = $count - 1;

			$ns = "[" . join(",", @{$ns}) . "]";

			$count_removable_lines += $limit;

			info("found duplicate (count: $count, itemid: $itemid, clock: $clock, value: $value, ns: $ns)");

			if (!opt('dry-run'))
			{
				# delete duplicates

				db_exec("delete from $history_table" .
					" where" .
						" itemid=$itemid and" .
						" clock=$clock and" .
						" value=$value" .
					" order by ns desc" .
					" limit $limit");

				$count_removed_lines += $limit;

				# make sure that there is one history entry left

				my $rows = db_select("select 1 from $history_table" .
					" where" .
						" itemid=$itemid and" .
						" clock=$clock and" .
						" value=$value");

				if (scalar(@{$rows}) != 1)
				{
					fail("expected to have 1 row left in history table, got " . scalar(@{$rows}) . " rows");
				}
			}
		}

		$count_total += @{$rows};
		$count_duplicates += @duplicates;
	}

	db_disconnect();

	__log(
		"history entries - %d total, %d with duplicates, %d need to be deleted, %d were deleted",
		$count_total,
		$count_duplicates,
		$count_removable_lines,
		$count_removed_lines
	);

	slv_exit(SUCCESS);
}

sub get_itemids($$)
{
	my $item = shift;
	my $host = shift;

	my $sql = "select" .
			" items.itemid," .
			" items.value_type" .
		" from" .
			" items" .
			" left join hosts on hosts.hostid=items.hostid" .
			" left join hosts_groups on hosts_groups.hostid=hosts.hostid" .
		" where" .
			" items.key_='$item' and" .
			" hosts_groups.groupid in (${\TLDS_GROUPID}, ${\TLD_PROBE_RESULTS_GROUPID})";
	if ($host)
	{
		$sql .= " and hosts.host='$host'";
	}

	my $rows = db_select($sql);

	if (!@{$rows})
	{
		fail("could not find itemids (item: '$item', host: '$host')");
	}

	my $value_type = $rows->[0][1];

	map { fail("value type mismatch, itemid $_->[0] has different value type than itemid $rows->[0][0]") if $_->[1] != $value_type } @{$rows};

	return $value_type, map($_->[0], @{$rows});
}

sub get_history_table($)
{
	my $value_type = shift;

	return 'history'      if ($value_type == ITEM_VALUE_TYPE_FLOAT);
	return 'history_uint' if ($value_type == ITEM_VALUE_TYPE_UINT64);
	return 'history_str'  if ($value_type == ITEM_VALUE_TYPE_STR);

	fail("unhandled value type: $value_type");
}

sub parse_cli_opts()
{
	setopt('stats');
	setopt('nolog');
	parse_opts('server-id=i', 'item=s', 'host=s', 'from=s', 'till=s', 'delay=i');

	if (!opt('server-id'))
	{
		fail("missing option: --server-id");
	}
	if (!opt('item'))
	{
		fail("missing option: --item");
	}
	if (!opt('from'))
	{
		fail("missing option: --from");
	}
	if (!opt('till'))
	{
		fail("missing option: --till");
	}

	my $item = getopt('item');
	my $host = getopt('host');
	my $from = getopt('from');
	my $till = getopt('till');
	my $delay = getopt('delay') // 1;

	$from = parse_time_str($from, "invalid format for option: --from");

	if ($till eq '-')
	{
		$till = DateTime->from_epoch('epoch' => $from)->truncate('to' => 'day')->add('days' => 1)->subtract('seconds' => 1)->epoch();
	}
	else
	{
		$till = parse_time_str($till, "invalid format for option: --till");
	}

	my $time = time();
	$from = min($from, $time);
	$till = min($till, $time);

	return ($item, $host, $from, $till, $delay);
}

sub parse_time_str($$)
{
	my $str = shift;
	my $err = shift;

	if (my @matches = ($str =~ /^(\d\d\d\d)-(\d\d)-(\d\d) (\d\d):(\d\d):(\d\d)$/))
	{
		my ($year, $month, $date, $hour, $minute, $second) = @matches;

		return DateTime->new('year' => $year, 'month' => $month, 'day' => $date, 'hour' => $hour, 'minute' => $minute, 'second' => $second)->epoch();
	}

	if (my @matches = ($str =~ /^(\d\d\d\d)-(\d\d)-(\d\d)$/))
	{
		my ($year, $month, $date) = @matches;

		return DateTime->new('year' => $year, 'month' => $month, 'day' => $date)->epoch();
	}

	if (my @matches = ($str =~ /^(\d+)$/))
	{
		return $matches[0];
	}

	fail($err);
}

sub __ts_full($)
{
	my $clock = shift;

	return sprintf("%s (%d)", DateTime->from_epoch('epoch' => $clock) =~ s/T/ /r, $clock);
}

sub __log
{
	printf("%6d:%s [INF] %s\n", $$, ts_str(), sprintf(shift, @_));
}

main();

__END__

=head1 NAME

handle-duplicates.pl - find and delete duplicate values in history tables

=head1 SYNOPSIS

handle-duplicates.pl --server-id <id> --item <item> --from <datetime> --till <datetime> [--delay <delay>] [--host <host>] [--dry-run] [--debug] [--help]

IMPORTANT! When running without --dry-run, redirect output to a file to make sure that, if anything goes bad, there's enough information to restore the data in the history table.

=head1 OPTIONS

=head2 MANDATORY OPTIONS

=over 8

=item B<--server-id <id>>

ID of Zabbix server.

=item B<--item <item>>

Item key.

=item B<--from <datetime>>

Beginning of the period as UNIX timestamp or in "yyyy-mm-dd" or "yyyy-mm-dd hh:mm:ss" format.

=item B<--till <datetime>>

End of the period, same format as --from. Special value "-" means "till the end of the day specified by --from".

=head2 OPTIONAL OPTIONS

=over 8

=item B<--delay <delay>>

Delay of the item, in seconds. Defaults to 1. If delay is 1, then each second of history will be checked.
If delay is 30, then each hh:mm:00 and each hh:mm:30 will be checked (if --from starts at hh:mm:00 or hh:mm:30).

When setting delay, it's important to set the --from to a timestamp when there is some value.
E.g., if delay is 3000 (50 minutes), then there won't be any data at 2019-08-01 00:00:00.
In such case, --from would have to be 2019-08-01 00:00:40.

=item B<--host <host>>

Handle only single host.

=item B<--dry-run>

Only find and report the duplicates, don't delete them.

=item B<--debug>

Produce insane amount of debug messages.

=item B<--help>

Display this help and exit.

=cut
