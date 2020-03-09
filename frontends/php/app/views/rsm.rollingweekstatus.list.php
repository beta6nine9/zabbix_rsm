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
			(new CButton('checkAllGroups', _('All/Any')))->addClass(ZBX_STYLE_BTN_LINK)
		]);

	if (!is_RDAP_standalone()) {
		$filter_column_2
			->addRow((new CSpan(_('Enabled subservices')))->addStyle('padding: 0 25px;'), [
				new CSpan([
					(new CCheckBox('filter_rdds_subgroup'))->setChecked($data['filter_rdds_subgroup']),
					SPACE,
					_(RSM_RDDS_SUBSERVICE_RDDS)
				], 'checkbox-block'),
				SPACE,
				new CSpan([
					(new CCheckBox('filter_rdap_subgroup'))->setChecked($data['filter_rdap_subgroup']),
					SPACE,
					_(RSM_RDDS_SUBSERVICE_RDAP)
				], 'checkbox-block'),
				SPACE,
				(new CButton('checkAllSubservices', _('All/Any')))->addClass(ZBX_STYLE_BTN_LINK)
			]);
	}

	$filter_fields[] = $filter_column_2;
}

// Add right-most filter column.
$filter_value = (new CComboBox('filter_slv', isset($data['filter_slv']) ? $data['filter_slv'] : null))
	->addItem('', _('any'))
	->addItem(SLA_MONITORING_SLV_FILTER_NON_ZERO, _('non-zero'));

foreach (explode(',', $data['slv']) as $slv) {
	$filter_value->addItem($slv, $slv.'%');
}

