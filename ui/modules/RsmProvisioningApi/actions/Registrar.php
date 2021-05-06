<?php

//declare(strict_types=1); // TODO: enable strict_types

namespace Modules\RsmProvisioningApi\Actions;

use API;
use Exception;

class Registrar extends MonitoringTarget
{
	/******************************************************************************************************************
	 * Functions for validation                                                                                       *
	 ******************************************************************************************************************/

	protected function checkMonitoringTarget(): bool
	{
		return $this->getMonitoringTarget() == MONITORING_TARGET_REGISTRAR;
	}

	protected function getInputRules(): array
	{
		switch ($_SERVER['REQUEST_METHOD'])
		{
			case self::REQUEST_METHOD_GET:
				return [
					'type' => API_OBJECT, 'fields' => [
						'id'                     => ['type' => API_RSM_CUSTOM , 'function' => 'RsmValidateInt', 'min' => 1, 'error' => 'IANAID must be a positive integer'],
					]
				];

			case self::REQUEST_METHOD_DELETE:
				return [
					'type' => API_OBJECT, 'fields' => [
						'id'                     => ['type' => API_RSM_CUSTOM , 'function' => 'RsmValidateInt', 'min' => 1, 'error' => 'IANAID must be a positive integer'],
					]
				];

			case self::REQUEST_METHOD_PUT:
				return [
					'type' => API_OBJECT, 'fields' => [
						'id'                     => ['type' => API_RSM_CUSTOM , 'function' => 'RsmValidateInt', 'min' => 1, 'error' => 'IANAID must be a positive integer'],
						'registrarName'          => ['type' => API_STRING_UTF8],
						'registrarFamily'        => ['type' => API_STRING_UTF8],
						'servicesStatus'         => ['type' => API_OBJECTS    , 'uniq' => [['service']], 'fields' => [
							'service'            => ['type' => API_RSM_CUSTOM , 'function' => 'RsmValidateEnum', 'in' => ['rdap', 'rdds43', 'rdds80'], 'error' => 'Service is not supported'],
							'enabled'            => ['type' => API_BOOLEAN    ],
						]],
						'rddsParameters'         => ['type' => API_OBJECT     , 'fields' => [
							'rdds43Server'       => ['type' => API_RSM_CUSTOM , 'function' => 'RsmValidateHostname', 'error' => 'Invalid domain name provided in "tld", "ns", "rdds43Server", "rdds43TestedDomain", "rdapTestedDomain" or "nsTestPrefix" element'],
							'rdds43TestedDomain' => ['type' => API_RSM_CUSTOM , 'function' => 'RsmValidateDomainName', 'error' => 'Invalid domain name provided in "tld", "ns", "rdds43Server", "rdds43TestedDomain", "rdapTestedDomain" or "nsTestPrefix" element'],
							'rdds80Url'          => ['type' => API_RSM_CUSTOM , 'function' => 'RsmValidateUrl', 'error' => 'Invalid URL provided on rdds80Url'],
							'rdapUrl'            => ['type' => API_RSM_CUSTOM , 'function' => 'RsmValidateRdapUrl', 'error' => 'The "rdapUrl" element can only be an URL or "not listed" or "no https"'],
							'rdapTestedDomain'   => ['type' => API_RSM_CUSTOM , 'function' => 'RsmValidateDomainName', 'error' => 'Invalid domain name provided in "tld", "ns", "rdds43Server", "rdds43TestedDomain", "rdapTestedDomain" or "nsTestPrefix" element'],
							'rdds43NsString'     => ['type' => API_STRING_UTF8],
						]],
					]
				];

			default:
				throw new Exception('Unsupported request method');
		}
	}

	/******************************************************************************************************************
	 * Functions for retrieving object                                                                                *
	 ******************************************************************************************************************/

