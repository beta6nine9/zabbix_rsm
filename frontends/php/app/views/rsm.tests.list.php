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

$table = (new CTableInfo())
	->setHeader([
		_('Time'),
		_('Affects rolling week'),
		''
]);

foreach ($data['tests'] as $test) {
	if (!$test['incident']) {
		$affects_rolling_week = _('No');
	}
	elseif ($test['incident'] == 1) {
		$affects_rolling_week = _('Yes');
	}
	else {
		$affects_rolling_week = _('No / False positive');
	}

	$table->addRow([
		date(DATE_TIME_FORMAT_SECONDS, $test['clock']),
		$affects_rolling_week,
		new CLink(
			_('Details'),
			(new CUrl('zabbix.php'))
				->setArgument('action', 'rsm.particulartests')
				->setArgument('slvItemId', $data['slvItemId'])
				->setArgument('host', $data['tld']['host'])
				->setArgument('time', $test['clock'])
				->setArgument('type', $data['type'])
				->setArgument('slvItemId', $data['slvItemId'])
		)
	]);
}

if ($data['type'] == RSM_DNS) {
	$service_name = _('DNS service availability');
}
elseif ($data['type'] == RSM_DNSSEC) {
	$service_name = _('DNSSEC service availability');
}
elseif ($data['type'] == RSM_RDDS) {
	$service_name = _('RDDS service availability');
}
elseif ($data['type'] == RSM_RDAP) {
	$service_name = _('RDAP service availability');
}
else {
	$service_name = _('EPP service availability');
}



$object_info = ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRAR)
	? [
		new CSpan([bold(_('Registrar ID')), ': ', '', $data['tld']['host']]),
		BR(),
		new CSpan([bold(_('Registrar name')), ': ', '', $data['tld']['info_1']]),
		BR(),
		new CSpan([bold(_('Registrar family')), ': ', '', $data['tld']['info_2']])
	]
	: new CSpan([bold(_('TLD')), ': ', '', $data['tld']['host']]);

$filter_url = (new CUrl('zabbix.php'))->setArgument('action', 'rsm.tests');

(new CWidget())
	->setTitle($data['title'])
	->addItem((new CFilter($filter_url))
		->setProfile($data['profileIdx'])
		->setActiveTab($data['active_tab'])
		->addTimeSelector($data['from'], $data['to'])
		->addVar('action', 'rsm.tests')
	)
	->addItem((new CDiv(
		(new CTable())
			->addClass('incidents-info')
			->addRow([[
				$object_info,
				BR(),
				new CSpan([bold(_('Service')), ': ', '', $service_name])
			]])
			->addRow([[
				[
					(new CSpan([bold(_('Number of tests downtime')), ':', SPACE, $this->data['downTests']]))->addClass('first-row-element'),
					new CSpan([bold(_('Number of mimutes downtime')), ':', SPACE, $this->data['downTimeMinutes']])
				],
				BR(),
				[
					(new CSpan([bold(_('Number of state changes')), ':', SPACE, $this->data['statusChanges']]))->addClass('first-row-element'),
					new CSpan([bold(_('Total time within selected period')), ':', SPACE, convertUnitsS($this->data['downPeriod'])])
				]
			]])
	))->addClass('table-forms-container'))
	->addItem([$table, $data['paging']])
	->show();

// Initialize time control.
$tc_obj_data = [
	'id' => 'timeline_1',
	'domid' => 'tests',
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
