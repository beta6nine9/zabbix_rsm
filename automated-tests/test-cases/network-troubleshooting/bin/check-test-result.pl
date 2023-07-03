#!/usr/bin/perl

use strict;
use warnings;

use Archive::Tar;
use Data::Dumper;
use Getopt::Long qw(GetOptionsFromArray);
use IO::Uncompress::Gunzip qw(gunzip $GunzipError);
use JSON::XS qw(decode_json);
use Pod::Usage;
use Text::Diff;

################################################################################
# main
################################################################################

sub main()
{
	parse_opts();

	usage("--archive is missing"  , 1) if (!opt("archive"));
	usage("--timestamp is missing", 1) if (!opt("timestamp"));
	usage("--proxy is missing"    , 1) if (!opt("proxy"));
	usage("--rsmhosts is missing" , 1) if (!opt("rsmhosts"));
	usage("--resolved is missing" , 1) if (!opt("resolved"));

	check_archive("Probe1", getopt("archive"), getopt("timestamp"));
}

sub check_archive($$$)
{
	my $probe     = shift;
	my $filename  = shift;
	my $timestamp = shift;

	my %archive = read_archive($filename);

	my $proxy_filename    = "$probe-proxy-$timestamp.json.gz";
	my $rsmhosts_filename = "$probe-rsmhosts-$timestamp.json.gz";
	my $resolved_filename = "$probe-resolved_hosts-$timestamp.json.gz";

	die("file '$proxy_filename' not found in the archive")    if (!exists($archive{$proxy_filename}));
	die("file '$rsmhosts_filename' not found in the archive") if (!exists($archive{$rsmhosts_filename}));
	die("file '$resolved_filename' not found in the archive") if (!exists($archive{$resolved_filename}));

	compare_file_contents(\%archive, $proxy_filename   , getopt("proxy"));
	compare_file_contents(\%archive, $rsmhosts_filename, getopt("rsmhosts"));
	compare_file_contents(\%archive, $resolved_filename, getopt("resolved"));

	my $proxy_config = decode_json($archive{$proxy_filename});
	my $rsmhosts_config = decode_json($archive{$rsmhosts_filename});

	if (is_rsmhost_enabled($rsmhosts_config))
	{
		die("ipv4 is enabled in proxy, but mtr outputs for ipv4 are missing")  if ($proxy_config->{"ipv4"} && !has_mtr_outputs(\%archive, "ipv4"));
		die("ipv4 is disabled in proxy, but mtr outputs for ipv4 are present") if (!$proxy_config->{"ipv4"} && has_mtr_outputs(\%archive, "ipv4"));
		die("ipv4 is enabled in proxy, but mtr outputs for ipv4 are missing")  if ($proxy_config->{"ipv6"} && !has_mtr_outputs(\%archive, "ipv6"));
		die("ipv4 is disabled in proxy, but mtr outputs for ipv4 are present") if (!$proxy_config->{"ipv6"} && has_mtr_outputs(\%archive, "ipv6"));
	}
	else
	{
		die("all rsmhosts are disabled, but mtr outputs for ipv4 are present")  if (has_mtr_outputs(\%archive, "ipv4"));
		die("all rsmhosts are disabled, but mtr outputs for ipv6 are present")  if (has_mtr_outputs(\%archive, "ipv6"));
	}

}

sub read_archive($)
{
	my $filename = shift;

	my $tar = new Archive::Tar($filename) or die("failed to read archive: $!");

	my %archive = map { $_ => $tar->get_content($_) } $tar->list_files();

	foreach my $filename (keys(%archive))
	{
		my $compressed = $archive{$filename};

		gunzip(\$compressed => \$archive{$filename}) or die("gunzip failed for '$filename': $GunzipError");
	}

	return %archive;
}

sub compare_file_contents($$$)
{
	my %archive                  = %{+shift};
	my $archived_output_filename = shift;
	my $expected_output_filename = shift;

	my $archived_output = $archive{$archived_output_filename};
	my $expected_output = read_file($expected_output_filename);

	if ($archived_output ne $expected_output)
	{
		print("--- expected VS generated:\n");
		print(diff(\$expected_output, \$archived_output), "\n");

		die("contents of '$archived_output_filename' don't match expected contents of '$expected_output_filename'");
	}
}

sub read_file($)
{
	my $filename = shift;

	local $/ = undef;

	my $fh;

	open($fh, '<', $filename) or fail("cannot open file '$filename': $!");
	my $text = <$fh>;
	close($fh) or fail("cannot close file '$filename': $!");

	return $text;
}

sub is_rsmhost_enabled($)
{
	my %rsmhosts_config = %{+shift};

	foreach my $config (values(%rsmhosts_config))
	{
		if ($config->{'dns_tcp'} eq '1' ||
			$config->{'dns_udp'} eq '1' ||
			$config->{'rdap'} eq '1' ||
			$config->{'rdds43'} eq '1' ||
			$config->{'rdds80'} eq '1')
		{
			return 1;
		}
	}

	return 0;
}

sub has_mtr_outputs($$)
{
	my %archive    = %{+shift};
	my $ip_version = shift;

	my $ip_pattern;

	$ip_pattern = '\d+\.\d+\.\d+\.\d+' if ($ip_version eq "ipv4");
	$ip_pattern = '[0-9a-fA-F]*(:[:0-9a-fA-F])+' if ($ip_version eq "ipv6");

	die("unsupported IP version: '$ip_version'") if (!defined($ip_pattern));

	return scalar(grep(/-$ip_pattern-\d+-\d+\.json\.gz$/, keys(%archive))) > 0;
}

################################################################################
# command-line options
################################################################################

my %OPTS;

sub parse_opts()
{
	my $rv = GetOptionsFromArray([@ARGV], \%OPTS, "archive=s", "timestamp=i", "proxy=s", "rsmhosts=s", "resolved=s", "help");

	if (!$rv || $OPTS{"help"})
	{
		usage(undef, 0);
	}
}

sub opt($)
{
	my $key = shift;

	return exists($OPTS{$key});
}

sub getopt($)
{
	my $key = shift;

	return exists($OPTS{$key}) ? $OPTS{$key} : undef;
}

sub usage($$)
{
	my $message = shift;
	my $exitval = shift;

	pod2usage(
		-message => $message,
		-exitval => $exitval,
		-verbose => 2,
		-noperldoc,
		-output  => $exitval == 0 ? \*STDOUT : \*STDERR,
	);
}

################################################################################
# end of script
################################################################################

main();

__END__

=head1 NAME

check-test-result.pl - checks the results of the test.

=head1 SYNOPSIS

check-test-result.pl --archive <filename> --timestamp <timestamp> --proxy <filename> --rsmhosts <filename> --resolved <filename>
check-test-result.pl --help

=head1 OPTIONS

=over 8

=item B<--archive> string

Specify filename of the archive.

=item B<--timestamp> integer

Specify cycle timestamp.

=item B<--proxy> string

Specify filename of file with expected contents of "proxy config" JSON.

=item B<--rsmhosts> JSON.

Specify filename of file with expected contents of "rsmhosts config" JSON.

=item B<--resolved> string

Specify filename of file with expected contents of "resolved hosts" JSON.

=item B<--help>

Print a brief help message and exit.

=back

=cut
