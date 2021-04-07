<?php

namespace Modules\RsmProvisioningApi\Actions;

use Exception;

class Registrar extends MonitoringTarget {

	protected function checkMonitoringTarget() {
		return $this->getMonitoringTarget() == MONITORING_TARGET_REGISTRAR;
	}

	protected function getObjectIdInputField() {
		return 'registrar';
	}

	protected function getInputRules(): array {
		switch ($_SERVER['REQUEST_METHOD'])
		{
			case self::REQUEST_METHOD_GET:
				return [
					'type' => API_OBJECT, 'fields' => [
						'registrar'                     => ['type' => API_UINT64],
					]
				];

			case self::REQUEST_METHOD_DELETE:
				return [
					'type' => API_OBJECT, 'fields' => [
						'registrar'                     => ['type' => API_UINT64     , 'flags' => API_REQUIRED],
					]
				];

			case self::REQUEST_METHOD_PUT:
				return [
					'type' => API_OBJECT, 'fields' => [
						'registrar'                     => ['type' => API_UINT64     , 'flags' => API_REQUIRED],
						'registrarName'                 => ['type' => API_STRING_UTF8, 'flags' => API_REQUIRED],
						'registrarFamily'               => ['type' => API_STRING_UTF8, 'flags' => API_REQUIRED],
						'servicesStatus'                => ['type' => API_OBJECTS    , 'flags' => API_REQUIRED, 'uniq' => [['service']], 'fields' => [  // TODO: all services (i.e. rdds, rdap) must be specified
							'service'                   => ['type' => API_STRING_UTF8, 'flags' => API_REQUIRED, 'in' => 'rdap,rdds'],
							'enabled'                   => ['type' => API_BOOLEAN    , 'flags' => API_REQUIRED],
						]],
						'rddsParameters'                => ['type' => API_OBJECT     , 'flags' => API_REQUIRED, 'fields' => [
							'rdds43Server'              => ['type' => API_STRING_UTF8, 'flags' => API_REQUIRED],
							'rdds43TestedDomain'        => ['type' => API_STRING_UTF8, 'flags' => API_REQUIRED],
							'rdds80Url'                 => ['type' => API_STRING_UTF8, 'flags' => API_REQUIRED],
							'rdapUrl'                   => ['type' => API_STRING_UTF8, 'flags' => API_REQUIRED],
							'rdapTestedDomain'          => ['type' => API_STRING_UTF8, 'flags' => API_REQUIRED],
							'rdds43NsString'            => ['type' => API_STRING_UTF8, 'flags' => API_REQUIRED],
						]],
						'zabbixMonitoringCentralServer' => ['type' => API_UINT64     , 'in' => implode(',', array_keys($GLOBALS['DB']['SERVERS']))],    // TODO: check if that's actually current server
					]
				];

			default:
				throw new Exception('Unsupported request method');
		}
	}

	/******************************************************************************************************************
	 * Functions for retrieving object                                                                                *
	 ******************************************************************************************************************/

	protected function getObjects(?string $objectId) {
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
				self::MACRO_RDAP_ENABLED,
				self::MACRO_RDDS_ENABLED,
				self::MACRO_RDAP_BASE_URL,
				self::MACRO_RDAP_TEST_DOMAIN,
				self::MACRO_RDDS43_TEST_DOMAIN,
				//self::MACRO_RDDS_43_SERVERS,
				//self::MACRO_RDDS_80_SERVERS,
				self::MACRO_RDDS_NS_STRING,
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
						'service'               => 'rdds',
						'enabled'               => (bool)$macros[$host][self::MACRO_RDDS_ENABLED],
					],
				],
				'rddsParameters'                => [
					'rdds43Server'              => 'TODO', // $macros[$host][self::MACRO_RDDS_ENABLED] ? $macros[$host][self::MACRO_RDDS_43_SERVERS]    : null,
					'rdds43TestedDomain'        => $macros[$host][self::MACRO_RDDS_ENABLED] ? $macros[$host][self::MACRO_RDDS43_TEST_DOMAIN] : null,
					'rdds80Url'                 => 'TODO', // $macros[$host][self::MACRO_RDDS_ENABLED] ? $macros[$host][self::MACRO_RDDS_80_SERVERS]    : null,
					'rdapUrl'                   => $macros[$host][self::MACRO_RDAP_ENABLED] ? $macros[$host][self::MACRO_RDAP_BASE_URL]      : null,
					'rdapTestedDomain'          => $macros[$host][self::MACRO_RDAP_ENABLED] ? $macros[$host][self::MACRO_RDAP_TEST_DOMAIN]   : null,
					'rdds43NsString'            => $macros[$host][self::MACRO_RDDS_ENABLED] ? $macros[$host][self::MACRO_RDDS_NS_STRING]     : null,
				],
				'zabbixMonitoringCentralServer' => 'TODO',                                                              // TODO: fill with real value
			];
		}

		return $result;
	}

	/******************************************************************************************************************
	 * Functions for creating object                                                                                  *
	 ******************************************************************************************************************/

	protected function createStatusHost(array $input) {
		$config = [
			'host'       => $input[$this->getObjectIdInputField()],
			'status'     => HOST_STATUS_MONITORED,
			'interfaces' => [self::DEFAULT_MAIN_INTERFACE],
			'groups'     => [
				['groupid' => $this->hostGroupIds['TLDs']],
				['groupid' => $this->hostGroupIds['gTLD']],
			],
			'templates'  => [
				['templateid' => $this->templateIds['Template Rsmhost Config ' . $input[$this->getObjectIdInputField()]]],
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

	protected function updateObject() {
	}

	/******************************************************************************************************************
	 * Helper functions                                                                                               *
	 ******************************************************************************************************************/

	protected function getRsmhostConfigsFromInput(array $input) {
		$services = array_column($input['servicesStatus'], 'enabled', 'service');

		return [
			$input[$this->getObjectIdInputField()] => [
				'tldType' => 'gTLD',
				'rdap'    => $services['rdap'],
				'rdds'    => $services['rdds'],
			],
		];
	}

	protected function getMacrosConfig(array $input) {
		$services = array_column($input['servicesStatus'], 'enabled', 'service');

		return [
			$this->createMacroConfig(self::MACRO_TLD                   , $input[$this->getObjectIdInputField()]),
			$this->createMacroConfig(self::MACRO_TLD_CONFIG_TIMES      , $_SERVER['REQUEST_TIME']),

			$this->createMacroConfig(self::MACRO_TLD_RDAP_ENABLED      , (int)$services['rdap']),
			$this->createMacroConfig(self::MACRO_TLD_RDDS_ENABLED      , (int)$services['rdds']),

			$this->createMacroConfig(self::MACRO_TLD_RDAP_BASE_URL     , $input['rddsParameters']['rdapUrl']),
			$this->createMacroConfig(self::MACRO_TLD_RDAP_TEST_DOMAIN  , $input['rddsParameters']['rdapTestedDomain']),

			$this->createMacroConfig(self::MACRO_TLD_RDDS43_TEST_DOMAIN, $input['rddsParameters']['rdds43TestedDomain']),
			$this->createMacroConfig(self::MACRO_TLD_RDDS_NS_STRING    , $input['rddsParameters']['rdds43NsString']),
			//$this->createMacroConfig(self::MACRO_TLD_RDDS_43_SERVERS   , $input['rddsParameters']['rdds43Server']),     // TODO: fill with real value
			//$this->createMacroConfig(self::MACRO_TLD_RDDS_80_SERVERS   , $input['rddsParameters']['rdds80Url']),        // TODO: fill with real value
		];
	}
}
