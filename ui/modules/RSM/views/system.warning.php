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
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
**/


/**
 * RSM specifics: this whole file is an override of standard Zabbix system.warning view!
 */

$pageHeader = (new CPageHeader(_('Fatal error, please report to Zabbix team')))
	->addCssFile('assets/styles/'.CHtml::encode($data['theme']).'.css')
	->display();

$buttons = [
	(new CButton('back', _('Go to Rolling Week status')))
		->onClick('javascript: document.location = "zabbix.php?action=rsm.rollingweekstatus"'
)];

echo '<body lang="'.CWebUser::getLang().'">';

// take only the first error message from $data['messages']
(new CDiv((new CTag('main', true,
                    new CWarning(_('Fatal error, please report to Zabbix team'), [reset($data['messages'])], $buttons)
))))
	->addClass(ZBX_STYLE_LAYOUT_WRAPPER)
	->show();

echo '</body>';
echo '</html>';
