<?php

//declare(strict_types=1); // TODO: enable strict_types

namespace Modules\RsmProvisioningApi\Actions;

use API;

abstract class MonitoringTarget extends ActionBaseEx {

	abstract protected function getRsmhostConfigsFromInput(array $input);
	abstract protected function getMacrosConfig(array $input);
	abstract protected function createStatusHost(array $input);
	abstract protected function updateStatustHost(array $input);
	abstract protected function getHostGroupNames(array $input, ?array $additionalNames);
	abstract protected function getTemplateNames(?array $additionalNames);

	protected $statusHostId = null;

	/******************************************************************************************************************
	 * Functions for creating object                                                                                  *
	 ******************************************************************************************************************/

	protected function createObject() {
		$input = $this->getInputAll();

		$this->hostGroupIds += $this->getHostGroupIds($this->getHostGroupNames($input, null));
		$this->templateIds  += $this->getTemplateIds($this->getTemplateNames(null));

		$this->createHostGroups($input);
		$this->createTemplates($input);
		$this->statusHostId = $this->createStatusHost($input);

		// create "<rsmhost> <probe>" hosts

		$rsmhostConfigs = $this->getRsmhostConfigsFromInput($input);
		$probeConfigs = $this->getProbeConfigs();

		$testHosts = $this->createTestHosts($rsmhostConfigs, $probeConfigs);

		// enable/disable items, based on service status and standalone rdap status

		$statusHosts = [$this->statusHostId => $input['id']];

		$this->updateServiceItemStatus($statusHosts, $testHosts, $rsmhostConfigs, $probeConfigs);
	}

	private function createHostGroups(array $input) {
		$config = [
			'name' => 'TLD ' . $input['id'],
		];
		$data = API::HostGroup()->create($config);

		$this->hostGroupIds['TLD ' . $input['id']] = $data['groupids'][0];
	}

	protected function createTemplates(array $input) {
		$config = [
			'host'   => 'Template Rsmhost Config ' . $input['id'],
			'groups' => [
				['groupid' => $this->hostGroupIds['Templates - TLD']],
			],
			'macros' => $this->getMacrosConfig($input),
		];
		$data = API::Template()->create($config);

		$this->templateIds['Template Rsmhost Config ' . $input['id']] = $data['templateids'][0];
	}

	/******************************************************************************************************************
	 * Functions for updating object                                                                                  *
	 ******************************************************************************************************************/

	protected function updateObject() {
		$input = $this->getInputAll();

		$this->hostGroupIds += $this->getHostGroupIds($this->getHostGroupNames($input, ['TLD ' . $input['id']]));
		$this->templateIds  += $this->getTemplateIds($this->getTemplateNames(null));

		$this->updateTemplates($input);
		$this->statusHostId = $this->updateStatustHost($input);

		// update "<rsmhost> <probe>" hosts

		$rsmhostConfigs = $this->getRsmhostConfigsFromInput($input);
		$probeConfigs = $this->getProbeConfigs();

		$rsmhostProbeHosts = $this->updateRsmhostProbeHosts($rsmhostConfigs, $probeConfigs);

		// enable/disable items, based on service status and standalone rdap status

		$statusHosts = [$this->statusHostId => $input['id']];

		$this->updateServiceItemStatus($statusHosts, $rsmhostProbeHosts, $rsmhostConfigs, $probeConfigs);
	}

	protected function updateTemplates(array $input) {
		$config = [
			'templateid' => $this->getTemplateId('Template Rsmhost Config ' . $input['id']),
			'macros'     => $this->getMacrosConfig($input),
		];
		$data = API::Template()->update($config);
	}

	/******************************************************************************************************************
	 * Functions for deleting object                                                                                  *
	 ******************************************************************************************************************/

	protected function deleteObject() {
		$input = $this->getInputAll();
		$rsmhost = $input['id'];

		$templateId = $this->getTemplateId('Template Rsmhost Config ' . $rsmhost);

		$hostids = array_column($this->getHostsByTemplateId($templateId, null, null), 'hostid', 'host');

		// delete "<rsmhost>", "<rsmhost> <probe>" hosts
		$data = API::Host()->delete(array_values($hostids));

		// delete "Template Rsmhost Config <rsmhost>" template
		$data = API::Template()->delete([$templateId]);

		// delete "TLD <rsmhost>" host group
		$hostGroupId = $this->getHostGroupId('TLD ' . $rsmhost);
		$data = API::HostGroup()->delete([$hostGroupId]);
	}
}
