<?php

class CSlaReport
{
	const MONITORING_TARGET_MACRO     = "{\$RSM.MONITORING.TARGET}";
	const MONITORING_TARGET_REGISTRY  = "registry";
	const MONITORING_TARGET_REGISTRAR = "registrar";
	const STANDALONE_RDAP_MACRO       = "{\$RSM.RDAP.STANDALONE}";

	const RSMHOST_DNS_NS_LOG_ACTION_CREATE  = 0;
	const RSMHOST_DNS_NS_LOG_ACTION_ENABLE  = 1;
	const RSMHOST_DNS_NS_LOG_ACTION_DISABLE = 2;

	public static $error;

	private static $sql_count;
	private static $sql_time;
	private static $dbh;

	/**
	 * Generates SLA reports.
	 *
	 * @param int   $server_id ID of server in config file
	 * @param array $tlds      array of TLD names; if empty, reports for all TLDs will be generated
	 * @param int   $year      year
	 * @param int   $month     month
	 *
	 * @static
	 *
	 * @return array|null Returns array of reports or NULL on error. Use CSlaReport::$error to get the erorr message.
	 */
	public static function generate($server_id, $tlds, $year, $month)
	{
		if (!is_int($server_id))
		{
			self::$error = "\$server_id must be integer";
			return null;
		}
		if (!is_array($tlds))
		{
			self::$error = "\$tlds must be array";
			return null;
		}
		if (!is_int($year))
		{
			self::$error = "\$year must be integer";
			return null;
		}
		if (!is_int($month))
		{
			self::$error = "\$month must be integer";
			return null;
		}

		$duplicate_tlds = array_keys(array_diff(array_count_values($tlds), array_count_values(array_unique($tlds))));
		if (count($duplicate_tlds) > 0)
		{
			self::$error = "\$tlds contains duplicate values: " . implode(", ", $duplicate_tlds);
			return null;
		}

		$time = time();
		$from = gmmktime(0, 0, 0, $month, 1, $year);
		$till = gmmktime(0, 0, -1, $month + 1, 1, $year);
		$till = min($till, $time);

		if ($from > $time)
		{
			self::$error = sprintf("%d-%02d seems to be a future date", $year, $month);
			return null;
		}

		self::$error = NULL;

		$default_timezone = date_default_timezone_get();
		date_default_timezone_set("UTC");

		if (defined("DEBUG") && DEBUG === true)
		{
			printf("(DEBUG) %s() server_id - %d\n", __method__, $server_id);
			printf("(DEBUG) %s() tlds  - %s\n", __method__, implode(", ", $tlds));
			printf("(DEBUG) %s() year  - %d\n", __method__, $year);
			printf("(DEBUG) %s() month - %d\n", __method__, $month);
			printf("(DEBUG) %s() from  - %s\n", __method__, date("c", $from));
			printf("(DEBUG) %s() till  - %s\n", __method__, date("c", $till));
		}

		$error_handler = function($severity, $message, $file, $line)
		{
			throw new \Exception($message);
		};
		set_error_handler($error_handler);

		try
		{
			/*
				$data = [
					$hostid => [
						"host" => string,
						"dns"  => [
							"availability" => int,
							"ns" => [
								$itemid => [
									"hostname"     => string,
									"ipAddress"    => string,
									"availability" => int,
									"from"         => int,
									"to"           => int,
								],
								...
							],
							"rttUDP" => float,
							"rttTCP" => float,
						],
						"rdds" => [
							"enabled"      => true|false,
							"availability" => int,
							"rtt"          => float,
						],
						"rdap" => [
							"enabled"      => true|false,
							"availability" => int,
							"rtt"          => float,
						],
					],
					...
				];
			*/

			// TODO: how to handle cases when there's no data? e.g., "--year 2017"

			self::dbConnect($server_id);

			$data = self::collectData($tlds, $from, $till);

			self::validateData($data);

			$slrs = self::getSlrValues($from);

			$reports = self::generateXml($tlds, $data, $slrs, $time, $from, $till);
		}
		catch (Exception $e)
		{
			$reports = null;

			self::$error = $e->getMessage();

			if (defined("DEBUG") && DEBUG === true)
			{
				self::$error .= "\n" . $e->getTraceAsString();
			}
		}

		restore_error_handler();

		self::dbDisconnect();

		date_default_timezone_set($default_timezone);

		return $reports;
	}