	protected function getObjects(?string $objectId): array
	{
		// get hosts

		$data = $this->getHostsByHostGroup('TLDs', $objectId, ['info_1', 'info_2']);

		if (empty($data))
		{
			return [];
		}

		$hosts = array_column($data, 'host', 'hostid');
		$info1 = array_column($data, 'info_1', 'host');
		$info2 = array_column($data, 'info_2', 'host');

		// get templates

		$templateNames = array_values(array_map(fn($host) => 'Template Rsmhost Config ' . $host, $hosts));
		$templates = array_flip($this->getTemplateIds($templateNames));

		// get template macros

		$macros = $this->getHostMacros(
			array_map(fn($host) => str_replace('Template Rsmhost Config ', '', $host), $templates),
			[
				self::MACRO_TLD_RDAP_ENABLED,
				self::MACRO_TLD_RDDS_ENABLED,
				self::MACRO_TLD_RDAP_BASE_URL,
				self::MACRO_TLD_RDAP_TEST_DOMAIN,
				self::MACRO_TLD_RDDS43_SERVER,
				self::MACRO_TLD_RDDS43_TEST_DOMAIN,
				self::MACRO_TLD_RDDS80_URL,
				self::MACRO_TLD_RDDS43_NS_STRING,
			]
		);

		// join data in a common data structure

		$result = [];

		foreach ($hosts as $host)
		{
			$result[] = [
				'registrar'                     => $host,
				'registrarName'                 => $info1[$host],
				'registrarFamily'               => $info2[$host],
				'servicesStatus'                => [
					[
						'service'               => 'rdap',
						'enabled'               => (bool)$macros[$host][self::MACRO_RDAP_ENABLED],
					],
					[
						'service'               => 'rdds43',
						'enabled'               => (bool)$macros[$host][self::MACRO_RDDS_ENABLED],
					],
					[
						'service'               => 'rdds80',
						'enabled'               => (bool)$macros[$host][self::MACRO_RDDS_ENABLED],
					],
				],
				'rddsParameters'                => [
					'rdds43Server'              => $macros[$host][self::MACRO_RDDS_ENABLED] ? $macros[$host][self::MACRO_TLD_RDDS43_SERVER]  : null,
					'rdds43TestedDomain'        => $macros[$host][self::MACRO_RDDS_ENABLED] ? $macros[$host][self::MACRO_RDDS43_TEST_DOMAIN] : null,
					'rdds80Url'                 => $macros[$host][self::MACRO_RDDS_ENABLED] ? $macros[$host][self::MACRO_TLD_RDDS80_URL]     : null,
					'rdapUrl'                   => $macros[$host][self::MACRO_RDAP_ENABLED] ? $macros[$host][self::MACRO_RDAP_BASE_URL]      : null,
					'rdapTestedDomain'          => $macros[$host][self::MACRO_RDAP_ENABLED] ? $macros[$host][self::MACRO_RDAP_TEST_DOMAIN]   : null,
					'rdds43NsString'            => $macros[$host][self::MACRO_RDDS_ENABLED] ? $macros[$host][self::MACRO_RDDS_NS_STRING]     : null,
				],
			];
		}

		return $result;
	}

	/******************************************************************************************************************
	 * Functions for creating object                                                                                  *
	 ******************************************************************************************************************/

	protected function createStatusHost(): int
	{
		$config = [
			'host'       => $this->newObject['id'],
			'status'     => HOST_STATUS_MONITORED,
			'interfaces' => [self::DEFAULT_MAIN_INTERFACE],
			'groups'     => [
				['groupid' => $this->hostGroupIds['TLDs']],
				['groupid' => $this->hostGroupIds['gTLD']],
			],
			'templates'  => [
				['templateid' => $this->templateIds['Template Rsmhost Config ' . $this->newObject['id']]],
				['templateid' => $this->templateIds['Template Config History']],
				['templateid' => $this->templateIds['Template RDAP Status']],
				['templateid' => $this->templateIds['Template RDDS Status']],
			],
		];
		$data = API::Host()->create($config);

		return $data['hostids'][0];
	}

