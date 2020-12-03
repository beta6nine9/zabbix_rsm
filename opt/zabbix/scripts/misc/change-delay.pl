#!/usr/bin/perl

use FindBin;
use lib "$FindBin::RealBin/..";

use strict;
use warnings;

use TLD_constants qw(:api);
use RSM;
use RSMSLV;

set_slv_config(get_rsm_config());

parse_opts('type=i', 'delay=i');
usage() unless (__validate_input() == SUCCESS);

my ($macro, $sql, $sth);
my %macros = (
	1 => '{$RSM.DNS.DELAY}',
	2 => '{$RSM.RDDS.DELAY}',
	3 => '{$RSM.RDAP.DELAY}',
	4 => '{$RSM.EPP.DELAY}',
);

db_connect();

if (getopt('type') == 3 && !is_rdap_standalone())
{
	print("RDAP is not standalone yet\n");
	exit;
}

$macro = $macros{getopt('type')};

if (opt('dry-run'))
{
	print("would set delay ", getopt('delay'), " for macro $macro\n");
	exit;
}

$sql = "update globalmacro set value=? where macro=?";
$sth = $dbh->prepare($sql) or die $dbh->errstr;
$sth->execute(getopt('delay'), $macro) or die $dbh->errstr;

sub __validate_input
{
	return E_FAIL unless (getopt('type') and getopt('delay'));
	return E_FAIL unless (getopt('type') >= 1 and getopt('type') <= 5);
	return E_FAIL unless (getopt('delay') >= 60 and getopt('delay') <= 3600);
	return E_FAIL unless (getopt('delay') % 60 == 0);

	return SUCCESS;
}

__END__

=head1 NAME

change-delay.pl - change delay of a particular service

=head1 SYNOPSIS

change-delay.pl --type <1-4> --delay <60-3600> [--dry-run] [--debug] [--help]

=head1 OPTIONS

=over 8

=item B<--type> number

Specify number of the service: 1 - DNS, 2 - RDDS, 3 - RDAP (if RDAP is standalone), 4 - EPP.

=item B<--delay> number

Specify seconds if delay between tests. Allowed values between 60 and 3600. Use full minutes (e. g. 60, 180, 600).

=item B<--dry-run>

Print data to the screen, do not change anything in the system.

=item B<--debug>

Run the script in debug mode. This means printing more information.

=item B<--help>

Print a brief help message and exit.

=back

=head1 DESCRIPTION

B<This program> will change the delay between particuar test in the system.

=head1 EXAMPLES

./change-delay.pl --type 2 --delay 120

This will set the the delay between RDDS tests to 120 seconds.

=cut
