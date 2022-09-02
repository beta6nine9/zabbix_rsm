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

use constant PROBE_PID_FILE => '/tmp/zabbix-rsm.probe.online.pl.cron.pid';
use constant DNS_PID_FILE   => '/tmp/zabbix-rsm.slv.dns.avail.pl.cron.pid';
use constant RDDS_PID_FILE  => '/tmp/zabbix-rsm.slv.rdds.avail.pl.cron.pid';
use constant RDAP_PID_FILE  => '/tmp/zabbix-rsm.slv.rdap.avail.pl.cron.pid';

my $job_user = 'zabbix';	# default user of the job

my $echo_probe_pid = 'echo -n $$ > ' . PROBE_PID_FILE . '; ';
my $echo_dns_pid   = 'echo -n $$ > ' . DNS_PID_FILE   . '; ';
my $echo_rdds_pid  = 'echo -n $$ > ' . RDDS_PID_FILE  . '; ';
my $echo_rdap_pid  = 'echo -n $$ > ' . RDAP_PID_FILE  . '; ';

my $wait_probe_pid = 'sleep 3; timeout 30 tail --pid=$(cat ' . PROBE_PID_FILE . ') -f /dev/null; ';
my $wait_dns_pid   = 'sleep 3; timeout 45 tail --pid=$(cat ' . DNS_PID_FILE   . ') -f /dev/null; ';
my $wait_rdds_pid  = 'sleep 3; timeout 45 tail --pid=$(cat ' . RDDS_PID_FILE  . ') -f /dev/null; ';
my $wait_rdap_pid  = 'sleep 3; timeout 45 tail --pid=$(cat ' . RDAP_PID_FILE  . ') -f /dev/null; ';

my @cron_jobs = (
	'# ignore missing home directory errors',
	'HOME=/tmp',
	'',
	['main', '* * * * *' , 0, $echo_probe_pid . SCRIPTS_DIR . '/slv/rsm.probe.online.pl'                                                                  , SLV_LOG_FILE],
	'',
	['main', '* * * * *' , 0, $echo_dns_pid  . $wait_probe_pid . SCRIPTS_DIR . '/slv/rsm.slv.dns.avail.pl'                                                , SLV_LOG_FILE],
	['main', '* * * * *' , 0, $echo_rdds_pid . $wait_probe_pid . SCRIPTS_DIR . '/slv/rsm.slv.rdds.avail.pl'                                               , SLV_LOG_FILE],
	['main', '* * * * *' , 0, $echo_rdap_pid . $wait_probe_pid . SCRIPTS_DIR . '/slv/rsm.slv.rdap.avail.pl'                                               , SLV_LOG_FILE],
	'',
	['main', '* * * * *' , 0, $wait_dns_pid  . SCRIPTS_DIR . '/slv/rsm.slv.dns.rollweek.pl'                                                               , SLV_LOG_FILE],
	['main', '* * * * *' , 0, $wait_rdds_pid . SCRIPTS_DIR . '/slv/rsm.slv.rdds.rollweek.pl'                                                              , SLV_LOG_FILE],
	['main', '* * * * *' , 0, $wait_rdap_pid . SCRIPTS_DIR . '/slv/rsm.slv.rdap.rollweek.pl'                                                              , SLV_LOG_FILE],
	'',
	['main', '* * * * *' , 0, $wait_dns_pid  . SCRIPTS_DIR . '/slv/rsm.slv.dns.downtime.pl'                                                               , SLV_LOG_FILE],
	['main', '* * * * *' , 0, $wait_rdds_pid . SCRIPTS_DIR . '/slv/rsm.slv.rdds.downtime.pl'                                                              , SLV_LOG_FILE],
	['main', '* * * * *' , 0, $wait_rdap_pid . SCRIPTS_DIR . '/slv/rsm.slv.rdap.downtime.pl'                                                              , SLV_LOG_FILE],
	'',
	['main', '* * * * *' , 0, $wait_probe_pid . '(' . SCRIPTS_DIR . '/slv/rsm.slv.dnssec.avail.pl && ' . SCRIPTS_DIR . '/slv/rsm.slv.dnssec.rollweek.pl)' , SLV_LOG_FILE],
	['main', '* * * * *' , 0, $wait_probe_pid . '(' . SCRIPTS_DIR . '/slv/rsm.slv.dns.ns.avail.pl && ' . SCRIPTS_DIR . '/slv/rsm.slv.dns.ns.downtime.pl)' , SLV_LOG_FILE],
	'',
	['main', '* * * * *' , 0, SCRIPTS_DIR . '/slv/rsm.slv.dns.udp.rtt.pl'                                                                                 , SLV_LOG_FILE],
	['main', '* * * * *' , 0, SCRIPTS_DIR . '/slv/rsm.slv.dns.tcp.rtt.pl'                                                                                 , SLV_LOG_FILE],
	['main', '* * * * *' , 0, SCRIPTS_DIR . '/slv/rsm.slv.rdds.rtt.pl'                                                                                    , SLV_LOG_FILE],
	['main', '* * * * *' , 0, SCRIPTS_DIR . '/slv/rsm.slv.rdap.rtt.pl'                                                                                    , SLV_LOG_FILE],
	'',
	['main', '0 1 1 * *' , 0, SCRIPTS_DIR . '/disable-rdds-for-rdap-hosts.pl'                                                                             , LOG_DIR . '/disable-rdds-for-rdap-hosts.err'],
	['main', '20 0 1 * *', 0, SCRIPTS_DIR . '/sla-monthly-status.pl'                                                                                      , LOG_DIR . '/sla-monthly-status.err'],
	['main', '0 15 1 * *', 0, SCRIPTS_DIR . '/sla-report.php'                                                                                             , LOG_DIR . '/sla-report.err'],
	'',
	['main', '* * * * *' , 0, 'sleep 15; ' . SCRIPTS_DIR . '/config-cache-reload.pl'                                                                      , LOG_DIR . '/config-cache-reload.log'],
	['main', '* * * * *' , 0, 'sleep 45; ' . SCRIPTS_DIR . '/config-cache-reload.pl'                                                                      , LOG_DIR . '/config-cache-reload.log'],
);

