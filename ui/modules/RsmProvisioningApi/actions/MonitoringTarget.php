<?php

namespace Modules\RsmProvisioningApi\Actions;

use API;

abstract class MonitoringTarget extends ActionBaseEx
{
	private   const MONITORING_TARGET           = '{$RSM.MONITORING.TARGET}';
	protected const MONITORING_TARGET_REGISTRY  = 'registry';
	protected const MONITORING_TARGET_REGISTRAR = 'registrar';

	protected function getMonitoringTarget()
	{
		// TODO: add sanity checks

		$data = API::UserMacro()->get([
			'output' => ['value'],
			'globalmacro' => true,
			'filter' => ['macro' => self::MONITORING_TARGET],
		]);

		return $data[0]['value'];
	}
}
