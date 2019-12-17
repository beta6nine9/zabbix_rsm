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
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/


// Load JS files.
$this->addJsFile('flickerfreescreen.js');
$this->addJsFile('gtlc.js');
$this->addJsFile('class.calendar.js');

$object_label = ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRAR) ? _('Registrar ID') : _('TLD');

// Make info block.
if ($data['tld']) {
	$details = [$object_label => $data['tld']['host']];

	if ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRAR) {
		$details += [
			_('Registrar name') => $data['tld']['info_1'],
			_('Registrar family') => $data['tld']['info_2']
		];
	}

	$details += [
		_('Period') => implode(' - ', [
			date(DATE_TIME_FORMAT, zbxDateToTime($data['from_ts'])),
			date(DATE_TIME_FORMAT, zbxDateToTime($data['to_ts']))
		]),
		_('Server') => new CLink($data['server'],
			(new CUrl($data['url'].'zabbix.php'))->setArgument('action', 'rsm.rollingweekstatus')
		)
	];

	$info_block = (new CDiv())
		->addClass(ZBX_STYLE_TABLE_FORMS_CONTAINER)
		->addItem(gen_details_item($details));
}
else {
	$info_block = null;
}

// Make incident page table.
$headers = [
	_('Incident ID'),
	_('Status'),
	_('Start time'),
	_('End time'),
	_('Failed tests within incident'),
	_('Total number of tests')
];

