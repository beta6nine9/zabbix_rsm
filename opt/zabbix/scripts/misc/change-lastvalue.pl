#!/usr/bin/perl

use strict;
use warnings;

use Data::Dumper;

use lib '/opt/zabbix/scripts';

use RSM;
use RSMSLV;
use TLD_constants qw(:api :groups);

use constant EVENT_OBJECT_TRIGGER => 0;
use constant EVENT_SOURCE_TRIGGERS => 0;

sub __table($);
sub __get_tlds($$);
sub __delete_events($$);

parse_opts('dns-clock=n', 'rdds-clock=n', 'tld=s', 'force!');

setopt('nolog');

usage() unless (opt('dns-clock') && opt('rdds-clock'));

my $config = get_rsm_config();

set_slv_config($config);

db_connect();

my $rows_ref = db_select(
	"select distinct key_".
	" from items".
	" where key_ like 'rsm.slv.%'"
);

my %keys;

foreach my $row_ref (@{$rows_ref})
{
	my $key = $row_ref->[0];

	my $service = (split(/\./, $key))[2];

	$keys{$service}->{$key} = undef;	# set default value for --force option later
}

my %clocks;
my %delays;

foreach my $service (keys(%keys))
{
	if ($service eq 'dns')
	{
		$clocks{$service} = getopt('dns-clock');
		$delays{$service} = get_dns_udp_delay($clocks{$service});
	}
	elsif ($service eq 'dnssec')
	{
		$clocks{$service} = getopt('dns-clock');
		$delays{$service} = get_dns_udp_delay($clocks{$service});
	}
	elsif ($service eq 'rdds')
	{
		$clocks{$service} = getopt('rdds-clock');
		$delays{$service} = get_rdds_delay($clocks{'rdds'});
	}
	elsif ($service eq 'rdap')
	{
		wrn("RDAP service is not implemented");
	}
	else
	{
		fail("unknown service \"$service\"");
	}

	# now that we know delay, adjust the clock
	$clocks{$service} = cycle_start($clocks{$service}, $delays{$service});

	info(uc($service), "\t: ", scalar(localtime($clocks{$service})));
}

if (opt('force'))
{
	# for --force option, set default values for previous cycle, if the value is not available
	foreach my $keys (values(%keys))
	{
		foreach my $key (keys(%{$keys}))
		{
			if ($key =~ /\.avail/)		# for name servers these will be .avail[...]
			{
				$keys->{$key} = 1;
			}
			elsif ($key =~ /\.downtime/)	# for name servers these will be .downtime[...]
			{
				$keys->{$key} = 0;
			}
			elsif ($key =~ /\.rollweek$/)
			{
				$keys->{$key} = 0.0;
			}
			elsif ($key =~ /\.performed$/)
			{
				$keys->{$key} = 0;
			}
			elsif ($key =~ /\.pfailed$/)
			{
				$keys->{$key} = 0;
			}
			elsif ($key =~ /\.failed$/)
			{
				$keys->{$key} = 0;
			}
			else
			{
				fail("unknown SLV item key \"$key\"");
			}
		}
	}
}

db_disconnect();

my @server_keys = get_rsm_server_keys($config);

foreach (@server_keys)
{
	$server_key = $_;

	db_connect($server_key);

	tld_interface_enabled_delete_cache();   # delete cache of previous server_key
        tld_interface_enabled_create_cache($clocks{'dns'}, ('dns', 'dnssec', 'rdds43', 'rdds80', 'rdap'));

	foreach my $service (keys(%keys))
	{
		my $tlds = __get_tlds($service, $clocks{$service});

		next if (scalar(keys(%{$tlds})) == 0);

		foreach my $hostid (keys(%{$tlds}))
		{
			$tld = $tlds->{$hostid};

			my %itemids;

			foreach my $key (keys(%{$keys{$service}}))
			{
				my $rows_ref = db_select("select itemid,value_type from items where key_='$key' and hostid=$hostid");

				map {
					$itemids{$_->[0]}->{'value_type'} = $_->[1];
					$itemids{$_->[0]}->{'default_value'} = $keys{$service}->{$key};
				} (@{$rows_ref});
			}

			my $clock_end = cycle_end($clocks{$service}, $delays{$service});

			foreach my $itemid (keys(%itemids))
			{
				my $value_type = $itemids{$itemid}->{'value_type'};
				my $default_value = $itemids{$itemid}->{'default_value'};

				my $rows_ref = db_select(
					"select clock,value".
					" from " . __table($value_type).
					" where itemid=$itemid".
						" and " . sql_time_condition($clocks{$service}, $clock_end)
				);

				if (opt('dry-run'))
				{
					if (scalar(@{$rows_ref}) == 0)
					{
						wrn("$service item ($itemid) does not have calculated cycle result at ", ts_full($clocks{$service}));
					}

					next;
				}

				# the last available value will be set in lastvalue table
				my $value;

				if (scalar(@{$rows_ref}) == 0)
				{
					if (opt('force'))
					{
						# set the value for lastvalue table
						$value = $default_value;

						my $clock = $clocks{$service} - $delays{$service};

						db_exec("delete from lastvalue where itemid=$itemid");

						db_exec(
							"insert into lastvalue (itemid,value,clock)".
							" values ($itemid,$default_value,$clock)");
					}
					else
					{
						# set lastvalue
						db_exec("update lastvalue set clock=" . ($clocks{$service} - $delays{$service}) . " where itemid=$itemid");

						my $r = db_select("select key_ from items where itemid=$itemid");

						fail("$service item ($itemid) is missing calculated cycle result at ", ts_full($clocks{$service}),
							"\n\nrun: /opt/zabbix/scripts/slv/", $r->[0]->[0], ".pl --tld $tld --now ", $clocks{$service}, " --debug\n\nand run this script again");
					}
				}
				elsif (scalar(@{$rows_ref}) != 1)
				{
					# set the value for lastvalue table
					$value = $rows_ref->[0]->[1];

					db_exec("delete from " . __table($value_type) . " where itemid=$itemid and " . sql_time_condition($clocks{$service}, $clock_end));

					db_exec("insert into " . __table($value_type) . " (`itemid`,`clock`,`value`) values ($itemid," . $clocks{$service} . ",$value)");
				}
				else
				{
					if ($rows_ref->[0]->[0] != $clocks{$service})
					{
						wrn("fixing $service history value of item $itemid...");

						db_exec("update ". __table($value_type) . " set itemid=$itemid,clock=" . $clocks{$service} . " where itemid=$itemid and clock=" . $rows_ref->[0]->[0]);
					}
				}

				# delete everything further
				db_exec("delete from " . __table($value_type) . " where itemid=$itemid and clock>" . $clocks{$service});

				# set lastvalue

				db_exec("insert ignore into lastvalue (itemid,clock,value)".
					" values ($itemid," . $clocks{$service} . ",$value)".
					" on duplicate key update value=$value,clock=" . $clocks{$service});

				# delete events
				__delete_events($itemid, $clocks{$service});
			}
		}
	}

#	last if (opt('tld'));
}

