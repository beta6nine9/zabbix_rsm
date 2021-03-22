<?php

namespace Modules\RsmProvisioningApi\Actions;

require_once __DIR__ . '/../validators/validators.inc.php';

use API;
use Exception;

abstract class ActionBaseEx extends ActionBase {

	protected const DEFAULT_MAIN_INTERFACE = [
		'type'  => INTERFACE_TYPE_AGENT,
		'main'  => INTERFACE_PRIMARY,
		'useip' => INTERFACE_USE_IP,
		'ip'    => '127.0.0.1',
		'dns'   => '',
		'port'  => '10050',
	];

	private const MACRO_GLOBAL_MONITORING_TARGET = '{$RSM.MONITORING.TARGET}';
	private const MACRO_GLOBAL_RDAP_STANDALONE   = '{$RSM.RDAP.STANDALONE}';

	protected const MACRO_PROBE_PROXY_NAME   = '{$RSM.PROXY_NAME}';
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
	//protected const MACRO_TLD_RDDS_43_SERVERS    ? '{$RSM.TLD.RDDS.43.SERVERS}';
	//protected const MACRO_TLD_RDDS_80_SERVERS    ? '{$RSM.TLD.RDDS.80.SERVERS}';
	protected const MACRO_TLD_RDDS_NS_STRING     = '{$RSM.RDDS.NS.STRING}';

	protected const MACRO_DESCRIPTIONS = [
		self::MACRO_PROBE_PROXY_NAME       => '',
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
		//self::MACRO_TLD_RDDS_43_SERVERS    ?? 'List of RDDS43 server host names as candidates for a test',
		//self::MACRO_TLD_RDDS_80_SERVERS    ?? 'List of Web Whois server host names as candidates for a test',
		self::MACRO_TLD_RDDS_NS_STRING     => 'What to look for in RDDS output, e.g. "Name Server:"',
	];

	protected const MONITORING_TARGET_REGISTRY  = 'registry';
	protected const MONITORING_TARGET_REGISTRAR = 'registrar';

