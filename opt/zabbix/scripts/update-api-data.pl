#!/usr/bin/env perl

use FindBin;
use lib $FindBin::RealBin;

use strict;
use warnings;

use RSM;
use RSMSLV;
use TLD_constants qw(:ec :api :config :items);
use ApiHelper;
use Parallel::ForkManager;
use Data::Dumper;

use constant JSON_VALUE_UP => 'Up';
use constant JSON_VALUE_DOWN => 'Down';
use constant JSON_VALUE_ALARMED_YES => 'Yes';
use constant JSON_VALUE_ALARMED_NO => 'No';
use constant JSON_VALUE_ALARMED_DISABLED => 'Disabled';

use constant MAX_CONTINUE_PERIOD => 30;	# minutes (NB! make sure to update this number in the help message)

use constant DEFAULT_INCIDENT_MEASUREMENTS_LIMIT => 3600;	# seconds, maximum period back from current time to look
								# back for recent measurement files for an incident

# We must wait for the maximum period when Service Availability and Rolling Week
# values are calculated, sent to the server and saved in the database. Here we
# specify the minimum age of latest cycle we are able to process.
use constant LATEST_CYCLE_AGE	=> 240;	# seconds (must be divisible by 60)

parse_opts(
	'tld=s',
	'service=s',
	'period=i',
	'from=i',
	'continue!',
	'print-period!',
	'ignore-file=s',
	'probe=s',
	'limit=i',
	'max-children=i',
	'server-key=s'
);

# do not write any logs
setopt('nolog');

fail_if_running();

if (opt('debug'))
{
	dbg("command-line parameters:");
	dbg("$_ => ", getopt($_)) foreach (optkeys());
}

__validate_input();	# needs to be connected to db

ah_set_debug(getopt('debug'));

if (!opt('dry-run') && (my $error = rsm_targets_prepare(AH_SLA_API_TMP_DIR, AH_SLA_API_DIR)))
{
	fail($error);
}

my $config = get_rsm_config();
set_slv_config($config);

my $incident_measurements_limit =
		$config->{'sla_api'}->{'incident_measurements_limit'} // DEFAULT_INCIDENT_MEASUREMENTS_LIMIT;

my @server_keys = (opt('server-key') ? getopt('server-key') : get_rsm_server_keys($config));

validate_tld(getopt('tld'), \@server_keys) if (opt('tld'));
validate_service(getopt('service')) if (opt('service'));

my $opt_from = getopt('from');

if (defined($opt_from))
{
	$opt_from = truncate_from($opt_from);	# use the whole minute
	dbg("option \"from\" truncated to the start of a minute: $opt_from") if ($opt_from != getopt('from'));
}

db_connect();
my $monitoring_target = get_monitoring_target();
my $rdap_is_standalone = is_rdap_standalone();
db_disconnect();

dbg("RDAP ", ($rdap_is_standalone ? "is" : "is NOT"), " standalone");

my %services;
if (opt('service'))
{
	$services{lc(getopt('service'))} = undef;
}
else
{
	if ($monitoring_target eq MONITORING_TARGET_REGISTRY)
	{
		$services{'dns'} = undef;
		$services{'dnssec'} = undef;
		$services{'rdds'} = undef;
		$services{'rdap'} = undef if ($rdap_is_standalone);
		$services{'epp'} = undef;
	}
	elsif ($monitoring_target eq MONITORING_TARGET_REGISTRAR)
	{
		$services{'rdds'} = undef;
		$services{'rdap'} = undef if ($rdap_is_standalone);
	}
}

my @interfaces;
foreach my $service (keys(%services))
{
	if ($service eq 'rdds')
	{
		push(@interfaces, 'rdds43', 'rdds80');

		push(@interfaces, 'rdap') if (!$rdap_is_standalone);
	}
	else
	{
		push(@interfaces, $service);
	}
}

my %ignore_hash;

if (opt('ignore-file'))
{
	my $ignore_file = getopt('ignore-file');

	my $handle;
	fail("cannot open ignore file \"$ignore_file\": $!") unless open($handle, '<', $ignore_file);

	chomp(my @lines = <$handle>);

	close($handle);

	%ignore_hash = map { $_ => 1 } @lines;
}

db_connect();
my $cfg_avail_valuemaps = get_avail_valuemaps();
db_disconnect();

my $now = time();

my $max_till = truncate_till(time() - LATEST_CYCLE_AGE);

my ($check_from, $check_till, $continue_file);

