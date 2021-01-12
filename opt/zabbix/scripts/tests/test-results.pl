#!/usr/bin/env perl

use FindBin;
use lib "$FindBin::RealBin/..";

use strict;
use warnings;

use RSM;
use RSMSLV;
use ApiHelper;
use JSON::XS qw(decode_json);
use Data::Dumper;
use Data::Compare;
use Path::Tiny;

$Data::Dumper::Terse = 1;       # do not output names like "$VAR1 = "
$Data::Dumper::Pair = ": ";     # use separator instead of " => "
$Data::Dumper::Useqq = 1;       # use double quotes instead of single quotes
$Data::Dumper::Indent = 1;      # 1 provides less indentation instead of 2

parse_opts();
setopt('nolog');

use constant DIR => path($0)->parent()->realpath()->stringify() . '/test-results-data';

sub read_service_data($$$$$$$$)
{
	my $service = shift;
	my $itemids_uint_buf = shift;
	my $itemids_float_buf = shift;
	my $itemids_str_buf = shift;
	my $results_uint_buf = shift;
	my $results_float_buf = shift;
	my $results_str_buf = shift;
	my $item_data_buf = shift;

	my ($file, $buf, $error);

	$file = "@{[DIR]}/$service-itemids-uint-encoded.txt";
	if (SUCCESS != read_file($file, \$buf, \$error))
	{
		fail("$file: $error");
	}
	$$itemids_uint_buf = decode_json($buf);

	$file = "@{[DIR]}/$service-itemids-float-encoded.txt";
	if (SUCCESS != read_file($file, \$buf, \$error))
	{
		fail("$file: $error");
	}
	$$itemids_float_buf = decode_json($buf);

	$file = "@{[DIR]}/$service-itemids-str-encoded.txt";
	if (SUCCESS != read_file($file, \$buf, \$error))
	{
		fail("$file: $error");
	}
	$$itemids_str_buf = decode_json($buf);

	$file = "@{[DIR]}/$service-results-uint-encoded.txt";
	if (SUCCESS != read_file($file, \$buf, \$error))
	{
		fail("$file: $error");
	}
	$$results_uint_buf = decode_json($buf);

	$file = "@{[DIR]}/$service-results-float-encoded.txt";
	if (SUCCESS != read_file($file, \$buf, \$error))
	{
		fail("$file: $error");
	}
	$$results_float_buf = decode_json($buf);

	$file = "@{[DIR]}/$service-results-str-encoded.txt";
	if (SUCCESS != read_file($file, \$buf, \$error))
	{
		fail("$file: $error");
	}
	$$results_str_buf = decode_json($buf);

	$file = "@{[DIR]}/$service-probe-data-encoded.txt";
	if (SUCCESS != read_file($file, \$buf, \$error))
	{
		fail("$file: $error");
	}
	$$item_data_buf = decode_json($buf);
}

my ($itemids_uint, $itemids_float, $itemids_str, $results_uint, $results_float, $results_str, $item_data);

my $services = {
	'dns' => 60,
	'rdds' => 300,
	'rdap' => 300,
};

my $now = time() - 3600 * 24 * 1;

set_slv_config(get_rsm_config());

db_connect();

foreach my $service (sort(keys(%{$services})))
{
	my $from = cycle_start($now, $services->{$service});
	my $till = $from + $services->{$service} - 1;

	read_service_data(
		$service,
		\$itemids_uint,
		\$itemids_float,
		\$itemids_str,
		\$results_uint,
		\$results_float,
		\$results_str,
		\$item_data
	);

	my $results = get_test_results(
		[@{$results_uint}, @{$results_float}, @{$results_str}],
		$item_data,
		$service
	);

	my ($buf, $error);

	my $file = "@{[DIR]}/$service-expected-test-results.json";

	if (SUCCESS != read_file($file, \$buf, \$error))
	{
		fail("$file: $error");
	}

	my $expected_results = decode_json($buf);

	my $c = new Data::Compare;
	my $res = $c->Cmp($results, $expected_results);

	print("$service\t: collected and expected probe results are ", ($res ? "" : "not "), "identical.\n");

	if (!$res)
	{
		print("---------------------------------------------\n");
		print("Collected results:\n", Dumper($results));
		print("---------------------------------------------\n");
		print("Expected results:\n", Dumper($expected_results));
		print("Error: collected $service data structure is unexpected, see above\n");

		db_disconnect();

		exit -1;
	}
}

db_disconnect();

print("OK: function RSMSLV::results() is working correctly.\n");
