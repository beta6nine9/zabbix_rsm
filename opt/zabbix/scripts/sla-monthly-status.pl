#!/usr/bin/perl

BEGIN
{
	our $MYDIR = $0; $MYDIR =~ s,(.*)/.*,$1,; $MYDIR = '.' if ($MYDIR eq $0);
}
use lib $MYDIR;

use strict;
use warnings;

use RSM;
use RSMSLV;
use TLD_constants qw(:api :groups);
use Data::Dumper;
use DateTime;

use constant SLV_ITEM_KEY_DNS_DOWNTIME      => "rsm.slv.dns.downtime";
use constant SLV_ITEM_KEY_DNS_NS_DOWNTIME   => "rsm.slv.dns.ns.downtime[%,%]";
use constant SLV_ITEM_KEY_RDDS_DOWNTIME     => "rsm.slv.rdds.downtime";
use constant SLV_ITEM_KEY_DNS_UDP_PERFORMED => "rsm.slv.dns.udp.rtt.performed";
use constant SLV_ITEM_KEY_DNS_UDP_FAILED    => "rsm.slv.dns.udp.rtt.failed";
use constant SLV_ITEM_KEY_DNS_UDP_PFAILED   => "rsm.slv.dns.udp.rtt.pfailed";
use constant SLV_ITEM_KEY_DNS_TCP_PERFORMED => "rsm.slv.dns.tcp.rtt.performed";
use constant SLV_ITEM_KEY_DNS_TCP_FAILED    => "rsm.slv.dns.tcp.rtt.failed";
use constant SLV_ITEM_KEY_DNS_TCP_PFAILED   => "rsm.slv.dns.tcp.rtt.pfailed";
use constant SLV_ITEM_KEY_RDDS_PERFORMED    => "rsm.slv.rdds.rtt.performed";
use constant SLV_ITEM_KEY_RDDS_FAILED       => "rsm.slv.rdds.rtt.failed";
use constant SLV_ITEM_KEY_RDDS_PFAILED      => "rsm.slv.rdds.rtt.pfailed";

sub main()
{
	parse_opts("year=i", "month=i");
	fail_if_running();
	set_slv_config(get_rsm_config());

	my ($from, $till) = get_time_limits();

	my %hosts = ();

	db_connect();

	my $slrs = get_slrs();

	my ($items, $itemids_float, $itemids_uint) = get_items($slrs, $from);

	my $data_uint  = get_data($itemids_uint , "history_uint", $from, $till);
	my $data_float = get_data($itemids_float, "history"     , $from, $till);

	my @data = (@{$data_uint}, @{$data_float});

	foreach my $row (@data)
	{
		my ($itemid, $value, $clock) = @{$row};
		my ($hostid, $host, $itemkey) = @{$items->{$itemid}};

		if ($itemkey eq SLV_ITEM_KEY_DNS_DOWNTIME)
		{
			push(@{$hosts{$host}}, [
				$clock,
				"SLR_END_MONTH_DNS_service_availability",
				$value,
			]);
		}
		elsif ($itemkey eq SLV_ITEM_KEY_RDDS_DOWNTIME)
		{
			push(@{$hosts{$host}}, [
				$clock,
				"SLR_END_MONTH_RDDS_service_availability",
				format_float($value / $slrs->{'rdds_downtime'} * 100, '%'),
				$value,
			]);
		}
		elsif ($itemkey eq SLV_ITEM_KEY_DNS_UDP_PFAILED)
		{
			push(@{$hosts{$host}}, [
				$clock,
				"SLR_END_MONTH_DNS_UDP_RTT_availability",
				format_float($value / $slrs->{'dns_udp_rtt'} * 100, '%'),
				get_history("history_uint", get_itemid_by_hostid($hostid, SLV_ITEM_KEY_DNS_UDP_PERFORMED), $clock),
				get_history("history_uint", get_itemid_by_hostid($hostid, SLV_ITEM_KEY_DNS_UDP_FAILED), $clock),
			]);
		}
		elsif ($itemkey eq SLV_ITEM_KEY_DNS_TCP_PFAILED)
		{
			push(@{$hosts{$host}}, [
				$clock,
				"SLR_END_MONTH_DNS_TCP_RTT_availability",
				format_float($value / $slrs->{'dns_tcp_rtt'} * 100, '%'),
				get_history("history_uint", get_itemid_by_hostid($hostid, SLV_ITEM_KEY_DNS_TCP_PERFORMED), $clock),
				get_history("history_uint", get_itemid_by_hostid($hostid, SLV_ITEM_KEY_DNS_TCP_FAILED), $clock),
			]);
		}
		elsif ($itemkey eq SLV_ITEM_KEY_RDDS_PFAILED)
		{
			push(@{$hosts{$host}}, [
				$clock,
				"SLR_END_MONTH_RDDS_RTT_service_availability",
				format_float($value / $slrs->{'rdds_rtt'} * 100, '%'),
				get_history("history_uint", get_itemid_by_hostid($hostid, SLV_ITEM_KEY_RDDS_PERFORMED), $clock),
				get_history("history_uint", get_itemid_by_hostid($hostid, SLV_ITEM_KEY_RDDS_FAILED), $clock),
			]);
		}
		elsif ($itemkey =~ /^rsm\.slv\.dns\.ns\.downtime\[(.+),(.+)\]$/)
		{
			my $ns = $1;
			my $ip = $2;

			push(@{$hosts{$host}}, [
				$clock,
				"SLR_END_MONTH_NS_availability",
				$ns,
				$ip,
				format_float($value / $slrs->{'dns_ns_downtime'} * 100, '%'),
				$value,
			]);
		}
	}

	db_disconnect();

	foreach my $host (keys(%hosts))
	{
		foreach my $data (@{$hosts{$host}})
		{
			my $clock = shift(@{$data});
			notify($clock, $host, $data);
		}
	}

	slv_exit(SUCCESS);
}

