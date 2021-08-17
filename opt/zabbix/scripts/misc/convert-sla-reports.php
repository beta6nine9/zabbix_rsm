#!/usr/bin/php
<?php

require_once dirname(__FILE__) . "/../CSlaReport.php";

main($argv);

function main($argv)
{
	date_default_timezone_set("UTC");

	$start_time = microtime(true);

	$args = parseArgs($argv);

	try
	{
		print("Connecting to the database\n");

		CSlaReport::dbConnect($args["server_id"]);

		print("Reading XML reports\n");

		$sql = "select" .
					" sla_reports.hostid," .
					"hosts.host," .
					"sla_reports.year," .
					"sla_reports.month," .
					"sla_reports.report_xml" .
				" from" .
					" sla_reports" .
					" inner join hosts on hosts.hostid=sla_reports.hostid" .
				" where" .
					" sla_reports.report_json=''" .
				" order by" .
					" hosts.host asc," .
					" sla_reports.year asc," .
					" sla_reports.month asc";
		$rows = CSlaReport::dbSelect($sql);

		print("Converting XML reports to JSON reports\n");

		if (!$args["dry_run"])
		{
			CSlaReport::dbBeginTransaction();
		}

		foreach ($rows as $row)
		{
			list($hostid, $host, $year, $month, $report_xml) = $row;

			print("Converting report for rsmhost '{$host}', year {$year}, month {$month}\n");

			$report_json = convertReport($report_xml);

			if ($args["dry_run"])
			{
				print(json_encode(json_decode($report_json), JSON_PRETTY_PRINT) . "\n");
			}
			else
			{
				$sql = "update sla_reports set report_json=? where hostid=? and year=? and month=?";
				$params = [$report_json, $hostid, $year, $month];
				CSlaReport::dbExecute($sql, $params);
			}
		}

		if (!$args["dry_run"])
		{
			CSlaReport::dbCommit();
		}

		print("Disconnecting from the database\n");

		CSlaReport::dbDisconnect();
	}
	catch (Exception $e)
	{
		if (CSlaReport::dbConnected())
		{
			CSlaReport::dbRollBack();
			CSlaReport::dbDisconnect();
		}

		$error = $e->getMessage();

		if (defined("DEBUG") && DEBUG === true)
		{
			$error .= "\n" . $e->getTraceAsString();
		}

		fail($error);
	}

	if (defined("STATS") && STATS === true)
	{
		printf("(STATS) Report count - %d\n", count($rows));
		printf("(STATS) Total time   - %.6f\n", microtime(true) - $start_time);
		printf("(STATS) Mem usage    - %.2f MB\n", memory_get_peak_usage(true) / 1024 / 1024);
	}
}

function convertReport($report_xml)
{
	$xml = simplexml_load_string($report_xml);

	$json = [
		"$" => [
			"id"                 => (string)$xml['id'],
			"generationDateTime" => (int)$xml['generationDateTime'],
			"reportPeriodFrom"   => (int)$xml['reportPeriodFrom'],
			"reportPeriodTo"     => (int)$xml['reportPeriodTo']
		]
	];

	if (isset($xml->DNS))
	{
		$json["DNS"] = [
			"serviceAvailability" => [
				"value" => (string)$xml->DNS->serviceAvailability,
				"$"     => [
					"downtimeSLR" => (int)$xml->DNS->serviceAvailability["downtimeSLR"]
				]
			],
			"nsAvailability" => [
			],
			"rttUDP" => [
				"value" => (string)$xml->DNS->rttUDP,
				"$"     => [
					"rttSLR"        => (int)$xml->DNS->rttUDP["rttSLR"],
					"percentageSLR" => (int)$xml->DNS->rttUDP["percentageSLR"]
				]
			],
			"rttTCP" => [
				"value" => (string)$xml->DNS->rttTCP,
				"$"     => [
					"rttSLR"        => (int)$xml->DNS->rttTCP["rttSLR"],
					"percentageSLR" => (int)$xml->DNS->rttTCP["percentageSLR"]
				]
			]
		];

		foreach ($xml->DNS->nsAvailability as $nsAvailability)
		{
			array_push(
				$json["DNS"]["nsAvailability"],
				[
					"value" => (string)$nsAvailability,
					"$"     => [
						"hostname"    => (string)$nsAvailability["hostname"],
						"ipAddress"   => (string)$nsAvailability["ipAddress"],
						"from"        => (int)$nsAvailability["from"],
						"to"          => (int)$nsAvailability["to"],
						"downtimeSLR" => (int)$nsAvailability["downtimeSLR"]
					]
				]
			);
		}

	}

	if (isset($xml->RDDS))
	{
		$json["RDDS"] = [
			"serviceAvailability" => [
				"value" => (string)$xml->RDDS->serviceAvailability,
				"$"     => [
					"downtimeSLR" => (int)$xml->RDDS->serviceAvailability["downtimeSLR"]
				]
			],
			"rtt" => [
				"value" => (string)$xml->RDDS->rtt,
				"$"     => [
					"rttSLR"        => (int)$xml->RDDS->rtt["rttSLR"],
					"percentageSLR" => (int)$xml->RDDS->rtt["percentageSLR"]
				]
			]
		];
	}

	if (isset($xml->RDAP))
	{
		$json["RDAP"] = [
			"serviceAvailability" => [
				"value" => (string)$xml->RDAP->serviceAvailability,
				"$"     => [
					"downtimeSLR" => (int)$xml->RDAP->serviceAvailability["downtimeSLR"]
				]
			],
			"rtt" => [
				"value" => (string)$xml->RDAP->rtt,
				"$"     => [
					"rttSLR"        => (int)$xml->RDAP->rtt["rttSLR"],
					"percentageSLR" => (int)$xml->RDAP->rtt["percentageSLR"]
				]
			]
		];
	}

	$json = ["reportTLD" => $json];

	return json_encode($json);
}

