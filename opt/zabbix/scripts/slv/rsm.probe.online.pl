#!/usr/bin/env perl
#
# This script is ment to be run by cron every minute. It calculates availability of each probe (host "<Probe> - mon")
# at particular time and sends results to Zabbix trapper.

use FindBin;
use lib "$FindBin::RealBin/..";

use strict;
use warnings;

use Data::Dumper;
use RSM;
use RSMSLV;
use TLD_constants qw(:items :api);

sub main()
{
	parse_opts('now=i');
	fail_if_running();
	log_execution_time(1);
	set_slv_config(get_rsm_config());

	db_connect();
	init_values();

	my $probe_avail_limit = get_macro_probe_avail_limit();
	my $probes_ref = get_probes();

	if (opt('now'))
	{
		process_cycle($probes_ref, getopt('now'), $probe_avail_limit);
	}
	else
	{
		process_cycle($probes_ref, $^T - PROBE_DELAY * 2, $probe_avail_limit);
		process_cycle($probes_ref, $^T - PROBE_DELAY * 1, $probe_avail_limit);
	}

	send_values();
	db_disconnect();
	slv_exit(SUCCESS);
}

sub process_cycle($$$)
{
	my $probes_ref        = shift;
	my $now               = shift;
	my $probe_avail_limit = shift;

	my $from = cycle_start($now, PROBE_DELAY);
	my $till = cycle_end($now, PROBE_DELAY);
	my $value_ts = $from;

	dbg("selected period: ", selected_period($from, $till), ", with value timestamp: ", ts_full($value_ts));

	foreach my $probe (keys(%{$probes_ref}))
	{
		next if ($probes_ref->{$probe}{'status'} != HOST_STATUS_MONITORED);

		next if (uint_value_exists($value_ts, get_itemid_by_host("$probe - mon", PROBE_KEY_ONLINE)));

		my $status = get_main_status($probe, $probes_ref->{$probe}{'hostid'}, $from, $till, $probe_avail_limit);

		if (!defined($status))
		{
			if (cycle_start($^T, PROBE_DELAY) - $from < WAIT_FOR_PROBE_DATA)
			{
				next;
			}

			$status = OFFLINE;
		}

		my $status_str = "$probe is " . ($status == ONLINE ? "Up" : "Down");

		push_value("$probe - mon", PROBE_KEY_ONLINE, $value_ts, $status, ITEM_VALUE_TYPE_UINT64, $status_str);
	}
}

sub get_main_status($$$$$)
{
	my $probe_host        = shift;
	my $probe_hostid      = shift;
	my $from              = shift;
	my $till              = shift;
	my $probe_avail_limit = shift;

	my $status;

	$status = get_lastaccess_status("$probe_host - mon", $from, $till, $probe_avail_limit);

	if (!defined($status) || $status == OFFLINE)
	{
		return $status;
	}

	$status = get_automatic_status($probe_hostid, $from, $till);

	if (!defined($status) || $status == OFFLINE)
	{
		return $status;
	}

	$status = get_manual_status($probe_hostid, $from, $till);

	if (!defined($status) || $status == OFFLINE)
	{
		return $status;
	}

	return ONLINE;
}

sub get_lastaccess_status($$$)
{
	my $host              = shift;
	my $from              = shift;
	my $till              = shift;
	my $probe_avail_limit = shift;

	my $itemid = get_itemid_by_host($host, PROBE_KEY_LASTACCESS);

	my $sql = "select clock,value from history_uint where itemid=? and clock between ? and ?";
	my $params = [$itemid, $from, $till];
	my $rows = db_select($sql, $params);

	if (!@{$rows})
	{
		return undef;
	}
	if ($rows->[0][0] - $rows->[0][1] > $probe_avail_limit)
	{
		return OFFLINE;
	}
	return ONLINE;
}

sub get_automatic_status($$$)
{
	my $hostid = shift;
	my $from   = shift;
	my $till   = shift;

	my $itemid = get_itemid_like_by_hostid($hostid, PROBE_KEY_AUTOMATIC);

	my $sql = "select value from history_uint where itemid=? and clock between ? and ?";
	my $params = [$itemid, $from, $till];
	my $rows = db_select($sql, $params);

	if (!@{$rows})
	{
		return undef;
	}
	if ($rows->[0][0] != ONLINE)
	{
		return OFFLINE;
	}
	return ONLINE;
}

sub get_manual_status($$$)
{
	my $hostid = shift;
	my $from   = shift;
	my $till   = shift;

	my $itemid = get_itemid_by_hostid($hostid, PROBE_KEY_MANUAL);

	my $sql;
	my $params;
	my $rows;

	$sql = "select clock,value from lastvalue where itemid=?";
	$params = [$itemid];
	$rows = db_select($sql, $params);

	if (!@{$rows})
	{
		# manual status has never changed (ONLINE by default)
		return ONLINE;
	}
	if ($rows->[0][0] < $from)
	{
		# manual status changed before the cycle
		return $rows->[0][1] != ONLINE ? OFFLINE : ONLINE;
	}
	if ($rows->[0][0] >= $from && $rows->[0][0] <= $till && $rows->[0][1] == OFFLINE)
	{
		# manual status changed to OFFLINE during the cycle
		return OFFLINE;
	}

	$sql = "select value from history_uint where itemid=? and clock between ? and ? and value=? limit 1";
	$params = [$itemid, $from, $till, OFFLINE];
	$rows = db_select($sql, $params);

	if (@{$rows})
	{
		# manual status changed to OFFLINE during the cycle
		return OFFLINE;
	}

	$sql = "select value from history_uint where itemid=? and clock<? order by clock desc limit 1";
	$params = [$itemid, $from];
	$rows = db_select($sql, $params);

	if (!@{$rows})
	{
		# manual status has never changed before the cycle (ONLINE by default)
		return ONLINE;
	}
	if ($rows->[0][0] != ONLINE)
	{
		# manual status before the cycle was OFFLINE
		return OFFLINE;
	}

	return ONLINE;
}

main();