	private static function collectData($tlds, $from, $till)
	{
		$data = [];

		// get hostid of TLDs

		$rows = self::getTldHostIds($tlds);

		foreach ($rows as $row)
		{
			list($hostid, $host) = $row;

			$data[$hostid] = [
				"host" => $host,
				"dns"  => [
					"availability" => null,
					"ns" => [],
					"rttUDP" => null,
					"rttTCP" => null,
				],
				"rdds" => [
					"enabled"      => null,
					"availability" => null,
					"rtt"          => null,
				],
			];

			if (self::isRdapStandalone($from))
			{
				$data[$hostid] += [
					"rdap" => [
						"enabled"      => null,
						"availability" => null,
						"rtt"          => null,
					],
				];
			}
		}

		if (count($tlds) > 0 && count($tlds) != count($data))
		{
			$existing_tlds = [];
			foreach ($data as $tld)
			{
				array_push($existing_tlds, $tld["host"]);
			}

			$missing_tlds = array_diff($tlds, $existing_tlds);
			$missing_tlds = preg_filter("/^.*$/", "'\\0'", $missing_tlds);
			$missing_tlds = implode(", ", $missing_tlds);

			throw new Exception("Could not find TLD(s): {$missing_tlds}");
		}

		if (count($tlds) === 0)
		{
			foreach ($data as $tld)
			{
				array_push($tlds, $tld["host"]);
			}
		}

		// get RDDS and RDAP status (enabled/disabled)

		$rdds_rows = self::getServiceStatus($tlds, $from, $till, "rdds");
		$rdap_rows = self::getServiceStatus($tlds, $from, $till, "rdap");

		$rdds_status = [];
		$rdap_status = [];

		foreach ($rdds_rows as $row)
		{
			list($tld, $status) = $row;
			$rdds_status[$tld] = (bool)$status;
		}
		foreach ($rdap_rows as $row)
		{
			list($tld, $status) = $row;
			$rdap_status[$tld] = (bool)$status;
		}

		if (self::isRdapStandalone($from))
		{
			foreach ($data as $hostid => $tld)
			{
				$data[$hostid]["rdds"]["enabled"] = $rdds_status[$tld["host"]];
				$data[$hostid]["rdap"]["enabled"] = $rdap_status[$tld["host"]];
			}
		}
		else
		{
			foreach ($data as $hostid => $tld)
			{
				$data[$hostid]["rdds"]["enabled"] = $rdds_status[$tld["host"]] || $rdap_status[$tld["host"]];
			}
		}

		// get itemid of relevant items

		$all_hostids = array_keys($data);
		$rdds_hostids = [];
		$rdap_hostids = [];

		foreach ($data as $hostid => $tld)
		{
			if ($tld["rdds"]["enabled"])
			{
				array_push($rdds_hostids, $hostid);
			}
			if (self::isRdapStandalone($from) && $tld["rdap"]["enabled"])
			{
				array_push($rdap_hostids, $hostid);
			}
		}

		$rows = self::getItemIds($all_hostids, $rdds_hostids, $rdap_hostids);

		$itemkeys = [];
		$itemhostids = [];
		$itemids_float = [];
		$itemids_uint = [];
		$itemids_ns_downtime = [];

		foreach ($rows as $row)
		{
			list($itemid, $hostid, $key, $type) = $row;

			$itemkeys[$itemid] = $key;
			$itemhostids[$itemid] = $hostid;

			if ($type === 0)
			{
				array_push($itemids_float, $itemid);
			}
			elseif ($type === 3)
			{
				array_push($itemids_uint, $itemid);
			}
			else
			{
				throw new Exception("Unhandled item type: '{$type}' (hostid: {$hostid}, key: {$key})");
			}
		}

		// get monthly lastvalue

		$rows = array_merge(
			self::getLastValue($itemids_float, "history"     , $from, $till),
			self::getLastValue($itemids_uint , "history_uint", $from, $till)
		);

		foreach ($rows as $row)
		{
			list($itemid, $value) = $row;
			$hostid = $itemhostids[$itemid];
			$key = $itemkeys[$itemid];

			switch ($key)
			{
				case "rsm.slv.dns.downtime":
					$data[$hostid]["dns"]["availability"] = $value;
					break;

				case "rsm.slv.dns.udp.rtt.pfailed":
					$data[$hostid]["dns"]["rttUDP"] = 100.0 - $value;
					break;

				case "rsm.slv.dns.tcp.rtt.pfailed":
					$data[$hostid]["dns"]["rttTCP"] = 100.0 - $value;
					break;

				case "rsm.slv.rdds.downtime":
					$data[$hostid]["rdds"]["availability"] = $value;
					break;

				case "rsm.slv.rdds.rtt.pfailed":
					$data[$hostid]["rdds"]["rtt"] = 100.0 - $value;
					break;

				case "rsm.slv.rdap.downtime":
					$data[$hostid]["rdap"]["availability"] = $value;
					break;

				case "rsm.slv.rdap.rtt.pfailed":
					$data[$hostid]["rdap"]["rtt"] = 100.0 - $value;
					break;

				default:
					if (preg_match("/^rsm\.slv\.dns\.ns\.downtime\[(.+),(.+)\]$/", $key, $matches))
					{
						$hostname   = $matches[1];
						$ip_address = $matches[2];

						$data[$hostid]["dns"]["ns"][$itemid] = [
							"hostname"     => $hostname,
							"ipAddress"    => $ip_address,
							"availability" => $value,
							"from"         => null,
							"to"           => null,
						];

						array_push($itemids_ns_downtime, $itemid);

						break;
					}

					throw new Exception("Unhandled item key: '{$key}'");
					break;
			}
		}

		// get monthly min and max clocks

		$periods = self::getNsAvailabilityPeriod($itemids_ns_downtime, $from, $till);

		foreach ($data as $hostid => $tld)
		{
			foreach ($tld["dns"]["ns"] as $itemid => $ns)
			{
				$data[$hostid]["dns"]["ns"][$itemid]["from"] = $periods[$itemid]["from"];
				$data[$hostid]["dns"]["ns"][$itemid]["to"]   = $periods[$itemid]["till"];
			}
		}

		return $data;
	}

