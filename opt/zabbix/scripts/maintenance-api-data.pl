#!/usr/bin/env perl

use FindBin;
use lib $FindBin::RealBin;

use strict;
use warnings;

use ApiHelper;
use Getopt::Long;
use Pod::Usage;
use Path::Tiny qw(path);
use RSM;
use RSMSLV; # required for ApiHelper

parse_opts('ignore-file=s');

setopt('nolog');

my %ignore;

if (opt('ignore-file'))
{
	my ($buf, $error);

	if (! -f getopt('ignore-file'))
	{
		fail(getopt('ignore-file') . ": this file does not exist or is not a file");
	}

	if (read_file(getopt('ignore-file'), \$buf, \$error) != SUCCESS)
	{
		fail("error reading \"" . getopt('ignore-file') . "\": $error");
	}

	map {$ignore{$_} = 1;} (split('\n', $buf));
}

my $error = rsm_targets_prepare(AH_SLA_API_TMP_DIR, AH_SLA_API_DIR);

fail($error) if ($error);

foreach my $version (AH_SLA_API_VERSION_1, AH_SLA_API_VERSION_2)
{
	foreach my $tld_dir (path(AH_SLA_API_DIR . "/v$version")->children)
	{
		next unless ($tld_dir->is_dir());

		my $tld = $tld_dir->basename();

		dbg("tld=[$tld]");

		next if (exists($ignore{$tld}));

		my $json;

		print("cannot read \"$tld\" state: ", ah_get_error())
			unless (ah_read_state($version, $tld, \$json) == AH_SUCCESS);

		$json->{'status'} = 'Up-inconclusive';
		$json->{'testedServices'} = {
			'DNS'		=> JSON_OBJECT_DISABLED_SERVICE,
			'DNSSEC'	=> JSON_OBJECT_DISABLED_SERVICE,
			'EPP'		=> JSON_OBJECT_DISABLED_SERVICE,
			'RDDS'		=> JSON_OBJECT_DISABLED_SERVICE,
		};

		fail("cannot set \"$tld\" state: ", ah_get_error())
			unless (ah_save_state($version, $tld, $json) == AH_SUCCESS);
	}
}

$error = rsm_targets_apply();

fail($error) if ($error);

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
