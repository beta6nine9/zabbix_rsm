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

use CControllerResponseFatal;
use CProfile;
use CWebUser;
use API;
use CPagerHelper;
use CUrl;
use CControllerResponseData;
use CUser;

class RollingWeekStatusListAction extends Action {

	protected function checkInput() {
		$sort_fields = ['type', 'server', 'dns_lastvalue', 'dnssec_lastvalue', 'rdds_lastvalue',
			'rdap_lastvalue', 'epp_lastvalue', 'info_1', 'info_2', 'host'
		];

		$fields = [
			'filter_set'				=> 'in 1',
			'filter_rst'				=> 'in 1',
			'filter_search'				=> 'string',
			'filter_dns'				=> 'in 1',
			'filter_dnssec'				=> 'in 1',
			'filter_rdds'				=> 'in 1',
			'filter_rdap'				=> 'in 1',
			'filter_epp'				=> 'in 1',
			'filter_slv'				=> 'string',
			'filter_status'				=> 'in 0,1,2',
			'filter_gtld_group'			=> 'in 1',
			'filter_cctld_group'		=> 'in 1',
			'filter_othertld_group'		=> 'in 1',
			'filter_test_group'			=> 'in 1',
			'filter_rdap_subgroup'		=> 'in 1',
			'filter_rdds43_subgroup'	=> 'in 1',
			'filter_rdds80_subgroup'	=> 'in 1',
			'filter_registrar_id'		=> 'string',
			'filter_registrar_name'		=> 'string',
			'filter_registrar_family'	=> 'string',
			'sort'						=> 'in '.implode(',', $sort_fields),
			'sortorder'					=> 'in '.implode(',', [ZBX_SORT_DOWN, ZBX_SORT_UP]),
			'page'						=> 'int32',
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

	protected function updateProfile() {
		// Set/Reset filter.
		DBStart();

		if ($this->hasInput('filter_set')) {
			CProfile::update('web.rsm.rollingweekstatus.sort', $this->getInput('sort', 'host'), PROFILE_TYPE_STR);
			CProfile::update('web.rsm.rollingweekstatus.sortorder', $this->getInput('sortorder', ZBX_SORT_UP), PROFILE_TYPE_STR);
			CProfile::update('web.rsm.rollingweekstatus.filter_search', $this->getInput('filter_search', ''), PROFILE_TYPE_STR);
			CProfile::update('web.rsm.rollingweekstatus.filter_dns', $this->getInput('filter_dns', 0), PROFILE_TYPE_INT);
			CProfile::update('web.rsm.rollingweekstatus.filter_dnssec', $this->getInput('filter_dnssec', 0), PROFILE_TYPE_INT);
			CProfile::update('web.rsm.rollingweekstatus.filter_rdds', $this->getInput('filter_rdds', 0), PROFILE_TYPE_INT);
			CProfile::update('web.rsm.rollingweekstatus.filter_rdap', $this->getInput('filter_rdap', 0), PROFILE_TYPE_INT);
			CProfile::update('web.rsm.rollingweekstatus.filter_epp', $this->getInput('filter_epp', 0), PROFILE_TYPE_INT);
			CProfile::update('web.rsm.rollingweekstatus.filter_slv', $this->getInput('filter_slv', 0), PROFILE_TYPE_STR);
			CProfile::update('web.rsm.rollingweekstatus.filter_status', $this->getInput('filter_status', 0), PROFILE_TYPE_INT);
			CProfile::update('web.rsm.rollingweekstatus.filter_gtld_group', $this->getInput('filter_gtld_group', 0), PROFILE_TYPE_INT);
			CProfile::update('web.rsm.rollingweekstatus.filter_cctld_group', $this->getInput('filter_cctld_group', 0), PROFILE_TYPE_INT);
			CProfile::update('web.rsm.rollingweekstatus.filter_othertld_group', $this->getInput('filter_othertld_group', 0), PROFILE_TYPE_INT);
			CProfile::update('web.rsm.rollingweekstatus.filter_test_group', $this->getInput('filter_test_group', 0), PROFILE_TYPE_INT);
			CProfile::update('web.rsm.rollingweekstatus.filter_rdap_subgroup', $this->getInput('filter_rdap_subgroup', 0), PROFILE_TYPE_INT);
			CProfile::update('web.rsm.rollingweekstatus.filter_rdds43_subgroup', $this->getInput('filter_rdds43_subgroup', 0), PROFILE_TYPE_INT);
			CProfile::update('web.rsm.rollingweekstatus.filter_rdds80_subgroup', $this->getInput('filter_rdds80_subgroup', 0), PROFILE_TYPE_INT);
			CProfile::update('web.rsm.rollingweekstatus.filter_registrar_id', $this->getInput('filter_registrar_id', ''), PROFILE_TYPE_STR);
			CProfile::update('web.rsm.rollingweekstatus.filter_registrar_name', $this->getInput('filter_registrar_name', ''), PROFILE_TYPE_STR);
			CProfile::update('web.rsm.rollingweekstatus.filter_registrar_family', $this->getInput('filter_registrar_family', ''), PROFILE_TYPE_STR);
		}
		elseif (hasRequest('filter_rst')) {
			CProfile::delete('web.rsm.rollingweekstatus.sort');
			CProfile::delete('web.rsm.rollingweekstatus.sortorder');
			CProfile::delete('web.rsm.rollingweekstatus.filter_search');
			CProfile::delete('web.rsm.rollingweekstatus.filter_dns');
			CProfile::delete('web.rsm.rollingweekstatus.filter_dnssec');
			CProfile::delete('web.rsm.rollingweekstatus.filter_rdds');
			CProfile::delete('web.rsm.rollingweekstatus.filter_rdap');
			CProfile::delete('web.rsm.rollingweekstatus.filter_epp');
			CProfile::delete('web.rsm.rollingweekstatus.filter_slv');
			CProfile::delete('web.rsm.rollingweekstatus.filter_status');
			CProfile::delete('web.rsm.rollingweekstatus.filter_gtld_group');
			CProfile::delete('web.rsm.rollingweekstatus.filter_cctld_group');
			CProfile::delete('web.rsm.rollingweekstatus.filter_othertld_group');
			CProfile::delete('web.rsm.rollingweekstatus.filter_test_group');
			CProfile::delete('web.rsm.rollingweekstatus.filter_rdap_subgroup');
			CProfile::delete('web.rsm.rollingweekstatus.filter_rdds43_subgroup');
			CProfile::delete('web.rsm.rollingweekstatus.filter_rdds80_subgroup');
			CProfile::delete('web.rsm.rollingweekstatus.filter_registrar_id');
			CProfile::delete('web.rsm.rollingweekstatus.filter_registrar_name');
			CProfile::delete('web.rsm.rollingweekstatus.filter_registrar_family');
		}
		DBend();
	}

	protected function readValues(array &$data) {
		$data += [
			'sort_field' =>  $this->getInput('sort', 'host'),
			'sort_order' =>  $this->getInput('sortorder', ZBX_SORT_UP),
			'filter_search' => CProfile::get('web.rsm.rollingweekstatus.filter_search'),
			'filter_dns' => CProfile::get('web.rsm.rollingweekstatus.filter_dns'),
			'filter_dnssec' => CProfile::get('web.rsm.rollingweekstatus.filter_dnssec'),
			'filter_rdds' => CProfile::get('web.rsm.rollingweekstatus.filter_rdds'),
			'filter_rdap' => CProfile::get('web.rsm.rollingweekstatus.filter_rdap'),
			'filter_epp' => CProfile::get('web.rsm.rollingweekstatus.filter_epp'),
			'filter_slv' => CProfile::get('web.rsm.rollingweekstatus.filter_slv'),
			'filter_status' => CProfile::get('web.rsm.rollingweekstatus.filter_status'),
			'filter_gtld_group' => CProfile::get('web.rsm.rollingweekstatus.filter_gtld_group'),
			'filter_cctld_group' => CProfile::get('web.rsm.rollingweekstatus.filter_cctld_group'),
			'filter_othertld_group' => CProfile::get('web.rsm.rollingweekstatus.filter_othertld_group'),
			'filter_test_group' => CProfile::get('web.rsm.rollingweekstatus.filter_test_group'),
			'filter_rdap_subgroup' => CProfile::get('web.rsm.rollingweekstatus.filter_rdap_subgroup'),
			'filter_rdds43_subgroup' => CProfile::get('web.rsm.rollingweekstatus.filter_rdds43_subgroup'),
			'filter_rdds80_subgroup' => CProfile::get('web.rsm.rollingweekstatus.filter_rdds80_subgroup'),
			'filter_registrar_id' => CProfile::get('web.rsm.rollingweekstatus.filter_registrar_id'),
			'filter_registrar_name' => CProfile::get('web.rsm.rollingweekstatus.filter_registrar_name'),
			'filter_registrar_family' => CProfile::get('web.rsm.rollingweekstatus.filter_registrar_family'),
			'active_tab' => CProfile::get('web.rsm.rollingweekstatus.filter.active', 1),
			'sid' => CWebUser::getSessionCookie(),
			'paging' => null
		];

		// Erase fields that are not supported in particular monitoring mode.
		if ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRAR) {
			$data['filter_search'] = '';
			$data['filter_dns'] = '';
			$data['filter_dnssec'] = '';
			$data['filter_epp'] = '';
			$data['filter_gtld_group'] = '';
			$data['filter_cctld_group'] = '';
			$data['filter_othertld_group'] = '';
			$data['filter_test_group'] = '';
			if (!$data['filter_registrar_id']) {
			//	$data['filter_registrar_id'] = '';
			}
			if (!$data['filter_registrar_name']) {
				$data['filter_registrar_name'] = '';
			}
			if (!$data['filter_registrar_family']) {
				$data['filter_registrar_family'] = '';
			}
			$data['filter_rdds'] = true;
			$data['filter_rdap'] = true;
		}
		else {
			$data['filter_registrar_id'] = '';
			$data['filter_registrar_name'] = '';
			$data['filter_registrar_family'] = '';
		}

		if (is_RDAP_standalone()) {
			$data['filter_rdap_subgroup'] = false;
		}
		else {
			$data['filter_rdap'] = false;
		}

		// Unset to avoid redundant validation later.
		if ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRAR) {
			$data['filter_dns'] = 0;
		}
	}