sub __table($)
{
	my $value_type = shift;

	fail("THIS_SHOULD_NEVER_HAPPEN") unless (defined($value_type));

	return "history_uint" if ($value_type == ITEM_VALUE_TYPE_UINT64);
	return "history" if ($value_type == ITEM_VALUE_TYPE_FLOAT);

	fail("THIS_SHOULD_NEVER_HAPPEN");
}

sub __get_tlds($$)
{
	my $service = shift;
	my $now = shift;

	my $host_cond = "";

	$host_cond = " and h.host='" . getopt('tld') . "'" if (opt('tld'));

	my $rows_ref = db_select(
		"select distinct h.host,h.hostid".
		" from hosts h,hosts_groups hg".
		" where h.hostid=hg.hostid".
			" and hg.groupid=".TLDS_GROUPID.
			" and h.status=0".
			$host_cond.
		" order by h.host");

	my %tlds;
	foreach my $row_ref (@$rows_ref)
	{
		next unless (tld_service_enabled($row_ref->[0], $service, $now));

		$tlds{$row_ref->[1]} = $row_ref->[0];
	}

	return \%tlds;
}

sub __delete_events($$)
{
	my $itemid = shift;
	my $clock = shift;

	my $rows_ref = db_select(
		"select distinct t.triggerid".
		" from triggers t,functions f".
		" where t.triggerid=f.triggerid".
			" and f.itemid=$itemid"
	);

	foreach my $row_ref (@{$rows_ref})
	{
		my $triggerid = $row_ref->[0];

		db_exec(
			"delete from events".
			" where object=".EVENT_OBJECT_TRIGGER.
				" and source=".EVENT_SOURCE_TRIGGERS.
				" and objectid=$triggerid".
				" and clock>$clock"
		);
	}
}

__END__

=head1 NAME

change-lastvalue.pl - delete all SLV data points since specified timestamp

=head1 SYNOPSIS

change-lastvalue.pl <--dns-clock clock> <--rdds-clock clock> [--tld <tld>] [--force] [--dry-run] [--debug] [--help]

=head1 OPTIONS

=head2 REQUIRED OPTIONS

=over 8

=item B<--dns-clock> clock

Specify the timestamp of the DNS cycle to delete data from (including).

=item B<--rdds-clock> clock

Same as --dns-clock, but for RDDS.

=item B<--tld> tld

Optionally specify single TLD to handle.

=item B<--force>

Optionally choose to ignore the case when the value prior to specified time is not available and use defaults for the
lastvalue table.

=item B<--dry-run>

Print data to the screen, do not write anything to the filesystem.

=item B<--debug>

Run the script in debug mode. This means printing more information.

=item B<--help>

Print a brief help message and exit.

=back

=head1 DESCRIPTION

B<This program> was created to fix gaps in SLV values somehow appeared after one of the upgrades. It attempts to go back
and delete all data points generated by SLV scripts since specified DNS and RDDS clock (full cycle). This also affects
"lastvalue" table, where "lastclock" would be set to the timestamp of the previous cycle, considering that it was the
last one that had correct calculations.

=head1 EXAMPLES

./change-lastvalue.pl --dns-clock $(date +%s -d '2019-10-25') --rdds-clock $(date +%s -d '2019-10-25')

This will delete all SLV data since 25th of October 2019 (including) and attempt to set lastvalues in the lastvalue
table to the previous cycle.

=cut
