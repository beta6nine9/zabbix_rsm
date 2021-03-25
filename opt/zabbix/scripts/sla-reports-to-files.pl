#!/usr/bin/env perl

use strict;
use warnings;

use FindBin;
use lib $FindBin::RealBin;

use RSM;
use RSMSLV;

use File::Path qw(make_path);

sub main()
{
	parse_opts("year=i", "month=i");

	fail_if_running();

	usage() unless (opt('month') && opt('year'));

	setopt('nolog');

	my $config = get_rsm_config();
	set_slv_config($config);

	my @server_keys = get_rsm_server_keys($config);

	foreach (@server_keys)
	{
		$server_key = $_;

		print("$server_key: ") if (opt('dry-run'));

		db_connect($server_key);

		my $rows = db_select(
			"select h.host,r.year,r.month,r.report_json".
			" from hosts h,sla_reports r".
			" where h.hostid=r.hostid".
				" and year="  . getopt('year').
				" and month=" . getopt('month')
		);

		foreach my $row (@{$rows})
		{
			my $rsmhost = $row->[0];
			my $year    = $row->[1];
			my $month   = $row->[2];
			my $json    = $row->[3];

			if (opt('dry-run'))
			{
				print(",$rsmhost");
				next;
			}

			my $path = "/opt/zabbix/sla/v2/$rsmhost/monthlyReports";

			make_path($path, {error => \my $err});

			if (@$err)
			{
				my $error_string;

				if (ref($err) eq "ARRAY")
				{
					for my $diag (@{$err})
					{
						my ($file, $message) = %{$diag};

						if ($file eq '')
						{
							$error_string .= "$message. ";
						}
						else
						{
							$error_string .= "$file: $message. ";
						}

						fail($error_string);
					}
				}

				fail(join('', $err, @_));
			}

			my $file = sprintf("%s/%.2d-%.2d", $path, $year, $month);

			my $error;

			if (write_file($file, $json, \$error) != SUCCESS)
			{
				fail($error);
			}
		}

		if (opt('dry-run'))
		{
			print("\n");
		}

		db_disconnect();
	}
}

main();

__END__

=head1 NAME

sla-reports-to-files.pl - get SLA reports from the database and save them to the files.

=head1 SYNOPSIS

sla-reports-to-files.pl [--year <year>] [--month <month>] [--dry-run] [--debug] [--help]

=head1 OPTIONS

=over 8

=item B<--year> int

Specify year. If year is specified, month also has to be specified.

=item B<--month> int

Specify month. If month is specified, year also has to be specified.

=item B<--dry-run>

Print data to the screen, do not change anything in the system.

=item B<--debug>

Run the script in debug mode. This means printing more information.

=item B<--help>

Print a brief help message and exit.

=back

=cut
