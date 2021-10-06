package Framework;

use strict;
use warnings;

use Exporter qw(import);
our @EXPORT = qw(
	initialize
	finalize
	pushd
	popd
	execute
	read_file
	write_file
	get_dir_tree
	get_source_directory
	get_build_directory
	get_logs_directory
	get_server_pid_file
	get_libfaketime_so_path
	zbx_build
	zbx_drop_db
	zbx_create_db
	zbx_update_config
	zbx_get_server_pid
	zbx_start_server
	zbx_stop_server
	rsm_update_config
	tar_unpack
	tar_compare
	str_starts_with
	to_unixtimestamp
	format_table
);

use Archive::Tar;
use Cwd;
use Data::Dumper;
use Date::Parse;
use File::Spec;
use List::Util qw(max);
use Text::Diff;

use Database;
use Options;
use Output;

my @prev_dir = undef;

sub initialize()
{
	initialize_log(!opt('nolog'), 1, \&finalize);
	info("command line: %s %s", $0, join(' ', map(index($_, ' ') == -1 ? $_ : "'$_'", @ARGV)));
}

sub finalize()
{
	db_disconnect();
}

sub pushd($)
{
	my $dir = shift;

	push(@prev_dir, cwd());

	info("chdir to $dir");
	chdir($dir);
}

sub popd()
{
	if (@prev_dir)
	{
		my $dir = pop(@prev_dir);

		info("chdir to $dir");
		chdir($dir);
	}
	else
	{
		fail("directory stack is empty");
	}
}

sub execute
{
	info("executing: " . join(" ", map('"' . $_ . '"', @_)));
	if (system(@_) != 0)
	{
		if ($? == -1) {
			fail("failed to execute: $!");
		}
		elsif ($? & 127) {
			fail("child died with signal %d, %s coredump", ($? & 127),  ($? & 128) ? "with" : "without");
		}
		else {
			fail("child exited with value %d", $? >> 8);
		}
	}
}

sub read_file($)
{
	my $filename = shift;

	local $/ = undef;

	my $fh;

	open($fh, '<', $filename) or fail("cannot open file '$filename': $!");
	my $text = <$fh>;
	close($fh) or fail("cannot close file '$filename': $!");

	return $text;
}

sub write_file($$)
{
	my $filename = shift;
	my $text     = shift;

	my $fh;

	open($fh, '>', $filename) or fail("cannot open file '$filename': $!");
	print({$fh} $text)        or fail("cannot write to '$filename': $!");
	close($fh)                or fail("cannot close file '$filename': $!");
}

sub get_dir_tree($)
{
	my $root = shift;

	$root = File::Spec->canonpath($root);

	my @dirs = ($root);
	my @names;

	my $dh;
	my $fh;

	while (@dirs)
	{
		my $dir = shift(@dirs);
		my $file;

		opendir($dh, $dir)       or fail("cannot open dir '$dir': $!");
		my @files = readdir($dh) or fail("cannot read dir '$dir': $!");
		closedir($dh)            or fail("cannot close dir '$dir': $!");

		foreach my $file (@files)
		{
			next if ($file eq '.');
			next if ($file eq '..');

			$file = File::Spec->catfile($dir, $file);

			if (-d $file)
			{
				push(@dirs, $file);
				push(@names, "$file/");
			}
			else
			{
				push(@names, $file);
			}
		}
	}

	@names = sort(map($_ =~ s!^$root/!!r, @names));

	return @names;
}

sub get_source_directory()
{
	return $ENV{"WORKSPACE"} . "/source";
}

sub get_build_directory()
{
	return $ENV{"WORKSPACE"} . "/build";
}

sub get_logs_directory()
{
	return $ENV{"WORKSPACE"} . "/logs";
}

sub get_server_pid_file()
{
	return $ENV{"WORKSPACE"} . "/zabbix_server.pid";
}

