<?php

namespace Modules\RsmProvisioningApi\Actions;

use API;
use Exception;

class Tld extends MonitoringTarget
{
	protected function checkMonitoringTarget()
	{
		return $this->getMonitoringTarget() == MONITORING_TARGET_REGISTRY;
	}

	protected function getObjectIdInputField()
	{
		return 'tld';
	}

	protected function getInputRules(): array
	{
		switch ($_SERVER['REQUEST_METHOD'])
		{
			case self::REQUEST_METHOD_GET:
				return [
					'type' => API_OBJECT, 'fields' => [
						'tld'                           => ['type' => API_RSM_CUSTOM , 'function' => 'RsmValidateTldIdentifier'],
					]
				];

			case self::REQUEST_METHOD_DELETE:
				return [
					'type' => API_OBJECT, 'fields' => [
						'tld'                           => ['type' => API_RSM_CUSTOM , 'flags' => API_REQUIRED, 'function' => 'RsmValidateTldIdentifier'],
					]
				];

			case self::REQUEST_METHOD_PUT:
				return [
					'type' => API_OBJECT, 'fields' => [
						'tld'                           => ['type' => API_RSM_CUSTOM , 'flags' => API_REQUIRED, 'function' => 'RsmValidateTldIdentifier'],
						'tldType'                       => ['type' => API_STRING_UTF8, 'flags' => API_REQUIRED, 'in' => 'gTLD,ccTLD,otherTLD,testTLD'],
						'dnsParameters'                 => ['type' => API_OBJECT     , 'flags' => API_REQUIRED, 'fields' => [
							'nsIps'                     => ['type' => API_OBJECTS    , 'flags' => API_REQUIRED, 'uniq' => [['ns', 'ip']], 'fields' => [ // TODO: at least one NS, IP pair is required
								'ns'                    => ['type' => API_STRING_UTF8, 'flags' => API_REQUIRED],
								'ip'                    => ['type' => API_RSM_CUSTOM , 'flags' => API_REQUIRED, 'function' => 'RsmValidateIP'],
							]],
							'dnssecEnabled'             => ['type' => API_BOOLEAN    , 'flags' => API_REQUIRED],
							'nsTestPrefix'              => ['type' => API_STRING_UTF8, 'flags' => API_REQUIRED],
							'minNameServersUP'          => ['type' => API_UINT64     , 'flags' => API_REQUIRED],
						]],
						'servicesStatus'                => ['type' => API_OBJECTS    , 'flags' => API_REQUIRED, 'uniq' => [['service']], 'fields' => [  // TODO: all services (i.e. rdds, rdap, dnsTCP and dnsUDP) must be specified
							'service'                   => ['type' => API_STRING_UTF8, 'flags' => API_REQUIRED, 'in' => 'dnsUDP,dnsTCP,rdap,rdds'],
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

	protected function getObjects(?string $objectId)
	{
		// TODO: add sanity checks
		// TODO: include disabled objects (with all services disabled)

		$data = $this->getHostsByHostGroup('TLDs', $objectId, null);
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
				self::MACRO_TLD_DNS_NAME_SERVERS,
				self::MACRO_TLD_DNS_AVAIL_MINNS,
				self::MACRO_TLD_DNS_TESTPREFIX,
				self::MACRO_TLD_RDAP_BASE_URL,
				self::MACRO_TLD_RDAP_TEST_DOMAIN,
				self::MACRO_TLD_RDDS43_TEST_DOMAIN,
				//self::MACRO_TLD_RDDS_43_SERVERS,
				//self::MACRO_TLD_RDDS_80_SERVERS,
				self::MACRO_TLD_RDDS_NS_STRING,
			]
		);

		// join data in a common data structure
		$result = [];

		foreach ($hosts as $host)
		{
			$result[] = [
				'tld'                           => $host,
				'tldType'                       => 'TODO',                                                              // TODO: fill with real value; test what performs better - API::Host()->get() or API::HostGroup()->Get()
				'dnsParameters'                 => [
					'nsIps'                     => $this->parseNsIpPairs($macros[$host][self::MACRO_TLD_DNS_NAME_SERVERS]),
					'dnssecEnabled'             => (bool)$macros[$host][self::MACRO_TLD_DNSSEC_ENABLED],
					'nsTestPrefix'              => $macros[$host][self::MACRO_TLD_DNS_TESTPREFIX],
					'minNameServersUP'          => (int)$macros[$host][self::MACRO_TLD_DNS_AVAIL_MINNS],
				],
				'servicesStatus'                => [
					[
						'service'               => 'dnsUDP',
						'enabled'               => (bool)$macros[$host][self::MACRO_TLD_DNS_UDP_ENABLED],
					],
					[
						'service'               => 'dnsTCP',
						'enabled'               => (bool)$macros[$host][self::MACRO_TLD_DNS_TCP_ENABLED],
					],
					[
						'service'               => 'rdap',
						'enabled'               => (bool)$macros[$host][self::MACRO_TLD_RDAP_ENABLED],
					],
					[
						'service'               => 'rdds',
						'enabled'               => (bool)$macros[$host][self::MACRO_TLD_RDDS_ENABLED],
					],
				],
				'rddsParameters'                => [
					'rdds43Server'              => 'TODO', // $macros[$host][self::MACRO_TLD_RDDS_ENABLED] ? $macros[$host][self::MACRO_TLD_RDDS_43_SERVERS]    : null,
					'rdds43TestedDomain'        => $macros[$host][self::MACRO_TLD_RDDS_ENABLED] ? $macros[$host][self::MACRO_TLD_RDDS43_TEST_DOMAIN] : null,
					'rdds80Url'                 => 'TODO', // $macros[$host][self::MACRO_TLD_RDDS_ENABLED] ? $macros[$host][self::MACRO_TLD_RDDS_80_SERVERS]    : null,
					'rdapUrl'                   => $macros[$host][self::MACRO_TLD_RDAP_ENABLED] ? $macros[$host][self::MACRO_TLD_RDAP_BASE_URL]      : null,
					'rdapTestedDomain'          => $macros[$host][self::MACRO_TLD_RDAP_ENABLED] ? $macros[$host][self::MACRO_TLD_RDAP_TEST_DOMAIN]   : null,
					'rdds43NsString'            => $macros[$host][self::MACRO_TLD_RDDS_ENABLED] ? $macros[$host][self::MACRO_TLD_RDDS_NS_STRING]     : null,
				],
				'zabbixMonitoringCentralServer' => 'TODO',                                                              // TODO: fill with real value
			];
		}

		return $result;
	}

	protected function createObject()
	{
		$input = $this->getInputAll();

		$hostGroupIds = $this->getHostGroupIds($this->getHostGroupNames($input['tldType'], null));
		$templateIds = $this->getTemplateIds($this->getTemplateNames(null));

		$data = API::HostGroup()->create($this->createHostGroupConfig($input));
		$hostGroupIds['TLD ' . $input['tld']] = $data['groupids'][0];

		$data = API::Template()->create($this->createTemplateConfig($input, $hostGroupIds));
		$templateIds['Template Rsmhost Config ' . $input['tld']] = $data['templateids'][0];

		$data = API::Host()->create($this->createTldHostConfig($input, $hostGroupIds, $templateIds));

		$this->createRsmhostProbeHosts($input['tld'], null, $hostGroupIds, $templateIds);

		// TODO: enable/disable items, based on service status and standalone rdap status
	}

	protected function updateObject()
	{
		$input = $this->getInputAll();

		$hostGroupIds = $this->getHostGroupIds($this->getHostGroupNames($input['tldType'], ['TLD ' . $input['tld']]));
		$templateIds = $this->getTemplateIds($this->getTemplateNames(['Template Rsmhost Config ' . $input['tld']]));

		$config = $this->createTemplateConfig($input, $hostGroupIds);
		$config['templateid'] = $templateIds[$config['host']];
		$data = API::Template()->update($config);

		$config = $this->createTldHostConfig($input, $hostGroupIds, $templateIds);
		$config['hostid'] = $this->getHostId($config['host']);
		$data = API::Host()->update($config);

		$this->updateRsmhostProbeHosts($input['tld'], null, $hostGroupIds, $templateIds);

		// TODO: enable/disable items, based on service status and standalone rdap status
	}

	protected function deleteObject()
	{
	}

	function parseNsIpPairs($str)
	{
		// IPv4/IPv6 comparison function for sorting

		$cmp_function = function($a, $b)
		{
			$a = inet_pton($a);
			$b = inet_pton($b);

			if (strlen($a) != strlen($b))
			{
				// put IPv4 before IPv6
				return strlen($a) - strlen($b);
			}
			else
			{
				return strcmp($a, $b);
			}
		};

		// parsing, sorting nsip list

		$nsipList = [];

		foreach (explode(' ', $str) as $nsip)
		{
			list($ns, $ip) = explode(',', $nsip);

			$nsipList[$ns][] = $ip;
		}

		foreach ($nsipList as &$ips)
		{
			usort($ips, $cmp_function);
		}
		unset($ips);

		ksort($nsipList, SORT_STRING);

		// formatting output

		$result = [];

		foreach ($nsipList as $ns => $ips)
		{
			foreach ($ips as $ip)
			{
				$result[] = [
					'ns' => $ns,
					'ip' => $ip,
				];
			}
		}

		return $result;
	}

	private function getHostGroupNames(string $tldType, ?array $additionalNames)
	{
		$names = [
			// groups for "rsmhost" host
			'Templates - TLD',
			'TLDs',
			$tldType,
			// groups for "rsmhost probe" hosts
			$tldType . ' Probe results',
			'TLD Probe results',
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
			// templates for "rsmhost" host
			'Template Config History',
			'Template DNS Status',
			'Template DNSSEC Status',
			'Template RDAP Status',
			'Template RDDS Status',
			// templates for "rsmhost probe" hosts
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

	private function createHostGroupConfig(array $input)
	{
		return [
			'name' => 'TLD ' . $input['tld'],
		];
	}

	private function createTemplateConfig(array $input, array $hostGroupIds)
	{
		$services = array_column($input['servicesStatus'], 'enabled', 'service');

		return [
			'host'   => 'Template Rsmhost Config ' . $input['tld'],
			'groups' => [
				['groupid' => $hostGroupIds['Templates - TLD']],
			],
			'macros' => [
				$this->createMacroConfig(self::MACRO_TLD                   , $input['tld']),
				$this->createMacroConfig(self::MACRO_TLD_CONFIG_TIMES      , $_SERVER['REQUEST_TIME']),

				$this->createMacroConfig(self::MACRO_TLD_DNS_UDP_ENABLED   , (int)$services['dnsUDP']),
				$this->createMacroConfig(self::MACRO_TLD_DNS_TCP_ENABLED   , (int)$services['dnsTCP']),
				$this->createMacroConfig(self::MACRO_TLD_DNSSEC_ENABLED    , (int)$input['dnsParameters']['dnssecEnabled']),
				$this->createMacroConfig(self::MACRO_TLD_RDAP_ENABLED      , (int)$services['rdap']),
				$this->createMacroConfig(self::MACRO_TLD_RDDS_ENABLED      , (int)$services['rdds']),

				$this->createMacroConfig(self::MACRO_TLD_DNS_TESTPREFIX    , $input['dnsParameters']['nsTestPrefix']),
				$this->createMacroConfig(self::MACRO_TLD_DNS_AVAIL_MINNS   , $input['dnsParameters']['minNameServersUP']),    // TODO: schedule minns change

				$this->createMacroConfig(self::MACRO_TLD_RDAP_BASE_URL     , $input['rddsParameters']['rdapUrl']),
				$this->createMacroConfig(self::MACRO_TLD_RDAP_TEST_DOMAIN  , $input['rddsParameters']['rdapTestedDomain']),

				$this->createMacroConfig(self::MACRO_TLD_RDDS43_TEST_DOMAIN, $input['rddsParameters']['rdds43TestedDomain']),
				$this->createMacroConfig(self::MACRO_TLD_RDDS_NS_STRING    , $input['rddsParameters']['rdds43NsString']),
				//$this->createMacroConfig(self::MACRO_TLD_RDDS_43_SERVERS   , $input['rddsParameters']['rdds43Server']),     // TODO: fill with real value
				//$this->createMacroConfig(self::MACRO_TLD_RDDS_80_SERVERS   , $input['rddsParameters']['rdds80Url']),        // TODO: fill with real value
			],
		];
	}

	private function createTldHostConfig(array $input, array $hostGroupIds, array $templateIds)
	{
		return [
			'host'         => $input['tld'],
			'status'       => HOST_STATUS_MONITORED,
			'interfaces'   => [
				self::DEFAULT_MAIN_INTERFACE,
			],
			'groups'       => [
				['groupid' => $hostGroupIds['TLDs']],
				['groupid' => $hostGroupIds[$input['tldType']]],
			],
			'templates'    => [
				['templateid' => $templateIds['Template Rsmhost Config ' . $input['tld']]],
				['templateid' => $templateIds['Template Config History']],
				['templateid' => $templateIds['Template DNS Status']],
				['templateid' => $templateIds['Template DNSSEC Status']],
				['templateid' => $templateIds['Template RDAP Status']],
				['templateid' => $templateIds['Template RDDS Status']],
			],
		];
	}
}
