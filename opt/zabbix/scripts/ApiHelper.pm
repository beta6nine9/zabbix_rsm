package ApiHelper;

use strict;
use warnings;

use RSM;
use RSMSLV;
use File::Path qw(make_path);
use DateTime::Format::RFC3339;
use base 'Exporter';
use JSON::XS;
use Types::Serialiser;
use File::Copy;
use Fcntl qw(:flock);
use File::Copy;

use constant AH_SUCCESS => 0;
use constant AH_FAIL => 1;

use constant AH_INTERFACE_DNS => 'DNS';
use constant AH_INTERFACE_DNSSEC => 'DNSSEC';
use constant AH_INTERFACE_RDDS43 => 'RDDS43';
use constant AH_INTERFACE_RDDS80 => 'RDDS80';
use constant AH_INTERFACE_RDAP => 'RDAP';
use constant AH_INTERFACE_EPP => 'EPP';

use constant AH_CITY_UP => 'Up';
use constant AH_CITY_DOWN => 'Down';
use constant AH_CITY_OFFLINE => 'Offline';
use constant AH_CITY_NO_RESULT => 'No result';

use constant AH_INCIDENT_ACTIVE => 'ACTIVE';
use constant AH_STATE_FILE => 'state';
use constant AH_INCIDENT_STATE_FILE => 'state';
use constant AH_FALSE_POSITIVE_FILE => 'falsePositive';
use constant AH_ALARMED_FILE => 'alarmed';
use constant AH_DOWNTIME_FILE => 'downtime';
use constant AH_SLA_API_DIR => '/opt/zabbix/sla';
use constant AH_SLA_API_TMP_DIR => '/opt/zabbix/sla-tmp';

use constant AH_ROOT_ZONE_DIR => 'zz--root';			# map root zone name (.) to something human readable

use constant AH_CONTINUE_FILE		=> 'last_update.txt';	# file with timestamp of last run with --continue
use constant AH_AUDIT_FILE_PREFIX	=> 'last_audit_';	# file containing timestamp of last auditlog entry that
								# was processed, is saved per db (false_positive change):
								# AH_AUDIT_FILE_PREFIX _ <SERVER_KEY> .txt

use constant JSON_VALUE_INCIDENT_ACTIVE => 'Active';
use constant JSON_VALUE_INCIDENT_RESOLVED => 'Resolved';

use constant JSON_OBJECT_DISABLED_SERVICE => {
	'status'	=> 'Disabled'
};

use constant JSON_VALUE_NUMBER         => 1;
use constant JSON_VALUE_STRING         => 2;
use constant JSON_VALUE_BOOLEAN        => 3;
use constant JSON_VALUE_NUMBER_OR_NULL => 4;

# keep fields in alphabetical order
my $JSON_FIELDS = {
	'cycleCalculationDateTime' => JSON_VALUE_NUMBER,
	'emergencyThreshold'       => JSON_VALUE_NUMBER,
	'downtime'                 => JSON_VALUE_NUMBER,
	'lastUpdateApiDatabase'    => JSON_VALUE_NUMBER,
	'minNameServersUp'         => JSON_VALUE_NUMBER,
	'testDateTime'             => JSON_VALUE_NUMBER,
	'version'                  => JSON_VALUE_NUMBER,
	'alarmed'                  => JSON_VALUE_STRING,
	'city'                     => JSON_VALUE_STRING,
	'incidentID'               => JSON_VALUE_STRING,
	'interface'                => JSON_VALUE_STRING,
	'nsid'                     => JSON_VALUE_STRING,
	'result'                   => JSON_VALUE_STRING,
	'service'                  => JSON_VALUE_STRING,
	'state'                    => JSON_VALUE_STRING,
	'status'                   => JSON_VALUE_STRING,
	'target'                   => JSON_VALUE_STRING,
	'targetIP'                 => JSON_VALUE_STRING,
	'testedName'               => JSON_VALUE_STRING,
	'tld'                      => JSON_VALUE_STRING,
	'transport'                => JSON_VALUE_STRING,
	'falsePositive'            => JSON_VALUE_BOOLEAN,
	'endTime'                  => JSON_VALUE_NUMBER_OR_NULL,
	'rtt'                      => JSON_VALUE_NUMBER_OR_NULL,
	'startTime'                => JSON_VALUE_NUMBER_OR_NULL,
	'updateTime'               => JSON_VALUE_NUMBER_OR_NULL,
};

