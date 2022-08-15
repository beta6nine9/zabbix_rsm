<?php

namespace Modules\RSM\Actions\Zabbix;

trait CheckPermissionsTrait {

	protected function checkPermissions() {
		return ($this->getUserType() == USER_TYPE_ZABBIX_ADMIN || $this->getUserType() == USER_TYPE_SUPER_ADMIN);
	}
}
