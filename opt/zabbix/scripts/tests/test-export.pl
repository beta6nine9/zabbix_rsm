#!/usr/bin/env perl

use strict;
use warnings;

use Getopt::Long;
use Text::CSV_XS;
use DateTime;

my @catalog_names = (
	'ipAddresses',
	'ipVersions',
	'nsFQDNs',
	'probeNames',
	'serviceCategory',
	'statusMaps',
	'testTypes',
	'tlds',
	'tldTypes',
	'transportProtocols',
	'nsid',
	'target',
	'testedName',
);

my @generic_data_file_names = (
	'probeChanges',
	'falsePositiveChanges',
);

my @tld_data_file_names = (
	'cycles',
	'incidents',
	'incidentsEndTime',
	'nsTests',
	'tests',
	'testDetails',
	'minns',
);

use constant DEBUG_NTH_ROW => 1000;	# in debug mode, each Nth row to print

sub parse_time_str($);

#####################################
#                                   #
# dealing with command line options #
#                                   #
#####################################

# display help message
my $help = 0;

my $monitoring_target;
my $rsmhost;

# start and length of the interval for which CSV files were generated
my $clock = 0;
my $period = 86400;

# output progress while testing
my $debug = 0;

# stop checking file on the first failed test
my $fail_immediately = 0;

my %files;
my %options;

$options{'help'} = \$help;
$options{'monitoring-target=s'} = \$monitoring_target;
$options{'tld=s'} = \$rsmhost;
$options{'clock=s'} = \$clock;
$options{'period=i'} = \$period;
$options{'debug'} = \$debug;
$options{'fail-immediately'} = \$fail_immediately;

if (!GetOptions(%options))
{
	$help = 1;
}

if (!$rsmhost && !$help)
{
	print("--tld is mandatory\n");
	$help = 1;
}

use constant TARGET_RY => "registry";
use constant TARGET_RR => "registrar";

if (!$monitoring_target && !$help)
{
	print("--monitoring-target is mandatory\n");
	$help = 1;
}
elsif ($monitoring_target && !$help && $monitoring_target ne TARGET_RY && $monitoring_target ne TARGET_RR)
{
	print("\"$monitoring_target\": invalid monitoring target, expected \"".TARGET_RY."\" or \"".TARGET_RR."\"\n");
	$help = 1;
}

if (!$clock && !$help)
{
	print("--clock is mandatory\n");
	$help = 1;
}

my $clock_ret;
if (!($clock_ret = parse_time_str($clock)))
{
	print("invalid format of timestamp: '$clock'\n");
	$help = 1;
}
$clock = $clock_ret;

