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

class CControllerIncidentDetails extends CController {

	protected function init() {
		$this->disableSIDValidation();
	}

	protected function checkInput() {
		$fields = [
			'host'					=>	'required|db hosts.host',
			'eventid'				=>	'db events.eventid',
			'slvItemId'				=>	'db items.itemid',
			'availItemId'			=>	'required|db items.itemid',
			'filter_failing_tests'	=>	'in 0,1',
			'filter_set'			=>	'in 1',
			'filter_rst'			=>	'in 1',
			'from'					=>	'string',
			'to'					=>	'string'
		];

		$ret = $this->validateInput($fields);

		if (!$ret) {
			$this->setResponse(new CControllerResponseFatal());
		}

		return $ret;
	}

	protected function checkPermissions() {
		$valid_users = [USER_TYPE_READ_ONLY, USER_TYPE_ZABBIX_USER, USER_TYPE_POWER_USER, USER_TYPE_COMPLIANCE,
			USER_TYPE_ZABBIX_ADMIN, USER_TYPE_SUPER_ADMIN];

		return in_array($this->getUserType(), $valid_users);
	}

	protected function updateProfile(array &$data = []) {
		$data += [
			'host' => $this->getInput('host', ''),
			'eventid' =>  $this->hasInput('eventid') ? $this->getInput('eventid') : null,
			'slvItemId' => $this->hasInput('slvItemId') ? $this->getInput('slvItemId') : null,
			'availItemId' => $this->hasInput('availItemId') ? $this->getInput('availItemId') : null
		];

		if ($this->hasInput('filter_set')) {
			$data['filter_failing_tests'] = $this->getInput('filter_failing_tests', 0);
			updateTimeSelectorPeriod($data);
			CProfile::update('web.rsm.incidentdetails.filter_failing_tests', $data['filter_failing_tests'], PROFILE_TYPE_INT);
		}
		elseif ($this->hasInput('filter_rst')) {
			$data['filter_failing_tests'] = 0;
			$data['from'] = ZBX_PERIOD_DEFAULT_FROM;
			$data['to'] = ZBX_PERIOD_DEFAULT_TO;
			updateTimeSelectorPeriod($data);
			CProfile::delete('web.rsm.incidentdetails.filter_failing_tests');
		}
		else {
			$data['filter_failing_tests'] = CProfile::get('web.rsm.incidentdetails.filter_failing_tests');
		}

		$data = getTimeSelectorPeriod($data);
	}

	protected function getTLD(array &$data) {
		if ($data['eventid'] && $data['slvItemId'] && $data['availItemId'] && $data['host']) {
			$tld = API::Host()->get([
				'output' => ['hostid', 'host', 'info_1', 'info_2'],
				'filter' => [
					'host' => $data['host']
				],
				'tlds' => true
			]);

			if ($tld) {
				$data['tld'] = reset($tld);
			}
			else {
				$this->access_deny = true;
			}
		}
		else {
			$this->access_deny = true;
		}
	}

	protected function getSLV(array &$data) {
		if ($this->access_deny) {
			return;
		}

		// Get SLV item.
		$slv_items = API::Item()->get([
			'output' => ['name', 'key_', 'lastvalue', 'lastclock'],
			'itemids' => $data['slvItemId']
		]);

		if ($slv_items) {
			$data['slvItem'] = reset($slv_items);
		}
		else {
			$this->access_deny = true;
		}
	}

	protected function getMainEvent(array &$data) {
		if ($this->access_deny) {
			return false;
		}

		$main_event = API::Event()->get([
			'output' => API_OUTPUT_EXTEND,
			'eventids' => $data['eventid']
		]);

		if ($main_event) {
			$data['main_event'] = reset($main_event);
		}
	}

	protected function getRSM(array &$data) {
		if ($this->access_deny) {
			return false;
		}

		$rsm = API::Host()->get([
			'output' => ['hostid'],
			'filter' => [
				'host' => RSM_HOST
			]
		]);

		if ($rsm) {
			$data['rsm'] = reset($rsm);
		}
		else {
			error(_s('No permissions to referred host "%1$s" or it does not exist!', RSM_HOST));
			$this->access_deny = true;
		}
	}

