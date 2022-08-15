<?php

namespace Modules\RsmProvisioningApi;

use CController as Action;
use Core\CModule as BaseModule;
use CMessageHelper;

class Module extends BaseModule
{
	public function init(): void
	{
		// get rid of an expected error message
		$messages = CMessageHelper::getMessages();
		if (count($messages) === 1 && $messages[0]['type'] === 'error' && $messages[0]['message'] === _('No permissions for system access.'))
		{
			CMessageHelper::clear();
		}

		parent::init();
	}

	public function onBeforeAction(Action $action): void
	{
		parent::onBeforeAction($action);
	}

	public function onTerminate(Action $action): void
	{
		parent::onTerminate($action);
	}
}
