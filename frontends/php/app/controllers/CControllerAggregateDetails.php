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


require_once './include/config.inc.php';
require_once './include/incidentdetails.inc.php';

class CControllerAggregateDetails extends CController {

	protected function init() {
		$this->disableSIDValidation();
	}

	protected function checkPermissions() {
		$valid_users = [USER_TYPE_READ_ONLY, USER_TYPE_ZABBIX_USER, USER_TYPE_POWER_USER, USER_TYPE_COMPLIANCE,
			USER_TYPE_ZABBIX_ADMIN, USER_TYPE_SUPER_ADMIN];

		return in_array($this->getUserType(), $valid_users);
	}

	protected function checkInput() {
		$fields = [
			'tld_host'	=> 'string',
			'type'		=> 'required|in '.implode(',', [RSM_DNS, RSM_DNSSEC]),
			'time'		=> 'int32',
			'slvItemId' => 'int32'
		];

		$ret = $this->validateInput($fields);

		if (!$ret) {
			$this->setResponse(new CControllerResponseFatal());
		}

		return $ret;
	}

	protected function updateProfiles(array &$data) {
		if ($data['tld_host'] && $data['time'] && $data['slvItemId'] && $data['type'] !== null) {
			CProfile::update('web.rsm.aggregatedresults.tld_host', $data['tld_host'], PROFILE_TYPE_STR);
			CProfile::update('web.rsm.aggregatedresults.time', $data['time'], PROFILE_TYPE_INT);
			CProfile::update('web.rsm.aggregatedresults.slvItemId', $data['slvItemId'], PROFILE_TYPE_ID);
			CProfile::update('web.rsm.aggregatedresults.type', $data['type'], PROFILE_TYPE_INT);
		}
		elseif (!$data['tld_host'] && !$data['time'] && !$data['slvItemId'] && $data['type'] === null) {
			$data['tld_host'] = CProfile::get('web.rsm.aggregatedresults.tld_host');
			$data['time'] = CProfile::get('web.rsm.aggregatedresults.time');
			$data['slvItemId'] = CProfile::get('web.rsm.aggregatedresults.slvItemId');
			$data['type'] = CProfile::get('web.rsm.aggregatedresults.type');
		}
	}