our @EXPORT = qw(
	AH_SUCCESS AH_FAIL
	AH_SLA_API_DIR
	AH_SLA_API_TMP_DIR ah_set_debug ah_get_error ah_read_state ah_save_state
	ah_save_alarmed ah_save_downtime ah_create_incident_json ah_save_incident
	ah_save_false_positive
	ah_continue_file_name ah_lock_continue_file ah_unlock_continue_file
	ah_get_api_tld ah_get_last_audit
	ah_copy_measurement ah_save_measurement ah_save_recent_cache ah_read_recent_cache
	ah_get_most_recent_measurement_ts
	ah_save_audit ah_save_continue_file JSON_OBJECT_DISABLED_SERVICE
	AH_INTERFACE_DNS AH_INTERFACE_DNSSEC AH_INTERFACE_RDDS43 AH_INTERFACE_RDDS80 AH_INTERFACE_RDAP AH_INTERFACE_EPP
	AH_CITY_UP AH_CITY_DOWN AH_CITY_NO_RESULT AH_CITY_OFFLINE
	AH_SLA_API_VERSION_1 AH_SLA_API_VERSION_2
);

use constant AH_SLA_API_VERSION_1 => 1;
use constant AH_SLA_API_VERSION_2 => 2;

my $_error_string = "";
my $_debug = 0;
my $_json_xs;

sub ah_set_debug(;$)
{
	my $value = shift;

	$_debug = (defined($value) && $value != 0 ? 1 : 0);
}

sub ah_get_error()
{
	return $_error_string;
}

sub __ts_str(;$)
{
	my $ts = shift;

	my ($sec, $min, $hour, $mday, $mon, $year, $wday, $yday, $isdst) = localtime($ts);

	return sprintf("%.4d%.2d%.2d:%.2d%.2d%.2d", $year + 1900, $mon + 1, $mday, $hour, $min, $sec);
}

sub __ts_full(;$)
{
	my $ts = shift;

	$ts = time() unless ($ts);

	return __ts_str($ts) . " ($ts)";
}

# Generate path for SLA API file or directory, e. g.
#
#   [base_dir]/v1/example/monitoring/dns
#
sub __gen_base_path($$;$$)
{
	my $version = shift;
	my $tld = shift;
	my $service = shift;
	my $add_path = shift;

	my $path = "v$version/$tld/monitoring";

	$path .= "/$service" if (defined($service));
	$path .= "/$add_path" if (defined($add_path));

	return $path;
}

# Generate path for SLA API incident directory, e. g.
#
#   [base_dir]/v1/example/monitoring/dns/incidents/1548853740.13468
#
sub __gen_inc_path($$$$$)
{
	my $version = shift;
	my $tld = shift;
	my $service = shift;
	my $eventid = shift;
	my $start = shift;

	return __gen_base_path($version, $tld, $service, "incidents/$start.$eventid");
}

sub __make_base_path($$$$)
{
	my $version = shift;
	my $tld = shift;
	my $service = shift;
	my $result_path_ptr = shift;	# pointer

	my $path = AH_SLA_API_TMP_DIR . '/' . __gen_base_path($version, $tld, $service, undef);

	make_path($path, {error => \my $err});

	if (@$err)
	{
		__set_file_error($err);
		return AH_FAIL;
	}

	$$result_path_ptr = $path;

	return AH_SUCCESS;
}

