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


namespace Modules\RSM\Actions;

use API;
use CUrl;
use CItemKey;
use CArrayHelper;
use Exception;
use CControllerResponseFatal;
use CControllerResponseData;
use CControllerResponseRedirect;

class AggregateDetailsAction extends Action {

	private $tld = [];

	private $slv_item = [];

	private $availability_item = [];

	private $probes = [];

	private $probe_errors = [];

	protected function checkInput() {
		$fields = [
			'host'		=> 'required|string',
			'type'		=> 'required|in '.implode(',', [RSM_DNS, RSM_DNSSEC]),
			'time'		=> 'required|int32',
			'slvItemId' => 'required|int32'
		];

		// Report is not available in registrar mode.
		if (get_rsm_monitoring_type() === MONITORING_TARGET_REGISTRAR) {
			$this->setResponse(new CControllerResponseRedirect((new CUrl('zabbix.php'))
				->setArgument('action', 'rsm.incidentdetails')
				->setArgument('host', $this->getInput('host', ''))
				->getUrl()
			));

			return false;
		}

		$ret = $this->validateInput($fields);
		if (!$ret) {
			$this->setResponse(new CControllerResponseFatal());
		}

		return $ret;
	}

	/**
	 * Check if user has enough permissions to all requested resources.
	 *
	 * @return boolean
	 */
	protected function checkPermissions() {
		$valid_users = [USER_TYPE_READ_ONLY, USER_TYPE_ZABBIX_USER, USER_TYPE_POWER_USER, USER_TYPE_COMPLIANCE,
			USER_TYPE_ZABBIX_ADMIN, USER_TYPE_SUPER_ADMIN];

		if (!in_array($this->getUserType(), $valid_users))
			return false;

		return (parent::checkPermissions() && $this->initAdditionalInput());
	}

	/**
	 * Check is requested host and slvItemId exists.
	 * Initializes properties: 'tld', 'slv_item', 'availability_item', 'probes'.
	 *
	 * @return bool
	 */
	protected function initAdditionalInput() {
		// tld
		$tld = API::Host()->get([
			'output' => ['hostid', 'host', 'name'],
			'tlds' => true,
			'filter' => [
				'host' => $this->getInput('host')
			]
		]);
		$this->tld = reset($tld);

		if (!$this->tld) {
			return false;
		}

		// slv_item
		$slv_items = API::Item()->get([
			'output' => ['name'],
			'itemids' => $this->getInput('slvItemId')
		]);
		$this->slv_item = reset($slv_items);

		if (!$this->slv_item) {
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
			return false;
		}

		// probes
		$tld_probe_names = [];
		$db_probes = API::Host()->get([
			'output' => ['hostid', 'host'],
			'groupids' => PROBES_MON_GROUPID
		]);

		foreach ($db_probes as $probe) {
			$probe_host = substr($probe['host'], 0, strrpos($probe['host'], ' - mon'));

			if ($probe_host) {
				// tld "example" and probe "Los_Angeles - mon" will result in host "example Los_Angeles"
				$tld_probe_names[$this->tld['host'].' '.$probe_host] = $probe['hostid'];
				$this->probes[$probe['hostid']] = [
					'host' => $probe_host,
					'hostid' => $probe['hostid'],
					'ipv4' => 0,
					'ipv6' => 0,
					'ns_up' => 0,
					'ns_down' => 0
				];
			}
			else {
				error(_s('Unexpected host name "%1$s" among probe hosts.', $probe['host']));
			}
		}

		if ($tld_probe_names) {
			$tld_probes = API::Host()->get([
				'output' => ['hostid', 'host', 'name', 'status'],
				'filter' => [
					'host' => array_keys($tld_probe_names)
				]
			]);

			foreach ($tld_probes as $tld_probe) {
				$probeid = $tld_probe_names[$tld_probe['host']];
				$this->probes[$probeid] += [
					'tldprobe_hostid' => $tld_probe['hostid'],
					'tldprobe_host' => $tld_probe['host'],
					'tldprobe_name' => $tld_probe['name'],
					'tldprobe_status' => $tld_probe['status'],
				];
			}
		}

		return true;
	}