if ($help)
{
	print("Usage: $0 --monitoring-target=<registry|registrar> --tld=<TLD> --clock=<timestamp or yyyy-mm-dd HH:MM:SS> [--period=<period length (seconds)>]
		[--debug] [--fail-immediately]\n");

	exit(1);
}

##################
#                #
# set file names #
#                #
##################

# catalogs
foreach my $csv_file (@catalog_names)
{
	$files{$csv_file} = "/opt/zabbix/export/$csv_file.csv";
}

# sec, min, hour, mday, mon, year
my (undef, undef, undef, $mday, $mon, $year) = localtime($clock);
$mon += 1;
$year += 1900;

$mon = "0$mon" if (length($mon) == 1);
$mday = "0$mday" if (length($mday) == 1);

# generic data files
foreach my $csv_file (@generic_data_file_names)
{
	$files{$csv_file} = "/opt/zabbix/export/$year/$mon/$mday/$csv_file.csv";
}

# tld data files
foreach my $csv_file (@tld_data_file_names)
{
	$files{$csv_file} = "/opt/zabbix/export/$year/$mon/$mday/$rsmhost/$csv_file.csv";
}

##########################################
#                                        #
# validation helper functions and regexp #
#                                        #
##########################################

use constant INT => qr/^[+-]?[0-9]+$/;
use constant DEC => qr/^[+-]?[0-9]+.?[0-9]*$/;
use constant TIME => qr/^[0-9]+$/;
use constant ERR => qr/^-[0-9]+/;

sub parse_time_str($)
{
	my $str = shift;

	if (my @matches = ($str =~ /^(\d\d\d\d)-(\d\d)-(\d\d) (\d\d):(\d\d):(\d\d)$/))
	{
		my ($year, $month, $date, $hour, $minute, $second) = @matches;

		return DateTime->new('year' => $year, 'month' => $month, 'day' => $date, 'hour' => $hour, 'minute' => $minute, 'second' => $second)->epoch();
	}

	if (my @matches = ($str =~ /^(\d+)$/))
	{
		return $matches[0];
	}

	return undef;
}

sub empty($)
{
	my $value = shift;

	return '' eq $value;
}

sub emptyif($$)
{
	my $value = shift;
	my $condition = shift;

	return empty($value) && $condition || !empty($value) && !$condition;
}

sub isround($)
{
	my $value = shift;

	if ($value =~ TIME)
	{
		return 0 == $value % 60;
	}

	return 0;
}

sub fromperiod($)
{
	my $value = shift;

	if ($value =~ TIME)
	{
		return $clock <= $value && $value < $clock + $period
	}

	return 0;
}

sub after($$)
{
	my $end_time = shift;
	my $start_time = shift;

	if ($end_time =~ TIME && $start_time =~ TIME)
	{
		return $end_time >= $start_time;
	}

	return 0;
}

my @uniqueness;

sub unique($$)
{
	my $column = shift;
	my $row = shift;

	my $insertion_point = \$uniqueness[$column]->{$row->[$column]};

	if ($$insertion_point)
	{
		return 0;
	}

	$$insertion_point = 1;

	return 1;
}

use constant OK =>	'[ OK ]';
use constant FAIL =>	'[FAIL]';

use constant MANDATORY =>	1;
use constant OPTIONAL =>	2;

my $all_tests_successful = 1;

sub open_file($$$)
{
	my $fileref = shift;
	my $filename = shift;
	my $mode = shift;

	print("Checking \"$filename\"...\n");

	if (open(${$fileref}, "<:encoding(utf8)", $filename))
	{
		return 1;
	}

	if ($mode == MANDATORY)
	{
		print(FAIL . " \"$filename\" does not exist\n");

		$all_tests_successful = 0;
	}

	return 0;
}

sub print_results($$$$$$)
{
	my $filename = shift;
	my $csv_ok = shift;
	my $first_fail = shift;
	my $columns_not_ok = shift;
	my $no_fails_ref = shift;
	my $cases_ref = shift;

	if ($csv_ok || $first_fail)
	{
		if ($columns_not_ok)
		{
			print(FAIL . " wrong number of columns in \"$filename\"\n");
		}
		else
		{
			for (my $index = 0; $index < scalar(@{$cases_ref}); $index++)
			{
				print(($no_fails_ref->[$index] ? OK : FAIL) . " $cases_ref->[$index]\n");
			}
		}
	}
	else
	{
		print(FAIL . " \"$filename\" is not a valid CSV file\n");
	}
}

sub everything_ok($$)
{
	my $wrong_columns = shift;
	my $flags = shift;

	my $res = 1;

	$all_tests_successful = 0 if ($wrong_columns);

	foreach my $flag (@{$flags})
	{
		$res &&= $flag;

		# set global value here
		$all_tests_successful = 0 if (!$flag);

		last unless ($res);
	}

	return $res;
}

########################
#                      #
# doing actual testing #
#                      #
########################

$|=1 if ($debug);	#autoflush

my $csv = Text::CSV_XS->new(
	{
		binary => 1,
		auto_diag => 1,
		sep_char  => ','
	}
);

my $name;
my $file;
my $columns;
my $wrong_columns;
my $row_number;
my $interrupted;
my @cases;
my @no_fails;

# check "ipAddresses" file
$name = 'ipAddresses';
$columns = 2;
@cases = (
	'"id" is an integer',
	'"id" column contains unique values',
	'"ipAddress" column contains unique values'
);
@no_fails = (1) x scalar(@cases);
@uniqueness = ({});

if (open_file(\$file, $files{$name}, MANDATORY))
{
	$row_number = 0;
	$interrupted = 0;

	while (my $row = $csv->getline($file))
	{
		last if ($wrong_columns = scalar(@{$row}) != $columns);

		$row_number++;
		print("\033[JProcessing row $row_number" . "\033[G") if ($debug && $row_number % DEBUG_NTH_ROW == 0);

		$no_fails[0] &&= $row->[0] =~ INT;
		$no_fails[1] &&= unique(0, $row);
		$no_fails[2] &&= unique(1, $row);

		if (!everything_ok($wrong_columns, \@no_fails) && $fail_immediately)
		{
			print("Interrupted on row $row_number!\n");
			$interrupted = 1;
			last;
		}
	}

	print_results($files{$name}, $csv->eof(), $interrupted, $wrong_columns, \@no_fails, \@cases);
	close($file);
}

my %ipAddresses_id = %{$uniqueness[0]};

# check "ipVersions" file
$name = 'ipVersions';
$columns = 2;
@cases = (
	'"id" is an integer',
	'"id" column contains unique values',
	'"ipVersion" column contains unique values'
);
@no_fails = (1) x scalar(@cases);
@uniqueness = ({});

if (open_file(\$file, $files{$name}, MANDATORY))
{
	$row_number = 0;
	$interrupted = 0;

	while (my $row = $csv->getline($file))
	{
		last if ($wrong_columns = scalar(@{$row}) != $columns);

		$row_number++;
		print("\033[JProcessing row $row_number" . "\033[G") if ($debug && $row_number % DEBUG_NTH_ROW == 0);

		$no_fails[0] &&= $row->[0] =~ INT;
		$no_fails[1] &&= unique(0, $row);
		$no_fails[2] &&= unique(1, $row);

		if (!everything_ok($wrong_columns, \@no_fails) && $fail_immediately)
		{
			print("Interrupted on row $row_number!\n");
			$interrupted = 1;
			last;
		}
	}

	print_results($files{$name}, $csv->eof(), $interrupted, $wrong_columns, \@no_fails, \@cases);
	close($file);
}

my %ipVersions_id = %{$uniqueness[0]};

# check "nsFQDNs" file
$name = 'nsFQDNs';
$columns = 2;
@cases = (
	'"id" is an integer',
	'"id" column contains unique values',
	'"nsFQDN" column contains unique values'
);
@no_fails = (1) x scalar(@cases);
@uniqueness = ({});

if (open_file(\$file, $files{$name}, ($monitoring_target eq TARGET_RY ? MANDATORY : OPTIONAL)))
{
	$row_number = 0;
	$interrupted = 0;

	while (my $row = $csv->getline($file))
	{
		last if ($wrong_columns = scalar(@{$row}) != $columns);

		$row_number++;
		print("\033[JProcessing row $row_number" . "\033[G") if ($debug && $row_number % DEBUG_NTH_ROW == 0);

		$no_fails[0] &&= $row->[0] =~ INT;
		$no_fails[1] &&= unique(0, $row);
		$no_fails[2] &&= unique(1, $row);

		if (!everything_ok($wrong_columns, \@no_fails) && $fail_immediately)
		{
			print("Interrupted on row $row_number!\n");
			$interrupted = 1;
			last;
		}
	}

	print_results($files{$name}, $csv->eof(), $interrupted, $wrong_columns, \@no_fails, \@cases);
	close($file);
}

my %nsFQDNs_id = %{$uniqueness[0]};

# check "probeNames" file
$name = 'probeNames';
$columns = 2;
@cases = (
	'"id" is an integer',
	'"id" column contains unique values',
	'"probeName" column contains unique values'
);
@no_fails = (1) x scalar(@cases);
@uniqueness = ({});

if (open_file(\$file, $files{$name}, MANDATORY))
{
	$row_number = 0;
	$interrupted = 0;

	while (my $row = $csv->getline($file))
	{
		last if ($wrong_columns = scalar(@{$row}) != $columns);

		$row_number++;
		print("\033[JProcessing row $row_number" . "\033[G") if ($debug && $row_number % DEBUG_NTH_ROW == 0);

		$no_fails[0] &&= $row->[0] =~ INT;
		$no_fails[1] &&= unique(0, $row);
		$no_fails[2] &&= unique(1, $row);

		if (!everything_ok($wrong_columns, \@no_fails) && $fail_immediately)
		{
			print("Interrupted on row $row_number!\n");
			$interrupted = 1;
			last;
		}
	}

	print_results($files{$name}, $csv->eof(), $interrupted, $wrong_columns, \@no_fails, \@cases);
	close($file);
}

my %probeNames_id = %{$uniqueness[0]};

# check "serviceCategory" file
$name = 'serviceCategory';
$columns = 2;
@cases = (
	'"id" is an integer',
	'"id" column contains unique values',
	'"serviceCategory" column contains unique values',
	'"id" for DNS service is specified',
	'"id" for NS service is specified',
	'"id" for RDDS service is specified',
	'"id" for RDAP service is specified',
);
@no_fails = (1) x scalar(@cases);
@uniqueness = ({});

my $DNS_id;
my $NS_id;
my $RDDS_id;
my $RDAP_id;

if (open_file(\$file, $files{$name}, MANDATORY))
{
	$row_number = 0;
	$interrupted = 0;

	while (my $row = $csv->getline($file))
	{
		last if ($wrong_columns = scalar(@{$row}) != $columns);

		$row_number++;
		print("\033[JProcessing row $row_number" . "\033[G") if ($debug && $row_number % DEBUG_NTH_ROW == 0);

		$no_fails[0] &&= $row->[0] =~ INT;
		$no_fails[1] &&= unique(0, $row);
		$no_fails[2] &&= unique(1, $row);

		if ($row->[1] =~ /^DNS$/i)
		{
			$DNS_id = $row->[0];
		}

		if ($row->[1] =~ /^NS$/i)
		{
			$NS_id = $row->[0];
		}

		if ($row->[1] =~ /^RDDS$/i)
		{
			$RDDS_id = $row->[0];
		}

		if ($row->[1] =~ /^RDAP$/i)
		{
			$RDAP_id = $row->[0];
		}

		if (!everything_ok($wrong_columns, \@no_fails) && $fail_immediately)
		{
			print("Interrupted on row $row_number!\n");
			$interrupted = 1;
			last;
		}
	}

	$no_fails[3] &&= $DNS_id;
	$no_fails[4] &&= $NS_id;
	$no_fails[5] &&= $RDDS_id;
	$no_fails[6] &&= $RDAP_id;

	if (!$DNS_id)
	{
		$DNS_id = 1;
	}

	if (!$NS_id)
	{
		$NS_id = 5;
	}

	if (!$RDDS_id)
	{
		$RDDS_id = 3;
	}

	if (!$RDAP_id)
	{
		$RDAP_id = 6;
	}

	print_results($files{$name}, $csv->eof(), $interrupted, $wrong_columns, \@no_fails, \@cases);
	close($file);
}

my %serviceCategory_id = %{$uniqueness[0]};

# check "statusMaps" file
$name = 'statusMaps';
$columns = 2;
@cases = (
	'"id" is an integer',
	'"id" column contains unique values',
	'"status" column contains unique values'
);
@no_fails = (1) x scalar(@cases);
@uniqueness = ({});

if (open_file(\$file, $files{$name}, MANDATORY))
{
	$row_number = 0;
	$interrupted = 0;

	while (my $row = $csv->getline($file))
	{
		last if ($wrong_columns = scalar(@{$row}) != $columns);

		$row_number++;
		print("\033[JProcessing row $row_number" . "\033[G") if ($debug && $row_number % DEBUG_NTH_ROW == 0);

		$no_fails[0] &&= $row->[0] =~ INT;
		$no_fails[1] &&= unique(0, $row);
		$no_fails[2] &&= unique(1, $row);

		if (!everything_ok($wrong_columns, \@no_fails) && $fail_immediately)
		{
			print("Interrupted on row $row_number!\n");
			$interrupted = 1;
			last;
		}
	}

	print_results($files{$name}, $csv->eof(), $interrupted, $wrong_columns, \@no_fails, \@cases);
	close($file);
}

my %statusMaps_id = %{$uniqueness[0]};

# check "testTypes" file
$name = 'testTypes';
$columns = 2;
@cases = (
	'"id" is an integer',
	'"id" column contains unique values',
	'"testType" column contains unique values'
);
@no_fails = (1) x scalar(@cases);
@uniqueness = ({});

my $DNSTYPE_id;

if (open_file(\$file, $files{$name}, MANDATORY))
{
	$row_number = 0;
	$interrupted = 0;

	while (my $row = $csv->getline($file))
	{
		last if ($wrong_columns = scalar(@{$row}) != $columns);

		$row_number++;
		print("\033[JProcessing row $row_number" . "\033[G") if ($debug && $row_number % DEBUG_NTH_ROW == 0);

		$no_fails[0] &&= $row->[0] =~ INT;
		$no_fails[1] &&= unique(0, $row);
		$no_fails[2] &&= unique(1, $row);

		if ($row->[1] =~ /^DNS$/i)
		{
			$DNSTYPE_id = $row->[0];
		}

		if (!everything_ok($wrong_columns, \@no_fails) && $fail_immediately)
		{
			print("Interrupted on row $row_number!\n");
			$interrupted = 1;
			last;
		}
	}

	$no_fails[3] &&= $DNSTYPE_id;

	if (!$DNSTYPE_id)
	{
		$DNSTYPE_id = 1;
	}

	print_results($files{$name}, $csv->eof(), $interrupted, $wrong_columns, \@no_fails, \@cases);
	close($file);
}

my %testTypes_id = %{$uniqueness[0]};

# check "tlds" file
$name = 'tlds';
$columns = 2;
@cases = (
	'"id" is an integer',
	'"id" column contains unique values',
	'"tld" column contains unique values'
);
@no_fails = (1) x scalar(@cases);
@uniqueness = ({});

if (open_file(\$file, $files{$name}, MANDATORY))
{
	$row_number = 0;
	$interrupted = 0;

	while (my $row = $csv->getline($file))
	{
		last if ($wrong_columns = scalar(@{$row}) != $columns);

		$row_number++;
		print("\033[JProcessing row $row_number" . "\033[G") if ($debug && $row_number % DEBUG_NTH_ROW == 0);

		$no_fails[0] &&= $row->[0] =~ INT;
		$no_fails[1] &&= unique(0, $row);
		$no_fails[2] &&= unique(1, $row);

		if (!everything_ok($wrong_columns, \@no_fails) && $fail_immediately)
		{
			print("Interrupted on row $row_number!\n");
			$interrupted = 1;
			last;
		}
	}

	print_results($files{$name}, $csv->eof(), $interrupted, $wrong_columns, \@no_fails, \@cases);
	close($file);
}

my %tlds_id = %{$uniqueness[0]};

# check "tldTypes" file
$name = 'tldTypes';
$columns = 2;
@cases = (
	'"id" is an integer',
	'"id" column contains unique values',
	'"tldType" column contains unique values'
);
@no_fails = (1) x scalar(@cases);
@uniqueness = ({});

if (open_file(\$file, $files{$name}, MANDATORY))
{
	$row_number = 0;
	$interrupted = 0;

	while (my $row = $csv->getline($file))
	{
		last if ($wrong_columns = scalar(@{$row}) != $columns);

		$row_number++;
		print("\033[JProcessing row $row_number" . "\033[G") if ($debug && $row_number % DEBUG_NTH_ROW == 0);

		$no_fails[0] &&= $row->[0] =~ INT;
		$no_fails[1] &&= unique(0, $row);
		$no_fails[2] &&= unique(1, $row);

		if (!everything_ok($wrong_columns, \@no_fails) && $fail_immediately)
		{
			print("Interrupted on row $row_number!\n");
			$interrupted = 1;
			last;
		}
	}

	print_results($files{$name}, $csv->eof(), $interrupted, $wrong_columns, \@no_fails, \@cases);
	close($file);
}

my %tldTypes_id = %{$uniqueness[0]};

# check "transportProtocols" file
$name = 'transportProtocols';
$columns = 2;
@cases = (
	'"id" is an integer',
	'"id" column contains unique values',
	'"transportProtocol" column contains unique values'
);
@no_fails = (1) x scalar(@cases);
@uniqueness = ({});

if (open_file(\$file, $files{$name}, MANDATORY))
{
	$row_number = 0;
	$interrupted = 0;

	while (my $row = $csv->getline($file))
	{
		last if ($wrong_columns = scalar(@{$row}) != $columns);

		$row_number++;
		print("\033[JProcessing row $row_number" . "\033[G") if ($debug && $row_number % DEBUG_NTH_ROW == 0);

		$no_fails[0] &&= $row->[0] =~ INT;
		$no_fails[1] &&= unique(0, $row);
		$no_fails[2] &&= unique(1, $row);

		if (!everything_ok($wrong_columns, \@no_fails) && $fail_immediately)
		{
			print("Interrupted on row $row_number!\n");
			$interrupted = 1;
			last;
		}
	}

	print_results($files{$name}, $csv->eof(), $interrupted, $wrong_columns, \@no_fails, \@cases);
	close($file);
}

my %transportProtocols_id = %{$uniqueness[0]};

# check "nsid" file
$name = 'nsid';
$columns = 2;
@cases = (
	'"id" is an integer',
	'"id" column contains unique values',
	'"nsid" column contains unique values'
);
@no_fails = (1) x scalar(@cases);
@uniqueness = ({});

if (open_file(\$file, $files{$name}, ($monitoring_target eq TARGET_RY ? MANDATORY : OPTIONAL)))
{
	$row_number = 0;
	$interrupted = 0;

	while (my $row = $csv->getline($file))
	{
		last if ($wrong_columns = scalar(@{$row}) != $columns);

		$row_number++;
		print("\033[JProcessing row $row_number" . "\033[G") if ($debug && $row_number % DEBUG_NTH_ROW == 0);

		$no_fails[0] &&= $row->[0] =~ INT;
		$no_fails[1] &&= unique(0, $row);
		$no_fails[2] &&= unique(1, $row);

		if (!everything_ok($wrong_columns, \@no_fails) && $fail_immediately)
		{
			print("Interrupted on row $row_number!\n");
			$interrupted = 1;
			last;
		}
	}

	print_results($files{$name}, $csv->eof(), $interrupted, $wrong_columns, \@no_fails, \@cases);
	close($file);
}

my %nsid_id = %{$uniqueness[0]};

# check "target" file
$name = 'target';
$columns = 2;
@cases = (
	'"id" is an integer',
	'"id" column contains unique values',
	'"target" column contains unique values'
);
@no_fails = (1) x scalar(@cases);
@uniqueness = ({});

if (open_file(\$file, $files{$name}, MANDATORY))
{
	$row_number = 0;
	$interrupted = 0;

	while (my $row = $csv->getline($file))
	{
		last if ($wrong_columns = scalar(@{$row}) != $columns);

		$row_number++;
		print("\033[JProcessing row $row_number" . "\033[G") if ($debug && $row_number % DEBUG_NTH_ROW == 0);

		$no_fails[0] &&= $row->[0] =~ INT;
		$no_fails[1] &&= unique(0, $row);
		$no_fails[2] &&= unique(1, $row);

		if (!everything_ok($wrong_columns, \@no_fails) && $fail_immediately)
		{
			print("Interrupted on row $row_number!\n");
			$interrupted = 1;
			last;
		}
	}

	print_results($files{$name}, $csv->eof(), $interrupted, $wrong_columns, \@no_fails, \@cases);
	close($file);
}

my %target_id = %{$uniqueness[0]};

# check "testedName" file
$name = 'testedName';
$columns = 2;
@cases = (
	'"id" is an integer',
	'"id" column contains unique values',
	'"testedName" column contains unique values'
);
@no_fails = (1) x scalar(@cases);
@uniqueness = ({});

if (open_file(\$file, $files{$name}, MANDATORY))
{
	$row_number = 0;
	$interrupted = 0;

	while (my $row = $csv->getline($file))
	{
		last if ($wrong_columns = scalar(@{$row}) != $columns);

		$row_number++;
		print("\033[JProcessing row $row_number" . "\033[G") if ($debug && $row_number % DEBUG_NTH_ROW == 0);

		$no_fails[0] &&= $row->[0] =~ INT;
		$no_fails[1] &&= unique(0, $row);
		$no_fails[2] &&= unique(1, $row);

		if (!everything_ok($wrong_columns, \@no_fails) && $fail_immediately)
		{
			print("Interrupted on row $row_number!\n");
			$interrupted = 1;
			last;
		}
	}

	print_results($files{$name}, $csv->eof(), $interrupted, $wrong_columns, \@no_fails, \@cases);
	close($file);
}

my %testedName_id = %{$uniqueness[0]};

# check "probeChanges" file
$name = 'probeChanges';
$columns = 4;
@cases = (
	'"probeName" is an integer',
	'"probeName" column entries are from "probeNames" "id" column',
	'"probeChangeDateTime" is a timestamp',
	'"probeChangeDateTime" is from requested period',
#	'"probeChangeDateTime" contains unique values', # todo: this should work with probe-timestamp
	'"probestatus" is an integer',
	'"probestatus" column entries are from "statusMaps" "id" column'
);
@no_fails = (1) x scalar(@cases);
@uniqueness = ({});

if (open_file(\$file, $files{$name}, OPTIONAL))
{
	$row_number = 0;
	$interrupted = 0;

	while (my $row = $csv->getline($file))
	{
		last if ($wrong_columns = scalar(@{$row}) != $columns);

		$row_number++;
		print("\033[JProcessing row $row_number" . "\033[G") if ($debug && $row_number % DEBUG_NTH_ROW == 0);

		$no_fails[0] &&= $row->[0] =~ INT;
		$no_fails[1] &&= exists($probeNames_id{$row->[0]});
		$no_fails[2] &&= $row->[1] =~ TIME;
		$no_fails[3] &&= fromperiod($row->[1]);
#		$no_fails[4] &&= unique(1, $row); # todo: this should work with probe-timestamp
		$no_fails[4] &&= $row->[2] =~ INT;
		$no_fails[5] &&= exists($statusMaps_id{$row->[2]});

		if (!everything_ok($wrong_columns, \@no_fails) && $fail_immediately)
		{
			print("Interrupted on row $row_number!\n");
			$interrupted = 1;
			last;
		}
	}

	print_results($files{$name}, $csv->eof(), $interrupted, $wrong_columns, \@no_fails, \@cases);
	close($file);
}

# check "incidents" file
$name = 'incidents';
$columns = 5;
@cases = (
	'"incidentID" is an integer',
	'"incidentID" contains unique values',
	'"incidentStartTime" is a timestamp',
	'"incidentStartTime" is from requested period',
	'"incidentTLD" is an integer',
	'"incidentTLD" column entries are from "tlds" "id" column',
	'"serviceCategory" is an integer',
	'"serviceCategory" column entries are from "serviceCategories" "id" column',
	'"tldType" is an integer',
	'"tldType" column entries are from "tldTypes" "id" column'
);
@no_fails = (1) x scalar(@cases);
@uniqueness = ({});

my %incidents;
my $incident;

if (open_file(\$file, $files{$name}, OPTIONAL))
{
	$row_number = 0;
	$interrupted = 0;

	while (my $row = $csv->getline($file))
	{
		last if ($wrong_columns = scalar(@{$row}) != $columns);

		$row_number++;
		print("\033[JProcessing row $row_number" . "\033[G") if ($debug && $row_number % DEBUG_NTH_ROW == 0);

		$no_fails[0] &&= $row->[0] =~ INT;
		$no_fails[1] &&= unique(0, $row);
		$no_fails[2] &&= $row->[1] =~ TIME;
		$no_fails[3] &&= fromperiod($row->[1]);
		$no_fails[4] &&= $row->[2] =~ INT;
		$no_fails[5] &&= exists($tlds_id{$row->[2]});
		$no_fails[6] &&= $row->[3] =~ INT;
		$no_fails[7] &&= exists($serviceCategory_id{$row->[3]});
		$no_fails[8] &&= $row->[4] =~ INT;
		$no_fails[9] &&= exists($tldTypes_id{$row->[4]});

		$incidents{$row->[0]} = {
			'incidentStartTime' => $row->[1],
			'incidentTLD' => $row->[2],
			'serviceCategory' => $row->[3],
			'tldType' => $row->[4]
		};

		if (!everything_ok($wrong_columns, \@no_fails) && $fail_immediately)
		{
			print("Interrupted on row $row_number!\n");
			$interrupted = 1;
			last;
		}
	}

	print_results($files{$name}, $csv->eof(), $interrupted, $wrong_columns, \@no_fails, \@cases);
	close($file);
}

# check "incidentsEndTime" file
$name = 'incidentsEndTime';
$columns = 3;
@cases = (
	'"incidentID" is an integer',
	'"incidentID" contains unique values',
	'"incidentEndTime" is a timestamp',
	'"incidentEndTime" is from requested period',
	'"incidentEndTime" is greater than "incidents" "incidentStartTime" if "incidentsEndTime" "incidentID" matches "incidents" "incidentID"',
	'"incidentFailedTests" is an integer'
);
@no_fails = (1) x scalar(@cases);
@uniqueness = ({});

if (open_file(\$file, $files{$name}, OPTIONAL))
{
	$row_number = 0;
	$interrupted = 0;

	while (my $row = $csv->getline($file))
	{
		last if ($wrong_columns = scalar(@{$row}) != $columns);

		$row_number++;
		print("\033[JProcessing row $row_number" . "\033[G") if ($debug && $row_number % DEBUG_NTH_ROW == 0);

		$incident = $incidents{$row->[0]};

		$no_fails[0] &&= $row->[0] =~ INT;
		$no_fails[1] &&= unique(0, $row);
		$no_fails[2] &&= $row->[1] =~ TIME;
		$no_fails[3] &&= fromperiod($row->[1]);
		$no_fails[4] &&= after($row->[1], $incident->{'incidentStartTime'}) if ($incident);
		$no_fails[5] &&= $row->[2] =~ INT;

		if (!everything_ok($wrong_columns, \@no_fails) && $fail_immediately)
		{
			print("Interrupted on row $row_number!\n");
			$interrupted = 1;
			last;
		}
	}

	print_results($files{$name}, $csv->eof(), $interrupted, $wrong_columns, \@no_fails, \@cases);
	close($file);
}

# check "falsePositiveChanges" file
$name = 'falsePositiveChanges';
$columns = 4;
@cases = (
	'"incidentID" is an integer',
	'"incidentChangeDateTime" is a timestamp',
	'"incidentChangeDateTime" is from requested period',
	'"incidentChangeDateTime" is greater than "incidents" "incidentStartTime" if "falsePositiveChanges" "incidentID" matches "incidents" "incidentID"',
	'"incidentStatus" is an integer'
);
@no_fails = (1) x scalar(@cases);
@uniqueness = ({});

if (open_file(\$file, $files{$name}, OPTIONAL))
{
	$row_number = 0;
	$interrupted = 0;

	while (my $row = $csv->getline($file))
	{
		last if ($wrong_columns = scalar(@{$row}) != $columns);

		$row_number++;
		print("\033[JProcessing row $row_number" . "\033[G") if ($debug && $row_number % DEBUG_NTH_ROW == 0);

		$incident = $incidents{$row->[0]};

		$no_fails[0] &&= $row->[0] =~ INT;
		$no_fails[1] &&= $row->[1] =~ TIME;
		$no_fails[2] &&= fromperiod($row->[1]);
		$no_fails[3] &&= after($row->[1], $incident->{'incidentStartTime'}) if ($incident);
		$no_fails[4] &&= $row->[2] =~ INT;

		if (!everything_ok($wrong_columns, \@no_fails) && $fail_immediately)
		{
			print("Interrupted on row $row_number!\n");
			$interrupted = 1;
			last;
		}
	}

	print_results($files{$name}, $csv->eof(), $interrupted, $wrong_columns, \@no_fails, \@cases);
	close($file);
}

# check "cycles" file
$name = 'cycles';
$columns = 12;
@cases = (
	'"cycleID" contains unique values',
	'"cycleDateMinute" is a timestamp',
	'"cycleDateMinute" seconds are 00',
	'"cycleDateMinute" is from requested period',
	'"cycleEmergencyThreshold" is a decimal number',
	'"cycleStatus" is an integer',
	'"cycleStatus" column entries are from "statusMaps" "id" column',
	'"incidentID" is an integer or empty',
	'"cycleTLD" is an integer',
	'"cycleTLD" column entries are from "tlds" "id" column',
	'"cycleTLD" matches "incidents" "incidentTLD" if "cycles" "incidentID" matches "incidents" "incidentID"',
	'"serviceCategory" is an integer',
	'"serviceCategory" column entries are from "serviceCategory" "id" column',
	'"serviceCategory" matches "incidents" "serviceCategory" if "cycles" "incidentID" matches "incidents" "incidentID"',
	'"cycleNSFQDN" is an integer or empty',
	'"cycleNSFQDN" column non-empty entries are from "nsFQDNs" "id" column',
	'"cycleNSFQDN" is empty iff "serviceCategory" is not NS',
	'"cycleNSIP" is an integer or empty',
	'"cycleNSIP" column entries are from "ipAddresses" "id" column or empty',
	'"cycleNSIP" is empty iff "serviceCategory" is not NS',
	'"cycleNSIPversion" is an integer or empty',
	'"cycleNSIPversion" column non-empty entries are from "ipVersions" "id" column',
	'"cycleNSIPversion" is empty iff "serviceCategory" is not NS',
	'"tldType" is an integer',
	'"tldType" column entries are from "tldTypes" "id" column',
	'"tldType" matches "incidents" "tldType" if "cycles" "incidentID" matches "incidents" "incidentID"',
	'"cycleNSProtocol" is an integer',
	'"cycleNSProtocol" column entries are from "transportProtocols" "id" column',
	'"cycleID" is the concatenation of "cycleDateMinute", "serviceCategory", "cycleTLD", "cycleNSFQDN", "cycleNSIP"'
);
@no_fails = (1) x scalar(@cases);
@uniqueness = ({});

my %cycles;
my $cycle;

if (open_file(\$file, $files{$name}, MANDATORY))
{
	$row_number = 0;
	$interrupted = 0;

	while (my $row = $csv->getline($file))
	{
		last if ($wrong_columns = scalar(@{$row}) != $columns);

		$row_number++;
		print("\033[JProcessing row $row_number" . "\033[G") if ($debug && $row_number % DEBUG_NTH_ROW == 0);

		$incident = (!empty($row->[4]) ? $incidents{$row->[4]} : undef);

		$no_fails[ 0] &&= unique(0, $row);
		$no_fails[ 1] &&= $row->[1] =~ TIME;
		$no_fails[ 2] &&= isround($row->[1]);
		$no_fails[ 3] &&= fromperiod($row->[1]);
		$no_fails[ 4] &&= ($row->[2] =~ DEC || $row->[6] == $NS_id); # cycleEmergencyThreshold is not implemented for service category "NS"
		$no_fails[ 5] &&= $row->[3] =~ INT;
		$no_fails[ 6] &&= exists($statusMaps_id{$row->[3]});
		$no_fails[ 7] &&= $row->[4] =~ INT                   || empty($row->[4]);
		$no_fails[ 8] &&= $row->[5] =~ INT;
		$no_fails[ 9] &&= exists($tlds_id{$row->[5]});
		$no_fails[10] &&= $row->[5] eq $incident->{'incidentTLD'} if ($incident);
		$no_fails[11] &&= $row->[6] =~ INT;
		$no_fails[12] &&= exists($serviceCategory_id{$row->[6]});
		$no_fails[13] &&= $row->[6] eq $incident->{'serviceCategory'} if ($incident);
		$no_fails[14] &&= $row->[7] =~ INT                   || empty($row->[7]);
		$no_fails[15] &&= exists($nsFQDNs_id{$row->[7]})     || empty($row->[7]);
		$no_fails[16] &&= emptyif($row->[7], $row->[6] ne $NS_id);
		$no_fails[17] &&= $row->[8] =~ INT                   || empty($row->[8]);
		$no_fails[18] &&= exists($ipAddresses_id{$row->[8]}) || empty($row->[8]);
		$no_fails[19] &&= emptyif($row->[8], $row->[6] ne $NS_id);
		$no_fails[20] &&= $row->[9] =~ INT                   || empty($row->[9]);
		$no_fails[21] &&= exists($ipVersions_id{$row->[9]})  || empty($row->[9]);
		$no_fails[22] &&= emptyif($row->[9], $row->[6] ne $NS_id);
		$no_fails[23] &&= $row->[10] =~ INT;
		$no_fails[24] &&= exists($tldTypes_id{$row->[10]});
		$no_fails[25] &&= $row->[10] eq $incident->{'tldType'} if ($incident);
		$no_fails[26] &&= ($row->[11] =~ INT || $row->[6] == $NS_id || $row->[6] == $DNS_id);                          # cycleProtocol is unknown for
		$no_fails[27] &&= (exists($transportProtocols_id{$row->[11]}) || $row->[6] == $NS_id || $row->[6] == $DNS_id); # "NS" and "DNS" service category
		$no_fails[28] &&= $row->[0] eq sprintf("%d%03d%05d%05d%05d", $row->[1], $row->[6], $row->[5], $row->[7] || 0, $row->[8] || 0);

		$cycles{$row->[0]} = {
			'cycleDateMinute' => $row->[1],
			'cycleTLD' => $row->[5],
			'serviceCategory' => $row->[6],
			'cycleNSFQDN' => $row->[7],
			'cycleNSIP' => $row->[8],
			'cycleNSIPversion' => $row->[9],
			'tldType' => $row->[10],
			'cycleNSProtocol' => $row->[11]
		};

		if (!everything_ok($wrong_columns, \@no_fails) && $fail_immediately)
		{
			print("Interrupted on row $row_number!\n");
			$interrupted = 1;
			last;
		}
	}

	print_results($files{$name}, $csv->eof(), $interrupted, $wrong_columns, \@no_fails, \@cases);
	close($file);
}

%incidents = ();

# check "tests" file
$name = 'tests';
$columns = 13;
@cases = (
	'"probeName" is an integer',
	'"probeName" column entries are from "probeNames" "id" column',
	'"cycleDateMinute" is a timestamp',
	'"cycleDateMinute" seconds are 00',
	'"cycleDateMinute" is from requested period',
	'"cycleDateMinute" matches "cycles" "cycleDateMinute" if "tests" "cycleID" matches "cycles" "cycleID"',
	'"testDateTime" is a timestamp',
	'"testDateTime" is from requested period',
	'"testRRT" is an integer',
	'"testTLD" is an integer',
	'"testTLD" column entries are from "tlds" "id" column',
	'"testTLD" matches "cycles" "cycleTLD" if "tests" "cycleID" matches "cycles" "cycleID"',
	'"testProtocol" is an integer',
	'"testProtocol" column entries are from "transportProtocols" "id" column',
	'"testProtocol" matches "cycles" "cycleNSProtocol" if "tests" "cycleID" matches "cycles" "cycleID"',
	'"testIPversion" is an integer',
	'"testIPversion" column entries are from "ipVersions" "id" column',
	'"testIPversion" matches "cycles" "cycleNSIPversion" if "tests" "cycleID" matches "cycles" "cycleID and "cycles" "serviceCategory" is not RDDS"',
	'"testIPaddress" is an integer or empty if "testRRT" is negative',
	'"testIPaddress" column non-empty entries are from "ipAddresses" "id" column',
	'"testIPaddress" matches "cycles" "cycleNSIP" if "tests" "cycleID" matches "cycles" "cycleID" and "cycles" "serviceCategory" is not RDDS',
	'"testType" is an integer',
	'"testType" column entries are from "testTypes" "id" column',
	'"testNSFQDN" is an integer or empty',
	'"testNSFQDN" column non-empty entries are from "nsFQDNs" "id" column',
	'"testNSFQDN" is empty iff "testType" is not DNS',
	'"testNSFQDN" matches "cycles" "cycleNSFQDN" if "tests" "cycleID" matches "cycles" "cycleID"',
	'"tldType" is an integer',
	'"tldType" column entries are from "tldTypes" "id" column',
	'"tldType" matches "cycles" "tldType" if "tests" "cycleID" matches "cycles" "cycleID"',
	'"nsid" is an integer',
	'"nsid" column entries are from "nsid" "id" column'
);
@no_fails = (1) x scalar(@cases);
@uniqueness = ({});

if (open_file(\$file, $files{$name}, MANDATORY))
{
	$row_number = 0;
	$interrupted = 0;

	while (my $row = $csv->getline($file))
	{
		last if ($wrong_columns = scalar(@{$row}) != $columns);

		$row_number++;
		print("\033[JProcessing row $row_number" . "\033[G") if ($debug && $row_number % DEBUG_NTH_ROW == 0);

		$cycle = $cycles{$row->[4]};

		$no_fails[ 0] &&= $row->[0] =~ INT;
		$no_fails[ 1] &&= exists($probeNames_id{$row->[0]});
		$no_fails[ 2] &&= $row->[1] =~ TIME;
		$no_fails[ 3] &&= isround($row->[1]);
		$no_fails[ 4] &&= fromperiod($row->[1]);
		$no_fails[ 5] &&= $row->[1] eq $cycle->{'cycleDateMinute'} if ($cycle);
		$no_fails[ 6] &&= $row->[2] =~ TIME;
		$no_fails[ 7] &&= fromperiod($row->[2]);
		$no_fails[ 8] &&= $row->[3] =~ INT;
		$no_fails[ 9] &&= $row->[5] =~ INT;
		$no_fails[10] &&= exists($tlds_id{$row->[5]});
		$no_fails[11] &&= $row->[5] eq $cycle->{'cycleTLD'} if ($cycle);
		$no_fails[12] &&= $row->[6] =~ INT;
		$no_fails[13] &&= exists($transportProtocols_id{$row->[6]});
		$no_fails[14] &&= $row->[6] eq $cycle->{'cycleNSProtocol'} if ($cycle);
		$no_fails[15] &&= $row->[7] =~ INT                   || empty($row->[7]) && $row->[3] =~ ERR;
		$no_fails[16] &&= exists($ipVersions_id{$row->[7]})  || empty($row->[7]);
		$no_fails[17] &&= $row->[7] eq $cycle->{'cycleNSIPversion'} if ($cycle && $cycle->{'serviceCategory'} ne $RDDS_id);
		$no_fails[18] &&= $row->[8] =~ INT                   || empty($row->[8]) && $row->[3] =~ ERR;
		$no_fails[19] &&= exists($ipAddresses_id{$row->[8]}) || empty($row->[8]);
		$no_fails[20] &&= $row->[8] eq $cycle->{'cycleNSIP'} if ($cycle && $cycle->{'serviceCategory'} ne $RDDS_id);
		$no_fails[21] &&= $row->[9] =~ INT;
		$no_fails[22] &&= exists($testTypes_id{$row->[9]});
		$no_fails[23] &&= $row->[10] =~ INT                  || empty($row->[10]);
		$no_fails[24] &&= exists($nsFQDNs_id{$row->[10]})    || empty($row->[10]);
		$no_fails[25] &&= emptyif($row->[10], $row->[9] ne $DNS_id);
		$no_fails[26] &&= $row->[10] eq $cycle->{'cycleNSFQDN'} if ($cycle);
		$no_fails[27] &&= $row->[11] =~ INT;
		$no_fails[28] &&= exists($tldTypes_id{$row->[11]});
		$no_fails[29] &&= $row->[11] eq $cycle->{'tldType'} if ($cycle);
		$no_fails[30] &&= ($row->[12] =~ INT || $row->[9] != $DNSTYPE_id);
		$no_fails[31] &&= (exists($nsid_id{$row->[12]}) || $row->[9] != $DNSTYPE_id);

		if (!everything_ok($wrong_columns, \@no_fails) && $fail_immediately)
		{
			print("Interrupted on row $row_number!\n");
			$interrupted = 1;
			last;
		}
	}

	print_results($files{$name}, $csv->eof(), $interrupted, $wrong_columns, \@no_fails, \@cases);
	close($file);
}

# check "nsTests" file
$name = 'nsTests';
$columns = 7;
@cases = (
	'"probeName" is an integer',
	'"probeName" column entries are from "probeNames" "id" column',
	'"nsFQDN" is an integer',
	'"nsFQDN" column entries are from "nsFQDNs" "id" column',
	'"nsFQDN" matches "cycles" "cycleNSFQDN" if "nsTests" "cycleID" matches "cycles" "cycleID"',
	'"nsTestTLD" is an integer',
	'"nsTestTLD" column entries are from "tlds" "id" column',
	'"nsTestTLD" matches "cycles" "cycleTLD" if "nsTests" "cycleID" matches "cycles" "cycleID"',
	'"cycleDateMinute" is a timestamp',
	'"cycleDateMinute" seconds are 00',
	'"cycleDateMinute" is from requested period',
	'"cycleDateMinute" matches "cycles" "cycleDateMinute" if "nsTests" "cycleID" matches "cycles" "cycleID"',
	'"nsTestStatus" is an integer',
	'"nsTestStatus" column entries are from "statusMaps" "id" column',
	'"tldType" is an integer',
	'"tldType" column entries are from "tldTypes" "id" column',
	'"tldType" matches "cycles" "tldType" if "nsTests" "cycleID" matches "cycles" "cycleID"',
	'"nsTestProtocol" is an integer',
	'"nsTestProtocol" column entries are from "transportProtocols" "id" column',
	'"nsTestProtocol" matches "cycles" "cycleNSProtocol" if "nsTests" "cycleID" matches "cycles" "cycleID"'
);
@no_fails = (1) x scalar(@cases);
@uniqueness = ({});

if (open_file(\$file, $files{$name}, ($monitoring_target eq TARGET_RY ? MANDATORY : OPTIONAL)))
{
	$row_number = 0;
	$interrupted = 0;

	while (my $row = $csv->getline($file))
	{
		last if ($wrong_columns = scalar(@{$row}) != $columns);

		$row_number++;
		print("\033[JProcessing row $row_number" . "\033[G") if ($debug && $row_number % DEBUG_NTH_ROW == 0);

		$cycle = $cycles{$row->[5]};

		$no_fails[ 0] &&= $row->[0] =~ INT;
		$no_fails[ 1] &&= exists($probeNames_id{$row->[0]});
		$no_fails[ 2] &&= $row->[1] =~ INT;
		$no_fails[ 3] &&= exists($nsFQDNs_id{$row->[1]});
		$no_fails[ 4] &&= $row->[1] eq $cycle->{'cycleNSFQDN'} if ($cycle);
		$no_fails[ 5] &&= $row->[2] =~ INT;
		$no_fails[ 6] &&= exists($tlds_id{$row->[2]});
		$no_fails[ 7] &&= $row->[2] eq $cycle->{'cycleTLD'} if ($cycle);
		$no_fails[ 8] &&= $row->[3] =~ TIME;
		$no_fails[ 9] &&= isround($row->[3]);
		$no_fails[10] &&= fromperiod($row->[3]);
		$no_fails[11] &&= $row->[3] eq $cycle->{'cycleDateMinute'} if ($cycle);
		$no_fails[12] &&= $row->[4] =~ INT;
		$no_fails[13] &&= exists($statusMaps_id{$row->[4]});
		$no_fails[14] &&= $row->[5] =~ INT;
		$no_fails[15] &&= exists($tldTypes_id{$row->[5]});
		$no_fails[16] &&= $row->[5] eq $cycle->{'tldType'} if ($cycle);
		$no_fails[17] &&= $row->[6] =~ INT;
		$no_fails[18] &&= exists($transportProtocols_id{$row->[6]});
		$no_fails[19] &&= $row->[6] eq $cycle->{'cycleNSProtocol'} if ($cycle);

		if (!everything_ok($wrong_columns, \@no_fails) && $fail_immediately)
		{
			print("Interrupted on row $row_number!\n");
			$interrupted = 1;
			last;
		}
	}

	print_results($files{$name}, $csv->eof(), $interrupted, $wrong_columns, \@no_fails, \@cases);
	close($file);
}

# check "testDetails" file
$name = 'testDetails';
$columns = 6;
@cases = (
	'"probeName" is an integer',
	'"probeName" column entries are from "probeNames" "id" column',
	'"cycleDateMinute" is a timestamp',
	'"cycleDateMinute" seconds are 00',
	'"cycleDateMinute" is from requested period',
	'"serviceCategory" is an integer',
	'"serviceCategory" column entries are from "serviceCategory" "id" column',
	'"testType" is an integer',
	'"testType" column entries are from "testTypes" "id" column',
	'"target" is an integer',
	'"target" column entries are from "target" "id" column',
	'"testedName" is an integer',
	'"testedName" column entries are from "testedName" "id" column'
);
@no_fails = (1) x scalar(@cases);
@uniqueness = ({});

if (open_file(\$file, $files{$name}, OPTIONAL))
{
	$row_number = 0;
	$interrupted = 0;

	while (my $row = $csv->getline($file))
	{
		last if ($wrong_columns = scalar(@{$row}) != $columns);

		$row_number++;
		print("\033[JProcessing row $row_number" . "\033[G") if ($debug && $row_number % DEBUG_NTH_ROW == 0);

		$cycle = $cycles{$row->[5]};

		$no_fails[ 0] &&= $row->[0] =~ INT;
		$no_fails[ 1] &&= exists($probeNames_id{$row->[0]});
		$no_fails[ 2] &&= $row->[1] =~ TIME;
		$no_fails[ 3] &&= isround($row->[1]);
		$no_fails[ 4] &&= fromperiod($row->[1]);
		$no_fails[ 5] &&= $row->[2] =~ INT;
		$no_fails[ 6] &&= exists($serviceCategory_id{$row->[2]});
		$no_fails[ 7] &&= $row->[3] =~ INT;
		$no_fails[ 8] &&= exists($testTypes_id{$row->[3]});
		$no_fails[ 9] &&= $row->[4] =~ INT                  || empty($row->[4]);
		$no_fails[10] &&= exists($target_id{$row->[4]})     || empty($row->[4]);
		$no_fails[11] &&= $row->[5] =~ INT                  || empty($row->[5]);
		$no_fails[12] &&= exists($testedName_id{$row->[5]}) || empty($row->[5]);

		if (!everything_ok($wrong_columns, \@no_fails) && $fail_immediately)
		{
			print("Interrupted on row $row_number!\n");
			$interrupted = 1;
			last;
		}
	}

	print_results($files{$name}, $csv->eof(), $interrupted, $wrong_columns, \@no_fails, \@cases);
	close($file);
}

# check "minns" file
$name = 'minns';
$columns = 3;
@cases = (
	'"minnsTLD" is an integer',
	'"minnsTLD" column entries are from "tlds" "id" column',
	'"minns" is an integer',
	'"cycleDateMinute" is a timestamp',
	'"cycleDateMinute" seconds are 00',
	'"cycleDateMinute" is from requested period',
);
@no_fails = (1) x scalar(@cases);
@uniqueness = ({});

if (open_file(\$file, $files{$name}, ($monitoring_target eq TARGET_RY ? MANDATORY : OPTIONAL)))
{
	$row_number = 0;
	$interrupted = 0;

	while (my $row = $csv->getline($file))
	{
		last if ($wrong_columns = scalar(@{$row}) != $columns);

		$row_number++;
		print("\033[JProcessing row $row_number" . "\033[G") if ($debug && $row_number % DEBUG_NTH_ROW == 0);

		$no_fails[0] &&= $row->[0] =~ INT;
		$no_fails[1] &&= exists($tlds_id{$row->[0]});
		$no_fails[2] &&= $row->[1] =~ INT;
		$no_fails[3] &&= $row->[2] =~ TIME;
		$no_fails[4] &&= isround($row->[2]);
		$no_fails[5] &&= fromperiod($row->[2]);

		if (!everything_ok($wrong_columns, \@no_fails) && $fail_immediately)
		{
			print("Interrupted on row $row_number!\n");
			$interrupted = 1;
			last;
		}
	}

	print_results($files{$name}, $csv->eof(), $interrupted, $wrong_columns, \@no_fails, \@cases);
	close($file);
}

%cycles = ();

exit !$all_tests_successful;
