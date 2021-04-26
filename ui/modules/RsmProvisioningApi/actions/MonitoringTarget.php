<?php

//declare(strict_types=1); // TODO: enable strict_types

namespace Modules\RsmProvisioningApi\Actions;

use API;

abstract class MonitoringTarget extends ActionBaseEx
{
	abstract protected function getRsmhostConfigsFromInput(): array;
	abstract protected function getMacrosConfig(): array;
	abstract protected function createStatusHost(): int;
	abstract protected function updateStatustHost(): int;
	abstract protected function getHostGroupNames(?array $additionalNames): array;
	abstract protected function getTemplateNames(?array $additionalNames): array;

	protected $statusHostId = null;

	/******************************************************************************************************************
	 * Functions for creating object                                                                                  *
	 ******************************************************************************************************************/

	protected function createObject(): void
	{
		$this->hostGroupIds += $this->getHostGroupIds($this->getHostGroupNames(null));
		$this->templateIds  += $this->getTemplateIds($this->getTemplateNames(null));

		$this->createHostGroups();
		$this->createTemplates();
		$this->statusHostId = $this->createStatusHost();

		// create "<rsmhost> <probe>" hosts

		$rsmhostConfigs = $this->getRsmhostConfigsFromInput();
		$probeConfigs = $this->getProbeConfigs();

		$testHosts = $this->createTestHosts($rsmhostConfigs, $probeConfigs);

		// enable/disable items, based on service status and standalone rdap status

		$statusHosts = [$this->statusHostId => $this->newObject['id']];

		$this->updateServiceItemStatus($statusHosts, $testHosts, $rsmhostConfigs, $probeConfigs);
	}

	private function createHostGroups(): void
	{
		$config = [
			'name' => 'TLD ' . $this->newObject['id'],
		];
		$data = API::HostGroup()->create($config);

		$this->hostGroupIds['TLD ' . $this->newObject['id']] = $data['groupids'][0];
	}

	protected function createTemplates(): void
	{
		$config = [
			'host'   => 'Template Rsmhost Config ' . $this->newObject['id'],
			'groups' => [
				['groupid' => $this->hostGroupIds['Templates - TLD']],
			],
			'macros' => $this->getMacrosConfig(),
		];
		$data = API::Template()->create($config);

		$this->templateIds['Template Rsmhost Config ' . $this->newObject['id']] = $data['templateids'][0];
	}

	/******************************************************************************************************************
	 * Functions for updating object                                                                                  *
	 ******************************************************************************************************************/

	protected function updateObject(): void
	{
		$this->hostGroupIds += $this->getHostGroupIds($this->getHostGroupNames(['TLD ' . $this->newObject['id']]));
		$this->templateIds  += $this->getTemplateIds($this->getTemplateNames(null));

		$this->updateTemplates();
		$this->statusHostId = $this->updateStatustHost();

		// update "<rsmhost> <probe>" hosts

		$rsmhostConfigs = $this->getRsmhostConfigsFromInput();
		$probeConfigs = $this->getProbeConfigs();

		$testHosts = $this->updateTestHosts($rsmhostConfigs, $probeConfigs);

		// enable/disable items, based on service status and standalone rdap status

		$statusHosts = [$this->statusHostId => $this->newObject['id']];

		$this->updateServiceItemStatus($statusHosts, $testHosts, $rsmhostConfigs, $probeConfigs);
	}

	private function updateTemplates(): void
	{
		$config = [
			'templateid' => $this->getTemplateId('Template Rsmhost Config ' . $this->newObject['id']),
			'macros'     => $this->getMacrosConfig(),
		];
		$data = API::Template()->update($config);
	}

	/******************************************************************************************************************
	 * Functions for deleting object                                                                                  *
	 ******************************************************************************************************************/

	protected function deleteObject(): void
	{
		$templateId = $this->getTemplateId('Template Rsmhost Config ' . $this->oldObject['id']);

		$hostids = array_column($this->getHostsByTemplateId($templateId, null, null), 'hostid', 'host');

		// delete "<rsmhost>", "<rsmhost> <probe>" hosts
		$data = API::Host()->delete(array_values($hostids));

		// delete "Template Rsmhost Config <rsmhost>" template
		$data = API::Template()->delete([$templateId]);

		// delete "TLD <rsmhost>" host group
		$hostGroupId = $this->getHostGroupId('TLD ' . $this->oldObject['id']);
		$data = API::HostGroup()->delete([$hostGroupId]);
	}
}