	private static function validateData(&$data)
	{
		foreach ($data as $hostid => $tld)
		{
			// validate host

			if (is_null($tld["host"]))
			{
				if (defined("DEBUG") && DEBUG === true)
					printf("(DEBUG) %s() \$data[{$hostid}]['host'] is null\n", __method__);

				throw new Exception("partial or missing TLD data in the database");
			}

			// validate DNS

			if (self::getMonitoringTarget() == self::MONITORING_TARGET_REGISTRY)
			{
				if (is_null($tld["dns"]["availability"]))
				{
					if (defined("DEBUG") && DEBUG === true)
						printf("(DEBUG) %s() \$data[{$hostid}]['dns']['availability'] is null (TLD: '{$tld["host"]}')\n", __method__);

					throw new Exception("partial or missing DNS Service Availability data in the database");
				}
				if (!is_array($tld["dns"]["ns"]))
				{
					if (defined("DEBUG") && DEBUG === true)
						printf("(DEBUG) %s() \$data[{$hostid}]['dns']['ns'] is not an array (TLD: '{$tld["host"]}')\n", __method__);

					throw new Exception("unexpected XML data structure");
				}
				if (count($tld["dns"]["ns"]) === 0)
				{
					if (defined("DEBUG") && DEBUG === true)
						printf("(DEBUG) %s() \$data[{$hostid}]['dns']['ns'] is empty array (TLD: '{$tld["host"]}')\n", __method__);

					throw new Exception("no Name Server availability data in the database");
				}
				foreach ($tld["dns"]["ns"] as $i => $ns)
				{
					if (is_null($ns["hostname"]))
					{
						if (defined("DEBUG") && DEBUG === true)
							printf("(DEBUG) %s() \$data[{$hostid}]['dns']['ns'][{$i}]['hostname'] is null (TLD: '{$tld["host"]}')\n", __method__);

						throw new Exception("unexpected XML data structure");
					}
					if (is_null($ns["ipAddress"]))
					{
						if (defined("DEBUG") && DEBUG === true)
							printf("(DEBUG) %s() \$data[{$hostid}]['dns']['ns'][{$i}]['ipAddress'] is null (TLD: '{$tld["host"]}')\n", __method__);

						throw new Exception("unexpected XML data structure");
					}
					// TODO: "availability", "from", "till" - what if NS was disabled for whole month?
					if (is_null($ns["availability"]))
					{
						if (defined("DEBUG") && DEBUG === true)
							printf("(DEBUG) %s() \$data[{$hostid}]['dns']['ns'][{$i}]['availability'] is null (TLD: '{$tld["host"]}')\n", __method__);

						throw new Exception("no availability data of Name Server ".$ns["hostname"].":".$ns["ipAddress"]." in the database");
					}
					if (is_null($ns["from"]))
					{
						if (defined("DEBUG") && DEBUG === true)
							printf("(DEBUG) %s() \$data[{$hostid}]['dns']['ns'][{$i}]['from'] is null (TLD: '{$tld["host"]}')\n", __method__);

						throw new Exception("unexpected XML data structure");
					}
					if (is_null($ns["to"]))
					{
						if (defined("DEBUG") && DEBUG === true)
							printf("(DEBUG) %s() \$data[{$hostid}]['dns']['ns'][{$i}]['to'] is null (TLD: '{$tld["host"]}')\n", __method__);

						throw new Exception("unexpected XML data structure");
					}
				}
				if (!is_float($tld["dns"]["rttUDP"]))
				{
					if (defined("DEBUG") && DEBUG === true)
						printf("(DEBUG) %s() \$data[{$hostid}]['dns']['rttUDP'] is not float (TLD: '{$tld["host"]}')\n", __method__);

					throw new Exception("invalid DNS UDP Resolution RTT value type in the database");
				}
				if (!is_float($tld["dns"]["rttTCP"]))
				{
					if (defined("DEBUG") && DEBUG === true)
						printf("(DEBUG) %s() \$data[{$hostid}]['dns']['rttTCP'] is not float (TLD: '{$tld["host"]}')\n", __method__);

					throw new Exception("invalid DNS TCP Resolution RTT value type in the database");
				}
			}
			if (self::getMonitoringTarget() == self::MONITORING_TARGET_REGISTRAR)
			{
				/*
					TODO: consider adding checks for DNS

					We might want to check that DNS-related values aren't filled, i.e.,
					* $tld["dns"]["availability"] is null
					* $tld["dns"]["ns"] is array
					* $tld["dns"]["ns"] is empty
					* $tld["dns"]["rttUDP"] is null
					* $tld["dns"]["rttTCP"] is null

					On the other hand, these checks would make it harder to develop & test in environments where
					switching between registries and registrars is possible.
				*/
			}

			// validate RDDS

			if (!is_bool($tld["rdds"]["enabled"]))
			{
				if (defined("DEBUG") && DEBUG === true)
					printf("(DEBUG) %s() \$data[{$hostid}]['rdds']['enabled'] is not bool (TLD: '{$tld["host"]}')\n", __method__);

				throw new Exception("invalid RDDS state value type in the database");
			}
			if ($tld["rdds"]["enabled"])
			{
				if (is_null($tld["rdds"]["availability"]))
				{
					if (defined("DEBUG") && DEBUG === true)
						printf("(DEBUG) %s() \$data[{$hostid}]['rdds']['availability'] is null (TLD: '{$tld["host"]}')\n", __method__);

					throw new Exception("partial or missing RDDS Service Availability data in the database");
				}
				if (!is_float($tld["rdds"]["rtt"]))
				{
					if (defined("DEBUG") && DEBUG === true)
						printf("(DEBUG) %s() \$data[{$hostid}]['rdds']['rtt'] is not float (TLD: '{$tld["host"]}')\n", __method__);

					throw new Exception("partial or missing RDDS Query RTT data in the database");
				}
			}
			else
			{
				if (!is_null($tld["rdds"]["availability"]))
				{
					if (defined("DEBUG") && DEBUG === true)
						printf("(DEBUG) %s() \$data[{$hostid}]['rdds']['availability'] is not null (TLD: '{$tld["host"]}')\n", __method__);

					throw new Exception("RDDS Service Availability data found in the database while it shouldn't have been");
				}
				if (!is_null($tld["rdds"]["rtt"]))
				{
					if (defined("DEBUG") && DEBUG === true)
						printf("(DEBUG) %s() \$data[{$hostid}]['rdds']['rtt'] is not null (TLD: '{$tld["host"]}')\n", __method__);

					throw new Exception("RDDS Query RTT data found in the database while it shouldn't have been");
				}
			}

			// validate RDAP
			// TODO: remove "if" after switching to Standalone RDAP
			if (array_key_exists("rdap", $tld))
			{
				if (!is_bool($tld["rdap"]["enabled"]))
				{
					if (defined("DEBUG") && DEBUG === true)
						printf("(DEBUG) %s() \$data[{$hostid}]['rdap']['enabled'] is not bool (TLD: '{$tld["host"]}')\n", __method__);

					throw new Exception("invalid RDAP state value type in the database");
				}
				if ($tld["rdap"]["enabled"])
				{
					if (is_null($tld["rdap"]["availability"]))
					{
						if (defined("DEBUG") && DEBUG === true)
							printf("(DEBUG) %s() \$data[{$hostid}]['rdap']['availability'] is null (TLD: '{$tld["host"]}')\n", __method__);

						throw new Exception("partial or missing RDAP Service Availability data in the database");
					}
					if (!is_float($tld["rdap"]["rtt"]))
					{
						if (defined("DEBUG") && DEBUG === true)
							printf("(DEBUG) %s() \$data[{$hostid}]['rdap']['rtt'] is not float (TLD: '{$tld["host"]}')\n", __method__);

						throw new Exception("partial or missing RDAP Query RTT data in the database");
					}
				}
				else
				{
					if (!is_null($tld["rdap"]["availability"]))
					{
						if (defined("DEBUG") && DEBUG === true)
							printf("(DEBUG) %s() \$data[{$hostid}]['rdap']['availability'] is not null (TLD: '{$tld["host"]}')\n", __method__);

						throw new Exception("RDAP Service Availability data found in the database while it shouldn't have been");
					}
					if (!is_null($tld["rdap"]["rtt"]))
					{
						if (defined("DEBUG") && DEBUG === true)
							printf("(DEBUG) %s() \$data[{$hostid}]['rdap']['rtt'] is not null (TLD: '{$tld["host"]}')\n", __method__);

						throw new Exception("RDAP Query RTT data found in the database while it shouldn't have been");
					}
				}
			}
		}
	}

