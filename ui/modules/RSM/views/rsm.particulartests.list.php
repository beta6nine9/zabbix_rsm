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


use Modules\RSM\Helpers\CTableInfo;

$object_label = ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRAR) ? _('Registrar ID') : _('TLD');
$rdap_is_part_of_rdds = ($data['type'] == RSM_RDDS && !is_RDAP_standalone($data['test_time_from']));

if ($data['type'] == RSM_RDAP) {
	/*
	 * If 'status' is not set, probe is UP. So, we need to check if all (length of $probes_status = 0) or
	 * at least one (array_sum($probes_status) > 0) probe is UP.
	 */
	$probes_status = zbx_objectValues($data['probes'], 'status');

	$table = (new CTableInfo())
		->setMultirowHeader([
			(new CTag('tr', true))
				->addItem((new CTag('th', true, _('Probe ID')))
					->setAttribute('rowspan', 2)
					->setAttribute('style', 'border-left: 0px;')
				)
				->addItem((new CTag('th', true, _('RDAP')))
					->setAttribute('colspan', 5)
					->setAttribute('class', 'center')
				),
			(new CTag('tr', true))
				->addItem((new CTag('th', true, _('Status'))))
				->addItem((new CTag('th', true, _('IP'))))
				->addItem((new CTag('th', true, _('Target'))))
				->addItem((new CTag('th', true, _('Tested name'))))
				->addItem((new CTag('th', true, _('RTT'))))
		], 5)
		->setAttribute('class', 'list-table table-bordered-head');
}
elseif ($data['type'] == RSM_RDDS) {
	/**
	 * If 'status' is not set, probe is UP. So, we need to check if all (length of $probes_status = 0) or
	 * at least one (array_sum($probes_status) > 0) probe is UP.
	 */
	$probes_status = zbx_objectValues($data['probes'], 'status');

	$row_1 = (new CTag('tr', true))
		->addItem((new CTag('th', true, _('Probe ID')))->setAttribute('rowspan', 2)->setAttribute('style', 'border-left: 0px;'))
		->addItem((new CTag('th', true, _('RDDS43')))->setAttribute('colspan', 5)->setAttribute('class', 'center'))
		->addItem((new CTag('th', true, _('RDDS80')))->setAttribute('colspan', 4)->setAttribute('class', 'center'));

	$row_2 = (new CTag('tr', true))
		->addItem((new CTag('th', true, _('Status'))))
		->addItem((new CTag('th', true, _('IP'))))
		->addItem((new CTag('th', true, _('Target'))))
		->addItem((new CTag('th', true, _('Tested name'))))
		->addItem((new CTag('th', true, _('RTT'))))
		->addItem((new CTag('th', true, _('Status'))))
		->addItem((new CTag('th', true, _('IP'))))
		->addItem((new CTag('th', true, _('Target'))))
		->addItem((new CTag('th', true, _('RTT'))));

	if ($rdap_is_part_of_rdds) {
		$row_1->addItem(
			(new CTag('th', true, _('RDAP')))
				->setAttribute('colspan', 5)
				->setAttribute('class', 'center')
		);

		$row_2
			->addItem((new CTag('th', true, _('Status'))))
			->addItem((new CTag('th', true, _('IP'))))
			->addItem((new CTag('th', true, _('Target'))))
			->addItem((new CTag('th', true, _('Tested name'))))
			->addItem((new CTag('th', true, _('RTT'))));
	}

	$column_count = $rdap_is_part_of_rdds ? 12 : 8;
	$table = (new CTableInfo())
		->setMultirowHeader([$row_1, $row_2], $column_count)
		->setAttribute('class', 'list-table table-bordered-head');
}
else {
	$table = (new CTableInfo())->setHeader([
		_('Probe ID'),
		_('Row result'),
		_('IP'),
		_('Login'),
		_('Update'),
		_('Info')
	]);
}

$down = (new CSpan(_('Down')))->addClass(ZBX_STYLE_RED);
$offline = (new CSpan(_('Offline')))->addClass(ZBX_STYLE_GREY);
$no_result = (new CSpan(_('No result')))->addClass(ZBX_STYLE_GREY);
$disabled = (new CSpan(_('Disabled')))->addClass(ZBX_STYLE_GREY);
$up = (new CSpan(_('Up')))->addClass(ZBX_STYLE_GREEN);

