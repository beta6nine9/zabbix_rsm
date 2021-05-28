package RSMSLV;

use strict;
use warnings;

use DBI;
use DBI qw(:sql_types);
use DBI::Profile;
use Getopt::Long;
use Pod::Usage;
use Exporter qw(import);
use Zabbix;
use Alerts;
use TLD_constants qw(:api :items :ec :groups :config :templates);
use File::Pid;
use POSIX qw(floor);
use Sys::Syslog;
use Data::Dumper;
use Time::HiRes;
use Fcntl qw(:flock);	# for the LOCK_* constants, logging to stdout by multiple processes
use RSM;
use Pusher qw(push_to_trapper);
use Fcntl qw(:flock);
use List::Util qw(min max);
use Devel::StackTrace;

use constant E_ID_NONEXIST			=> -2;
use constant E_ID_MULTIPLE			=> -3;

use constant PROTO_UDP				=> 0;
use constant PROTO_TCP				=> 1;

							# "RSM Service Availability" value mapping:
use constant DOWN				=> 0;	# Down
use constant UP					=> 1;	# Up
use constant UP_INCONCLUSIVE_NO_DATA		=> 2;	# Up-inconclusive-no-data
use constant UP_INCONCLUSIVE_NO_PROBES		=> 3;	# Up-inconclusive-no-probes
use constant UP_INCONCLUSIVE_RECONFIG		=> 4;	# Up-inconclusive-reconfig

use constant ONLINE				=> 1;	# todo: check where these are used
use constant OFFLINE				=> 0;	# todo: check where these are used
use constant SLV_UNAVAILABILITY_LIMIT		=> 49;	# NB! must be in sync with frontend

use constant MIN_LOGIN_ERROR			=> -205;
use constant MAX_LOGIN_ERROR			=> -203;
use constant MIN_INFO_ERROR			=> -211;
use constant MAX_INFO_ERROR			=> -209;

use constant TRIGGER_SEVERITY_NOT_CLASSIFIED	=> 0;
use constant EVENT_OBJECT_TRIGGER		=> 0;
use constant EVENT_SOURCE_TRIGGERS		=> 0;
use constant TRIGGER_VALUE_FALSE		=> 0;
use constant TRIGGER_VALUE_TRUE			=> 1;
use constant INCIDENT_FALSE_POSITIVE		=> 1;	# NB! must be in sync with frontend

# In order to do the calculation we should wait till all the results
# are available on the server (from proxies). We shift back 2 minutes
# in case of "availability" and 3 minutes in case of "rolling week"
# calculations.

use constant WAIT_FOR_AVAIL_DATA		=> 120; # seconds to wait before sending UP_INCONCLUSIVE_NO_DATA to <service>.avail item
use constant WAIT_FOR_PROBE_DATA		=> 120; # seconds to wait before sending OFFLINE to rsm.probe.online item

use constant PROBE_ONLINE_STR			=> 'Online';

use constant PROBE_DELAY			=> 60;

use constant DETAILED_RESULT_DELIM		=> ', ';

use constant USE_CACHE_FALSE			=> 0;
use constant USE_CACHE_TRUE			=> 1;

use constant RECONFIG_MINUTES 			=> 10; # how much time to consider cycles in reconfig

our ($result, $dbh, $tld, $server_key);

our %OPTS; # specified command-line options

our @EXPORT = qw($result $dbh $tld $server_key
		E_ID_NONEXIST E_ID_MULTIPLE UP DOWN SLV_UNAVAILABILITY_LIMIT MIN_LOGIN_ERROR
		UP_INCONCLUSIVE_NO_PROBES
		UP_INCONCLUSIVE_NO_DATA
		UP_INCONCLUSIVE_RECONFIG
		PROTO_UDP
		PROTO_TCP
		MAX_LOGIN_ERROR MIN_INFO_ERROR MAX_INFO_ERROR PROBE_ONLINE_STR
		WAIT_FOR_AVAIL_DATA
		WAIT_FOR_PROBE_DATA
		PROBE_DELAY
		ONLINE OFFLINE
		USE_CACHE_FALSE USE_CACHE_TRUE
		get_macro_dns_probe_online
		get_macro_rdds_probe_online
		get_macro_rdap_probe_online
		get_macro_dns_rollweek_sla
		get_macro_rdds_rollweek_sla
		get_macro_rdap_rollweek_sla
		get_macro_dns_udp_rtt_high
		get_macro_dns_udp_rtt_low
		get_macro_dns_tcp_rtt_high
		get_macro_dns_tcp_rtt_low
		get_macro_rdds_rtt_low
		get_macro_rdap_rtt_low
		get_dns_delay
		get_rdds_delay
		get_rdap_delay
		get_epp_delay
		get_macro_epp_probe_online
		get_macro_epp_rollweek_sla
		get_macro_dns_update_time
		get_macro_rdds_update_time
		get_test_items
		get_hostid
		get_rtt_low
		get_macro_epp_rtt_low get_macro_probe_avail_limit
		get_macro_incident_dns_fail get_macro_incident_dns_recover
		get_macro_incident_rdds_fail get_macro_incident_rdds_recover
		get_macro_incident_rdap_fail get_macro_incident_rdap_recover
		get_monitoring_target
		get_rdap_standalone_ts is_rdap_standalone
		is_rsmhost_reconfigured
		get_dns_minns
		get_itemid_by_key get_itemid_by_host
		get_itemid_by_hostid get_itemid_like_by_hostid get_itemids_by_host_and_keypart get_lastclock
		get_tlds get_tlds_and_hostids
		get_oldest_clock
		get_probes get_nsips tld_exists tld_service_enabled db_connect db_disconnect
		validate_tld validate_service
		get_templated_nsips db_exec tld_interface_enabled
		tld_interface_enabled_create_cache tld_interface_enabled_delete_cache
		db_handler_read_status_start db_handler_read_status_end
		db_select db_select_col db_select_row db_select_value db_select_binds db_explain
		set_slv_config get_rollweek_bounds get_downtime_bounds
		current_month_first_cycle month_start
		probe_online_at_init probe_online_at probes2tldhostids
		slv_max_cycles
		get_probe_online_key_itemid
		init_values push_value send_values get_nsip_from_key is_service_error
		is_service_error_desc
		is_internal_error
		is_internal_error_desc
		collect_slv_cycles
		online_probes
		process_slv_avail_cycles
		process_slv_rollweek_cycles
		process_slv_downtime_cycles
		uint_value_exists
		float_value_exists
		sql_time_condition get_incidents get_downtime
		history_table
		get_lastvalue get_itemids_by_hostids get_nsip_values
		get_valuemaps get_statusmaps get_detailed_result
		get_avail_valuemaps
		get_result_string get_tld_by_trigger truncate_from truncate_till alerts_enabled
		get_real_services_period dbg info wrn fail set_on_fail log_only_message log_stacktrace
		format_stats_time
		init_process finalize_process
		slv_exit
		fail_if_running
		exit_if_running
		ltrim
		rtrim
		trim
		str_starts_with
		str_ends_with
		parse_opts parse_slv_opts override_opts
		opt getopt setopt unsetopt optkeys ts_str ts_full ts_ymd ts_hms selected_period
		cycle_start
		cycle_end
		update_slv_rtt_monthly_stats
		recalculate_downtime
		generate_report
		get_test_history
		get_test_results
		set_log_tld unset_log_tld
		convert_suffixed_number
		usage);

# configuration, set in set_slv_config()
my $config = undef;

my $_sender_values;	# used to send values to Zabbix server

my $POD2USAGE_FILE;	# usage message file

my $start_time;
my $total_sql_count = 0;
my $total_sql_duration = 0.0;

my $log_open = 0;

my $monitoring_target; # see get_monitoring_target()
my $rdap_standalone_ts; # see get_rdap_standalone_ts()

sub get_macro_dns_probe_online()
{
	return __get_macro('{$RSM.DNS.PROBE.ONLINE}');
}

sub get_macro_rdds_probe_online()
{
	return __get_macro('{$RSM.RDDS.PROBE.ONLINE}');
}

sub get_macro_rdap_probe_online()
{
	return __get_macro('{$RSM.RDAP.PROBE.ONLINE}');
}

sub get_macro_dns_rollweek_sla()
{
	return __get_macro('{$RSM.DNS.ROLLWEEK.SLA}');
}

sub get_macro_rdds_rollweek_sla()
{
	return __get_macro('{$RSM.RDDS.ROLLWEEK.SLA}');
}

sub get_macro_rdap_rollweek_sla()
{
	return __get_macro('{$RSM.RDAP.ROLLWEEK.SLA}');
}

sub get_macro_dns_udp_rtt_high()
{
	return __get_macro('{$RSM.DNS.UDP.RTT.HIGH}');
}

sub get_macro_dns_udp_rtt_low()
{
	return __get_macro('{$RSM.DNS.UDP.RTT.LOW}');
}

sub get_macro_dns_tcp_rtt_low()
{
	return __get_macro('{$RSM.DNS.TCP.RTT.LOW}');
}

sub get_macro_dns_tcp_rtt_high()
{
	return __get_macro('{$RSM.DNS.TCP.RTT.HIGH}');
}

sub get_macro_rdds_rtt_low()
{
	return __get_macro('{$RSM.RDDS.RTT.LOW}');
}

sub get_macro_rdap_rtt_low()
{
	return __get_macro('{$RSM.RDAP.RTT.LOW}');
}

sub get_dns_delay()
{
	return __get_macro('{$RSM.DNS.DELAY}');
}

sub get_rdds_delay()
{
	return __get_macro('{$RSM.RDDS.DELAY}');
}

sub get_rdap_delay()
{
	return __get_macro('{$RSM.RDAP.DELAY}');
}

sub get_epp_delay()
{
	return __get_macro('{$RSM.EPP.DELAY}');
}

sub get_macro_dns_update_time()
{
	return __get_macro('{$RSM.DNS.UPDATE.TIME}');
}

sub get_macro_rdds_update_time()
{
	return __get_macro('{$RSM.RDDS.UPDATE.TIME}');
}

sub get_macro_epp_probe_online()
{
	return __get_macro('{$RSM.EPP.PROBE.ONLINE}');
}

sub get_macro_epp_rollweek_sla()
{
	return __get_macro('{$RSM.EPP.ROLLWEEK.SLA}');
}

sub get_rtt_low
{
	my $service = shift;
	my $proto = shift;	# for DNS
	my $command = shift;	# for EPP: 'login', 'info' or 'update'

	if ($service eq 'dns' || $service eq 'dnssec')
	{
		fail("internal error: get_rtt_low() called for $service without specifying protocol")
			unless (defined($proto));

		if ($proto == PROTO_UDP)
		{
			return get_macro_dns_udp_rtt_low();	# can be per TLD
		}
		elsif ($proto == PROTO_TCP)
		{
			return get_macro_dns_tcp_rtt_low();	# can be per TLD
		}
		else
		{
			fail("unhandled protocol: '$proto'");
		}
	}

	if ($service eq 'rdds')
	{
		return get_macro_rdds_rtt_low();
	}

	if ($service eq 'rdap')
	{
		return get_macro_rdap_rtt_low();
	}

	if ($service eq 'epp')
	{
		return get_macro_epp_rtt_low($command);	# can be per TLD
	}

	fail("unhandled service: '$service'");
}

sub get_slv_rtt($;$)
{
	my $service = shift;
	my $proto = shift;	# for DNS

	if ($service eq 'dns' || $service eq 'dnssec')
	{
		fail("internal error: get_slv_rtt() called for $service without specifying protocol")
			unless (defined($proto));

		return __get_macro('{$RSM.SLV.DNS.UDP.RTT}') if ($proto == PROTO_UDP);
		return __get_macro('{$RSM.SLV.DNS.TCP.RTT}') if ($proto == PROTO_TCP);

		fail("Unhandled protocol \"$proto\"");
	}

	return __get_macro('{$RSM.SLV.RDDS.RTT}')   if ($service eq 'rdds');
	return __get_macro('{$RSM.SLV.RDDS43.RTT}') if ($service eq 'rdds43');
	return __get_macro('{$RSM.SLV.RDDS80.RTT}') if ($service eq 'rdds80');

	fail("Unhandled service \"$service\"");
}

sub get_macro_epp_rtt_low
{
	return __get_macro('{$RSM.EPP.'.uc(shift).'.RTT.LOW}');
}

sub get_macro_probe_avail_limit
{
	return __get_macro('{$RSM.PROBE.AVAIL.LIMIT}');
}

sub get_macro_incident_dns_fail()
{
	return __get_macro('{$RSM.INCIDENT.DNS.FAIL}');
}

sub get_macro_incident_dns_recover()
{
	return __get_macro('{$RSM.INCIDENT.DNS.RECOVER}');
}

sub get_macro_incident_rdds_fail()
{
	return __get_macro('{$RSM.INCIDENT.RDDS.FAIL}');
}

sub get_macro_incident_rdds_recover()
{
	return __get_macro('{$RSM.INCIDENT.RDDS.RECOVER}');
}

sub get_macro_incident_rdap_fail()
{
	return __get_macro('{$RSM.INCIDENT.RDAP.FAIL}');
}

sub get_macro_incident_rdap_recover()
{
	return __get_macro('{$RSM.INCIDENT.RDAP.RECOVER}');
}

sub get_monitoring_target()
{
	if (!defined($monitoring_target))
	{
		$monitoring_target = __get_macro('{$RSM.MONITORING.TARGET}');

		if ($monitoring_target ne MONITORING_TARGET_REGISTRY && $monitoring_target ne MONITORING_TARGET_REGISTRAR)
		{
			fail("{\$RSM.MONITORING.TARGET} has unexpected value: '$monitoring_target'");
		}
	}

	return $monitoring_target;
}

# Returns timestamp, when treating RDAP as a standalone service has to be started, or undef
sub get_rdap_standalone_ts()
{
	if (!defined($rdap_standalone_ts))
	{
		# NB! Don't store undef as cached value!
		$rdap_standalone_ts = __get_macro('{$RSM.RDAP.STANDALONE}');
	}

	return $rdap_standalone_ts ? int($rdap_standalone_ts) : undef;
}

# Returns 1, if RDAP has to be treated as a standalone service, 0 otherwise
sub is_rdap_standalone(;$)
{
	my $now = shift // time();

	my $ts = get_rdap_standalone_ts();

	return defined($ts) && $now >= $ts ? 1 : 0;
}

my $config_times;

sub is_rsmhost_reconfigured($$$)
{
	my $rsmhost = shift;
	my $delay   = shift;
	my $clock   = shift;

	if (!defined($config_times))
	{
		my $sql = "select" .
				" hosts.host," .
				" hostmacro.value" .
			" from" .
				" hosts" .
				" inner join hosts_templates on hosts_templates.hostid=hosts.hostid" .
				" inner join hosts as templates on templates.hostid=hosts_templates.templateid" .
				" inner join hostmacro on hostmacro.hostid=templates.hostid" .
				" inner join hosts_groups on hosts_groups.hostid=hosts.hostid" .
				" inner join hstgrp on hstgrp.groupid=hosts_groups.groupid" .
			" where" .
				" hstgrp.name='TLDs' and" .
				" hostmacro.macro='{\$RSM.TLD.CONFIG.TIMES}'";
		my $rows = db_select($sql);

		foreach my $row (@{$rows})
		{
			my ($rsmhost, $macro) = @{$row};

			if (!$macro)
			{
				fail("invalid value of {\$RSM.TLD.CONFIG.TIMES} for '$rsmhost': '$macro'");
			}

			$config_times->{$rsmhost} = [split(/;/, $macro)];
		}
	}

	if (!exists($config_times->{$rsmhost}))
	{
		fail("{\$RSM.TLD.CONFIG.TIMES} for '$rsmhost' not found");
	}

	my $cycle_start = cycle_start($clock, $delay);
	my $cycle_end   = cycle_end($clock, $delay);

	foreach my $config_time (@{$config_times->{$rsmhost}})
	{
		my $reconfig_time_start = cycle_start($config_time, 60);
		my $reconfig_time_end   = cycle_end($config_time + (RECONFIG_MINUTES - 1) * 60, 60);

		if ($cycle_end >= $reconfig_time_start && $cycle_start <= $reconfig_time_end)
		{
			return 1;
		}
	}

	return 0;
}

my $dns_minns_cache;

sub get_dns_minns($$)
{
	my $rsmhost = shift;
	my $clock   = shift;

	if (!defined($dns_minns_cache))
	{
		my $sql = "select" .
				" rsmhost.value," .
				"minns.value" .
			" from" .
				" hostmacro as rsmhost," .
				"hostmacro as minns" .
			" where" .
				" rsmhost.hostid=minns.hostid and" .
				" rsmhost.macro='{\$RSM.TLD}' and" .
				" minns.macro='{\$RSM.TLD.DNS.AVAIL.MINNS}'";
		my $rows = db_select($sql);

		foreach my $row (@{$rows})
		{
			my ($rsmhost, $macro) = @{$row};

			if ($macro =~ /^(\d+)(?:;(\d+):(\d+))?$/)
			{
				$dns_minns_cache->{$rsmhost} = {
					'curr_minns' => $1,
					'next_clock' => $2,
					'next_minns' => $3,
				};
			}
			else
			{
				fail("invalid value of {\$RSM.TLD.DNS.AVAIL.MINNS} for '$rsmhost': '$macro'");
			}

		}
	}

	if (!exists($dns_minns_cache->{$rsmhost}))
	{
		fail("unknown rsmhost '$rsmhost'");
	}

	my $minns = $dns_minns_cache->{$rsmhost};

	if (!defined($minns->{'next_clock'}) || $clock < $minns->{'next_clock'})
	{
		return $minns->{'curr_minns'};
	}
	else
	{
		return $minns->{'next_minns'};
	}
}

sub get_itemid_by_key
{
	my $key = shift;

	return __get_itemid_by_sql("select itemid from items where key_='$key'");
}

sub get_itemid_by_host
{
	my $host = shift;
	my $key = shift;

	my $itemid = __get_itemid_by_sql(
		"select i.itemid".
		" from items i,hosts h".
		" where i.hostid=h.hostid".
			" and h.host='$host'".
			" and i.key_='$key'"
	);

	fail("item \"$key\" does not exist for \"$host\"") if ($itemid == E_ID_NONEXIST);
	fail("more than one item \"$key\" found for \"$host\"") if ($itemid == E_ID_MULTIPLE);

	return $itemid;
}

sub get_itemid_by_hostid
{
	my $hostid = shift;
	my $key = shift;

	return __get_itemid_by_sql("select itemid from items where hostid=$hostid and key_='$key'");
}

sub get_itemid_like_by_hostid
{
	my $hostid = shift;
	my $key = shift;

	return __get_itemid_by_sql("select itemid from items where hostid=$hostid and key_ like '$key'");
}

sub __get_itemid_by_sql
{
	my $sql = shift;

	my $rows_ref = db_select($sql);

	return E_ID_NONEXIST if (scalar(@$rows_ref) == 0);
        return E_ID_MULTIPLE if (scalar(@$rows_ref) > 1);

        return $rows_ref->[0]->[0];
}

# Return itemids of Name Server items in form:
# {
#     ns1.example.com,10.20.30.40 => 32512,
#     ns2.example.com,10.20.30.41 => 32513,
#     ....
# }
sub get_itemids_by_host_and_keypart
{
	my $host = shift;
	my $key_part = shift;

	my $rows_ref = db_select(
		"select i.itemid,i.key_".
		" from items i,hosts h".
		" where i.hostid=h.hostid".
			" and h.host='$host'".
			" and i.key_ like '$key_part%'");

	fail("cannot find items ($key_part%) at host ($host)") if (scalar(@$rows_ref) == 0);

	my $result = {};

	foreach my $row_ref (@$rows_ref)
	{
		my $itemid = $row_ref->[0];
		my $key = $row_ref->[1];

		my $nsip = get_nsip_from_key($key);

		$result->{$nsip} = $itemid;
	}

	return $result;
}

# input:
# [
#     [host, key],
#     [host, key],
#     ...
# ]
# output:
# {
#     host => {
#         key => itemid,
#         key => itemid,
#     },
#     ...
# }
sub get_itemids_by_hosts_and_keys($)
{
	my $filter = shift; # [[host, key], ...]

	my $filter_string = join(" or ", ("(hosts.host = ? and items.key_ = ?)") x scalar(@{$filter}));
	my $filter_params = [map(($_->[0], $_->[1]), @{$filter})];

	my $sql = "select" .
			" hosts.host," .
			" items.key_," .
			" items.itemid" .
		" from" .
			" hosts" .
			" left join items on items.hostid = hosts.hostid" .
		" where" .
			" " . $filter_string;

	my $rows = db_select($sql, $filter_params);

	my $result = {};

	foreach my $row (@{$rows})
	{
		my ($host, $key, $itemid) = @{$row};

		$result->{$host}{$key} = $itemid;
	}

	return $result;
}

# returns:
# E_FAIL - if item was not found
#      0 - if lastclock is NULL
#      * - lastclock
sub get_lastclock($$$)
{
	my $host = shift;
	my $key = shift;
	my $value_type = shift;

	my $sql;

	if (str_ends_with($key, "["))
	{
		$sql =
			"select i.itemid".
			" from items i,hosts h".
			" where i.hostid=h.hostid".
				" and i.status=0".
				" and h.host='$host'".
				" and i.key_ like '$key%'".
			" limit 1";
	}
	else
	{
		$sql =
			"select i.itemid".
			" from items i,hosts h".
			" where i.hostid=h.hostid".
				" and i.status=0".
				" and h.host='$host'".
				" and i.key_='$key'";
	}

	my $rows_ref = db_select($sql);

	return E_FAIL if (scalar(@$rows_ref) == 0);

	my $itemid = $rows_ref->[0]->[0];
	my $lastclock;

	if (get_lastvalue($itemid, $value_type, undef, \$lastclock) != SUCCESS)
	{
		$lastclock = 0;
	}

	return $lastclock;
}