	protected function readMacros(array &$data) {
		$macros = API::UserMacro()->get([
			'output' => ['macro', 'value'],
			'filter' => [
				'macro' => [RSM_PAGE_SLV, RSM_ROLLWEEK_SECONDS]
			],
			'globalmacro' => true
		]);

		foreach ($macros as $macro) {
			if ($macro['macro'] === RSM_PAGE_SLV) {
				$data['slv'] = $macro['value'];
			}
			else {
				$data['rollWeekSeconds'] = $macro['value'];
			}
		}

		if (!array_key_exists('slv', $data)) {
			error(_s('Macro "%1$s" doesn\'t not exist.', RSM_PAGE_SLV));
		}

		if (!array_key_exists('rollWeekSeconds', $data)) {
			error(_s('Macro "%1$s" doesn\'t not exist.', RSM_ROLLWEEK_SECONDS));
		}
	}

	protected function getTLDGroups(array &$data) {
		$selected_groupids = [];
		$included_groupids = []; // Groups selected in filter as TLD types. In case of registrar mode, there will be all available groups.

		$data['allowedGroups'] = [
			RSM_CC_TLD_GROUP => false,
			RSM_G_TLD_GROUP => false,
			RSM_OTHER_TLD_GROUP => false,
			RSM_TEST_GROUP => false
		];

		$tld_groups = API::HostGroup()->get([
			'output' => ['groupid', 'name'],
			'filter' => [
				'name' => [RSM_TLDS_GROUP, RSM_CC_TLD_GROUP, RSM_G_TLD_GROUP, RSM_OTHER_TLD_GROUP, RSM_TEST_GROUP]
			]
		]);

		foreach ($tld_groups as $tld_group) {
			switch ($tld_group['name']) {
				case RSM_TLDS_GROUP:
					$selected_groupids[$tld_group['groupid']] = $tld_group['groupid'];
					break;

				case RSM_CC_TLD_GROUP:
					$data['allowedGroups'][RSM_CC_TLD_GROUP] = true;

					if ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRAR || $data['filter_cctld_group']) {
						$included_groupids[$tld_group['groupid']] = $tld_group['groupid'];
					}
					break;

				case RSM_G_TLD_GROUP:
					$data['allowedGroups'][RSM_G_TLD_GROUP] = true;

					if ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRAR || $data['filter_gtld_group']) {
						$included_groupids[$tld_group['groupid']] = $tld_group['groupid'];
					}
					break;

				case RSM_OTHER_TLD_GROUP:
					$data['allowedGroups'][RSM_OTHER_TLD_GROUP] = true;

					if ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRAR || $data['filter_othertld_group']) {
						$included_groupids[$tld_group['groupid']] = $tld_group['groupid'];
					}
					break;

