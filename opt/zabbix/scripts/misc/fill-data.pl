#!/usr/bin/perl

use strict;
use warnings;

use Data::Dumper;
use DateTime;
use Time::HiRes qw(sleep);

use lib '/opt/zabbix/scripts';

use RSM;
use RSMSLV;
use TLD_constants qw(:groups :api);

use constant DEFAULT_DELETE_CHUNK =>	3600 * 24;	# seconds, when deleting data from history (default: 1 day)

my $BACKSPACE = chr(0x08);

# flush to stdout immediately
$| = 1;

#sub collect_source_data($$$$);
sub get_service_from_probe_key($);
sub text_print($);
sub text_clear();
sub text_clear_num($);
sub exec_sql($);
sub delete_tld_slv_data($);
sub copy_data($$$);
sub min($$);

parse_opts('date=s', 'months=i', 'continue-tld=s', 'server-id=s', 'tld=s', 'delete-chunk=i');

setopt('nolog');

if (!opt('date'))
{
	fail("you must specify the date using option --date");
}

if (opt('months'))
{
	if (1 > getopt('months') || getopt('months') > 12)
	{
		fail("number of months must be between 1 and 12");
	}
}

if (getopt('date') !~ /^\d\d\/\d\d\/\d\d\d\d$/)
{
	fail(getopt('date'), " -- invalid date, expected format: dd/mm/yyyy\n");
}

my $delete_chunk = (opt('delete-chunk') ? getopt('delete-chunk') * 3600 : DEFAULT_DELETE_CHUNK);

my ($d, $m, $y) = split('/', getopt('date'));

my $dt_date = DateTime->new(
	year       => $y,
	month      => $m,
	day        => $d,
	hour       => 0,
	minute     => 0,
	second     => 0,
	nanosecond => 0,
	time_zone  => 'UTC'
);

my $dt_current_month_start = $dt_date->clone();
$dt_current_month_start->truncate(to => 'month');

my $source_from = $dt_date->epoch();
my $source_till = $source_from + 86400 - 1;	# must be 1 full day

my ($dt_destination_from, $dt_destination_till);

$dt_destination_till = $dt_date->clone();
$dt_destination_till->subtract(seconds => 1);

if (opt('months'))
{
	$dt_destination_from = $dt_current_month_start->clone();
	$dt_destination_from->subtract(months => getopt('months'));
}
else
{
	# only current month
	dbg('only current month');

	$dt_destination_from = $dt_current_month_start->clone();
}

if ($dt_destination_till->epoch() < $dt_destination_from->epoch())
{
	print("nothing to do\n");
	exit 1;
}

my $total_start_time = time();

my $skip_tlds = opt('continue-tld') ? 1 : 0;

my $config = get_rsm_config();

set_slv_config($config);

my @server_keys;

if (opt('server-id'))
{
	my @keys = get_rsm_server_keys($config);

	foreach my $key (@keys)
	{
		my $id = get_rsm_server_id($key);

		if (getopt('server-id') eq $id)
		{
			push(@server_keys, $key);
			last;
		}
	}

	if (scalar(@server_keys) == 0)
	{
		fail("server id \"" . getopt('server-id') . "\" is unknown");
	}
}
else
{
	@server_keys = get_rsm_server_keys($config);
}

print("WARNING: will copy Probe nodes data of ", $dt_date->ymd(), " to days",
	" from ", $dt_destination_from->ymd(),
	" till ", $dt_destination_till->ymd(),
	" ", (opt('server-id') ? "on " . $server_keys[0] : "on all servers"),
	" overriding any existing data, databases involved:\n");
foreach (@server_keys)
{
	print("    ", $config->{$_}->{'db_name'}, "\n");
}
print("Continue? [Y/n] ");
chomp(my $answer = <STDIN>);

exit(1) unless ($answer eq "" || $answer eq "y" || $answer eq "Y");

my $modified = 0;

my %delays;

my $tld_start_time;

my $prev_tld = "";	# see if we did anything
my $prev_host = "";

