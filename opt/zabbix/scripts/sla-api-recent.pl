#!/usr/bin/perl

use strict;
use warnings;

use Path::Tiny;
use lib path($0)->parent->realpath()->stringify();

use Data::Dumper;

use RSM;
use RSMSLV;
use TLD_constants qw(:api :config :groups :items);
use ApiHelper;

$Data::Dumper::Terse = 1;	# do not output names like "$VAR1 = "
$Data::Dumper::Pair = " : ";	# use separator instead of " => "
$Data::Dumper::Useqq = 1;	# use double quotes instead of single quotes
$Data::Dumper::Indent = 1;	# 1 provides less indentation instead of 2

use constant SLV_UNAVAILABILITY_LIMIT => 49;

use constant TARGET_PLACEHOLDER => 'TARGET_PLACEHOLDER';	# for non-DNS services

use constant MAX_PERIOD => 30 * 60;	# 30 minutes

sub cycles_to_calculate($$$$$$);
sub get_lastvalues_from_db($$);
sub calculate_cycle($$$$$$$$);
sub get_interfaces($$$);
sub probe_online_at_init();
sub get_history_by_itemid($$$);

parse_opts('tld=s', 'service=s', 'server-id=i', 'now=i', 'period=i', 'print-period!');

setopt('nolog');

usage() if (opt('help'));

exit_if_running();	# exit with 0 exit code

if (opt('debug'))
{
	dbg("command-line parameters:");
	dbg("$_ => ", getopt($_)) foreach (optkeys());
}

ah_set_debug(getopt('debug'));

my $config = get_rsm_config();

set_slv_config($config);

my @server_keys;

if (opt('server-id'))
{
	push(@server_keys, get_rsm_server_key(getopt('server-id')));
}
else
{
	@server_keys = get_rsm_server_keys($config);
}

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

my %service_keys = (
	'dns' => 'rsm.slv.dns.avail',
	'dnssec' => 'rsm.slv.dnssec.avail',
	'rdds' => 'rsm.slv.rdds.avail'
);

db_disconnect();

my %rtt_limits;

foreach (@server_keys)
{
	$server_key = $_;

	# Last values actually contain lastclock of the items in "lastvalue" table.
	# 'cycles' represents cycles to recalculate for tld-service pair.
	#
	# {
	#     tld => {
	#         service => {
	#            'probes' => {
	#                 probe => {
	#                     itemid = {
	#                         'key' => key,
	#                         'value_type' => value_type,
	#                         'clock' => clock
	#                     }
	#                 }
	#             },
	#             'cycles' => {
	#                 clock => 1
	#             },
	#             'lastclock' => clock
	#         }
	#     }
	# }
	my %lastvalues;

	my $lastvalues_cache = {};

	if (ah_get_recent_cache($server_key, \$lastvalues_cache) != AH_SUCCESS)
	{
		dbg("there's no recent measurements cache file yet, but no worries");
	}

	db_connect($server_key);

	# initialize probe online cache
	probe_online_at_init();

	get_lastvalues_from_db(\%lastvalues, \%delays);

	# probes available for every service
	my %probes;

	foreach (sort(keys(%lastvalues)))
	{
		$tld = $_;	# global variable

		next if (opt('tld') && $tld ne getopt('tld'));

		foreach my $service (sort(keys(%{$lastvalues{$tld}})))
		{
			next if (opt('service') && $service ne getopt('service'));

			# get actual cycle times to calculate
			my @cycles_to_calculate = cycles_to_calculate(
				$tld,
				$service,
				$delays{$service},
				$service_keys{$service},
				\%lastvalues,
				$lastvalues_cache
			);

			next if (scalar(@cycles_to_calculate) == 0);
			next unless (tld_service_enabled($tld, $service, $cycles_to_calculate[0]));

			if (opt('print-period'))
			{
				info("selected $service period: ", selected_period(
					$cycles_to_calculate[0],
					cycle_end($cycles_to_calculate[-1], $delays{$service})
				));
			}

			my $interfaces_ref = get_interfaces($tld, $service, $now);

			$probes{$service} = get_probes($service) unless (defined($probes{$service}));

			if ($service eq 'dns' && scalar(@cycles_to_calculate) != 0)
			{
				# rtt limits only considered for DNS currently
				$rtt_limits{'dns'} = get_history_by_itemid(
					CONFIGVALUE_DNS_UDP_RTT_HIGH_ITEMID,
					$cycles_to_calculate[0],
					$cycles_to_calculate[-1]
				);
			}

			# these are cycles we are going to recalculate for this tld-service
			foreach my $clock (@cycles_to_calculate)
			{
				calculate_cycle(
					$tld,
					$service,
					$lastvalues{$tld}{$service}{'probes'},
					$clock,
					$delays{$service},
					$rtt_limits{$service},
					$probes{$service},
					$interfaces_ref
				);

				$lastvalues{$tld}{$service}{'lastclock'} = $clock;
			}

			# TODO: no need to have these in a cache but this could be done in a more elegant way
			undef($lastvalues{$tld}{$service}{'probes'});
			undef($lastvalues{$tld}{$service}{'cycles'});
		}

		# DNSSEC is using DNS data
		undef($lastvalues{$tld}{'dnssec'}{'probes'});
		undef($lastvalues{$tld}{'dnssec'}{'cycles'});
	}

	if (!opt('dry-run') && !opt('now'))
	{
		if (ah_save_recent_cache($server_key, \%lastvalues) != AH_SUCCESS)
		{
			fail("cannot save recent measurements cache: ", ah_get_error());
		}
	}

	db_disconnect();
}