if ($data['tld']) {
	$tabs_map = ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRAR)
		? [RSM_RDDS, RSM_RDAP]
		: [RSM_DNS, RSM_DNSSEC, RSM_RDDS, RSM_EPP, RSM_RDAP];

	$incident_page = (new CTabView(['remember' => true]))->setSelected(array_search($data['type'], $tabs_map));

	// DNS
	if ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRAR) {
		$dns_tab = null;
	}
	elseif (isset($data['dns']['events'])) {
		$dns_table = (new CTableInfo())
			->setNoDataMessage(_('No incidents found.'))
			->setHeader($headers);
		$delay_time = $data['dns']['delay'];

		foreach ($data['dns']['events'] as $event) {
			$incident_status = getIncidentStatus($event['false_positive'], $event['status']);
			$start_time = date(DATE_TIME_FORMAT_SECONDS, $event['startTime'] - $event['startTime'] % $delay_time);
			$end_time = array_key_exists('endTime', $event)
				? date(DATE_TIME_FORMAT_SECONDS, $event['endTime'] - $event['endTime'] % $delay_time + $delay_time - 1)
				: '-';

			$dns_table->addRow([
				new CLink(
					$event['eventid'],
					(new CUrl($data['url'].'zabbix.php'))
						->setArgument('action', 'rsm.incidentdetails')
						->setArgument('host', $data['tld']['host'])
						->setArgument('eventid', $event['eventid'])
						->setArgument('slvItemId', $data['dns']['itemid'])
						->setArgument('eventid', $event['eventid'])
						->setArgument('from', $start_time)
						->setArgument('to', array_key_exists('endTime', $event) ? $end_time : 'now')
						->setArgument('availItemId', $data['dns']['availItemId'])
						->setArgument('filter_set', 1)
						->setArgument('sid', $data['sid'])
						->setArgument('set_sid', 1)
				),
				$incident_status,
				$start_time,
				$end_time,
				$event['incidentFailedTests'],
				$event['incidentTotalTests']
			]);
		}

		$tests_down = new CLink(
			$data['dns']['totalTests'],
			(new CUrl($data['url'].'zabbix.php'))
				->setArgument('action', 'rsm.tests')
				->setArgument('from', $data['from'])
				->setArgument('to', $data['to'])
				->setArgument('filter_set', 1)
				->setArgument('host', $data['tld']['host'])
				->setArgument('type', RSM_DNS)
				->setArgument('slvItemId', $data['dns']['itemid'])
				->setArgument('sid', $data['sid'])
				->setArgument('set_sid', 1)
		);

		$tests_info = [
			bold(_('Tests are down')),
			':',
			SPACE,
			$tests_down,
			SPACE,
			_n('test', 'tests', $data['dns']['totalTests']),
			SPACE,
			'('._s(
				'%1$s in incidents, %2$s outside incidents',
				$data['dns']['inIncident'],
				$data['dns']['totalTests'] - $data['dns']['inIncident']
			).')'
		];

		$details = new CSpan([
			bold(_('Incidents')),
			':',
			SPACE,
			isset($data['dns']) ? count($data['dns']['events']) : 0,
			BR(),
			$tests_info,
			BR(),
			[[bold(_('SLA')), ':'.SPACE], convert_units(['value' => $data['dns']['slaValue'], 'units' => 's'])],
			BR(),
			[[bold(_('Frequency/delay')), ':'.SPACE], convert_units(['value' => $data['dns']['delay'], 'units' => 's'])]
		]);

		$rolling_week = is_null($data['dns']['slvTestTime']) ? [] : [
			(new CSpan(_s('%1$s Rolling week status', $data['dns']['slv'].'%')))->addClass('rolling-week-status'),
			BR(),
			(new CSpan(date(DATE_TIME_FORMAT, $data['dns']['slvTestTime'])))->addClass('rsm-date-time')
		];

		$dns_tab = (new CDiv())
			->additem((new CTable())
				->addClass('incidents-info')
				->addRow([$details, $rolling_week])
			)
			->additem($dns_table);
	}
	else {
		$dns_tab = (new CDiv())->additem(new CDiv(bold(_('Incorrect TLD configuration.')), 'red center'));
	}

	// DNSSEC
	if ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRAR) {
		$dnssec_tab = null;
	}
	elseif (isset($data['dnssec']['events'])) {
		$dnssec_table = (new CTableInfo())
			->setNoDataMessage(_('No incidents found.'))
			->setHeader($headers);
		$delay_time = $data['dnssec']['delay'];

		foreach ($data['dnssec']['events'] as $event) {
			$incident_status = getIncidentStatus($event['false_positive'], $event['status']);
			$start_time = date(DATE_TIME_FORMAT_SECONDS, $event['startTime'] - $event['startTime'] % $delay_time);
			$end_time = array_key_exists('endTime', $event)
				? date(DATE_TIME_FORMAT_SECONDS, $event['endTime'] - $event['endTime'] % $delay_time + $delay_time - 1)
				: '-';

			$dnssec_table->addRow([
				new CLink(
					$event['eventid'],
					(new CUrl($data['url'].'zabbix.php'))
						->setArgument('action', 'rsm.incidentdetails')
						->setArgument('host', $data['tld']['host'])
						->setArgument('eventid', $event['eventid'])
						->setArgument('slvItemId', $data['dnssec']['itemid'])
						->setArgument('from', $start_time)
						->setArgument('to', array_key_exists('endTime', $event) ? $end_time : 'now')
						->setArgument('availItemId', $data['dnssec']['availItemId'])
						->setArgument('filter_set', 1)
						->setArgument('sid', $data['sid'])
						->setArgument('set_sid', 1)
				),
				$incident_status,
				$start_time,
				$end_time,
				$event['incidentFailedTests'],
				$event['incidentTotalTests']
			]);
		}

		$tests_down = new CLink(
			$this->data['dnssec']['totalTests'],
			(new CUrl($data['url'].'zabbix.php'))
				->setArgument('action', 'rsm.tests')
				->setArgument('from', $data['from'])
				->setArgument('to', $data['to'])
				->setArgument('filter_set', 1)
				->setArgument('host', $data['tld']['host'])
				->setArgument('type', RSM_DNSSEC)
				->setArgument('slvItemId', $data['dnssec']['itemid'])
				->setArgument('sid', $data['sid'])
				->setArgument('set_sid', 1)
		);

		$tests_info = [
			bold(_('Tests are down')),
			':',
			SPACE,
			$tests_down,
			SPACE,
			_n('test', 'tests', $data['dnssec']['totalTests']),
			SPACE,
			'('._s(
				'%1$s in incidents, %2$s outside incidents',
				$data['dnssec']['inIncident'],
				$data['dnssec']['totalTests'] - $data['dnssec']['inIncident']
			).')'
		];

		$details = new CSpan([
			bold(_('Incidents')),
			':',
			SPACE,
			isset($data['dnssec']) ? count($data['dnssec']['events']) : 0,
			BR(),
			$tests_info,
			BR(),
			[[bold(_('SLA')), ':'.SPACE], convert_units(['value' => $data['dnssec']['slaValue'], 'units' => 's'])],
			BR(),
			[[bold(_('Frequency/delay')), ':'.SPACE], convert_units(['value' => $data['dnssec']['delay'], 'units' => 's'])]
		]);

		$rolling_week = is_null($data['dnssec']['slvTestTime']) ? [] : [
			(new CSpan(_s('%1$s Rolling week status', $data['dnssec']['slv'].'%')))->addClass('rolling-week-status'),
			BR(),
			(new CSpan(date(DATE_TIME_FORMAT, $data['dnssec']['slvTestTime'])))->addClass('rsm-date-time')
		];

		$dnssec_tab = (new CDiv())
			->additem((new CTable())
				->addRow([$details, $rolling_week])
				->addClass('incidents-info')
			)
			->additem($dnssec_table);
	}
	else {
		$dnssec_tab = (new CDiv())->additem(new CDiv(bold(_('DNSSEC is disabled.')), 'red center'));
	}

	// RDDS
	if (isset($data['rdds']['events'])) {
		$rdds_table = (new CTableInfo())
			->setNoDataMessage(_('No incidents found.'))
			->setHeader($headers);
		$delay_time = $data['rdds']['delay'];

		foreach ($data['rdds']['events'] as $event) {
			$incident_status = getIncidentStatus($event['false_positive'], $event['status']);
			$start_time = date(DATE_TIME_FORMAT_SECONDS, $event['startTime'] - $event['startTime'] % $delay_time);
			$end_time = array_key_exists('endTime', $event)
				? date(DATE_TIME_FORMAT_SECONDS, $event['endTime'] - $event['endTime'] % $delay_time + $delay_time - 1)
				: '-';

			$rdds_table->addRow([
				new CLink(
					$event['eventid'],
					(new CUrl($data['url'].'zabbix.php'))
						->setArgument('action', 'rsm.incidentdetails')
						->setArgument('host', $data['tld']['host'])
						->setArgument('eventid', $event['eventid'])
						->setArgument('slvItemId', $data['rdds']['itemid'])
						->setArgument('from', $start_time)
						->setArgument('to', array_key_exists('endTime', $event) ? $end_time : 'now')
						->setArgument('availItemId', $data['rdds']['availItemId'])
						->setArgument('filter_set', 1)
						->setArgument('sid', $data['sid'])
						->setArgument('set_sid', 1)
				),
				$incident_status,
				$start_time,
				$end_time,
				$event['incidentFailedTests'],
				$event['incidentTotalTests']
			]);
		}

		$tests_down = new CLink(
			$data['rdds']['totalTests'],
			(new CUrl($data['url'].'zabbix.php'))
				->setArgument('action', 'rsm.tests')
				->setArgument('from', $data['from'])
				->setArgument('to', $data['to'])
				->setArgument('filter_set', 1)
				->setArgument('host', $data['tld']['host'])
				->setArgument('type', RSM_RDDS)
				->setArgument('slvItemId', $data['rdds']['itemid'])
				->setArgument('sid', $data['sid'])
				->setArgument('set_sid', 1)
		);

		$tests_info = [
			bold(_('Tests are down')),
			':',
			SPACE,
			$tests_down,
			SPACE,
			_n('test', 'tests', $data['rdds']['totalTests']),
			SPACE,
			'('._s(
				'%1$s in incidents, %2$s outside incidents',
				$data['rdds']['inIncident'],
				$data['rdds']['totalTests'] - $data['rdds']['inIncident']
			).')'
		];

		$details = new CSpan([
			bold(_('Incidents')),
			':',
			SPACE,
			isset($data['rdds']) ? count($data['rdds']['events']) : 0,
			BR(),
			$tests_info,
			BR(),
			[[bold(_('SLA')), ':'.SPACE], convert_units(['value' => $data['rdds']['slaValue'], 'units' => 's'])],
			BR(),
			[[bold(_('Frequency/delay')), ':'.SPACE], convert_units(['value' => $data['rdds']['delay'], 'units' => 's'])]
		]);

		$rolling_week = is_null($data['rdds']['slvTestTime']) ? [] : [
			(new CSpan(_s('%1$s Rolling week status', $data['rdds']['slv'].'%')))->addClass('rolling-week-status'),
			BR(),
			(new CSpan(date(DATE_TIME_FORMAT, $data['rdds']['slvTestTime'])))->addClass('rsm-date-time')
		];

		$rdds_tab = (new CDiv())
			->additem((new CTable())
				->addClass('incidents-info')
				->addRow([$details, $rolling_week])
			)
			->additem($rdds_table);
	}
	else {
		$rdds_tab = (new CDiv())->additem(new CDiv(bold(_('RDDS is disabled.')), 'red center'));
	}

	// RDAP
	if (isset($data['rdap']['events'])) {
		if ($data['rdap_standalone_start_ts'] > 0) {
			$rdap_tab->additem(new CDiv(bold(_s('RDAP was not a standalone service before %s.',
				date(DATE_TIME_FORMAT, $data['rdap_standalone_start_ts'])
			))));
		}

		$rdap_tab = new CDiv();
		$rdap_table = (new CTableInfo())
			->setNoDataMessage(_('No incidents found.'))
			->setHeader($headers);
		$delay_time = $data['rdap']['delay'];

		foreach ($data['rdap']['events'] as $event) {
			$incident_status = getIncidentStatus($event['false_positive'], $event['status']);
			$start_time = date(DATE_TIME_FORMAT_SECONDS, $event['startTime'] - $event['startTime'] % $delay_time);
			$end_time = array_key_exists('endTime', $event)
				? date(DATE_TIME_FORMAT_SECONDS, $event['endTime'] - $event['endTime'] % $delay_time + $delay_time - 1)
				: '-';

			$rdap_table->addRow([
				new CLink(
					$event['eventid'],
					(new CUrl($data['url'].'zabbix.php'))
						->setArgument('action', 'rsm.incidentdetails')
						->setArgument('host', $data['tld']['host'])
						->setArgument('eventid', $event['eventid'])
						->setArgument('slvItemId', $data['rdap']['itemid'])
						->setArgument('from', $start_time)
						->setArgument('to', array_key_exists('endTime', $event) ? $end_time : 'now')
						->setArgument('availItemId', $data['rdap']['availItemId'])
						->setArgument('filter_set', 1)
						->setArgument('sid', $data['sid'])
						->setArgument('set_sid', 1)
				),
				$incident_status,
				$start_time,
				$end_time,
				$event['incidentFailedTests'],
				$event['incidentTotalTests']
			]);
		}

		$tests_down = new CLink(
			$data['rdap']['totalTests'],
			(new CUrl($data['url'].'zabbix.php'))
				->setArgument('action', 'rsm.tests')
				->setArgument('filter_from', $data['from'])
				->setArgument('to', $data['to'])
				->setArgument('filter_set', 1)
				->setArgument('host', $data['tld']['host'])
				->setArgument('type', RSM_RDAP)
				->setArgument('slvItemId', $data['rdap']['itemid'])
				->setArgument('sid', $data['sid'])
				->setArgument('set_sid', 1)
		);

		$tests_info = [
			bold(_('Tests are down')),
			':',
			SPACE,
			$tests_down,
			SPACE,
			_n('test', 'tests', $data['rdap']['totalTests']),
			SPACE,
			'('._s(
				'%1$s in incidents, %2$s outside incidents',
				$data['rdap']['inIncident'],
				$data['rdap']['totalTests'] - $data['rdap']['inIncident']
			).')'
		];

		$details = new CSpan([
			bold(_('Incidents')),
			':',
			SPACE,
			isset($data['rdap']) ? count($data['rdap']['events']) : 0,
			BR(),
			$tests_info,
			BR(),
			[[bold(_('SLA')), ':'.SPACE], convert_units(['value' => $data['rdap']['slaValue'], 'units' => 's'])],
			BR(),
			[[bold(_('Frequency/delay')), ':'.SPACE], convert_units(['value' => $data['rdap']['delay'], 'units' => 's'])]
		]);

		$rolling_week = is_null($data['rdap']['slvTestTime']) ? [] : [
			(new CSpan(_s('%1$s Rolling week status', $data['rdap']['slv'].'%')))->addClass('rolling-week-status'),
			BR(),
			(new CSpan(date(DATE_TIME_FORMAT, $data['rdap']['slvTestTime'])))->addClass('rsm-date-time')
		];

		$rdap_tab
			->additem((new CTable())
				->addRow([$details, $rolling_week])
				->addClass('incidents-info')
			)
			->additem($rdap_table);
	}
	else {
		$message = is_RDAP_standalone($data['from_ts'])
			? _('RDAP is disabled.')
			: _('RDAP is not a standalone service.');

		$rdap_tab = (new CDiv())->additem(new CDiv(bold($message), 'red center'));
	}

	// EPP
	if ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRAR) {
		$epp_tab = null;
	}
	elseif (isset($data['epp']['events'])) {
		$epp_table = (new CTableInfo())
			->setNoDataMessage(_('No incidents found.'))
			->setHeader($headers);
		$delay_time = $data['epp']['delay'];

		foreach ($data['epp']['events'] as $event) {
			$incident_status = getIncidentStatus($event['false_positive'], $event['status']);

			$start_time = date(DATE_TIME_FORMAT_SECONDS, $event['startTime'] - $event['startTime'] % $delay_time);
			$end_time = array_key_exists('endTime', $event)
				? date(DATE_TIME_FORMAT_SECONDS, $event['endTime'] - $event['endTime'] % $delay_time + $delay_time - 1)
				: '-';

			$epp_table->addRow([
				new CLink(
					$event['eventid'],
					(new CUrl($data['url'].'zabbix.php'))
						->setArgument('action', 'rsm.incidentdetails')
						->setArgument('host', $data['tld']['host'])
						->setArgument('from', $start_time)
						->setArgument('to', array_key_exists('endTime', $event) ? $end_time : 'now')
						->setArgument('availItemId', $data['epp']['availItemId'])
						->setArgument('filter_set', 1)
						->setArgument('sid', $data['sid'])
						->setArgument('set_sid', 1)
				),
				$incident_status,
				$start_time,
				$end_time,
				$event['incidentFailedTests'],
				$event['incidentTotalTests']
			]);
		}

		$tests_down = new CLink(
			$data['epp']['totalTests'],
			(new CUrl($data['url'].'zabbix.php'))
				->setArgument('action', 'rsm.tests')
				->setArgument('from', $data['from'])
				->setArgument('to', $data['to'])
				->setArgument('filter_set', 1)
				->setArgument('host', $data['tld']['host'])
				->setArgument('type', RSM_EPP)
				->setArgument('slvItemId', $data['epp']['itemid'])
				->setArgument('sid', $data['sid'])
				->setArgument('set_sid', 1)
		);

		$tests_info = [
			bold(_('Tests are down')),
			':',
			SPACE,
			$tests_down,
			SPACE,
			_n('test', 'tests', $data['epp']['totalTests']),
			SPACE,
			'('._s(
				'%1$s in incidents, %2$s outside incidents',
				$data['epp']['inIncident'],
				$data['epp']['totalTests'] - $data['epp']['inIncident']
			).')'
		];

		$details = new CSpan([
			bold(_('Incidents')),
			':',
			SPACE,
			isset($data['epp']) ? count($data['epp']['events']) : 0,
			BR(),
			$tests_info,
			BR(),
			[[bold(_('SLA')), ':'.SPACE], convert_units(['value' => $data['epp']['slaValue'], 'units' => 's'])],
			BR(),
			[[bold(_('Frequency/delay')), ':'.SPACE], convert_units(['value' => $data['epp']['delay'], 'units' => 's'])]
		]);

		$rolling_week = is_null($data['epp']['slvTestTime']) ? [] : [
			(new CSpan(_s('%1$s Rolling week status', $data['epp']['slv'].'%')))->addClass('rolling-week-status'),
			BR(),
			(new CSpan(date(DATE_TIME_FORMAT, $data['epp']['slvTestTime'])))->addClass('rsm-date-time')
		];

		$epp_tab = (new CDiv())
			->additem((new CTable())
				->addRow([$details, $rolling_week])
				->addClass('incidents-info'))
			->additem($epp_table);
	}
	else {
		$epp_tab = (new CDiv())->additem(new CDiv(bold(_('EPP is disabled.')), 'red center'));
	}

	if ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRAR) {
		$incident_page->addTab('rddsTab', _('RDDS'), $rdds_tab);
		$incident_page->addTab('rdapTab', _('RDAP'), $rdap_tab);
	}
	else {
		$incident_page->addTab('dnsTab', _('DNS'), $dns_tab);
		$incident_page->addTab('dnssecTab', _('DNSSEC'), $dnssec_tab);
		$incident_page->addTab('rddsTab', _('RDDS'), $rdds_tab);
		$incident_page->addTab('eppTab', _('EPP'), $epp_tab);
		$incident_page->addTab('rdapTab', _('RDAP'), $rdap_tab);
	}
}
else {
	$incident_page = new CTableInfo(_('No TLD defined.'));
}

