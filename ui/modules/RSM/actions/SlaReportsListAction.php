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

use DB;
use API;
use CUrl;
use CProfile;
use CWebUser;
use Exception;
use CSlaReport;
use SimpleXMLElement;
use CControllerResponseData;
use CControllerResponseFatal;
use Modules\RSM\Helpers\UrlHelper as URL;

class SlaReportsListAction extends Action {

	/**
	 * @var bool
	 */
	protected $filter_valid;

	public function init() {
		$this->filter_valid = true;
		parent::init();
	}

	protected function checkInput() {
		$fields = [
			'filter_set' =>		'in 1',
			'filter_rst' =>		'in 1',
			'filter_search' =>	'db hosts.host',
			'filter_year' =>	'string',
			'filter_month' =>	'string'
		];

		$ret = $this->validateInput($fields);
		if (!$ret) {
			$this->setResponse(new CControllerResponseFatal());
		}

		// Response is not rewritten here.
		if ($this->getInput('filter_year', '') > date('Y')
				|| ($this->getInput('filter_year', '') == date('Y') && $this->getInput('filter_month', '') > date('n'))) {
			error(_('Incorrect report period.'));
			$this->filter_valid = false;
		}

		return $ret;
	}

	protected function checkPermissions() {
		$valid_users = [USER_TYPE_READ_ONLY, USER_TYPE_ZABBIX_USER, USER_TYPE_POWER_USER, USER_TYPE_COMPLIANCE,
			USER_TYPE_ZABBIX_ADMIN, USER_TYPE_SUPER_ADMIN];

		if (!in_array($this->getUserType(), $valid_users))
			return false;

		// In the future we should consider adding the check if specified Rsmhost exist here.
		// Currently it's in fetchData() since we don't want to do the same job twice.
		return true;
	}

	protected function fetchData(array &$data) {
		global $DB;

		foreach ($DB['SERVERS'] as $server_nr => $server) {
			if (!multiDBconnect($server, $error)) {
				error(_($server['NAME'].': '.$error));
				continue;
			}

			$tld = API::Host()->get([
				'output' => ['hostid', 'host', 'name', 'status', 'info_1', 'info_2'],
				'selectItems' => ['itemid', 'key_', 'value_type'],
				'selectMacros' => ['macro', 'value'],
				'tlds' => true,
				'filter' => [
					'host' => $data['filter_search']
				]
			]);

			// TLD not found, proceed to search on another server.
			if (!$tld) {
				continue;
			}

			$data['tld'] = $tld[0];
			$data['url'] = $server['URL'];
			$data['server'] = $server['NAME'];
			$data['server_nr'] = $server_nr;

			break;
		}
	}

	protected function getReport($data) {
		$is_current_month = (date('Yn') === $data['filter_year'].$data['filter_month']);
		$report_row = null;

		if (($data['tld']['status'] != HOST_STATUS_MONITORED && $is_current_month) || !$is_current_month) {
			// Searching for pregenerated SLA report in database.
			$report_row = DB::find('sla_reports', [
				'hostid'	=> $data['tld']['hostid'],
				'month'		=> $data['filter_month'],
				'year'		=> $data['filter_year']
			]);

			$report_row = reset($report_row);
			if (!$report_row) {
				error(_('Report is not generated for requested month.'));
			}
		}
		elseif (!class_exists('CSlaReport', true)) {
			error(_('SLA Report generation file is missing.'));
		}
		else {
			// CSlaReport class file path: ./include/classes/services/CSlaReport.php
			$report_row = CSlaReport::generate($data['server_nr'], [$data['tld']['host']], $data['filter_year'],
				$data['filter_month'], ['XML']
			);

			if (!$report_row) {
				error(_s('Unable to generate XML report: %1$s', CSlaReport::$error));
				if ($is_current_month) {
					error(_('Please try again after 5 minutes.'));
				}
			}
			else {
				$report_row = reset($report_row);
				$report_row += ['year' => $data['filter_year'], 'month' => $data['filter_month']];

				$report_row['report_xml'] = &$report_row['report']['XML'];
			}
		}

		return $report_row;
	}

