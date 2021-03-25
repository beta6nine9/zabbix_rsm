#!/usr/bin/env perl

use FindBin;
use lib "$FindBin::RealBin";
use lib "$FindBin::RealBin/..";

use strict;
use warnings;

use RSM;
use RSMSLV;
use DaWa;
use Data::Dumper;
use Time::Local;
use POSIX qw(floor);
use Time::HiRes qw(time);
use TLD_constants qw(:ec :api :general :config);
use Parallel::ForkManager;

use constant RDDS_SUBSERVICE => 'sub';

use constant PROBE_STATUS_UP => 'Up';
use constant PROBE_STATUS_DOWN => 'Down';
use constant PROBE_STATUS_UNKNOWN => 'Unknown';

use constant JSON_INTERFACE_DNS		=> 'DNS';
use constant JSON_INTERFACE_DNSSEC	=> 'DNSSEC';
use constant TRIGGER_SEVERITY_NOT_CLASSIFIED	=> 0;
use constant TRIGGER_VALUE_FALSE	=> 0;
use constant SEC_PER_WEEK	=> 604800;
use constant EVENT_OBJECT_TRIGGER	=> 0;
use constant EVENT_SOURCE_TRIGGERS	=> 0;
use constant TRIGGER_VALUE_TRUE		=> 1;
use constant JSON_INTERFACE_RDDS43	=> 'RDDS43';
use constant JSON_INTERFACE_RDDS80	=> 'RDDS80';
use constant JSON_INTERFACE_RDAP	=> 'RDAP';
use constant ROOT_ZONE_READABLE		=> 'zz--root';

use constant PROBE_OFFLINE_STR	=> 'Offline';
use constant PROBE_ONLINE_STR	=> 'Online';

use constant AH_STATUS_UP	=> 'Up';
use constant AH_STATUS_DOWN	=> 'Down';

use constant true => 1;

use constant TARGETS_TMP_DIR => '/opt/zabbix/export-tmp';
use constant TARGETS_TARGET_DIR => '/opt/zabbix/export';

use constant EXPORT_MAX_CHILDREN_DEFAULT => 24;
use constant EXPORT_MAX_CHILDREN_FLOOR => 1;
use constant EXPORT_MAX_CHILDREN_CEIL => 128;

sub __get_test_data($$$);
sub __save_csv_data($$);
sub __get_probe_changes($$);

parse_opts('probe=s', 'service=s', 'tld=s', 'date=s', 'day=i', 'shift=i', 'force', 'max-children=i');

setopt('nolog');

my $config = get_rsm_config();
set_slv_config($config);

my @server_keys = get_rsm_server_keys($config);

validate_tld(getopt('tld'), \@server_keys) if (opt('tld'));
validate_service(getopt('service')) if (opt('service'));

db_connect();

__validate_input() unless (opt('force'));

my ($d, $m, $y) = split('/', getopt('date'));

usage() unless ($d && $m && $y);

dw_set_date($y, $m, $d);

if (!opt('dry-run') && (my $error = rsm_targets_prepare(TARGETS_TMP_DIR, TARGETS_TARGET_DIR)))
{
	fail($error);
}

my $services;
if (opt('service'))
{
	$services->{getopt('service')} = undef;
}
else
{
	if (get_monitoring_target() eq MONITORING_TARGET_REGISTRY)
	{
		$services->{'dns'} = undef;
		$services->{'dnssec'} = undef;
		$services->{'epp'} = undef;
	}
	$services->{'rdds'} = undef;
	$services->{'rdap'} = undef if (is_rdap_standalone());
}

my @interfaces;
foreach my $service (keys(%{$services}))
{
	if ($service eq 'rdds')
	{
		push(@interfaces, 'rdds43', 'rdds80');
		push(@interfaces, 'rdap') if (!is_rdap_standalone());
	}
	else
	{
		push(@interfaces, $service);
	}
}

my $cfg_avail_valuemaps = get_avail_valuemaps();

# changed from get_result_string($cfg_dns_statusmaps, UP/Down)
my $general_status_up = 'Up';
my $general_status_down = 'Down';

__get_delays($services);
__get_keys($services);

my $date = timelocal(0, 0, 0, $d, $m - 1, $y);

my $shift = opt('shift') ? getopt('shift') : 0;
$date += $shift;

my $day = opt('day') ? getopt('day') : 86400;

my $check_till = $date + $day - 1;
my ($from, $till) = get_real_services_period($services, $date, $check_till);

if (opt('debug'))
{
	dbg("from: ", ts_full($from));
	dbg("till: ", ts_full($till));
}

my $max = cycle_end(time() - 240, 60);
fail("cannot export data: selected time period is in the future") if (!opt('force') && $till > $max);