# keep to avoid reading multiple times
my $global_lastclock;

sub cycles_to_calculate($$$$$$)
{
	my $tld = shift;
	my $service = shift;
	my $delay = shift;
	my $service_key = shift;
	my $lastvalues = shift;
	my $lastvalues_cache = shift;

	my @cycles;

	if (!opt('now') && defined($lastvalues_cache->{$tld}{$service}{'lastclock'}))
	{
		my $lastclock = $lastvalues_cache->{$tld}{$service}{'lastclock'};

		dbg("using last clock from previous run: $lastclock");

		$lastclock += $delay;

		my $max_clock = $lastclock + $max_period;

		while ($lastclock < $max_clock && $lastclock <= $lastvalues->{$tld}{$service}{'lastclock'})
		{
			push(@cycles, $lastclock);

			$lastclock += $delay;
		}

		return @cycles;
	}

	if (!defined($global_lastclock))
	{
		if (opt('now'))
		{
			$global_lastclock //= cycle_start(getopt('now'), $delay);

			dbg("using specified last clock: $global_lastclock");
		}
		else
		{
			# see if we have last_update.txt in SLA API directory

			my $continue_file //= ah_get_continue_file();

			my $error;

			if (-e $continue_file)
			{
				if (read_file($continue_file, \$global_lastclock, \$error) != SUCCESS)
				{
					fail("cannot read file \"$continue_file\": $error");
				}

				while (chomp($global_lastclock)) {}

				$global_lastclock++;

				dbg("using last clock from SLA API directory, file $continue_file: $global_lastclock");
			}
			else
			{
				# if not, get the oldest from the database
				$global_lastclock //= get_oldest_clock($tld, $service_key, ITEM_VALUE_TYPE_UINT64);

				fail("unexpected error: item \"$service_key\" not found on TLD $tld") if ($global_lastclock == E_FAIL);
				fail("cannot yet calculate, no data in the database yet") if ($global_lastclock == 0);

				dbg("using last clock from the database: $global_lastclock");
			}
		}
	}

	my $lastclock = $global_lastclock;
	my $max_clock = $lastclock + $max_period;

	while ($lastclock < $max_clock && $lastclock < $real_now)
	{
		push(@cycles, $lastclock);

		$lastclock += $delay;
	}

	return @cycles;
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

	fail("internal error") unless ($timestamp);

	# TODO implement binary search

	my $value;

	foreach my $row (@{$history})
	{
		last if ($timestamp < $row->[0]);	# stop iterating if history clock overshot the timestamp
	}
	continue
	{
		$value = $row->[1];	# keep the value preceeding overshooting
	}

	fail("timestamp $timestamp is out of bounds of selected historical data range") unless (defined($value));

	return $value;
}

