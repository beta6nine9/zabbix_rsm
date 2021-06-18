<?php

//declare(strict_types=1); // TODO: enable strict_types

namespace Modules\RsmProvisioningApi\Actions;

require_once __DIR__ . '/../validators/validators.inc.php';

use API;
use Exception;

abstract class ActionBaseEx extends ActionBase
{
	protected const DEFAULT_MAIN_INTERFACE = [
		'type'  => INTERFACE_TYPE_AGENT,
		'main'  => INTERFACE_PRIMARY,
		'useip' => INTERFACE_USE_IP,
		'ip'    => '127.0.0.1',
		'dns'   => '',
		'port'  => '10050',
	];

	private const MACRO_GLOBAL_MONITORING_TARGET   = '{$RSM.MONITORING.TARGET}';
	private const MACRO_GLOBAL_RDAP_STANDALONE     = '{$RSM.RDAP.STANDALONE}';
	private const MACRO_GLOBAL_CONFIG_CACHE_RELOAD = '{$RSM.CONFIG.CACHE.RELOAD.REQUESTED}';

	protected const MACRO_PROBE_PROXY_NAME   = '{$RSM.PROXY_NAME}';
	protected const MACRO_PROBE_PROXY_IP     = '{$RSM.PROXY.IP}';
	protected const MACRO_PROBE_PROXY_PORT   = '{$RSM.PROXY.PORT}';
	protected const MACRO_PROBE_IP4_ENABLED  = '{$RSM.IP4.ENABLED}';
	protected const MACRO_PROBE_IP6_ENABLED  = '{$RSM.IP6.ENABLED}';
	protected const MACRO_PROBE_RDAP_ENABLED = '{$RSM.RDAP.ENABLED}';
	protected const MACRO_PROBE_RDDS_ENABLED = '{$RSM.RDDS.ENABLED}';
	protected const MACRO_PROBE_RESOLVER     = '{$RSM.RESOLVER}';

	protected const MACRO_TLD                    = '{$RSM.TLD}';
	protected const MACRO_TLD_CONFIG_TIMES       = '{$RSM.TLD.CONFIG.TIMES}';
	protected const MACRO_TLD_DNS_UDP_ENABLED    = '{$RSM.TLD.DNS.UDP.ENABLED}';
	protected const MACRO_TLD_DNS_TCP_ENABLED    = '{$RSM.TLD.DNS.TCP.ENABLED}';
	protected const MACRO_TLD_DNSSEC_ENABLED     = '{$RSM.TLD.DNSSEC.ENABLED}';
	protected const MACRO_TLD_RDAP_ENABLED       = '{$RDAP.TLD.ENABLED}';
	protected const MACRO_TLD_RDDS_ENABLED       = '{$RSM.TLD.RDDS.ENABLED}';
	protected const MACRO_TLD_DNS_NAME_SERVERS   = '{$RSM.DNS.NAME.SERVERS}';
	protected const MACRO_TLD_DNS_AVAIL_MINNS    = '{$RSM.TLD.DNS.AVAIL.MINNS}';
	protected const MACRO_TLD_DNS_TESTPREFIX     = '{$RSM.DNS.TESTPREFIX}';
	protected const MACRO_TLD_RDAP_BASE_URL      = '{$RDAP.BASE.URL}';
	protected const MACRO_TLD_RDAP_TEST_DOMAIN   = '{$RDAP.TEST.DOMAIN}';
	protected const MACRO_TLD_RDDS43_TEST_DOMAIN = '{$RSM.RDDS43.TEST.DOMAIN}';
	protected const MACRO_TLD_RDDS43_SERVER      = '{$RSM.TLD.RDDS43.SERVER}';
	protected const MACRO_TLD_RDDS43_NS_STRING   = '{$RSM.RDDS.NS.STRING}';
	protected const MACRO_TLD_RDDS80_URL         = '{$RSM.TLD.RDDS80.URL}';