sub get_server_socket_dir()
{
	return $ENV{"WORKSPACE"};
}

sub get_libfaketime_so_path()
{
	my $path = undef;

	$path = qx(find /usr/lib /usr/lib64 -name "libfaketime.so.*");
	$path = (split(/\n/, $path))[0];

	if (!defined($path))
	{
		fail("could not find libfaketime library");
	}

	return $path;
}

sub zbx_build($$$)
{
	my $enable_server = shift;
	my $enable_proxy  = shift;
	my $enable_agent  = shift;

	my @configure_args = ();

	if ($enable_server || $enable_proxy || $enable_agent)
	{
		push(@configure_args, '--prefix=' . get_build_directory());
		push(@configure_args, '--enable-dependency-tracking');
		push(@configure_args, '--with-libevent');
		push(@configure_args, '--with-libpcre');
		push(@configure_args, '--with-libcurl');
		push(@configure_args, '--with-openssl');
		push(@configure_args, '--with-mysql');
		push(@configure_args, '--enable-ipv6');
		push(@configure_args, '--enable-server') if ($enable_server);
		push(@configure_args, '--enable-proxy')  if ($enable_proxy);
		push(@configure_args, '--enable-agent')  if ($enable_agent);
	}

	pushd(get_source_directory());

	execute('./bootstrap.sh');
	{
		local $ENV{'CFLAGS'} = '-O2 -g -Wall -Wextra -Wdeclaration-after-statement -Wpointer-arith -Wno-maybe-uninitialized -Wformat -Wmissing-prototypes';

		info("CFLAGS: $ENV{'CFLAGS'}");
		execute('./configure', @configure_args);
	}

	execute("make dbschema");

	if ($enable_server || $enable_proxy || $enable_agent)
	{
		execute("make");
		execute("make install");
	}

	popd();

	if ($enable_proxy)
	{
		zbx_update_config(
			get_source_directory() . "/conf/zabbix_proxy.conf",
			get_build_directory() . "/etc/zabbix_proxy.conf.example",
			{
				"ProxyMode"            => "1",
				"Server"               => "127.0.0.1",
				"Hostname"             => "<Hostname>",
				"ListenPort"           => "<ListenPort>",
				"LogFile"              => get_logs_directory() . "/zabbix_proxy.log",
				"LogFileSize"          => "0",
				"PidFile"              => $ENV{"WORKSPACE"} . "/zabbix_proxy.pid",
				"DBHost"               => $ENV{"ZBX_PROXY_DB_HOST"}     // "",
				"DBName"               => $ENV{"ZBX_PROXY_DB_NAME"}     // "",
				"DBUser"               => $ENV{"ZBX_PROXY_DB_USER"}     // "",
				"DBPassword"           => $ENV{"ZBX_PROXY_DB_PASSWORD"} // "",
				"CacheSize"            => "1G",
			}
		);
	}

	rsm_update_config(
		get_source_directory() . "/opt/zabbix/scripts/rsm.conf.example",
		get_source_directory() . "/opt/zabbix/scripts/rsm.conf",
		{
			"server_1.za_url"                     => $ENV{"ZBX_FRONTEND_URL"},
			"server_1.za_user"                    => $ENV{"ZBX_FRONTEND_USER"},
			"server_1.za_password"                => $ENV{"ZBX_FRONTEND_PASSWORD"},
			"server_1.db_host"                    => $ENV{"ZBX_SERVER_DB_HOST"},
			"server_1.db_name"                    => $ENV{"ZBX_SERVER_DB_NAME"},
			"server_1.db_user"                    => $ENV{"ZBX_SERVER_DB_USER"},
			"server_1.db_password"                => $ENV{"ZBX_SERVER_DB_PASSWORD"},
			"slv.zserver"                         => "127.0.0.1",
			"slv.zport"                           => "10051",
			"sla_api.incident_measurements_limit" => 0,
			"sla_api.allow_missing_measurements"  => 0,
			"sla_api.initial_measurements_limit"  => 0,
			"sla_api.output_dir"                  => "/opt/zabbix/sla",
			"data_export.output_dir"              => "/opt/zabbix/export",
		}
	);
}