	private static function generateXml(&$tlds, &$data, &$slrs, $generationDateTime, $reportPeriodFrom, $reportPeriodTo)
	{
		$reports = array_fill_keys($tlds, null); // for sorting, based on $tlds

		foreach ($data as $tldid => $tld)
		{
			$xml = new SimpleXMLElement("<reportTLD/>");
			$xml->addAttribute("id", $tld["host"]);
			$xml->addAttribute("generationDateTime", $generationDateTime);
			$xml->addAttribute("reportPeriodFrom", $reportPeriodFrom);
			$xml->addAttribute("reportPeriodTo", $reportPeriodTo);

			if (self::getMonitoringTarget() == self::MONITORING_TARGET_REGISTRY)
			{
				$xml_dns = $xml->addChild("DNS");
				$xml_dns_avail = $xml_dns->addChild("serviceAvailability", $tld["dns"]["availability"]);
				$xml_dns_avail->addAttribute("downtimeSLR", $slrs["dns-avail"]);
				foreach ($tld["dns"]["ns"] as $ns)
				{
					$xml_ns = $xml_dns->addChild("nsAvailability", $ns["availability"]);
					$xml_ns->addAttribute("hostname", $ns["hostname"]);
					$xml_ns->addAttribute("ipAddress", $ns["ipAddress"]);
					$xml_ns->addAttribute("from", $ns["from"]);
					$xml_ns->addAttribute("to", $ns["to"]);
					$xml_ns->addAttribute("downtimeSLR", $slrs["ns-avail"]);
				}
				$xml_dns_udp_rtt = $xml_dns->addChild("rttUDP", $tld["dns"]["rttUDP"]);
				$xml_dns_udp_rtt->addAttribute("rttSLR", $slrs["dns-udp-rtt"]);
				$xml_dns_udp_rtt->addAttribute("percentageSLR", $slrs["dns-udp-percentage"]);
				$xml_dns_tcp_rtt = $xml_dns->addChild("rttTCP", $tld["dns"]["rttTCP"]);
				$xml_dns_tcp_rtt->addAttribute("rttSLR", $slrs["dns-tcp-rtt"]);
				$xml_dns_tcp_rtt->addAttribute("percentageSLR", $slrs["dns-tcp-percentage"]);
			}

			$xml_rdds = $xml->addChild("RDDS");
			if ($tld["rdds"]["enabled"])
			{
				$xml_rdds_avail = $xml_rdds->addChild("serviceAvailability", $tld["rdds"]["availability"]);
				$xml_rdds_rtt = $xml_rdds->addChild("rtt", $tld["rdds"]["rtt"]);
			}
			else
			{
				$xml_rdds_avail = $xml_rdds->addChild("serviceAvailability", "disabled");
				$xml_rdds_rtt = $xml_rdds->addChild("rtt", "disabled");
			}

			$xml_rdds_avail->addAttribute("downtimeSLR", $slrs["rdds-avail"]);
			$xml_rdds_rtt->addAttribute("rttSLR", $slrs["rdds-rtt"]);
			$xml_rdds_rtt->addAttribute("percentageSLR", $slrs["rdds-percentage"]);

			if (self::isRdapStandalone($reportPeriodFrom))
			{
				$xml_rdap = $xml->addChild("RDAP");
				if ($tld["rdap"]["enabled"])
				{
					$xml_rdap_avail = $xml_rdap->addChild("serviceAvailability", $tld["rdap"]["availability"]);
					$xml_rdap_rtt = $xml_rdap->addChild("rtt", $tld["rdap"]["rtt"]);
				}
				else
				{
					$xml_rdap_avail = $xml_rdap->addChild("serviceAvailability", "disabled");
					$xml_rdap_rtt = $xml_rdap->addChild("rtt", "disabled");
				}

				$xml_rdap_avail->addAttribute("downtimeSLR", $slrs["rdap-avail"]);
				$xml_rdap_rtt->addAttribute("rttSLR", $slrs["rdap-rtt"]);
				$xml_rdap_rtt->addAttribute("percentageSLR", $slrs["rdap-percentage"]);
			}

			$dom = dom_import_simplexml($xml)->ownerDocument;
			$dom->formatOutput = true;

			$reports[$tld["host"]] = [
				"hostid" => $tldid,
				"host"   => $tld["host"],
				"report" => $dom->saveXML(),
			];
		}

		return array_values($reports);
	}

