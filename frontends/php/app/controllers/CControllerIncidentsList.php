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


require_once './include/incidents.inc.php';
require_once './include/incidentdetails.inc.php';


class CControllerIncidentsList extends CController {

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
			'host'			=>	'db hosts.host',
			'eventid'		=>	'db events.eventid',
			'type'			=>	'in '.implode(',', [RSM_DNS, RSM_DNSSEC, RSM_RDDS, RSM_RDAP, RSM_EPP]),
			'filter_set'	=>	'in 1',
			'filter_rst'	=>	'in 1',
			'filter_search' =>	'db hosts.host',
			'from'			=>	'string',
			'to'			=>	'string'
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
				'from' => ZBX_PERIOD_DEFAULT_FROM,
				'to' => ZBX_PERIOD_DEFAULT_TO
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

		if (is_RDAP_standalone($this->filter_time_from) || is_RDAP_standalone($this->filter_time_till)) {
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
			$dns_items = [];
			$dnssec_items = [];
			$rdds_items = [];
			$rdap_items = [];
			$epp_items = [];
			$dns_avail_item = [];
			$dnssec_avail_item = [];
			$rdds_avail_item = [];
			$rdap_avail_item = [];
			$epp_avail_item = [];
			$itemids = [];

			foreach ($items as $item) {
				switch ($item['key_']) {
					case RSM_SLV_DNS_ROLLWEEK:
						$data['dns']['itemid'] = $item['itemid'];
						$data['dns']['slv'] = sprintf('%.3f', $item['lastvalue']);
						$data['dns']['slvTestTime'] = $item['lastclock'];
						$data['dns']['events'] = [];
						break;

					case RSM_SLV_DNSSEC_ROLLWEEK:
						$data['dnssec']['itemid'] = $item['itemid'];
						$data['dnssec']['slv'] = sprintf('%.3f', $item['lastvalue']);
						$data['dnssec']['slvTestTime'] = $item['lastclock'];
						$data['dnssec']['events'] = [];
						break;

					case RSM_SLV_RDDS_ROLLWEEK:
						$data['rdds']['itemid'] = $item['itemid'];
						$data['rdds']['slv'] = sprintf('%.3f', $item['lastvalue']);
						$data['rdds']['slvTestTime'] = $item['lastclock'];
						$data['rdds']['events'] = [];
						break;

					case RSM_SLV_RDAP_ROLLWEEK:
						$data['rdap']['itemid'] = $item['itemid'];
						$data['rdap']['slv'] = sprintf('%.3f', $item['lastvalue']);
						$data['rdap']['slvTestTime'] = $item['lastclock'];
						$data['rdap']['events'] = [];
						break;

					case RSM_SLV_EPP_ROLLWEEK:
						$data['epp']['itemid'] = $item['itemid'];
						$data['epp']['slv'] = sprintf('%.3f', $item['lastvalue']);
						$data['epp']['slvTestTime'] = $item['lastclock'];
						$data['epp']['events'] = [];
						break;

					case RSM_SLV_DNS_AVAIL:
						$data['dns']['availItemId'] = $item['itemid'];
						$dns_avail_item = $item['itemid'];
						$dns_items[] = $item['itemid'];
						$itemids[] = $item['itemid'];
						break;

					case RSM_SLV_DNSSEC_AVAIL:
						$data['dnssec']['availItemId'] = $item['itemid'];
						$dnssec_avail_item = $item['itemid'];
						$dnssec_items[] = $item['itemid'];
						$itemids[] = $item['itemid'];
						break;

					case RSM_SLV_RDDS_AVAIL:
						$data['rdds']['availItemId'] = $item['itemid'];
						$rdds_avail_item = $item['itemid'];
						$rdds_items[] = $item['itemid'];
						$itemids[] = $item['itemid'];
						break;

					case RSM_SLV_RDAP_AVAIL:
						$data['rdap']['availItemId'] = $item['itemid'];
						$rdap_avail_item = $item['itemid'];
						$rdap_items[] = $item['itemid'];
						$itemids[] = $item['itemid'];
						break;

					case RSM_SLV_EPP_AVAIL:
						$data['epp']['availItemId'] = $item['itemid'];
						$epp_avail_item = $item['itemid'];
						$epp_items[] = $item['itemid'];
						$itemids[] = $item['itemid'];
						break;
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

				if (in_array($trigger_item['itemid'], $dns_items)) {
					$dns_triggers[] = $trigger['triggerid'];
				}
				elseif (in_array($trigger_item['itemid'], $dnssec_items)) {
					$dnssec_triggers[] = $trigger['triggerid'];
				}
				elseif (in_array($trigger_item['itemid'], $rdap_items)) {
					$rdap_triggers[] = $trigger['triggerid'];
				}
				elseif (in_array($trigger_item['itemid'], $rdds_items)) {
					$rdds_triggers[] = $trigger['triggerid'];
				}
				elseif (in_array($trigger_item['itemid'], $epp_items)) {
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
				$item_info = [];

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
									unset($data['dns']['events'][$i]['status']);
									$item_type = 'dns';
									$itemid = $dns_avail_item;
									$data['dns']['events'][$i] = array_merge($data['dns']['events'][$i], $newData[$i]);
								}
								elseif (in_array($event_triggerid, $dnssec_triggers)) {
									unset($data['dnssec']['events'][$i]['status']);
									$item_type = 'dnssec';
									$itemid = $dnssec_avail_item;
									$data['dnssec']['events'][$i] = array_merge(
										$data['dnssec']['events'][$i],
										$newData[$i]
									);
								}
								elseif (in_array($event_triggerid, $rdds_triggers)) {
									unset($data['rdds']['events'][$i]['status']);
									$item_type = 'rdds';
									$itemid = $rdds_avail_item;
									$data['rdds']['events'][$i] = array_merge($data['rdds']['events'][$i], $newData[$i]);
								}
								elseif (in_array($event_triggerid, $rdap_triggers)) {
									unset($data['rdap']['events'][$i]['status']);
									$item_type = 'rdap';
									$itemid = $rdap_avail_item;
									$data['rdap']['events'][$i] = array_merge($data['rdap']['events'][$i], $newData[$i]);
								}
								elseif (in_array($event_triggerid, $epp_triggers)) {
									unset($data['epp']['events'][$i]['status']);
									$item_type = 'epp';
									$itemid = $epp_avail_item;
									$data['epp']['events'][$i] = array_merge($data['epp']['events'][$i], $newData[$i]);
								}

								$data[$item_type]['events'][$i]['incidentTotalTests'] = getTotalTestsCount(
									$itemid,
									$this->filter_time_from,
									$this->filter_time_till,
									$data[$item_type]['events'][$i]['startTime'],
									$data[$item_type]['events'][$i]['endTime']
								);

								$data[$item_type]['events'][$i]['incidentFailedTests'] = getFailedTestsCount(
									$itemid,
									$this->filter_time_till,
									$data[$item_type]['events'][$i]['startTime'],
									$data[$item_type]['events'][$i]['endTime']
								);
							}
						}
						else {
							if (isset($data['dns']['events'][$i])) {
								$item_info = [
									'itemType' => 'dns',
									'itemId' => $dns_avail_item
								];
							}
							elseif (isset($data['dnssec']['events'][$i])) {
								$item_info = [
									'itemType' => 'dnssec',
									'itemId' => $dnssec_avail_item
								];
							}
							elseif (isset($data['rdds']['events'][$i])) {
								$item_info = [
									'itemType' => 'rdds',
									'itemId' => $rdds_avail_item
								];
							}
							elseif (isset($data['rdap']['events'][$i])) {
								$item_info = [
									'itemType' => 'rdap',
									'itemId' => $rdap_avail_item
								];
							}
							elseif (isset($data['epp']['events'][$i])) {
								$item_info = [
									'itemType' => 'epp',
									'itemId' => $epp_avail_item
								];
							}

							if ($item_info) {
								$data[$item_info['itemType']]['events'][$i]['incidentTotalTests'] = getTotalTestsCount(
									$item_info['itemId'],
									$this->filter_time_from,
									$this->filter_time_till,
									$data[$item_info['itemType']]['events'][$i]['startTime']
								);

								$data[$item_info['itemType']]['events'][$i]['incidentFailedTests'] = getFailedTestsCount(
									$item_info['itemId'],
									$this->filter_time_till,
									$data[$item_info['itemType']]['events'][$i]['startTime']
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
						'false_positive' => $event['false_positive']
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
								$info_itemid = $dns_avail_item;
							}
							elseif (in_array($event_triggerid, $dnssec_triggers)) {
								$info_itemid = $dnssec_avail_item;
							}
							elseif (in_array($event_triggerid, $rdds_triggers)) {
								$info_itemid = $rdds_avail_item;
							}
							elseif (in_array($event_triggerid, $rdap_triggers)) {
								$info_itemid = $rdap_avail_item;
							}
							elseif (in_array($event_triggerid, $epp_triggers)) {
								$info_itemid = $epp_avail_item;
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
					if (isset($data['dns']['events'][$i])) {
						unset($data['dns']['events'][$i]['status']);

						$item_type = 'dns';
						$itemid = $dns_avail_item;
						$get_history = true;

						$data['dns']['events'][$i] = array_merge($data['dns']['events'][$i], $incidents[$i]);
					}
					else {
						if (isset($incidents[$i])) {
							$data['dns']['events'][$i] = $incidents[$i];
						}
					}
				}
				elseif (in_array($event_triggerid, $dnssec_triggers)) {
					if (isset($data['dnssec']['events'][$i])) {
						unset($data['dnssec']['events'][$i]['status']);

						$item_type = 'dnssec';
						$itemid = $dnssec_avail_item;
						$get_history = true;

						$data['dnssec']['events'][$i] = array_merge($data['dnssec']['events'][$i], $incidents[$i]);
					}
					else {
						if (isset($incidents[$i])) {
							$data['dnssec']['events'][$i] = $incidents[$i];
						}
					}
				}
				elseif (in_array($event_triggerid, $rdds_triggers)) {
					if (isset($data['rdds']['events'][$i]) && $data['rdds']['events'][$i]) {
						unset($data['rdds']['events'][$i]['status']);

						$item_type = 'rdds';
						$itemid = $rdds_avail_item;
						$get_history = true;

						$data['rdds']['events'][$i] = array_merge($data['rdds']['events'][$i], $incidents[$i]);
					}
					else {
						if (isset($incidents[$i])) {
							$data['rdds']['events'][$i] = $incidents[$i];
						}
					}
				}
				elseif (in_array($event_triggerid, $rdap_triggers)) {
					if (isset($data['rdap']['events'][$i]) && $data['rdap']['events'][$i]) {
						unset($data['rdap']['events'][$i]['status']);

						$item_type = 'rdap';
						$itemid = $rdap_avail_item;
						$get_history = true;

						$data['rdap']['events'][$i] = array_merge($data['rdap']['events'][$i], $incidents[$i]);
					}
					else {
						if (isset($incidents[$i])) {
							$data['rdap']['events'][$i] = $incidents[$i];
						}
					}
				}
				elseif (in_array($event_triggerid, $epp_triggers)) {
					if (isset($data['epp']['events'][$i]) && $data['epp']['events'][$i]) {
						unset($data['epp']['events'][$i]['status']);

						$item_type = 'epp';
						$itemid = $epp_avail_item;
						$get_history = true;

						$data['epp']['events'][$i] = array_merge($data['epp']['events'][$i], $incidents[$i]);
					}
					else {
						if (isset($incidents[$i])) {
							$data['epp']['events'][$i] = $incidents[$i];
						}
					}
				}

				if ($get_history) {
					$data[$item_type]['events'][$i]['incidentTotalTests'] = getTotalTestsCount(
						$itemid,
						$this->filter_time_from,
						$this->filter_time_till,
						$data[$item_type]['events'][$i]['startTime'],
						$data[$item_type]['events'][$i]['endTime']
					);

					$data[$item_type]['events'][$i]['incidentFailedTests'] = getFailedTestsCount(
						$itemid,
						$this->filter_time_till,
						$data[$item_type]['events'][$i]['startTime'],
						$data[$item_type]['events'][$i]['endTime']
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
					$item_type = 'dns';
					$itemid = $dns_avail_item;
				}
				elseif (in_array($objectid, $dnssec_triggers)) {
					$item_type = 'dnssec';
					$itemid = $dnssec_avail_item;
				}
				elseif (in_array($objectid, $rdds_triggers)) {
					$item_type = 'rdds';
					$itemid = $rdds_avail_item;
				}
				elseif (in_array($objectid, $rdap_triggers)) {
					$item_type = 'rdap';
					$itemid = $rdap_avail_item;
				}
				elseif (in_array($objectid, $epp_triggers)) {
					$item_type = 'epp';
					$itemid = $epp_avail_item;
				}

				if ($event) {
					$data[$item_type]['events'][$i]['status'] = TRIGGER_VALUE_FALSE;
					$data[$item_type]['events'][$i]['endTime'] = $event['clock'];
				}

				$data[$item_type]['events'][$i]['incidentTotalTests'] = getTotalTestsCount(
					$itemid,
					$this->filter_time_from,
					$this->filter_time_till,
					$data[$item_type]['events'][$i]['startTime'],
					$event ? $event['clock'] : null
				);
				$data[$item_type]['events'][$i]['incidentFailedTests'] = getFailedTestsCount(
					$itemid,
					$this->filter_time_till,
					$data[$item_type]['events'][$i]['startTime'],
					$event ? $event['clock'] : null
				);
			}

			$data['dns']['totalTests'] = 0;
			$data['dnssec']['totalTests'] = 0;
			$data['rdds']['totalTests'] = 0;
			$data['rdap']['totalTests'] = 0;
			$data['epp']['totalTests'] = 0;
			$data['dns']['inIncident'] = 0;
			$data['dnssec']['inIncident'] = 0;
			$data['rdds']['inIncident'] = 0;
			$data['rdap']['inIncident'] = 0;
			$data['epp']['inIncident'] = 0;

			$avail_items = [];
			if ($dns_avail_item) {
				$avail_items[] = $dns_avail_item;
			}
			if ($dnssec_avail_item) {
				$avail_items[] = $dnssec_avail_item;
			}
			if ($rdds_avail_item) {
				$avail_items[] = $rdds_avail_item;
			}
			if ($rdap_avail_item) {
				$avail_items[] = $rdap_avail_item;
			}
			if ($epp_avail_item) {
				$avail_items[] = $epp_avail_item;
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
				if ($items_history['itemid'] == $dns_avail_item) {
					$type = 'dns';
				}
				elseif ($items_history['itemid'] == $dnssec_avail_item) {
					$type = 'dnssec';
				}
				elseif ($items_history['itemid'] == $rdds_avail_item) {
					$type = 'rdds';
				}
				elseif ($items_history['itemid'] == $rdap_avail_item) {
					$type = 'rdap';
				}
				elseif ($items_history['itemid'] == $epp_avail_item) {
					$type = 'epp';
				}

				$data[$type]['totalTests']++;

				foreach ($data[$type]['events'] as $incident) {
					if ($items_history['clock'] >= $incident['startTime'] && (!isset($incident['endTime'])
							|| (isset($incident['endTime']) && $items_history['clock'] <= $incident['endTime']))) {
						$data[$type]['inIncident']++;
					}
				}
			}

			// Input into rolling week calculation block.
			$services = [];

			// Get delay items.
			$item_keys = [];
			if (isset($data['dns']['events']) || isset($data['dnssec']['events'])) {
				array_push($item_keys, CALCULATED_ITEM_DNS_DELAY, CALCULATED_DNS_ROLLWEEK_SLA);
				if (isset($data['dns']['events'])) {
					$services['dns'] = [];
				}
				if (isset($data['dnssec']['events'])) {
					$services['dnssec'] = [];
				}
			}
			if (isset($data['rdds']['events'])) {
				array_push($item_keys, CALCULATED_ITEM_RDDS_DELAY, CALCULATED_RDDS_ROLLWEEK_SLA);
			}
			if (isset($data['rdap']['events'])) {
				array_push($item_keys, CALCULATED_ITEM_RDAP_DELAY, CALCULATED_RDAP_ROLLWEEK_SLA);
			}
			if (isset($data['epp']['events'])) {
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

						$item_value = reset($item_value);

						if ($item['key_'] === CALCULATED_DNS_ROLLWEEK_SLA) {
							if (isset($services['dns'])) {
								$services['dns']['slaValue'] = $item_value['value'];
							}
							if (isset($services['dnssec'])) {
								$services['dnssec']['slaValue'] = $item_value['value'];
							}
						}
						elseif ($item['key_'] === CALCULATED_RDDS_ROLLWEEK_SLA) {
							$services['rdds']['slaValue'] = $item_value['value'];
						}
						elseif ($item['key_'] === CALCULATED_RDAP_ROLLWEEK_SLA) {
							$services['rdap']['slaValue'] = $item_value['value'];
						}
						else {
							$services['epp']['slaValue'] = $item_value['value'];
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

						if ($item['key_'] == CALCULATED_ITEM_DNS_DELAY) {
							if (isset($services['dns'])) {
								$services['dns']['delay'] = $item_value['value'];
								$services['dns']['itemId'] = $dns_avail_item;
							}
							if (isset($services['dnssec'])) {
								$services['dnssec']['delay'] = $item_value['value'];
								$services['dnssec']['itemId'] = $dnssec_avail_item;
							}
						}
						elseif ($item['key_'] == CALCULATED_ITEM_RDDS_DELAY) {
							$services['rdds']['delay'] = $item_value['value'];
							$services['rdds']['itemId'] = $rdds_avail_item;
						}
						elseif ($item['key_'] == CALCULATED_ITEM_RDAP_DELAY) {
							$services['rdap']['delay'] = $item_value['value'];
							$services['rdap']['itemId'] = $rdap_avail_item;
						}
						elseif ($item['key_'] == CALCULATED_ITEM_EPP_DELAY) {
							$services['epp']['delay'] = $item_value['value'];
							$services['epp']['itemId'] = $epp_avail_item;
						}
					}
				}

				foreach ($services as $key => $service) {
					$data[$key]['slaValue'] = $service['slaValue'] * 60;
					$data[$key]['delay'] = $service['delay'];
				}
			}
		}

		return true;
	}