	/******************************************************************************************************************
	 * Functions for updating object                                                                                  *
	 ******************************************************************************************************************/

	protected function updateStatustHost(): int
	{
		$config = [
			'hostid' => $this->getHostId($this->newObject['id']),
			'groups' => [
				['groupid' => $this->hostGroupIds['TLDs']],
				['groupid' => $this->hostGroupIds['gTLD']],
			],
		];
		$data = API::Host()->update($config);

		return $data['hostids'][0];
	}

	protected function disableObject(): void {
		parent::disableObject();

		$this->updateMacros(
			$this->templateIds['Template Rsmhost Config ' . $this->getInput('id')],
			[
				self::MACRO_TLD_RDAP_ENABLED => 0,
				self::MACRO_TLD_RDDS_ENABLED => 0,
				//self::MACRO_TLD_RDDS_ENABLED => 0, // TODO: split into RDDS43 and RDDS80
			]
		);
	}

	/******************************************************************************************************************
	 * Misc functions                                                                                               *
	 ******************************************************************************************************************/

	protected function getHostGroupNames(?array $additionalNames): array
	{
		$names = [
			// groups for "<rsmhost>" host
			'Templates - TLD',
			'TLDs',
			'gTLD',
			// groups for "<rsmhost> <probe>" hosts
			'gTLD Probe results',
			'TLD Probe results',
		];

		if (!is_null($additionalNames))
		{
			$names = array_merge($names, $additionalNames);
		}

		return $names;
	}

	protected function getTemplateNames(?array $additionalNames): array
	{
		$names = [
			// templates for "<rsmhost>" host
			'Template Config History',
			'Template RDAP Status',
			'Template RDDS Status',
			// templates for "<rsmhost> <probe>" hosts
			'Template RDAP Test',
			'Template RDDS Test',
		];

		if (!is_null($additionalNames))
		{
			$names = array_merge($names, $additionalNames);
		}

		return $names;
	}

	protected function getRsmhostConfigsFromInput(): array
	{
		$services = array_column($this->newObject['servicesStatus'], 'enabled', 'service');

		return [
			$this->newObject['id'] => [
				'tldType' => 'gTLD',
				'rdap'    => $services['rdap'],
				'rdds43'  => $services['rdds43'],
				'rdds80'  => $services['rdds80'],
			],
		];
	}

	protected function getMacrosConfig(): array
	{
		$services = array_column($this->newObject['servicesStatus'], 'enabled', 'service');

		return [
			$this->createMacroConfig(self::MACRO_TLD                   , $this->newObject['id']),
			$this->createMacroConfig(self::MACRO_TLD_CONFIG_TIMES      , $_SERVER['REQUEST_TIME']),

			$this->createMacroConfig(self::MACRO_TLD_RDAP_ENABLED      , (int)$services['rdap']),
			$this->createMacroConfig(self::MACRO_TLD_RDDS_ENABLED      , (int)$services['rdds43']),
			//$this->createMacroConfig(self::MACRO_TLD_RDDS_ENABLED      , (int)$services['rdds80']),

			$this->createMacroConfig(self::MACRO_TLD_RDAP_BASE_URL     , $this->newObject['rddsParameters']['rdapUrl']),
			$this->createMacroConfig(self::MACRO_TLD_RDAP_TEST_DOMAIN  , $this->newObject['rddsParameters']['rdapTestedDomain']),

			$this->createMacroConfig(self::MACRO_TLD_RDDS43_TEST_DOMAIN, $this->newObject['rddsParameters']['rdds43TestedDomain']),
			$this->createMacroConfig(self::MACRO_TLD_RDDS43_NS_STRING  , $this->newObject['rddsParameters']['rdds43NsString']),
			$this->createMacroConfig(self::MACRO_TLD_RDDS43_SERVER     , $this->newObject['rddsParameters']['rdds43Server']),
			$this->createMacroConfig(self::MACRO_TLD_RDDS80_URL        , $this->newObject['rddsParameters']['rdds80Url']),
		];
	}
}
