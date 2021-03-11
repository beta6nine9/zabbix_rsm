<?php

namespace Modules\RsmProvisioningApi\Actions;

use API;

class Probe extends ActionBaseEx
{
	protected function checkMonitoringTarget()
	{
		return true;
	}

	protected function getObjectIdInputField()
	{
		return 'probe';
	}

	protected function getInputRules(): array
	{
		switch ($_SERVER['REQUEST_METHOD'])
		{
			case self::REQUEST_METHOD_GET:
				return [
					'type' => API_OBJECT, 'fields' => [
						'probe'                         => ['type' => API_RSM_CUSTOM , 'function' => 'RsmValidateProbeIdentifier'],
					]
				];

			case self::REQUEST_METHOD_DELETE:
				return [
					'type' => API_OBJECT, 'fields' => [
						'probe'                         => ['type' => API_RSM_CUSTOM , 'flags' => API_REQUIRED, 'function' => 'RsmValidateProbeIdentifier'],
					]
				];

			case self::REQUEST_METHOD_PUT:
				return [
					'type' => API_OBJECT, 'fields' => [
						'probe'                         => ['type' => API_RSM_CUSTOM , 'flags' => API_REQUIRED, 'function' => 'RsmValidateProbeIdentifier'],
						'servicesStatus'                => ['type' => API_OBJECTS    , 'flags' => API_REQUIRED, 'uniq' => [['service']], 'fields' => [  // TODO: all services (i.e. rdds and rdap) must be specified
							'service'                   => ['type' => API_STRING_UTF8, 'flags' => API_REQUIRED, 'in' => 'rdap,rdds'],
							'enabled'                   => ['type' => API_BOOLEAN    , 'flags' => API_REQUIRED],
						]],
						'zabbixProxyParameters'         => ['type' => API_OBJECT     , 'flags' => API_REQUIRED, 'fields' => [
							'ipv4Enable'                => ['type' => API_BOOLEAN    , 'flags' => API_REQUIRED],
							'ipv6Enable'                => ['type' => API_BOOLEAN    , 'flags' => API_REQUIRED],
							'ipResolver'                => ['type' => API_RSM_CUSTOM , 'flags' => API_REQUIRED, 'function' => 'RsmValidateIPv4'],       // TODO: IPv4, IPv6 or both?
							'proxyIp'                   => ['type' => API_RSM_CUSTOM , 'flags' => API_REQUIRED, 'function' => 'RsmValidateIPv4'],       // TODO: IPv4, IPv6 or both?
							'proxyPort'                 => ['type' => API_UINT64     , 'flags' => API_REQUIRED, 'in' => '1:65535'],
							'proxyPskIdentity'          => ['type' => API_STRING_UTF8, 'flags' => API_REQUIRED],
							'proxyPsk'                  => ['type' => API_PSK        , 'flags' => API_REQUIRED],
						]],
						'online'                        => ['type' => API_BOOLEAN    , 'flags' => API_REQUIRED],                                        // TODO: "element to put the probe node in manual offline mode" - when receiving list, we should skip disabled probes (not knocked off) completely?
						'zabbixMonitoringCentralServer' => ['type' => API_UINT64     , 'in' => implode(',', array_keys($GLOBALS['DB']['SERVERS']))],    // TODO: check if that's actually current server
					]
				];

			default:
				throw new Exception('Unsupported request method');
		}
	}