sub main()
{
	parse_opts('enable', 'disable', 'delete', 'user=s');
	setopt('nolog');

	if (opt('user'))
	{
		$job_user = getopt('user');
	}
	if (opt('enable'))
	{
		usage("Cannot use --enable with --disable") if (opt('disable'));
		usage("Cannot use --enable with --delete") if (opt('delete'));
	}
	if (opt('disable'))
	{
		usage("Cannot use --disable with --delete") if (opt('delete'));
	}

	if (opt('enable'))
	{
		create_all(1);
	}
	elsif (opt('disable'))
	{
		create_all(0);
	}
	elsif (opt('delete'))
	{
		delete_all();
	}
	else
	{
		usage("At least one option must be specified");
	}
}

sub create_all($)
{
	my $enable = shift;

	my $timing_len = max(map(ref($_) eq 'ARRAY'                 ? length($_->[1]) : 0, @cron_jobs));
	my $delay_len  = max(map(ref($_) eq 'ARRAY' && $_->[2] != 0 ? length($_->[2]) : 0, @cron_jobs));
	my $script_len = max(map(ref($_) eq 'ARRAY'                 ? length($_->[3]) : 0, @cron_jobs));

	my $group_status = {
		"main" => $enable ? "" : "#",
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
				"%s%-${timing_len}s ${job_user} %s%-${script_len}s >> %s 2>&1\n",
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

setup-cron.pl [--enable] [--disable] [--delete] [--user user] [--help]

=head1 OPTIONS

=over 8

=item B<--enable>

Enable cron jobs for SLV scripts (executed every minute) and some jobs that are executed once per month.

=item B<--disable>

Disable all cron jobs.

=item B<--delete>

Delete all cron jobs.

=item B<--user> user

Override the default "zabbix" cron job user.

=item B<--help>

Print a brief help message and exit.

=back

=cut
