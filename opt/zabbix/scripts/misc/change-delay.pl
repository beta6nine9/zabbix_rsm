#!/usr/bin/perl

BEGIN
{
	our $MYDIR = $0; $MYDIR =~ s,(.*)/.*,$1,; $MYDIR = '.' if ($MYDIR eq $0);
	our $MYDIR2 = $0; $MYDIR2 =~ s,(.*)/.*/.*,$1,; $MYDIR2 = '..' if ($MYDIR2 eq $0);
}
use lib $MYDIR;
use lib $MYDIR2;

use strict;
use warnings;

use TLD_constants qw(:api);
use RSM;
use RSMSLV;

set_slv_config(get_rsm_config());

parse_opts('type=n', 'delay=n');
usage() unless (__validate_input() == SUCCESS);

my ($key_parts, $macro, $sql);

db_connect();

if (getopt('type') == 1)
{
	$key_parts = ['rsm.dns.udp[%'];
	$macro = '{$RSM.DNS.UDP.DELAY}';
}
elsif (getopt('type') == 2)
{
	$key_parts = ['rsm.dns.tcp[%'];
	$macro = '{$RSM.DNS.TCP.DELAY}';
}
elsif (getopt('type') == 3)
{
	$key_parts = is_rdap_standalone() ? ['rsm.rdds[%'] : ['rsm.rdds[%', 'rdap[%'];
	$macro = '{$RSM.RDDS.DELAY}';
}
elsif (getopt('type') == 4)
{
	$key_parts = ['rsm.epp[%'];
	$macro = '{$RSM.EPP.DELAY}';
}
elsif (getopt('type') == 5)
{
	if (is_rdap_standalone())
	{
		$key_parts = ['rdap[%'];
		$macro = '{$RSM.RDAP.DELAY}';
	}
	else
	{
		print("RDAP is not standalone yet\n");
		exit;
	}
}

if (opt('dry-run'))
{
	print("would set delay ", getopt('delay'), " for items with type ".ITEM_TYPE_SIMPLE." and keys like ", join(" or like ", @{$key_parts}), "\n");
	print("would set macro $macro to ", getopt('delay'), "\n");
	exit;
}

my $sth;

$sql = "update items set delay=? where type=".ITEM_TYPE_SIMPLE." and key_ like ?";

foreach my $key_part (@{$key_parts})
{
	$sth = $dbh->prepare($sql) or die $dbh->errstr;
	$sth->execute(getopt('delay'), $key_part) or die $dbh->errstr;
}

$sql = "update globalmacro set value=? where macro=?";
$sth = $dbh->prepare($sql) or die $dbh->errstr;
$sth->execute(getopt('delay'), $macro) or die $dbh->errstr;

sub __validate_input
{
	return E_FAIL unless (getopt('type') and getopt('delay'));
	return E_FAIL unless (getopt('type') >= 1 and getopt('type') <= 5);
	return E_FAIL unless (getopt('delay') >= 60 and getopt('delay') <= 3600);

	return SUCCESS;
}

__END__

=head1 NAME

change-delay.pl - change delay of a particular service

=head1 SYNOPSIS

change-delay.pl --type <1-5> --delay <60-3600> [--dry-run] [--debug] [--help]

=head1 OPTIONS

=over 8

=item B<--type> number

Specify number of the service: 1 - DNS UDP, 2 - DNS TCP, 3 - RDDS, 4 - EPP, 5 - RDAP (if RDAP is standalone).

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

This will set the delay between DNS TCP tests to 120 seconds.

=cut
