#!/usr/bin/env perl

use strict;
use warnings;

use Config::Tiny;
use DBD::SQLite::Constants qw/:file_open/;
use DBI;
use Data::Dumper;
use Devel::StackTrace;
use Fcntl qw(:flock SEEK_END);
use File::Path qw(rmtree);
use Getopt::Long qw(GetOptionsFromArray);
use IO::Compress::Gzip qw(gzip $GzipError);
use IO::Select;
use JSON::XS;
use List::Util qw(shuffle);
use Net::DNS::Async;
use Parallel::ForkManager;
use Pod::Usage;
use Socket;
use Time::HiRes;

use constant MONITORING_TARGET_REGISTRY  => 'registry';
use constant MONITORING_TARGET_REGISTRAR => 'registrar';

my $json_xs;
my $config;
my $probe_name;

################################################################################
# main
################################################################################

sub main()
{
	my $time_start;
	my $time_end;

	# initialize script

	initialize();

	# get data from local proxy database

	info("reading data from proxy database...");
	$time_start = Time::HiRes::time();

	db_connect();

	my $monitoring_target = get_monitoring_target();
	my $proxy_name = get_proxy_name();
	my %proxy_config = get_proxy_config($proxy_name);
	my %rsmhosts_config = get_rsmhosts_config($monitoring_target);

	db_disconnect();

	set_probe_name($proxy_name);
	initialize_work_dir();

	my $ipv4 = $proxy_config{'ipv4'};
	my $ipv6 = $proxy_config{'ipv6'};

	$time_end = Time::HiRes::time();
	info("finished reading data from proxy database in %.3fs", $time_end - $time_start);

	# resolve RDAP, RDDS hostnames

	info("resolving hostnames...");
	$time_start = Time::HiRes::time();

	my %hosts_to_resolve = get_rdap_rdds_hosts(\%proxy_config, \%rsmhosts_config);
	my %resolved_hosts = resolve_hosts(\%hosts_to_resolve, $ipv4, $ipv6);

	$time_end = Time::HiRes::time();
	info("finished resolving hostnames in %.3fs", $time_end - $time_start);

	# write configuration to JSON files

	write_gz_file(get_json_file_path('proxy', 0), format_json(\%proxy_config));
	write_gz_file(get_json_file_path('rsmhosts', 0), format_json(\%rsmhosts_config));
	write_gz_file(get_json_file_path('resolved_hosts', 0), format_json(\%resolved_hosts));

	# preform tracerouting

	info("doing traceroutes...");
	$time_start = Time::HiRes::time();

	my %ip_rsmhost_mapping = create_ip_rsmhost_mapping(\%rsmhosts_config, \%hosts_to_resolve, \%resolved_hosts, $ipv4, $ipv6);
	my @ip_list = create_ip_list(\%rsmhosts_config, \%resolved_hosts, $ipv4, $ipv6);

	traceroute(\@ip_list, \%ip_rsmhost_mapping);

	$time_end = Time::HiRes::time();
	info("finished doing traceroutes in %.3fs", $time_end - $time_start);

	# create tarball

	info("creating tarball...");
	$time_start = Time::HiRes::time();

	my $cycle_work_dir  = get_cycle_work_dir();                            # directory with measurement files
	my $output_file_tmp = get_base_work_dir() . '/' . get_tar_file_name(); # where to create an archive
	my $output_file     = get_output_dir() . '/' . get_tar_file_name();    # where to move an archive after creating it
	my $verbose         = opt('debug') ? '-v' : '';

	if (-f $output_file_tmp)
	{
		fail("file already exists: $output_file_tmp");
	}
	if (-f $output_file)
	{
		fail("file already exists: $output_file");
	}

	if (!execute("ls -1 '$cycle_work_dir' | tar $verbose -C '$cycle_work_dir' --remove-files -cf '$output_file_tmp' -T -"))
	{
		fail("failed to compress output files");
	}

	if (!rename($output_file_tmp, $output_file))
	{
		fail("failed to move archive: $!");
	}

	if (!rmtree($cycle_work_dir))
	{
		fail("failed to remove work dir: $!");
	}

	$time_end = Time::HiRes::time();
	info("finished creating tarball in %.3fs", $time_end - $time_start);
}