// Assemble everything together.
$filter_url = (new CUrl($data['url'].'zabbix.php'))->setArgument('action', 'rsm.incidents');

(new CWidget())
	->setTitle($data['title'])
	->addItem((new CFilter($filter_url))
		->setProfile($data['profileIdx'])
		->setActiveTab($data['active_tab'])
		->addTimeSelector($data['from'], $data['to'])
		->addFilterTab(_('Filter'), [(new CFormList())->addRow($object_label,
			(new CTextBox('filter_search', $data['filter_search']))
				->setWidth(ZBX_TEXTAREA_FILTER_SMALL_WIDTH)
				->setAttribute('autocomplete', 'off')
			)
		])
		->addVar('action', 'rsm.incidents')
	)
	->addItem($info_block)
	->addItem($incident_page)
	->show();

// Initialize time control.
$tc_obj_data = [
	'id' => 'timeline_1',
	'domid' => 'incidents',
	'loadSBox' => 0,
	'loadImage' => 0,
	'dynamic' => 0,
	'mainObject' => 1
];

$filter = [
	'timeline' => [
		'profileIdx' => 'web.avail_report.filter',
		'profileIdx2' => 0,
		'from' => $data['from'],
		'to' => $data['to'],
		'from_ts' => $data['from_ts'],
		'to_ts' => $data['to_ts']
	],
	'active_tab' => $data['active_tab']
];

$this->addPostJS(
	'timeControl.addObject("incidents", '.CJs::encodeJson($filter).', '.CJs::encodeJson($tc_obj_data).');'.
	'timeControl.processObjects();'
);
