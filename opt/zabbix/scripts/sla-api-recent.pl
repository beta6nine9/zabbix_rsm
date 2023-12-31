#!/usr/bin/env perl

use FindBin;
use lib $FindBin::RealBin;

use strict;
use warnings;

use Data::Dumper;
use File::Copy;
use IO::Select;
use Parallel::ForkManager;
use Socket;

use RSM;
use RSMSLV;
use TLD_constants qw(:api :config :groups :items);
use ApiHelper;
use sigtrap 'handler' => \&main_process_signal_handler, 'normal-signals';

$Data::Dumper::Terse = 1;	# do not output names like "$VAR1 = "
$Data::Dumper::Pair = ": ";	# use separator instead of " => "
$Data::Dumper::Useqq = 1;	# use double quotes instead of single quotes
$Data::Dumper::Indent = 1;	# 1 provides less indentation instead of 2

use constant MAX_PERIOD => 30 * 60;	# 30 minutes, do not handle longer periods in 1 run

use constant SUBSTR_KEY_LEN => 20;	# for logging

use constant DEFAULT_MAX_CHILDREN => 64;
use constant DEFAULT_MAX_WAIT => 600;	# maximum seconds to wait befor terminating child process

use constant DEFAULT_INITIAL_MEASUREMENTS_LIMIT => 7200;	# seconds, if the metric is not in cache and
								# no measurements within this period, start generating
								# them from this period in the past back for recent
								# measurement files for an incident

use constant RSMHOST => "";	# for Service Availability items, this is only needed for cache

my %service_status_to_str = (
	&UP                        => 'Up',
	&DOWN                      => 'Down',
	&UP_INCONCLUSIVE_NO_DATA   => 'Up-inconclusive-no-data',
	&UP_INCONCLUSIVE_NO_PROBES => 'Up-inconclusive-no-probes',
	&UP_INCONCLUSIVE_RECONFIG  => 'Up-inconclusive-reconfig',
);

sub main_process_signal_handler();
sub process_server($);
sub process_tld_batch($$$$);
sub process_tld($$$$$$);
sub cycles_to_calculate($$$$$$$$);
sub get_lastvalues_from_db($$$$);
sub calculate_cycle($$$$$$$$$$);
sub translate_interfaces($);
sub get_interfaces($$$);
sub get_history_by_itemid($$$);
sub child_error($$$$$);
sub update_lastvalues_cache($);
sub set_on_finish($);
sub terminate_children($);

parse_opts('tld=s', 'service=s', 'server-id=i', 'now=i', 'period=i', 'print-period', 'max-children=i', 'max-wait=i', 'prettify-json', 'debug2');

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

my $rdap_is_standalone = is_rdap_standalone($now);
dbg("RDAP ", ($rdap_is_standalone ? "is" : "is NOT"), " standalone");

my $cfg_minonline = get_macro_dns_probe_online();

my %delays;
$delays{'dns'} = $delays{'dnssec'} = get_dns_delay();
$delays{'rdds'} = get_rdds_delay();
$delays{'rdap'} = get_rdap_delay() if ($rdap_is_standalone);

my %clock_limits;

$clock_limits{'dns'} = $clock_limits{'dnssec'} = cycle_start($now - $initial_measurements_limit, $delays{'dnssec'});
$clock_limits{'rdds'} = cycle_start($now - $initial_measurements_limit, $delays{'rdds'});
$clock_limits{'rdap'} = cycle_start($now - $initial_measurements_limit, $delays{'rdap'}) if ($rdap_is_standalone);

db_disconnect();

my %service_keys = (
	'dns'    => 'rsm.slv.dns.avail',
	'dnssec' => 'rsm.slv.dnssec.avail',
	'rdds'   => 'rsm.slv.rdds.avail',
	'rdap'   => 'rsm.slv.rdap.avail',
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

info("servers         : $server_count") if (opt('stats'));
info("children/server : $children_per_server") if (opt('stats'));

my $child_failed = 0;
my $signal_sent = 0;
my $lastvalues_cache = {};

my $fm = new Parallel::ForkManager($server_count);
set_on_finish($fm);

# {
#     PID => {'desc' => 'server_#_parent', 'from' => 1234324235},
#     ...
#     PID => {'desc' => 'server_#_child', 'from' => 1234324235},
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
	};

	dbg("$child_desc{$pid}->{'desc'} (PID:$pid) STARTED");
}

$fm->wait_all_children();

slv_exit($child_failed ? E_FAIL : SUCCESS);

sub main_process_signal_handler()
{
	wrn("main process caught a signal: $!");
	slv_exit(E_FAIL);
}

sub get_tld_from_socket($)
{
	my $select = shift;

	my @ready;
	my $socket;

	@ready = $select->can_write();
	$socket = $ready[0];

	print $socket "\n";

	@ready = $select->can_read();
	$socket = $ready[0];

	my $tld = <$socket>;

	if (defined($tld))
	{
		chomp($tld);
	}

	return $tld;
}