	################################################################################
	# Data retrieval methods
	################################################################################

	private static function getItemIds($all_hostids, $rdds_hostids, $rdap_hostids)
	{
		$hostids_placeholder = substr(str_repeat("?,", count($all_hostids)), 0, -1);
		$sql = "select itemid,hostid,key_,value_type" .
			" from items" .
			" where (" .
					"hostid in ({$hostids_placeholder}) and" .
					" (" .
						"key_ in ('rsm.slv.dns.downtime','rsm.slv.dns.udp.rtt.pfailed','rsm.slv.dns.tcp.rtt.pfailed') or" .
						" key_ like 'rsm.slv.dns.ns.downtime[%,%]'" .
					")" .
				")";
		$params = $all_hostids;

		if (count($rdds_hostids) > 0)
		{
			$hostids_placeholder = substr(str_repeat("?,", count($rdds_hostids)), 0, -1);
			$sql .= " or (" .
					"hostid in ({$hostids_placeholder}) and" .
					" key_ in ('rsm.slv.rdds.downtime','rsm.slv.rdds.rtt.pfailed')" .
				")";
			$params = array_merge($params, $rdds_hostids);
		}

		if (count($rdap_hostids) > 0)
		{
			$hostids_placeholder = substr(str_repeat("?,", count($rdap_hostids)), 0, -1);
			$sql .= " or (" .
					"hostid in ({$hostids_placeholder}) and" .
					" key_ in ('rsm.slv.rdap.downtime','rsm.slv.rdap.rtt.pfailed')" .
				")";
			$params = array_merge($params, $rdap_hostids);
		}

		return self::dbSelect($sql, $params);
	}

	private static function getServiceStatus($tlds, $from, $till, $service)
	{
		$tlds_subquery = "";
		foreach ($tlds as $tld)
		{
			if ($tlds_subquery === "")
			{
				$tlds_subquery .= "select ? as tld";
			}
			else
			{
				$tlds_subquery .= " union all select ?";
			}
		}

		$sql = "select" .
				" tlds.tld," .
				" exists (" .
					"select * from" .
						" items" .
						" left join hosts on hosts.hostid=items.hostid" .
						" left join hosts_groups on hosts_groups.hostid=hosts.hostid" .
						" left join history_uint on history_uint.itemid=items.itemid" .
					" where" .
						" items.key_ = ? and" .
						" hosts.host like concat(tlds.tld,' %') and" .
						" hosts_groups.groupid=190 and" .
						" history_uint.clock between ? and ? and" .
						" history_uint.value=1" .
				") as status" .
			" from ({$tlds_subquery}) as tlds";

		$params = array_merge(["{$service}.enabled", $from, $till], $tlds);

		return self::dbSelect($sql, $params);
	}

	private static function getTldHostIds($tlds)
	{
		$sql = "select hosts.hostid,hosts.host" .
			" from hosts" .
				" left join hosts_groups on hosts_groups.hostid=hosts.hostid" .
			" where hosts_groups.groupid=140 and hosts.status=0";
		$params = [];

		if (count($tlds) === 0)
		{
			$sql .= " order by hosts.host asc";
		}
		else
		{
			$tlds_placeholder = substr(str_repeat("?,", count($tlds)), 0, -1);
			$sql .= " and hosts.host in ({$tlds_placeholder})";
			$params = array_merge($params, $tlds);
		}

		return self::dbSelect($sql, $params);
	}

	private static function getLastValue($itemids, $history_table, $from, $till)
	{
		if (count($itemids) === 0)
		{
			return [];
		}

		$itemids_placeholder = substr(str_repeat("?,", count($itemids)), 0, -1);
		$sql = "select {$history_table}.itemid,{$history_table}.value" .
			" from {$history_table}," .
				" (" .
					"select itemid,max(clock) as clock" .
					" from {$history_table}" .
					" where itemid in ({$itemids_placeholder}) and" .
						" clock between ? and ?" .
					" group by itemid" .
				") as history_max_clock" .
			" where history_max_clock.itemid={$history_table}.itemid and" .
				" history_max_clock.clock={$history_table}.clock";
		$params = array_merge($itemids, [$from, $till]);
		return self::dbSelect($sql, $params);
	}

