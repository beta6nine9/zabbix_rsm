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
use CSpan;
use CProfile;
use CArrayHelper;
use CControllerResponseData;
use CControllerResponseFatal;

class ParticularTestsListAction extends Action {

	protected function checkInput() {
		$fields = [
			'host'		=> 'string',
			'type'		=> 'required|in '.implode(',', [RSM_DNS, RSM_DNSSEC, RSM_RDDS, RSM_EPP, RSM_RDAP]),
			'time'		=> 'int32',
			'slvItemId' => 'int32'
		];

		$ret = $this->validateInput($fields);

		if (!$ret) {
			$this->setResponse(new CControllerResponseFatal());
		}

		return $ret;
	}

	protected function checkPermissions() {
		// ensure we have access to Rsmhost, limit output to hostid to avoid selecting the whole thing
		return !empty(API::Host()->get(['output' => ['hostid'], 'filter' => ['host' => $this->getInput('host', null)]]));
	}

	protected function updateProfile(array &$data) {
		if ($data['host'] && $data['time'] && $data['slvItemId'] && $data['type'] !== null) {
			CProfile::update('web.rsm.particulartests.host', $data['host'], PROFILE_TYPE_STR);
			CProfile::update('web.rsm.particulartests.time', $data['time'], PROFILE_TYPE_ID);
			CProfile::update('web.rsm.particulartests.slvItemId', $data['slvItemId'], PROFILE_TYPE_ID);
			CProfile::update('web.rsm.particulartests.type', $data['type'], PROFILE_TYPE_INT);
		}
		elseif (!$data['host'] && !$data['time'] && !$data['slvItemId'] && $data['type'] === null) {
			$data['host'] = CProfile::get('web.rsm.particulartests.host');
			$data['time'] = CProfile::get('web.rsm.particulartests.time');
			$data['slvItemId'] = CProfile::get('web.rsm.particulartests.slvItemId');
			$data['type'] = CProfile::get('web.rsm.particulartests.type');
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

		$data['test_time_from'] = $test_time_from;
		$data['is_rdap_standalone'] = is_RDAP_standalone($test_time_from);

		$data['tld_rdds_enabled'] = false;
		$data['tld_rdap_enabled'] = false;

		if ($data['type'] == RSM_RDAP && !$data['is_rdap_standalone']) {
			error(_('RDAP wasn\'t a standalone service at requested time!'));
			return;
		}

		$data['totalProbes'] = 0;

		// Decide which items need to select.
		if ($data['type'] == RSM_DNS || $data['type'] == RSM_DNSSEC) {
			$calculated_item_key[] = CALCULATED_ITEM_DNS_DELAY;

			if ($data['type'] == RSM_DNS) {
				$data['downProbes'] = 0;
			}
			else {
				$data['totalTests'] = 0;
			}
		}
		elseif ($data['type'] == RSM_RDAP) {
			$calculated_item_key[] = CALCULATED_ITEM_RDAP_DELAY;
		}
		elseif ($data['type'] == RSM_RDDS) {
			$calculated_item_key[] = CALCULATED_ITEM_RDDS_DELAY;
		}
		else {
			$calculated_item_key[] = CALCULATED_ITEM_EPP_DELAY;
		}

		if ($data['type'] == RSM_DNS) {
			$calculated_item_key[] = CALCULATED_ITEM_DNS_UDP_RTT_HIGH;
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
			error(_s('No permissions to referred host "%1$s" or it does not exist!', RSM_HOST));
			return;
		}

		// Get macros old value.
		$macro_items = API::Item()->get([
			'output' => ['itemid', 'key_', 'value_type'],
			'hostids' => $rsm['hostid'],
			'filter' => [
				'key_' => $calculated_item_key
			]
		]);

		foreach ($macro_items as $macro_item) {
			$macro_item_value = API::History()->get([
				'output' => API_OUTPUT_EXTEND,
				'itemids' => $macro_item['itemid'],
				'time_from' => $test_time_from,
				'history' => $macro_item['value_type'],
				'limit' => 1
			]);

			$macro_item_value = reset($macro_item_value);

			if ($data['type'] == RSM_DNS) {
				if ($macro_item['key_'] == CALCULATED_ITEM_DNS_UDP_RTT_HIGH) {
					$udp_rtt = $macro_item_value['value'];
				}
				else {
					$macro_time = $macro_item_value['value'] - 1;
				}
			}
			else {
				$macro_time = $macro_item_value['value'] - 1;
			}
		}

		// Time calculation.
		$test_time_till = $test_time_from + $macro_time;
		$data['test_time_till'] = $test_time_till;

		// Get TLD.
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
			show_error_message(_('No permissions to referred TLD or it does not exist!'));
			return;
		}

		// Get TLD level macros.
		if ($data['type'] == RSM_RDAP || $data['type'] == RSM_RDDS) {
			$tld_templates = API::Template()->get([
				'output' => [],
				'filter' => [
					'host' => [sprintf(TEMPLATE_NAME_TLD_CONFIG, $data['tld']['host'])]
				],
				'preservekeys' => true
			]);

			$user_macros_filter = [RSM_TLD_RDDS43_ENABLED, RSM_TLD_RDDS80_ENABLED];
			if ($data['type'] == RSM_RDDS || is_RDAP_standalone($test_time_from)) {
				$user_macros_filter = array_merge($user_macros_filter, [RSM_RDAP_TLD_ENABLED]);
			}

			$template_macros = API::UserMacro()->get([
				'output' => ['macro', 'value'],
				'hostids' => array_keys($tld_templates),
				'filter' => [
					'macro' => $user_macros_filter
				]
			]);

			$data['tld']['macros'] = [];
			foreach ($template_macros as $template_macro) {
				$data['tld']['macros'][$template_macro['macro']] = $template_macro['value'];
			}
		}

		// Get SLV item.
		$slvItems = API::Item()->get([
			'output' => ['name'],
			'itemids' => $data['slvItemId']
		]);

		if ($slvItems) {
			$data['slvItem'] = reset($slvItems);
		}
		else {
			show_error_message(_('No permissions to referred SLV item or it does not exist!'));
			return;
		}

		// Get test result.
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

		// Get items.
		$avail_items = API::Item()->get([
			'output' => ['itemid', 'value_type'],
			'hostids' => $data['tld']['hostid'],
			'filter' => [
				'key_' => $key
			],
			'preservekeys' => true
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

			$test_result = reset($test_results);
			if ($test_result === false) {
				$test_result['value'] = null;
			}

			// Get mapped value for test result.
			if (in_array($data['type'], [RSM_DNS, RSM_DNSSEC, RSM_RDDS, RSM_RDAP])) {
				$test_result_label = ($test_result['value'] !== null)
					? getMappedValue($test_result['value'], RSM_SERVICE_AVAIL_VALUE_MAP)
					: false;

				if (!$test_result_label) {
					$test_result_label = _('No result');
					$test_result_color = ZBX_STYLE_GREY;
				}
				else {
					$test_result_color = ($test_result['value'] == PROBE_DOWN) ? ZBX_STYLE_RED : ZBX_STYLE_GREEN;
				}

				$data['test_result'] = (new CSpan($test_result_label))->addClass($test_result_color);
			}
			else {
				$data['test_result'] = $test_result['value'];
			}
		}
		else {
			error(_s('Item with key "%1$s" not exist on TLD!', $key));
			return;
		}

		// Get probes.
		$hosts = API::Host()->get([
			'output' => ['hostid', 'host'],
			'groupids' => PROBES_MON_GROUPID,
			'preservekeys' => true
		]);

		$hostids = [];
		$tlds_probes = [];
		foreach ($hosts as $host) {
			$pos = strrpos($host['host'], ' - mon');
			if ($pos === false) {
				error(_s('Unexpected host name "%1$s" among probe hosts.', $host['host']));
				continue;
			}
			$data['probes'][$host['hostid']] = [
				'host' => substr($host['host'], 0, $pos),
				'name' => substr($host['host'], 0, $pos)
			];

			$tlds_probes[] = $data['tld']['host'].' '.$data['probes'][$host['hostid']]['host'];
			$hostids[] = $host['hostid'];
		}

		$data['totalProbes'] = count($hostids);

		if ($tlds_probes) {
			$tlds_probes = API::Host()->get([
				'output' => [],
				'filter' => [
					'host' => $tlds_probes
				],
				'monitored_hosts' => true,
				'preservekeys' => true
			]);

			$_enabled_itemid = $tlds_probes ? API::Item()->get([
				'output' => ['itemid', 'key_'],
				'hostids' => $data['tld']['hostid'],
				'filter' => [
					'key_' => [RDAP_ENABLED, RDDS43_ENABLED, RDDS80_ENABLED]
				]
			]) : null;

			if ($_enabled_itemid) {
				$_enabled_item_map = [
					RDAP_ENABLED => null,
					RDDS43_ENABLED => null,
					RDDS80_ENABLED => null
				];

				foreach ($_enabled_itemid as $_enabled_itemid) {
					// Only first item should be checked.
					if ($_enabled_item_map[$_enabled_itemid['key_']] === null) {
						$_enabled_item_map[$_enabled_itemid['key_']] = $_enabled_itemid['itemid'];
					}
				}

				// No need to show RDAP integrated in RDDS if it is enabled as standalone service.
				if (is_RDAP_standalone($test_time_from)) {
					unset($_enabled_item_map[RDAP_ENABLED]);
				}
				// Since RDAP is separate service, no need to process RDDS data anymore.
				if ($data['type'] == RSM_RDAP) {
					unset($_enabled_item_map[RDDS43_ENABLED]);
					unset($_enabled_item_map[RDDS80_ENABLED]);
				}

				foreach ($_enabled_item_map as $_enabled_item => $_enabled_itemid) {
					if ($_enabled_itemid !== null) {
						$history_value = API::History()->get([
							'output' => API_OUTPUT_EXTEND,
							'itemids' => $_enabled_itemid,
							'time_from' => $test_time_from,
							'time_till' => $test_time_till
						]);

						/**
						 * This is workaround to find that at least one value in given period had value set to 1.
						 *
						 * This is needed because originally system was designed to store historical value each minute and
						 * also test cycles matched to one per minute.
						 *
						 * Later, at some point it was changed and now, difference between $test_time_from and $test_time_till
						 * can be longer, although tests are still run each minute.
						 *
						 * Workaround looks at all historical values stored at period between $test_time_from and $test_time_till
						 * and assumes that service was enabled whole cycle if at least in one minute it was enabled.
						 *
						 * Example:
						 * Historically RDDS(43/80)_ENABLED is collected each minute. Historically test cycle was minute long.
						 * Now test cycle is 5 minutes long during which RDDS(43/80)_ENABLED can be both enabled and disabled. So,
						 * with this workaround we consider it as enabled if at least one minute it was enabled or disabled
						 * if all 5 minutes it was disabled.
						 */
						if ($history_value) {
							$history_value[0]['value'] = (array_sum(zbx_objectValues($history_value, 'value')) > 0) ? 1 : 0;
							$history_value = $history_value[0];

							switch ($history_value['itemid']) {
								case $_enabled_item_map[RDDS43_ENABLED]:
									$data['tld']['macros'][RSM_TLD_RDDS43_ENABLED] = $history_value['value'];
									break;

								case $_enabled_item_map[RDDS80_ENABLED]:
									$data['tld']['macros'][RSM_TLD_RDDS80_ENABLED] = $history_value['value'];
									break;

								case $_enabled_item_map[RDAP_ENABLED]:
									$data['tld']['macros'][RSM_RDAP_TLD_ENABLED] = $history_value['value'];
									break;
							}
						}
					}
				}
			}
		}

		// Get probe status.
		$probe_items = API::Item()->get([
			'output' => ['itemid', 'key_', 'hostid'],
			'hostids' => $hostids,
			'filter' => [
				'key_' => PROBE_KEY_ONLINE
			],
			'monitored' => true,
			'preservekeys' => true
		]);

		foreach ($probe_items as $probe_item) {
			$itemValues = DBfetchArray(DBselect(
				'SELECT h.value,h.clock'.
				' FROM history_uint h'.
				' WHERE h.itemid='.$probe_item['itemid'].
					' AND h.clock between '.$test_time_from.' AND '.$test_time_till
			));

			$mappedValues = array();

			foreach ($itemValues as $value) {
				$mappedValues[$value['clock']] = $value['value'];
			}

			for ($clock = $test_time_from; $clock < $test_time_till; $clock += RSM_PROBE_DELAY) {
				if (!array_key_exists($clock, $mappedValues) || $mappedValues[$clock] == PROBE_DOWN) {
					$data['probes'][$probe_item['hostid']]['status'] = PROBE_DOWN;
					break;
				}
			}
		}

		$host_names = [];

		// get probes data hosts
		foreach ($data['probes'] as $hostId => $probe) {
			if (!isset($probe['status'])) {
				$host_names[] = $data['tld']['host'].' '.$probe['host'];
			}
		}

		$hosts = empty($host_names) ? [] : API::Host()->get([
			'output' => ['hostid', 'host', 'name'],
			'selectParentTemplates' => ['templateid'],
			'filter' => [
				'host' => $host_names
			],
			'preservekeys' => true
		]);

		// Get hostids; Find probe level macros.
		$hostids = [];
		$hosted_templates = [];
		$hosts_templates = [];
		foreach ($hosts as &$host) {
			$hostids[$host['hostid']] = $host['hostid'];

			$host['macros'] = [];
			foreach ($host['parentTemplates'] as $parent_template) {
				$hosts_templates[$host['hostid']][$parent_template['templateid']] = true;
				$hosted_templates[$parent_template['templateid']] = true;
			}
			unset($host['parentTemplates']);
		}
		unset($host);

		$probe_macros = API::UserMacro()->get(array(
			'output' => ['hostid', 'macro', 'value'],
			'hostids' => array_merge(array_keys($hosted_templates), $hostids),
			'filter' => array(
				'macro' => RSM_RDDS_ENABLED
			)
		));

		foreach ($probe_macros as $probe_macro) {
			$hostid = null;

			if (array_key_exists($probe_macro['hostid'], $hosts_templates)) {
				$hostid = $probe_macro['hostid'];
			}
			else {
				foreach ($hosts_templates as $host => $templates) {
					if (array_key_exists($probe_macro['hostid'], $templates)) {
						$hostid = $host;
					}
				}
			}

			if ($hostid && array_key_exists($hostid, $hosts)) {
				$hosts[$hostid]['macros'][$probe_macro['macro']] = $probe_macro['value'];

				// No need to select items for disabled probes.
				if ($probe_macro['macro'] === RSM_RDDS_ENABLED && $probe_macro['value'] == 0) {
					unset($hostids[$hostid]);
				}
			}
		}
		unset($hosts_templates, $hosted_templates);

		// get only used items
		if ($data['type'] == RSM_DNS || $data['type'] == RSM_DNSSEC) {
			$probe_item_key = ' AND (i.key_ LIKE ('.zbx_dbstr(PROBE_DNS_UDP_ITEM_RTT.'%').') OR i.key_='.zbx_dbstr(PROBE_DNS_UDP_ITEM).')';
		}
		elseif ($data['type'] == RSM_RDAP) {
			$items_to_check = [];
			$probe_item_key = [];

			if (!isset($data['tld']['macros'][RSM_RDAP_TLD_ENABLED]) || $data['tld']['macros'][RSM_RDAP_TLD_ENABLED] != 0) {
				$data['tld_rdap_enabled'] = true;

				$items_to_check[] = PROBE_RDAP_IP;
				$items_to_check[] = PROBE_RDAP_RTT;
				$items_to_check[] = PROBE_RDAP_TARGET;
				$items_to_check[] = PROBE_RDAP_TESTEDNAME;
				$items_to_check[] = PROBE_RDAP_STATUS;
			}

			if ($items_to_check) {
				$probe_item_key[] = dbConditionString('i.key_', $items_to_check);
			}
			$probe_item_key = $probe_item_key ? ' AND ('.implode(' OR ', $probe_item_key).')' : '';
		}
		elseif ($data['type'] == RSM_RDDS) {
			$items_to_check = [];
			$probe_item_key = [];

			// RDAP should be under type=RSM_RDDS only if it is not enabled as standalone service.
			if ((!isset($data['tld']['macros'][RSM_RDAP_TLD_ENABLED]) || $data['tld']['macros'][RSM_RDAP_TLD_ENABLED] != 0)
					&& !is_RDAP_standalone($test_time_from)) {
				$data['tld_rdds_enabled'] = true;

				$items_to_check[] = PROBE_RDAP_IP;
				$items_to_check[] = PROBE_RDAP_RTT;
				$items_to_check[] = PROBE_RDAP_TARGET;
				$items_to_check[] = PROBE_RDAP_TESTEDNAME;
				$items_to_check[] = PROBE_RDAP_STATUS;
			}

			if (!isset($data['tld']['macros'][RSM_TLD_RDDS43_ENABLED]) || $data['tld']['macros'][RSM_TLD_RDDS43_ENABLED] != 0) {
				$data['tld_rdds_enabled'] = true;

				$items_to_check[] = PROBE_RDDS43_IP;
				$items_to_check[] = PROBE_RDDS43_RTT;
				$items_to_check[] = PROBE_RDDS43_TARGET;
				$items_to_check[] = PROBE_RDDS43_TESTEDNAME;
				$items_to_check[] = PROBE_RDDS43_STATUS;
				$items_to_check[] = PROBE_RDDS_STATUS;
			}

			if (!isset($data['tld']['macros'][RSM_TLD_RDDS80_ENABLED]) || $data['tld']['macros'][RSM_TLD_RDDS80_ENABLED] != 0) {
				$data['tld_rdds_enabled'] = true;

				$items_to_check[] = PROBE_RDDS80_IP;
				$items_to_check[] = PROBE_RDDS80_RTT;
				$items_to_check[] = PROBE_RDDS80_TARGET;
				$items_to_check[] = PROBE_RDDS80_STATUS;
				$items_to_check[] = PROBE_RDDS_STATUS;
			}

			if ($items_to_check) {
				$probe_item_key[] = dbConditionString('i.key_', $items_to_check);
			}
			$probe_item_key = $probe_item_key ? ' AND ('.implode(' OR ', $probe_item_key).')' : '';
		}
		else {
			$probe_item_key = ' AND (i.key_ LIKE ('.zbx_dbstr(PROBE_EPP_RESULT.'%').')'.
			' OR '.dbConditionString('i.key_', [PROBE_EPP_IP, PROBE_EPP_UPDATE, PROBE_EPP_INFO, PROBE_EPP_LOGIN]).')';
		}

		if ($test_result['value'] != UP_INCONCLUSIVE_RECONFIG) {
			// Get items.
			$items = ($probe_item_key !== '') ? DBselect(
				'SELECT i.itemid,i.key_,i.hostid,i.value_type,i.valuemapid,i.units'.
				' FROM items i'.
				' WHERE '.dbConditionInt('i.hostid', $hostids).
					' AND i.status='.ITEM_STATUS_ACTIVE.
					$probe_item_key
			) : null;

			$nsArray = [];

			// get items value
			if ($items) {
				while ($item = DBfetch($items)) {
					$itemValue = API::History()->get([
						'itemids' => $item['itemid'],
						'time_from' => $test_time_from,
						'time_till' => $test_time_till,
						'history' => $item['value_type'],
						'output' => API_OUTPUT_EXTEND
					]);

					$itemValue = reset($itemValue);

					if ($data['type'] == RSM_DNS && $item['key_'] === PROBE_DNS_UDP_ITEM) {
						$hosts[$item['hostid']]['result'] = $itemValue ? $itemValue['value'] : null;
					}
					elseif ($data['type'] == RSM_DNS && mb_substr($item['key_'], 0, 16) == PROBE_DNS_UDP_ITEM_RTT) {
						preg_match('/^[^\[]+\[([^\]]+)]$/', $item['key_'], $matches);
						$nsValues = explode(',', $matches[1]);

						if (!$itemValue) {
							$nsArray[$item['hostid']][$nsValues[1]]['value'][] = NS_NO_RESULT;
						}
						elseif ($itemValue['value'] < $udp_rtt && !isServiceErrorCode($itemValue['value'], $data['type'])) {
							$nsArray[$item['hostid']][$nsValues[1]]['value'][] = NS_UP;
						}
						else {
							$nsArray[$item['hostid']][$nsValues[1]]['value'][] = NS_DOWN;
						}
					}
					elseif ($data['type'] == RSM_DNSSEC && mb_substr($item['key_'], 0, 16) == PROBE_DNS_UDP_ITEM_RTT) {
						if (!isset($hosts[$item['hostid']]['value'])) {
							$hosts[$item['hostid']]['value']['ok'] = 0;
							$hosts[$item['hostid']]['value']['fail'] = 0;
							$hosts[$item['hostid']]['value']['total'] = 0;
							$hosts[$item['hostid']]['value']['noResult'] = 0;
						}

						if ($itemValue) {
							if (isServiceErrorCode($itemValue['value'], $data['type'])) {
								$hosts[$item['hostid']]['value']['fail']++;
							}
							else {
								$hosts[$item['hostid']]['value']['ok']++;
							}
						}
						else {
							$hosts[$item['hostid']]['value']['noResult']++;
						}

						$hosts[$item['hostid']]['value']['total']++;
					}
					elseif ($data['type'] == RSM_RDDS || $data['type'] == RSM_RDAP) {
						if ($item['key_'] == PROBE_RDDS43_IP) {
							$hosts[$item['hostid']]['rdds43']['ip'] = $itemValue ? $itemValue['value'] : null;
						}
						elseif ($item['key_'] == PROBE_RDDS43_RTT) {
							if (isset($itemValue['value'])) {
								//$rtt_value = convert_units(['value' => $itemValue['value'], 'units' => $item['units']]);
								$rtt_value = convertUnits(['value' => $itemValue['value']]);
								$hosts[$item['hostid']]['rdds43']['rtt'] = [
									'description' => $rtt_value < 0 ? applyValueMap($rtt_value, $item['valuemapid']) : null,
									'value' => $rtt_value
								];
							}
						}
						elseif ($item['key_'] == PROBE_RDDS80_IP) {
							$hosts[$item['hostid']]['rdds80']['ip'] = $itemValue ? $itemValue['value'] : null;
						}
						elseif ($item['key_'] == PROBE_RDDS80_RTT) {
							if (isset($itemValue['value'])) {
								//$rtt_value = convert_units(['value' => $itemValue['value'], 'units' => $item['units']]);
								$rtt_value = convertUnits(['value' => $itemValue['value']]);
								$hosts[$item['hostid']]['rdds80']['rtt'] = [
									'description' => $rtt_value < 0 ? applyValueMap($rtt_value, $item['valuemapid']) : null,
									'value' => $rtt_value
								];
							}
						}
						elseif ($item['key_'] == PROBE_RDAP_IP) {
							$hosts[$item['hostid']]['rdap']['ip'] = $itemValue ? $itemValue['value'] : null;
						}
						elseif ($item['key_'] == PROBE_RDAP_RTT) {
							if (isset($itemValue['value'])) {
								//$rtt_value = convert_units(['value' => $itemValue['value'], 'units' => $item['units']]);
								$rtt_value = convertUnits(['value' => $itemValue['value']]);
								$hosts[$item['hostid']]['rdap']['rtt'] = [
									'description' => $rtt_value < 0 ? applyValueMap($rtt_value, $item['valuemapid']) : null,
									'value' => $rtt_value
								];
							}
						}
						elseif ($item['key_'] == PROBE_RDDS43_TESTEDNAME) {
							$hosts[$item['hostid']]['rdds43']['testedname'] = $itemValue['value'];
						}
						elseif ($item['key_'] == PROBE_RDDS43_TARGET) {
							$hosts[$item['hostid']]['rdds43']['target'] = $itemValue['value'];
						}
						elseif ($item['key_'] == PROBE_RDDS43_STATUS) {
							$hosts[$item['hostid']]['rdds43']['status'] = $itemValue['value'];
						}
						elseif ($item['key_'] == PROBE_RDDS80_TARGET) {
							$hosts[$item['hostid']]['rdds80']['target'] = $itemValue['value'];
						}
						elseif ($item['key_'] == PROBE_RDDS80_STATUS) {
							$hosts[$item['hostid']]['rdds80']['status'] = $itemValue['value'];
						}
						elseif ($item['key_'] == PROBE_RDAP_TESTEDNAME) {
							$hosts[$item['hostid']]['rdap']['testedname'] = $itemValue['value'];
						}
						elseif ($item['key_'] == PROBE_RDAP_TARGET) {
							$hosts[$item['hostid']]['rdap']['target'] = $itemValue['value'];
						}
						elseif (substr($item['key_'], 0, strlen(PROBE_RDDS_STATUS)) === PROBE_RDDS_STATUS) {
							$hosts[$item['hostid']]['rdds']['status'] = $itemValue['value'];
						}
						elseif (substr($item['key_'], 0, strlen(PROBE_RDAP_STATUS)) === PROBE_RDAP_STATUS) {
							$hosts[$item['hostid']]['rdap']['status'] = $itemValue['value'];
						}

						// Count result for table bottom summary rows.
						if ($item['key_'] == PROBE_RDAP_RTT && 0 > $itemValue['value']) {
							$error_code = (int)$itemValue['value'];

							if (!array_key_exists($error_code, $data['errors'])) {
								$data['errors'][$error_code] = [
									'description' => applyValueMap($error_code, $item['valuemapid'])
								];
							}
							if (!array_key_exists('rdap', $data['errors'][$error_code])) {
								$data['errors'][$error_code]['rdap'] = 0;
							}

							$data['errors'][$error_code]['rdap']++;
						}
						elseif ($item['key_'] == PROBE_RDDS43_RTT || $item['key_'] == PROBE_RDDS80_RTT) {
							$column = $item['key_'] == PROBE_RDDS43_RTT ? 'rdds43' : 'rdds80';

							if (0 > $itemValue['value']) {
								$error_code = (int)$itemValue['value'];

								if (!array_key_exists($error_code, $data['errors'])) {
									$data['errors'][$error_code] = [
										'description' => applyValueMap($error_code, $item['valuemapid'])
									];
								}
								if (!array_key_exists($column, $data['errors'][$error_code])) {
									$data['errors'][$error_code][$column] = 0;
								}

								$data['errors'][$error_code][$column]++;
							}
						}
					}
					elseif ($data['type'] == RSM_EPP) {
						if ($item['key_'] == PROBE_EPP_IP) {
							$hosts[$item['hostid']]['ip'] = $itemValue['value'];
						}
						elseif ($item['key_'] == PROBE_EPP_UPDATE) {
							$hosts[$item['hostid']]['update'] = $itemValue['value']
								? applyValueMap(convertUnits(['value' => $itemValue['value'], 'units' => $item['units']]), $item['valuemapid'])
								: null;
						}
						elseif ($item['key_'] == PROBE_EPP_INFO) {
							$hosts[$item['hostid']]['info'] = $itemValue['value']
								? applyValueMap(convertUnits(['value' => $itemValue['value'], 'units' => $item['units']]), $item['valuemapid'])
								: null;
						}
						elseif ($item['key_'] == PROBE_EPP_LOGIN) {
							$hosts[$item['hostid']]['login'] = $itemValue['value']
								? applyValueMap(convertUnits(['value' => $itemValue['value'], 'units' => $item['units']]), $item['valuemapid'])
								: null;
						}
						else {
							$hosts[$item['hostid']]['value'] = $itemValue['value'];
						}
					}
				}
			}
		}

		// Sort errors.
		krsort($data['errors']);

		if ($data['type'] == RSM_DNS) {
			foreach ($nsArray as $hostId => $nss) {
				$hosts[$hostId]['value']['fail'] = 0;

				foreach ($nss as $nsName => $nsValue) {
					if (in_array(NS_DOWN, $nsValue['value'])) {
						$hosts[$hostId]['value']['fail']++;
					}
				}

				// Calculate Down probes.
				if (count($nss) - $hosts[$hostId]['value']['fail'] < $min_dns_count) {	// TODO: remove 3 months after deployment
					$data['downProbes']++;
					$hosts[$hostId]['class'] = ZBX_STYLE_RED;
				}
				else {
					$hosts[$hostId]['class'] = ZBX_STYLE_GREEN;
				}
			}
		}

		foreach ($hosts as $host) {
			foreach ($data['probes'] as $hostId => $probe) {
				if (mb_strtoupper($host['host']) == mb_strtoupper($data['tld']['host'].' '.$probe['host'])) {
					$data['probes'][$hostId] = $host;
					$data['probes'][$hostId]['name'] = $probe['host'];
					break;
				}
			}
		}

		CArrayHelper::sort($data['probes'], ['name']);
	}

	protected function doAction() {
		$data = [
			'title'	=> _('Test details'),
			'module_style' => $this->module->getStyle(),
			'host' => $this->getInput('host', null),
			'time' => $this->getInput('time', null),
			'slvItemId' => $this->getInput('slvItemId', null),
			'type' => $this->getInput('type', null),
			'probes' => [],
			'errors' => [],
			'rsm_monitoring_mode' => get_rsm_monitoring_type()
		];

		$this->updateProfile($data);

		if ($data['host'] && $data['time'] && $data['slvItemId'] && $data['type'] !== null) {
			$this->getReportData($data);
			$data += $this->getMacroHistoryValue([CALCULATED_ITEM_RDDS_RTT_HIGH, CALCULATED_ITEM_RDAP_RTT_HIGH], $data['test_time_till']);

			// Get value maps for error messages.
			if ($data['type'] == RSM_RDDS) {
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
			}

			$response = new CControllerResponseData($data);
			$response->setTitle($data['title']);
			$this->setResponse($response);
		}
		else {
			$response = new CControllerResponseFatal();
			$this->setResponse($response);
		}
	}
}