	protected function getReportData(array &$data) {
		// Get host with calculated items.
		$rsm = API::Host()->get([
			'output' => ['hostid'],
			'filter' => [
				'host' => RSM_HOST
			]
		]);

		if ($rsm) {
			$rsm = reset($rsm);
		}
		else {
			error(_s('No permissions to referred host "%1$s" or it does not exist!', RSM_HOST));
			return;
		}

		// Get macros old value.
		$macro_items = API::Item()->get([
			'output' => ['itemid', 'key_', 'value_type'],
			'hostids' => $rsm['hostid'],
			'filter' => [
				'key_' => [
					CALCULATED_ITEM_DNS_DELAY,
					CALCULATED_ITEM_DNS_AVAIL_MINNS,
					CALCULATED_ITEM_DNS_UDP_RTT_HIGH
				]
			]
		]);

		foreach ($macro_items as $macro_item) {
			/**
			 * To get value that actually was current at the time when data was collected, we need to get history record
			 * that was newest at the moment of requested time.
			 *
			 * In other words:
			 * SELECT * FROM history_uint WHERE itemid=<itemid> AND <test_time_from> >= clock ORDER BY clock DESC LIMIT 1
			 */
			$macro_item_value = API::History()->get([
				'output' => API_OUTPUT_EXTEND,
				'itemids' => $macro_item['itemid'],
				'time_till' => $test_time_from,
				'history' => $macro_item['value_type'],
				'sortfield' => 'clock',
				'sortorder' => 'DESC',
				'limit' => 1
			]);

			$macro_item_value = reset($macro_item_value);

			if ($macro_item['key_'] === CALCULATED_ITEM_DNS_AVAIL_MINNS) {
				$min_dns_count = $macro_item_value['value'];
			}
			elseif ($macro_item['key_'] === CALCULATED_ITEM_DNS_UDP_RTT_HIGH) {
				$data['udp_rtt'] = $macro_item_value['value'];
			}
			else {
				$macro_time = $macro_item_value['value'] - 1;
			}
		}

		// Time calculation.
		$test_time_till = $test_time_from + $macro_time;

		// Get TLD.
		$tld = API::Host()->get([
			'output' => ['hostid', 'host', 'name'],
			'tlds' => true,
			'filter' => [
				'host' => $data['tld_host']
			]
		]);

		if ($tld) {
			$data['tld'] = reset($tld);
		}
		else {
			error(_('No permissions to referred TLD or it does not exist!'));
			return;
		}

		// Get slv item.
		$slv_items = API::Item()->get([
			'output' => ['name'],
			'itemids' => $data['slvItemId']
		]);

		if ($slv_items) {
			$data['slvItem'] = reset($slv_items);
		}
		else {
			error(_('No permissions to referred SLV item or it does not exist!'));
			return;
		}

		// Get test result.
		$key = ($data['type'] == RSM_DNS) ? RSM_SLV_DNS_AVAIL : RSM_SLV_DNSSEC_AVAIL;

		// Get items.
		$avail_items = API::Item()->get([
			'output' => ['itemid', 'value_type'],
			'hostids' => $data['tld']['hostid'],
			'filter' => [
				'key_' => $key
			]
		]);

		if ($avail_items) {
			$avail_item = reset($avail_items);

			$test_results = API::History()->get([
				'output' => API_OUTPUT_EXTEND,
				'itemids' => $avail_item['itemid'],
				'time_from' => $test_time_from,
				'time_till' => $test_time_till,
				'history' => $avail_item['value_type'],
				'limit' => 1
			]);

			if (($test_result = reset($test_results)) !== false) {
				$data['testResult'] = $test_result['value'];
			}
		}
		else {
			error(_s('Item with key "%1$s" not exist on TLD!', $key));
			return;
		}

		// Get probes.
		$probes = API::Host()->get([
			'output' => ['hostid', 'host'],
			'groupids' => PROBES_MON_GROUPID,
			'preservekeys' => true
		]);

		$tld_probe_names = [];
		foreach ($probes as $probe) {
			$pos = strrpos($probe['host'], ' - mon');
			if ($pos === false) {
				error(_s('Unexpected host name "%1$s" among probe hosts.', $probe['host']));
				continue;
			}

			$tld_probe_name = substr($probe['host'], 0, $pos);

			$data['probes'][$probe['hostid']] = [
				'host' => $tld_probe_name,
				'name' => $tld_probe_name
			];

			$tld_probe_names[$data['tld']['host'] . ' ' . $tld_probe_name] = $probe['hostid'];
		}

		// Get total number of probes for summary block before results table.
		$data['totalProbes'] = count($data['probes']);

		$probe_items = API::Item()->get([
			'output' => ['itemid', 'key_', 'hostid'],
			'hostids' => array_keys($data['probes']),
			'filter' => [
				'key_' => PROBE_KEY_ONLINE
			],
			'monitored' => true,
			'preservekeys' => true
		]);

		if ($probe_items) {
			$item_values = API::History()->get([
				'output' => API_OUTPUT_EXTEND,
				'itemids' => array_keys($probe_items),
				'time_from' => $test_time_from,
				'time_till' => $test_time_till
			]);

			$items_utilized = [];

			foreach ($item_values as $item_value) {
				if (array_key_exists($item_value['itemid'], $items_utilized)) {
					continue;
				}

				$probe_hostid = $probe_items[$item_value['itemid']]['hostid'];
				$items_utilized[$item_value['itemid']] = true;

				/**
				 * Value of probe item PROBE_KEY_ONLINE == PROBE_DOWN means that both DNS UDP and DNS TCP are offline.
				 * Support for TCP will be added in phase 3.
				 */
				if ($item_value['value'] == PROBE_DOWN) {
					$data['probes'][$probe_hostid]['status_udp'] = PROBE_OFFLINE;
				}
			}
			unset($items_utilized);
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

		if ($probes_udp_items) {
			$item_values_db = API::History()->get([
				'output' => API_OUTPUT_EXTEND,
				'itemids' => zbx_objectValues($probes_udp_items, 'itemid'),
				'time_from' => $test_time_from,
				'time_till' => $test_time_till,
				'history' => reset($probes_udp_items)['value_type']
			]);

			$item_values = [];
			foreach ($item_values_db as $item_value_db) {
				$item_values[$item_value_db['itemid']] = $item_value_db['value'];
			}
			unset($item_values_db);

			foreach ($probes_udp_items as $probes_item) {
				$probeid = $tld_probe_names[reset($probes_item['hosts'])['host']];
				$item_value = !array_key_exists('status_udp', $data['probes'][$probeid])	// Skip offline probes
						&& array_key_exists($probes_item['itemid'], $item_values)
					? (int) $item_values[$probes_item['itemid']]
					: null;

				preg_match('/^[^\[]+\[([^\]]+)]$/', $probes_item['key_'], $matches);
				if (!$matches) {
					show_error_message(_s('Unexpected item key "%1$s".', $probes_item['key_']));
					continue;
				}

				$matches = explode(',', $matches[1]);
				$ipv = filter_var($matches[2], FILTER_VALIDATE_IP, FILTER_FLAG_IPV4) ? 'ipv4' : 'ipv6';

				$data['dns_udp_nameservers'][$matches[1]][$ipv][$matches[2]] = true;
				$data['probes'][$probeid]['results_udp'][$matches[1]][$ipv][$matches[2]] = $item_value;

				$error_key = 'udp_'.$matches[1].'_'.$ipv.'_'.$matches[2];

				if (0 > $item_value) {
					if (!array_key_exists($item_value, $data['errors'])) {
						$data['errors'][$item_value] = [];
					}

					if (!array_key_exists($error_key, $data['errors'][$item_value])) {
						$data['errors'][$item_value][$error_key] = 0;
					}

					$data['errors'][$item_value][$error_key]++;
				}
				elseif ($item_value > $data['udp_rtt'] && $data['type'] == RSM_DNS) {
					if (!array_key_exists($error_key, $data['probes_above_max_rtt'])) {
						$data['probes_above_max_rtt'][$error_key] = 0;
					}

					$data['probes_above_max_rtt'][$error_key]++;
				}
			}
		}

		// Sort errors.
		krsort($data['errors']);

		foreach ($data['probes'] as &$probe) {
			$probe['udp_ns_down'] = 0;
			$probe['udp_ns_up'] = 0;

			if (array_key_exists('results_udp', $probe)) {
				$nameservers_up = [];

				/**
				 * NameServer is considered as Down once at least one of its IP addresses is either negative value (error
				 * code) or its RTT is higher then CALCULATED_ITEM_DNS_UDP_RTT_HIGH.
				 */
				foreach ($probe['results_udp'] as $ns => &$ipvs) {
					foreach (['ipv4', 'ipv6'] as $ipv) {
						if (array_key_exists($ipv, $ipvs)) {
							foreach ($ipvs[$ipv] as $item_value) {
								if ($item_value !== null
										&& ($item_value > $data['udp_rtt'] || isServiceErrorCode($item_value, $data['type']))) {
									$ipvs['status'] = NAMESERVER_DOWN;
									$probe['udp_ns_down']++;
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

				$probe['udp_ns_up'] = count($nameservers_up);
			}
		}
		unset($probe);

		// Get status for each TLD probe (displayed in column 'DNS UDP' -> 'Status').

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
				'time_from' => $test_time_from,
				'time_till' => $test_time_till,
				'history' => 3
			]);
			$items_utilized = [];

			foreach ($item_values as $item_value) {
				if (array_key_exists($item_value['itemid'], $items_utilized)) {
					continue;
				}

				$probe_item = $probe_items[$item_value['itemid']];
				$probe_hostid = $tld_probe_names[$tld_probes[$probe_item['hostid']]['name']];
				$items_utilized[$item_value['itemid']] = true;

				if (array_key_exists('status_udp', $data['probes'][$probe_hostid])) {
					continue;
				}

				/**
				 * DNS is considered to be UP if selected value is greater than rsm.configvalue[RSM.DNS.AVAIL.MINNS]
				 * for <RSM_HOST> at given time;
				 */
				$data['probes'][$probe_hostid]['status_udp'] = ($item_value['value'] >= $min_dns_count)
					? PROBE_UP
					: PROBE_DOWN;
			}
			unset($items_utilized);
		}

		CArrayHelper::sort($data['probes'], ['name']);
	}

	protected function doAction() {
		// Report is not available in registrar mode.
		if (get_rsm_monitoring_type() === MONITORING_TARGET_REGISTRAR) {
			$this->setResponse(new CControllerResponseRedirect((new CUrl('zabbix.php'))
				->setArgument('action', 'rsm.incidentdetails')
				->setArgument('host', $this->getInput('tld_host', ''))
				->getUrl()
			));
		}

		$data = [
			'title' => _('Details of particular test'),
			'tld_host' => $this->getInput('tld_host', null),
			'time' => $this->getInput('time', null),
			'slvItemId' => $this->getInput('slvItemId', null),
			'type' => $this->getInput('type', null),
			'probes' => [],
			'errors' => []
		];

		$this->updateProfiles($data);

		if ($data['type'] == RSM_DNS) {
			$data['probes_above_max_rtt'] = [];
		}

		if ($data['tld_host'] && $data['time'] && $data['slvItemId'] && $data['type'] !== null) {
			$test_time_from = mktime(
				date('H', $data['time']),
				date('i', $data['time']),
				0,
				date('n', $data['time']),
				date('j', $data['time']),
				date('Y', $data['time'])
			);

			$data['testResult'] = null;
			$data['totalProbes'] = 0;

			$this->getReportData($data);

			// Get value maps for error messages.
			$error_msg_value_map = API::ValueMap()->get([
				'output' => [],
				'selectMappings' => ['value', 'newvalue'],
				'valuemapids' => [RSM_DNS_RTT_ERRORS_VALUE_MAP]
			]);

			if ($error_msg_value_map) {
				foreach ($error_msg_value_map[0]['mappings'] as $val) {
					$data['error_msgs'][$val['value']] = $val['newvalue'];
				}
			}

			// Get mapped value for test result.
			$data['testResultLabel'] = getMappedValue($data['testResult'], RSM_SERVICE_AVAIL_VALUE_MAP);
			if (!$data['testResultLabel']) {
				$data['testResultLabel'] = _('No result');
			}
		}

		$response = new CControllerResponseData($data);
		$response->setTitle($data['title']);
		$this->setResponse($response);
	}
}
