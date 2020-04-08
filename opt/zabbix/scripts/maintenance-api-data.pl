#!/usr/bin/perl

BEGIN
{
	our $MYDIR = $0; $MYDIR =~ s,(.*)/.*,$1,; $MYDIR = '.' if ($MYDIR eq $0);
}
use lib $MYDIR;

use strict;
use warnings;

use Path::Tiny qw(path);
use ApiHelper;
use Getopt::Long;
use Pod::Usage;
use RSM;
use RSMSLV; # required for ApiHelper

my %OPTS;

if (!GetOptions(\%OPTS, 'help!', 'ignore-file=s'))
{
	pod2usage(-verbose => 0);
}

if ($OPTS{'help'})
{
	pod2usage(-verbose => 1);
}

my %ignore;

if ($OPTS{'ignore-file'})
{
	my ($buf, $error);

	if (! -f $OPTS{'ignore-file'})
	{
		print("$OPTS{'ignore-file'}: this file does not exist or is not a file\n");
		exit 1;
	}

	if (read_file($OPTS{'ignore-file'}, \$buf, \$error) != SUCCESS)
	{
		print("Error reading $OPTS{'ignore-file'}: $error\n");
		exit 1;
	}

	map {$ignore{$_} = 1;} (split('\n', $buf));
}

my $error = rsm_targets_prepare(AH_SLA_API_TMP_DIR, AH_SLA_API_DIR);

die($error) if ($error);

foreach my $tld_dir (path(AH_SLA_API_DIR)->children)
{
	next unless ($tld_dir->is_dir());

	my $tld = $tld_dir->basename();

	next if (exists($ignore{$tld}));

	my $json;

	die("cannot read \"$tld\" state: ", ah_get_error()) unless (ah_state_file_json($tld, \$json) == AH_SUCCESS);

	$json->{'status'} = 'Up-inconclusive';
	$json->{'testedServices'} = {
		'DNS'		=> JSON_OBJECT_DISABLED_SERVICE,
		'DNSSEC'	=> JSON_OBJECT_DISABLED_SERVICE,
		'EPP'		=> JSON_OBJECT_DISABLED_SERVICE,
		'RDDS'		=> JSON_OBJECT_DISABLED_SERVICE
	};

	die("cannot set \"$tld\" state: ", ah_get_error()) unless (ah_save_state($tld, $json) == AH_SUCCESS);
}

$error = rsm_targets_apply();

die($error) if ($error);

__END__

=head1 NAME

maintenance-api-data.pl - set status of all TLDs in SLA API to "maintenance"

=head1 SYNOPSIS

maintenance-api-data.pl.pl [--ignore-file <file>] [--help]

=head1 OPTIONS

=over 8

=item B<--ignore-file> file

Optionally specify text file with list of new-line separated TLDs (directories under /opt/zabbix/sla) to ignore.

=item B<--help>

Print a brief help message and exit.

=back

=head1 DESCRIPTION

B<This program> will set status of all TLDs in SLA API to "maintenance".

=head1 EXAMPLES

./maintenance-api-data.pl

=cut