	protected const MACRO_DESCRIPTIONS = [
		self::MACRO_PROBE_PROXY_NAME       => '',
		self::MACRO_PROBE_PROXY_IP         => 'Proxy IP of the proxy',
		self::MACRO_PROBE_PROXY_PORT       => 'Port of the proxy',
		self::MACRO_PROBE_IP4_ENABLED      => 'Indicates whether the probe supports IPv4',
		self::MACRO_PROBE_IP6_ENABLED      => 'Indicates whether the probe supports IPv6',
		self::MACRO_PROBE_RDAP_ENABLED     => 'Indicates whether the probe supports RDAP protocol',
		self::MACRO_PROBE_RDDS_ENABLED     => 'Indicates whether the probe supports RDDS protocol',
		self::MACRO_PROBE_RESOLVER         => 'DNS resolver used by the probe',

		self::MACRO_TLD                    => 'Name of the rsmhost, e. g. "example"',
		self::MACRO_TLD_CONFIG_TIMES       => 'Semicolon separated list of timestamps when TLD was changed',
		self::MACRO_TLD_DNS_UDP_ENABLED    => 'Indicates whether DNS UDP enabled on the rsmhost',
		self::MACRO_TLD_DNS_TCP_ENABLED    => 'Indicates whether DNS TCP enabled on the rsmhost',
		self::MACRO_TLD_DNSSEC_ENABLED     => 'Indicates whether DNSSEC is enabled on the rsmhost',
		self::MACRO_TLD_RDAP_ENABLED       => 'Indicates whether RDAP is enabled on the rsmhost',
		self::MACRO_TLD_RDDS_ENABLED       => 'Indicates whether RDDS is enabled on the rsmhost',
		self::MACRO_TLD_DNS_NAME_SERVERS   => 'List of Name Server (name, IP pairs) to monitor',
		self::MACRO_TLD_DNS_AVAIL_MINNS    => 'Consider DNS Service availability at a particular time UP if during DNS test more than specified number of Name Servers replied successfully.',
		self::MACRO_TLD_DNS_TESTPREFIX     => 'Prefix for DNS tests, e.g. nonexistent',
		self::MACRO_TLD_RDAP_BASE_URL      => 'Base URL for RDAP queries, e.g. http://whois.zabbix',
		self::MACRO_TLD_RDAP_TEST_DOMAIN   => 'Test domain for RDAP queries, e.g. whois.zabbix',
		self::MACRO_TLD_RDDS43_TEST_DOMAIN => 'Domain name to use when querying RDDS43 server, e.g. "whois.example"',
		self::MACRO_TLD_RDDS43_SERVER      => 'Hostname of the RDDS43 server',
		self::MACRO_TLD_RDDS80_URL         => 'URL of the RDDS80 service to be tested',
		self::MACRO_TLD_RDDS43_NS_STRING   => 'What to look for in RDDS output, e.g. "Name Server:"',
	];

	protected const MONITORING_TARGET_REGISTRY  = 'registry';
	protected const MONITORING_TARGET_REGISTRAR = 'registrar';

	protected $templateIds  = [];
	protected $hostGroupIds = [];

	/**
	 * Creates "<rsmhost> <probe>" hosts when either new rsmhost or new probe is created.
	 */
	protected function createTestHosts(array $rsmhostConfigs, array $probeConfigs): array
	{
		// get missing host group ids

		$missingHostGroups = array_merge(
			array_diff(
				array_map(fn($rsmhost) => 'TLD ' . $rsmhost, array_keys($rsmhostConfigs)),
				array_keys($this->hostGroupIds)
			),
			array_diff(
				array_keys($probeConfigs),
				array_keys($this->hostGroupIds)
			)
		);
		$this->hostGroupIds += $this->getHostGroupIds($missingHostGroups);

		// get missing template ids

		$missingTemplates = array_merge(
			array_diff(
				array_map(fn($rsmhost) => 'Template Rsmhost Config ' . $rsmhost, array_keys($rsmhostConfigs)),
				array_keys($this->templateIds)
			),
			array_diff(
				array_map(fn($probe) => 'Template Probe Config ' . $probe, array_keys($probeConfigs)),
				array_keys($this->templateIds)
			)
		);
		if ($this->getMonitoringTarget() === self::MONITORING_TARGET_REGISTRY)
		{
			$missingTemplates = array_merge(
				$missingTemplates,
				array_diff(
					array_map(fn($rsmhost) => 'Template DNS Test - ' . $rsmhost, array_keys($rsmhostConfigs)),
					array_keys($this->templateIds)
				)
			);
		}
		$this->templateIds += $this->getTemplateIds($missingTemplates);

		// create configs for hosts

		$configs = [];

		foreach ($rsmhostConfigs as $rsmhost => $rsmhostConfig)
		{
			foreach ($probeConfigs as $probe => $probeConfig)
			{
				$enabled = $rsmhostConfig['enabled'] && $probeConfig['enabled'];

				$configs[] = [
					'host'         => $rsmhost . ' ' . $probe,
					'status'       => $enabled ? HOST_STATUS_MONITORED : HOST_STATUS_NOT_MONITORED,
					'proxy_hostid' => $probeConfig['proxy_hostid'],
					'interfaces'   => [self::DEFAULT_MAIN_INTERFACE],
					'groups'       => $this->getTestHostGroupsConfig($rsmhostConfig['tldType'], $probe, $rsmhost),
					'templates'    => $this->getTestTemplatesConfig($probe, $rsmhost),
				];
			}
		}

		// create hosts

		$hosts = [];

		if (!empty($configs))
		{
			$data = API::Host()->create($configs);
			$hosts = array_combine($data['hostids'], array_column($configs, 'host'));
		}

		return $hosts;
	}

