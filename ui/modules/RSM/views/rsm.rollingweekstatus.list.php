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
$this->includeJSfile('rsm.rollingweekstatus.list.js.php');

// Build filter.
if ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRAR) {
	$filter_fields = [
		(new CFormList())
			->addRow(_('Registrar ID'), (new CTextBox('filter_registrar_id', $data['filter_registrar_id']))
				->setWidth(ZBX_TEXTAREA_FILTER_SMALL_WIDTH)
				->setAttribute('autocomplete', 'off')
			)
			->addRow(_('Registrar name'), (new CTextBox('filter_registrar_name', $data['filter_registrar_name']))
				->setWidth(ZBX_TEXTAREA_FILTER_SMALL_WIDTH)
				->setAttribute('autocomplete', 'off')
			)
			->addRow(_('Registrar family'), (new CTextBox('filter_registrar_family', $data['filter_registrar_family']))
				->setWidth(ZBX_TEXTAREA_FILTER_SMALL_WIDTH)
				->setAttribute('autocomplete', 'off')
			)
	];
}
else {
	$filter_fields = [
		(new CFormList())->addRow(_('TLD'), (new CTextBox('filter_search', $data['filter_search']))
			->setWidth(ZBX_TEXTAREA_FILTER_SMALL_WIDTH)
			->setAttribute('autocomplete', 'off')
		)
	];

	$services_filter = [
		new CSpan([
			(new CCheckBox('filter_dns'))->setChecked($data['filter_dns']),
			SPACE,
			_('DNS')
		], 'checkbox-block'),
		SPACE,
		new CSpan([
			(new CCheckBox('filter_dnssec'))->setChecked($data['filter_dnssec']),
			SPACE,
			_('DNSSEC')
		], 'checkbox-block'),
		SPACE,
		new CSpan([
			(new CCheckBox('filter_rdds'))->setChecked($data['filter_rdds']),
			SPACE,
			_('RDDS')
		], 'checkbox-block'),
		SPACE
	];

	if (is_RDAP_standalone()) {
		$services_filter = array_merge($services_filter, [
			new CSpan([
				(new CCheckBox('filter_rdap'))->setChecked($data['filter_rdap']),
				SPACE,
				_('RDAP')
			], 'checkbox-block'),
			SPACE
		]);
	}

	$services_filter = array_merge($services_filter, [
		new CSpan([
			(new CCheckBox('filter_epp'))->setChecked($data['filter_epp']),
			SPACE,
			_('EPP')
		], 'checkbox-block'),
		SPACE,
		(new CButton('checkAllServices', _('All/Any')))->addClass(ZBX_STYLE_BTN_LINK)
	]);

	// Subservices
	$subservices_components = [
		new CSpan([
			(new CCheckBox('filter_rdds43_subgroup'))->setChecked($data['filter_rdds43_subgroup']),
			SPACE,
			_(RSM_RDDS_SUBSERVICE_RDDS43)
		], 'checkbox-block'),
		SPACE,
		new CSpan([
			(new CCheckBox('filter_rdds80_subgroup'))->setChecked($data['filter_rdds80_subgroup']),
			SPACE,
			_(RSM_RDDS_SUBSERVICE_RDDS80)
		], 'checkbox-block')
	];

	if (!is_RDAP_standalone()) {
		$subservices_components = array_merge($subservices_components, [
			SPACE,
			new CSpan([
				(new CCheckBox('filter_rdap_subgroup'))->setChecked($data['filter_rdap_subgroup']),
				SPACE,
				_(RSM_RDDS_SUBSERVICE_RDAP)
			], 'checkbox-block')
		]);
	}

	$subservices_components = array_merge($subservices_components, [
		SPACE,
		(new CButton('checkAllSubservices', _('All/Any')))->addClass(ZBX_STYLE_BTN_LINK)
	]);

	$filter_column_2 = (new CFormList())
		->addRow((new CSpan(_('Services')))->addStyle('padding: 0 25px;'), $services_filter)
		->addRow((new CSpan(_('TLD types')))->addStyle('padding: 0 25px;'), [
			new CSpan([
				// ccTLD's group
				(new CCheckBox('filter_cctld_group'))
					->setEnabled($data['allowedGroups'][RSM_CC_TLD_GROUP])
					->setChecked($data['filter_cctld_group']),
				SPACE,
				_(RSM_CC_TLD_GROUP)
			], 'checkbox-block'),
			SPACE,
			new CSpan([
				// gTLD's group
				(new CCheckBox('filter_gtld_group'))
					->setEnabled($data['allowedGroups'][RSM_G_TLD_GROUP])
					->setChecked($data['filter_gtld_group']),
				SPACE,
				_(RSM_G_TLD_GROUP)
			], 'checkbox-block'),
			SPACE,
			new CSpan([
				// other TLD's group
				(new CCheckBox('filter_othertld_group'))
					->setEnabled($data['allowedGroups'][RSM_OTHER_TLD_GROUP])
					->setChecked($data['filter_othertld_group']),
				SPACE,
				_(RSM_OTHER_TLD_GROUP)
			], 'checkbox-block'),
			SPACE,
			new CSpan([
				// test TLD's group
				(new CCheckBox('filter_test_group'))
					->setEnabled($data['allowedGroups'][RSM_TEST_GROUP])
					->setChecked($data['filter_test_group']),
				SPACE,
				_(RSM_TEST_GROUP)
			], 'checkbox-block'),
			SPACE,
			(new CButton('checkAllGroups', _('All/Any')))->addClass(ZBX_STYLE_BTN_LINK),
		])
		->addRow((new CSpan(_('Enabled subservices')))->addStyle('padding: 0 25px;'), $subservices_components);

	$filter_fields[] = $filter_column_2;
}

