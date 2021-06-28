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


use Modules\RSM\Helpers\UrlHelper as URL;
use Modules\RSM\Helpers\DynamicContent;

$object_label = ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRAR) ? _('Registrar ID') : _('TLD');

$table = (new CTableInfo())->setHeader([
	_('Incident'),
	_('Time'),
	_('Result'),
	_('Historical rolling week value'),
	''
]);

foreach ($data['tests'] as $test) {
	if ($test['startEvent']) {
		$start_end_incident = _('Start time');
	}
	elseif ($test['endEvent']) {
		$start_end_incident = _('Resolved');
	}
	else {
		$start_end_incident = SPACE;
	}

	$details_link = new CLink(
		_('Details'),
		(new CUrl('zabbix.php'))
			->setArgument('action', ($data['type'] == RSM_DNS || $data['type'] == RSM_DNSSEC) ? 'rsm.aggregatedetails' : 'rsm.particulartests')
			->setArgument('slvItemId', $data['slvItemId'])
			->setArgument('host', $data['tld']['host'])
			->setArgument('time', $test['clock'])
			->setArgument('type', $data['type'])
	);

	$table->addRow([
		$start_end_incident,
		date(DATE_TIME_FORMAT_SECONDS, $test['clock']),
		array_key_exists($test['value'], $data['test_value_mapping'])
			? (new CSpan($data['test_value_mapping'][$test['value']]))
				->setAttribute('class', $test['value'] == PROBE_DOWN ? 'red' : 'green')
			: '',
		isset($test['slv']) ? $test['slv'].'%' : '-',
		$details_link
	]);
}

if ($data['incidentType'] == INCIDENT_ACTIVE) {
	$incident_type = _('Active');
	$change_incident_type = INCIDENT_FALSE_POSITIVE;
	$change_incident_type_label = _('Mark incident as false positive');
}
elseif ($data['incidentType'] == INCIDENT_RESOLVED) {
	$incident_type = _('Resolved');
	$change_incident_type = INCIDENT_FALSE_POSITIVE;
	$change_incident_type_label = _('Mark incident as false positive');
}
else {
	$incident_type = _('False positive');
	$change_incident_type = $data['active'] ? INCIDENT_ACTIVE : INCIDENT_RESOLVED;
	$change_incident_type_label = _('Unmark incident as false positive');
}

$mark_btn_on_click = (new CUrl('zabbix.php'))
	->setArgument('action', 'rsm.markincident')
	->setArgument('mark_as', $change_incident_type)
	->setArgument('eventid', $data['eventid'])
	->setArgument('host', $data['tld']['host'])
	->setArgument('type', $data['type'])
	->setArgument('from', $data['from'])
	->setArgument('to', $data['to'])
	->setArgument('slvItemId', $data['slvItemId'])
	->setArgument('availItemId', $data['availItemId'])
	->getUrl();

// Make info block.
$details = [
	$object_label => $data['tld']['host']
];
if ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRAR) {
	$details[_('Registrar name')] = $data['tld']['info_1'];
	$details[_('Registrar family')] = $data['tld']['info_2'];
}
$details[_('Service')] = $data['slvItem']['name'];
$details[_('Incident type')] = $incident_type;
if ($data['type'] == RSM_RDDS) {
	$details[_('Current testing interface')] = $data['testing_interfaces'];
}

// Make widget object.
$filter_url = Url::get('rsm.incidentdetails', [
	'host' => $data['host'],
	'eventid' => $data['eventid'],
	'slvItemId' => $data['slvItemId'],
	'availItemId' => $data['availItemId'],
	'filter_set' => 1,
]);
$filter_buttons = (new CDiv())
	->addClass(ZBX_STYLE_FILTER_FORMS)
	->addItem((new CSubmitButton(_('Rolling week'), 'rolling_week', 1)));
$dynamic_node = [
	(new CDiv())
		->addClass(ZBX_STYLE_TABLE_FORMS_CONTAINER)
		->addItem((new CTable())
			->addClass('incidents-info')
			->addRow([
				gen_details_item($details),
				($data['slvTestTime'] > 0)
					? [
						(new CSpan(_s('%1$s Rolling week status', $data['slv'].'%')))->addClass('rolling-week-status'),
						BR(),
						(new CSpan(date(DATE_TIME_FORMAT, $data['slvTestTime'])))->addClass('rsm-date-time'),
					]
					: null
			])
		),
	$data['paging'],
	$table,
	$data['paging'],
	in_array(CWebUser::getType(), [USER_TYPE_ZABBIX_ADMIN, USER_TYPE_SUPER_ADMIN, USER_TYPE_POWER_USER])
		? (new CButton('mark_incident', $change_incident_type_label))
			->onClick(sprintf('javascript: location.href = "%s";', $mark_btn_on_click))
			->addStyle('margin-top: 5px;')
		: null
];
if ($data['ajax_request']) {
	$dynamic_node = new CDiv($dynamic_node);
}
else {
	// Load JS files.
	$this->addJsFile('flickerfreescreen.js');
	$this->addJsFile('gtlc.js');
	$this->addJsFile('class.calendar.js');

	$dynamic_node = new DynamicContent($dynamic_node);
	$dynamic_node->refresh_seconds = $data['refresh'];
}

(new CWidget())
	->setTitle($data['title'])
	->addItem((new CFilter(new CUrl($filter_url)))
		->setProfile($data['profileIdx'])
		->setActiveTab($data['active_tab'])
		->addTimeSelector($data['from'], $data['to'])
		->hideFilterButtons()
		->addFilterTab(_('Filter'), [(new CFormList())->addRow('',
			(new CRadioButtonList('filter_failing_tests', (int) $data['filter_failing_tests']))
				->addValue(_('Only failing tests'), 1)
				->addValue(_('Show all'), 0)
				->onChange('$(this).closest("form").submit()')
				->setModern(true)
			)
		], $filter_buttons)
		->addVar('action', 'rsm.incidentdetails')
		->addVar('host', $data['host'])
		->addVar('eventid', $data['eventid'])
		->addVar('slvItemId', $data['slvItemId'])
		->addVar('availItemId', $data['availItemId'])
		->addVar('filter_set', 1)
	)
	->addItem($dynamic_node->setId('incident_details'))
	->addItem($data['module_style'])
	->show();