	protected function getReportData(array &$data, $time_from, $time_till) {
		$key_parser = new CItemKey;

		# array of Name Servers with 'ipv4' and 'ipv6' sub arrays
		$dns_nameservers = [];

		# map of '<TLD> <Probe>' hostid => '<Probe>' hostid
		$tldprobeid_probeid = [];

		# '<Probe>' => status
		$data['probes_status'] = [];

		foreach ($this->probes as $hostid => $hash) {
			// Only take data from Probes that are ONLINE
			if (!isset($hash['online_status']) || $hash['online_status'] != PROBE_OFFLINE) {
				$tldprobeid_probeid[$hash['tldprobe_hostid']] = $hostid;
				$data['probes_status'][$hash['host']] = $hash['tldprobe_status'];
			}
		}

		if (!$tldprobeid_probeid) {
			return;
		}

		// Keys for PROBE_DNS_UDP_RTT and PROBE_DNS_TCP_RTT differs only by last parameter value.
		$key_parser->parse(PROBE_DNS_UDP_RTT);
		$dns_rtt_key = $key_parser->getKey();
		$rtt_items = API::Item()->get([
			'output' => ['key_', 'itemid', 'hostid'],
			'hostids' => array_keys($tldprobeid_probeid),
			'search' => [
				'key_' => $dns_rtt_key.'['
			],
			'startSearch' => true,
			'monitored' => true
		]);

		if ($rtt_items) {
			$rtt_values = API::History()->get([
				'output' => API_OUTPUT_EXTEND,
				'itemids' => array_column($rtt_items, 'itemid'),
				'time_from' => $time_from,
				'time_till' => $time_till,
				'history' => ITEM_VALUE_TYPE_FLOAT
			]);
			$rtt_values = array_column($rtt_values, 'value', 'itemid');
		}

		foreach ($rtt_items as $rtt_item) {
			if (!array_key_exists($rtt_item['itemid'], $rtt_values)) {
				continue;
			}

			$tldprobeid = $rtt_item['hostid'];
			$probeid = $tldprobeid_probeid[$tldprobeid];

			$item_value = !array_key_exists('online_status', $this->probes[$probeid])	// Skip offline probes
					&& array_key_exists($rtt_item['itemid'], $rtt_values)
				? (int) $rtt_values[$rtt_item['itemid']]
				: null;

			if ($key_parser->parse($rtt_item['key_']) != CItemKey::PARSE_SUCCESS || $key_parser->getParamsNum() != 3) {
				error(_s('Unexpected item key "%1$s".', $rtt_item['key_']));
				continue;
			}

			$ns = $key_parser->getParam(0);
			$ip = $key_parser->getParam(1);
			$transport = $key_parser->getParam(2);	// TODO: we have now special item for that "rsm.dns.transport"
			$ipv = filter_var($ip, FILTER_VALIDATE_IP, FILTER_FLAG_IPV4) ? 'ipv4' : 'ipv6';
			$dns_nameservers[$ns][$ipv][$ip] = true;
			$rtt_max = ($transport == 'udp') ? $data['udp_rtt'] : $data['tcp_rtt'];

			if (strtolower($this->probes[$probeid]['transport']) != $transport) {
				error(_s('Item transport value and probe transport value mismatch found for probe "%s" item "%s"',
					$this->probes[$probeid]['host'], $rtt_item['key_']
				));
			}

			$this->probes[$probeid]['results'][$ns][$ipv][$ip] = $item_value;
			$error_key = $ns.$ip;

			if ($item_value < 0) {
				if (isServiceErrorCode($item_value, $data['type'])) {
					$this->probes[$probeid]['dns_error'][$error_key] = true;
				}

				if (!isset($this->probe_errors[$item_value][$error_key])) {
					$this->probe_errors[$item_value][$error_key] = 0;
				}

				$this->probe_errors[$item_value][$error_key]++;
			}
			elseif ($item_value > $rtt_max && $data['type'] == RSM_DNS) {
				$this->probes[$probeid]['above_max_rtt'][$error_key] = true;

				if (!isset($data['probes_above_max_rtt'][$error_key])) {
					$data['probes_above_max_rtt'][$error_key] = [
						'tcp' => 0,
						'udp' => 0,
					];
				}

				$data['probes_above_max_rtt'][$error_key][$transport]++;
			}
		}

		$data['dns_nameservers'] = $dns_nameservers;
		$data['nsids'] = $this->getNSIDdata($dns_nameservers, $time_from, $time_till);
		$key_parser->parse(PROBE_DNS_NS_STATUS);
		$ns_status_key = $key_parser->getKey();
		$tldprobe_values = $this->getItemsHistoryValue([
			'output' => ['key_', 'itemid', 'hostid'],
			'hostids' => array_column($this->probes, 'tldprobe_hostid'),
			'search' => [
				'key_' => [
					$ns_status_key.'[',
					PROBE_DNS_NSSOK,
					($data['type'] == RSM_DNS ? PROBE_DNS_STATUS : PROBE_DNSSEC_STATUS),
					CALCULATED_PROBE_RSM_IP4_ENABLED,
					CALCULATED_PROBE_RSM_IP6_ENABLED
				]
			],
			'startSearch' => true,
			'searchByAny' => true,
			'monitored' => true,
			'time_from' => $time_from,
			'time_till' => $time_till,
			'history' => ITEM_VALUE_TYPE_UINT64
		]);
		$probe_nscount = [];

		foreach ($tldprobe_values as $tldprobe_value) {
			$key_parser->parse($tldprobe_value['key_']);
			$key = $key_parser->getKey();

			if (!array_key_exists($tldprobe_value['hostid'], $tldprobeid_probeid)) {
				continue;
			}

			$probeid = $tldprobeid_probeid[$tldprobe_value['hostid']];

			if (!array_key_exists('history_value', $tldprobe_value)) {
				continue;
			}

			$value = $tldprobe_value['history_value'];

			switch ($key) {
				case PROBE_DNS_NSSOK:
					// Set Name servers up count.
					$this->probes[$probeid]['ns_up'] = $value;
					break;

				case PROBE_DNS_STATUS:
				case PROBE_DNSSEC_STATUS:
					// Set DNS/DNSSEC Test status.
					$this->probes[$probeid]['online_status'] = $value;
					break;

				case $ns_status_key:
					// Set Name server status.
					$this->probes[$probeid]['results'][$key_parser->getParam(0)]['status'] = $value;
					$probe_nscount[$probeid] = isset($probe_nscount[$probeid]) ? $probe_nscount[$probeid] + 1 : 1;
					break;

				case 'probe.configvalue':
					$ipv = ($tldprobe_value['key_'] == CALCULATED_PROBE_RSM_IP4_ENABLED) ? 'ipv4' : 'ipv6';
					$this->probes[$probeid][$ipv] = $value;
					break;
			}
		}

		// Calculate Name servers down value for tld probe.
		foreach ($probe_nscount as $probeid => $count) {
			$nssok = isset($this->probes[$probeid]['ns_up']) ? $this->probes[$probeid]['ns_up'] : 0;
			$this->probes[$probeid]['ns_down'] = $count - $nssok;
		}

		CArrayHelper::sort($this->probes, ['host']);
	}