if (opt('continue'))
{
	$continue_file = ah_continue_file_name();

	if (! -e $continue_file)
	{
		if (!defined($check_from = __get_config_minclock()))
		{
			info("no data from Probe nodes yet");
			slv_exit(SUCCESS);
		}
	}
	else
	{
		my $handle;

		fail("cannot open continue file $continue_file\": $!") unless (open($handle, '<', $continue_file));

		chomp(my @lines = <$handle>);

		close($handle);

		my $ts = $lines[0];

		if (!$ts)
		{
			# last_update file exists but is empty, this means something went wrong
			fail("The last update file \"$continue_file\" exists but is empty.".
				" Please set the timestamp of last update in it manually and run the script again.");
		}

		dbg("last update time: ", ts_full($ts));

		my $next_ts = $ts + 1;	# continue with the next minute

		$check_from = truncate_from($next_ts);

		if ($check_from != $next_ts)
		{
			wrn(sprintf("truncating last update value (%s) to %s", ts_str($ts), ts_str($check_from)));
		}
	}

	if ($check_from == 0)
	{
		fail("no data from probes in the database yet");
	}

	my $period = (opt('period') ? getopt('period') : MAX_CONTINUE_PERIOD);

	$check_till = $check_from + $period * 60 - 1;
	$check_till = $max_till if ($check_till > $max_till);
}
elsif (opt('from'))
{
	$check_from = $opt_from;
	$check_till = (opt('period') ? $check_from + getopt('period') * 60 - 1 : $max_till);
}
elsif (opt('period'))
{
	# only period specified
	$check_till = $max_till;
	$check_from = $check_till - getopt('period') * 60 + 1;
}

fail("cannot get the beginning of calculation period") unless(defined($check_from));
fail("cannot get the end of calculation period") unless(defined($check_till));

dbg("check_from:", ts_full($check_from), " check_till:", ts_full($check_till), " max_till:", ts_full($max_till));

if ($check_till < $check_from)
{
	info("no new data yet, we are up-to-date");
	slv_exit(SUCCESS);
}

if ($check_till > $max_till)
{
	my $left = ($check_till - $max_till) / 60;
	my $left_str;

	if ($left == 1)
	{
		$left_str = "1 minute";
	}
	else
	{
		$left_str = "$left minutes";
	}

	wrn(sprintf("the specified period (%s) is in the future, please wait for %s",
			selected_period($check_from, $check_till), $left_str));

	slv_exit(SUCCESS);
}

db_connect();
foreach my $service (keys(%services))
{
	$services{$service}{'delay'} = get_dns_delay()  if ($service eq 'dns');
	$services{$service}{'delay'} = get_dns_delay()  if ($service eq 'dnssec');
	$services{$service}{'delay'} = get_rdds_delay() if ($service eq 'rdds');
	$services{$service}{'delay'} = get_rdap_delay() if ($service eq 'rdap');
	$services{$service}{'delay'} = get_epp_delay()  if ($service eq 'epp');

	$services{$service}{'avail_key'} = "rsm.slv.$service.avail";
	$services{$service}{'rollweek_key'} = "rsm.slv.$service.rollweek";

	dbg("$service delay: ", $services{$service}{'delay'});
}
db_disconnect();

my ($from, $till) = get_real_services_period(\%services, $check_from, $check_till);

if (opt('print-period'))
{
	foreach my $service (sort(keys(%services)))
	{
		next if (!defined($services{$service}{'from'}));
		info(sprintf("selected %6s period: %s",
				$service,
				selected_period($services{$service}{'from'}, $services{$service}{'till'})));
	}
}
else
{
	dbg("real services period: ", selected_period($from, $till));
}

if (!$from)
{
	info("no full test periods within specified time range: ", selected_period($check_from, $check_till));

	slv_exit(SUCCESS);
}

my $fm = new Parallel::ForkManager(opt('max-children') ? getopt('max-children') : 64);

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

	dbg("getting probe statuses for period:", selected_period($from, $till));

	db_connect($server_key);

