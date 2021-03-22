<?php

namespace Modules\RsmProvisioningApi\Actions;

use API;

abstract class MonitoringTarget extends ActionBaseEx {

	protected function deleteObject() {
		$input = $this->getInputAll();

		$templateId = $this->getTemplateId('Template Rsmhost Config ' . $input[$this->getObjectIdInputField()]);

		$hostids = array_column($this->getHostsByTemplateId($templateId, null, null), 'hostid', 'host');

		// delete "<rsmhost>", "<rsmhost> <probe>" hosts
		$data = API::Host()->delete(array_values($hostids));

		// delete "Template Rsmhost Config <rsmhost>" template
		$data = API::Template()->delete([$templateId]);

		// delete "TLD <rsmhost>" host group
		$hostGroupId = $this->getHostGroupId('TLD ' . $input[$this->getObjectIdInputField()]);
		$data = API::HostGroup()->delete([$hostGroupId]);
	}
}
