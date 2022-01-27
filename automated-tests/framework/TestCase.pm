package TestCase;

use strict;
use warnings;

use Exporter qw(import);
our @EXPORT = qw(
	run_test_case
	run_test_case_command
);

use Data::Dumper;
use DateTime;
use Date::Parse;
use File::Copy;
use File::Spec;
use File::Basename;
use File::Path qw(make_path);
use Text::CSV_XS qw(csv);

use Configuration;
use Database;
use Framework;
use HttpClient;
use Options;
use Output;
use ProvisioningApi;

my %command_handlers = (
	# command => [$handler, $has_arguments, $fork],
	'test-case'               => [\&__cmd_test_case              , 1, 0], # name
	'set-variable'            => [\&__cmd_set_variable           , 1, 0], # name,value
	'enable-debug-mode'       => [\&__cmd_enable_debug_mode      , 0, 0], # (void)
	'disable-debug-mode'      => [\&__cmd_disable_debug_mode     , 0, 0], # (void)
	'empty-directory'         => [\&__cmd_empty_directory        , 1, 1], # directory
	'extract-files'           => [\&__cmd_extract_files          , 1, 1], # directory,archive
	'compare-files'           => [\&__cmd_compare_files          , 1, 1], # directory,archive
	'prepare-server-database' => [\&__cmd_prepare_server_database, 0, 1], # (void)
	'execute-sql-query'       => [\&__cmd_execute_sql_query      , 1, 1], # query,param,param,param,...
	'compare-sql-query'       => [\&__cmd_compare_sql_query      , 1, 1], # query,value,value,value,...
	'fill-history'            => [\&__cmd_fill_history           , 1, 1], # host,item,delay,clock,value,value,value,...
	'compare-history'         => [\&__cmd_compare_history        , 1, 1], # host,item,delay,clock,value,value,value,...
	'set-lastvalue'           => [\&__cmd_set_lastvalue          , 1, 1], # host,item,clock,value
	'fix-lastvalue-tables'    => [\&__cmd_fix_lastvalue_tables   , 0, 1], # (void)
	'set-global-macro'        => [\&__cmd_set_global_macro       , 1, 1], # macro,value
	'set-host-macro'          => [\&__cmd_set_host_macro         , 1, 1], # host,macro,value
	'execute'                 => [\&__cmd_execute                , 1, 1], # datetime,command or datetime,command,arg,arg,arg,...
	'start-server'            => [\&__cmd_start_server           , 1, 1], # datetime,key=value,key=value,...
	'stop-server'             => [\&__cmd_stop_server            , 0, 1], # (void)
	'update-rsm-conf'         => [\&__cmd_update_rsm_conf        , 1, 1], # section,property,value
	'create-probe'            => [\&__cmd_create_probe           , 1, 1], # probe,ip,port,ipv4,ipv6,rdds,rdap
	'create-tld'              => [\&__cmd_create_tld             , 1, 1], # tld,dns_test_prefix,type,dnssec,dns_udp,dns_tcp,ns_servers_v4,ns_servers_v6,rdds43_servers,rdds80_servers,rdap_base_url,rdap_test_domain,rdds_test_prefix
	'disable-tld'             => [\&__cmd_disable_tld            , 1, 1], # tld
	'create-incident'         => [\&__cmd_create_incident        , 1, 1], # rsmhost,description,from,till,false_positive
	'check-incident'          => [\&__cmd_check_incident         , 1, 1], # rsmhost,description,from,till
	'check-event-count'       => [\&__cmd_check_event_count      , 1, 1], # rsmhost,description,count
	'provisioning-api'        => [\&__cmd_provisioning_api       , 1, 1], # endpoint,method,expected_code,user,request,response
	'start-tool'              => [\&__cmd_start_tool             , 1, 1], # tool_name,pid-file,input-file
	'stop-tool'               => [\&__cmd_stop_tool              , 1, 1], # tool_name,pid-file
);

my $test_case_filename;
my $test_case_name;
my $test_case_variables;

################################################################################
# main functions
################################################################################