#	my $dns_udp_rtt_high_history = get_history_by_itemid(CONFIGVALUE_DNS_UDP_RTT_HIGH_ITEMID, $from, $till);

	my $all_probes_ref;

	if (opt('probe'))
	{
		my $probe = getopt('probe');

		unless (exists($all_probes_ref->{$probe}))
		{
			my $msg = "unknown probe \"$probe\"\n\nAvailable probes:\n";

			foreach my $name (keys(%$all_probes_ref))
			{
				$msg .= "  $name\n";
			}

			fail($msg);
		}

		$all_probes_ref = get_probes(undef, $probe);
	}
	else
	{
		$all_probes_ref = get_probes();
	}

	my $tlds_processed = 0;

	my $tlds_ref;
	if (opt('tld'))
	{
		if (tld_exists(getopt('tld')) == 0)
		{
			if ($server_keys[-1] eq $server_key)
			{
				# last server in list
				info("TLD ", getopt('tld'), " does not exist.");
				goto WAIT_CHILDREN;
			}

			# try next server
			next;
		}

		$tlds_ref = [ getopt('tld') ];
	}
	else
	{
		$tlds_ref = get_tlds(undef, $till);
	}

	# Prepare the cache for function tld_service_enabled(). Make sure this is called before creating child processes!
	tld_interface_enabled_delete_cache();	# delete cache of previous server
	tld_interface_enabled_create_cache(@interfaces);

	db_disconnect();

	$fm->run_on_wait(
		sub ()
		{
			dbg("max children reached, please wait...");
		}
	);

	foreach my $tld_for_a_child_to_process (@{$tlds_ref})
	{
		goto WAIT_CHILDREN if ($child_failed);	# break from both server and TLD loops

		last if (opt('limit') && $tlds_processed == getopt('limit'));

		my $pid;

		# start a new child and send parent to the next iteration

		if (($pid = $fm->start()))
		{
			$tldmap{$pid} = $tld_for_a_child_to_process;

			next;
		}

		init_process();

		$tld = $tld_for_a_child_to_process;

		$tlds_processed++;

		if (__tld_ignored($tld) == SUCCESS)
		{
			dbg("tld \"$tld\" found in IGNORE list");
		}
		else
		{
			db_connect($server_key);

			my $ah_tld = ah_get_api_tld($tld);

			my $state_file_exists;
			my $json_state_ref;

			# for services that we do not process at this time
			# (e. g. RDDS) keep their current state
			if (ah_read_state(AH_SLA_API_VERSION_1, $tld, \$json_state_ref) != AH_SUCCESS)
			{
				# if there is no state file we need to consider full
				# cycle for each of the services to get correct states

				$state_file_exists = 0;

				if (get_monitoring_target() eq MONITORING_TARGET_REGISTRY)
				{
					$json_state_ref->{'tld'} = $tld;
				}
				elsif (get_monitoring_target() eq MONITORING_TARGET_REGISTRAR)
				{
					$json_state_ref->{'registrarID'} = $tld;
				}

				$json_state_ref->{'testedServices'} = {};
			}
			else
			{
				$state_file_exists = 1;
			}

			# find out which services are disabled, for others get lastclock
			foreach my $service (keys(%services))
			{
				my $service_from = $services{$service}{'from'};
				my $service_till = $services{$service}{'till'};

				my $delay = $services{$service}{'delay'};

				my $avail_key = $services{$service}{'avail_key'};
				my $rollweek_key = $services{$service}{'rollweek_key'};

				# not the right time for this service/delay yet
				if (!$service_from || !$service_till)
				{
					next unless ($state_file_exists == 0);

					dbg("$service: there is no state file, consider previous cycle");

					# but since there is no state file we need to consider previous cycle
					$service_from = cycle_start($till - $delay, $delay);
					$service_till = cycle_end($till - $delay, $delay);
				}

				if (!tld_service_enabled($tld, $service, $service_till))
				{
					if (opt('dry-run'))
					{
						__prnt(uc($service), " DISABLED");
					}
					else
					{
						if (ah_save_alarmed(
								AH_SLA_API_VERSION_2,
								$ah_tld,
								$service,
								JSON_VALUE_ALARMED_DISABLED) != AH_SUCCESS)
						{
							fail("cannot save alarmed: ", ah_get_error());
						}

						if ($service ne 'rdap')
						{
							if (ah_save_alarmed(
									AH_SLA_API_VERSION_1,
									$ah_tld,
									$service,
									JSON_VALUE_ALARMED_DISABLED) != AH_SUCCESS)
							{
								fail("cannot save alarmed: ", ah_get_error());
							}
						}

						$json_state_ref->{'testedServices'}->{uc($service)} = JSON_OBJECT_DISABLED_SERVICE;
					}

					next;
				}

				my $lastclock_key = $services{$service}{'rollweek_key'};

				dbg("tld:$tld lastclock_key:$lastclock_key value_type:", ITEM_VALUE_TYPE_FLOAT);

				my $lastclock = get_lastclock($tld, $lastclock_key, ITEM_VALUE_TYPE_FLOAT);

				if ($lastclock == E_FAIL)
				{
					wrn(uc($service), ": configuration error, item $lastclock_key not found");

					if (opt('dry-run'))
					{
						__prnt(uc($service), " UP (configuration error)");
					}
					else
					{
						if (ah_save_alarmed(
								AH_SLA_API_VERSION_2,
								$ah_tld,
								$service,
								JSON_VALUE_ALARMED_NO) != AH_SUCCESS)
						{
							fail("cannot save alarmed: ", ah_get_error());
						}

						if ($service ne 'rdap')
						{
							if (ah_save_alarmed(
									AH_SLA_API_VERSION_1,
									$ah_tld,
									$service,
									JSON_VALUE_ALARMED_NO) != AH_SUCCESS)
							{
								fail("cannot save alarmed: ", ah_get_error());
							}
						}

						$json_state_ref->{'testedServices'}->{uc($service)} = {
							'status' => JSON_VALUE_UP,
							'emergencyThreshold' => 0,
							'incidents' => []
						};
					}

					next;
				}

				if ($lastclock == 0)
				{
					wrn(uc($service), ": no rolling week data in the database yet");

					if (opt('dry-run'))
					{
						__prnt(uc($service), " UP (no rolling week data in the database)");
					}
					else
					{
						if (ah_save_alarmed(
								AH_SLA_API_VERSION_2,
								$ah_tld,
								$service,
								JSON_VALUE_ALARMED_NO) != AH_SUCCESS)
						{
							fail("cannot save alarmed: ", ah_get_error());
						}

						if ($service ne 'rdap')
						{
							if (ah_save_alarmed(
									AH_SLA_API_VERSION_1,
									$ah_tld,
									$service,
									JSON_VALUE_ALARMED_NO) != AH_SUCCESS)
							{
								fail("cannot save alarmed: ", ah_get_error());
							}
						}

						$json_state_ref->{'testedServices'}->{uc($service)} = {
							'status' => JSON_VALUE_UP,
							'emergencyThreshold' => 0,
							'incidents' => []
						};
					}

					next;
				}

				dbg("lastclock:$lastclock");

				my $hostid = get_hostid($tld);
				my $avail_itemid = get_itemid_by_hostid($hostid, $avail_key);

				if ($avail_itemid < 0)
				{
					if ($avail_itemid == E_ID_NONEXIST)
					{
						wrn("configuration error: service $service enabled but item \"$avail_key\" not found");
					}
					elsif ($avail_itemid == E_ID_MULTIPLE)
					{
						wrn("configuration error: multiple items with key \"$avail_key\" found");
					}
					else
					{
						wrn("cannot get ID of $service item ($avail_key): unknown error");
					}

					if (opt('dry-run'))
					{
						__prnt(uc($service), " UP (configuration error)");
					}
					else
					{
						if (ah_save_alarmed(
								AH_SLA_API_VERSION_2,
								$ah_tld,
								$service,
								JSON_VALUE_ALARMED_NO) != AH_SUCCESS)
						{
							fail("cannot save alarmed: ", ah_get_error());
						}

						if ($service ne 'rdap')
						{
							if (ah_save_alarmed(
									AH_SLA_API_VERSION_1,
									$ah_tld,
									$service,
									JSON_VALUE_ALARMED_NO) != AH_SUCCESS)
							{
								fail("cannot save alarmed: ", ah_get_error());
							}
						}

						$json_state_ref->{'testedServices'}->{uc($service)} = {
							'status' => JSON_VALUE_UP,
							'emergencyThreshold' => 0,
							'incidents' => []
						};
					}

					next;
				}

				my $rollweek_itemid = get_itemid_by_hostid($hostid, $rollweek_key);

				if ($rollweek_itemid < 0)
				{
					if ($rollweek_itemid == E_ID_NONEXIST)
					{
						wrn("configuration error: service $service enabled but item \"$rollweek_key\" not found");
					}
					elsif ($rollweek_itemid == E_ID_MULTIPLE)
					{
						wrn("configuration error: multiple items with key \"$rollweek_key\" found");
					}
					else
					{
						wrn("cannot get ID of $service item ($rollweek_key): unknown error");
					}

					if (opt('dry-run'))
					{
						__prnt(uc($service), " UP (configuration error)");
					}
					else
					{
						if (ah_save_alarmed(
								AH_SLA_API_VERSION_2,
								$ah_tld,
								$service,
								JSON_VALUE_ALARMED_NO) != AH_SUCCESS)
						{
							fail("cannot save alarmed: ", ah_get_error());
						}

						if ($service ne 'rdap')
						{
							if (ah_save_alarmed(
									AH_SLA_API_VERSION_1,
									$ah_tld,
									$service,
									JSON_VALUE_ALARMED_NO) != AH_SUCCESS)
							{
								fail("cannot save alarmed: ", ah_get_error());
							}
						}

						$json_state_ref->{'testedServices'}->{uc($service)} = {
							'status' => JSON_VALUE_UP,
							'emergencyThreshold' => 0,
							'incidents' => []
						};
					}

					next;
				}

				# we need down time in minutes, not percent, that's why we can't use "rsm.slv.$service.rollweek" value
				my ($rollweek_from, $rollweek_till) = get_rollweek_bounds($delay, $service_till);

				my $rollweek_incidents = get_incidents($avail_itemid, $delay, $rollweek_from, $rollweek_till);

				my $downtime = get_downtime($avail_itemid, $rollweek_from, $rollweek_till, 0, $rollweek_incidents, $delay);

				__prnt(uc($service), " period: ", selected_period($service_from, $service_till)) if (opt('dry-run') or opt('debug'));

				if (opt('dry-run'))
				{
					__prnt(uc($service), " downtime: $downtime (", ts_str($lastclock), ")");
				}
				else
				{
					if (ah_save_downtime(
							AH_SLA_API_VERSION_2,
							$ah_tld,
							$service,
							$downtime,
							$lastclock) != AH_SUCCESS)
					{
						fail("cannot save downtime: ", ah_get_error());
					}

					if ($service ne 'rdap')
					{
						if (ah_save_downtime(
								AH_SLA_API_VERSION_1,
								$ah_tld,
								$service,
								$downtime,
								$lastclock) != AH_SUCCESS)
						{
							fail("cannot save downtime: ", ah_get_error());
						}
					}
				}

				dbg("getting current $service service availability (delay:$delay)");

				# get alarmed
				my $incidents = get_incidents($avail_itemid, $delay, $now);

				my $alarmed_status;

				if (scalar(@$incidents) != 0 && $incidents->[0]->{'false_positive'} == 0 &&
						!defined($incidents->[0]->{'end'}))
				{
					$alarmed_status = JSON_VALUE_ALARMED_YES;
				}
				else
				{
					$alarmed_status = JSON_VALUE_ALARMED_NO;
				}

				if (opt('dry-run'))
				{
					__prnt(uc($service), " alarmed:$alarmed_status");
				}
				else
				{
					if (ah_save_alarmed(
							AH_SLA_API_VERSION_2,
							$ah_tld,
							$service,
							$alarmed_status,
							$lastclock) != AH_SUCCESS)
					{
						fail("cannot save alarmed: ", ah_get_error());
					}

					if ($service ne 'rdap')
					{
						if (ah_save_alarmed(
								AH_SLA_API_VERSION_1,
								$ah_tld,
								$service,
								$alarmed_status,
								$lastclock) != AH_SUCCESS)
						{
							fail("cannot save alarmed: ", ah_get_error());
						}
					}					
				}

				my $rollweek;
				if (get_lastvalue($rollweek_itemid, ITEM_VALUE_TYPE_FLOAT, \$rollweek, undef) != SUCCESS)
				{
					wrn(uc($service), ": no rolling week data in the database yet");

					if (opt('dry-run'))
					{
						__prnt(uc($service), " UP (no rolling week data in the database)");
					}
					else
					{
						if (ah_save_alarmed(
								AH_SLA_API_VERSION_2,
								$ah_tld,
								$service,
								JSON_VALUE_ALARMED_NO) != AH_SUCCESS)
						{
							fail("cannot save alarmed: ", ah_get_error());
						}

						if ($service ne 'rdap')
						{
							if (ah_save_alarmed(
									AH_SLA_API_VERSION_1,
									$ah_tld,
									$service,
									JSON_VALUE_ALARMED_NO) != AH_SUCCESS)
							{
								fail("cannot save alarmed: ", ah_get_error());
							}
						}

						$json_state_ref->{'testedServices'}->{uc($service)} = {
							'status' => JSON_VALUE_UP,
							'emergencyThreshold' => 0,
							'incidents' => []
						};
					}

					next;
				}

				my $latest_avail_select = db_select(
						"select value from history_uint" .
							" where itemid=$avail_itemid" .
							" and clock<=$service_till" .
						" order by clock desc limit 1");

				my $latest_avail_value = scalar(@{$latest_avail_select}) == 0 ?
						UP_INCONCLUSIVE_NO_DATA : $latest_avail_select->[0]->[0];

				if (opt('dry-run'))
				{
					unless (exists($cfg_avail_valuemaps->{int($latest_avail_value)}))
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

						wrn("unknown availability result: $latest_avail_value (expected $expected_list)");
					}
				}

				$json_state_ref->{'testedServices'}->{uc($service)} = {
					'status' => get_result_string($cfg_avail_valuemaps, $latest_avail_value),
					'emergencyThreshold' => $rollweek,
					'incidents' => []
				};

				foreach my $incident (@{get_incidents($avail_itemid, $delay, $service_from, $service_till)})
				{
					my $eventid = $incident->{'eventid'};
					my $event_start = $incident->{'start'};
					my $event_end = $incident->{'end'};
					my $false_positive = $incident->{'false_positive'};
					my $event_clock = $incident->{'event_clock'};

					my $start = (defined($service_from) && ($service_from > $event_start) ?
							$service_from : $event_start);

					if (opt('dry-run'))
					{
						__prnt(uc($service), " incident id:$eventid start:", ts_str($event_start), " end:" . ($event_end ? ts_str($event_end) : "ACTIVE") . " fp:$false_positive");
					}
					else
					{
						if (ah_save_incident(
								AH_SLA_API_VERSION_2,
								$ah_tld,
								$service,
								$eventid,
								$event_clock,
								$event_start,
								$event_end,
								$false_positive,
								$lastclock) != AH_SUCCESS)
						{
							fail("cannot save incident: ", ah_get_error());
						}

						if ($service ne 'rdap')
						{
							if (ah_save_incident(
									AH_SLA_API_VERSION_1,
									$ah_tld,
									$service,
									$eventid,
									$event_clock,
									$event_start,
									$event_end,
									$false_positive,
									$lastclock) != AH_SUCCESS)
							{
								fail("cannot save incident: ", ah_get_error());
							}
						}
					}

					my $recent_json;

					# Check if we have missing measurement files for processed incident.
					# Don't go back further than $incident_measurements_limit.

					my $limit = cycle_start($now - $incident_measurements_limit, $delay);

					my $clock = ($event_start > $limit ? $event_start : $limit);

					while ($clock < ($event_end // $service_till))
					{
						# wait for 30 seconds at most until measurement file appears
						my $max_wait = time() + 30;

						while (1)
						{
							if (ah_copy_measurement(
									AH_SLA_API_VERSION_2,
									$ah_tld,
									$service,
									$clock,
									$eventid,
									$event_clock) != AH_SUCCESS)
							{
								if (time() > $max_wait)
								{
									fail("missing $service measurement for ",
										ts_str($clock), ": ",
										ah_get_error());
								}
								else
								{
									dbg("missing $service measurement for ",
										ts_str($clock), ", waiting...");

									sleep(1);
								}
							}
							else
							{
								last;
							}
						}

						if ($service ne 'rdap')
						{
							while (1)
							{
								if (ah_copy_measurement(
										AH_SLA_API_VERSION_1,
										$ah_tld,
										$service,
										$clock,
										$eventid,
										$event_clock) != AH_SUCCESS)
								{
									if (time() > $max_wait)
									{
										fail("missing $service measurement for ",
											ts_str($clock), ": ",
											ah_get_error());
									}
									else
									{
										dbg("missing $service measurement for ",
											ts_str($clock), ", waiting...");

										sleep(1);
									}
								}
								else
								{
									last;
								}
							}
						}

						$clock += $delay;
					}
				} # foreach my $incident (...)

				foreach my $rolling_week_incident (@{$rollweek_incidents})
				{
					push(
						@{$json_state_ref->{'testedServices'}->{uc($service)}->{'incidents'}},
						ah_create_incident_json(
							$rolling_week_incident->{'eventid'},
							$rolling_week_incident->{'start'},
							$rolling_week_incident->{'end'},
							$rolling_week_incident->{'false_positive'}
						)
					);
				}
			} # foreach my $service

			# finally, set TLD state
			$json_state_ref->{'status'} = JSON_VALUE_UP;
			foreach my $service (values(%{$json_state_ref->{'testedServices'}}))
			{
				if ($service->{'status'} eq JSON_VALUE_DOWN)
				{
					$json_state_ref->{'status'} = JSON_VALUE_DOWN;
					last;
				}
			}

			if (ah_save_state(AH_SLA_API_VERSION_2, $ah_tld, $json_state_ref) != AH_SUCCESS)
			{
				fail("cannot save TLD state: ", ah_get_error());
			}

			# version 1 has no standalone RDAP
			delete($json_state_ref->{'testedServices'}{'RDAP'});

			# version 1 has no Up-inconclusive-reconfig
			foreach my $service (values(%{$json_state_ref->{'testedServices'}}))
			{
				if ($service->{'status'} eq 'Up-inconclusive-reconfig')
				{
					$service->{'status'} = 'Up-inconclusive-no-data';
				}
			}

			if (ah_save_state(AH_SLA_API_VERSION_1, $ah_tld, $json_state_ref) != AH_SUCCESS)
			{
				fail("cannot save TLD state: ", ah_get_error());
			}
		}

		finalize_process();

		# When we fork for real it makes no difference for Parallel::ForkManager whether child calls exit() or
		# calls $fm->finish(), therefore we do not need to introduce $fm->finish() in all our low-level error
		# handling routines, but having $fm->finish() here leaves a possibility to debug a happy path scenario
		# without the complications of actual forking by using:
		# my $fm = new Parallel::ForkManager(0);

		$fm->finish(SUCCESS);
	} # for each TLD

	# unset TLD (for the logs)
	undef($tld);

	$fm->run_on_wait(undef);	# unset the callback which prints debug message about reached children limit

	$fm->wait_all_children();

	db_connect($server_key);

	if (!opt('dry-run') && !opt('tld'))
	{
		__update_false_positives();
	}

	db_disconnect();

	last if (opt('tld'));
} # foreach (@server_keys)
undef($server_key);

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