// Add right-most filter column.
$filter_value = (new CSelect('filter_slv'))
	->setFocusableElementId('label-filter-value');

if (isset($data['filter_slv'])) {
	$filter_value->setValue($data['filter_slv']);
}

$options = [
	'' => _('any'),
	SLA_MONITORING_SLV_FILTER_NON_ZERO => _('non-zero')
];

foreach (explode(',', $data['slv']) as $slv) {
	$options[$slv] = $slv.'%';
}

$filter_value->addOptions(CSelect::createOptionsFromArray($options));

// filter status
$filter_status = (new CSelect('filter_status'))
	->setFocusableElementId('label-filter-status');

if (isset($data['filter_status'])) {
	$filter_status->setValue($data['filter_status']);
}

$filter_status->addOptions(CSelect::createOptionsFromArray(
	[
		0 => _('all'),
		1 => _('fail'),
		2 => _('disabled'),
	]
));

$filter_fields[] = (new CFormList())
	->addRow(new CLabel(_('Exceeding or equal to'), 'label-filter-value'), $filter_value)
	->addRow(new CLabel(_('Current status'), 'label-filter-status'), $filter_status);

// Create data table.
if ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRAR) {
	if (is_RDAP_standalone()) {
		$header_columns = [
			make_sorting_header(_('Registrar ID'), 'host', $data['sort_field'], $data['sort_order']),
			make_sorting_header(_('Registrar name'), 'info_1', $data['sort_field'], $data['sort_order']),
			make_sorting_header(_('Registrar family'), 'info_2', $data['sort_field'], $data['sort_order']),
			make_sorting_header(_('RDDS (24Hrs)'), 'rdds_lastvalue', $data['sort_field'], $data['sort_order']),
			make_sorting_header(_('RDAP (24Hrs)'), 'rdap_lastvalue', $data['sort_field'], $data['sort_order']),
			make_sorting_header(_('Server'), 'server', $data['sort_field'], $data['sort_order'])
		];
	}
	else {
		$header_columns = [
			make_sorting_header(_('Registrar ID'), 'host', $data['sort_field'], $data['sort_order']),
			make_sorting_header(_('Registrar name'), 'info_1', $data['sort_field'], $data['sort_order']),
			make_sorting_header(_('Registrar family'), 'info_2', $data['sort_field'], $data['sort_order']),
			make_sorting_header(_('RDDS (24Hrs)'), 'rdds_lastvalue', $data['sort_field'], $data['sort_order']),
			make_sorting_header(_('Server'), 'server', $data['sort_field'], $data['sort_order'])
		];
	}
}
else {
	if (is_RDAP_standalone()) {
		$header_columns = [
			make_sorting_header(_('TLD'), 'host', $data['sort_field'], $data['sort_order']),
			make_sorting_header(_('Type'), 'type', $data['sort_field'], $data['sort_order']),
			make_sorting_header(_('DNS (4Hrs)'), 'dns_lastvalue', $data['sort_field'], $data['sort_order']),
			make_sorting_header(_('DNSSEC (4Hrs)'), 'dnssec_lastvalue', $data['sort_field'], $data['sort_order']),
			make_sorting_header(_('RDDS (24Hrs)'), 'rdds_lastvalue', $data['sort_field'], $data['sort_order']),
			make_sorting_header(_('RDAP (24Hrs)'), 'rdap_lastvalue', $data['sort_field'], $data['sort_order']),
			make_sorting_header(_('EPP (24Hrs)'), 'epp_lastvalue', $data['sort_field'], $data['sort_order']),
			make_sorting_header(_('Server'), 'server', $data['sort_field'], $data['sort_order'])
		];
	}
	else {
		$header_columns = [
			make_sorting_header(_('TLD'), 'host', $data['sort_field'], $data['sort_order']),
			make_sorting_header(_('Type'), 'type', $data['sort_field'], $data['sort_order']),
			make_sorting_header(_('DNS (4Hrs)'), 'dns_lastvalue', $data['sort_field'], $data['sort_order']),
			make_sorting_header(_('DNSSEC (4Hrs)'), 'dnssec_lastvalue', $data['sort_field'], $data['sort_order']),
			make_sorting_header(_('RDDS (24Hrs)'), 'rdds_lastvalue', $data['sort_field'], $data['sort_order']),
			make_sorting_header(_('EPP (24Hrs)'), 'epp_lastvalue', $data['sort_field'], $data['sort_order']),
			make_sorting_header(_('Server'), 'server', $data['sort_field'], $data['sort_order'])
		];
	}
}

