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
);

$table = (new CTableInfo())->setHeader([
	_('Service'),
	_('FQDN and IP'),
	_('From'),
	_('To'),
	_('SLV'),
	_('Monthly SLR')
]);

// TLD details.
if ($data['tld']) {
	$details = [
		($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRAR)
			? [
				bold(_s('Registrar ID')), ': ', $data['tld']['host'], BR(),
				bold(_s('Registrar name')), ': ', $data['tld']['info_1'], BR(),
				bold(_s('Registrar family')), ': ', $data['tld']['info_2']
			]
			: [
				bold(_s('TLD')), ': ', $data['tld']['host']
			]
	];

	if (array_key_exists('details', $data)) {
		$details = array_merge($details, [
			BR(),
			bold(_s('Period')),
			': ',
			gmdate('Y/m/d H:i:s', $data['details']['from']),
			' '._('till').' ',
			gmdate('Y/m/d H:i:s', $data['details']['to']),
			BR(),
			bold(_s('Generation time')), 
			': ',
			gmdate('dS F Y, H:i:s e', $data['details']['generated']),
			BR(),
			bold(_('Server')), ': ', new CLink($data['server'], $data['rolling_week_url'])
		]);
	}

	$widget->additem((new CDiv())->addItem($details));
}

if (!array_key_exists('details', $data)) {
	return $widget->addItem([
		$table,
		(new CDiv())
			->addItem((new CButton('export', 'Download XML'))->setEnabled(false))
			->addClass('action-buttons')
	]);
}

// DNS Service Availability.
if ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRY) {
	$table->addRow([
			bold(_('DNS Service Availability')),
			'-',
			gmdate('Y-m-d H:i:s e', $data['details']['from']),
			gmdate('Y-m-d H:i:s e', $data['details']['to']),
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
				gmdate('Y-m-d H:i:s e', $item['from']),
				gmdate('Y-m-d H:i:s e', $item['to']),
				_s('%1$s (minutes of downtime)', $item['slv']),
				_s('%1$s (minutes of downtime)', $item['slr'])
			],
			($item['slv'] > $item['slr']) ? 'red-bg' : null
		);
	}

	// DNS UDP/TCP Resolution RTT.
	$table
		->addRow([
				_('DNS UDP Resolution RTT'),
				'-',
				gmdate('Y-m-d H:i:s e', $data['details']['from']),
				gmdate('Y-m-d H:i:s e', $data['details']['to']),
				_s('%1$s %% (queries <= %2$s ms)', $data['slv_dns_udp_pfailed'],
					$data['slr_dns_udp_pfailed_ms']
				),
				_s('<= %1$s ms, for at least %2$s %% of queries', $data['slr_dns_udp_pfailed_ms'],
					$data['slr_dns_udp_pfailed']
				)
			],
			($data['slv_dns_udp_pfailed'] < (100 - $data['slr_dns_udp_pfailed'])) ? 'red-bg' : null
		)->addRow([
				_('DNS TCP Resolution RTT'),
				'-',
				gmdate('Y-m-d H:i:s e', $data['details']['from']),
				gmdate('Y-m-d H:i:s e', $data['details']['to']),
				_s('%1$s %% (queries <= %2$s ms)', $data['slv_dns_tcp_pfailed'],
					$data['slr_dns_tcp_pfailed_ms']
				),
				_s('<= %1$s ms, for at least %2$s %% of queries', $data['slr_dns_tcp_pfailed_ms'],
					$data['slr_dns_tcp_pfailed']
				)
			],
			($data['slv_dns_tcp_pfailed'] < (100 - $data['slr_dns_tcp_pfailed'])) ? 'red-bg' : null
	);
}

// RDDS Service Availability and Query RTT.
if (array_key_exists('slv_rdds_downtime', $data) && $data['slv_rdds_downtime'] !== 'disabled'
		&& $data['slv_rdds_rtt_downtime'] !== 'disabled') {
	$table->addRow([
			bold(_('RDDS Service Availability')),
			'-',
			gmdate('Y-m-d H:i:s e', $data['details']['from']),
			gmdate('Y-m-d H:i:s e', $data['details']['to']),
			_s('%1$s (minutes of downtime)', $data['slv_rdds_downtime']),
			_s('<= %1$s min of downtime', $data['slr_rdds_downtime'])
		],
		($data['slv_rdds_downtime'] > $data['slr_rdds_downtime']) ? 'red-bg' : null
	)->addRow([
			_('RDDS Query RTT'),
			'-',
			gmdate('Y-m-d H:i:s e', $data['details']['from']),
			gmdate('Y-m-d H:i:s e', $data['details']['to']),
			_s('%1$s %% (queries <= %2$s ms)', $data['slv_rdds_rtt_downtime'], $data['slr_rdds_rtt_downtime_ms']),
			_s('<= %1$s ms, for at least %2$s %% of the queries', $data['slr_rdds_rtt_downtime_ms'],
				$data['slr_rdds_rtt_downtime']
			)
		],
		($data['slv_rdds_rtt_downtime'] < (100 - $data['slr_rdds_rtt_downtime'])) ? 'red-bg' : null
	);
}

// RDAP Service Availability and Query RTT.
if (array_key_exists('slv_rdap_downtime', $data) && $data['slv_rdap_downtime'] !== 'disabled'
		&& $data['slv_rdap_rtt_downtime'] !== 'disabled') {
	$table->addRow([
			bold(_('RDAP Service Availability')),
			'-',
			gmdate('Y-m-d H:i:s e', $data['details']['from']),
			gmdate('Y-m-d H:i:s e', $data['details']['to']),
			_s('%1$s (minutes of downtime)', $data['slv_rdap_downtime']),
			_s('<= %1$s min of downtime', $data['slr_rdap_downtime'])
		],
		($data['slv_rdap_downtime'] > $data['slr_rdap_downtime']) ? 'red-bg' : null
	)->addRow([
			_('RDAP Query RTT'),
			'-',
			gmdate('Y-m-d H:i:s e', $data['details']['from']),
			gmdate('Y-m-d H:i:s e', $data['details']['to']),
			_s('%1$s %% (queries <= %2$s ms)', $data['slv_rdap_rtt_downtime'], $data['slr_rdap_rtt_downtime_ms']),
			_s('<= %1$s ms, for at least %2$s %% of the queries', $data['slr_rdap_rtt_downtime_ms'],
				$data['slr_rdap_rtt_downtime']
			)
		],
		($data['slv_rdap_rtt_downtime'] < (100 - $data['slr_rdap_rtt_downtime'])) ? 'red-bg' : null
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
