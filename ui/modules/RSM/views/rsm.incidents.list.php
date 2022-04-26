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
use Modules\RSM\Helpers\TabView;

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
	$incident_page = (new TabView(['remember' => true]))
		->setCookieName('incidents_tab')
		->setSelected($data['incidents_tab']);

	// We need specific order of the tabs.
	$services = array();

	if ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRY) {
		$services[] = 'dns';
		$services[] = 'dnssec';
	}

	$services[] = 'rdds';

	if ($data['rdap_standalone_start_ts'] != 0)
		$services[] = 'rdap';

	$services[] = 'epp';

	foreach ($services as $service) {
		if (!array_key_exists($service, $data['services']))
			continue;

		$service_data = $data['services'][$service];

		if (array_key_exists('events', $service_data)) {
			$table = (new CTableInfo())
				->setNoDataMessage(_('No incidents found.'))
				->setHeader($headers);

			$delay_time = (array_key_exists('delay', $service_data) ? $service_data['delay'] : 60);

			foreach ($service_data['events'] as $event) {
				$incident_status = getIncidentStatus($event['false_positive'], $event['status']);
				$start_time = date(DATE_TIME_FORMAT_SECONDS, $event['startTime'] - $event['startTime'] % $delay_time);
				$end_time = array_key_exists('endTime', $event)
					? date(DATE_TIME_FORMAT_SECONDS, $event['endTime'] - $event['endTime'] % $delay_time + $delay_time - 1)
					: '-';

				$table->addRow([
					new CLink(
						$event['eventid'],
						Url::getFor($data['url'], 'rsm.incidentdetails', [
							'host'        => $data['tld']['host'],
							'eventid'     => $event['eventid'],
							'slvItemId'   => $service_data['itemid'],
							'eventid'     => $event['eventid'],
							'availItemId' => $service_data['availItemId'],
						])
					),
					$incident_status,
					$start_time,
					$end_time,
					$event['incidentFailedTests'],
					$event['incidentTotalTests']
				]);
			}

			$tests_down =
				array_key_exists('itemid', $service_data)
				? new CLink(
					$service_data['totalTests'],
					Url::getFor($data['url'], 'rsm.tests', [
						'host'       => $data['tld']['host'],
						'from'       => $data['from'],
						'to'         => $data['to'],
						'filter_set' => 1,
						'type'       => $data['type'],
						'slvItemId'  => $service_data['itemid'],
					]))
				: 0;

			$tests_info = [
				bold(_('Tests are down')),
				':',
				SPACE,
				$tests_down,
				SPACE,
				_n('test', 'tests', $service_data['totalTests']),
				SPACE,
				'('._s(
					'%1$s in incidents, %2$s outside incidents',
					$service_data['inIncident'],
					$service_data['totalTests'] - $service_data['inIncident']
				).')'
			];

			$rolling_week = !array_key_exists('slvTestTime', $service_data)
				? []
				: [
					(new CSpan(_s('%1$s Rolling week status', $service_data['slv'].'%')))->addClass('rolling-week-status'),
					BR(),
					(new CSpan(date(DATE_TIME_FORMAT, $service_data['slvTestTime'])))->addClass('rsm-date-time')
			];

			$sla   = (array_key_exists('slaValue', $service_data) ? convertUnits(['value' => $service_data['slaValue'], 'units' => 's']) : '');
			$delay = (array_key_exists('slaValue', $service_data) ? convertUnits(['value' => $service_data['delay'],    'units' => 's']) : '');

			$details = new CSpan([
				bold(_('Incidents')),
				':',
				SPACE,
				array_key_exists('events', $service_data) ? count($service_data['events']) : 0,
				BR(),
				$tests_info,
				BR(),
				[[bold(_('SLA')), ':'.SPACE], $sla],
				BR(),
				[[bold(_('Frequency/delay')), ':'.SPACE], $delay]
			]);

			$tab = new CDiv();

			if ($service == 'rdap' && $data['rdap_standalone_start_ts'] > 0) {
				$tab->additem(new CDiv(bold(_s('RDAP was not a standalone service before %s.',
					date(DATE_TIME_FORMAT, $data['rdap_standalone_start_ts'])
				))));
			}

			$tab->additem((new CTable())
					->addClass('incidents-info')
					->addRow([$details, $rolling_week])
				)
				->additem($table);
		}
		else {
			$tab = (new CDiv())->additem(new CDiv(bold(_(strtoupper($service) . ' is disabled.')), 'red center'));
		}

		($tab === null) || $incident_page->addTab($service . 'Tab', _(strtoupper($service)), $tab);
	}
}
else {
	$incident_page = new CTableInfo(_('No Rsmhost defined.'));
}

// Assemble everything together.
$filter_buttons = (new CDiv())
	->addClass(ZBX_STYLE_FILTER_FORMS)
	->addItem((new CSubmitButton(_('Rolling week'), 'filter_set', 1)));

if ($data['ajax_request']) {
	$dynamic_node = new CDiv([
		$info_block,
		$incident_page,
		is_a($incident_page, CTabView::class) ? (new CScriptTag($incident_page->makeJavascript())) : null,
	]);
}
else {
	// Load JS files.
	$this->addJsFile('flickerfreescreen.js');
	$this->addJsFile('gtlc.js');
	$this->addJsFile('class.calendar.js');

	$dynamic_node = new DynamicContent([
		$info_block,
		$incident_page,
	]);
	$dynamic_node->refresh_seconds = $data['refresh'];
}

(new CWidget())
	->setTitle($data['title'])
	->addItem((new CFilter())
		->setProfile($data['profileIdx'])
		->setActiveTab($data['active_tab'])
		->hideFilterButtons()
		->addTimeSelector($data['from'], $data['to'])
		->addFilterTab(_('Filter'), [(new CFormList())->addRow($object_label,
			(new CTextBox('filter_search', $data['filter_search']))
				->setWidth(ZBX_TEXTAREA_FILTER_SMALL_WIDTH)
				->setAttribute('autocomplete', 'off')
			)
	], $filter_buttons)
		->addVar('action', 'rsm.incidents')
		->addVar('rolling_week', 1)
		->addVar('type', $data['type'])
	)
	->addItem($dynamic_node->setId('incidents_data'))
	->addItem($data['module_style'])
	->show();