	protected function getData(array &$data) {
		if ($this->access_deny) {
			return;
		}

		switch ($data['slvItem']['key_']) {
			case RSM_SLV_DNS_ROLLWEEK:
				$keys = [CALCULATED_ITEM_DNS_FAIL, CALCULATED_ITEM_DNS_DELAY];
				$data['type'] = RSM_DNS;
				break;

			case RSM_SLV_DNSSEC_ROLLWEEK:
				$keys = [CALCULATED_ITEM_DNSSEC_FAIL, CALCULATED_ITEM_DNS_DELAY];
				$data['type'] = RSM_DNSSEC;
				break;

			case RSM_SLV_RDAP_ROLLWEEK:
				$keys = [CALCULATED_ITEM_RDAP_FAIL, CALCULATED_ITEM_RDAP_DELAY];
				$data['type'] = RSM_RDAP;
				break;

			case RSM_SLV_RDDS_ROLLWEEK:
				$keys = [CALCULATED_ITEM_RDDS_FAIL, CALCULATED_ITEM_RDDS_DELAY];
				$data['type'] = RSM_RDDS;
				$data['tld']['subservices'] = [];

				$templates = API::Template()->get([
					'output' => ['templateid'],
					'filter' => [
						'host' => 'Template '.$data['tld']['host']
					],
					'preservekeys' => true
				]);

				if (($template = reset($templates)) !== false) {
					$ok_rdds_services = [];

					$template_macros = API::UserMacro()->get([
						'output' => ['macro', 'value'],
						'hostids' => $template['templateid'],
						'filter' => [
							'macro' => is_RDAP_standalone($data['main_event']['clock'])
								? [RSM_TLD_RDDS43_ENABLED, RSM_TLD_RDDS80_ENABLED, RSM_TLD_RDDS_ENABLED]
								: [RSM_TLD_RDDS43_ENABLED, RSM_TLD_RDDS80_ENABLED, RSM_RDAP_TLD_ENABLED,
										RSM_RDAP_TLD_ENABLED, RSM_TLD_RDDS_ENABLED]
						]
					]);

					foreach ($template_macros as $template_macro) {
						$data['tld']['subservices'][$template_macro['macro']] = $template_macro['value'];

						if ($template_macro['macro'] === RSM_TLD_RDDS_ENABLED && $template_macro['value'] != 0) {
							$ok_rdds_services[] = 'RDDS';
						}
						elseif ($template_macro['macro'] === RSM_RDAP_TLD_ENABLED && $template_macro['value'] != 0) {
							$ok_rdds_services[] = 'RDAP';
						}
					}

					$data['testing_interfaces'] = implode(' / ', $ok_rdds_services);
				}
				break;

			case RSM_SLV_EPP_ROLLWEEK:
				$keys = [CALCULATED_ITEM_EPP_FAIL, CALCULATED_ITEM_EPP_DELAY];
				$data['type'] = RSM_EPP;
				break;
		}

		$items = API::Item()->get([
			'output' => ['itemid', 'key_'],
			'hostids' => $data['rsm']['hostid'],
			'filter' => [
				'key_' => $keys
			]
		]);

		if (count($items) != 2) {
			error(_s('Missing items at host "%1$s"!', RSM_HOST));
			$this->access_deny = true;
			return;
		}

		foreach ($items as $item) {
			if ($item['key_'] == CALCULATED_ITEM_DNS_FAIL
					|| $item['key_'] == CALCULATED_ITEM_DNSSEC_FAIL
					|| $item['key_'] == CALCULATED_ITEM_RDDS_FAIL
					|| $item['key_'] == CALCULATED_ITEM_RDAP_FAIL
					|| $item['key_'] == CALCULATED_ITEM_EPP_FAIL) {
				$fail_count = getFirstUintValue($item['itemid'], $data['main_event']['clock']);
			}
			else {
				$delay_time = getFirstUintValue($item['itemid'], $data['main_event']['clock']);
			}
		}

		$main_event_from_time = $data['main_event']['clock'] - $data['main_event']['clock'] % $delay_time - ($fail_count - 1) * $delay_time;
		$original_main_event_from_time = $main_event_from_time;
		$main_event_from_time -= (DISPLAY_CYCLES_BEFORE_RECOVERY * $delay_time);

		if ($this->hasInput('filter_set')) {
			$from_time = ($main_event_from_time >= $data['from_ts'])
				? $main_event_from_time
				: $data['from_ts'];
		}
		else {
			$from_time = $main_event_from_time;
		}

		// Get end event.
		$end_event_time_till = $this->hasInput('filter_set') ? ' AND e.clock<='.$data['to_ts'] : '';

		$end_event = DBfetch(DBselect(
			'SELECT e.clock,e.value'.
			' FROM events e'.
			' WHERE e.objectid='.$data['main_event']['objectid'].
				' AND e.clock>='.$data['main_event']['clock'].
				$end_event_time_till.
				' AND e.object='.EVENT_OBJECT_TRIGGER.
				' AND e.source='.EVENT_SOURCE_TRIGGERS.
				' AND e.value='.TRIGGER_VALUE_FALSE.
			' ORDER BY e.clock',
			1
		));

		$to_time = $this->hasInput('filter_set') ? $data['to_ts'] : $this->server_time;

		if ($end_event) {
			$to_time = $end_event['clock'] - ($end_event['clock'] % $delay_time) + (DISPLAY_CYCLES_AFTER_RECOVERY * $delay_time);

			if ($this->hasInput('filter_set') && $to_time >= $data['to_ts']) {
				$to_time = $data['to_ts'];
			}
		}

		// result generation
		$data['slv'] = sprintf('%.3f', $data['slvItem']['lastvalue']);
		$data['slvTestTime'] = (int) $data['slvItem']['lastclock'];

		if ($data['main_event']['false_positive']) {
			$data['incidentType'] = INCIDENT_FALSE_POSITIVE;
		}
		elseif ($end_event && $end_event['value'] == TRIGGER_VALUE_FALSE) {
			$data['incidentType'] = INCIDENT_RESOLVED;
		}
		else {
			$data['incidentType'] = INCIDENT_ACTIVE;
		}

		$data['active'] = (bool) $end_event;

		$failing_tests = $data['filter_failing_tests'] ? ' AND h.value='.DOWN : '';
		$tests = DBselect($sql =
			'SELECT h.value, h.clock'.
			' FROM history_uint h'.
			' WHERE h.itemid='.zbx_dbstr($data['availItemId']).
				' AND h.clock>='.$from_time.
				' AND h.clock<='.$to_time.
				$failing_tests.
			' ORDER BY h.clock asc'
		);

		$printed = []; // Prevent listing of repeated tests before incident start time. This can happen right after delay is changed.
		while ($test = DBfetch($tests)) {
			if ($test['clock'] > $original_main_event_from_time || !array_key_exists($test['clock'], $printed)) {
				$printed[$test['clock']] = true;
				$data['tests'][] = [
					'clock' => $test['clock'],
					'value' => $test['value'],
					'startEvent' => ($data['main_event']['clock'] == $test['clock']),
					'endEvent' => $end_event && $end_event['clock'] == $test['clock'] ? $end_event['value'] : TRIGGER_VALUE_TRUE
				];
			}
		}
		unset($printed);

		$slvs = DBselect(
			'SELECT h.value,h.clock'.
			' FROM history h'.
			' WHERE h.itemid='.zbx_dbstr($data['slvItemId']).
				' AND h.clock>='.$from_time.
				' AND h.clock<='.$to_time.
			' ORDER BY h.clock asc'
		);

		$slv = DBfetch($slvs);

		foreach ($data['tests'] as &$test) {
			while ($slv) {
				$latest = $slv;

				if (!($slv = DBfetch($slvs)) || $slv['clock'] > $test['clock']) {
					$test['slv'] = sprintf('%.3f', $latest['value']);
					break;
				}
			}
		}
	}