sub put_tld_into_socket($$)
{
	my $select = shift;
	my $tld    = shift;

	my @ready = $select->can_read();
	my $socket = $ready[0];

	<$socket>;

	print $socket "$tld\n";
}

sub process_server($)
{
	my $server_key = shift;

	$lastvalues_cache = {};

	if (ah_read_recent_cache($server_key, \$lastvalues_cache) != AH_SUCCESS)
	{
		dbg("there's no recent measurements cache file yet, but no worries");
		$lastvalues_cache->{'tlds'} = {};
	}

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

	db_disconnect();

	return unless (scalar(@{$server_tlds}));

	my $server_tld_count = scalar(@{$server_tlds});

	my $children_count = ($server_tld_count < $children_per_server ? $server_tld_count : $children_per_server);

	my $fm = new Parallel::ForkManager($children_count);
	set_on_finish($fm);

	%child_desc = ();

	my @sockets;

	for (my $i = 0; $i < $children_count; $i++)
	{
		socketpair(my $child, my $parent, AF_UNIX, SOCK_STREAM, PF_UNSPEC) or fail("socketpair() failed: $!");
		$child->autoflush(1);
		$parent->autoflush(1);

		my %child_data;

		my $pid = $fm->start();

		if ($pid == 0)
		{
			close($parent);

			set_alarm($max_wait);

			init_process();

			process_tld_batch($child, \%child_data, $all_probes_ref, $server_key);

			finalize_process();

			close($child);

			$fm->finish(SUCCESS, \%child_data);
		}

		close($child);
		push(@sockets, $parent);

		$child_desc{$pid} = {
			'desc' => "${server_key}_child",
			'from' => time(),
		};

		dbg("$child_desc{$pid}->{'desc'} (PID:$pid) STARTED");
	}

	my $select = IO::Select->new(@sockets);

	foreach my $tld (sort(@{$server_tlds}))
	{
		put_tld_into_socket($select, $tld);
	}

	while ($select->count())
	{
		my @ready = $select->can_read();

		foreach my $socket (@ready)
		{
			$select->remove($socket);
			<$socket>;
			close($socket);
		}
	}

	$fm->wait_all_children();

	# Do not update cache if something happened.
	if (opt('dry-run') || opt('now'))
	{
		info("not updating sla-api cache because running with either --dry-run or --now");
	}
	else
	{
		if (ah_save_recent_cache($server_key, $lastvalues_cache) != AH_SUCCESS)
		{
			fail("cannot save recent measurements cache: ", ah_get_error());
		}
	}
}

sub process_tld_batch($$$$)
{
	my $socket         = shift;
	my $child_data_ref = shift;
	my $all_probes_ref = shift;	# all available probes in the system
	my $server_key     = shift;

	my $probes_ref;	# probes by services

	db_connect($server_key);

	my $select = IO::Select->new($socket);

	while (1)
	{
		$tld = get_tld_from_socket($select); # global variable

		if (!defined($tld))
		{
			last;
		}

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
		# NB! Besides results from probes we cache Service Availability
		# values, which are stored on a <RSMHOST> host.

		my $lastvalues_db    = {'tlds' => {}};
		my $lastvalues_nsids = {'tlds' => {}};

		get_lastvalues_from_db($lastvalues_db, $lastvalues_nsids, $tld, \%delays);

		$lastvalues_cache->{'tlds'}{$tld} //= {};

		process_tld(
			$tld,
			$probes_ref,
			$all_probes_ref,
			$lastvalues_db->{'tlds'}{$tld},
			$lastvalues_nsids->{'tlds'}{$tld},
			$lastvalues_cache->{'tlds'}{$tld}
		);

		$child_data_ref->{$tld} = $lastvalues_cache->{'tlds'}{$tld};

		$tld = undef;	# unset global variable
	}

	db_disconnect();
}

my $nsids_cache;