# returns:
# E_FAIL - if item was not found
# undef  - if history table is empty
# *      - lastclock
sub get_oldest_clock($$$$)
{
	my $host = shift;
	my $key = shift;
	my $value_type = shift;
	my $clock_limit = shift;

	my $rows_ref = db_select(
		"select i.itemid".
		" from items i,hosts h".
		" where i.hostid=h.hostid".
			" and i.status=0".
			" and h.host='$host'".
			" and i.key_='$key'"
	);

	return E_FAIL if (scalar(@$rows_ref) == 0);

	my $itemid = $rows_ref->[0]->[0];

	$rows_ref = db_select(
		"select min(clock)".
		" from " . history_table($value_type).
		" where itemid=$itemid".
			" and clock>$clock_limit"
	);

	return $rows_ref->[0]->[0];
}

# $tlds_cache{$server_key}{$service}{$clock} = ["tld1", "tld2", ...];
my %tlds_cache = ();

sub get_tlds(;$$$)
{
	my $service = shift;	# optionally specify service which must be enabled
	my $clock = shift;	# used only if $service is defined
	my $use_cache = shift // USE_CACHE_FALSE;

	if ($use_cache != USE_CACHE_FALSE && $use_cache != USE_CACHE_TRUE)
	{
		fail("invalid value for \$use_cache argument - '$use_cache'");
	}

	if ($use_cache == USE_CACHE_TRUE && exists($tlds_cache{$server_key}{$service // ''}{$clock // 0}))
	{
		return $tlds_cache{$server_key}{$service // ''}{$clock // 0};
	}

	my $rows_ref = db_select(
		"select distinct h.host".
		" from hosts h,hosts_groups hg".
		" where h.hostid=hg.hostid".
			" and hg.groupid=".TLDS_GROUPID.
			" and h.status=".HOST_STATUS_MONITORED.
		" order by h.host");

	my @tlds;
	foreach my $row_ref (@$rows_ref)
	{
		my $tld = $row_ref->[0];

		if (defined($service))
		{
			next unless (tld_service_enabled($tld, $service, $clock));
		}

		push(@tlds, $tld);
	}

	if ($use_cache == USE_CACHE_TRUE)
	{
		$tlds_cache{$server_key}{$service // ''}{$clock // 0} = \@tlds;
	}

	return \@tlds;
}

# get all tlds and their hostids or a single tld with its hostid
sub get_tlds_and_hostids(;$)
{
	my $tld = shift;
	my $tld_cond = '';

	if (defined($tld))
	{
		$tld_cond = " and h.host='$tld'";
	}

	return db_select(
		"select distinct h.host,h.hostid".
		" from hosts h,hosts_groups hg".
		" where h.hostid=hg.hostid".
			" and hg.groupid=".TLDS_GROUPID.
			" and h.status=0".
			$tld_cond.
		" order by h.host");
}

# $probes_cache{$server_key}{$name}{$service} = {$host => $hostid, ...}
my %probes_cache = ();

# Returns a reference to hash of all probes (host => {'hostid' => hostid, 'status' => status}).
sub get_probes(;$$)
{
	my $service = shift; # "IP4", "IP6", "RDDS", "RDAP" or any other
	my $name = shift;

	$service = defined($service) ? uc($service) : "ALL";
	$name //= "";

	if ($service ne "IP4" && $service ne "IP6" && $service ne "RDDS" && $service ne "RDAP")
	{
		$service = "ALL";
	}

	if (!exists($probes_cache{$server_key}{$name}))
	{
		$probes_cache{$server_key}{$name} = __get_probes($name);
	}

	return $probes_cache{$server_key}{$name}{$service};
}

sub __get_probes($)
{
	my $name = shift;

	my $name_condition = ($name ? "name='$name' and" : "");

	my $rows = db_select(
		"select hosts.hostid,hosts.host,hostmacro.macro,hostmacro.value,hosts.status" .
		" from hosts" .
			" left join hosts_groups on hosts_groups.hostid=hosts.hostid" .
			" left join hosts_templates on hosts_templates.hostid=hosts.hostid" .
			" left join hostmacro on hostmacro.hostid=hosts_templates.templateid" .
		" where $name_condition" .
			" hosts_groups.groupid=" . PROBES_GROUPID . " and" .
			" hostmacro.macro in ('{\$RSM.IP4.ENABLED}','{\$RSM.IP6.ENABLED}','{\$RSM.RDDS.ENABLED}','{\$RSM.RDAP.ENABLED}')");

	my %result = (
		'ALL'  => {},
		'IP4'  => {},
		'IP6'  => {},
		'RDDS' => {},
		'RDAP' => {},
	);

	foreach my $row (@{$rows})
	{
		my ($hostid, $host, $macro, $value, $status) = @{$row};

		if (!exists($result{'ALL'}{$host}))
		{
			$result{'ALL'}{$host} = {'hostid' => $hostid, 'status' => $status};
		}

		if ($macro eq '{$RSM.IP4.ENABLED}')
		{
			$result{'IP4'}{$host} = {'hostid' => $hostid, 'status' => $status} if ($value);
		}
		elsif ($macro eq '{$RSM.IP6.ENABLED}')
		{
			$result{'IP6'}{$host} = {'hostid' => $hostid, 'status' => $status} if ($value);
		}
		elsif ($macro eq '{$RSM.RDDS.ENABLED}')
		{
			$result{'RDDS'}{$host} = {'hostid' => $hostid, 'status' => $status} if ($value);
		}
		elsif ($macro eq '{$RSM.RDAP.ENABLED}')
		{
			$result{'RDAP'}{$host} = {'hostid' => $hostid, 'status' => $status} if ($value);
		}
	}

	if (opt("debug"))
	{
		dbg("number of probes - " . scalar(keys(%{$result{'ALL'}})));
		dbg("number of probes with IP4 support  - " . scalar(keys(%{$result{'IP4'}})));
		dbg("number of probes with IP6 support  - " . scalar(keys(%{$result{'IP6'}})));
		dbg("number of probes with RDDS support - " . scalar(keys(%{$result{'RDDS'}})));
		dbg("number of probes with RDAP support - " . scalar(keys(%{$result{'RDAP'}})));
	}

	return \%result;
}

# get array of key nameservers ('i.ns.se,130.239.5.114', ...)
sub get_nsips
{
	my $host = shift;
	my $key = shift;

	my $rows_ref = db_select(
		"select key_".
		" from items i,hosts h".
		" where i.hostid=h.hostid".
			" and i.status<>".ITEM_STATUS_DISABLED.
			" and h.host='$host'".
			" and i.key_ like '$key%'");

	my @nss;
	foreach my $row_ref (@$rows_ref)
	{
		push(@nss, get_nsip_from_key($row_ref->[0]));
	}

	fail("cannot find items ($key*) at host ($host)") if (scalar(@nss) == 0);

	return \@nss;
}

sub get_templated_nsips
{
	my $host = shift;
	my $key = shift;

	return get_nsips("Template $host", $key);
}

# returns a reference to a hash:
# {
#     hostid => {
#         itemid => 'key_',
#         ...
#     },
#     ...
# }
sub __get_host_items
{
	my $hostids_ref = shift;
	my $keys_ref = shift;

	my $rows_ref = db_select(
		"select hostid,itemid,key_".
		" from items".
		" where hostid in (" . join(',', @{$hostids_ref}) . ")".
			" and key_ in (" . join(',', map {"'$_'"} (@{$keys_ref})) . ")");

	my $result = {};

	foreach my $row_ref (@$rows_ref)
	{
		$result->{$row_ref->[0]}->{$row_ref->[1]} = $row_ref->[2];
	}

	return $result;
}

sub get_test_items($)
{
	my $rsmhost = shift;

	# TODO: in the future consider also collecting SLV items, to get everything related to the test
	#
	#my $host_cond = " and (" .
	#			"(hg.groupid=" . TLDS_GROUPID . " and h.host='$rsmhost') or" .
	#			" (hg.groupid=" . TLD_PROBE_RESULTS_GROUPID . " and h.host like '$rsmhost %')" .
	#		")";

	my $rows_ref = db_select(
		"select h.host,hg.groupid,i.itemid,i.key_,i.value_type".
		" from items i,hosts h,hosts_groups hg".
		" where h.hostid=i.hostid".
			" and hg.hostid=h.hostid".
			" and h.status=".HOST_STATUS_MONITORED.
			" and i.status<>".ITEM_STATUS_DISABLED.
			" and hg.groupid=" . TLD_PROBE_RESULTS_GROUPID . " and h.host like '$rsmhost %'"
	);

	my $result = {};

	foreach my $row_ref (@{$rows_ref})
	{
		my $host = $row_ref->[0];
		my $groupid = $row_ref->[1];
		my $itemid = $row_ref->[2];
		my $key = $row_ref->[3];
		my $value_type = $row_ref->[4];

		my $probe;

		# TODO: in the future consider also collecting SLV items, to get everything related to the test
		#
		#if ($groupid == TLDS_GROUPID)
		#{
		#	$probe = "";
		#}
		#elsif ($host =~ /$rsmhost (.*)/)
		if ($host =~ /$rsmhost (.*)/)
		{
			$probe = $1;
		}
		else
		{
			fail("unexpected host name: \"$host\"");
		}

		$result->{$probe}{$itemid} = {
			'key' => $key,
			'value_type' => $value_type,
		};
	}

	return $result;
}

sub get_hostid
{
	my $host = shift;

	my $rows_ref = db_select("select hostid from hosts where host='$host'");

	fail("host \"$host\" not found") if (scalar(@$rows_ref) == 0);
	fail("multiple hosts \"$host\" found") if (scalar(@$rows_ref) > 1);

	return $rows_ref->[0]->[0];
}

sub tld_exists_locally($)
{
	my $tld = shift;

	my $rows_ref = db_select(
		"select 1".
		" from hosts h,hosts_groups hg,hstgrp g".
		" where h.hostid=hg.hostid".
			" and hg.groupid=g.groupid".
			" and g.name='TLDs'".
			" and h.status=0".
			" and h.host='$tld'"
	);

	return 0 if (scalar(@$rows_ref) == 0);

	return 1;
}

sub tld_exists($)
{
	return tld_exists_locally(shift);
}

sub validate_tld($$)
{
	my $tld = shift;
	my $server_keys = shift;

	foreach my $server_key (@{$server_keys})
	{
		db_connect($server_key);

		my $rv = tld_exists_locally($tld);

		db_disconnect();

		if ($rv)
		{
			dbg("tld $tld found on $server_key");

			return;
		}
	}

	fail("tld \"$tld\" does not exist");
}

sub validate_service($)
{
	my $service = shift;

	db_connect();

	if (get_monitoring_target() eq MONITORING_TARGET_REGISTRY)
	{
		if (!grep {/$service/} ('dns', 'dnssec', 'rdds', 'rdap', 'epp'))
		{
			fail("service \"$service\" is unknown");
		}
	}
	elsif (get_monitoring_target() eq MONITORING_TARGET_REGISTRAR)
	{
		if (!grep {/$service/} ('rdds', 'rdap'))
		{
			fail("service \"$service\" is unknown");
		}
	}

	db_disconnect();
}

my %tld_service_enabled_cache = ();

sub tld_service_enabled($$$)
{
	my $tld     = shift;
	my $service = shift;
	my $now     = shift;

	$service = lc($service);

	if (!defined($tld_service_enabled_cache{$server_key}{$tld}{$service}{$now}))
	{
		$tld_service_enabled_cache{$server_key}{$tld}{$service}{$now} = __tld_service_enabled($tld, $service, $now);
	}

	return $tld_service_enabled_cache{$server_key}{$tld}{$service}{$now};
}

sub __tld_service_enabled($$$)
{
	my $tld     = shift;
	my $service = shift;
	my $now     = shift;

	if ($service eq 'rdds')
	{
		return 1 if (tld_interface_enabled($tld, 'rdds43', $now));
		return 1 if (tld_interface_enabled($tld, 'rdap', $now) && !is_rdap_standalone($now));
		return 0;
	}
	else
	{
		return tld_interface_enabled($tld, $service, $now);
	}
}

sub enabled_item_key_from_interface
{
	my $interface = shift;

	if ($interface eq 'rdds43' || $interface eq 'rdds80')
	{
		return 'rdds.enabled';
	}

	return "$interface.enabled";
}

# NB! When parallelization is used use this function to create cache in parent
# process to use functions tld_<service|interface>_enabled() in child processes.
#
# Collect the itemids of 'enabled' items in one SQL to improve performance of the function
#
# %enabled_items_cache =
# (
#     'rdds.enabled' => {
#         'tld1' => [
#             itemid1,
#             itemid2,
#             ...
#         ],
#         'tld2' => [
#             ...
#         ]
#     },
#     'dnssec.enabled' => {
#         'tld1' => [
#             itemid1,
#             itemid2,
#             ...
#         ],
#         'tld2' => [
#             ...
#         ]
#         ...
#     },
#     ...
# )
#
# These variables are initialized at db_connect()

my %enabled_hosts_cache;	# (hostid1 => tld1, ...)
my %enabled_items_cache;	# (key1 => {tld1 => [itemid1, itemid2, ...], ...}, ...)
my @tlds_cache;			# (tld1, tld2, ...)

sub uniq
{
	my %seen;

	grep(!$seen{$_}++, @_);
}

sub tld_interface_enabled_create_cache
{
	my @interfaces = @_;

	dbg(join(',', @interfaces));

	return if (scalar(@interfaces) == 0);

	if (scalar(keys(%enabled_hosts_cache)) == 0)
	{
		my $rows_ref = db_select(
			"select h.hostid,h.host".
			" from hosts h,hosts_groups hg".
			" where h.hostid=hg.hostid".
				" and h.status=0".
				" and hg.groupid=".TLDS_GROUPID);

		map {$enabled_hosts_cache{$_->[0]} = $_->[1]} (@{$rows_ref});

		@tlds_cache = uniq(values(%enabled_hosts_cache)) if (scalar(@tlds_cache) == 0);
	}

	return if (scalar(keys(%enabled_hosts_cache)) == 0);

	foreach my $interface (@interfaces)
	{
		$interface = lc($interface);

		my $item_key = enabled_item_key_from_interface($interface);

		next if ($interface eq 'dns');

		if (!defined($enabled_items_cache{$item_key}))
		{
			$enabled_items_cache{$item_key} = ();

			my $rows_ref = db_select(
				"select itemid,hostid".
				" from items".
				" where key_='$item_key'".
					" and hostid in (" . join(',', keys(%enabled_hosts_cache)) . ")");

			map {$enabled_items_cache{$item_key}{$_} = []} (@tlds_cache);

			foreach my $row_ref (@{$rows_ref})
			{
				my $itemid = $row_ref->[0];
				my $hostid = $row_ref->[1];

				my $_tld = $enabled_hosts_cache{$hostid};

				push(@{$enabled_items_cache{$item_key}{$_tld}}, $itemid);
			}
		}
	}
}

sub tld_interface_enabled_delete_cache()
{
	%enabled_items_cache = ();
	%enabled_hosts_cache = ();
	@tlds_cache = ();
}

sub tld_interface_enabled($$$)
{
	my $tld = shift;
	my $interface = shift;
	my $now = shift;

	$interface = lc($interface);

	if ($interface eq 'epp')
	{
		return 0;
	}
	elsif ($interface eq 'dns')
	{
		return 1 if (get_monitoring_target() eq MONITORING_TARGET_REGISTRY);
		return 0 if (get_monitoring_target() eq MONITORING_TARGET_REGISTRAR);
	}
	elsif ($interface eq 'dnssec')
	{
		return 0 if (get_monitoring_target() eq MONITORING_TARGET_REGISTRAR);
	}

	my $item_key = enabled_item_key_from_interface($interface);

	if (!defined($enabled_items_cache{$item_key}))
	{
		tld_interface_enabled_create_cache($interface);
	}

	if (!defined($enabled_items_cache{$item_key}{$tld}))
	{
		# do nothing, no .enabled items in cache for this TLD
	}
	elsif (scalar(@{$enabled_items_cache{$item_key}{$tld}}) == 0)
	{
		# List of .enabled items for this TLD defined but is empty because
		# tld_interface_enabled_create_cache() didn't find items. This is probably
		# misconfiguration.
		wrn("no items with '$item_key' for host '$tld'");
	}
	else
	{
		# find the latest value but make sure to specify time bounds, relatively to $now

		$now = time() - 120 unless ($now);	# go back 2 minutes if time unspecified

		my $till = cycle_end($now, 60);

		my @conditions = (
			[$till - 0 * 3600 -  1 * 60 + 1, $till            , "max(clock)"],	# go back 1 minute
			[$till - 0 * 3600 - 30 * 60 + 1, $till            , "max(clock)"],	# go back 30 minutes
			[$till - 6 * 3600 -  0 * 60 + 1, $till            , "max(clock)"],	# go back 6 hours
			[$till + 1                     , $till + 24 * 3600, "min(clock)"]	# go forward 1 day
		);

		my $condition_index = 0;
		my $itemids_placeholder = join(",", ("?") x scalar(@{$enabled_items_cache{$item_key}{$tld}}));

		while ($condition_index < scalar(@conditions))
		{
			my $from = $conditions[$condition_index]->[0];
			my $till = $conditions[$condition_index]->[1];
			my $clock = $conditions[$condition_index]->[2];

			my $rows_ref = db_select(
				"select value" .
				" from" .
					" history_uint" .
					" inner join (" .
						" select itemid,$clock as clock" .
						" from history_uint" .
						" where" .
							" clock between ? and ? and" .
							" itemid in ($itemids_placeholder)" .
						" group by itemid" .
					" ) as history_clock on history_clock.itemid=history_uint.itemid and history_clock.clock=history_uint.clock",
					[$from, $till, @{$enabled_items_cache{$item_key}{$tld}}]);

			my $found = 0;

			foreach my $row_ref (@{$rows_ref})
			{
				if (defined($row_ref->[0]))
				{
					$found = 1;

					return 1 if ($row_ref->[0]);
				}
			}

			return 0 if ($found);

			$condition_index++;
		}
	}

	# try the Template macro

	my $host = TEMPLATE_RSMHOST_CONFIG_PREFIX . $tld;

	my $macro;

	if ($interface eq 'rdap')
	{
		$macro = '{$RDAP.TLD.ENABLED}';
	}
	elsif ($interface eq 'rdds43' || $interface eq 'rdds80')
	{
		$macro = '{$RSM.TLD.RDDS.ENABLED}';
	}
	else
	{
		$macro = '{$RSM.TLD.' . uc($interface) . '.ENABLED}';
	}

	my $rows_ref = db_select(
		"select hm.value".
		" from hosts h,hostmacro hm".
		" where h.hostid=hm.hostid".
			" and h.host='$host'".
			" and hm.macro='$macro'");

	if (scalar(@{$rows_ref}) != 0)
	{
		return $rows_ref->[0]->[0];
	}

	wrn("macro \"$macro\" does not exist at \"$host\", assuming $interface disabled");

	return 0;
}

sub generate_db_error($$)
{
	my $handle  = shift;
	my $message = shift // $handle->errstr;

	my @message_parts = ();

	if ($tld)
	{
		push(@message_parts, "[tld:$tld]");
	}

	push(@message_parts, 'database error:');

	push(@message_parts, $message);

	if (defined($handle->{'Statement'}))
	{
		push(@message_parts, "(query: [$handle->{'Statement'}])");
	}

	if (defined($handle->{'ParamValues'}) && %{$handle->{'ParamValues'}})
	{
		my $params = $handle->{'ParamValues'};
		my @params = @{$params}{sort {$a <=> $b} keys(%{$params})};
		my $params_str = join(',', map($_ // 'undef', @params));

		push(@message_parts, "(params: [$params_str])");
	}

	if (defined($handle->{'ParamArrays'}) && %{$handle->{'ParamArrays'}})
	{
		my $params = join(',', values(%{$handle->{'ParamArrays'}}));
		push(@message_parts, "(params 2: [$params])");
	}

	return join(' ', @message_parts);
}

sub handle_db_error($$$)
{
	my $message = shift;
	my $handle  = shift;

	fail(generate_db_error($handle, undef));
}

{
	package RSMDBI;
	use DBI;
	use vars qw(@ISA);
	@ISA = qw(DBI);

	package RSMDBI::db;
	use vars qw(@ISA);
	@ISA = qw(DBI::db);

	package RSMDBI::st;
	use vars qw(@ISA);
	@ISA = qw(DBI::st);

	our $warn_duration;
	our $warn_function;

	sub query
	{
		my ($handle, $method, @args) = @_;

		my $parent_method = "SUPER::$method";

		my $result;
		my @result;

		my $start = Time::HiRes::time();

		if (wantarray())
		{
			@result = $handle->$parent_method(@args);
		}
		else
		{
			$result = $handle->$parent_method(@args);
		}

		my $duration = Time::HiRes::time() - $start;

		if ($duration > $warn_duration)
		{
			$warn_function->(sprintf("slow query: [%s] took %.3f seconds (%s)", $handle->{'Statement'}, $duration, $method));
		}

		return wantarray() ? @result : $result;
	}

	sub bind_param        { return query(shift, "bind_param"       , @_); }
	sub bind_param_inout  { return query(shift, "bind_param_inout" , @_); }
	sub bind_param_array  { return query(shift, "bind_param_array" , @_); }
	sub execute           { return query(shift, "execute"          , @_); }
	sub execute_array     { return query(shift, "execute_array"    , @_); }
	sub execute_for_fetch { return query(shift, "execute_for_fetch", @_); }
	sub last_insert_id    { return query(shift, "last_insert_id"   , @_); }
	sub fetchrow_arrayref { return query(shift, "fetchrow_arrayref", @_); }
	sub fetchrow_array    { return query(shift, "fetchrow_array"   , @_); }
	sub fetchrow_hashref  { return query(shift, "fetchrow_hashref" , @_); }
	sub fetchall_arrayref { return query(shift, "fetchall_arrayref", @_); }
	sub fetchall_hashref  { return query(shift, "fetchall_hashref" , @_); }
	sub finish            { return query(shift, "finish"           , @_); }
	sub rows              { return query(shift, "rows"             , @_); }
	sub bind_col          { return query(shift, "bind_col"         , @_); }
	sub bind_columns      { return query(shift, "bind_columns"     , @_); }
	sub dump_results      { return query(shift, "dump_results"     , @_); }
}

sub db_connect
{
	$server_key = shift;

	dbg("server_key:", ($server_key ? $server_key : "UNDEF"));

	fail("Error: no database configuration") unless (defined($config));

	db_disconnect() if (defined($dbh));

	$server_key = get_rsm_local_key($config) unless ($server_key);

	fail("Configuration error: section \"$server_key\" not found") unless (defined($config->{$server_key}));

	my $section = $config->{$server_key};

	foreach my $key ('db_name', 'db_user')
	{
		fail("configuration error: database $key not specified in section \"$server_key\"")
			unless (defined($section->{$key}));
	}

	my $db_tls_settings = get_db_tls_settings($section);

	my $data_source = "DBI:mysql:database=$section->{'db_name'};host=$section->{'db_host'};$db_tls_settings";

	# NB! Timeouts have to be specified via DSN. To check if they're actually being used:
	# $ export DBI_TRACE=2=dbitrace.log
	# $ ./XXX.pl
	# $ grep 'Setting' dbitrace.log
	$data_source .= ';mysql_connect_timeout=' . ($section->{'db_connect_timeout'} // 30);
	$data_source .= ';mysql_write_timeout='   . ($section->{'db_write_timeout'} // 30);
	$data_source .= ';mysql_read_timeout='    . ($section->{'db_read_timeout'} // 30);

	dbg($data_source);

	my $connect_opts = {
		'PrintError'		=> 0,
		'HandleError'		=> \&handle_db_error,
		'mysql_auto_reconnect'	=> 1,
	};

	if (opt('warnslow'))
	{
		$connect_opts->{'RootClass'} = 'RSMDBI';
		$RSMDBI::st::warn_duration = getopt('warnslow');
		$RSMDBI::st::warn_function = \&wrn;
	}

	if (opt('stats'))
	{
		$DBI::Profile::ON_DESTROY_DUMP = sub{};
		$connect_opts->{'Profile'} = DBI::Profile->new(Path => ['!MethodName']);
	}

	# errors should be handled by handle_db_error() automatically, but lets call fail() as a fallback
	$dbh = DBI->connect($data_source, $section->{'db_user'}, $section->{'db_password'}, $connect_opts)
		or fail("database error: " . DBI->errstr . " (data source was: [$data_source])");

	# verify that established database connection uses TLS if there was any hint that it is required in the config
	unless ($db_tls_settings eq "mysql_ssl=0")
	{
		my $rows_ref = db_select("show status like 'Ssl_cipher';");

		fail("established connection is not secure") if ($rows_ref->[0]->[1] eq "");

		dbg("established connection uses \"" . $rows_ref->[0]->[1] . "\" cipher");
	}
	else
	{
		dbg("established connection is unencrypted");
	}

	# improve performance of selects, see
	# http://search.cpan.org/~capttofu/DBD-mysql-4.028/lib/DBD/mysql.pm
	# for details
	$dbh->{'mysql_use_result'} = 1;
}

sub db_disconnect
{
	dbg("connection: ", (defined($dbh) ? 'defined' : 'UNDEF'));

	if (defined($dbh))
	{
		if (opt('stats'))
		{
			my ($sql_count, $sql_duration) = db_get_stats();
			$total_sql_count += $sql_count;
			$total_sql_duration += $sql_duration;
		}

		my @active_handles = ();

		foreach my $handle (@{$dbh->{'ChildHandles'}})
		{
			if (defined($handle) && $handle->{'Type'} eq 'st' && $handle->{'Active'})
			{
				push(@active_handles, $handle);
			}
		}

		if (@active_handles)
		{
			wrn("called while having " . scalar(@active_handles) . " active statement handle(s)");

			foreach my $handle (@active_handles)
			{
				wrn(generate_db_error($handle, 'active statement'));
				$handle->finish();
			}
		}

		$dbh->disconnect() || wrn($dbh->errstr);
		undef($dbh);
	}
}

# Variable for storing DB session's status variables like 'Handler_read_%'.
#
# Use db_handler_read_status_start() and db_handler_read_status_end()
# to compare different ways of getting data from the DB.
#
# https://dev.mysql.com/doc/refman/5.6/en/server-status-variables.html#statvar_Handler_read_first
my $handler_read_status = {};

sub db_handler_read_status_start()
{
	foreach (@{db_select("show session status like 'Handler_read_%'")})
	{
		$handler_read_status->{$_->[0]} = -$_->[1];
	}
}

sub db_handler_read_status_end()
{
	foreach (@{db_select("show session status like 'Handler_read_%'")})
	{
		$handler_read_status->{$_->[0]} += $_->[1];
	}

	my @cols = (
		"Handler_read_first",
		"Handler_read_key",
		"Handler_read_last",
		"Handler_read_next",
		"Handler_read_prev",
		"Handler_read_rnd",
		"Handler_read_rnd_next",
	);

	my $head = "|";
	my $data = "|";

	foreach my $col (@cols)
	{
		$head .= $col . " |";
		$data .= sprintf("%-*s |", length($col), $handler_read_status->{$col});
	}

	my $line = "-" x length($head);

	info($line);
	info($head);
	info($line);
	info($data);
	info($line);
}

sub db_get_stats()
{
	if (!defined($dbh) || !defined($dbh->{'Profile'}))
	{
		return (undef, undef);
	}

	# check that all profiled DBI methods are "handled" while determining number of queries

	my %allowed_method_names = map { $_ => 1 } (
		'DESTROY',
		'FETCH',
		'FIRSTKEY',
		'STORE',
		'connected',
		'disconnect',
		'execute',
		'fetchall_arrayref',
		'fetchrow_array',
		'prepare',
	);
	my @unhandled_method_names = grep(!exists($allowed_method_names{$_}), keys(%{$dbh->{'Profile'}{'Data'}}));
	fail("Unhandled DBI methods: " . join(', ', @unhandled_method_names)) if (@unhandled_method_names);

	# return number of queries and time spent in DBI

	my $count = $dbh->{'Profile'}{'Data'}{'execute'}[0] if (exists($dbh->{'Profile'}{'Data'}{'execute'}));
	my $duration = dbi_profile_merge_nodes(my $total = [], $dbh->{'Profile'}{'Data'});

	return ($count // 0, $duration);
}

sub db_select($;$)
{
	my $sql = shift;
	my $bind_values = shift; # optional; reference to an array

	my $sth = $dbh->prepare($sql)
		or fail("cannot prepare [$sql]: ", $dbh->errstr);

	if (defined($bind_values))
	{
		dbg("[$sql] ", join(',', @{$bind_values}));

		$sth->execute(@{$bind_values})
			or fail("cannot execute [$sql]: ", $sth->errstr);
	}
	else
	{
		dbg("[$sql]");

		$sth->execute()
			or fail("cannot execute [$sql]: ", $sth->errstr);
	}

	my $rows_ref = $sth->fetchall_arrayref();

	if (opt('debug'))
	{
		if (scalar(@{$rows_ref}) == 1)
		{
			dbg(join(',', map {$_ // 'UNDEF'} (@{$rows_ref->[0]})));
		}
		else
		{
			dbg(scalar(@{$rows_ref}), " rows");
		}
	}

	return $rows_ref;
}

sub db_select_col($;$)
{
	my $sql = shift;
	my $bind_values = shift; # optional; reference to an array

	my $rows = db_select($sql, $bind_values);

	fail("query returned more than one column") if (scalar(@{$rows}) > 0 && scalar(@{$rows->[0]}) > 1);

	return [map($_->[0], @{$rows})];
}

sub db_select_row($;$)
{
	my $sql = shift;
	my $bind_values = shift; # optional; reference to an array

	my $rows = db_select($sql, $bind_values);

	fail("query did not return any row") if (scalar(@{$rows}) == 0);
	fail("query returned more than one row") if (scalar(@{$rows}) > 1);

	return $rows->[0];
}

sub db_select_value($;$)
{
	my $sql = shift;
	my $bind_values = shift; # optional; reference to an array

	my $row = db_select_row($sql, $bind_values);

	fail("query returned more than one value") if (scalar(@{$row}) > 1);

	return $row->[0];
}

sub db_explain($;$)
{
	my $sql = shift;
	my $bind_values = shift; # optional; reference to an array

	my $rows = db_select("explain $sql", $bind_values);

	my @header;
	if (@{$rows->[0]} == 10)
	{
		# MariaDB version - 10.2.24-MariaDB-log
		@header = ("id", "select_type", "table", "type", "possible_keys", "key", "key_len", "ref", "rows", "Extra");
	}
	elsif (@{$rows->[0]} == 12)
	{
		# MySQL version - 5.7.27-0ubuntu0.18.04.1
		@header = ("id", "select_type", "table", "partitions", "type", "possible_keys", "key", "key_len", "ref", "rows", "filtered", "Extra");
	}

	my @col_widths = map(length, @header);

	foreach my $row (@{$rows})
	{
		for (my $i = 0; $i < scalar(@{$row}); $i++)
		{
			$row->[$i] //= "NULL";
			if ($col_widths[$i] < length($row->[$i]))
			{
				$col_widths[$i] = length($row->[$i]);
			}
		}
	}

	my $line_width = 0;
	my $line_format = "";
	for (my $i = 0; $i < scalar(@header); $i++)
	{
		$line_width += 2 + $col_widths[$i] + 1;
		$line_format .= "| %-${col_widths[$i]}s ";
	}
	$line_width += 2;
	$line_format .= " |\n";

	print("-" x $line_width . "\n");
	printf($line_format, @header);
	print("-" x $line_width . "\n");
	foreach my $row (@{$rows})
	{
		printf($line_format, @{$row});
	}
	print("-" x $line_width . "\n");
}

sub db_select_binds
{
	my $sql = shift;
	my $bind_values = shift;

	my $sth = $dbh->prepare($sql)
		or fail("cannot prepare [$sql]: ", $dbh->errstr);

	dbg("[$sql] ", join(',', @{$bind_values}));

	my ($total);

	my @rows;
	foreach my $bind_value (@{$bind_values})
	{
		$sth->execute($bind_value)
			or fail("cannot execute [$sql] bind_value:$bind_value: ", $sth->errstr);

		while (my @row = $sth->fetchrow_array())
		{
			push(@rows, \@row);
		}
	}

	if (opt('debug'))
	{
		if (scalar(@rows) == 1)
		{
			dbg(join(',', map {$_ // 'UNDEF'} (@{$rows[0]})));
		}
		else
		{
			dbg(scalar(@rows), " rows");
		}
	}

	return \@rows;
}

sub db_exec($;$)
{
	my $sql = shift;
	my $bind_values = shift; # optional; reference to an array

	my $sth = $dbh->prepare($sql)
		or fail("cannot prepare [$sql]: ", $dbh->errstr);

	if (defined($bind_values))
	{
		dbg("[$sql] ", join(',', @{$bind_values}));

		$sth->execute(@{$bind_values})
			or fail("cannot execute [$sql]: ", $sth->errstr);
	}
	else
	{
		dbg("[$sql]");

		$sth->execute()
			or fail("cannot execute [$sql]: ", $sth->errstr);
	}

	return $sth->{'mysql_insertid'};
}

sub db_mass_update($$$$$)
{
	# Function for updating LOTS of rows (hundreds, thousands or even tens of thousands) in batches.
	#
	# Example usage:
	#
	# <code>
	# db_mass_update(
	# 	"history_uint",
	# 	["clock", "value"],
	# 	[[1546300800, 1], [1546300860, 2], [1546300920, 3]],
	# 	["clock"],
	# 	[["itemid", 10052]]
	# );
	# </code>
	#
	# Does all following updates, but in a single query:
	#
	# <code>
	# update history_uint set value = 1 where itemid = 10052 and clock = 1546300800;
	# update history_uint set value = 2 where itemid = 10052 and clock = 1546300860;
	# update history_uint set value = 3 where itemid = 10052 and clock = 1546300920;
	# </code>
	#
	# Following calls would have the same effect:
	#
	# <code>
	# db_mass_update(
	# 	"tbl",
	# 	["col1", "col2", "col3"],
	# 	[["val1", "val2", "val3"]],
	# 	["col1", "col2"],
	# 	[["col4", "val4"], ["col5", "val5"]]
	# );
	# </code>
	#
	# <code>
	# update tbl set col3 = val3 where (col1 = val1 and col2 = val2) and (col4 = val4 and col5 = val5);
	# </code>
	#
	# Fields from $values that are listed in $filter_fields are used for filtering.
	# Fields from $values that are not listed in $filter_fields are updated.

	my $table         = shift; # table name
	my $fields        = shift; # names of fields that are present in $values
	my $values        = shift; # values for filtering/updating
	my $filter_fields = shift; # fields from $values that are used for filtering
	my $filter_values = shift; # additional filter values; may be undef or empty array

	my $update_fields = [];

	foreach my $field (@{$fields})
	{
		push(@{$update_fields}, $field) if (!grep($field eq $_, @{$filter_fields}));
	}

	$dbh->begin_work() or fail($dbh->errstr);

	while (my @values_batch = splice(@{$values}, 0, 1000))
	{
		my $subquery;

		foreach (@values_batch)
		{
			if (!defined($subquery))
			{
				$subquery = "select " . join(",", map("? as $_", @{$fields}));
			}
			else
			{
				$subquery .= " union select " . join(",", map("?", @{$fields}));
			}
		}

		my $sql = "update $table"
			. " inner join ($subquery) as update_values on "
			. join(" and ", map("$table.$_=update_values.$_", @{$filter_fields}))
			. " set "
			. join(",", map("$table.$_=update_values.$_", @{$update_fields}));

		if (defined($filter_values) && scalar(@{$filter_values}) > 0)
		{
			$sql .= " where " . join(" and ", map($table . "." . $_->[0] . "=?", @{$filter_values}));
		}

		my @params = (
			map(@{$_}, @values_batch),
			map($_->[1], @{$filter_values})
		);

		db_exec($sql, \@params);
	}

	$dbh->commit() or fail($dbh->errstr);
}

sub set_slv_config
{
	$config = shift;
}

sub current_month_first_cycle
{
	return month_start(time());
}

sub month_start
{
	require DateTime;

	my $dt = DateTime->from_epoch('epoch' => shift());
	$dt->truncate('to' => 'month');
	return $dt->epoch();
}

# Get time bounds for rolling week calculation. Last cycle must be complete.
sub get_rollweek_bounds
{
	my $delay = shift;
	my $now = shift || (time() - $delay);

	my $till = cycle_end($now, $delay);
	my $from = $till - __get_macro('{$RSM.ROLLWEEK.SECONDS}') + 1;

	return ($from, $till, cycle_start($till, $delay));
}

# Get bounds for monthly downtime calculation. $till is the last second of latest calculated test cycle.
# $from is the first second of the month.
sub get_downtime_bounds
{
	my $delay = shift;
	my $now = shift || (time() - $delay);

	require DateTime;

	my $till = cycle_end($now, $delay);

	my $dt = DateTime->from_epoch('epoch' => $till);
	$dt->truncate('to' => 'month');
	my $from = $dt->epoch;

	return ($from, $till, cycle_start($till, $delay));
}

# maximum cycles to process by SLV scripts
sub slv_max_cycles($)
{
	my $service = shift;

	if ($service ne 'dns' && $service ne 'dnssec' && $service ne 'rdap' && $service ne 'rdds')
	{
		fail("unhandled service: '$service'");
	}

	my $var = 'max_cycles_' . $service;

	if (!defined($config))
	{
		fail("missing config");
	}
	if (!defined($config->{'slv'}{$var}))
	{
		fail("missing config option: '$var'");
	}

	return $config->{'slv'}->{$var};
}

#
# Cache probe online/offline statuses
#
# {
#     'Probe1' => {
#         'itemid' => 12345,
#         'statuses' => {
#             clock1 => 1,
#             clock2 => 0,
#             ...
#         }
#     },
#     'Probe2' => {
#         'itemid' => 12346,
#         'statuses' => {
#             clock1 => 1,
#             clock2 => 0,
#             ...
#         }
#     }
# }
my %_PROBESTATUSES;
sub probe_online_at_init()
{
	%_PROBESTATUSES = ();
}

#
# The probe is considered ONLINE only if each minute of the cycle (this is why
# we need a $delay) there is ONLINE result in the database. If any of the cycle
# minute lacks a result or has OFFLINE result the returned value will be OFFLINE.
# Otherwise - ONLINE.
#
sub probe_online_at($$$)
{
	my $probe = shift;
	my $clock = shift;
	my $delay = shift;

	if (!defined($_PROBESTATUSES{$probe}))
	{
		$_PROBESTATUSES{$probe}{'itemid'} = get_probe_online_key_itemid($probe);
	}

	my $clock_limit = $clock + $delay - 1;

	for (my $cl = $clock; $cl < $clock_limit; $cl += PROBE_DELAY)
	{
		if (!defined($_PROBESTATUSES{$probe}{'statuses'}{$cl}))
		{
			my $rows_ref = db_select(
				"select clock,value".
				" from history_uint".
				" where itemid=".$_PROBESTATUSES{$probe}{'itemid'}.
					" and clock between $cl and $clock_limit");

			my %values;
			map {$values{$_->[0]} = $_->[1]} (@{$rows_ref});

			for (my $c = $cl; $c < $clock_limit; $c += PROBE_DELAY)
			{
				# missing value is treated as DOWN
				$_PROBESTATUSES{$probe}{'statuses'}{$c} = $values{$c} // DOWN;
			}
		}

		# the probe is considered OFFLINE during the cycle in case of single DOWN
		return 0 if ($_PROBESTATUSES{$probe}{'statuses'}{$cl} == DOWN);
	}

	# probe was ONLINE during the whole cycle
	return 1;
}

# Translate probe names to hostids of appropriate tld hosts.
#
# E. g., we have hosts (host/hostid):
#   "Probe2"		1
#   "Probe12"		2
#   "org Probe2"	100
#   "org Probe12"	101
# calling
#   probes2tldhostids("org", ["Probe2", "Probe12"])
# will return
#  (100, 101)
sub probes2tldhostids
{
	my $tld = shift;
	my $probes_ref = shift;

	croak("Internal error: invalid argument to probes2tldhostids()") unless (ref($probes_ref) eq 'ARRAY');

	my $result = [];

	return $result if (scalar(@{$probes_ref}) == 0);

	my $hosts_str = '';
	foreach (@{$probes_ref})
	{
		$hosts_str .= ' or ' unless ($hosts_str eq '');
		$hosts_str .= "host='$tld $_'";
	}

	unless ($hosts_str eq "")
	{
		my $rows_ref = db_select("select hostid from hosts where $hosts_str");

		foreach my $row_ref (@$rows_ref)
		{
			push(@{$result}, $row_ref->[0]);
		}
	}

	return $result;
}

sub get_probe_online_key_itemid
{
	my $probe = shift;

	return get_itemid_by_host("$probe - mon", PROBE_KEY_ONLINE);
}

sub init_values
{
	$_sender_values->{'data'} = [];

	if (opt('dry-run'))
	{
		# data that helps format the output nicely
		$_sender_values->{'maxhost'} = 0;
		$_sender_values->{'maxkey'} = 0;
		$_sender_values->{'maxclock'} = 0;
		$_sender_values->{'maxvalue'} = 0;
	}
}

sub push_value
{
	my $hostname   = shift;
	my $key        = shift;
	my $clock      = shift;
	my $value      = shift;
	my $value_type = shift;

	my $info = join('', @_);

	push(@{$_sender_values->{'data'}},
		{
			'tld' => $tld,
			'data' =>
			{
				'host' => $hostname,
				'key' => $key,
				'value' => "$value",
				'clock' => $clock
			},
			'value_type' => $value_type,
			'info' => $info,
		});

	if (opt('dry-run'))
	{
		$_sender_values->{'maxhost'}  = max($_sender_values->{'maxhost'}  // 0, length($hostname));
		$_sender_values->{'maxkey'}   = max($_sender_values->{'maxkey'}   // 0, length($key));
		$_sender_values->{'maxclock'} = max($_sender_values->{'maxclock'} // 0, length($clock));
		$_sender_values->{'maxvalue'} = max($_sender_values->{'maxvalue'} // 0, length($value));
	}
}

#
# send previously collected values:
#
# [
#   {'host' => 'host1', 'key' => 'item1', 'value' => '5', 'clock' => 1391790685},
#   {'host' => 'host2', 'key' => 'item1', 'value' => '4', 'clock' => 1391790685},
#   ...
# ]
#
sub send_values
{
	if (opt('dry-run'))
	{
		my $mh = $_sender_values->{'maxhost'};
		my $mk = $_sender_values->{'maxkey'};
		my $mv = $_sender_values->{'maxvalue'};
		my $mc = $_sender_values->{'maxclock'};

		my $fmt = "%-${mh}s | %${mk}s | %-${mv}s | %-${mc}s | %s";

		# $tld is a global variable which is used in info()
		foreach my $h (@{$_sender_values->{'data'}})
		{
			my $msg = sprintf($fmt,
				$h->{'data'}->{'host'},
				$h->{'data'}->{'key'},
				$h->{'data'}->{'value'},
				ts_str($h->{'data'}->{'clock'}),
				$h->{'info'});

			info($msg);
		}

		return;
	}

	if (opt('fill-gap'))
	{
		foreach my $sender_value (@{$_sender_values->{'data'}})
		{
			my $host  = $sender_value->{'data'}{'host'};
			my $key   = $sender_value->{'data'}{'key'};
			my $clock = $sender_value->{'data'}{'clock'};
			my $value = $sender_value->{'data'}{'value'};

			my $table = history_table($sender_value->{'value_type'});

			my $sql = "insert into $table (itemid,clock,value,ns)" .
				" select items.itemid,?,?,0" .
				" from" .
					" items" .
					" left join hosts on hosts.hostid=items.hostid" .
				" where" .
					" hosts.host=? and" .
					" items.key_=?";

			db_exec($sql, [$clock, $value, $host, $key]);
		}
	}
	else
	{
		my $total_values = scalar(@{$_sender_values->{'data'}});

		if ($total_values == 0)
		{
			dbg(__script(), ": no data collected, nothing to send");
			return;
		}

		my $data = [map($_->{'data'}, @{$_sender_values->{'data'}})];

		if (opt('output-file'))
		{
			my $output_file = getopt('output-file');
			dbg("writing $total_values values to $output_file");
			write_file($output_file, Dumper($data));
		}
		else
		{
			dbg("sending $total_values values");	# send everything in one batch since server should be local
			push_to_trapper($config->{'slv'}->{'zserver'}, $config->{'slv'}->{'zport'}, 10, 5, $data);
		}
	}

	# $tld is a global variable which is used in info()
	my $saved_tld = $tld;
	foreach my $h (@{$_sender_values->{'data'}})
	{
		$tld = $h->{'tld'};
		info(sprintf("%s:%s=%s | %s | %s",
				$h->{'data'}->{'host'},
				$h->{'data'}->{'key'},
				$h->{'data'}->{'value'},
				ts_str($h->{'data'}->{'clock'}),
				$h->{'info'}));
	}
	$tld = $saved_tld;

	check_sent_values()
}

# Returns 0 if hashes are different.
# Returns 1 if hashes are the same.
sub compare_hashes($$)
{
	my $a = shift;
	my $b = shift;

	if (!defined($a) || !defined($b))
	{
		return 0;
	}

	if (keys(%{$a}) != keys(%{$b}))
	{
		return 0;
	}

	foreach my $key (keys(%{$a}))
	{
		if (!exists($b->{$key}))
		{
			return 0;
		}
		if ($a->{$key} ne $b->{$key})
		{
			return 0;
		}
	}

	return 1;
}

# Wait until all pushed values are stored by history syncers on Zabbix Server.
#
# Note: Don't wait for too long, DB transactions on Zabbix Server may fail and
# then data won't be synced. If this happens, warnings will be thrown.
sub check_sent_values()
{
	my $data = [];

	foreach my $sender_value (@{$_sender_values->{'data'}})
	{
		push(
			@{$data},
			{
				'host'       => $sender_value->{'data'}{'host'},
				'key'        => $sender_value->{'data'}{'key'},
				'clock'      => $sender_value->{'data'}{'clock'},
				'value'      => $sender_value->{'data'}{'value'},
				'value_type' => $sender_value->{'value_type'},
				'itemid'     => undef,
			}
		);
	}

	dbg("getting itemids of all pushed items");

	my $host_key_pairs_hash = {};
	my $host_key_pairs_list = [];

	foreach my $value (@{$data})
	{
		my $host = $value->{'host'};
		my $key  = $value->{'key'};

		if (!exists($host_key_pairs_hash->{$host}{$key}))
		{
			$host_key_pairs_hash->{$host}{$key} = undef;
			push(@{$host_key_pairs_list}, [$host, $key]);
		}
	}

	my $itemids = get_itemids_by_hosts_and_keys($host_key_pairs_list);
	my $itemids_list = [map(values(%{$_}), values(%{$itemids}))];

	foreach my $value (@{$data})
	{
		my $host = $value->{'host'};
		my $key  = $value->{'key'};

		$value->{'itemid'} = $itemids->{$host}{$key};
	}

	dbg("getting max pushed clock for each item");

	my $pushed_clocks = {};

	foreach my $value (@{$data})
	{
		my $host   = $value->{'host'};
		my $key    = $value->{'key'};
		my $clock  = $value->{'clock'};
		my $itemid = $value->{'itemid'};

		$pushed_clocks->{$itemid} = max($pushed_clocks->{$itemid} // 0, $clock);
	}

	dbg("waiting until clocks in lastvalue reach pushed clocks");

	# note(1): clocks in lastvalue table might be larger than pushed clocks if script is used for filling a gap
	# note(2): clocks in lastvalue table might fail to reach pushed clocks if some DB transaction fails

	my $itemids_placeholder = join(",", ("?") x scalar(@{$itemids_list}));
	my $lastvalue_sql = "select itemid, clock from lastvalue where itemid in ($itemids_placeholder)";

	my $lastvalue_clocks;
	my $lastvalue_changed_time;

	WAIT_FOR_LASTVALUE:
	while (1)
	{
		select(undef, undef, undef, 0.25);

		my $rows = db_select($lastvalue_sql, $itemids_list);
		my $lastvalue_clocks_tmp = {map { $_->[0] => $_->[1] } @{$rows}};

		if (compare_hashes($lastvalue_clocks_tmp, $lastvalue_clocks))
		{
			my $timeout = 30;

			if (Time::HiRes::time() - $lastvalue_changed_time >= $timeout)
			{
				wrn("lastvalue table hasn't changed for $timeout seconds");
				last WAIT_FOR_LASTVALUE;
			}

			next WAIT_FOR_LASTVALUE;
		}

		$lastvalue_clocks = $lastvalue_clocks_tmp;
		$lastvalue_changed_time = Time::HiRes::time();

		if (keys(%{$lastvalue_clocks}) != keys(%{$pushed_clocks}))
		{
			next WAIT_FOR_LASTVALUE;
		}

		foreach my $itemid (@{$itemids_list})
		{
			if ($lastvalue_clocks->{$itemid} < $pushed_clocks->{$itemid})
			{
				next WAIT_FOR_LASTVALUE;
			}
		}

		last WAIT_FOR_LASTVALUE;
	}

	dbg("get data from history tables");

	my $history_params = {};

	foreach my $value (@{$data})
	{
		my $itemid = $value->{'itemid'};
		my $clock  = $value->{'clock'};
		my $table  = history_table($value->{'value_type'});

		push(@{$history_params->{$table}}, $itemid, $clock);
	}

	my $history = {};

	foreach my $table (keys(%{$history_params}))
	{
		# TODO: it might be needed to group entries by clock, i.e.,
		# where (clock=? and itemid in (?,?,?)) or (clock=? and itemid in (?,?,?))

		my $filter = join(" or ", ("(itemid=? and clock=?)") x (@{$history_params->{$table}} / 2));
		my $sql = "select itemid,value,clock from $table where $filter";
		my $rows = db_select($sql, $history_params->{$table});

		foreach my $row (@{$rows})
		{
			my ($itemid, $value, $clock) = @{$row};

			if (exists($history->{$itemid}{$clock}))
			{
				wrn("THIS SHOULD NOT HAPPEN, value for itemid=$itemid, clock=$clock has duplicates or exists in multiple history tables");
			}

			$history->{$itemid}{$clock} = $value;
		}
	}

	dbg("checking that all pushed data exists in history tables");

	foreach my $value (@{$data})
	{
		my $host          = $value->{'host'};
		my $key           = $value->{'key'};
		my $itemid        = $value->{'itemid'};
		my $clock         = $value->{'clock'};
		my $value_pushed  = $value->{'value'};
		my $value_from_db = $history->{$itemid}{$clock};

		if (!defined($value_from_db))
		{
			my $clock_str = ts_str($clock);

			wrn("VALUE LOST! host=$host, key=$key, itemid=$itemid, clock=$clock_str, value=$value_pushed");
		}
		else
		{
			my $differs = 0;

			if ($value->{'value_type'} == ITEM_VALUE_TYPE_FLOAT)
			{
				$differs = 1 if (abs($value_from_db - $value_pushed) > 0.0001);
			}
			else
			{
				$differs = 1 if ($value_from_db ne $value_pushed);
			}

			if ($differs)
			{
				my $clock_str = ts_str($clock);

				wrn("VALUE DOES NOT MATCH! host=$host, key=$key, itemid=$itemid, clock=$clock_str, value=$value_pushed (got $value_from_db)");
			}
		}
	}
}

sub get_nsip_from_key($)
{
	my $key = shift;

	return "$1,$2" if ($key =~ /rsm.dns.rtt\[(.*),(.*),udp\]/);
	return "$1,$2" if ($key =~ /rsm.dns.rtt\[(.*),(.*),tcp\]/);
	return "$1,$2" if ($key =~ /rsm.dns.nsid\[(.*),(.*)\]/);
	return "$1,$2" if ($key =~ /rsm.slv.dns.ns.avail\[(.*),(.*)\]/);
	return "$1,$2" if ($key =~ /rsm.slv.dns.ns.downtime\[(.*),(.*)\]/);

	wrn("unhandled key: $key");
	return "";
}

sub is_internal_error
{
	my $rtt = shift;

	return 0 unless (defined($rtt));

	return 1 if (ZBX_EC_INTERNAL_FIRST >= $rtt && $rtt >= ZBX_EC_INTERNAL_LAST);	# internal error

	return 0;
}

sub get_value_from_desc
{
	my $desc = shift;

	my $index = index($desc, DETAILED_RESULT_DELIM);

	return ($index == -1 ? $desc : substr($desc, 0, $index));
}

sub is_internal_error_desc
{
	my $desc = shift;

	return 0 unless (defined($desc));
	return 0 unless (str_starts_with($desc, "-"));

	return is_internal_error(get_value_from_desc($desc));
}

sub is_service_error
{
	my $service = shift;
	my $rtt = shift;
	my $rtt_limit = shift;	# optional

	return 0 unless (defined($rtt));

	# not an error
	if ($rtt >= 0)
	{
		return 1 if ($rtt_limit && $rtt > $rtt_limit);

		# rtt within limit
		return 0;
	}

	# internal error
	return 0 if (is_internal_error($rtt));

	# dnssec error
	if (lc($service) eq 'dnssec')
	{
		return 1 if (ZBX_EC_DNS_UDP_DNSSEC_FIRST >= $rtt && $rtt >= ZBX_EC_DNS_UDP_DNSSEC_LAST);
		return 1 if (ZBX_EC_DNS_TCP_DNSSEC_FIRST >= $rtt && $rtt >= ZBX_EC_DNS_TCP_DNSSEC_LAST);

		return 0;
	}

	# other service error
	return 1;
}

# Check full error description and tell if it's a service error.
# E. g. if desc is "-401, DNS UDP - The TLD is configured as DNSSEC-enabled, but no DNSKEY was found in the apex"
# this function will return 1 for dnssec service.
sub is_service_error_desc
{
	my $service = shift;
	my $desc = shift;
	my $rtt_limit = shift;	# optional

	return 0 unless (defined($desc));
	return 0 if ($desc eq "");

	return is_service_error($service, get_value_from_desc($desc), $rtt_limit);
}

# Collect cycles that needs to be calculated in form:
# {
#     value_ts1 : [
#         tld1,
#         tld2,
#         tld3,
#         ...
#     ],
#     value_ts2 : [
#         ...
#     ]
# }
#
# where value_ts is value timestamp of the cycle
sub collect_slv_cycles($$$$$$)
{
	my $tlds_ref    = shift;
	my $delay       = shift;
	my $cfg_key_out = shift;
	my $value_type  = shift;	# value type of $cfg_key_out
	my $max_clock   = shift;	# latest cycle to process
	my $max_cycles  = shift;

	# cache TLD data
	my %cycles;

	my ($lastvalue, $lastclock);

	foreach my $tld (@{$tlds_ref})
	{
		set_log_tld($tld);

		my $itemid = get_itemid_by_host($tld, $cfg_key_out);

		if (get_lastvalue($itemid, $value_type, \$lastvalue, \$lastclock) != SUCCESS)
		{
			# new item; add some shiftback to avoid skipping the cycle because insufficient number of probes
			my $clock = cycle_start($max_clock - 120, $delay);
			push(@{$cycles{$clock}}, $tld);

			next;
		}

		if (opt('fill-gap'))
		{
			my $clock = cycle_start(getopt('fill-gap'), $delay);
			if (!history_value_exists($value_type, $clock, $itemid))
			{
				push(@{$cycles{$clock}}, $tld);
			}
			next;
		}

		next if (!opt('dry-run') && history_value_exists($value_type, $max_clock, $itemid));

		my $cycles_added = 0;

		while ($lastclock < $max_clock && (!$max_cycles || $cycles_added < $max_cycles))
		{
			$lastclock += $delay;

			push(@{$cycles{$lastclock}}, $tld);

			$cycles_added++;
		}
	}

	unset_log_tld();

	return \%cycles;
}

#
# Returns reference to array of Probes that were ONLINE during the
# cycle specified by $clock. The probe is added only if it was
# ONLINE during the whole cycle (that's why $delay is needed).
# Missing probe ONLINE/OFFLINE status at a particular minute of the
# cycle is treated as OFFLINE.
#
sub online_probes($$$)
{
	my $probes_ref = shift;
	my $clock = shift;
	my $delay = shift;

	my @online_probes;

	foreach my $probe (keys(%{$probes_ref}))
	{
		push(@online_probes, $probe) if (probe_online_at($probe, $clock, $delay))
	}

	return \@online_probes;
}

# Process cycles that need to be calculcated.
sub process_slv_avail_cycles($$$$$$$$$)
{
	my $cycles_ref            = shift;
	my $probes_ref            = shift;
	my $delay                 = shift;
	my $cfg_keys_in           = shift;	# if input key(s) is/are known
	my $cfg_keys_in_cb        = shift;	# if input key(s) is/are unknown (DNSSEC, RDDS), call this function to get them
	my $cfg_key_out           = shift;
	my $cfg_minonline         = shift;
	my $check_probe_values_cb = shift;
	my $cfg_value_type        = shift;

	# cache TLD data
	my %keys_in;

	# hash for storing TLDs that should be skipped (e.g., because they don't have enough data)
	my %skip_tlds;

	init_values();

	foreach my $value_ts (sort { $a <=> $b } (keys(%{$cycles_ref})))
	{
		my $from = cycle_start($value_ts, $delay);

		dbg("processing cycle ", ts_str($from), " (delay: $delay)");

		# check if ONLINE/OFFLINE status of all probes is available
		# (i.e., rsm.probe.online item must have values for every minute during the cycle)

		my $sql = "select" .
				" count(*)" .
			" from" .
				" hosts" .
				" inner join items on items.hostid=hosts.hostid" .
				" left join lastvalue on lastvalue.itemid=items.itemid" .
				" inner join hosts_groups on hosts_groups.hostid=hosts.hostid" .
			" where" .
				" hosts.status=? and" .
				" hosts_groups.groupid=? and" .
				" items.key_=? and" .
				" coalesce(lastvalue.clock,0)<?";
		my $params = [HOST_STATUS_MONITORED, PROBES_MON_GROUPID, PROBE_KEY_ONLINE, $from + $delay - 60];
		my $probes_without_status = db_select_value($sql, $params);

		if (db_select_value($sql, $params) > 0)
		{
			dbg("skipping cycle, found $probes_without_status probe(s) without status");
			last;
		}

		# process rsmhosts

		foreach my $tld (@{$cycles_ref->{$value_ts}})
		{
			set_log_tld($tld);

			if (exists($skip_tlds{$tld}))
			{
				next;
			}

			if (!defined($keys_in{$tld}))
			{
				$keys_in{$tld} = $cfg_keys_in // $cfg_keys_in_cb->($tld);
			}

			if (@{$keys_in{$tld}} == 0)
			{
				# fail("cannot get input keys for Service availability calculation");

				# We used to fail here but not anymore because rsm.rdds items can be
				# disabled after switch to RDAP standalone. So some of TLDs may not have
				# RDDS checks thus making SLV calculations for rsm.slv.rdds.* useless

				dbg("no input keys for $tld, considering service disabled");
				next;
			}

			my ($value, $info) = process_slv_avail($tld, $keys_in{$tld}, $cfg_key_out, $value_ts, $from,
				$delay, $cfg_minonline, $probes_ref, $check_probe_values_cb, $cfg_value_type);

			if (!defined($value))
			{
				# something unexpected happened, process_slv_avail() probably wrote it to the log file
				next;
			}

			if ($value == UP_INCONCLUSIVE_NO_DATA && cycle_start(time(), $delay) - $from < WAIT_FOR_AVAIL_DATA)
			{
				# not enough data, but cycle isn't old enough
				$skip_tlds{$tld} = undef;
				next;
			}

			push_value($tld, $cfg_key_out, $value_ts, $value, ITEM_VALUE_TYPE_UINT64, $info);
		}

		unset_log_tld();
	}

	send_values();
}

sub process_slv_avail($$$$$$$$$$)
{
	my $tld                    = shift;
	my $cfg_keys_in            = shift;	# array reference, e. g. ['rsm.dns.rtt[...,udp]', ...]
	my $cfg_key_out            = shift;
	my $value_ts               = shift;
	my $from                   = shift;
	my $delay                  = shift;
	my $cfg_minonline          = shift;
	my $probes_ref             = shift;
	my $check_probe_values_ref = shift;
	my $value_type             = shift;

	if (is_rsmhost_reconfigured($tld, $delay, $from))
	{
		return (UP_INCONCLUSIVE_RECONFIG, "Up (rsmhost has been reconfigured recently)");
	}

	my $online_probes = online_probes($probes_ref, $from, $delay);
	my $online_probe_count = scalar(@{$online_probes});

	if ($online_probe_count < $cfg_minonline)
	{
		if (alerts_enabled() == SUCCESS)
		{
			add_alert(ts_str($value_ts) . "#system#zabbix#$cfg_key_out#PROBLEM#$tld (not enough" .
					" probes online, $online_probe_count while $cfg_minonline required)");
		}

		return (UP_INCONCLUSIVE_NO_PROBES,
			"Up (not enough probes online, $online_probe_count while $cfg_minonline required)");
	}

	my $hostids_ref = probes2tldhostids($tld, $online_probes);
	if (scalar(@$hostids_ref) == 0)
	{
		wrn("no probe hosts found");
		return;
	}

	my $host_items_ref = __get_host_items($hostids_ref, $cfg_keys_in);
	if (scalar(keys(%{$host_items_ref})) == 0)
	{
		wrn("no items (".join(',',@{$cfg_keys_in}).") found");
		return;
	}

	my $till = cycle_end($from, $delay);

	my $values_ref = __get_item_values($host_items_ref, $from, $till, $value_type);

	my $probes_with_results = scalar(@{$values_ref});

	if ($probes_with_results < $cfg_minonline)
	{
		if (alerts_enabled() == SUCCESS)
		{
			add_alert(ts_str($value_ts) . "#system#zabbix#$cfg_key_out#PROBLEM#$tld (not enough" .
					" probes with results, $probes_with_results while $cfg_minonline required)");
		}

		return (UP_INCONCLUSIVE_NO_DATA,
			"Up (not enough probes with results, $probes_with_results while $cfg_minonline required)");
	}

	my $probes_with_positive = 0;

	foreach my $probe_values (@{$values_ref})
	{
		my $result = $check_probe_values_ref->($probe_values);

		$probes_with_positive++ if (SUCCESS == $result);

		next unless (opt('debug'));

		dbg("probe result: ", (SUCCESS == $result ? "up" : "down"));
	}

	my $perc = $probes_with_positive * 100 / $probes_with_results;
	my $detailed_info = sprintf("%d/%d positive, %.3f%%", $probes_with_positive, $probes_with_results, $perc);

	if ($perc > SLV_UNAVAILABILITY_LIMIT)
	{
		return (UP, "Up ($detailed_info)");
	}
	else
	{
		return (DOWN, "Down ($detailed_info)");
	}
}

sub process_slv_rollweek_cycles($$$$$)
{
	my $cycles_ref = shift;
	my $delay = shift;
	my $cfg_key_in = shift;
	my $cfg_key_out = shift;
	my $cfg_sla = shift;

	my %itemids;

	init_values();

	foreach my $clock (sort { $a <=> $b } keys(%{$cycles_ref}))
	{
		my ($from, $till, $value_ts) = get_rollweek_bounds($delay, $clock);

		dbg("selecting period ", selected_period($from, $till), " (value_ts:", ts_str($clock), ")");

		foreach my $tld (@{$cycles_ref->{$clock}})
		{
			set_log_tld($tld);

			$itemids{$tld}{'itemid_in'} = get_itemid_by_host($tld, $cfg_key_in) unless ($itemids{$tld}{'itemid_in'});
			$itemids{$tld}{'itemid_out'} = get_itemid_by_host($tld, $cfg_key_out) unless ($itemids{$tld}{'itemid_out'});

			next if (!opt('dry-run') && float_value_exists($value_ts, $itemids{$tld}{'itemid_out'}));

			# skip calculation if Service Availability value is not yet there
			next if (!opt('dry-run') && !uint_value_exists($value_ts, $itemids{$tld}{'itemid_in'}));

			my $downtime = get_downtime($itemids{$tld}{'itemid_in'}, $from, $till, undef, undef, $delay);	# consider incidents
			my $perc = sprintf("%.3f", $downtime * 100 / $cfg_sla);

			push_value($tld, $cfg_key_out, $value_ts, $perc, ITEM_VALUE_TYPE_FLOAT, "result: $perc% (down: $downtime minutes, sla: $cfg_sla)");
		}

		unset_log_tld();
	}

	send_values();
}

sub process_slv_downtime_cycles($$$$)
{
	my $cycles_ref = shift;
	my $delay = shift;
	my $cfg_key_in = shift;
	my $cfg_key_out = shift;

	my $sth = get_downtime_prepare();

	my %itemids;

	init_values();

	foreach my $clock (sort { $a <=> $b } keys(%{$cycles_ref}))
	{
		my ($from, $till, $value_ts) = get_downtime_bounds($delay, $clock);

		dbg("selecting period ", selected_period($from, $till), " (value_ts:", ts_str($clock), ")");

		foreach my $tld (@{$cycles_ref->{$clock}})
		{
			set_log_tld($tld);

			$itemids{$tld}{'itemid_in'} = get_itemid_by_host($tld, $cfg_key_in) unless ($itemids{$tld}{'itemid_in'});
			$itemids{$tld}{'itemid_out'} = get_itemid_by_host($tld, $cfg_key_out) unless ($itemids{$tld}{'itemid_out'});

			next if (!opt('dry-run') && uint_value_exists($value_ts, $itemids{$tld}{'itemid_out'}));

			# skip calculation if Service Availability value is not yet there
			next if (!opt('dry-run') && !uint_value_exists($value_ts, $itemids{$tld}{'itemid_in'}));

			my $downtime;
			if ($cfg_key_out eq 'rsm.slv.dns.downtime' && $value_ts == $from)
			{
				# There's a trigger "DNS service was unavailable for at least 1m", it goes into PROBLEM state
				# as soon as 'rsm.slv.dns.downtime' is larger than 0. Value of 'rsm.slv.dns.downtime' resets
				# to 0 at the beginning of each month. This also changes the state of the trigger to OK state
				# at the beginning of the month.
				#
				# If an incident is active when the month switches, then value of 'rsm.slv.dns.downtime'
				# resets to 0 *and* increases by 1 on the first cycle, because DNS service is down. This
				# prevents trigger from switching to OK state and then again to PROBLEM state. As a result,
				# alerts aren't being sent when the month switches.
				#
				# Workaround - always store 0 minutes of downtime on the first cycle of the month. This
				# will make sure that trigger switches to OK state. This must be compensated on the second
				# cycle of the month (i.e., downtime on the second cycle must be 2 if incident is active
				# and availability on both first and second cycle of the month is DOWN).

				$downtime = 0;
			}
			else
			{
				$downtime = get_downtime_execute($sth, $itemids{$tld}{'itemid_in'}, $from, $till, 0, $delay);
			}

			push_value($tld, $cfg_key_out, $value_ts, $downtime, ITEM_VALUE_TYPE_UINT64, ts_str($from), " - ", ts_str($till));
		}

		unset_log_tld();
	}

	send_values();
}

# organize values grouped by hosts:
#
# [
#     {
#         'foo[a,b]' => [1],
#         'bar[c,d]' => [-201]
#     },
#     {
#         'foo[a,b]' => [34],
#         'bar[c,d]' => [27, 14]
#     },
#     ...
# ]

sub __get_item_values($$$$)
{
	my $host_items_ref = shift;
	my $from = shift;
	my $till = shift;
	my $value_type = shift;

	return [] if (scalar(keys(%{$host_items_ref})) == 0);

	my %item_host_ids_map = map {
		my $hostid = $_;
		map { $_ => $hostid } (keys(%{$host_items_ref->{$hostid}}))
	} (keys(%{$host_items_ref}));

	my @itemids = map { keys(%{$_}) } (values(%{$host_items_ref}));

	return [] if (scalar(@itemids) == 0);

	my $rows_ref = db_select(
		"select itemid,value".
		" from " . history_table($value_type).
		" where itemid in (" . join(',', @itemids) . ")".
			" and clock between $from and $till".
		" order by clock");

	my %result;

	foreach my $row_ref (@$rows_ref)
	{
		my $itemid = $row_ref->[0];
		my $value = $row_ref->[1];

		my $hostid = $item_host_ids_map{$itemid};
		my $key = $host_items_ref->{$hostid}->{$itemid};

		push(@{$result{$hostid}->{$key}}, $value);

		dbg("  h:$hostid $key=$value");
	}

	return [values(%result)];
}

sub uint_value_exists($$)
{
        my $clock = shift;
        my $itemid = shift;

        my $rows_ref = db_select("select 1 from history_uint where itemid=$itemid and clock=$clock");

        return 1 if (defined($rows_ref->[0]->[0]));

        return 0;
}

sub float_value_exists($$)
{
        my $clock = shift;
        my $itemid = shift;

        my $rows_ref = db_select("select 1 from history where itemid=$itemid and clock=$clock");

        return 1 if (defined($rows_ref->[0]->[0]));

        return 0;
}

sub history_value_exists($$$)
{
	my $value_type = shift;
        my $clock = shift;
        my $itemid = shift;

	my $rows_ref;

	return uint_value_exists($clock, $itemid) if ($value_type == ITEM_VALUE_TYPE_UINT64);
	return float_value_exists($clock, $itemid) if ($value_type == ITEM_VALUE_TYPE_FLOAT);

	fail("internal error: value type $value_type is not supported by function history_value_exists()");
}

sub __make_incident
{
	my %h;

	$h{'eventid'} = shift;
	$h{'false_positive'} = shift;
	$h{'event_clock'} = shift;
	$h{'start'} = shift;
	$h{'end'} = shift;

	return \%h;
}

sub sql_time_condition
{
	my $from = shift;
	my $till = shift;
	my $clock_field = shift;

	$clock_field = "clock" unless (defined($clock_field));

	if (defined($from) and not defined($till))
	{
		return "$clock_field>=$from";
	}

	if (not defined($from) and defined($till))
	{
		return "$clock_field<=$till";
	}

	if (defined($from) and defined($till))
	{
		return "$clock_field=$from" if ($from == $till);
		fail("invalid time conditions: from=$from till=$till") if ($from > $till);
		return "$clock_field between $from and $till";
	}

	return "1=1";
}

# return incidents as an array reference (sorted by time):
#
# [
#     {
#         'eventid' => '5881',
#         'start' => '1418272230',
#         'end' => '1418273230',
#         'false_positive' => '0'
#     },
#     {
#         'eventid' => '6585',
#         'start' => '1418280000',
#         'false_positive' => '1'
#     }
# ]
#
# An incident is a period when the problem was active. This period is
# limited by 2 events, the PROBLEM event and the first OK event after
# that.
#
# Incidents are returned within time limits specified by $from and $till.
# If an incident is on-going at the $from time the event "start" time is
# used. In case event is on-going at time specified as $till it's "end"
# time is not defined.
sub get_incidents
{
	my $itemid = shift;
	my $delay = shift;
	my $from = shift;
	my $till = shift;

	dbg(selected_period($from, $till));

	my (@incidents, $rows_ref, $row_ref);

	$rows_ref = db_select(
		"select distinct t.triggerid".
		" from triggers t,functions f".
		" where t.triggerid=f.triggerid".
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

	if (defined($from))
	{
		# first check for ongoing incident
		$rows_ref = db_select(
			"select max(clock)".
			" from events".
			" where object=".EVENT_OBJECT_TRIGGER.
				" and source=".EVENT_SOURCE_TRIGGERS.
				" and objectid=$triggerid".
				" and clock<$from");

		$row_ref = $rows_ref->[0];

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

			if (opt('debug'))
			{
				my $type = ($value == TRIGGER_VALUE_FALSE ? 'closing' : 'opening');
				dbg("$type pre-event $eventid: clock:" . ts_str($clock) . " ($clock), false_positive:$false_positive");
			}

			# do not add 'value=TRIGGER_VALUE_TRUE' to SQL above just for corner case of 2 events at the same second
			if ($value == TRIGGER_VALUE_TRUE)
			{
				push(@incidents, __make_incident($eventid, $false_positive, $clock, cycle_start($clock, $delay)));

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

		# NB! Incident start/end times must not be truncated to first/last second
		# of a minute (do not use truncate_from and truncate_till) because they
		# can be used by a caller to identify an incident.

		if (opt('debug'))
		{
			my $type = ($value == TRIGGER_VALUE_FALSE ? 'closing' : 'opening');
			dbg("$type event $eventid: clock:" . ts_str($clock) . " ($clock), false_positive:$false_positive");
		}

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
				$incidents[$idx]->{'event_clock'} = $clock;
			}
		}

		next if ($value == $last_trigger_value);

		if ($value == TRIGGER_VALUE_FALSE)
		{
			# event that closes the incident
			my $idx = scalar(@incidents) - 1;

			$incidents[$idx]->{'end'} = cycle_end($clock, $delay);
		}
		else
		{
			# event that starts an incident
			push(@incidents, __make_incident($eventid, $false_positive, $clock, cycle_start($clock, $delay)));
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

sub get_downtime
{
	my $itemid = shift;
	my $from = shift;
	my $till = shift;
	my $ignore_incidents = shift;	# if set check the whole period
	my $incidents_ref = shift;	# optional reference to array of incidents, ignored if $ignore_incidents is true
	my $delay = shift;		# only needed if incidents are not ignored and are not supplied by caller

	my $incidents;
	if ($ignore_incidents)
	{
		push(@$incidents, __make_incident(0, 0, 0, $from, $till));
	}
	elsif ($incidents_ref)
	{
		$incidents = $incidents_ref;
	}
	else
	{
		$incidents = get_incidents($itemid, $delay, $from, $till);
	}

	my $count = 0;
	my $downtime = 0;

	foreach (@{$incidents})
	{
		my $false_positive = $_->{'false_positive'};
		my $period_from = $_->{'start'};
		my $period_till = $_->{'end'};

		if (($period_from < $from) && defined($period_till) && ($period_till < $from))
		{
			fail("internal error: incident outside time bounds, check function get_incidents()");
		}

		$period_from = $from if ($period_from < $from);
		$period_till = $till unless (defined($period_till)); # last incident may be ongoing

		next if ($false_positive != 0);

		my $rows_ref = db_select(
			"select value,clock".
			" from history_uint".
			" where itemid=$itemid".
				" and " . sql_time_condition($period_from, $period_till).
			" order by clock");

		my $prevclock;

		foreach my $row_ref (@{$rows_ref})
		{
			my $value = $row_ref->[0];
			my $clock = $row_ref->[1];

			if (defined($prevclock) && $clock - $prevclock != $delay)
			{
				my $prevclock_ts = ts_full($prevclock);
				my $clock_ts = ts_full($clock);

				my $info = "itemid=$itemid, prevclock=$prevclock_ts, clock=$clock_ts, delay=$delay";

				if ($clock - $prevclock > $delay)
				{
					fail("cycle is missing availability value ($info)");
				}
				else
				{
					fail("cycle has more than one availability value ($info)");
				}
			}

			if ($value == DOWN)
			{
				$downtime += $delay;
			}

			$prevclock = $clock;
		}
	}

	# return minutes
	return int($downtime / 60);
}

sub get_downtime_prepare
{
	my $query =
		"select value,clock".
		" from history_uint".
		" where itemid=?".
			" and clock between ? and ?".
		" order by clock";

	my $sth = $dbh->prepare($query)
		or fail("cannot prepare [$query]: ", $dbh->errstr);

	dbg("[$query]");

	return $sth;
}

sub get_downtime_execute
{
	my $sth = shift;
	my $itemid = shift;
	my $from = shift;
	my $till = shift;
	my $ignore_incidents = shift;	# if set check the whole period
	my $delay = shift;

	my $incidents;
	if ($ignore_incidents)
	{
		my %h;

		$h{'start'} = $from;
		$h{'end'} = $till;
		$h{'false_positive'} = 0;

		push(@$incidents, \%h);
	}
	else
	{
		$incidents = get_incidents($itemid, $delay, $from, $till);
	}

	my $count = 0;
	my $downtime = 0;

	foreach (@{$incidents})
	{
		my $false_positive = $_->{'false_positive'};
		my $period_from = $_->{'start'};
		my $period_till = $_->{'end'};

		if (($period_from < $from) && defined($period_till) && ($period_till < $from))
		{
			fail("internal error: incident outside time bounds, check function get_incidents()")
		}

		$period_from = $from if ($period_from < $from);
		$period_till = $till unless (defined($period_till)); # last incident may be ongoing

		next if ($false_positive != 0);

		$sth->bind_param(1, $itemid, SQL_INTEGER);
		$sth->bind_param(2, $period_from, SQL_INTEGER);
		$sth->bind_param(3, $period_till, SQL_INTEGER);

		$sth->execute()
			or fail("cannot execute query: ", $sth->errstr);

		my ($value, $clock);
		$sth->bind_columns(\$value, \$clock);

		my $prevclock;

		while ($sth->fetch())
		{
			if (defined($prevclock) && $clock - $prevclock != $delay)
			{
				my $prevclock_ts = ts_full($prevclock);
				my $clock_ts = ts_full($clock);

				my $info = "itemid=$itemid, prevclock=$prevclock_ts, clock=$clock_ts, delay=$delay";

				if ($clock - $prevclock > $delay)
				{
					fail("cycle is missing availability value ($info)");
				}
				else
				{
					fail("cycle has more than one availability value ($info)");
				}
			}

			if ($value == DOWN)
			{
				$downtime += $delay;
			}

			$prevclock = $clock;
		}
	}

	# return minutes
	return int($downtime / 60);
}

sub history_table($)
{
	my $value_type = shift;

	return "history_uint" if (!defined($value_type) || $value_type == ITEM_VALUE_TYPE_UINT64);	# default
	return "history" if ($value_type == ITEM_VALUE_TYPE_FLOAT);
	return "history_str" if ($value_type == ITEM_VALUE_TYPE_STR);

	fail("THIS_SHOULD_NEVER_HAPPEN");
}

# returns:
# SUCCESS - last clock and value found
# E_FAIL  - nothing found
sub get_lastvalue($$$$)
{
	my $itemid = shift;
	my $value_type = shift;
	my $value_ref = shift;
	my $clock_ref = shift;

	fail("THIS_SHOULD_NEVER_HAPPEN") unless ($clock_ref || $value_ref);

	my $rows_ref;

	if ($value_type == ITEM_VALUE_TYPE_FLOAT || $value_type == ITEM_VALUE_TYPE_UINT64)
	{
		$rows_ref = db_select("select value,clock from lastvalue where itemid=$itemid");
	}
	else
	{
		$rows_ref = db_select("select value,clock from lastvalue_str where itemid=$itemid");
	}

	if (@{$rows_ref})
	{
		$$value_ref = $rows_ref->[0]->[0] if ($value_ref);
		$$clock_ref = $rows_ref->[0]->[1] if ($clock_ref);

		return SUCCESS;
	}

	return E_FAIL;
}

#
# returns array of itemids: [itemid1, itemid2 ...]
#
sub get_itemids_by_hostids
{
	my $hostids_ref = shift;
	my $all_items = shift;

	my $result = [];

	foreach my $hostid (@$hostids_ref)
	{
		unless ($all_items->{$hostid})
		{
			dbg("\nhostid $hostid from:\n", Dumper($hostids_ref), "was not found in:\n", Dumper($all_items)) if (opt('debug'));
			fail("internal error: no hostid $hostid in input items");
		}

		foreach my $itemid (keys(%{$all_items->{$hostid}}))
		{
			push(@{$result}, $itemid);
		}
	}

	return $result;
}

#
# returns array of itemids: [itemid1, itemid2, ...]
#
sub get_itemids_by_key_pattern_and_hosts($$;$)
{
	my $key_pattern = shift; # pattern for 'items.key_ like ...' condition
	my $hosts       = shift; # ref to array of hosts, e.g., ['tld1', 'tld2', ...]
	my $item_status = shift; # optional; ITEM_STATUS_ACTIVE or ITEM_STATUS_DISABLED

	my $hosts_placeholder = join(",", ("?") x scalar(@{$hosts}));

	my $item_status_condition = defined($item_status) ? ("items.status=" . $item_status . " and") : "";

	my $bind_values = [$key_pattern, @{$hosts}];
	my $rows = db_select(
		"select items.itemid" .
		" from items left join hosts on hosts.hostid=items.hostid" .
		" where $item_status_condition" .
			" items.key_ like ? and" .
			" hosts.host in ($hosts_placeholder)", $bind_values);

	return [map($_->[0], @{$rows})];
}

# organize values from all probes grouped by nsip and return "nsip"->values hash
#
# {
#     'ns1,192.0.34.201' => {
#                   'itemid' => 23764,
#                   'values' => [
#                                 '-204.0000',
#                                 '-204.0000',
#                                 '-204.0000',
#                                 '-204.0000',
#                                 '-204.0000',
# ...
sub get_nsip_values
{
	my $itemids_ref = shift;
	my $times_ref = shift; # from, till, ...
	my $items_ref = shift;

	my $result = {};

	if (scalar(@$itemids_ref) != 0)
	{
		my $itemids_str = join(',', @{$itemids_ref});

		my $idx = 0;
		my $times_count = scalar(@$times_ref);
		while ($idx < $times_count)
		{
			my $from = $times_ref->[$idx++];
			my $till = $times_ref->[$idx++];

			my $rows_ref = db_select("select itemid,value from history where itemid in ($itemids_str) and " . sql_time_condition($from, $till). " order by clock");

			foreach my $row_ref (@$rows_ref)
			{
				my $itemid = $row_ref->[0];
				my $value = $row_ref->[1];

				my $nsip;
				my $last = 0;
				foreach my $hostid (keys(%$items_ref))
				{
					foreach my $i (keys(%{$items_ref->{$hostid}}))
					{
						if ($i == $itemid)
						{
							$nsip = $items_ref->{$hostid}{$i};
							$last = 1;
							last;
						}
					}
					last if ($last == 1);
				}

				fail("internal error: name server of item $itemid not found") unless (defined($nsip));

				unless (exists($result->{$nsip}))
				{
					$result->{$nsip} = {
						'itemid'	=> $itemid,
						'values'	=> []
					};
				}

				push(@{$result->{$nsip}->{'values'}}, $value);
			}
		}
	}

	return $result;
}

sub __get_valuemappings
{
	my $vmname = shift;

	my $rows_ref = db_select("select m.value,m.newvalue from valuemaps v,mappings m where v.valuemapid=m.valuemapid and v.name='$vmname'");

	my $result = {};

	foreach my $row_ref (@$rows_ref)
	{
		$result->{$row_ref->[0]} = $row_ref->[1];
	}

	return $result;
}

# todo: the $vmname's must be fixed accordingly
# todo: also, consider renaming to something like get_rtt_valuemaps()
sub get_valuemaps
{
	my $service = shift;

	my $vmname;
	if ($service eq 'dns' or $service eq 'dnssec')
	{
		$vmname = 'RSM DNS rtt';
	}
	elsif ($service eq 'rdds')
	{
		$vmname = 'RSM RDDS rtt';
	}
	elsif ($service = 'rdap')
	{
		$vmname = 'RSM RDAP rtt';
	}
	elsif ($service eq 'epp')
	{
		$vmname = 'RSM EPP rtt';
	}
	else
	{
		fail("service '$service' is unknown");
	}

	return __get_valuemappings($vmname);
}

# todo: the $vmname's must be fixed accordingly
# todo: also, consider renaming to something like get_result_valuemaps()
sub get_statusmaps
{
	my $service = shift;

	my $vmname;
	if ($service eq 'dns' or $service eq 'dnssec')
	{
		# todo: this will be used later (many statuses)
		#$vmname = 'RSM DNS result';
		return undef;
	}
	elsif ($service eq 'rdds')
	{
		$vmname = 'RSM RDDS result';
	}
	elsif ($service eq 'epp')
	{
		$vmname = 'RSM EPP result';
	}
	else
	{
		fail("service '$service' is unknown");
	}

	return __get_valuemappings($vmname);
}

sub get_avail_valuemaps
{
	return __get_valuemappings('RSM Service Availability');
}

sub get_detailed_result
{
	my $maps = shift;
	my $value = shift;

	return undef unless (defined($value));

	my $value_int = int($value);

	return $value_int unless (exists($maps->{$value_int}));

	return $value_int . DETAILED_RESULT_DELIM . $maps->{$value_int};
}

sub get_result_string
{
	my $maps = shift;
	my $value = shift;

	my $value_int = int($value);

	return $value_int unless ($maps);
	return $value_int unless (exists($maps->{$value_int}));

	return $maps->{$value_int};
}

# returns (tld, service)
sub get_tld_by_trigger
{
	my $triggerid = shift;

	my $rows_ref = db_select("select distinct itemid from functions where triggerid=$triggerid");

	my $itemid = $rows_ref->[0]->[0];

	return (undef, undef) unless ($itemid);

	dbg("itemid:$itemid");

	$rows_ref = db_select("select hostid,substring(key_,9,locate('.avail',key_)-9) as service from items where itemid=$itemid");

	my $hostid = $rows_ref->[0]->[0];
	my $service = $rows_ref->[0]->[1];

	fail("cannot get TLD by itemid $itemid") unless ($hostid);

	dbg("hostid:$hostid");

	$rows_ref = db_select("select host from hosts where hostid=$hostid");

	return ($rows_ref->[0]->[0], $service);
}

# truncate specified unix timestamp to 0 seconds
sub truncate_from
{
	my $ts = shift;

	return $ts - ($ts % 60);
}

# truncate specified unix timestamp to 59 seconds
sub truncate_till
{
	return truncate_from(shift) + 59;
}

# whether additional alerts through Redis are enabled, disable in config passed with set_slv_config()
sub alerts_enabled
{
	return SUCCESS if ($config && $config->{'redis'} && $config->{'redis'}->{'enabled'} && ($config->{'redis'}->{'enabled'} ne "0"));

	return E_FAIL;
}

# returns beginning of the test period if specified upper bound is within it,
# 0 otherwise
sub get_test_start_time
{
	my $till = shift;	# must be :59 seconds
	my $delay = shift;	# service delay in seconds (e. g. DNS: 60)

	my $remainder = $till % 60;

	fail("internal error: first argument to get_test_start_time() must be :59 seconds") unless ($remainder == 59);

	$till++;

	$remainder = $till % $delay;

	return 0 if ($remainder != 0);

	return $till - $delay;
}

# $services is a hash reference of services that need to be checked.
# For each service the delay must be provided. "from" and "till" values
# will be set for services whose tests fall under given time between
# $check_from and $check_till.
#
# Input $services:
#
# [
#   {'dns' => {'delay' => 60}},
#   {'rdds' => {'delay' => 300}}
# ]
#
# Output $services:
#
# [
#   {'dns' => {'delay' => 60, 'from' => 1234234200, 'till' => 1234234259}}	# <- test period found for 'dns' but not for 'rdds'
# ]
#
# The return value is min($from), max($till) from all found periods
#
sub get_real_services_period
{
	my $services = shift;
	my $check_from = shift;
	my $check_till = shift;

	my ($from, $till);

	# adjust test and probe periods we need to calculate for
	foreach my $service (values(%{$services}))
	{
		my $delay = $service->{'delay'};

		my ($loop_from, $loop_till);

		# go through the check period minute by minute selecting test cycles
		for ($loop_from = $check_from, $loop_till = $loop_from + 59;
				(!$service->{'from'} || $service->{'till'}) && $loop_from < $check_till;
				$loop_from += 60, $loop_till += 60)
		{
			my $test_from = get_test_start_time($loop_till, $delay);

			next if ($test_from == 0);

			if (!$from || $from > $test_from)
			{
				$from = $test_from;
			}

			if (!$till || $till < $loop_till)
			{
				$till = $loop_till;
			}

			if (!$service->{'from'})
			{
				$service->{'from'} = $test_from;
			}

			if (!$service->{'till'} || $service->{'till'} < $loop_till)
			{
				$service->{'till'} = $loop_till;
			}
		}
	}

	return ($from, $till);
}

sub format_stats_time
{
	my $time = shift;

	my $m = int($time / 60);
	my $s = $time - $m * 60;

	return sprintf("%dm %ds", $m, $s) if ($m != 0);

	return sprintf("%.3lfs", $s);
}

# Call this function from child, to open separate log file handler and reset stats.
sub init_process
{
	$log_open = 0;
	$start_time = Time::HiRes::time();
	$total_sql_count = 0;
	$total_sql_duration = 0.0;
}

# this will be used for making sure only one copy of script runs (see function __is_already_running())
my $pidfile;
use constant PID_DIR => '/tmp';

# avoid messed up output from parallel processes
my ($stdout_lock_handle, $stdout_lock_file);;

sub finalize_process
{
	my $rv = shift // SUCCESS;

	if (defined($pidfile) && $pidfile->pid == $$)
	{
		$pidfile->remove() or wrn("cannot unlink pid file");
	}

	db_disconnect();

	if (SUCCESS == $rv && opt('stats'))
	{
		info(sprintf("%sPID (%d), total: %s, sql: %s (%d queries)",
				$tld ? "$tld " : '',
				$$,
				format_stats_time(Time::HiRes::time() - $start_time),
				format_stats_time($total_sql_duration),
				$total_sql_count));
	}

	unlink($stdout_lock_file) if (defined($stdout_lock_file));

	closelog();
}

sub slv_exit
{
	my $rv = shift;

	finalize_process($rv);

	if ($rv != SUCCESS && log_stacktrace())
	{
		map { __log('err', $_) } split("\n", Devel::StackTrace->new()->as_string());
	}

	exit($rv);
}

sub __is_already_running()
{
	my $filename = __get_pidfile();

	$pidfile = File::Pid->new({ file => $filename });

	fail("cannot lock script") unless (defined($pidfile));

	$pidfile->write() or fail("cannot write to a pid file ", $pidfile->file);

	# the only instance running is us
	return if ($pidfile->pid == $$);

	# pid file exists, see if the pid in it is valid
	my $pid = $pidfile->running();

	if ($pid)
	{
		# yes, we have another instance running
		return $pid;
	}

	# invalid pid in the pid file, update it
	$pidfile->pid($$);
	$pidfile->write() or fail("cannot write to a pid file ", $pidfile->file);

	return;
}

sub fail_if_running()
{
	return if (opt('dry-run'));

	my $pid = __is_already_running();

	if ($pid)
	{
		fail(__script() . " is already running (pid:$pid)");
	}
}

sub exit_if_running()
{
	return if (opt('dry-run') || opt('now'));

	my $pid = __is_already_running();

	if ($pid)
	{
		wrn(__script() . " is already running (pid:$pid)");
		exit 0;
	}
}

sub dbg
{
	return unless (opt('debug'));

	__log('debug', join('', @_));
}

sub info
{
	__log('info', join('', @_));
}

sub wrn
{
	__log('warning', join('', @_));
}

my $on_fail_cb;

sub set_on_fail
{
	$on_fail_cb = shift;
}

my $log_only_message = 0;

sub log_only_message(;$)
{
	my $flag = shift;

	my $prev = $log_only_message;

	if (defined($flag))
	{
		$log_only_message = $flag;
	}

	return $prev;
}

my $log_stacktrace = 1;

sub log_stacktrace(;$)
{
	my $flag = shift;

	my $prev = $log_stacktrace;

	if (defined($flag))
	{
		$log_stacktrace = $flag;
	}

	return $prev;
}

sub fail
{
	__log('err', join('', @_));

	if ($on_fail_cb)
	{
		dbg("script failed, calling \"on fail\" callback...");
		$on_fail_cb->();
		dbg("\"on fail\" callback finished");
	}

	slv_exit(E_FAIL);
}

sub ltrim($)
{
	my $string = shift;

	return $string =~ s/^\s+//r;
}

sub rtrim($)
{
	my $string = shift;

	return $string =~ s/\s+$//r;
}

sub trim($)
{
	my $string = shift;

	return $string =~ s/^\s+|\s+$//gr;
}

sub str_starts_with($$;$$)
{
	my $string = shift;

	while (my $prefix = shift)
	{
		return 1 if (rindex($string, $prefix, 0) == 0);
	}

	return 0;
}

sub str_ends_with($$)
{
	my $string = shift;
	my $suffix = shift;

	return substr($string, -length($suffix)) eq $suffix;
}

sub parse_opts
{
	if (!GetOptions(\%OPTS, 'help', 'dry-run', 'warnslow=f', 'nolog', 'debug', 'stats', @_))
	{
		pod2usage(-verbose => 0, -input => $POD2USAGE_FILE);
	}

	if (opt('help'))
	{
		pod2usage(-verbose => 1, -input => $POD2USAGE_FILE);
	}

	setopt('nolog') if (opt('dry-run') || opt('debug'));

	$start_time = Time::HiRes::time() if (opt('stats'));

	if (opt('debug'))
	{
		dbg("command-line parameters:");
		dbg("$_ => ", getopt($_)) foreach (optkeys());
	}
}

sub parse_slv_opts
{
	$POD2USAGE_FILE = '/opt/zabbix/scripts/slv/rsm.slv.usage';

	parse_opts('tld=s', 'now=i', 'cycles=i', 'output-file=s', 'fill-gap=i');
}

sub override_opts($)
{
	my $new_opts = shift;

	%OPTS = %{$new_opts};
}

sub opt
{
	return defined($OPTS{shift()});
}

sub getopt
{
	return $OPTS{shift()};
}

sub setopt
{
	my $key = shift;
	my $value = shift;

	$value = 1 unless (defined($value));

	$OPTS{$key} = $value;
}

sub unsetopt
{
	$OPTS{shift()} = undef;
}

sub optkeys
{
	return keys(%OPTS);
}

sub ts_str
{
	my $ts = shift // time();

	# sec, min, hour, mday, mon, year, wday, yday, isdst
	my ($sec, $min, $hour, $mday, $mon, $year) = localtime($ts);

	return sprintf("%.4d%.2d%.2d:%.2d%.2d%.2d", $year + 1900, $mon + 1, $mday, $hour, $min, $sec);
}

sub ts_full
{
	my $ts = shift // time();

	my $str = ts_str($ts);

	return "$str ($ts)";
}

sub ts_ymd
{
	my $ts    = shift // time();
	my $delim = shift // '';

	my (undef, undef, undef, $mday, $mon, $year) = localtime($ts);

	return sprintf("%.4d%s%.2d%s%.2d", $year + 1900, $delim, $mon + 1, $delim, $mday);
}

sub ts_ym
{
	my $ts    = shift // time();
	my $delim = shift // '-';

	my (undef, undef, undef, undef, $mon, $year) = localtime($ts);

	return sprintf("%.4d%s%.2d", $year + 1900, $delim, $mon + 1);
}

sub ts_hms
{
	my $ts    = shift // time();
	my $delim = shift // '';

	my ($sec, $min, $hour) = localtime($ts);

	return sprintf("%.2d%s%.2d%s%.2d", $hour, $delim, $min, $delim, $sec);
}

sub selected_period
{
	my $from = shift;
	my $till = shift;

	my ($from_date, $from_time, $till_date, $till_time);

	if ($from)
	{
		$from_date = ts_ymd($from);
		$from_time = ts_hms($from);
	}

	if ($till)
	{
		$till_date = ts_ymd($till);
		$till_time = ts_hms($till);
	}

	if ($from and $till)
	{
		return "from $from_date:$from_time till $till_time" if ($from_date eq $till_date);
		return "from $from_date:$from_time till $till_date:$till_time";
	}

	return "till $till_date:$till_time" if (!$from and $till);
	return "from $from_date:$from_time" if ($from and !$till);

	return "any time";
}

sub cycle_start($$)
{
	my $now = shift;
	my $delay = shift;

	return $now - ($now % $delay);
}

sub cycle_end($$)
{
	my $now = shift;
	my $delay = shift;

	return cycle_start($now, $delay) + $delay - 1;
}

sub cycles_till_end_of_month($$)
{
	my $now = shift;
	my $delay = shift;

	my $end_of_month = get_end_of_month($now);
	my $this_cycle_start = cycle_start($now, $delay);
	my $last_cycle_end = cycle_end($end_of_month, $delay);
	my $cycle_count = ($last_cycle_end + 1 - $this_cycle_start) / $delay;

	if (opt('debug'))
	{
		require DateTime;

		dbg('now              - ', DateTime->from_epoch('epoch' => $now));
		dbg('this cycle start - ', DateTime->from_epoch('epoch' => $this_cycle_start));
		dbg('end of month     - ', DateTime->from_epoch('epoch' => $end_of_month));
		dbg('last cycle end   - ', DateTime->from_epoch('epoch' => $last_cycle_end));
		dbg('delay            - ', $delay);
		dbg('cycle count      - ', $cycle_count);
	}

	return $cycle_count;
}

sub get_end_of_month($)
{
	my $now = shift;

	require DateTime;

	my $dt = DateTime->from_epoch('epoch' => $now);
	$dt->truncate('to' => 'month');
	$dt->add('months' => 1);
	$dt->subtract('seconds' => 1);
	return $dt->epoch();
}

sub get_end_of_prev_month($)
{
	my $now = shift;

	require DateTime;

	my $dt = DateTime->from_epoch('epoch' => $now);
	$dt->truncate('to' => 'month');
	$dt->subtract('seconds' => 1);
	return $dt->epoch();
}

sub get_month_bounds(;$)
{
	my $now = shift // time();

	require DateTime;

	my $from;
	my $till;

	my $dt = DateTime->from_epoch('epoch' => $now);
	$dt->truncate('to' => 'month');
	$from = $dt->epoch();
	$dt->add('months' => 1);
	$dt->subtract('seconds' => 1);
	$till = $dt->epoch();

	return ($from, $till);
}

sub get_slv_rtt_cycle_stats($$$$$$)
{
	my $tld             = shift;
	my $rtt_params      = shift;
	my $cycle_start     = shift;
	my $cycle_end       = shift;
	my $now             = shift;
	my $max_nodata_time = shift;

	my $probes                     = $rtt_params->{'probes'};
	my $rtt_item_key_pattern       = $rtt_params->{'rtt_item_key_pattern'};
	my $lastclock_control_item_key = $rtt_params->{'lastclock_control_item_key'};
	my $timeout_error_value        = $rtt_params->{'timeout_error_value'};
	my $timeout_threshold_value    = $rtt_params->{'timeout_threshold_value'};

	if (scalar(keys(%{$probes})) == 0)
	{
		dbg("there are no probes that would be able to collect RTT stats for TLD '$tld', item '$rtt_item_key_pattern'");
		return {
			'expected'   => 0,
			'performed'  => 0,
			'failed'     => 0,
			'successful' => 0,
		};
	}

	my $tld_hosts = [map("$tld $_", keys(%{$probes}))];
	my $tld_itemids = get_itemids_by_key_pattern_and_hosts($rtt_item_key_pattern, $tld_hosts, ITEM_STATUS_ACTIVE);
	my $tld_itemids_str = join(",", @{$tld_itemids});

	if (scalar(@{$tld_itemids}) == 0)
	{
		dbg("items '$rtt_item_key_pattern' not found for for TLD '$tld'");
		return {
			'expected'   => 0,
			'performed'  => 0,
			'failed'     => 0,
			'successful' => 0,
		};
	}

	my $row = db_select_row(
			"select count(*)," .
				" count(if(value=$timeout_error_value || value>$timeout_threshold_value,1,null))," .
				" count(if(value between 0 and $timeout_threshold_value,1,null))" .
			" from history" .
			" where itemid in ($tld_itemids_str) and clock between $cycle_start and $cycle_end");

	if ($row->[0] < scalar(@{$tld_itemids}) && $cycle_end > $now - $max_nodata_time)
	{
		if (defined($lastclock_control_item_key))
		{
			# for DNS, it's not known into which item (i.e., TCP or UDP) RTT value for this cycle is being written;
			# to check if RTT was already received, check the status of the "lastclock control item" that is being written on each cycle

			my $itemids = get_itemids_by_key_pattern_and_hosts($lastclock_control_item_key, $tld_hosts, ITEM_STATUS_ACTIVE);
			my $itemids_str = join(",", @{$itemids});

			my $count = db_select_value("select count(*) from lastvalue where itemid in ($itemids_str) and clock>=$cycle_start");

			if ($count < scalar(@{$tld_itemids}))
			{
				# not enough data, try again later
				return undef;
			}
		}
		else
		{
			# not enough data, try again later
			return undef;
		}
	}

	return {
		'expected'   => scalar(@{$tld_itemids}),  # number of expected tests, based on number of items and number of probes
		'performed'  => $row->[1] + $row->[2],    # number of received values, excluding errors (timeout errors are valid values)
		'failed'     => $row->[1],                # number of failed tests - timeout errors and successful queries over the time limit
		'successful' => $row->[2],                # number of successful tests
	};
}

sub get_slv_rtt_cycle_stats_aggregated($$$$$$)
{
	my $rtt_params_list = shift; # array of hashes
	my $cycle_start     = shift;
	my $cycle_end       = shift;
	my $tld             = shift;
	my $now             = shift;
	my $max_nodata_time = shift;

	my %aggregated_stats = (
		'expected'   => 0,
		'performed'  => 0,
		'failed'     => 0,
		'successful' => 0
	);

	foreach my $rtt_params (@{$rtt_params_list})
	{
		if (!tld_service_enabled($tld, $rtt_params->{'tlds_service'}, $cycle_end))
		{
			next;
		}

		my $service_stats = get_slv_rtt_cycle_stats($tld, $rtt_params, $cycle_start, $cycle_end, $now, $max_nodata_time);

		if (!defined($service_stats))
		{
			return undef;
		}

		$aggregated_stats{'expected'}   += $service_stats->{'expected'};
		$aggregated_stats{'performed'}  += $service_stats->{'performed'};
		$aggregated_stats{'failed'}     += $service_stats->{'failed'};
		$aggregated_stats{'successful'} += $service_stats->{'successful'};
	}

	return \%aggregated_stats;
}

sub get_slv_rtt_monthly_items($$$$)
{
	my $single_tld             = shift; # undef or name of TLD
	my $slv_item_key_performed = shift;
	my $slv_item_key_failed    = shift;
	my $slv_item_key_pfailed   = shift;

	my $host_condition = "";

	my @bind_values = (
		$slv_item_key_performed,
		$slv_item_key_failed,
		$slv_item_key_pfailed
	);

	if (defined($single_tld))
	{
		$host_condition = "hosts.host=? and";
		push(@bind_values, $single_tld);
	}

	my $slv_items = db_select(
			"select hosts.host,items.key_,lastvalue.clock,lastvalue.value" .
			" from items" .
				" left join hosts on hosts.hostid=items.hostid" .
				" left join hosts_groups on hosts_groups.hostid=hosts.hostid" .
				" left join lastvalue on lastvalue.itemid=items.itemid" .
			" where items.status=" . ITEM_STATUS_ACTIVE . " and" .
				" items.key_ in (?,?,?) and" .
				" $host_condition" .
				" hosts.status=" . HOST_STATUS_MONITORED . " and" .
				" hosts_groups.groupid=" . TLDS_GROUPID, \@bind_values);

	# contents: $slv_items_by_tld{$tld}{$item_key} = [$last_clock, $last_value];
	my %slv_items_by_tld = ();

	foreach my $slv_item (@{$slv_items})
	{
		my ($tld, $item_key, $last_clock, $last_value) = @{$slv_item};
		$slv_items_by_tld{$tld}{$item_key} = [$last_clock, $last_value];
	}

	foreach my $tld (keys(%slv_items_by_tld))
	{
		set_log_tld($tld);

		my %tld_items = %{$slv_items_by_tld{$tld}};

		# if any item was found on TLD, then all items must exist
		fail("Item '$slv_item_key_performed' not found for TLD '$tld'")
				unless (exists($tld_items{$slv_item_key_performed}));
		fail("Item '$slv_item_key_failed' not found for TLD '$tld'")
				unless (exists($tld_items{$slv_item_key_failed}));
		fail("Item '$slv_item_key_pfailed' not found for TLD '$tld'")
				unless (exists($tld_items{$slv_item_key_pfailed}));

		if (!defined($tld_items{$slv_item_key_performed}[0]) ||
				!defined($tld_items{$slv_item_key_failed}[0]) ||
				!defined($tld_items{$slv_item_key_pfailed}[0]))
		{
			# if any lastvalue on TLD is undefined, then all lastvalues must be undefined

			fail("Item '$slv_item_key_performed' on TLD '$tld' has lastvalue while other related items don't")
					if (defined($tld_items{$slv_item_key_performed}[0]));
			fail("Item '$slv_item_key_failed' on TLD '$tld' has lastvalue while other related items don't")
					if (defined($tld_items{$slv_item_key_failed}[0]));
			fail("Item '$slv_item_key_pfailed' on TLD '$tld' has lastvalue while other related items don't")
					if (defined($tld_items{$slv_item_key_pfailed}[0]));
		}
		else
		{
			# if all lastvalues on TLD are defined, their clock must be the same

			if ($tld_items{$slv_item_key_performed}[0] != $tld_items{$slv_item_key_failed}[0] ||
					$tld_items{$slv_item_key_performed}[0] != $tld_items{$slv_item_key_pfailed}[0])
			{
				fail("Items '$slv_item_key_performed', '$slv_item_key_failed' and '$slv_item_key_pfailed' have different lastvalue clocks on TLD '$tld'");
			}
		}

		unset_log_tld();
	}

	return \%slv_items_by_tld;
}

sub update_slv_rtt_monthly_stats($$$$$$$$;$)
{
	my $now                         = shift;
	my $max_cycles                  = shift;
	my $single_tld                  = shift; # undef or name of TLD
	my $slv_item_key_performed      = shift;
	my $slv_item_key_failed         = shift;
	my $slv_item_key_pfailed        = shift;
	my $cycle_delay                 = shift;
	my $rtt_params_list             = shift;
	my $rdap_standalone_params_list = shift;

	# $params_list - for RDDS, this is either $rtt_params_list (RDDS43, RRDS80 and RDAP) the migration to
	# Standalone RDAP, or $rdap_standalone_params_list (RDDS43 and RDDS80) after migration to Standalone RDAP.
	# For other services, $params_list is always $rtt_params_list.
	# TODO: remove after migration to Standalone RDAP
	my $params_list = $rtt_params_list;

	# how long to wait for data after $cycle_end if number of performed checks is smaller than expected checks
	# TODO: $max_nodata_time = $cycle_delay * x?
	# TODO: move to rsm.conf?
	my $max_nodata_time = 300;

	# contents: $slv_items->{$tld}{$item_key} = [$last_clock, $last_value];
	my $slv_items = get_slv_rtt_monthly_items($single_tld, $slv_item_key_performed, $slv_item_key_failed, $slv_item_key_pfailed);

	# starting time of the last cycle of the previous month
	my $end_of_prev_month = cycle_start(get_end_of_prev_month($now), $cycle_delay);

	init_values();

	TLD_LOOP:
	foreach my $tld (keys(%{$slv_items}))
	{
		set_log_tld($tld);

		my $last_clock           = $slv_items->{$tld}{$slv_item_key_performed}[0];
		my $last_performed_value = $slv_items->{$tld}{$slv_item_key_performed}[1];
		my $last_failed_value    = $slv_items->{$tld}{$slv_item_key_failed}[1];
		my $last_pfailed_value   = $slv_items->{$tld}{$slv_item_key_pfailed}[1];

		# if there's no lastvalue, start collecting stats from the begining of the current month
		if (!defined($last_clock))
		{
			$last_clock = $end_of_prev_month;
		}

		my $cycles_till_end_of_month = cycles_till_end_of_month($last_clock + $cycle_delay, $cycle_delay);

		for (my $i = 0; $i < $max_cycles; $i++)
		{
			# if new month starts, reset the counters
			if ($last_clock == $end_of_prev_month)
			{
				$cycles_till_end_of_month = cycles_till_end_of_month($last_clock + $cycle_delay, $cycle_delay);
				$last_performed_value = 0;
				$last_failed_value    = 0;
				$last_pfailed_value   = 0;
			}

			my $cycle_start = cycle_start($last_clock + $cycle_delay, $cycle_delay);
			my $cycle_end   = cycle_end($last_clock + $cycle_delay, $cycle_delay);

			if ($cycle_start > $now)
			{
				next TLD_LOOP;
			}

			if (defined($rdap_standalone_params_list) && is_rdap_standalone($cycle_start))
			{
				dbg("using parameters w/o RDAP, cycle_start=$cycle_start");
				$params_list = $rdap_standalone_params_list;
			}

			my $rtt_stats;

			if (is_rsmhost_reconfigured($tld, $cycle_delay, $cycle_start))
			{
				$rtt_stats = {
					'expected'   => 0,
					'performed'  => 0,
					'failed'     => 0,
					'successful' => 0
				};
			}
			else
			{
				$rtt_stats = get_slv_rtt_cycle_stats_aggregated($params_list, $cycle_start, $cycle_end, $tld, $now, $max_nodata_time);

				if (!defined($rtt_stats))
				{
					dbg("stopping updatig TLD '$tld' because of missing data, cycle from $cycle_start till $cycle_end");
					next TLD_LOOP;
				}
			}

			$cycles_till_end_of_month--;

			if ($cycles_till_end_of_month < 0)
			{
				if (opt('debug'))
				{
					dbg("\$i                        = $i");
					dbg("\$cycles_till_end_of_month = $cycles_till_end_of_month");
					dbg("\$end_of_prev_month        = $end_of_prev_month");
					dbg("\$last_clock               = $last_clock");
					dbg("\$cycle_delay              = $cycle_delay");
					dbg("\$cycle_start              = $cycle_start");
					dbg("\$cycle_end                = $cycle_end");
				}

				fail("\$cycles_till_end_of_month must not be less than 0, perhaps last value clock is older than beginning of previous month");
			}

			$last_performed_value += $rtt_stats->{'performed'};
			$last_failed_value    += $rtt_stats->{'failed'};

			my $performed_with_expected = $last_performed_value + $cycles_till_end_of_month * $rtt_stats->{'expected'};

			if ($performed_with_expected == 0)
			{
				wrn("performed ($last_performed_value)".
					" + expected (cycles:$cycles_till_end_of_month * tests:$rtt_stats->{'expected'})".
					" number of tests is zero");

				if ($last_pfailed_value > 0)
				{
					fail("unexpected last pfailed value:\n" .
						"\$i                        = $i\n" .
						"\$tld                      = $tld\n" .
						"\$cycles_till_end_of_month = $cycles_till_end_of_month\n" .
						"\$end_of_prev_month        = $end_of_prev_month\n" .
						"\$last_clock               = $last_clock\n" .
						"\$cycle_delay              = $cycle_delay\n" .
						"\$cycle_start              = $cycle_start\n" .
						"\$cycle_end                = $cycle_end\n" .
						"\$last_pfailed_value       = $last_pfailed_value\n" .
						"\$rtt_stats->{'expected'}  = $rtt_stats->{'expected'}");
				}
			}
			else
			{
				$last_pfailed_value = 100 * $last_failed_value / $performed_with_expected;
			}

			push_value($tld, $slv_item_key_performed, $cycle_start, $last_performed_value, ITEM_VALUE_TYPE_UINT64);
			push_value($tld, $slv_item_key_failed   , $cycle_start, $last_failed_value, ITEM_VALUE_TYPE_UINT64);
			push_value($tld, $slv_item_key_pfailed  , $cycle_start, $last_pfailed_value, ITEM_VALUE_TYPE_FLOAT);

			$last_clock = $cycle_start;
		}

		unset_log_tld();
	}

	send_values();
}

sub recalculate_downtime($$$$$$)
{
	my $auditlog_log_file = shift;
	my $item_key_avail    = shift; # exact key for rdds and dns, pattern for dns.ns
	my $item_key_downtime = shift; # exact key for rdds and dns, undef for dns.ns
	my $incident_fail     = shift; # how many cycles have to fail to start the incident
	my $incident_recover  = shift; # how many cycles have to succeed to recover from the incident
	my $delay             = shift;

	fail("not supported when running in --dry-run mode") if (opt('dry-run'));

	# get service from item's key ('DNS', 'DNS.NS', 'RDDS', 'RDAP')
	my $service = uc($item_key_avail =~ s/^rsm\.slv\.(.+)\.avail(?:\[.*\])?$/$1/r);

	# get last auditid
	my $last_auditlog_auditid = __fp_read_last_auditid($auditlog_log_file);
	dbg("last_auditlog_auditid = $last_auditlog_auditid");

	# get list of events.eventid (incidents) that changed their "false positive" state
	my @eventids = __fp_get_updated_eventids(\$last_auditlog_auditid);

	# process incidents
	if (@eventids)
	{
		# my @report_updates = ([host, clock, incident, $false_positive], ...)
		# One incident may start on one month, end on the next month.
		# One incident may result in recalculating history of multiple items (downtime of DNS nameservers).
		# In these cases, there will be more than 1 entry in @report_updates for an incident.
		# __fp_regenerate_reports() must take care of removing "duplicates" from @report_updates.
		my @report_updates = ();

		foreach my $eventid (@eventids)
		{
			__fp_process_incident(
				$service,
				$eventid,
				$item_key_avail,
				$item_key_downtime,
				$incident_fail,
				$incident_recover,
				$delay,
				\@report_updates
			);
		}

		__fp_regenerate_reports($service, \@report_updates);
	}

	# save last auditid (it may have changed even if @eventids is empty)
	__fp_write_last_auditid($auditlog_log_file, $last_auditlog_auditid);
}

sub generate_report($$;$)
{
	my $tld   = shift;
	my $ts    = shift;
	my $force = shift;

	my ($year, $month) = split("-", ts_ym($ts));

	my $cmd = "/opt/zabbix/scripts/sla-report.php";
	my @args = ();

	# add --server-id, if called from a script that supports --server-id option (e.g., tld.pl)
	push(@args, "--server-id", getopt("server-id")) if (opt("server-id"));

	push(@args, "--debug") if opt("debug");
	push(@args, "--stats") if opt("stats");
	push(@args, "--force") if $force;

	push(@args, "--tld"      , $tld);
	push(@args, "--year"     , int($year));
	push(@args, "--month"    , int($month));

	@args = map('"' . $_ . '"', @args);

	dbg("executing $cmd @args");
	my $out = qx($cmd @args 2>&1);

	if ($out)
	{
		info("output of $cmd:\n" . $out);
	}

	if ($? == -1)
	{
		fail("failed to generate reports, failed to execute $cmd: $!");
	}
	if ($? != 0)
	{
		fail("failed to generate report, command $cmd exited with value " . ($? >> 8));
	}
}

#
# Helper function for collecting data for SLA API and Data Export.
#
# The data is collected for specified period, specified items of type uint, float and str. The data is to be used later
# for calling get_test_results().
#
sub get_test_history($$$$$$$$)
{
	my $from = shift;              # input
	my $till = shift;              # input
	my $itemids_uint = shift;      # input
	my $itemids_float = shift;     # input
	my $itemids_str = shift;       # input
	my $results_uint_buf = shift;  # output
	my $results_float_buf = shift; # output
	my $results_str_buf = shift;   # output

	if (@{$itemids_uint} == 0)
	{
		$$results_uint_buf = [];
	}
	else
	{
		$$results_uint_buf = db_select(
			"select itemid,value,clock".
			" from " . history_table(ITEM_VALUE_TYPE_UINT64).
			" where itemid in (" . join(',', @{$itemids_uint}) . ")".
				" and " . sql_time_condition($from, $till) .
			" order by clock,itemid"
		);
	}

	if (@{$itemids_float} == 0)
	{
		$$results_float_buf = [];
	}
	else
	{
		$$results_float_buf = db_select(
			"select itemid,value,clock".
			" from " . history_table(ITEM_VALUE_TYPE_FLOAT).
			" where itemid in (" . join(',', @{$itemids_float}) . ")".
				" and " . sql_time_condition($from, $till) .
			" order by clock,itemid"
		);
	}

	if (@{$itemids_str} == 0)
	{
		$$results_str_buf = [];
	}
	else
	{
		$$results_str_buf = db_select(
			"select itemid,value,clock".
			" from " . history_table(ITEM_VALUE_TYPE_STR).
			" where itemid in (" . join(',', @{$itemids_str}) . ")".
				" and " . sql_time_condition($from, $till) .
			" order by clock,itemid"
		);

	}
}

use constant INTERFACE_DNS    => 'dns';
use constant INTERFACE_DNSSEC => 'dnssec';
use constant INTERFACE_RDDS43 => 'rdds43';
use constant INTERFACE_RDDS80 => 'rdds80';
use constant INTERFACE_RDAP   => 'rdap';

sub __get_interface($$)
{
	my $service = shift;
	my $key = shift;

	if ($service eq 'dns')
	{
		return INTERFACE_DNS;
	}

	if ($service eq 'dnssec')
	{
		return INTERFACE_DNSSEC;
	}

	if ($service eq 'rdap')
	{
		return INTERFACE_RDAP;
	}

	# RDDS service is the only having multiple interfaces

	if (str_starts_with($key, "rsm.rdds.43"))
	{
		return INTERFACE_RDDS43;
	}

	if (str_starts_with($key, "rsm.rdds.80"))
	{
		return INTERFACE_RDDS80;
	}

	if (str_starts_with($key, "rdap"))
	{
		return INTERFACE_RDAP;
	}

	fail("Cannot identify interface from $service key \"$key\"");
}

sub get_service_from_key($;$)
{
	my $key = shift;
	my $clock = shift;

	return 'dns' if (str_starts_with($key, 'rsm.dns'));
	return 'rdds' if (str_starts_with($key, 'rsm.rdds'));
	return 'rdds' if (str_starts_with($key, 'rdap') && !is_rdap_standalone($clock));
	return 'rdap' if (str_starts_with($key, 'rdap') && is_rdap_standalone($clock));

	fail("cannot identify service, key \"$key\" is unknown");
}

# in order to keep data structures consistent let's use these for non-DNS services
use constant FAKE_NS => '';
use constant FAKE_NSIP => '';

#
# Items we collect the data from:
#
# DNS:
# | rsm.dns.mode                                 |
# | rsm.dns.ns.status[ns1.zabbix.dev]            |
# | rsm.dns.ns.status[ns2.zabbix.dev]            |
# | rsm.dns.nsid[ns1.zabbix.dev,192.168.3.11]    |
# | rsm.dns.nsid[ns2.zabbix.dev,192.168.3.9]     |
# | rsm.dns.nsid[ns2.zabbix.dev,192.168.8.75]    |
# | rsm.dns.protocol                             |
# | rsm.dns.rtt[ns1.zabbix.dev,192.168.3.11,tcp] |
# | rsm.dns.rtt[ns1.zabbix.dev,192.168.3.11,udp] |
# | rsm.dns.rtt[ns2.zabbix.dev,192.168.3.9,tcp]  |
# | rsm.dns.rtt[ns2.zabbix.dev,192.168.3.9,udp]  |
# | rsm.dns.rtt[ns2.zabbix.dev,192.168.8.75,tcp] |
# | rsm.dns.rtt[ns2.zabbix.dev,192.168.8.75,udp] |
# | rsm.dns.status
# RDDS:
# | rsm.rdds.43.ip         |
# | rsm.rdds.43.rtt        |
# | rsm.rdds.43.target     |
# | rsm.rdds.43.testedname |
# | rsm.rdds.80.ip         |
# | rsm.rdds.80.rtt        |
# | rsm.rdds.80.target     |
# | rsm.rdds.status        |
#  RDAP:
# | rdap.ip         |
# | rdap.rtt        |
# | rdap.status     |
# | rdap.target     |
# | rdap.testedname |
#
# Format history data in a convenient way for SLA API and Data Export scripts. We need to group metrics by targets.
# Targets are differently fetched in case for DNS/DNSSEC and RDDS/RDAP but formatted in the same way (see comments
# in the code):
#
# {
#     'dns' => {
#         '12345600' => {        <-- cycleclock
#             'status' => 1,
#             'interfaces' => {
#                 'DNS' => {
#                     'clock' => 123456789,
#                     'status' => 1,
#                     'protocol' => 0,
#                     'testedname' => 'nonexistend.example.com',
#                     'targets' => {
#                         'ns1.example.com' => {
#                             'status' => 1,
#                             'metrics' => [
#                                 'rtt' => 13,
#                                 'ip' => '1.2.3.4',
#                                 'nsid' => ''
#                             ]
#                         },
#                         'ns1.example.com' => {
#                             'status' => 1,
#                             'metrics' => [
#                                 'rtt' => 13,
#                                 'ip' => '1.2.3.4',
#                                 'nsid' => ''
#                             ]
#                         }
#                     }
#                 }
#             }
#         }
#     }
#     'rdds' => {
#         '123450000' => {        <-- cycleclock
#             'status' => 1,
#             'interfaces' => {
#                 'RDDS43' => {
#                     'clock' => 123456789,
#                     'status' => 1,
#                     'testedname' => 'example.com'
#                     'targets' => {
#                         'whois.example.com' => {
#                             'status' => 1,
#                             'metrics' => [
#                                 'rtt' => 35,
#                                 'ip' => '1.2.3.4',
#                             ]
#                         }
#                     }
#                 },
#                 'RDAP' => {
#                     'clock' => 123456800,
#                     'status' => 1,
#                     'testedname' => 'test.example.com'
#                     'targets' => {
#                         'http://rdap.example.com/rdap' => {
#                             'status' => 0,
#                             'metrics' => [
#                                 'rtt' => 120,
#                                 'ip' => '1.2.3.8',
#                             ]
#                         }
#                     }
#                 }
#             }
#         }
#     }
# }
#
sub get_test_results($$;$)
{
	my $results = shift;
	my $item_data = shift;
	my $service_filter = shift;	# optional: only get the data of the specified service

	my %delays = (
		'dns' => get_dns_delay(),
		'rdds' => get_rdap_delay(),
		'rdap' => get_rdap_delay(),
	);

	# We have:
	#
	# - rtt
	# - ip
	# - nsid (only dns)
	# - protocol (only dns)
	# - target
	# - testedname
	# - target status
	# - interface status
	# - service status
	#
	# Let's pre-format the data for later convenient generation.
	#
	# # servicestatuses:
	# cycleclock => service => 'status' = 1
	#
	# # interfacedata:
	# cycleclock => service => 'interfaces' => interface => 'status' = 1
	# cycleclock => service => 'interfaces' => interface => 'clock' = 123455667
	# cycleclock => service => 'interfaces' => interface => 'testedname' = example.com (not rdds80)
	# cycleclock => service => 'interfaces' => interface => 'protocol' = 0 (dns only)
	#
	# # metrics:
	# cycleclock => service => 'interfaces' => interface => 'metrics' => ns => nsip => 'rtt' = 12
	# cycleclock => service => 'interfaces' => interface => 'metrics' => ns => nsip => 'nsid' = '' (dns only)
	# cycleclock => service => 'interfaces' => interface => 'metrics' => ns => nsip => 'ip' = '1.2.3.4' (not dns)
	# cycleclock => service => 'interfaces' => interface => 'metrics' => ns => nsip => 'target' = 'whois.example.com' (not dns)
	# cycleclock => service => 'interfaces' => interface => 'metrics' => ns => nsip => 'status' = 1 (status of a target)
	#
	# NB! For non-DNS serives ns and nsip will be fake strings. This is needed for connecting results and
	# targets because in DNS there are multiple targets. Let's keep single structure of the data for convenience.

	my %data;

	foreach my $row_ref (@{$results})
	{
		my $itemid = $row_ref->[0];
		my $value = $row_ref->[1];
		my $clock = $row_ref->[2];

		my $i = $item_data->{$itemid};

		next if (str_ends_with($i->{'key'}, ".enabled"));
		next if (str_starts_with($i->{'key'}, "rsm.slv."));
		next if (str_starts_with($i->{'key'}, "rsm.dns.nssok"));
		next if (str_starts_with($i->{'key'}, "rsm.dns.mode"));

		my $service = get_service_from_key($i->{'key'});
		my $cycleclock = cycle_start($clock, $delays{$service});

		# DNSSEC is part of DNS
		$service = 'dnssec' if ($service_filter && $service_filter eq 'dnssec');

		next if ($service_filter && $service ne $service_filter);

		# RDDS is the only service that is not self-interface
		if (str_starts_with($i->{'key'}, "rsm.rdds.status"))
		{
			# service status
			$data{$cycleclock}{$service}{'status'} = $value;
			next;
		}

		my $interface = __get_interface($service, $i->{'key'});

		# if RDAP is not standalone, connect RDAP with RDDS
		if (str_starts_with($i->{'key'}, "rdap.status"))
		{
			if (is_rdap_standalone($cycleclock))
			{
				# service status
				$data{$cycleclock}{$service}{'status'} = $value;
			}
			else
			{
				# RDDS is UP only when all interfaces are up
				if (!exists($data{$cycleclock}{$service}{'status'}) || $data{$cycleclock}{$service}{'status'} == 1)
				{
					$data{$cycleclock}{$service}{'status'} = $value;
				}
			}

			# interface status and clock
			$data{$cycleclock}{$service}{'interfaces'}{$interface}{'status'} = $value;
			$data{$cycleclock}{$service}{'interfaces'}{$interface}{'clock'} = $clock;

			# target status
			$data{$cycleclock}{$service}{'interfaces'}{$interface}{'metrics'}{FAKE_NS()}{FAKE_NSIP()}{'status'} = $value;
		}
		elsif (str_starts_with($i->{'key'}, "rsm.dns.status"))
		{
			# service status
			$data{$cycleclock}{$service}{'status'} = $value;

			# interface status and clock
			$data{$cycleclock}{$service}{'interfaces'}{$interface}{'status'} = $value;
			$data{$cycleclock}{$service}{'interfaces'}{$interface}{'clock'} = $clock;
		}
		elsif (str_starts_with($i->{'key'}, "rsm.dnssec.status"))
		{
			# service status
			$data{$cycleclock}{$service}{'status'} = $value;

			# interface status and clock
			$data{$cycleclock}{$service}{'interfaces'}{$interface}{'status'} = $value;
			$data{$cycleclock}{$service}{'interfaces'}{$interface}{'clock'} = $clock;
		}
		elsif (str_starts_with($i->{'key'}, "rsm.rdds.43.status", "rsm.rdds.80.status"))
		{
			# interface status and clock
			$data{$cycleclock}{$service}{'interfaces'}{$interface}{'status'} = $value;
			$data{$cycleclock}{$service}{'interfaces'}{$interface}{'clock'} = $clock;

			# target status
			$data{$cycleclock}{$service}{'interfaces'}{$interface}{'metrics'}{FAKE_NS()}{FAKE_NSIP()}{'status'} = $value;
		}
		elsif (str_starts_with($i->{'key'}, "rsm.dns.testedname", "rsm.rdds.43.testedname", "rdap.testedname"))
		{
			# interface tested name
			$data{$cycleclock}{$service}{'interfaces'}{$interface}{'testedname'} = $value;
		}
		elsif (str_starts_with($i->{'key'}, "rsm.dns.protocol"))
		{
			# interface protocol
			$data{$cycleclock}{$service}{'interfaces'}{$interface}{'protocol'} = $value;
		}
		elsif (str_starts_with($i->{'key'}, "rsm.dns.mode"))
		{
			# interface mode
			$data{$cycleclock}{$service}{'interfaces'}{$interface}{'mode'} = $value;
		}
		elsif (str_starts_with($i->{'key'}, "rsm.dns.nsid"))
		{
			# DNS metric: nsid
			my ($ns, $nsip) = split(',', get_nsip_from_key($i->{'key'}));

			$data{$cycleclock}{$service}{'interfaces'}{$interface}{'metrics'}{$ns}{$nsip}{'nsid'} = $value;
		}
		elsif (str_starts_with($i->{'key'}, "rsm.dns.rtt"))
		{
			# DNS metric: rtt
			my ($ns, $nsip) = split(',', get_nsip_from_key($i->{'key'}));

			$data{$cycleclock}{$service}{'interfaces'}{$interface}{'metrics'}{$ns}{$nsip}{'rtt'} = $value;
		}
		elsif (str_starts_with($i->{'key'}, "rsm.dns.ns.status"))
		{
			# DNS metric: target status
			my $ns;

			$ns = $1 if ($i->{'key'} =~ /rsm.dns.ns.status\[(.*)\]/);

			$data{$cycleclock}{$service}{'interfaces'}{$interface}{'metrics'}{$ns}{FAKE_NSIP()}{'status'} = $value;
		}
		elsif (str_starts_with($i->{'key'}, "rsm.rdds.43.target", "rsm.rdds.80.target", "rdap.target"))
		{
			$data{$cycleclock}{$service}{'interfaces'}{$interface}{'metrics'}{FAKE_NS()}{FAKE_NSIP()}{'target'} = $value;
		}
		elsif (str_starts_with($i->{'key'}, "rsm.rdds.43.rtt", "rsm.rdds.80.rtt", "rdap.rtt"))
		{
			$data{$cycleclock}{$service}{'interfaces'}{$interface}{'metrics'}{FAKE_NS()}{FAKE_NSIP()}{'rtt'} = $value;
		}
		elsif (str_starts_with($i->{'key'}, "rsm.rdds.43.ip", "rsm.rdds.80.ip", "rdap.ip"))
		{
			$data{$cycleclock}{$service}{'interfaces'}{$interface}{'metrics'}{FAKE_NS()}{FAKE_NSIP()}{'ip'} = $value;
		}
		else
		{
			fail("unhandled key: ", $i->{'key'});
		}
	}

	my $result = {};

	# format the data accordingly
	foreach my $cycleclock (sort(keys(%data)))
	{
		foreach my $service (sort(keys(%{$data{$cycleclock}})))
		{
			# service status
			$result->{$service}{$cycleclock}{'status'} = $data{$cycleclock}{$service}{'status'};

			foreach my $interface (sort(keys(%{$data{$cycleclock}{$service}{'interfaces'}})))
			{
				# interface status
				$result->{$service}{$cycleclock}{'interfaces'}{$interface}{'status'} =
					$data{$cycleclock}{$service}{'interfaces'}{$interface}{'status'};

				# interface clock
				$result->{$service}{$cycleclock}{'interfaces'}{$interface}{'clock'} =
					$data{$cycleclock}{$service}{'interfaces'}{$interface}{'clock'};

				# interface protocol
				if (exists($data{$cycleclock}{$service}{'interfaces'}{$interface}{'protocol'}))
				{
					$result->{$service}{$cycleclock}{'interfaces'}{$interface}{'protocol'} =
						$data{$cycleclock}{$service}{'interfaces'}{$interface}{'protocol'};
				}

				# interface testedname
				if (exists($data{$cycleclock}{$service}{'interfaces'}{$interface}{'testedname'}))
				{
					$result->{$service}{$cycleclock}{'interfaces'}{$interface}{'testedname'} =
						$data{$cycleclock}{$service}{'interfaces'}{$interface}{'testedname'};
				}

				foreach my $ns (sort(keys(%{$data{$cycleclock}{$service}{'interfaces'}{$interface}{'metrics'}})))
				{
					# get rid of fake target
					my $target = ($ns eq FAKE_NS ? $data{$cycleclock}{$service}{'interfaces'}{$interface}{'metrics'}{$ns}{FAKE_NSIP()}{'target'} : $ns);

					# target status is in FAKE_NS
					$result->{$service}{$cycleclock}{'interfaces'}{$interface}{'targets'}{$target}{'status'} =
						$data{$cycleclock}{$service}{'interfaces'}{$interface}{'metrics'}{$ns}{FAKE_NSIP()}{'status'};

					foreach my $nsip (sort(keys(%{$data{$cycleclock}{$service}{'interfaces'}{$interface}{'metrics'}{$ns}})))
					{
						# fake NSIP for target status (dns)
						next unless (exists($data{$cycleclock}{$service}{'interfaces'}{$interface}{'metrics'}{$ns}{$nsip}{'rtt'}));

						# get rid of fake NSIP
						my $ip = ($nsip eq FAKE_NSIP ? $data{$cycleclock}{$service}{'interfaces'}{$interface}{'metrics'}{$ns}{$nsip}{'ip'} : $nsip);

						my $h = {
							'rtt' => $data{$cycleclock}{$service}{'interfaces'}{$interface}{'metrics'}{$ns}{$nsip}{'rtt'},
							'ip' => $ip,
						};

						if (exists($data{$cycleclock}{$service}{'interfaces'}{$interface}{'metrics'}{$ns}{$nsip}{'nsid'}))
						{
							$h->{'nsid'} = $data{$cycleclock}{$service}{'interfaces'}{$interface}{'metrics'}{$ns}{$nsip}{'nsid'};
						}

						# the metrics
						push(@{$result->{$service}{$cycleclock}{'interfaces'}{$interface}{'targets'}{$target}{'metrics'}}, $h);
					}
				}
			}
		}
	}

#	TODO: at the end copy dnssec as dns if it exists

	return $result;
}

sub set_log_tld($)
{
	$tld = shift;
}

sub unset_log_tld()
{
	undef($tld);
}

sub convert_suffixed_number($)
{
	my $number = shift;

	my %suffix_map = (
		"K" => 1024,
		"M" => 1048576,
		"G" => 1073741824,
		"T" => 1099511627776,
		"s" => 1,
		"m" => 60,
		"h" => 3600,
		"d" => 86400,
		"w" => 7*86400
	);

	my $suffix = substr($number, -1);

	return $number unless (exists($suffix_map{$suffix}));

	substr($number, -1) = '';

	return $number * $suffix_map{$suffix};
}

sub usage
{
	pod2usage(shift);
}

#######################################################
# Internal subs for handling false-positive incidents #
#######################################################

my $__fp_logfile = "/var/log/zabbix/false-positive.log";

sub __fp_log($$$)
{
	my $host    = shift;
	my $service = shift;
	my $message = shift;

	my $log_str = sprintf("[%s:%s] [%s] [%s] %s", $$, ts_str(), $host, $service, $message);

	dbg($log_str);

	open(my $fh, ">>", $__fp_logfile) or fail("cannot open file '$__fp_logfile'");
	flock($fh, LOCK_EX)               or fail("cannot lock file '$__fp_logfile'");
	print($fh $log_str . "\n")        or fail("cannot write to file '$__fp_logfile'");
	close($fh)                        or fail("cannot close file '$__fp_logfile'");
}

sub __fp_read_last_auditid($)
{
	my $file = shift;

	my $auditid = 0;

	if (-e $file)
	{
		my $error;

		if (read_file($file, \$auditid, \$error) != SUCCESS)
		{
			fail("cannot read file \"$file\": $error");
		}
	}

	return $auditid;
}

sub __fp_write_last_auditid($$)
{
	my $file    = shift;
	my $auditid = shift;

	if (write_file($file, $auditid) != SUCCESS)
	{
		fail("cannot write file \"$file\"");
	}
}

sub __fp_get_updated_eventids($)
{
	my $last_auditlog_auditid_ref = shift;

	# check integrity

	my $max_auditid = db_select_value("select max(auditid) from auditlog") // 0;
	if (${$last_auditlog_auditid_ref} > $max_auditid)
	{
		fail("value of last processed auditlog.auditid (${$last_auditlog_auditid_ref}) is larger than max auditid in the database ($max_auditid)");
	}

	# get unprocessed auditlog entries

	my $sql = "select if(resourcetype=?,resourceid,0) as auditlog_eventid,count(*),max(auditid)" .
		" from auditlog" .
		" where auditid > ?" .
		" group by auditlog_eventid";

	my $params = [AUDIT_RESOURCE_INCIDENT, ${$last_auditlog_auditid_ref}];
	my $rows = db_select($sql, $params);

	if (scalar(@{$rows}) == 0)
	{
		# all auditlog entries are processed already
		return;
	}

	# get list of events.eventid (incidents) that changed their "false positive" state

	my @eventids = ();

	foreach my $row (@{$rows})
	{
		my ($eventid, $count, $max_auditid) = @{$row};

		${$last_auditlog_auditid_ref} = max(${$last_auditlog_auditid_ref}, $max_auditid);

		next if ($eventid == 0); # this is not AUDIT_RESOURCE_INCIDENT
		next if ($count % 2 == 0); # marked + unmarked, no need to recalculate

		push(@eventids, $eventid);
	}

	return sort { $a <=> $b } @eventids;
}

sub __fp_process_incident($$$$$$$)
{
	my $service            = shift;
	my $eventid            = shift;
	my $item_key_avail     = shift;
	my $item_key_downtime  = shift;
	my $incident_fail      = shift;
	my $incident_recover   = shift;
	my $delay              = shift;
	my $report_updates_ref = shift;

	dbg("processing incident #$eventid");

	# get info about an incident that changed its false positiveness

	my ($triggerid, $incident_from, $incident_till, $rsmhostid, $rsmhost, $itemid, $key, $false_positive) = __fp_get_incident_info($eventid);

	# check incident's item key to make sure that $service is related to this incident

	my $skip = 0;
	$skip = 1 if ($service eq 'DNS.NS' && $key ne 'rsm.slv.dns.avail');
	$skip = 1 if ($service ne 'DNS.NS' && $key ne $item_key_avail);
	if ($skip)
	{
		dbg("skipping incident #$eventid (\$service = '$service', \$item_key_avail = '$item_key_avail', incident's \$key = '$key')");
		return;
	}

	# get $itemid_avail => $itemid_downtime map of items that have to be recalculated

	my %itemids_map = __fp_get_itemids_map($service, $rsmhostid, $item_key_avail, $item_key_downtime);

	# process each item

	while (my ($itemid_avail, $itemid_downtime) = each(%itemids_map))
	{
		# determine time interval that has to be recalculated

		my $recalculate_from = cycle_start($incident_from, $delay);
		my $recalculate_till = cycle_start(get_end_of_month($incident_till), $delay) if (defined($incident_till));

		my $lastclock_rows = db_select("select clock from lastvalue where itemid=?", [$itemid_downtime]);
		if (!@{$lastclock_rows})
		{
			dbg("skipping incident #$eventid for item #$itemid_downtime (new item without history)");
			next;
		}

		my $lastclock = $lastclock_rows->[0][0];
		$recalculate_till = defined($recalculate_till) ? min($recalculate_till, $lastclock) : $lastclock;

		dbg("eventid          - ", $eventid);
		dbg("triggerid        - ", $triggerid);
		dbg("itemid_avail     - ", $itemid_avail);
		dbg("itemid_downtime  - ", $itemid_downtime);
		dbg("incident from    - ", defined($incident_from) ? ts_full($incident_from) : 'undef');
		dbg("incident till    - ", defined($incident_till) ? ts_full($incident_till) : 'undef');
		dbg("recalculate from - ", defined($recalculate_from) ? ts_full($recalculate_from) : 'undef');
		dbg("recalculate till - ", defined($recalculate_till) ? ts_full($recalculate_till) : 'undef');

		# get downtime clock & value right before the updated incident

		dbg("getting last downtime data before the incident...");

		my %downtime = __fp_get_history_values($itemid_downtime, $recalculate_from - $delay);

		if (!%downtime)
		{
			dbg("skipping incident #$eventid for item #$itemid_downtime (was too long ago, not enough data for recalculating hitsory)");
			next;
		}

		# get availability data

		dbg("getting availability data...");

		my %avail = __fp_get_history_values(
			$itemid_avail,
			$recalculate_from - $delay * ($incident_fail - 1),
			$recalculate_till
		);

		for (my $clock = $recalculate_from - $delay * ($incident_fail - 1); $clock <= $recalculate_till; $clock += $delay)
		{
			if (!exists($avail{$clock}))
			{
				$clock = ts_full($clock);
				fail("missing availability data (\$itemid = $itemid, \$clock = $clock)");
			}
		}

		# update availability data, based on false positive incidents

		dbg("getting time ranges of false positive incidents...");

		my @fp_ranges = __fp_get_false_positive_ranges($triggerid, $recalculate_from, $recalculate_till);

		foreach my $fp_range (@fp_ranges)
		{
			my ($fp_from, $fp_till) = @{$fp_range};

			if (!defined($fp_till) || $fp_till > $recalculate_till)
			{
				$fp_till = $recalculate_till;
			}

			for (my $clock = $fp_from; $clock <= $fp_till; $clock += $delay)
			{
				$avail{$clock} = UP;
			}
		}

		# recalculate downtime

		my @downtime_values = ();

		my $downtime_value = $downtime{$recalculate_from - $delay};
		my $is_incident = $false_positive ? 0 : 1;
		my $counter = 0;
		my $beginning_of_next_month = cycle_start(get_end_of_month($recalculate_from - $delay), $delay) + $delay;

		push(@{$report_updates_ref}, [$rsmhost, $recalculate_from, $eventid, $false_positive]);

		dbg("recalculating downtime values...");

		for (my $clock = $recalculate_from; $clock <= $recalculate_till; $clock += $delay)
		{
			if (!defined($avail{$clock}))
			{
				fail("failed to update history, missing availability data (itemid: $itemid_avail; clock: $clock)");
			}

			__fp_update_incident_state($incident_fail, $incident_recover, $avail{$clock}, \$counter, \$is_incident);

			if ($clock == $beginning_of_next_month)
			{
				$downtime_value = 0;
				$beginning_of_next_month = cycle_start(get_end_of_month($clock), $delay) + $delay;
				push(@{$report_updates_ref}, [$rsmhost, $clock, $eventid, $false_positive]);
			}

			if ($is_incident && $avail{$clock} == DOWN)
			{
				$downtime_value += $delay / 60;
			}

			push(@downtime_values, [$clock, $downtime_value]);
		}

		# store new downtime values

		dbg("updating downtime values...");

		my $debug = opt('debug');
		unsetopt('debug');

		db_mass_update(
			"history_uint",
			["clock", "value"],
			\@downtime_values,
			["clock"],
			[['itemid', $itemid_downtime]]
		);

		setopt('debug', 1) if ($debug);

		# update lastvalue if necessary

		if ($recalculate_till == $lastclock)
		{
			dbg("updating lastvalue of itemid $itemid_downtime...");
			my $sql = "update" .
					" lastvalue" .
					" inner join history_uint on history_uint.itemid=lastvalue.itemid and history_uint.clock=lastvalue.clock" .
				" set lastvalue.value=history_uint.value" .
				" where lastvalue.itemid=?";
			db_exec($sql, [$itemid_downtime]);
		}
	}
}

sub __fp_get_incident_info($)
{
	my $eventid = shift;

	my $sql = "select " .
			"events.source," .
			"events.object," .
			"events.value," .
			"events.objectid," .
			"events.false_positive," .
			"events.clock," .
			"(" .
				"select clock" .
				" from events as events_inner" .
				" where" .
					" events_inner.source=events.source and" .
					" events_inner.object=events.object and" .
					" events_inner.objectid=events.objectid and" .
					" events_inner.value=? and" .
					" events_inner.eventid>events.eventid" .
				" order by events_inner.eventid asc" .
				" limit 1" .
			") as clock2," .
			"function_items.hostid," .
			"function_items.host," .
			"function_items.itemid," .
			"function_items.key_" .
		" from" .
			" events" .
			" left join (" .
				"select distinct" .
					" functions.triggerid," .
					"items.itemid," .
					"items.key_," .
					"hosts.hostid," .
					"hosts.host" .
				" from" .
					" functions" .
					" left join items on items.itemid=functions.itemid" .
					" left join hosts on hosts.hostid=items.hostid" .
			") as function_items on function_items.triggerid=events.objectid" .
		" where" .
			" events.eventid=?";
	my $params = [TRIGGER_VALUE_FALSE, $eventid];
	my $row = db_select_row($sql, $params);

	my ($source, $object, $value, $triggerid, $false_positive, $from, $till, $rsmhostid, $rsmhost, $itemid, $key) = @{$row};

	fail("unexpected value of events.source for incident #$eventid: $source") if ($source != EVENT_SOURCE_TRIGGERS);
	fail("unexpected value of events.object for incident #$eventid: $object") if ($object != EVENT_OBJECT_TRIGGER);
	fail("unexpected value of events.value for incident #$eventid: $object")  if ($value  != TRIGGER_VALUE_TRUE);

	return ($triggerid, $from, $till, $rsmhostid, $rsmhost, $itemid, $key, $false_positive);
}

sub __fp_get_itemids_map($$$$)
{
	my $service           = shift;
	my $rsmhostid         = shift;
	my $item_key_avail    = shift;
	my $item_key_downtime = shift;

	my %itemids_map = (); # $itemid_avail => $itemid_downtime

	if ($service eq 'DNS.NS')
	{
		my $sql = "select itemid, key_ from items where hostid=? and (key_ like ? or key_ like ?)";
		my $params = [$rsmhostid, $item_key_avail, $item_key_downtime];
		my $rows = db_select($sql, $params);

		my %itemids_map_tmp = ();

		foreach my $row (@{$rows})
		{
			my ($itemid, $key) = @{$row};

			$key =~ /^rsm.slv.dns.ns.(\w+)\[(.+)\]$/; # $1 = 'avail' or 'downtime', $2 - ns,ip

			$itemids_map_tmp{$2}{$1} = $itemid;
		}

		%itemids_map = map { $itemids_map_tmp{$_}{'avail'} => $itemids_map_tmp{$_}->{'downtime'} } keys(%itemids_map_tmp);
	}
	else
	{
		my $sql = "select itemid, key_ from items where hostid=? and key_ in (?,?)";
		my $params = [$rsmhostid, $item_key_avail, $item_key_downtime];
		my $rows = db_select($sql, $params);

		fail("failed to get itemids of '$item_key_avail' and '$item_key_downtime'") if (scalar(@{$rows}) != 2);

		my $itemid_avail;
		my $itemid_downtime;

		foreach my $row (@{$rows})
		{
			my ($itemid, $key) = @{$row};

			$itemid_avail    = $itemid if ($key eq $item_key_avail);
			$itemid_downtime = $itemid if ($key eq $item_key_downtime);
		}

		$itemids_map{$itemid_avail} = $itemid_downtime;
	}

	foreach my $itemid_avail (keys(%itemids_map))
	{
		if (!$itemid_avail || !$itemids_map{$itemid_avail})
		{
			dbg(Dumper(\%itemids_map));
			fail("failed to get avail/downtime itemids");
		}
	}

	return %itemids_map;
}

sub __fp_get_history_values($$;$)
{
	my $itemid = shift;
	my $from   = shift;
	my $till   = shift // $from;

	my $sql = "select clock,value from history_uint where itemid=? and clock between ? and ?";
	my $params = [$itemid, $from, $till];
	my $rows = db_select($sql, $params);

	return map { $_->[0] => $_->[1] } @{$rows};
}

sub __fp_get_false_positive_ranges($$$)
{
	my $triggerid = shift;
	my $from      = shift;
	my $till      = shift;

	my $sql = "select" .
		" events.eventid," .
		"events.source," .
		"events.object," .
		"events.clock," .
		"(" .
			"select clock" .
			" from events as events_inner" .
			" where" .
				" events_inner.source=events.source and" .
				" events_inner.object=events.object and" .
				" events_inner.objectid=events.objectid and" .
				" events_inner.value=? and" .
				" events_inner.eventid>events.eventid" .
			" order by events_inner.eventid asc" .
			" limit 1" .
		") as clock2" .
	" from" .
		" events" .
	" where" .
		" events.objectid=? and" .
		" events.value=? and" .
		" events.clock between ? and ? and" .
		" events.false_positive=?";

	my $params = [
		TRIGGER_VALUE_FALSE,
		$triggerid,
		TRIGGER_VALUE_TRUE,
		$from,
		$till,
		1,
	];

	my $rows = db_select($sql, $params);

	my @ranges = ();

	for my $row (@{$rows})
	{
		my ($eventid, $source, $object, $from, $till) = @{$row};

		fail("unexpected value of events.source for incident #$eventid: $source") if ($source != EVENT_SOURCE_TRIGGERS);
		fail("unexpected value of events.object for incident #$eventid: $object") if ($object != EVENT_OBJECT_TRIGGER);

		push(@ranges, [$from, $till]);
	}

	return @ranges;
}

sub __fp_update_incident_state($$$$$)
{
	my $incident_fail    = shift;
	my $incident_recover = shift;
	my $avail            = shift;
	my $counter_ref      = shift;
	my $is_incident_ref  = shift;

	if (${$is_incident_ref})
	{
		if ($avail == DOWN)
		{
			${$counter_ref} = 0;
		}
		else
		{
			if (${$counter_ref} < $incident_recover - 1)
			{
				${$counter_ref}++;
			}
			else
			{
				${$counter_ref} = 0;
				${$is_incident_ref} = 0;
			}
		}
	}
	else
	{
		if ($avail == DOWN)
		{
			if (${$counter_ref} < $incident_fail - 1)
			{
				${$counter_ref}++;
			}
			else
			{
				${$counter_ref} = 0;
				${$is_incident_ref} = 1;
			}
		}
		else
		{
			${$counter_ref} = 0;
		}
	}
}

sub __fp_regenerate_reports($$)
{
	my $service            = shift;
	my $report_updates_ref = shift;

	require DateTime;

	my %report_updates = ();

	my $curr_month = DateTime->now()->truncate('to' => 'month')->epoch();

	foreach my $row (@{$report_updates_ref})
	{
		my ($rsmhost, $clock, $incidentid, $false_positive) = @{$row};

		$clock = DateTime->from_epoch('epoch' => $clock)->truncate('to' => 'month')->epoch();

		if ($clock < $curr_month)
		{
			$report_updates{$rsmhost}{$clock}{$incidentid} = $false_positive;
		}
	}

	foreach my $rsmhost (sort(keys(%report_updates)))
	{
		foreach my $clock (sort {$a <=> $b} keys(%{$report_updates{$rsmhost}}))
		{
			my @incidents = ();
			foreach my $incidentid (keys(%{$report_updates{$rsmhost}{$clock}}))
			{
				if ($report_updates{$rsmhost}{$clock}{$incidentid})
				{
					push(@incidents, "incident $incidentid was marked as false-positive");
				}
				else
				{
					push(@incidents, "incident $incidentid was unmarked as false-positive");
				}
			}

			my $month = ts_ym($clock);
			my $reason = join(", ", @incidents);

			# To safely regenerate the repots, we may need data since the beginning of the month, e.g.,
			# period when name servers were tested might be determined by reading min/max timestamps of RTT
			# checks during the month. Therefore, it's important to check if we have the data since the
			# beginning of the month before trying to regenerate the report.

			my $oldest_clock = max(
				__fp_get_oldest_clock($rsmhost, 'history'),
				__fp_get_oldest_clock($rsmhost, 'history_uint')
			);

			if ($clock >= $oldest_clock)
			{
				generate_report($rsmhost, $clock);
				__fp_log($rsmhost, $service, "regenerated report for $month ($reason)");
			}
			else
			{
				__fp_log($rsmhost, $service, "could not regenerate report for $month, not enough data in the database ($reason)");
			}
		}
	}
}

sub __fp_get_oldest_clock($$)
{
	my $rsmhost = shift;
	my $table   = shift;

	my $sql = "select" .
			" min(clock)" .
		" from" .
			" $table" .
			" left join items on items.itemid=$table.itemid" .
			" left join hosts on hosts.hostid=items.hostid" .
		" where" .
			" hosts.host=?";

	my $params = [$rsmhost];

	return db_select_value($sql, $params);
}

#################
# Internal subs #
#################

my $program = $0; $program =~ s,.*/,,g;
my $logopt = 'pid';
my $facility = 'user';
my $prev_tld = "";

sub __func
{
	my $depth = 3;

	my $func = (caller($depth))[3];

	$func =~ s/^[^:]*::(.*)$/$1/ if (defined($func));

	return "$func() " if (defined($func));

	return "";
}

sub __script
{
	my $script = $0;

	$script =~ s,.*/([^/]*)$,$1,;

	return $script;
}


sub __init_stdout_lock
{
	if (!defined($stdout_lock_handle))
	{
		my $username = $ENV{LOGNAME} || $ENV{USER} || getpwuid($<) || getlogin() || 'unknown';

		$stdout_lock_file = PID_DIR . '/' . __script() . ".${username}.stdout.lock";

		open($stdout_lock_handle, ">", $stdout_lock_file) or die("cannot open \"$stdout_lock_file\": $!");
	}
}

sub __log
{
	my $syslog_priority = shift;
	my $msg = shift;

	my $priority;
	my $stdout = 1;

	if ($syslog_priority eq 'info')
	{
		$priority = 'INF';
	}
	elsif ($syslog_priority eq 'err')
	{
		$stdout = 0 unless (opt('debug') || opt('dry-run'));
		$priority = 'ERR';
	}
	elsif ($syslog_priority eq 'warning')
	{
		$stdout = 0 unless (opt('debug') || opt('dry-run'));
		$priority = 'WRN';
	}
	elsif ($syslog_priority eq 'debug')
	{
		$priority = 'DBG';
	}
	else
	{
		$priority = 'UND';
	}

	my $cur_tld = $tld // "";
	my $server_str = ($server_key ? "\@$server_key " : "");

	if (opt('dry-run') or opt('nolog'))
	{
		__init_stdout_lock();

		flock($stdout_lock_handle, LOCK_EX) or die("cannot lock \"$stdout_lock_file\": $!");

		if (log_only_message())
		{
			print {$stdout ? *STDOUT : *STDERR} ("[$priority] $msg\n");
		}
		else
		{
			print {$stdout ? *STDOUT : *STDERR} (sprintf("%6d:", $$), ts_str(), " [$priority] ", $server_str, ($cur_tld eq "" ? "" : "$cur_tld: "), __func(), "$msg\n");
		}

		# flush stdout
		select()->flush();

		flock($stdout_lock_handle, LOCK_UN) or die("cannot unlock \"$stdout_lock_file\": $!");

		return;
	}

	my $ident = ($cur_tld eq "" ? "" : "$cur_tld-") . $program;

	if ($log_open == 0)
	{
		openlog($ident, $logopt, $facility);
		$log_open = 1;
	}
	elsif ($cur_tld ne $prev_tld)
	{
		closelog();
		openlog($ident, $logopt, $facility);
	}

	syslog($syslog_priority, sprintf("%6d:", $$) . ts_str() . " [$priority] " . $server_str . $msg);	# second parameter is the log message

	$prev_tld = $cur_tld;
}

sub __get_macro
{
	my $m = shift;

	my $rows_ref = db_select("select value from globalmacro where macro='$m'");

	fail("cannot find macro '$m'") unless (1 == scalar(@$rows_ref));

	return $rows_ref->[0]->[0];
}

sub __get_pidfile
{
	return PID_DIR . '/' . __script() . '.pid';
}

1;