$offline_probes = 0;
$no_result_probes = 0;
$rdds80_above_max_rtt = 0;
$rdds43_above_max_rtt = 0;
$rdap_above_max_rtt = 0;

$down_probes = 0;

$show_totals = false;

foreach ($data['probes'] as $probe) {
	$status = null;

	if (isset($probe['rdds43']['rtt']) || isset($probe['rdds80']['rtt']) || isset($probe['rdap']['rtt'])) {
		$show_totals = true;
	}

	if (isset($probe['status']) && $probe['status'] === PROBE_DOWN) {
		if ($data['type'] == RSM_RDAP) {
			$rdap = $offline;
		}
		elseif ($data['type'] == RSM_RDDS) {
			$rdds = ZBX_STYLE_GREY;
			$rdds43 = $offline;
			$rdds80 = $offline;
			$rdap = $offline;
		}

		$offline_probes++;
	}
	else {
		$probe_down = false;
		$probe_no_result = false;
		$rdds = ZBX_STYLE_GREEN;

		if ($data['type'] == RSM_RDDS) {
			// RDDS
			if ((!isset($probe['rdds']['status']) || $probe['rdds']['status'] === null)
					&& (!isset($probe['rdap']['status']) || $probe['rdap']['status'] === null)) {
				$rdds = ZBX_STYLE_GREY;
				$probe_no_result = true;
			}
			elseif (isset($probe['rdds']['status']) && $probe['rdds']['status'] !== null) {
				if ($probe['rdds']['status'] == 0) {
					$rdds = ZBX_STYLE_RED;
					$probe_down = true;
				}
			}
			elseif (isset($probe['rdap']['status']) && $probe['rdap']['status'] !== null) {
				if ($probe['rdap']['status'] == 0) {
					$rdds = ZBX_STYLE_RED;
					$probe_down = true;
				}
			}

			if (isset($data['tld']['macros'][RSM_TLD_RDDS43_ENABLED])
					&& $data['tld']['macros'][RSM_TLD_RDDS43_ENABLED] == 0) {
				$rdds43 = $disabled;
			}
			elseif (!isset($probe['rdds43']['status']) || $probe['rdds43']['status'] === null) {
				$rdds43 = $no_result;
			}
			elseif ($probe['rdds43']['status'] == 0) {
				$rdds43 = $down;
			}
			elseif ($probe['rdds43']['status'] == 1) {
				$rdds43 = $up;
			}

			if (isset($data['tld']['macros'][RSM_TLD_RDDS80_ENABLED])
					&& $data['tld']['macros'][RSM_TLD_RDDS80_ENABLED] == 0) {
				$rdds80 = $disabled;
			}
			elseif (!isset($probe['rdds80']['status']) || $probe['rdds80']['status'] === null) {
				$rdds80 = $no_result;
			}
			elseif ($probe['rdds80']['status'] == 0) {
				$rdds80 = $down;
			}
			elseif ($probe['rdds80']['status'] == 1) {
				$rdds80 = $up;
			}

			if (isset($data['tld']['macros'][RSM_RDAP_TLD_ENABLED])
					&& $data['tld']['macros'][RSM_RDAP_TLD_ENABLED] == 0) {
				$rdap = $disabled;
			}
			elseif (!isset($probe['rdap']['status']) || $probe['rdap']['status'] === null) {
				$rdap = $no_result;
			}
			elseif ($probe['rdap']['status'] == 0) {
				$rdap = $down;
			}
			elseif ($probe['rdap']['status'] == 1) {
				$rdap = $up;
			}
		}
		else {
			// RDAP
			if (isset($data['tld']['macros'][RSM_RDAP_TLD_ENABLED])
					&& $data['tld']['macros'][RSM_RDAP_TLD_ENABLED] == 0) {
				$rdap = $disabled;
			}
			elseif (!isset($probe['rdap']['status']) || $probe['rdap']['status'] === null) {
				$rdap = $no_result;
			}
			elseif ($probe['rdap']['status'] == 0) {
				$rdds = ZBX_STYLE_RED;
				$probe_down = true;
				$rdap = $down;
			}
			elseif ($probe['rdap']['status'] == 1) {
				if ($data['type'] == RSM_RDDS && $rdds !== ZBX_STYLE_RED) {
					$rdds = ZBX_STYLE_GREEN;
				}

				$rdap = $up;
			}

			/**
			 * An exception: if sub-service is disabled at TLD level, sub-services should be disabled at probe level
			 * too. This need to be added as exception because in case if sub-service is disabled at TLD level, we never
			 * request values of related items. As the result, we cannot detect what is a reason why there are no
			 * results for sub-service.
			 *
			 * See issue 386 for more details.
			 */

			if ($data['tld_rdds_enabled'] == false) {
				if (isset($rdds43) && $rdds43 === $no_result) {
					$rdds43 = $disabled;
				}

				if (isset($rdds80) && $rdds80 === $no_result) {
					$rdds80 = $disabled;
				}

				if (isset($rdds43) && $rdds43 === $no_result && isset($rdds80) && $rdds80 === $no_result) {
					$probe_no_result = false;
				}
			}

			/**
			 * Another exception: if RDDS is disabled at probe level, this is another case when we don't request
			 * data and cannot distinguish when probe has no data and when it is disabled. So, let's use macros.
			 *
			 * Macros {$RSM.RDDS.ENABLED} is used to disable all 3 sub-services, so, if its 0, all three are displayed
			 * as disabled.
			 */
			elseif (isset($probe['macros'][RSM_RDDS_ENABLED]) && $probe['macros'][RSM_RDDS_ENABLED] == 0) {
				$rdds43 = $disabled;
				$rdds80 = $disabled;
				$rdap = $disabled;
			}

			if ($data['type'] == RSM_RDAP) {
				if ($rdap === $disabled || $rdap === $no_result) {
					$probe_no_result = true;
					$probe_down = false;
					$rdds = ZBX_STYLE_GREY;
				}
			}
			elseif (($rdap_is_part_of_rdds && ($rdap === $disabled || $rdap === $no_result))
					&& ($rdds43 === $disabled || $rdds43 === $no_result)
					&& ($rdds80 === $disabled || $rdds80 === $no_result)) {
				$probe_no_result = true;
				$probe_down = false;
				$rdds = ZBX_STYLE_GREY;
			}
		}
	}

	if ($probe_down) {
		$down_probes++;
	}
	elseif ($probe_no_result) {
		$no_result_probes++;
	}

	if ($data['type'] == RSM_RDDS) {
		$rdap_rtt = '';
		$rdds43_rtt = '';
		$rdds80_rtt = '';

		if (isset($probe['rdds43']['rtt'])) {
			$rdds43_rtt = (new CSpan($probe['rdds43']['rtt']['value']))
				->setAttribute('class', $rdds43 === $down ? ZBX_STYLE_RED : ZBX_STYLE_GREEN);

			if ($probe['rdds43']['rtt']['description']) {
				$rdds43_rtt->setAttribute('title', $probe['rdds43']['rtt']['description']);
			}
		}

		if (isset($probe['rdds80']['rtt'])) {
			$rdds80_rtt = (new CSpan($probe['rdds80']['rtt']['value']))
				->setAttribute('class', $rdds80 === $down ? ZBX_STYLE_RED : ZBX_STYLE_GREEN);

			if ($probe['rdds80']['rtt']['description']) {
				$rdds80_rtt->setAttribute('title', $probe['rdds80']['rtt']['description']);
			}
		}

		if ($rdap_is_part_of_rdds && isset($probe['rdap']['rtt'])) {
			$rdap_rtt = (new CSpan($probe['rdap']['rtt']['value']))
				->setAttribute('class', $rdap === $down ? ZBX_STYLE_RED : ZBX_STYLE_GREEN);

			if ($probe['rdap']['rtt']['description']) {
				$rdap_rtt->setAttribute('title', $probe['rdap']['rtt']['description']);
			}
		}

		$row = [
			(new CSpan($probe['name']))->addClass($rdds),
			$rdds43,
			(isset($probe['rdds43']['ip']) && $probe['rdds43']['ip'])
				? (new CSpan($probe['rdds43']['ip']))->setAttribute('class', $rdds43 === $down ? ZBX_STYLE_RED : ZBX_STYLE_GREEN)
				: '',
			(isset($probe['rdds43']['target']) && $probe['rdds43']['target'])
				? $probe['rdds43']['target']
				: '',
			(isset($probe['rdds43']['testedname']) && $probe['rdds43']['testedname'])
				? $probe['rdds43']['testedname']
				: '',
			$rdds43_rtt,
			$rdds80,
			(isset($probe['rdds80']['ip']) && $probe['rdds80']['ip'])
				? (new CSpan($probe['rdds80']['ip']))->setAttribute('class', $rdds80 === $down ? ZBX_STYLE_RED : ZBX_STYLE_GREEN)
				: '',
			(isset($probe['rdds80']['target']) && $probe['rdds80']['target'])
				? $probe['rdds80']['target']
				: '',
			$rdds80_rtt
		];

		if ($rdap_is_part_of_rdds) {
			$row = array_merge($row, [
				$rdap,
				(isset($probe['rdap']['ip']) && $probe['rdap']['ip'])
					? (new CSpan($probe['rdap']['ip']))->setAttribute('class', $rdap === $down ? ZBX_STYLE_RED : ZBX_STYLE_GREEN)
					: '',
				(isset($probe['rdap']['target']) && $probe['rdap']['target'])
					? $probe['rdap']['target']
					: '',
				(isset($probe['rdap']['testedname']) && $probe['rdap']['testedname'])
					? $probe['rdap']['testedname']
					: '',
				$rdap_rtt
			]);
		}

		/**
		 * If $rddsNN is DOWN and RTT is non-negative, it is considered as above max RTT.
		 *
		 * Following scenarios are possible:
		 * - If RTT is negative, it is an error and is considered as DOWN.
		 * - If RTT is positive but $rddsNN is still DOWN, it indicates that at the time of calculation, RTT was greater
		 *	 than max allowed RTT.
		 * - If RTT is positive but $rddsNN is UP, it indicates that at the time of calculation, RTT was in the range of
		 *	 allowed values - greater than 0 (was not an error) and smaller than max allowed RTT.
		 */
		if ($rdds80 === $down && isset($probe['rdds80']['rtt']) && $probe['rdds80']['rtt']['value'] > 0) {
			$rdds80_above_max_rtt++;
		}
		if ($rdds43 === $down && isset($probe['rdds43']['rtt']) && $probe['rdds43']['rtt']['value'] > 0) {
			$rdds43_above_max_rtt++;
		}
		if ($rdap_is_part_of_rdds && $rdap === $down && isset($probe['rdap']['rtt']) && $probe['rdap']['rtt']['value'] > 0) {
			$rdap_above_max_rtt++;
		}
	}
	elseif ($data['type'] == RSM_RDAP) {
		$rdap_rtt = '';

		if (isset($probe['rdap']['rtt'])) {
			$rdap_rtt = (new CSpan($probe['rdap']['rtt']['value']))
				->setAttribute('class', $rdap === $down ? ZBX_STYLE_RED : ZBX_STYLE_GREEN);

			if ($probe['rdap']['rtt']['description']) {
				$rdap_rtt->setAttribute('title', $probe['rdap']['rtt']['description']);
			}
		}

		$row = [
			$probe['name'],
			$rdap,
			(isset($probe['rdap']['ip']) && $probe['rdap']['ip'])
				? (new CSpan($probe['rdap']['ip']))->setAttribute('class', $rdap === $down ? ZBX_STYLE_RED : ZBX_STYLE_GREEN)
				: '',
			(isset($probe['rdap']['target']) && $probe['rdap']['target'])
				? $probe['rdap']['target']
				: '',
			(isset($probe['rdap']['testedname']) && $probe['rdap']['testedname'])
				? $probe['rdap']['testedname']
				: '',
			$rdap_rtt
		];

		if ($rdap === $down && isset($probe['rdap']['rtt']) && $probe['rdap']['rtt']['value'] > 0) {
			$rdap_above_max_rtt++;
		}
	}

	$table->addRow($row);
}