sub __make_inc_path($$$$$$)
{
	my $version = shift;
	my $tld = shift;
	my $service = shift;
	my $start = shift;
	my $eventid = shift;
	my $inc_path_ptr = shift;	# pointer

	my $path = AH_SLA_API_TMP_DIR . '/' . __gen_inc_path($version, $tld, $service, $eventid, $start);

	make_path($path, {error => \my $err});

	if (@$err)
	{
		__set_file_error($err);
		return AH_FAIL;
	}

	$$inc_path_ptr = $path;

	return AH_SUCCESS;
}

sub __make_path($)
{
	my $path = shift;

	make_path($path, {error => \my $err});

	if (@$err)
	{
		__set_file_error($err);
		return AH_FAIL;
	}

	return AH_SUCCESS;
}

sub __set_error
{
	$_error_string = join('', @_);
}

sub __set_file_error
{
	my $err = shift;

	$_error_string = "";

	if (ref($err) eq "ARRAY")
	{
		for my $diag (@$err)
		{
			my ($file, $message) = %$diag;
			if ($file eq '')
			{
				$_error_string .= "$message. ";
			}
			else
			{
				$_error_string .= "$file: $message. ";
			}

			return;
		}
	}

	$_error_string = join('', $err, @_);
}

sub __write_file
{
	my $full_path = shift;
	my $text = shift;
	my $clock = shift;

	my $OUTFILE;
	my $full_path_new = $full_path . ".new";

	unless (open($OUTFILE, '>', $full_path_new))
	{
		__set_error("cannot open file \"$full_path_new\": $!");
		return AH_FAIL;
	}

	unless (print {$OUTFILE} $text)
	{
		__set_error("cannot write to \"$full_path_new\": $!");
		return AH_FAIL;
	}

	unless (close($OUTFILE))
	{
		__set_error("cannot close file \"$full_path_new\": $!");
		return AH_FAIL;
	}

	unless (move($full_path_new, $full_path))
	{
		__set_error("cannot move \"$full_path_new\" to \"$full_path\": $!");
		return AH_FAIL;
	}

	if (defined($clock) && !utime($clock, $clock, $full_path))
	{
		__set_error("cannot set mtime of \"$full_path\": $!");
		return AH_FAIL;
	}

	RSMSLV::dbg("wrote file \"$full_path\"");

	if ($_debug)
	{
		my $buf;

		return AH_FAIL unless (__read_file($full_path, \$buf) == AH_SUCCESS);

		if ($buf ne $text)
		{
			__set_error("contents of file \"$full_path\" is unexpected after writing, expected ==>$text<== got ==>$buf<==");
			return AH_FAIL;
		}
	}

	return AH_SUCCESS;
}

sub __fix_json_values($)
{
	my @values = (shift);

	while (@values)
	{
		my $value = pop(@values);

		if (ref($value) eq 'ARRAY')
		{
			push(@values, @{$value});
		}
		elsif (ref($value) eq 'HASH')
		{
			foreach my $field (keys(%{$value}))
			{
				if (ref($value->{$field}) eq '')
				{
					if (!exists($JSON_FIELDS->{$field}))
					{
						die("unknown field: $field\n");
					}

					if ($JSON_FIELDS->{$field} == JSON_VALUE_NUMBER)
					{
						$value->{$field} += 0;
					}
					elsif ($JSON_FIELDS->{$field} == JSON_VALUE_STRING)
					{
						$value->{$field} = (defined($value->{$field}) ? "$value->{$field}" : undef);
					}
					elsif ($JSON_FIELDS->{$field} == JSON_VALUE_BOOLEAN)
					{
						$value->{$field} = ($value->{$field} ? Types::Serialiser::true : Types::Serialiser::false);
					}
					elsif ($JSON_FIELDS->{$field} == JSON_VALUE_NUMBER_OR_NULL)
					{
						$value->{$field} = int_or_null($value->{$field});
					}
					else
					{
						die("unknown \"$field\" value type: $JSON_FIELDS->{$field}\n");
					}
				}
				else
				{
					push(@values, $value->{$field});
				}
			}
		}
	}
}