sub get_service_from_key($)
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

sub get_lastvalues_from_db($$)
{
	my $lastvalues = shift;
	my $delays = shift;

	# join lastvalue and lastvalue_str tables
	my $rows_ref = db_select(
		"select h.host,i.itemid,i.key_,i.value_type,l.clock".
		" from lastvalue l,items i,hosts h,hosts_groups g".
		" where g.hostid=h.hostid".
			" and h.hostid=i.hostid".
			" and i.itemid=l.itemid".
			" and i.type in (".ITEM_TYPE_SIMPLE.",".ITEM_TYPE_TRAPPER.")".
			" and i.status=".ITEM_STATUS_ACTIVE.
			" and h.status=".HOST_STATUS_MONITORED.
			" and g.groupid=".TLD_PROBE_RESULTS_GROUPID.
		" union ".
		"select h.host,i.itemid,i.key_,i.value_type,l.clock".
		" from lastvalue_str l,items i,hosts h,hosts_groups g".
		" where g.hostid=h.hostid".
			" and h.hostid=i.hostid".
			" and i.itemid=l.itemid".
			" and i.type in (".ITEM_TYPE_SIMPLE.",".ITEM_TYPE_TRAPPER.")".
			" and i.status=".ITEM_STATUS_ACTIVE.
			" and h.status=".HOST_STATUS_MONITORED.
			" and g.groupid=".TLD_PROBE_RESULTS_GROUPID
	);

	foreach my $row_ref (@{$rows_ref})
	{
		my $host = $row_ref->[0];
		my $itemid = $row_ref->[1];
		my $key = $row_ref->[2];
		my $value_type = $row_ref->[3];
		my $clock = $row_ref->[4];

		next if (substr($key, 0, length("rsm.dns.tcp")) eq "rsm.dns.tcp");

		my $index = index($host, ' ');

		$tld = substr($host, 0, $index);	# $tld is global variable
		my $probe = substr($host, $index + 1);

		my $service = get_service_from_key($key);

		fail("cannot identify item \"$key\" at host \"$host\"") unless ($service);

#		dbg($tld, "-", $service);

		foreach my $serv ($service eq 'dns' ? ('dns', 'dnssec') : ($service))
		{
			$lastvalues->{$tld}{$serv}{'probes'}{$probe}{$itemid} = {
				'key' => $key,
				'value_type' => $value_type,
				'clock' => $clock
			};

			my $cycle_clock = cycle_start($clock, $delays->{$serv});

			# 'lastclock' is the last cycle we want to calculate for this tld-service
			if (!defined($lastvalues->{$tld}{$serv}{'lastclock'}) ||
					$lastvalues->{$tld}{$serv}{'lastclock'} < $cycle_clock)
			{
				$lastvalues->{$tld}{$serv}{'lastclock'} = $cycle_clock;
			}
		}

#		print(ts_str($clock), " $tld,$probe ($host) | $key\n");
	}
}