sub initialize()
{
	parse_opts();

	if (opt('debug'))
	{
		log_debug_messages(1);
	}

	usage("--config is missing", 1) if (!opt('config'));

	initialize_config();
	validate_config();
	initialize_log(!opt('nolog') && !opt('dry-run'));

	info("command line: %s %s", $0, join(' ', map(index($_, ' ') == -1 ? $_ : "'$_'", @ARGV)));

	set_max_execution_time(get_config('time_limits', 'script'), 1, "script running for too long, terminating...");
}

sub initialize_config()
{
	$config = Config::Tiny->read(getopt('config'));

	if (!defined($config))
	{
		fail(Config::Tiny->errstr());
	}
}

sub get_config($$)
{
	my $section  = shift;
	my $property = shift;

	if (!defined($config))
	{
		fail('config has not been initialized');
	}
	if (!exists($config->{$section}))
	{
		fail("section '$section' does not exist in the config file");
	}
	if (!exists($config->{$section}{$property}))
	{
		fail("property '$section.$property' does not exist in the config file");
	}

	my $value = $config->{$section}{$property};

	if ($value eq '')
	{
		fail("property '$section.$property' is empty in the config file");
	}

	return $value;
}

sub validate_config()
{
	# use dbg() not only for printing config, but also for checking if all required config options are present,
	# get_config() fails if config option does not exist or is empty

	dbg("config:");
	dbg();
	dbg("[paths]");
	dbg("log_file        = %s", get_config('paths', 'log_file'));
	dbg("proxy_config    = %s", get_config('paths', 'proxy_config'));
	dbg("work_dir        = %s", get_config('paths', 'work_dir'));
	dbg("output_dir      = %s", get_config('paths', 'output_dir'));
	dbg();
	dbg("[time_limits]");
	dbg("script          = %s", get_config('time_limits', 'script'));
	dbg("mtr_term        = %s", get_config('time_limits', 'mtr_term'));
	dbg("mtr_kill        = %s", get_config('time_limits', 'mtr_kill'));
	dbg("output_handling = %s", get_config('time_limits', 'output_handling'));
	dbg();
	dbg("[resolver]");
	dbg("queue_size      = %s", get_config('resolver', 'queue_size'));
	dbg("timeout         = %s", get_config('resolver', 'timeout'));
	dbg("retries         = %s", get_config('resolver', 'retries'));
	dbg();
	dbg("[mtr]");
	dbg("options         = %s", get_config('mtr', 'options'));
	dbg();
}

sub initialize_work_dir()
{
	my $base_work_dir = get_base_work_dir();
	my $cycle_work_dir = get_cycle_work_dir();

	fail("work dir '$base_work_dir' does not exist")              if (! -e $base_work_dir);
	fail("work dir '$base_work_dir' is not a directory")          if (! -d $base_work_dir);
	fail("work dir '$base_work_dir' is not a writable directory") if (! -w $base_work_dir);

	mkdir($cycle_work_dir) or fail("failed to create work dir '$cycle_work_dir': $!");
}

sub set_max_execution_time($$$)
{
	my $max_execution_time = shift;
	my $terminate_group    = shift;
	my $message            = shift;

	$SIG{'ALRM'} = sub()
	{
		local *__ANON__ = 'SIGALRM-handler';

		if ($terminate_group)
		{
			$SIG{'TERM'} = 'IGNORE';	# ignore signal we will send to ourselves in the next step
			kill('TERM', 0);		# send signal to the entire process group
			$SIG{'TERM'} = 'DEFAULT';	# restore default signal handler
		}

		fail($message);
	};

	alarm($max_execution_time);
}

sub format_json($)
{
	my $data = shift;

	if (!defined($json_xs))
	{
		$json_xs = JSON::XS->new();
		$json_xs->utf8();
		$json_xs->canonical();
		$json_xs->pretty();
	}

	return $json_xs->encode($data);
}

