<?php

namespace Modules\RSM\Actions;

use APP;
use API;
use CController as CAction;
use CControllerResponseFatal;

class Action extends CAction {

	protected $rsm_monitoring_mode;
	protected $is_rdap_standalone;

	protected $preload_macros = [];

	protected $macro;
	protected $module;

	public function init() {
		/**
		 * Disabling SID validation is a hackish way but without it current
		 * implementation of switching between frontends will not work.
		 */
		$this->disableSIDValidation();

		/** @var Modules\RSM\Services\MacroService $macro */
		$macro = APP::component()->get('rsm.macro');
		$macro->read($this->preload_macros);
		$this->macro = $macro;

		$this->rsm_monitoring_mode = $macro->get(RSM_MONITORING_TARGET);

		/** @var Modules\RSM\Module $module */
		$this->module = APP::component()->RSM;
	}

	/**
	 * Check request is made via ajax.
	 */
	public function isAjaxRequest(): bool {
		return strtolower($_SERVER['HTTP_X_REQUESTED_WITH'] ?? '') === 'xmlhttprequest';
	}

	/**
	 * Based on timestamp value stored in {$RSM.RDAP.STANDALONE}, check if RDAP at given time $timestamp is configured as
	 * standalone service or as dependent sub-service of RDDS. It is expected that switch from RDAP as sub-service of RDDS
	 * to RDAP as standalone service will be done only once and will never be switched back to initial state.
	 *
	 * @param integer|string  $timestamp  Optional timestamp value.
	 *
	 * @return bool
	 */
	protected function isRdapStandalone($timestamp = null) {
		$value = $this->macro->get(RSM_RDAP_STANDALONE);
		$rsm_rdap_standalone_ts = is_null($value) ? 0 : (int) $value;
		$timestamp = is_null($timestamp) ? time() : (int) $timestamp;

		return ($rsm_rdap_standalone_ts > 0 && $rsm_rdap_standalone_ts <= $timestamp);
	}

	protected function checkInput() {
		if (!$this->fields) {
			return true;
		}

		$ret = $this->validateInput($this->fields);
		if (!$ret) {
			$this->setResponse(new CControllerResponseFatal());
		}

		return $ret;
	}

	protected function checkPermissions() {
		$valid_users = [
			USER_TYPE_READ_ONLY, USER_TYPE_ZABBIX_USER, USER_TYPE_POWER_USER, USER_TYPE_COMPLIANCE,
			USER_TYPE_ZABBIX_ADMIN, USER_TYPE_SUPER_ADMIN
		];

		return in_array($this->getUserType(), $valid_users);
	}

	protected function doAction() {
	}

	/**
	 * Get history value for macro for desired datetime.
	 *
	 * @param array $macro             Array of desired macro key.
	 * @param int   $time_till         Timestamp untill time when newest macro value will be get.
	 * @return array
	 */
	protected function getMacroHistoryValue(array $macro, $time_till) {
		static $rsm;
		$values = [];

		if (!$rsm) {
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
				 * To get value that actually was current at the time when data was collected, we need to get history
				 * record that was newest at the moment of requested time.
				 *
				 * In other words:
				 * SELECT * FROM history_uint WHERE itemid=<itemid> AND <test_time_from> >= clock ORDER BY clock DESC
				 * LIMIT 1
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
	 * Get item history value. Return array of items with history value in 'history_value' key. Key will be
	 * set only for items having value in desired period. When multiple values exists for single item first value
	 * will be set.
	 *
	 * @param int $options['time_from']  Interval start time to retrieve value of item.
	 * @param int $options['time_till']  Interval end time to retrieve value of item.
	 * @param int $options['history']    Item value type.
	 * @return array
	 */
	protected function getItemsHistoryValue(array $options, array $history_options = []) {
		$options += [
			'output' => ['itemid', 'hostid'],
			'preservekeys' => true
		];

		$items = API::Item()->get($options);

		if ($items) {
			$history_options += [
				'output'    => ['itemid', 'value'],
				'itemids'   => array_keys($items),
				'time_from' => $options['time_from'],
				'time_till' => $options['time_till'],
				'history'   => $options['history'],
			];

			$values = API::History()->get($history_options);

			foreach ($values as $value) {
				$items[$value['itemid']] += ['history_value' => $value['value']];
			}
		}

		return $items;
	}
}