sub __save_inc_false_positive($$$$)
{
	my $version = shift;
	my $inc_path = shift;
	my $false_positive = shift;
	my $clock = shift;

	my $false_positive_path = "$inc_path/" . AH_FALSE_POSITIVE_FILE;

	my $json =
	{
		'falsePositive' => ($false_positive ? Types::Serialiser::true : Types::Serialiser::false),
		'updateTime' => int_or_null($clock)
	};

	return __write_file($false_positive_path, __encode_json($version, $json, 1));
}

sub ah_read_state($$$)
{
	my $version = shift;
	my $ah_tld = shift;
	my $json_ref = shift;

	my $state_path = AH_SLA_API_DIR . '/' . __gen_base_path($version, $ah_tld, undef, undef) . '/' . AH_STATE_FILE;
	my $buf;

	return AH_FAIL unless (__read_file($state_path, \$buf) == AH_SUCCESS);

	$$json_ref = decode_json($buf);

	return AH_SUCCESS;
}

sub ah_save_state($$$)
{
	my $version = shift;
	my $ah_tld = shift;
	my $json = shift;

	my $base_path;

	return AH_FAIL unless (__make_base_path($version, $ah_tld, undef, \$base_path) == AH_SUCCESS);

	my $state_path = "$base_path/" . AH_STATE_FILE;

	return __write_file($state_path, __encode_json($version, $json, 1));
}

sub ah_save_alarmed($$$$;$)
{
	my $version = shift;
	my $tld = shift;
	my $service = shift;
	my $status = shift;
	my $clock = shift;

	my $base_path;

	return AH_FAIL unless (__make_base_path($version, $tld, $service, \$base_path) == AH_SUCCESS);

	my $alarmed_path = "$base_path/" . AH_ALARMED_FILE;

	my $json = {'alarmed' => $status};

	return __write_file($alarmed_path, __encode_json($version, $json, 1), $clock);
}

sub ah_save_downtime($$$$$)
{
	my $version = shift;
	my $tld = shift;
	my $service = shift;
	my $downtime = shift;
	my $clock = shift;

	my $base_path;

	return AH_FAIL unless (__make_base_path($version, $tld, $service, \$base_path) == AH_SUCCESS);

	my $alarmed_path = "$base_path/" . AH_DOWNTIME_FILE;

	my $json = {'downtime' => $downtime};

	return __write_file($alarmed_path, __encode_json($version, $json, 1), $clock);
}

sub ah_create_incident_json($$$$)
{
	my $eventid = shift;	# incident is identified by event ID
	my $start = shift;
	my $end = shift;
	my $false_positive = shift;

	return
	{
		'incidentID' => "$start.$eventid",
		'startTime' => int_or_null($start),
		'endTime' => int_or_null($end),
		'falsePositive' => ($false_positive ? Types::Serialiser::true : Types::Serialiser::false),
		'state' => (defined($end) ? JSON_VALUE_INCIDENT_RESOLVED : JSON_VALUE_INCIDENT_ACTIVE)
	};
}

sub __save_inc_state($$$$)
{
	my $version = shift;
	my $inc_path = shift;
	my $json = shift;
	my $lastclock = shift;

	my $inc_state_path = "$inc_path/" . AH_INCIDENT_STATE_FILE;

	return __write_file($inc_state_path, __encode_json($version, $json, 1), $lastclock);
}

sub ah_save_incident($$$$$$$$$)
{
	my $version = shift;
	my $tld = shift;
	my $service = shift;
	my $eventid = shift;	# incident is identified by event ID
	my $event_clock = shift;
	my $start = shift;
	my $end = shift;
	my $false_positive = shift;
	my $lastclock = shift;

	my $inc_path;

	return AH_FAIL unless (__make_inc_path($version, $tld, $service, $event_clock, $eventid, \$inc_path) == AH_SUCCESS);

	my $json = {'incidents' => [ah_create_incident_json($eventid, $start, $end, $false_positive)]};

	return AH_FAIL unless (__save_inc_state($version, $inc_path, $json, $lastclock) == AH_SUCCESS);

	# If the there's no falsePositive file yet, just create it with updateTime null.
	# Otherwise do nothing, it should always contain correct false positiveness.
	# The false_positive changes will be updated later, when calling ah_save_false_positive().

	my $buf;
	if (__read_inc_file($version, $tld, $service, $eventid, $start, AH_FALSE_POSITIVE_FILE, \$buf) == AH_SUCCESS)
	{
		my $current_json = decode_json($buf);

		if ($false_positive eq $current_json->{'falsePositive'})
		{
			# content hasn't changed
			return AH_SUCCESS;
		}
	}

	return __save_inc_false_positive($version, $inc_path, $false_positive, undef);
}