slv_exit(E_FAIL) if ($child_failed);

# at this point there should be no child processes so we do not care about locking

if (!opt('dry-run'))
{
	if (defined($continue_file))
	{
		if (ah_save_continue_file($till) != AH_SUCCESS)
		{
			fail("cannot save continue file: ", ah_get_error());
		}

		dbg("last update: ", ts_str($till));
	}

	my $continue_file_lock;
	ah_lock_continue_file(\$continue_file_lock);
	my $error = rsm_targets_apply();
	ah_unlock_continue_file($continue_file_lock);

	if ($error)
	{
		fail($error);
	}
}

slv_exit(SUCCESS);

sub __wait_all_children_cb
{
	return if ($fm->is_child());

	$fm->wait_all_children();
}

sub __prnt
{
	my $server_str = ($server_key ? "\@$server_key " : "");
	print($server_str, (defined($tld) ? "$tld: " : ''), join('', @_), "\n");
}

sub __tld_ignored
{
	my $tld = shift;

	return SUCCESS if (exists($ignore_hash{$tld}));

	return E_FAIL;
}

sub __update_false_positives
{
	my $last_audit = ah_get_last_audit($server_key);

	# now check for possible false_positive change in front-end
	my $maxclock = 0;

	# should we update false positiveness later? (incident state file does not exist yet)
	my $later = 0;
	# select resourceid,note,clock from auditlog where resourcetype=32 and clock>0 order by clock;
	my $rows_ref = db_select(
		"select resourceid,note,clock".
		" from auditlog".
		" where resourcetype=".AUDIT_RESOURCE_INCIDENT.
			" and clock>$last_audit".
		" order by clock");

	foreach my $row_ref (@$rows_ref)
	{
		my $eventid = $row_ref->[0];
		my $note = $row_ref->[1];
		my $clock = $row_ref->[2];

		if ($eventid == 0)
		{
			$eventid = $note;
			$eventid =~ s/^([0-9]+): .*/$1/;
		}

		$maxclock = $clock if ($clock > $maxclock);

		my $rows_ref2 = db_select("select objectid,clock,false_positive from events where eventid=$eventid");

		if (scalar(@$rows_ref2) != 1)
		{
			wrn("looks like event ID $eventid found in auditlog does not exist any more");
			next;
		}

		my $triggerid = $rows_ref2->[0]->[0];
		my $event_clock = $rows_ref2->[0]->[1];
		my $false_positive = $rows_ref2->[0]->[2];

		my ($tld, $service) = get_tld_by_trigger($triggerid);

		if (!$tld)
		{
			dbg("looks like trigger ID $triggerid found in auditlog does not exist any more");
			next;
		}

		dbg("auditlog: service:$service eventid:$eventid start:[".ts_str($event_clock)."] changed:[".ts_str($clock)."] false_positive:$false_positive");

		my $ah_tld = ah_get_api_tld($tld);

		unless (ah_save_false_positive(
				AH_SLA_API_VERSION_2,
				$ah_tld,
				$service,
				$eventid,
				$event_clock,
				$false_positive,
				$clock,
				\$later) == AH_SUCCESS)
		{
			if ($later == 1)
			{
				wrn(ah_get_error());
			}
			else
			{
				fail("cannot update false_positive state: ", ah_get_error());
			}
		}

		if ($service ne 'rdap')
		{
			unless (ah_save_false_positive(
					AH_SLA_API_VERSION_1,
					$ah_tld,
					$service,
					$eventid,
					$event_clock,
					$false_positive,
					$clock,
					\$later) == AH_SUCCESS)
			{
				if ($later == 1)
				{
					wrn(ah_get_error());
				}
				else
				{
					fail("cannot update false_positive state: ", ah_get_error());
				}
			}
		}
	}

	# If the "later" flag is non-zero it means the incident for which we would like to change
	# false positiveness was not processed yet and there is no incident state file. We cannot
	# modify falsePositive file without making sure incident state file is also updated.
	if ($maxclock != 0 && $later == 0)
	{
		ah_save_audit($server_key, $maxclock);
	}
}

