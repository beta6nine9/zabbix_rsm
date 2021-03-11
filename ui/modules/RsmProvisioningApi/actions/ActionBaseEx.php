<?php

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

	/******************************************************************************************************************
	 * Common functions                                                                                               *
	 ******************************************************************************************************************/

	protected function createRsmhostProbeHosts(?string $rsmhost, ?string $probe, array $hostGroupIds, array $templateIds)
	{
		// TODO: sanity checks

		$rsmhostConfigs = $this->getRsmhostConfigs($rsmhost);
		$probeConfigs = $this->getProbeConfigs($probe);

		$hostGroupIds = array_merge(
			$hostGroupIds,
			$this->getHostGroupIds(array_keys($probeConfigs))
		);
		$templateIds = array_merge(
			$templateIds,
			$this->getTemplateIds(array_map(fn($probe) => 'Template Probe Config ' . $probe, array_keys($probeConfigs)))
		);

		foreach ($rsmhostConfigs as $rsmhost => $rsmhostConfig)
		{
			foreach ($probeConfigs as $probe => $probeConfig)
			{
				$config = $this->createRsmhostProbeHostConfig($hostGroupIds, $templateIds, $rsmhost, $rsmhostConfig['tldType'], $probe, $probeConfig['proxy_hostid']);
				$data = API::Host()->create($config);
				print_r($data);
			}
		}
	}

	protected function updateRsmhostProbeHosts(?string $rsmhost, ?string $probe, array $hostGroupIds, array $templateIds)
	{
		// TODO: sanity checks

		$rsmhostConfigs = $this->getRsmhostConfigs($rsmhost);
		$probeConfigs = $this->getProbeConfigs($probe);

		$hostGroupIds = array_merge(
			$hostGroupIds,
			$this->getHostGroupIds(array_keys($probeConfigs))
		);
		$templateIds = array_merge(
			$templateIds,
			$this->getTemplateIds(array_map(fn($probe) => 'Template Probe Config ' . $probe, array_keys($probeConfigs)))
		);

		foreach ($rsmhostConfigs as $rsmhost => $rsmhostConfig)
		{
			foreach ($probeConfigs as $probe => $probeConfig)
			{
				$config = $this->createRsmhostProbeHostConfig($hostGroupIds, $templateIds, $rsmhost, $rsmhostConfig['tldType'], $probe, $probeConfig['proxy_hostid']);
				$config['hostid'] = $this->getHostId($config['host']);
				$data = API::Host()->update($config);
			}
		}
	}

	/******************************************************************************************************************
	 * Probe and Rsmhost config getters                                                                               *
	 ******************************************************************************************************************/

	protected function getProbeConfigs(?string $probe)
	{
		// TODO: add sanity checks
		// TODO: include disabled objects (with all services disabled)

		// get 'Probes' host group id
		$hostGroupIds = $this->getHostGroupIds(['Probes']);

		// get probe hosts
		$data = API::Host()->get([
			'output'   => ['host', 'proxy_hostid'],
			'groupids' => [$hostGroupIds['Probes']],
			'filter'   => [
				'status' => HOST_STATUS_MONITORED,
				'host'   => is_null($probe) ? [] : [$probe],
			],
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

	protected function getRsmhostConfigs(?string $rsmhost)
	{
		// TODO: add sanity checks
		// TODO: include disabled objects (with all services disabled)

		// get 'TLDs' host group id
		$hostGroupIds = $this->getHostGroupIds(['TLDs']);

		// get tld hosts
		$data = API::Host()->get([
			'output'   => ['host'],
			'groupids' => [$hostGroupIds['TLDs']],
			'filter'   => [
				'status' => HOST_STATUS_MONITORED,
				'host'   => is_null($rsmhost) ? [] : [$rsmhost],
			],
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
				'tldType' => 'gTLD',                                                              // TODO: fill with real value; test what performs better - API::Host()->get() or API::HostGroup()->Get()
				'dnsUdp'  => (bool)$macros[$host][self::MACRO_TLD_DNS_UDP_ENABLED],
				'dnsTcp'  => (bool)$macros[$host][self::MACRO_TLD_DNS_TCP_ENABLED],
				'rdap'    => (bool)$macros[$host][self::MACRO_TLD_RDAP_ENABLED],
				'rdds'    => (bool)$macros[$host][self::MACRO_TLD_RDDS_ENABLED],
			];
		}

		return $result;
	}

	/******************************************************************************************************************
	 * API helpers                                                                                                    *
	 ******************************************************************************************************************/

	protected function createMacroConfig($macro, $value)
	{
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

	protected function createRsmhostProbeHostConfig(array $hostGroupIds, array $templateIds, string $rsmhost, string $rsmhostType, string $probe, string $proxyHostId)
	{
		return [
			'host'         => $rsmhost . ' ' . $probe,
			'status'       => HOST_STATUS_MONITORED,
			'proxy_hostid' => $proxyHostId,
			'interfaces'   => [
				self::DEFAULT_MAIN_INTERFACE,
			],
			'groups'       => [
				['groupid' => $hostGroupIds['TLD Probe results']],
				['groupid' => $hostGroupIds[$rsmhostType . ' Probe results']],
				['groupid' => $hostGroupIds[$probe]],
				['groupid' => $hostGroupIds['TLD ' . $rsmhost]],
			],
			'templates'    => [
				['templateid' => $templateIds['Template DNS Test']],
				['templateid' => $templateIds['Template RDAP Test']],
				['templateid' => $templateIds['Template RDDS Test']],
				['templateid' => $templateIds['Template Probe Config ' . $probe]],
				['templateid' => $templateIds['Template Rsmhost Config ' . $rsmhost]],
			],
		];
	}

	protected function getHostsByHostGroup(string $hostGroup, ?string $host, ?array $additionalFields)
	{
		$outputFields = ['host'];

		if (!is_null($additionalFields))
		{
			$outputFields = array_merge($outputFields, $additionalFields);
		}

		$hostGroupIds = $this->getHostGroupIds([$hostGroup]);

		return API::Host()->get([
			'output'   => $outputFields,
			'groupids' => $hostGroupIds,
			'filter'   => [
				'host'   => is_null($host) ? [] : [$host],
			],
		]);
	}

	protected function getHostId(string $host)
	{
		$data = API::Host()->get([
			'output' => ['hostid'],
			'filter' => ['host' => $host],
		]);

		return $data[0]['hostid'];
	}

	protected function getHostGroupIds(array $hostGroupNames)
	{
		$data = API::HostGroup()->get([
			'output' => ['groupid', 'name'],
			'filter' => ['name' => $hostGroupNames],
		]);

		$hostGroupIds = array_column($data, 'groupid', 'name');

		// TODO: check if all requested groups are found

		return $hostGroupIds;
	}

	protected function getTemplateIds(array $templateHosts)
	{
		$data = API::Template()->get([
			'output' => ['templateid', 'host'],
			'filter' => ['host' => $templateHosts],
		]);

		$templateIds = array_column($data, 'templateid', 'host');

		// TODO: check if all requested templates are found

		return $templateIds;
	}

	protected function getHostMacros(array $hosts, array $macros)
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
}