$filter_fields[] = (new CFormList())
	->addRow(_('Exceeding or equal to'), $filter_value)
	->addRow(_('Current status'),
		(new CComboBox('filter_status', array_key_exists('filter_status', $data) ? $data['filter_status'] : null))
			->addItem(0, _('all'))
			->addItem(1, _('fail'))
			->addItem(2, _('disabled'))
	);

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
	$serverTime = time() - RSM_ROLLWEEK_SHIFT_BACK;
	$from = date('YmdHis', $serverTime - $data['rollWeekSeconds']);
	$till = date('YmdHis', $serverTime);

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

		// DNS
		if ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRY && array_key_exists(RSM_DNS, $tld)
				&& array_key_exists('trigger', $tld[RSM_DNS])) {
			if ($tld[RSM_DNS]['trigger'] && $tld[RSM_DNS]['incident']) {
				if (array_key_exists('availItemId', $tld[RSM_DNS]) && array_key_exists('itemid', $tld[RSM_DNS])) {
					$dns_status = new CLink(
						(new CDiv())->addClass('service-icon status_icon_extra iconrollingweekfail cell-value pointer'),
						(new CUrl($tld['url'].'zabbix.php'))
							->setArgument('action', 'rsm.incidentdetails')
							->setArgument('host', $tld['host'])
							->setArgument('eventid', $tld[RSM_DNS]['incident'])
							->setArgument('slvItemId', $tld[RSM_DNS]['itemid'])
							->setArgument('filter_from', $from)
							->setArgument('filter_to', $till)
							->setArgument('availItemId', $tld[RSM_DNS]['availItemId'])
							->setArgument('filter_set', 1)
							->setArgument('sid', $data['sid'])
							->setArgument('set_sid', 1)
					);
				}
				else {
					$dns_status = (new CDiv())->addClass('service-icon status_icon_extra iconrollingweekfail cell-value pointer');
				}
			}
			else {
				$dns_status = (new CDiv())->addClass('service-icon status_icon_extra iconrollingweekok cell-value');
			}

			$dns_value = ($tld[RSM_DNS]['lastvalue'] > 0)
				? (new CLink(
					$tld[RSM_DNS]['lastvalue'].'%',
					(new CUrl($tld['url'].'zabbix.php'))
						->setArgument('action', 'rsm.incidents')
						->setArgument('filter_rolling_week', 1)
						->setArgument('filter_set', 1)
						->setArgument('type', RSM_DNS)
						->setArgument('host', $tld['host'])
						->setArgument('sid', $data['sid'])
						->setArgument('set_sid', 1)
				))->addClass('first-cell-value')
				: (new CSpan('0.000%'))->addClass('first-cell-value');

			$dns_graph = ($tld[RSM_DNS]['lastvalue'] > 0)
				? new CLink('graph',
					(new CUrl($tld['url'].'history.php'))
						->setArgument('action', 'showgraph')
						->setArgument('period', $data['rollWeekSeconds'])
						->setArgument('itemids', [$tld[RSM_DNS]['itemid']])
						->setArgument('sid', $data['sid'])
						->setArgument('set_sid', 1),
					'cell-value'
				)
				: null;
			$row[] = [(new CSpan($dns_value))->addClass('right'), $dns_status, SPACE, $dns_graph];
		}
		elseif ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRY) {
			$row[] = (new CDiv())
				->addClass('service-icon status_icon_extra iconrollingweekdisabled disabled-service')
				->setHint(_('Incorrect TLD configuration.'), '', false);
		}

		// DNSSEC
		if ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRY && array_key_exists(RSM_DNSSEC, $tld)
				&& array_key_exists('trigger', $tld[RSM_DNSSEC])) {
			if ($tld[RSM_DNSSEC]['trigger'] && $tld[RSM_DNSSEC]['incident']) {
				if (array_key_exists('availItemId', $tld[RSM_DNSSEC]) && array_key_exists('itemid', $tld[RSM_DNSSEC])) {
					$dnssec_status = new CLink(
						(new CDiv())->addClass('service-icon status_icon_extra iconrollingweekfail cell-value pointer'),
						(new CUrl($tld['url'].'zabbix.php'))
							->setArgument('action', 'rsm.incidentdetails')
							->setArgument('host', $tld['host'])
							->setArgument('eventid', $tld[RSM_DNSSEC]['incident'])
							->setArgument('slvItemId', $tld[RSM_DNSSEC]['itemid'])
							->setArgument('filter_from', $from)
							->setArgument('filter_to', $till)
							->setArgument('availItemId', $tld[RSM_DNSSEC]['availItemId'])
							->setArgument('filter_set', 1)
							->setArgument('sid', $data['sid'])
							->setArgument('set_sid', 1)
					);
				}
				else {
					$dnssec_status = (new CDiv())->addClass('service-icon status_icon_extra iconrollingweekfail cell-value pointer');
				}
			}
			else {
				$dnssec_status = (new CDiv())->addClass('service-icon status_icon_extra iconrollingweekok cell-value');
			}

			$dnssec_value = ($tld[RSM_DNSSEC]['lastvalue'] > 0)
				? (new CLink(
					$tld[RSM_DNSSEC]['lastvalue'].'%',
					(new CUrl($tld['url'].'zabbix.php'))
						->setArgument('action', 'rsm.incidents')
						->setArgument('filter_set', 1)
						->setArgument('filter_rolling_week', 1)
						->setArgument('type', RSM_DNSSEC)
						->setArgument('host', $tld['host'])
						->setArgument('sid', $data['sid'])
						->setArgument('set_sid', 1)
				))->addClass('first-cell-value')
				: (new CSpan('0.000%'))->addClass('first-cell-value');

			$dnssec_graph = ($tld[RSM_DNSSEC]['lastvalue'] > 0)
				? new CLink('graph',
					(new CUrl($tld['url'].'history.php'))
						->setArgument('action', 'showgraph')
						->setArgument('period', $data['rollWeekSeconds'])
						->setArgument('itemids', [$tld[RSM_DNSSEC]['itemid']]),
					'cell-value'
				)
				: null;
			$row[] = [(new CSpan($dnssec_value))->addClass('right'), $dnssec_status, SPACE, $dnssec_graph];
		}
		elseif ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRY) {
			$row[] = (new CDiv(null))
				->addClass('service-icon status_icon_extra iconrollingweekdisabled disabled-service')
				->setHint('DNSSEC is disabled.', '', false);
		}

		// RDDS
		// RDDS column is shown in registrar monitoring as well.
		if (array_key_exists(RSM_RDDS, $tld) && array_key_exists('trigger', $tld[RSM_RDDS])) {
			if ($tld[RSM_RDDS]['trigger'] && $tld[RSM_RDDS]['incident']) {
				if (array_key_exists('availItemId', $tld[RSM_RDDS]) && array_key_exists('itemid', $tld[RSM_RDDS])) {
					$rdds_status = new CLink(
						(new CDiv())->addClass('service-icon status_icon_extra iconrollingweekfail cell-value pointer'),
						(new CUrl($tld['url'].'zabbix.php'))
							->setArgument('action', 'rsm.incidentdetails')
							->setArgument('host', $tld['host'])
							->setArgument('eventid', $tld[RSM_RDDS]['incident'])
							->setArgument('slvItemId', $tld[RSM_RDDS]['itemid'])
							->setArgument('filter_from', $from)
							->setArgument('filter_to', $till)
							->setArgument('availItemId', $tld[RSM_RDDS]['availItemId'])
							->setArgument('filter_set', 1)
							->setArgument('sid', $data['sid'])
							->setArgument('set_sid', 1)
					);
				}
				else {
					$rdds_status = (new CDiv())->addClass('service-icon status_icon_extra iconrollingweekfail cell-value pointer');
				}
			}
			else {
				$rdds_status = (new CDiv())->addClass('service-icon status_icon_extra iconrollingweekok cell-value');
			}

			$rdds_value = ($tld[RSM_RDDS]['lastvalue'] > 0)
				? (new CLink(
					$tld[RSM_RDDS]['lastvalue'].'%',
					(new CUrl($tld['url'].'zabbix.php'))
						->setArgument('action', 'rsm.incidents')
						->setArgument('filter_set', 1)
						->setArgument('filter_rolling_week', 1)
						->setArgument('type', RSM_RDDS)
						->setArgument('host', $tld['host'])
						->setArgument('sid', $data['sid'])
						->setArgument('set_sid', 1)
				))->addClass('first-cell-value')
				: (new CSpan('0.000%'))->addClass('first-cell-value');

			$rdds_graph = ($tld[RSM_RDDS]['lastvalue'] > 0)
				? new CLink('graph',
					(new CUrl($tld['url'].'history.php'))
						->setArgument('action', 'showgraph')
						->setArgument('period', $data['rollWeekSeconds'])
						->setArgument('itemids', [$tld[RSM_RDDS]['itemid']])
						->setArgument('sid', $data['sid'])
						->setArgument('set_sid', 1),
					'cell-value'
				)
				: null;

			$ok_rdds_services = [];
			if (array_key_exists(RSM_TLD_RDDS_ENABLED, ($tld[RSM_RDDS]['subservices']))
					&& $tld[RSM_RDDS]['subservices'][RSM_TLD_RDDS_ENABLED] != 0) {
				$ok_rdds_services[] = 'RDDS';
			}
			if (array_key_exists(RSM_RDAP_TLD_ENABLED, ($tld[RSM_RDDS]['subservices'])) && !is_RDAP_standalone()
					&& $tld[RSM_RDDS]['subservices'][RSM_RDAP_TLD_ENABLED] != 0) {
				$ok_rdds_services[] = 'RDAP';
			}

			$rdds_services = is_RDAP_standalone() ? null : implode(' / ', $ok_rdds_services);

			$row[] = [(new CSpan($rdds_value))->addClass('right'), $rdds_status, SPACE, $rdds_graph, [SPACE,SPACE,SPACE],
				new CSpan($rdds_services, 'bold')
			];
		}
		else {
			$row[] = (new CDiv(null))
				->addClass('service-icon status_icon_extra iconrollingweekdisabled disabled-service')
				->setHint('RDDS is disabled.', '', false);
		}

		// RDAP
		if (is_RDAP_standalone() && array_key_exists(RSM_RDAP, $tld) && array_key_exists('trigger', $tld[RSM_RDAP])) {
			if ($tld[RSM_RDAP]['trigger'] && $tld[RSM_RDAP]['incident']) {
				if (array_key_exists('availItemId', $tld[RSM_RDAP]) && array_key_exists('itemid', $tld[RSM_RDAP])) {
					$rdap_status =  new CLink(
						(new CDiv())->addClass('service-icon status_icon_extra iconrollingweekfail cell-value pointer'),
						(new CUrl($tld['url'].'zabbix.php'))
							->setArgument('action', 'rsm.incidentdetails')
							->setArgument('host', $tld['host'])
							->setArgument('eventid', $tld[RSM_RDAP]['incident'])
							->setArgument('slvItemId', $tld[RSM_RDAP]['itemid'])
							->setArgument('filter_from', $from)
							->setArgument('filter_to', $till)
							->setArgument('availItemId', $tld[RSM_RDAP]['availItemId'])
							->setArgument('filter_set', 1)
							->setArgument('sid', $data['sid'])
							->setArgument('set_sid', 1)
					);
				}
				else {
					$rdap_status = (new CDiv())->addClass('service-icon status_icon_extra iconrollingweekfail cell-value pointer');
				}
			}
			else {
				$rdap_status = (new CDiv())->addClass('service-icon status_icon_extra iconrollingweekok cell-value');
			}

			$rdap_value = ($tld[RSM_RDAP]['lastvalue'] > 0)
				? (new CLink(
					$tld[RSM_RDAP]['lastvalue'].'%',
					(new CUrl($tld['url'].'zabbix.php'))
						->setArgument('action', 'rsm.incidents')
						->setArgument('filter_set', 1)
						->setArgument('filter_rolling_week', 1)
						->setArgument('type', RSM_RDAP)
						->setArgument('host', $tld['host'])
						->setArgument('sid', $data['sid'])
						->setArgument('set_sid', 1)
				))->addClass('first-cell-value')
				: (new CSpan('0.000%'))->addClass('first-cell-value');

			$rdap_graph = ($tld[RSM_RDAP]['lastvalue'] > 0)
				? new CLink('graph',
					(new CUrl($tld['url'].'history.php'))
						->setArgument('action', 'showgraph')
						->setArgument('period', $data['rollWeekSeconds'])
						->setArgument('itemids', [$tld[RSM_RDAP]['itemid']]),
					'cell-value'
				)
				: null;
			$row[] = [(new CSpan($rdap_value))->addClass('right'), $rdap_status, SPACE, $rdap_graph];
		}
		elseif (is_RDAP_standalone()) {
			$row[] = (new CDiv())
				->addClass('service-icon status_icon_extra iconrollingweekdisabled disabled-service')
				->setHint('RDAP is disabled.', '', false);
		}

		// EPP
		if ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRY && array_key_exists(RSM_EPP, $tld)
				&& array_key_exists('trigger', $tld[RSM_EPP])) {
			if ($tld[RSM_EPP]['trigger'] && $tld[RSM_EPP]['incident']) {
				if (array_key_exists('availItemId', $tld[RSM_EPP]) && array_key_exists('itemid', $tld[RSM_EPP])) {
					$epp_status = new CLink(
						(new CDiv())->addClass('service-icon status_icon_extra iconrollingweekfail cell-value pointer'),
						(new CUrl($tld['url'].'zabbix.php'))
							->setArgument('action', 'rsm.incidentdetails')
							->setArgument('host', $tld['host'])
							->setArgument('eventid', $tld[RSM_EPP]['incident'])
							->setArgument('slvItemId', $tld[RSM_EPP]['itemid'])
							->setArgument('filter_from', $from)
							->setArgument('filter_to', $till)
							->setArgument('availItemId', $tld[RSM_EPP]['availItemId'])
							->setArgument('filter_set', 1)
							->setArgument('sid', $data['sid'])
							->setArgument('set_sid', 1)
					);
				}
				else {
					$epp_status = (new CDiv())->addClass('service-icon status_icon_extra iconrollingweekfail cell-value pointer');
				}
			}
			else {
				$epp_status = (new CDiv())->addClass('service-icon status_icon_extra iconrollingweekok cell-value');
			}

			$epp_value = ($tld[RSM_EPP]['lastvalue'] > 0)
				? (new CLink(
					$tld[RSM_EPP]['lastvalue'].'%',
					(new CUrl($tld['url'].'zabbix.php'))
						->setArgument('action', 'rsm.incidents')
						->setArgument('filter_set', 1)
						->setArgument('filter_rolling_week', 1)
						->setArgument('type', RSM_EPP)
						->setArgument('host', $tld['host'])
						->setArgument('sid', $data['sid'])
						->setArgument('set_sid', 1)
				))->addClass('first-cell-value')
				: (new CSpan('0.000%'))->addClass('first-cell-value');

			$epp_graph = ($tld[RSM_EPP]['lastvalue'] > 0)
				? new CLink('graph',
					(new CUrl($tld['url'].'history.php'))
						->setArgument('action', 'showgraph')
						->setArgument('period', $data['rollWeekSeconds'])
						->setArgument('itemids', [$tld[RSM_EPP]['itemid']])
						->setArgument('sid', $data['sid'])
						->setArgument('set_sid', 1),
					'cell-value'
				)
				: null;

			$row[] = [(new CSpan($epp_value))->addClass('right'), $epp_status, SPACE, $epp_graph];
		}
		elseif ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRY) {
			$row[] = (new CDiv(null))
				->addClass('service-icon status_icon_extra iconrollingweekdisabled disabled-service')
				->setHint('EPP is disabled.', '', false);
		}

		$row[] = new CLink($tld['server'],(new CUrl($tld['url'].'zabbix.php'))
			->setArgument('action', 'rsm.rollingweekstatus')
			->setArgument('sid', $data['sid'])
			->setArgument('set_sid', 1)
		);

		$table->addRow($row);
	}
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
			->addItem([$table, $data['paging']])
			->setName('rollingweek')
	)
	->show();