sub process_tld($$$$$$)
{
	my $tld = shift;
	my $probes_ref = shift;
	my $all_probes_ref = shift;
	my $lastvalues_db_tld = shift;
	my $lastvalues_nsids_tld = shift;
	my $lastvalues_cache_tld = shift;

	$nsids_cache = {};

	# ensure 'dnssec' comes after 'dns' by using sort(), because
	# 'dnssec' will partly use 'dns' results
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

		if (scalar(@cycles_to_calculate) == 0)
		{

			info(sprintf("selected %6s period: -", $service)) if (opt('print-period'));
			next;
		}

		my $cycles_from = $cycles_to_calculate[0];
		my $cycles_till = cycle_end($cycles_to_calculate[-1], $delays{$service});

		next unless (tld_service_enabled($tld, $service, $cycles_from));

		if (opt('print-period'))
		{
			info(sprintf("selected %6s period: %s", $service, selected_period($cycles_from, $cycles_till)));
		}

		# TODO: leave only next line after migrating to Standalone RDAP
		# my $interfaces_ref = get_interfaces($tld, $service, $now);
		my $interfaces_ref;
		my $interfaces_ref_rdap_before_switch;
		my $interfaces_ref_rdap_after_switch;

		if ($service ne 'rdds')
		{
			$interfaces_ref = get_interfaces($tld, $service, $now);
		}

		$probes_ref->{$service} = get_probes($service) unless (defined($probes_ref->{$service}));

		# TODO: RTT limits are currently stored per Server, not per TLD, so we
		#       shouldn't be collecting them for the periods we aready handled.

		if ($service eq 'dns')
		{
			# rtt limits only considered for DNS currently
			$rtt_limits{$service} = get_history_by_itemid(
				CONFIGVALUE_DNS_UDP_RTT_HIGH_ITEMID,
				$cycles_from,
				$cycles_till
			);

			wrn("no DNS RTT limits in history for selected period")
					unless (scalar(@{$rtt_limits{$service}}));
		}

		# these are cycles we are going to recalculate for this tld-service
		foreach my $clock (@cycles_to_calculate)
		{
			if ($service eq 'rdds')
			{
				if (!is_rdap_standalone($clock))
				{
					$interfaces_ref_rdap_before_switch //= get_interfaces($tld, $service, $clock);
					$interfaces_ref = $interfaces_ref_rdap_before_switch;
				}
				else
				{
					$interfaces_ref_rdap_after_switch //= get_interfaces($tld, $service, $clock);
					$interfaces_ref = $interfaces_ref_rdap_after_switch;
				}

			}

			calculate_cycle(
				$tld,
				$service,
				$lastvalues_db_tld->{$service}{'probes'},
				$lastvalues_nsids_tld->{'probes'},
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

	# this is for Service Availability items, we calculate cycles based on those values
	my $probe = RSMHOST;

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
					AH_SLA_API_VERSION_1,
					ah_get_api_tld($tld),
					$service,
					$delay,
					cycle_start($now, $delay),
					$clock_limits{$service},
					\$ts) == AH_SUCCESS)
			{
				$lastclock = $ts;

				dbg("$service: itemid $itemid from probe \"$probe\" not in cache yet");
				dbg("using the time since most recent measurement file: ", ts_str($lastclock));
			}
			else
			{
				$lastclock = $clock_limits{$service};

				dbg("$service: itemid $itemid from probe \"$probe\" not in cache yet");
				dbg("using last clock based on initial_measurements_limit: ", ts_str($lastclock));
			}

			# TODO: remove this after migrating to Standalone RDAP
			if ($service eq "rdap" && $lastclock < get_rdap_standalone_ts())
			{
				# when we switch to standalone RDAP we should start generating
				# data starting from the time of the switch

				wrn("truncating lastclock to Standalone RDAP switch time ", ts_str($lastclock));

				$lastclock = get_rdap_standalone_ts();
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

	$timestamp_from -= convert_suffixed_number($rows_ref->[0]->[0]);

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
	$key = substr($key, length("rsm.")) if (str_starts_with($key, "rsm."));

	my $service;

	if (str_starts_with($key, "dns"))
	{
		$service = "dns";
	}
	elsif (str_starts_with($key, "rdds"))
	{
		$service = "rdds";
	}
	elsif (str_starts_with($key, "rdap"))
	{
		$service = "rdap";
	}

	return $service;
}

sub get_service_from_slv_key($)
{
	my $key = shift;

	# remove possible "rsm.slv."
	$key = substr($key, length("rsm.slv.")) if (str_starts_with($key, "rsm.slv."));

	my $service;

	if (str_starts_with($key, "dns."))
	{
		$service = "dns";
	}
	elsif (str_starts_with($key, "dnssec."))
	{
		$service = "dns";
	}
	elsif (str_starts_with($key, "rdds."))
	{
		$service = "rdds";
	}
	elsif (str_starts_with($key, "rdap."))
	{
		$service = "rdap";
	}
	else
	{
		fail("cannot extract service from item \"$key\"");
	}

	return $service;
}

sub get_lastvalues_from_db($$$$)
{
	my $lastvalues_db = shift;
	my $lastvalues_nsids = shift;
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

	my $item_str_rows_ref = db_select(
		"select h.host,hg.groupid,i.itemid,i.key_,i.value_type".
		" from items i,hosts h,hosts_groups hg".
		" where h.hostid=i.hostid".
			" and hg.hostid=h.hostid".
			" and i.status=".ITEM_STATUS_ACTIVE.
			" and i.value_type=".ITEM_VALUE_TYPE_STR.
			$host_cond
	);

	# return if nothing to do
	return if (scalar(@{$item_num_rows_ref}) == 0 && scalar(@{$item_str_rows_ref}) == 0);

	my $sql = '';

	# get everything from lastvalue, lastvalue_str tables
	if (scalar(@{$item_num_rows_ref}))
	{
		my $itemids = '';

		foreach my $row_ref (@{$item_num_rows_ref})
		{
			$itemids .= $row_ref->[2] . ',';
		}

		chop($itemids);

		# Note! The "value" field is only needed for $lastvalues_nsids!

		$sql .=
			"select itemid,clock,value".
			" from lastvalue".
			" where itemid in ($itemids)";
	}

	if (scalar(@{$item_str_rows_ref}))
	{
		if (scalar(@{$item_num_rows_ref}))
		{
			$sql .= " union ";
		}

		my $itemids = '';

		foreach my $row_ref (@{$item_str_rows_ref})
		{
			$itemids .= $row_ref->[2] . ',';
		}

		chop($itemids);

		# Note! The "value" field is only needed for $lastvalues_nsids!

		$sql .=
			"select itemid,clock,value".
			" from lastvalue_str".
			" where itemid in ($itemids)";
	}

	my $lastval_rows_ref = db_select($sql);

	my $lastvalues_map = {};

	map { $lastvalues_map->{$_->[0]} = {'clock' => $_->[1], 'value' => $_->[2]} } @{$lastval_rows_ref};

	undef($lastval_rows_ref);

	# join items and lastvalues

	my @item_rows_ref = (@{$item_num_rows_ref}, @{$item_str_rows_ref});
	undef($item_num_rows_ref);
	undef($item_str_rows_ref);

	foreach my $row_ref (@item_rows_ref)
	{
		my ($host, $hostgroupid, $itemid, $key, $value_type) = @{$row_ref};

		next unless(defined($lastvalues_map->{$itemid}));

		my $clock = $lastvalues_map->{$itemid}{'clock'};

		# Note! The "value" field is only needed for $lastvalues_nsids!
		my $value = $lastvalues_map->{$itemid}{'value'};

		my ($probe, $key_service);

		if ($hostgroupid == TLDS_GROUPID)
		{
			# this item belongs to TLD (we only care about Service availability (*.avail) items)
			next unless (str_ends_with($key, "avail"));

			$key_service = get_service_from_slv_key($key);
		}
		elsif ($hostgroupid == TLD_PROBE_RESULTS_GROUPID)
		{
			# not interested in those
			next if (str_starts_with($key, "rsm.conf"));
			next if (str_starts_with($key, "rsm.probe"));

			# not interested in master items that return JSON and mode
			next if (str_starts_with($key, "rsm.dns["));
			next if (str_starts_with($key, "rsm.rdds["));
			next if (str_starts_with($key, "rdap["));
			next if (str_starts_with($key, "rsm.dns.mode"));

			# from what's left, only interested in those
			next unless (str_starts_with($key, "rsm.") ||
					str_starts_with($key, "rdap."));

			(undef, $probe) = split(" ", $host, 2);

			$key_service = get_service_from_probe_key($key);
		}
		else
		{
			fail("unexpected host group id \"$hostgroupid\"");
		}

		# TODO: remove this override after migrating to Standalone RDAP
		if ($key_service eq "rdap" && !is_rdap_standalone($clock))
		{
			dbg("changing \$key_service from 'rdap' to 'rdds' because Standalone RDAP hasn't started yet");
			$key_service = "rdds";
		}

		fail("cannot identify Service of key \"$key\"") unless ($key_service);

		foreach my $service ($key_service eq 'dns' ? ('dns', 'dnssec') : ($key_service))
		{
			# fake name is for Service Availability items
			$lastvalues_db->{'tlds'}{$tld}{$service}{'probes'}{$probe // RSMHOST()}{$itemid} = {
				'key'        => $key,
				'clock'      => $clock,
				'value_type' => $value_type,
			};

			if ($service eq 'dns' && str_starts_with($key, "rsm.dns.nsid"))
			{
				my ($ns, $ip) = split(',', get_nsip_from_key($key));

				$lastvalues_nsids->{'tlds'}{$tld}{'probes'}{$probe}{$ns}{$ip} = {
					'itemid' => $itemid,
					'clock'  => $clock,
					'value'  => $value,
				}
			}
		}
	}
}

sub fill_test_data($$$$)
{
	my $service = shift;
	my $src = shift;
	my $dst = shift;
	my $hist = shift;

	foreach my $target (sort(keys(%{$src})))
	{
		my $test_data_ref = {
			'target'	=> $target,
			'status'	=> undef,
			'metrics'	=> []
		};

		foreach my $src_metric_ref (sort { $a->{'ip'} cmp $b->{'ip'} } @{$src->{$target}})
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
				'testDateTime'	=> $src_metric_ref->{'clock'},
				'targetIP'	=> $src_metric_ref->{'ip'},
			};

			if (exists($src_metric_ref->{'nsid'}))
			{
				$metric->{'nsid'} = $src_metric_ref->{'nsid'};
			}

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

sub get_history($$$$)
{
	my $itemids = shift;
	my $table   = shift;
	my $from    = shift;
	my $till    = shift;

	my $max_chunk_size = 100;

	my @itemids = @{$itemids};
	my @rows = ();

	while (@itemids)
	{
		my @itemids_splice = splice(@itemids, 0, $max_chunk_size);
		my $itemids_placeholder = join(",", ("?") x scalar(@itemids_splice));

		my $sql = "select itemid,value,clock" .
			" from $table" .
			" where" .
				" itemid in ($itemids_placeholder) and" .
				" clock between ? and ?";
		my $params = [@itemids_splice, $from, $till];

		my $rows = db_select($sql, $params);

		push(@rows, @{$rows});
	}

	return @rows;
}

sub get_history_with_throttling($$$$)
{
	my $itemids = shift;
	my $table   = shift;
	my $from    = shift;
	my $till    = shift;

	my $sql;
	my $params;
	my $rows;

	# get max clocks

	my $itemids_placeholder = join(",", ("?") x scalar(@{$itemids}));
	$sql = "select itemid,max(clock) from $table where itemid in ($itemids_placeholder) and clock between ? and ? group by itemid";
	$params = [@{$itemids}, $from, $till];
	$rows = db_select($sql, $params);

	if (!@{$rows})
	{
		return [];
	}

	# get values

	my $filter_placeholder = join("or", ("(itemid=? and clock=?)") x scalar(@{$rows}));
	$sql = "select itemid,clock,value from $table where $filter_placeholder";
	$params = [map(@{$_}, @{$rows})];
	$rows = db_select($sql, $params);

	return $rows;
}

sub get_cycle_nsids($$$)
{
	my $lastvalues = shift;
	my $from       = shift;
	my $till       = shift;

	my %result  = ();
	my %missing = ();

	foreach my $probe (keys(%{$lastvalues}))
	{
		foreach my $ns (keys(%{$lastvalues->{$probe}}))
		{
			foreach my $ip (keys(%{$lastvalues->{$probe}{$ns}}))
			{
				my $itemid = $lastvalues->{$probe}{$ns}{$ip}{'itemid'};
				my $clock  = $lastvalues->{$probe}{$ns}{$ip}{'clock'};
				my $value  = $lastvalues->{$probe}{$ns}{$ip}{'value'};

				if ($clock <= $till)
				{
					$result{$probe}{$ns}{$ip} = $value;
				}
				else
				{
					$missing{$itemid} = \$result{$probe}{$ns}{$ip};
				}
			}
		}
	}

	if (keys(%missing))
	{
		my $history = get_history_with_throttling([keys(%missing)], 'history_str', $from - get_heartbeat() + 60, $till);

		foreach my $row (@{$history})
		{
			my ($itemid, undef, $value) = @{$row};

			${$missing{$itemid}} = $value;
		}
	}

	return \%result;
}

sub probes_data_to_itemids($)
{
	my $data = shift;

	my $itemids;

	foreach my $probe (keys(%{$data}))
	{
		foreach my $itemid (keys(%{$data->{$probe}}))
		{
			my $key = $data->{$probe}{$itemid}{'key'};

			$itemids->{$probe}{$key} = $itemid;
		}
	}

	return $itemids;
}

sub calculate_cycle($$$$$$$$$$)
{
	$tld = shift;		# set globally
	my $service              = shift;
	my $probes_data          = shift;
	my $lastvalues_nsids_tld = shift;
	my $cycle_clock          = shift;
	my $delay                = shift;
	my $rtt_limit            = shift;
	my $service_probes_ref   = shift;	# probes enabled for the service ('name' => {'hostid' => hostid, 'status' => status}) available for this service
	my $all_probes_ref       = shift;	# same structure but all available probes in the system, for listing in JSON files
	my $interfaces_ref       = shift;

	my $from = cycle_start($cycle_clock, $delay);
	my $till = cycle_end($cycle_clock, $delay);

	my $itemids = probes_data_to_itemids($probes_data);

	my $json = {'testedInterface' => []};

	if (get_monitoring_target() eq MONITORING_TARGET_REGISTRY)
	{
		$json->{'tld'} = $tld;
	}
	elsif (get_monitoring_target() eq MONITORING_TARGET_REGISTRAR)
	{
		$json->{'registrarID'} = $tld;
	}

	$json->{'service'} = $service;
	$json->{'cycleCalculationDateTime'} = $from;

	# TODO: in the future consider getting rid of %tested_interfaces and using $probe_results directly
	my %tested_interfaces;

	#
	# First, get Service Availability data.
	#

	my $service_availability_itemid = $itemids->{RSMHOST()}{"rsm.slv.$service.avail"};

	if (!$service_availability_itemid)
	{
		wrn("no $service Service Availability data in the database yet!");
		return;
	}

	my $rows_ref = db_select(
		"select itemid,value".
		" from " . history_table(ITEM_VALUE_TYPE_UINT64).
		" where itemid=$service_availability_itemid" .
			" and " . sql_time_condition($from, $till)
	);

	# {
	#     ITEMID => value (int),
	# }
	my %values;

	map {push(@{$values{$_->[0]}}, int($_->[1]))} (@{$rows_ref});

	# skip cycles that do not have test result
	return if (scalar(keys(%values)) == 0);

	my $rawstatus;

	foreach my $itemid (keys(%values))
	{
		my $key = $probes_data->{RSMHOST()}{$itemid}{'key'};

		if (str_starts_with($key, "rsm.slv."))
		{
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

			$rawstatus = $values{$itemid}[0];

			$json->{'status'} = $service_status_to_str{$rawstatus};

			if (!defined($json->{'status'}))
			{
				fail("item \"$key\" ($itemid) contains unexpected value \"$rawstatus\"");
			}
		}
		else
		{
			fail("unexpected item key: \"$key\"");
		}
	}

	if (!defined($json->{'status'}))
	{
		fail("Rsmhost $tld is missing ", uc($service), " service availability value at ", ts_full($cycle_clock));
	}

	dbg("$service cycle: $json->{'status'}");

	#
	# Now get test results.
	#

	# get itemids of probe items, group them by type and by probe

	my %itemids_by_probe;

	my @itemids_uint;
	my @itemids_float;
	my @itemids_str;

	foreach my $probe (keys(%{$probes_data}))
	{
		# service availability items are already handled
		next if ($probe eq RSMHOST);

		# in case of Up-inconclusive-reconfig do not collect probe results
		next if ($rawstatus == UP_INCONCLUSIVE_RECONFIG);

		# collect itemids, group them by value_type to fetch values from according history table later

		my $probe_data = $probes_data->{$probe};

		my @probe_itemids_uint  = grep { $probe_data->{$_}{'value_type'} == ITEM_VALUE_TYPE_UINT64 } keys(%{$probe_data});
		my @probe_itemids_float = grep { $probe_data->{$_}{'value_type'} == ITEM_VALUE_TYPE_FLOAT  } keys(%{$probe_data});
		my @probe_itemids_str   = grep { $probe_data->{$_}{'value_type'} == ITEM_VALUE_TYPE_STR && $probe_data->{$_}{'key'} !~ /^rsm\.dns\.nsid\[/ } keys(%{$probe_data});

		next if (@probe_itemids_uint == 0 || @probe_itemids_float == 0);

		$itemids_by_probe{$probe} = [@probe_itemids_uint, @probe_itemids_float, @probe_itemids_str];

		push(@itemids_uint , @probe_itemids_uint);
		push(@itemids_float, @probe_itemids_float);
		push(@itemids_str  , @probe_itemids_str);
	}

	# get history of probe items

	my @rows;

	push(@rows, get_history(\@itemids_uint , "history_uint", $from, $till));
	push(@rows, get_history(\@itemids_float, "history"     , $from, $till));
	push(@rows, get_history(\@itemids_str  , "history_str" , $from, $till));

	my %rows_by_itemid = map { $_->[0] => $_ } @rows;

	my $nsids;

	if ($service eq 'dns' || $service eq 'dnssec')
	{
		if (!exists($nsids_cache->{$cycle_clock}))
		{
			$nsids_cache->{$cycle_clock} = get_cycle_nsids($lastvalues_nsids_tld, $from, $till);
		}

		$nsids = $nsids_cache->{$cycle_clock};
	}

	# we need to aggregate DNS target statuses from Probes to generate Name Server Availability data
	my $name_server_availability_data = {};

	foreach my $probe (keys(%{$probes_data}))
	{
		# Service Availability items are already handled
		next if ($probe eq RSMHOST);

		my $results;

		# In case of Up-inconclusive-reconfig do not collect probe results
		if ($rawstatus != UP_INCONCLUSIVE_RECONFIG)
		{
			next if (!defined($itemids_by_probe{$probe}));

			my @results = grep { defined($_) } map($rows_by_itemid{$_}, @{$itemids_by_probe{$probe}});

			$results = get_test_results(\@results, $probes_data->{$probe}, $service);
		}

		foreach my $cycleclock (keys(%{$results->{$service}}))
		{
			foreach my $interface (keys(%{$results->{$service}{$cycleclock}{'interfaces'}}))
			{
				my $clock = $results->{$service}{$cycleclock}{'interfaces'}{$interface}{'clock'};

				next unless ($clock);

				my $tested_interface = translate_interface($interface);

				foreach my $target (keys(%{$results->{$service}{$cycleclock}{'interfaces'}{$interface}{'targets'}}))
				{
					# go through DNS target statuses on Probes and aggregate them
					if ($interface eq 'dns' || $interface eq 'dnssec')
					{
						my $city_status = $results->{$service}{$cycleclock}{'interfaces'}{$interface}{'targets'}{$target}{'status'};

						if (!defined($name_server_availability_data->{'interfaces'}{$interface}{'targets'}{$target}) ||
								$name_server_availability_data->{'interfaces'}{$interface}{'targets'}{$target} != DOWN)
						{
							$name_server_availability_data->{'interfaces'}{$interface}{'targets'}{$target} = $city_status;
						}

						$name_server_availability_data->{'probes'}{$probe}{$target} = $city_status;
					}

					foreach my $metric (@{$results->{$service}{$cycleclock}{'interfaces'}{$interface}{'targets'}{$target}{'metrics'}})
					{
						# convert clock and rtt to integer
						my $h = {
							'rtt'        => int($metric->{'rtt'}),
							'ip'         => $metric->{'ip'},
							'clock'      => int($clock),
						};

						if ($service eq 'dns' || $service eq 'dnssec')
						{
							$h->{'nsid'} = $nsids->{$probe}{$target}{$metric->{'ip'}} // '';
						}

						push(@{$tested_interfaces{$tested_interface}{$probe}{'testData'}{$target}}, $h);
					}
				}

				# interface status
				$tested_interfaces{$tested_interface}{$probe}{'status'} =
					($results->{$service}{$cycleclock}{'interfaces'}{$interface}{'status'} == UP ? 'Up' : 'Down');

				# interface tested name
				if (exists($results->{$service}{$cycleclock}{'interfaces'}{$interface}{'testedname'}))
				{
					$tested_interfaces{$tested_interface}{$probe}{'testedname'} =
						$results->{$service}{$cycleclock}{'interfaces'}{$interface}{'testedname'};
				}

				# interface transport protocol, it's TCP if unspecified
				if (exists($results->{$service}{$cycleclock}{'interfaces'}{$interface}{'protocol'}) &&
						($results->{$service}{$cycleclock}{'interfaces'}{$interface}{'protocol'} == PROTO_UDP))
				{
					$tested_interfaces{$tested_interface}{$probe}{'transport'} = 'udp';
				}
				else
				{
					$tested_interfaces{$tested_interface}{$probe}{'transport'} = 'tcp';
				}
			}
		}
	}

	# add "Offline" and "No results"
	foreach my $probe (keys(%{$all_probes_ref}))
	{
		my $is_probe_online;	# 0 or 1, just for storing the correct value later

		if (defined($service_probes_ref->{$probe}) &&
				$service_probes_ref->{$probe}{'status'} == HOST_STATUS_MONITORED)
		{
			$is_probe_online = probe_online_at($probe, $from, $delay);
		}
		else
		{
			$is_probe_online = 0;
		}

		foreach my $interface (@{$interfaces_ref})
		{
			if ($rawstatus == UP_INCONCLUSIVE_RECONFIG)
			{
				$tested_interfaces{$interface}{$probe}{'status'} = AH_CITY_NO_RESULT;
			}
			elsif (!$is_probe_online)
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
	}

	#
	# add data that was collected from history and calculated in previous cycle
	#

	foreach my $interface (sort(keys(%tested_interfaces)))
	{
		my $interface_json = {
			'interface'	=> $interface,
			'probes'	=> []
		};

		foreach my $probe (sort(keys(%{$tested_interfaces{$interface}})))
		{
			my $probe_ref = {
				'city'		=> $probe,
				'status'	=> $tested_interfaces{$interface}{$probe}{'status'},	# Probe status
				'testData'	=> []
			};

			if ($tested_interfaces{$interface}{$probe}{'status'} ne AH_CITY_OFFLINE &&
					$tested_interfaces{$interface}{$probe}{'status'} ne AH_CITY_NO_RESULT)
			{
				$probe_ref->{'transport'}  = $tested_interfaces{$interface}{$probe}{'transport'};
				$probe_ref->{'testedName'} = $tested_interfaces{$interface}{$probe}{'testedname'};
			}

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

	# Add aggregated Name Server Availability data. E. g.
	#
	# "minNameServersUp": 2,
	# "nameServerAvailability": {
	# 	"nameServerStatus": [
	# 		{
	# 			"target": "ns1.nic.exmaple",
	# 			"status": "Down"
	# 		},
	# 		{
	# 			"target": "ns2.nic.example",
	# 			"status": "Down"
	# 		}
	# 	],
	# 	"probes": [
	# 		{
	# 			"city": "WashingtonDC",
	# 			"testData": [
	# 				{
	# 					"target": "ns1.nic.example",
	# 					"status": "Down"
	# 				},
	# 				{
	# 					"target": "ns2.nic.example",
	#					"status": "Down"
	# 				}
	# 			]
	# 		},
	# 		{
	# 			"city": "Sydney",
	# 			"testData": [
	# 				{
	# 					"target": "ns1.nic.example",
	# 					"status": "Up"
	# 				},
	# 				{
	# 					"target": "ns2.nic.example",
	# 					"status": "Up"
	# 				}
	# 			]
	# 		}
	# 	]
	# }

	foreach my $interface (%{$name_server_availability_data->{'interfaces'}})
	{
		foreach my $target (sort(keys(%{$name_server_availability_data->{'interfaces'}{$interface}{'targets'}})))
		{
			my $status = $name_server_availability_data->{'interfaces'}{$interface}{'targets'}{$target};

			next unless (defined($status));

			push(@{$json->{'nameServerAvailability'}{'nameServerStatus'}},
				{
					'target' => $target,
					'status' => ($status == UP ? 'Up' : 'Down'),
				}
			);
		}
	}

	foreach my $probe (sort(keys(%{$name_server_availability_data->{'probes'}})))
	{
		my $test_data = [];

		foreach my $target (sort(keys(%{$name_server_availability_data->{'probes'}{$probe}})))
		{
			my $status = $name_server_availability_data->{'probes'}{$probe}{$target};

			next unless (defined($status));

			push(@{$test_data},
				{
					'target' => $target,
					'status' => ($status == UP ? 'Up' : 'Down'),
				}
			);
		}

		push(@{$json->{'nameServerAvailability'}{'probes'}},
			{
				'city' => $probe,
				'testData' => $test_data,
			}
		);
	}

	# add configuration data
	if ($service eq 'dns' || $service eq 'dnssec')
	{
		my $cfg_minns = get_dns_minns($tld, $cycle_clock);

		fail("number of Minimum Name Servers for TLD $tld is configured as $cfg_minns") if (1 > $cfg_minns);

		$json->{'minNameServersUp'} = int($cfg_minns);
	}

	if (opt('debug2'))
	{
		print(Dumper($json));
	}

	return if (opt('dry-run'));

	if (ah_save_measurement(
			AH_SLA_API_VERSION_2,
			ah_get_api_tld($tld),
			$service,
			$json,
			$from) != AH_SUCCESS)
	{
		fail("cannot save recent measurement: ", ah_get_error());
	}

	# the first version had no RDAP and no additional things that appeared in version 2, so let's remove them
	if ($service ne 'rdap')
	{
		delete($json->{'minNameServersUp'});
		delete($json->{'nameServerAvailability'});

		if ($rawstatus == UP_INCONCLUSIVE_RECONFIG)
		{
			$json->{'status'} = 'Up-inconclusive-no-data';
		}

		foreach my $i_ref (@{$json->{'testedInterface'}})
		{
			foreach my $p_ref (@{$i_ref->{'probes'}})
			{
				delete($p_ref->{'transport'});
				delete($p_ref->{'testedName'});

				foreach my $t_ref (@{$p_ref->{'testData'}})
				{
					undef($t_ref->{'target'}) if ($service eq 'rdds');

					foreach my $m_ref (@{$t_ref->{'metrics'}})
					{
						delete($m_ref->{'nsid'});
					}
				}
			}
		}

		if (ah_save_measurement(
				AH_SLA_API_VERSION_1,
				ah_get_api_tld($tld),
				$service,
				$json,
				$from) != AH_SUCCESS)
		{
			fail("cannot save recent measurement: ", ah_get_error());
		}
	}
}

sub translate_interface($)
{
	my $interface = shift;

	return AH_INTERFACE_DNS    if ($interface eq 'dns');
	return AH_INTERFACE_DNSSEC if ($interface eq 'dnssec');
	return AH_INTERFACE_RDDS43 if ($interface eq 'rdds43');
	return AH_INTERFACE_RDDS80 if ($interface eq 'rdds80');
	return AH_INTERFACE_RDAP   if ($interface eq 'rdap');

	fail("$interface: unknown interface");
}

sub get_interfaces($$$)
{
	my $tld = shift;
	my $service = shift;
	my $clock = shift;

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
		push(@result, AH_INTERFACE_RDDS43) if (tld_interface_enabled($tld, 'rdds43', $clock));
		push(@result, AH_INTERFACE_RDDS80) if (tld_interface_enabled($tld, 'rdds80', $clock));

		# TODO: remove this after migrating to Standalone RDAP
		if (!is_rdap_standalone($clock))
		{
			push(@result, AH_INTERFACE_RDAP) if (tld_interface_enabled($tld, 'rdap', $clock));
		}
	}
	elsif ($service eq 'rdap')
	{
		push(@result, AH_INTERFACE_RDAP);
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

__END__

=head1 NAME

sla-api-current.pl - generate recent SLA API measurement files for newly collected data

=head1 SYNOPSIS

sla-api-current.pl [--tld <tld>] [--service <name>] [--server-id <id>] [--now unixtimestamp] [--period minutes] [--print-period] [--max-children n] [--max-wait seconds] [--prettify-json] [--debug] [--dry-run] [--help]

=head1 OPTIONS

=over 8

=item B<--tld> tld

Optionally specify TLD.

=item B<--service> name

Optionally specify service, one of: dns, dnssec, rdds, rdap (if it's standalone).

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

=item B<--prettify-json>

Prettify output in JSON files.

=item B<--debug>

Run the script in debug mode. This means printing more information.

=item B<--dry-run>

Print data to the screen, do not write anything to the filesystem.

=item B<--help>

Print a brief help message and exit.

=back

=head1 DESCRIPTION

B<This program> will generate the most recent measurement files for newly collected monitoring data. The files will be
available under directory specified in rsm.conf . Each run the script would generate new measurement files for the period
from the last run till up to 30 minutes (unless option --period is given).

=head1 EXAMPLES

/opt/zabbix/scripts/sla-api-recent.pl

Generate recent measurement files for the period from last generated till up to 30 minutes.

=cut
