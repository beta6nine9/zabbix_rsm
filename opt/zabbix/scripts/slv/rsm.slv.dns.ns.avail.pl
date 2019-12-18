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
use Data::Dumper;

parse_slv_opts();
fail_if_running();
set_slv_config(get_rsm_config());
db_connect();

slv_exit(SUCCESS) if (get_monitoring_target() ne MONITORING_TARGET_REGISTRY);

my $cfg_keys_out_pattern = 'rsm.slv.dns.ns.avail';
my $cfg_keys_in_pattern = 'rsm.dns.ns.status';


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
# we use ns status values, but take the delay value from the udp macro
my $cycle_delay = get_dns_udp_delay();
my $current_month_latest_cycle = current_month_latest_cycle();
my $cfg_minonline = get_macro_dns_probe_online();
my $probes = get_probes('DNS');

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

	foreach (@{get_ns_avail_items($hostid)})
	{
		process_slv_item($tld, @$_);
	}
}

sub get_ns_avail_items
{
	my $hostid = shift;

	return db_select("select itemid,key_ from items where hostid=$hostid".
			 " and key_ like '$cfg_keys_out_pattern\[%'".
			 " and status=${\ITEM_STATUS_ACTIVE}");
}

sub get_ns_status_items
{
	my $hostid = shift;
	my $nsname = shift;

	return db_select("select itemid from items where hostid=$hostid".
				" and key_ like '$cfg_keys_in_pattern\[$nsname\]'".
				" and status=${\ITEM_STATUS_ACTIVE}");
}

sub process_slv_item
{
	my $tld = shift;
	my $slv_itemid = shift;
	my $slv_itemkey = shift; # rsm.slv.dns.ns.avail[ns1.longrow,172.19.0.3]

	if ($slv_itemkey =~ /\[(.+),(.+)\]$/)
	{
		process_cycles($tld, $slv_itemid, $slv_itemkey, $1);
	}
	else
	{
		fail("missing ns,ip pair in item key '$slv_itemkey'");
	}
}

sub process_cycles # for a particular slv item
{
	my $tld = shift;
	my $slv_itemid = shift;
	my $slv_itemkey = shift;
	my $nsname = shift;

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
			#start from beginning of the current month if no slv data
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
				"Up (not enough probes online, $online_probe_count while $cfg_minonline required)");

			next;
		}

		my $tld_ns_status_itemids = get_all_tld_ns_status_itemids($tld, $nsname);
		my $ns_status_values = get_ns_status_values($from, $till, $tld_ns_status_itemids->{$tld}{$nsname});
		my $probes_with_results = scalar(@{$ns_status_values});

		if ($probes_with_results < $cfg_minonline)
		{
			push_value($tld, $slv_itemkey, $from, UP_INCONCLUSIVE_NO_DATA, ITEM_VALUE_TYPE_UINT64,
				"Up (not enough probes with results, $probes_with_results while $cfg_minonline required)");

			next;
		}

		my $total_ns_status_items_count = 0;
		my $down_ns_status_items_count = 0;

		foreach  my $x( ${ns_status_values} )
		{
			foreach my $xx(@{$x})
			{
				if ($xx == 0) # ns status 0 means it is down
				{
					$down_ns_status_items_count++;
				}

				$total_ns_status_items_count++;
			}
		}

		my $down_ns_status_items_limit = (SLV_UNAVAILABILITY_LIMIT * 0.01) * $total_ns_status_items_count;
		push_value($tld,
				$slv_itemkey,
				$from,
				($down_ns_status_items_count > $down_ns_status_items_limit) ? DOWN : UP,
				ITEM_VALUE_TYPE_UINT64);
	}
}

sub get_all_tld_ns_status_itemids
{
	my $itemids = {};
	my $tld = shift;
	my $nsname = shift;

	foreach my $probename( keys %{$probes} ) {
		my $res = db_select(
			"select hostid from hosts where host='$tld $probename'".
			" and status=${\ITEM_STATUS_ACTIVE}");
		my $ns_status_items = get_ns_status_items($res->[0]->[0], $nsname);
		push(@{$itemids->{$tld}{$nsname}{$res->[0]->[0]}}, \@{$ns_status_items});
	}

	my $res = {};
	my @tld_items = ();

	#
	# flatten the result:
	#
	#      tld             tld ns          hostid (probe)   itemid
	#
	# { 'hazelburn' => {
	#                  'ns1.hazelburn' => {
	#                                       '100023' => [
	#                                                     [
	#                                                       [
	#                                                         100561
	#                                                       ]
	#                                                     ]
	#                                                   ],
	#                                       '100024' => [
	#                                                     [
	#                                                       [
	#                                                         100569
	#                                                       ]
	#                                                     ]
	#                                                   ]
	#                                     }
	#                }
	# }
	#
	# into:
	#
	#   { 'hazelburn' => {
	#                    'ns1.hazelburn' => [
	#                                         100569,
	#                                         100561
	#                                       ]
	#                  }
	# };
	foreach my $x(values %{$itemids->{$tld}{$nsname}})
	{
		foreach my $xx(@{$x})
		{
		    foreach my $xxx(@{$xx})
		    {
			foreach my $xxxx(@{$xxx})
			{
				push @tld_items , $xxxx;
			}
		}
		}
		$res->{$tld}{$nsname} = \@tld_items;
	}

	return $res;
}

my $online_probe_count_cache = {};

sub get_online_probe_count
{
	my $from = shift;
	my $till = shift;
	my $key = "$from-$till";

	if (!defined($online_probe_count_cache->{$key}))
	{
		$online_probe_count_cache->{$key} = scalar(keys(%{get_probe_times($from, $till, $probes)}));
	}

	return $online_probe_count_cache->{$key};
}

sub get_ns_status_values
{
	my $from = shift;
	my $till = shift;
	my $rtt_itemids = shift;
	my $itemids_placeholder = join(",", ("?") x scalar(@{$rtt_itemids}));

	return db_select_col(
		"select value" .
		" from history_uint" .
		" where itemid in ($itemids_placeholder) and clock between ? and ?",
		[@{$rtt_itemids}, $from, $till]
	);
}

sub current_month_latest_cycle
{
	# we don't know the rollweek bounds yet so we assume it ends at least few minutes back
	return cycle_start($now, $cycle_delay) - AVAIL_SHIFT_BACK;
}
