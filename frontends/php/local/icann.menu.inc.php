<?php
/*
** Zabbix
** Copyright (C) 2001-2020 Zabbix SIA
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


$label = (get_rsm_monitoring_type() === MONITORING_TARGET_REGISTRAR)
	? _('Registrar monitoring')
	: _('Registry monitoring');

$menu = APP::Component()->get('menu.main');
$menu
	->insertAfter(_('Monitoring'), $label, [
		'items' => [
			_('Rolling week status') => [
				'action' => 'rsm.rollingweekstatus',
			],
			_('Incidents') => [
				'action' => 'rsm.incidents',
				'alias' => [
					'rsm.incidents',
					'rsm.incidentdetails',
					'rsm.tests',
					'rsm.particulartests',
					'rsm.particularproxys',
					'rsm.aggregatedetails',
				]
			],
			_('SLA reports') => [
				'action' => 'rsm.slareports',
			],
		],
	]);