// Add table footer rows:
if ($data['type'] == RSM_RDAP) {
	foreach ($data['errors'] as $error_code => $error) {
		$table->addRow([
			(new CSpan(_('Total ') . $error_code))->setAttribute('title', $error['description']),
			'',
			'',
			'',
			'',
			array_key_exists('rdap', $error) ? $error['rdap'] : ''
		]);
	}

	if ($show_totals) {
		$table->addRow([_('Total above max. RTT'), '', '', '', '', $rdap_above_max_rtt]);
	}
}
elseif ($data['type'] == RSM_RDDS) {
	foreach ($data['errors'] as $error_code => $error) {
		$row = [
			(new CSpan(_('Total ') . $error_code))->setAttribute('title', $error['description']),
			'',
			'',
			'',
			'',
			array_key_exists('rdds43', $error) ? $error['rdds43'] : '',
			'',
			'',
			'',
			array_key_exists('rdds80', $error) ? $error['rdds80'] : ''
		];

		if ($rdap_is_part_of_rdds) {
			$row = array_merge($row, ['', '', '', '', array_key_exists('rdap', $error) ? $error['rdap'] : '']);
		}

		$table->addRow($row);
	}

	$row = [
		_('Total above max. RTT'),
		'',
		'',
		'',
		'',
		$rdds43_above_max_rtt,
		'',
		'',
		'',
		$rdds80_above_max_rtt
	];

	if ($rdap_is_part_of_rdds) {
		$row = array_merge($row, ['', '', '', '', $rdap_above_max_rtt]);
	}

	if ($show_totals) {
		$table->addRow($row);
	}
}

