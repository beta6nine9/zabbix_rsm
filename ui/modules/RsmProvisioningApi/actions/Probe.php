<?php

//declare(strict_types=1); // TODO: enable strict_types

namespace Modules\RsmProvisioningApi\Actions;

use API;
use Exception;
use Modules\RsmProvisioningApi\RsmException;

class Probe extends ActionBaseEx
{
	protected function checkMonitoringTarget(): bool
	{
		return true;
	}

	protected function getInputRules(): array
	{
		switch ($_SERVER['REQUEST_METHOD'])
		{
			case self::REQUEST_METHOD_GET:
				return [
					'type' => API_OBJECT, 'fields' => [
						'id'                    => ['type' => API_RSM_CUSTOM , 'function' => 'RsmValidateProbeIdentifier', 'error' => 'The syntax of the probe node in the URL is invalid'],
					]
				];

			case self::REQUEST_METHOD_DELETE:
				return [
					'type' => API_OBJECT, 'fields' => [
						'id'                    => ['type' => API_RSM_CUSTOM , 'function' => 'RsmValidateProbeIdentifier', 'error' => 'The syntax of the probe node in the URL is invalid'],
					]
				];

			case self::REQUEST_METHOD_PUT:
				return [
					'type' => API_OBJECT, 'fields' => [
						'id'                    => ['type' => API_RSM_CUSTOM , 'function' => 'RsmValidateProbeIdentifier', 'error' => 'The syntax of the probe node in the URL is invalid'],
						'probe'                 => ['type' => API_RSM_CUSTOM , 'function' => 'RsmValidateInvalid', 'error' => 'The "probe" element was included in a PUT request'],
						'servicesStatus'        => ['type' => API_OBJECTS    , 'uniq' => [['service']], 'fields' => [
							'service'           => ['type' => API_RSM_CUSTOM , 'function' => 'RsmValidateEnum', 'in' => ['rdap', 'rdds'], 'error' => 'Service is not supported'],
							'enabled'           => ['type' => API_BOOLEAN    ],
						]],
						'zabbixProxyParameters' => ['type' => API_OBJECT     , 'fields' => [
							'ipv4Enable'        => ['type' => API_BOOLEAN    ],
							'ipv6Enable'        => ['type' => API_BOOLEAN    ],
							'proxyIp'           => ['type' => API_RSM_CUSTOM , 'function' => 'RsmValidateIP', 'error' => 'Invalid IP provided in the "proxyIp" element'],
							'proxyPort'         => ['type' => API_RSM_CUSTOM , 'function' => 'RsmValidateInt', 'min' => 1, 'max' => 65535, 'error' => 'The "proxyPort" element must be a positive integer'],
							'proxyPskIdentity'  => ['type' => API_STRING_UTF8, 'flags' => API_NOT_EMPTY],
							'proxyPsk'          => ['type' => API_PSK        , 'flags' => API_NOT_EMPTY],
						]],
						'online'                => ['type' => API_RSM_CUSTOM , 'function' => 'RsmValidateInvalid', 'error' => ' The "online" element was in the payload'],
					]
				];

			default:
				throw new Exception('Unsupported request method');
		}
	}

	protected function rsmValidateInput(): void
	{
		parent::rsmValidateInput();

		if ($_SERVER['REQUEST_METHOD'] == self::REQUEST_METHOD_PUT)
		{
			$this->validateInputServices();
			$this->requireArrayKeys(['zabbixProxyParameters'], $this->input, 'JSON does not comply with definition');
			$this->requireArrayKeys(['ipv4Enable', 'ipv6Enable', 'proxyIp', 'proxyPort', 'proxyPskIdentity', 'proxyPsk'], $this->input['zabbixProxyParameters'], 'JSON does not comply with definition');

			if (!$this->input['zabbixProxyParameters']['ipv4Enable'] &&
				!$this->input['zabbixProxyParameters']['ipv6Enable'])
			{
				$services = array_column($this->input['servicesStatus'], 'enabled');

				if (!empty(array_filter($services)))
				{
					throw new RsmException(400, 'If ipv4Enable and ipv6Enable are disabled, then all services also must be disabled');
				}
			}
		}
	}