# consider only tests that started within given period
my $cfg_dns_minonline;
foreach my $service (sort(keys(%{$services})))
{
	dbg("$service") if (opt('debug'));

	if ($service eq 'dns' || $service eq 'dnssec')
	{
		if (!$cfg_dns_minonline)
		{
			$cfg_dns_minonline = get_macro_dns_probe_online();
		}

		$services->{$service}->{'minonline'} = $cfg_dns_minonline;
	}

	if ($services->{$service}->{'from'} && $services->{$service}->{'from'} < $date)
	{
		# exclude test that starts outside our period
		$services->{$service}->{'from'} += $services->{$service}->{'delay'};
	}

	if ($services->{$service}->{'till'} && $services->{$service}->{'till'} < $check_till)
	{
		# include test that overlaps on the next period
		$services->{$service}->{'till'} += $services->{$service}->{'delay'};
	}

	if (opt('debug'))
	{
		dbg("  delay\t : ", $services->{$service}->{'delay'});
		dbg("  from\t : ", ts_full($services->{$service}->{'from'}));
		dbg("  till\t : ", ts_full($services->{$service}->{'till'}));
		dbg("  avail\t : ", $services->{$service}->{'key_avail'} // 'UNDEF');
	}
}

my $probes_data;

my ($time_start, $time_get_test_data, $time_load_ids, $time_process_records, $time_write_csv);

my $fm = new Parallel::ForkManager(opt('max-children') ? getopt('max-children') : EXPORT_MAX_CHILDREN_DEFAULT);

set_on_fail(\&__wait_all_children_cb);

my $child_failed = 0;
my $signal_sent = 0;

my %tldmap;	# <PID> => <TLD> hashmap

$fm->run_on_finish(
	sub ($$$$$)
	{
		my $pid = shift;
		my $exit_code = shift;
		my $id = shift;
		my $exit_signal = shift;
		my $core_dump = shift;

		# We just raise a $child_failed flag here and send a SIGTERM signal later because we can be in a state
		# when we have already requested Parallel::ForkManager to start another child, but it has already
		# reached the limit and it is waiting for one of them to finish. If we send SIGTERM now, that child will
		# not receive it since it starts after we send the signal.

		if ($core_dump == 1)
		{
			$child_failed = 1;
			info("child (PID:$pid) handling TLD ", $tldmap{$pid}, " core dumped");
		}
		elsif ($exit_code != SUCCESS)
		{
			$child_failed = 1;
			info("child (PID:$pid) handling TLD ", $tldmap{$pid},
					($exit_signal == 0 ? "" : " got signal " . sig_name($exit_signal) . " and"),
					" exited with code $exit_code");
		}
		elsif ($exit_signal != 0)
		{
			$child_failed = 1;
			info("child (PID:$pid) handling TLD ", $tldmap{$pid}, " got signal ", sig_name($exit_signal));
		}
		else
		{
			dbg("child (PID:$pid) handling TLD ", $tldmap{$pid}, " exited successfully");
		}
	}
);

# go through all the databases

foreach (@server_keys)
{
$server_key = $_;

db_disconnect();
db_connect($server_key);

# NB! We need previous value for probeChanges file (see __get_probe_changes())
my $check_probes_from = $from - PROBE_DELAY;
$probes_data->{$server_key} = get_probes();

my $tlds_ref = [];
if (opt('tld'))
{
	foreach my $t (split(',', getopt('tld')))
	{
		if (!tld_exists($t))
		{
			if ($server_keys[-1] eq $server_key)
			{
				# last server in list
				info("TLD $t does not exist.");
				goto WAIT_CHILDREN;
			}

			next;
		}

		push(@{$tlds_ref}, $t);
	}

	next if (scalar(@{$tlds_ref}) == 0);
}
else
{
	$tlds_ref = get_tlds(undef, $from, USE_CACHE_TRUE);
}

# Prepare the cache for function tld_service_enabled(). Make sure this is called before creating child processes!
tld_interface_enabled_delete_cache();	# delete cache of previous server
tld_interface_enabled_create_cache(@interfaces);

db_disconnect();

# unset TLD (for the logs)
undef($tld);

foreach my $tld_for_a_child_to_process (@{$tlds_ref})
{
		goto WAIT_CHILDREN if ($child_failed);	# break from both server and TLD loops

		my $pid;

		# start a new child and send parent to the next iteration

		if (($pid = $fm->start()))
		{
			$tldmap{$pid} = $tld_for_a_child_to_process;

			next;
		}

		init_process();

		set_log_tld($tld_for_a_child_to_process);

		db_connect($server_key);

		$time_start = time();

		# cache probe online statuses
		# TODO: FIXME, we have done that already in other processes! (look for this message in this file)
		foreach my $probe (keys(%{$probes_data->{$server_key}}))
		{
			# probe, from, delay
			probe_online_at($probe, $from, ($till + 1 - $from));
		}

		my $result = __get_test_data($tld_for_a_child_to_process, $from, $till);

		$time_get_test_data = time();

		db_disconnect();

		db_connect();	# connect to the local node
		__save_csv_data($tld_for_a_child_to_process, $result);
		db_disconnect();

		info(sprintf("get data: %s, load ids: %s, process records: %s, write csv: %s",
				format_stats_time($time_get_test_data - $time_start),
				format_stats_time($time_load_ids - $time_get_test_data),
				format_stats_time($time_process_records - $time_load_ids),
				format_stats_time($time_write_csv - $time_process_records))) if (opt('stats'));

		finalize_process();

		# When we fork for real it makes no difference for Parallel::ForkManager whether child calls exit() or
		# calls $fm->finish(), therefore we do not need to introduce $fm->finish() in all our low-level error
		# handling routines, but having $fm->finish() here leaves a possibility to debug a happy path scenario
		# without the complications of actual forking by using:
		# my $fm = new Parallel::ForkManager(0);

		$fm->finish(SUCCESS);
}

last if (opt('tld'));
}	# foreach (@server_keys)
undef($server_key) unless (opt('tld'));	# keep $server_key if --tld was specified (for __get_false_positives())

WAIT_CHILDREN:

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

slv_exit(E_FAIL) unless ($child_failed == 0);

# at this point there should be no child processes so we do not care about locking

my $false_positives = __get_false_positives($from, $till, $server_key);

undef($server_key);

my $probe_changes = __get_probe_changes($from, $till);	# connects to db

db_connect();
dw_csv_init();
dw_load_ids_from_db();
foreach my $fp_ref (@$false_positives)
{
	dbg("writing false positive entry:");
	dbg("  eventid:", $fp_ref->{'eventid'} ? $fp_ref->{'eventid'} : "UNDEF");
	dbg("  clock:", $fp_ref->{'clock'} ? $fp_ref->{'clock'} : "UNDEF");
	dbg("  status:", $fp_ref->{'status'} ? $fp_ref->{'status'} : "UNDEF");

	dw_append_csv(DATA_FALSE_POSITIVE, [
			      $fp_ref->{'eventid'},
			      $fp_ref->{'clock'},
			      dw_get_id(ID_STATUS_MAP, $fp_ref->{'status'}),
			      ''	# reason is not implemented in front-end
		]);
}

foreach my $pc_ref (@{$probe_changes})
{
	dbg("writing probe changes entry:");
	dbg("  name:", $pc_ref->{'probe'});
	dbg("  changed at:", ts_full($pc_ref->{'clock'}));
	dbg("  new status:", $pc_ref->{'status'});

	dw_append_csv(DATA_PROBE_CHANGES, [
			      dw_get_id(ID_PROBE, $pc_ref->{'probe'}),
			      $pc_ref->{'clock'},
			      dw_get_id(ID_STATUS_MAP, $pc_ref->{'status'}),
			      ''	# reason is not implemented yet
		]);
}

dw_write_csv_files();
dw_write_csv_catalogs();

if (!opt('dry-run') && (my $error = rsm_targets_apply()))
{
	fail($error);
}

slv_exit(SUCCESS);

sub __wait_all_children_cb
{
	return if ($fm->is_child());

	$fm->wait_all_children();
}

sub __validate_input
{
	my $error_found = 0;

	if (!opt('date'))
	{
		print("Error: you must specify the date using option --date\n");
		$error_found = 1;
	}
	elsif (getopt('date') !~ /^\d\d\/\d\d\/\d\d\d\d$/)
	{
		print("Error: ", getopt('date'), " -- invalid date, expected format: dd/mm/yyyy\n");
		$error_found = 1;
	}

	foreach my $opt ('probe', 'tld', 'service', 'day', 'shift')
	{
		if (opt($opt) && !opt('dry-run'))
		{
			print("Error: option --$opt can only be used together with --dry-run\n");
			$error_found = 1;
		}
	}

	if (opt('day') && (getopt('day') % 60) != 0)
	{
		print("Error: parameter of option --day must be multiple of 60\n");
		$error_found = 1;
	}

	if (opt('max-children') && (getopt('max-children') < EXPORT_MAX_CHILDREN_FLOOR ||
			EXPORT_MAX_CHILDREN_CEIL < getopt('max-children')))
	{
		usage(sprintf("allowed max-children: %d-%d", EXPORT_MAX_CHILDREN_FLOOR, EXPORT_MAX_CHILDREN_CEIL));
	}

	usage() unless ($error_found == 0);
}

sub __get_delays
{
	my $cfg_dns_delay = undef;
	my $services = shift;

	foreach my $service (sort(keys(%$services)))
	{
		if ($service eq 'dns' || $service eq 'dnssec')
		{
			if (!$cfg_dns_delay)
			{
				$cfg_dns_delay = get_dns_delay();
			}

			$services->{$service}{'delay'} = $cfg_dns_delay;
		}
		elsif ($service eq 'rdds')
		{
			$services->{$service}{'delay'} = get_rdds_delay();
		}
		elsif ($service eq 'rdap')
		{
			$services->{$service}{'delay'} = get_rdap_delay();
		}
		elsif ($service eq 'epp')
		{
			$services->{$service}{'delay'} = get_epp_delay();
		}

		fail("$service delay (", $services->{$service}{'delay'}, ") is not multiple of 60") unless ($services->{$service}{'delay'} % 60 == 0);
	}
}

sub __get_keys
{
	my $services = shift;

	foreach my $service (sort(keys(%$services)))
	{
		$services->{$service}{'key_avail'} = "rsm.slv.$service.avail";
		$services->{$service}{'key_rollweek'} = "rsm.slv.$service.rollweek";
	}
}

# CSV file	: nsTest
# Columns	: probeID,nsFQDNID,tldID,cycleTimestamp,status,cycleID,tldType,nsTestProtocol
#
# Note! cycleID is the concatenation of cycleDateMinute (timestamp) + serviceCategory (5) + tldID
# E. g. 1420070400-5-11
#
# How it works:
# - get list of items
# - get results:
#   "probe1" =>
#   	"ns1.foo.example" =>
#   		"192.0.1.2" =>
#   			"clock" => 1439154000,
#   			"rtt" => 120,
#   		"192.0.1.3"
#   			"clock" => 1439154000,
#   			"rtt" => 1603,
#   	"ns2.foo.example" =>
#   	...
sub __get_test_data($$$)
{
	my $tld = shift;
	my $from = shift;
	my $till = shift;

	my $result = {};
	my $cycles;
	my $incidents;

	foreach my $service (sort(keys(%{$services})))
	{
		next if (!tld_service_enabled($tld, $service, $from));

		my $delay = $services->{$service}{'delay'};
		my $service_from = $services->{$service}{'from'};
		my $service_till = $services->{$service}{'till'};
		my $key_avail = $services->{$service}{'key_avail'};
		my $key_rollweek = $services->{$service}{'key_rollweek'};

		next if (!$service_from || !$service_till);

		my ($itemid_avail, $itemid_rollweek);

		my $hostid = get_hostid($tld);

		$itemid_avail = get_itemid_by_hostid($hostid, $key_avail);

		if ($itemid_avail == E_ID_NONEXIST)
		{
			wrn("configuration error: service $service enabled $service availability item does not exist");
			next;
		}

		$itemid_rollweek = get_itemid_by_hostid($hostid, $key_rollweek);

		if ($itemid_rollweek == E_ID_NONEXIST)
		{
			wrn("configuration error: service $service enabled $service rolling week item does not exist");
			next;
		}

		$incidents->{$service} = __get_incidents2($itemid_avail, $delay, $service_from, $service_till);

		# SERVICE availability data
		my $rows_ref = db_select(
			"select value,clock".
			" from history_uint".
			" where itemid=$itemid_avail".
				" and " . sql_time_condition($service_from, $service_till).
			" order by itemid,clock"	# NB! order is important, see how the result is used below
		);

		my $last_avail_clock;

		foreach my $row_ref (@$rows_ref)
		{
			my $value = $row_ref->[0];
			my $clock = $row_ref->[1];

			next if ($last_avail_clock && $last_avail_clock == $clock);

			$last_avail_clock = $clock;

			#dbg("$service availability at ", ts_full($clock), ": $value");

			unless (exists($cfg_avail_valuemaps->{int($value)}))
			{
				my $expected_list;

				while (my ($status, $description) = each(%{$cfg_avail_valuemaps}))
				{
					if (defined($expected_list))
					{
						$expected_list .= ", ";
					}
					else
					{
						$expected_list = "";
					}

					$expected_list .= "$status ($description)";
				}

				wrn("unknown availability result: $value (expected $expected_list)");
			}

			# We have the test resulting value (Up or Down) at "clock". Now we need to select the
			# time bounds (start/end) of all data points from all proxies.
			#
			#   +........................period (service delay)...........................+
			#   |                                                                         |
			# start                                 clock                                end
			#   |.....................................|...................................|
			#   0 seconds <--zero or more minutes--> 30                                  59
			#

			my $cycleclock = cycle_start($clock, $delay);

			# todo: later rewrite to use valuemap ID from item
			$cycles->{$service}{$cycleclock}{'rawstatus'} = $value;
			$cycles->{$service}{$cycleclock}{'status'} = get_result_string($cfg_avail_valuemaps, $value);
		}

		# Rolling week data (is synced with availability data from above)
		$rows_ref = db_select(
			"select value,clock".
			" from history".
			" where itemid=$itemid_rollweek".
				" and " . sql_time_condition($service_from, $service_till).
			" order by clock"	# NB! order is important, see how the result is used below
		);

		foreach my $row_ref (@$rows_ref)
		{
			my $value = $row_ref->[0];
			my $clock = $row_ref->[1];

			#dbg("$service rolling week at ", ts_full($clock), ": $value");

			my $cycleclock = cycle_start($clock, $delay);

			$cycles->{$service}{$cycleclock}{'rollweek'} = $value;
		}

		if (scalar(keys(%{$cycles->{$service}})) == 0)
		{
			wrn("$service: no results; will not process remaining services");
			return $result;
		}

	}

	my $test_items = get_test_items($tld);

	foreach my $probe (keys(%{$test_items}))
	{
		my (@itemids_uint, @itemids_float, @itemids_str);

		#
		# collect itemids, separate them by value_type to fetch values from according history table later
		#

		map {
			my $i = $test_items->{$probe}{$_};

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
		} (keys(%{$test_items->{$probe}}));

		next if (@itemids_uint == 0 || @itemids_float == 0);

		my ($results_uint, $results_float, $results_str);

		get_test_history(
			$from,
			$till,
			\@itemids_uint,
			\@itemids_float,
			\@itemids_str,
			\$results_uint,
			\$results_float,
			\$results_str
		);

		my $results = get_test_results(
			[@{$results_uint}, @{$results_float}, @{$results_str}],
			$test_items->{$probe}
		);

		# add tests to appropriate cycles
		foreach my $service (keys(%{$results}))
		{
			my $delay = $services->{$service}{'delay'};

			foreach my $cycleclock (sort(keys(%{$results->{$service}})))
			{
				if (!exists($cycles->{$service}{$cycleclock}))
				{
					__no_cycle_result(uc($service) . " Service Availability", "rsm.slv.$service.avail", $cycleclock);
					next;
				}

				if ($service eq 'dns' || $service eq 'dnssec')
				{
					$cycles->{$service}{$cycleclock}{'minns'} = get_dns_minns($tld, $cycleclock);
				}

				foreach my $interface (keys(%{$results->{$service}{$cycleclock}{'interfaces'}}))
				{
					# the status is set later
					$cycles->{$service}{$cycleclock}{'interfaces'}{$interface}{'probes'}{$probe}{'status'} = undef;

					if (!probe_online_at($probe, $cycleclock, $delay))
					{
						$cycles->{$service}{$cycleclock}{'interfaces'}{$interface}{'probes'}{$probe}{'status'} = PROBE_OFFLINE_STR;
					}

					$cycles->{$service}{$cycleclock}{'interfaces'}{$interface}{'probes'}{$probe} =
						$results->{$service}{$cycleclock}{'interfaces'}{$interface};

					# TODO: each target own protocol? FIXME
				}
			}
		}
	}

	foreach my $service (sort(keys(%{$services})))
	{
		next if (!tld_service_enabled($tld, $service, $from));

		$result->{'services'}{$service}{'cycles'} = $cycles->{$service};
		$result->{'services'}{$service}{'incidents'} = $incidents->{$service} // [];
	}

	$result->{'type'} = __get_tld_type($tld);

	return $result;
}

sub __save_csv_data($$)
{
	my $tld = shift;
	my $result = shift;

	$time_load_ids = $time_process_records = $time_write_csv = time();

	return if (scalar(keys(%{$result})) == 0);

	dw_csv_init();
	dw_load_ids_from_db();

	$time_load_ids = time();

	my $ns_service_category_id = dw_get_id(ID_SERVICE_CATEGORY, 'ns');
	my $dns_service_category_id = dw_get_id(ID_SERVICE_CATEGORY, 'dns');
	my $dnssec_service_category_id = dw_get_id(ID_SERVICE_CATEGORY, 'dnssec');
	my $rdds_service_category_id = dw_get_id(ID_SERVICE_CATEGORY, 'rdds');
	my $epp_service_category_id = dw_get_id(ID_SERVICE_CATEGORY, 'epp');
	my $rdap_service_category_id = dw_get_id(ID_SERVICE_CATEGORY, 'rdap');
	my $udp_protocol_id = dw_get_id(ID_TRANSPORT_PROTOCOL, 'udp');
	my $tcp_protocol_id = dw_get_id(ID_TRANSPORT_PROTOCOL, 'tcp');

	my $tld_id = dw_get_id(ID_TLD, $tld);
	my $tld_type_id = dw_get_id(ID_TLD_TYPE, $result->{'type'});

	# RTT.LOW macros
	my $rtt_low;

	foreach my $service (sort(keys(%{$result->{'services'}})))
	{
		my $service_ref = $services->{$service};

		my ($service_category_id);

		if ($service eq 'dns')
		{
			$service_category_id = $dns_service_category_id;
		}
		elsif ($service eq 'dnssec')
		{
			$service_category_id = $dnssec_service_category_id;
		}
		elsif ($service eq 'rdds')
		{
			$service_category_id = $rdds_service_category_id;
		}
		elsif ($service eq 'rdap')
		{
			$service_category_id = $rdap_service_category_id;
		}
		elsif ($service eq 'epp')
		{
			$service_category_id = $epp_service_category_id;
		}
		else
		{
			fail("THIS SHOULD NEVER HAPPEN");
		}

		my $incidents = $result->{'services'}{$service}{'incidents'};
		my $incidents_count = scalar(@{$incidents});
		my $inc_idx = 0;

		# test results
		foreach my $cycleclock (sort(keys(%{$result->{'services'}{$service}{'cycles'}})))
		{
			my $cycle_ref = $result->{'services'}{$service}{'cycles'}{$cycleclock};

			if (!defined($cycle_ref->{'status'}))
			{
				__no_cycle_result(uc($service) . " Service Availability", "rsm.slv.$service.avail", $cycleclock);
				next;
			}

			if (!defined($cycle_ref->{'rollweek'}))
			{
				__no_cycle_result(uc($service) . " Rolling Week", "rsm.slv.$service.rollweek", $cycleclock);
				next;
			}

			my %nscycle;	# for Name Server cycle

			my $eventid = '';

			if ($inc_idx < $incidents_count)
			{
				while ($inc_idx < $incidents_count && $incidents->[$inc_idx]{'end'} && $incidents->[$inc_idx]{'end'} < $cycleclock)
				{
					$inc_idx++;
				}

				if ($inc_idx < $incidents_count && (!$incidents->[$inc_idx]->{'end'} || $cycleclock >= $incidents->[$inc_idx]{'start'} && $incidents->[$inc_idx]{'end'} >= $cycleclock))
				{
					$eventid = $incidents->[$inc_idx]->{'eventid'};
				}
			}

			my ($proto, $protocol_id);

			# for DNS Service protocol on a whole cycle level does not
			# make sense since it can be different on each probe node
			if ($service ne 'dns')
			{
				$proto = PROTO_TCP;
				$protocol_id = $tcp_protocol_id;
			}

			# SERVICE cycle
			dw_append_csv(DATA_CYCLE, [
					      dw_get_cycle_id($cycleclock, $service_category_id, $tld_id),
					      $cycleclock,
					      $cycle_ref->{'rollweek'},
					      dw_get_id(ID_STATUS_MAP, $cycle_ref->{'status'}),
					      $eventid,
					      $tld_id,
					      $service_category_id,
					      '',
					      '',
					      '',
					      $tld_type_id,
					      $protocol_id // ''	# TODO: no cycle protocol as DNS can be UDP/TCP since DNS Reboot!
			]);

			if ($service eq 'dns')
			{
				# DNS MINNS
				dw_append_csv(DATA_MINNS, [
						      $tld_id,
						      $cycle_ref->{'minns'},
						      $cycleclock
				]);
			}

			foreach my $interface (keys(%{$cycle_ref->{'interfaces'}}))
			{
				my $test_type_id = dw_get_id(ID_TEST_TYPE, $interface);

				foreach my $probe (keys(%{$cycle_ref->{'interfaces'}{$interface}{'probes'}}))
				{
					my $probe_id = dw_get_id(ID_PROBE, $probe);

					my $probe_ref = $cycle_ref->{'interfaces'}{$interface}{'probes'}{$probe};

					if (exists($probe_ref->{'protocol'}))
					{
						$proto =  $probe_ref->{'protocol'};
						$protocol_id = ($proto == PROTO_UDP ? $udp_protocol_id : $tcp_protocol_id);
					}

					my $testedname_id = '';

					if (exists($probe_ref->{'testedname'}))
					{
						$testedname_id = dw_get_id(
							ID_TESTEDNAME,
							$probe_ref->{'testedname'},
						);
					}

					my $testclock = $probe_ref->{'clock'};

					# DNS test details are saved per probe
					if ($service eq 'dns')
					{
						dw_append_csv(DATA_TEST_DETAILS, [
							$probe_id,
							$cycleclock,
							$service_category_id,
							$test_type_id,
							'',
							$testedname_id,
						]) unless ($cycle_ref->{'rawstatus'} == UP_INCONCLUSIVE_RECONFIG);
					}

					foreach my $target (keys(%{$cycle_ref->{'interfaces'}{$interface}{'probes'}{$probe}{'targets'}}))
					{
						my $target_ref = $cycle_ref->{'interfaces'}{$interface}{'probes'}{$probe}{'targets'}{$target};

						my $target_status = ($target_ref->{'status'} eq UP ? $general_status_up : $general_status_down);

						my $cycle_ns_id;

						# non-DNS test details are saved per target
						if ($service ne 'dns')
						{
							dw_append_csv(DATA_TEST_DETAILS, [
								$probe_id,
								$cycleclock,
								$service_category_id,
								$test_type_id,
								dw_get_id(ID_TARGET, $target),
								$testedname_id,
							]) unless ($cycle_ref->{'rawstatus'} == UP_INCONCLUSIVE_RECONFIG);
						}
						else
						{
							$cycle_ns_id = dw_get_id(ID_NS_NAME, $target);
						}

						foreach my $metric_ref (@{$cycle_ref->{'interfaces'}{$interface}{'probes'}{$probe}{'targets'}{$target}{'metrics'}})
						{
							if (!defined($rtt_low) || !defined($rtt_low->{$tld}) || !defined($rtt_low->{$tld}{$service})
								|| !defined($rtt_low->{$tld}{$service}{$proto}))
							{
								$rtt_low->{$tld}{$service}{$proto} = get_rtt_low($service, $proto);	# TODO: add third parameter (command) for EPP!
							}

							my $test_status;

							if (is_service_error($interface, $metric_ref->{'rtt'},
									$rtt_low->{$tld}{$service}{$proto}))
							{
								$test_status = $general_status_up;
							}
							else
							{
								$test_status = $general_status_down;
							}

							my $rtt = $metric_ref->{'rtt'};

							my ($ip, $ip_id, $ip_version_id);

							if ($metric_ref->{'ip'})
							{
								$ip = $metric_ref->{'ip'};
								$ip_id = dw_get_id(ID_NS_IP, $ip);
								$ip_version_id = dw_get_id(ID_IP_VERSION, __ip_version($ip));
							}
							else
							{
								$ip = '';
								$ip_id = '';
								$ip_version_id = '';
							}

							my $cycle_id = dw_get_cycle_id(
								$cycleclock,
								$service_category_id,
								$tld_id,
								$cycle_ns_id // '',
								$ip_id);

							my $nsid_id = (exists($metric_ref->{'nsid'}) ? dw_get_id(ID_NSID, $metric_ref->{'nsid'}) : '');

							# TEST
							dw_append_csv(DATA_TEST, [
									      $probe_id,
									      $cycleclock,
									      $testclock,
									      __format_rtt($rtt),
									      $cycle_id,
									      $tld_id,
									      $protocol_id,
									      $ip_version_id,
									      $ip_id,
									      $test_type_id,
									      $cycle_ns_id // '',
									      $tld_type_id,
									      $nsid_id
							]) unless ($cycle_ref->{'rawstatus'} == UP_INCONCLUSIVE_RECONFIG);

							if ($ip)
							{
								if (!defined($nscycle{$target}) || !defined($nscycle{$target}{$ip}))
								{
									$nscycle{$target}{$ip}{'total'} = 0;
									$nscycle{$target}{$ip}{'positive'} = 0;
								}

								$nscycle{$target}{$ip}{'total'}++;
								$nscycle{$target}{$ip}{'positive'}++ if ($test_status eq $general_status_up);
							}
						}

						if ($cycle_ns_id)
						{
							# Name Server (target) test
							dw_append_csv(DATA_NSTEST, [
									      $probe_id,
									      $cycle_ns_id,
									      $tld_id,
									      $cycleclock,
									      dw_get_id(ID_STATUS_MAP, $target_status),
									      $tld_type_id,
									      $protocol_id
								]) unless ($cycle_ref->{'rawstatus'} == UP_INCONCLUSIVE_RECONFIG);
						}
					}
				}

				if ($interface eq 'dns')
				{
					foreach my $ns (keys(%nscycle))
					{
						foreach my $ip (keys(%{$nscycle{$ns}}))
						{
							#dbg("NS $ns,$ip : positive ", $nscycle{$ns}{$ip}{'positive'}, "/", $nscycle{$ns}{$ip}{'total'});

							my $nscyclestatus;

							if ($cycle_ref->{'status'} ne UP && $cycle_ref->{'status'} ne DOWN)
							{
								# Up-inconclusive-*
								$nscyclestatus = $cycle_ref->{'status'};
							}
							elsif ($nscycle{$ns}{$ip}{'total'} < $services->{$service}->{'minonline'})
							{
								$nscyclestatus = $general_status_up;
							}
							else
							{
								my $perc = $nscycle{$ns}{$ip}{'positive'} * 100 / $nscycle{$ns}{$ip}{'total'};
								$nscyclestatus = ($perc > SLV_UNAVAILABILITY_LIMIT ? $general_status_up : $general_status_down);
							}

							my $ns_id = dw_get_id(ID_NS_NAME, $ns);
							my $ip_id = dw_get_id(ID_NS_IP, $ip);

							if (!defined($nscyclestatus))
							{
								wrn("no status of $interface cycle (", ts_full($cycleclock), ")!");
								next;
							}

							# Name Server availability cycle
							dw_append_csv(DATA_CYCLE, [
									      dw_get_cycle_id($cycleclock, $ns_service_category_id, $tld_id, $ns_id, $ip_id),
									      $cycleclock,
									      '',	# TODO: emergency threshold not yet supported for NS Availability (make sure this fix (0 -> '') exists)
									      dw_get_id(ID_STATUS_MAP, $nscyclestatus),
									      '',	# TODO: incident ID not yet supported for NS Availability
									      $tld_id,
									      $ns_service_category_id,
									      $ns_id,
									      $ip_id,
									      dw_get_id(ID_IP_VERSION, __ip_version($ip)),
									      $tld_type_id,
									      ''	# TODO: no cycle protocol as DNS can be UDP/TCP since DNS Reboot!
								]);
						}
					}
				}
			}
		}

		# incidents
		foreach (@$incidents)
		{
			my $eventid = $_->{'eventid'};
			my $event_start = $_->{'start'};
			my $event_end = $_->{'end'};
			my $failed_tests = $_->{'failed_tests'};
			my $false_positive = $_->{'false_positive'};

			if (opt('debug'))
			{
				dbg("incident id:$eventid start:" . ts_full($event_start) .
					" end:" . ts_full($event_end) . " fp:$false_positive" .
					" failed_tests:" . ($failed_tests // "(null)"));
			}

			# write event that resolves incident
			if ($event_end)
			{
				dw_append_csv(DATA_INCIDENT_END, [
						      $eventid,
						      $event_end,
						      $failed_tests
				]);
			}

			# report only incidents within given period
			if ($event_start >= $from)
			{
				dw_append_csv(DATA_INCIDENT, [
						      $eventid,
						      $event_start,
						      $tld_id,
						      $service_category_id,
						      $tld_type_id
				]);
			}
		}
	}

	$time_process_records = time();
	dw_write_csv_files();
	$time_write_csv = time();
}

sub __ip_version
{
	my $addr = shift;

	return 'IPv6' if ($addr =~ /:/);

	return 'IPv4';
}

sub __get_tld_type
{
	my $tld = shift;

	my $rows_ref = db_select(
		"select g.name".
		" from hosts_groups hg,hstgrp g,hosts h".
		" where hg.groupid=g.groupid".
			" and hg.hostid=h.hostid".
			" and g.name like '%TLD'".
			" and h.host='$tld'");

	if (scalar(@$rows_ref) != 1)
	{
		fail("cannot get type of TLD $tld");
	}

	return $rows_ref->[0]->[0];
}

sub __get_false_positives
{
	my $from = shift;
	my $till = shift;
	my $_server_key = shift;	# if --tld was specified

	my @local_server_keys;

	if ($_server_key)
	{
		push(@local_server_keys, $_server_key)
	}
	else
	{
		@local_server_keys = @server_keys;
	}

	my @result;

	# go through all the databases
	foreach (@local_server_keys)
	{
	$server_key = $_;
	db_connect($server_key);

	# check for possible false_positive changes made in front-end
	my $rows_ref = db_select(
		"select resourceid,note,clock".
		" from auditlog".
		" where resourcetype=" . AUDIT_RESOURCE_INCIDENT.
			" and clock between $from and $till".
		" order by clock");

	foreach my $row_ref (@$rows_ref)
	{
		my $eventid = $row_ref->[0];
		my $note = $row_ref->[1];
		my $clock = $row_ref->[2];

		my $status = ($note =~ m/unmarked/i ? 'Deactivated' : 'Activated');

		push(@result, {'clock' => $clock, 'eventid' => $eventid, 'status' => $status});
	}
	db_disconnect();
	}
	undef($server_key);

	return \@result;
}

# Get ONLINE periods of probe nodes.
#
# Returns hash of probe names as keys and array with online times as values:
#
# {
#     'probe' => [ from1, till1, from2, till2 ... ]
#     ...
# }
#
# NB! If a probe was down for the whole specified period or is currently disabled it won't be in a hash.
sub __get_probe_times($$$)
{
	my $from = shift;
	my $till = shift;
	my $probes_ref = shift;	# {host => {'hostid' => hostid, 'status' => status}, ...}

	my $result = {};

	return $result if (scalar(keys(%{$probes_ref})) == 0);

	my @probes;
	foreach my $probe (keys(%{$probes_ref}))
	{
		next unless ($probes_ref->{$probe}->{'status'} == HOST_STATUS_MONITORED);

		my $status;
		my $prev_status = DOWN;

		for (my $clock = $from; $clock < $till; $clock += PROBE_DELAY)
		{
			$status = probe_online_at($probe, $clock, PROBE_DELAY);

			if ($status == UP && $prev_status == DOWN)
			{
				push(@{$result->{$probe}}, $clock);
			}
			elsif ($status == DOWN && $prev_status == UP)
			{
				push(@{$result->{$probe}}, $clock - 1);	# 00-59
			}

			$prev_status = $status;
		}

		if ($status == UP)
		{
			push(@{$result->{$probe}}, $till);
		}
	}

	if (!defined($result))
	{
		dbg("Probes have no values yet.");
	}

	return $result;
}

sub __get_probe_changes($$)
{
	my $from = shift;
	my $till = shift;

	my @result;

	foreach my $_server_key (sort(keys(%{$probes_data})))
	{
		db_connect($_server_key);

		# cache probe online statuses
		# TODO: FIXME, we have done that already in other processes! (look for this message in this file)
		foreach my $probe (keys(%{$probes_data->{$server_key}}))
		{
			# probe, from, delay
			probe_online_at($probe, $from, ($till + 1 - $from));
		}

		my $probe_times = __get_probe_times($from, $till, $probes_data->{$_server_key});

		db_disconnect();

		foreach my $probe (sort(keys(%{$probe_times})))
		{
			for (my $idx = 0; defined($probe_times->{$probe}[$idx]); $idx++)
			{
				my $clock = $probe_times->{$probe}[$idx];
				my $status = ($idx % 2 == 0 ? PROBE_ONLINE_STR : PROBE_OFFLINE_STR);

				if ($idx == 0 && $clock < $from)
				{
					# ignore previous minute status
					next;
				}

				if ($idx + 1 == scalar(@{$probe_times->{$probe}}) && ($clock % 60 != 0))
				{
					# ignore last second status
					next;
				}

				push(@result, {'probe' => $probe, 'status' => $status, 'clock' => $clock});
			}
		}
	}

	return \@result;
}

sub __format_rtt
{
	my $rtt = shift;

	return "UNDEF" unless (defined($rtt));		# it should never be undefined

	return $rtt unless ($rtt);			# allow empty string (in case of error)

	return int($rtt);
}

sub __print_undef
{
	my $string = shift;

	return (defined($string) ? $string : "UNDEF");
}

# todo: taken from RSMSLV.pm
sub __make_incident
{
	my %h;

	$h{'eventid'} = shift;
	$h{'false_positive'} = shift;
	$h{'start'} = shift;
	$h{'end'} = shift;

	return \%h;
}

# todo: taken from RSMSLV.pm
# todo: NB! Contains the fix to recalculate number of failed tests, see below label "FIX-PH1"
sub __get_incidents2
{
	my $itemid = shift;
	my $delay = shift;
	my $from = shift;
	my $till = shift;

	my (@incidents, $rows_ref, $row_ref);

	$rows_ref = db_select(
		"select distinct t.triggerid".
		" from triggers t,functions f".
		" where t.triggerid=f.triggerid".
			" and t.status<>".TRIGGER_STATUS_DISABLED.
			" and f.itemid=$itemid".
			" and t.priority=".TRIGGER_SEVERITY_NOT_CLASSIFIED);

	my $rows = scalar(@$rows_ref);

	unless ($rows == 1)
	{
		wrn("configuration error: item $itemid must have one not classified trigger (found: $rows)");
		return \@incidents;
	}

	my $triggerid = $rows_ref->[0]->[0];

	my $last_trigger_value = TRIGGER_VALUE_FALSE;

	# FIX-PH1
	use constant SEC_PER_MONTH => 2592000;

	if (defined($from))
	{
		# First check for ongoing incident.

		my $attempts = 5;

		undef($row_ref);

		my $attempt = 0;

		my $clock_till = $from;
		# start FIX-PH1
		#my $clock_from = $clock_till - SEC_PER_WEEK;
		my $clock_from = $clock_till - SEC_PER_MONTH;
		# end FIX-PH1
		$clock_till--;

		while ($attempt++ < $attempts && !defined($row_ref))
		{
			$rows_ref = db_select(
				"select max(clock)".
				" from events".
				" where object=".EVENT_OBJECT_TRIGGER.
					" and source=".EVENT_SOURCE_TRIGGERS.
					" and objectid=$triggerid".
					" and " . sql_time_condition($clock_from, $clock_till));

			$row_ref = $rows_ref->[0];

			$clock_till = $clock_from - 1;
			$clock_from -= (SEC_PER_WEEK * $attempt * 2);
		}

		if (!defined($row_ref))
		{
			$rows_ref = db_select(
				"select max(clock)".
				" from events".
				" where object=".EVENT_OBJECT_TRIGGER.
					" and source=".EVENT_SOURCE_TRIGGERS.
					" and objectid=$triggerid".
					" and clock<$clock_from");

			$row_ref = $rows_ref->[0];
		}

		if (defined($row_ref) and defined($row_ref->[0]))
		{
			my $preincident_clock = $row_ref->[0];

			$rows_ref = db_select(
				"select eventid,clock,value,false_positive".
				" from events".
				" where object=".EVENT_OBJECT_TRIGGER.
					" and source=".EVENT_SOURCE_TRIGGERS.
					" and objectid=$triggerid".
					" and clock=$preincident_clock".
				" order by ns desc".
				" limit 1");

			$row_ref = $rows_ref->[0];

			my $eventid = $row_ref->[0];
			my $clock = $row_ref->[1];
			my $value = $row_ref->[2];
			my $false_positive = $row_ref->[3];

			dbg("reading pre-event $eventid: clock:" . ts_str($clock) . " ($clock), value:", ($value == 0 ? 'OK' : 'PROBLEM'), ", false_positive:$false_positive") if (opt('debug'));

			# do not add 'value=TRIGGER_VALUE_TRUE' to SQL above just for corner case of 2 events at the same second
			if ($value == TRIGGER_VALUE_TRUE)
			{
				push(@incidents, __make_incident($eventid, $false_positive, cycle_start($clock, $delay)));

				$last_trigger_value = TRIGGER_VALUE_TRUE;
			}
		}
	}

	# now check for incidents within given period
	$rows_ref = db_select(
		"select eventid,clock,value,false_positive".
		" from events".
		" where object=".EVENT_OBJECT_TRIGGER.
			" and source=".EVENT_SOURCE_TRIGGERS.
			" and objectid=$triggerid".
			" and ".sql_time_condition($from, $till).
		" order by clock,ns");

	foreach my $row_ref (@$rows_ref)
	{
		my $eventid = $row_ref->[0];
		my $clock = $row_ref->[1];
		my $value = $row_ref->[2];
		my $false_positive = $row_ref->[3];

		dbg("reading event $eventid: clock:" . ts_str($clock) . " ($clock), value:", ($value == 0 ? 'OK' : 'PROBLEM'), ", false_positive:$false_positive") if (opt('debug'));

		# ignore non-resolved false_positive incidents (corner case)
		if ($value == TRIGGER_VALUE_TRUE && $last_trigger_value == TRIGGER_VALUE_TRUE)
		{
			my $idx = scalar(@incidents) - 1;

			if ($incidents[$idx]->{'false_positive'} != 0)
			{
				# replace with current
				$incidents[$idx]->{'eventid'} = $eventid;
				$incidents[$idx]->{'false_positive'} = $false_positive;
				$incidents[$idx]->{'start'} = cycle_start($clock, $delay);
			}
		}

		next if ($value == $last_trigger_value);

		if ($value == TRIGGER_VALUE_FALSE)
		{
			# event that closes the incident
			my $idx = scalar(@incidents) - 1;

			$incidents[$idx]->{'end'} = cycle_end($clock, $delay);

			# start FIX-PH1
			# count failed tests within resolved incident
			my $rows_ref2 = db_select(
				"select count(*)".
				" from history_uint".
				" where itemid=$itemid".
					" and value=".DOWN.
					" and ".sql_time_condition($incidents[$idx]->{'start'}, $incidents[$idx]->{'end'}));
			$incidents[$idx]->{'failed_tests'} = $rows_ref2->[0]->[0];
			# end FIX-PH1
		}
		else
		{
			# event that starts an incident
			push(@incidents, __make_incident($eventid, $false_positive, cycle_start($clock, $delay)));
		}

		$last_trigger_value = $value;
	}

	# DEBUG
	if (opt('debug'))
	{
		foreach (@incidents)
		{
			my $eventid = $_->{'eventid'};
			my $inc_from = $_->{'start'};
			my $inc_till = $_->{'end'};
			my $false_positive = $_->{'false_positive'};

			my $str = "$eventid";
			$str .= " (false positive)" if ($false_positive != 0);
			$str .= ": " . ts_str($inc_from) . " ($inc_from) -> ";
			$str .= $inc_till ? ts_str($inc_till) . " ($inc_till)" : "null";

			dbg($str);
		}
	}

	return \@incidents;
}

sub __get_readable_tld
{
	my $tld = shift;

	return ROOT_ZONE_READABLE if ($tld eq ".");

	return $tld;
}

# todo: taken from RSMSLV.pm
# NB! THIS IS FIXED VERSION WHICH MUST REPLACE EXISTING ONE
# (improved log message)
sub __no_cycle_result($$$)
{
	my $name  = shift;
	my $key   = shift;
	my $clock = shift;

	wrn("$name result is missing for timestamp ", ts_full($clock), ".",
		" This means that either script was not executed or Zabbix server was",
		" not running at that time. In order to fix this problem please connect",
		" to appropriate server (check @<server_key> in the beginning of this message)",
		" and run the following command:");
	wrn("/opt/zabbix/scripts/slv/$key.pl --now $clock");
}

__END__

=head1 NAME

export.pl - export data from Zabbix database in CSV format

=head1 SYNOPSIS

export.pl --date <dd/mm/yyyy> [--warnslow <seconds>] [--dry-run] [--debug] [--probe <name>] [--tld <name>] [--service <name>] [--day <seconds>] [--shift <seconds>] [--help]

=head1 OPTIONS

=over 8

=item B<--date> dd/mm/yyyy

Process data of the specified day. E. g. 01/10/2015 .

=item B<--dry-run>

Print data to the screen, do not write anything to the filesystem.

=item B<--warnslow> seconds

Issue a warning in case an SQL query takes more than specified number of seconds. A floating-point number
is supported as seconds (i. e. 0.5, 1, 1.5 are valid).

=item B<--debug>

Run the script in debug mode. This means printing more information.

=item B<--probe> name

Specify probe name. All other probes will be ignored.

Implies option --dry-run.

=item B<--tld> name

Specify TLD. All other TLDs will be ignored.

Implies option --dry-run.

=item B<--service> name

Specify service. All other services will be ignored. Known services are: dns, dnssec, rdds, epp.

Implies option --dry-run.

=item B<--day> seconds

Specify length of the day in seconds. By default 1 day equals 86400 seconds.

Implies option --dry-run.

=item B<--shift> seconds

Move forward specified number of seconds from the date specified with --date.

Implies option --dry-run.

=item B<--max-children> n

Specify maximum number of child processes to run in parallel.

=item B<--help>

Print a brief help message and exit.

=back

=head1 DESCRIPTION

B<This program> will collect monitoring data from Zabbix database and save it in CSV format in different files.

=head1 EXAMPLES

./export.pl --date 01/10/2015

This will process monitoring data of the 1st of October 2015.

=cut
