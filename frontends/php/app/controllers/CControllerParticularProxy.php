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

class CControllerParticularProxy extends CController {

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
			'host' => 'required|string',
			'type' => 'required|in 0,1',
			'probe' => 'required|string',
			'time' => 'required|int32',
			'slvItemId' => 'required|int32'
		];

		$ret = $this->validateInput($fields);

		if (!$ret) {
			$this->setResponse(new CControllerResponseFatal());
		}

		return $ret;
	}

	protected function updateProfiles(array &$data) {
		if ($data['host'] && $data['time'] && $data['slvItemId'] && $data['type'] !== null && $data['probe']) {
			CProfile::update('web.rsm.particularproxys.host', $data['host'], PROFILE_TYPE_STR);
			CProfile::update('web.rsm.particularproxys.time', $data['time'], PROFILE_TYPE_ID);
			CProfile::update('web.rsm.particularproxys.slvItemId', $data['slvItemId'], PROFILE_TYPE_ID);
			CProfile::update('web.rsm.particularproxys.type', $data['type'], PROFILE_TYPE_ID);
			CProfile::update('web.rsm.particularproxys.probe', $data['probe'], PROFILE_TYPE_STR);
		}
		elseif (!$data['host'] && !$data['time'] && !$data['slvItemId'] && $data['type'] === null && !$data['probe']) {
			$data['host'] = CProfile::get('web.rsm.particularproxys.host');
			$data['time'] = CProfile::get('web.rsm.particularproxys.time');
			$data['slvItemId'] = CProfile::get('web.rsm.particularproxys.slvItemId');
			$data['type'] = CProfile::get('web.rsm.particularproxys.type');
			$data['probe'] = CProfile::get('web.rsm.particularproxys.probe');
		}
	}

	protected function getReportData(array &$data) {
		$test_time_from = mktime(
			date('H', $data['time']),
			date('i', $data['time']),
			0,
			date('n', $data['time']),
			date('j', $data['time']),
			date('Y', $data['time'])
		);

		// Get TLD.
		$tld = API::Host()->get([
			'output' => ['hostid', 'host', 'name'],
			'tlds' => true,
			'filter' => [
				'host' => $data['host']
			]
		]);

		// Get slv item.
		$slv_items = API::Item()->get([
			'output' => ['name'],
			'itemids' => $data['slvItemId']
		]);

		// Get probe.
		$probe = API::Host()->get([
			'output' => ['hostid', 'host', 'name'],
			'filter' => [
				'host' => $data['probe']
			]
		]);

		// Get host with calculated items.
		$rsm = API::Host()->get([
			'output' => ['hostid'],
			'filter' => [
				'host' => RSM_HOST
			]
		]);

		if (!$probe || !$slv_items || !$tld || !$rsm) {
			if (!$rsm) {
				error(_s('No permissions to referred host "%1$s" or it does not exist!', RSM_HOST));
			}

			return;
		}

		$data['probe'] = reset($probe);
		$data['tld'] = reset($tld);
		$data['slvItem'] = reset($slv_items);
		$rsm = reset($rsm);

		$macro_item_key[] = CALCULATED_ITEM_DNS_UDP_RTT_HIGH;
		if ($data['type'] == RSM_DNS) {
			$macro_item_key[] = CALCULATED_ITEM_DNS_DELAY;
			$macro_item_key[] = CALCULATED_ITEM_DNS_AVAIL_MINNS;	// TODO: remove 3 months after deployment
		}
		elseif ($data['type'] == RSM_DNSSEC) {
			$macro_item_key[] = CALCULATED_ITEM_DNS_DELAY;
		}
		elseif ($data['type'] == RSM_RDDS) {
			$macro_item_key[] = CALCULATED_ITEM_RDDS_DELAY;
		}
		else {
			$macro_item_key[] = CALCULATED_ITEM_EPP_DELAY;
		}

		// Get macros old value.
		$macro_items = API::Item()->get([
			'output' => ['itemid', 'value_type', 'key_'],
			'hostids' => $rsm['hostid'],
			'filter' => [
				'key_' => $macro_item_key
			]
		]);

		// Check items.
		if (count($macro_items) != count($macro_item_key)) {
			error(_s('Missing calculated items at host "%1$s"!', RSM_HOST));
			return;
		}

		// Get time till.
		foreach ($macro_items as $key => $macro_item) {
			if (in_array($macro_item['key_'], [CALCULATED_ITEM_DNS_DELAY, CALCULATED_ITEM_RDDS_DELAY, CALCULATED_ITEM_EPP_DELAY])) {
				$macro_item_value = API::History()->get([
					'output' => API_OUTPUT_EXTEND,
					'itemids' => $macro_item['itemid'],
					'time_from' => $test_time_from,
					'history' => $macro_item['value_type'],
					'limit' => 1
				]);

				$macro_item_value = reset($macro_item_value);
				$test_time_till = $test_time_from + $macro_item_value['value'] - 1;

				unset($macro_items[$key]);
			}
		}

		foreach ($macro_items as $macroItem) {
			$macro_item_value = API::History()->get([
				'output' => API_OUTPUT_EXTEND,
				'itemids' => $macroItem['itemid'],
				'time_from' => $test_time_from,
				'time_till' => $test_time_till,
				'history' => $macroItem['value_type']
			]);

			$macro_item_value = reset($macro_item_value);

			if ($macroItem['key_'] == CALCULATED_ITEM_DNS_UDP_RTT_HIGH) {
				$dns_udp_rtt = $macro_item_value['value'];
			}
			else {
				$min_ns = $macro_item_value['value'];
			}
		}

		// Get test result for DNS service.
		if ($data['type'] == RSM_DNS) {
			$probe_result_items = API::Item()->get([
				'output' => ['itemid', 'value_type', 'key_'],
				'hostids' => $data['probe']['hostid'],
				'filter' => [
					'key_' => PROBE_DNS_UDP_ITEM
				],
				'monitored' => true
			]);

			$probe_result_item = reset($probe_result_items);

			$item_value = API::History()->get([
				'output' => API_OUTPUT_EXTEND,
				'itemids' => $probe_result_item['itemid'],
				'time_from' => $test_time_from,
				'time_till' => $test_time_till,
				'history' => $probe_result_item['value_type']
			]);

			if ($item_value) {
				$item_value = reset($item_value);
				$data['testResult'] = ($item_value['value'] >= $min_ns);
			}
			else {
				$data['testResult'] = null;
			}
		}

		// Get items.
		$probe_items = API::Item()->get([
			'output' => ['itemid', 'key_', 'hostid', 'valuemapid', 'units', 'value_type'],
			'hostids' => $data['probe']['hostid'],
			'search' => [
				'key_' => PROBE_DNS_UDP_ITEM_RTT
			],
			'startSearch' => true,
			'monitored' => true,
			'preservekeys' => true
		]);

		$total_ns = [];
		$negative_ns = [];
		foreach ($probe_items as $probe_item) {
			preg_match('/^[^\[]+\[([^\]]+)]$/', $probe_item['key_'], $matches);
			$ns_values = explode(',', $matches[1]);

			// Get NS values.
			$item_value = API::History()->get([
				'output' => API_OUTPUT_EXTEND,
				'itemids' => $probe_item['itemid'],
				'time_from' => $test_time_from,
				'time_till' => $test_time_till,
				'history' => $probe_item['value_type']
			]);

			$item_value = reset($item_value);

			$ms = convert_units(['value' => $item_value['value'], 'units' => $probe_item['units']]);
			$ms = $item_value ? applyValueMap($ms, $probe_item['valuemapid']) : null;

			$data['proxys'][$probe_item['itemid']] = [
				'ns' => $ns_values[1],
				'ip' => $ns_values[2],
				'ms' => $ms
			];

			$total_ns[$ns_values[1]] = true;

			if (($item_value['value'] < 0 || $item_value['value'] > $dns_udp_rtt) && $item_value['value'] !== null) {
				$negative_ns[$ns_values[1]] = true;
			}
		}

		$data['totalNs'] = count($total_ns);
		$data['positiveNs'] = count($total_ns) - count($negative_ns);
		$data['minMs'] = $dns_udp_rtt;
	}

	protected function doAction() {
		// Report is not available in registrar mode.
		if (get_rsm_monitoring_type() === MONITORING_TARGET_REGISTRAR) {
			$this->setResponse(new CControllerResponseRedirect((new CUrl('zabbix.php'))
				->setArgument('action', 'rsm.particulartests')
				->setArgument('host', $this->getInput('host', ''))
				->getUrl()
			));
		}

		$data = [
			'title' => _('Test result from particular proxy'),
			'host' => $this->getInput('host', null),
			'time' => $this->getInput('time', null),
			'slvItemId' => $this->getInput('slvItemId', null),
			'type' => $this->getInput('type', null),
			'probe' => $this->getInput('probe', null),
			'proxys' => []
		];

		$this->updateProfiles($data);

		if ($data['host'] && $data['time'] && $data['slvItemId'] && $data['type'] !== null && $data['probe']) {
			$this->getReportData($data);
		}

		$response = new CControllerResponseData($data);
		$response->setTitle($data['title']);
		$this->setResponse($response);
	}
}