	/******************************************************************************************************************
	 * Functions for retrieving object                                                                                *
	 ******************************************************************************************************************/

	protected function getObjects(?string $objectId): array
	{
		// get hosts

		$data = $this->getHostsByHostGroup('Probes', $objectId, null);

		if (empty($data))
		{
			return [];
		}

		$hosts = array_column($data, 'host', 'hostid');

		// get templates

		$templateNames = array_values(array_map(fn($host) => 'Template Probe Config ' . $host, $hosts));
		$templates = array_flip($this->getTemplateIds($templateNames));

		// get template macros

		$macros = $this->getHostMacros(
			array_map(fn($host) => str_replace('Template Probe Config ', '', $host), $templates),
			[
				self::MACRO_PROBE_PROXY_IP,
				self::MACRO_PROBE_PROXY_PORT,
				self::MACRO_PROBE_IP4_ENABLED,
				self::MACRO_PROBE_IP6_ENABLED,
				self::MACRO_PROBE_RDAP_ENABLED,
				self::MACRO_PROBE_RDDS_ENABLED,
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
					'proxyIp'                   => $macros[$host][self::MACRO_PROBE_PROXY_IP],
					'proxyPort'                 => $macros[$host][self::MACRO_PROBE_PROXY_PORT],
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

	protected function createObject(): void
	{
		$this->hostGroupIds += $this->getHostGroupIds($this->getHostGroupNames(null));
		$this->templateIds  += $this->getTemplateIds($this->getTemplateNames(null));

		// create proxy

		$config = [
			'host'             => $this->newObject['id'],
			'status'           => HOST_STATUS_PROXY_PASSIVE,
			'tls_connect'      => HOST_ENCRYPTION_PSK,
			'tls_psk_identity' => $this->newObject['zabbixProxyParameters']['proxyPskIdentity'],
			'tls_psk'          => $this->newObject['zabbixProxyParameters']['proxyPsk'],
			'interface'        => [
				'useip' => INTERFACE_USE_IP,
				'ip'    => $this->newObject['zabbixProxyParameters']['proxyIp'],
				'dns'   => '',
				'port'  => $this->newObject['zabbixProxyParameters']['proxyPort'],
			],
		];
		$data = API::Proxy()->create($config);
		$proxyId = $data['proxyids'][0];

		// create "<probe>" host group

		$config = [
			'name' => $this->newObject['id'],
		];
		$data = API::HostGroup()->create($config);
		$this->hostGroupIds[$this->newObject['id']] = $data['groupids'][0];

		// create "Template Probe Config <probe>" template

		$config = [
			'host'   => 'Template Probe Config ' . $this->newObject['id'],
			'groups' => [
				['groupid' => $this->hostGroupIds['Templates - TLD']],
			],
			'macros' => $this->getMacrosConfig(),
		];
		$data = API::Template()->create($config);
		$this->templateIds['Template Probe Config ' . $this->newObject['id']] = $data['templateids'][0];

		// create "<probe>" host

		$config = [
			'host'         => $this->newObject['id'],
			'status'       => HOST_STATUS_MONITORED,
			'proxy_hostid' => $proxyId,
			'interfaces'   => [self::DEFAULT_MAIN_INTERFACE],
			'groups'       => [
				['groupid' => $this->hostGroupIds['Probes']],
			],
			'templates'    => [
				['templateid' => $this->templateIds['Template Probe Config ' . $this->newObject['id']]],
				['templateid' => $this->templateIds['Template Probe Status']],
			],
		];
		$data = API::Host()->create($config);

		// create "<probe> - mon" host

		$config = [
			'host'         => $this->newObject['id'] . ' - mon',
			'status'       => HOST_STATUS_MONITORED,
			'interfaces'   => [
				[
					'type'  => INTERFACE_TYPE_AGENT,
					'main'  => INTERFACE_PRIMARY,
					'useip' => INTERFACE_USE_IP,
					'ip'    => $this->newObject['zabbixProxyParameters']['proxyIp'],
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
				$this->createMacroConfig(self::MACRO_PROBE_PROXY_NAME, $this->newObject['id']),
			],
		];
		$data = API::Host()->create($config);

		// create "<rsmhost> <probe>" hosts

		$rsmhostConfigs = $this->getRsmhostConfigs();
		$probeConfigs = $this->getProbeConfigFromInput($proxyId);

		$testHosts = $this->createTestHosts($rsmhostConfigs, $probeConfigs);

		// enable/disable items, based on service status and standalone rdap status

		$this->updateServiceItemStatus([], $testHosts, $rsmhostConfigs, $probeConfigs);
	}

	/******************************************************************************************************************
	 * Functions for updating object                                                                                  *
	 ******************************************************************************************************************/

	protected function isObjectDisabled(array $object): bool
	{
		$params = $object['zabbixProxyParameters'];

		return !$params['ipv4Enable'] && !$params['ipv6Enable'];
	}

	protected function updateObject(): void
	{
		$this->templateIds += $this->getTemplateIds($this->getTemplateNames(null));

		// update proxy

		$proxyId = $this->getProxyId($this->newObject['id']);
		$interfaceId = $this->getInterfaceId($proxyId);

		$config = [
			'proxyid'          => $proxyId,
			'status'           => HOST_STATUS_PROXY_PASSIVE,
			'tls_connect'      => HOST_ENCRYPTION_PSK,
			'tls_psk_identity' => $this->newObject['zabbixProxyParameters']['proxyPskIdentity'],
			'tls_psk'          => $this->newObject['zabbixProxyParameters']['proxyPsk'],
			'interface'        => [
				'interfaceid'  => $interfaceId,
				'useip'        => INTERFACE_USE_IP,
				'ip'           => $this->newObject['zabbixProxyParameters']['proxyIp'],
				'dns'          => '',
				'port'         => $this->newObject['zabbixProxyParameters']['proxyPort'],
			],
		];
		$data = API::Proxy()->update($config);

		// update "Template Probe Config <probe>" template

		$config = [
			'templateid' => $this->getTemplateId('Template Probe Config ' . $this->newObject['id']),
			'macros'     => $this->getMacrosConfig(),
		];
		$data = API::Template()->update($config);

		// update "<probe>" host

		$config = [
			'hostid' => $this->getHostId($this->newObject['id']),
		];
		$data = API::Host()->update($config);

		// update "<probe> - mon" host

		$config = [
			'hostid'     => $this->getHostId($this->newObject['id'] . ' - mon'),
			'interfaces' => [
				[
					'type'  => INTERFACE_TYPE_AGENT,
					'main'  => INTERFACE_PRIMARY,
					'useip' => INTERFACE_USE_IP,
					'ip'    => $this->newObject['zabbixProxyParameters']['proxyIp'],
					'dns'   => '',
					'port'  => '10050',
				],
			],
		];
		$data = API::Host()->update($config);

		// enable/disable items, based on service status and standalone rdap status

		$testHosts = $this->getHostsByHostGroup($this->newObject['id'], null, null);

		$rsmhostConfigs = $this->getRsmhostConfigs();
		$probeConfigs = $this->getProbeConfigFromInput($proxyId);

		$this->updateServiceItemStatus([], $testHosts, $rsmhostConfigs, $probeConfigs);
	}

	protected function disableObject(): void
	{
		// get proxyid and hostids of "<probe>" and "<rsmhost> <probe>" hosts
		$data = API::Proxy()->get([
			'output'      => ['proxyid'],
			'filter'      => [ 'host' => $this->input['id']],
			'selectHosts' => ['hostid', 'host'],
		]);
		$proxyId = $data[0]['proxyid'];
		$hostids = array_column($data[0]['hosts'], 'hostid', 'host');

		// get hostid of "<probe> - mon"
		$hostids[$this->input['id'] . ' - mon'] = $this->getHostId($this->input['id'] . ' - mon');

		// disable hosts
		$config = array_map(
			fn($hostid) => [
				'hostid' => $hostid,
				'status' => HOST_STATUS_NOT_MONITORED,
			],
			array_values($hostids)
		);
		$data = API::Host()->update($config);

		// update macros
		$config = [
			'templateid' => $this->getTemplateId('Template Probe Config ' . $this->newObject['id']),
			'macros'     => $this->getMacrosConfig(),
		];
		$data = API::Template()->update($config);

		// "disable" proxy
		$config = [
			'proxyid' => $proxyId,
			'status'  => HOST_STATUS_PROXY_ACTIVE,
		];
		$data = API::Proxy()->update($config);
	}

	/******************************************************************************************************************
	 * Functions for deleting object                                                                                  *
	 ******************************************************************************************************************/

	protected function deleteObject(): void
	{
		$templateId = $this->getTemplateId('Template Probe Config ' . $this->input['id']);

		$hostids = array_column($this->getHostsByTemplateId($templateId, null, null), 'hostid', 'host');
		$hostids += $this->getHostIds([$this->input['id'] . ' - mon']);

		// delete "<probe>", "<probe> - mon", "<rsmhost> <probe>" hosts
		$data = API::Host()->delete(array_values($hostids));

		// delete "Template Probe Config <probe>" template
		$data = API::Template()->delete([$templateId]);

		// delete "<probe>" host group
		$hostGroupId = $this->getHostGroupId($this->input['id']);
		$data = API::HostGroup()->delete([$hostGroupId]);

		// delete proxy
		$proxyId = $this->getProxyId($this->input['id']);
		$data = API::Proxy()->delete([$proxyId]);
	}

	/******************************************************************************************************************
	 * Helper functions                                                                                               *
	 ******************************************************************************************************************/

	private function getProbeConfigFromInput(?int $proxyId): array
	{
		$services = array_column($this->newObject['servicesStatus'], 'enabled', 'service');

		return [
			$this->newObject['id'] => [
				'proxy_hostid' => $proxyId,
				'ipv4'         => $this->newObject['zabbixProxyParameters']['ipv4Enable'],
				'ipv6'         => $this->newObject['zabbixProxyParameters']['ipv6Enable'],
				'rdap'         => $services['rdap'],
				'rdds'         => $services['rdds'],
			],
		];
	}

	private function getHostGroupNames(?array $additionalNames): array
	{
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

	private function getTemplateNames(?array $additionalNames): array
	{
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

	private function getMacrosConfig(): array
	{
		$services = array_column($this->newObject['servicesStatus'], 'enabled', 'service');

		// use IPv4 resolver if either IPv4 is enabled or probe is disabled, IPv6 resolver if IPv4 is disabled, IPv6 is enabled
		$ipResolver = !$this->newObject['zabbixProxyParameters']['ipv4Enable'] ? '0:0:0:0:0:0:0:1' : '127.0.0.1';

		return [
			$this->createMacroConfig(self::MACRO_PROBE_PROXY_IP    , $this->newObject['zabbixProxyParameters']['proxyIp']),
			$this->createMacroConfig(self::MACRO_PROBE_PROXY_PORT  , $this->newObject['zabbixProxyParameters']['proxyPort']),
			$this->createMacroConfig(self::MACRO_PROBE_IP4_ENABLED , (int)$this->newObject['zabbixProxyParameters']['ipv4Enable']),
			$this->createMacroConfig(self::MACRO_PROBE_IP6_ENABLED , (int)$this->newObject['zabbixProxyParameters']['ipv6Enable']),
			$this->createMacroConfig(self::MACRO_PROBE_RDAP_ENABLED, (int)$services['rdap']),
			$this->createMacroConfig(self::MACRO_PROBE_RDDS_ENABLED, (int)$services['rdds']),
			$this->createMacroConfig(self::MACRO_PROBE_RESOLVER    , $ipResolver),
		];
	}
}
