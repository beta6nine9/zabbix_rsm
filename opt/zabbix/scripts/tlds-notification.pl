#!/usr/bin/env perl

use strict;
use warnings;

use DBI;
use Data::Dumper;
use Devel::StackTrace;
use Fcntl qw(:flock SEEK_END);
use Getopt::Long qw(GetOptionsFromArray);
use Pod::Usage;

use constant MAX_EXECUTION_TIME           => 60;
use constant HISTORY_RETRY_DELAY          => 5;

use constant LOG_FILE                     => '/var/log/zabbix/tlds-notification.log';
use constant ZABBIX_SERVER_CONF_FILE      => '/etc/zabbix/zabbix_server.conf';
use constant EXTERNAL_NOTIFICATION_SCRIPT => '{AlertScriptsPath}/script.py';

use constant EVENT_SOURCE_TRIGGERS        => 0; # event was generated by a trigger status change
use constant EVENT_OBJECT_TRIGGER         => 0; # trigger
use constant TRIGGER_VALUE_FALSE          => 0; # trigger changed state to OK
use constant TRIGGER_VALUE_TRUE           => 1; # trigger changed state to PROBLEM
use constant ITEM_VALUE_TYPE_FLOAT        => 0; # float
use constant ITEM_VALUE_TYPE_UINT64       => 3; # unsigned integer

my $item_key_local_resolver_status = 'resolver.status[{$RSM.RESOLVER},{$RESOLVER.STATUS.TIMEOUT},{$RESOLVER.STATUS.TRIES},{$RSM.IP4.ENABLED},{$RSM.IP6.ENABLED}]';
my $item_key_probe_status_automatic = 'rsm.probe.status[automatic,"{$RSM.IP4.ENABLED}","{$RSM.IP6.ENABLED}","{$RSM.IP4.ROOTSERVERS1}","{$RSM.IP6.ROOTSERVERS1}","{$RSM.IP4.MIN.SERVERS}","{$RSM.IP6.MIN.SERVERS}","{$RSM.IP4.REPLY.MS}","{$RSM.IP6.REPLY.MS}","{$RSM.PROBE.ONLINE.DELAY}"]';

my %value_maps = (
	'rsm.slv.dns.avail'               => 'RSM Service Availability',
	'rsm.slv.dnssec.avail'            => 'RSM Service Availability',
	'rsm.slv.rdap.avail'              => 'RSM Service Availability',
	'rsm.slv.rdds.avail'              => 'RSM Service Availability',
	$item_key_local_resolver_status   => 'Service state',
	$item_key_probe_status_automatic  => 'RSM Probe status',
	'rsm.probe.status[manual]'        => 'RSM Probe status',
	'rsm.probe.online'                => 'RSM Probe status',
	'rdap.rtt'                        => 'RSM RDAP rtt',
	'rdap.status'                     => 'Service state',
	'rsm.dns.mode'                    => 'DNS test mode',
	'rsm.dns.protocol'                => 'Transport protocol',
	'rsm.dns.status'                  => 'Service state',
	'rsm.rdds.43.rtt'                 => 'RSM RDDS rtt',
	'rsm.rdds.status'                 => 'Service state',
	'rsm.rdds.80.rtt'                 => 'RSM RDDS rtt',
	'rsm.rdds.43.status'              => 'Service state',
	'rsm.rdds.80.status'              => 'Service state',
	'rsm.dnssec.status'               => 'Service state',
);

################################################################################
# main
################################################################################

sub main()
{
	parse_opts();

	usage("--send-to is missing", 1) if (!opt('send-to'));
	usage("--event-id is missing", 1) if (!opt('event-id'));

	initialize();

	if (opt('debug'))
	{
		log_debug_messages(1);
	}

	process_event(getopt('send-to'), getopt('event-id'));

	finalize();
}

