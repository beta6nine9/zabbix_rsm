<?php
/*
** Zabbix
** Copyright (C) 2001-2013 Zabbix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
**/


require_once './local/icann.func.inc.php';
require_once './include/incidentdetails.inc.php';

class CControllerAggregateDetails extends RSMControllerBase {

	private $tld = [];

	private $slv_item = [];

	private $availability_item = [];

	private $probes = [];

	private $probe_errors = [];

	protected function checkInput() {
		$fields = [
			'tld_host'	=> 'required|string',
			'type'		=> 'required|in '.implode(',', [RSM_DNS, RSM_DNSSEC]),
			'time'		=> 'required|int32',
			'slv_itemid' => 'required|int32'
		];

		$ret = $this->validateInput($fields) && $this->initAdditionalInput();

		if (get_rsm_monitoring_type() === MONITORING_TARGET_REGISTRAR) {
			// Report is not available in registrar mode.
			$this->setResponse(new CControllerResponseRedirect((new CUrl('zabbix.php'))
				->setArgument('action', 'rsm.incidentdetails')
				->setArgument('host', $this->getInput('tld_host', ''))
				->getUrl()
			));

			return false;
		}

		if (!$ret) {
			$this->setResponse(new CControllerResponseFatal());
		}

		return $ret;
	}

	/**
	 * Check is requested tld_host and slv_itemid exists. Initializes properties:
	 *   'tld', 'slv_item', 'availability_item', 'probes'.
	 *
	 * @return bool
	 */
	protected function initAdditionalInput() {
		// tld
		$tld = API::Host()->get([
			'output' => ['hostid', 'host', 'name'],
			'tlds' => true,
			'filter' => [
				'host' => $this->getInput('tld_host')
			]
		]);
		$this->tld = reset($tld);

		if (!$this->tld) {
			error(_('No permissions to referred TLD or it does not exist!'));
			return false;
		}

		// slv_item
		$slv_items = API::Item()->get([
			'output' => ['name'],
			'itemids' => $this->getInput('slv_itemid')
		]);
		$this->slv_item = reset($slv_items);

		if (!$this->slv_item) {
			error(_('No permissions to referred SLV item or it does not exist!'));
			return false;
		}

		// availability_item
		$key = $this->getInput('type') == RSM_DNS ? RSM_SLV_DNS_AVAIL : RSM_SLV_DNSSEC_AVAIL;
		$avail_item = API::Item()->get([
			'output' => ['itemid', 'value_type'],
			'hostids' => $this->tld['hostid'],
			'filter' => ['key_' => $key]
		]);
		$this->availability_item = reset($avail_item);

		if (!$this->availability_item) {
			error(_s('Item with key "%1$s" not exist on TLD!', $key));
			return false;
		}

		// probes
		$db_probes = API::Host()->get([
			'output' => ['hostid', 'host'],
			'groupids' => PROBES_MON_GROUPID
		]);

		foreach ($db_probes as $probe) {
			$probe_host = substr($probe['host'], 0, strrpos($probe['host'], ' - mon'));

			if ($probe_host) {
				$this->probes[$probe['hostid']] = [
					'host' => $probe_host,
					'hostid' => $probe['hostid'],
					'ns_up' => 0,
					'ns_down' => 0
				];
			}
			else {
				error(_s('Unexpected host name "%1$s" among probe hosts.', $probe['host']));
			}
		}

		return true;
	}