sub get_time_limits()
{
	my $year = getopt("year");
	my $month = getopt("month");

	if (defined($year) || defined($month))
	{
		if (!defined($year))
		{
			fail("--year is not specified");
		}
		if (!defined($month))
		{
			fail("--month is not specified");
		}
	}
	else
	{
		my $dt = DateTime->now();
		$dt->truncate('to' => 'month');
		$dt->subtract('months' => 1);

		$year = $dt->year();
		$month = $dt->month();
	}

	my $from = DateTime->new('year' => $year, 'month' => $month);
	my $till = DateTime->last_day_of_month('year' => $year, 'month' => $month, 'hour' => 23, 'minute' => 59, 'second' => 59);

	return $from->epoch(), $till->epoch();
}

sub get_slrs()
{
	my %slrs;

	my $sql = "select macro, value from globalmacro where macro in (?, ?, ?, ?, ?, ?)";
	my $params = [
		'{$RSM.SLV.DNS.DOWNTIME}',
		'{$RSM.SLV.NS.DOWNTIME}',
		'{$RSM.SLV.DNS.UDP.RTT}',
		'{$RSM.SLV.DNS.TCP.RTT}',
		'{$RSM.SLV.RDDS.DOWNTIME}',
		'{$RSM.SLV.RDDS.RTT}'
	];
	my $rows = db_select($sql, $params);

	foreach my $row (@{$rows})
	{
		my ($macro, $value) = @{$row};

		$slrs{'dns_downtime'}    = $value if ($macro eq '{$RSM.SLV.DNS.DOWNTIME}');
		$slrs{'dns_ns_downtime'} = $value if ($macro eq '{$RSM.SLV.NS.DOWNTIME}');
		$slrs{'dns_udp_rtt'}     = $value if ($macro eq '{$RSM.SLV.DNS.UDP.RTT}');
		$slrs{'dns_tcp_rtt'}     = $value if ($macro eq '{$RSM.SLV.DNS.TCP.RTT}');
		$slrs{'rdds_downtime'}   = $value if ($macro eq '{$RSM.SLV.RDDS.DOWNTIME}');
		$slrs{'rdds_rtt'}        = $value if ($macro eq '{$RSM.SLV.RDDS.RTT}');
	}

	fail('global macro {$RSM.SLV.DNS.DOWNTIME} was not found')  unless (exists($slrs{'dns_downtime'}));
	fail('global macro {$RSM.SLV.NS.DOWNTIME} was not found')   unless (exists($slrs{'dns_ns_downtime'}));
	fail('global macro {$RSM.SLV.DNS.UDP.RTT} was not found')   unless (exists($slrs{'dns_udp_rtt'}));
	fail('global macro {$RSM.SLV.DNS.TCP.RTT} was not found')   unless (exists($slrs{'dns_tcp_rtt'}));
	fail('global macro {$RSM.SLV.RDDS.DOWNTIME} was not found') unless (exists($slrs{'rdds_downtime'}));
	fail('global macro {$RSM.SLV.RDDS.RTT} was not found')      unless (exists($slrs{'rdds_rtt'}));

	return \%slrs;
}

