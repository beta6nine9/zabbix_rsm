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


$particular_proxys_table = (new CTableInfo())
	->setNoDataMessage(_('No particular proxy found.'))
	->setHeader([
		_('NS name'),
		_('IP'),
		_('Ms')
	]);

// List generation.
$current_ns = null;
foreach ($data['proxys'] as $proxy) {
	// Remove probe name from list.
	if ($proxy['ns'] === $current_ns) {
		$proxy['ns'] = SPACE;
	}
	else {
		$current_ns = $proxy['ns'];
	}

	if ($proxy['ms']) {
		if (!$data['minMs']) {
			$ms = $proxy['ms'];
		}
		elseif ($proxy['ms'] < $data['minMs']) {
			$ms = (new CSpan($proxy['ms']))->addClass('green');
		}
		else {
			$ms = (new CSpan($proxy['ms']))->addClass('red');
		}
	}
	else {
		$ms = '-';
	}

	$particular_proxys_table->addRow([
		$proxy['ns'],
		$proxy['ip'],
		$ms
	]);
}

$particular_proxys = [
	new CSpan([bold(_('TLD')), ': ', $data['tld']['name']]),
	BR(),
	new CSpan([bold(_('Service')), ': ', $data['slvItem']['name']]),
	BR(),
	new CSpan([bold(_('Test time')), ': ', date(DATE_TIME_FORMAT_SECONDS, $data['time'])]),
	BR(),
	new CSpan([bold(_('Probe')), ': ', $data['probe']['name']]),
];

if ($data['type'] == RSM_DNS) {
	if ($data['testResult'] == true) {
		$test_result = (new CSpan(_('Up')))->addClass('green');
	}
	elseif ($data['testResult'] == false) {
		$test_result = (new CSpan(_('Down')))->addClass('red');
	}
	else {
		$test_result = (new CSpan(_('No result')))->addClass('grey');
	}

	array_push($particular_proxys, [BR(),
		new CSpan([
			bold(_('Test result')),
			': ',
			$test_result
		])
	]);
}

(new CWidget())
	->setTitle($data['title'])
	->additem((new CTable())
		->addClass('incidents-info')
		->addRow([$particular_proxys])
		->addRow([[
			new CSpan([bold(_('Total number of NS')), ': ', $data['totalNs']], 'first-row-element'),
			BR(),
			new CSpan([bold(_('Number of NS with positive result')), ': ', $data['positiveNs']], 'second-row-element'),
			BR(),
			new CSpan([bold(_('Number of NS with negative result')), ': ', $data['totalNs'] - $data['positiveNs']])
		]])
	)
	->additem($particular_proxys_table)
	->show();