sub write_gz_file($$)
{
	my $filename = shift;
	my $contents = shift;

	my $gz = IO::Compress::Gzip->new($filename) or fail("IO::Compress::Gzip failed: $GzipError");
	$gz->write($contents);
	$gz->close();
}

sub get_rdap_rdds_hosts($$)
{
	my %proxy_config    = %{+shift};
	my %rsmhosts_config = %{+shift};

	my %hosts = ();

	foreach my $rsmhost (keys(%rsmhosts_config))
	{
		if ($proxy_config{'rdds'} && $rsmhosts_config{$rsmhost}{'rdds43'})
		{
			$hosts{$rsmhosts_config{$rsmhost}{'rdds43_server'}}{$rsmhost} = undef;
		}
		if ($proxy_config{'rdds'} && $rsmhosts_config{$rsmhost}{'rdds80'})
		{
			$hosts{$rsmhosts_config{$rsmhost}{'rdds80_server'}}{$rsmhost} = undef;
		}
		if ($proxy_config{'rdap'} && $rsmhosts_config{$rsmhost}{'rdap'})
		{
			$hosts{$rsmhosts_config{$rsmhost}{'rdap_server'}}{$rsmhost} = undef;
		}
	}

	foreach my $host (keys(%hosts))
	{
		$hosts{$host} = [keys(%{$hosts{$host}})];
	}

	return %hosts;
}