sub get_items($$)
{
	my $slr  = shift;
	my $from = shift;

	my $sql = "select items.itemid, items.key_, items.value_type, hosts.hostid, hosts.host" .
		" from items" .
			" left join hosts on hosts.hostid = items.hostid" .
			" left join hosts_groups on hosts_groups.hostid = hosts.hostid" .
		" where (items.key_ in (?, ?, ?, ?, ?) or items.key_ like ?) and" .
			" hosts_groups.groupid = ? and" .
			" hosts.created < ?";

	my $params = [
		SLV_ITEM_KEY_DNS_DOWNTIME,
		SLV_ITEM_KEY_RDDS_DOWNTIME,
		SLV_ITEM_KEY_DNS_UDP_PFAILED,
		SLV_ITEM_KEY_DNS_TCP_PFAILED,
		SLV_ITEM_KEY_RDDS_PFAILED,
		SLV_ITEM_KEY_DNS_NS_DOWNTIME,
		TLDS_GROUPID,
		$from,
	];

	my $rows = db_select($sql, $params);

	my %items         = (); # $items{$itemid} = [$hostid, $host, $key];
	my %itemids_float = (); # $itemids_float{$slr} = [$itemid1, $itemid2, ...]
	my %itemids_uint  = (); # $itemids_uint{$slr}  = [$itemid1, $itemid2, ...]

	foreach my $row (@{$rows})
	{
		my ($itemid, $key, $type, $hostid, $host) = @{$row};

		$items{$itemid} = [$hostid, $host, $key];

		if ($key eq SLV_ITEM_KEY_DNS_DOWNTIME)
		{
			fail("Unexpected item type (key: '$key', type: '$type'") unless ($type == ITEM_VALUE_TYPE_UINT64);
			push(@{$itemids_uint{$slr->{'dns_downtime'}}}, $itemid);
		}
		elsif ($key eq SLV_ITEM_KEY_RDDS_DOWNTIME)
		{
			fail("Unexpected item type (key: '$key', type: '$type'") unless ($type == ITEM_VALUE_TYPE_UINT64);
			push(@{$itemids_uint{$slr->{'rdds_downtime'}}}, $itemid);
		}
		elsif ($key eq SLV_ITEM_KEY_DNS_UDP_PFAILED)
		{
			fail("Unexpected item type (key: '$key', type: '$type'") unless ($type == ITEM_VALUE_TYPE_FLOAT);
			push(@{$itemids_float{$slr->{'dns_udp_rtt'}}}, $itemid);
		}
		elsif ($key eq SLV_ITEM_KEY_DNS_TCP_PFAILED)
		{
			fail("Unexpected item type (key: '$key', type: '$type'") unless ($type == ITEM_VALUE_TYPE_FLOAT);
			push(@{$itemids_float{$slr->{'dns_tcp_rtt'}}}, $itemid);
		}
		elsif ($key eq SLV_ITEM_KEY_RDDS_PFAILED)
		{
			fail("Unexpected item type (key: '$key', type: '$type'") unless ($type == ITEM_VALUE_TYPE_FLOAT);
			push(@{$itemids_float{$slr->{'rdds_rtt'}}}, $itemid);
		}
		else # if ($key eq SLV_ITEM_KEY_DNS_NS_DOWNTIME
		{
			push(@{$itemids_uint{$slr->{'dns_ns_downtime'}}}, $itemid);
			fail("Unexpected item type (key: '$key', type: '$type'") unless ($type == ITEM_VALUE_TYPE_UINT64);
		}
	}

	return \%items, \%itemids_float, \%itemids_uint;
}

