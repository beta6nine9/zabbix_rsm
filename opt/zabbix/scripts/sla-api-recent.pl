#!/usr/bin/perl

use strict;
use warnings;

use Path::Tiny;
use lib path($0)->parent->realpath()->stringify();

use Data::Dumper;

use Parallel::ForkManager;

use RSM;
use RSMSLV;
use TLD_constants qw(:api :config :groups :items);
use ApiHelper;
use File::Copy;
use sigtrap 'handler' => \&main_process_signal_handler, 'normal-signals';

$Data::Dumper::Terse = 1;	# do not output names like "$VAR1 = "
$Data::Dumper::Pair = " : ";	# use separator instead of " => "
$Data::Dumper::Useqq = 1;	# use double quotes instead of single quotes
$Data::Dumper::Indent = 1;	# 1 provides less indentation instead of 2

use constant TARGET_PLACEHOLDER => 'TARGET_PLACEHOLDER';	# for non-DNS services

use constant MAX_PERIOD => 30 * 60;	# 30 minutes

use constant SUBSTR_KEY_LEN => 20;	# for logging

use constant DEFAULT_MAX_CHILDREN => 64;
use constant DEFAULT_MAX_WAIT => 600;	# maximum seconds to wait befor terminating child process

use constant DEFAULT_INITIAL_MEASUREMENTS_LIMIT => 7200;	# seconds, if the metric is not in cache and
								# no measurements within this period, start generating
								# them from this period in the past back for recent
								# measurement files for an incident

sub main_process_signal_handler();
sub process_server($);
sub process_tld_batch($$$$$$);
sub process_tld($$$$$);
sub cycles_to_calculate($$$$$$$$);
sub get_lastvalues_from_db($$$);
sub calculate_cycle($$$$$$$$$);
sub get_interfaces($$$);
sub probe_online_at_init();
sub get_history_by_itemid($$$);
sub child_error($$$$$);
sub update_lastvalues_cache($);
sub set_on_finish($);
sub wait_for_children($);
sub terminate_children($);
sub get_swap_usage($);

parse_opts('tld=s', 'service=s', 'server-id=i', 'now=i', 'period=i', 'print-period!', 'max-children=i', 'max-wait=i', 'debug2!');

setopt('nolog');

usage() if (opt('help'));

exit_if_running();	# exit with 0 exit code

my $max_wait = getopt('max-wait') // DEFAULT_MAX_WAIT;

if (opt('debug'))
{
	dbg("command-line parameters:");
	dbg("$_ => ", getopt($_)) foreach (optkeys());
}

ah_set_debug(getopt('debug'));

my $config = get_rsm_config();

set_slv_config($config);

my $initial_measurements_limit = $config->{'sla_api'}->{'initial_measurements_limit'} // DEFAULT_INITIAL_MEASUREMENTS_LIMIT;

my @server_keys;

if (opt('server-id'))
{
	push(@server_keys, get_rsm_server_key(getopt('server-id')));
}
else
{
	@server_keys = get_rsm_server_keys($config);
}

validate_tld(getopt('tld'), \@server_keys) if (opt('tld'));
validate_service(getopt('service')) if (opt('service'));

my $server_count = scalar(@server_keys);

fail("no servers defined") unless ($server_count);