$table = (new CTableInfo())->setHeader($header_columns);

if ($data['tld']) {
	// Services must be in certain order.
	$services = array();

	if ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRY) {
		$services[RSM_DNS] = "DNS";
		$services[RSM_DNSSEC] = "DNSSEC";
	}

	$services[RSM_RDDS] = "RDDS";

	if (is_RDAP_standalone())
		$services[RSM_RDAP] = "RDAP";

	$services[RSM_EPP] = "EPP";

	foreach ($data['tld'] as $key => $tld) {
		// REGISTRAR type.
		if ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRAR) {
			$row = [
				$tld['host'],
				$tld['info_1'],
				$tld['info_2']
			];
		}
		// TLD type.
		else {
			$row = [
				$tld['host'],
				$tld['type']
			];
		}

		foreach ($services as $service => $service_name) {
			$rdds_subservices = null;

			if (array_key_exists($service, $tld) && array_key_exists('trigger', $tld[$service])) {

				if ($service === RSM_RDDS) {
					$subservices = [];

					if (array_key_exists(RSM_TLD_RDDS43_ENABLED, ($tld[$service]['subservices']))
							&& $tld[RSM_RDDS]['subservices'][RSM_TLD_RDDS43_ENABLED] != 0) {
						$subservices[] = 'RDDS43';
					}

					if (array_key_exists(RSM_TLD_RDDS80_ENABLED, ($tld[$service]['subservices']))
							&& $tld[RSM_RDDS]['subservices'][RSM_TLD_RDDS80_ENABLED] != 0) {
						$subservices[] = 'RDDS80';
					}

					if (!is_RDAP_standalone()) {
						if (array_key_exists(RSM_RDAP_TLD_ENABLED, ($tld[$service]['subservices']))
										&& $tld[RSM_RDDS]['subservices'][RSM_RDAP_TLD_ENABLED] != 0) {
							$subservices[] = 'RDAP';
						}
					}

					$rdds_subservices = [SPACE, SPACE, SPACE, new CSpan(implode(' / ', $subservices), 'bold')];
				}

				if ($tld[$service]['clock']) {
					if ($tld[$service]['trigger'] && $tld[$service]['incident']) {
						if (array_key_exists('availItemId', $tld[$service]) && array_key_exists('itemid', $tld[$service])) {

							$rollweek_status = new CLink(
									(new CDiv())->addClass('service-icon status_icon_extra iconrollingweekfail cell-value pointer'),
										Url::getFor($tld['url'], 'rsm.incidentdetails', [
											'host' => $tld['host'],
											'eventid' => $tld[$service]['incident'],
											'slvItemId' => $tld[$service]['itemid'],
											'availItemId' => $tld[$service]['availItemId']
									])
							);
						}
						else {
							$rollweek_status = (new CDiv())->addClass('service-icon status_icon_extra iconrollingweekfail cell-value pointer');
						}
					}
					else {
						$rollweek_status = (new CDiv())->addClass('service-icon status_icon_extra iconrollingweekok cell-value');
					}

					$rollweek_value = ($tld[$service]['lastvalue'] > 0)
						? new CLink(
							$tld[$service]['lastvalue'].'%',
							Url::getFor($tld['url'], 'rsm.incidents', [
								'host' => $tld['host'],
								'type' => $service,
								'rolling_week' => 1,
								'filter_set' => 1,
							])
							)
						: new CSpan('0.000%');

					if ($tld[$service]['clock']) {
						$rollweek_value->setAttribute('title', date(DATE_TIME_FORMAT_SECONDS, $tld[$service]['clock']), '', false);
					}

					$rollweek_graph = ($tld[$service]['lastvalue'] > 0)
						? new CLink('graph',
							Url::getFor($tld['url'], 'history.php', [
								'action' => 'showgraph',
								'period' => $data['rollWeekSeconds'],
								'itemids' => [$tld[$service]['itemid']],
							]),
							'cell-value')
						: null;

					$row[] = [
						(new CSpan($rollweek_value))->addClass('rolling-week-value'),
						$rollweek_status,
						(new CLink(
							'',
							Url::getFor($tld['url'], 'rsm.tests', [
								'slvItemId' => $tld[$service]['itemid'],
								'host' => $tld['host'],
								'type' => $service,
								'from' => ZBX_PERIOD_DEFAULT_FROM,
								'to' => ZBX_PERIOD_DEFAULT_TO,
							])
						))
						->addClass('icon-eye')
						->setAttribute('title', date(DATE_TIME_FORMAT_SECONDS, $tld[$service]['availClock']), '', false),
						SPACE,
						(new CSpan($rollweek_graph))->addClass('rolling-week-graph'),
						$rdds_subservices
					];
				}
				else {
					$row[] = [
						(new CSpan(''))->addClass('rolling-week-value'),
						(new CDiv())
							->addClass('service-icon status_icon_extra iconrollingweeknodata disabled-service')
							->setAttribute('title', _('No data yet'), '', false),
						(new CSpan(null))->addClass('rolling-week-graph'),
						$rdds_subservices,
					];
				}
			}
			else {
				$row[] = [
					(new CSpan(null))->addClass('rolling-week-value'),
					(new CDiv())
						->addClass('service-icon status_icon_extra iconrollingweekdisabled disabled-service')
						->setAttribute('title', _("$service_name is disabled"), '', false)
				];
			}
		}

		$row[] = new CLink($tld['server'], Url::getFor($tld['url'], 'rsm.rollingweekstatus', []));

		if ($tld['status'] == HOST_STATUS_MONITORED)
			$table->addRow($row);
		else
			$table->addRow($row, ZBX_STYLE_DISABLED);
	}
}

if ($data['ajax_request']) {
	$dynamic_node = new CDiv([$table, $data['paging']]);
}
else {
	$dynamic_node = new DynamicContent([$table, $data['paging']]);
	$dynamic_node->refresh_seconds = $data['refresh'];
}

$widget = (new CWidget())
	->setTitle($data['title'])
	->addItem(
		(new CFilter((new CUrl('zabbix.php'))->setArgument('action', 'rsm.rollingweekstatus')))
			->setProfile('web.rsm.rollingweekstatus.filter')
			->setActiveTab($data['active_tab'])
			->addFilterTab(_('Filter'), $filter_fields)
			->addVar('action', 'rsm.rollingweekstatus')
			->addVar('checkAllServicesValue', 0)
			->addVar('checkAllSubservicesValue', 0)
			->addVar('checkAllGroupsValue', 0)
	)
	->addItem(
		(new CForm())
			->addItem($dynamic_node->setId('rollingweek'))
			->setName('rollingweek')
	)
	->addItem($data['module_style'])
	->show();
