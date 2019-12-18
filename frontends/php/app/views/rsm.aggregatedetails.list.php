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


// Create table header.
$row_1 = (new CTag('tr', true))
	->addItem((new CTag('th', true, _('Probe ID')))->setAttribute('rowspan', 3)
		->setAttribute('style', 'border-left: 0px;'));

$row_2 = (new CTag('tr', true))
	->addItem((new CTag('th', true, _('Status')))->setAttribute('rowspan', 2))
	->addItem((new CTag('th', true, _('Name Servers')))
		->setAttribute('colspan', 2)
		->setAttribute('class', 'center'));

$row_3 = [_('UP'), _('DOWN')];

if (array_key_exists('dns_udp_nameservers', $data)) {
	foreach ($data['dns_udp_nameservers'] as $ns_name => $ns_ips) {
		$row_3[] = [_('Status')];
		$ips = [];

		if (array_key_exists('ipv4', $ns_ips)) {
			$ips = $ns_ips['ipv4'];
		}

		if (array_key_exists('ipv6', $ns_ips)) {
			// Compress IPv6 address to save space in table header.
			$ipv6_ips = array_keys($ns_ips['ipv6']);
			foreach ($ipv6_ips as &$ipv6) {
				$ipv6 = inet_ntop(inet_pton($ipv6));
			}
			unset($ipv6);

			$ips = array_merge($ips, $ipv6_ips);
		}

		foreach (array_keys($ips) as $ip) {
			$row_3[] = $ip;
			$row_3[] = _('NSID');
		}


		$row_2
			->addItem((new CTag('th', true, $ns_name))
			->setAttribute('colspan', 1 + count($ips) * 2)
			->setAttribute('class', 'center'));
	}
}

$row_1->addItem((new CTag('th', true, _('DNS UDP')))
	->setAttribute('colspan', count($row_3) + 1)
	->setAttribute('class', 'center'));

$table = (new CTableInfo())
	->setMultirowHeader([$row_1, $row_2, new CRowHeader($row_3)], count($row_3) + 4)
	->setAttribute('class', 'list-table table-bordered-head');

$down = (new CSpan(_('Down')))->addClass(ZBX_STYLE_RED);
$offline = (new CSpan(_('Offline')))->addClass(ZBX_STYLE_GREY);
$no_result = (new CSpan(_('No result')))->addClass(ZBX_STYLE_GREY);
$disabled = (new CSpan(_('Disabled')))->addClass(ZBX_STYLE_GREY);
$up = (new CSpan(_('Up')))->addClass(ZBX_STYLE_GREEN);

// Results summary.
$offline_probes = 0;
$no_result_probes = 0;
$down_probes = 0;