	protected function prepareReportData(array &$data, array $report_row = null) {
		$xml = null;
		if ($report_row && array_key_exists('report_xml', $report_row)) {
			try {
				$xml = new SimpleXMLElement($report_row['report_xml']);
			} catch (Exception $x) {
				error(_('Unable to parse XML report.'));
			}
		}

		if ($xml) {
			$details = $xml->attributes();

			if ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRY) {
				$ns_items = [];
				foreach ($xml->DNS->nsAvailability as $ns_item) {
					$attrs = $ns_item->attributes();
					$ns_items[] = [
						'from'	=> (int) $attrs->from,
						'to'	=> (int) $attrs->to,
						'host'	=> (string) $attrs->hostname,
						'ip'	=> (string) $attrs->ipAddress,
						'slv'	=> (string) $ns_item[0],
						'slr'	=> (string) $attrs->downtimeSLR
					];
				}

				$data += [
					'ns_items'	=> $ns_items,

					'slv_dns_downtime'				=> (string) $xml->DNS->serviceAvailability,
					'slr_dns_downtime'				=> (string) $xml->DNS->serviceAvailability->attributes()->downtimeSLR,

					'slv_dns_tcp_rtt_percentage'	=> (string) $xml->DNS->rttTCP,
					'slr_dns_tcp_rtt_percentage'	=> (String) $xml->DNS->rttTCP->attributes()->percentageSLR,
					'slr_dns_tcp_rtt_ms'			=> (string) $xml->DNS->rttTCP->attributes()->rttSLR,

					'slv_dns_udp_rtt_percentage'	=> (string) $xml->DNS->rttUDP,
					'slr_dns_udp_rtt_percentage'	=> (string) $xml->DNS->rttUDP->attributes()->percentageSLR,
					'slr_dns_udp_rtt_ms'			=> (string) $xml->DNS->rttUDP->attributes()->rttSLR,
				];
			}

			$data += [
				'details'	=> [
					'id'		=> (string) $details->id,
					'from'		=> (int) $details->reportPeriodFrom,
					'to'		=> (int) $details->reportPeriodTo,
					'generated'	=> (int) $details->generationDateTime
				],

				'slv_rdds_downtime'			=> (string) $xml->RDDS->serviceAvailability,
				'slr_rdds_downtime'			=> (string) $xml->RDDS->serviceAvailability->attributes()->downtimeSLR,

				'slv_rdds_rtt_percentage'	=> (string) $xml->RDDS->rtt,
				'slr_rdds_rtt_percentage'	=> (string) $xml->RDDS->rtt->attributes()->percentageSLR,
				'slr_rdds_rtt_ms'			=> (string) $xml->RDDS->rtt->attributes()->rttSLR
			];

			if (isset($xml->RDAP)) {
				if (!is_RDAP_standalone($data['details']['from'])) {
					error(_('RDAP values exists for time when service was not standalone.'));
				}

				$rdap = $xml->RDAP;

				$data += [
					'slv_rdap_downtime'			=> (string) $rdap->serviceAvailability,
					'slr_rdap_downtime'			=> (string) $rdap->serviceAvailability->attributes()->downtimeSLR,

					'slv_rdap_rtt_percentage'	=> (string) $rdap->rtt,
					'slr_rdap_rtt_percentage'	=> (string) $rdap->rtt->attributes()->percentageSLR,
					'slr_rdap_rtt_ms'			=> (string) $rdap->rtt->attributes()->rttSLR
				];
			}
			else if (is_RDAP_standalone($data['details']['from'])) {
				error(_('Cannot find RDAP values.'));
			}

			if ($data['tld']['host'] !== strval($data['details']['id'])) {
				error(_('Incorrect report tld value.'));
			}
		}
	}

	protected function doAction() {
		global $DB;

		$master = $DB;

		$data = [
			'title' => _('SLA report'),
			'module_style' => $this->module->getStyle(),
			'tld' => [],
			'url' => '',
			'filter_search' => $this->getInput('filter_search', ''),
			'filter_year' => (int) $this->getInput('filter_year', date('Y')),
			'filter_month' => (int) $this->getInput('filter_month', date('n')),
			'active_tab' => CProfile::get('web.rsm.slareports.filter.active', 1),
			'rsm_monitoring_mode' => get_rsm_monitoring_type(),
		];

		if ($this->hasInput('filter_rst')) {
			$data['filter_search'] = '';
			$data['filter_year'] = date('Y');
			$data['filter_month'] = date('n');
		}

		// Do filtering and data fetch.
		if ($data['filter_search']) {
			$this->fetchData($data);
		}

		if ($data['tld'] && $this->filter_valid) {
			if ($report_row = $this->getReport($data)) {
				$this->prepareReportData($data, $report_row);
			}

			if ($DB === $master) {
				$url = (new CUrl($data['url'].'zabbix.php'))->setArgument('action', 'rsm.rollingweekstatus');
			}
			else {
				$DB = $master;
				$url = Url::getFor($data['url'], 'rsm.rollingweekstatus', []);
			}

			$data['rolling_week_url'] = $url;
		}
		elseif ($this->filter_valid && $data['filter_search']) {
			$object_label = (get_rsm_monitoring_type() === MONITORING_TARGET_REGISTRAR) ? _('Registrar ID') : _('TLD');

			error(_s('%s "%s" does not exist or you do not have permissions to access it.', $object_label, $data['filter_search']));
		}

		$response = new CControllerResponseData($data);
		$response->setTitle($data['title']);
		$this->setResponse($response);
	}
}
