<?php

namespace Modules\RsmProvisioningApi\Actions;

use API;
use Exception;

class Tld extends MonitoringTarget {

	protected function checkMonitoringTarget() {
		return $this->getMonitoringTarget() == MONITORING_TARGET_REGISTRY;
	}

	protected function getObjectIdInputField() {
		return 'tld';
	}

	protected function getInputRules(): array {
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
					]
				];

			default:
				throw new Exception('Unsupported request method');
		}
	}

	protected function getObjects(?string $objectId) {
		// get hosts

		$data = $this->getHostsByHostGroup('TLDs', $objectId, null);

		if (empty($data))
		{
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
				'tldType'                       => $tldTypes[$host],
				'dnsParameters'                 => [
					'nsIps'                     => $this->nsipStrToList($macros[$host][self::MACRO_TLD_DNS_NAME_SERVERS]),
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
			];
		}

		return $result;
	}

	protected function getTldTypes(array $hostids) {
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

	protected function createObject() {
		parent::createObject();
		$this->updateDnsNsItems();
	}

	protected function updateObject() {
		parent::updateObject();
		$this->updateDnsNsItems();
	}

	protected function createStatusHost(array $input) {
		$config = [
			'host'       => $input[$this->getObjectIdInputField()],
			'status'     => HOST_STATUS_MONITORED,
			'interfaces' => [self::DEFAULT_MAIN_INTERFACE],
			'groups'     => [
				['groupid' => $this->hostGroupIds['TLDs']],
				['groupid' => $this->hostGroupIds[$input['tldType']]],
			],
			'templates'  => [
				['templateid' => $this->templateIds['Template Rsmhost Config ' . $input[$this->getObjectIdInputField()]]],
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

	protected function createTemplates(array $input) {
		parent::createTemplates($input);

		$config = [
			'host'      => 'Template DNS Test - ' . $input[$this->getObjectIdInputField()],
			'groups'    => [
				['groupid' => $this->hostGroupIds['Templates - TLD']],
			],
			'templates' => [$this->templateIds['Template DNS Test']],
		];
		$data = API::Template()->create($config);

		$this->templateIds['Template DNS Test - ' . $input[$this->getObjectIdInputField()]] = $data['templateids'][0];
	}

	protected function getRsmhostProbeTemplatesConfig(string $probe, string $rsmhost) {
		$templates = parent::getRsmhostProbeTemplatesConfig($probe, $rsmhost);

		$templates[] = ['templateid' => $this->templateIds['Template DNS Test - ' . $rsmhost]];

		return $templates;
	}

	protected function updateStatustHost(array $input) {
		$config = [
			'hostid' => $this->getHostId($input['tld']),
			'groups' => [
				['groupid' => $this->hostGroupIds['TLDs']],
				['groupid' => $this->hostGroupIds[$input['tldType']]],
			],
		];
		$data = API::Host()->update($config);

		return $data['hostids'][0];
	}

	protected function deleteObject() {
		parent::deleteObject();

		$templateId = $this->getTemplateId('Template DNS Test - ' . $this->getInput('tld'));
		$data = API::Template()->delete([$templateId]);
	}

	protected function getHostGroupNames(array $input, ?array $additionalNames) {
		$names = [
			// groups for "<rsmhost>" host
			'Templates - TLD',
			'TLDs',
			$input['tldType'],
			// groups for "<rsmhost> <probe>" hosts
			$input['tldType'] . ' Probe results',
			'TLD Probe results',
		];

		if (!is_null($additionalNames))
		{
			$names = array_merge($names, $additionalNames);
		}

		return $names;
	}

	protected function getTemplateNames(?array $additionalNames) {
		$names = [
			// templates for "<rsmhost>" host
			'Template Config History',
			'Template DNS Status',
			'Template DNSSEC Status',
			'Template RDAP Status',
			'Template RDDS Status',
			// templates for "<rsmhost> <probe>" hosts
			'Template DNS Test',
			'Template DNS Test - ' . $this->newObject['tld'],
			'Template RDAP Test',
			'Template RDDS Test',
		];

		if (!is_null($additionalNames))
		{
			$names = array_merge($names, $additionalNames);
		}

		return $names;
	}

	protected function getRsmhostConfigsFromInput(array $input) {
		$services = array_column($input['servicesStatus'], 'enabled', 'service');

		return [
			$input[$this->getObjectIdInputField()] => [
				'tldType' => $input['tldType'],
				'dnsUdp'  => $services['dnsUDP'],
				'dnsTcp'  => $services['dnsTCP'],
				'dnssec'  => $input['dnsParameters']['dnssecEnabled'],
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

			$this->createMacroConfig(self::MACRO_TLD_DNS_UDP_ENABLED   , (int)$services['dnsUDP']),
			$this->createMacroConfig(self::MACRO_TLD_DNS_TCP_ENABLED   , (int)$services['dnsTCP']),
			$this->createMacroConfig(self::MACRO_TLD_DNSSEC_ENABLED    , (int)$input['dnsParameters']['dnssecEnabled']),
			$this->createMacroConfig(self::MACRO_TLD_RDAP_ENABLED      , (int)$services['rdap']),
			$this->createMacroConfig(self::MACRO_TLD_RDDS_ENABLED      , (int)$services['rdds']),

			$this->createMacroConfig(self::MACRO_TLD_DNS_NAME_SERVERS  , $this->nsipListToStr($input['dnsParameters']['nsIps'])),
			$this->createMacroConfig(self::MACRO_TLD_DNS_AVAIL_MINNS   , $input['dnsParameters']['minNameServersUP']),    // TODO: schedule minns change
			$this->createMacroConfig(self::MACRO_TLD_DNS_TESTPREFIX    , $input['dnsParameters']['nsTestPrefix']),

			$this->createMacroConfig(self::MACRO_TLD_RDAP_BASE_URL     , $input['rddsParameters']['rdapUrl']),
			$this->createMacroConfig(self::MACRO_TLD_RDAP_TEST_DOMAIN  , $input['rddsParameters']['rdapTestedDomain']),

			$this->createMacroConfig(self::MACRO_TLD_RDDS43_TEST_DOMAIN, $input['rddsParameters']['rdds43TestedDomain']),
			$this->createMacroConfig(self::MACRO_TLD_RDDS_NS_STRING    , $input['rddsParameters']['rdds43NsString']),
			//$this->createMacroConfig(self::MACRO_TLD_RDDS_43_SERVERS   , $input['rddsParameters']['rdds43Server']),     // TODO: fill with real value
			//$this->createMacroConfig(self::MACRO_TLD_RDDS_80_SERVERS   , $input['rddsParameters']['rdds80Url']),        // TODO: fill with real value
		];
	}

	private function updateDnsNsItems() {
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

		$testTemplateId = $this->templateIds['Template DNS Test - ' . $this->newObject['tld']];

		if (!empty($createNsIp))
		{
			// get value maps

			$valueMapIds = $this->getValueMapIds([
				'RSM Service Availability',
				'RSM DNS rtt',
			]);

			// get itemid of "DNS Test" master item

			$dnsTestItemId = $this->getTemplateItemId('Template DNS Test - ' . $this->newObject['tld'], 'rsm.dns[');

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
				'output' => ['itemid', 'key_'],
				'hostid' => $this->statusHostId,
				'filter' => ['key_' => array_keys($statusItems)],
			]);
			if (!empty($data))
			{
				$statusItems = array_diff_key($statusItems, array_column($data, 'itemid', 'key_'));
				$enableItemIds = array_merge($enableItemIds, array_column($data, 'itemid'));
			}

			$data = API::Item()->get([
				'output' => ['itemid', 'key_'],
				'hostid' => $testTemplateId,
				'filter' => ['key_' => array_keys($testItems)],
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
						'expression'  => sprintf('{%s:%s.last()}>{$RSM.SLV.NS.DOWNTIME}%s', $this->newObject['tld'], $key, $thresholdStr),
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

	private function getTemplateItemId(string $template, string $key): int {
		$config = [
			'output'      => ['itemid'],
			'templated'   => true,
			'templateids' => [$this->templateIds[$template]],
			'search'      => ['key_' => $key],
		];
		$data = API::Item()->get($config);

		return $data[0]['itemid'];
	}

	private function nsipStrToList(string $str): array {
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

	private function nsipListToStr(array $list): string {
		$list = $this->sortNsIpPairs($list);

		foreach ($list as &$nsip)
		{
			$nsip = $nsip['ns'] . ',' . $nsip['ip'];
		}
		unset($nsip);

		return implode(' ', $list);
	}

	private function sortNsIpPairs(array $nsipList): array {
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

	private function compareIp(string $a, string $b): int {
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

	private function compareNsIp(array $a, array $b): int {
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