my $real_now = time();
my $now = (getopt('now') // $real_now);

my $max_period = (opt('period') ? getopt('period') * 60 : MAX_PERIOD);

db_connect();

my $cfg_minonline = get_macro_dns_probe_online();
my $cfg_minns = get_macro_minns();

fail("number of required working Name Servers is configured as $cfg_minns") if (1 > $cfg_minns);

my %delays;
$delays{'dns'} = $delays{'dnssec'} = get_dns_udp_delay($now);
$delays{'rdds'} = get_rdds_delay($now);

my %clock_limits;
$clock_limits{'dns'} = $clock_limits{'dnssec'} = cycle_start($now - $initial_measurements_limit, $delays{'dnssec'});
$clock_limits{'rdds'} = cycle_start($now - $initial_measurements_limit, $delays{'rdds'});

db_disconnect();

my %service_keys = (
	'dns' => 'rsm.slv.dns.avail',
	'dnssec' => 'rsm.slv.dnssec.avail',
	'rdds' => 'rsm.slv.rdds.avail'
);

my %rtt_limits;

my $children_per_server;

if (opt('max-children'))
{
	my $max_children = getopt('max-children');

	if ($max_children % $server_count != 0)
	{
		fail("max-children value must be divisible by the number of servers ($server_count)");
	}

	$children_per_server = $max_children / $server_count;
}
else
{
	my $max_children = DEFAULT_MAX_CHILDREN;

	$max_children = $server_count if ($server_count > DEFAULT_MAX_CHILDREN);

	while ($max_children % $server_count)
	{
		$max_children--;
	}

	$children_per_server = $max_children / $server_count;

	fail("cannot calculate maximum number of processes to use") unless ($children_per_server);
}

info("servers             : $server_count") if (opt('stats'));
info("max children/server : $children_per_server") if (opt('stats'));

my $child_failed = 0;
my $signal_sent = 0;
my $lastvalues_cache = {};

my $fm = new Parallel::ForkManager($server_count);
set_on_finish($fm);

# {
#     PID => {'desc' => 'server_#_parent', 'from' => 1234324235, 'swap-usage' => '0 kB'},
#     ...
#     PID => {'desc' => 'server_#_child', 'from' => 1234324235, 'swap-usage' => '4 kB'},
#     ...
# }
my %child_desc;

foreach my $server_key (@server_keys)
{
	my $pid = $fm->start();

	if ($pid == 0)
	{
		init_process();

		process_server($server_key);

		finalize_process();

		$fm->finish(SUCCESS);
	}

	$child_desc{$pid} = {
		'desc' => "${server_key}_parent",
		'from' => time(),
		'swap-usage' => '0 kB'
	};
	#$child_desc{$pid}->{'smaps-dumped'} = 0;

	dbg("$child_desc{$pid}->{'desc'} (PID:$pid) STARTED");
}

wait_for_children($fm);

slv_exit($child_failed ? E_FAIL : SUCCESS);

sub wait_for_children($)
{
	my $fm = shift;

	for (;;)
	{
		$fm->reap_finished_children();

		my @procs = $fm->running_procs();

		return unless (scalar(@procs));

		dbg("waiting for ", scalar(@procs), " children:");

		# check wether there's long running process
		foreach my $pid (@procs)
		{
			my $swap_usage = get_swap_usage($pid);

			if (defined($swap_usage) && ($swap_usage ne $child_desc{$pid}->{'swap-usage'}))
			{
				wrn("$child_desc{$pid}->{'desc'} (PID:$pid) is swapping $swap_usage");

				$child_desc{$pid}->{'swap-usage'} = $swap_usage;

				# TODO: consider writing if is over 1000 kB, add date/time to the file name, write only if size changed
				# if (!$child_desc{$pid}->{'smaps-dumped'})
				# {
				# 	mkdir("/tmp/sla-api-recent-smaps") unless (-d "/tmp/sla-api-recent-smaps");
				#
				# 	copy("/proc/$pid/smaps","/tmp/sla-api-recent-smaps/$pid") or fail("cannot copy smaps file: $!");
				#
				# 	$child_desc{$pid}->{'smaps-dumped'} = 1;
				#
				# 	wrn("$child_desc{$pid}->{'desc'} (PID:$pid) smaps file saved to /tmp/sla-api-recent-smaps/$pid");
				# }
			}
			else
			{
				dbg("$child_desc{$pid}->{'desc'} (PID:$pid), running for ", (time() - $child_desc{$pid}->{'from'}), " seconds");
			}
		}

		sleep(1);
	}
}

sub main_process_signal_handler()
{
	wrn("main process caught a signal: $!");
	slv_exit(E_FAIL);
}

sub process_server($)
{
	my $server_key = shift;

	$lastvalues_cache = {};

	if (ah_get_recent_cache($server_key, \$lastvalues_cache) != AH_SUCCESS)
	{
		dbg("there's no recent measurements cache file yet, but no worries");
		$lastvalues_cache->{'tlds'} = {};
	}

	# initialize probe online cache
	probe_online_at_init();

	# probes available for every service
	my %probes;
	my $server_tlds;

	db_connect($server_key);

	my $all_probes_ref = get_probes();

	if (opt('tld'))
	{
		$server_tlds = [getopt('tld')];
	}
	else
	{
		$server_tlds = get_tlds();
	}

	db_disconnect($server_key);

	return unless (scalar(@{$server_tlds}));

	my $server_tld_count = scalar(@{$server_tlds});

	my $children_count = ($server_tld_count < $children_per_server ? $server_tld_count : $children_per_server);

	my $tlds_per_child = int($server_tld_count / $children_count);

	info("children/$server_key : $children_count") if (opt('stats'));

	my $fm = new Parallel::ForkManager($children_count);
	set_on_finish($fm);

	my $tldi_begin = 0;
	my $tldi_end;

	%child_desc = ();

	while ($children_count)
	{
		$tldi_end = $tldi_begin + $tlds_per_child;

		# add one extra from remainder
		$tldi_end++ if ($server_tld_count - $tldi_begin - ($children_count * $tlds_per_child));

		my %child_data;

		my $pid = $fm->start();

		if ($pid == 0)
		{
			init_process();

			set_alarm($max_wait);

			process_tld_batch($server_tlds, $tldi_begin, $tldi_end, \%child_data, \%probes, $all_probes_ref);

			finalize_process();

			$fm->finish(SUCCESS, \%child_data);
		}

		$child_desc{$pid} = {
			'desc' => "${server_key}_child",
			'from' => time(),
			'swap-usage' => '0 kB'
		};
		#$child_desc{$pid}->{'smaps-dumped'} = 0;

		dbg("$child_desc{$pid}->{'desc'} (PID:$pid) STARTED");

		$tldi_begin = $tldi_end;

		$children_count--;
	}

	wait_for_children($fm);

	# Do not update cache if something happened.
	if (!opt('dry-run') && !opt('now'))
	{
		if (ah_save_recent_cache($server_key, $lastvalues_cache) != AH_SUCCESS)
		{
			fail("cannot save recent measurements cache: ", ah_get_error());
		}
	}
}

sub process_tld_batch($$$$$$)
{
	my $tlds = shift;
	my $tldi_begin = shift;
	my $tldi_end = shift;
	my $child_data_ref = shift;
	my $probes_ref = shift;		# probes by services
	my $all_probes_ref = shift;	# all available probes in the system

	db_connect($server_key);

	for (my $tldi = $tldi_begin; $tldi != $tldi_end; $tldi++)
	{
		$tld = $tlds->[$tldi]; # global variable

		# Last values from the "lastvalue" (availability, RTTs) and "lastvalue_str" (IPs) tables.
		#
		# {
		#     'tlds' => {
		#         tld => {
		#             service => {
		#                 'probes' => {
		#                     probe => {
		#                         itemid = {
		#                             'key' => key,
		#                             'value_type' => value_type,
		#                             'clock' => clock
		#                         }
		#                     }
		#                     ...
		#                     "" => {                            <-- empty probe is for *.avail (<TLD>) items
		#                         itemid = {
		#                             'key' => ,
		#                             'value_type' => value_type,
		#                             'clock' => clock
		#                         }
		#                     }
		#                 }
		#             }
		#         }
		#     }
		# }
		#
		# NB! Besides results from probes we cache Service Availability values, which
		# are stored on a <TLD> host, for those we use empty string ("") as probe.

		my $lastvalues_db = {'tlds' => {}};
		get_lastvalues_from_db($lastvalues_db, $tld, \%delays);

		$lastvalues_cache->{'tlds'}{$tld} //= {};

		process_tld(
			$tld,
			$probes_ref,
			$all_probes_ref,
			$lastvalues_db->{'tlds'}{$tld},
			$lastvalues_cache->{'tlds'}{$tld}
		);

		$child_data_ref->{$tld} = $lastvalues_cache->{'tlds'}{$tld};

		$tld = undef;	# unset global variable
	}

	db_disconnect();
}

sub process_tld($$$$$)
{
	my $tld = shift;
	my $probes_ref = shift;
	my $all_probes_ref = shift;
	my $lastvalues_db_tld = shift;
	my $lastvalues_cache_tld = shift;

	foreach my $service (sort(keys(%{$lastvalues_db_tld})))
	{
		next if (opt('service') && $service ne getopt('service'));

		my @cycles_to_calculate;

		# get actual cycle times to calculate
		if (cycles_to_calculate(
				$tld,
				$service,
				$delays{$service},
				$max_period,
				$service_keys{$service},
				$lastvalues_db_tld,
				$lastvalues_cache_tld,
				\@cycles_to_calculate) == E_FAIL)
		{
			next;
		}

		if (opt('debug'))
		{
			dbg("$service cycles to calculate: ", join(',', map {ts_str($_)} (@cycles_to_calculate)));
		}

		next if (scalar(@cycles_to_calculate) == 0);

		my $cycles_from = $cycles_to_calculate[0];
		my $cycles_till = $cycles_to_calculate[-1];

		next unless (tld_service_enabled($tld, $service, $cycles_from));

		if (opt('print-period'))
		{
			info("selected $service period: ", selected_period(
				$cycles_from,
				cycle_end($cycles_till, $delays{$service})
			));
		}

		my $interfaces_ref = get_interfaces($tld, $service, $now);

		$probes_ref->{$service} = get_probes($service) unless (defined($probes_ref->{$service}));

		# TODO: RTT limits are currently stored per Server, not per TLD, so we
		#       shouldn't be collecting them for the periods we aready handled.

		if ($service eq 'dns')
		{
			# rtt limits only considered for DNS currently
			$rtt_limits{$service} = get_history_by_itemid(
				CONFIGVALUE_DNS_UDP_RTT_HIGH_ITEMID,
				$cycles_from,
				cycle_end($cycles_till, $delays{$service})
			);

			wrn("no DNS RTT limits in history for period ", ts_str($cycles_from),
					" - ", ts_str(cycle_end($cycles_till, $delays{$service})))
				unless (scalar(@{$rtt_limits{$service}}));
		}

		# these are cycles we are going to recalculate for this tld-service
		foreach my $clock (@cycles_to_calculate)
		{
			calculate_cycle(
				$tld,
				$service,
				$lastvalues_db_tld->{$service}{'probes'},
				$clock,
				$delays{$service},
				$rtt_limits{$service},
				$probes_ref->{$service},
				$all_probes_ref,
				$interfaces_ref
			);
		}
	}
}

sub add_cycles($$$$$$$$$$$)
{
	my $tld = shift;
	my $service = shift;
	my $probe = shift;
	my $itemid = shift;
	my $lastclock = shift;
	my $lastclock_db = shift;
	my $delay = shift;
	my $max_period = shift;
	my $cycles_ref = shift;
	my $lastvalues_cache_tld = shift;
	my $lastvalues_db_tld = shift;	# for debugging only

	return if ($lastclock == $lastclock_db);	# we are up-to-date, according to cache

	$lastclock += $delay; # don't process cycle that is already processed

	my $cycle_start = cycle_start($lastclock, $delay);

	my $db_cycle_start = cycle_start($lastclock_db, $delay);

	my $max_clock = cycle_end($cycle_start - $delay + $max_period, $delay);

	# issue #511
	# Sometimes we get strange timestamp of cycle to calculate, e. g. 960 .
	# Next time it happens, print out related variables, we want to find out
	# if this value comes from cache, database (unlikely) or last_update.txt .
	# Currently we keep 2 month of history, so we'll use 7776000 seconds
	# (3 months) from current time to understand if the timestamp is corrupted.
	if ($real_now - $cycle_start > 7776000)
	{
		my $rows_ref = db_select("select key_ from items where itemid=$itemid");
		my $key = $rows_ref->[0]->[0];

		fail("something went wrong, while getting cycles to calculate got time \"", ts_full($cycle_start), "\"",
			", which is over 3 months old. Affected variables:\n",
			"  itemid       : $itemid\n",
			"  key          : $key\n",
			"  probe        : $probe\n",
			"  lastclock    : ", ts_full($lastclock), "\n",
			"  lastclock_db : ", ts_full($lastclock_db));
	}

	# keep adding cycles to calculate while we are inside max period and within lastvalue
	while ($cycle_start < $max_clock && $lastclock <= $lastclock_db)
	{
		if ($cycle_start == $db_cycle_start)
		{
			# cache the real clock of the item
			$lastvalues_cache_tld->{$service}{'probes'}{$probe}{$itemid}{'clock'} = $lastclock_db;
		}
		else
		{
			# we don't know the real clock so cache cycle start
			$lastvalues_cache_tld->{$service}{'probes'}{$probe}{$itemid}{'clock'} = $cycle_start;
		}

		if (opt('debug'))
		{
			dbg("cycle ", ts_str($cycle_start), " will be calculated because of item ",
				substr(
					$lastvalues_db_tld->{$service}{'probes'}{$probe}{$itemid}{'key'},
					0,
					SUBSTR_KEY_LEN
				),
				", cache clock ", ts_str($lastvalues_cache_tld->{$service}{'probes'}{$probe}{$itemid}{'clock'})
			);
		}

		# for cycles_to_calculate we use the timestamp of the beginning of the cycle
		$cycles_ref->{$cycle_start} = 1;

		# move forward
		$lastclock += $delay;
		$cycle_start += $delay;
	}
}

#
# TODO: This function currently updates cache, which is not reflected in the name.
#
sub cycles_to_calculate($$$$$$$$)
{
	my $tld = shift;
	my $service = shift;
	my $delay = shift;
	my $max_period = shift;	# seconds
	my $service_key = shift;
	my $lastvalues_db_tld = shift;
	my $lastvalues_cache_tld = shift;
	my $cycles_ref = shift;	# result

	my %cycles;

	# empty probe is for *.avail (<TLD>) items, we calculate cycles based on those values
	my $probe = "";

	if (%{$lastvalues_db_tld->{$service}{'probes'}{$probe} // {}})
	{
		foreach my $itemid (keys(%{$lastvalues_db_tld->{$service}{'probes'}{$probe}}))
		{
			my $lastclock_db = $lastvalues_db_tld->{$service}{'probes'}{$probe}{$itemid}{'clock'};

			my $lastclock;

			my $ts;

			if (opt('now'))
			{
				$lastclock = cycle_start(getopt('now') - $delay, $delay);

				dbg("using specified last clock: ", ts_str($lastclock));
			}
			elsif (defined($lastvalues_cache_tld->{$service}{'probes'}{$probe}{$itemid}))
			{
				$lastclock = $lastvalues_cache_tld->{$service}{'probes'}{$probe}{$itemid}{'clock'};

				dbg("$service: using last clock from cache: ", ts_full($lastclock));

				if ($lastclock > $lastclock_db)
				{
					fail("item ($itemid) clock ($lastclock) in cache is newer than in database ($lastclock_db)");
				}
			}
			elsif (ah_get_most_recent_measurement_ts(
					ah_get_api_tld($tld),
					$service,
					$delay,
					cycle_start($now, $delay),
					$clock_limits{$service},
					\$ts) == AH_SUCCESS)
			{
				$lastclock = $ts + $delay;

				dbg("$service: itemid $itemid from probe \"$probe\" not in cache yet");
				dbg("using the time since most recent measurement file: ", ts_str($lastclock));
			}
			else
			{
				$lastclock = $clock_limits{$service};

				dbg("$service: itemid $itemid from probe \"$probe\" not in cache yet");
				dbg("using last clock based on initial_measurements_limit: ", ts_str($lastclock));
			}

			if (opt('debug'))
			{
				dbg("[", $lastvalues_db_tld->{$service}{'probes'}{$probe}{$itemid}{'key'}, "] last ",
					($lastclock ? ts_str($lastclock) :'NULL'), ", db ", ts_str($lastclock_db),
					($probe ? $probe : ''));
			}

			add_cycles(
				$tld,
				$service,
				$probe,
				$itemid,
				$lastclock,
				$lastclock_db,
				$delay,
				$max_period,
				\%cycles,
				$lastvalues_cache_tld,
				$lastvalues_db_tld
			);
		}
	}

	# ensure numeric sort of timestamps
	@{$cycles_ref} = sort { $a <=> $b } (keys(%cycles));

	return SUCCESS;
}

# gets the history of item for a given period
sub get_history_by_itemid($$$)
{
	my $itemid = shift;
	my $timestamp_from = shift;
	my $timestamp_till = shift;

	# we need previous value to have at the time of @timestamp_from
	my $rows_ref = db_select("select delay from items where itemid=$itemid");

	$timestamp_from -= $rows_ref->[0]->[0];

	return db_select(
			"select clock,value" .
			" from history_uint" .
			" where itemid=$itemid" .
				" and " . sql_time_condition($timestamp_from, $timestamp_till) .
			" order by clock"
	);
}

# gets the value of item at a given timestamp
sub get_historical_value_by_time($$)
{
	my $history = shift;
	my $timestamp = shift;

	fail("internal error: missing 2nd argument to get_historical_value_by_time()") unless ($timestamp);

	my $value_timestamp = cycle_start($timestamp, 60);

	# TODO implement binary search

	my ($value, $last_value);

	foreach my $row (@{$history})
	{
		$last_value = $row->[1];

		# stop iterating if history clock overshot the timestamp
		last if ($value_timestamp < cycle_start($row->[0], 60));
	}
	continue
	{
		$value = $row->[1];	# keep the value preceeding overshooting
	}

	$value = $last_value unless (defined($value));

	fail("there are no values in RTT LIMIT history") unless (defined($value));

	return $value;
}

sub get_service_from_probe_key($)
{
	my $key = shift;

	# remove possible "rsm."
	$key = substr($key, length("rsm.")) if (substr($key, 0, length("rsm.")) eq "rsm.");

	my $service;

	if (substr($key, 0, length("dns")) eq "dns")
	{
		$service = "dns";
	}
	elsif (substr($key, 0, length("rdds")) eq "rdds")
	{
		$service = "rdds";
	}
	elsif (substr($key, 0, length("rdap")) eq "rdap")
	{
		$service = "rdds";
	}

	return $service;
}

sub get_service_from_slv_key($)
{
	my $key = shift;

	# remove possible "rsm.slv."
	$key = substr($key, length("rsm.slv.")) if (substr($key, 0, length("rsm.slv.")) eq "rsm.slv.");

	my $service;

	if (substr($key, 0, length("dns.")) eq "dns.")
	{
		$service = "dns";
	}
	elsif (substr($key, 0, length("dnssec.")) eq "dnssec.")
	{
		$service = "dns";
	}
	elsif (substr($key, 0, length("rdds.")) eq "rdds.")
	{
		$service = "rdds";
	}
	else
	{
		fail("cannot extract service from item \"$key\"");
	}

	return $service;
}

sub get_lastvalues_from_db($$$)
{
	my $lastvalues_db = shift;
	my $tld = shift;
	my $delays = shift;

	my $host_cond = " and (" .
				"(hg.groupid=" . TLDS_GROUPID . " and h.host='$tld') or" .
				" (hg.groupid=" . TLD_PROBE_RESULTS_GROUPID . " and h.host like '$tld %')" .
			")";

	my $item_num_rows_ref = db_select(
		"select h.host,hg.groupid,i.itemid,i.key_,i.value_type".
		" from items i,hosts h,hosts_groups hg".
		" where h.hostid=i.hostid".
			" and hg.hostid=h.hostid".
			" and i.status=".ITEM_STATUS_ACTIVE.
			" and (i.value_type=".ITEM_VALUE_TYPE_FLOAT.
			" or i.value_type=".ITEM_VALUE_TYPE_UINT64.")".
			$host_cond
	);

	my $itemids_num = '';

	foreach my $row_ref (@{$item_num_rows_ref})
	{
		$itemids_num .= $row_ref->[2];
		$itemids_num .= ',';
	}

	chop($itemids_num);

	my $item_str_rows_ref = db_select(
		"select h.host,hg.groupid,i.itemid,i.key_,i.value_type".
		" from items i,hosts h,hosts_groups hg".
		" where h.hostid=i.hostid".
			" and hg.hostid=h.hostid".
			" and i.status=".ITEM_STATUS_ACTIVE.
			" and i.value_type=".ITEM_VALUE_TYPE_STR.
			$host_cond
	);

	my $itemids_str = '';

	foreach my $row_ref (@{$item_str_rows_ref})
	{
		$itemids_str .= $row_ref->[2];
		$itemids_str .= ',';
	}

	chop($itemids_str);

	# get everything from lastvalue, lastvalue_str tables
	my $lastval_rows_ref = db_select(
		"select itemid,clock".
		" from lastvalue".
		" where itemid in ($itemids_num)".
		" union".
		" select itemid,clock".
		" from lastvalue_str".
		" where itemid in ($itemids_str)"
	);

	my %lastvalues_map = map { $_->[0] => $_->[1] } @{$lastval_rows_ref};

	undef($lastval_rows_ref);

	# join items and lastvalues

	my @item_rows_ref = (@{$item_num_rows_ref}, @{$item_str_rows_ref});
	undef($item_num_rows_ref);
	undef($item_str_rows_ref);

	foreach my $row_ref (@item_rows_ref)
	{
		my ($host, $hostgroupid, $itemid, $key, $value_type) = @{$row_ref};

		next unless(defined($lastvalues_map{$itemid}));

		my $clock = $lastvalues_map{$itemid};

		my ($probe, $key_service);

		if ($hostgroupid == TLDS_GROUPID)
		{
			# this item belongs to TLD (we only care about Service availability (*.avail) items)
			next unless (substr($key, -5) eq "avail");

			$key_service = get_service_from_slv_key($key);
		}
		elsif ($hostgroupid == TLD_PROBE_RESULTS_GROUPID)
		{
			# this item belongs to Probe (we do not care about DNS TCP)
			next if (substr($key, 0, length("rsm.dns.tcp")) eq "rsm.dns.tcp");

			next if (substr($key, 0, length("rsm.conf")) eq "rsm.conf");
			next if (substr($key, 0, length("rsm.probe")) eq "rsm.probe");

			next unless (substr($key, 0, length("rsm.")) eq "rsm." ||
				substr($key, 0, length("rdap[")) eq "rdap[" ||
				substr($key, 0, length("rdap.ip")) eq "rdap.ip" ||
				substr($key, 0, length("rdap.rtt")) eq "rdap.rtt");

			(undef, $probe) = split(" ", $host, 2);

			$key_service = get_service_from_probe_key($key);
		}
		else
		{
			fail("unexpected host group id \"$hostgroupid\"");
		}

		fail("cannot identify Service of key \"$key\"") unless ($key_service);

		foreach my $service ($key_service eq 'dns' ? ('dns', 'dnssec') : ($key_service))
		{
			# empty probe is for *.avail (<TLD>) items
			$lastvalues_db->{'tlds'}{$tld}{$service}{'probes'}{$probe // ""}{$itemid} = {
				'key' => $key,
				'value_type' => $value_type,
				'clock' => $clock
			};
		}
	}
}

sub fill_test_data($$$$)
{
	my $service = shift;
	my $src = shift;
	my $dst = shift;
	my $hist = shift;

	foreach my $target (keys(%{$src}))
	{
		my $test_data_ref = {
			'target'	=> ($target eq TARGET_PLACEHOLDER ? undef : $target),
			'status'	=> undef,
			'metrics'	=> []
		};

		foreach my $src_metric_ref (@{$src->{$target}})
		{
			# "rtt" and "ip" values represent the same test but they are stored in different items with
			# different value types. This means they can appear in history tables at different times.
			# In this case we must skip that test with partial metrics, it will be added on the next run
			# when we have all the needed data.
			#
			# Exception! In case of negative RTT the IP is sometimes undefined. We assume that this is the
			# case and allow empty IPs.
			#

			next if (!defined($src_metric_ref->{'rtt'}) || ($src_metric_ref->{'rtt'} >= 0 && !defined($src_metric_ref->{'ip'})));

			my $metric = {
				'testDateTime'	=> int($src_metric_ref->{'clock'}),
				'targetIP'	=> $src_metric_ref->{'ip'}
			};

			my $rtt = $src_metric_ref->{'rtt'};

			if (!defined($rtt))
			{
				$metric->{'rtt'} = undef;
				$metric->{'result'} = 'no data';
			}
			elsif (is_internal_error_desc($rtt))
			{
				$metric->{'rtt'} = undef;
				$metric->{'result'} = $rtt;

				# don't override NS status with "Up" if NS is already known to be down
				if (!defined($test_data_ref->{'status'}) || $test_data_ref->{'status'} ne "Down")
				{
					$test_data_ref->{'status'} = "Up";
				}
			}
			elsif (is_service_error_desc($service, $rtt))
			{
				$metric->{'rtt'} = undef;
				$metric->{'result'} = $rtt;

				$test_data_ref->{'status'} = "Down";
			}
			else
			{
				if ($service eq 'dnssec' && $rtt < 0)
				{
					# DNSSEC is exceptional as it may be successful in case of negative RTT.
					# This can happen if the DNS error code is not related to DNSSEC.
					$metric->{'rtt'} = undef;
					$metric->{'result'} = $rtt;
				}
				else
				{
					$metric->{'rtt'} = $rtt;
					$metric->{'result'} = "ok";
				}

				# skip threshold check if NS is already known to be down
				if ($hist)
				{
					if  (!defined($test_data_ref->{'status'}) || $test_data_ref->{'status'} eq "Up")
					{
						$test_data_ref->{'status'} = (
							$rtt > get_historical_value_by_time(
								$hist,
								$metric->{'testDateTime'}
							) ? "Down" : "Up"
						);
					}
				}
				else
				{
					$test_data_ref->{'status'} = "Up";
				}
			}

			push(@{$test_data_ref->{'metrics'}}, $metric);
		}

		$test_data_ref->{'status'} //= AH_CITY_NO_RESULT;

		push(@{$dst}, $test_data_ref);
	}
}

#
# Probe status value cache. itemid - PROBE_KEY_ONLINE item
#
# {
#     probe => {
#         'itemid' => 1234,
#         'values' => {
#             'clock' => value,
#             ...
#         }
#     }
# }
#
my %probe_statuses;
sub probe_online_at_init()
{
	%probe_statuses = ();
}

sub probe_online_at($$)
{
	my $probe = shift;
	my $clock = shift;

	if (!defined($probe_statuses{$probe}{'itemid'}))
	{
		my $host = "$probe - mon";

		my $rows_ref = db_select(
			"select i.itemid,i.key_,h.host".
			" from items i,hosts h".
			" where i.hostid=h.hostid".
				" and h.host='$host'".
				" and i.key_='".PROBE_KEY_ONLINE."'"
		);

		fail("internal error: no \"$host\" item " . PROBE_KEY_ONLINE) unless (defined($rows_ref->[0]));

		$probe_statuses{$probe}{'itemid'} = $rows_ref->[0]->[0];
	}

	if (!defined($probe_statuses{$probe}{'values'}{$clock}))
	{
		my $rows_ref = db_select(
			"select value".
			" from " . history_table(ITEM_VALUE_TYPE_UINT64).
			" where itemid=" . $probe_statuses{$probe}{'itemid'}.
				" and clock=".$clock
		);

		# Online if no value in the database
		$probe_statuses{$probe}{'values'}{$clock} = (defined($rows_ref->[0]) ? $rows_ref->[0]->[0] : 1);
	}

	return $probe_statuses{$probe}{'values'}{$clock};
}

sub calculate_cycle($$$$$$$$$)
{
	$tld = shift;		# set globally
	my $service = shift;
	my $probes_data = shift;
	my $cycle_clock = shift;
	my $delay = shift;
	my $rtt_limit = shift;
	my $service_probes_ref = shift;	# probes enabled for the service ('name' => {'hostid' => hostid, 'status' => status}) available for this service
	my $all_probes_ref = shift;	# same structure but all available probes in the system, for listing in JSON files
	my $interfaces_ref = shift;

	my $from = cycle_start($cycle_clock, $delay);
	my $till = cycle_end($cycle_clock, $delay);

	my $json = {'tld' => $tld, 'service' => $service, 'cycleCalculationDateTime' => $from};

	my %tested_interfaces;

	my $probes_with_results = 0;
	my $probes_with_positive = 0;
	my $probes_online = 0;

	foreach my $probe (keys(%{$probes_data}))
	{
		my (@itemids_uint, @itemids_float, @itemids_str);

		#
		# collect itemids, separate them by value_type to fetch values from according history table later
		#

		map {
			my $i = $probes_data->{$probe}{$_};

			if ($i->{'value_type'} == ITEM_VALUE_TYPE_UINT64)
			{
				push(@itemids_uint, $_);
			}
			elsif ($i->{'value_type'} == ITEM_VALUE_TYPE_FLOAT)
			{
				push(@itemids_float, $_);
			}
			elsif ($i->{'value_type'} == ITEM_VALUE_TYPE_STR)
			{
				push(@itemids_str, $_);
			}
		} (keys(%{$probes_data->{$probe}}));

		next if (@itemids_uint == 0);

		#
		# Fetch availability (Integer) values (on a TLD level and Probe level):
		#
		# TLD level example	: rsm.slv.dns.avail
		# Probe level example	: rsm.dns.udp
		#

		my $rows_ref = db_select(
			"select itemid,value".
			" from " . history_table(ITEM_VALUE_TYPE_UINT64).
			" where itemid in (" . join(',', @itemids_uint) . ")".
				" and " . sql_time_condition($from, $till)
		);

		# {
		#     ITEMID => value (int),
		# }
		my %values;

		map {push(@{$values{$_->[0]}}, int($_->[1]))} (@{$rows_ref});

		# skip cycles that do not have test result
		next if (scalar(keys(%values)) == 0);

		my $service_up = 1;

		foreach my $itemid (keys(%values))
		{
			my $key = $probes_data->{$probe}{$itemid}{'key'};

			dbg("trying to identify interfaces of $service key \"$key\"...");

			if (substr($key, 0, length("rsm.rdds")) eq "rsm.rdds")
			{
				#
				# RDDS Availability on the Probe level. This item contains Availability of interfaces:
				#
				# - RDDS43
				# - RDDS80
				#

				foreach my $value (@{$values{$itemid}})
				{
					$service_up = 0 unless ($value == RDDS_UP);

					my $interface = AH_INTERFACE_RDDS43;

					if (!defined($tested_interfaces{$interface}{$probe}{'status'}) ||
						$tested_interfaces{$interface}{$probe}{'status'} eq AH_CITY_DOWN)
					{
						$tested_interfaces{$interface}{$probe}{'status'} =
							($value == RDDS_UP || $value == RDDS_43_ONLY ? AH_CITY_UP : AH_CITY_DOWN);
					}

					$interface = AH_INTERFACE_RDDS80;

					if (!defined($tested_interfaces{$interface}{$probe}{'status'}) ||
						$tested_interfaces{$interface}{$probe}{'status'} eq AH_CITY_DOWN)
					{
						$tested_interfaces{$interface}{$probe}{'status'} =
							($value == RDDS_UP || $value == RDDS_80_ONLY ? AH_CITY_UP : AH_CITY_DOWN);
					}
				}
			}
			elsif (substr($key, 0, length("rdap")) eq "rdap")
			{
				#
				# RDAP Availability on the Probe level. This item contains Availability of interfaces:
				#
				# - RDAP
				#

				my $interface = AH_INTERFACE_RDAP;

				my $city_status;

				foreach my $value (@{$values{$itemid}})
				{
					last if (defined($city_status) && $city_status eq AH_CITY_UP);

					$city_status = ($value == UP ? AH_CITY_UP : AH_CITY_DOWN);
				}

				$service_up = 0 if ($city_status eq AH_CITY_DOWN);

				$tested_interfaces{$interface}{$probe}{'status'} = $city_status;

			}
			elsif (substr($key, 0, length("rsm.dns.udp")) eq "rsm.dns.udp")
			{
				#
				# DNS Availability on the Probe level. This item contains Availability of interfaces:
				#
				# - DNS
				# - DNSSEC
				#

				my $interface;

				if ($service eq 'dnssec')
				{
					$interface = AH_INTERFACE_DNSSEC;
				}
				else
				{
					$interface = AH_INTERFACE_DNS;
				}

				my $city_status;

				foreach my $value (@{$values{$itemid}})
				{
					last if (defined($city_status) && $city_status eq AH_CITY_UP);

					$city_status = ($value >= $cfg_minns ? AH_CITY_UP : AH_CITY_DOWN);
				}

				$service_up = 0 if ($city_status eq AH_CITY_DOWN);

				$tested_interfaces{$interface}{$probe}{'status'} = $city_status;
			}
			elsif (substr($key, 0, length("rsm.slv.")) eq "rsm.slv.")
			{
				#
				# Service Availability on a TLD level.
				#

				my $sub_key = substr($key, length("rsm.slv."));

				my $index = index($sub_key, '.');	# <SERVICE>.avail

				fail("cannot extract Service from item \"$key\"") if ($index == -1);

				my $key_service = substr($sub_key, 0, $index);

				next unless ($key_service eq $service);

				fail("$service status is re-defined (status=$json->{'status'})") if (defined($json->{'status'}));

				if (scalar(@{$values{$itemid}}) != 1)
				{
					my $msg = "item \"$key\" contains more than 1 value ".
						selected_period($from, $till) . ": " . join(',', @{$values{$itemid}}) . "\n";

					my $sql =
						"select hi.itemid,hi.clock,hi.value,hi.ns".
						" from history_uint hi,items i,hosts h".
						" where hi.itemid=i.itemid".
							" and i.hostid=h.hostid".
							" and i.key_='$key'".
							" and h.host='$tld'".
							" and " . sql_time_condition($from, $till);

					my $rows_ref = db_select($sql);

					$msg .= "SQL: $sql\n";
					$msg .= "---------------------\n";
					$msg .= "itemid,clock,value,ns\n";
					$msg .= "---------------------\n";

					foreach (@{$rows_ref})
					{
						$msg .= join(',', @{$_}) . "\n";
					}

					chomp($msg);

					fail($msg);
				}

				if ($values{$itemid}->[0] == UP)
				{
					$json->{'status'} = 'Up';
				}
				elsif ($values{$itemid}->[0] == DOWN)
				{
					$json->{'status'} = 'Down';
				}
				elsif ($values{$itemid}->[0] == UP_INCONCLUSIVE_NO_DATA)
				{
					$json->{'status'} = 'Up-inconclusive-no-data';
				}
				elsif ($values{$itemid}->[0] == UP_INCONCLUSIVE_NO_PROBES)
				{
					$json->{'status'} = 'Up-inconclusive-no-probes';
				}
				else
				{
					fail("item \"$key\" ($itemid) contains unexpected value \"", $values{$itemid}->[0] , "\"");
				}
			}
			else
			{
				fail("unexpected key \"$key\" when trying to identify Service interface");
			}
		}

		if ($service_up)
		{
			$probes_with_positive++;
		}

		$probes_with_results++;

		next if (@itemids_float == 0);

		#
		# Fetch RTT (Float) values (on Probe level).
		#
		# Note, for DNS service we will also collect target (Name Server) and IP
		# because these are provided in RTT items, e. g.:
		#
		# rsm.dns.udp.rtt["ns1.example.com",1.2.3.4]
		#
		# For other services the IPs are located in separate items which we collect on the next run.
		#

		$rows_ref = db_select(
			"select itemid,value,clock".
			" from " . history_table(ITEM_VALUE_TYPE_FLOAT).
			" where itemid in (" . join(',', @itemids_float) . ")".
				" and " . sql_time_condition($from, $till)
		);

		# for convenience convert the data to format:
		#
		# {
		#     ITEMID => [
		#         {
		#             'value' => value (float: RTT),
		#             'clock' => clock
		#         }
		#     ]
		# }
		%values = ();

		map {push(@{$values{$_->[0]}}, {'value' => int($_->[1]), 'clock' => $_->[2]})} (@{$rows_ref});

		foreach my $itemid (keys(%values))
		{
			my $i = $probes_data->{$probe}{$itemid};

			foreach my $value_ref (@{$values{$itemid}})
			{
				my $interface;

				if ($service eq 'dnssec')
				{
					$interface = AH_INTERFACE_DNSSEC;
				}
				else
				{
					$interface = ah_get_interface($i->{'key'});
				}

				my ($target, $ip);
				if (substr($i->{'key'}, 0, length("rsm.dns.udp.rtt")) eq "rsm.dns.udp.rtt")
				{
					($target, $ip) = split(',', get_nsip_from_key($i->{'key'}));
				}
				else
				{
					# for non-DNS service "target" is NULL, but we
					# can't use it as hash key so we use placeholder
					$target = TARGET_PLACEHOLDER;
				}

				dbg("found $service RTT: ", $value_ref->{'value'}, " IP: ", ($ip // 'UNDEF'), " (target: $target)");

				push(@{$tested_interfaces{$interface}{$probe}{'testData'}{$target}}, {
						'clock' => $value_ref->{'clock'},
						'rtt' => $value_ref->{'value'},
						'ip' => $ip
					}
				);
			}
		}

		next if (@itemids_str == 0);

		#
		# Fetch IP (String) values (on Probe level) for non-DNS tests.
		#
		# Note, this is because only for non-DNS services there are special items for IP, e. g.:
		#
		# rsm.rdds.43.ip
		#
		# Note, targets (Name Servers) are unused in non-DNS services.
		#

		$rows_ref = db_select(
			"select itemid,value,clock".
			" from " . history_table(ITEM_VALUE_TYPE_STR).
			" where itemid in (" . join(',', @itemids_str) . ")".
				" and " . sql_time_condition($from, $till)
		);

		# for convenience convert the data to format:
		#
		# {
		#     ITEMID => [
		#         {
		#             'value' => value (string: IP),
		#             'clock' => clock
		#         }
		#     ]
		# }
		#
		# Note, we only have non-DNS items here, for DNS we have collected everything above.
		%values = ();

		map {push(@{$values{$_->[0]}}, {'value' => $_->[1], 'clock' => $_->[2]})} (@{$rows_ref});

		foreach my $itemid (keys(%values))
		{
			my $i = $probes_data->{$probe}{$itemid};

			foreach my $value_ref (@{$values{$itemid}})
			{
				my $interface = ah_get_interface($i->{'key'});

				# for non-DNS service "target" is NULL, but we
				# can't use it as hash key so we use placeholder
				my $target = TARGET_PLACEHOLDER;

				dbg("found $service IP: ", $value_ref->{'value'}, " (target: $target)");

				# For non-DNS we have only 1 metric, thus we refer to the first element of array.
				# "clock" and "rtt" of this metric were already collected above.

				$tested_interfaces{$interface}{$probe}{'testData'}{$target}->[0]->{'ip'} = $value_ref->{'value'};
			}
		}
	}

	# add "Offline" and "No results"
	foreach my $probe (keys(%{$all_probes_ref}))
	{
		my $probe_online;

		if (defined($service_probes_ref->{$probe}) &&
				$service_probes_ref->{$probe}->{'status'} == HOST_STATUS_MONITORED)
		{
			$probe_online = probe_online_at($probe, $from);
		}
		else
		{
			$probe_online = 0;
		}

		foreach my $interface (@{$interfaces_ref})
		{
			if (!$probe_online)
			{
				$tested_interfaces{$interface}{$probe}{'status'} = AH_CITY_OFFLINE;

				# We detected that probe was offline but there might be still results.
				# It is requested to ignore those in both here and in Frontend.
				undef($tested_interfaces{$interface}{$probe}{'testData'});
			}
			elsif (!defined($tested_interfaces{$interface}{$probe}{'status'}))
			{
				$tested_interfaces{$interface}{$probe}{'status'} = AH_CITY_NO_RESULT;
			}
		}

		$probes_online++ if ($probe_online);
	}

	#
	# add data that was collected from history and calculated in previous cycle to JSON
	#

	foreach my $interface (keys(%tested_interfaces))
	{
		my $interface_json = {
			'interface'	=> $interface,
			'probes'	=> []
		};

		foreach my $probe (keys(%{$tested_interfaces{$interface}}))
		{
			my $probe_ref = {
				'city'		=> $probe,
				'status'	=> $tested_interfaces{$interface}{$probe}{'status'},	# Probe status
				'testData'	=> []
			};

			fill_test_data(
				$service,
				$tested_interfaces{$interface}{$probe}{'testData'},
				$probe_ref->{'testData'},
				$rtt_limit
			);

			push(@{$interface_json->{'probes'}}, $probe_ref);
		}

		push(@{$json->{'testedInterface'}}, $interface_json);
	}

	my $perc;
	if ($probes_with_results == 0)
	{
		$perc = 0;
	}
	else
	{
		$perc = $probes_with_positive * 100 / $probes_with_results;
	}

	my $detailed_info;

	if (defined($json->{'status'}))
	{
		$detailed_info = "taken from Service Availability";
	}
	else
	{
		$detailed_info = sprintf("%d/%d positive, %.3f%%, %d online", $probes_with_positive, $probes_with_results, $perc, $probes_online);

		if ($probes_online < $cfg_minonline)
		{
			$json->{'status'} = 'Up-inconclusive-no-probes';
		}
		elsif ($probes_with_results < $cfg_minonline)
		{
			$json->{'status'} = 'Up-inconclusive-no-data';
		}
		elsif ($perc > SLV_UNAVAILABILITY_LIMIT)
		{
			$json->{'status'} = 'Up';
		}
		else
		{
			$json->{'status'} = 'Down';
		}
	}

	dbg("cycle: $json->{'status'} ($detailed_info)");

	if (opt('debug2'))
	{
		print(Dumper($json));
	}

	return if (opt('dry-run'));

	if (ah_save_recent_measurement(ah_get_api_tld($tld), $service, $json, $from) != AH_SUCCESS)
	{
		fail("cannot save recent measurement: ", ah_get_error());
	}
}

sub get_interfaces($$$)
{
	my $tld = shift;
	my $service = shift;
	my $now = shift;

	my @result;

	if ($service eq 'dns')
	{
		push(@result, AH_INTERFACE_DNS);
	}
	elsif ($service eq 'dnssec')
	{
		push(@result, AH_INTERFACE_DNSSEC);
	}
	elsif ($service eq 'rdds')
	{
		push(@result, AH_INTERFACE_RDDS43) if (tld_interface_enabled($tld, 'rdds43', $now));
		push(@result, AH_INTERFACE_RDDS80) if (tld_interface_enabled($tld, 'rdds80', $now));
		push(@result, AH_INTERFACE_RDAP) if (tld_interface_enabled($tld, 'rdap', $now));
	}

	return \@result;
}

sub set_alarm($)
{
	my $max_wait = shift;

	$SIG{"ALRM"} = sub()
	{
		wrn("received ALARM signal");

		slv_exit(E_FAIL);
	};

	alarm($max_wait);
}

sub terminate_children($)
{
	my $fm = shift;

	$fm->run_on_wait(
		sub ()
		{
			# This callback ensures that before waiting for the next child to terminate we check the $child_failed
			# flag and send terminate all running children if needed. After sending SIGTERM we raise $signal_sent
			# flag to make sure that we don't do it multiple times.

			return unless ($child_failed);
			return if ($signal_sent);

			info("one of the child processes failed, terminating others...");

			$SIG{'TERM'} = 'IGNORE';	# ignore signal we will send to ourselves in the next step
			kill('TERM', 0);		# send signal to the entire process group
			$SIG{'TERM'} = 'DEFAULT';	# restore default signal handler

			$signal_sent = 1;
		}
	);

	$fm->wait_all_children();
}

sub child_error($$$$$)
{
	my $pid = shift;
	my $exit_code = shift;
	my $id = shift;
	my $exit_signal = shift;
	my $core_dump = shift;

	if ($core_dump == 1)
	{
		wrn("$child_desc{$pid}->{'desc'} (PID:$pid) core dumped");
		return 1;
	}
	elsif ($exit_code != SUCCESS)
	{
		wrn("$child_desc{$pid}->{'desc'} (PID:$pid)",
			($exit_signal == 0 ? "" : " got signal " . sig_name($exit_signal) . " and"),
			" exited with code $exit_code");
		return 1;
	}
	elsif ($exit_signal != 0)
	{
		wrn("$child_desc{$pid}->{'desc'} (PID:$pid) got signal " . sig_name($exit_signal));
		return 1;
	}

	return 0;
}

sub update_lastvalues_cache($)
{
	my $child_data_ref = shift;

	foreach my $tld (keys(%{$child_data_ref}))
	{
		$lastvalues_cache->{'tlds'}{$tld} = $child_data_ref->{$tld};
	}
}

sub set_on_finish($)
{
	my $fm = shift;

	$fm->run_on_finish(
		sub ($$$$$$)
		{
			my $pid = shift;
			my $exit_code = shift;
			my $id = shift;
			my $exit_signal = shift;
			my $core_dump = shift;
			my $child_data = shift;

			if (child_error($pid, $exit_code, $id, $exit_signal, $core_dump))
			{
				wrn("$child_desc{$pid}->{'desc'} (PID:$pid) failed,",
					" run time: ", time() - $child_desc{$pid}->{'from'}, " seconds");

				$child_failed = 1;

				terminate_children($fm);

				slv_exit(E_FAIL);
			}
			else
			{
				dbg("$child_desc{$pid}->{'desc'} (PID:$pid) exited successfully,",
					" run time: ", time() - $child_desc{$pid}->{'from'}, " seconds");

				return unless (defined($child_data));

				update_lastvalues_cache($child_data);
			}
		}
	);
}

sub get_swap_usage($)
{
	my $pid = shift;

	my $status_file = "/proc/$pid/status";

	my $swap_usage;

	open(my $status, '<', $status_file) or fail("cannot open \"$status_file\": $!");

	while (<$status>)
	{
		if (/^VmSwap:\s+(\d*.*)/)
		{
			$swap_usage = $1;
			last;
		}
	}

	close($status);

	return $swap_usage;
}

__END__

=head1 NAME

sla-api-current.pl - generate recent SLA API measurement files for newly collected data

=head1 SYNOPSIS

sla-api-current.pl [--tld <tld>] [--service <name>] [--server-id <id>] [--now unixtimestamp] [--period minutes] [--print-period] [--max-children n] [--max-wait seconds] [--debug] [--dry-run] [--help]

=head1 OPTIONS

=over 8

=item B<--tld> tld

Optionally specify TLD.

=item B<--service> name

Optionally specify service, one of: dns, dnssec, rdds

=item B<--server-id> ID

Optionally specify the server ID to query the data from.

=item B<--now> unixtimestamp

Optionally specify the time of the cycle to start from. Maximum 30 cycles will be processed.

=item B<--period> minutes

Optionally specify maximum period to handle (default: 30 minutes).

=item B<--print-period>

Print selected period on the screen.

=item B<--max-children> n

Specify maximum number of child processes to run in parallel (default: 64).

=item B<--max-wait> seconds

Specify maximum number of seconds to wait for single child process to finish (default: 600). If still running
the process will be sent TERM signal causing the script to exit with non-zero exit code.

=item B<--debug>

Run the script in debug mode. This means printing more information.

=item B<--dry-run>

Print data to the screen, do not write anything to the filesystem.

=item B<--help>

Print a brief help message and exit.

=back

=head1 DESCRIPTION

B<This program> will generate the most recent measurement files for newly collected monitoring data. The files will be
available under directory /opt/zabbix/sla-v2 . Each run the script would generate new measurement files for the period
from the last run till up to 30 minutes.

=head1 EXAMPLES

/opt/zabbix/scripts/sla-api-recent.pl

Generate recent measurement files for the period from last generated till up to 30 minutes.

=cut
