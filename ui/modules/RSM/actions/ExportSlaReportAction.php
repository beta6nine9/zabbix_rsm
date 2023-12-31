<?php
/*
** Zabbix
** Copyright (C) 2001-2016 Zabbix SIA
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
use CSlaReport;
use DB;
use CControllerResponseFatal;

class ExportSlaReportAction extends Action {

	protected function checkInput() {
		$fields = [
			'filter_search' => 'required|db hosts.host',
			'filter_year' => 'required|int32|ge '.SLA_MONITORING_START_YEAR.'|le '.date('Y'),
			'filter_month' => 'required|in '.implode(',', range(1,12))
		];

		$ret = $this->validateInput($fields);
		if (!$ret) {
			$this->setResponse(new CControllerResponseFatal());
		}

		return $ret;
	}

	protected function fetchData(array &$data) {
		global $DB;

		foreach ($DB['SERVERS'] as $server_nr => $server) {
			if (!multiDBconnect($server, $error)) {
				continue;
			}

			$tld = API::Host()->get([
				'output' => ['hostid', 'host', 'status'],
				'selectItems' => ['itemid', 'key_', 'value_type'],
				'selectMacros' => ['macro', 'value'],
				'tlds' => true,
				'filter' => [
					'host' => $data['filter_search']
				]
			]);

			// TLD not found, proceed to search on another server.
			if ($tld) {
				$data['tld'] = $tld[0];
				$data['server_nr'] = $server_nr;
				break;
			}
		}
	}

	protected function getReport(array $data) {
		$is_current_month = (date('Yn') === $data['filter_year'].$data['filter_month']);
		$report_row = null;

		if (($data['tld']['status'] != HOST_STATUS_MONITORED && $is_current_month) || !$is_current_month) {
			// Searching for pregenerated SLA report in database.
			$report_row = DB::find('sla_reports', [
				'hostid' => $data['tld']['hostid'],
				'month'	 => $data['filter_month'],
				'year'	 => $data['filter_year']
			]);

			$report_row = reset($report_row);
		}
		elseif (class_exists('CSlaReport', true)) {
			$report_row = CSlaReport::generate($data['server_nr'], [$data['tld']['host']], $data['filter_year'],
				$data['filter_month'], ['XML']
			);

			if ($report_row) {
				$report_row = reset($report_row);
				$report_row += [
					'year' => $data['filter_year'],
					'month' => $data['filter_month'],
					'report_xml' => $report_row['report']['XML'],
				];
			}
		}

		return $report_row;
	}

	protected function doAction() {
		global $DB;

		$data = [
			'tld' => [],
			'filter_search' => $this->getInput('filter_search'),
			'filter_year' => (int) $this->getInput('filter_year'),
			'filter_month' => (int) $this->getInput('filter_month')
		];

		// Do filtering and data fetch.
		if ($data['filter_search']) {
			$master = $DB;
			$this->fetchData($data);
			$DB = $master;
		}

		if ($data['tld'] && ($report_row = $this->getReport($data)) !== null) {
			header('Content-Type: text/xml');
			header(sprintf('Content-disposition: attachment; filename="%s-%d-%s.xml"',
				$data['tld']['host'], $report_row['year'], getMonthCaption($report_row['month']))
			);

			echo $report_row['report_xml'];
			exit;
		}
	}
}
