<?php

//declare(strict_types=1); // TODO: enable strict_types

namespace Modules\RsmProvisioningApi\Actions;

use API;
use Exception;
use Modules\RsmProvisioningApi\RsmException;

class Tld extends MonitoringTarget
{
	protected function checkMonitoringTarget(): bool
	{
		return $this->getMonitoringTarget() == MONITORING_TARGET_REGISTRY;
	}

	protected function getInputRules(): array
	{
		switch ($_SERVER['REQUEST_METHOD'])
		{
			case self::REQUEST_METHOD_GET:
				return [
					'type' => API_OBJECT, 'fields' => [
						'id'                     => ['type' => API_RSM_CUSTOM , 'function' => 'RsmValidateTldIdentifier', 'error' => 'The syntax of the TLD in the URL is invalid'],
					]
				];

			case self::REQUEST_METHOD_DELETE:
				return [
					'type' => API_OBJECT, 'fields' => [
						'id'                     => ['type' => API_RSM_CUSTOM , 'function' => 'RsmValidateTldIdentifier', 'error' => 'The syntax of the TLD in the URL is invalid'],
					]
				];

			case self::REQUEST_METHOD_PUT:
				return [
					'type' => API_OBJECT, 'fields' => [
						'id'                     => ['type' => API_RSM_CUSTOM , 'function' => 'RsmValidateTldIdentifier', 'error' => 'The syntax of the TLD in the URL is invalid'],
						'tld'                    => ['type' => API_RSM_CUSTOM , 'function' => 'RsmValidateInvalid', 'error' => 'The "tld" element included in a PUT request'],
						'tldType'                => ['type' => API_RSM_CUSTOM , 'function' => 'RsmValidateEnum', 'in' => ['gTLD', 'ccTLD', 'otherTLD', 'testTLD'], 'error' => 'TLD type is invalid'],
						'dnsParameters'          => ['type' => API_OBJECT     , 'fields' => [
							'nsIps'              => ['type' => API_OBJECTS    , 'uniq' => [['ns', 'ip']], 'fields' => [
								'ns'             => ['type' => API_RSM_CUSTOM , 'function' => 'RsmValidateHostname', 'error' => 'Invalid domain name provided in "tld", "ns", "rdds43Server", "rdds43TestedDomain", "rdapTestedDomain" or "nsTestPrefix" element'],
								'ip'             => ['type' => API_RSM_CUSTOM , 'function' => 'RsmValidateIP', 'error' => 'Invalid IP provided in the "ip" element'],
							]],
							'dnssecEnabled'      => ['type' => API_BOOLEAN    ],
							'nsTestPrefix'       => ['type' => API_RSM_CUSTOM , 'function' => 'RsmValidateDomainName', 'error' => 'Invalid domain name provided in "tld", "ns", "rdds43Server", "rdds43TestedDomain", "rdapTestedDomain" or "nsTestPrefix" element'],
							'minNs'              => ['type' => API_RSM_CUSTOM , 'function' => 'RsmValidateInt', 'min' => 1, 'error' => 'The "minNS" element must be a positive integer'],
						]],
						'servicesStatus'         => ['type' => API_OBJECTS    , 'uniq' => [['service']], 'fields' => [
							'service'            => ['type' => API_RSM_CUSTOM , 'function' => 'RsmValidateEnum', 'in' => ['dnsUDP', 'dnsTCP', 'rdap', 'rdds43', 'rdds80'], 'error' => 'Service is not supported'],
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

	protected function rsmValidateInput(): void
	{
		parent::rsmValidateInput();

		if ($_SERVER['REQUEST_METHOD'] == self::REQUEST_METHOD_PUT)
		{
			$this->validateInputServices('All services (i.e. rdds43, rdds80, rdap, dnsTCP and dnsUDP) must be specified');

			$services = array_column($this->input['servicesStatus'], 'enabled', 'service');

			if (!$this->isStandaloneRdap() && $services['rdds43'] != $services['rdds80'])
			{
				throw new RsmException(400, 'An enabled status for rdds43 require that rdds80 is enabled and vice versa');
			}

			$this->requireArrayKeys(['tldType'], $this->input, 'JSON does not comply with definition');

			if ($services['dnsUDP'] || $services['dnsTCP'])
			{
				$this->requireArrayKeys(['dnsParameters'], $this->input, 'dnsParameters object is missing and the DNS service is enabled');
				$this->requireArrayKeys(['nsIps', 'dnssecEnabled', 'nsTestPrefix', 'minNs'], $this->input['dnsParameters'], 'JSON does not comply with definition');
				if (empty($this->input['dnsParameters']['nsIps']))
				{
					throw new RsmException(400, 'At least one NS, IP pair is required');
				}
			}
			else
			{
				$this->forbidArrayKeys(['dnsParameters'], $this->input, 'An element within the dnsParameters object or the dnsParameters object is included but the status of the service is disabled');

				if ($services['rdap'] || $services['rdds43'] || $services['rdds80'])
				{
					throw new RsmException(400, 'DNS service can only be disabled if all other services are disabled');
				}
			}

			if ($services['rdap'] || $services['rdds43'] || $services['rdds80'])
			{
				$this->requireArrayKeys(['rddsParameters'], $this->input, 'rddsParameters object is missing and at least one RDDS service (i.e. rdds43, rdds80 or rdap) is enabled');

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

	protected function getObjects(?string $objectId): array
	{
		// get hosts

		$data = $this->getHostsByHostGroup('TLDs', $objectId, null);

		if (empty($data))
		{
			if (!is_null($objectId))
			{
				throw new RsmException(404, 'The TLD does not exist in Zabbix');
			}

			return [];
		}

		$hosts = array_column($data, 'host', 'hostid');

		// get TLD types

		$tldTypes = $this->getTldTypes(array_keys($hosts));

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
				'tld'                    => $host,
				'tldType'                => $tldTypes[$host],
				'dnsParameters'          => [
					'nsIps'              => $this->nsipStrToList($macros[$host][self::MACRO_TLD_DNS_NAME_SERVERS]),
					'dnssecEnabled'      => (bool)$macros[$host][self::MACRO_TLD_DNSSEC_ENABLED],
					'nsTestPrefix'       => $macros[$host][self::MACRO_TLD_DNS_TESTPREFIX],
					'minNs'              => (int)$macros[$host][self::MACRO_TLD_DNS_AVAIL_MINNS],
				],
				'servicesStatus'         => [
					[
						'service'        => 'dnsUDP',
						'enabled'        => (bool)$macros[$host][self::MACRO_TLD_DNS_UDP_ENABLED],
					],
					[
						'service'        => 'dnsTCP',
						'enabled'        => (bool)$macros[$host][self::MACRO_TLD_DNS_TCP_ENABLED],
					],
					[
						'service'        => 'rdap',
						'enabled'        => (bool)$macros[$host][self::MACRO_TLD_RDAP_ENABLED],
					],
					[
						'service'        => 'rdds43',
						'enabled'        => (bool)$macros[$host][self::MACRO_TLD_RDDS_ENABLED],
					],
					[
						'service'        => 'rdds80',
						'enabled'        => (bool)$macros[$host][self::MACRO_TLD_RDDS_ENABLED],
					],
				],
				'rddsParameters'         => [
					'rdds43Server'       => $macros[$host][self::MACRO_TLD_RDDS_ENABLED] ? $macros[$host][self::MACRO_TLD_RDDS43_SERVER]      : null,
					'rdds43TestedDomain' => $macros[$host][self::MACRO_TLD_RDDS_ENABLED] ? $macros[$host][self::MACRO_TLD_RDDS43_TEST_DOMAIN] : null,
					'rdds80Url'          => $macros[$host][self::MACRO_TLD_RDDS_ENABLED] ? $macros[$host][self::MACRO_TLD_RDDS80_URL]         : null,
					'rdapUrl'            => $macros[$host][self::MACRO_TLD_RDAP_ENABLED] ? $macros[$host][self::MACRO_TLD_RDAP_BASE_URL]      : null,
					'rdapTestedDomain'   => $macros[$host][self::MACRO_TLD_RDAP_ENABLED] ? $macros[$host][self::MACRO_TLD_RDAP_TEST_DOMAIN]   : null,
					'rdds43NsString'     => $macros[$host][self::MACRO_TLD_RDDS_ENABLED] ? $macros[$host][self::MACRO_TLD_RDDS43_NS_STRING]   : null,
				],
			];
		}

		return $result;
	}

	private function getTldTypes(array $hostids): array
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

	protected function createObject(): void
	{
		parent::createObject();
		$this->updateDnsNsItems();
	}

	protected function updateObject(): void
	{
		$services = array_column($this->newObject['servicesStatus'], 'enabled', 'service');

		if ($services['dnsUDP'] || $services['dnsTCP'])
		{
			$newMinNs = $this->newObject['dnsParameters']['minNs'];
			$oldMinNs = $this->oldObject['dnsParameters']['minNs'];

			if (!preg_match('/^(\d+)(?:;(\d+):(\d+))?$/', $oldMinNs, $matches))
			{
				throw new Exception("Unexpected 'minNs' value in \$this->oldObject: '$oldMinNs'");
			}

			if (!isset($matches[2]) || (int)(time() / 60) * 60 < $matches[2])
			{
				$oldMinNs = $matches[1];
			}
			else
			{
				$oldMinNs = $matches[3];
			}

			if ($newMinNs != $oldMinNs)
			{
				throw new RsmException(400, 'The minNS value is not the same as in the system');
			}
		}

		parent::updateObject();
		$this->updateDnsNsItems();
	}

	protected function createStatusHost(): int
	{
		$config = [
			'host'       => $this->newObject['id'],
			'status'     => HOST_STATUS_MONITORED,
			'interfaces' => [self::DEFAULT_MAIN_INTERFACE],
			'groups'     => [
				['groupid' => $this->hostGroupIds['TLDs']],
				['groupid' => $this->hostGroupIds[$this->newObject['tldType']]],
			],
			'templates'  => [
				['templateid' => $this->templateIds['Template Rsmhost Config ' . $this->newObject['id']]],
				['templateid' => $this->templateIds['Template Config History']],
				['templateid' => $this->templateIds['Template DNS Status']],
				['templateid' => $this->templateIds['Template DNSSEC Status']],
				['templateid' => $this->templateIds['Template RDAP Status']],
				['templateid' => $this->templateIds['Template RDDS Status']],
			],
		];
		$data = API::Host()->create($config);

		return $data['hostids'][0];
	}

	protected function createTemplates(): void
	{
		parent::createTemplates();

		$config = [
			'host'      => 'Template DNS Test - ' . $this->newObject['id'],
			'groups'    => [
				['groupid' => $this->hostGroupIds['Templates - TLD']],
			],
			'templates' => [$this->templateIds['Template DNS Test']],
		];
		$data = API::Template()->create($config);

		$this->templateIds['Template DNS Test - ' . $this->newObject['id']] = $data['templateids'][0];
	}

	protected function getTestTemplatesConfig(string $probe, string $rsmhost): array
	{
		$templates = parent::getTestTemplatesConfig($probe, $rsmhost);

		$templates[] = ['templateid' => $this->templateIds['Template DNS Test - ' . $rsmhost]];

		return $templates;
	}

	protected function updateStatustHost(): int
	{
		$config = [
			'hostid' => $this->getHostId($this->newObject['id']),
			'groups' => [
				['groupid' => $this->hostGroupIds['TLDs']],
				['groupid' => $this->hostGroupIds[$this->newObject['tldType']]],
			],
		];
		$data = API::Host()->update($config);

		return $data['hostids'][0];
	}

	protected function deleteObject(): void
	{
		parent::deleteObject();

		$templateId = $this->getTemplateId('Template DNS Test - ' . $this->getInput('id'));
		$data = API::Template()->delete([$templateId]);
	}

	protected function getHostGroupNames(?array $additionalNames): array
	{
		$names = [
			// groups for "<rsmhost>" host
			'Templates - TLD',
			'TLDs',
			$this->newObject['tldType'],
			// groups for "<rsmhost> <probe>" hosts
			$this->newObject['tldType'] . ' Probe results',
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
			'Template DNS Status',
			'Template DNSSEC Status',
			'Template RDAP Status',
			'Template RDDS Status',
			// templates for "<rsmhost> <probe>" hosts
			'Template DNS Test',
			'Template DNS Test - ' . $this->newObject['id'],
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
				'tldType' => $this->newObject['tldType'],
				'dnsUdp'  => $services['dnsUDP'],
				'dnsTcp'  => $services['dnsTCP'],
				'dnssec'  => $this->newObject['dnsParameters']['dnssecEnabled'],
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

			$this->createMacroConfig(self::MACRO_TLD_DNS_UDP_ENABLED   , (int)$services['dnsUDP']),
			$this->createMacroConfig(self::MACRO_TLD_DNS_TCP_ENABLED   , (int)$services['dnsTCP']),
			$this->createMacroConfig(self::MACRO_TLD_DNSSEC_ENABLED    , (int)$this->newObject['dnsParameters']['dnssecEnabled']),
			$this->createMacroConfig(self::MACRO_TLD_RDAP_ENABLED      , (int)$services['rdap']),
			$this->createMacroConfig(self::MACRO_TLD_RDDS_ENABLED      , (int)$services['rdds43']),
			//$this->createMacroConfig(self::MACRO_TLD_RDDS_ENABLED      , (int)$services['rdds80']),

			$this->createMacroConfig(self::MACRO_TLD_DNS_NAME_SERVERS  , $this->nsipListToStr($this->newObject['dnsParameters']['nsIps'])),
			$this->createMacroConfig(self::MACRO_TLD_DNS_AVAIL_MINNS   , $this->newObject['dnsParameters']['minNs']),    // TODO: schedule minns change
			$this->createMacroConfig(self::MACRO_TLD_DNS_TESTPREFIX    , $this->newObject['dnsParameters']['nsTestPrefix']),

			$this->createMacroConfig(self::MACRO_TLD_RDAP_BASE_URL     , $this->newObject['rddsParameters']['rdapUrl']),
			$this->createMacroConfig(self::MACRO_TLD_RDAP_TEST_DOMAIN  , $this->newObject['rddsParameters']['rdapTestedDomain']),

			$this->createMacroConfig(self::MACRO_TLD_RDDS43_TEST_DOMAIN, $this->newObject['rddsParameters']['rdds43TestedDomain']),
			$this->createMacroConfig(self::MACRO_TLD_RDDS43_NS_STRING  , $this->newObject['rddsParameters']['rdds43NsString']),
			$this->createMacroConfig(self::MACRO_TLD_RDDS43_SERVER     , $this->newObject['rddsParameters']['rdds43Server']),
			$this->createMacroConfig(self::MACRO_TLD_RDDS80_URL        , $this->newObject['rddsParameters']['rdds80Url']),
		];
	}

	private function updateDnsNsItems(): void
	{
		$oldNsIpList = is_null($this->oldObject) ? [] : $this->oldObject['dnsParameters']['nsIps'];
		$newNsIpList = is_null($this->newObject) ? [] : $this->newObject['dnsParameters']['nsIps'];
		$oldNsList   = array_unique(array_column($oldNsIpList, 'ns'));
		$newNsList   = array_unique(array_column($newNsIpList, 'ns'));

		$createNsIp  = array_udiff($newNsIpList, $oldNsIpList, [$this, 'compareNsIp']);
		$disableNsIp = array_udiff($oldNsIpList, $newNsIpList, [$this, 'compareNsIp']);
		$createNs    = array_diff($newNsList, $oldNsList);
		$disableNs   = array_diff($oldNsList, $newNsList);

		$createNsIp  = $this->sortNsIpPairs($createNsIp);
		$disableNsIp = $this->sortNsIpPairs($disableNsIp);
		sort($createNs);
		sort($disableNs);

		$testTemplateId = $this->templateIds['Template DNS Test - ' . $this->newObject['id']];

		if (!empty($createNsIp))
		{
			// get value maps

			$valueMapIds = $this->getValueMapIds([
				'RSM Service Availability',
				'RSM DNS rtt',
			]);

			// get itemid of "DNS Test" master item

			$dnsTestItemId = $this->getTemplateItemId('Template DNS Test - ' . $this->newObject['id'], 'rsm.dns[');

			// create item pseudo-configs

			$statusItems = [];
			$testItems = [];

			foreach ($createNsIp as $nsip)
			{
				$ns = $nsip['ns'];
				$ip = $nsip['ip'];

				$statusItems += [
					"rsm.slv.dns.ns.avail[$ns,$ip]" => [
						'name'       => "DNS NS \$1 (\$2) availability",
						'valuemapid' => $valueMapIds['RSM Service Availability'],
					],
					"rsm.slv.dns.ns.downtime[$ns,$ip]" => [
						'name'       => "DNS minutes of \$1 (\$2) downtime",
						'valuemapid' => null,
					],
				];

				$testItems += [
					"rsm.dns.nsid[$ns,$ip]" => [
						'name'                 => "DNS NSID of $ns,$ip",
						'value_type'           => ITEM_VALUE_TYPE_STR,
						'valuemapid'           => null,
						'description'          => 'DNS Name Server Identifier of the target Name Server that was tested.',
						'preprocessing_params' => "\$.nsips[?(@.['ns'] == '$ns' && @.['ip'] == '$ip')].nsid.first()",
					],
					"rsm.dns.rtt[$ns,$ip,tcp]" => [
						'name'                 => "DNS NS RTT of $ns,$ip using tcp",
						'value_type'           => ITEM_VALUE_TYPE_FLOAT,
						'valuemapid'           => $valueMapIds['RSM DNS rtt'],
						'description'          => 'The Round-Time Trip returned when testing specific IP of Name Server using TCP protocol.',
						'preprocessing_params' => "\$.nsips[?(@.['ns'] == '$ns' && @.['ip'] == '$ip' && @.['protocol'] == 'tcp')].rtt.first()",
					],
					"rsm.dns.rtt[$ns,$ip,udp]" => [
						'name'                 => "DNS NS RTT of $ns,$ip using udp",
						'value_type'           => ITEM_VALUE_TYPE_FLOAT,
						'valuemapid'           => $valueMapIds['RSM DNS rtt'],
						'description'          => 'The Round-Time Trip returned when testing specific IP of Name Server using UDP protocol.',
						'preprocessing_params' => "\$.nsips[?(@.['ns'] == '$ns' && @.['ip'] == '$ip' && @.['protocol'] == 'udp')].rtt.first()",
					],
				];
			}

			foreach ($createNs as $ns)
			{
				$testItems += [
					"rsm.dns.ns.status[$ns]" => [
						'name'                 => "DNS Test: DNS NS status of $ns",
						'value_type'           => ITEM_VALUE_TYPE_FLOAT,
						'valuemapid'           => $valueMapIds['RSM DNS rtt'],
						'description'          => 'Status of Name Server: 0 (Down), 1 (Up). The Name Server is considered to be up if all its IPs returned successful RTTs.',
						'preprocessing_params' => "\$.nss[?(@.['ns'] == '$ns')].status.first()",
					],
				];
			}

			// check which items already exist; enable them, remove from pseudo-configs

			$enableItemIds = [];

			$data = API::Item()->get([
				'output'  => ['itemid', 'key_'],
				'hostids' => $this->statusHostId,
				'filter'  => ['key_' => array_keys($statusItems)],
			]);
			if (!empty($data))
			{
				$statusItems = array_diff_key($statusItems, array_column($data, 'itemid', 'key_'));
				$enableItemIds = array_merge($enableItemIds, array_column($data, 'itemid'));
			}

			$data = API::Item()->get([
				'output'  => ['itemid', 'key_'],
				'hostids' => $testTemplateId,
				'filter'  => ['key_' => array_keys($testItems)],
			]);
			if (!empty($data))
			{
				$testItems = array_diff_key($testItems, array_column($data, 'itemid', 'key_'));
				$enableItemIds = array_merge($enableItemIds, array_column($data, 'itemid'));
			}

			if (!empty($enableItemIds))
			{
				$config = array_map(fn($itemid) => ['itemid' => $itemid, 'status' => ITEM_STATUS_ACTIVE], $enableItemIds);
				$data = API::Item()->update($config);
			}

			// build configs for items that need to be created

			$itemConfigs = [];

			foreach ($statusItems as $key => $item)
			{
				$itemConfigs[] = [
					'name'       => $item['name'],
					'key_'       => $key,
					'status'     => ITEM_STATUS_ACTIVE,
					'hostid'     => $this->statusHostId,
					'type'       => ITEM_TYPE_TRAPPER,
					'value_type' => ITEM_VALUE_TYPE_UINT64,
					'valuemapid' => $item['valuemapid'],
				];
			}

			foreach ($testItems as $key => $item)
			{
				$itemConfigs[] = [
					'name'          => $item['name'],
					'key_'          => $key,
					'status'        => ITEM_STATUS_ACTIVE,
					'hostid'        => $testTemplateId,
					'type'          => ITEM_TYPE_DEPENDENT,
					'master_itemid' => $dnsTestItemId,
					'value_type'    => $item['value_type'],
					'valuemapid'    => $item['valuemapid'],
					'description'   => $item['description'],
					'preprocessing' => [[
						'type'                 => ZBX_PREPROC_JSONPATH,
						'params'               => $item['preprocessing_params'],
						'error_handler'        => ZBX_PREPROC_FAIL_DISCARD_VALUE,
						'error_handler_params' => '',
					]],
				];
			}

			// create items

			$data = API::Item()->create($itemConfigs);

			// create triggers

			$thresholds = [
				['threshold' =>  '10', 'priority' => 2],
				['threshold' =>  '25', 'priority' => 3],
				['threshold' =>  '50', 'priority' => 3],
				['threshold' =>  '75', 'priority' => 4],
				['threshold' => '100', 'priority' => 5],
			];

			$triggerConfigs = [];

			foreach ($createNsIp as $nsip)
			{
				$ns  = $nsip['ns'];
				$ip  = $nsip['ip'];
				$key = "rsm.slv.dns.ns.downtime[$ns,$ip]";

				foreach ($thresholds as $thresholdRow)
				{
					$threshold = $thresholdRow['threshold'];
					$priority  = $thresholdRow['priority'];

					$thresholdStr = $threshold < 100 ? '*' . ($threshold * 0.01) : '';

					$triggerConfigs[] = [
						'description' => "DNS $ns ($ip) downtime exceeded $threshold% of allowed \$1 minutes",
						'expression'  => sprintf('{%s:%s.last()}>{$RSM.SLV.NS.DOWNTIME}%s', $this->newObject['id'], $key, $thresholdStr),
						'priority'    => $priority,
					];
				}
			}

			if (!empty($triggerConfigs))
			{
				$data = API::Trigger()->create($triggerConfigs);

				$triggerDependencyConfigs = [];

				foreach ($data['triggerids'] as $i => $triggerId)
				{
					if ($i % count($thresholds) === 0)
					{
						continue;
					}

					$triggerDependencyConfigs[] = [
						'triggerid'          => $data['triggerids'][$i - 1],
						'dependsOnTriggerid' => $triggerId,
					];
				}

				$data = API::Trigger()->addDependencies($triggerDependencyConfigs);
			}
		}

		if (!empty($disableNsIp))
		{
			$statusItems = [];
			$testItems   = [];

			foreach ($disableNsIp as $nsip)
			{
				$ns = $nsip['ns'];
				$ip = $nsip['ip'];

				$statusItems[] = "rsm.slv.dns.ns.avail[$ns,$ip]";
				$statusItems[] = "rsm.slv.dns.ns.downtime[$ns,$ip]";

				$testItems[] = "rsm.dns.nsid[$ns,$ip]";
				$testItems[] = "rsm.dns.rtt[$ns,$ip,tcp]";
				$testItems[] = "rsm.dns.rtt[$ns,$ip,udp]";

			}
			foreach ($disableNs as $ns)
			{
				$testItems[] = "rsm.dns.ns.status[$ns]";
			}

			$disableItemIds = [];
			$disableItemIds += $this->getItemIds($this->statusHostId, $statusItems);
			$disableItemIds += $this->getItemIds($testTemplateId, $testItems);

			$config = array_map(fn($itemid) => ['itemid' => $itemid, 'status' => ITEM_STATUS_DISABLED], array_values($disableItemIds));
			$data = API::Item()->update($config);
		}
	}

	private function getTemplateItemId(string $template, string $key): int
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

	private function nsipStrToList(string $str): array
	{
		$list = [];

		foreach (explode(' ', $str) as $nsip)
		{
			list($ns, $ip) = explode(',', $nsip);

			$list[] = [
				'ns' => $ns,
				'ip' => $ip,
			];
		}

		return $this->sortNsIpPairs($list);
	}

	private function nsipListToStr(array $list): string
	{
		$list = $this->sortNsIpPairs($list);

		foreach ($list as &$nsip)
		{
			$nsip = $nsip['ns'] . ',' . $nsip['ip'];
		}
		unset($nsip);

		return implode(' ', $list);
	}

	private function sortNsIpPairs(array $nsipList): array
	{
		// make [ns => [ip1, ip2, ...], ...] array

		$nsipListGrouped = [];

		foreach ($nsipList as $nsip)
		{
			$nsipListGrouped[$nsip['ns']][] = $nsip['ip'];
		}

		// sort ip addresses

		foreach ($nsipListGrouped as &$ips)
		{
			usort($ips, [$this, 'compareIp']);
		}
		unset($ips);

		// sort nameservers

		ksort($nsipListGrouped, SORT_STRING);

		// format output

		$result = [];

		foreach ($nsipListGrouped as $ns => $ips)
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

	private function compareIp(string $a, string $b): int
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
	}

	private function compareNsIp(array $a, array $b): int
	{
		if ($a['ns'] != $b['ns'])
		{
			return strcmp($a['ns'], $b['ns']);
		}
		else
		{
			return $this->compareIp($a['ip'], $b['ip']);
		}
	}
}
