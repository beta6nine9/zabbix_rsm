#!/usr/bin/env perl

################################################################################
#
# Script for testing SLV scripts.
#
# It's callers responsibility to:
# * prepare clean sources in ./source
# * link ./source/opt/zabbix to /opt/zabbix before executing tests
# * unlink /opt/zabbix after executing tests
#
################################################################################

use strict;
use warnings;

use FindBin;
use lib "$FindBin::RealBin";

use Data::Dumper;
use DateTime;
use File::Spec;
use Getopt::Long qw(GetOptionsFromArray);
use Pod::Usage;
use Time::HiRes qw(time);
use XML::LibXML;

use Output; # Output must be "used" before any other module to setup __WARN__ and __DIE__ hooks and catch potential issues during "using" other modules
use Configuration;
use Options;
use Database;
use Framework;
use TestCase;

use constant XML_REPORT_FILE => "test-results.xml";

################################################################################
# main
################################################################################

sub main()
{
	parse_opts(
		"test-case-file=s@",
		"test-case-dir=s@",
		"skip-build",
		"build-server",
		"build-proxy",
		"build-agent",
		"stop-on-failure",
		"no-forks",
		"debug",
		"help",
	);
	setopt("nolog", 1);

	usage("can't use both --skip-build and --build-server at the same time", 1) if (opt("skip-build") && opt("build-server"));
	usage("can't use both --skip-build and --build-proxy at the same time" , 1) if (opt("skip-build") && opt("build-proxy"));
	usage("can't use both --skip-build and --build-agent at the same time" , 1) if (opt("skip-build") && opt("build-agent"));

	initialize();

	if (opt("debug"))
	{
		log_debug_messages(1);
	}

	if (-f XML_REPORT_FILE)
	{
		unlink(XML_REPORT_FILE) or fail("cannot unlink file '%s': %s", XML_REPORT_FILE, $!);
	}

	if (!opt("skip-build"))
	{
		my @directories = (
			get_config('paths', 'build_dir'),
			get_config('paths', 'logs_dir'),
		);
		my @files = (
			get_config('paths', 'source_dir') . "/database/mysql/dump.sql",
		);

		foreach my $directory (@directories)
		{
			if (-d $directory)
			{
				execute("rm -rf $directory/*");
			}
			else
			{
				mkdir($directory) or fail("cannot create dir '%s': %s", $directory, $!);
			}
		}
		foreach my $file (@files)
		{
			if (-f $file)
			{
				execute("unlink $file");
			}
		}

		zbx_build(
			opt("build-server"),
			opt("build-proxy"),
			opt("build-agent")
		);
	}

	my @test_case_results = ();

	my $test_suite_duration = time();
	foreach my $filename (get_test_case_files())
	{
		my $test_case_duration = time();
		my ($test_case_name, $skipped, $failure) = run_test_case($filename);
		$test_case_duration = time() - $test_case_duration;

		push(@test_case_results, [$filename, $test_case_name, $skipped, $failure, $test_case_duration]);

		if (defined($failure) && opt("stop-on-failure"))
		{
			last;
		}
	}
	$test_suite_duration = time() - $test_suite_duration;

	my $xml_report = get_xml_report($test_suite_duration, \@test_case_results);

	write_file(XML_REPORT_FILE, $xml_report);

	finalize();
}

sub get_test_case_files()
{
	my %test_cases;

	if (opt("test-case-file"))
	{
		foreach my $filename (@{getopt("test-case-file")})
		{
			$filename = File::Spec->rel2abs($filename);
			$filename =~ s!/[^/]+/\.\./!/!g;

			if ($filename =~ /\.txt~?$/ && -f $filename)
			{
				$test_cases{$filename} = undef;
			}
			else
			{
				fail("not a .txt file: '$filename'");
			}
		}
	}

	if (opt("test-case-dir"))
	{
		foreach my $dir (@{getopt('test-case-dir')})
		{
			if (! -d $dir)
			{
				fail("directory does not exist: '$dir'");
			}

			my @dir_tree = get_dir_tree($dir);
			@dir_tree = grep($_ =~ /\.txt$/, @dir_tree);
			@dir_tree = map(File::Spec->catfile($dir, $_), @dir_tree);

			foreach my $filename (@dir_tree)
			{
				$filename = File::Spec->rel2abs($filename);
				$filename =~ s!/[^/]+/\.\./!/!g;

				$test_cases{$filename} = undef;
			}
		}
	}

	# ignore "." in front of filename when sorting
	return sort { $a =~ s/(\/)\.?([^\/]+)$/$1$2/r cmp $b =~ s/(\/)\.?([^\/]+)$/$1$2/r } keys(%test_cases);
}