	protected function getReportDataNew(array &$data, $time_from, $time_till) {
		$key_parser = new CItemKey;
		$tld_probe_names = [];

		foreach ($this->probes as $probe) {
			$tld_probe_names[$this->tld['host'].' '.$probe['host']] = $probe['hostid'];
		}

		$tld_probes = API::Host()->get([
			'output' => ['hostid', 'host', 'name', 'status'],
			'filter' => [
				'host' => array_keys($tld_probe_names)
			],
			'preservekeys' => true
		]);

		$data['probes_status'] = [];

		foreach ($tld_probes as $tld_probe) {
			$probe_name = substr($tld_probe['host'], strlen($data['tld_host']) + 1);
			$data['probes_status'][$probe_name] = $tld_probe['status'];
		}

		// Keys for RSM_SLV_KEY_DNS_UDP_RTT and RSM_SLV_KEY_DNS_TCP_RTT differs only by last parameter value.
		$key_parser->parse(RSM_SLV_KEY_DNS_UDP_RTT);
		$dns_rtt_key = $key_parser->getKey();
		$rtt_items = API::Item()->get([
			'output' => ['key_', 'itemid', 'value_type'],
			'selectHosts' => ['hostid'],
			'hostids' => array_keys($tld_probes),
			'search' => [
				'key_' => $dns_rtt_key.'['
			],
			'startSearch' => true,
			'monitored' => true
		]);

		if ($rtt_items) {
			$rtt_values = API::History()->get([
				'output' => API_OUTPUT_EXTEND,
				'itemids' => array_keys($rtt_items),
				'time_from' => $time_from,
				'time_till' => $time_till,
				'history' => reset($rtt_items)['value_type']
			]);
			$rtt_values = array_column($rtt_values, 'value', 'itemid');
		}

		foreach ($rtt_items as $rtt_item) {
			$probeid = reset($rtt_item['hosts'])['hostid'];
			$item_value = !array_key_exists('online_status', $this->probes[$probeid])	// Skip offline probes
					&& array_key_exists($rtt_item['itemid'], $rtt_values)
				? (int) $rtt_values[$rtt_item['itemid']]
				: null;

			if ($key_parser->parse($rtt_item['key_']) != CItemKey::PARSE_SUCCESS || $key_parser->getParamsNum() != 4) {
				error(_s('Unexpected item key "%1$s".', $rtt_item['key_']));
				continue;
			}

			$ns = $key_parser->getParam(1);
			$ip = $key_parser->getParam(2);
			$trasport = $key_parser->getParam(3);
			$ipv = filter_var($ip, FILTER_VALIDATE_IP, FILTER_FLAG_IPV4) ? 'ipv4' : 'ipv6';
			$dns_nameservers[$ns][$ipv][$ip] = true;
			$rtt_max = ($transport == 'udp') ? $data['udp_rtt'] : $data['tcp_rtt'];

			if (strtolower($this->probes[$probeid]['transport']) == $trasport) {
				$this->probes[$probeid]['results'][$ns][$ipv][$ip] = $item_value;
				$error_key = $ns.$ip;

				if ($item_value < 0) {
					if (!isset($this->probe_errors[$item_value][$error_key])) {
						$this->probe_errors[$item_value][$error_key] = 0;
					}

					$this->probe_errors[$item_value][$error_key]++;
				}
				elseif ($item_value > $rtt_max && $data['type'] == RSM_DNS) {
					if (!isset($data['probes_above_max_rtt'][$error_key])) {
						$data['probes_above_max_rtt'][$error_key] = 0;
					}

					$data['probes_above_max_rtt'][$error_key]++;
				}
			}
			else if (array_key_exists($rtt_item['itemid'], $rtt_values)) {
				error(_s('Item transport value and probe transport value mismatch found for probe "%s" item "%s"',
					$this->probe[$probeid]['host'], $rtt_item['key_']
				));
			}
		}

		$data['dns_nameservers'] = $dns_nameservers;
		$data['nsids'] = $this->getNSIDdata($dns_nameservers, $data['time']);

		$key_parser->parse(RSM_SLV_KEY_DNS_NSSOK);
		$nssok_key = $key_parser->getKey();
		$key_parser->parse(RSM_SLV_KEY_DNS_NS_STATUS);
		$ns_status_key = $key_parser->getKey();

		$ns_items = API::Item()->get([
			'output' => ['key_', 'itemid', 'value_type'],
			'selectHosts' => ['hostid'],
			'hostids' => array_keys($tld_probes),
			'search' => [
				'key_' => [$nssok_key.'[', $ns_status_key.'[']
			],
			'startSearch' => true,
			'monitored' => true
		]);

		if ($ns_items) {
			$ns_values = API::History()->get([
				'output' => API_OUTPUT_EXTEND,
				'itemids' => array_keys($ns_items),
				'time_from' => $time_from,
				'time_till' => $time_till,
				'history' => reset($ns_items)['value_type']
			]);
			$ns_values = array_column($ns_values, 'value', 'itemid');
		}

		foreach ($ns_items as $ns_item) {
			$probeid = reset($rtt_item['hosts'])['hostid'];
			$value = array_key_exists($ns_item['itemid'], $ns_values) ? $ns_values[$ns_item['itemid']] : null;

			switch ($key_parser->getKey()) {
				case $nssok_key:
					// Set Name servers up count.
					$this->probes[$probeid]['ns_up'] = $value;
					$this->probes[$probeid]['online_status'] = ($value >= $data['min_dns_count'])
						? PROBE_UP
						: PROBE_DOWN;
					break;

				case $ns_status_key:
					// Set Name server status.
					$this->probes[$probeid]['results'][$key_parser->getParam(0)]['status'] = $value;
					break;
			}
		}

		CArrayHelper::sort($this->probes, ['host']);
	}

