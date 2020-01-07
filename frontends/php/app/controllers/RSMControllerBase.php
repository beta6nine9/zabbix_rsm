<?php
/*
** Zabbix
** Copyright (C) 2001-2019 Zabbix SIA
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


/**
 * Base controller for ICANN mvc controllers.
 */
class RSMControllerBase extends CController {

	/**
	 * Get profile data value for key $key.
	 *
	 * @param string $key          Profile data key, combined from controller $profile_prefix and $key.
	 * @return mixed
	 */
	protected function getProfileValue($key) {
		return CProfile::get(implode('.', [$this->profile_prefix, $key]));
	}

	/**
	 * Update profile value.
	 *
	 * @param string $key          Profile data key, combined from controller $profile_prefix and $key.
	 * @param mixed  $value        Profile data value.
	 * @param int    $value_type   Profile data value type, one of PROFILE_TYPE_* constant values.
	 */
	protected function setProfileValue($key, $value, $value_type) {
		return CProfile::update(implode('.', [$this->profile_prefix, $key]), $value, $value_type);
	}

	/**
	 * Get value mapping by mapping id.
	 *
	 * @param int $valuemapid    Value map database id.
	 * @return array
	 */
	protected function getValueMapping($valuemapid) {
		static $valuemaps = [];

		if (!isset($valuemaps[$valuemapid])) {
			$db_mapping = API::ValueMap()->get([
				'output' => [],
				'selectMappings' => ['value', 'newvalue'],
				'valuemapids' => [$valuemapid]
			]);
			$db_mapping = $db_mapping ? reset($db_mapping)['mappings'] : [];
			$valuemaps[$valuemapid] = [];

			foreach ($db_mapping as $mapping) {
				$valuemaps[$valuemapid][$mapping['value']] = $mapping['newvalue'];
			}
		}

		return $valuemaps[$valuemapid];
	}

	/**
	 * Update user profile from input.
	 *
	 * @param array $key_valuetype    Array with profile key as key and profile value type as value.
	 * @param array $data             Array with input data to be updated from.
	 */
	protected function updateProfileChanges(array $key_valuetype, array $data) {
		foreach ($key_valuetype as $key => $value_type) {
			if ($this->hasInput($key)) {
				$this->setProfileValue($key, $data[$key], $value_type);
			}
		}
	}

	/**
	 * Get history value for macro for desired datetime.
	 *
	 * @param array $macro             Array of desired macro key.
	 * @param int   $time_till         Timestamp untill time when newest macro value will be get.
	 * @return array
	 */
	protected function getHistoryMacroValue(array $macro, $time_till) {
		static $rsm_hostid;
		$values = [];

		if (!$rsm_hostid) {
			$rsm = API::Host()->get([
				'output' => ['hostid'],
				'filter' => ['host' => RSM_HOST]
			]);
			$rsm = reset($rsm);
		}

		if (!$rsm) {
			error(_s('No permissions to referred host "%1$s" or it does not exist!', RSM_HOST));
		}
		else {
			$macro_items = API::Item()->get([
				'output' => ['itemid', 'key_', 'value_type'],
				'hostids' => $rsm['hostid'],
				'filter' => [
					'key_' => $macro
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
					'output' => ['value'],
					'itemids' => $macro_item['itemid'],
					'time_till' => $time_till,
					'history' => $macro_item['value_type'],
					'sortfield' => 'clock',
					'sortorder' => ZBX_SORT_DOWN,
					'limit' => 1
				]);
				$macro_item_value = reset($macro_item_value);
				$values[$macro_item['key_']] = $macro_item_value['value'];
			}
		}

		return $values;
	}

	/**
	 * Get item history value. Return array of items with history value as value of 'history_value' key. Key will be
	 * set only for items having value in desirec period. When multiple values exists for single item first value
	 * will be set.
	 *
	 * @param int $options['time_from']  Interval start time to retrieve value of item.
	 * @param int $options['time_till']  Interval end time to retrvieve value of item.
	 * @param int $options['history']    Item value type.
	 * @return array
	 */
	protected function getItemsHistoryValue(array $options) {
		$options += [
			'output' => ['itemid', 'hostid'],
			'preservekeys' => true
		];

		$items = API::Item()->get($options);

		if ($items) {
			$values = API::History()->get([
				'output' => ['itemid', 'value'],
				'itemids' => array_keys($items),
				'time_from' => $options['time_from'],
				'time_till' => $options['time_till'],
				'history' => $options['history']
			]);

			foreach ($values as $value) {
				$items[$value['itemid']] += ['history_value' => $value['value']];
			}
		}

		return $items;
	}
	/**
	 * Generic permissions check for ICANN.
	 *
	 * @return bool
	 */
	protected function checkPermissions() {
		$valid_users = [USER_TYPE_READ_ONLY, USER_TYPE_ZABBIX_USER, USER_TYPE_POWER_USER, USER_TYPE_COMPLIANCE,
			USER_TYPE_ZABBIX_ADMIN, USER_TYPE_SUPER_ADMIN];

		return in_array($this->getUserType(), $valid_users);
	}

	protected function checkSID() {
		return true;
	}

	protected function checkInput() {
		return true;
	}

	protected function doAction() {
		error(_s('%s doAction is not implemented', get_class($this)));
		$this->setResponse(new CControllerResponseFatal());
	}
}
