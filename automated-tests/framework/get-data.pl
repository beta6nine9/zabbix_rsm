#!/usr/bin/env perl

use strict;
use warnings;

use FindBin;
use lib "$FindBin::RealBin";

use Data::Dumper;
use DateTime;
use List::Util qw(min max);
use Memoize;

use Output;
use Options;
use Database;

# sort hashes in Data::Dumper output by keys
$Data::Dumper::Sortkeys = sub
{
	my @keys = keys(%{$_[0]});

	if (grep { $_ !~ /^\d+$/ } @keys)
	{
		return [sort(@keys)];
	}
	else
	{
		return [sort { $a <=> $b } @keys];
	}
};

# cache return values of functions
memoize('get_global_macro');

# flush after every print
$| = 1;

################################################################################
# main
################################################################################

sub main()
{
	parse_opts('db-host=s', 'db-name=s', 'db-user=s', 'db-password=s', 'rsmhost=s@', 'clock=s', 'minutes=i', 'debug', 'help');

	usage('--db-host is not specified'    , 1) if (!opt('db-host'));
	usage('--db-name is not specified'    , 1) if (!opt('db-name'));
	usage('--db-user is not specified'    , 1) if (!opt('db-user'));
	usage('--db-password is not specified', 1) if (!opt('db-password'));
	usage('--rsmhost is not specified'    , 1) if (!opt('rsmhost'));
	usage('--clock is not specified'      , 1) if (!opt('clock'));
	usage('--minutes is not specified'    , 1) if (!opt('minutes'));

	my $from = parse_time_str(getopt('clock'));
	my $till = $from + getopt('minutes') * 60 - 1;
	my $data = {};

	local $ENV{'ZBX_SERVER_DB_HOST'}     = getopt('db-host');
	local $ENV{'ZBX_SERVER_DB_NAME'}     = getopt('db-name');
	local $ENV{'ZBX_SERVER_DB_USER'}     = getopt('db-user');
	local $ENV{'ZBX_SERVER_DB_PASSWORD'} = getopt('db-password');

	db_connect();
	db_exec("use $ENV{'ZBX_SERVER_DB_NAME'}");

	get_items($data, getopt('rsmhost'));
	get_history($data, $from, $till);

	db_disconnect();

	print_history($data);
}

sub get_items($$)
{
	my $data     = shift;
	my $rsmhosts = shift;

	my $host_filter = "";
	my @host_params = ();

	$host_filter .= "hosts.host = 'Global macro history'";
	$host_filter .= " or hosts.host = 'Probe statuses'";
	$host_filter .= " or hstgrp.name = 'Template Probe Status'";
	$host_filter .= " or (hstgrp.name = 'Probes - Mon' and hosts.host like '% - mon')";

	foreach my $rsmhost (@{$rsmhosts})
	{
		$host_filter .= " or (hstgrp.name = 'TLDs' and hosts.host = ?)";
		$host_filter .= " or (hstgrp.name = 'TLD Probe results' and hosts.host like ?)";
		push(@host_params, $rsmhost);
		push(@host_params, "$rsmhost %");
	}

	my $sql = "select
			hosts.host,
			items.key_,
			items.delay,
			items.itemid,
			items.value_type
		from
			hosts
			inner join hosts_groups on hosts_groups.hostid = hosts.hostid
			inner join hstgrp       on hstgrp.groupid      = hosts_groups.groupid
			inner join items        on items.hostid        = hosts.hostid
		where
			$host_filter
		";

	$sql =~ s/\s+/ /g;
	$sql =~ s/^\s+//g;
	$sql =~ s/\s+$//g;

	my $rows = db_select($sql, [@host_params]);

	foreach my $row (@{$rows})
	{
		my ($host, $key, $delay, $itemid, $value_type) = @{$row};

		if ($delay eq '1m')
		{
			$delay = '60';
		}
		elsif ($delay eq '5m')
		{
			$delay = '300';
		}
		elsif ($delay =~ /^\{\$.+\}$/)
		{
			$delay = get_global_macro($delay);
		}
		elsif ($delay == 0)
		{
			$delay = $key =~ /rdds|rdap/ ? 300 : 60;
		}

		my $history_table;
		$history_table = 'history'      if ($value_type == 0); # ITEM_VALUE_TYPE_FLOAT
		$history_table = 'history_str'  if ($value_type == 1); # ITEM_VALUE_TYPE_STR
		$history_table = 'history_log'  if ($value_type == 2); # ITEM_VALUE_TYPE_LOG
		$history_table = 'history_uint' if ($value_type == 3); # ITEM_VALUE_TYPE_UINT64
		$history_table = 'history_text' if ($value_type == 4); # ITEM_VALUE_TYPE_TEXT

		$data->{$host}{$key} = {
			'delay'         => $delay,
			'itemid'        => $itemid,
			'history_table' => $history_table,
			'history'       => {},
		};
	}
}

