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

// NSIDs converted from hex to ASCII
$nsids_converted = [];
if (isset($data['nsids'])) {
	foreach ($data['nsids'] as $index => $value) {
		$value = chunk_split($value, 2, ' ').' ("'.hex2bin($value).'")';
		$nsids_converted[$index] = $value;
	}
}

// Create table header.
$rows = [
	(new CRow(null, true))
		->addItem((new CColHeader(_('Probe ID')))->setRowSpan(2))
		->addItem((new CColHeader(_('Status')))->setRowSpan(2))
		->addItem((new CColHeader(_('Transport')))->setRowSpan(2))
		->addItem((new CColHeader(_('Name Servers')))->setColSpan(2)),
	[_('UP'), _('DOWN')]
];

if (array_key_exists('dns_nameservers', $data)) {
	foreach ($data['dns_nameservers'] as $ns_name => $ns_ips) {
		$ips = array_key_exists('ipv4', $ns_ips) ? array_keys($ns_ips['ipv4']): [];

		if (array_key_exists('ipv6', $ns_ips)) {
			// Compress IPv6 address to save space in table header.
			$ips = array_merge($ips, array_map('inet_ntop', array_map('inet_pton', array_keys($ns_ips['ipv6']))));
		}

		$rows[1][] = _('Status');

		foreach ($ips as $ip) {
			$rows[1][] = $ip;
			$rows[1][] = _('NSID');
		}

		$rows[0]->addItem((new CColHeader($ns_name))->setColSpan(1 + count($ips) * 2)->addClass('center'));
	}
}

$column_count = count($rows[1]) + 4;
$rows[1] = new CRowHeader($rows[1]);
$table = (new CTableInfo())
	->setMultirowHeader($rows, $column_count)
	->addClass('table-bordered-head');

$up = (new CSpan(_('Up')))->addClass(ZBX_STYLE_GREEN);
$down = (new CSpan(_('Down')))->addClass(ZBX_STYLE_RED);
$offline = (new CSpan(_('Offline')))->addClass(ZBX_STYLE_GREY);
$no_result = (new CSpan(_('No result')))->addClass(ZBX_STYLE_GREY);
$disabled = (new CSpan(_('Disabled')))->addClass(ZBX_STYLE_GREY);

// Results summary.
$offline_probes = 0;
$no_result_probes = 0;
$down_probes = 0;

