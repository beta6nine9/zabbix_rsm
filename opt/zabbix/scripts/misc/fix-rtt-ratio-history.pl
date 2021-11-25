#!/usr/bin/env perl

use FindBin;
use lib "$FindBin::RealBin/..";

use strict;
use warnings;

use RSM;
use RSMSLV;
use TLD_constants qw(:api :groups);
use Data::Dumper;
use DateTime;
use List::Util qw(min);

sub main()
{
	my $server_id;
	my $service;
	my $check;
	my $fix;

	parse_cli_opts(\$server_id, \$service, \$check, \$fix);

	set_slv_config(get_rsm_config());

	db_connect($server_id ? get_rsm_server_key($server_id) : undef);

	my $rows = get_corruptions($service);

	check($service, $rows) if ($check);
	fix($service, $rows)   if ($fix);

	db_disconnect();

	slv_exit(0);
}

sub check($)
{
	my $service = shift;
	my $rows    = shift;

	my $format = "| %-24s | %-32s | %-32s | %-32s |\n";
	my $line = '+' . '-' x 26 . '+' . '-' x 34 . '+' . '-' x 34 . '+' . '-' x 34 . '+' . "\n";

	print($line);
	printf($format, 'host', "rsm.slv.${service}.rtt.performed", "rsm.slv.${service}.rtt.failed", "rsm.slv.${service}.rtt.pfailed");
	print($line);

	foreach my $row (@{$rows})
	{
		my ($host, undef, undef, undef, $performed_clock, $failed_clock, $pfailed_clock) = @{$row};

		$performed_clock = defined($performed_clock) ? __ts_full($performed_clock) : 'null';
		$pfailed_clock   = defined($pfailed_clock)   ? __ts_full($pfailed_clock)   : 'null';
		$failed_clock    = defined($failed_clock)    ? __ts_full($failed_clock)    : 'null';

		printf($format, $host, $performed_clock, $failed_clock, $pfailed_clock);
	}

	print($line);
}