sub get_history($$$)
{
	my $data = shift;
	my $from = shift;
	my $till = shift;

	foreach my $host (sort(keys(%{$data})))
	{
		foreach my $key (sort(keys(%{$data->{$host}})))
		{
			print(".");

			my $item = $data->{$host}{$key};

			my $itemid  = $item->{'itemid'};
			my $table   = $item->{'history_table'};
			my $history = $item->{'history'};

			my $sql = "select clock, value from $table where itemid = ? and clock between ? and ?";
			my $params = [$itemid, $from, $till];

			my $rows = db_select($sql, $params);

			foreach my $row (@{$rows})
			{
				my ($clock, $value) = @{$row};

				$history->{$clock} = $value;
			}
		}
		print("\n");
	}
}

sub print_history($)
{
	my $data = shift;

	foreach my $host (sort(keys(%{$data})))
	{
		my $has_data = 0;

		foreach my $key (sort(keys(%{$data->{$host}})))
		{
			my $item = $data->{$host}{$key};

			if (!%{$item->{'history'}})
			{
				dbg("host '%s', item '%s' - history not found", $host, $key);
				next;
			}

			$has_data = 1;

			my $delay = $item->{'delay'};
			my $min_clock = min(keys(%{$item->{'history'}}));
			my $max_clock = max(keys(%{$item->{'history'}}));

			my @values = ();

			push(@values, "\"$host\"");
			push(@values, "\"$key\"");
			push(@values, $delay);
			push(@values, $min_clock);

			for (my $clock = $min_clock; $clock <= $max_clock; $clock += $delay)
			{
				my $value = delete($item->{'history'}{$clock});

				$value = delete($item->{'history'}{$clock + 1}) if (!defined($value));
				$value = delete($item->{'history'}{$clock + 2}) if (!defined($value));
				$value = delete($item->{'history'}{$clock + 3}) if (!defined($value));

				$value = delete($item->{'history'}{$clock - 1}) if (!defined($value) && ($clock % 60 >= 1));
				$value = delete($item->{'history'}{$clock - 2}) if (!defined($value) && ($clock % 60 >= 2));
				$value = delete($item->{'history'}{$clock - 3}) if (!defined($value) && ($clock % 60 >= 3));

				push(@values, $value // '');
			}

			print(join(",", @values) . "\n");

			# print out values with "invalid" clock

			if (%{$item->{'history'}})
			{
				print("-" x 80 . "\n");
				print("min clock - $min_clock\n");
				print("max clock - $max_clock\n");
				print("delay     - $delay\n");
				print(Dumper($data->{$host}{$key}{'history'}));
				print("-" x 80 . "\n");
			}
		}

		if ($has_data)
		{
			print("\n");
		}
	}
}

sub parse_time_str($)
{
	my $str = shift;

	if (my @matches = ($str =~ /^(\d\d\d\d)-(\d\d)-(\d\d) (\d\d):(\d\d):(\d\d)$/))
	{
		my ($year, $month, $date, $hour, $minute, $second) = @matches;

		return DateTime->new('year' => $year, 'month' => $month, 'day' => $date, 'hour' => $hour, 'minute' => $minute, 'second' => $second)->epoch();
	}

	if (my @matches = ($str =~ /^(\d+)$/))
	{
		return $matches[0];
	}

	fail("invalid format of timestamp: '$str'");
}

sub get_global_macro($)
{
	my $macro = shift;

	return db_select_value("select value from globalmacro where macro=?", [$macro]);
}

################################################################################
# end of script
################################################################################

main();

__END__

=head1 NAME

get-data.pl - get data from DB and format it for the test case file.

=head1 SYNOPSIS

test.pl --db-host <host> --db-name <name> --db-user <user> --db-password <password> --rsmhost <rsmhost> --clock <timestamp> --minutes <minutes> [--debug] [--help]

=head1 OPTIONS

=over 8

=item B<--db-host> string

Specify DB host.

=item B<--db-name> string

Specify DB name.

=item B<--db-user> string

Specify DB user.

=item B<--db-password> string

Specify DB password.

=item B<--rsmhost> string

Specify RSMHOST. This option can be used multiple times.

=item B<--clock> string|int

Specify beginning of the period as UNIX timestamp or in "yyyy-mm-dd hh:mm:ss" format.

=item B<--minutes> int

Specify number of minutes.

=item B<--debug>

Run the script in debug mode. This means printing more information.

=item B<--help>

Print a brief help message and exit.

=back

=cut
