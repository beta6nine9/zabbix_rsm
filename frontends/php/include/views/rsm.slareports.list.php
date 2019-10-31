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


$widget = (new CWidget())->setTitle(_('SLA report'));

$months = range(1, 12);
$years = range(SLA_MONITORING_START_YEAR, date('Y', time()));

$object_label = ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRAR) ? _('Registrar ID') : _('TLD');

$widget->addItem(
	(new CFilter(new CUrl('rsm.slareports.php')))->addFilterTab(_('Filter'),
		(new CFormList())
			->addVar('filter_set', 1)
			->addRow($object_label, (new CTextBox('filter_search', $data['filter_search']))
				->setWidth(ZBX_TEXTAREA_FILTER_STANDARD_WIDTH)
				->setAttribute('autocomplete', 'off')
			)
			->addRow(_('Period'), [
				new CComboBox('filter_month', $data['filter_month'], null, array_combine($months,
					array_map('getMonthCaption', $months))),
				SPACE,
				new CComboBox('filter_year', $data['filter_year'], null, array_combine($years, $years))
			])
	)
);

/*$widget->addItem(
	(new CFilter('web.rsm.slareports.filter.state'))->addColumn(
		(new CFormList())
			->addVar('filter_set', 1)
			->addRow($object_label, (new CTextBox('filter_search', $data['filter_search']))
				->setWidth(ZBX_TEXTAREA_FILTER_STANDARD_WIDTH)
				->setAttribute('autocomplete', 'off')
			)
			->addRow(_('Period'), [
				new CComboBox('filter_month', $data['filter_month'], null, array_combine($months,
					array_map('getMonthCaption', $months))),
				SPACE,
				new CComboBox('filter_year', $data['filter_year'], null, array_combine($years, $years))
			])
	)
);*/

$table = (new CTableInfo())->setHeader([
	_('Service'),
	_('FQDN and IP'),
	_('From'),
	_('To'),
	_('SLV'),
	_('Monthly SLR')
]);

// Return disabled "Download XML" button if nothing selected.
if (!array_key_exists('details', $data)) {
	return $widget->addItem([
		$table,
		(new CDiv())
			->addItem((new CButton('export', 'Download XML'))->setEnabled(false))
			->addClass('action-buttons')
	]);
}

// TLD details.
$date_from = date(DATE_TIME_FORMAT_SECONDS, zbxDateToTime($data['details']['from']));
$date_till = date(DATE_TIME_FORMAT_SECONDS, zbxDateToTime($data['details']['to']));
$date_generated = date(DATE_TIME_FORMAT_SECONDS, zbxDateToTime($data['details']['generated']));

$details = [$object_label => $data['tld']['host']];

if ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRAR) {
	$details += [
		_('Registrar name') => $data['tld']['info_1'],
		_('Registrar family') => $data['tld']['info_2']
	];
}

$details += [
	_('Period') => $date_from . ' - ' . $date_till,
	_('Generation time') => $date_generated,
	_('Server') => new CLink($data['server'], $data['rolling_week_url'])
];

$widget->additem((new CDiv())
	->addClass(ZBX_STYLE_TABLE_FORMS_CONTAINER)
	->addItem(gen_details_item($details))
);

// DNS Service Availability.
if ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRY) {
	$table->addRow([
			bold(_('DNS Service Availability')),
			'',
			'',
			'',
			_s('%d (minutes of downtime)', $data['slv_dns_downtime']),
			_s('%d (minutes of downtime)', $data['slr_dns_downtime'])
		],
		($data['slv_dns_downtime'] > $data['slr_dns_downtime']) ? 'red-bg' : null
	);

	// DNS Name Server Availability.
	foreach ($data['ns_items'] as $item) {
		$table->addRow([
				_('DNS Name Server Availability'),
				implode(', ', array_filter([$item['host'], $item['ip']], 'strlen')),
				date(DATE_TIME_FORMAT_SECONDS, zbxDateToTime($item['from'])),
				date(DATE_TIME_FORMAT_SECONDS, zbxDateToTime($item['to'])),
				_s('%1$s (minutes of downtime)', $item['slv']),
				_s('%1$s (minutes of downtime)', $item['slr'])
			],
			($item['slv'] > $item['slr']) ? 'red-bg' : null
		);
	}

	// DNS UDP/TCP Resolution RTT.
	$table->addRow([
			_('DNS UDP Resolution RTT'),
			'',
			'',
			'',
			_s('%1$s %% (queries <= %2$s ms)', $data['slv_dns_udp_rtt_percentage'],
				$data['slr_dns_udp_rtt_ms']
			),
			_s('<= %1$s ms, for at least %2$s %% of queries', $data['slr_dns_udp_rtt_ms'],
				$data['slr_dns_udp_rtt_percentage']
			)
		],
		($data['slv_dns_udp_rtt_percentage'] < $data['slr_dns_udp_rtt_percentage']) ? 'red-bg' : null
	)->addRow([
			_('DNS TCP Resolution RTT'),
			'',
			'',
			'',
			_s('%1$s %% (queries <= %2$s ms)', $data['slv_dns_tcp_rtt_percentage'],
				$data['slr_dns_tcp_rtt_ms']
			),
			_s('<= %1$s ms, for at least %2$s %% of queries', $data['slr_dns_tcp_rtt_ms'],
				$data['slr_dns_tcp_rtt_percentage']
			)
		],
		($data['slv_dns_tcp_rtt_percentage'] <  $data['slr_dns_tcp_rtt_percentage']) ? 'red-bg' : null
	);
}