// Add results for each probe.
foreach ($data['probes'] as $probe) {
	if ($probe['probe_status'] == PROBE_DISABLED) {
		$probe_status = $disabled;
		$probe_status_color = ZBX_STYLE_GREY;
		$no_result_probes++;
	}
	elseif ($probe['probe_status'] == PROBE_OFFLINE) {
		$probe_status = $offline;
		$probe_status_color = ZBX_STYLE_GREY;
		$offline_probes++;
	}
	elseif ($probe['probe_status'] == PROBE_DOWN) {
		$probe_status = $down;
		$probe_status_color = ZBX_STYLE_RED;
		$down_probes++;
	}
	elseif ($probe['probe_status'] == PROBE_UP) {
		$probe_status = $up;
		$probe_status_color = ZBX_STYLE_GREEN;
	}
	elseif ($probe['probe_status'] == PROBE_NORESULT) {
		$probe_status = $no_result;
		$probe_status_color = ZBX_STYLE_GREY;
		$no_result_probes++;
	}
	else {
		error("Internal error: unknown probe_status of probe \"" . $probe['host'] . "\": " . $probe['probe_status']);
	}

	$row = [
		(new CSpan($probe['host']))->addClass($probe_status_color),
		$probe_status,
		isset($probe['transport']) ? $probe['transport'] : '',
		($probe['probe_status'] == PROBE_UP || $probe['probe_status'] == PROBE_DOWN) ? $probe['ns_up'] : '',
		($probe['probe_status'] == PROBE_UP || $probe['probe_status'] == PROBE_DOWN) ? $probe['ns_down'] : '',
	];

	if (isset($data['dns_nameservers'])) {
		foreach ($data['dns_nameservers'] as $dns_udp_ns => $ipvs) {
			if (array_key_exists('results', $probe) && array_key_exists($dns_udp_ns, $probe['results'])) {
				if (array_key_exists('status', $probe['results'][$dns_udp_ns])) {
					$row[] = ($probe['results'][$dns_udp_ns]['status'] == NAMESERVER_DOWN) ? $down : $up;
				}
				else {
					$row[] = '';
				}

				foreach (['ipv4', 'ipv6'] as $ipv) {
					if (array_key_exists($ipv, $probe['results'][$dns_udp_ns]) && $probe['results'][$dns_udp_ns][$ipv]) {
						foreach (array_keys($ipvs[$ipv]) as $ip) {
							if (array_key_exists($ip, $probe['results'][$dns_udp_ns][$ipv])) {
								$rtt = $probe['results'][$dns_udp_ns][$ipv][$ip];

								$nskey = $dns_udp_ns.$ip;
								$span = new CSpan($rtt);
								$class = isset($probe['above_max_rtt'][$nskey]) ? ZBX_STYLE_RED : ZBX_STYLE_GREEN;

								if ($rtt < 0) {
									$class = ($class == ZBX_STYLE_GREEN && !isset($probe['dns_error'][$nskey]))
											? ZBX_STYLE_GREEN : ZBX_STYLE_RED;
									$span->setHint($data['test_error_message'][$rtt]);
								}

								$span->addClass($class);
								$row[] = $span;

								if (isset($probe['results_nsid'][$dns_udp_ns][$ip]) && is_numeric($probe['results_nsid'][$dns_udp_ns][$ip])) {
									$nsid_index = $probe['results_nsid'][$dns_udp_ns][$ip];
									$row[] = (new CDiv($nsid_index + 1))->setHint($nsids_converted[$nsid_index]);
								}
								else {
									$row[] = '';
								}
							}
							else {
								$row[] = '';
								$row[] = '';
							}
						}
					}
					else if (array_key_exists($ipv, $data['dns_nameservers'][$dns_udp_ns])) {
						$cell_cnt = count($data['dns_nameservers'][$dns_udp_ns][$ipv]) * 2;
						$cells = array_fill(1, (($cell_cnt > 1) ? $cell_cnt : 1), '');
						$row = array_merge($row, $cells);
					}
				}
			}
			else {
				$cell_cnt = array_key_exists('ipv4', $ipvs) ? count($ipvs['ipv4'])*2 : 0;
				$cell_cnt += array_key_exists('ipv6', $ipvs) ? count($ipvs['ipv6'])*2 : 0;
				$cells = array_fill(1, (($cell_cnt > 1) ? 1 + $cell_cnt : 1), '');
				$row = array_merge($row, $cells);
			}
		}
	}

	$table->addRow($row);
}

// Add total of errors at the bottom of the table.
foreach ($data['errors'] as $error_code => $errors) {
	$row = [(new CSpan(_('Total ') . $error_code))->setHint($data['test_error_message'][$error_code]), '', '', '', ''];

	foreach ($data['dns_nameservers'] as $ns_name => $ns_ips) {
		// 'Status' column is unused.
		$row[] = '';

		foreach (array_keys(array_reduce($ns_ips, 'array_merge', [])) as $ip) {
			$error_key = $ns_name.$ip;
			// 'IP' column.
			$row[] = array_key_exists($error_key, $errors) ? $errors[$error_key] : '';
			// 'NSID' column is unused.
			$row[] = '';
		}
	}

	$table->addRow($row);
}

