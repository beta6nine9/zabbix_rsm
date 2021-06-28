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
use CPagerHelper;
use CControllerResponseFatal;
use CControllerResponseData;
use CControllerResponseRedirect;
use Modules\RSM\Helpers\UrlHelper;

class IncidentDetailsAction extends Action {

	protected function checkInput() {
		$fields = [
			'host'					=> 'required|db hosts.host',
			'eventid'				=> 'db events.eventid',
			'slvItemId'				=> 'db items.itemid',
			'availItemId'			=> 'required|db items.itemid',
			'filter_failing_tests'	=> 'in 0,1',
			'filter_set'			=> 'in 1',
			'filter_rst'			=> 'in 1',
			'from'					=> 'string',
			'to'					=> 'string',
			'rolling_week'			=> 'in 1',
			'page'					=> 'int32',
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

		if (!in_array($this->getUserType(), $valid_users))
			return false;

		// ensure we have access to Rsmhost, limit output to hostid to avoid selecting the whole thing
		return !empty(API::Host()->get(['output' => ['hostid'], 'filter' => ['host' => $this->getInput('host', null)]]));
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
			CProfile::update('web.rsm.incidents.filter_failing_tests', $data['filter_failing_tests'], PROFILE_TYPE_INT);
		}
		elseif ($this->hasInput('filter_rst')) {
			$data['filter_failing_tests'] = 0;
			$data['from'] = ZBX_PERIOD_DEFAULT_FROM;
			$data['to'] = ZBX_PERIOD_DEFAULT_TO;
			updateTimeSelectorPeriod($data);
			CProfile::delete('web.rsm.incidents.filter_failing_tests');
		}
		else {
			$data['filter_failing_tests'] = CProfile::get('web.rsm.incidents.filter_failing_tests');
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

			$data['is_rdap_standalone'] = is_RDAP_standalone($data['main_event']['clock']);
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
						'host' => sprintf(TEMPLATE_NAME_TLD_CONFIG, $data['tld']['host'])
					],
					'preservekeys' => true
				]);

				if (($template = reset($templates)) === false) {
					error(_s('Cannot get configuration template!'));
					$this->access_deny = true;
					return;
				}
				else {
					$ok_rdds_services = [];

					$template_macros = API::UserMacro()->get([
						'output' => ['macro', 'value'],
						'hostids' => $template['templateid'],
						'filter' => [
							'macro' => $data['is_rdap_standalone']
								? [RSM_TLD_RDDS43_ENABLED, RSM_TLD_RDDS80_ENABLED]
								: [RSM_TLD_RDDS43_ENABLED, RSM_TLD_RDDS80_ENABLED, RSM_RDAP_TLD_ENABLED]
						]
					]);

					foreach ($template_macros as $template_macro) {
						$data['tld']['subservices'][$template_macro['macro']] = $template_macro['value'];

						if ($template_macro['macro'] === RSM_TLD_RDDS43_ENABLED && $template_macro['value'] != 0) {
							$ok_rdds_services[] = 'RDDS43';
						}
						elseif ($template_macro['macro'] === RSM_TLD_RDDS80_ENABLED && $template_macro['value'] != 0) {
							$ok_rdds_services[] = 'RDDS80';
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

		// get recovery event
		$recovery_event = null;

		if ($data['main_event']['r_eventid']) {
			$recovery_event = API::Event()->get([
				'output' => ['clock'],
				'eventids' => $data['main_event']['r_eventid']
			]);
			$recovery_event = reset($recovery_event);
		}

		// get from/till times
		$from = $data['from_ts'];
		$till = $data['to_ts'];

		$from = max($from, $data['main_event']['clock'] - DISPLAY_CYCLES_BEFORE_RECOVERY * $delay_time);

		if ($recovery_event) {
			$till = min($till, $recovery_event['clock'] + DISPLAY_CYCLES_AFTER_RECOVERY * $delay_time);
		}

		// result generation
		$data['slv'] = sprintf('%.3f', $data['slvItem']['lastvalue']);
		$data['slvTestTime'] = (int) $data['slvItem']['lastclock'];

		if ($data['main_event']['false_positive']) {
			$data['incidentType'] = INCIDENT_FALSE_POSITIVE;
		}
		elseif ($recovery_event) {
			$data['incidentType'] = INCIDENT_RESOLVED;
		}
		else {
			$data['incidentType'] = INCIDENT_ACTIVE;
		}

		$data['active'] = (bool) $recovery_event; // for "mark/unmark as false positive"

		$failing_tests = $data['filter_failing_tests'] ? ' AND h.value='.DOWN : '';
		$tests = DBselect($sql =
			'SELECT h.value, h.clock'.
			' FROM history_uint h'.
			' WHERE h.itemid='.zbx_dbstr($data['availItemId']).
				' AND h.clock>='.$from.
				' AND h.clock<='.$till.
				$failing_tests.
			' ORDER BY h.clock asc'
		);

		while ($test = DBfetch($tests)) {
			$data['tests'][] = [
				'clock' => $test['clock'],
				'value' => $test['value'],
				'startEvent' => $data['main_event']['clock'] == $test['clock'],
				'endEvent' => $recovery_event && $recovery_event['clock'] == $test['clock']
			];
		}

		$rollweek_values = DBfetchArrayAssoc(DBselect(
			'SELECT h.value,h.clock'.
			' FROM history h'.
			' WHERE h.itemid='.zbx_dbstr($data['slvItemId']).
				' AND h.clock>='.$from.
				' AND h.clock<='.$till.
			' ORDER BY h.clock asc'
		), 'clock');

		foreach ($data['tests'] as &$test) {
			if (isset($rollweek_values[$test['clock']])) {
				$test['slv'] = sprintf('%.3f', $rollweek_values[$test['clock']]['value']);
			}
		}
	}

	protected function doAction() {
		$macros = API::UserMacro()->get([
			'output' => ['macro', 'value'],
			'filter' => ['macro' => RSM_ROLLWEEK_SECONDS],
			'globalmacro' => true
		]);
		$macros = array_column($macros, 'value', 'macro');

		if ($this->hasInput('rolling_week')) {
			$data = $this->getInputAll();
			unset($data['rolling_week']);
			$timeshift = ($macros[RSM_ROLLWEEK_SECONDS]%SEC_PER_DAY)
					? $macros[RSM_ROLLWEEK_SECONDS]
					: ($macros[RSM_ROLLWEEK_SECONDS]/SEC_PER_DAY).'d';
			$data['from'] = 'now-'.$timeshift;
			$data['to'] = 'now';
			$response = new CControllerResponseRedirect(UrlHelper::get($this->getAction(), $data));
			CProfile::update('web.rsm.incidents.filter.active', 2, PROFILE_TYPE_INT);
			$this->setResponse($response);

			return;
		}

		$this->access_deny = false;
		$this->server_time = time();
		$data = [
			'title' => _('Incidents details'),
			'ajax_request' => $this->isAjaxRequest(),
			'refresh' => CWebUser::$data['refresh'] ? timeUnitToSeconds(CWebUser::$data['refresh']) : null,
			'module_style' => $this->module->getStyle(),
			'profileIdx' => 'web.rsm.incidents.filter',
			'profileIdx2' => 0,
			'active_tab' => CProfile::get('web.rsm.incidents.filter.active', 1),
			'filter_failing_tests' => 0,
			'rsm_monitoring_mode' => get_rsm_monitoring_type(),
			'tests' => [],
		];
		$this->getInputs($data, ['from', 'to']);

		$this->updateProfile($data);
		$this->getTLD($data);
		$this->getSLV($data);
		$this->getMainEvent($data);
		$this->getRSM($data);
		$this->getData($data);

		$data['paging'] = CPagerHelper::paginate($this->getInput('page', 1), $data['tests'], ZBX_SORT_UP, new CUrl());

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