// RDDS Service Availability and Query RTT.
if (array_key_exists('slv_rdds_downtime', $data)) {
	$disabled = ($data['slv_rdds_downtime'] === 'disabled' && $data['slv_rdds_rtt_percentage'] === 'disabled');

	if ($disabled) {
		$availability_class = 'disabled';
		$rtt_class = 'disabled';
	}
	else {
		$availability_class = ($data['slv_rdds_downtime'] > $data['slr_rdds_downtime']) ? 'red-bg' : null;
		$rtt_class = ($data['slv_rdds_rtt_percentage'] < $data['slr_rdds_rtt_percentage']) ? 'red-bg' : null;
	}

	$table->addRow([
			bold(_('RDDS Service Availability')),
			'',
			'',
			'',
			$disabled ? 'disabled' : _s('%1$s (minutes of downtime)', $data['slv_rdds_downtime']),
			$disabled ? 'disabled' : _s('<= %1$s min of downtime',  $data['slr_rdds_downtime'])
		],
		$availability_class
	)->addRow([
			_('RDDS Query RTT'),
			'',
			'',
			'',
			$disabled ? 'disabled' : _s('%1$s %% (queries <= %2$s ms)', $data['slv_rdds_rtt_percentage'],
				$data['slr_rdds_rtt_ms']
			),
			$disabled ? 'disabled' : _s('<= %1$s ms, for at least %2$s %% of the queries',
				$data['slr_rdds_rtt_ms'], $data['slr_rdds_rtt_percentage']
			)
		],
		$rtt_class
	);
}

// RDAP Service Availability and Query RTT.
if (array_key_exists('slv_rdap_downtime', $data) {
	$disabled = ($data['slv_rdap_downtime'] === 'disabled' && $data['slv_rdap_rtt_percentage'] === 'disabled');

	if ($disabled) {
		$availability_class = 'disabled';
		$rtt_class = 'disabled';
	}
	else {
		$availability_class = ($data['slv_rdap_downtime'] > $data['slr_rdap_downtime']) ? 'red-bg' : null;
		$rtt_class = ($data['slv_rdap_rtt_percentage'] < $data['slr_rdap_rtt_percentage']) ? 'red-bg' : null;
	}

	$table->addRow([
			bold(_('RDAP Service Availability')),
			'',
			'',
			'',
			$disabled ? 'disabled' : _s('%1$s (minutes of downtime)', $data['slv_rdap_downtime']),
			$disabled ? 'disabled' : _s('<= %1$s min of downtime',  $data['slr_rdap_downtime'])
		],
		$availability_class
	)->addRow([
			_('RDAP Query RTT'),
			'',
			'',
			'',
			$disabled ? 'disabled' : _s('%1$s %% (queries <= %2$s ms)', $data['slv_rdap_rtt_percentage'],
				$data['slr_rdap_rtt_ms']
			),
			$disabled ? 'disabled' : _s('<= %1$s ms, for at least %2$s %% of the queries',
				$data['slr_rdap_rtt_ms'], $data['slr_rdap_rtt_percentage']
			)
		],
		$rtt_class
	);
}

return $widget->addItem([
	$table,
	(new CDiv())
		->addItem((new CForm())
			->addVar('filter_search', $data['filter_search'])
			->addVar('filter_year', $data['filter_year'])
			->addVar('filter_month', $data['filter_month'])
			->additem(new CSubmitButton(_('Download XML'), 'export', 1))
		)
		->addClass('action-buttons')
]);
