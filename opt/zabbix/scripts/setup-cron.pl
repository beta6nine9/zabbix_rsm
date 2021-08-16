#!/usr/bin/env perl

use FindBin;
use lib $FindBin::RealBin;

use strict;
use warnings;

use RSM;
use RSMSLV;
use List::Util qw(max);
use Getopt::Long qw(:config no_auto_abbrev);

use constant CRONTAB_FILE => '/etc/cron.d/rsm';
use constant LOG_DIR      => '/var/log/zabbix';
use constant SCRIPTS_DIR  => '/opt/zabbix/scripts';
use constant SLV_LOG_FILE => LOG_DIR . '/rsm.slv.err';

use constant PID_FILE     => '/tmp/zabbix-rsm.probe.online.pl.cron.pid';

my $JOB_USER = 'zabbix';	# default user of the job

my $echo_pid = 'echo -n $$ > ' . PID_FILE . '; ';
my $wait_pid = 'sleep 3; timeout 30 tail --pid=$(cat ' . PID_FILE . ') -f /dev/null; ';

my @cron_jobs = (
	'# ignore missing home directory errors',
	'HOME=/tmp',
	'',
	['main', '* * * * *' , 0, $echo_pid . SCRIPTS_DIR . '/slv/rsm.probe.online.pl'                                                                                                                 , SLV_LOG_FILE],
	'',
	['main', '* * * * *' , 0, $wait_pid . '(' . SCRIPTS_DIR . '/slv/rsm.slv.dns.avail.pl && ' . SCRIPTS_DIR . '/slv/rsm.slv.dns.rollweek.pl && ' . SCRIPTS_DIR . '/slv/rsm.slv.dns.downtime.pl)'   , SLV_LOG_FILE],
	['main', '* * * * *' , 0, $wait_pid . '(' . SCRIPTS_DIR . '/slv/rsm.slv.rdds.avail.pl && ' . SCRIPTS_DIR . '/slv/rsm.slv.rdds.rollweek.pl && ' . SCRIPTS_DIR . '/slv/rsm.slv.rdds.downtime.pl)', SLV_LOG_FILE],
	['main', '* * * * *' , 0, $wait_pid . '(' . SCRIPTS_DIR . '/slv/rsm.slv.rdap.avail.pl && ' . SCRIPTS_DIR . '/slv/rsm.slv.rdap.rollweek.pl && ' . SCRIPTS_DIR . '/slv/rsm.slv.rdap.downtime.pl)', SLV_LOG_FILE],
	['main', '* * * * *' , 0, $wait_pid . '(' . SCRIPTS_DIR . '/slv/rsm.slv.dnssec.avail.pl && ' . SCRIPTS_DIR . '/slv/rsm.slv.dnssec.rollweek.pl)'                                                , SLV_LOG_FILE],
	['main', '* * * * *' , 0, $wait_pid . '(' . SCRIPTS_DIR . '/slv/rsm.slv.dns.ns.avail.pl && ' . SCRIPTS_DIR . '/slv/rsm.slv.dns.ns.downtime.pl)'                                                , SLV_LOG_FILE],
	'',
	['main', '* * * * *' , 0, SCRIPTS_DIR . '/slv/rsm.slv.dns.udp.rtt.pl'                                                                                                                          , SLV_LOG_FILE],
	['main', '* * * * *' , 0, SCRIPTS_DIR . '/slv/rsm.slv.dns.tcp.rtt.pl'                                                                                                                          , SLV_LOG_FILE],
	['main', '* * * * *' , 0, SCRIPTS_DIR . '/slv/rsm.slv.rdds.rtt.pl'                                                                                                                             , SLV_LOG_FILE],
	['main', '* * * * *' , 0, SCRIPTS_DIR . '/slv/rsm.slv.rdap.rtt.pl'                                                                                                                             , SLV_LOG_FILE],
	'',
	['main', '0 1 1 * *' , 0, SCRIPTS_DIR . '/disable-rdds-for-rdap-hosts.pl'                                                                                                                      , LOG_DIR . '/disable-rdds-for-rdap-hosts.err'],
	['main', '0 15 1 * *', 0, SCRIPTS_DIR . '/sla-monthly-status.pl'                                                                                                                               , LOG_DIR . '/sla-monthly-status.err'],
	['main', '0 15 1 * *', 0, SCRIPTS_DIR . '/sla-report.php'                                                                                                                                      , LOG_DIR . '/sla-report.err'],
	'',
	['main', '* * * * *' , 0, 'sleep 15; ' . SCRIPTS_DIR . '/config-cache-reload.pl'                                                                                                               , LOG_DIR . '/config-cache-reload.log'],
	['main', '* * * * *' , 0, 'sleep 45; ' . SCRIPTS_DIR . '/config-cache-reload.pl'                                                                                                               , LOG_DIR . '/config-cache-reload.log'],
	'',
	['db'  , '0 23 * * *', 0, SCRIPTS_DIR . '/MySQL_part_management.pl'                                                                                                                            , LOG_DIR . '/zabbix-mysql-partitioning'],
	['db'  , '0 2 * * *' , 0, SCRIPTS_DIR . '/MySQL_part_management.pl'                                                                                                                            , LOG_DIR . '/zabbix-mysql-partitioning'],
);

