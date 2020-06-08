#!/usr/bin/perl

use strict;
use warnings;

use Path::Tiny;
use lib path($0)->parent()->parent()->realpath()->stringify();
use lib '/opt/zabbix/scripts';

use RSM;
use RSMSLV;
use ApiHelper;
use JSON::XS qw(decode_json encode_json);
use Data::Dumper;
use Data::Compare;

$Data::Dumper::Terse = 1;       # do not output names like "$VAR1 = "
$Data::Dumper::Pair = ": ";     # use separator instead of " => "
$Data::Dumper::Useqq = 1;       # use double quotes instead of single quotes
$Data::Dumper::Indent = 1;      # 1 provides less indentation instead of 2

parse_opts();
setopt('nolog');

use constant DIR => path($0)->parent()->realpath()->stringify() . '/probe-results-data';

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
	'dnssec' => 60,
	'rdds' => 300,
	'rdap' => 300,
};

my $now = time() - 3600 * 24 * 1;

set_slv_config(get_rsm_config());

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

#	print("$service itemids_uint:\n", Dumper($itemids_uint));
#	print("$service results_uint:\n", Dumper($results_uint));
#	print("$service itemids_float:\n", Dumper($itemids_float));
#	print("$service results_float:\n", Dumper($results_float));
#	print("$service itemids_str:\n", Dumper($itemids_str));
#	print("$service results_str:\n", Dumper($results_str));
#	print("$service item_data:\n", Dumper($item_data));

#	db_connect();
#	setopt('debug');
#	my $probe_history = get_test_history(
#		$from,
#		$till,
#		$itemids_uint,
#		$itemids_float,
#		$itemids_str,
#		\$results_uint,
#		\$results_float,
#		\$results_str
#	);
#	db_disconnect();
#
#	print("$service probe history:\n", Dumper($probe_history));
#
#	exit;

	my $probe_results = get_test_results(
		$service,
		[@{$results_uint}, @{$results_float}, @{$results_str}],
		$item_data
	);

	my ($buf, $error);

	my $file = "@{[DIR]}/$service-expected-probe-results.json";

	if (SUCCESS != read_file($file, \$buf, \$error))
	{
		fail("$file: $error");
	}

	my $expected_probe_results = decode_json($buf);

	my $c = new Data::Compare;
	my $res = $c->Cmp($probe_results, $expected_probe_results);

	print("$service\t: collected and expected probe results are ", ($res ? "" : "not "), "identical.\n");

	if (!$res)
	{
		print("---------------------------------------------\n");
		print("Collected results:\n", Dumper($probe_results));
		print("---------------------------------------------\n");
		print("Expected results:\n", Dumper($expected_probe_results));
		print("Error: collected $service data structure is unexpected, see above\n");

		exit -1;
	}
}

print("OK: function RSMSLV::probe_results() is working correctly.\n");
