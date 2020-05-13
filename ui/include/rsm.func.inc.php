<?php
/*
** Zabbix
** Copyright (C) 2001-2020 Zabbix SIA
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
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/


/**
 * Return current type of RSM monitoring.
 *
 * @return int
 */
function get_rsm_monitoring_type() {
	static $type;

	if ($type === null) {
		$db_macro = API::UserMacro()->get([
			'output' => ['value'],
			'filter' => ['macro' => RSM_MONITORING_TARGET],
			'globalmacro' => true
		]);

		if ($db_macro) {
			$type = $db_macro[0]['value'];
		}
	}

	return $type;
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
function is_RDAP_standalone($timestamp = null) {
	static $rsm_rdap_standalone_ts;

	if (is_null($rsm_rdap_standalone_ts)) {
		$db_macro = API::UserMacro()->get([
			'output' => ['value'],
			'filter' => ['macro' => RSM_RDAP_STANDALONE],
			'globalmacro' => true
		]);

		$rsm_rdap_standalone_ts = $db_macro ? (int) $db_macro[0]['value'] : 0;
	}

	$timestamp = is_null($timestamp) ? time() : (int) $timestamp;

	return ($rsm_rdap_standalone_ts > 0 && $rsm_rdap_standalone_ts <= $timestamp);
}