sub get_data($$$$)
{
	my $itemids       = shift; # $itemids = {$slr => [$itemid1, $itemid2, ...], ...}
	my $history_table = shift; # "history" or "history_uint"
	my $from          = shift; # timestamp
	my $till          = shift; # timestamp

	my @itemids_params = (); # all itemids for use in subquery
	my @filter_params = ();  # [<itemid1, itemid2, ..., slr>, <itemid3, ..., slr>, ...]
	my $filter_sql = "";

	foreach my $slr (keys(%{$itemids}))
	{
		if ($filter_sql)
		{
			$filter_sql .= " or ";
		}

		my $itemids_placeholder = join(",", ("?") x scalar(@{$itemids->{$slr}}));
		$filter_sql .= "($history_table.itemid in ($itemids_placeholder) and value > ?)";

		push(@itemids_params, @{$itemids->{$slr}});
		push(@filter_params, @{$itemids->{$slr}});
		push(@filter_params, $slr);
	}

	my $itemids_placeholder = join(",", ("?") x scalar(@itemids_params));
	my $sql = "select $history_table.itemid, $history_table.value, $history_table.clock" .
		" from $history_table," .
			" (" .
				"select itemid, max(clock) as max_clock" .
				" from $history_table" .
				" where clock between ? and ? and" .
					" itemid in ($itemids_placeholder)" .
				" group by itemid" .
			") as history_max_clock" .
		" where $history_table.itemid = history_max_clock.itemid and" .
			" $history_table.clock = history_max_clock.max_clock and" .
			" ($filter_sql)";

	my @params = ($from, $till, @itemids_params, @filter_params);

	return db_select($sql, \@params);
}

sub get_history($$$)
{
	my $table   = shift;
	my $item_id = shift;
	my $clock   = shift;

	my $query = "select value from $table where itemid=? and clock=?";
	my $params = [$item_id, $clock];

	return db_select_value($query, $params);
}

sub format_float($$)
{
	my $value = shift;
	my $unit  = shift;

	$value = sprintf("%.2f", $value);
	$value =~ s/\.?0+$//;
	$value = sprintf("%s %s", $value, $unit) if ($unit);

	return $value;
}

sub notify($$$$)
{
	my $clock = shift;
	my $host  = shift;
	my $data  = shift;

	my ($sec, $min, $hour, $mday, $mon, $year) = localtime($clock);
	my $clock_str = sprintf("%.4d.%.2d.%.2d %.2d:%.2d:%.2d UTC", $year + 1900, $mon + 1, $mday, $hour, $min, $sec);

	my $cmd = "/opt/slam/library/alertcom/script.py";

	my @args = (
		"zabbix alert",
		join("#", ("tld", "PROBLEM", $host, @{$data})),
		$clock_str,
	);

	@args = map('"' . $_ . '"', @args);

	if (opt("dry-run"))
	{
		print("$cmd @args\n");
	}
	else
	{
		dbg("executing $cmd @args");

		my $out = qx($cmd @args 2>&1);

		if ($out)
		{
			dbg("output of $cmd:\n" . $out);
		}

		if ($? == -1)
		{
			fail("failed to execute '$cmd $args[0]': $!");
		}
		if ($? != 0)
		{
			fail("command '$cmd $args[0]' exited with value " . ($? >> 8));
		}
	}
}

main();

__END__

=head1 NAME

sla-monthly-status.pl - get SLV entries that violate SLA.

=head1 SYNOPSIS

sla-monthly-status.pl [--year <year>] [--month <month>] [--dry-run] [--debug] [--help]

=head1 OPTIONS

=over 8

=item B<--year> int

Specify year. If year is specified, month also has to be specified.

=item B<--month> int

Specify month. If month is specified, year also has to be specified.

=item B<--dry-run>

Print data to the screen, do not change anything in the system.

=item B<--debug>

Run the script in debug mode. This means printing more information.

=item B<--help>

Print a brief help message and exit.

=back

=cut