sub __read_file($$)
{
	my $file = shift;
	my $buf_ref = shift;

	$$buf_ref = do
	{
		local $/ = undef;
		my $fh;
		if (!open($fh, "<", $file))
		{
			__set_error("cannot open file \"$file\": $!");
			return AH_FAIL;
		}

		<$fh>;
	};

	return AH_SUCCESS;
}

sub __copy_file($$$)
{
	my $src = shift;
	my $dst = shift;
	my $clock = shift;	# mtime

	if (!copy($src, $dst))
	{
		__set_error("cannot copy \"$src\" to \"$dst\": $!");
		return AH_FAIL;
	}

	if (!utime($clock, $clock, $dst))
	{
		__set_error("cannot set mtime of \"$dst\": $!");
		return AH_FAIL;
	}

	return AH_SUCCESS;
}

# read current file from incident directory
sub __read_inc_file($$$$$$$)
{
	my $version = shift;
	my $tld = shift;
	my $service = shift;
	my $eventid = shift;
	my $start = shift;
	my $file = shift;
	my $buf_ref = shift;

	$file = AH_SLA_API_DIR . '/' . __gen_inc_path($version, $tld, $service, $eventid, $start) . '/' . $file;

	RSMSLV::dbg("reading file: $file");

	return __read_file($file, $buf_ref);
}

# When saving false positiveness, read from AH_SLA_API_DIR, write to AH_SLA_API_TMP_DIR.
#
# We need to get the incident state file from AH_SLA_API_DIR in order to get current
# "falsePositive" value and if it has changed, update it in the state file. We
# don't want to change any other parameter (e. g. incident start time) of the
# incident in the state file.
#
# If we received a false positiveness update request but the incident is not yet
# processed (no incident state file) we ignore this change and notify the caller
# about the need to try updating false positiveness later by setting $later_ref
# flag to 1.
sub ah_save_false_positive($$$$$$$$)
{
	my $version = shift;
	my $tld = shift;
	my $service = shift;
	my $eventid = shift;	# incident is identified by event ID
	my $start = shift;
	my $false_positive = shift;
	my $clock = shift;
	my $later_ref = shift;	# should we update false positiveness later? (incident state file does not exist yet)

	if (!defined($later_ref))
	{
		die("internal error: ah_save_false_positive() called without last parameter");
	}

	my $buf;
	if (__read_inc_file($version, $tld, $service, $eventid, $start, AH_INCIDENT_STATE_FILE, \$buf) == AH_FAIL)
	{
		# no incident state file yet, do not update false positiveness at this point
		$$later_ref = 1;

		my $curr_err = ah_get_error();
		__set_error("incident state file not found, try to update false positiveness (changed at ", __ts_full($clock), ") later (error was: $curr_err)");

		return AH_FAIL;
	}

	my $json = decode_json($buf);

	my $inc_path;

	return AH_FAIL unless (__make_inc_path($version, $tld, $service, $start, $eventid, \$inc_path) == AH_SUCCESS);

	my $curr_false_positive = (($json->{'incidents'}->[0]->{'falsePositive'} eq Types::Serialiser::true) ? 1 : 0);

	if ($curr_false_positive != $false_positive)
	{
		RSMSLV::dbg("false positiveness of $eventid changed: $false_positive");

		$json->{'incidents'}->[0]->{'falsePositive'} = ($false_positive ? Types::Serialiser::true : Types::Serialiser::false);

		return AH_FAIL unless (__save_inc_state($version, $inc_path, $json, $clock) == AH_SUCCESS);
	}

	return __save_inc_false_positive($version, $inc_path, $false_positive, $clock);
}

