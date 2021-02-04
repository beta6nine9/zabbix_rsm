#!/usr/bin/env perl

use FindBin;
use lib "$FindBin::RealBin/..";

use strict;
use warnings;

use TLD_constants qw(:api);
use RSM;
use RSMSLV;

parse_opts();

setopt('nolog');
setopt('dry-run');

set_slv_config(get_rsm_config());

db_connect();

my $result = __get_probe_macros();

foreach my $probe (keys(%{$result}))
{
	print($probe, "\n-------------------------\n");

	foreach my $macro (keys(%{$result->{$probe}}))
	{
		print("  $macro\t: ", $result->{$probe}->{$macro}, "\n");
	}
}

sub __get_probe_macros
{
	my $rows_ref = db_select(
		"select host".
		" from hosts".
		" where status=".HOST_STATUS_PROXY_PASSIVE);

	my $result;

	foreach my $row_ref (@$rows_ref)
	{
		my $host = $row_ref->[0];

		my $rows_ref2 = db_select(
			"select hm.macro,hm.value".
			" from hosts h,hostmacro hm".
			" where h.hostid=hm.hostid".
				" and h.host='Template $host'");

		foreach my $row_ref2 (@$rows_ref2)
		{
			$result->{$host}->{$row_ref2->[0]} = $row_ref2->[1];
		}
	}

	return $result;
}