foreach (@server_keys)
{
	$server_key = $_;

	db_connect($server_key);

	text_print("collecting $server_key itemids...");

	my $add_cond = (opt('tld') ? " and h.host like '" . getopt('tld') . " %'" : '');

	my $probe_items_ref = db_select(
		"select i.itemid,i.value_type,i.key_,h.host".
		" from hosts h,hosts_groups hg,items i".
		" where h.hostid=hg.hostid".
			" and h.hostid=i.hostid".
			" and hg.groupid=".TLD_PROBE_RESULTS_GROUPID.
			$add_cond.
		" order by h.host,i.key_"
	);

	text_clear();

	dbg(sprintf("found %s probe items", scalar(@{$probe_items_ref})));

	$tld_start_time = time();

	$prev_tld = "";
	$prev_host = "";

	foreach my $row_ref (@{$probe_items_ref})
	{
		my $itemid = $row_ref->[0];
		my $value_type = $row_ref->[1];
		my $key = $row_ref->[2];
		my $host = $row_ref->[3];

		$tld = (split(/ /, $host))[0];

		if (opt('continue-tld') && $skip_tlds == 1)
		{
			$skip_tlds = 0 if (getopt('continue-tld') eq $tld);
		}

		next if ($skip_tlds == 1);

		if (opt('tld'))
		{
			if (!$prev_tld)
			{
				# skip tld not matching specified one
				next if ($tld ne getopt('tld'));
			}
			else
			{
				# stop after processing specified tld
				if ($tld ne getopt('tld'))
				{
					text_clear_num(length("$prev_host "));

					print("$tld ");

					delete_tld_slv_data($tld);

					db_disconnect();

					wrn("Probe statuses were ignored because TLD was specified");

					exit;
				}
			}
		}

		if ($prev_tld && $prev_tld ne $tld)
		{
			text_clear_num(length("$prev_host "));
			print("$prev_tld ");
			delete_tld_slv_data($prev_tld);
		}

		if ($host ne $prev_host)
		{
			text_clear_num(length("$server_key $prev_host ")) if ($prev_host);

			print("$server_key $host ");

			$prev_tld = $tld;
			$prev_host = $host;
		}

		my $service = get_service_from_probe_key($key);

		if (!$service)
		{
			fail("THIS SHOULD NEVER HAPPEN: unknown key $key");
		}

		if (!$delays{$service})
		{
			if ($service eq 'dns-udp')
			{
				$delays{$service} = get_dns_delay($source_from);
			}
			elsif ($service eq 'dns-tcp')
			{
				$delays{$service} = get_dns_delay($source_from);
			}
			elsif ($service eq 'rdds')
			{
				$delays{$service} = get_rdds_delay($source_from);
			}
			elsif ($service eq 'epp')
			{
				$delays{$service} = get_epp_delay($source_from);
			}
			elsif ($service eq 'config')
			{
				$delays{$service} = 60;
			}
			else
			{
				fail("THIS SHOULD NEVER HAPPEN: unknown service $service");
			}
		}

		copy_data($itemid, $value_type, $key);
	}

	if ($prev_tld)
	{
		text_clear_num(length("$prev_host "));
		print("$prev_tld ");
		delete_tld_slv_data($prev_tld);
	}

	# handle probe statuses
	print("$server_key Probe statuses ");

	my $probe_statuses_start_time = time();

	my $rows_ref = db_select(
		"select i.itemid,i.value_type,i.key_".
		" from items i,hosts h".
		" where i.hostid=h.hostid".
			" and i.hostid=100001"	# hostid of "Probe statuses"
	);

	foreach my $row_ref (@{$rows_ref})
	{
		copy_data($row_ref->[0], $row_ref->[1], $row_ref->[2]);
	}

	print("spent ", format_stats_time(time() - $probe_statuses_start_time), "\n");

	db_disconnect();
}

if (opt('tld'))
{
	if ($prev_tld eq "")
	{
		print("TLD \"" . getopt('tld') . "\" not found\n");
		exit 1;
	}

	exit;
}

