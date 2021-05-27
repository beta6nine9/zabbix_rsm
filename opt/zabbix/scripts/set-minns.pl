#!/usr/bin/env perl

use FindBin;
use lib $FindBin::RealBin;

use strict;
use warnings;

use RSM;
use RSMSLV;
use TLD_constants qw(:templates);
use Data::Dumper;

use constant MACRO_TLD_DNS_AVAIL_MINNS => '{$RSM.TLD.DNS.AVAIL.MINNS}';

use constant DNS_MINNS_OFFSET_MINUTES		=> 15;
use constant DNS_MINNS_OFFSET			=> DNS_MINNS_OFFSET_MINUTES * 60;
use constant DNS_MINNS_MIN_INTERVAL_DAYS	=> 90;
use constant DNS_MINNS_MIN_INTERVAL		=> DNS_MINNS_MIN_INTERVAL_DAYS * 86400;

sub main()
{
	my $config = get_rsm_config();

	set_slv_config($config);

	parse_opts('tld=s', 'schedule', 'value=i', 'timestamp=i', 'cancel', 'status');
	setopt('nolog');
	log_only_message(1);
	check_opts();

	my @server_keys = get_rsm_server_keys($config);

	my $found_tld = 0;
	my $tld = getopt('tld');

	foreach my $server_key (@server_keys)
	{
		db_connect($server_key);

		my ($macro_id, $macro_value) = get_minns_macro($tld);

		if (defined($macro_id))
		{
			$found_tld = 1;
			info('Found TLD "' . $tld . '" on server "' . $server_key . '"');
			info('Value of macro: "' . $macro_value . '"');

			if (opt('schedule'))
			{
				schedule($tld, $macro_id, $macro_value, getopt('value'), getopt('timestamp'));

				# TODO: schedule config-cache-reload
			}
			if (opt('cancel'))
			{
				cancel($tld, $macro_id, $macro_value);

				# TODO: schedule config-cache-reload
			}
			if (opt('status'))
			{
				status($tld, $macro_value);
			}

			db_disconnect();

			last;
		}

		db_disconnect();
	}

	if (!$found_tld)
	{
		pfail('Could not find tld "' . getopt('tld') . '"');
	}

	slv_exit(SUCCESS);
}

sub pfail
{
	log_stacktrace(0);
	fail(@_);
}

sub format_time($)
{
	my $ts = shift;

	if (!defined($ts))
	{
		return undef;
	}

	my $ymd = ts_ymd($ts, '-');
	my $hms = ts_hms($ts, ':');

	return "$ymd $hms ($ts)";
}

sub check_opts()
{
	if (!opt('tld'))
	{
		pfail('Missing option: --tld');
	}

	my $opt_count = 0;

	$opt_count++ if (opt('schedule'));
	$opt_count++ if (opt('cancel'));
	$opt_count++ if (opt('status'));

	if ($opt_count == 0)
	{
		pfail('Missing option: --schedule, --cancel, --status');
	}

	if ($opt_count > 1)
	{
		pfail('Only one option may be used: --schedule, --cancel, --status');
	}

	if (opt('schedule'))
	{
		if (!opt('value'))
		{
			pfail('Missing option: --value');
		}
	}
	else
	{
		if (opt('value'))
		{
			pfail('Option can be used only with --schedule: --value');
		}
		if (opt('timestamp'))
		{
			pfail('Option can be used only with --schedule: --timestamp');
		}
	}
}

sub get_minns_macro($)
{
	my $tld         = shift;

	my $macro_id    = undef;
	my $macro_value = undef;

	my $sql = 'select' .
			' hostmacro.hostmacroid,' .
			' hostmacro.value' .
		' from' .
			' hosts' .
			' inner join hostmacro on hostmacro.hostid=hosts.hostid' .
		' where' .
			' hosts.host=? and' .
			' hostmacro.macro=?';

	my $params = [
		TEMPLATE_RSMHOST_CONFIG_PREFIX . $tld,
		MACRO_TLD_DNS_AVAIL_MINNS,
	];

	my $rows = db_select($sql, $params);

	if (@{$rows})
	{
		$macro_id    = $rows->[0][0];
		$macro_value = $rows->[0][1];
	}

	return ($macro_id, $macro_value);
}

sub set_minns_macro($$)
{
	my $macro_id    = shift;
	my $macro_value = shift;

	my $sql = 'update hostmacro set value=? where hostmacroid=?';
	my $params = [$macro_value, $macro_id];

	db_exec($sql, $params);
}