	/**
	 * Updates "<rsmhost> <probe>" hosts when tldType of existing rsmhost is modified.
	 */
	protected function updateTestHosts(array $rsmhostConfigs, array $probeConfigs): array
	{
		// get missing host group ids

		$missingHostGroups = array_merge(
			array_diff(
				array_map(fn($rsmhost) => 'TLD ' . $rsmhost, array_keys($rsmhostConfigs)),
				array_keys($this->hostGroupIds)
			),
			array_diff(
				array_keys($probeConfigs),
				array_keys($this->hostGroupIds)
			)
		);
		$this->hostGroupIds += $this->getHostGroupIds($missingHostGroups);

		// create list of hosts, get hostids

		$hosts = [];

		foreach ($rsmhostConfigs as $rsmhost => $rsmhostConfig)
		{
			foreach ($probeConfigs as $probe => $probeConfig)
			{
				$hosts[] = $rsmhost . ' ' . $probe;
			}
		}

		$hostids = $this->getHostIds($hosts);
		$hosts = array_flip($hostids);

		// create configs for hosts

		$configs = [];

		foreach ($rsmhostConfigs as $rsmhost => $rsmhostConfig)
		{
			foreach ($probeConfigs as $probe => $probeConfig)
			{
				$enabled = $rsmhostConfig['enabled'] && $probeConfig['enabled'];

				$configs[] = [
					'hostid' => $hostids[$rsmhost . ' ' . $probe],
					'status' => $enabled ? HOST_STATUS_MONITORED : HOST_STATUS_NOT_MONITORED,
					'groups' => $this->getTestHostGroupsConfig($rsmhostConfig['tldType'], $probe, $rsmhost),
				];
			}
		}

		// update hosts

		if (!empty($configs))
		{
			$data = API::Host()->update($configs);
		}

		return $hosts;
	}

	private function getTestHostGroupsConfig(string $tldType, string $probe, string $rsmhost): array
	{
		return [
			['groupid' => $this->hostGroupIds['TLD Probe results']],
			['groupid' => $this->hostGroupIds[$tldType . ' Probe results']],
			['groupid' => $this->hostGroupIds[$probe]],
			['groupid' => $this->hostGroupIds['TLD ' . $rsmhost]],
		];
	}

	protected function getTestTemplatesConfig(string $probe, string $rsmhost): array
	{
		$templates = [
			['templateid' => $this->templateIds['Template RDAP Test']],
			['templateid' => $this->templateIds['Template RDDS Test']],
			['templateid' => $this->templateIds['Template Probe Config ' . $probe]],
			['templateid' => $this->templateIds['Template Rsmhost Config ' . $rsmhost]],
		];

		if ($this->getMonitoringTarget() === self::MONITORING_TARGET_REGISTRY)
		{
			$templates[] = ['templateid' => $this->templateIds['Template DNS Test - ' . $rsmhost]];
		}

		return $templates;
	}