	protected function doAction() {
		$time_from = strtotime(date('Y-m-d H:i:0', $this->getInput('time')));
		$defaults = [
			CALCULATED_ITEM_DNS_DELAY => null,
			CALCULATED_ITEM_DNS_UDP_RTT_HIGH => null,
			CALCULATED_ITEM_DNS_TCP_RTT_HIGH => null
		];
		$macro = $this->getMacroHistoryValue(array_keys($defaults), $time_from);

		foreach (array_diff_key($defaults, $macro) as $key => $val) {
			error(_s('Macro "%s" value not found.', $key));
		}

		$macro += $defaults;
		$data = [
			'title' => _('Test details'),
			'module_style' => $this->module->getStyle(),
			'tld_host' => $this->tld['host'],
			'slv_item_name' => $this->slv_item['name'],
			'type' => $this->getInput('type'),
			'time' => $time_from,
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

		// Initialize probes data for probes with offline status.
		$probes = $this->getItemsHistoryValue([
			'output' => ['itemid', 'key_', 'hostid'],
			'hostids' => array_keys($this->probes),
			'filter' => [
				'key_' => PROBE_KEY_ONLINE
			],
			'monitored' => true,
			'time_from' => $time_from,
			'time_till' => $time_till,
			'history' => ITEM_VALUE_TYPE_UINT64
		]);

		foreach ($probes as $probe) {
			/**
			 * Value of probe item PROBE_KEY_ONLINE == PROBE_DOWN means that Probe is OFFLINE
			 */
			if (isset($probe['history_value']) && $probe['history_value'] == PROBE_DOWN) {
				$this->probes[$probe['hostid']]['online_status'] = PROBE_OFFLINE;
			}
		}

		if ($test_result['value'] != UP_INCONCLUSIVE_RECONFIG) {
			// Set probes test trasport.
			$protocol_type = $this->getValueMapping(RSM_DNS_TRANSPORT_PROTOCOL_VALUE_MAP);

			if (!$protocol_type) {
				error(_('Value mapping for "Transport protocol" not found.'));
			}
			else {
				$tldprobeid_probeid = array_combine(array_column($this->probes, 'tldprobe_hostid'), array_keys($this->probes));
				$tldprobe_values = $this->getItemsHistoryValue([
					'output' => ['itemid', 'hostid'],
					'hostids' => array_column($this->probes, 'tldprobe_hostid'),
					'filter' => ['key_' => PROBE_DNS_PROTOCOL],
					'time_from' => $time_from,
					'time_till' => $time_till,
					'history' => ITEM_VALUE_TYPE_UINT64
				]);

				foreach ($tldprobe_values as $tldprobe_value) {
					if (isset($tldprobe_value['history_value'])) {
						$tldprobeid = $tldprobe_value['hostid'];
						$probeid = $tldprobeid_probeid[$tldprobeid];
						$this->probes[$probeid]['transport'] = $protocol_type[$tldprobe_value['history_value']];
					}
				}
			}

			$this->getReportData($data, $time_from, $time_till);
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
	protected function getNSIDdata(array $dns_nameservers, $time_from, $time_till) {
		$key_parser = new CItemKey;
		$nsid_item_keys = [];
		$nsids = [];

		foreach ($dns_nameservers as $ns_name => $ips) {
			// Merge 'ivp4' and 'ipv6' arrays.
			$ips = array_reduce($ips, 'array_merge', []);

			foreach (array_keys($ips) as $ip) {
				$nsid_item_keys[] = strtr(PROBE_DNS_NSID, [
					'{#NS}' => $ns_name,
					'{#IP}' => $ip
				]);
			}
		}

		if (!$nsid_item_keys) {
			return $nsids;
		}

		$nsid_items = $this->getItemsHistoryValue([
			'output' => ['key_', 'type', 'hostid'],
			'hostids' => array_column($this->probes, 'tldprobe_hostid'),
			'filter' => ['key_' => $nsid_item_keys],
			'time_from' => $time_from,
			'time_till' => $time_till,
			'history' => ITEM_VALUE_TYPE_STR
		]);

		$key_parser->parse(PROBE_DNS_NSID);
		$params_count = $key_parser->getParamsNum();
		$nsids = array_unique(array_column($nsid_items, 'history_value'));
		$nsids = array_filter($nsids, 'strlen');

		if (!$nsids) {
			return $nsids;
		}

		sort($nsids, SORT_LOCALE_STRING|SORT_NATURAL|SORT_FLAG_CASE);
		$tldprobeid_probeid = array_combine(array_column($this->probes, 'tldprobe_hostid'), array_keys($this->probes));

		foreach ($nsid_items as $nsid_item) {
			$key_parser->parse($nsid_item['key_']);

			if ($key_parser->getParamsNum() != $params_count) {
				error(_s('Unexpected item key "%1$s".', $nsid_item['key_']));
				continue;
			}

			if (isset($nsid_item['history_value'])) {
				$name = $key_parser->getParam(0);
				$ip = $key_parser->getParam(1);
				$probeid = $tldprobeid_probeid[$nsid_item['hostid']];
				$this->probes[$probeid]['results_nsid'][$name][$ip] = array_search(
					$nsid_item['history_value'], $nsids
				);
			}
		}

		return $nsids;
	}
}