sub process_event($$)
{
	my $send_to  = shift;
	my $event_id = shift;

	info("event id: %d", $event_id);

	my $rows;

	$rows = db_select(
		"select" .
			" events.source," .
			"events.object," .
			"events.objectid," .
			"events.clock," .
			"events.value" .
		" from" .
			" events" .
		" where" .
			" events.eventid=?", [$event_id]);

	fail("event not found") if (@{$rows} == 0);
	fail("multiple events found") if (@{$rows} > 1);

	my ($event_source, $event_object, $trigger_id, $event_clock, $event_value) = @{$rows->[0]};

	fail("unexpected event source") if ($event_source != EVENT_SOURCE_TRIGGERS);
	fail("unexpected event object") if ($event_object != EVENT_OBJECT_TRIGGER);
	fail("unexpected event value") if ($event_value != TRIGGER_VALUE_FALSE && $event_value != TRIGGER_VALUE_TRUE);

	if ($event_value == TRIGGER_VALUE_FALSE)
	{
		my $query = "select exists(select * from events where objectid=? and eventid<?)";
		my $params = [$trigger_id, $event_id];

		if (!db_select_value($query, $params))
		{
			info("skipping because this is the first OK event after creating new trigger");
			return;
		}
	}

	$rows = db_select("select distinct itemid from functions where triggerid=?", [$trigger_id]);

	fail("itemid not found in trigger functions") if (@{$rows} == 0);
	fail("multiple itemids found in trigger functions") if (@{$rows} > 1);

	$rows = db_select(
		"select" .
			" items.itemid," .
			"items.key_," .
			"hosts.hostid," .
			"hosts.name" .
		" from" .
			" items" .
			" left join hosts on hosts.hostid = items.hostid" .
		" where" .
			" items.itemid=?", [$rows->[0][0]]);

	my ($item_id, $item_key, $host_id, $host_name) = @{$rows->[0]};

	dbg("event_clock = %s", $event_clock // 'UNDEF');
	dbg("event_value = %s", $event_value // 'UNDEF');
	dbg("item_key    = %s", $item_key    // 'UNDEF'); # to find out what type of trigger it is
	dbg("host_id     = %s", $host_id     // 'UNDEF');
	dbg("host_name   = %s", $host_name   // 'UNDEF');

	my $data = get_trigger_data($event_clock, $host_id, $item_id, $item_key);

	notify($send_to, $event_value, $event_clock, $host_name, $data);
}

sub get_trigger_data($$$$)
{
	my $event_clock = shift;
	my $host_id     = shift;
	my $item_id     = shift;
	my $item_key    = shift;

	my $data;

	if ($item_key eq 'rsm.slv.dns.downtime')
	{
		$data = [
			'SLR_CURRENT_MONTH_DNS_service_availability',
			get_history('history_uint', $item_id, $event_clock),
		];
	}
	elsif ($item_key =~ /^rsm\.slv\.dns\.ns\.downtime\[(.+),(.+)\]$/)
	{
		my $ns = $1;
		my $ip = $2;

		my $slv = get_history('history_uint', $item_id, $event_clock);
		my $slr = get_macro('{$RSM.SLV.NS.DOWNTIME}');

		$data = [
			'SLR_CURRENT_MONTH_NS_availability',
			$ns,
			$ip,
			format_float($slv / $slr * 100, '%'),
			$slv,
		];
	}
	elsif ($item_key eq 'rsm.slv.rdds.downtime')
	{
		my $slv = get_history('history_uint', $item_id, $event_clock);
		my $slr = get_macro('{$RSM.SLV.RDDS.DOWNTIME}');

		$data = [
			'SLR_CURRENT_MONTH_RDDS_service_availability',
			format_float($slv / $slr * 100, '%'),
			$slv,
		];
	}
	elsif ($item_key eq 'rsm.slv.dns.udp.rtt.pfailed')
	{
		my $slv = get_history('history', get_item_id($host_id, $item_key), $event_clock);
		my $slr = get_macro('{$RSM.SLV.DNS.UDP.RTT}');

		$data = [
			'SLR_CURRENT_MONTH_DNS_UDP_RTT_availability',
			format_float($slv / $slr * 100, '%'),
			get_history('history_uint', get_item_id($host_id, 'rsm.slv.dns.udp.rtt.performed'), $event_clock),
			get_history('history_uint', get_item_id($host_id, 'rsm.slv.dns.udp.rtt.failed'), $event_clock),
		];
	}
	elsif ($item_key eq 'rsm.slv.dns.tcp.rtt.pfailed')
	{
		my $slv = get_history('history', get_item_id($host_id, $item_key), $event_clock);
		my $slr = get_macro('{$RSM.SLV.DNS.TCP.RTT}');

		$data = [
			'SLR_CURRENT_MONTH_DNS_TCP_RTT_availability',
			format_float($slv / $slr * 100, '%'),
			get_history('history_uint', get_item_id($host_id, 'rsm.slv.dns.tcp.rtt.performed'), $event_clock),
			get_history('history_uint', get_item_id($host_id, 'rsm.slv.dns.tcp.rtt.failed'), $event_clock),
		];
	}
	elsif ($item_key eq 'rsm.slv.rdds.rtt.pfailed')
	{
		my $slv = get_history('history', get_item_id($host_id, $item_key), $event_clock);
		my $slr = get_macro('{$RSM.SLV.RDDS.RTT}');

		$data = [
			'SLR_CURRENT_MONTH_RDDS_RTT_service_availability',
			format_float($slv / $slr * 100, '%'),
			get_history('history_uint', get_item_id($host_id, 'rsm.slv.rdds.rtt.performed'), $event_clock),
			get_history('history_uint', get_item_id($host_id, 'rsm.slv.rdds.rtt.failed'), $event_clock),
		];
	}
	else
	{
		my $row = db_select_row("select name,value_type,units from items where itemid=?", [$item_id]);

		my ($item_name, $item_value_type, $item_units) = @{$row};

		my $history_table = {
			ITEM_VALUE_TYPE_FLOAT , 'history',
			ITEM_VALUE_TYPE_UINT64, 'history_uint',
		}->{$item_value_type};

		my $value = get_history($history_table, $item_id, $event_clock);

		if ($item_value_type == ITEM_VALUE_TYPE_FLOAT)
		{
			$value = format_float($value, $item_units);
		}
		elsif ($item_value_type == ITEM_VALUE_TYPE_UINT64 && exists($value_maps{$item_key}))
		{
			my $value_map_id = get_value_map_id($value_maps{$item_key});

			my $query = "select newvalue from valuemap_mapping where valuemapid=? and value=?";
			my $params = [$value_map_id, $value];

			my $new_value = db_select_value($query, $params);

			$value = sprintf("%s (%s)", $new_value, $value);
		}

		$data = [$item_name, $value];
	}

	return $data;
}

sub get_value_map_id($)
{
	my $value_map = shift;

	my $query = 'select' .
			' valuemap.valuemapid' .
		' from' .
			' hosts' .
			' inner join valuemap on valuemap.hostid=hosts.hostid' .
		' where' .
			' hosts.host=? and' .
			' valuemap.name=?';
	my $params = ['Template Value Maps', $value_map];

	return db_select_value($query, $params);
}

sub set_max_execution_time($)
{
	my $max_execution_time = shift;

	$SIG{"ALRM"} = sub()
	{
		local *__ANON__ = 'SIGALRM-handler';

		fail("received ALARM signal");
	};

	alarm($max_execution_time);
}

sub initialize()
{
	set_max_execution_time(MAX_EXECUTION_TIME);
	initialize_log(!opt('nolog') && !opt('dry-run'));
	info("command line: %s %s", $0, join(' ', map(index($_, ' ') == -1 ? $_ : "'$_'", @ARGV)));
	db_connect();
}

sub finalize()
{
	db_disconnect();
}

sub get_history($$$)
{
	my $table   = shift;
	my $item_id = shift;
	my $clock   = shift;

	my $query = "select value from $table where itemid=? and clock=?";
	my $params = [$item_id, $clock];

	my $value;

	while (!defined($value))
	{
		my $data = db_select_col($query, $params);

		if (@{$data})
		{
			$value = $data->[0];
		}
		else
		{
			info("didn't find data in history table (table:$table, itemid:$item_id, clock:$clock), sleeping for ${\HISTORY_RETRY_DELAY} second(s)");
			select(undef, undef, undef, HISTORY_RETRY_DELAY);
		}
	}

	return $value;
}

sub get_macro($)
{
	my $macro = shift;

	return db_select_value("select value from globalmacro where macro=?", [$macro]);
}

sub get_item_id($$)
{
	my $host_id  = shift;
	my $item_key = shift;

	my $query = "select itemid from items where hostid=? and key_=?";
	my $params = [$host_id, $item_key];

	return db_select_value($query, $params);
}

sub format_float($$)
{
	my $value = shift;
	my $unit  = shift;

	$value = sprintf("%.2f", $value);
	$value =~ s/\.?0+$//;
	$value = sprintf("%s %s", $value, $unit) if ($unit);

	return $value;
}

sub notify($$$$$)
{
	my $send_to     = shift;
	my $event_value = shift;
	my $event_clock = shift;
	my $host_name   = shift;
	my $data        = shift;

	my $target = get_macro('{$RSM.MONITORING.TARGET}') eq 'registrar' ? 'registrar' : 'tld';

	my $event_value_str = {
		TRIGGER_VALUE_FALSE, 'OK',
		TRIGGER_VALUE_TRUE , 'PROBLEM',
	}->{$event_value};

	my ($sec, $min, $hour, $mday, $mon, $year) = localtime($event_clock);
	my $event_clock_str = sprintf("%.4d.%.2d.%.2d %.2d:%.2d:%.2d UTC", $year + 1900, $mon + 1, $mday, $hour, $min, $sec);

	my @args = (
		$send_to,
		join('#', ($target, $event_value_str, $host_name, @{$data})),
		$event_clock_str,
	);

	@args = map('"' . $_ . '"', @args);

	my $script_path = get_alert_scripts_path();
	my $script_file = EXTERNAL_NOTIFICATION_SCRIPT =~ s/\{AlertScriptsPath\}/$script_path/r;

	if (opt('dry-run'))
	{
		info("dry run: $script_file @args");
	}
	else
	{
		info("executing $script_file @args");

		my $out = qx($script_file @args 2>&1);

		if ($out)
		{
			info("output of $script_file:\n" . $out);
		}

		if ($? == -1)
		{
			fail("failed to execute $script_file: $!");
		}
		if ($? != 0)
		{
			fail("command $script_file exited with value " . ($? >> 8));
		}
	}
}

sub get_alert_scripts_path()
{
	my $alert_scripts_path;

	open(my $fh, '<', ZABBIX_SERVER_CONF_FILE) or fail("cannot open ${\ZABBIX_SERVER_CONF_FILE}: $!");

	while (<$fh>)
	{
		if (/^AlertScriptsPath=(.*?)\/*$/)
		{
			$alert_scripts_path = $1;
			last;
		}
	}

	close($fh) or fail("cannot close ${\ZABBIX_SERVER_CONF_FILE}: $!");

	fail("could not find AlertScriptsPath in ${\ZABBIX_SERVER_CONF_FILE}") if (!defined($alert_scripts_path));

	return $alert_scripts_path;
}

################################################################################
# output
################################################################################

use constant LOG_LEVEL_DEBUG   => 1;
use constant LOG_LEVEL_INFO    => 2;
use constant LOG_LEVEL_WARNING => 3;
use constant LOG_LEVEL_FAILURE => 4;

my $log_time_str;
my $log_time = 0;

my $log_debug_messages = 0;

$SIG{__WARN__} = sub
{
	local *__ANON__ = 'perl-warn';
	__log(LOG_LEVEL_WARNING, $_[0] =~ s/(\r?\n)+$//r);
};

$SIG{__DIE__} = sub
{
	local *__ANON__ = 'perl-die';
	__log(LOG_LEVEL_FAILURE, $_[0] =~ s/(\r?\n)+$//r);
	finalize();
	exit(255); # Perl's default exit code on die()
};

sub dbg
{
	__log(LOG_LEVEL_DEBUG, @_) if ($log_debug_messages);
}

sub info
{
	__log(LOG_LEVEL_INFO, @_);
}

sub wrn
{
	__log(LOG_LEVEL_WARNING, @_);
}

sub fail
{
	__log(LOG_LEVEL_FAILURE, @_);
	finalize();
	exit(1);
}

sub log_debug_messages(;$)
{
	my $log_debug_messages_tmp = $log_debug_messages;
	if (@_)
	{
		$log_debug_messages = shift;
	}
	return $log_debug_messages_tmp;
}

sub initialize_log($)
{
	my $use_log_file = shift;

	if ($use_log_file)
	{
		close(STDOUT) or fail("cannot close STDOUT: $!");
		close(STDERR) or fail("cannot close STDERR: $!");

		open(STDOUT, '>>', LOG_FILE) or fail("cannot open ${\LOG_FILE}: $!");
		open(STDERR, '>>', LOG_FILE) or fail("cannot open ${\LOG_FILE}: $!");
	}
}

sub __log
{
	my $message_log_level = shift;
	my $message = (@_ <= 1 ? shift // "" : sprintf(shift, @_)) . "\n";

	if ($message_log_level != LOG_LEVEL_DEBUG && $message_log_level != LOG_LEVEL_INFO)
	{
		# 'skip_frames' => 1 removes call to the __log() from the stack trace
		$message .= Devel::StackTrace->new('skip_frames' => 1, 'indent' => 1)->as_string() =~ s/^\t/> /mgr;
	}

	my $log_time_tmp = time();
	if ($log_time != $log_time_tmp)
	{
		my ($sec, $min, $hour, $mday, $mon, $year) = localtime($log_time_tmp);
		$log_time_str = sprintf("%.4d%.2d%.2d:%.2d%.2d%.2d", $year + 1900, $mon + 1, $mday, $hour, $min, $sec);
		$log_time = $log_time_tmp;
	}

	my $log_level_str;
	$log_level_str = 'DBG' if ($message_log_level == LOG_LEVEL_DEBUG);
	$log_level_str = 'INF' if ($message_log_level == LOG_LEVEL_INFO);
	$log_level_str = 'WRN' if ($message_log_level == LOG_LEVEL_WARNING);
	$log_level_str = 'ERR' if ($message_log_level == LOG_LEVEL_FAILURE);

	my $caller = (caller(2))[3] // "";
	if ($caller)
	{
		$caller = " " . ($caller =~ s/^.*:://r) . "()";
	}

	my $message_prefix = sprintf("%6d:%s [%s]%s", $$, $log_time_str, $log_level_str, $caller);

	my $output_handle;

	$output_handle = *STDOUT if ($message_log_level == LOG_LEVEL_DEBUG);
	$output_handle = *STDOUT if ($message_log_level == LOG_LEVEL_INFO);
	$output_handle = *STDERR if ($message_log_level == LOG_LEVEL_WARNING);
	$output_handle = *STDERR if ($message_log_level == LOG_LEVEL_FAILURE);

	flock($output_handle, LOCK_EX); # ignore errors, don't fail() to avoid recursion
	print($output_handle $message =~ s/^/$message_prefix /mgr);
	flock($output_handle, LOCK_UN); # ignore errors, don't fail() to avoid recursion
}

################################################################################
# command-line options
################################################################################

my %OPTS;

sub parse_opts()
{
	my $rv = GetOptionsFromArray([@ARGV], \%OPTS, "send-to=s", "event-id=i", "nolog", "dry-run", "debug", "help");

	if (!$rv || $OPTS{'help'})
	{
		usage(undef, 0);
	}
}

sub opt($)
{
	my $key = shift;

	return exists($OPTS{$key});
}

sub getopt($)
{
	my $key = shift;

	return exists($OPTS{$key}) ? $OPTS{$key} : undef;
}

sub usage($$)
{
	my $message = shift;
	my $exitval = shift;

	pod2usage(
		-message => $message,
		-exitval => $exitval,
		-verbose => 2,
		-noperldoc,
		-output  => $exitval == 0 ? \*STDOUT : \*STDERR,
	);
}

################################################################################
# database
################################################################################

my $db_handle;

sub db_connect()
{
	if (defined($db_handle))
	{
		fail("already connected to the database");
	}

	my ($db_host, $db_name, $db_user, $db_pswd, $db_tls_settings) = __get_db_config();

	my $data_source = "DBI:mysql:";

	$data_source .= "host=$db_host;";
	$data_source .= "database=$db_name;";

	$data_source .= "mysql_connect_timeout=30;";
	$data_source .= "mysql_write_timeout=30;";
	$data_source .= "mysql_read_timeout=30;";

	$data_source .= $db_tls_settings;

	my $connect_opts = {
		'PrintError'           => 0,
		'HandleError'          => \&__handle_db_error,
		'mysql_auto_reconnect' => 1,
	};

	$db_handle = DBI->connect($data_source, $db_user, $db_pswd, $connect_opts);

	# verify that established database connection uses TLS if there was any hint that it is required in the config
	unless ($db_tls_settings eq "mysql_ssl=0;")
	{
		my $rows = db_select("show status like 'Ssl_cipher'");

		fail("established connection is not secure") if ($rows->[0][1] eq "");

		dbg("established connection uses \"" . $rows->[0][1] . "\" cipher");
	}
	else
	{
		dbg("established connection is unencrypted");
	}

	# improve performance of selects, see
	# http://search.cpan.org/~capttofu/DBD-mysql-4.028/lib/DBD/mysql.pm
	# for details
	$db_handle->{'mysql_use_result'} = 1;
}

sub db_disconnect()
{
	if (!defined($db_handle))
	{
		return;
	}

	my @active_handles = ();

	foreach my $handle (@{$db_handle->{'ChildHandles'}})
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
			wrn(__generate_db_error($handle, "active statement"));
			$handle->finish();
		}
	}

	$db_handle->disconnect() or wrn($db_handle->errstr);
	undef($db_handle);
}

sub db_select($;$)
{
	my $query = shift;
	my $bind_values = shift; # optional; reference to an array

	my $sth = $db_handle->prepare($query);

	if (defined($bind_values))
	{
		dbg("[$query] " . join(',', @{$bind_values})) if (log_debug_messages());

		$sth->execute(@{$bind_values});
	}
	else
	{
		dbg("[$query]") if (log_debug_messages());

		$sth->execute();
	}

	my $rows = $sth->fetchall_arrayref();

	if (log_debug_messages())
	{
		if (scalar(@{$rows}) == 1)
		{
			dbg(join(',', map($_ // 'UNDEF', @{$rows->[0]})));
		}
		else
		{
			dbg(scalar(@{$rows}) . " rows");
		}
	}

	return $rows;
}

sub db_select_col($;$)
{
	my $query = shift;
	my $bind_values = shift; # optional; reference to an array

	my $rows = db_select($query, $bind_values);

	fail("query returned more than one column") if (scalar(@{$rows}) > 0 && scalar(@{$rows->[0]}) > 1);

	return [map($_->[0], @{$rows})];
}

sub db_select_row($;$)
{
	my $query = shift;
	my $bind_values = shift; # optional; reference to an array

	my $rows = db_select($query, $bind_values);

	fail("query did not return any row") if (scalar(@{$rows}) == 0);
	fail("query returned more than one row") if (scalar(@{$rows}) > 1);

	return $rows->[0];
}

sub db_select_value($;$)
{
	my $query = shift;
	my $bind_values = shift; # optional; reference to an array

	my $row = db_select_row($query, $bind_values);

	fail("query returned more than one value") if (scalar(@{$row}) > 1);

	return $row->[0];
}

sub db_exec($;$)
{
	my $query = shift;
	my $bind_values = shift; # optional; reference to an array

	my $sth = $db_handle->prepare($query);

	if (defined($bind_values))
	{
		dbg("[$query] " . join(',', @{$bind_values})) if (log_debug_messages());

		$sth->execute(@{$bind_values});
	}
	else
	{
		dbg("[$query]") if (log_debug_messages());

		$sth->execute();
	}

	return $sth->{mysql_insertid};
}

sub __get_db_config()
{
	my $db_host = 'localhost';
	my $db_name = undef;
	my $db_user = '';
	my $db_pswd = '';

	my $db_tls_key_file  = undef;
	my $db_tls_cert_file = undef;
	my $db_tls_ca_file   = undef;
	my $db_tls_cipher    = undef;

	open(my $fh, '<', ZABBIX_SERVER_CONF_FILE) or fail("cannot open ${\ZABBIX_SERVER_CONF_FILE}: $!");

	while (<$fh>)
	{
		if (/^(DB.*)=(.*)$/)
		{
			my $key   = $1;
			my $value = $2;

			$db_host = $value if ($key eq 'DBHost');
			$db_name = $value if ($key eq 'DBName');
			$db_user = $value if ($key eq 'DBUser');
			$db_pswd = $value if ($key eq 'DBPassword');

			$db_tls_key_file  = $value if ($key eq 'DBTLSKeyFile');
			$db_tls_cert_file = $value if ($key eq 'DBTLSCertFile');
			$db_tls_ca_file   = $value if ($key eq 'DBTLSCAFile');
			$db_tls_cipher    = $value if ($key eq 'DBTLSCipher13');
		}
	}

	close($fh) or fail("cannot close ${\ZABBIX_SERVER_CONF_FILE}: $!");

	my $db_tls_settings = "";

	$db_tls_settings .= "mysql_ssl_client_key="  . $db_tls_key_file  . ";" if (defined($db_tls_key_file));
	$db_tls_settings .= "mysql_ssl_client_cert=" . $db_tls_cert_file . ";" if (defined($db_tls_cert_file));
	$db_tls_settings .= "mysql_ssl_ca_file="     . $db_tls_ca_file   . ";" if (defined($db_tls_ca_file));
	$db_tls_settings .= "mysql_ssl_cipher="      . $db_tls_cipher    . ";" if (defined($db_tls_cipher));

	if ($db_tls_settings)
	{
		$db_tls_settings = "mysql_ssl=1;" . $db_tls_settings;
	}
	else
	{
		$db_tls_settings = "mysql_ssl=0;";
	}

	return $db_host, $db_name, $db_user, $db_pswd, $db_tls_settings;
}

sub __handle_db_error($$$)
{
	my $message = shift;
	my $handle  = shift;

	fail(__generate_db_error($handle, undef));
}

sub __generate_db_error($$)
{
	my $handle  = shift;
	my $message = shift // $handle->errstr;

	my @message_parts = ();

	push(@message_parts, "database error:");

	push(@message_parts, $message);

	if (defined($handle->{'Statement'}))
	{
		push(@message_parts, "(query: [$handle->{'Statement'}])");
	}

	if (defined($handle->{'ParamValues'}) && %{$handle->{'ParamValues'}})
	{
		my $params = join(',', values(%{$handle->{'ParamValues'}}));
		push(@message_parts, "(params: [$params])");
	}

	if (defined($handle->{'ParamArrays'}) && %{$handle->{'ParamArrays'}})
	{
		my $params = join(',', values(%{$handle->{'ParamArrays'}}));
		push(@message_parts, "(params 2: [$params])");
	}

	return join(' ', @message_parts);
}

################################################################################
# end of script
################################################################################

main();

__END__

=head1 NAME

tlds-notification.pl - calls script.py.

=head1 SYNOPSIS

tlds-notification.pl --send-to <receiver> --event-id <event-id> [--nolog] [--dry-run] [--debug] [--help]

=head1 OPTIONS

=over 8

=item B<--send-to> string

Specify receiver

=item B<--event-id> int

Specify event ID.

=item B<--nolog>

Print output to stdout and stderr instead of a log file.

=item B<--dry-run>

Print data to the screen, do not change anything in the system.

=item B<--debug>

Run the script in debug mode. This means printing more information.

=item B<--help>

Print a brief help message and exit.

=back

=cut