if ($data['type'] == RSM_RDDS || $data['type'] == RSM_RDAP) {
	$additionInfo = [
		new CSpan([bold(_('Probes total')), ':', SPACE, $data['totalProbes']]),
		BR(),
		new CSpan([bold(_('Probes offline')), ':', SPACE, $offline_probes]),
		BR(),
		new CSpan([bold(_('Probes with No Result')), ':', SPACE, $no_result_probes]),
		BR(),
		new CSpan([bold(_('Probes with Result')), ':', SPACE,
			$data['totalProbes'] - $offline_probes - $no_result_probes
		]),
		BR(),
		new CSpan([bold(_('Probes Up')), ':', SPACE,
			$data['totalProbes'] - $offline_probes - $no_result_probes - $down_probes
		]),
		BR(),
		new CSpan([bold(_('Probes Down')), ':', SPACE, $down_probes])
	];
}

if (in_array($data['type'], [RSM_RDDS, RSM_RDAP])) {
	$test_result = $data['test_result'];
}
else {
	if ($data['test_result'] === null) {
		$test_result = $no_result;
	}
	elseif ($data['test_result'] == PROBE_UP) {
		$test_result = $up;
	}
	else {
		$test_result = $down;
	}
}

$details = [$object_label => $data['tld']['host']];