sub fill_test_data($$$$)
{
	my $service = shift;
	my $src = shift;
	my $dst = shift;
	my $hist = shift;

	foreach my $ns (keys(%{$src}))
	{
		my $test_data_ref = {
			'target'	=> ($ns eq TARGET_PLACEHOLDER ? undef : $ns),
			'status'	=> undef,
			'metrics'	=> []
		};

		foreach my $clock (sort(keys(%{$src->{$ns}{'metrics'}})))
		{
			my $test = $src->{$ns}{'metrics'}{$clock};

			dbg("ns:$ns ip:", $test->{'targetIP'} // "UNDEF", " clock:", $test->{'testDateTime'} // "UNDEF", " rtt:", $test->{'rtt'} // "UNDEF");

			my $metric = {
				'testDateTime'	=> $clock,
				'targetIP'	=> $test->{'ip'}
			};

			if (!defined($test->{'rtt'}))
			{
				$metric->{'rtt'} = undef;
				$metric->{'result'} = 'no data';
			}
			elsif (is_internal_error_desc($test->{'rtt'}))
			{
				$metric->{'rtt'} = undef;
				$metric->{'result'} = $test->{'rtt'};

				# don't override NS status with "Up" if NS is already known to be down
				if (!defined($test_data_ref->{'status'}) || $test_data_ref->{'status'} ne "Down")
				{
					$test_data_ref->{'status'} = "Up";
				}
			}
			elsif (is_service_error_desc($service, $test->{'rtt'}))
			{
				$metric->{'rtt'} = undef;
				$metric->{'result'} = $test->{'rtt'};

				$test_data_ref->{'status'} = "Down";
			}
			else
			{
				$metric->{'rtt'} = $test->{'rtt'};
				$metric->{'result'} = "ok";

				# skip threshold check if NS is already known to be down
				if ($hist)
				{
					if  (!defined($test_data_ref->{'status'}) || $test_data_ref->{'status'} eq "Up")
					{
						$test_data_ref->{'status'} =
							($test->{'rtt'} > get_historical_value_by_time($hist,
								$metric->{'testDateTime'}) ? "Down" : "Up");
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
# Probe status value cache, itemid - ID of PROBE_KEY_ONLINE item
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

sub calculate_cycle($$$$$$$$)
{
	my $tld = shift;
	my $service = shift;
	my $probe_items = shift;
	my $cycle_clock = shift;
	my $delay = shift;
	my $rtt_limit = shift;
	my $probes_ref = shift;	# probes ('name' => 'hostid') available for this service
	my $interfaces_ref = shift;

	my $from = cycle_start($cycle_clock, $delay);
	my $till = cycle_end($cycle_clock, $delay);

	my $json = {'tld' => $tld, 'service' => $service, 'cycleCalculationDateTime' => $from};

	my %tested_interfaces;

#	print("$tld:\n");

	my $probes_with_results = 0;
	my $probes_with_positive = 0;
	my $probes_online = 0;

	foreach my $probe (keys(%{$probe_items}))
	{
		my (@itemids_uint, @itemids_float, @itemids_str);

		#
		# collect IDs of probe items, separate them by value_type to fetch values from history later
		#

		map {
			my $i = $probe_items->{$probe}{$_};

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
		} (keys(%{$probe_items->{$probe}}));

		next if (@itemids_uint == 0);

		#
		# fetch them separately
		#

		my $rows_ref = db_select(
			"select itemid,value".
			" from " . history_table(ITEM_VALUE_TYPE_UINT64).
			" where itemid in (" . join(',', @itemids_uint) . ")".
				" and " . sql_time_condition($from, $till)
		);

		# {
		#     ITEMID => value,
		#     ...
		# }
		my %values;

		map {push(@{$values{$_->[0]}}, int($_->[1]))} (@{$rows_ref});

		# skip cycles that do not have test result
		next if (scalar(keys(%values)) == 0);

#		print("  $probe:\n");

		my $service_up = 1;

		foreach my $itemid (keys(%values))
		{
			my $key = $probe_items->{$probe}{$itemid}{'key'};

			if (substr($key, 0, length("rsm.rdds")) eq "rsm.rdds")
			{
				foreach my $value (@{$values{$itemid}})
				{
					$service_up = 0 unless ($value == RDDS_UP);

					my $interface = AH_INTERFACE_RDDS43;

					if (!defined($tested_interfaces{$interface}{$probe}{'status'}) ||
						$tested_interfaces{$interface}{$probe}{'status'} == AH_CITY_DOWN)
					{
						$tested_interfaces{$interface}{$probe}{'status'} =
							($value == RDDS_UP || $value == RDDS_43_ONLY ? AH_CITY_UP : AH_CITY_DOWN);
					}

					$interface = AH_INTERFACE_RDDS80;

					if (!defined($tested_interfaces{$interface}{$probe}{'status'}) ||
						$tested_interfaces{$interface}{$probe}{'status'} == AH_CITY_DOWN)
					{
						$tested_interfaces{$interface}{$probe}{'status'} =
							($value == RDDS_UP || $value == RDDS_80_ONLY ? AH_CITY_UP : AH_CITY_DOWN);
					}
				}
			}
			elsif (substr($key, 0, length("rdap")) eq "rdap")
			{
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
		# collect clock->rtt keypairs (and clock->ip for DNS because for DNS metrics IP (and target) is taken from item key)
		#

		$rows_ref = db_select(
			"select itemid,value,clock".
			" from " . history_table(ITEM_VALUE_TYPE_FLOAT).
			" where itemid in (" . join(',', @itemids_float) . ")".
				" and " . sql_time_condition($from, $till)
		);

		%values = ();

		map {push(@{$values{$_->[0]}}, {'clock' => $_->[2], 'value' => int($_->[1])})} (@{$rows_ref});

		foreach my $itemid (keys(%values))
		{
			my $i = $probe_items->{$probe}{$itemid};

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

				$tested_interfaces{$interface}{$probe}{'testData'}{$target}{'metrics'}{$value_ref->{'clock'}}{'rtt'} = $value_ref->{'value'};
				$tested_interfaces{$interface}{$probe}{'testData'}{$target}{'metrics'}{$value_ref->{'clock'}}{'ip'} = $ip;
			}
		}

		next if (@itemids_str == 0);

		#
		# collect clock->ip keypairs for non-DNS services
		#

		$rows_ref = db_select(
			"select itemid,value,clock".
			" from " . history_table(ITEM_VALUE_TYPE_STR).
			" where itemid in (" . join(',', @itemids_str) . ")".
				" and " . sql_time_condition($from, $till)
		);

		%values = ();

		map {push(@{$values{$_->[0]}}, {'clock' => $_->[2], 'value' => $_->[1]})} (@{$rows_ref});

		foreach my $itemid (keys(%values))
		{
			my $i = $probe_items->{$probe}{$itemid};

			foreach my $value_ref (@{$values{$itemid}})
			{
				my $interface = ah_get_interface($i->{'key'});

				# for non-DNS service "target" is NULL, but we
				# can't use it as hash key so we use placeholder
				my $target = TARGET_PLACEHOLDER;

				$tested_interfaces{$interface}{$probe}{'testData'}{$target}{'metrics'}{$value_ref->{'clock'}}{'ip'} = $value_ref->{'value'};
			}
		}
	}

	# add "Offline" and "No results"
	foreach my $probe (keys(%{$probes_ref}))
	{
		my $probe_online = probe_online_at($probe, $from);

		foreach my $interface (@{$interfaces_ref})
		{
			if (!$probe_online)
			{
				$tested_interfaces{$interface}{$probe}{'status'} = AH_CITY_OFFLINE;

				undef($tested_interfaces{$interface}{$probe}{'testData'});
			}
			elsif (!defined($tested_interfaces{$interface}{$probe}{'status'}))
			{
				$tested_interfaces{$interface}{$probe}{'status'} = AH_CITY_NO_RESULT;

				undef($tested_interfaces{$interface}{$probe}{'testData'});
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

	my $detailed_info = sprintf("%d/%d positive, %.3f%%, %d online", $probes_with_positive, $probes_with_results, $perc, $probes_online);

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

	dbg("cycle: $json->{'status'} ($detailed_info)");

	if (opt('dry-run'))
	{
		print(Dumper($json));
		return;
	}

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

__END__

=head1 NAME

sla-api-current.pl - generate recent SLA API measurement files for newly collected data

=head1 SYNOPSIS

sla-api-current.pl [--tld <tld>] [--service <name>] [--server-id <id>] [--now unixtimestamp] [--period minutes] [--print-period] [--debug] [--dry-run] [--help]

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
