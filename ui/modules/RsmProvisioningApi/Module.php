<?php

namespace Modules\RsmProvisioningApi;

use CController as Action;
use Core\CModule as BaseModule;

class Module extends BaseModule
{
	public function init(): void
	{
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
