#!/usr/bin/env perl

use FindBin;
use lib $FindBin::RealBin;

use strict;
use warnings;

use Data::Dumper;
use File::Basename;
use File::Path qw(make_path);
use JSON::XS;

use RSM;
use RSMSLV;

my $config;

################################################################################
# main
################################################################################

sub main()
{
	my $time_start;
	my $time_end;

	initialize();

	my $mtr_dir_base = get_config('network_troubleshooting', 'output_dir');
	my $sla_dir_base = get_config('sla_api', 'output_dir');

	my $archive_file = getopt('archive');
	my $archive_filename = basename($archive_file);

	if (!-f $archive_file)
	{
		fail("file not found: $archive_file");
	}

	if ($archive_filename !~ /^(\d\d\d\d)(\d\d)(\d\d)-\d\d\d\d\d\d-(.+)\.tar$/)
	{
		fail("invalid structure of archive filename, expected '<yyyymmdd>-<hhmmss>-<probe>.tar', got '$archive_filename'");
	}

	my ($y, $m, $d, $probe) = ($1, $2, $3, $4);

	my $mtr_dir = "$mtr_dir_base/$y/$m/$d";

	if (!-d $mtr_dir)
	{
		create_dir($mtr_dir);
	}

	my $file_list = execute("tar", "-C", $mtr_dir, "-vxf", $archive_file);
	my @file_list = split(/\n/, $file_list);

	my $proxy_config_filename    = (grep { /.*-proxy-.*\.gz/ } @file_list)[0];
	my $rsmhosts_config_filename = (grep { /.*-rsmhosts-.*\.gz/ } @file_list)[0];
	my $resolved_hosts_filename  = (grep { /.*-resolved_hosts-.*\.gz/ } @file_list)[0];

	fail("failed to find a file with proxy configuration in the archive")    if (!defined($proxy_config_filename));
	fail("failed to find a file with rsmhosts configuration in the archive") if (!defined($rsmhosts_config_filename));
	fail("failed to find a file with resolved hosts in the archive")         if (!defined($resolved_hosts_filename));

	@file_list = grep {
		$_ ne $proxy_config_filename &&
		$_ ne $rsmhosts_config_filename &&
		$_ ne $resolved_hosts_filename
	} @file_list;

	my $proxy_config    = decode_json(read_gzip($mtr_dir . '/' . $proxy_config_filename));
	my $rsmhosts_config = decode_json(read_gzip($mtr_dir . '/' . $rsmhosts_config_filename));
	my $resolved_hosts  = decode_json(read_gzip($mtr_dir . '/' . $resolved_hosts_filename));

	my %ip_rsmhosts = create_ip_rsmhosts_mapping($proxy_config, $rsmhosts_config, $resolved_hosts);

	foreach my $filename (@file_list)
	{
		if ($filename !~ /^$probe-(\d+\.\d+\.\d+\.\d+|[\d:]+)-\d+-\d+\.json\.gz$/)
		{
			fail("unexpected structure of filename, expected '<probe>-<ip>-<cycle_timestamp>-<metric_timestamp>.json.gz', got '$filename'");
		}

		my $ip = $1;

		foreach my $rsmhost (@{$ip_rsmhosts{$ip}})
		{
			my $sla_dir = "$sla_dir_base/v2/$rsmhost/networkTroubleshooting/mtr/$y/$m/$d";

			if (!-d $sla_dir)
			{
				create_dir($sla_dir);
			}

			my $src_file = $mtr_dir . '/' . $filename;
			my $dst_file = $sla_dir . '/' . $filename;

			if (!symlink($src_file, $dst_file))
			{
				log_stacktrace(0);
				wrn("failed to create a symlink: $!");
				wrn("* src_file: '$src_file'");
				wrn("* dst_file: '$dst_file'");
				log_stacktrace(1);

				fail();
			}
		}
	}
}

sub initialize()
{
	parse_opts("archive=s");

	usage("--archive is missing", 1) if (!opt('archive'));

	initialize_config();
	validate_config();
}

sub initialize_config()
{
	$config = get_rsm_config();
}

sub validate_config()
{
	# use dbg() not only for printing config, but also for checking if all required config options are present,
	# get_config() fails if config option does not exist or is empty

	dbg("config:");
	dbg();
	dbg("[network_troubleshooting]");
	dbg("output_dir = %s", get_config('network_troubleshooting', 'output_dir'));
	dbg();
}