print("spent in total ", format_stats_time(time() - $total_start_time), "\n");

# returns:
#
# [ [clock, ns, value], [clock, ns, value], ... ]
#
# ordered by timestamp asc
# sub collect_source_data($$$$)
# {
# 	my $itemid = shift;
# 	my $table = shift;
# 	my $from = shift;
# 	my $till = shift;

# 	return db_select(
# 		"select clock,ns,value".
# 		" from $table".
# 		" where itemid=$itemid".
# 			" and " . sql_time_condition($from, $till).
# 		" order by clock asc"
# 	);
# }

sub get_service_from_probe_key($)
{
	my $key = shift;

	# remove possible "rsm."
	$key = substr($key, length("rsm.")) if (substr($key, 0, length("rsm.")) eq "rsm.");

	my $service;

	if (substr($key, 0, length("dns.udp")) eq "dns.udp")
	{
		$service = "dns-udp";
	}
	elsif (substr($key, 0, length("dns.tcp")) eq "dns.tcp")
	{
		$service = "dns-tcp";
	}
	elsif (substr($key, 0, length("dnssec")) eq "dnssec")
	{
		$service = "dns-udp";
	}
	elsif (substr($key, 0, length("rdds")) eq "rdds")
	{
		$service = "rdds";
	}
	elsif (substr($key, 0, length("rdap")) eq "rdap")
	{
		$service = "rdds";
	}
	elsif (substr($key, 0, length("probe")) eq "probe")
	{
		$service = "config";
	}
	elsif (substr($key, -length(".enabled"), length(".enabled")) eq ".enabled")	# match *.enabled
	{
		$service = "config";
	}

	return $service;
}

my $text_len;
sub text_print($)
{
	my $text = shift;

	$text_len = length($text);

	print($text);
}

sub text_clear()
{
	print($BACKSPACE, " ", $BACKSPACE) while ($text_len--);
}

sub text_clear_num($)
{
	my $num = shift;

	print($BACKSPACE, " ", $BACKSPACE) while ($num--);
}

sub exec_sql($)
{
	my $sql = shift;

	if (opt('dry-run'))
	{
		dbg($sql);
		sleep(0.001);
	}
	else
	{
		db_exec($sql);
	}
}

sub delete_tld_slv_data($)
{
	my $tld = shift;

	my $delete_from = $dt_destination_from->epoch();

	my $rows_ref = db_select(
		"select i.itemid".
		" from hosts_groups hg,hosts h,items i".
		" where hg.hostid=h.hostid".
			" and h.hostid=i.hostid".
			" and hg.groupid=".TLDS_GROUPID.
			" and h.host='$tld'"
	);

	my @itemids;

	map {push(@itemids, $_->[0])} (@{$rows_ref});

	my $chunk = 0;

	my $chunks = (($dt_destination_till->epoch() + 1 - $dt_destination_from->epoch()) / $delete_chunk);

	while ($delete_from < $dt_destination_till->epoch())
	{
		text_print(sprintf("deleting SLV data, chunk %4s of %s", ++$chunk, $chunks));

		my $chunk_size = min($dt_destination_till->epoch() - $delete_from, $delete_chunk - 1);

		foreach my $itemid (@itemids)
		{
			foreach my $table ('history', 'history_uint')
			{
				exec_sql(
					"delete from $table".
					" where itemid=$itemid".
						" and clock between $delete_from and $delete_from+$chunk_size"
				);
			}
		}

		text_clear();

		$delete_from += $chunk_size;
	}

	print("spent ", format_stats_time(time() - $tld_start_time), "\n");

	$tld_start_time = time();
}