sub __validate_input
{
	if (opt('tld') and opt('ignore-file'))
	{
		print("Error: options --tld and --ignore-file cannot be used together\n");
		usage();
	}

	if (opt('continue') and opt('from'))
        {
                print("Error: options --continue and --from cannot be used together\n");
                usage();
        }

	if (opt('probe'))
	{
		if (not opt('dry-run'))
		{
			print("Error: option --probe can only be used together with --dry-run\n");
			usage();
		}
        }
}

# this function was modified to allow earlier run on freshly installed database
sub __get_config_minclock
{
	my $minclock;

	foreach (@server_keys)
	{
		$server_key = $_;
		db_connect($server_key);

		my $rows_ref = db_select(
				"select min(clock)".
				" from history_uint".
				" where itemid in".
					" (select itemid".
					" from items".
					" where key_='" . PROBE_KEY_ONLINE.
						"' and templateid is not null)");

		next unless (defined($rows_ref->[0]->[0]));

		my $newclock = int($rows_ref->[0]->[0]);
		dbg("min(clock): $newclock");

		$minclock = $newclock if (!defined($minclock) || $newclock < $minclock);
		db_disconnect();
	}
	undef($server_key);

	return undef if (!defined($minclock));

	dbg("oldest data found: ", ts_full($minclock));

	return truncate_from($minclock);
}

