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

class Probes extends Action {

	protected function checkInput() {
		return true;
	}

	#
	# The following data is used for displaying the status of each Probe.
	# We display the last values of 6 items that belong to 2 hosts.
	#
	# Host: <Probe> - mon
	# +----------------------------------+------------------------------+----------------+--------------+
	# | item key                         | item name                    | value          | item type    |
	# +----------------------------------+------------------------------+----------------+--------------+
	# | rsm.probe.online                 | Probe main status            | 1/0            | Trapper      |
	# | zabbix[proxy,<Probe>,lastaccess] | Availability of probe        | Unix timestamp | Internal     |
	# +----------------------------------+------------------------------+----------------+--------------+
	#
	# Host: <Probe>
	# +----------------------------------+------------------------------+----------------+--------------+
	# | item key                         | item name                    | value          | item type    |
	# +----------------------------------+------------------------------+----------------+--------------+
	# | resolver.status[<IP>,...]        | Local resolver status (<IP>) | 1/0            | Simple check |
	# | rsm.probe.status[automatic,...]  | Probe status (automatic)     | 1/0            | Simple check |
	# | rsm.probe.status[manual]         | Probe status (manual)        | 1/0            | Trapper      |
	# | rsm.errors                       | Internal error rate          | 0-N            | Simple check |
	# +----------------------------------+------------------------------+----------------+--------------+
	#

	protected function doAction() {
		// required for table style
		$data = [
			'module_style' => $this->module->getStyle(),
		];

		$data['title'] = 'Probe statuses';

		# get proxy names
		$proxies = API::Proxy()->get([
			'output' => ['host'],
			'filter' => ['status' => HOST_STATUS_PROXY_PASSIVE]
		]);

		$probe_names = [];

		foreach ($proxies as $proxy) {
			$probe_names[] = $proxy['host'];
		}

		$items = API::Item()->get([
			'output' => ['itemid', 'key_'],
			'selectHosts' => ['host'],
			'templated' => false,
			'search' => [
				'key_' => [
					'rsm.probe.online',
					'zabbix[proxy,{$RSM.PROXY_NAME},lastaccess]',
					'resolver.status[',
					'rsm.probe.status[automatic,',
					'rsm.probe.status[manual]',
					'rsm.errors'
				]
			],
			'startSearch' => true,
			'searchByAny' => true
		]);

		// for later translation of itemids to hosts and items
		$hosts_map = [];
		$items_map = [];

		foreach ($items as $i) {
			$host = $i['hosts'][0]['host'];

			if (strstr($host, ' - mon')) {
				$host = substr($host, 0, -strlen(' - mon'));
			}

			if (!in_array($host, $probe_names))
				continue;

			$hosts_map[$i['itemid']] = $host;

			if ($i['key_'] == 'rsm.probe.online') {
				$items_map[$i['itemid']] = 'mainstatus';
			} elseif ($i['key_'] == 'zabbix[proxy,{$RSM.PROXY_NAME},lastaccess]') {
				$items_map[$i['itemid']] = 'lastaccess';
			} elseif (strstr($i['key_'], 'resolver.status[')) {
				$items_map[$i['itemid']] = 'resolver';
			} elseif (strstr($i['key_'], 'rsm.probe.status[automatic,')) {
				$items_map[$i['itemid']] = 'automatic';
			} elseif ($i['key_'] == 'rsm.probe.status[manual]') {
				$items_map[$i['itemid']] = 'manual';
			} elseif ($i['key_'] == 'rsm.errors') {
				$items_map[$i['itemid']] = 'errors';
			} else {
				$items_map[$i['itemid']] = '*UNKNOWN*';
			}
		}

		$values = DBselect(
			'SELECT itemid,value,clock'.
			' FROM lastvalue'.
			' WHERE '.dbConditionString('itemid', array_keys($items_map))
		);

		$data['probes'] = array();

		while ($value = DBfetch($values)) {
			$data['probes'][$hosts_map[$value['itemid']]][$items_map[$value['itemid']]]['value'] = $value['value'];
			$data['probes'][$hosts_map[$value['itemid']]][$items_map[$value['itemid']]]['clock'] = $value['clock'];
		}

		if (!($lastaccess_limit = API::UserMacro()->get([
				'output' => ['value'],
				'filter' => ['macro' => RSM_PROBE_AVAIL_LIMIT],
				'globalmacro' => true
				]))) {
			error('global macro "' . RSM_PROBE_AVAIL_LIMIT . '" not found');
		}
		else {
			// give the limit a little time to avoid false-positives,
			// it's currently 60 seconds with item delay 60 seconds
			$data['lastaccess_limit'] = $lastaccess_limit[0]['value'] + 60;
		}

		if (!($probe_macros = API::UserMacro()->get([
				'output' => ['macro', 'value'],
				'search' => ['macro' => 'PROBE'],
				'globalmacro' => true
				]))) {
			error('cannot get global macros');
		}
		else {
			foreach ($probe_macros as $macro) {
				$data['probe_macros'][$macro['macro']] = $macro['value'];
			}
		}
		$data['generated_at'] = time();

		$response = new CControllerResponseData($data);
		$response->setTitle($data['title']);
		$this->setResponse($response);
	}
}