sub zbx_drop_db()
{
	my $db_name = get_db_name();
	info("dropping database '%s'", $db_name);
	db_exec("drop database if exists $db_name");
}

sub zbx_create_db()
{
	my $sql_dir = get_source_directory() . "/database/mysql";

	my $db_host = get_db_host();
	my $db_name = get_db_name();
	my $db_user = get_db_user();
	my $db_pswd = get_db_pswd();

	info("creating database '%s'", $db_name);
	db_exec("create database $db_name character set utf8 collate utf8_bin");
	db_exec("use $db_name");

	my $db_args = "--host='$db_host' --port=3306 --user='$db_user' '$db_name'";

	my $dump_file = "$sql_dir/dump.sql";

	local $ENV{'MYSQL_PWD'} = $db_pswd;

	if (! -f $dump_file)
	{
		foreach my $sql_file ('schema.sql', 'images.sql', 'data.sql')
		{
			info("importing $sql_file into database '$db_name'");
			execute("mysql $db_args < '$sql_dir/$sql_file'");
		}

		info("dumping database");
		execute("mysqldump $db_args > '$dump_file'");
	}
	else
	{
		info("importing dump.sql into database '$db_name'");
		execute("mysql $db_args < '$dump_file'");
	}
}

sub zbx_update_config($$$)
{
	my $template_filename = shift;
	my $config_filename   = shift;
	my $changes           = shift;

	my $config = read_file($template_filename);

	foreach my $key (keys(%{$changes}))
	{
		my $value = $changes->{$key};

		if ($config =~ /^$key=/m)
		{
			$config =~ s!^$key=.*$!$key=$value!m;
		}
		elsif ($config =~ /^# $key=/m)
		{
			$config =~ s!^(#\s*$key=.*)$!$1\n\n$key=$value!m;
		}
		else
		{
			fail("Key '$key' not found in config file '$template_filename'");
		}
	}

	write_file($config_filename, $config);
}

sub zbx_get_server_pid()
{
	my $pid = undef;
	my $pid_file = get_server_pid_file();

	if (-f $pid_file)
	{
		$pid = read_file($pid_file);

		if ($pid !~ /^\d+$/)
		{
			fail("invalid format of server pid: '%s'", $pid);
		}

		if (!kill(0, $pid))
		{
			wrn("Zabbix server PID '$pid' found, but process does not accept signals");
			$pid = undef;
		}
	}

	return $pid;
}

sub zbx_start_server(;$$$)
{
	my $datetime         = shift;
	my $logfile_suffix   = shift // '';
	my $config_overrides = shift // {};

	my $executable     = get_build_directory() . "/sbin/zabbix_server";
	my $log_file       = get_logs_directory() . "/zabbix_server" . $logfile_suffix . ".log";
	my $libfaketime_so = get_libfaketime_so_path();

	info("updating zabbix_server.conf");

	zbx_update_config(
		get_source_directory() . "/conf/zabbix_server.conf",
		get_build_directory() . "/etc/zabbix_server.conf",
		{
			(
				"ListenPort"              => "10051",
				"LogFile"                 => $log_file,
				"SocketDir"               => get_server_socket_dir(),
				"LogFileSize"             => "0",
				"PidFile"                 => get_server_pid_file(),
				"DBHost"                  => $ENV{"ZBX_SERVER_DB_HOST"},
				"DBName"                  => $ENV{"ZBX_SERVER_DB_NAME"},
				"DBUser"                  => $ENV{"ZBX_SERVER_DB_USER"},
				"DBPassword"              => $ENV{"ZBX_SERVER_DB_PASSWORD"},
				"CacheSize"               => "1G",
				"TrapperTimeout"          => "3",
				"StartTrappers"           => "1",
				"StartEscalators"         => "1",
				"StartDBSyncers"          => "1",
				"StartPreprocessors"      => "1",
				"StartTimers"             => "1",
				"StartAlerters"           => "1",
				"StartLLDProcessors"      => "1",
				"StartPollers"            => "0",
				"StartPollersUnreachable" => "0",
				"StartDiscoverers"        => "0",
				"StartHTTPPollers"        => "0",
				"StartProxyPollers"       => "0",
				"HousekeepingFrequency"   => "0",
				"ProxyDataFrequency"      => "3600",
				"AlertScriptsPath"        => "/opt/zabbix/alertscripts",
				"ExternalScripts"         => "/opt/zabbix/externalscripts",
				"ProxyConfigFrequency"    => "60",
			),
			%{$config_overrides}
		}
	);

	info("starting server");

	dbg("checking if server is running");

	my $pid = zbx_get_server_pid();

	if (defined($pid))
	{
		fail("server is already running, pid: '%d'", $pid);
	}

	dbg("starting the server");
	if ($datetime)
	{
		execute("LD_PRELOAD='$libfaketime_so' FAKETIME='\@$datetime' sh -c '$executable'");
	}
	else
	{
		execute($executable);
	}

	dbg("waiting until pid file and log file are created");
	sleep(1);

	dbg("getting pid of currently running server");
	$pid = zbx_get_server_pid();

	if (!defined($pid))
	{
		fail("zabbix server failed to start, check '%s'", get_logs_directory() . "/zabbix_server.log");
	}

	dbg("waiting until server reports 'main process started' in the log file");

	my $fh;
	my $started;

	open($fh, '<', $log_file) or fail("cannot open file '$log_file': $!");

	TRY:
	for (my $i = 0; $i < 30; $i++)
	{
		while (<$fh>)
		{
			$started = $_ =~ /^ *$pid:\d+:\d+.\d+ server #0 started \[main process\]$/m;

			if ($started)
			{
				last TRY;
			}
		}

		sleep(1);

		seek($fh, 0, 1);
	}

	close($fh) or fail("cannot close file '$log_file': $!");

	if (!$started)
	{
		fail("failed to start the server");
	}
}

sub zbx_stop_server()
{
	info("stopping server");

	dbg("checking if server is running");

	my $pid = zbx_get_server_pid();

	if (!defined($pid))
	{
		fail("server is not running");
	}

	dbg("stopping the server");

	if (!kill("SIGINT", $pid))
	{
		fail("failed to send SIGINT to server");
	}

	dbg("waiting until server is stopped");

	my $stopped;

	for (my $i = 0; $i < 30; $i++)
	{
		$stopped = !kill(0, $pid);

		if ($stopped)
		{
			last;
		}

		sleep(1);
	}

	if (!$stopped)
	{
		fail("failed to stop the server");
	}
}

sub rsm_update_config($$$)
{
	my $template_filename = shift;
	my $config_filename   = shift;
	my $changes           = shift;

	my @config_text = split(/\n/, read_file($template_filename));
	my %config_refs = ();

	my $section = '';

	foreach my $line (@config_text)
	{
		if ($line =~ /\[(.*)\]/)
		{
			$section = $1;
		}
		if ($line =~ /^;?(\w+)\s*=\s*(\S.*)?$/)
		{
			my $key = $1;

			$config_refs{$section}{$key} = \$line;
		}
	}

	foreach my $key (keys(%{$changes}))
	{
		my ($section, $property) = split(/\./, $key);
		my $value = $changes->{$key};

		${$config_refs{$section}{$property}} = "$property = $value";
	}

	write_file($config_filename, join("\n", @config_text) . "\n");
}

sub tar_unpack($$)
{
	my $tar_file  = shift;
	my $directory = shift;

	my $tar = Archive::Tar->new($tar_file);

	$tar->setcwd($directory);
	$tar->extract();
}

sub tar_compare($$)
{
	my $tar_file  = shift;
	my $directory = shift;

	my $tar = Archive::Tar->new($tar_file);

	my %names_tar = map { $_ => undef } $tar->list_files();
	my %names_fs  = map { $_ => undef } get_dir_tree($directory);
	my %names_all = (%names_tar, %names_fs);

	my @unexpected_dirs;
	my @unexpected_files;
	my @missing_dirs;
	my @missing_files;
	my @different_contents;

	foreach my $name (sort(keys(%names_all)))
	{
		if (!exists($names_tar{$name}))
		{
			if ($name =~ /\/$/)
			{
				push(@unexpected_dirs, $name);
			}
			else
			{
				push(@unexpected_files, $name);
			}
		}
	}
	foreach my $name (sort(keys(%names_all)))
	{
		if (!exists($names_fs{$name}))
		{
			if ($name =~ /\/$/)
			{
				push(@missing_dirs, $name);
			}
			else
			{
				push(@missing_files, $name);
			}
		}
	}
	foreach my $name (sort(keys(%names_all)))
	{
		next if (!exists($names_tar{$name}));
		next if (!exists($names_fs{$name}));

		next if (-d "$directory/$name");

		if (read_file("$directory/$name") ne $tar->get_content($name))
		{
			# show what exactly differs
			my $content_tar = $tar->get_content($name);
			my $content_dir = read_file("$directory/$name");

			print("--- '$name' expected VS generated:\n");
			print(diff(\$content_tar, \$content_dir), "\n");

			push(@different_contents, $name);
		}
	}

	if (@unexpected_dirs || @unexpected_files || @missing_dirs || @missing_files || @different_contents)
	{
		if (@unexpected_dirs)
		{
			info("unexpected directories:");
			map(info("* $_"), @unexpected_dirs);
		}
		if (@unexpected_files)
		{
			info("unexpected files:");
			map(info("* $_"), @unexpected_files);
		}
		if (@missing_dirs)
		{
			info("missing directories:");
			map(info("* $_"), @missing_dirs);
		}
		if (@missing_files)
		{
			info("missing files:");
			map(info("* $_"), @missing_files);
		}
		if (@different_contents)
		{
			info("different files:");
			map(info("* $_"), @different_contents);
		}

		return 0;
	}
	else
	{
		return 1;
	}
}

sub str_starts_with($$)
{
	my $string = shift;
	my $prefix = shift;

	if (rindex($string, $prefix, 0) == 0)
	{
		return 1
	}
	else
	{
		return 0;
	}
}

sub to_unixtimestamp($)
{
	return str2time(shift);
}

sub format_table($$)
{
	my $data = shift;
	my $cols = shift;

	my $table = '';

	# get column widths, based on column titles
	my %widths = map { $_ => length($_) } @{$cols};

	# get column widths, based on data
	foreach my $row (@{$data})
	{
		foreach my $col (@{$cols})
		{
			$widths{$col} = max($widths{$col}, length($row->{$col}));
		}
	}

	# create horizontal line
	my $line = '+';
	foreach my $col (@{$cols})
	{
		$line .= '-' x ($widths{$col} + 2) . '+';
	}

	# line before column titles
	$table .= $line . "\n";

	# column titles
	$table .= '|';
	foreach my $col (@{$cols})
	{
		$table .= sprintf(" %-${widths{$col}}s |", $col);
	}
	$table .= "\n";

	# line between column titles and data
	$table .= $line . "\n";

	# data
	foreach my $row (@{$data})
	{
		$table .= '|';
		foreach my $col (@{$cols})
		{
			$table .= sprintf(" %-${widths{$col}}s |", $row->{$col});
		}
		$table .= "\n";
	}

	# line after data
	$table .= $line;

	return $table;
}

1;