				case RSM_TEST_GROUP:
					$data['allowedGroups'][RSM_TEST_GROUP] = true;

					if ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRAR || $data['filter_test_group']) {
						$included_groupids[$tld_group['groupid']] = $tld_group['groupid'];
					}
					break;
			}
		}

		return [$selected_groupids, $included_groupids];
	}

	protected function getTLDs(array $data, array $selected_groupids, array $included_groupids, $db_nr, $key) {
		global $DB;

		$tlds = [];
		$where_host = [];
		$hosts_table_alias = (CUser::$userData['type'] == USER_TYPE_SUPER_ADMIN) ? 'h' : 'hh';

		// Search by exact matching registrar id.
		if ($data['filter_search'] !== '') {
			$where_host[] = $hosts_table_alias.'.host LIKE ('.zbx_dbstr('%'.$data['filter_search'].'%').')';
		}
		if ($data['filter_registrar_id'] !== '') {
			$where_host[] = dbConditionString($hosts_table_alias.'.host ', [$data['filter_registrar_id']]);
		}
		if ($data['filter_registrar_name'] !== '') {
			$where_host[] = $hosts_table_alias.'.info_1 LIKE ('.zbx_dbstr('%'.$data['filter_registrar_name'].'%').')';
		}
		if ($data['filter_registrar_family'] !== '') {
			$where_host[] = $hosts_table_alias.'.info_2 LIKE ('.zbx_dbstr('%'.$data['filter_registrar_family'].'%').')';
		}

		// Stringify query where conditions.
		$where_host = $where_host ? ' AND ('.implode(' AND ', $where_host).')' : '';

		// Select TLD hosts.
		if (CUser::$userData['type'] == USER_TYPE_SUPER_ADMIN) {
			$host_count = (count($selected_groupids) >= 2) ? 2 : 1;

			$db_tlds = DBselect(
				'SELECT h.hostid,h.host,h.info_1,h.info_2,h.status'.
				' FROM hosts h'.
				' WHERE hostid IN ('.
					'SELECT hg.hostid from hosts_groups hg'.
					' WHERE '.dbConditionInt('hg.groupid', $selected_groupids).
					' GROUP BY hg.hostid HAVING COUNT(hg.hostid)>='.$host_count.')'.
					$where_host
			);
		}
		else {
			$user_groupids = getUserGroupsByUserId(CWebUser::$data['userid']);

			$db_tlds = DBselect(
				'SELECT h.hostid,h.host,h.info_1,h.info_2,h.status'.
				' FROM hosts h'.
				' WHERE hostid IN ('.
					'SELECT hgg.hostid'.
					' FROM hosts_groups hgg'.
					' JOIN rights r ON r.id=hgg.groupid AND '.dbConditionInt('r.groupid', $user_groupids).
					' WHERE hgg.hostid IN ('.
						'SELECT hh.hostid'.
						' FROM hosts hh,hosts_groups hg'.
						' WHERE '.dbConditionInt('hg.groupid', $selected_groupids).
							' AND hh.hostid=hg.hostid'.
							$where_host.
					')'.
					'GROUP BY hgg.hostid HAVING MIN(r.permission)>=2)'
			);
		}

		if ($db_tlds) {
			$hostids = [];

			while ($db_tld = DBfetch($db_tlds)) {
				$hostids[] = $db_tld['hostid'];

				$tlds[$db_nr.$db_tld['hostid']] = [
					'hostid' => $db_tld['hostid'],
					'host' => $db_tld['host'],
					'info_1' => $db_tld['info_1'],
					'info_2' => $db_tld['info_2'],
					'status' => $db_tld['status'],
					'dns_lastvalue' => 0,
					'dnssec_lastvalue' => 0,
					'rdds_lastvalue' => 0,
					'epp_lastvalue' => 0,
					'server' => $DB['SERVERS'][$key]['NAME'],
					'url' => $DB['SERVERS'][$key]['URL'],
					'type' => null,
					'db' => $key
				];
			}

			// Apply TLD type representing hostgroups.
			$host_groups = API::HostGroup()->get([
				'output' => ['groupid', 'name'],
				'selectHosts' => ['hostid'],
				'hostids' => $hostids,
				'groupids' => $included_groupids
			]);

			foreach ($host_groups as $host_group) {
				foreach ($host_group['hosts'] as $host) {
					if (array_key_exists($db_nr.$host['hostid'], $tlds)) {
						$tlds[$db_nr.$host['hostid']]['type'] = $host_group['name'];
					}
				}
			}

			// Unset TLD hosts without type specified.
			$hostids = array_flip($hostids);
			foreach ($tlds as $key => $value) {
				if ($value['type'] === null) {
					unset($tlds[$key], $hostids[$value['hostid']]);
				}
			}
		}

		return $tlds;
	}

	protected function fetchData(array &$data) {
		global $DB;

		$master = $DB;
		$data['tld'] = [];

		foreach ($DB['SERVERS'] as $key => $value) {
			/*
			 * If registrar mode is ON, there are no check-boxes to filter records by TLD type. That's why we assume
			 * that they all are checked. Later we will make more precise conditions to limit results received from
			 * database.
			 */
			$filter_by_tlds = ($data['filter_cctld_group'] || $data['filter_gtld_group']
				|| $data['filter_othertld_group'] || $data['filter_test_group']);

			if ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRAR || $filter_by_tlds) {
				// Check if new database connection should be made.
				if ($DB['SERVER'] !== $DB['SERVERS'][$key]['SERVER']
					|| $DB['PORT'] !== $DB['SERVERS'][$key]['PORT']
					|| $DB['DATABASE'] !== $DB['SERVERS'][$key]['DATABASE']
					|| $DB['USER'] !== $DB['SERVERS'][$key]['USER']
					|| $DB['PASSWORD'] !== $DB['SERVERS'][$key]['PASSWORD']
				) {
					if (!multiDBconnect($DB['SERVERS'][$key], $error)) {
						error(_($DB['SERVERS'][$key]['NAME'].': '.$error));
						continue;
					}
				}

				$db_nr = $DB['SERVERS'][$key]['NR'];

				/*
				 * Get "TLDs" groups.
				 *
				 * Groups selected in filter as TLD types. In case of registrar mode,all groups are available because
				 * there are o checkboxes in UI to filter.
				 */
				list($selected_groupids, $included_groupids) = $this->getTLDGroups($data);

				if (!$selected_groupids) {
					error(_s('No permissions to referred "%1$s" group or it doesn\'t not exist.', RSM_TLDS_GROUP));
				}

				// Use filter values to find matching hosts (TLDs/Registrars).
				$data['tld'] += $this->getTLDs($data, $selected_groupids, $included_groupids, $db_nr, $key);
			}
			else {
				// Get "TLDs" groups.
				$this->getTLDGroups($data);
			}
		}

		order_result($data['tld'], $data['sort_field'], $data['sort_order']);
	}

	protected function selectTLDAttributes(array &$data) {
		global $DB;

		$tlds_by_server = [];
		foreach ($data['tld'] as $tld) {
			$tlds_by_server[$tld['db']][$tld['hostid']] = $tld['host'];
		}

		$itemkey_type = [
			RSM_SLV_DNS_ROLLWEEK => RSM_DNS,
			RSM_SLV_DNSSEC_ROLLWEEK => RSM_DNSSEC,
			RSM_SLV_RDDS_ROLLWEEK => RSM_RDDS,
			RSM_SLV_RDAP_ROLLWEEK => RSM_RDAP,
			RSM_SLV_EPP_ROLLWEEK => RSM_EPP
		];
		$avail_type = [
			RSM_SLV_DNS_AVAIL => RSM_DNS,
			RSM_SLV_DNSSEC_AVAIL => RSM_DNSSEC,
			RSM_SLV_RDDS_AVAIL => RSM_RDDS,
			RSM_SLV_RDAP_AVAIL => RSM_RDAP,
			RSM_SLV_EPP_AVAIL => RSM_EPP
		];
		$filter_type = [
			'filter_dns' => RSM_DNS,
			'filter_dnssec' => RSM_DNSSEC,
			'filter_rdds' => RSM_RDDS,
			'filter_rdap' => RSM_RDAP,
			'filter_epp' => RSM_EPP
		];
		$item_keys = ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRY)
			? [RSM_SLV_DNS_ROLLWEEK, RSM_SLV_DNSSEC_ROLLWEEK, RSM_SLV_RDDS_ROLLWEEK, RSM_SLV_EPP_ROLLWEEK]
			: [RSM_SLV_RDDS_ROLLWEEK];
		$avail_items = ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRY)
			? [RSM_SLV_DNS_AVAIL, RSM_SLV_DNSSEC_AVAIL, RSM_SLV_RDDS_AVAIL, RSM_SLV_EPP_AVAIL]
			: [RSM_SLV_RDDS_AVAIL];

		if (is_RDAP_standalone()) {
			$item_keys[] = RSM_SLV_RDAP_ROLLWEEK;
			$avail_items[] = RSM_SLV_RDAP_AVAIL;
		}

		foreach ($tlds_by_server as $key => $hosts) {
			if (!$hosts) {
				continue;
			}

			multiDBconnect($DB['SERVERS'][$key], $error);

			$itemIds = [];
			$filter_slv = [];
			// get items
			$items = [];
			$db_items = DBselect(
				'SELECT i.itemid, i.hostid, i.key_'.
				' FROM items i'.
				' WHERE '.dbConditionString('i.key_', $item_keys).
					' AND '.dbConditionInt('i.hostid', array_keys($hosts))
			);

			$rsm_itemids = [];
			while ($item = DBfetch($db_items)) {
				$items[$item['itemid']] = [
					'itemid' => $item['itemid'],
					'hostid' => $item['hostid'],
					'key_' => $item['key_'],
					'lastvalue' => null
				];

				$rsm_itemids[$item['itemid']] = true;
			}

			if ($rsm_itemids) {
				$db_histories = DBselect(
					'SELECT l.itemid, l.value, l.clock'.
					' FROM lastvalue l'.
					' WHERE '.dbConditionInt('l.itemid', array_keys($rsm_itemids))
				);

				while ($history = DBfetch($db_histories)) {
					$items[$history['itemid']]['lastvalue'] = $history['value'];
					$items[$history['itemid']]['clock'] = $history['clock'];
				}
			}

			if ($avail_items) {
				$db_avail_items = API::Item()->get([
					'output' => ['itemid', 'hostid', 'key_'],
					'hostids' => array_keys($hosts),
					'filter' => [
						'key_' => $avail_items
					],
					'preservekeys' => true
				]);
			}

			if ($items) {
				foreach ($items as $item) {
					// service type filter
					if (!array_key_exists($item['hostid'], $filter_slv) || $filter_slv[$item['hostid']] === false) {
						$filter_slv[$item['hostid']] = false;

						if ($data['filter_slv'] === '') {
							$filter_slv[$item['hostid']] = true;
						}
						elseif (
							($data['filter_dns'] && $item['key_'] === RSM_SLV_DNS_ROLLWEEK)
							|| ($data['filter_dnssec'] && $item['key_'] === RSM_SLV_DNSSEC_ROLLWEEK)
							|| ($data['filter_rdds'] && $item['key_'] === RSM_SLV_RDDS_ROLLWEEK)
							|| ($data['filter_rdap'] && $item['key_'] === RSM_SLV_RDAP_ROLLWEEK)
							|| ($data['filter_epp'] && $item['key_'] === RSM_SLV_EPP_ROLLWEEK)
						) {
							if ($data['filter_slv'] != SLA_MONITORING_SLV_FILTER_NON_ZERO
									&& $item['lastvalue'] >= $data['filter_slv']) {
								$filter_slv[$item['hostid']] = true;
							}
							elseif ($data['filter_slv'] == SLA_MONITORING_SLV_FILTER_NON_ZERO && $item['lastvalue'] != 0) {
								$filter_slv[$item['hostid']] = true;
							}
						}
					}

					$hostid_key = $DB['SERVERS'][$key]['NR'].$item['hostid'];

					if (array_key_exists($hostid_key, $data['tld'])) {
						$data['tld'][$hostid_key][$itemkey_type[$item['key_']]] = [
							'itemid' => $item['itemid'],
							'lastvalue' => is_null($item['lastvalue']) ? null : sprintf('%.3f', $item['lastvalue']),
							'clock' => isset($item['clock']) ? $item['clock'] : null,
							'trigger' => false
						];
					}
				}

				foreach ($db_avail_items as $item) {
					$hostid_key = $DB['SERVERS'][$key]['NR'].$item['hostid'];
					$data['tld'][$hostid_key][$avail_type[$item['key_']]]['availItemId'] = $item['itemid'];
					$itemIds[$item['itemid']] = true;

					$rows = DBselect(
						'SELECT clock'.
						' FROM lastvalue'.
						' WHERE itemid='.$item['itemid']
					);

					$row = DBfetch($rows);
					if ($row && $row['clock']) {
						$data['tld'][$hostid_key][$avail_type[$item['key_']]]['availClock'] = $row['clock'];
					}
				}

				$items += $db_avail_items;

				if ($data['filter_slv'] !== '') {
					foreach ($filter_slv as $filtred_hostid => $value) {
						if ($value === false) {
							unset($data['tld'][$DB['SERVERS'][$key]['NR'].$filtred_hostid], $hosts[$filtred_hostid]);
						}
					}
				}

				if ($hosts) {
					// disabled services check
					$templateName = [];
					foreach ($hosts as $hostid => $host) {
						$name = sprintf(TEMPLATE_NAME_TLD_CONFIG, $host);
						$templateName[$hostid] = $name;
						$hostIdByTemplateName[$name] = $hostid;
					}

					$templates = API::Template()->get([
						'output' => ['templateid', 'host'],
						'filter' => [
							'host' => $templateName
						],
						'preservekeys' => true
					]);

					$templateIds = array_keys($templates);

					foreach ($templates as $template) {
						$templateName[$template['host']] = $template['templateid'];
					}

					$template_macros = [RSM_TLD_DNSSEC_ENABLED, RSM_TLD_EPP_ENABLED, RSM_TLD_RDDS43_ENABLED,
						RSM_TLD_RDDS80_ENABLED, RSM_RDAP_TLD_ENABLED];

					$templateMacros = API::UserMacro()->get([
						'output' => API_OUTPUT_EXTEND,
						'hostids' => $templateIds,
						'filter' => [
							'macro' => $template_macros
						]
					]);

					// Holds hostids with at least one disabled item detected.
					$hosts_with_disabled_items = [];

					foreach ($templateMacros as $template_macro) {
						$current_hostid = $hostIdByTemplateName[$templates[$template_macro['hostid']]['host']];

						if (in_array($template_macro['macro'], [RSM_TLD_DNSSEC_ENABLED, RSM_TLD_EPP_ENABLED, RSM_RDAP_TLD_ENABLED])) {
							if ($template_macro['value'] == 0) {
								if ($template_macro['macro'] === RSM_TLD_DNSSEC_ENABLED) {
									$service_type = RSM_DNSSEC;
								}
								elseif (is_RDAP_standalone() && $template_macro['macro'] === RSM_RDAP_TLD_ENABLED) {
									$service_type = RSM_RDAP;
								}
								else {
									$service_type = RSM_EPP;
								}

								// Unset disabled services.
								if (isset($data['tld'][$DB['SERVERS'][$key]['NR'].$current_hostid][$service_type])) {
									$hosts_with_disabled_items[$current_hostid] = true;

									if (array_key_exists('availItemId', $data['tld'][$DB['SERVERS'][$key]['NR'].$current_hostid][$service_type])) {
										unset($itemIds[$data['tld'][$DB['SERVERS'][$key]['NR'].$current_hostid][$service_type]['availItemId']]);
									}
									unset($data['tld'][$DB['SERVERS'][$key]['NR'].$current_hostid][$service_type]);
								}
							}
							elseif ($template_macro['macro'] === RSM_RDAP_TLD_ENABLED && !is_RDAP_standalone()) {
								// handle enabled RDAP when it's part of RDDS
								if (array_key_exists(RSM_RDDS, $data['tld'][$DB['SERVERS'][$key]['NR'].$current_hostid])) {
									$data['tld'][$DB['SERVERS'][$key]['NR'].$current_hostid][RSM_RDDS]['subservices'][$template_macro['macro']] = $template_macro['value'];
								}
							}
						}
						else {
							if (array_key_exists(RSM_RDDS, $data['tld'][$DB['SERVERS'][$key]['NR'].$current_hostid])) {
								$data['tld'][$DB['SERVERS'][$key]['NR'].$current_hostid][RSM_RDDS]['subservices'][$template_macro['macro']] = $template_macro['value'];
							}
						}
					}

					foreach ($hosts as $hostid => $host) {
						$tld_key = $DB['SERVERS'][$key]['NR'].$hostid;
						$tld = $data['tld'][$tld_key];

						if (array_key_exists(RSM_RDDS, $tld)) {
							if (!array_key_exists('subservices', $tld[RSM_RDDS]) || !array_sum($tld[RSM_RDDS]['subservices'])) {
								unset($itemIds[$tld[RSM_RDDS]['availItemId']]);
								unset($data['tld'][$tld_key][RSM_RDDS]);
								$hosts_with_disabled_items[$hostid] = true;
							}
						}
					}

					/**
					 * Even if previously in service type filter particular service matched filter (see $filter_slv), now it
					 * could be necessary to remove TLD row from result set, just because discovering user macros we have
					 * figured out that one or more services are disabled.
					 *
					 * It is better to make redundant check here (instead of checking enabled/disabled status before
					 * service filter) because it reduces amount of records in $templates and $templateMacros selected by
					 * API::Template() and API::UserMacro() and gives better performance.
					 *
					 * Only hosts with disabled items are re-tested.
					 * Only if filter 'Exceeding or equal to' is not set to 'any'.
					 */
					if ($hosts_with_disabled_items && $data['filter_slv'] !== '') {
						foreach ($hosts_with_disabled_items as $hostid => $value) {
							$available = false;
							$host = $data['tld'][$DB['SERVERS'][$key]['NR'].$hostid];
							$active_filters = array_intersect_key($filter_type, $data);
							$item_filter = array_intersect_key(array_flip($active_filters), $host);

							foreach($item_filter as $type => $filter) {
								if ($data[$filter]) {
									$available = ($data['filter_slv'] == SLA_MONITORING_SLV_FILTER_NON_ZERO)
										? ($host[$type]['lastvalue'] != 0)
										: ($host[$type]['lastvalue'] >= $data['filter_slv']);
								}

								if ($available) {
									break;
								}
							}

							// Unset if no displayable services found.
							if (!$available) {
								unset($data['tld'][$DB['SERVERS'][$key]['NR'].$hostid], $hosts[$hostid]);
							}
						}
					}
					unset($hosts_with_disabled_items);

					// get triggers
					$triggers = API::Trigger()->get(array(
						'output' => array('triggerid', 'value'),
						'selectItems' => ['itemid'],
						'itemids' => array_keys($itemIds),
						'filter' => ['value' => TRIGGER_VALUE_TRUE]
					));

					foreach ($triggers as $trigger) {
						$tritem = $trigger['items'][0]['itemid'];
						$hostid = $DB['SERVERS'][$key]['NR'].$items[$tritem]['hostid'];

						if (array_key_exists($hostid, $data['tld'])) {
							$type = $avail_type[$items[$tritem]['key_']];
							$data['tld'][$hostid][$type]['incident'] = $trigger['triggerid'];
							$data['tld'][$hostid][$type]['trigger'] = (bool) $trigger['triggerid'];
						}
					}
				}
			}
		}

		if ($data['filter_status']) {
			foreach ($data['tld'] as $key => $tld) {
				if ($data['filter_status'] == 1) { // Current status == fail
					if ((!$data['filter_dns'] || (!array_key_exists(RSM_DNS, $tld) || !$tld[RSM_DNS]['trigger']))
							&& (!$data['filter_dnssec'] || (!array_key_exists(RSM_DNSSEC, $tld) || !$tld[RSM_DNSSEC]['trigger']))
							&& (!$data['filter_rdds'] || (!array_key_exists(RSM_RDDS, $tld) || !$tld[RSM_RDDS]['trigger']))
							&& (!$data['filter_rdap'] || (!array_key_exists(RSM_RDAP, $tld) || !$tld[RSM_RDAP]['trigger']))
							&& (!$data['filter_epp'] || (!array_key_exists(RSM_EPP, $tld) || !$tld[RSM_EPP]['trigger']))) {
						unset($data['tld'][$key]);
					}
				}
				elseif ($data['filter_status'] == 2 && $tld['status'] == HOST_STATUS_MONITORED ) {  // Current status == disabled
					unset($data['tld'][$key]);
				}
			}
		}

		// Filter RDDS subservices.
		if ($data['filter_rdap_subgroup'] || $data['filter_rdds43_subgroup'] || $data['filter_rdds80_subgroup']) {
			foreach ($data['tld'] as $key => $tld) {
				if (is_RDAP_standalone() && !array_key_exists(RSM_RDDS, $tld)) {
					// do not let RDDS subservices affect RDAP-only enabled Rsmhosts in Standalone RDAP mode
					continue;
				}

				if (!array_key_exists(RSM_RDDS, $tld) || !array_key_exists('subservices', $tld[RSM_RDDS])) {
					unset($data['tld'][$key]);
					continue;
				}

				$subservices = $tld[RSM_RDDS]['subservices'];
				$available = false;

				if ($data['filter_rdap_subgroup'] && array_key_exists(RSM_RDAP_TLD_ENABLED, $subservices) && $subservices[RSM_RDAP_TLD_ENABLED]) {
					$available = true;
				}
				elseif ($data['filter_rdds43_subgroup'] && array_key_exists(RSM_TLD_RDDS43_ENABLED, $subservices) && $subservices[RSM_TLD_RDDS43_ENABLED]) {
					$available = true;
				}
				elseif ($data['filter_rdds80_subgroup'] && array_key_exists(RSM_TLD_RDDS80_ENABLED, $subservices) && $subservices[RSM_TLD_RDDS80_ENABLED]) {
					$available = true;
				}

				if (!$available) {
					unset($data['tld'][$key]);
					continue;
				}
			}
		}

		foreach ($tlds_by_server as $key => $hosts) {
			multiDBconnect($DB['SERVERS'][$key], $error);
			foreach ($hosts as $hostid => $host) {
				$tld_key = $DB['SERVERS'][$key]['NR'].$hostid;
				if (array_key_exists($tld_key, $data['tld'])) {
					$false_positive = true;
					if (array_key_exists(RSM_DNS, $data['tld'][$tld_key])
							&& array_key_exists('incident', $data['tld'][$tld_key][RSM_DNS])) {
						$data['tld'][$tld_key][RSM_DNS]['incident'] = getLastEvent($data['tld'][$tld_key][RSM_DNS]['incident']);
						if ($data['tld'][$tld_key][RSM_DNS]['incident']) {
							$false_positive = false;
						}
					}
					if (array_key_exists(RSM_DNSSEC, $data['tld'][$tld_key])
							&& array_key_exists('incident', $data['tld'][$tld_key][RSM_DNSSEC])) {
						$data['tld'][$tld_key][RSM_DNSSEC]['incident'] = getLastEvent($data['tld'][$tld_key][RSM_DNSSEC]['incident']);
						if ($data['tld'][$tld_key][RSM_DNSSEC]['incident']) {
							$false_positive = false;
						}
					}
					if (array_key_exists(RSM_RDDS, $data['tld'][$tld_key])
							&& array_key_exists('incident', $data['tld'][$tld_key][RSM_RDDS])) {
						$data['tld'][$tld_key][RSM_RDDS]['incident'] = getLastEvent($data['tld'][$tld_key][RSM_RDDS]['incident']);
						if ($data['tld'][$tld_key][RSM_RDDS]['incident']) {
							$false_positive = false;
						}
					}
					if (array_key_exists(RSM_RDAP, $data['tld'][$tld_key])
							&& array_key_exists('incident', $data['tld'][$tld_key][RSM_RDAP])) {
						$data['tld'][$tld_key][RSM_RDAP]['incident'] = getLastEvent($data['tld'][$tld_key][RSM_RDAP]['incident']);
						if ($data['tld'][$tld_key][RSM_RDAP]['incident']) {
							$false_positive = false;
						}
					}
					if (array_key_exists(RSM_EPP, $data['tld'][$tld_key])
							&& array_key_exists('incident', $data['tld'][$tld_key][RSM_EPP])) {
						$data['tld'][$tld_key][RSM_EPP]['incident'] = getLastEvent($data['tld'][$tld_key][RSM_EPP]['incident']);
						if ($data['tld'][$tld_key][RSM_EPP]['incident']) {
							$false_positive = false;
						}
					}

					if ($data['filter_status'] == 1 && $false_positive) {
						unset($data['tld'][$tld_key]);
					}
				}
			}
		}
	}

	protected function doAction() {
		global $DB;

		$data = [
			'ajax_request' => $this->isAjaxRequest(),
			'refresh' => CWebUser::$data['refresh'] ? timeUnitToSeconds(CWebUser::$data['refresh']) : null,
			'module_style' => $this->module->getStyle(),
		];
		$data['rsm_monitoring_mode'] = get_rsm_monitoring_type();
		$data['title'] = ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRAR)
			? _('Registrar rolling week status')
			: _('TLD Rolling week status');

		$this->updateProfile();
		$this->readValues($data);
		$this->readMacros($data);

		$master = $DB;
		$no_history = !($data['filter_status'] != 0 || $data['filter_slv'] != 0 || $data['filter_dns']
				|| $data['filter_dnssec'] || $data['filter_rdds'] || $data['filter_rdap'] || $data['filter_epp']);

		$this->fetchData($data);
		$this->selectTLDAttributes($data);

		unset($DB['DB']);
		$DB = $master;
		DBconnect($error);

		if (!$no_history) {
			order_result($data['tld'], $data['sort_field'], $data['sort_order']);

			$data['paging'] = CPagerHelper::paginate($this->getInput('page', 1), $data['tld'], ZBX_SORT_UP, new CUrl());
		}

		$response = new CControllerResponseData($data);
		$response->setTitle($data['title']);
		$this->setResponse($response);
	}
}
