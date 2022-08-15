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
use CProfile;
use CArrayHelper;
use CWebUser;
use CControllerResponseFatal;
use CControllerResponseData;
use CControllerResponseRedirect;
use Modules\RSM\Helpers\UrlHelper;
use CSettingsHelper;

class IncidentsListAction extends Action {

	protected function checkInput() {
		$fields = [
			'host'			=> 'db hosts.host',
			'eventid'		=> 'db events.eventid',
			'type'			=> 'in '.implode(',', [RSM_DNS, RSM_DNSSEC, RSM_RDDS, RSM_RDAP, RSM_EPP]),
			'filter_set'	=> 'in 1',
			'filter_rst'	=> 'in 1',
			'filter_search' => 'db hosts.host',
			'rolling_week'  => 'in 1',
			'from'			=> 'string',
			'to'			=> 'string',
		];

		$ret = $this->validateInput($fields);

		if (!$ret) {
			$this->setResponse(new CControllerResponseFatal());
		}

		return $ret;
	}

	protected function readValues(array &$data) {
		if ($this->hasInput('filter_set')) {
			$data['filter_search'] = $this->getInput('filter_search', '');
			CProfile::update('web.rsm.incidents.filter.search', $data['filter_search'], PROFILE_TYPE_STR);
		}
		elseif ($this->hasInput('filter_rst')) {
			$data += [
				'filter_search' => '',
				'from' => 'now-'.CSettingsHelper::get(CSettingsHelper::PERIOD_DEFAULT),
				'to' => 'now',
			];

			CProfile::delete('web.rsm.incidents.filter.search');
			CProfile::delete('web.rsm.incidents.filter.from');
			CProfile::delete('web.rsm.incidents.filter.to');
			CProfile::delete('web.rsm.incidents.filter.active');
		}
		else {
			$data += [
				'filter_search' => CProfile::get('web.rsm.incidents.filter.search', ''),
			];
		}

		$data = getTimeSelectorPeriod($data);
	}

	protected function fetchItems(array &$data) {
		$item_keys = ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRAR)
			? [RSM_SLV_RDDS_ROLLWEEK]
			: [RSM_SLV_DNSSEC_ROLLWEEK, RSM_SLV_RDDS_ROLLWEEK, RSM_SLV_EPP_ROLLWEEK];