	private static function getNsAvailabilityPeriod($itemids, $from, $till)
	{
		if (count($itemids) === 0)
		{
			return [];
		}

		$itemids_placeholder = substr(str_repeat("?,", count($itemids)), 0, -1);
		$sql = "select" .
				" rsmhost_dns_ns_log.itemid," .
				"rsmhost_dns_ns_log.clock," .
				"rsmhost_dns_ns_log.action" .
			" from" .
				" rsmhost_dns_ns_log" .
				" inner join (" .
					"select" .
						" itemid," .
						"max(clock) as clock" .
					" from" .
						" rsmhost_dns_ns_log" .
					" where" .
						" itemid in ($itemids_placeholder) and" .
						" clock<=?" .
					" group by" .
						" itemid" .
				") as max_clock on max_clock.itemid=rsmhost_dns_ns_log.itemid and max_clock.clock=rsmhost_dns_ns_log.clock" .
			" union distinct" .
			" select" .
				" itemid," .
				"clock," .
				"action" .
			" from" .
				" rsmhost_dns_ns_log" .
			" where" .
				" itemid in ($itemids_placeholder) and" .
				" clock between ? and ?" .
			" order by" .
				" itemid asc," .
				"clock asc";
		$params = array_merge(
			$itemids,
			[$from],
			$itemids,
			[$from, $till]
		);

		$rows = self::dbSelect($sql, $params);

		$periods     = [];   // resulting array, ['itemid' => ['from' => $clock, 'till' => $clock], ...]
		$itemid_tmp  = null; // temporary variable for detecting when data for new item starts
		$period_from = null; // ref to $periods[$itemid]['from']
		$period_till = null; // ref to $periods[$itemid]['till']
		$state       = null; // state of an item (enabled/disabled) for integrity checking

		foreach ($rows as $row)
		{
			list ($itemid, $clock, $action) = $row;

			if ($itemid_tmp != $itemid)
			{
				$itemid_tmp = $itemid;
				$periods[$itemid] = [
					'from' => null,
					'till' => null,
				];
				$period_from = &$periods[$itemid]['from'];
				$period_till = &$periods[$itemid]['till'];
				$state = null;
			}

			switch ($action)
			{
				case self::RSMHOST_DNS_NS_LOG_ACTION_CREATE:
				case self::RSMHOST_DNS_NS_LOG_ACTION_ENABLE:
					if ($state === true)
					{
						throw new Exception("Unexpected action: '{$action}' (itemid: {$itemid}, clock: {$clock})");
					}
					$state = true;
					if (is_null($period_from))
					{
						$period_from = $clock;
					}
					if (!is_null($period_till))
					{
						$period_till = null;
					}
					break;

				case self::RSMHOST_DNS_NS_LOG_ACTION_DISABLE:
					if ($state === false)
					{
						throw new Exception("Unexpected action: '{$action}' (itemid: {$itemid}, clock: {$clock})");
					}
					$state = false;
					if (!is_null($period_from))
					{
						$period_till = $clock;
					}
					break;

				default:
					throw new Exception("Unhandled action: '{$action}' (itemid: {$itemid}, clock: {$clock})");
			}
		}
		unset($period_from);
		unset($period_till);

		foreach ($periods as &$period)
		{
			if ($period["from"] < $from)
			{
				$period["from"] = $from;
			}
			if (is_null($period["till"]))
			{
				$period["till"] = $till;
			}
			$period["from"] = (int)($period["from"] / 60) * 60;
			$period["till"] = (int)($period["till"] / 60) * 60 + 59;
		}
		unset($period);

		return $periods;
	}

	private static function getSlrValues($from)
	{
		// map macro names to slr names
		$macro_names = [
				'RSM.SLV.DNS.DOWNTIME'	=> 'dns-avail',				// minutes
				'RSM.SLV.NS.DOWNTIME'	=> 'ns-avail',				// minutes
				'RSM.SLV.DNS.TCP.RTT'	=> 'dns-tcp-percentage',	// %
				'RSM.DNS.TCP.RTT.LOW'	=> 'dns-tcp-rtt',			// ms
				'RSM.SLV.DNS.UDP.RTT'	=> 'dns-udp-percentage',	// %
				'RSM.DNS.UDP.RTT.LOW'	=> 'dns-udp-rtt',			// ms
				'RSM.SLV.RDDS.DOWNTIME'	=> 'rdds-avail',			// minutes
				'RSM.SLV.RDDS.RTT'		=> 'rdds-percentage',		// %
				'RSM.RDDS.RTT.LOW'		=> 'rdds-rtt',				// ms
				'RSM.SLV.RDAP.DOWNTIME'	=> 'rdap-avail',			// minutes
				'RSM.SLV.RDAP.RTT'		=> 'rdap-percentage',		// %
				'RSM.RDAP.RTT.LOW'		=> 'rdap-rtt',				// ms
		];

		$items = [];

		foreach (array_keys($macro_names) as $macro_name)
		{
			array_push($items, "rsm.configvalue[{$macro_name}]");
		}

		$items_placeholder = substr(str_repeat("?,", count($items)), 0, -1);
		$sql = "select i.key_,hi.value" .
			" from history_uint hi,items i,hosts ho" .
			" where ho.hostid=i.hostid" .
				" and i.itemid=hi.itemid" .
				" and ho.host=?" .
				" and hi.clock between ? and ?" .
				" and i.key_ in ({$items_placeholder})";

		$params = array_merge(['Global macro history', $from, $from + 59], $items);

		$rows = self::dbSelect($sql, $params);

		$slrs = array();

		foreach ($rows as $row)
		{
			list($item, $value) = $row;

			$macro_name = substr($item, strlen("rsm.configvalue["), -1);
			$slr_name = $macro_names[$macro_name];

			// TODO: fix percentage SLR in the database and remove this code!
			if ($slr_name === 'dns-tcp-percentage' ||
					$slr_name === 'dns-udp-percentage' ||
					$slr_name === 'rdds-percentage' ||
					$slr_name === 'rdap-percentage')
			{
				$value = 100 - $value;
			}

			$slrs[$slr_name] = $value;
		}

		// if SLR not found in history table, get from global macro
		foreach ($macro_names as $macro_name => $slr_name)
		{
			if (!array_key_exists($slr_name, $slrs))
			{
				$sql = "select value from globalmacro where macro=?";
				$rows = self::dbSelect($sql, ['{$' . $macro_name . '}']);

				if (!$rows)
				{
					if (defined("DEBUG") && DEBUG === true)
					{
						printf("(DEBUG) %s() macro $macro_name not found\n", __method__);
					}

					throw new Exception("no SLR value for $slr_name");
				}

				$value = $rows[0][0];

				// TODO: fix percentage SLR in the database and remove this code!
				if ($slr_name === 'dns-tcp-percentage' ||
						$slr_name === 'dns-udp-percentage' ||
						$slr_name === 'rdds-percentage' ||
						$slr_name === 'rdap-percentage')
				{
					$value = 100 - $value;
				}

				$slrs[$slr_name] = $value;
			}
		}

		return $slrs;
	}

