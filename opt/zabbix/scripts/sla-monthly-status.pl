#!/usr/bin/env perl

use FindBin;
use lib $FindBin::RealBin;

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
use constant SLV_ITEM_KEY_RDAP_DOWNTIME     => "rsm.slv.rdap.downtime";
use constant SLV_ITEM_KEY_DNS_UDP_PERFORMED => "rsm.slv.dns.udp.rtt.performed";
use constant SLV_ITEM_KEY_DNS_UDP_FAILED    => "rsm.slv.dns.udp.rtt.failed";
use constant SLV_ITEM_KEY_DNS_UDP_PFAILED   => "rsm.slv.dns.udp.rtt.pfailed";
use constant SLV_ITEM_KEY_DNS_TCP_PERFORMED => "rsm.slv.dns.tcp.rtt.performed";
use constant SLV_ITEM_KEY_DNS_TCP_FAILED    => "rsm.slv.dns.tcp.rtt.failed";
use constant SLV_ITEM_KEY_DNS_TCP_PFAILED   => "rsm.slv.dns.tcp.rtt.pfailed";
use constant SLV_ITEM_KEY_RDDS_PERFORMED    => "rsm.slv.rdds.rtt.performed";
use constant SLV_ITEM_KEY_RDDS_FAILED       => "rsm.slv.rdds.rtt.failed";
use constant SLV_ITEM_KEY_RDDS_PFAILED      => "rsm.slv.rdds.rtt.pfailed";
use constant SLV_ITEM_KEY_RDAP_PERFORMED    => "rsm.slv.rdap.rtt.performed";
use constant SLV_ITEM_KEY_RDAP_FAILED       => "rsm.slv.rdap.rtt.failed";
use constant SLV_ITEM_KEY_RDAP_PFAILED      => "rsm.slv.rdap.rtt.pfailed";