sub get_config($$)
{
	my $section  = shift;
	my $property = shift;

	if (!defined($config))
	{
		fail('config has not been initialized');
	}
	if (!exists($config->{$section}))
	{
		fail("section '$section' does not exist in the config file");
	}
	if (!exists($config->{$section}{$property}))
	{
		fail("property '$section.$property' does not exist in the config file");
	}

	my $value = $config->{$section}{$property};

	if ($value eq '')
	{
		fail("property '$section.$property' is empty in the config file");
	}

	return $value;
}

sub create_dir($)
{
	my $dir = shift;

	my $errors;

	make_path($dir, { 'error' => \$errors });

	if (@{$errors})
	{
		log_stacktrace(0);

		wrn("Failed to create directory '$dir':");

		foreach my $error (@{$errors})
		{
			my ($file, $message) = %{$error};

			wrn("* $file: $message");
		}

		log_stacktrace(1);

		fail();
	}
}

sub execute
{
	dbg("executing: " . join(" ", map('"' . $_ . '"', @_)));

	my $stdout = qx(@_);

	if ($? != 0)
	{
		if ($? == -1)
		{
			fail(sprintf("failed to execute: %s", $!));
		}
		elsif ($? & 127)
		{
			fail(sprintf("executed process died with signal %d, %s coredump", ($? & 127),  ($? & 128) ? "with" : "without"));
		}
		else
		{
			fail(sprintf("executed process exited with value %d", $? >> 8));
		}
	}

	return $stdout;
}

sub read_gzip($)
{
	my $file = shift;

	local $/ = undef;

	my $fh;

	open($fh, '-|', 'gunzip', '-c', $file) or fail("cannot open file '$file': $!");
	my $contents = <$fh>;
	close($fh) or fail("cannot close file '$file': $!");

	return $contents;
}

sub create_ip_rsmhosts_mapping($$$)
{
	my %proxy_config    = %{+shift};
	my %rsmhosts_config = %{+shift};
	my %resolved_hosts  = %{+shift};

	my %mapping = ();

	foreach my $rsmhost (keys(%rsmhosts_config))
	{
		foreach my $nsip (@{$rsmhosts_config{$rsmhost}{'nsip_list'}})
		{
			my $ip = $nsip->[1];

			if ($proxy_config{'ipv4'} && is_ipv4($ip))
			{
				$mapping{$ip}{$rsmhost} = undef;
			}
			if ($proxy_config{'ipv6'} && is_ipv6($ip))
			{
				$mapping{$ip}{$rsmhost} = undef;
			}
		}

		my %hosts = ();

		if ($proxy_config{'rdds'} && $rsmhosts_config{$rsmhost}{'rdds43'})
		{
			$hosts{$rsmhosts_config{$rsmhost}{'rdds43_server'}} = undef;
		}
		if ($proxy_config{'rdds'} && $rsmhosts_config{$rsmhost}{'rdds80'})
		{
			$hosts{$rsmhosts_config{$rsmhost}{'rdds80_server'}} = undef;
		}
		if ($proxy_config{'rdap'} && $rsmhosts_config{$rsmhost}{'rdap'})
		{
			$hosts{$rsmhosts_config{$rsmhost}{'rdap_server'}} = undef;
		}

		my @ip_list = ();

		foreach my $host (keys(%hosts))
		{
			push(@ip_list, @{$resolved_hosts{$host}{'ipv4'} // []}) if ($proxy_config{'ipv4'});
			push(@ip_list, @{$resolved_hosts{$host}{'ipv6'} // []}) if ($proxy_config{'ipv6'});
		}

		foreach my $ip (@ip_list)
		{
			$mapping{$ip}{$rsmhost} = undef;
		}
	}

	foreach my $ip (keys(%mapping))
	{
		$mapping{$ip} = [keys(%{$mapping{$ip}})];
	}

	return %mapping;
}

sub is_ipv4($)
{
	return (shift =~ m/^\d+\.\d+\.\d+\.\d+$/);
}

sub is_ipv6($)
{
	return !is_ipv4(shift);
}

################################################################################
# end of script
################################################################################

main();

__END__

=head1 NAME

mtr-symlinker.pl - unpacks archive and make symlinks in SLA API directories.

=head1 SYNOPSIS

tracerouter-mtr.pl --archive <file> [--nolog] [--debug] [--help]

=head1 OPTIONS

=over 8

=item B<--archive> string

Archive with rsmhosts' and proxy configurations, resolved hostnames and mtr results.

=item B<--nolog>

Print output to stdout and stderr instead of a log file.

=item B<--debug>

Run the script in debug mode. This means printing more information.

=item B<--help>

Print a brief help message and exit.

=back

=cut