	protected function doAction() {
		$this->access_deny = false;
		$this->server_time = time() - RSM_ROLLWEEK_SHIFT_BACK;

		$data = [
			'title' => _('Incidents details'),
			'profileIdx' => 'web.rsm.incidentsdetails.filter',
			'profileIdx2' => 0,
			'from' => $this->getInput('from', ZBX_PERIOD_DEFAULT_FROM),
			'to' => $this->getInput('to', ZBX_PERIOD_DEFAULT_TO),
			'active_tab' => CProfile::get('web.rsm.incidentsdetails.filter.active', 1),
			'filter_failing_tests' => 0,
			'rsm_monitoring_mode' => get_rsm_monitoring_type(),
			'tests' => [],
			'sid' => CWebUser::getSessionCookie()
		];

		$this->getInputs($data, ['from', 'to']);
		$this->updateProfile($data);
		$this->getTLD($data);
		$this->getSLV($data);
		$this->getMainEvent($data);
		$this->getRSM($data);
		$this->getData($data);

		$data['paging'] = getPagingLine($data['tests'], ZBX_SORT_UP, new CUrl());

		if ($data['tests']) {
			$data['test_value_mapping'] = [];

			$test_value_mapping = API::ValueMap()->get([
				'output' => [],
				'selectMappings' => ['value', 'newvalue'],
				'valuemapids' => [RSM_SERVICE_AVAIL_VALUE_MAP]
			]);

			if ($test_value_mapping) {
				foreach ($test_value_mapping[0]['mappings'] as $val) {
					$data['test_value_mapping'][$val['value']] = $val['newvalue'];
				}
			}
		}

		$response = new CControllerResponseData($data);
		$response->setTitle($data['title']);
		$this->setResponse($response);
	}
}
