#!/usr/bin/perl

BEGIN
{
        our $MYDIR = $0; $MYDIR =~ s,(.*)/.*,$1,; $MYDIR = '.' if ($MYDIR eq $0);
}
use lib $MYDIR;

use strict;
use warnings;

use RSM;
use RSMSLV;
use List::Util qw(max);
use Getopt::Long qw(:config no_auto_abbrev);

use constant CRONTAB_FILE => '/etc/cron.d/rsm';
use constant SLV_LOG_FILE => '/var/log/zabbix/rsm.slv.err';

my @cron_jobs = (
	['main', '* * * * *' , 0 , '/opt/zabbix/scripts/slv/rsm.probe.online.pl'       , SLV_LOG_FILE],
	undef,
	['main', '* * * * *' , 10, '/opt/zabbix/scripts/slv/rsm.slv.dns.avail.pl'      , SLV_LOG_FILE],
	['main', '* * * * *' , 20, '/opt/zabbix/scripts/slv/rsm.slv.dns.rollweek.pl'   , SLV_LOG_FILE],
	['main', '* * * * *' , 20, '/opt/zabbix/scripts/slv/rsm.slv.dns.downtime.pl'   , SLV_LOG_FILE],
	['main', '* * * * *' , 30, '/opt/zabbix/scripts/slv/rsm.slv.dns.udp.rtt.pl'    , SLV_LOG_FILE],
	['main', '* * * * *' , 30, '/opt/zabbix/scripts/slv/rsm.slv.dns.tcp.rtt.pl'    , SLV_LOG_FILE],
	['main', '* * * * *' , 10, '/opt/zabbix/scripts/slv/rsm.slv.dns.ns.avail.pl'   , SLV_LOG_FILE],
	['main', '* * * * *' , 20, '/opt/zabbix/scripts/slv/rsm.slv.dns.ns.downtime.pl', SLV_LOG_FILE],
	undef,
	['main', '* * * * *' , 10, '/opt/zabbix/scripts/slv/rsm.slv.dnssec.avail.pl'   , SLV_LOG_FILE],
	['main', '* * * * *' , 20, '/opt/zabbix/scripts/slv/rsm.slv.dnssec.rollweek.pl', SLV_LOG_FILE],
	undef,
	['main', '* * * * *' , 10, '/opt/zabbix/scripts/slv/rsm.slv.rdds.avail.pl'     , SLV_LOG_FILE],
	['main', '* * * * *' , 20, '/opt/zabbix/scripts/slv/rsm.slv.rdds.rollweek.pl'  , SLV_LOG_FILE],
	['main', '* * * * *' , 20, '/opt/zabbix/scripts/slv/rsm.slv.rdds.downtime.pl'  , SLV_LOG_FILE],
	['main', '* * * * *' , 30, '/opt/zabbix/scripts/slv/rsm.slv.rdds.rtt.pl'       , SLV_LOG_FILE],
	undef,
	['main', '* * * * *' , 10, '/opt/zabbix/scripts/slv/rsm.slv.rdap.avail.pl'     , SLV_LOG_FILE],
	['main', '* * * * *' , 20, '/opt/zabbix/scripts/slv/rsm.slv.rdap.rollweek.pl'  , SLV_LOG_FILE],
	['main', '* * * * *' , 20, '/opt/zabbix/scripts/slv/rsm.slv.rdap.downtime.pl'  , SLV_LOG_FILE],
	['main', '* * * * *' , 30, '/opt/zabbix/scripts/slv/rsm.slv.rdap.rtt.pl'       , SLV_LOG_FILE],
	undef,
	['main', '0 1 1 * *' , 0 , '/opt/zabbix/scripts/disable-rdds-for-rdap-hosts.pl', '/var/log/zabbix/disable-rdds-for-rdap-hosts.err'],
	['main', '0 15 1 * *', 0 , '/opt/zabbix/scripts/sla-monthly-status.pl'         , '/var/log/zabbix/sla-monthly-status.err'],
	['main', '0 15 1 * *', 0 , '/opt/zabbix/scripts/sla-report.php'                , '/var/log/zabbix/sla-report.err'],
	undef,
	['db'  , '0 23 * * *', 0 , '/opt/zabbix/scripts/MySQL_part_management.pl'      , '/var/log/zabbix/zabbix-mysql-partitioning'],
	['db'  , '0 2 * * *' , 0 , '/opt/zabbix/scripts/MySQL_part_management.pl'      , '/var/log/zabbix/zabbix-mysql-partitioning'],
);

sub main()
{
	parse_opts('enable-main', 'enable-db-partitioning', 'enable-all', 'disable-all', 'delete-all');
	setopt('nolog');

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

	my $timing_len = max(map(defined($_) ? length($_->[1]) : 0, @cron_jobs));
	my $delay_len  = max(map(defined($_) ? length($_->[2]) : 0, @cron_jobs));
	my $script_len = max(map(defined($_) ? length($_->[3]) : 0, @cron_jobs));

	my $group_status = {
		"main" => $enable_main ? "" : "#",
		"db"   => $enable_db   ? "" : "#",
	};

	my $crontab = "";

	for my $job (@cron_jobs)
	{
		if (defined($job))
		{
			my ($group, $timing, $delay, $script, $logfile) = @{$job};

			$crontab .= sprintf("%s%-${timing_len}s root sleep %${delay_len}d; %-${script_len}s >> %s 2>&1\n",
					$group_status->{$group}, $timing, $delay, $script, $logfile);
		}
		else
		{
			$crontab .= "\n";
		}
	}

	my $error;
	if (write_file(CRONTAB_FILE, $crontab, \$error) != SUCCESS)
	{
		print(STDERR "failed to enable cron jobs\n");
		print(STDERR "$error\n");
		exit(1);
	}
	else
	{
		printf("cron jobs created successfully\n");
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

setup-cron.pl [--enable-main] [--enable-db-partitioning] [--enable-all] [--disable-all] [--delete-all] [--help]

=head1 OPTIONS

=over 8

=item B<--enable-main> int

Enable main cron jobs. These are jobs for SLV scripts (executed every minute) and some jobs that are executed once per month.

=item B<--enable-db-partitioning> int

Enable DB partitioning cron jobs.

=item B<--enable-all> int

Enable all cron jobs.

=item B<--disable-all> int

Disable all cron jobs.

=item B<--delete-all> int

Delete cron jobs.

=item B<--help>

Print a brief help message and exit.

=back

=cut
