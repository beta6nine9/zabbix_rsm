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
use CProfile;
use CWebUser;
use CArrayHelper;
use CPagerHelper;
use CControllerResponseData;
use CControllerResponseFatal;

class TestsListAction extends Action {

	protected function checkInput() {
		$fields = [
			'host'			=> 'required|not_empty|string',
			'type'			=> 'required|in 0,1,2,3,4',
			'slvItemId'		=> 'required|int32',
			'filter_set'	=> 'in 1',
			'filter_rst'	=> 'in 1',
			'from'			=> 'string',
			'to'			=> 'string',
			'page'			=> 'int32',
		];

		$ret = $this->validateInput($fields);

		if (!$ret) {
			$this->setResponse(new CControllerResponseFatal());
		}

		return $ret;
	}

	protected function getTLD(array &$data) {
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
			error(_('No permissions to referred object or it does not exist!'));
			return false;
		}

		return true;
	}

	protected function getItems(array &$data) {
		if ($data['type'] == RSM_DNS) {
			$key = RSM_SLV_DNS_AVAIL;
		}
		elseif ($data['type'] == RSM_DNSSEC) {
			$key = RSM_SLV_DNSSEC_AVAIL;
		}
		elseif ($data['type'] == RSM_RDDS) {
			$key = RSM_SLV_RDDS_AVAIL;
		}
		elseif ($data['type'] == RSM_RDAP) {
			$key = RSM_SLV_RDAP_AVAIL;
		}
		else {
			$key = RSM_SLV_EPP_AVAIL;
		}

		// get items
		$items = API::Item()->get([
			'output' => ['itemid', 'hostid', 'key_'],
			'hostids' => $data['tld']['hostid'],
			'filter' => [
				'key_' => $key
			],
			'preservekeys' => true
		]);

		if ($items) {
			$item = reset($items);
			$avail_item = $item['itemid'];

			// Get triggers.
			$triggers = API::Trigger()->get([
				'output' => ['triggerids'],
				'itemids' => $avail_item,
				'preservekeys' => true
			]);

			$events = API::Event()->get([
				'output' => API_OUTPUT_EXTEND,
				'selectTriggers' => API_OUTPUT_EXTEND,
				'objectids' => array_keys($triggers),
				'source' => EVENT_SOURCE_TRIGGERS,
				'object' => EVENT_OBJECT_TRIGGER,
				'time_from' => $data['from_ts'],
				'time_till' => $data['to_ts']
			]);

			CArrayHelper::sort($events, ['objectid', 'clock']);

			$i = 0;
			$incidents = [];
			$incidents_data = [];

			foreach ($events as $event) {
				if ($event['value'] == TRIGGER_VALUE_TRUE) {
					if (isset($incidents[$i]) && $incidents[$i]['status'] == TRIGGER_VALUE_TRUE) {
						// Get event end time.
						$add_event = DBfetch(DBselect(
							'SELECT e.clock'.
							' FROM events e'.
							' WHERE e.objectid='.$incidents[$i]['objectid'].
								' AND e.clock>='.$data['to_ts'].
								' AND e.object='.EVENT_OBJECT_TRIGGER.
								' AND e.source='.EVENT_SOURCE_TRIGGERS.
								' AND e.value='.TRIGGER_VALUE_FALSE.
							' ORDER BY e.clock,e.ns',
							1
						));

						if ($add_event) {
							$incidents_data[$i]['endTime'] = $add_event['clock'];
							$incidents_data[$i]['status'] = TRIGGER_VALUE_FALSE;
						}
					}

					$i++;
					$incidents[$i] = [
						'objectid' => $event['objectid'],
						'status' => TRIGGER_VALUE_TRUE,
						'startTime' => $event['clock'],
						'false_positive' => $event['false_positive']
					];
				}
				else {
					if (isset($incidents[$i])) {
						$incidents[$i] = [
							'status' => TRIGGER_VALUE_FALSE,
							'endTime' => $event['clock']
						];
					}
					else {
						$i++;
						// Get event start time.
						$add_event = API::Event()->get([
							'output' => API_OUTPUT_EXTEND,
							'objectids' => [$event['objectid']],
							'source' => EVENT_SOURCE_TRIGGERS,
							'object' => EVENT_OBJECT_TRIGGER,
							'selectTriggers' => API_OUTPUT_EXTEND,
							'time_till' => $event['clock'] - 1,
							'filter' => ['value' => TRIGGER_VALUE_TRUE],
							'limit' => 1,
							'sortorder' => ZBX_SORT_DOWN
						]);

						if ($add_event) {
							$add_event = reset($add_event);

							$incidents[$i] = [
								'objectid' => $event['objectid'],
								'status' => TRIGGER_VALUE_FALSE,
								'startTime' => $add_event['clock'],
								'endTime' => $event['clock'],
								'false_positive' => $add_event['false_positive']
							];
						}
					}
				}

				if (isset($incidents_data[$i]) && $incidents_data[$i]) {
					$incidents_data[$i] = array_merge($incidents_data[$i], $incidents[$i]);
				}
				else {
					if (isset($incidents[$i])) {
						$incidents_data[$i] = $incidents[$i];
					}
				}
			}

			if (isset($incidents[$i]) && $incidents[$i]['status'] == TRIGGER_VALUE_TRUE) {
				// Get event end time.
				$add_event = DBfetch(DBselect(
					'SELECT e.clock'.
					' FROM events e'.
					' WHERE e.objectid='.$incidents[$i]['objectid'].
						' AND e.clock>='.$data['to_ts'].
						' AND e.object='.EVENT_OBJECT_TRIGGER.
						' AND e.source='.EVENT_SOURCE_TRIGGERS.
						' AND e.value='.TRIGGER_VALUE_FALSE.
					' ORDER BY e.clock,e.ns',
					1
				));

				if ($add_event) {
					$new_data[$i] = [
						'status' => TRIGGER_VALUE_FALSE,
						'endTime' => $add_event['clock']
					];

					unset($incidents_data[$i]['status']);
					$incidents_data[$i] = array_merge($incidents_data[$i], $new_data[$i]);
				}
			}

			$tests = DBselect(
				'SELECT h.clock, h.value'.
				' FROM history_uint h'.
				' WHERE h.itemid='.$avail_item.
					' AND h.clock>='.$data['from_ts'].
					' AND h.clock<='.$data['to_ts'].
				' ORDER BY h.clock DESC'
			);

			// Result generation.
			$data['downTests'] = 0;
			$data['statusChanges'] = 0;

			while ($test = DBfetch($tests)) {
				$data['tests'][] = [
					'value' => $test['value'],
					'clock' => $test['clock'],
					'incident' => 0,
					'updated' => false
				];

					if (!$test['value']) {
						$data['downTests']++;
					}

				// State changes.
				if (!isset($status_changed)) {
					$status_changed = $test['value'];
				}
				else {
					if ($status_changed != $test['value']) {
						$status_changed = $test['value'];
						$data['statusChanges']++;
					}
				}
			}

			$data['downPeriod'] = $data['to_ts'] - $data['from_ts'];

			if ($data['type'] == RSM_DNS || $data['type'] == RSM_DNSSEC) {
				$item_key = CALCULATED_ITEM_DNS_DELAY;
			}
			elseif ($data['type'] == RSM_RDDS) {
				$item_key = CALCULATED_ITEM_RDDS_DELAY;
			}
			elseif ($data['type'] == RSM_RDAP) {
				$item_key = CALCULATED_ITEM_RDAP_DELAY;
			}
			else {
				$item_key = CALCULATED_ITEM_EPP_DELAY;
			}

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
				error(_s('No permissions to host "%1$s" or it does not exist!', RSM_HOST));
				return false;
			}

			$item = API::Item()->get([
				'output' => ['itemid', 'value_type'],
				'hostids' => $rsm['hostid'],
				'filter' => ['key_' => $item_key]
			]);

			if ($item) {
				$item = reset($item);
			}
			else {
				error(_s('Missing items at host "%1$s"!', RSM_HOST));
				return false;
			}

			$item_value = API::History()->get([
				'output' => API_OUTPUT_EXTEND,
				'itemids' => $item['itemid'],
				'time_from' => $data['from_ts'],
				'history' => $item['value_type'],
				'limit' => 1
			]);

			$item_value = reset($item_value);

			$time_step = $item_value['value'] ?  $item_value['value'] / SEC_PER_MIN : 1;

			$data['downTimeMinutes'] = $data['downTests'] * $time_step;

			foreach ($incidents_data as $incident) {
				foreach ($data['tests'] as $key => $test) {
					if (!$test['updated'] && $incident['startTime'] <= $test['clock'] && (!isset($incident['endTime'])
							|| (isset($incident['endTime']) && $incident['endTime'] >= $test['clock']))) {
						$data['tests'][$key]['incident'] = $incident['false_positive'] ? INCIDENT_FALSE_POSITIVE : INCIDENT_RESOLVED;
						$data['tests'][$key]['updated'] = true;
					}
				}
			}
		}

		return true;
	}

	protected function doAction() {
		$macros = API::UserMacro()->get([
			'output' => ['macro', 'value'],
			'filter' => ['macro' => RSM_ROLLWEEK_SECONDS],
			'globalmacro' => true
		]);
		$macros = array_column($macros, 'value', 'macro');
		$timeshift = ($macros[RSM_ROLLWEEK_SECONDS]%SEC_PER_DAY)
					? $macros[RSM_ROLLWEEK_SECONDS]
					: ($macros[RSM_ROLLWEEK_SECONDS]/SEC_PER_DAY).'d';

		$data = [
			'title' => _('Tests'),
			'ajax_request' => $this->isAjaxRequest(),
			'refresh' => CWebUser::$data['refresh'] ? timeUnitToSeconds(CWebUser::$data['refresh']) : null,
			'module_style' => $this->module->getStyle(),
			'profileIdx' => 'web.rsm.tests.filter',
			'active_tab' => CProfile::get('web.rsm.tests.filter.active', 1),
			'rsm_monitoring_mode' => get_rsm_monitoring_type(),
			'host' => $this->getInput('host'),
			'type' => $this->getInput('type'),
			'slvItemId' => $this->getInput('slvItemId'),
			'from' => $this->getInput('from', ZBX_PERIOD_DEFAULT_FROM),
			'to' => $this->getInput('to', ZBX_PERIOD_DEFAULT_TO),
			'rollingweek_from' => 'now-'.$timeshift,
			'rollingweek_to' => 'now',
			'tests' => [],
		];

		$timeline = getTimeSelectorPeriod([
			'profileIdx' => $data['profileIdx'],
			'profileIdx2' => 0,
			'from' => $data['from'],
			'to' => $data['to'],
		]);
		$data += [
			'from_ts' => $timeline['from_ts'],
			'to_ts' => $timeline['to_ts'],
		];

		if ($this->getTLD($data) && $this->getItems($data)) {
			$data['paging'] = CPagerHelper::paginate($this->getInput('page', 1), $data['tests'], ZBX_SORT_UP, new CUrl());

			$response = new CControllerResponseData($data);
			$response->setTitle($data['title']);
			$this->setResponse($response);
		}
		else {
			$this->setResponse(new CControllerResponseFatal());
		}
	}
}
