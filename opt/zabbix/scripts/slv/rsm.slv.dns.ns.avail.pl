#!/usr/bin/env perl
#
# Availability of particular nameservers

use FindBin;
use lib "$FindBin::RealBin/..";

use strict;
use warnings;

use RSM;
use RSMSLV;
use TLD_constants qw(:groups :api);

parse_slv_opts();
fail_if_running();
set_slv_config(get_rsm_config());
db_connect();

slv_exit(SUCCESS) if (get_monitoring_target() ne MONITORING_TARGET_REGISTRY);

my $cfg_key_in_pattern  = 'rsm.dns.rtt[%,%,%]';		# <NS>,<IP>,<PROTOCOL>
my $cfg_key_out_pattern = 'rsm.slv.dns.ns.avail[%,%]';	# <NS>,<IP>

my $now;

if (opt('now'))
{
	$now = getopt('now');
}
else
{
	$now = time();
}

my $max_cycles = (opt('cycles') ? getopt('cycles') : slv_max_cycles('dns'));
my $cycle_delay = get_dns_delay();
my $cfg_minonline = get_macro_dns_probe_online();

my $dns_udp_rtt_high = get_macro_dns_udp_rtt_high();
my $dns_tcp_rtt_high = get_macro_dns_tcp_rtt_high();
my $rtt_itemids = get_all_dns_rtt_itemids();

my $probes_ref = get_probes('DNS');

init_values();
process_values();
send_values();

slv_exit(SUCCESS);

sub process_values
{
	my $tlds = get_tlds_and_hostids(opt('tld') ? getopt('tld') : undef);

	foreach my $tld (@{$tlds})
	{
		my ($host, $hostid) = @{$tld};

		set_log_tld($host);
		process_tld($host, $hostid);
		unset_log_tld();
	}
}

sub process_tld($$)
{
	my $tld    = shift;
	my $hostid = shift;

	my $rows = db_select(
		"select itemid,key_".
		" from items".
		" where hostid=$hostid".
			" and flags=" . ZBX_FLAG_DISCOVERY_NORMAL .
			" and key_ like '$cfg_key_out_pattern'".
			" and status<>" . ITEM_STATUS_DISABLED
	);

	foreach my $row (@{$rows})
	{
		my ($slv_itemid, $slv_itemkey) = @{$row};

		process_cycles($tld, $slv_itemid, $slv_itemkey);
	}
}

# process cycles of a particular NS-IP pair
sub process_cycles($$$$)
{
	my $tld         = shift;
	my $slv_itemid  = shift;
	my $slv_itemkey = shift;

	my $nsip = get_nsip_from_key($slv_itemkey);

	my $slv_clock;

	get_lastvalue($slv_itemid, ITEM_VALUE_TYPE_UINT64, undef, \$slv_clock);

	for (my $n = 0; $n < $max_cycles; $n++)
	{
		if (defined($slv_clock))
		{
			$slv_clock += $cycle_delay;
		}
		else
		{
			# start from beginning of the current month if no slv data
			$slv_clock = current_month_first_cycle();
		}

		if ($slv_clock >= cycle_start($now, $cycle_delay))
		{
			dbg("processed all available data");
			last;
		}

		my $from = $slv_clock;
		my $till = $slv_clock + $cycle_delay - 1;

		if (is_rsmhost_reconfigured($tld, $cycle_delay, $from))
		{
			push_value($tld, $slv_itemkey, $from, UP_INCONCLUSIVE_RECONFIG, ITEM_VALUE_TYPE_UINT64,
				"Up (rsmhost has been reconfigured recently)");

			next;
		}

		my $online_probe_count = scalar(@{online_probes($probes_ref, $from, $cycle_delay)});

		if ($online_probe_count < $cfg_minonline)
		{
			push_value($tld, $slv_itemkey, $from, UP_INCONCLUSIVE_NO_PROBES, ITEM_VALUE_TYPE_UINT64,
				"Up (not enough probes online, $online_probe_count while" .
				" $cfg_minonline required)");

			next;
		}

		my $udp_rtt_values = get_rtt_values($from, $till, $rtt_itemids->{$tld}{$nsip}{"udp"} // []);
		my $tcp_rtt_values = get_rtt_values($from, $till, $rtt_itemids->{$tld}{$nsip}{"tcp"} // []);
		my $probes_with_results = scalar(@{$udp_rtt_values}) + scalar(@{$tcp_rtt_values});

		if ($probes_with_results < $cfg_minonline)
		{
			if (cycle_start(time(), $cycle_delay) - $from < WAIT_FOR_AVAIL_DATA)
			{
				# not enough data, but cycle isn't old enough+
				last;
			}
			else
			{
				push_value($tld, $slv_itemkey, $from, UP_INCONCLUSIVE_NO_DATA, ITEM_VALUE_TYPE_UINT64,
					"Up (not enough probes with results, $probes_with_results" .
					" while $cfg_minonline required)");

				next;
			}
		}

		my $down_rtt_count = 0;

		foreach my $udp_rtt_value (@{$udp_rtt_values})
		{
			if (is_service_error('dns', $udp_rtt_value, $dns_udp_rtt_high))
			{
				$down_rtt_count++;
			}
		}

		foreach my $tcp_rtt_value (@{$tcp_rtt_values})
		{
			if (is_service_error('dns', $tcp_rtt_value, $dns_tcp_rtt_high))
			{
				$down_rtt_count++;
			}
		}

		my $limit = (SLV_UNAVAILABILITY_LIMIT * 0.01) * $probes_with_results;

		push_value($tld, $slv_itemkey, $from, ($down_rtt_count > $limit) ? DOWN : UP, ITEM_VALUE_TYPE_UINT64);
	}
}

sub get_all_dns_rtt_itemids
{
	my $rows = db_select(
		"select" .
			" substring_index(hosts.host,' ',1)," .
			"items.itemid," .
			"items.key_" .
		" from" .
			" items" .
			" left join hosts on hosts.hostid = items.hostid" .
		" where" .
			" items.key_ like '$cfg_key_in_pattern' and" .
			" items.flags=${\ZBX_FLAG_DISCOVERY_CREATED} and" .
			" items.status<>${\ITEM_STATUS_DISABLED} and" .
			" hosts.host like '% %'"
	);

	my $itemids = {};

	foreach my $row (@{$rows})
	{
		my ($tld, $itemid, $key) = @{$row};

		$key =~ /^.+\[(.+,.+),(.+)\]$/;
		my $nsip = $1;
		my $protocol = $2;

		push(@{$itemids->{$tld}{$nsip}{$protocol}}, $itemid);
	}

	return $itemids;
}

sub get_rtt_values($$$)
{
	my $from    = shift;
	my $till    = shift;
	my $itemids = shift;

	return [] unless (scalar(@{$itemids}));

	my $itemids_placeholder = join(",", ("?") x scalar(@{$itemids}));

	return db_select_col(
		"select value" .
		" from history" .
		" where itemid in ($itemids_placeholder) and clock between ? and ?",
		[@{$itemids}, $from, $till]
	);
}