if ($data['rsm_monitoring_mode'] === MONITORING_TARGET_REGISTRAR) {
	$details += [
		_('Registrar name') => $data['tld']['info_1'],
		_('Registrar family') => $data['tld']['info_2']
	];
}

if ($data['type'] == RSM_RDAP) {
	$allowed_rtt_str = isset($data[CALCULATED_ITEM_RDAP_RTT_HIGH])
		? sprintf('%s ms', $data[CALCULATED_ITEM_RDAP_RTT_HIGH])
		: _('No data');
}
elseif ($data['is_rdap_standalone']) {
	$allowed_rtt_str = isset($data[CALCULATED_ITEM_RDDS_RTT_HIGH])
		? sprintf('%s ms', $data[CALCULATED_ITEM_RDDS_RTT_HIGH])
		: _('No data');
}
else {
	$allowed_rtt_str = isset($data[CALCULATED_ITEM_RDDS_RTT_HIGH])
		? sprintf('RDDS - %s ms, RDAP - %s ms', $data[CALCULATED_ITEM_RDDS_RTT_HIGH], $data[CALCULATED_ITEM_RDAP_RTT_HIGH])
		: _('No data');
}

$details += [
	_('Service') => $data['slvItem']['name'],
	_('Test time') => date(DATE_TIME_FORMAT_SECONDS, $data['time']),
	_('Test result') => $test_result,
	_('Max allowed RTT') => $allowed_rtt_str,
	_('Note') => _(
		'The following table displays the data that has been received by the central node, some of'.
		' the values may not have been available at the time of the calculation of the "Test result"'
	)
];

$right_details = [
	_('Probes total') => $data['totalProbes'],
	_('Probes offline') => $offline_probes,
	_('Probes with No Result') => $no_result_probes,
	_('Probes with Result') => $data['totalProbes'] - $offline_probes - $no_result_probes,
];

$right_details += [
	_('Probes Up') => $data['totalProbes'] - $offline_probes - $no_result_probes - $down_probes,
	_('Probes Down') => $down_probes
];

(new CWidget())
	->setTitle($data['title'])
	->additem((new CDiv())
		->addClass(ZBX_STYLE_TABLE_FORMS_CONTAINER)
		->addItem((new CTable())
			->addClass('incidents-info')
			->addRow([
				gen_details_item($details),
				gen_details_item($right_details),
			])
		)
	)
	->addItem($table)
	->addItem($data['module_style'])
	->show();
