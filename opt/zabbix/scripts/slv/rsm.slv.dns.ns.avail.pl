#!/usr/bin/perl
#
# Availability of particular nameservers

BEGIN { our $MYDIR = $0; $MYDIR =~ s,(.*)/.*/.*,$1,; $MYDIR = '..' if ($MYDIR eq $0); }
use lib $MYDIR;
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

my $cfg_key_in_pattern = 'rsm.dns.rtt[%,%,%]';		# <NS>,<IP>,<PROTOCOL>
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
my $current_month_latest_cycle = current_month_latest_cycle();
my $cfg_minonline = get_macro_dns_probe_online();
my $udp_dns_rtt_low = get_rtt_low('dns', PROTO_UDP);
my $tcp_dns_rtt_low = get_rtt_low('dns', PROTO_TCP);
my $rtt_itemids = get_all_dns_rtt_itemids();

init_values();
process_values();
send_values();

slv_exit(SUCCESS);

sub process_values
{
	foreach my $tld (@{get_tlds_and_hostids(opt('tld') ? getopt('tld') : undef)})
	{
		set_log_tld($tld->[0]);

		process_tld(@{$tld});

		unset_log_tld();
	}
}

sub process_tld
{
	my $tld = shift;
	my $hostid = shift;

	foreach (@{get_slv_dns_ns_avail_items($hostid)})
	{
		process_slv_item($tld, @$_);
	}
}

sub get_slv_dns_ns_avail_items
{
	my $hostid = shift;

	return db_select(
		"select i.itemid,i.key_".
		" from items i".
		" where i.hostid=$hostid".
			" and i.flags=${\ZBX_FLAG_DISCOVERY_NORMAL}".
			" and i.key_ like '$cfg_key_out_pattern'".
			" and i.status<>". ITEM_STATUS_DISABLED
	);
}

sub process_slv_item
{
	my $tld = shift;
	my $slv_itemid = shift;
	my $slv_itemkey = shift;	# rsm.slv.dns.ns.avail[<NS>,<IP>]

	if ($slv_itemkey =~ /\[(.+,.+)\]$/)
	{
		process_cycles($tld, $slv_itemid, $slv_itemkey, $1);
	}
	else
	{
		fail("missing ns,ip pair in item key '$slv_itemkey'");
	}
}

# process cycles of a particular NS-IP pair
sub process_cycles
{
	my $tld = shift;
	my $slv_itemid = shift;
	my $slv_itemkey = shift;
	my $nsip = shift;

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

		if ($slv_clock >= $current_month_latest_cycle)
		{
			dbg("processed all available data");
			last;
		}

		my $from = $slv_clock;
		my $till = $slv_clock + $cycle_delay - 1;

		my $online_probe_count = get_online_probe_count($from, $till);

		if ($online_probe_count < $cfg_minonline)
		{
			push_value($tld, $slv_itemkey, $from, UP_INCONCLUSIVE_NO_PROBES, ITEM_VALUE_TYPE_UINT64,
				"Up (not enough probes online, $online_probe_count while" .
				" $cfg_minonline required)");

			next;
		}

		my $udp_rtt_values = get_rtt_values($from, $till, $rtt_itemids->{$nsip}{"udp"} // []);
		my $tcp_rtt_values = get_rtt_values($from, $till, $rtt_itemids->{$nsip}{"tcp"} // []);
		my $probes_with_results = scalar(@{$udp_rtt_values}) + scalar(@{$tcp_rtt_values});

		if ($probes_with_results < $cfg_minonline)
		{
			push_value($tld, $slv_itemkey, $from, UP_INCONCLUSIVE_NO_DATA, ITEM_VALUE_TYPE_UINT64,
				"Up (not enough probes with results, $probes_with_results" .
				" while $cfg_minonline required)");

			next;
		}

		my $down_rtt_count = 0;

		foreach my $udp_rtt_value (@{$udp_rtt_values})
		{
			if (is_service_error('dns', $udp_rtt_value, $udp_dns_rtt_low))
			{
				$down_rtt_count++;
			}
		}

		foreach my $tcp_rtt_value (@{$tcp_rtt_values})
		{
			if (is_service_error('dns', $tcp_rtt_value, $tcp_dns_rtt_low))
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
		"select substring_index(hosts.host,' ',1),items.itemid,items.key_" .
		" from" .
			" items" .
			" left join hosts on hosts.hostid = items.hostid" .
		" where" .
			" items.key_ like '$cfg_key_in_pattern' and" .
			" items.flags=${\ZBX_FLAG_DISCOVERY_NORMAL} and" .
			" items.status<>${\ITEM_STATUS_DISABLED} and" .
			" hosts.host like '% %'"
	);

	my $itemids = {};

	foreach my $row (@{$rows})
	{

		my $tld    = $row->[0];
		my $itemid = $row->[1];
		my $key    = $row->[2];

		$key =~ s/^.+\[(.+,.+),(.+)\]$//;
		my $nsip = $1;
		my $protocol = $2;

		push(@{$itemids->{$nsip}{$protocol}}, $itemid);
	}

	return $itemids;
}

my $online_probe_count_cache = {};

sub get_online_probe_count
{
	my $from = shift;
	my $till = shift;
	my $key = "$from-$till";

	if (!defined($online_probe_count_cache->{$key}))
	{
		$online_probe_count_cache->{$key} = scalar(keys(%{get_probe_times($from, $till, get_probes('DNS'))}));
	}

	return $online_probe_count_cache->{$key};
}

sub get_rtt_values
{
	my $from = shift;
	my $till = shift;
	my $rtt_itemids = shift;

	return [] unless (scalar(@{$rtt_itemids}));

	my $itemids_placeholder = join(",", ("?") x scalar(@{$rtt_itemids}));

	return db_select_col(
		"select value" .
		" from history" .
		" where itemid in ($itemids_placeholder) and clock between ? and ?",
		[@{$rtt_itemids}, $from, $till]
	);
}

sub current_month_latest_cycle
{
	# we don't know the rollweek bounds yet so we assume it ends at least few minutes back
	return cycle_start($now, $cycle_delay) - AVAIL_SHIFT_BACK;
}