	/**
	 * Creates "<rsmhost> <probe>" hosts when either new rsmhost or new probe is created.
	 */
	protected function createRsmhostProbeHosts(array $rsmhostConfigs, array $probeConfigs, array $hostGroupIds, array $templateIds) {
		// get missing host group ids

		$missingHostGroups = array_merge(
			array_diff(
				array_map(fn($rsmhost) => 'TLD ' . $rsmhost, array_keys($rsmhostConfigs)),
				array_keys($hostGroupIds)
			),
			array_diff(
				array_keys($probeConfigs),
				array_keys($hostGroupIds)
			)
		);
		$hostGroupIds += $this->getHostGroupIds($missingHostGroups);

		// get missing template ids

		$missingTemplates = array_merge(
			array_diff(
				array_map(fn($rsmhost) => 'Template Rsmhost Config ' . $rsmhost, array_keys($rsmhostConfigs)),
				array_keys($templateIds)
			),
			array_diff(
				array_map(fn($probe) => 'Template Probe Config ' . $probe, array_keys($probeConfigs)),
				array_keys($templateIds)
			)
		);
		$templateIds += $this->getTemplateIds($missingTemplates);

		// create configs for hosts

		$configs = [];

		foreach ($rsmhostConfigs as $rsmhost => $rsmhostConfig)
		{
			foreach ($probeConfigs as $probe => $probeConfig)
			{
				$configs[] = [
					'host'         => $rsmhost . ' ' . $probe,
					'status'       => HOST_STATUS_MONITORED,
					'proxy_hostid' => $probeConfig['proxy_hostid'],
					'interfaces'   => [self::DEFAULT_MAIN_INTERFACE],
					'groups'       => $this->getRsmhostProbeHostGroupsConfig($hostGroupIds, $rsmhostConfig['tldType'], $probe, $rsmhost),
					'templates'    => $this->getRsmhostProbeTemplatesConfig($templateIds, $probe, $rsmhost),
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
	protected function updateRsmhostProbeHosts(array $rsmhostConfigs, array $probeConfigs, array $hostGroupIds) {
		// get missing host group ids

		$missingHostGroups = array_merge(
			array_diff(
				array_map(fn($rsmhost) => 'TLD ' . $rsmhost, array_keys($rsmhostConfigs)),
				array_keys($hostGroupIds)
			),
			array_diff(
				array_keys($probeConfigs),
				array_keys($hostGroupIds)
			)
		);
		$hostGroupIds += $this->getHostGroupIds($missingHostGroups);

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
				$configs[] = [
					'hostid' => $hostids[$rsmhost . ' ' . $probe],
					'groups' => $this->getRsmhostProbeHostGroupsConfig($hostGroupIds, $rsmhostConfig['tldType'], $probe, $rsmhost),
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

	private function getRsmhostProbeHostGroupsConfig(array $hostGroupIds, string $tldType, string $probe, string $rsmhost) {
		return [
			['groupid' => $hostGroupIds['TLD Probe results']],
			['groupid' => $hostGroupIds[$tldType . ' Probe results']],
			['groupid' => $hostGroupIds[$probe]],
			['groupid' => $hostGroupIds['TLD ' . $rsmhost]],
		];
	}

	private function getRsmhostProbeTemplatesConfig(array $templateIds, string $probe, string $rsmhost) {
		$templates = [];

		if ($this->getMonitoringTarget() === self::MONITORING_TARGET_REGISTRY)
		{
			$templates[] = ['templateid' => $templateIds['Template DNS Test']];
		}

		$templates[] = ['templateid' => $templateIds['Template RDAP Test']];
		$templates[] = ['templateid' => $templateIds['Template RDDS Test']];
		$templates[] = ['templateid' => $templateIds['Template Probe Config ' . $probe]];
		$templates[] = ['templateid' => $templateIds['Template Rsmhost Config ' . $rsmhost]];

		return $templates;
	}

	/**
	 * Enables and disables items in "<rsmhost>" and "<rsmhost> <probe>" hosts.
	 */
	protected function updateServiceItemStatus(array $statusHosts, array $testHosts, array $templateIds, array $rsmhostConfigs, array $probeConfigs) {
		$hosts = $statusHosts + $testHosts;

		// get template items

		$config = [
			'output' => ['key_', 'hostid'],
			'hostids' => [
				$templateIds['Template DNS Test'],
				$templateIds['Template RDAP Test'],
				$templateIds['Template RDDS Test'],
			],
		];
		if (!empty($statusHosts))
		{
			$config['hostids'] = array_merge(
				$config['hostids'],
				[
					$templateIds['Template DNS Status'],
					$templateIds['Template DNSSEC Status'],
					$templateIds['Template RDAP Status'],
					$templateIds['Template RDDS Status'],
				]
			);
		}
		$data = API::Item()->get($config);

		$templates = array_flip($templateIds);

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
			$status = $rsmhostConfigs[$host]['dnssec'] ? ITEM_STATUS_ACTIVE : ITEM_STATUS_DISABLED;
			$config += $this->getItemStatusConfig($hostItems[$host], $templateItems['Template DNSSEC Status'], $status);

			if ($this->isStandaloneRdap())
			{
				$status = $rsmhostConfigs[$host]['rdap'] ? ITEM_STATUS_ACTIVE : ITEM_STATUS_DISABLED;
				$config += $this->getItemStatusConfig($hostItems[$host], $templateItems['Template RDAP Status'], $status);

				$status = $rsmhostConfigs[$host]['rdds'] ? ITEM_STATUS_ACTIVE : ITEM_STATUS_DISABLED;
				$config += $this->getItemStatusConfig($hostItems[$host], $templateItems['Template RDDS Status'], $status);
			}
			else
			{
				$status = ITEM_STATUS_DISABLED;
				$config += $this->getItemStatusConfig($hostItems[$host], $templateItems['Template RDAP Status'], $status);

				$status = $rsmhostConfigs[$host]['rdap'] || $rsmhostConfigs[$host]['rdds'] ? ITEM_STATUS_ACTIVE : ITEM_STATUS_DISABLED;
				$config += $this->getItemStatusConfig($hostItems[$host], $templateItems['Template RDDS Status'], $status);
			}

			foreach ($testHosts as $hostid => $host)
			{
				list ($rsmhost, $probe) = explode(' ', $host);

				$status = $rsmhostConfigs[$rsmhost]['rdap'] && $probeConfigs[$probe]['rdap'] ? ITEM_STATUS_ACTIVE : ITEM_STATUS_DISABLED;
				$config += $this->getItemStatusConfig($hostItems[$host], $templateItems['Template RDAP Test'], $status);

				$status = $rsmhostConfigs[$rsmhost]['rdds'] && $probeConfigs[$probe]['rdds'] ? ITEM_STATUS_ACTIVE : ITEM_STATUS_DISABLED;
				$config += $this->getItemStatusConfig($hostItems[$host], $templateItems['Template RDDS Test'], $status);
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
	private function getItemStatusConfig(array $items, array $keys, int $newStatus): array {
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
	protected function getMonitoringTarget() {
		static $result = null;

		if (is_null($result))
		{
			$data = API::UserMacro()->get([
				'output'      => ['value'],
				'globalmacro' => true,
				'filter'      => ['macro' => self::MACRO_GLOBAL_MONITORING_TARGET],
			]);

			$result = $data[0]['value'];
		}

		return $result;
	}

	/**
	 * Returns status of Standalone RDAP.
	 */
	protected function isStandaloneRdap() {
		static $result = null;

		if (is_null($result))
		{
			$data = API::UserMacro()->get([
				'output'      => ['value'],
				'globalmacro' => true,
				'filter'      => ['macro' => self::MACRO_GLOBAL_RDAP_STANDALONE],
			]);

			$ts = (int)$data[0]['value'];

			$result = $ts && $_SERVER['REQUEST_TIME'] >= $ts;
		}

		return $result;
	}

	protected function getProbeConfigs() {
		// get 'Probes' host group id
		$hostGroupIds = $this->getHostGroupIds(['Probes']);

		// get probe hosts
		$data = API::Host()->get([
			'output'   => ['host', 'proxy_hostid'],
			'groupids' => [$hostGroupIds['Probes']],
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
			$result[$host] = [
				'proxy_hostid' => $proxies[$hostid],
				'ipv4'         => (bool)$macros[$host][self::MACRO_PROBE_IP4_ENABLED],
				'ipv6'         => (bool)$macros[$host][self::MACRO_PROBE_IP6_ENABLED],
				'rdap'         => (bool)$macros[$host][self::MACRO_PROBE_RDAP_ENABLED],
				'rdds'         => (bool)$macros[$host][self::MACRO_PROBE_RDDS_ENABLED],
			];
		}

		return $result;
	}

	protected function getRsmhostConfigs() {
		// get 'TLDs' host group id
		$hostGroupIds = $this->getHostGroupIds(['TLDs']);

		// get tld hosts
		$data = API::Host()->get([
			'output'   => ['host'],
			'groupids' => [$hostGroupIds['TLDs']],
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

		// join data in a common data structure
		$result = [];

		foreach ($hosts as $host)
		{
			$result[$host] = [
				'tldType' => 'gTLD',                                                                                    // TODO: fill with real value; test what performs better - API::Host()->get() or API::HostGroup()->Get()
				'dnsUdp'  => (bool)$macros[$host][self::MACRO_TLD_DNS_UDP_ENABLED],
				'dnsTcp'  => (bool)$macros[$host][self::MACRO_TLD_DNS_TCP_ENABLED],
				'dnssec'  => (bool)$macros[$host][self::MACRO_TLD_DNSSEC_ENABLED],
				'rdap'    => (bool)$macros[$host][self::MACRO_TLD_RDAP_ENABLED],
				'rdds'    => (bool)$macros[$host][self::MACRO_TLD_RDDS_ENABLED],
			];
		}

		return $result;
	}

	/**
	 * Creates config array of a macro (macro, value, description).
	 */
	protected function createMacroConfig(string $macro, string $value) {
		if (!array_key_exists($macro, self::MACRO_DESCRIPTIONS))
		{
			throw new Exception("Macro '$macro' does not have description");
		}

		return [
			'description' => self::MACRO_DESCRIPTIONS[$macro],
			'macro' => $macro,
			'value' => $value,
		];
	}

	protected function getHostsByHostGroupId(int $hostGroupId, ?string $host, ?array $additionalFields) {
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

	protected function getHostsByHostGroup(string $hostGroup, ?string $host, ?array $additionalFields) {
		return $this->getHostsByHostGroupId($this->getHostGroupId($hostGroup), $hostGroup, $additionalFields);
	}

	protected function getHostsByTemplateId(int $templateId, ?string $host, ?array $additionalFields) {
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

	protected function getHostsByTemplate(string $template, ?string $host, ?array $additionalFields) {
		return $this->getHostsByTemplateId($this->getTemplateId($template), $host, $additionalFields);
	}

	protected function getHostIds(array $hosts) {
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

	protected function getHostId(string $host) {
		$hostids = $this->getHostIds([$host]);
		return $hostids[$host];
	}

	protected function getHostGroupIds(array $hostGroupNames) {
		if (empty($hostGroupNames))
		{
			return [];
		}

		$data = API::HostGroup()->get([
			'output' => ['groupid', 'name'],
			'filter' => ['name' => $hostGroupNames],
		]);

		$hostGroupIds = array_column($data, 'groupid', 'name');

		return $hostGroupIds;
	}

	protected function getHostGroupId(string $hostGroup) {
		$hostGroupIds = $this->getHostGroupIds([$hostGroup]);
		return $hostGroupIds[$hostGroup];
	}

	protected function getTemplateIds(array $templateHosts) {
		if (empty($templateHosts))
		{
			return [];
		}

		$data = API::Template()->get([
			'output' => ['templateid', 'host'],
			'filter' => ['host' => $templateHosts],
		]);

		$templateIds = array_column($data, 'templateid', 'host');

		return $templateIds;
	}

	protected function getTemplateId(string $templateHost) {
		$templateIds = $this->getTemplateIds([$templateHost]);
		return $templateIds[$templateHost];
	}

	protected function getProxyIds(array $proxies) {
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

	protected function getProxyId(string $proxy) {
		$proxyIds = $this->getProxyIds([$proxy]);
		return $proxyIds[$proxy];
	}

	protected function getHostMacros(array $hosts, array $macros) {
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
}
