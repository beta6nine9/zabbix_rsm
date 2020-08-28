#!/usr/bin/php
<?php

require_once dirname(__FILE__) . "/CSlaReport.php";

main($argv);

function main($argv)
{
	date_default_timezone_set("UTC");

	$start_time = microtime(true);

	$args = parseArgs($argv);

	if (!$args["dry_run"] && !$args["force"])
	{
		$curr_year  = (int)date("Y");
		$curr_month = (int)date("n");
		if ($args["year"] >= $curr_year && $args["month"] >= $curr_month)
		{
			fail(sprintf("Cannot generate reports for %04d-%02d, month hasn't ended yet", $args["year"], $args["month"]));
		}
	}

	printf("Generating reports (server-id: %d, year: %d, month: %d)\n", $args["server_id"], $args["year"], $args["month"]);

	$reports = CSlaReport::generate($args["server_id"], $args["tlds"], $args["year"], $args["month"], ["XML", "JSON"]);
	if (is_null($reports))
	{
		fail(CSlaReport::$error);
	}

	if ($args["dry_run"])
	{
		foreach ($reports as $report)
		{
			print(str_pad(" {$report["host"]} ", 120, "=", STR_PAD_BOTH) . "\n");

			if ($args["xml"] && $args["json"])
			{
				echo $report["report"]["XML"];
				echo $report["report"]["JSON"] . "\n";
			}
			elseif ($args["xml"] || !$args["json"])
			{
				echo $report["report"]["XML"];
			}
			elseif ($args["json"])
			{
				echo json_encode(json_decode($report["report"]["JSON"]), JSON_PRETTY_PRINT) . "\n";
			}
		}
		print(str_repeat("=", 120) . "\n");
	}
	else
	{
		try
		{
			print("Saving reports to the database\n");

			CSlaReport::dbConnect($args["server_id"]);

			CSlaReport::dbBeginTransaction();

			$sql = "insert into sla_reports (hostid,year,month,report_xml,report_json) values (?,?,?,?,?)" .
					" on duplicate key update report_xml=?,report_json=?";

			foreach ($reports as $report)
			{
				$params = [
					$report["hostid"],
					$args["year"],
					$args["month"],
					$report["report"]["XML"],
					$report["report"]["JSON"],
					$report["report"]["XML"],
					$report["report"]["JSON"],
				];

				CSlaReport::dbExecute($sql, $params);
			}

			CSlaReport::dbCommit();

			CSlaReport::dbDisconnect();
		}
		catch (Exception $e)
		{
			CSlaReport::dbRollBack();

			CSlaReport::dbDisconnect();

			$error = $e->getMessage();

			if (defined("DEBUG") && DEBUG === true)
			{
				$error .= "\n" . $e->getTraceAsString();
			}

			fail($error);
		}
	}

	if (defined("STATS") && STATS === true)
	{
		printf("(STATS) Report count - %d\n", count($reports));
		printf("(STATS) Total time   - %.6f\n", microtime(true) - $start_time);
		printf("(STATS) Mem usage    - %.2f MB\n", memory_get_peak_usage(true) / 1024 / 1024);
	}
}

function parseArgs($argv)
{
	$args = [
		"dry_run"   => false,
		"xml"       => false,
		"json"      => false,
		"server_id" => null,
		"tlds"      => [],
		"year"      => null,
		"month"     => null,
		"force"     => false,
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

			case "--tld":
				$tld = array_shift($argv);
				if ($tld[0] === "-")
				{
					usage($script, "Value of --tld must be a TLD name, got: {$tld}");
				}
				if (in_array($tld, $args["tlds"]))
				{
					usage($script, "TLD was specified multiple times: {$tld}");
				}
				array_push($args["tlds"], $tld);
				break;

			case "--year":
				$args["year"] = array_shift($argv);
				if (!ctype_digit($args["year"]))
				{
					usage($script, "Value of --year must be a number, got: {$args["year"]}");
				}
				$args["year"] = (int)$args["year"];
				break;

			case "--month":
				$args["month"] = array_shift($argv);
				if (!ctype_digit($args["month"]))
				{
					usage($script, "Value of --month must be a number, got: {$args["month"]}");
				}
				$args["month"] = (int)$args["month"];
				if ($args["month"] < 1 || $args["month"] > 12)
				{
					usage($script, "Invalid value of --month: {$args["month"]}");
				}
				break;

			case "--force":
				$args["force"] = true;
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

			case "--xml":
				$args["xml"] = true;
				break;

			case "--json":
				$args["json"] = true;
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

	if (is_null($args["year"]))
	{
		$args["year"] = (int)date("Y");
	}

	if (is_null($args["month"]))
	{
		$args["month"] = (int)date("n") - 1;
		if ($args["month"] === 0)
		{
			$args["year"]  = $args["year"] - 1;
			$args["month"] = 12;
		}
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

	$conf = parse_ini_string($conf_string, true, INI_SCANNER_RAW);

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
	echo "        {$script} [--help] [--debug] [--stats] [--dry-run] [--xml] [--json] [--server-id <server_id>] [--tld <tld>] [--year <year>] [--month <month>] [--force]\n";
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
	echo "        --xml\n";
	echo "                When running with --dry-run, generate report in XML format (default when format is not specified).\n";
	echo "\n";
	echo "        --json\n";
	echo "                When running with --dry-run, generate report in JSON format.\n";
	echo "\n";
	echo "        --server-id <server_id>\n";
	echo "                Specify ID of Zabbix server.\n";
	echo "\n";
	echo "        --tld <tld>\n";
	echo "                Specify TLD name.\n";
	echo "\n";
	echo "        --year <year>\n";
	echo "                Specify the year of the report.\n";
	echo "\n";
	echo "        --month <month>\n";
	echo "                Specify the month of the report (1 through 12).\n";
	echo "\n";
	echo "        --force\n";
	echo "                Generate and save report even if specified year/month hasn't ended yet.\n";
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