	/**
	 * This method can be removed after 3 months. For details/explanation see ICA-605.
	 *
	 */
	protected function getReportData(array &$data, $time_from, $time_till) {
		$key_parser = new CItemKey;
		$tld_probe_names = [];

		foreach ($this->probes as $probe) {
			$tld_probe_names[$this->tld['host'].' '.$probe['host']] = $probe['hostid'];
		}

		// Get probes for specific TLD.
		$tld_probes = API::Host()->get([
			'output' => ['hostid', 'host', 'name', 'status'],
			'filter' => [
				'host' => array_keys($tld_probe_names)
			],
			'preservekeys' => true
		]);

		$data['probes_status'] = [];

		foreach ($tld_probes as $tld_probe) {
			$probe_name = substr($tld_probe['host'], strlen($data['tld_host']) + 1);
			$data['probes_status'][$probe_name] = $tld_probe['status'];
		}

		/**
		 * Select what NameServers are used by each probe.
		 * NameServer host names and IP addresses are extracted from Item keys.
		 *
		 * Following items are used:
		 *  - PROBE_DNS_UDP_ITEM_RTT - for UDP based service monitoring;
		 *
		 * TCP based service monitoring will be implemented in phase 3.
		 */
		$probes_udp_items = API::Item()->get([
			'output' => ['key_', 'itemid', 'value_type'],
			'selectHosts' => ['host'],
			'hostids' => array_keys($tld_probes),
			'search' => [
				'key_' => PROBE_DNS_UDP_ITEM_RTT
			],
			'startSearch' => true,
			'monitored' => true,
			'preservekeys' => true
		]);
		$dns_nameservers = [];

		if ($probes_udp_items) {
			$item_values_db = API::History()->get([
				'output' => API_OUTPUT_EXTEND,
				'itemids' => array_keys($probes_udp_items),
				'time_from' => $time_from,
				'time_till' => $time_till,
				'history' => reset($probes_udp_items)['value_type']
			]);
			$item_values = array_column($item_values_db, 'value', 'itemid');

			foreach ($probes_udp_items as $probes_item) {
				$probeid = $tld_probe_names[reset($probes_item['hosts'])['host']];
				$item_value = !array_key_exists('online_status', $this->probes[$probeid])	// Skip offline probes
						&& array_key_exists($probes_item['itemid'], $item_values)
					? (int) $item_values[$probes_item['itemid']]
					: null;
				;

				if ($key_parser->parse($probes_item['key_']) != CItemKey::PARSE_SUCCESS
						|| $key_parser->getParamsNum() != 3) {
					error(_s('Unexpected item key "%1$s".', $probes_item['key_']));
					continue;
				}

				$ns = $key_parser->getParam(1);
				$ip = $key_parser->getParam(2);
				$ipv = filter_var($ip, FILTER_VALIDATE_IP, FILTER_FLAG_IPV4) ? 'ipv4' : 'ipv6';
				$dns_nameservers[$ns][$ipv][$ip] = true;
				$this->probes[$probeid]['results'][$ns][$ipv][$ip] = $item_value;
				$error_key = $ns.$ip;

				if ($item_value < 0) {
					if (!isset($this->probe_errors[$item_value][$error_key])) {
						$this->probe_errors[$item_value][$error_key] = 0;
					}

					$this->probe_errors[$item_value][$error_key]++;
				}
				elseif ($item_value > $data['udp_rtt'] && $data['type'] == RSM_DNS) {
					if (!isset($data['probes_above_max_rtt'][$error_key])) {
						$data['probes_above_max_rtt'][$error_key] = 0;
					}

					$data['probes_above_max_rtt'][$error_key]++;
				}
			}
		}

		$data['dns_nameservers'] = $dns_nameservers;
		$data['nsids'] = $this->getNSIDdata($dns_nameservers, $data['time']);

		foreach ($this->probes as $probeid => &$probe) {
			if (!array_key_exists('results', $probe)) {
				continue;
			}

			$nameservers_up = [];
			$probe['transport'] = 'UDP';

			/**
			 * NameServer is considered as Down once at least one of its IP addresses is either negative value (error
			 * code) or its RTT is higher then CALCULATED_ITEM_DNS_UDP_RTT_HIGH.
			 */
			foreach ($probe['results'] as $ns => &$ipvs) {
				foreach (['ipv4', 'ipv6'] as $ipv) {
					if (array_key_exists($ipv, $ipvs)) {
						foreach ($ipvs[$ipv] as $item_value) {
							if ($item_value !== null
									&& ($item_value > $data['udp_rtt'] || isServiceErrorCode($item_value, $data['type']))) {
								$ipvs['status'] = NAMESERVER_DOWN;
								$probe['ns_down']++;
								unset($nameservers_up[$ns]);
								break(2);
							}
							elseif ($item_value !== null) {
								$nameservers_up[$ns] = true;
								$ipvs['status'] = NAMESERVER_UP;
								/**
								 * Break is no missed here. If value is positive, we still continue to search for negative
								 * values to change the status to NAMESERVER_DOWN once found.
								 *
								 * It is opposite with negative values. Once negative value is found, NameServer is marked
								 * as NAMESERVER_DOWN and cannot be turned back to NAMESERVER_UP.
								 *
								 * In fact, NAMESERVER_UP means that there are some items with values for particular
								 * NameServer and non of them is in the range of error codes.
								 */
							}
						}
					}
				}
			}
			unset($ipvs);

			$probe['ns_up'] = count($nameservers_up);
		}
		unset($probe);

		/**
		 * If probe is not offline we should check values of additional item PROBE_DNS_UDP_ITEM and compare selected
		 * values with value stored in CALCULATED_ITEM_DNS_AVAIL_MINNS.
		 */
		$probe_items = API::Item()->get([
			'output' => ['hostid', 'key_'],
			'hostids' => array_keys($tld_probes),
			'filter' => [
				'key_' => PROBE_DNS_UDP_ITEM
			],
			'monitored' => true,
			'preservekeys' => true
		]);

		if ($probe_items) {
			$item_values = API::History()->get([
				'output' => API_OUTPUT_EXTEND,
				'itemids' => array_keys($probe_items),
				'time_from' => $time_from,
				'time_till' => $time_till,
				'history' => ITEM_VALUE_TYPE_UINT64
			]);

			foreach ($item_values as $item_value) {
				$probe_item = $probe_items[$item_value['itemid']];
				$probe_hostid = $tld_probe_names[$tld_probes[$probe_item['hostid']]['name']];

				if (!isset($this->probes[$probe_hostid]['online_status'])) {
					/**
					 * DNS is considered to be UP if selected value is greater or equal to
					 * rsm.configvalue[RSM.DNS.AVAIL.MINNS] value of <RSM_HOST> at given time.
					 */
					$this->probes[$probe_hostid]['online_status'] = ($item_value['value'] >= $data['min_dns_count'])
						? PROBE_UP
						: PROBE_DOWN;
				}
			}
		}

		CArrayHelper::sort($this->probes, ['host']);
	}

