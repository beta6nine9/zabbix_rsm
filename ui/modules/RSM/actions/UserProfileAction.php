<?php

namespace Modules\RSM\Actions;

use CWebUser;
use CControllerUserProfileEdit as Action;

class UserProfileAction extends Action {

	protected function doAction() {
		// Change logged in user type to USER_TYPE_ZABBIX_USER to prevent 'Media' tab to be shown.
		CWebUser::$data['type'] = USER_TYPE_ZABBIX_USER;

		parent::doAction();
	}
}