sub get_xml_report($$)
{
	# https://github.com/windyroad/JUnit-Schema/blob/master/JUnit.xsd
	# https://metacpan.org/pod/XML::LibXML
	# https://metacpan.org/pod/XML::LibXML::Document
	# https://metacpan.org/pod/XML::LibXML::Element
	# https://metacpan.org/pod/XML::LibXML::Node

	my $test_suite_duration = shift;
	my $test_case_results   = shift;

	my $count_tests    = scalar(@{$test_case_results});
	my $count_failures = 0;
	my $count_errors   = 0;
	my $count_skipped  = 0;

	my $xml = XML::LibXML::Document->new("1.0", "UTF-8");

	my $testsuite = $xml->createElement("testsuite");

	foreach my $test_case_result (@{$test_case_results})
	{
		my ($filename, $test_case_name, $skipped, $failure, $test_case_duration) = @{$test_case_result};

		my $testcase = $xml->createElement("testcase");

		$testcase->setAttribute("name"     , $test_case_name);
		$testcase->setAttribute("classname", $filename);
		$testcase->setAttribute("time"     , $test_case_duration);

		if ($skipped)
		{
			$count_skipped++;

			$testcase->appendChild($xml->createElement("skipped"));
		}

		if (defined($failure))
		{
			$count_failures++;

			$testcase->appendTextChild("failure", $failure);
		}

		$testsuite->appendChild($testcase);
	}

	$testsuite->setAttribute("timestamp", DateTime->from_epoch("epoch" => $^T)->iso8601());
	$testsuite->setAttribute("tests"    , $count_tests);
	$testsuite->setAttribute("failures" , $count_failures);
	$testsuite->setAttribute("errors"   , $count_errors);
	$testsuite->setAttribute("skipped"  , $count_skipped);
	$testsuite->setAttribute("time"     , $test_suite_duration);

	$xml->setDocumentElement($testsuite);

	return $xml->toString(1);
}

################################################################################
# end of script
################################################################################

main();

__END__

=head1 NAME

run-tests.pl - execute test cases.

=head1 SYNOPSIS

run-tests.pl [--test-case-file <file>] ... [--test-case-dir <dir>] ... [--skip-build] [--build-server] [--build-proxy] [--build-agent] [--stop-on-failure] [--no-forks] [--debug] [--help]

=head1 EXAMPLES

run-tests.pl --build-proxy --build-server

run-tests.pl --skip-build --no-forks --test-case-file <dir>/001-*.txt

run-tests.pl --skip-build $(printf -- "--test-case-file %s " $(find <dir> -name '0??-*.txt'))

run-tests.pl --skip-build --test-case-dir <dir> --stop-on-failure

=head1 DEPENDENCIES

 Running tests depends on the following software:
   - mysql/mariadb client
   - faketime

=head1 OPTIONS

=over 8

=item B<--test-case-file> string

Specify test case file. This option can be used multiple times.

=item B<--test-case-dir> string

Specify test case directory. This option can be used multiple times.

=item B<--skip-build>

Skip building Zabbix server (this includes SQL files for creating database).

=item B<--build-server>

Build Zabbix server.

=item B<--build-proxy>

Build Zabbix proxy.

=item B<--build-agent>

Build Zabbix agent.

=item B<--stop-on-failure>

Stop executing test cases on the first failure.

=item B<--no-forks>

Do not fork when executing commands that normally would be executed in a forked process.
Improves performance, but instead of failing the test and reporting it as failed, the whole test framework will fail.

=item B<--debug>

Run the script in debug mode. This means printing more information.

=item B<--help>

Print a brief help message and exit.

=back

=cut
