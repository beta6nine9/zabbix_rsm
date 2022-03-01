<?php

namespace Modules\RSM\Actions\Zabbix;

use CWebUser;
use CControllerExportXml as Action;

class CControllerExportXml extends Action {

	protected function checkPermissions() {
		switch ($this->getInput('action')) {
			case 'export.mediatypes.xml':
			case 'export.valuemaps.xml':
				return (CWebUser::$data['type'] == USER_TYPE_SUPER_ADMIN);

			case 'export.hosts.xml':
			case 'export.templates.xml':
				return (CWebUser::$data['type'] == USER_TYPE_ZABBIX_ADMIN || CWebUser::$data['type'] == USER_TYPE_SUPER_ADMIN);

			case 'export.screens.xml':
			case 'export.sysmaps.xml':
				return (CWebUser::$data['type'] == USER_TYPE_ZABBIX_USER || CWebUser::$data['type'] == USER_TYPE_ZABBIX_ADMIN || CWebUser::$data['type'] == USER_TYPE_SUPER_ADMIN);

			default:
				return false;
		}
	}
}