		$avail_item_keys = ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRAR)
			? [RSM_SLV_RDDS_AVAIL]
			: [RSM_SLV_DNSSEC_AVAIL, RSM_SLV_RDDS_AVAIL, RSM_SLV_EPP_AVAIL];

		if (isRdapStandalone($this->filter_time_from) || isRdapStandalone($this->filter_time_till)) {
			$avail_item_keys[] = RSM_SLV_RDAP_AVAIL;
			$item_keys[] = RSM_SLV_RDAP_ROLLWEEK;
		}

		if ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRY) {
			$item_keys[] = RSM_SLV_DNS_ROLLWEEK;
			$avail_item_keys[] = RSM_SLV_DNS_AVAIL;
		}

		$items = [];
		$db_items = DBselect(
			'SELECT i.itemid, i.hostid, i.key_'.
			' FROM items i'.
			' WHERE '.dbConditionString('i.key_', $item_keys).
				' AND i.hostid = '.zbx_dbstr($data['tld']['hostid'])
		);

		$itemids = [];
		while ($item = DBfetch($db_items)) {
			$items[$item['itemid']] = [
				'itemid' => $item['itemid'],
				'hostid' => $item['hostid'],
				'key_' => $item['key_'],
				'lastvalue' => null,
				'lastclock' => null
			];

			$itemids[] = $item['itemid'];
		}

		if ($itemids) {
			$db_lastvalues = DBselect(
				'SELECT itemid, value, clock'.
				' FROM lastvalue'.
				' WHERE '.dbConditionString('itemid', $itemids)
			);

			while ($lastvalue = DBfetch($db_lastvalues)) {
				$items[$lastvalue['itemid']]['lastvalue'] = $lastvalue['value'];
				$items[$lastvalue['itemid']]['lastclock'] = $lastvalue['clock'];
			}
		}

		$items += API::Item()->get([
			'output' => ['itemid', 'hostid', 'key_'],
			'hostids' => [$data['tld']['hostid']],
			'filter' => [
				'key_' => $avail_item_keys
			],
			'preservekeys' => true
		]);

		return $items;
	}

	protected function fetchData(&$data) {
		$tld = API::Host()->get([
			'output' => ['hostid', 'host', 'info_1', 'info_2'],
			'filter' => $data['host']
				? ['host' => $data['host']]
				: ['host' => $data['filter_search']],
			'tlds' => true
		]);

		$data['tld'] = reset($tld);

		if (!$data['tld']) {
			return false;
		}

		// Get items.
		$items = $this->fetchItems($data);

		if ($items) {
			$avail_itemid['dns'] = null;
			$avail_itemid['dnssec'] = null;
			$avail_itemid['rdds'] = null;
			$avail_itemid['rdap'] = null;
			$avail_itemid['epp'] = null;

			$itemids = [];

			# Rolling Week items
			foreach ($items as $item) {
				switch ($item['key_']) {
					case RSM_SLV_DNS_ROLLWEEK:
						$service = 'dns';
						break;
					case RSM_SLV_DNSSEC_ROLLWEEK:
						$service = 'dnssec';
						break;
					case RSM_SLV_RDDS_ROLLWEEK:
						$service = 'rdds';
						break;
					case RSM_SLV_RDAP_ROLLWEEK:
						$service = 'rdap';
						break;
					case RSM_SLV_EPP_ROLLWEEK:
						$service = 'epp';
						break;
					default:
						$service = null;
				}

				if ($service !== null) {
					$data['services'][$service]['itemid'] = $item['itemid'];
					$data['services'][$service]['slv'] = sprintf('%.3f', $item['lastvalue']);
					$data['services'][$service]['slvTestTime'] = $item['lastclock'];
					$data['services'][$service]['events'] = [];
				}
			}

			# Service Availability items
			foreach ($items as $item) {
				switch ($item['key_']) {
					case RSM_SLV_DNS_AVAIL:
						$service = 'dns';
						break;
					case RSM_SLV_DNSSEC_AVAIL:
						$service = 'dnssec';
						break;
					case RSM_SLV_RDDS_AVAIL:
						$service = 'rdds';
						break;
					case RSM_SLV_RDAP_AVAIL:
						$service = 'rdap';
						break;
					case RSM_SLV_EPP_AVAIL:
						$service = 'epp';
						break;
					default:
						$service = null;
				}

				if ($service !== null) {
					$data['services'][$service]['availItemId'] = $item['itemid'];
					$avail_itemid[$service] = $item['itemid'];
					$itemids[] = $item['itemid'];
				}
			}

			// Get triggers.
			$triggers = API::Trigger()->get([
				'output' => ['triggerids'],
				'selectItems' => ['itemid'],
				'itemids' => $itemids,
				'filter' => [
					'priority' => TRIGGER_SEVERITY_NOT_CLASSIFIED
				],
				'preservekeys' => true
			]);

			$triggerids = array_keys($triggers);
			$dns_triggers = [];
			$dnssec_triggers = [];
			$rdds_triggers = [];
			$rdap_triggers = [];
			$epp_triggers = [];

			foreach ($triggers as $trigger) {
				$trigger_item = reset($trigger['items']);

				if ($trigger_item['itemid'] == $avail_itemid['dns']) {
					$dns_triggers[] = $trigger['triggerid'];
				}
				elseif ($trigger_item['itemid'] == $avail_itemid['dnssec']) {
					$dnssec_triggers[] = $trigger['triggerid'];
				}
				elseif ($trigger_item['itemid'] == $avail_itemid['rdds']) {
					$rdds_triggers[] = $trigger['triggerid'];
				}
				elseif ($trigger_item['itemid'] == $avail_itemid['rdap']) {
					$rdap_triggers[] = $trigger['triggerid'];
				}
				elseif ($trigger_item['itemid'] == $avail_itemid['epp']) {
					$epp_triggers[] = $trigger['triggerid'];
				}
			}

			// Select events, where time from < filter from and value TRIGGER_VALUE_TRUE.
			$new_eventids = [];
			foreach ($triggerids as $triggerid) {
				$begin_event = DBfetch(DBselect(
					'SELECT e.eventid,e.value'.
					' FROM events e'.
					' WHERE e.objectid='.$triggerid.
						' AND e.clock<'.$this->filter_time_from.
						' AND e.object='.EVENT_OBJECT_TRIGGER.
						' AND source='.EVENT_SOURCE_TRIGGERS.
					' ORDER BY e.clock DESC',
					1
				));

				if ($begin_event && $begin_event['value'] == TRIGGER_VALUE_TRUE) {
					$new_eventids[] = $begin_event['eventid'];
				}
			}

			// Get events.
			$events = API::Event()->get(array(
				'output' => API_OUTPUT_EXTEND,
				'selectTriggers' => API_OUTPUT_EXTEND,
				'objectids' => $triggerids,
				'source' => EVENT_SOURCE_TRIGGERS,
				'object' => EVENT_OBJECT_TRIGGER,
				'time_from' => $this->filter_time_from,
				'time_till' => $this->filter_time_till
			));

			if ($new_eventids) {
				$new_events = API::Event()->get(array(
					'output' => API_OUTPUT_EXTEND,
					'selectTriggers' => API_OUTPUT_EXTEND,
					'eventids' => $new_eventids
				));

				$events = array_merge($events, $new_events);
			}

			CArrayHelper::sort($events, array('objectid', 'clock'));

			$i = 0;
			$incidents = [];
			$last_event_value = [];

			// Data generation.
			foreach ($events as $event) {
				$event_triggerid = null;
				$get_history = false;

				// Ignore event duplicates.
				$currentValue = ($event['value'] == TRIGGER_VALUE_FALSE) ? TRIGGER_VALUE_FALSE : $event['value'];
				if (isset($last_event_value[$event['objectid']])
						&& $last_event_value[$event['objectid']] == $currentValue) {
					continue;
				}
				else {
					$last_event_value[$event['objectid']] = $currentValue;
				}

				if ($event['value'] == TRIGGER_VALUE_TRUE) {
					if (isset($incidents[$i]) && $incidents[$i]['status'] == TRIGGER_VALUE_TRUE) {
						// Get event end time.
						$add_event = DBfetch(DBselect(
							'SELECT e.clock,e.objectid,e.value'.
							' FROM events e'.
							' WHERE e.objectid='.$incidents[$i]['objectid'].
								' AND e.clock>='.$this->filter_time_till.
								' AND e.object='.EVENT_OBJECT_TRIGGER.
								' AND e.source='.EVENT_SOURCE_TRIGGERS.
								' AND e.value='.TRIGGER_VALUE_FALSE.
							' ORDER BY e.clock,e.ns',
							1
						));

						if ($add_event) {
							$newData[$i] = array(
								'status' => $add_event['value'],
								'endTime' => $add_event['clock']
							);

							$event_triggerid = $add_event['objectid'];

							if (in_array($event_triggerid, $dns_triggers)
									|| in_array($event_triggerid, $dnssec_triggers)
									|| in_array($event_triggerid, $rdds_triggers)
									|| in_array($event_triggerid, $rdap_triggers)
									|| in_array($event_triggerid, $epp_triggers)) {
								if (in_array($event_triggerid, $dns_triggers)) {
									$service = 'dns';
								}
								elseif (in_array($event_triggerid, $dnssec_triggers)) {
									$service = 'dnssec';
								}
								elseif (in_array($event_triggerid, $rdds_triggers)) {
									$service = 'rdds';
								}
								elseif (in_array($event_triggerid, $rdap_triggers)) {
									$service = 'rdap';
								}
								elseif (in_array($event_triggerid, $epp_triggers)) {
									$service = 'epp';
								}
								else {
									$service = null;
								}

								if ($service !== null) {
									unset($data['services'][$service]['events'][$i]['status']);
									$itemid = $avail_itemid[$service];
									$data['services'][$service]['events'][$i] = array_merge(
										$data['services'][$service]['events'][$i],
										$newData[$i]
									);
								}

								$data['services'][$service]['events'][$i]['incidentTotalTests'] = getTotalTestsCount(
									$itemid,
									$this->filter_time_from,
									$this->filter_time_till,
									$data['services'][$service]['events'][$i]['startTime'],
									$data['services'][$service]['events'][$i]['endTime']
								);

								$data['services'][$service]['events'][$i]['incidentFailedTests'] = getFailedTestsCount(
									$itemid,
									$this->filter_time_till,
									$data['services'][$service]['events'][$i]['startTime'],
									$data['services'][$service]['events'][$i]['endTime']
								);
							}
						}
						else {
							if (isset($data['services']['dns']['events'][$i])) {
								$service = 'dns';
							}
							elseif (isset($data['services']['dnssec']['events'][$i])) {
								$service = 'dnssec';
							}
							elseif (isset($data['services']['rdds']['events'][$i])) {
								$service = 'rdds';
							}
							elseif (isset($data['services']['rdap']['events'][$i])) {
								$service = 'rdap';
							}
							elseif (isset($data['services']['epp']['events'][$i])) {
								$service = 'epp';
							}
							else {
								$service = null;
							}

							if ($service !== null) {
								$data['services'][$service]['events'][$i]['incidentTotalTests'] = getTotalTestsCount(
									$avail_itemid[$service],
									$this->filter_time_from,
									$this->filter_time_till,
									$data['services'][$service]['events'][$i]['startTime']
								);

								$data['services'][$service]['events'][$i]['incidentFailedTests'] = getFailedTestsCount(
									$avail_itemid[$service],
									$this->filter_time_till,
									$data['services'][$service]['events'][$i]['startTime']
								);
							}
						}
					}

					$event_triggerid = $event['objectid'];

					$i++;
					$incidents[$i] = array(
						'eventid' => $event['eventid'],
						'objectid' => $event['objectid'],
						'status' => TRIGGER_VALUE_TRUE,
						'startTime' => $event['clock'],
						'false_positive' => getEventFalsePositiveness($event['eventid']),
					);
				}
				else {
					if (isset($incidents[$i])) {
						if ($incidents[$i]['objectid'] == $event['objectid']) {
							$event_triggerid = $incidents[$i]['objectid'];
							$incidents[$i]['status'] = $event['value'];
							$incidents[$i]['endTime'] = $event['clock'];
						}
					}
					else {
						$i++;
						// Get event start time.
						$add_event = API::Event()->get([
							'output' => API_OUTPUT_EXTEND,
							'objectids' => [$event['objectid']],
							'source' => EVENT_SOURCE_TRIGGERS,
							'object' => EVENT_OBJECT_TRIGGER,
							'time_till' => $event['clock'] - 1,
							'filter' => [
								'value' => TRIGGER_VALUE_TRUE
							],
							'limit' => 1,
							'sortorder' => ZBX_SORT_DOWN
						]);

						if ($add_event) {
							$add_event = reset($add_event);
							$event_triggerid = $event['objectid'];

							$info_itemid = '';
							if (in_array($event_triggerid, $dns_triggers)) {
								$info_itemid = $avail_itemid['dns'];
							}
							elseif (in_array($event_triggerid, $dnssec_triggers)) {
								$info_itemid = $avail_itemid['dnssec'];
							}
							elseif (in_array($event_triggerid, $rdds_triggers)) {
								$info_itemid = $avail_itemid['rdds'];
							}
							elseif (in_array($event_triggerid, $rdap_triggers)) {
								$info_itemid = $avail_itemid['rdap'];
							}
							elseif (in_array($event_triggerid, $epp_triggers)) {
								$info_itemid = $avail_itemid['epp'];
							}

							if ($info_itemid) {
								$incidents[$i] = [
									'objectid' => $event['objectid'],
									'eventid' => $add_event['eventid'],
									'status' => $event['value'],
									'startTime' => $add_event['clock'],
									'endTime' => $event['clock'],
									'false_positive' => $event['false_positive'],
									'incidentTotalTests' => getTotalTestsCount(
										$info_itemid,
										$this->filter_time_from,
										$this->filter_time_till,
										$add_event['clock'],
										$event['clock']
									),
									'incidentFailedTests' => getFailedTestsCount(
										$info_itemid,
										$this->filter_time_till,
										$add_event['clock'],
										$event['clock']
									)
								];
							}
						}
					}
				}

				if (in_array($event_triggerid, $dns_triggers)) {
					$service = 'dns';
				}
				elseif (in_array($event_triggerid, $dnssec_triggers)) {
					$service = 'dnssec';
				}
				elseif (in_array($event_triggerid, $rdds_triggers)) {
					$service = 'rdds';
				}
				elseif (in_array($event_triggerid, $rdap_triggers)) {
					$service = 'rdap';
				}
				elseif (in_array($event_triggerid, $epp_triggers)) {
					$service = 'epp';
				}
				else {
					$service = null;
				}

				if ($service !== null) {
					if (isset($data['services'][$service]['events'][$i])) {
						unset($data['services'][$service]['events'][$i]['status']);

						$itemid = $avail_itemid[$service];
						$get_history = true;

						$data['services'][$service]['events'][$i] = array_merge($data['services'][$service]['events'][$i], $incidents[$i]);
					}
					else {
						if (isset($incidents[$i])) {
							$data['services'][$service]['events'][$i] = $incidents[$i];
						}
					}
				}

				if ($get_history) {
					$data['services'][$service]['events'][$i]['incidentTotalTests'] = getTotalTestsCount(
						$itemid,
						$this->filter_time_from,
						$this->filter_time_till,
						$data['services'][$service]['events'][$i]['startTime'],
						$data['services'][$service]['events'][$i]['endTime']
					);

					$data['services'][$service]['events'][$i]['incidentFailedTests'] = getFailedTestsCount(
						$itemid,
						$this->filter_time_till,
						$data['services'][$service]['events'][$i]['startTime'],
						$data['services'][$service]['events'][$i]['endTime']
					);

					unset($get_history);
				}
			}

			if (isset($incidents[$i]) && $incidents[$i]['status'] == TRIGGER_VALUE_TRUE) {
				$objectid = $incidents[$i]['objectid'];
				// Get event end time.
				$event = DBfetch(DBselect(
					'SELECT e.clock'.
					' FROM events e'.
					' WHERE e.objectid='.$objectid.
						' AND e.clock>='.$this->filter_time_till.
						' AND e.object='.EVENT_OBJECT_TRIGGER.
						' AND e.source='.EVENT_SOURCE_TRIGGERS.
						' AND e.value='.TRIGGER_VALUE_FALSE.
					' ORDER BY e.clock,e.ns',
					1
				));

				if (in_array($objectid, $dns_triggers)) {
					$service = 'dns';
					$itemid = $avail_itemid['dns'];
				}
				elseif (in_array($objectid, $dnssec_triggers)) {
					$service = 'dnssec';
					$itemid = $avail_itemid['dnssec'];
				}
				elseif (in_array($objectid, $rdds_triggers)) {
					$service = 'rdds';
					$itemid = $avail_itemid['rdds'];
				}
				elseif (in_array($objectid, $rdap_triggers)) {
					$service = 'rdap';
					$itemid = $avail_itemid['rdap'];
				}
				elseif (in_array($objectid, $epp_triggers)) {
					$service = 'epp';
					$itemid = $avail_itemid['epp'];
				}

				if ($event) {
					$data['services'][$service]['events'][$i]['status'] = TRIGGER_VALUE_FALSE;
					$data['services'][$service]['events'][$i]['endTime'] = $event['clock'];
				}

				$data['services'][$service]['events'][$i]['incidentTotalTests'] = getTotalTestsCount(
					$itemid,
					$this->filter_time_from,
					$this->filter_time_till,
					$data['services'][$service]['events'][$i]['startTime'],
					$event ? $event['clock'] : null
				);
				$data['services'][$service]['events'][$i]['incidentFailedTests'] = getFailedTestsCount(
					$itemid,
					$this->filter_time_till,
					$data['services'][$service]['events'][$i]['startTime'],
					$event ? $event['clock'] : null
				);
			}

			$data['services']['dns']['totalTests'] = 0;
			$data['services']['dnssec']['totalTests'] = 0;
			$data['services']['rdds']['totalTests'] = 0;
			$data['services']['epp']['totalTests'] = 0;
			$data['services']['dns']['inIncident'] = 0;
			$data['services']['dnssec']['inIncident'] = 0;
			$data['services']['rdds']['inIncident'] = 0;
			$data['services']['epp']['inIncident'] = 0;

			if (isRdapStandalone($this->filter_time_from) || isRdapStandalone($this->filter_time_till)) {
				$data['services']['rdap']['totalTests'] = 0;
				$data['services']['rdap']['inIncident'] = 0;
			}

			$avail_items = [];
			if ($avail_itemid['dns']) {
				$avail_items[] = $avail_itemid['dns'];
			}
			if ($avail_itemid['dnssec']) {
				$avail_items[] = $avail_itemid['dnssec'];
			}
			if ($avail_itemid['rdds']) {
				$avail_items[] = $avail_itemid['rdds'];
			}
			if ($avail_itemid['rdap']) {
				$avail_items[] = $avail_itemid['rdap'];
			}
			if ($avail_itemid['epp']) {
				$avail_items[] = $avail_itemid['epp'];
			}

			$items_histories = DBselect(
				'SELECT h.clock, h.value, h.itemid'.
				' FROM history_uint h'.
				' WHERE '.dbConditionInt('h.itemid', $avail_items).
					' AND h.clock>='.$this->filter_time_from.
					' AND h.clock<='.$this->filter_time_till.
					' AND h.value=0'
			);

			while ($items_history = DBfetch($items_histories)) {
				if ($items_history['itemid'] == $avail_itemid['dns']) {
					$service = 'dns';
				}
				elseif ($items_history['itemid'] == $avail_itemid['dnssec']) {
					$service = 'dnssec';
				}
				elseif ($items_history['itemid'] == $avail_itemid['rdds']) {
					$service = 'rdds';
				}
				elseif ($items_history['itemid'] == $avail_itemid['rdap']) {
					$service = 'rdap';
				}
				elseif ($items_history['itemid'] == $avail_itemid['epp']) {
					$service = 'epp';
				}

				$data['services'][$service]['totalTests']++;

				foreach ($data['services'][$service]['events'] as $incident) {
					if ($items_history['clock'] >= $incident['startTime'] && (!isset($incident['endTime'])
							|| (isset($incident['endTime']) && $items_history['clock'] <= $incident['endTime']))) {
						$data['services'][$service]['inIncident']++;
					}
				}
			}

			// Get delay items.
			$item_keys = [];
			if (isset($data['services']['dns']['events']) || isset($data['services']['dnssec']['events'])) {
				array_push($item_keys, CALCULATED_ITEM_DNS_DELAY, CALCULATED_DNS_ROLLWEEK_SLA);
			}
			if (isset($data['services']['rdds']['events'])) {
				array_push($item_keys, CALCULATED_ITEM_RDDS_DELAY, CALCULATED_RDDS_ROLLWEEK_SLA);
			}
			if (isset($data['services']['rdap']['events'])) {
				array_push($item_keys, CALCULATED_ITEM_RDAP_DELAY, CALCULATED_RDAP_ROLLWEEK_SLA);
			}
			if (isset($data['services']['epp']['events'])) {
				array_push($item_keys, CALCULATED_ITEM_EPP_DELAY, CALCULATED_EPP_ROLLWEEK_SLA);
			}

			if ($item_keys) {
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
					return false;
				}

				$items = API::Item()->get([
					'output' => ['itemid', 'value_type', 'key_'],
					'hostids' => $rsm['hostid'],
					'filter' => [
						'key_' => $item_keys
					]
				]);

				if (count($item_keys) != count($items)) {
					error(_s('Missing service configuration items at host "%1$s".', RSM_HOST));
					return false;
				}

				// Get SLA items.
				foreach ($items as $item) {
					if ($item['key_'] === CALCULATED_DNS_ROLLWEEK_SLA
							|| $item['key_'] === CALCULATED_RDDS_ROLLWEEK_SLA
							|| $item['key_'] === CALCULATED_RDAP_ROLLWEEK_SLA
							|| $item['key_'] === CALCULATED_EPP_ROLLWEEK_SLA) {
						// Get last value.
						$item_value = API::History()->get([
							'output' => API_OUTPUT_EXTEND,
							'itemids' => $item['itemid'],
							'time_from' => $this->filter_time_from,
							'history' => $item['value_type'],
							'limit' => 1
						]);

						if ($item_value) {
							$item_value = reset($item_value);

							if ($item['key_'] === CALCULATED_DNS_ROLLWEEK_SLA) {
								$data['services']['dns']['slaValue'] = $item_value['value'] * 60;
								if (isset($data['services']['dnssec']['events'])) {
									$data['services']['dnssec']['slaValue'] = $item_value['value'] * 60;
								}
							}
							elseif ($item['key_'] === CALCULATED_RDDS_ROLLWEEK_SLA) {
								$data['services']['rdds']['slaValue'] = $item_value['value'] * 60;
							}
							elseif ($item['key_'] === CALCULATED_RDAP_ROLLWEEK_SLA) {
								$data['services']['rdap']['slaValue'] = $item_value['value'] * 60;
							}
							else {
								$data['services']['epp']['slaValue'] = $item_value['value'] * 60;
							}
						}
					}
					else {
						// Get last value.
						$item_value = API::History()->get([
							'output' => API_OUTPUT_EXTEND,
							'itemids' => $item['itemid'],
							'time_till' => $this->filter_time_till,
							'history' => $item['value_type'],
							'sortorder' => ZBX_SORT_DOWN,
							'sortfield' => ['clock'],
							'limit' => 1
						]);

						$item_value = reset($item_value);

						if ($item_value) {
							if ($item['key_'] == CALCULATED_ITEM_DNS_DELAY) {
								$data['services']['dns']['delay'] = $item_value['value'];

								if (isset($data['services']['dnssec']['events'])) {
									$data['services']['dnssec']['delay'] = $item_value['value'];
								}
							}
							elseif ($item['key_'] == CALCULATED_ITEM_RDDS_DELAY) {
								$data['services']['rdds']['delay'] = $item_value['value'];
							}
							elseif ($item['key_'] == CALCULATED_ITEM_RDAP_DELAY) {
								$data['services']['rdap']['delay'] = $item_value['value'];
							}
							elseif ($item['key_'] == CALCULATED_ITEM_EPP_DELAY) {
								$data['services']['epp']['delay'] = $item_value['value'];
							}
						}
					}
				}
			}
		}

		return true;
	}

	protected function sortResults(array &$data) {
		if (isset($data['services']['dns']['events'])) {
			$data['services']['dns']['events'] = array_reverse($data['services']['dns']['events']);
		}
		if (isset($data['services']['dnssec']['events'])) {
			$data['services']['dnssec']['events'] = array_reverse($data['services']['dnssec']['events']);
		}
		if (isset($data['services']['rdds']['events'])) {
			$data['services']['rdds']['events'] = array_reverse($data['services']['rdds']['events']);
		}
		if (isset($data['services']['rdap']['events'])) {
			$data['services']['rdap']['events'] = array_reverse($data['services']['rdap']['events']);
		}
		if (isset($data['services']['epp']['events'])) {
			$data['services']['epp']['events'] = array_reverse($data['services']['epp']['events']);
		}
	}

	protected function doAction() {
		global $DB;

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

		$server_now = time();

		$data = [
			'title'                    => _('Incidents'),
			'ajax_request'             => $this->isAjaxRequest(),
			'refresh'                  => CWebUser::$data['refresh'] ? timeUnitToSeconds(CWebUser::$data['refresh']) : null,
			'module_style'             => $this->module->getStyle(),
			'type'                     => $this->getInput('type', 0),
			'host'                     => $this->getInput('host', false),
			'tld'                      => null,
			'url'                      => '',
			'rdap_standalone_start_ts' => getRdapStandaloneTs(),
			'rsm_monitoring_mode'      => get_rsm_monitoring_type(),
			'profileIdx'               => 'web.rsm.incidents.filter',
			'profileIdx2'              => 0,
			'from'                     => $this->hasInput('from') ? $this->getInput('from') : null,
			'to'                       => $this->hasInput('to') ? $this->getInput('to') : null,
			'incident_from'            => date(DATE_TIME_FORMAT_SECONDS, $server_now - $macros[RSM_ROLLWEEK_SECONDS]),
			'incident_to'              => date(DATE_TIME_FORMAT_SECONDS, $server_now),
			'active_tab'               => CProfile::get('web.rsm.incidents.filter.active', 1),
			'incidents_tab'            => (isset($_COOKIE['incidents_tab']) ? (int) $_COOKIE['incidents_tab'] : 0),
		];

		if (!$this->isAjaxRequest() && $this->hasInput('type')) {
			$data['incidents_tab'] = serviceTabIndex($this->getInput('type'));
			setcookie('incidents_tab', $data['incidents_tab']);
		}

		$this->readValues($data);

		$this->filter_time_from = $data['from_ts'];
		$this->filter_time_till = $data['to_ts'];

		// Get data about incidents.
		if ($data['host'] || $data['filter_search']) {
			$master = $DB;

			foreach ($DB['SERVERS'] as $server) {
				if (!multiDBconnect($server, $error)) {
					error(_($server['NAME'].': '.$error));
					continue;
				}

				if (!$this->fetchData($data)) {
					continue;
				}

				// Update profile.
				if ($data['host'] && $data['filter_search'] != $data['tld']['host']) {
					$data['filter_search'] = $data['tld']['host'];
					CProfile::update('web.rsm.incidents.filter.search', $data['tld']['host'], PROFILE_TYPE_STR);
				}

				$data['url'] = $server['URL'];
				$data['server'] = $server['NAME'];
				break;
			}

			unset($DB['DB']);
			$DB = $master;
			DBconnect($error);
		}

		// Show error if no matching hosts found.
		if ($data['filter_search'] && !$data['tld']) {
			error(_s('Host "%s" doesn\'t exist or you don\'t have permissions to access it.', $data['filter_search']));
		}

		$this->sortResults($data);

		$response = new CControllerResponseData($data);
		$response->setTitle($data['title']);
		$this->setResponse($response);
	}
}