	protected function sortResults(array &$data) {
		if (isset($data['dns']['events'])) {
			$data['dns']['events'] = array_reverse($data['dns']['events']);
		}
		if (isset($data['dnssec']['events'])) {
			$data['dnssec']['events'] = array_reverse($data['dnssec']['events']);
		}
		if (isset($data['rdds']['events'])) {
			$data['rdds']['events'] = array_reverse($data['rdds']['events']);
		}
		if (isset($data['rdap']['events'])) {
			$data['rdap']['events'] = array_reverse($data['rdap']['events']);
		}
		if (isset($data['epp']['events'])) {
			$data['epp']['events'] = array_reverse($data['epp']['events']);
		}
	}

	protected function doAction() {
		global $DB;

		$data = [
			'title' => _('Incidents'),
			'type' => $this->getInput('type', get_cookie('ui-tabs-1', 0)),
			'host' => $this->getInput('host', false),
			'tld' => null,
			'url' => '',
			'rdap_standalone_start_ts' => 0,
			'rsm_monitoring_mode' => get_rsm_monitoring_type(),
			'profileIdx' => 'web.rsm.incidents.filter',
			'profileIdx2' => 0,
			'from' => $this->hasInput('from') ? $this->getInput('from') : null,
			'to' => $this->hasInput('to') ? $this->getInput('to') : null,
			'active_tab' => CProfile::get('web.rsm.incidents.filter.active', 1)
		];

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

				// Update profile.
				if ($this->fetchData($data)) {
					if ($data['host'] && $data['filter_search'] != $data['tld']['host']) {
						$data['filter_search'] = $data['tld']['host'];
						CProfile::update('web.rsm.incidents.filter.search', $data['tld']['host'], PROFILE_TYPE_STR);
					}
				}

				$data['url'] = $server['URL'];
				$data['server'] = $server['NAME'];
				break;
			}

			unset($DB['DB']);
			$DB = $master;
			DBconnect($error);
		}

		// Chceck if RDAP standalone service was enabled during the filtered period.
		if (is_RDAP_standalone($this->filter_time_from) !== is_RDAP_standalone($this->filter_time_till)) {
			$rdap_standalone_ts = API::UserMacro()->get([
				'output' => ['value'],
				'filter' => [
					'macro' => RSM_RDAP_STANDALONE
				],
				'globalmacro' => true
			]);

			$data['rdap_standalone_start_ts'] = $rdap_standalone_ts ? $rdap_standalone_ts[0]['value'] : 0;
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