// Add results for each probe.
foreach ($data['probes'] as $probe) {
	$probe_disabled = (array_key_exists($probe['host'], $data['probes_status']) && $data['probes_status'][$probe['host']] == 1);

	if ($probe_disabled) {
		$udp_status = $disabled;
		$probe_status_color = ZBX_STYLE_GREY;
		$no_result_probes++;
	}
	elseif (array_key_exists('status_udp', $probe)) {
		if ($probe['status_udp'] == PROBE_OFFLINE) {
			$probe_status_color = ZBX_STYLE_GREY;
			$udp_status = $offline;
			$offline_probes++;
		}
		elseif ($probe['status_udp'] == PROBE_DOWN) {
			$probe_status_color = ZBX_STYLE_RED;
			$udp_status = $down;
			$down_probes++;
		}
		elseif ($probe['status_udp'] == PROBE_UP) {
			$probe_status_color = ZBX_STYLE_GREEN;
			$udp_status = $up;
		}
	}
	else {
		$udp_status = $no_result;
		$probe_status_color = ZBX_STYLE_GREY;
		$no_result_probes++;
	}

	$row = [
		(new CSpan($probe['name']))->addClass($probe_status_color),
		$udp_status,
		$probe_disabled ? '-' : $probe['udp_ns_up'],
		$probe_disabled ? '-' : $probe['udp_ns_down']
	];

	if (array_key_exists('dns_udp_nameservers', $data)) {
		foreach ($data['dns_udp_nameservers'] as $dns_udp_ns => $ipvs) {
			if (array_key_exists('results_udp', $probe) && array_key_exists($dns_udp_ns, $probe['results_udp'])) {
				if (array_key_exists('status', $probe['results_udp'][$dns_udp_ns])) {
					$row[] = ($probe['results_udp'][$dns_udp_ns]['status'] == NAMESERVER_DOWN) ? $down : $up;
				}
				else {
					$row[] = '-';
				}

				foreach (['ipv4', 'ipv6'] as $ipv) {
					if (array_key_exists($ipv, $probe['results_udp'][$dns_udp_ns])) {
						foreach (array_keys($ipvs[$ipv]) as $ip) {
							if (array_key_exists($ip, $probe['results_udp'][$dns_udp_ns][$ipv])) {
								$result = $probe['results_udp'][$dns_udp_ns][$ipv][$ip];
								$is_dns_error = isServiceErrorCode($result, $data['type']);

								if ($result == 0) {
									$row[] = '-';
								}
								elseif (0 > $result) {
									$row[] = (new CSpan($result))
										->setHint($data['error_msgs'][$result])
										->setAttribute('class', $is_dns_error ? ZBX_STYLE_RED : ZBX_STYLE_GREEN);
								}
								elseif ($result > $data['udp_rtt']) {
									$row[] = (new CSpan($result))
										->setAttribute('class', ZBX_STYLE_RED);
								}
								else {
									$row[] = (new CSpan($result))
										->setAttribute('class', ZBX_STYLE_GREEN);
								}

								if (isset($probe['results_nsid'][$dns_udp_ns][$ip])) {
									$nsid_index = $probe['results_nsid'][$dns_udp_ns][$ip];
									$row[] = (new CDiv($nsid_index + 1))->setHint($data['nsids'][$nsid_index]);
								}
								else {
									$row[] = (new CDiv('-'))->setHint(_('No value.'), '', false);
								}
							}
							else {
								$row[] = '-';
								$row[] = '';
							}
						}
					}
					else {
						if (array_key_exists($ipv, $ipvs)) {
							$cell_cnt = count($ipvs[$ipv]);
						}
						elseif (array_key_exists($ipv, $ns_ips)) {
							$cell_cnt = count($ns_ips[$ipv]);
						}
						else {
							$cell_cnt = 0;
						}

						$row[] = ($cell_cnt > 1)
							? (new CCol('-'))->setColSpan($cell_cnt * 2)
							: ($cell_cnt ? '-' : null);
					}
				}
			}
			else {
				$cell_cnt = 1;
				$cell_cnt += array_key_exists('ipv4', $ipvs) ? count($ipvs['ipv4']) : 0;
				$cell_cnt += array_key_exists('ipv6', $ipvs) ? count($ipvs['ipv6']) : 0;
				$row[] = ($cell_cnt > 1) ? (new CCol('-'))->setColSpan($cell_cnt * 2) : '-';
			}
		}
	}

	$table->addRow($row);
}

// Add error rows at the bottom of table.
foreach ($data['errors'] as $error_code => $errors) {
	$row = [(new CSpan(_('Total ') . $error_code))->setHint($data['error_msgs'][$error_code]), '', '', ''];

	// Add number of error cells.
	if (array_key_exists('dns_udp_nameservers', $data)) {
		foreach ($data['dns_udp_nameservers'] as $ns_name => $ns_ips) {
			$row[] = '';

			if (array_key_exists('ipv4', $ns_ips)) {
				foreach (array_keys($ns_ips['ipv4']) as $ip) {
					$error_key = 'udp_'.$ns_name.'_ipv4_'.$ip;
					$row[] = array_key_exists($error_key, $errors) ? $errors[$error_key] : '';
					$row[] = '';
				}
			}

			if (array_key_exists('ipv6', $ns_ips)) {
				foreach (array_keys($ns_ips['ipv6']) as $ip) {
					$error_key = 'udp_'.$ns_name.'_ipv6_'.$ip;
					$row[] = array_key_exists($error_key, $errors) ? $errors[$error_key] : '';
					$row[] = '';
				}
			}
		}
	}

	$table->addRow($row);
}