# Base path for recent measurement, e. g.
#
#   [base_dir]/v1/example/monitoring/dns/measurements/2018/02/28
#
sub __gen_measurement_base_path($$$$)
{
	my $version = shift;
	my $ah_tld = shift;
	my $service = shift;
	my $clock = shift;

	my (undef, undef, undef, $mday, $mon, $year) = localtime($clock);

	$year += 1900;
	$mon++;

	my $add_path = sprintf("measurements/%04d/%02d/%02d", $year, $mon, $mday);

	return AH_SLA_API_DIR . '/' . __gen_base_path($version, $ah_tld, $service, $add_path);
}

# Generate path for recent measurement, e. g.
#
#   [base_dir]/v1/example/monitoring/dns/measurements/2018/02/28/<measurement>.json
#
sub __gen_measurement_path($$$$$$)
{
	my $version = shift;
	my $ah_tld = shift;
	my $service = shift;
	my $clock = shift;
	my $path_buf = shift;	# pointer to result
	my $create = shift;	# create missing directories

	my $path = __gen_measurement_base_path($version, $ah_tld, $service, $clock);

	if ($create)
	{
		return AH_FAIL unless (__make_path($path) == AH_SUCCESS);
	}

	$$path_buf = $path . "/$clock.json";

	return AH_SUCCESS;
}

sub ah_copy_measurement($$$$$$)
{
	my $version = shift;
	my $ah_tld = shift;
	my $service = shift;
	my $clock = shift;
	my $eventid = shift;
	my $event_start = shift;

	my $src_path;

	# do not create missing directories
	return AH_FAIL unless (__gen_measurement_path($version, $ah_tld, $service, $clock, \$src_path, 0) == AH_SUCCESS);

	my $inc_path;

	return AH_FAIL unless (__make_inc_path($version, $ah_tld, $service, $event_start, $eventid, \$inc_path) == AH_SUCCESS);

	my $dst_path = "$inc_path/$clock.$eventid.json";

	return __copy_file($src_path, $dst_path, $clock);
}

sub ah_save_measurement($$$$$)
{
	my $version = shift;
	my $ah_tld = shift;
	my $service = shift;
	my $json = shift;
	my $clock = shift;

	my $path;

	# force creation of missing directories
	return AH_FAIL unless (__gen_measurement_path($version, $ah_tld, $service, $clock, \$path, 1) == AH_SUCCESS);

	return __write_file($path, __encode_json($version, $json, 1), $clock);
}

# Generate path for recent measurement cache, e. g.
#
#   /opt/zabbix/cache/server_1/example.json
#
sub __gen_recent_cache_path($$)
{
	my $server_key = shift;
	my $path_buf = shift;

	my $path = "/opt/zabbix/cache/sla-api";

	return AH_FAIL unless (__make_path($path) == AH_SUCCESS);

	$$path_buf = $path . "/$server_key.json";

	return AH_SUCCESS;
}

sub ah_save_recent_cache($$)
{
	my $server_key = shift;
	my $json = shift;

	my $path;

	return AH_FAIL unless (__gen_recent_cache_path($server_key, \$path) == AH_SUCCESS);

	return __write_file($path, __encode_json(AH_SLA_API_VERSION_1, $json, 0));	# do not attempt to fix JSON values
}

sub ah_read_recent_cache($$)
{
	my $server_key = shift;
	my $json_ref = shift;

	my ($path, $buf);

	return AH_FAIL unless (__gen_recent_cache_path($server_key, \$path) == AH_SUCCESS);

	return AH_FAIL unless (__read_file($path, \$buf) == AH_SUCCESS);

	$$json_ref = decode_json($buf);

	return AH_SUCCESS;
}