sub main()
{
	parse_opts("year=i", "month=i");
	fail_if_running();
	set_slv_config(get_rsm_config());

	my ($from, $till) = get_time_limits();

	my %hosts = ();

	db_connect();

	my $slrs = get_slrs();

	my ($items, $itemids_float, $itemids_uint) = get_items($slrs, $till);

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
		elsif ($itemkey eq SLV_ITEM_KEY_RDAP_DOWNTIME)
		{
			push(@{$hosts{$host}}, [
				$clock,
				"SLR_END_MONTH_RDAP_service_availability",
				format_float($value / $slrs->{'rdap_downtime'} * 100, '%'),
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
		elsif ($itemkey eq SLV_ITEM_KEY_RDAP_PFAILED)
		{
			push(@{$hosts{$host}}, [
				$clock,
				"SLR_END_MONTH_RDAP_RTT_service_availability",
				format_float($value / $slrs->{'rdap_rtt'} * 100, '%'),
				get_history("history_uint", get_itemid_by_hostid($hostid, SLV_ITEM_KEY_RDAP_PERFORMED), $clock),
				get_history("history_uint", get_itemid_by_hostid($hostid, SLV_ITEM_KEY_RDAP_FAILED), $clock),
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

	my $sql = "select macro, value from globalmacro where macro in (?, ?, ?, ?, ?, ?, ?, ?)";
	my $params = [
		'{$RSM.SLV.DNS.DOWNTIME}',
		'{$RSM.SLV.NS.DOWNTIME}',
		'{$RSM.SLV.DNS.UDP.RTT}',
		'{$RSM.SLV.DNS.TCP.RTT}',
		'{$RSM.SLV.RDDS.DOWNTIME}',
		'{$RSM.SLV.RDDS.RTT}',
		'{$RSM.SLV.RDAP.DOWNTIME}',
		'{$RSM.SLV.RDAP.RTT}',
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
		$slrs{'rdap_downtime'}   = $value if ($macro eq '{$RSM.SLV.RDAP.DOWNTIME}');
		$slrs{'rdap_rtt'}        = $value if ($macro eq '{$RSM.SLV.RDAP.RTT}');
	}

	fail('global macro {$RSM.SLV.DNS.DOWNTIME} was not found')  unless (exists($slrs{'dns_downtime'}));
	fail('global macro {$RSM.SLV.NS.DOWNTIME} was not found')   unless (exists($slrs{'dns_ns_downtime'}));
	fail('global macro {$RSM.SLV.DNS.UDP.RTT} was not found')   unless (exists($slrs{'dns_udp_rtt'}));
	fail('global macro {$RSM.SLV.DNS.TCP.RTT} was not found')   unless (exists($slrs{'dns_tcp_rtt'}));
	fail('global macro {$RSM.SLV.RDDS.DOWNTIME} was not found') unless (exists($slrs{'rdds_downtime'}));
	fail('global macro {$RSM.SLV.RDDS.RTT} was not found')      unless (exists($slrs{'rdds_rtt'}));
	fail('global macro {$RSM.SLV.RDAP.DOWNTIME} was not found') unless (exists($slrs{'rdap_downtime'}));
	fail('global macro {$RSM.SLV.RDAP.RTT} was not found')      unless (exists($slrs{'rdap_rtt'}));

	return \%slrs;
}

sub get_items($$)
{
	my $slr  = shift;
	my $till = shift;

	my $monitoring_target = get_monitoring_target();

	my $sql;
	my $params;

	if ($monitoring_target eq MONITORING_TARGET_REGISTRY)
	{
		$sql = "select items.itemid, items.key_, items.value_type, hosts.hostid, hosts.host" .
			" from items" .
				" left join hosts on hosts.hostid = items.hostid" .
				" left join hosts_groups on hosts_groups.hostid = hosts.hostid" .
			" where (items.key_ in (?, ?, ?, ?, ?, ?, ?) or items.key_ like ?) and" .
				" hosts_groups.groupid = ? and" .
				" hosts.created <= ?";

		$params = [
			SLV_ITEM_KEY_DNS_DOWNTIME,
			SLV_ITEM_KEY_RDDS_DOWNTIME,
			SLV_ITEM_KEY_RDAP_DOWNTIME,
			SLV_ITEM_KEY_DNS_UDP_PFAILED,
			SLV_ITEM_KEY_DNS_TCP_PFAILED,
			SLV_ITEM_KEY_RDDS_PFAILED,
			SLV_ITEM_KEY_RDAP_PFAILED,
			SLV_ITEM_KEY_DNS_NS_DOWNTIME,
			TLDS_GROUPID,
			$till,
		];
	}
	elsif ($monitoring_target eq MONITORING_TARGET_REGISTRAR)
	{
		$sql = "select items.itemid, items.key_, items.value_type, hosts.hostid, hosts.host" .
			" from items" .
				" left join hosts on hosts.hostid = items.hostid" .
				" left join hosts_groups on hosts_groups.hostid = hosts.hostid" .
			" where items.key_ in (?, ?, ?, ?) and" .
				" hosts_groups.groupid = ? and" .
				" hosts.created <= ?";

		$params = [
			SLV_ITEM_KEY_RDDS_DOWNTIME,
			SLV_ITEM_KEY_RDDS_PFAILED,
			SLV_ITEM_KEY_RDAP_DOWNTIME,
			SLV_ITEM_KEY_RDAP_PFAILED,
			TLDS_GROUPID,
			$till,
		];
	}
	else
	{
		fail("unknown monitoring target '$monitoring_target'");
	}

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
		elsif ($key eq SLV_ITEM_KEY_RDAP_DOWNTIME)
		{
			fail("Unexpected item type (key: '$key', type: '$type'") unless ($type == ITEM_VALUE_TYPE_UINT64);
			push(@{$itemids_uint{$slr->{'rdap_downtime'}}}, $itemid);
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
		elsif ($key eq SLV_ITEM_KEY_RDAP_PFAILED)
		{
			fail("Unexpected item type (key: '$key', type: '$type'") unless ($type == ITEM_VALUE_TYPE_FLOAT);
			push(@{$itemids_float{$slr->{'rdap_rtt'}}}, $itemid);
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

	my $query  = "select value,clock" .
			" from $history_table" .
			" where itemid=? and clock between ? and ?" .
			" order by clock desc" .
			" limit 1";

	my $result = [];

	foreach my $slr (keys(%{$itemids}))
	{
		foreach my $itemid (@{$itemids->{$slr}})
		{
			my $rows = db_select($query, [$itemid, $from, $till]);

			if (@{$rows})
			{
				my ($value, $clock) = @{$rows->[0]};

				if ($value > $slr)
				{
					push(@{$result}, [$itemid, $value, $clock]);
				}
			}
		}
	}

	return $result;
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

	my $action_target = {
		MONITORING_TARGET_REGISTRY , "tld",
		MONITORING_TARGET_REGISTRAR, "registrar",
	}->{get_monitoring_target()};

	my ($sec, $min, $hour, $mday, $mon, $year) = localtime($clock);
	my $clock_str = sprintf("%.4d.%.2d.%.2d %.2d:%.2d:%.2d UTC", $year + 1900, $mon + 1, $mday, $hour, $min, $sec);

	my $cmd = "/opt/slam/library/alertcom/script.py";

	my @args = (
		"zabbix alert",
		join("#", ($action_target, "PROBLEM", $host, @{$data})),
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
