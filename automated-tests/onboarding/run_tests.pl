#!/usr/bin/perl -w

use warnings;
use strict;

use Getopt::Std;
use Module::Load;
use File::Spec;
use Term::ANSIColor qw(:constants);
use Data::Dumper;
use DBI;

use lib '.';
use rts_util;

my $dbh;

sub main
{
	our ($opt_w, $opt_h, $opt_u, $opt_p, $opt_d, $rsm_test);
	my %dbinfo;

	usage() unless (getopts('whu:p:d:') || defined($opt_h));

	my $case_config = shift @ARGV;

	usage() unless (defined($case_config) && -r $case_config);

	my (undef, $p, $f) = File::Spec->splitpath($case_config);
	$p = '.' if (!defined($p) || $p eq "");

	eval "use lib '$p'";
	$f =~ s/.pm$//;
	autoload $f;

	if (defined($opt_d))
	{
		$dbinfo{"dsn"} = "DBI:mysql:database=$opt_d:host=localhost";
		$dbinfo{"dbname"} = $opt_d;
		$dbinfo{"dbuser"} = $opt_u;
		$dbinfo{"dbpassword"} = $opt_p;
	}

	my $setup = $f->can('rts_setup');
	if (defined($setup) && !&$setup(\%dbinfo))
	{
		fatal("initialization failed");
	}

	process(\%dbinfo, rts_get_test());
}

sub process
{
	my $dbinfo = shift;
	my $test = shift;

	my ($cases_total, $cases_success) = (0, 0);

	show('Running test set: ', YELLOW, $test->{R_NAME});

	if ((run_cmd($test->{R_INIT}))[0] != 0)
	{
		fatal('failed to initialize tests');
	}

	for my $case (@{$test->{R_CASES}})
	{
		my ($name, $run) = ($case->{R_NAME}, $case->{R_RUN});
		show('Running case: ', YELLOW, $name);
		$cases_total++;

		my ($rc, $result) = run_cmd($run);
		if ($rc != 0)
		{
			show('Case ', $name, RED, ' FAIL');
			next;
		}

		my $c_result;
		for my $check (@{$case->{R_CHECKS}})
		{
			my $descr = $check->{R_DESCR};
			$c_result = process_check($dbinfo, $check);
			if (!$c_result)
			{
				show('> check ', $descr, BRIGHT_BLACK, ' -', RED, ' FAIL');
				last;
			}
			else
			{
				show('> check ', $descr, BRIGHT_BLACK, ' -', GREEN, ' OK');
			}
		}
		$cases_success++ if ($c_result);
	}

	printf("\nCases total: %d, failed: %d, successful: %d\n", $cases_total,
			$cases_total - $cases_success, $cases_success);
}

sub process_check
{
	my $dbinfo = shift;
	my $check = shift;

	my $result;
	my ($type, $run, $expect) = ($check->{R_TYPE}, $check->{R_RUN}, $check->{R_EXPECT});

	if ($type eq R_TYPE_DBSELECT)
	{
		my $ret = run_dbselect($dbinfo, $run);
		$result = ($ret =~ /$expect/);
	}
	elsif ($type eq R_TYPE_CMD)
	{
		my ($rc) = run_cmd($run);
		$result = !$rc;
	}
	else
	{
		fatal('unknown check type ', $type);
	}

	return $result;
}

sub run_dbselect
{
	my $dbinfo = shift;
	my $statement = shift;

	my ($retstr, $sth) = ('');

	chomp($statement);
	show('    Running sql: ', BRIGHT_BLACK, $statement);

	$dbh =  DBI->connect($dbinfo->{"dsn"}, $dbinfo->{"dbuser"}, $dbinfo->{"dbpassword"}) unless(defined($dbh));
	$sth = $dbh->prepare($statement);

	fatal("prepare error: ", $dbh->errstr) if (!defined($sth) || !$sth);
	fatal("execute error: ", $dbh->errstr) unless ($sth->execute());

	my @row = $sth->fetchrow_array();
	if (@row)
	{
		$retstr = join(',', @row);
		show('    Result ', BRIGHT_BLACK, $retstr);
	}

	$sth->finish();

	return $retstr;
}

sub run_cmd
{
	my $cmd = shift;

	my ($output, $rc);

	$cmd .= ' 2>&1';
	show('    Running cmd: ', BRIGHT_BLACK, $cmd);
	$output = qx/$cmd/;
	$rc = $? >> 8;

	return ($rc, $output);
}

sub show
{
	my @args = @_;

	print(RESET, BLUE, ':: ', RESET);
	map { print; } @args;
	print(RESET, "\n");
}

sub fatal
{
	my @args = @_;

	print(RESET, RED, 'fatal error: ', RESET);
	map { print; } @args;
	print("\n");

	exit(-1);
}

sub usage
{
	print STDERR <<EOF;
Usage: $0 [-w] [-u <username>] [-p password] [-d <dbname>] <test_cases.pm>
EOF
	exit(-1);
}

main();

$dbh->disconnect() if (defined($dbh));