__END__

=head1 NAME

update-api-data.pl - save information about the incidents to a filesystem

=head1 SYNOPSIS

update-api-data.pl [--service <dns|dnssec|rdds|rdap|epp>] [--tld <tld>|--ignore-file <file>] [--from <timestamp>|--continue] [--print-period] [--period minutes] [--dry-run [--probe name]] [--warnslow <seconds>] [--debug] [--help]

=head1 OPTIONS

=over 8

=item B<--service> service

Process only specified service. Service must be one of: dns, dnssec, rdds, rdap or epp.

=item B<--tld> tld

Process only specified TLD. If not specified all TLDs will be processed.

This option cannot be used together with option --ignore-file.

=item B<--ignore-file> file

Specify file containing the list of TLDs that should be ignored. TLDs are specified one per line.

This option cannot be used together with option --tld.

=item B<--period> minutes

Specify number minutes of the period to handle during this run. The first cycle to handle can be specified
using options --from or --continue (continue from the last time when --continue was used) (see below).

=item B<--from> timestamp

Specify Unix timestamp within the oldest test cycle to handle in this run. You don't need to specify the
first second of the test cycle, any timestamp within it will work. Number of test cycles to handle within
this run can be specified using option --period otherwise all completed test cycles available in the
database up till now will be handled.