	private static function getMonitoringTarget()
	{
		static $monitoring_target;

		if (is_null($monitoring_target))
		{
			$rows = self::dbSelect("select value from globalmacro where macro = ?", [self::MONITORING_TARGET_MACRO]);
			if (!$rows)
			{
				throw new Exception("no macro '" . self::MONITORING_TARGET_MACRO . "'");
			}

			$monitoring_target = $rows[0][0];
			if ($monitoring_target != self::MONITORING_TARGET_REGISTRY && $monitoring_target != self::MONITORING_TARGET_REGISTRAR)
			{
				throw new Exception("unexpected value of '" . self::MONITORING_TARGET_MACRO . "' - '" . $monitoring_target . "'");
			}

			if (defined("DEBUG") && DEBUG === true)
			{
				printf("(DEBUG) %s() monitoring target - %s\n", __method__, $monitoring_target);
			}
		}

		return $monitoring_target;
	}

	private static function isRdapStandalone($clock)
	{
		static $rdap_standalone_ts;

		if (is_null($rdap_standalone_ts))
		{
			$rows = self::dbSelect("select value from globalmacro where macro = ?", [self::STANDALONE_RDAP_MACRO]);
			if (!$rows)
			{
				throw new Exception("no macro '" . self::STANDALONE_RDAP_MACRO . "'");
			}

			$rdap_standalone_ts = (int)$rows[0][0];

			if (defined("DEBUG") && DEBUG === true)
			{
				printf("(DEBUG) %s() Standalone RDAP timestamp - %d\n", __method__, $rdap_standalone_ts);
			}
		}

		return $rdap_standalone_ts && $clock >= $rdap_standalone_ts;
	}

	################################################################################
	# DB methods
	################################################################################

	public static function dbSelect($sql, $input_parameters = NULL)
	{
		if (defined("DEBUG") && DEBUG === true)
		{
			$params = is_null($input_parameters) ? "NULL" : "[" . implode(", ", $input_parameters) . "]";
			printf("(DEBUG) %s() query  - %s\n", __method__, $sql);
			printf("(DEBUG) %s() params - %s\n", __method__, $params);
		}

		if (defined("STATS") && STATS === true)
		{
			$time = microtime(true);
		}

		$sth = self::$dbh->prepare($sql);
		$sth->execute($input_parameters);
		$rows = $sth->fetchAll();

		if (defined("STATS") && STATS === true)
		{
			self::$sql_time += microtime(true) - $time;
			self::$sql_count++;
		}

		if (defined("DEBUG") && DEBUG === true)
		{
			$result = count($rows) === 1 ? "[" . implode(", ", $rows[0]) . "]" : count($rows) . " row(s)";
			printf("(DEBUG) %s() result - %s\n", __method__, $result);
		}

		return $rows;
	}

	public static function dbExecute($sql, $input_parameters = NULL)
	{
		if (defined("DEBUG") && DEBUG === true)
		{
			$params = is_null($input_parameters) ? "NULL" : "[" . implode(", ", $input_parameters) . "]";
			printf("(DEBUG) %s() query  - %s\n", __method__, $sql);
			printf("(DEBUG) %s() params - %s\n", __method__, $params);
		}

		if (defined("STATS") && STATS === true)
		{
			$time = microtime(true);
		}

		$sth = self::$dbh->prepare($sql);
		$sth->execute($input_parameters);
		$rows = $sth->rowCount();

		if (defined("STATS") && STATS === true)
		{
			self::$sql_time += microtime(true) - $time;
			self::$sql_count++;
		}

		if (defined("DEBUG") && DEBUG === true)
		{
			printf("(DEBUG) %s() result - %s row(s)\n", __method__, $rows);
		}

		return $rows;
	}

	public static function dbBeginTransaction()
	{
		self::$dbh->beginTransaction();
	}

