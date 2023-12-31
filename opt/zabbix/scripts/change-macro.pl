#!/usr/bin/env perl

use FindBin;
use lib $FindBin::RealBin;

use strict;
use warnings;

use RSM;
use RSMSLV;

parse_opts('macro=s', 'value=s');
setopt('nolog');

__usage() unless (opt('macro') && opt('value'));

my $macro = getopt('macro');
my $value = getopt('value');

dbg("macro:$macro value:$value");

my $config = get_rsm_config();

set_slv_config($config);

my @server_keys = get_rsm_server_keys($config);

foreach my $server_key (@server_keys)
{
	if (opt('dry-run'))
	{
		print("would set global macro $macro=$value on central server $server_key\n");
		next;
	}

	db_connect($server_key);

	my $rows_ref = db_select("select value from globalmacro where macro='$macro'");

	fail("cannot find macro '$macro'") unless (1 == scalar(@$rows_ref));

	db_exec("update globalmacro set value='$value' where macro='$macro'");

	db_disconnect();
}

sub __usage
{
	print(join('', @_), "\n") if (@_);
	print("usage: change-macro.pl --macro <macro> --value <value> [--dry-run] [--debug] [--help]\n");
	exit(-1);
}

__END__

=head1 NAME

change-macro.pl - change global macro value on all configured servers

=head1 SYNOPSIS

change-macro.pl --macro <macro> --value <value> [--dry-run] [--debug] [--help]

=head1 OPTIONS

=over 8

=item B<--macro> string

Specify macro to change, e. g. '{$RSM.DNS.PROBE.ONLINE}'.

=item B<--value> string

Specify macro value.

=item B<--dry-run>

Print data to the screen, do not change anything in the system.

=item B<--debug>

Run the script in debug mode. This means printing more information.

=item B<--help>

Print a brief help message and exit.

=back

=head1 DESCRIPTION

B<This program> will change the macro value on all Zabbix servers.

=head1 EXAMPLES

./change-macro.pl --macro '{$RSM.DNS.PROBE.ONLINE}' --value 120

This will set the delay between DNS TCP tests to 120 seconds.

=cut