sub main()
{
	parse_opts('enable-main', 'enable-db-partitioning', 'enable-all', 'disable-all', 'delete-all', 'user=s');
	setopt('nolog');

	if (opt('user'))
	{
		$JOB_USER = getopt('user');
	}
	if (opt('enable-main'))
	{
		usage("Cannot use --enable-main with --enable-all") if (opt('enable-all'));
		usage("Cannot use --enable-main with --disable-all") if (opt('disable-all'));
		usage("Cannot use --enable-main with --delete-all") if (opt('delete-all'));
	}
	if (opt('enable-db-partitioning'))
	{
		usage("Cannot use --enable-db-partitioning with --enable-all") if (opt('enable-all'));
		usage("Cannot use --enable-db-partitioning with --disable-all") if (opt('disable-all'));
		usage("Cannot use --enable-db-partitioning with --delete-all") if (opt('delete-all'));
	}
	if (opt('enable-all'))
	{
		usage("Cannot use --enable-all with --disable-all") if (opt('disable-all'));
		usage("Cannot use --enable-all with --delete-all") if (opt('delete-all'));
	}
	if (opt('disable-all'))
	{
		usage("Cannot use --disable-all with --delete-all") if (opt('delete-all'));
	}

	if (opt('enable-main') || opt('enable-db-partitioning'))
	{
		create_all(getopt('enable-main'), getopt('enable-db-partitioning'));
	}
	elsif (opt('enable-all'))
	{
		create_all(1, 1);
	}
	elsif (opt('disable-all'))
	{
		create_all(0, 0);
	}
	elsif (opt('delete-all'))
	{
		delete_all();
	}
	else
	{
		usage("At least one option must be specified");
	}
}

sub create_all($$)
{
	my $enable_main = shift;
	my $enable_db   = shift;

	my $timing_len = max(map(ref($_) eq 'ARRAY'                 ? length($_->[1]) : 0, @cron_jobs));
	my $delay_len  = max(map(ref($_) eq 'ARRAY' && $_->[2] != 0 ? length($_->[2]) : 0, @cron_jobs));
	my $script_len = max(map(ref($_) eq 'ARRAY'                 ? length($_->[3]) : 0, @cron_jobs));

	my $group_status = {
		"main" => $enable_main ? "" : "#",
		"db"   => $enable_db   ? "" : "#",
	};

	my $crontab = "";

	for my $job (@cron_jobs)
	{
		if (ref($job) eq 'ARRAY')
		{
			my ($group, $timing, $delay, $script, $logfile) = @{$job};

			my $sleep = (
				$delay ?
				sprintf("sleep %${delay_len}d; ", $delay) :
				sprintf(($delay_len ? "        %${delay_len}s" : "%${delay_len}s"), "")
			);

			$crontab .= sprintf(
				"%s%-${timing_len}s $JOB_USER %s%-${script_len}s >> %s 2>&1\n",
				$group_status->{$group}, $timing, $sleep, $script, $logfile
			);
		}
		else
		{
			$crontab .= "$job\n";
		}
	}

	my $error;
	if (write_file(CRONTAB_FILE, $crontab, \$error) != SUCCESS)
	{
		print(STDERR "failed to save cron jobs\n");
		print(STDERR "$error\n");
		exit(1);
	}
	else
	{
		printf("cron jobs saved successfully\n");
	}
}

sub delete_all()
{
	if (unlink(CRONTAB_FILE))
	{
		print("cron jobs deleted successfully\n");
	}
	else
	{
		print(STDERR "failed to delete cron jobs\n");
		print(STDERR "cannot delete file ${\CRONTAB_FILE}: $!\n");
		exit(1);
	}
}

main();

__END__

=head1 NAME

setup-cron.pl - setup cron jobs.

=head1 SYNOPSIS

setup-cron.pl [--enable-main] [--enable-db-partitioning] [--enable-all] [--disable-all] [--delete-all] [--user user] [--help]

=head1 OPTIONS

=over 8

=item B<--enable-main>

Enable only main cron jobs. These are jobs for SLV scripts (executed every minute) and some jobs that are executed once per month.

=item B<--enable-db-partitioning>

Enable only DB partitioning cron jobs.

=item B<--enable-all>

Enable all cron jobs.

=item B<--disable-all>

Disable all cron jobs.

=item B<--delete-all>

Delete all cron jobs.

=item B<--user> user

Override the default "zabbix" cron job user.

=item B<--help>

Print a brief help message and exit.

=back

=cut