	public static function dbRollBack()
	{
		self::$dbh->rollBack();
	}

	public static function dbCommit()
	{
		self::$dbh->commit();
	}

	public static function dbConnect($server_id)
	{
		self::$sql_count = 0;
		self::$sql_time = 0.0;

		$conf = self::getDbConfig($server_id);
		$hostname = $conf["hostname"];
		$username = $conf["username"];
		$password = $conf["password"];
		$database = $conf["database"];
		$ssl_conf = $conf["ssl_conf"];

		self::$dbh = new PDO("mysql:host={$hostname};dbname={$database}", $username, $password, $ssl_conf);
		self::$dbh->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
		self::$dbh->setAttribute(PDO::ATTR_DEFAULT_FETCH_MODE, PDO::FETCH_NUM);
		self::$dbh->setAttribute(PDO::ATTR_EMULATE_PREPARES, false);
		self::$dbh->setAttribute(PDO::MYSQL_ATTR_USE_BUFFERED_QUERY, false);
	}

	public static function dbDisconnect()
	{
		self::$dbh = NULL;

		if (defined("STATS") && STATS === true)
		{
			printf("(STATS) SQL count - %d\n", self::$sql_count);
			printf("(STATS) SQL time  - %.6f\n", self::$sql_time);
		}
	}

	private static function getDbConfig($server_id)
	{
		if (array_key_exists('REQUEST_METHOD', $_SERVER))
		{
			return self::getDbConfigFromFrontend($server_id);
		}
		else
		{
			return self::getDbConfigFromRsmConf($server_id);
		}
	}

	private static function getDbConfigFromFrontend($server_id)
	{
		global $DB;

		if (!isset($DB))
		{
			throw new Exception("Failed to get DB config");
		}
		if (!array_key_exists($server_id, $DB["SERVERS"]))
		{
			throw new Exception("Invalid server ID: {$server_id}");
		}

		$server_conf = $DB["SERVERS"][$server_id];

		$ssl_conf = [];

		if (isset($server_conf["DB_KEY_FILE"]))
		{
			$ssl_conf[PDO::MYSQL_ATTR_SSL_KEY] = $server_conf["DB_KEY_FILE"];
		}
		if (isset($server_conf["DB_CERT_FILE"]))
		{
			$ssl_conf[PDO::MYSQL_ATTR_SSL_CERT] = $server_conf["DB_CERT_FILE"];
		}
		if (isset($server_conf["DB_CA_FILE"]))
		{
			$ssl_conf[PDO::MYSQL_ATTR_SSL_CA] = $server_conf["DB_CA_FILE"];
		}
		if (isset($server_conf["DB_CA_PATH"]))
		{
			$ssl_conf[PDO::MYSQL_ATTR_SSL_CAPATH] = $server_conf["DB_CA_PATH"];
		}
		if (isset($server_conf["DB_CA_CIPHER"]))
		{
			$ssl_conf[PDO::MYSQL_ATTR_SSL_CIPHER] = $server_conf["DB_CA_CIPHER"];
		}

		return [
			"hostname" => $server_conf["SERVER"],
			"username" => $server_conf["USER"],
			"password" => $server_conf["PASSWORD"],
			"database" => $server_conf["DATABASE"],
			"ssl_conf" => $ssl_conf,
		];
	}

	private static function getDbConfigFromRsmConf($server_id)
	{
		$conf_file = "/opt/zabbix/scripts/rsm.conf";

		if (!is_file($conf_file))
		{
			throw new Exception("File not found: {$conf_file}");
		}
		if (!is_readable($conf_file))
		{
			throw new Exception("File is not readable: {$conf_file}");
		}

		// PHP 5.3.0 - Hash marks (#) should no longer be used as comments and will throw a deprecation warning if used.
		// PHP 7.0.0 - Hash marks (#) are no longer recognized as comments.

		$conf_string = file_get_contents($conf_file);
		$conf_string = preg_replace("/^\s*#.*$/m", "", $conf_string);

		$conf = parse_ini_string($conf_string, true);

		if ($conf === false)
		{
			throw new Exception("Failed to parse {$conf_file}");
		}

		if (!array_key_exists("server_{$server_id}", $conf))
		{
			throw new Exception("Invalid server ID: {$server_id}");
		}

		$server_conf = $conf["server_{$server_id}"];

		$ssl_conf = [];

		if (array_key_exists("db_ca_file", $server_conf))
		{
			$ssl_conf[PDO::MYSQL_ATTR_SSL_CA] = $server_conf["db_ca_file"];
		}
		if (array_key_exists("db_ca_path", $server_conf))
		{
			$ssl_conf[PDO::MYSQL_ATTR_SSL_CAPATH] = $server_conf["db_ca_path"];
		}
		if (array_key_exists("db_cert_file", $server_conf))
		{
			$ssl_conf[PDO::MYSQL_ATTR_SSL_CERT] = $server_conf["db_cert_file"];
		}
		if (array_key_exists("db_cipher", $server_conf))
		{
			$ssl_conf[PDO::MYSQL_ATTR_SSL_CIPHER] = $server_conf["db_cipher"];
		}
		if (array_key_exists("db_key_file", $server_conf))
		{
			$ssl_conf[PDO::MYSQL_ATTR_SSL_KEY] = $server_conf["db_key_file"];
		}

		return [
			"hostname" => $server_conf["db_host"],
			"username" => $server_conf["db_user"],
			"password" => $server_conf["db_password"],
			"database" => $server_conf["db_name"],
			"ssl_conf" => $ssl_conf,
		];
	}
}