	protected function doAction() {
		$time_from = strtotime(date('Y-m-d H:i:0', $this->getInput('time')));
		$defaults = [
			CALCULATED_ITEM_DNS_DELAY => null,
			CALCULATED_ITEM_DNS_AVAIL_MINNS => null,
			CALCULATED_ITEM_DNS_UDP_RTT_HIGH => null,
			CALCULATED_ITEM_DNS_TCP_RTT_HIGH => null
		];
		$macro = $this->getHistoryMacroValue(array_keys($defaults), $time_from) + $defaults;
		$data = [
			'title' => _('Details of particular test'),
			'tld_host' => $this->tld['host'],
			'slv_item_name' => $this->slv_item['name'],
			'type' => $this->getInput('type'),
			'time' => $time_from,
			'min_dns_count' => $macro[CALCULATED_ITEM_DNS_AVAIL_MINNS],
			'udp_rtt' => $macro[CALCULATED_ITEM_DNS_UDP_RTT_HIGH],
			'tcp_rtt' => $macro[CALCULATED_ITEM_DNS_TCP_RTT_HIGH],
			'test_error_message' => $this->getValueMapping(RSM_DNS_RTT_ERRORS_VALUE_MAP),
			'test_status_message' => $this->getValueMapping(RSM_SERVICE_AVAIL_VALUE_MAP)
		];
		$time_till = $time_from + ($macro[CALCULATED_ITEM_DNS_DELAY] - 1);

		if ($data['type'] == RSM_DNS) {
			$data['probes_above_max_rtt'] = [];
		}

		$test_result = API::History()->get([
			'output' => ['value'],
			'itemids' => $this->availability_item['itemid'],
			'time_from' => $time_from,
			'time_till' => $time_till,
			'history' => $this->availability_item['value_type'],
			'limit' => 1
		]);
		$test_result = reset($test_result);

		if ($test_result) {
			$data['test_result'] = $test_result['value'];
		}

		$probe_items = API::Item()->get([
			'output' => ['itemid', 'key_', 'hostid'],
			'hostids' => array_keys($this->probes),
			'filter' => [
				'key_' => PROBE_KEY_ONLINE
			],
			'monitored' => true,
			'preservekeys' => true
		]);

		// Initialize probes data for probes with offline status.
		if ($probe_items) {
			$item_values = API::History()->get([
				'output' => ['itemid', 'value'],
				'itemids' => array_keys($probe_items),
				'time_from' => $time_from,
				'time_till' => $time_till
			]);

			foreach ($item_values as $item_value) {
				$probe_hostid = $probe_items[$item_value['itemid']]['hostid'];

				/**
				 * Value of probe item PROBE_KEY_ONLINE == PROBE_DOWN means that both DNS UDP and DNS TCP are offline.
				 */
				if (!isset($this->probes[$probe_hostid]['online_status']) && $item_value['value'] == PROBE_DOWN) {
					$this->probes[$probe_hostid]['online_status'] = PROBE_OFFLINE;
				}
			}
		}

		// Set probes test trasport.
		$protocol_type = $this->getValueMapping(RSM_DNS_TRANSPORT_PROTOCOL_VALUE_MAP);

		if (!$protocol_type) {
			error(_('Value mapping for "Transport protocol" is not found.'));
		}
		else {
			$protocol_items = API::Item()->get([
				'output' => ['itemid', 'hostid'],
				'hostids' => array_keys($this->probes),
				'filter' => ['key_' => RSM_SLV_KEY_DNS_PROTOCOL],
				'preservekeys' => true
			]);
			$protocol_items_data = API::History()->get([
				'output' => ['itemid', 'value'],
				'itemids' => array_keys($protocol_items),
				'time_from' => $time_from,
				'time_till' => $time_from,
				'history' => ITEM_VALUE_TYPE_UINT64
			]);

			foreach ($protocol_items_data as $protocol_item_data) {
				$protocol_item = $protocol_items[$protocol_item_data['itemid']];
				$this->probes[$protocol_item['hostid']]['transport'] = $protocol_type[$protocol_item_data['value']];
			}
		}

		// Detect old or new format (ICA-605) and envoke getReportData or getReportDataNew.
		$key_parser = new CItemKey;
		$key_parser->parse(RSM_SLV_KEY_DNS_TEST);
		$dns_test_item = API::Item()->get([
			'output' => ['value_type'],
			'hostids' => array_keys($this->probes),
			'filter' => ['key_' => $key_parser->getKey().'[']
		]);
		$old_format = true;

		if ($dns_test_item) {
			$dns_test_item_value = API::History()->get([
				'output' => ['itemid', 'value'],
				'itemids' => array_keys($dns_test_item),
				'time_from' => $time_from,
				'time_till' => $time_from,
				'history' => reset($dns_test_item)['value_type']
			]);

			$old_format = !$dns_test_item_value;
		}

		if ($old_format) {
			$this->getReportData($data, $time_from, $time_till);
		}
		else {
			$this->getReportDataNew($data, $time_from, $time_till);
		}

		$data['probes'] = $this->probes;
		$data['errors'] = $this->probe_errors;
		krsort($data['errors']);

		$response = new CControllerResponseData($data);
		$response->setTitle($data['title']);
		$this->setResponse($response);
	}