sub run_test_case($)
{
	# set global variable to make it available in command handlers
	$test_case_filename = shift;

	# skip test cases that have "." in front of the filename, but read the name of the test case to include in the report
	my $skip_test_case = str_starts_with([File::Spec->splitpath($test_case_filename)]->[2], ".");

	# reset the name of the test case
	$test_case_name = undef;

	# reset the variables of the test case
	$test_case_variables = {};

	my $test_case_uses_db = 0;

	my $line_num = 0;
	my $command = undef;
	my $succeeded = 1;
	my $failure_message = undef;

	my $test_case = read_file($test_case_filename);
	my @test_case = split(/\n/, $test_case);

	info("-" x 80);

	if (!$skip_test_case)
	{
		my $source_dir = get_config('paths', 'source_dir');

		copy(
			$source_dir . "/opt/zabbix/scripts/rsm.conf.default",
			$source_dir . "/opt/zabbix/scripts/rsm.conf",
		) or fail("cannot copy rsm.conf file: %s", $!);;

		db_connect();
	}

	for ($line_num = 0; $line_num < scalar(@test_case); $line_num++)
	{
		my $line = $test_case[$line_num];

		next if ($line eq "");   # skip empty lines
		next if ($line =~ /^#/); # skip comments

		dbg("[%s:%d] %s", $test_case_filename, $line_num + 1, $line);

		if ($line =~ /^\[(.+)\]$/)
		{
			if (!defined($test_case_name) && $1 ne 'test-case')
			{
				fail("test case file must start with [test-case], specifying test case's name");
			}

			$command = $1;

			info("handling command '$command'");

			if ($command eq "prepare-server-database")
			{
				$test_case_uses_db = 1;
			}

			if (!exists($command_handlers{$command}))
			{
				fail("unhandled command: '$command'");
			}

			if ($command_handlers{$command}[1] == 0)
			{
				$succeeded = run_test_case_command($command, undef);

				undef($command);
			}
		}
		else
		{
			if (!defined($command))
			{
				fail("command is not specified for arguments: '$line'");
			}

			if ($command_handlers{$command}[1] == 1)
			{
				$succeeded = run_test_case_command($command, $line);
			}
		}

		if (!$succeeded)
		{
			last;
		}

		if ($skip_test_case && defined($test_case_name))
		{
			last;
		}
	}

	if (!$skip_test_case)
	{
		db_disconnect();
	}

	my $zabbix_server_pid = zbx_get_server_pid();

	if (defined($zabbix_server_pid))
	{
		if ($succeeded)
		{
			wrn("test case should have stopped the server");
		}
		zbx_stop_server();
	}

	if ($skip_test_case)
	{
		info("test case skipped");
	}
	elsif ($succeeded)
	{
		info("test case succeeded");
	}
	else
	{
		$failure_message  = "test case failed\n";
		$failure_message .= "\n";
		$failure_message .= "test case: " . (defined($test_case_name) ? "'$test_case_name'" : "undef") . "\n";
		$failure_message .= "filename: '$test_case_filename'\n";
		$failure_message .= "command: " . (defined($command) ? "'$command'" : "undef") . "\n";
		$failure_message .= "line number: " . ($line_num + 1) . "\n";

		if ($test_case_uses_db == 1)
		{
			my $db_host = get_config("zabbix_server", "db_host");
			my $db_name = get_config("zabbix_server", "db_name");
			my $db_user = get_config("zabbix_server", "db_username");
			my $db_pswd = get_config("zabbix_server", "db_password");

			my $db_dumps_dir = get_config('paths', 'db_dumps_dir');

			make_path($db_dumps_dir);

			my $db_dump_file = $db_dumps_dir . '/' . basename($test_case_filename =~ s/\.txt^/.sql/r);

			local $ENV{'MYSQL_PWD'} = $db_pswd;

			execute("mysqldump --host='$db_host' --port=3306 --user='$db_user' '$db_name' > '$db_dump_file'");

			$failure_message .= "db dump created: $db_dump_file";
		}

		$failure_message .= "\n";
		$failure_message .= $test_case[$line_num];

		info($failure_message);
	}

	info("-" x 80);

	# unset filename so that command handlers don't use invalid filename, when called from the framework (not from test case)
	undef($test_case_filename);

	return ($test_case_name // '(undef)', $skip_test_case, $failure_message);
}

sub run_test_case_command($$)
{
	my $command = shift;
	my $args    = shift;

	my $ret = 0;

	my $func = $command_handlers{$command}[0];
	my $fork = $command_handlers{$command}[2];

	if ($fork)
	{
		my $pid = fork();

		if (!defined($pid))
		{
			fail("failed to fork: $!");
		}

		if ($pid == 0)
		{
			if (defined($args))
			{
				$func->($args);
			}
			else
			{
				$func->();
			}

			exit;
		}

		waitpid($pid, 0);

		if ($? == 0)
		{
			$ret = 1;
		}
		else
		{
			if ($? == -1) {
				wrn("failed to execute: $!");
			}
			elsif ($? & 127) {
				wrn("child died with signal %d, %s coredump", ($? & 127),  ($? & 128) ? "with" : "without");
			}
			else {
				wrn("child exited with value %d", $? >> 8);
			}
		}
	}
	else
	{
		if (defined($args))
		{
			$func->($args);
		}
		else
		{
			$func->();
		}

		$ret = 1;
	}

	return $ret;
}

################################################################################
# command handlers
################################################################################

sub __cmd_test_case($)
{
	my $args = shift;

	# [test-case]
	# name

	($test_case_name) = __unpack($args);

	info("test case - '$test_case_name'");
}

sub __cmd_set_variable($)
{
	my $args = shift;

	# [set-variable]
	# name,value

	my ($name, $value) = __unpack($args);

	info("storing variable (name: '$name', value: '$value')");

	$test_case_variables->{$name} = $value;
}

sub __cmd_enable_debug_mode()
{
	# [enable-debug-mode]
	# (void)

	info("enabling debug mode");

	log_debug_messages(1);
	setopt("debug", 1);
}

sub __cmd_disable_debug_mode()
{
	# [disable-debug-mode]
	# (void)

	info("disabling debug mode");

	log_debug_messages(0);
	unsetopt("debug");
}

sub __cmd_empty_directory($)
{
	my $args = shift;

	# [empty-directory]
	# directory

	my ($directory) = __unpack($args);

	info("preparing empty directory '$directory'");

	if (-d $directory)
	{
		execute("rm", "-rf", $directory);
	}

	mkdir($directory) or fail("cannot create dir '%s': %s", $directory, $!);
}

sub __cmd_extract_files($)
{
	my $args = shift;

	# [extract-files]
	# directory,archive

	my ($directory, $archive) = __unpack($args);

	info("extracting archive '$archive'");

	if (!File::Spec->file_name_is_absolute($archive))
	{
		my (undef, $test_case_dir, undef) = File::Spec->splitpath($test_case_filename);

		$archive = File::Spec->catfile($test_case_dir, $archive);
	}

	tar_unpack($archive, $directory);
}

sub __cmd_compare_files($)
{
	my $args = shift;

	# [compare-files]
	# directory,archive

	my ($directory, $archive) = __unpack($args);

	info("comparing directory '%s' with archive '%s'", $directory, basename($archive));

	if (!File::Spec->file_name_is_absolute($archive))
	{
		my (undef, $test_case_dir, undef) = File::Spec->splitpath($test_case_filename);

		$archive = File::Spec->catfile($test_case_dir, $archive);
	}

	if (!tar_compare($archive, $directory))
	{
		fail("contents of '$directory' differ from contents of '$archive'");
	}
}

sub __cmd_prepare_server_database()
{
	# [prepare-server-database]
	# (void)

	zbx_drop_db();
	zbx_create_db();
}

sub __cmd_execute_sql_query($)
{
	my $args = shift;

	# [execute-sql-query]
	# query,param,param,param,...

	my ($sql, @params) = __unpack($args);

	info("executing SQL [$sql] " . join(',', @params));

	db_exec($sql, \@params);
}

sub __cmd_compare_sql_query($)
{
	my $args = shift;

	# [compare-sql-query]
	# query,value,value,value,...

	my ($sql, @values) = __unpack($args);

	info("comparing SQL [$sql]");

	my $row = db_select_row($sql);

	if (scalar(@{$row}) != scalar(@values))
	{
		my $message = "query returned different number of values than expected\n";
		$message .= "expected:\n";
		$message .= Dumper(\@values);
		$message .= "returned:\n";
		$message .= Dumper($row);
		fail("%s", $message);
	}

	for (my $i = 0; $i < scalar(@values); $i++)
	{
		if ($row->[$i] ne $values[$i])
		{
			my $message = "query returned unexpected value #$i, expected '$values[$i]', got '$row->[$i]'\n";
			$message .= "expected:\n";
			$message .= Dumper(\@values);
			$message .= "returned:\n";
			$message .= Dumper($row);
			fail("%s", $message);
		}
	}
}

sub __cmd_fill_history($)
{
	my $args = shift;

	# [fill-history]
	# host,item,delay,clock,value,value,value,...

	my ($host, $item, $delay, $clock, @values) = __unpack($args);

	info("filling history (host: '%s', item: '%s')", $host, $item);

	if ($clock =~ /^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}$/)
	{
		$clock = str2time($clock);
	}

	my $itemid = __get_itemid($host, $item);
	my $table  = __get_history_table($itemid);

	my $sql = "insert into $table set itemid=?,clock=?,value=?";

	db_begin();

	foreach my $value (@values)
	{
		# skip cycle if no value specified
		if ($value ne "")
		{
			db_exec($sql, [$itemid, $clock, $value]);
		}

		$clock += $delay;
	}

	db_commit();
}

sub __cmd_compare_history($)
{
	my $args = shift;

	# [compare-history]
	# host,item,delay,clock,value,value,value,...

	my ($host, $item, $delay, $first_clock, @values) = __unpack($args);

	info("comparing history (host: '%s', item: '%s')", $host, $item);

	if ($first_clock =~ /^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}$/)
	{
		$first_clock = str2time($first_clock);
	}

	my %data = ();

	for (my $i = 0; $i < scalar(@values); $i++)
	{
		my $clock = $first_clock + $delay * $i;

		$data{$clock} = {
			'expected' => $values[$i],
		};
	}

	my $itemid = __get_itemid($host, $item);

	my $history_table = __get_history_table($itemid);

	my $sql = "select clock,value from $history_table where itemid=? and clock between ? and ?";
	my $params = [$itemid, $first_clock, $first_clock + $delay * scalar(@values) - 1];

	my $rows = db_select($sql, $params);

	foreach my $row (@{$rows})
	{
		my ($clock, $value) = @{$row};

		push(@{$data{$clock}{'actual'}}, $value);
	}

	foreach my $clock (sort { $a <=> $b } keys(%data))
	{
		my $error;

		if (!exists($data{$clock}{'expected'}) && !exists($data{$clock}{'actual'}))
		{
			$error = 'internal error, this clock should not be in the list';
		}
		elsif ((!exists($data{$clock}{'expected'}) || $data{$clock}{'expected'} eq '') && exists($data{$clock}{'actual'}))
		{
			$error = 'unexpected data in history table';
		}
		elsif (exists($data{$clock}{'expected'}) && !exists($data{$clock}{'actual'}))
		{
			if ($data{$clock}{'expected'} ne '')
			{
				$error = 'missing data in history table';
			}
		}
		elsif (scalar(@{$data{$clock}{'actual'}}) > 1)
		{
			$error = 'more than one entry in history table';
		}
		elsif ($history_table ne 'history')
		{
			if ($data{$clock}{'actual'}[0] ne $data{$clock}{'expected'})
			{
				$error = 'invalid value in history table';
			}
		}
		elsif ($history_table eq 'history')
		{
			my ($int, $fract) = split(/\./, $data{$clock}{'expected'});

			my $format = '%.' . (defined($fract) ? length($fract) : 6) . 'f';

			my $actual   = sprintf($format, $data{$clock}{'actual'}[0]);
			my $expected = sprintf($format, $data{$clock}{'expected'});

			if ($actual ne $expected)
			{
				$error = 'invalid value in history table';
			}
		}

		my $dt = DateTime->from_epoch('epoch' => $clock);

		$data{$clock} = {
			'datetime'  => $dt->ymd . ' ' . $dt->hms,
			'timestamp' => $clock,
			'expected'  => $data{$clock}{'expected'} // '',
			'actual'    => join(', ', @{$data{$clock}{'actual'} // ['']}),
			'error'     => $error // '',
		};
	}

	if (grep($_->{'error'}, values(%data)))
	{
		my @columns = (
			'datetime',
			'timestamp',
			'expected',
			'actual',
			'error',
		);

		my @table_data = @data{sort { $a <=> $b } keys(%data)};

		my $table = format_table(\@table_data, \@columns);

		info($table);

		fail("item history is invalid (host: '%s', item: '%s', itemid: '%d', table: '%s')",
				$host, $item, $itemid, $history_table);
	}
}

sub __cmd_set_lastvalue($)
{
	my $args = shift;

	# [set-lastvalue]
	# host,item,clock,value

	my ($host, $item, $clock, $value) = __unpack($args);

	my $itemid = __get_itemid($host, $item);

	my $history_table = __get_history_table($itemid);

	my $lastvalue_table;
	$lastvalue_table = "lastvalue"     if ($history_table eq "history");
	$lastvalue_table = "lastvalue"     if ($history_table eq "history_uint");
	$lastvalue_table = "lastvalue_str" if ($history_table eq "history_log");
	$lastvalue_table = "lastvalue_str" if ($history_table eq "history_str");
	$lastvalue_table = "lastvalue_str" if ($history_table eq "history_text");

	my $sql = "insert into $lastvalue_table set itemid=?,clock=?,value=?" .
			" on duplicate key update value=values(value),clock=values(clock)";
	my $params = [$itemid, $clock, $value];

	db_exec($sql, $params);
}

sub __cmd_fix_lastvalue_tables()
{
	# [fix-lastvalue-tables]
	# (void)

	my @table_map = (
		["history"     , "lastvalue"],
		["history_uint", "lastvalue"],
		["history_log" , "lastvalue_str"],
		["history_str" , "lastvalue_str"],
		["history_text", "lastvalue_str"],
	);

	db_exec("delete from lastvalue");
	db_exec("delete from lastvalue_str");

	foreach my $tables (@table_map)
	{
		my ($history_table, $lastvalue_table) = @{$tables};

		my $sql = "insert into $lastvalue_table (itemid,clock,value)" .
			" select" .
				" $history_table.itemid," .
				" $history_table.clock," .
				" $history_table.value" .
			" from" .
				" (select itemid,max(clock) as clock from $history_table group by itemid) as history_max_clock" .
				" inner join $history_table on" .
					" $history_table.itemid=history_max_clock.itemid and" .
					" $history_table.clock=history_max_clock.clock";

		db_exec($sql);
	}
}

sub __cmd_set_global_macro($)
{
	my $args = shift;

	# [set-global-macro]
	# macro,value

	my ($macro, $value) = __unpack($args);

	db_exec("update globalmacro set value=? where macro=?", [$value, $macro]);
}

sub __cmd_set_host_macro($)
{
	my $args = shift;

	# [set-host-macro]
	# host,macro,value

	my ($host, $macro, $value) = __unpack($args);

	db_exec("update hostmacro set value=? where hostid=? and macro=?", [$value, __get_hostid($host), $macro]);
}

sub __cmd_execute($)
{
	my $args = shift;

	# [execute]
	# datetime,command

	my ($datetime, @command) = __unpack($args);

	if ($datetime eq "")
	{
		if (scalar(@command) == 1)
		{
			execute($command[0]);
		}
		else
		{
			execute(@command);
		}
	}
	else
	{
		if ($datetime !~ /^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}$/)
		{
			fail("invalid format for datetime, expected 'yyyy-mm-dd hh:mm:ss': '$datetime'");
		}

		local $ENV{'TZ'} = 'UTC';
		local $ENV{'FAKETIME_DONT_RESET'} = '1';

		if (scalar(@command) == 1)
		{
			execute("faketime -f '\@$datetime' $command[0]");
		}
		else
		{
			execute("faketime", "-f", "@" . $datetime, @command);
		}
	}
}

sub __cmd_start_server($)
{
	my $args = shift;

	# [start-server]
	# datetime,key=value,key=value,...

	my ($datetime, @kv_list) = __unpack($args);

	my $logfile_suffix = '-' . $test_case_filename =~ s!^.*/([^/]*)\.txt$!$1!r;

	my %config_overrides = map { $_ =~ /^(.*?)=(.*)$/; $1 => $2 } @kv_list;

	zbx_start_server($datetime, $logfile_suffix, \%config_overrides);
}

sub __cmd_stop_server()
{
	# [stop-server]
	# (void)

	zbx_stop_server();
}

sub __cmd_update_rsm_conf($)
{
	my $args = shift;

	# [update-rsm-conf]
	# section,property,value

	my ($section, $property, $value) = __unpack($args);

	my $source_dir = get_config('paths', 'source_dir');
	my $config_file = $source_dir . "/opt/zabbix/scripts/rsm.conf";

	rsm_update_config($config_file, $config_file, {"$section.$property" => $value});
}

sub __cmd_create_probe($)
{
	my $args = shift;

	# [create-probe]
	# probe,ip,port,ipv4,ipv6,rdds,rdap

	my ($probe, $ip, $port, $ipv4, $ipv6, $rdds, $rdap) = __unpack($args);

	db_begin();
	create_probe(1, $probe, $ip, $port, $ipv4, $ipv6, $rdds, $rdap);
	db_commit();

	my $build_dir = get_config('paths', 'build_dir');

	if (-f $build_dir . "/etc/zabbix_proxy.conf.example")
	{
		zbx_update_config(
			$build_dir . "/etc/zabbix_proxy.conf.example",
			$build_dir . "/etc/zabbix_proxy_$probe.conf",
			{
				"Hostname"   => $probe,
				"ListenPort" => $port,
			}
		);
	}
}

sub __cmd_create_tld($)
{
	my $args = shift;

	# [create-tld]
	# tld,dns_test_prefix,type,dnssec,dns_udp,dns_tcp,ns_servers_v4,ns_servers_v6,rdds43_servers,rdds80_servers,rdap_base_url,rdap_test_domain,rdds_test_prefix

	my ($tld, $dns_test_prefix, $type, $dnssec, $dns_udp, $dns_tcp, $ns_servers_v4, $ns_servers_v6, $rdds43_servers,
			$rdds80_servers, $rdap_base_url, $rdap_test_domain, $rdds_test_prefix) = __unpack($args);

	db_begin();

	create_tld(
		1,
		$tld,
		$dns_test_prefix,
		$type,
		$dnssec,
		$dns_udp,
		$dns_tcp,
		$ns_servers_v4,
		$ns_servers_v6,
		$rdds43_servers,
		$rdds80_servers,
		$rdap_base_url,
		$rdap_test_domain,
		$rdds_test_prefix
	);

	my $probes = db_select_col(
			"select" .
				" hosts.host" .
			" from" .
				" hosts" .
				" inner join hosts_groups on hosts_groups.hostid=hosts.hostid" .
				" inner join hstgrp on hstgrp.groupid=hosts_groups.groupid" .
			" where" .
				" hstgrp.name='Probes'" .
			" order by" .
				" hosts.host");

	foreach my $probe (@{$probes})
	{
		create_tld_probe(
			$tld,
			$probe,
			$type,
			$rdds43_servers || $rdds80_servers,
			$rdap_base_url || $rdap_test_domain
		);
	}

	foreach my $probe (@{$probes})
	{
		create_tld_probe_nsip(
			$tld,
			$probe,
			$ns_servers_v4,
			$ns_servers_v6
		);
	}

	db_commit();
}

sub __cmd_disable_tld($)
{
	my $args = shift;

	# [disable-tld]
	# tld

	my ($tld) = __unpack($args);

	info("disabling tld '$tld'");

	my $sql = "select" .
			" hosts.hostid," .
			"hosts.host," .
			"hstgrp.name" .
		" from" .
			" hosts" .
			" inner join hosts_templates on hosts.hostid=hosts_templates.hostid" .
			" inner join hosts as templates on templates.hostid=hosts_templates.templateid" .
			" inner join hosts_groups on hosts_groups.hostid=hosts.hostid" .
			" inner join hstgrp on hstgrp.groupid=hosts_groups.groupid" .
		" where" .
			" hstgrp.name in ('TLDs', 'TLD Probe results') and" .
			" templates.host=?" .
		" order by" .
			" field(hstgrp.name, 'TLDs', 'TLD Probe results')," .
			" hosts.host";
	my $params = ["Template Rsmhost Config $tld"];

	my $rows = db_select($sql, $params);

	if (!@{$rows})
	{
		fail("tld '%s' not found", $tld);
	}

	foreach my $row (@{$rows})
	{
		my ($hostid, $host, $group) = @{$row};

		info("disabling host '%s' from group '%s'", $host, $group);

		db_exec("update hosts set status=? where hostid=?", [1, $hostid]);
	}
}

sub __cmd_create_incident($)
{
	my $args = shift;

	# [create-incident]
	# rsmhost,description,from,till,false_positive

	my ($rsmhost, $description, $from, $till, $false_positive) = __unpack($args);

	info("creating incident '$description' for rsmhost '$rsmhost'");

	my $eventid_problem;
	my $eventid_recovery;

	my $sql = "insert into events set" .
			" eventid=?," .
			"source=0," .           # EVENT_SOURCE_TRIGGERS
			"object=0," .           # EVENT_OBJECT_TRIGGER
			"objectid=?," .
			"clock=?," .
			"value=?," .            # 0 => TRIGGER_VALUE_OK , 1 => TRIGGER_VALUE_PROBLEM
			"acknowledged=0," .     # EVENT_NOT_ACKNOWLEDGED
			"ns=0," .
			"name=?," .
			"severity=0," .
			"false_positive=?";

	my $triggerid = __get_triggerid($rsmhost, $description);

	if ($from ne "")
	{
		my $clock = str2time($from);

		$eventid_problem  = db_select_value('select coalesce(max(eventid), 0) + 1 from events');

		my $params = [
			$eventid_problem,
			$triggerid,
			$clock,
			1,                      # TRIGGER_VALUE_PROBLEM
			$description,
			$false_positive,
		];
		db_exec($sql, $params);
	}

	if ($till ne "")
	{
		my $clock = str2time($till);

		$eventid_recovery = db_select_value('select coalesce(max(eventid), 0) + 1 from events');

		my $params = [
			$eventid_recovery,
			$triggerid,
			$clock,
			0,                      # TRIGGER_VALUE_OK
			$description,
			0,
		];
		db_exec($sql, $params);
	}

	if ($eventid_problem && $eventid_recovery)
	{
		my $sql = "insert into event_recovery set" .
				" eventid=?," .
				"r_eventid=?," .
				"c_eventid=null," .
				"correlationid=null," .
				"userid=null";
		my $params = [$eventid_problem, $eventid_recovery];
		db_exec($sql, $params);
	}
}

sub __cmd_check_incident($)
{
	my $args = shift;

	# [check-incident]
	# rsmhost,description,from,till

	my ($rsmhost, $description, $from, $till) = __unpack($args);

	info("checking incident '$description' for rsmhost '$rsmhost'");

	my $triggerid = __get_triggerid($rsmhost, $description);

	my $sql = "select 1 from events where" .
			" source=0 and" .       # EVENT_SOURCE_TRIGGERS
			" object=0 and" .       # EVENT_OBJECT_TRIGGER
			" objectid=? and" .
			" clock=? and" .
			" value=?";             # 0 => TRIGGER_VALUE_OK , 1 => TRIGGER_VALUE_PROBLEM

	my $error = undef;

	if ($from ne "")
	{
		my $rows = db_select($sql, [$triggerid, str2time($from), 1]);
		if (scalar(@{$rows}) == 0)
		{
			$error = "beginning of the incident not found at '$from'";
		}
		elsif (scalar(@{$rows}) > 1)
		{
			$error = "found more than one beginning of the incident at '$from'";
		}
	}

	if ($till ne "")
	{
		my $rows = db_select($sql, [$triggerid, str2time($till), 0]);
		if (scalar(@{$rows}) == 0)
		{
			$error = "end of the incident not found at '$till'";
		}
		elsif (scalar(@{$rows}) > 1)
		{
			$error = "found more than one end of the incident at '$till'";
		}
	}

	if ($error)
	{
		my $sql = "select source,object,clock,value from events where objectid=?";

		my $rows = db_select($sql, [$triggerid]);

		my @table_data = ();

		foreach my $row (@{$rows})
		{
			my ($source, $object, $clock, $value) = @{$row};

			my $dt = DateTime->from_epoch('epoch' => $clock);

			push(@table_data, {
				"source"    => $source == 0 ? "EVENT_SOURCE_TRIGGERS" : $source,
				"object"    => $object == 0 ? "EVENT_OBJECT_TRIGGER" : $object,
				"clock"     => $clock,
				"clock_str" => $dt->ymd . ' ' . $dt->hms,
				"value"     => {0 => "TRIGGER_VALUE_OK", 1 => "TRIGGER_VALUE_PROBLEM"}->{$value} // $value,
			});
		}

		my $table = format_table(\@table_data, ['source', 'object', 'clock', 'clock_str', 'value']);
		info($table);

		fail($error);
	}
}

sub __cmd_check_event_count($)
{
	my $args = shift;

	# [check-event-count]
	# rsmhost,description,count

	my ($rsmhost, $description, $expected_count) = __unpack($args);

	info("checking event '$description' count for rsmhost '$rsmhost'");

	my $sql = "select count(*) from events where" .
			" source=0 and" .       # EVENT_SOURCE_TRIGGERS
			" object=0 and" .       # EVENT_OBJECT_TRIGGER
			" objectid=?";          # 0 => TRIGGER_VALUE_OK , 1 => TRIGGER_VALUE_PROBLEM

	my $count = db_select_value($sql, [__get_triggerid($rsmhost, $description)]);

	if ($count != $expected_count)
	{
		fail("expected '$expected_count' events, found '$count'");
	}
}

sub __cmd_provisioning_api($)
{
	my $args = shift;

	# [provisioning-api]
	# endpoint,method,expected_code,user,request,response

	my ($endpoint, $method, $expected_code, $user, $request, $response) = __unpack($args);

	my $users = {
		'' => undef,
		'nonexistent' => {
			'username' => 'nonexistent',
			'password' => 'nonexistent',
		},
		'invalid_password' => {
			'username' => get_config('provisioning-api', 'username_readonly'),
			'password' => get_config('provisioning-api', 'password_readonly') . '_invalid',
		},
		'readonly' => {
			'username' => get_config('provisioning-api', 'username_readonly'),
			'password' => get_config('provisioning-api', 'password_readonly'),
		},
		'readwrite' => {
			'username' => get_config('provisioning-api', 'username_readwrite'),
			'password' => get_config('provisioning-api', 'password_readwrite'),
		},
	};

	if (!exists($users->{$user}))
	{
		fail("unsupported user '$user', supported users: '', 'nonexistent', 'invalid_password', 'readonly', 'readwrite'");
	}

	if ($request ne '' && !File::Spec->file_name_is_absolute($request))
	{
		info("request payload file: '%s'", $request);

		my (undef, $test_case_dir, undef) = File::Spec->splitpath($test_case_filename);

		$request = File::Spec->catfile($test_case_dir, $request);
	}
	if ($response ne '' && !File::Spec->file_name_is_absolute($response))
	{
		info("response payload file: '%s'", $request);

		my (undef, $test_case_dir, undef) = File::Spec->splitpath($test_case_filename);

		$response = File::Spec->catfile($test_case_dir, $response);
	}

	my $payload = $request eq '' ? undef : read_file($request);

	my $url = rtrim(get_config('provisioning-api', 'url'), '/') . '/' . ltrim($endpoint, '/');

	my ($status_code, $content_type, $response_body) = http_request($url, $method, $users->{$user}, $payload);

	if ($status_code != $expected_code)
	{
		fail("unexpected status code '$status_code', expected '$expected_code'");
	}

	if (!defined($content_type) || $content_type ne 'application/json')
	{
		$content_type //= 'undef';
		fail("unexpected content type '$content_type', expected 'application/json'");
	}

	if ($response ne '')
	{
		...;
	}
}

sub __cmd_start_tool($)
{
	my $args = shift;

	# [start-tool]
	# tool_name,pid_file,input_file

	my ($tool_name, $pid_file, $input_file) = __unpack($args);

	start_tool($tool_name, $pid_file, $input_file);
}

sub __cmd_stop_tool($)
{
	my $args = shift;

	# [stop-tool]
	# tool_name,pid_file

	my ($tool_name, $pid_file) = __unpack($args);

	stop_tool($tool_name, $pid_file);
}

################################################################################
# helper functions
################################################################################

sub __unpack($)
{
	my $args = shift;

	my @values = @{csv('allow_whitespace' => 1, 'in' => \$args)->[0]};

	foreach (@values)
	{
		$_ =~ s!(\$\{(.*?)\})! $test_case_variables->{$2} // $1 !ge;
	}

	return @values;
}

sub __expect($$$)
{
	my $value          = shift;
	my $expected_value = shift;
	my $message        = shift;

	if ($value ne $expected_value)
	{
		fail($message, $value, $expected_value);
	}
}

sub __get_hostid($)
{
	my $host = shift;

	my $sql = "select hostid from hosts where host=?";
	my $params = [$host];

	return db_select_value($sql, $params);
}

sub __get_itemid($$)
{
	my $host = shift;
	my $key  = shift;

	my $sql = "select itemid from items left join hosts on hosts.hostid=items.hostid where hosts.host=? and items.key_=?";
	my $params = [$host, $key];

	return db_select_value($sql, $params);
}

sub __get_triggerid($$)
{
	my $host        = shift;
	my $description = shift;

	my $sql = "select" .
			" triggers.triggerid" .
		" from" .
			" items" .
			" inner join hosts on hosts.hostid=items.hostid" .
			" inner join functions on functions.itemid=items.itemid" .
			" inner join triggers on" .
				" triggers.triggerid=functions.triggerid and" .
				" triggers.expression like concat('%{', functions.functionid, '}%')" .
		" where" .
			" hosts.host=? and" .
			" triggers.description=?";
	my $params = [$host, $description];

	return db_select_value($sql, $params);
}

sub __get_history_table($)
{
	my $itemid = shift;

	my $value_type = db_select_value("select value_type from items where itemid=?", [$itemid]);

	return "history"      if ($value_type == 0); # ITEM_VALUE_TYPE_FLOAT
	return "history_str"  if ($value_type == 1); # ITEM_VALUE_TYPE_STR
	return "history_log"  if ($value_type == 2); # ITEM_VALUE_TYPE_LOG
	return "history_uint" if ($value_type == 3); # ITEM_VALUE_TYPE_UINT64
	return "history_text" if ($value_type == 4); # ITEM_VALUE_TYPE_TEXT

	fail("unhandled value type: '$value_type'");
}

################################################################################
# end of module
################################################################################

1;
