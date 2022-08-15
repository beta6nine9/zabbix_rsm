<?php

//declare(strict_types=1); // TODO: enable strict_types

namespace Modules\RsmProvisioningApi\Actions;

use API;
use CSlaReport;
use Exception;
use Modules\RsmProvisioningApi\RsmException;

abstract class MonitoringTarget extends ActionBaseEx
{
	abstract protected function getRsmhostConfigsFromInput(): array;
	abstract protected function getMacrosConfig(): array;
	abstract protected function createStatusHost(): int;
	abstract protected function updateStatustHost(): int;

	protected $statusHostId = null;

	/******************************************************************************************************************
	 * Functions for validation                                                                                       *
	 ******************************************************************************************************************/

	protected function rsmValidateInput(): void
	{
		parent::rsmValidateInput();

		if ($_SERVER['REQUEST_METHOD'] == self::REQUEST_METHOD_PUT)
		{
			$rules = $this->getInputRules();
			$allServices = $rules['fields']['servicesStatus']['fields']['service']['in'];
			$allServices = implode(', ', $allServices);
			$allServices = preg_replace('/, ([^,]+)$/', ' and $1', $allServices);

			$this->validateInputServices();

			$services = array_column($this->input['servicesStatus'], 'enabled', 'service');

			if ($services['rdap'] || $services['rdds43'] || $services['rdds80'])
			{
				$this->requireArrayKeys(['rddsParameters'], $this->input, 'rddsParameters object is missing and at least one RDDS service (i.e., rdds43, rdds80 or rdap) is enabled');

				if ($services['rdap'])
				{
					$this->requireArrayKeys(['rdapUrl', 'rdapTestedDomain',], $this->input['rddsParameters'], 'An element within the rddsParameters object is missing based on the enabled status of a service to be monitored');
				}
				else
				{
					$this->forbidArrayKeys(['rdapUrl', 'rdapTestedDomain',], $this->input['rddsParameters'], 'An element within the rddsParameter object or the rddsParameter object is included but the status of the service is disabled');
				}
				if ($services['rdds43'])
				{
					$this->requireArrayKeys(['rdds43Server', 'rdds43TestedDomain', 'rdds43NsString'], $this->input['rddsParameters'], 'An element within the rddsParameters object is missing based on the enabled status of a service to be monitored');
				}
				else
				{
					$this->forbidArrayKeys(['rdds43Server', 'rdds43TestedDomain', 'rdds43NsString'], $this->input['rddsParameters'], 'An element within the rddsParameter object or the rddsParameter object is included but the status of the service is disabled');
				}
				if ($services['rdds80'])
				{
					$this->requireArrayKeys(['rdds80Url'], $this->input['rddsParameters'], 'An element within the rddsParameters object is missing based on the enabled status of a service to be monitored');
				}
				else
				{
					$this->forbidArrayKeys(['rdds80Url'], $this->input['rddsParameters'], 'An element within the rddsParameter object or the rddsParameter object is included but the status of the service is disabled');
				}
			}
			else
			{
				$this->forbidArrayKeys(['rddsParameters'], $this->input, 'An element within the rddsParameter object or the rddsParameter object is included but the status of the service is disabled');
			}
		}
	}

	/******************************************************************************************************************
	 * Functions for creating object                                                                                  *
	 ******************************************************************************************************************/

	protected function createObject(): void
	{
		$services = array_column($this->newObject['servicesStatus'], 'enabled');
		if (empty(array_filter($services)))
		{
			throw new RsmException(400, 'At least one service must be enabled');
		}

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
	}

	protected function createTemplates(): void
	{
		$config = [
			'host'   => 'Template Rsmhost Config ' . $this->newObject['id'],
			'groups' => [
				['groupid' => $this->getHostGroupId('Templates - TLD')],
			],
			'macros' => $this->getMacrosConfig(),
		];
		$data = API::Template()->create($config);
	}

	/******************************************************************************************************************
	 * Functions for updating object                                                                                  *
	 ******************************************************************************************************************/

	protected function isObjectDisabled(array $object): bool
	{
		$services = array_column($object['servicesStatus'], 'enabled');

		return empty(array_filter($services));
	}

	protected function getConfigTimesMacroValue(?string $oldValue): string
	{
		$times = is_null($oldValue) ? [] : explode(';', $oldValue);

		# remove entries that are more than 6 months old
		$times = array_filter($times, fn($t) => $t >= $_SERVER['REQUEST_TIME'] - 180 * 86400);

		# add current time
		$times[] = $_SERVER['REQUEST_TIME'];

		return implode(';', $times);
	}

	protected function updateObject(): void
	{
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

	protected function disableObject(): void
	{
		$this->generateMonthlyReport();

		$templateId = $this->getTemplateId('Template Rsmhost Config ' . $this->input['id']);
		$hosts = $this->getHostsByTemplateId($templateId, null, null);

		foreach ($hosts as $host)
		{
			if ($host['host'] == $this->input['id'])
			{
				$this->statusHostId = $host['hostid'];
				break;
			}
		}

		$config = array_map(fn($v) => ['hostid' => $v['hostid'], 'status' => HOST_STATUS_NOT_MONITORED], $hosts);
		API::Host()->update($config);
	}

	private function generateMonthlyReport(): void
	{
		$year  = idate('Y', $_SERVER['REQUEST_TIME']);
		$month = idate('m', $_SERVER['REQUEST_TIME']);

		$reports = CSlaReport::generate($this->getServerId(), [$this->input['id']], $year, $month, ["XML", "JSON"]);

		if (is_null($reports))
		{
			throw new RsmException(500, 'General error', 'Failed to generate monthly report: ' . CSlaReport::$error);
		}

		foreach ($reports as $report)
		{
			$sql = 'insert into sla_reports (hostid,year,month,report_xml,report_json) values (%s,%s,%s,%s,%s)' .
					' on duplicate key update report_xml=%s,report_json=%s';
			$sql = sprintf(
				$sql,
				zbx_dbstr($report['hostid']),
				zbx_dbstr($year),
				zbx_dbstr($month),
				zbx_dbstr($report['report']['XML']),
				zbx_dbstr($report['report']['JSON']),
				zbx_dbstr($report['report']['XML']),
				zbx_dbstr($report['report']['JSON'])
			);
			if (!DBexecute($sql))
			{
				throw new Exception('Query failed');
			}
		}
	}

	/******************************************************************************************************************
	 * Functions for deleting object                                                                                  *
	 ******************************************************************************************************************/

	protected function deleteObject(): void
	{
		$templateId = $this->getTemplateId('Template Rsmhost Config ' . $this->input['id']);

		$hostids = array_column($this->getHostsByTemplateId($templateId, null, null), 'hostid', 'host');

		// delete "<rsmhost>", "<rsmhost> <probe>" hosts
		$this->deleteHosts($hostids);

		// delete "Template Rsmhost Config <rsmhost>" template
		$data = API::Template()->delete([$templateId]);

		// delete "TLD <rsmhost>" host group
		$hostGroupId = $this->getHostGroupId('TLD ' . $this->input['id']);
		$data = API::HostGroup()->delete([$hostGroupId]);
	}
}