sub ah_get_most_recent_measurement_ts($$$$$$$)
{
	my $version = shift;
	my $ah_tld = shift;
	my $service = shift;
	my $delay = shift;		# use this delay to jump to the next possible file
	my $newest_clock = shift;	# start searching from here
	my $oldest_clock = shift;	# do not go further than this to the path
	my $ts_buf = shift;		# pointer to result

	if ($newest_clock < $oldest_clock)
	{
		__set_error("invalid time period: from $oldest_clock till $newest_clock");
		return AH_FAIL;
	}

	my $clock = $newest_clock;

	my %search_paths;

	while ($clock > $oldest_clock)
	{
		my $path = __gen_measurement_base_path($version, $ah_tld, $service, $clock);

		$search_paths{$path} = 1;

		if (-f "$path/$clock.json")
		{
			$$ts_buf = $clock;
			return AH_SUCCESS;
		}

		$clock -= $delay;
	}

	__set_error("no measurement files found between ", $oldest_clock, " and ", $newest_clock,
		", search paths:\n    ", join("\n    ", keys(%search_paths)));

	return AH_FAIL;
}

sub ah_continue_file_name()
{
	return AH_SLA_API_DIR . '/' . AH_CONTINUE_FILE;
}

sub ah_lock_continue_file($)
{
	my $handle_ref = shift;

	my $lock_file = ah_continue_file_name() . '.lock';

	open(${$handle_ref}, '>>', $lock_file) or RSMSLV::fail("cannot open \"$lock_file\": $!");
	flock(${$handle_ref}, LOCK_EX) or RSMSLV::fail("cannot lock \"$lock_file\": $!");
}

sub ah_unlock_continue_file($)
{
	my $handle = shift;

	my $lock_file = ah_continue_file_name() . '.lock';

	flock($handle, LOCK_UN) or RSMSLV::fail("cannot unlock \"$lock_file\": $!");
	close($handle) or RSMSLV::fail("cannot close '$lock_file': $!");
}

sub ah_save_continue_file
{
	my $ts = shift;

	return __write_file(AH_SLA_API_TMP_DIR . '/' . AH_CONTINUE_FILE, $ts);
}

sub ah_get_api_tld
{
	my $tld = shift;

	return AH_ROOT_ZONE_DIR if ($tld eq ".");

	return $tld;
}

sub __get_audit_file_path
{
	my $server_key = shift;

	return AH_SLA_API_DIR . '/' . AH_AUDIT_FILE_PREFIX . $server_key . '.txt';
}

sub __encode_json($$$)
{
	my $version = shift;
	my $json_ref = shift;
	my $fix_values = shift;

	__fix_json_values($json_ref) if ($fix_values == 1);

	$json_ref->{'version'} = $version;
	$json_ref->{'lastUpdateApiDatabase'} = $^T;

	if (!defined($_json_xs))
	{
		$_json_xs = JSON::XS->new();
		$_json_xs->utf8();
		$_json_xs->canonical();
		$_json_xs->pretty() if (opt('prettify-json'));
	}

	return $_json_xs->encode($json_ref);
}

# get the time of last audit log entry that was checked
sub ah_get_last_audit
{
	my $server_key = shift;

	die("Internal error: ah_get_last_audit() server_key not specified") unless ($server_key);

	my $audit_file = __get_audit_file_path($server_key);

	my $handle;

	if (-e $audit_file)
	{
		if (!open($handle, '<', $audit_file))
		{
			RSMSLV::fail("cannot open last audit check file $audit_file\": $!");
		}

		chomp(my @lines = <$handle>);

		close($handle);

		return $lines[0];
	}

	return 0;
}

sub ah_save_audit
{
	my $server_key = shift;
	my $clock = shift;

	die("Internal error: ah_save_audit() server_key not specified") unless ($server_key && $clock);

	return __write_file(AH_SLA_API_TMP_DIR . '/' . AH_AUDIT_FILE_PREFIX . $server_key . '.txt', $clock);
}

sub int_or_null
{
	my $val = shift;

	return defined($val) ? int($val) : $val;
}

1;