	protected function getObjects(?string $objectId)
	{
		// TODO: add sanity checks
		// TODO: include disabled objects (with all services disabled)

		$data = $this->getHostsByHostGroup('Probes', $objectId, null);
		$hosts = array_column($data, 'host', 'hostid');

		if (empty($hosts))
		{
			return [];
		}

		// get proxies
		$data = API::Proxy()->get([
			'output' => ['host'],
			'filter' => [
				'host' => $hosts,
			],
			'selectInterface' => ['ip', 'port'],
		]);
		$interfaces = array_column($data, 'interface', 'host');

		// get templates
		$templateNames = array_values(array_map(fn($host) => 'Template Probe Config ' . $host, $hosts));
		$templates = array_flip($this->getTemplateIds($templateNames));

		// get template macros
		$macros = $this->getHostMacros(
			array_map(fn($host) => str_replace('Template Probe Config ', '', $host), $templates),
			[
				self::MACRO_PROBE_IP4_ENABLED,
				self::MACRO_PROBE_IP6_ENABLED,
				self::MACRO_PROBE_RDAP_ENABLED,
				self::MACRO_PROBE_RDDS_ENABLED,
				self::MACRO_PROBE_RESOLVER,
			]
		);

		// get lastvalue of "rsm.probe.status[manual]" item
		$data = DBfetchArray(DBselect(
			'SELECT' .
				' items.hostid,' .
				' COALESCE(lastvalue.value,1) as value' .
			' FROM' .
				' items' .
				' LEFT JOIN lastvalue ON lastvalue.itemid=items.itemid' .
			' WHERE' .
				' items.hostid IN (' . implode(',', array_keys($hosts)) . ')'
		));
		$status = array_column($data, 'value', 'hostid');

		// join data in a common data structure
		$result = [];

		foreach ($hosts as $hostid => $host)
		{
			$result[] = [
				'probe'                         => $host,
				'serviceStatus'                 => [
					[
						'service'               => 'rdap',
						'enabled'               => (bool)$macros[$host][self::MACRO_PROBE_RDAP_ENABLED],
					],
					[
						'service'               => 'rdds',
						'enabled'               => (bool)$macros[$host][self::MACRO_PROBE_RDDS_ENABLED],
					],
				],
				'zabbixProxyParameters'         => [
					'ipv4Enable'                => (bool)$macros[$host][self::MACRO_PROBE_IP4_ENABLED],
					'ipv6Enable'                => (bool)$macros[$host][self::MACRO_PROBE_IP6_ENABLED],
					'ipResolver'                => $macros[$host][self::MACRO_PROBE_RESOLVER],
					'proxyIp'                   => $interfaces[$host]['ip'],
					'proxyPort'                 => $interfaces[$host]['port'],
					'proxyPskIdentity'          => null,
					'proxyPsk'                  => null,
				],
				'online'                        => (bool)$status[$hostid],
				'zabbixMonitoringCentralServer' => 'TODO',                                                              // TODO: fill with real value
			];
		}

		return $result;
	}

	protected function createObject()
	{
		$input = $this->getInputAll();

		$hostGroupIds = $this->getHostGroupIds($this->getHostGroupNames(null));
		$templateIds = $this->getTemplateIds($this->getTemplateNames(null));

		$data = API::Proxy()->create($this->createProxyConfig($input));
		$proxyId = $data['proxyids'][0];

		$data = API::HostGroup()->create($this->createHostGroupConfig($input));
		$hostGroupIds[$input['probe']] = $data['groupids'][0];

		$data = API::Template()->create($this->createTemplateConfig($input, $hostGroupIds));
		$templateIds['Template Probe Config ' . $input['probe']] = $data['templateids'][0];

		$data = API::Host()->create($this->createProbeHostConfig($input, $hostGroupIds, $templateIds, $proxyId));

		$data = API::Host()->create($this->createProbeMonHostConfig($input, $hostGroupIds, $templateIds));

		$this->updateRsmhostProbeHosts(null, $input['probe'], $hostGroupIds, $templateIds);
	}

	protected function updateObject()
	{
		$input = $this->getInputAll();

		$hostGroupIds = $this->getHostGroupIds($this->getHostGroupNames([$input['probe']]));
		$templateIds = $this->getTemplateIds($this->getTemplateNames(['Template Probe Config ' . $input['probe']]));

		$config = $this->createTemplateConfig($input, $hostGroupIds);
		$config['templateid'] = $templateIds[$config['host']];
		$data = API::Template()->update($config);

		$config = $this->createProbeHostConfig($input, $hostGroupIds, $templateIds, null);
		$config['hostid'] = $this->getHostId($config['host']);
		$data = API::Host()->update($config);

		$config = $this->createProbeMonHostConfig($input, $hostGroupIds, $templateIds);
		$config['hostid'] = $this->getHostId($config['host']);
		$data = API::Host()->update($config);

		$this->updateRsmhostProbeHosts(null, $input['probe'], $hostGroupIds, $templateIds);
	}

