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

$table = (new CTableInfo())->setHeader([
		'#',
		_('Probe Node'),
		_('Calculated'),
		_('Automatic'),
		_('Manual'),
		_('Last access'),
		_('Resolver'),
		_('Internal errors')
]);

$index = 1;
foreach ($data['probes'] as $probe => $values) {
	// defaults, if empty
	$elements['probe'] = $probe;
	$elements['calculated'] = '';
	$elements['automatic'] = '';
	$elements['manual'] = '';
	$elements['lastaccess'] = '';
	$elements['resolver'] = '';
	$elements['errors'] = '';

	if (isset($values['mainstatus']['value'])) {
		$time = elapsedTime('@'.$values['mainstatus']['clock']);

		$elements['probe'] = (new CSpan($probe))->addClass($values['mainstatus']['value'] == 0 ? ZBX_STYLE_RED : ZBX_STYLE_GREEN);
		$elements['calculated'] = ($time ? $time : (new CSpan('Never'))->addClass(ZBX_STYLE_RED));
	}

	if (isset($values['automatic']['value'])) {
		$value = $values['automatic']['value'] == 0 ? 'Down' : 'Up';
		$style = $values['automatic']['value'] == 0 ? ZBX_STYLE_RED : null;
		$time = elapsedTime('@'.$values['automatic']['clock']);

		$elements['automatic'] = (new CSpan($value))->addClass($style)->setAttribute('title', $time);
	}

	if (isset($values['manual']['value'])) {
		$value = $values['manual']['value'] == 0 ? 'Offline' : 'Up';
		$style = $values['manual']['value'] == 0 ? ZBX_STYLE_GREY : null;
		$time = elapsedTime('@'.$values['manual']['clock']);

		$elements['manual'] = (new CSpan($value))->addClass($style)->setAttribute('title', $time);
	}

	if (isset($values['lastaccess']['value'])) {
		$lastaccess_limit = isset($data['lastaccess_limit']) ? $data['lastaccess_limit'] : 0;

		$value = elapsedTime('@'.$values['lastaccess']['value']);
		$style = (time() - $values['lastaccess']['value']) > $lastaccess_limit ? ZBX_STYLE_RED : null;
		$time = elapsedTime('@'.$values['lastaccess']['clock']);

		$elements['lastaccess'] = (new CSpan($value))->addClass($style)->setAttribute('title', $time);
	}

	if (isset($values['resolver']['value'])) {
		$value = $values['resolver']['value'] == 0 ? 'Down' : 'Up';
		$style = $values['resolver']['value'] == 0 ? ZBX_STYLE_RED : null;
		$time = elapsedTime('@'.$values['resolver']['clock']);

		$elements['resolver'] = (new CSpan($value))->addClass($style)->setAttribute('title', $time);
	}

	if (isset($values['errors']['value'])) {
		$value = $values['errors']['value'] . ($values['errors']['value'] == 1 ? ' error' : ' errors');
		$style = $values['errors']['value'] ? ZBX_STYLE_RED : null;
		$time = elapsedTime('@'.$values['errors']['clock']);

		$elements['errors'] = (new CSpan($value))->addClass($style)->setAttribute('title', $time);
	}

	$table->addRow([
			$index++,
			$elements['probe'],
			$elements['calculated'],
			$elements['automatic'],
			$elements['manual'],
			$elements['lastaccess'],
			$elements['resolver'],
			$elements['errors']
	]);
}

$table_macros = (new CTableInfo())->setHeader([
		_('Macro'),
		_('Value')
]);

foreach ($data['probe_macros'] as $macro => $value) {
	$table_macros->addRow([
		new CLink($macro, Url::getFor('', 'macros.edit')),
		$value
	]);
}

(new CWidget())
		->setTitle($data['title'])
		->addItem($data['module_style'])
		->additem((new CDiv())
			->addClass(ZBX_STYLE_TABLE_FORMS_CONTAINER)
			->addItem($table))
		->additem((new CDiv())
			->addClass(ZBX_STYLE_TABLE_FORMS_SEPARATOR)
			->addItem($table_macros))
		->show();

echo (new CDiv(
		(new CDiv(
			(new CDiv('Page generated at ' . date(DATE_TIME_FORMAT_SECONDS, $data['generated_at'])))
				->addClass(ZBX_STYLE_TABLE_STATS)
			))->addClass(ZBX_STYLE_PAGING_BTN_CONTAINER)
		))->addClass(ZBX_STYLE_TABLE_PAGING);