	/**
	 * Enables and disables items in "<rsmhost>" and "<rsmhost> <probe>" hosts.
	 */
	protected function updateServiceItemStatus(array $statusHosts, array $testHosts, array $rsmhostConfigs, array $probeConfigs): void
	{
		$hosts = $statusHosts + $testHosts;

		// get template items

		$config = [
			'output' => ['key_', 'hostid'],
			'hostids' => [],
		];

		$config['hostids'] = array_merge(
			$config['hostids'],
			[
				$this->templateIds['Template RDAP Test'],
				$this->templateIds['Template RDDS Test'],
			]
		);
		if (!empty($statusHosts))
		{
			$config['hostids'] = array_merge(
				$config['hostids'],
				[
					$this->templateIds['Template RDAP Status'],
					$this->templateIds['Template RDDS Status'],
				]
			);
		}

		if ($this->getMonitoringTarget() === self::MONITORING_TARGET_REGISTRY)
		{
			$config['hostids'] = array_merge(
				$config['hostids'],
				[
					$this->templateIds['Template DNS Test'],
				]
			);
			if (!empty($statusHosts))
			{
				$config['hostids'] = array_merge(
					$config['hostids'],
					[
						$this->templateIds['Template DNS Status'],
						$this->templateIds['Template DNSSEC Status'],
					]
				);
			}
		}

		$data = API::Item()->get($config);

		$templates = array_flip($this->templateIds);

		$templateItems = []; // [host => [key1, key2, ...], ...]

		foreach ($data as $item)
		{
			$templateItems[$templates[$item['hostid']]][] = $item['key_'];
		}

		// get host items

		$config = [
			'output'  => ['itemid', 'key_', 'status', 'hostid'],
			'hostids' => array_keys($hosts),
		];
		$data = API::Item()->get($config);

		// group host items by host and status

		$hostItems = [];

		foreach ($data as $item)
		{
			$itemid = $item['itemid'];
			$key    = $item['key_'];
			$status = $item['status'];
			$host   = $hosts[$item['hostid']];

			if ($status != ITEM_STATUS_ACTIVE && $status != ITEM_STATUS_DISABLED)
			{
				throw new Exception("Unexpected item status");
			}

			$hostItems[$host][$key] = [
				'itemid' => $itemid,
				'status' => $status,
			];
		}

		// enable/disable items

		$config = [];

		foreach ($statusHosts as $hostid => $host)
		{
			if ($this->getMonitoringTarget() === self::MONITORING_TARGET_REGISTRY)
			{
				$status = $rsmhostConfigs[$host]['dnssec'] ? ITEM_STATUS_ACTIVE : ITEM_STATUS_DISABLED;
				$config += $this->getItemStatusConfig($hostItems[$host], $templateItems['Template DNSSEC Status'], $status);
			}

			if ($this->isStandaloneRdap())
			{
				$status = $rsmhostConfigs[$host]['rdap'] ? ITEM_STATUS_ACTIVE : ITEM_STATUS_DISABLED;
				$config += $this->getItemStatusConfig($hostItems[$host], $templateItems['Template RDAP Status'], $status);

				$status = $rsmhostConfigs[$host]['rdds43'] ? ITEM_STATUS_ACTIVE : ITEM_STATUS_DISABLED;
				$config += $this->getItemStatusConfig($hostItems[$host], $templateItems['Template RDDS Status'], $status);

				//$status = $rsmhostConfigs[$host]['rdds80'] ? ITEM_STATUS_ACTIVE : ITEM_STATUS_DISABLED;
				//$config += $this->getItemStatusConfig($hostItems[$host], $templateItems['Template RDDS Status'], $status);
			}
			else
			{
				$status = ITEM_STATUS_DISABLED;
				$config += $this->getItemStatusConfig($hostItems[$host], $templateItems['Template RDAP Status'], $status);

				$status = $rsmhostConfigs[$host]['rdap'] || $rsmhostConfigs[$host]['rdds43'] ? ITEM_STATUS_ACTIVE : ITEM_STATUS_DISABLED;
				$config += $this->getItemStatusConfig($hostItems[$host], $templateItems['Template RDDS Status'], $status);
			}

			foreach ($testHosts as $hostid => $host)
			{
				list ($rsmhost, $probe) = explode(' ', $host);

				$status = $rsmhostConfigs[$rsmhost]['rdap'] && $probeConfigs[$probe]['rdap'] ? ITEM_STATUS_ACTIVE : ITEM_STATUS_DISABLED;
				$config += $this->getItemStatusConfig($hostItems[$host], $templateItems['Template RDAP Test'], $status);

				$status = $rsmhostConfigs[$rsmhost]['rdds43'] && $probeConfigs[$probe]['rdds'] ? ITEM_STATUS_ACTIVE : ITEM_STATUS_DISABLED;
				$config += $this->getItemStatusConfig($hostItems[$host], $templateItems['Template RDDS Test'], $status);

				//$status = $rsmhostConfigs[$rsmhost]['rdds80'] && $probeConfigs[$probe]['rdds'] ? ITEM_STATUS_ACTIVE : ITEM_STATUS_DISABLED;
				//$config += $this->getItemStatusConfig($hostItems[$host], $templateItems['Template RDDS Test'], $status);
			}
		}

		if (!empty($config))
		{
			$data = API::Item()->update($config);
		}
	}

	/**
	 * Creates $config for enabling/disabling host items. Used in updateServiceItemStatus().
	 *
	 * @param array $items     list of all items
	 * @param array $keys      list of keys that need to be updated
	 * @param int   $newStatus new status
	 *
	 * @return array
	 */
	private function getItemStatusConfig(array $items, array $keys, int $newStatus): array
	{
		$config = [];

		foreach ($keys as $key)
		{
			if ($items[$key]['status'] != $newStatus)
			{
				$itemid = $items[$key]['itemid'];

				$config[$itemid] = [
					'itemid' => $itemid,
					'status' => $newStatus,
				];
			}
		}

		return $config;
	}

	/**
	 * Returns monitoring target.
	 */
	protected function getMonitoringTarget(): string
	{
		static $result = null;

		if (is_null($result))
		{
			$result = $this->getGlobalMacro(self::MACRO_GLOBAL_MONITORING_TARGET);
		}

		return $result;
	}

	/**
	 * Returns status of Standalone RDAP.
	 */
	protected function isStandaloneRdap(): bool
	{
		static $result = null;

		if (is_null($result))
		{
			$ts = (int)$this->getGlobalMacro(self::MACRO_GLOBAL_RDAP_STANDALONE);

			$result = $ts && $_SERVER['REQUEST_TIME'] >= $ts;
		}

		return $result;
	}

	/**
	 * Requests config-cache-reload to be performed.
	 */
	protected function requestConfigCacheReload(): void
	{
		$this->setGlobalMacro(self::MACRO_GLOBAL_CONFIG_CACHE_RELOAD, time());
	}