// Add 'Total above max rtt' row:
if ($data['type'] == RSM_DNS && array_key_exists('dns_nameservers', $data)) {
	$row_udp = [_('Total above max. UDP RTT'), '', '', '', ''];
	$row_tcp = [_('Total above max. TCP RTT'), '', '', '', ''];

	foreach ($data['dns_nameservers'] as $ns_name => $ns_ips) {
		// Name Server 'Status' column for UDP and TCP must be empty
		$row_udp[] = '';
		$row_tcp[] = '';

		foreach (array_keys(array_reduce($ns_ips, 'array_merge', [])) as $ip) {
			$error_key = $ns_name.$ip;
			// 'IP' column
			$row_udp[] = array_key_exists($error_key, $data['probes_above_max_rtt'])
				? $data['probes_above_max_rtt'][$error_key]['UDP']
				: '0';
			$row_tcp[] = array_key_exists($error_key, $data['probes_above_max_rtt'])
				? $data['probes_above_max_rtt'][$error_key]['TCP']
				: '0';
			// 'NSID' column
			$row_udp[] = '';
			$row_tcp[] = '';
		}
	}

	$table
		->addRow($row_udp)
		->addRow($row_tcp);
}

$test_result = (new CSpan(_('No result')))->addClass(ZBX_STYLE_GREY);

if (array_key_exists('test_result', $data)) {
	$test_result = (new CSpan($data['test_status_message'][$data['test_result']]))
		->addClass($data['test_result'] == DOWN ? ZBX_STYLE_RED : ZBX_STYLE_GREEN);
}

// NSID index/value table
if (isset($data['nsids']) && count($data['nsids']) != 0) {
	$nsids_table = (new CTable())
		->setHeader([(new CColHeader(_('Numeric NSID')))->setWidth('1%'), _('Real NSID')])
		->setAttribute('class', ZBX_STYLE_LIST_TABLE);

	foreach ($data['nsids'] as $index => $value) {
		$nsids_table->addRow([(new CCol($index + 1))->addClass(ZBX_STYLE_CENTER), $nsids_converted[$index]]);
	}

	$table = [new CTag('p', true, $table), $nsids_table];
}

// for DNSSEC service, display specific DNSSEC errors
if (isset($data['dnssec_errors']) && count($data['dnssec_errors']) != 0) {
	$dnssec_errors_table = (new CTable())
		->setHeader([(new CColHeader(_('Error code')))->setWidth('1%'), _('Description')])
		->setAttribute('class', ZBX_STYLE_LIST_TABLE);

	foreach ($data['dnssec_errors'] as $code => $description) {
		$dnssec_errors_table->addRow([(new CCol($code))->addClass(ZBX_STYLE_CENTER), $description]);
	}

	$table = [new CTag('p', true, $table), $dnssec_errors_table];
}

$total_probes = count($data['probes']);

$details = [
	_('TLD') => $data['tld_host'],
	_('Service') => $data['slv_item_name'],
	_('Test time') => date(DATE_TIME_FORMAT_SECONDS, $data['time']),
	_('Test result') => $test_result,
	_('Max allowed RTT') => isset($data['udp_rtt'])
		? sprintf('UDP - %s ms, TCP - %s ms', $data['udp_rtt'], $data['tcp_rtt'])
		: _('No data'),
	_('Note') => _(
		'The following table displays the data that has been received by the central node, some of'.
		' the values may not have been available at the time of the calculation of the "Test result"'
	)
];

if ($data['type'] == RSM_DNSSEC) {
	$details[_('Note 2')] = _('See the list of specific DNSSEC errors in the table below');
}

(new CWidget())
	->setTitle($data['title'])
	->additem((new CDiv())
		->addClass(ZBX_STYLE_TABLE_FORMS_CONTAINER)
		->addItem((new CTable())
			->addClass('incidents-info')
			->addRow([
				gen_details_item($details),
				gen_details_item([
					_('Probes total') => $total_probes,
					_('Probes offline') => $offline_probes,
					_('Probes with No Result') => $no_result_probes,
					_('Probes with Result') => $total_probes - $offline_probes - $no_result_probes,
					_('Probes Up') => $total_probes - $offline_probes - $no_result_probes - $down_probes,
					_('Probes Down') => $down_probes,
				]),
			])
		)
	)
	->addItem($table)
	->addItem($data['module_style'])
	->show();