sub schedule($$$$$)
{
	my $tld         = shift;
	my $macro_id    = shift;
	my $macro_value = shift;
	my $value       = shift;
	my $time        = shift;

	if (!defined($time))
	{
		$time = cycle_start($^T + DNS_MINNS_OFFSET, 60);
		wrn('Time of change is not specified, setting it to: ' . format_time($time));
	}
	if ($time % 60 != 0)
	{
		$time = cycle_start($time, 60);
		wrn('Truncating time of change to the beginning of the minute: ' . format_time($time));
	}

	my $now = cycle_start($^T, 60);
	my $minns = parse_minns_macro($macro_value);

	if ($time < $^T)
	{
		pfail('Specified time is in the past');
	}
	if ($time < $now + DNS_MINNS_OFFSET)
	{
		pfail('Specified time is within next ' . DNS_MINNS_OFFSET_MINUTES . ' minutes');
	}
	if (defined($minns->{'previous_clock'}) && $minns->{'previous_clock'} < $now && $minns->{'previous_clock'} >= $now - DNS_MINNS_MIN_INTERVAL)
	{
		pfail('There already was a change during last ' . DNS_MINNS_MIN_INTERVAL_DAYS . ' days');
	}
	if (defined($minns->{'scheduled_clock'})&& $minns->{'scheduled_clock'} >= $now && $minns->{'scheduled_clock'} < $^T + DNS_MINNS_OFFSET)
	{
		pfail('Cannot schedule the change, there already is a scheduled change within next ' . DNS_MINNS_OFFSET_MINUTES . ' minutes');
	}
	if ($value == $minns->{'current_value'})
	{
		pfail('Specified value is the same as current value');
	}

	set_minns_macro($macro_id, $minns->{'current_value'} . ';' . $time . ':' . $value);
	info();
	info('The change was scheduled successfully');
}

sub cancel($$$)
{
	my $tld         = shift;
	my $macro_id    = shift;
	my $macro_value = shift;

	my $minns = parse_minns_macro($macro_value);

	if (!defined($minns->{'scheduled_clock'}))
	{
		pfail('Cannot cancel scheduling, updating minns is not scheduled');
	}
	if (cycle_start($^T, 60) >= $minns->{'scheduled_clock'} - DNS_MINNS_OFFSET)
	{
		pfail('Cannot cancel scheduling, scheduled change is within next ' . DNS_MINNS_OFFSET_MINUTES . ' minutes');
	}

	set_minns_macro($macro_id, $minns->{'current_value'});
	info();
	info('The change was canceled successfully');
}

sub status($$)
{
	my $tld         = shift;
	my $macro_value = shift;

	my $minns = parse_minns_macro($macro_value);

	info();
	info('Current status:');
	info('* time: '  . format_time($^T));
	info('* minns: ' . $minns->{'current_value'});

	info();
	info('Scheduling status:');
	info('* time: '  . (format_time($minns->{'scheduled_clock'}) // '-'));
	info('* new minns: ' . ($minns->{'scheduled_value'} // '-'));

	info();
	info('Previous change:');
	info('* time: '  . (format_time($minns->{'previous_clock'}) // '-'));
	info('* old minns: ' . ($minns->{'previous_value'} // '-'));
}

sub parse_minns_macro($)
{
	my $macro = shift;

	if ($macro =~ /^(\d+)(?:;(\d+):(\d+))?$/)
	{
		my $curr_minns  = $1;
		my $sched_clock = $2;
		my $sched_minns = $3;
		my $prev_clock  = undef;
		my $prev_minns  = undef;

		if (defined($sched_clock) && $sched_clock < cycle_start($^T, 60))
		{
			$prev_clock = $sched_clock;
			$prev_minns = $curr_minns;
			$curr_minns = $sched_minns;
			undef($sched_clock);
			undef($sched_minns);
		}

		return {
			'current_value'   => $curr_minns,
			'scheduled_clock' => $sched_clock,
			'scheduled_value' => $sched_minns,
			'previous_clock'  => $prev_clock,
			'previous_value'  => $prev_minns,
		};
	}

	pfail('Unexpected value/format of macro: ' . $macro);
}

main();

__END__

=head1 NAME

set-minns.pl - update minimum number of name servers.

=head1 SYNOPSIS

set-minns.pl --tld <tld> --schedule --value <value> [--timestamp <timestamp>] [--debug]

set-minns.pl --tld <tld> --cancel [--debug]

set-minns.pl --tld <tld> --status [--debug]

set-minns.pl --help

=head1 OPTIONS

=over 8

=item B<--tld> string

Specify TLD.

=item B<--schedule>

Schedule the change.

=item B<--value> int

Specify new value.

=item B<--timestamp> int

Specify timestamp when the new value should start to be used. This value will be rounded to minutes.

=item B<--cancel>

Cancel the scheduled change.

=item B<--status>

Show current value and scheduled value and timestamp, if the change is scheduled.

=item B<--debug>

Run the script in debug mode. This means printing more information.

=item B<--help>

Print a brief help message and exit.

=back

=cut