sub copy_data($$$)
{
	my $itemid = shift;
	my $value_type = shift;
	my $key = shift;

	my $table = history_table($value_type);

	my $days_back_min = 1;
	my $days_back_max = $dt_destination_from->delta_days($dt_date)->{'days'};

	dbg("days_back_max=$days_back_max, days_back_min=$days_back_min");

	my $short_key = sprintf("%.140s", $key);

	my $delete_from = $dt_destination_from->epoch();

	my $chunk = 0;

	my $chunks = (($dt_destination_till->epoch() + 1 - $dt_destination_from->epoch()) / $delete_chunk);

	while ($delete_from < $dt_destination_till->epoch())
	{
		text_print(sprintf("deleting data, chunk %4s of %s of %s", ++$chunk, $chunks, $short_key));

		my $chunk_size = min($dt_destination_till->epoch() - $delete_from, $delete_chunk - 1);

		exec_sql(
			"delete from $table".
			" where itemid=$itemid".
				" and clock between $delete_from and $delete_from+$chunk_size"
		);

		text_clear();

		$delete_from += $chunk_size;
	}

	my $day = 0;

	for (my $days_back = $days_back_min; $days_back <= $days_back_max; $days_back++)
	{
		my $seconds_back = $days_back * 86400;

		my $delete_from = $source_from - $seconds_back;
		my $delete_till = $source_till - $seconds_back;

		dbg(sprintf("copying data of day %2s of %3s for %s (%d)",
			$day + 1, ($days_back_max - $days_back_min + 1), $short_key, $itemid));

		text_print(sprintf("copying data of day %2s of %3s for %s",
			++$day, ($days_back_max - $days_back_min + 1), $short_key));

		if ($value_type == ITEM_VALUE_TYPE_FLOAT)
		{
			exec_sql(
				"insert into $table (itemid,clock,value,ns)".
				" (".
					"select itemid,clock-$seconds_back,if(value < 0, value, floor(value + (rand()*10))),ns".
					" from $table".
					" where itemid=$itemid".
						" and clock between $source_from and $source_till".
				")");
		}
		else
		{
			exec_sql(
				"insert into $table (itemid,clock,value,ns)".
				" (".
					"select itemid,clock-$seconds_back,value,ns".
					" from $table".
					" where itemid=$itemid".
						" and clock between $source_from and $source_till".
				")");
		}

		text_clear();
	}
}

sub min ($$)
{
	$_[$_[0] > $_[1]];
}

__END__

=head1 NAME

fill-data.pl - copy Probe nodes data from specified date to fill current and/or prevous months

=head1 SYNOPSIS

fill-data.pl --date dd/mm/yyyy [--months <n>] [--continue-tld <tld>] [--tld <tld>] [--server-id <ID>] [--delete-chunk <n>] [--dry-run] [--debug] [--help]

=head1 OPTIONS

=over 8

=item B<--date> dd/mm/yyyy

Use the data of specified day for copying. E. g. 14/10/2019 .

=item B<--months> n

Optionally specify number of previous months to fill, in addition to the one specified by --date.
Minimum value 1, maximum 12.

=item B<--continue-tld> tld

If the program was stopped you can run it again, specifying which tld to start from.

=item B<--tld> tld

Optionally specify the only TLD to handle. You don't have to specify --server-id, by default all will be searched for it.

=item B<--server-id> ID

Optionally specify the only server to handle.

=item B<--delete-chunk> n

Optionally specify the chunk size in hours, when deleting history data, by default 24 hours.

=item B<--dry-run>

Print data to the screen, do not change anything in the system.

=item B<--debug>

Run the script in debug mode. This means printing more information.

=item B<--help>

Print a brief help message and exit.

=back

=head1 DESCRIPTION

B<This program> will fill current and/or previous month of data from Probe nodes. If Service
Availability/Downtime/Rolling week data is also needed, the SLV scripts must be run in order to generate it.
When copied, RTT data will be randomized by adding rand(10) to the values. The errors and strings will stay intact.
The source of data is used from the whole day specified with --date.

=head1 EXAMPLES

./fill-data.pl --date 14/10/2019

This will copy Probe nodes data of 14/10/2019 to the October 2019 from days 1 to 13 and delete affected SLV values.

./fill-data.pl --date 14/10/2019 --months 1

This will copy Probe nodes data of 14/10/2019 to the full September 2019 and October 2019 from days 1 to 13
and delete affected SLV values.

=cut