	/**
	 * Collects NSID item values for all probe name servers and ips. NSID unique values will be stored in
	 * $data['nsids] with key having incremental index and value NSID value. Additionaly probe ns+ip NSID results
	 * will be stored in 'results_nsid' property of $this->probes[{PROBE_ID}] array as incremental index
	 * pointing to value in $data['nsids'].
	 *
	 * $this->probes[{PROBE_ID}]['results_nsid'][{NAME_SERVER_NAME}][{NAME_SERVER_IP}] = NSID value index.
	 *
	 * Return array of unique NSID values found.
	 *
	 * @param array $dns_nameservers    DNS nameservers array with 'ipv4' and 'ipv6' sub arrays.
	 * @param int   $time               NSID values timestamp.
	 * @return array
	 */
	protected function getNSIDdata(array $dns_nameservers, $time) {
		$key_parser = new CItemKey;
		$nsid_item_keys = [];
		$nsids = [];

		foreach ($dns_nameservers as $ns_name => $ips) {
			// Merge 'ivp4' and 'ipv6' arrays.
			$ips = array_reduce($ips, 'array_merge', []);

			foreach (array_keys($ips) as $ip) {
				$nsid_item_keys[] = strtr(RSM_SLV_KEY_DNS_NSID, [
					'{#NS}' => $ns_name,
					'{#IP}' => $ip
				]);
			}
		}

		if (!$nsid_item_keys) {
			return $nsids;
		}

		$nsid_items = API::Item()->get([
			'output' => ['key_', 'type', 'hostid'],
			'hostids' => array_keys($this->probes),
			'filter' => ['key_' => $nsid_item_keys],
			'preservekeys' => true
		]);

		if (!$nsid_items) {
			return $nsids;
		}

		$nsid_values = API::History()->get([
			'output' => ['itemid', 'value'],
			'itemids' => array_keys($nsid_items),
			'time_from' => $time,
			'time_till' => $time,
			'history' => ITEM_VALUE_TYPE_TEXT
		]);

		$key_parser->parse(RSM_SLV_KEY_DNS_NSID);
		$params_count = $key_parser->getParamsNum();
		$nsids = array_unique(zbx_objectValues($nsid_values, 'value'));
		sort($nsids, SORT_LOCALE_STRING|SORT_NATURAL|SORT_FLAG_CASE);

		foreach ($nsid_values as $nsid_value) {
			$nsid_item = $nsid_items[$nsid_value['itemid']];
			$key_parser->parse($nsid_item['key_']);

			if ($key_parser->getParamsNum() != $params_count) {
				error(_s('Unexpected item key "%1$s".', $nsid_item['key_']));
				continue;
			}

			$ns_name = $key_parser->getParam(0);
			$ns_ip = $key_parser->getParam(1);
			$this->probes[$nsid_item['hostid']]['results_nsid'][$ns_name][$ns_ip] = array_search($nsid_value['value'],
				$nsids
			);
		}

		return $nsids;
	}
}