This option cannot be used together with option --continue.

=item B<--continue>

Continue calculation from the timestamp of the last run with --continue. In case of first run with
--continue the oldest available data will be used as starting point. You may specify the end point
of the period with --period option (see above). Default end point is as much data as available in the database
but not more than 30 minutes.

Note, that continue token is not updated if this option was specified together with --dry-run or when you use
--from option.

=item B<--print-period>

Print selected period on the screen.

=item B<--probe> name

Only calculate data from specified probe.

This option can only be used for debugging purposes and must be used together with option --dry-run .

=item B<--dry-run>

Print data to the screen, do not write anything to the filesystem.

=item B<--warnslow> seconds

Issue a warning in case an SQL query takes more than specified number of seconds. A floating-point number
is supported as seconds (i. e. 0.5, 1, 1.5 are valid).

=item B<--server-key> key

Specify the key of the server to handle (e. g. server_2). It must be listed in rsm.conf .

=item B<--max-children> n

Specify maximum number of child processes to run in parallel.

=item B<--debug>

Run the script in debug mode. This means printing more information.

=item B<--help>

Print a brief help message and exit.

=back

=head1 DESCRIPTION

B<This program> will run through all the incidents found at optionally specified time bounds
and store details about each on the filesystem. This information will be used by external
program to provide it for users in convenient way.

=head1 EXAMPLES

./update-api-data.pl --tld example --period 10

This will update API data of the last 10 minutes of DNS, DNSSEC, RDDS, RDAP and EPP services of TLD example.

=cut
