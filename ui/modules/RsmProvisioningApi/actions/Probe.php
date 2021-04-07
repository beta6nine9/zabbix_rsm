<?php

namespace Modules\RsmProvisioningApi\Actions;

use API;

class Probe extends ActionBaseEx {

	protected function checkMonitoringTarget() {
		return true;
	}

	protected function getObjectIdInputField() {
		return 'probe';
	}

	protected function getInputRules(): array {
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

		$data = $this->getHostsByHostGroup('Probes', $objectId, null);

		if (empty($data))
		{
			return [];
		}

		$hosts = array_column($data, 'host', 'hostid');

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
			];
		}

		return $result;
	}

	/******************************************************************************************************************
	 * Functions for creating object                                                                                  *
	 ******************************************************************************************************************/

	protected function createObject() {
		$input = $this->getInputAll();

		$this->hostGroupIds += $this->getHostGroupIds($this->getHostGroupNames(null));
		$this->templateIds  += $this->getTemplateIds($this->getTemplateNames(null));

		// create proxy

		$config = [
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
		$data = API::Proxy()->create($config);
		$proxyId = $data['proxyids'][0];

		// create "<probe>" host group

		$config = [
			'name' => $input['probe'],
		];
		$data = API::HostGroup()->create($config);
		$this->hostGroupIds[$input['probe']] = $data['groupids'][0];

		// create "Template Probe Config <probe>" template

		$config = [
			'host'   => 'Template Probe Config ' . $input['probe'],
			'groups' => [
				['groupid' => $this->hostGroupIds['Templates - TLD']],
			],
			'macros' => $this->getMacrosConfig($input),
		];
		$data = API::Template()->create($config);
		$this->templateIds['Template Probe Config ' . $input['probe']] = $data['templateids'][0];

		// create "<probe>" host

		$config = [
			'host'         => $input['probe'],
			'status'       => HOST_STATUS_MONITORED,
			'proxy_hostid' => $proxyId,
			'interfaces'   => [self::DEFAULT_MAIN_INTERFACE],
			'groups'       => [
				['groupid' => $this->hostGroupIds['Probes']],
			],
			'templates'    => [
				['templateid' => $this->templateIds['Template Probe Config ' . $input['probe']]],
				['templateid' => $this->templateIds['Template Probe Status']],
			],
		];
		$data = API::Host()->create($config);

		// create "<probe> - mon" host

		$config = [
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
				['groupid' => $this->hostGroupIds['Probes - Mon']],
			],
			'templates'    => [
				['templateid' => $this->templateIds['Template Proxy Health']],
			],
			'macros'       => [
				$this->createMacroConfig(self::MACRO_PROBE_PROXY_NAME, $input['probe']),
			],
		];
		$data = API::Host()->create($config);

		// create "<rsmhost> <probe>" hosts

		$rsmhostConfigs = $this->getRsmhostConfigs();
		$probeConfigs = $this->getProbeConfigs();

		$rsmhostProbeHosts = $this->createTestHosts($rsmhostConfigs, $probeConfigs);

		// enable/disable items, based on service status and standalone rdap status

		$this->updateServiceItemStatus([], $rsmhostProbeHosts, $rsmhostConfigs, $probeConfigs);
	}

	/******************************************************************************************************************
	 * Functions for updating object                                                                                  *
	 ******************************************************************************************************************/

	protected function updateObject() {
		$input = $this->getInputAll();

		$this->templateIds += $this->getTemplateIds($this->getTemplateNames(null));

		// update "Template Probe Config <probe>" template

		$config = [
			'templateid' => $this->getTemplateId('Template Probe Config ' . $input['probe']),
			'macros'     => $this->getMacrosConfig($input),
		];
		$data = API::Template()->update($config);

		// update "<probe>" host

		$config = [
			'hostid' => $this->getHostId($input['probe']),
		];
		$data = API::Host()->update($config);

		// update "<probe> - mon" host

		$config = [
			'hostid'     => $this->getHostId($input['probe'] . ' - mon'),
			'interfaces' => [
				[
					'type'  => INTERFACE_TYPE_AGENT,
					'main'  => INTERFACE_PRIMARY,
					'useip' => INTERFACE_USE_IP,
					'ip'    => $input['zabbixProxyParameters']['proxyIp'],
					'dns'   => '',
					'port'  => '10050',
				],
			],
		];
		$data = API::Host()->update($config);

		// enable/disable items, based on service status and standalone rdap status

		$rsmhostProbeHosts = $this->getHostsByHostGroup($input['probe'], null, null);

		$rsmhostConfigs = $this->getRsmhostConfigs();

		$services = array_column($input['servicesStatus'], 'enabled', 'service');

		$probeConfigs = [
			$input['probe'] => [
				'ipv4' => $input['zabbixProxyParameters']['ipv4Enable'],
				'ipv6' => $input['zabbixProxyParameters']['ipv6Enable'],
				'rdap' => $services['rdap'],
				'rdds' => $services['rdds'],
			],
		];

		$this->updateServiceItemStatus([], $rsmhostProbeHosts, $rsmhostConfigs, $probeConfigs);
	}

	/******************************************************************************************************************
	 * Functions for deleting object                                                                                  *
	 ******************************************************************************************************************/

	protected function deleteObject() {
		$input = $this->getInputAll();

		$templateId = $this->getTemplateId('Template Probe Config ' . $input['probe']);

		$hostids = array_column($this->getHostsByTemplateId($templateId, null, null), 'hostid', 'host');
		$hostids += $this->getHostIds([$input['probe'] . ' - mon']);

		// delete "<probe>", "<probe> - mon", "<rsmhost> <probe>" hosts
		$data = API::Host()->delete(array_values($hostids));

		// delete "Template Probe Config <probe>" template
		$data = API::Template()->delete([$templateId]);

		// delete "<probe>" host group
		$hostGroupId = $this->getHostGroupId($input['probe']);
		$data = API::HostGroup()->delete([$hostGroupId]);

		// delete proxy
		$proxyId = $this->getProxyId($input['probe']);
		$data = API::Proxy()->delete([$proxyId]);
	}

	/******************************************************************************************************************
	 * Helper functions                                                                                               *
	 ******************************************************************************************************************/

	private function getHostGroupNames(?array $additionalNames) {
		$names = [
			// groups for "<rsmhost>" and "<rsmhost> - mon" hosts
			'Templates - TLD',
			'Probes',
			'Probes - Mon',
			// groups for "<rsmhost> <probe>" hosts
			'TLD Probe results',
			'gTLD Probe results',
			'ccTLD Probe results',
			'testTLD Probe results',
			'otherTLD Probe results',
		];

		if (!is_null($additionalNames))
		{
			$names = array_merge($names, $additionalNames);
		}

		return $names;
	}

	private function getTemplateNames(?array $additionalNames) {
		$names = [
			// templates for "<rsmhost>" and "<rsmhost> - mon" hosts
			'Template Probe Status',
			'Template Proxy Health',
			// templates for "<rsmhost> <probe>" hosts
			'Template DNS Test',
			'Template RDAP Test',
			'Template RDDS Test',
		];

		if (!is_null($additionalNames))
		{
			$names = array_merge($names, $additionalNames);
		}

		return $names;
	}

	private function getMacrosConfig(array $input) {
		$services = array_column($input['servicesStatus'], 'enabled', 'service');

		return [
			$this->createMacroConfig(self::MACRO_PROBE_IP4_ENABLED , (int)$input['zabbixProxyParameters']['ipv4Enable']),
			$this->createMacroConfig(self::MACRO_PROBE_IP6_ENABLED , (int)$input['zabbixProxyParameters']['ipv6Enable']),
			$this->createMacroConfig(self::MACRO_PROBE_RDAP_ENABLED, (int)$services['rdap']),
			$this->createMacroConfig(self::MACRO_PROBE_RDDS_ENABLED, (int)$services['rdds']),
			$this->createMacroConfig(self::MACRO_PROBE_RESOLVER    , $input['zabbixProxyParameters']['ipResolver']),
		];
	}
}