	protected function getProbeConfigs(): array
	{
		// get 'Probes' host group id
		$this->hostGroupIds += $this->getHostGroupIds(['Probes']);

		// get probe hosts
		$data = API::Host()->get([
			'output'   => ['host', 'proxy_hostid'],
			'groupids' => [$this->hostGroupIds['Probes']],
		]);
		$hosts = array_column($data, 'host', 'hostid');
		$proxies = array_column($data, 'proxy_hostid', 'hostid');

		if (empty($hosts))
		{
			return [];
		}

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
			]
		);

		// join data in a common data structure
		$result = [];

		foreach ($hosts as $hostid => $host)
		{
			$config = [
				'proxy_hostid' => $proxies[$hostid],
				'enabled'      => null,
				'ipv4'         => (bool)$macros[$host][self::MACRO_PROBE_IP4_ENABLED],
				'ipv6'         => (bool)$macros[$host][self::MACRO_PROBE_IP6_ENABLED],
				'rdap'         => (bool)$macros[$host][self::MACRO_PROBE_RDAP_ENABLED],
				'rdds'         => (bool)$macros[$host][self::MACRO_PROBE_RDDS_ENABLED],
			];

			$config['enabled'] = $config['ipv4'] || $config['ipv6'];

			$result[$host] = $config;
		}

		return $result;
	}

	protected function getRsmhostConfigs(): array
	{
		// Warning: This method is used from Probe.php and cannot be moved to Tld.php and Registrar.php

		// get 'TLDs' host group id
		$this->hostGroupIds += $this->getHostGroupIds(['TLDs']);

		// get tld hosts
		$data = API::Host()->get([
			'output'   => ['host'],
			'groupids' => [$this->hostGroupIds['TLDs']],
		]);
		$hosts = array_column($data, 'host', 'hostid');

		if (empty($hosts))
		{
			return [];
		}

		// get templates
		$templateNames = array_values(array_map(fn($host) => 'Template Rsmhost Config ' . $host, $hosts));
		$templates = array_flip($this->getTemplateIds($templateNames));

		// get template macros
		$macros = $this->getHostMacros(
			array_map(fn($host) => str_replace('Template Rsmhost Config ', '', $host), $templates),
			[
				self::MACRO_TLD_DNS_UDP_ENABLED,
				self::MACRO_TLD_DNS_TCP_ENABLED,
				self::MACRO_TLD_DNSSEC_ENABLED,
				self::MACRO_TLD_RDAP_ENABLED,
				self::MACRO_TLD_RDDS_ENABLED,
			]
		);

		// get TLD types
		$tldTypes = $this->getTldTypes(array_keys($hosts));

		// join data in a common data structure
		$result = [];

		foreach ($hosts as $host)
		{
			$config = null;

			switch ($this->getMonitoringTarget())
			{
				case self::MONITORING_TARGET_REGISTRY:
					$config = [
						'tldType' => $tldTypes[$host],
						'enabled' => null,
						'dnsUdp'  => (bool)$macros[$host][self::MACRO_TLD_DNS_UDP_ENABLED],
						'dnsTcp'  => (bool)$macros[$host][self::MACRO_TLD_DNS_TCP_ENABLED],
						'dnssec'  => (bool)$macros[$host][self::MACRO_TLD_DNSSEC_ENABLED],
						'rdap'    => (bool)$macros[$host][self::MACRO_TLD_RDAP_ENABLED],
						'rdds43'  => (bool)$macros[$host][self::MACRO_TLD_RDDS_ENABLED],
						'rdds80'  => (bool)$macros[$host][self::MACRO_TLD_RDDS_ENABLED],
					];
					break;

				case self::MONITORING_TARGET_REGISTRAR:
					$config = [
						'tldType' => $tldTypes[$host],
						'enabled' => null,
						'rdap'    => (bool)$macros[$host][self::MACRO_TLD_RDAP_ENABLED],
						'rdds43'  => (bool)$macros[$host][self::MACRO_TLD_RDDS_ENABLED],
						'rdds80'  => (bool)$macros[$host][self::MACRO_TLD_RDDS_ENABLED],
					];
					break;

				default:
					throw new Exception('Unsupported monitoring target');
			}

			$config['enabled'] = $config['dnsUdp'] || $config['dnsTcp'];

			$result[$host] = $config;
		}

		return $result;
	}

	protected function getTldTypes(array $hostids): array
	{
		$tldTypeGroups = ['gTLD', 'ccTLD', 'otherTLD', 'testTLD'];

		$tldTypes = [];

		if (count($hostids) == 1)
		{
			$data = API::Host()->get([
				'output'       => ['hostid', 'host'],
				'hostids'      => $hostids,
				'selectGroups' => ['name'],
			]);
			$data = $data[0];

			foreach ($data['groups'] as $hostGroup)
			{
				if (in_array($hostGroup['name'], $tldTypeGroups))
				{
					$tldTypes[$data['host']] = $hostGroup['name'];
					break;
				}
			}
		}
		else
		{
			$data = API::HostGroup()->get([
				'output'      => ['groupid', 'name'],
				'filter'      => ['name' => $tldTypeGroups],
				'hostids'     => $hostids,
				'selectHosts' => ['host'],
			]);
			foreach ($data as $hostGroup)
			{
				foreach ($hostGroup['hosts'] as $hostid)
				{
					$tldTypes[$hostid['host']] = $hostGroup['name'];
				}
			}
		}

		return $tldTypes;
	}

	/**
	 * Creates config array of a macro (macro, value, description).
	 */
	protected function createMacroConfig(string $macro, string $value): array
	{
		if (!array_key_exists($macro, self::MACRO_DESCRIPTIONS))
		{
			throw new Exception("Macro '$macro' does not have description");
		}

		return [
			'description' => self::MACRO_DESCRIPTIONS[$macro],
			'macro'       => $macro,
			'value'       => $value,
		];
	}

	/**
	 * Updates macro values.
	 *
	 * @param int $hostid
	 * @param array $newValues
	 */
	protected function updateMacros(int $hostid, array $newValues): void
	{
		$data = API::UserMacro()->get([
			'output'  => ['hostmacroid', 'macro', 'value'],
			'hostids' => [$hostid],
			'filter'  => ['macro' => array_keys($newValues)],
		]);

		if (count($data) != count($newValues))
		{
			throw new Exception('Failed to find all macros');
		}

		$config = [];

		foreach ($data as $macro)
		{
			if ($macro['value'] != $newValues[$macro['macro']])
			{
				$config[] = [
					'hostmacroid' => $macro['hostmacroid'],
					'value'       => $newValues[$macro['macro']],
				];
			}
		}

		if (!empty($config))
		{
			API::UserMacro()->update($config);
		}
	}

	/**
	 * Returns hosts from specific host group in the following format:
	 * <pre>
	 * &nbsp; [
	 * &nbsp;     0 => [
	 * &nbsp;         'hostid' => ...,
	 * &nbsp;         'host' => ...,
	 * &nbsp;         ...
	 * &nbsp;     ],
	 * &nbsp;     ...,
	 * &nbsp; ]
	 * </pre>
	 * Use array_column() to extract specific column.
	 *
	 * @param int         $hostGroupId
	 * @param string|null $host
	 * @param array|null  $additionalFields
	 *
	 * @return array
	 */
	protected function getHostsByHostGroupId(int $hostGroupId, ?string $host, ?array $additionalFields): array
	{
		$outputFields = ['host'];

		if (!is_null($additionalFields))
		{
			$outputFields = array_merge($outputFields, $additionalFields);
		}

		return API::Host()->get([
			'output'   => $outputFields,
			'groupids' => [$hostGroupId],
			'filter'   => [
				'host' => is_null($host) ? [] : [$host],
			],
		]);
	}

	/**
	 * Returns hosts from specific host group in the following format:
	 * <pre>
	 * &nbsp; [
	 * &nbsp;     0 => [
	 * &nbsp;         'hostid' => ...,
	 * &nbsp;         'host' => ...,
	 * &nbsp;         ...
	 * &nbsp;     ],
	 * &nbsp;     ...,
	 * &nbsp; ]
	 * </pre>
	 * Use array_column() to extract specific column.
	 *
	 * @param string      $hostGroup
	 * @param string|null $host
	 * @param array|null  $additionalFields
	 *
	 * @return array
	 */
	protected function getHostsByHostGroup(string $hostGroup, ?string $host, ?array $additionalFields): array
	{
		return $this->getHostsByHostGroupId($this->getHostGroupId($hostGroup), $host, $additionalFields);
	}

	/**
	 * Returns hosts from specific template in the following format:
	 * <pre>
	 * &nbsp; [
	 * &nbsp;     0 => [
	 * &nbsp;         'hostid' => ...,
	 * &nbsp;         'host' => ...,
	 * &nbsp;         ...
	 * &nbsp;     ],
	 * &nbsp;     ...,
	 * &nbsp; ]
	 * </pre>
	 * Use array_column() to extract specific column.
	 *
	 * @param int         $templateId
	 * @param string|null $host
	 * @param array|null  $additionalFields
	 *
	 * @return array
	 */
	protected function getHostsByTemplateId(int $templateId, ?string $host, ?array $additionalFields): array
	{
		$outputFields = ['host'];

		if (!is_null($additionalFields))
		{
			$outputFields = array_merge($outputFields, $additionalFields);
		}

		return API::Host()->get([
			'output'      => $outputFields,
			'templateids' => [$templateId],
			'filter'      => [
				'host' => is_null($host) ? [] : [$host],
			],
		]);
	}

	/**
	 * Returns hosts from specific template in the following format:
	 * <pre>
	 * &nbsp; [
	 * &nbsp;     0 => [
	 * &nbsp;         'hostid' => ...,
	 * &nbsp;         'host' => ...,
	 * &nbsp;         ...
	 * &nbsp;     ],
	 * &nbsp;     ...,
	 * &nbsp; ]
	 * </pre>
	 * Use array_column() to extract specific column.
	 *
	 * @param string      $template
	 * @param string|null $host
	 * @param array|null  $additionalFields
	 *
	 * @return array
	 */
	protected function getHostsByTemplate(string $template, ?string $host, ?array $additionalFields): array
	{
		return $this->getHostsByTemplateId($this->getTemplateId($template), $host, $additionalFields);
	}

	/**
	 * Returns hostids in the following format:
	 * <pre>
	 * &nbsp; [
	 * &nbsp;     'host 1' => '100001',
	 * &nbsp;     'host 2' => '100002',
	 * &nbsp;     'host 3' => '100003',
	 * &nbsp;     ...,
	 * &nbsp; ]
	 * </pre>
	 *
	 * @param array $hosts
	 *
	 * @return array
	 */
	protected function getHostIds(array $hosts): array
	{
		if (empty($hosts))
		{
			return [];
		}

		$data = API::Host()->get([
			'output' => ['hostid', 'host'],
			'filter' => ['host' => $hosts],
		]);

		$hostids = array_column($data, 'hostid', 'host');

		return $hostids;
	}

	/**
	 * Returns hostid.
	 *
	 * @param string $host
	 *
	 * @return int
	 */
	protected function getHostId(string $host): int
	{
		return current($this->getHostIds([$host]));
	}

	/**
	 * Returns itemids in the following format:
	 *
	 * <pre>
	 * &nbsp; [
	 * &nbsp;     'key 1' => '100001',
	 * &nbsp;     'key 2' => '100002',
	 * &nbsp;     'key 3' => '100003',
	 * &nbsp;     ...,
	 * &nbsp; ]
	 * </pre>
	 *
	 * @param int $hostId
	 * @param array $keys
	 *
	 * @return array
	 */
	protected function getItemIds(int $hostId, array $keys): array
	{
		if (empty($keys))
		{
			return [];
		}

		$data = API::Item()->get([
			'output'  => ['itemid', 'key_'],
			'hostids' => [$hostId],
			'filter'  => ['key_' => $keys],
		]);

		return array_column($data, 'itemid', 'key_');
	}

	/**
	 * Returns itemid.
	 *
	 * @param int $hostId
	 * @param string $key
	 *
	 * @return int
	 */
	protected function getItemId(int $hostId, string $key): int
	{
		return current($this->getItemIds($hostId, [$key]));
	}

	/**
	 * Returns itemid.
	 *
	 * @param string $template
	 * @param string $key
	 *
	 * @return int
	 */
	protected function getTemplateItemId(string $template, string $key): int
	{
		$config = [
			'output'      => ['itemid'],
			'templated'   => true,
			'templateids' => [$this->templateIds[$template]],
			'search'      => ['key_' => $key],
		];
		$data = API::Item()->get($config);

		return $data[0]['itemid'];
	}

	/**
	 * Returns itemids in the following format:
	 *
	 * <pre>
	 * &nbsp; [
	 * &nbsp;     'key 1' => '100001',
	 * &nbsp;     'key 2' => '100002',
	 * &nbsp;     'key 3' => '100003',
	 * &nbsp;     ...,
	 * &nbsp; ]
	 * </pre>
	 *
	 * @param int $hostId
	 * @param array $keys
	 *
	 * @return array
	 */
	protected function findItemIds(int $hostId, array $keys): array
	{
		if (empty($keys))
		{
			return [];
		}

		$data = API::Item()->get([
			'hostids'                => [$hostId],
			'output'                 => ['itemid', 'key_'],
			'searchWildcardsEnabled' => true,
			'searchByAny'            => true,
			'search'                 => ['key_' => $keys],
		]);

		return array_column($data, 'itemid', 'key_');
	}

	/**
	 * Returns host group ids in the following format:
	 * <pre>
	 * &nbsp; [
	 * &nbsp;     'host group 1' => '100001',
	 * &nbsp;     'host group 2' => '100002',
	 * &nbsp;     'host group 3' => '100003',
	 * &nbsp;     ...,
	 * &nbsp; ]
	 * </pre>
	 *
	 * @param array $hostGroupNames
	 *
	 * @return array
	 */
	protected function getHostGroupIds(array $hostGroupNames): array
	{
		if (empty($hostGroupNames))
		{
			return [];
		}

		$data = API::HostGroup()->get([
			'output' => ['groupid', 'name'],
			'filter' => ['name' => $hostGroupNames],
		]);

		return array_column($data, 'groupid', 'name');
	}

	/**
	 * Returns host group id.
	 *
	 * @param string $hostGroup
	 *
	 * @return int
	 */
	protected function getHostGroupId(string $hostGroup): int
	{
		return current($this->getHostGroupIds([$hostGroup]));
	}

	/**
	 * Returns template ids in the following format:
	 * <pre>
	 * &nbsp; [
	 * &nbsp;     'template 1' => '100001',
	 * &nbsp;     'template 2' => '100002',
	 * &nbsp;     'template 3' => '100003',
	 * &nbsp;     ...,
	 * &nbsp; ]
	 * </pre>
	 *
	 * @param array $templateHosts
	 *
	 * @return array
	 */
	protected function getTemplateIds(array $templateHosts): array
	{
		if (empty($templateHosts))
		{
			return [];
		}

		$data = API::Template()->get([
			'output' => ['templateid', 'host'],
			'filter' => ['host' => $templateHosts],
		]);

		return array_column($data, 'templateid', 'host');
	}

	/**
	 * Returns template id.
	 *
	 * @param string $templateHost
	 *
	 * @return int
	 */
	protected function getTemplateId(string $templateHost): int
	{
		return current($this->getTemplateIds([$templateHost]));
	}

	/**
	 * Returns proxy ids in the following format:
	 * <pre>
	 * &nbsp; [
	 * &nbsp;     'proxy 1' => '100001',
	 * &nbsp;     'proxy 2' => '100002',
	 * &nbsp;     'proxy 3' => '100003',
	 * &nbsp;     ...,
	 * &nbsp; ]
	 * </pre>
	 *
	 * @param array $proxies
	 *
	 * @return array
	 */
	protected function getProxyIds(array $proxies): array
	{
		if (empty($proxies))
		{
			return [];
		}

		$data = API::Proxy()->get([
			'output' => ['proxyid', 'host'],
			'filter' => ['host' => $proxies],
		]);

		$proxyIds = array_column($data, 'proxyid', 'host');

		return $proxyIds;
	}

	/**
	 * Returns proxy id.
	 *
	 * @param string $proxy
	 *
	 * @return int
	 */
	protected function getProxyId(string $proxy): int
	{
		return current($this->getProxyIds([$proxy]));
	}

	protected function getInterfaceId(int $hostid): ?int
	{
		$data = API::HostInterface()->get([
			'output' => ['interfaceid'],
			'hostids' => [$hostid],
		]);

		if (count($data) > 1)
		{
			throw new Exception('Found more than one interface');
		}

		return count($data) === 0 ? null : $data[0]['interfaceid'];
	}

	/**
	 * Returns value map ids in the following format:
	 * <pre>
	 * &nbsp; [
	 * &nbsp;     'value map 1' => '100001',
	 * &nbsp;     'value map 2' => '100002',
	 * &nbsp;     'value map 3' => '100003',
	 * &nbsp;     ...,
	 * &nbsp; ]
	 * </pre>
	 *
	 * @param array $valueMaps
	 *
	 * @return array
	 */
	protected function getValueMapIds(array $valueMaps): array
	{
		if (empty($valueMaps))
		{
			return [];
		}

		$data = API::ValueMap()->get([
			'output' => ['valuemapid', 'name'],
			'filter' => ['name' => $valueMaps],
		]);

		$valueMapIds = array_column($data, 'valuemapid', 'name');

		return $valueMapIds;
	}

	/**
	 * Returns host macros in the following format:
	 * <pre>
	 * &nbsp; [
	 * &nbsp;     'host 1' => [
	 * &nbsp;         'macro 1' => 'value 1',
	 * &nbsp;         'macro 2' => 'value 2',
	 * &nbsp;         'macro 3' => 'value 3',
	 * &nbsp;         ...
	 * &nbsp;     ],
	 * &nbsp;     ...
	 * &nbsp; ]
	 * </pre>
	 *
	 * @param array $hosts
	 * @param array $macros
	 *
	 * @return type
	 */
	protected function getHostMacros(array $hosts, array $macros): array
	{
		$data = API::UserMacro()->get([
			'output'  => ['hostid', 'macro', 'value'],
			'hostids' => array_keys($hosts),
			'filter'  => ['macro' => $macros],
		]);

		$result = [];

		foreach ($data as $item)
		{
			$host  = $hosts[$item['hostid']];
			$macro = $item['macro'];
			$value = $item['value'];

			$result[$host][$macro] = $value;
		}

		return $result;
	}

	protected function getGlobalMacro(string $macro): string
	{
		$data = API::UserMacro()->get([
			'output'      => ['value'],
			'globalmacro' => true,
			'filter'      => ['macro' => $macro],
		]);

		return $data[0]['value'];
	}

	protected function setGlobalMacro(string $macro, string $value): void
	{
		$data = API::UserMacro()->get([
			'output'      => ['globalmacroid', 'value'],
			'globalmacro' => true,
			'filter'      => ['macro' => $macro],
		]);

		if ($data[0]['value'] != $value)
		{
			$data = API::UserMacro()->updateGlobal([
				'globalmacroid' => $data[0]['globalmacroid'],
				'value'         => $value,
			]);
		}
	}
}