	protected function deleteObject()
	{
	}

	private function getHostGroupNames(?array $additionalNames)
	{
		$names = [
			'Templates - TLD',
			'Probes',
			'Probes - Mon',
		];

		if (!is_null($additionalNames))
		{
			$names = array_merge($names, $additionalNames);
		}

		return $names;
	}

	private function getTemplateNames(?array $additionalNames)
	{
		$names = [
			'Template Probe Status',
			'Template Proxy Health',
		];

		if (!is_null($additionalNames))
		{
			$names = array_merge($names, $additionalNames);
		}

		return $names;
	}

	function createProxyConfig(array $input)
	{
		return [
			'host'             => $input['probe'],
			'status'           => HOST_STATUS_PROXY_PASSIVE,
			'tls_connect'      => HOST_ENCRYPTION_PSK,
			'tls_psk_identity' => $input['zabbixProxyParameters']['proxyPskIdentity'],
			'tls_psk'          => $input['zabbixProxyParameters']['proxyPsk'],
			'interface'        => [
				'type'  => INTERFACE_TYPE_AGENT,
				'main'  => INTERFACE_PRIMARY,
				'useip' => INTERFACE_USE_IP,
				'ip'    => $input['zabbixProxyParameters']['proxyIp'],
				'dns'   => '',
				'port'  => $input['zabbixProxyParameters']['proxyPort'],
			],
		];
	}

	private function createHostGroupConfig(array $input)
	{
		return [
			'name' => $input['probe'],
		];
	}

	function createTemplateConfig(array $input, array $hostGroupIds)
	{
		$services = array_column($input['servicesStatus'], 'enabled', 'service');

		return [
			'host'   => 'Template Probe Config ' . $input['probe'],
			'groups' => [
				['groupid' => $hostGroupIds['Templates - TLD']],
			],
			'macros' => [
				$this->createMacroConfig(self::MACRO_PROBE_IP4_ENABLED , (int)$input['zabbixProxyParameters']['ipv4Enable']),
				$this->createMacroConfig(self::MACRO_PROBE_IP6_ENABLED , (int)$input['zabbixProxyParameters']['ipv6Enable']),
				$this->createMacroConfig(self::MACRO_PROBE_RDAP_ENABLED, (int)$services['rdap']),
				$this->createMacroConfig(self::MACRO_PROBE_RDDS_ENABLED, (int)$services['rdds']),
				$this->createMacroConfig(self::MACRO_PROBE_RESOLVER    , $input['zabbixProxyParameters']['ipResolver']),
			],
		];
	}

	function createProbeHostConfig(array $input, array $hostGroupIds, array $templateIds, $proxyId)
	{
		$config = [
			'host'         => $input['probe'],
			'status'       => HOST_STATUS_MONITORED,
			'interfaces'   => [
				self::DEFAULT_MAIN_INTERFACE,
			],
			'groups'       => [
				['groupid' => $hostGroupIds['Probes']],
			],
			'templates'    => [
				['templateid' => $templateIds['Template Probe Config ' . $input['probe']]],
				['templateid' => $templateIds['Template Probe Status']],
			],
		];

		if (!is_null($proxyId))
		{
			$config['proxy_hostid'] = $proxyId;
		}

		return $config;
	}

	function createProbeMonHostConfig(array $input, array $hostGroupIds, array $templateIds)
	{
		return [
			'host'         => $input['probe'] . ' - mon',
			'status'       => HOST_STATUS_MONITORED,
			'interfaces'   => [
				[
					'type'  => INTERFACE_TYPE_AGENT,
					'main'  => INTERFACE_PRIMARY,
					'useip' => INTERFACE_USE_IP,
					'ip'    => $input['zabbixProxyParameters']['proxyIp'],
					'dns'   => '',
					'port'  => '10050',
				],
			],
			'groups'       => [
				['groupid' => $hostGroupIds['Probes - Mon']],
			],
			'templates'    => [
				['templateid' => $templateIds['Template Proxy Health']],
			],
			'macros'       => [
				$this->createMacroConfig(self::MACRO_PROBE_PROXY_NAME, $input['probe']),
			],
		];
	}
}