function parseArgs($argv)
{
	$args = [
		"dry_run"   => false,
		"server_id" => null,
	];

	$script = array_shift($argv);

	while ($arg = array_shift($argv))
	{
		switch ($arg)
		{
			case "--help":
				usage($script);
				break;

			case "--server-id":
				$args["server_id"] = array_shift($argv);
				if (!ctype_digit($args["server_id"]))
				{
					usage($script, "Value of --server-id must be a number, got: {$args["server_id"]}");
				}
				$args["server_id"] = (int)$args["server_id"];
				break;

			case "--debug":
				define("DEBUG", true);
				break;

			case "--stats":
				define("STATS", true);
				break;

			case "--dry-run":
				$args["dry_run"] = true;
				break;

			default:
				usage($script, "Invalid argument: {$arg}");
				break;
		}
	}

	if (is_null($args["server_id"]))
	{
		$args["server_id"] = getLocalServerId();
	}

	return $args;
}

function getLocalServerId()
{
	$conf_file = "/opt/zabbix/scripts/rsm.conf";

	if (!is_file($conf_file))
	{
		fail("File not found: {$conf_file}");
	}
	if (!is_readable($conf_file))
	{
		fail("File is not readable: {$conf_file}");
	}

	// PHP 5.3.0 - Hash marks (#) should no longer be used as comments and will throw a deprecation warning if used.
	// PHP 7.0.0 - Hash marks (#) are no longer recognized as comments.

	$conf_string = file_get_contents($conf_file);
	$conf_string = preg_replace("/^\s*#.*$/m", "", $conf_string);

	$conf = parse_ini_string($conf_string, true);

	if ($conf === false)
	{
		fail("Failed to parse {$conf_file}");
	}

	if (!preg_match("/\d+$/", $conf["local"], $id))
	{
		fail("Failed to get ID of local server");
	}

	return (int)$id[0];
}

function usage($script, $error_message = NULL)
{
	if (!is_null($error_message))
	{
		echo "Error:\n";
		echo "        {$error_message}\n";
		echo "\n";
	}

	echo "Usage:\n";
	echo "        {$script} [--help] [--debug] [--stats] [--dry-run] [--server-id <server_id>]\n";
	echo "\n";

	echo "Options:\n";
	echo "        --help\n";
	echo "                Print a brief help message and exit.\n";
	echo "\n";
	echo "        --debug\n";
	echo "                Run the script in debug mode. This means printing more information.\n";
	echo "\n";
	echo "        --stats\n";
	echo "                Print some statistics that are collected during runtime.\n";
	echo "\n";
	echo "        --dry-run\n";
	echo "                Print data to the screen, do not write anything to the filesystem.\n";
	echo "\n";
	echo "        --server-id <server_id>\n";
	echo "                Specify ID of Zabbix server.\n";
	echo "\n";

	if (!is_null($error_message))
	{
		fail($error_message);
	}
	else
	{
		exit(0);
	}
}

function fail($error_message)
{
	error_log("(ERROR) " . $error_message);
	exit(1);
}
