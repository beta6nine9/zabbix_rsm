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
use Modules\RSM\Helpers\ValueMapHelper as VM;

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

			foreach ($tld_probes as $res) {
				$probeid = $tld_probe_names[$res['host']];
				$this->probes[$probeid] += [
					'tldprobe_hostid' => $res['hostid'],
					'tldprobe_host' => $res['host'],
					'probe_status' => ($res['status'] == HOST_STATUS_NOT_MONITORED ? PROBE_DISABLED : PROBE_UNKNOWN),
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

		foreach ($this->probes as $probeid => $probe) {
			$tldprobeid_probeid[$probe['tldprobe_hostid']] = $probeid;
		}

		if (!$tldprobeid_probeid) {
			return;
		}

		// Get all the test items.
		$test_items = API::Item()->get([
			'output' => ['key_', 'itemid', 'hostid'],
			'hostids' => array_keys($tldprobeid_probeid),
			'search' => [
				'key_' => 'rsm.dns'
			],
			'startSearch' => true,
		]);

		if ($test_items) {
			// Get all the test results, from both history tables.
			$test_values = API::History()->get([
				'output' => API_OUTPUT_EXTEND,
				'itemids' => array_column($test_items, 'itemid'),
				'time_from' => $time_from,
				'time_till' => $time_till,
				'history' => ITEM_VALUE_TYPE_UINT64,
			]);

			$test_values = array_merge($test_values, API::History()->get([
				'output' => API_OUTPUT_EXTEND,
				'itemids' => array_column($test_items, 'itemid'),
				'time_from' => $time_from,
				'time_till' => $time_till,
				'history' => ITEM_VALUE_TYPE_FLOAT,
			]));

			$test_values = array_column($test_values, 'value', 'itemid');
		}

		$probe_nstotal = [];

		foreach ($test_items as $test_item) {
			if (!array_key_exists($test_item['itemid'], $test_values)) {
				continue;
			}

			$probeid = $tldprobeid_probeid[$test_item['hostid']];

			// If probe status is already set it's either OFFLINE or DISABLED, disregard data from them.
			if ($this->probes[$probeid]['probe_status'] == PROBE_OFFLINE || $this->probes[$probeid]['probe_status'] == PROBE_DISABLED) {
				continue;
			}

			$key_parser->parse($test_item['key_']);

			$value = $test_values[$test_item['itemid']];

			switch ($key_parser->getKey()) {
				case PROBE_DNS_MODE:
					// This is informational item, we do not use it.
					break;

				case PROBE_DNS_NSSOK:
					// Set Name servers up count.
					$this->probes[$probeid]['ns_up'] = $value;
					break;

				case PROBE_DNS_STATUS:
					// Set DNS Test status.
					if ($data['type'] == RSM_DNS) {
						$this->probes[$probeid]['probe_status'] = $value;
					}
					break;

				case PROBE_DNSSEC_STATUS:
					// Set DNSSEC Test status.
					if ($data['type'] == RSM_DNSSEC) {
						$this->probes[$probeid]['probe_status'] = $value;
					}
					break;

				case PROBE_DNS_NS_STATUS:
					// Set Name server status.
					$this->probes[$probeid]['results'][$key_parser->getParam(0)]['status'] = $value;
					$probe_nstotal[$probeid] = isset($probe_nstotal[$probeid]) ? $probe_nstotal[$probeid] + 1 : 1;
					break;

				case CALCULATED_PROBE_RSM_IP4_ENABLED:
					$this->probes[$probeid]['ipv4'] = $value;
					break;

				case CALCULATED_PROBE_RSM_IP6_ENABLED:
					$this->probes[$probeid]['ipv6'] = $value;
					break;

				case PROBE_DNS_TRANSPORT:
					$this->probes[$probeid]['transport'] = VM::get(RSM_VALUE_MAP_TRANSPORT_PROTOCOL, $value);
					break;

				case PROBE_DNS_RTT:
					if ($key_parser->getParamsNum() != 3) {
						error(_s('Unexpected item key "%1$s".', $test_item['key_']));
						break;
					}

					$ns = $key_parser->getParam(0);
					$ip = $key_parser->getParam(1);

					$ipv = filter_var($ip, FILTER_VALIDATE_IP, FILTER_FLAG_IPV4) ? 'ipv4' : 'ipv6';
					$dns_nameservers[$ns][$ipv][$ip] = true;

					$this->probes[$probeid]['results'][$ns][$ipv][$ip] = $value;
					break;

				default:
					error(_s('Unexpected item key "%1$s".', $test_item['key_']));
					break;
			}
		}

		$data['dns_nameservers'] = $dns_nameservers;
		$data['nsids'] = $this->getNSIDdata($dns_nameservers, $time_from, $time_till);

		// Set Name servers down for each probe.
		foreach ($probe_nstotal as $probeid => $total) {
			$this->probes[$probeid]['ns_down'] = $total - $this->probes[$probeid]['ns_up'];
		}

		// Collect all the RTTs that are either errors or above the limit and set "No result" for probes that have no result.
		foreach ($this->probes as $probeid => $probe) {
			if ($probe['probe_status'] == PROBE_UNKNOWN) {
				$this->probes[$probeid]['probe_status'] = PROBE_NORESULT;
				continue;
			}

			if ($probe['probe_status'] == PROBE_DISABLED || $probe['probe_status'] == PROBE_OFFLINE) {
				continue;
			}

			$transport = $this->probes[$probeid]['transport'];
			$rtt_max = ($transport == 'udp') ? $data['udp_rtt'] : $data['tcp_rtt'];

			foreach ($probe['results'] as $ns => $values) {
				foreach ($values as $ipv => $ipdata) {
					if (substr($ipv, 0, 3) !== "ipv")
						continue;

					foreach ($ipdata as $ip => $value) {
						$error_key = $ns . $ip;

						if ($value < 0) {
							if (isServiceErrorCode($value, $data['type'])) {
								$this->probes[$probeid]['dns_error'][$error_key] = true;
							}

							if (!isset($this->probe_errors[$value][$error_key])) {
								$this->probe_errors[$value][$error_key] = 0;
							}

							$this->probe_errors[$value][$error_key]++;
						}
						elseif ($value > $rtt_max && $data['type'] == RSM_DNS) {
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
				}
			}
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
			error(_s('History value of "%s" not found.', $key));
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

		if (!array_key_exists('test_result', $data) || $data['test_result'] == UP_INCONCLUSIVE_RECONFIG) {
			// In case of UP_INCONCLUSIVE_RECONFIG set all probes to "No result".
			foreach ($this->probes as $probeid => $probe) {
				$this->probes[$probeid]['probe_status'] = PROBE_NORESULT;
			}
		}
		else {
			// "Offline" probes get status right away.
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
					$this->probes[$probe['hostid']]['probe_status'] = PROBE_OFFLINE;
				}
			}

			// DNSSEC-specific errors for displaying in a table
			if ($data['type'] == RSM_DNSSEC) {
				foreach (VM::getMapping(RSM_VALUE_MAP_DNS_RTT) as $code => $description) {
					if (isServiceErrorCode($code, RSM_DNSSEC)) {
						$data['dnssec_errors'][$code] = $description;
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

	protected function getHeartbeat($hostid, $nsid_item_keys) {
		$heartbeat_item = API::Item()->get([
			'output'              => 'itemid',
			'hostids'             => $hostid,
			'filter'              => ['key_' => $nsid_item_keys],
			'selectPreprocessing' => 'extend',
			'limit'               => 1,
		]);

		foreach ($heartbeat_item[0]['preprocessing'] as $rule) {
			if ($rule['type'] != ZBX_PREPROC_THROTTLE_TIMED_VALUE)
				continue;

			return $rule['params'];
		}

		return 0;
	}

	/**
	 * Collects NSID item values for all probe name servers and ips. NSID unique values will be stored in
	 * $data['nsids'] with key having incremental index and value NSID value. Additionaly probe ns+ip NSID results
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

		// get NSID heartbeat value from the first item of the first probe
		$heartbeat = $this->getHeartbeat(reset($this->probes)['tldprobe_hostid'], $nsid_item_keys);

		if (!$heartbeat) {
			error('Cannot get NSID heartbeat value');
			return $nsids;
		}

		$options = [
			'output' => ['key_', 'type', 'hostid'],
			'hostids' => array_column($this->probes, 'tldprobe_hostid'),
			'filter' => ['key_' => $nsid_item_keys],
			'time_from' => $time_from - $heartbeat,
			'time_till' => $time_till,
			'history' => ITEM_VALUE_TYPE_STR,
		];

		$history_options = [
			'sortorder' => ZBX_SORT_DOWN,
			'sortfield' => 'clock',
		];

		$nsid_items = $this->getItemsHistoryValue($options, $history_options);

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