sub fix($)
{
	my $service = shift;
	my $rows    = shift;

	foreach my $row (@{$rows})
	{
		my ($host, $performed_itemid, $failed_itemid, $pfailed_itemid, $performed_clock, $failed_clock, $pfailed_clock) = @{$row};

		info("processing host: $host");
		info("rsm.slv.${service}.rtt.performed - $performed_itemid" . ", " . (defined($performed_clock) ? __ts_full($performed_clock) : 'null'));
		info("rsm.slv.${service}.rtt.failed    - $failed_itemid"    . ", " . (defined($failed_clock   ) ? __ts_full($failed_clock   ) : 'null'));
		info("rsm.slv.${service}.rtt.pfailed   - $pfailed_itemid"   . ", " . (defined($pfailed_clock  ) ? __ts_full($pfailed_clock  ) : 'null'));

		$dbh->begin_work() or fail($dbh->errstr);

		if (!defined($performed_clock) || !defined($failed_clock) || !defined($pfailed_clock))
		{
			info("history is completely missing for one or more items");

			info("deleting history of corrupted items...");
			db_exec("delete from history_uint where itemid = ?", [$performed_itemid]);
			db_exec("delete from history_uint where itemid = ?", [$failed_itemid]);
			db_exec("delete from history      where itemid = ?", [$pfailed_itemid]);

			info("deleting lastvalue of corrupted items...");
			db_exec("delete from lastvalue where itemid = ?", [$performed_itemid]);
			db_exec("delete from lastvalue where itemid = ?", [$failed_itemid]);
			db_exec("delete from lastvalue where itemid = ?", [$pfailed_itemid]);
		}
		else
		{
			info("missing history entries for one or more items");

			my $min_clock = min($performed_clock, $failed_clock, $pfailed_clock);

			info("deleting history of corrupted items that is newer than " . __ts_full($min_clock) . "...");
			db_exec("delete from history_uint where itemid = ? and clock > ?", [$performed_itemid, $min_clock]);
			db_exec("delete from history_uint where itemid = ? and clock > ?", [$failed_itemid   , $min_clock]);
			db_exec("delete from history      where itemid = ? and clock > ?", [$pfailed_itemid  , $min_clock]);

			info("updating lastvalue of corrupted items...");

			my $sql;

			$sql = __format_sql("
				update
					lastvalue
					inner join history_uint on history_uint.itemid = lastvalue.itemid
				set
					lastvalue.clock = history_uint.clock,
					lastvalue.value = history_uint.value
				where
					history_uint.itemid = ? and
					history_uint.clock = ?
			");

			db_exec($sql, [$performed_itemid, $min_clock]);
			db_exec($sql, [$failed_itemid   , $min_clock]);

			$sql = __format_sql("
				update
					lastvalue
					inner join history on history.itemid = lastvalue.itemid
				set
					lastvalue.clock = history.clock,
					lastvalue.value = history.value
				where
					history.itemid = ? and
					history.clock = ?
			");

			db_exec($sql, [$pfailed_itemid  , $min_clock]);
		}

		$dbh->commit() or fail($dbh->errstr);

		info("finished processing host: $host");
	}
}

sub get_corruptions($)
{
	my $service = shift;

	my $sql = __format_sql("
		select
			hosts.host,
			items_rtt_performed.itemid,
			items_rtt_failed.itemid,
			items_rtt_pfailed.itemid,
			lastvalue_rtt_performed.clock,
			lastvalue_rtt_failed.clock,
			lastvalue_rtt_pfailed.clock
		from
			hosts
			inner join hosts_groups on hosts_groups.hostid = hosts.hostid
			inner join hstgrp on hstgrp.groupid = hosts_groups.groupid
			left join items as items_rtt_performed on
				items_rtt_performed.hostid = hosts.hostid and
				items_rtt_performed.key_ like ?
			left join items as items_rtt_failed on
				items_rtt_failed.hostid = hosts.hostid and
				items_rtt_failed.key_ like ?
			left join items as items_rtt_pfailed on
				items_rtt_pfailed.hostid = hosts.hostid and
				items_rtt_pfailed.key_ like ?
			left join lastvalue as lastvalue_rtt_performed on
				lastvalue_rtt_performed.itemid = items_rtt_performed.itemid
			left join lastvalue as lastvalue_rtt_failed on
				lastvalue_rtt_failed.itemid = items_rtt_failed.itemid
			left join lastvalue as lastvalue_rtt_pfailed on
				lastvalue_rtt_pfailed.itemid = items_rtt_pfailed.itemid
		where
			hstgrp.name = 'TLDs' and
			coalesce(lastvalue_rtt_performed.clock, lastvalue_rtt_failed.clock, lastvalue_rtt_pfailed.clock) is not null and
			(
				lastvalue_rtt_performed.clock is null or
				lastvalue_rtt_failed.clock    is null or
				lastvalue_rtt_pfailed.clock   is null or
				lastvalue_rtt_performed.clock <> lastvalue_rtt_failed.clock or
				lastvalue_rtt_performed.clock <> lastvalue_rtt_pfailed.clock or
				lastvalue_rtt_failed.clock    <> lastvalue_rtt_pfailed.clock
			)
		order by
			hosts.host
	");

	my $params = [
		"rsm.slv.${service}.rtt.performed",
		"rsm.slv.${service}.rtt.failed",
		"rsm.slv.${service}.rtt.pfailed",
	];

	return db_select($sql, $params);
}

sub parse_cli_opts($$$$)
{
	my $server_id = shift;
	my $service   = shift;
	my $check     = shift;
	my $fix       = shift;

	setopt('nolog');

	parse_opts(
		'server-id=s' => $server_id,
		'service=s'   => $service,
		'check'       => $check,
		'fix'         => $fix,
	);

	if (!$$service)
	{
		usage('missing option: --service');
	}
	if ($$service ne 'dns.udp' && $$service ne 'dns.tcp' && $$service ne 'rdds' && $$service ne 'rdap')
	{
		usage('invalid service: ' . $$service);
	}
	if (!$$check && !$$fix)
	{
		usage('missing option: --check, --fix');
	}
	if ($$check && $$fix)
	{
		usage('only one of the following options can be used: --check, --fix');
	}
}

sub __format_sql($)
{
	my $sql = shift;

	$sql =~ s/\n\t+/ /g;
	$sql =~ s/  +/ /g;
	$sql =~ s/\( +/(/g;
	$sql =~ s/ +\)/)/g;
	$sql =~ s/^ +| +$//g;

	return $sql;
}

sub __ts_full($)
{
	my $clock = shift;

	return sprintf("%s (%d)", DateTime->from_epoch('epoch' => $clock) =~ s/T/ /r, $clock);
}

main();

__END__

=head1 NAME

fix-rtt-ratio-history.pl - check and fix history of RTT ratio items.
History may get corrupted due to "duplicate key" SQL errors in Zabbix server.
It is assumed that this issue will not be present in Zabbix 5.

=head1 SYNOPSIS

fix-rtt-ratio-history.pl [--server-id <server-id>] --service <service> --check|--fix [--debug] [--help]

=head1 OPTIONS

=head2 REQUIRED OPTIONS

=over 8

=item B<--service <service>>

Specify service - dns.udp, dns.tcp, rdds, rdap.

=item B<--check>

Check history of RTT ratio items.

=item B<--fix>

Fix history of RTT ratio items.

=head2 OPTIONAL OPTIONS

=over 8

=item B<--server-id <server-id>>

Specify the server ID to query the data from.

=item B<--debug>

Produce insane amount of debug messages.

=item B<--help>

Display this help and exit.

=cut