// Add 'Total above max rtt' row:
if ($data['type'] == RSM_DNS) {
	$row = [_('Total above max. RTT'), '', '', ''];
	if (array_key_exists('dns_udp_nameservers', $data)) {
		foreach ($data['dns_udp_nameservers'] as $ns_name => $ns_ips) {
			$row[] = '';

			if (array_key_exists('ipv4', $ns_ips)) {
				foreach (array_keys($ns_ips['ipv4']) as $ipv => $ip) {
					$error_key = 'udp_'.$ns_name.'_ipv4_'.$ip;
					$row[] = array_key_exists($error_key, $data['probes_above_max_rtt']) ? $data['probes_above_max_rtt'][$error_key] : '0';
					$row[] = '';
				}
			}

			if (array_key_exists('ipv6', $ns_ips)) {
				foreach (array_keys($ns_ips['ipv6']) as $ipv => $ip) {
					$error_key = 'udp_'.$ns_name.'_ipv6_'.$ip;
					$row[] = array_key_exists($error_key, $data['probes_above_max_rtt']) ? $data['probes_above_max_rtt'][$error_key] : '0';
					$row[] = '';
				}
			}
		}
	}
	$table->addRow($row);
}

$test_result = new CSpan($data['testResultLabel']);
if ($data['testResult'] === null) {
	$test_result->addClass(ZBX_STYLE_GREY);
}
elseif ($data['testResult'] == PROBE_DOWN) {
	$test_result->addClass(ZBX_STYLE_RED);
}
else {
	$test_result->addClass(ZBX_STYLE_GREEN);
}

// NSID index/value table
$nsids_table = null;

if ($data['nsids']) {
	$nsids_table = (new CTable())
		->setHeader([(new CColHeader(_('Numeric NSID')))->setWidth('1%'), _('Real NSID')])
		->setAttribute('class', ZBX_STYLE_LIST_TABLE);

	foreach ($data['nsids'] as $index => $value) {
		$nsids_table->addRow([(new CCol($index + 1))->addClass(ZBX_STYLE_CENTER), $value]);
	}

	$table = [new CTag('p', true, $table), $nsids_table];
}

(new CWidget())
	->setTitle($data['title'])
	->additem((new CDiv())
		->addClass(ZBX_STYLE_TABLE_FORMS_CONTAINER)
		->addItem((new CTable())
			->addClass('incidents-info')
			->addRow([
				gen_details_item([
					_('TLD') => $data['tld']['host'],
					_('Service') => $data['slvItem']['name'],
					_('Test time') => date(DATE_TIME_FORMAT_SECONDS, $data['time']),
					_('Test result') => [$test_result, ' ', _s('(calculated at %1$s)', date(DATE_TIME_FORMAT_SECONDS, $data['time'] + RSM_ROLLWEEK_SHIFT_BACK))],
					_('Note') => _(
						'The following table displays the data that has been received by the central node, some of'.
						' the values may not have been available at the time of the calculation of the "Test result"'
					)
				]),
				gen_details_item([
					_('Probes total') => $data['totalProbes'],
					_('Probes offline') => $offline_probes,
					_('Probes with No Result') => $no_result_probes,
					_('Probes with Result') => $data['totalProbes'] - $offline_probes - $no_result_probes,
					_('Probes Up') => $data['totalProbes'] - $offline_probes - $no_result_probes - $down_probes,
					_('Probes Down') => $down_probes,
				]),
			])
		)
	)
	->addItem($table)
	->show();
