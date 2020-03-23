#!/usr/bin/env perl

use strict;
use warnings;

use Path::Tiny;
use lib path($0)->parent->realpath()->stringify();

use RSM;
use RSMSLV;
use TLD_constants qw(:api);
use Data::Dumper;

use constant MONITORING_TARGET_MACRO => '{$RSM.MONITORING.TARGET}';

use constant ACTION_ID => 130;

sub main()
{
	my $config = get_rsm_config();

	set_slv_config($config);

	parse_opts("get", "set=s");
	check_opts();
	setopt("nolog");

	if (opt("get"))
	{
		get();
	}
	elsif (opt("set"))
	{
		my @server_keys = get_rsm_server_keys($config);
		set(\@server_keys, getopt("set"));
	}

	slv_exit(SUCCESS);
}

sub check_opts()
{
	my $opt_count = 0;

	$opt_count++ if (opt("get"));
	$opt_count++ if (opt("set"));

	if ($opt_count == 0)
	{
		pfail("Missing option: --get, --set");
	}

	if ($opt_count > 1)
	{
		pfail("Only one option may be used: --get, --set");
	}

	if (opt("set"))
	{
		my $monitoring_target = getopt("set");
		if ($monitoring_target ne MONITORING_TARGET_REGISTRY && $monitoring_target ne MONITORING_TARGET_REGISTRAR)
		{
			pfail(
				"Invalid monitoring target '$monitoring_target'. " .
				"Valid targets: ${\MONITORING_TARGET_REGISTRY}, ${\MONITORING_TARGET_REGISTRAR}"
			);
		}
	}
}

sub get()
{
	db_connect();

	my $monitoring_target = db_select_value("select value from globalmacro where macro = ?", [MONITORING_TARGET_MACRO]);
	print("Current monitoring target: '$monitoring_target'\n");

	db_disconnect();
}

sub set($$)
{
	my $server_keys       = shift;
	my $monitoring_target = shift;

	my $action_target = {
		MONITORING_TARGET_REGISTRY , "tld",
		MONITORING_TARGET_REGISTRAR, "registrar",
	}->{$monitoring_target};

	foreach my $server_key (@{$server_keys})
	{
		my $sql;
		my $params;

		db_connect($server_key);

		$sql = "update globalmacro set value = ? where macro = ?";
		$params = [
			$monitoring_target,
			MONITORING_TARGET_MACRO,
		];
		db_exec($sql, $params);

		$sql = "update actions set" .
				" def_shortdata = concat(?, substring(def_shortdata, locate('#', def_shortdata)))," .
				" r_shortdata = concat(?, substring(r_shortdata, locate('#', r_shortdata)))" .
			" where actionid = ?";
		$params = [
			$action_target,
			$action_target,
			ACTION_ID,
		];
		db_exec($sql, $params);

		db_disconnect();
	}
}

sub pfail
{
	print("Error: @_\n");
	slv_exit(E_FAIL);
}

main();

__END__

=head1 NAME

monitoring-target.pl - get/set monitoring target

=head1 SYNOPSIS

standalone-rdap.pl [--get] [--set {registry|registrar}]

=head1 OPTIONS

=over 8

=item B<--get>

Get monitornig target.

=item B<--set> {registry|registrar}

Set monitoring target.

=item B<--debug>

Run the script in debug mode. This means printing more information.

=item B<--help>

Print a brief help message and exit.

=back

=cut