sub create_ip_rsmhost_mapping($$$$$)
{
	my %rsmhosts_config  = %{+shift};
	my %hosts_to_resolve = %{+shift};
	my %resolved_hosts   = %{+shift};
	my $ipv4             = shift;
	my $ipv6             = shift;

	my %mapping = ();

	foreach my $rsmhost (keys(%rsmhosts_config))
	{
		foreach my $nsip (@{$rsmhosts_config{$rsmhost}{'nsip_list'}})
		{
			my $ip = $nsip->[1];
			my $is_ipv4 = ($ip =~ m/^\d+\.\d+\.\d+\.\d+$/);

			if ($ipv4 && $is_ipv4)
			{
				$mapping{$ip}{$rsmhost} = undef;
			}
			if ($ipv6 && !$is_ipv4)
			{
				$mapping{$ip}{$rsmhost} = undef;
			}
		}
	}

	foreach my $host (keys(%resolved_hosts))
	{
		my @ip_list = ();

		push(@ip_list, @{$resolved_hosts{$host}{'ipv4'} // []}) if ($ipv4);
		push(@ip_list, @{$resolved_hosts{$host}{'ipv6'} // []}) if ($ipv6);

		foreach my $ip (@ip_list)
		{
			foreach my $rsmhost (@{$hosts_to_resolve{$host}})
			{
				$mapping{$ip}{$rsmhost} = undef;
			}
		}
	}

	foreach my $ip (keys(%mapping))
	{
		$mapping{$ip} = [keys(%{$mapping{$ip}})];
	}

	return %mapping;
}

sub create_ip_list($$$$)
{
	my %rsmhosts_config = %{+shift};
	my %resolved_hosts  = %{+shift};
	my $ipv4            = shift;
	my $ipv6            = shift;

	my %ip_list = ();

	foreach (values(%rsmhosts_config))
	{
		foreach my $nsip (@{$_->{'nsip_list'}})
		{
			my $ip = $nsip->[1];
			my $is_ipv4 = ($ip =~ m/^\d+\.\d+\.\d+\.\d+$/);

			if ($ipv4 && $is_ipv4)
			{
				$ip_list{$ip} = undef;
			}
			if ($ipv6 && !$is_ipv4)
			{
				$ip_list{$ip} = undef;
			}
		}
	}

	foreach (values(%resolved_hosts))
	{
		if ($ipv4 && exists($_->{'ipv4'}))
		{
			foreach my $ip (@{$_->{'ipv4'}})
			{
				$ip_list{$ip} = undef;
			}
		}
		if ($ipv6 && exists($_->{'ipv6'}))
		{
			foreach my $ip (@{$_->{'ipv6'}})
			{
				$ip_list{$ip} = undef;
			}
		}
	}

	return keys(%ip_list);
}

sub set_probe_name($)
{
	$probe_name = shift;
}

sub get_probe_name()
{
	if (!defined($probe_name))
	{
		fail("get_probe_name() called before set_probe_name()");
	}
	return $probe_name;
}

sub get_base_work_dir()
{
	return get_config('paths', 'work_dir');
}

sub get_cycle_work_dir()
{
	# sec, min, hour, mday, mon, year, wday, yday, isdst
	my ($sec, $min, $hour, $mday, $mon, $year) = localtime(get_cycle_timestamp());

	return sprintf("%s/%04d%02d%02d-%02d%02d%02d-%s", get_base_work_dir(), $year + 1900, $mon + 1, $mday, $hour, $min, $sec, get_probe_name());
}

sub get_output_dir()
{
	return get_config('paths', 'output_dir');
}

sub get_json_file_path($$)
{
	my $name                      = shift; # IP address or name of config/mapping/something
	my $include_current_timestamp = shift;

	my $path = get_cycle_work_dir() . '/';
	$path .= get_probe_name();
	$path .= '-' . $name;
	$path .= '-' . get_cycle_timestamp();
	$path .= '-' . get_current_timestamp() if ($include_current_timestamp);
	$path .= '.json';
	$path .= '.gz';

	return $path;
}

sub get_tar_file_name()
{
	my ($sec, $min, $hour, $mday, $mon, $year) = localtime(get_cycle_timestamp());
	return sprintf("%04d%02d%02d-%02d%02d%02d-%s.tar", $year + 1900, $mon + 1, $mday, $hour, $min, $sec, get_probe_name());
}

sub get_cycle_timestamp()
{
	return $^T - ($^T % 60);
}

sub get_current_timestamp()
{
	return time();
}

################################################################################
# getting data from database
################################################################################

sub get_monitoring_target()
{
	my $query = 'select value from globalmacro where macro=?';
	my $params = ['{$RSM.MONITORING.TARGET}'];

	my $target = db_select_value($query, $params);

	if ($target ne MONITORING_TARGET_REGISTRY && $target ne MONITORING_TARGET_REGISTRAR)
	{
		fail("invalid monitoring target: '$target'");
	}

	return $target;
}

sub get_proxy_name()
{
	my $query = "select" .
			" hosts.host" .
		" from" .
			" hosts" .
			" inner join hosts_templates on hosts_templates.hostid=hosts.hostid" .
			" inner join hosts as templates on templates.hostid=hosts_templates.templateid" .
		" where" .
			" templates.host=?";
	my $params = ['Template Probe Status'];

	return db_select_value($query, $params);
}

sub get_proxy_config($)
{
	my $proxy_name = shift;

	my @macros = (
		'{$RSM.IP4.ENABLED}',
		'{$RSM.IP6.ENABLED}',
		'{$RSM.RDAP.ENABLED}',
		'{$RSM.RDDS.ENABLED}',
	);

	my $macros_placeholder = join(",", ("?") x scalar(@macros));
	my $query = "select" .
			" hostmacro.macro," .
			"hostmacro.value" .
		" from" .
			" hosts" .
			" inner join hostmacro on hostmacro.hostid=hosts.hostid" .
		" where" .
			" hosts.host=? and" .
			" hostmacro.macro in ($macros_placeholder)";
	my $params = ['Template Probe Config ' . $proxy_name, @macros];

	my $rows = db_select($query, $params);

	my %macros = map { $_->[0] => $_->[1] } @{$rows};

	foreach my $macro (@macros)
	{
		fail("missing probe config macro '$macro'") if (!exists($macros{$macro}));
	}

	return (
		'ipv4' => $macros{'{$RSM.IP4.ENABLED}'}  eq '1' ? 1 : 0,
		'ipv6' => $macros{'{$RSM.IP6.ENABLED}'}  eq '1' ? 1 : 0,
		'rdap' => $macros{'{$RSM.RDAP.ENABLED}'} eq '1' ? 1 : 0,
		'rdds' => $macros{'{$RSM.RDDS.ENABLED}'} eq '1' ? 1 : 0,
	);
}

sub get_rsmhosts_config($)
{
	my $monitoring_target = shift;

	# list of host macros that need to be selected for each rsmhost

	my @macros = (
		'{$RSM.TLD}',
		'{$RSM.TLD.DNS.TCP.ENABLED}',
		'{$RSM.TLD.DNS.UDP.ENABLED}',
		'{$RSM.TLD.RDDS43.ENABLED}',
		'{$RSM.TLD.RDDS80.ENABLED}',
		'{$RDAP.TLD.ENABLED}',
		'{$RSM.DNS.NAME.SERVERS}',
		'{$RSM.TLD.RDDS43.SERVER}',
		'{$RSM.TLD.RDDS80.URL}',
		'{$RDAP.BASE.URL}',
	);

	# get data from DB

	my $macros_placeholder = join(",", ("?") x scalar(@macros));
	my $query = "select" .
			" hosts.hostid," .
			"hostmacro.macro," .
			"hostmacro.value" .
		" from" .
			" hosts" .
			" inner join hostmacro on hostmacro.hostid=hosts.hostid" .
		" where" .
			" hosts.host like ? and" .
			" hostmacro.macro in ($macros_placeholder)";
	my $params = ['Template Rsmhost Config %', @macros];

	my $rows = db_select($query, $params);

	# group "macro => value" by hostid

	my %data_by_hostid = ();

	foreach my $row (@{$rows})
	{
		my ($hostid, $macro, $value) = @{$row};
		$data_by_hostid{$hostid}{$macro} = $value;
	}

	# check for failures while retrieving macros, convert "macro => value" to easier-to-use format

	my %data_by_rsmhost = ();

	foreach my $hostid (keys(%data_by_hostid))
	{
		my %macros = %{$data_by_hostid{$hostid}};

		foreach my $macro (@macros)
		{
			fail("missing rsmhost config macro '$macro' for hostid '$hostid'") if (!exists($macros{$macro}));
		}

		$data_by_rsmhost{$macros{'{$RSM.TLD}'}} = {
			'dns_tcp'       => $macros{'{$RSM.TLD.DNS.TCP.ENABLED}'},
			'dns_udp'       => $macros{'{$RSM.TLD.DNS.UDP.ENABLED}'},
			'rdds43'        => $macros{'{$RSM.TLD.RDDS43.ENABLED}'},
			'rdds80'        => $macros{'{$RSM.TLD.RDDS80.ENABLED}'},
			'rdap'          => $macros{'{$RDAP.TLD.ENABLED}'},
			'nsip_list'     => [map([split(/,/)], split(/ /, $macros{'{$RSM.DNS.NAME.SERVERS}'}))],
			'rdds43_server' => $macros{'{$RSM.TLD.RDDS43.SERVER}'},
			'rdds80_server' => get_hostname($macros{'{$RSM.TLD.RDDS80.URL}'}),
			'rdap_server'   => get_hostname($macros{'{$RDAP.BASE.URL}'}),
		};
	}

	return %data_by_rsmhost;
}

sub get_hostname($)
{
	my $url = shift;

	# TODO: consider removing 'http:///'
	if ($url eq '' || $url eq 'http:///')
	{
		return '';
	}

	my ($hostname) = $url =~ m!^\w+://([^/:]+)!;

	fail("invalid url: '$url'") if (!defined($hostname));

	return $hostname;
}

################################################################################
# resolving hosts
################################################################################

sub resolve_hosts($$$)
{
	my %hosts = %{+shift};
	my $ipv4  = shift;
	my $ipv6  = shift;

	my %result = ();

	my $resolver = new Net::DNS::Async(
		QueueSize => get_config('resolver', 'queue_size'),
		Timeout   => get_config('resolver', 'timeout'),
		Retries   => get_config('resolver', 'retries'),
	);

	foreach my $host (keys(%hosts))
	{
		$resolver->add(sub { resolver_callback($host, $hosts{$host}, \%result, 'A'   , shift); }, $host, 'A')    if ($ipv4);
		$resolver->add(sub { resolver_callback($host, $hosts{$host}, \%result, 'AAAA', shift); }, $host, 'AAAA') if ($ipv6);
	}
	$resolver->await();

	return %result;
}

sub resolver_callback($$$)
{
	my $host     = shift;
	my @rsmhosts = @{+shift};
	my $result   = shift;
	my $rr_type  = shift;
	my $response = shift;

	my $rsmhosts = join(', ', @rsmhosts);

	if (!defined($response))
	{
		log_stacktrace(0);
		wrn('timed out (host: %s; rr_type: %s; rsmhosts: %s)', $host, $rr_type, $rsmhosts);
		log_stacktrace(1);
		return;
	}

	my $rcode = $response->header->rcode;
	my $log_message = sprintf("host: %s; rcode: %s; records: %d; rsmhosts: %s", $host, $rcode, scalar($response->answer), $rsmhosts);
	if ($rcode eq 'NOERROR' || $rcode eq 'NXDOMAIN')
	{
		dbg($log_message);
	}
	else
	{
		log_stacktrace(0);
		wrn($log_message);
		log_stacktrace(1);
	}

	foreach my $rr ($response->answer)
	{
		if ($rr->type eq 'CNAME')
		{
			dbg("owner: %s, cname: %s", $rr->owner, $rr->cname);
		}
		elsif ($rr->type eq 'A')
		{
			dbg("owner: %s, ipv4: %s", $rr->owner, $rr->address);
			push(@{$result->{$host}{'ipv4'}}, $rr->address);
		}
		elsif ($rr->type eq 'AAAA')
		{
			dbg("owner: %s, ipv6: %s", $rr->owner, $rr->address);
			push(@{$result->{$host}{'ipv6'}}, $rr->address);
		}
		else
		{
			wrn("unexpected RR type '%s' in RR '%s'", $rr->type, $rr->plain);
		}
	}
};

################################################################################
# doing traceroutes
################################################################################

sub traceroute($$)
{
	my @ip_list            = @{+shift};
	my %ip_rsmhost_mapping = %{+shift};

	my @tasks   = create_tasks(\@ip_list);				# list of scheduled tasks
	my $fm      = new Parallel::ForkManager(scalar(@tasks));	# fork manager for managing workers
	my $select  = IO::Select->new();				# worker sockets for IPC
	my %sockets = ();						# mapping between worker PIDs and their sockets

	# when worker fails, its socket needs to be closed and removed from $select
	$fm->run_on_finish(
		sub
		{
			local *__ANON__ = '$fm->run_on_finish';

			my $pid         = shift; # pid of the process, which is terminated
			my $exit_code   = shift; # exit code of the program
			my $process_id  = shift; # identification of the process (if provided in the "start" method)
			my $exit_signal = shift; # exit signal (0-127: signal name)
			my $core_dump   = shift; # core dump (1 if there was core dump at exit)
			my $data        = shift; # datastructure reference or undef (see RETRIEVING DATASTRUCTURES)

			if ($exit_code != 0)
			{
				my $log_stacktrace = log_stacktrace(0);
				wrn("child process failed");
				wrn("* pid         = " . ($pid         // "<undef>"));
				wrn("* exit_code   = " . ($exit_code   // "<undef>"));
				wrn("* exit_signal = " . ($exit_signal // "<undef>"));
				wrn("* core_dump   = " . ($core_dump   // "<undef>"));
				log_stacktrace($log_stacktrace);

				close($sockets{$pid});
				$select->remove($sockets{$pid});
				delete($sockets{$pid});
			}
		}
	);

	# pass tasks to workers
	foreach my $task (@tasks)
	{
		# wait until it's time to process the task
		my $sleep_duration = $task->{'time'} - Time::HiRes::time();
		if ($sleep_duration > 0)
		{
			Time::HiRes::sleep($sleep_duration);
		}

		# reap failed workers, if any, and call "on finish" callbacks
		$fm->reap_finished_children();

		# get IPC socket
		my $socket = get_worker_socket($fm, $select, \%sockets);

		# read whatever worker has written to the socket
		<$socket>;

		# pass task to the worker
		my $ip = $task->{'ip'};
		my $rsmhosts = join(", ", @{$ip_rsmhost_mapping{$task->{'ip'}}});
		print $socket "$ip;$rsmhosts\n";
	}

	# close worker sockets
	stop_workers($select);

	# wait until all workers finish their tasks and exit
	$fm->wait_all_children();
}

sub create_tasks($)
{
	my @ip_list = @{+shift};

	@ip_list = shuffle(@ip_list);

	my $seconds = get_config('time_limits', 'script')		# max execution time
	            - (Time::HiRes::time() - $^T)			# time spent on initialization, resolving hostnames etc
	            - get_config('time_limits', 'mtr_kill')		# time reserved for timeouts
	            - get_config('time_limits', 'output_handling');	# time reserved for output handling (compressing etc)

	my $now = Time::HiRes::time();

	my @tasks = ();

	for (my $i = 0; $i < scalar(@ip_list); $i++)
	{
		my $task = {
			'ip'   => $ip_list[$i],
			'time' => $now + $seconds / (scalar(@ip_list) - 1) * $i,
		};
		push(@tasks, $task);
	}

	return @tasks;
}

sub get_worker_socket($)
{
	my $fm      = shift; # fork manager
	my $select  = shift; # IO::Select for IPC
	my $sockets = shift; # hash ref for mapping between worker PIDs and their sockets

	my $socket;

	# check if any worker is free

	$! = 0;
	my @ready = $select->can_read(0);

	if ($!)
	{
		fail("failed to get a list of sockets that are ready for reading: $!");
	}

	# reuse existing worker or create new one

	if (scalar(@ready) > 0)
	{
		dbg("reusing existing worker");

		$socket = $ready[0];
	}
	else
	{
		dbg("creating new worker");

		socketpair(my $child, $socket, AF_UNIX, SOCK_STREAM, PF_UNSPEC) or fail("socketpair() failed: $!");

		$child->autoflush(1);
		$socket->autoflush(1);

		my $pid = $fm->start();

		if ($pid == 0)
		{
			close($socket);
			do_work($child);
			close($child);
			$fm->finish(0);
		}

		close($child);
		$select->add($socket);
		$sockets->{$pid} = $socket;
	}

	return $socket;
}

sub stop_workers($)
{
	my $select = shift;

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
}

sub do_work($)
{
	my $socket = shift;

	local $ENV{'MTR_OPTIONS'} = get_config('mtr', 'options');

	my $select = IO::Select->new($socket);

	while (1)
	{
		my $task = get_task_from_socket($select);

		if (!defined($task))
		{
			last;
		}

		my ($ip, $rsmhosts) = split(/;/, $task);

		my $mtr_term = get_config('time_limits', 'mtr_term');
		my $mtr_kill = get_config('time_limits', 'mtr_kill');

		my $timeout;
		if ($mtr_kill > $mtr_term)
		{
			my $time_diff = $mtr_kill - $mtr_term;
			$timeout = "timeout --signal 'TERM' --kill-after '$time_diff' '$mtr_term'";
		}
		else
		{
			$timeout = "timeout --signal 'KILL' '$mtr_kill'";
		}

		dbg("starting mtr for '%s'", $ip);
		my $start = Time::HiRes::time();

		my $output_file = get_json_file_path($ip, 1);
		my $mtr_error = undef;

		if (!execute("$timeout mtr '$ip' | gzip > '$output_file'"))
		{
			$mtr_error = "executing mtr failed";
		}
		elsif (-s $output_file < 32)
		{
			$mtr_error = "looks like mtr generated empty report";
		}

		if (defined($mtr_error))
		{
			log_stacktrace(0);
			wrn("%s (ip: %s; rsmhosts: %s)", $mtr_error, $ip, $rsmhosts);
			log_stacktrace(1);
			unlink($output_file);
		}

		my $end = Time::HiRes::time();
		my $duration = $end - $start;
		dbg("finished mtr for '%s' in %.3f seconds", $ip, $duration);
	}
}

sub get_task_from_socket($)
{
	my $select = shift;

	my @ready;
	my $socket;

	@ready = $select->can_write();
	$socket = $ready[0];

	print $socket "\n";

	@ready = $select->can_read();
	$socket = $ready[0];

	my $task = <$socket>;

	if (defined($task))
	{
		chomp($task);
	}

	return $task;
}

sub execute
{
	dbg("executing: " . join(" ", map('"' . $_ . '"', @_)));

	my $error = undef;

	if (system(@_) != 0)
	{
		if ($? == -1)
		{
			$error = sprintf("failed to execute: %s", $!);
		}
		elsif ($? & 127)
		{
			$error = sprintf("executed process died with signal %d, %s coredump", ($? & 127),  ($? & 128) ? "with" : "without");
		}
		else
		{
			$error = sprintf("executed process exited with value %d", $? >> 8);
		}
	}

	if (defined($error))
	{
		log_stacktrace(0);
		wrn($error);
		log_stacktrace(1);

		return 0;
	}
	else
	{
		return 1;
	}
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
my $log_stacktrace = 1;

$SIG{__WARN__} = sub
{
	local *__ANON__ = 'perl-warn';
	__log(LOG_LEVEL_WARNING, $_[0] =~ s/(\r?\n)+$//r);
};

$SIG{__DIE__} = sub
{
	local *__ANON__ = 'perl-die';
	__log(LOG_LEVEL_FAILURE, $_[0] =~ s/(\r?\n)+$//r);
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

sub initialize_log($)
{
	my $use_log_file = shift;

	if ($use_log_file)
	{
		my $log_file = get_config('paths', 'log_file');

		close(STDOUT) or fail("cannot close STDOUT: $!");
		close(STDERR) or fail("cannot close STDERR: $!");

		open(STDOUT, '>>', $log_file) or fail("cannot open $log_file: $!");
		open(STDERR, '>>', $log_file) or fail("cannot open $log_file: $!");
	}
}

sub __log
{
	my $message_log_level = shift;
	my $message = (@_ <= 1 ? shift // "" : sprintf(shift, @_)) . "\n";

	if ($message_log_level != LOG_LEVEL_DEBUG && $message_log_level != LOG_LEVEL_INFO && log_stacktrace())
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
	my $rv = GetOptionsFromArray([@ARGV], \%OPTS, "config=s", "nolog", "debug", "help");

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

	if (defined($db_name) && -f $db_name)
	{
		db_connect_sqlite($db_name);
	}
	else
	{
		db_connect_mysql($db_host, $db_name, $db_user, $db_pswd, $db_tls_settings);
	}
}

sub db_connect_sqlite($)
{
	my $db_file = shift;

	my $data_source = "DBI:SQLite:uri=file:$db_file?mode=ro";

	my $connect_opts = {
		'PrintError'        => 0,
		'HandleError'       => \&__handle_db_error,
		'sqlite_open_flags' => SQLITE_OPEN_READONLY,
	};

	$db_handle = DBI->connect($data_source, undef, undef, $connect_opts);
}

sub db_connect_mysql($$$$$)
{
	my $db_host         = shift;
	my $db_name         = shift;
	my $db_user         = shift;
	my $db_pswd         = shift;
	my $db_tls_settings = shift;

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
	my $config_file = get_config('paths', 'proxy_config');

	my $db_host = 'localhost';
	my $db_name = undef;
	my $db_user = '';
	my $db_pswd = '';

	my $db_tls_key_file  = undef;
	my $db_tls_cert_file = undef;
	my $db_tls_ca_file   = undef;
	my $db_tls_cipher    = undef;

	open(my $fh, '<', $config_file) or fail("cannot open $config_file: $!");

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

	close($fh) or fail("cannot close $config_file: $!");

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

tracerouter-mtr.pl - calls mtr for all monitored IP addresses.

=head1 SYNOPSIS

tracerouter-mtr.pl --config <file> [--nolog] [--debug] [--help]

=head1 OPTIONS

=over 8

=item B<--config> string

Configuration file for the tool.

=item B<--nolog>

Print output to stdout and stderr instead of a log file.

=item B<--debug>

Run the script in debug mode. This means printing more information.

=item B<--help>

Print a brief help message and exit.

=back

=cut
