<?php

namespace Modules\RsmProvisioningApi\Actions;

use API;
use Modules\RsmProvisioningApi\RsmApi as RsmApi;

class Probe extends ActionBase
{
	const MACRO_IP4_ENABLED  = '{$RSM.IP4.ENABLED}';
	const MACRO_IP6_ENABLED  = '{$RSM.IP6.ENABLED}';
	const MACRO_RDAP_ENABLED = '{$RSM.RDAP.ENABLED}';
	const MACRO_RDDS_ENABLED = '{$RSM.RDDS.ENABLED}';
	const MACRO_RESOLVER     = '{$RSM.RESOLVER}';

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

	protected function handleGetRequest()
	{
		if ($this->hasInput('probe'))
		{
			$data = $this->getProbes($this->getInput('probe'));
			if (empty($data))
			{
				// TODO: probe not found
			}
			else
			{
				$data = $data[0];
			}
		}
		else
		{
			$data = $this->getProbes(null);
		}

		$this->returnJson($data);
	}

	protected function handleDeleteRequest()
	{
		var_dump(__METHOD__);
		$this->returnJson(['foo' => 'bar']);
	}

	protected function handlePutRequest()
	{
		var_dump(__METHOD__);
		$this->returnJson(['foo' => 'bar']);
	}

	private function getProbes($probe)
	{
		// TODO: add sanity checks

		// get 'Probes' host group id
		$data = API::HostGroup()->get([
			'output' => ['groupid'],
			'filter' => ['name' => 'Probes'],
		]);
		$hostGroupId = $data[0]['groupid'];

		// get probe hosts
		$data = API::Host()->get([
			'output' => ['host'],
			'groupids' => [$hostGroupId],
			'filter' => [
				'status' => HOST_STATUS_MONITORED,
				'host' => is_null($probe) ? [] : [$probe],
			],
		]);
		$hosts = array_column($data, 'host', 'hostid');

		if (empty($hosts))
		{
			return [];
		}

		// get proxies
		$data = API::Proxy()->get([
			'output' => ['host'],
			'filter' => ['status' => HOST_STATUS_PROXY_PASSIVE, 'host' => $hosts],
			'selectInterface' => ['ip', 'port'],
		]);
		$interfaces = array_column($data, 'interface', 'host');

		// get templates
		$templateNames = array_values(array_map(fn($host) => 'Template Probe Config ' . $host, $hosts));
		$data = API::Template()->get([
			'output' => ['host', 'templateid'],
			'filter' => ['host' => $templateNames],
		]);
		$templates = array_column($data, 'host', 'templateid');

		// get template macros
		$data = API::UserMacro()->get([
			'output'  => ['hostid', 'macro', 'value'],
			'hostids' => array_keys($templates),
			'filter'  => [
				'macro' => [
					self::MACRO_IP4_ENABLED,
					self::MACRO_IP6_ENABLED,
					self::MACRO_RDAP_ENABLED,
					self::MACRO_RDDS_ENABLED,
					self::MACRO_RESOLVER,
				],
			],
		]);
		$macros = [];
		foreach ($data as $item)
		{
			$host = str_replace('Template Probe Config ', '' , $templates[$item['hostid']]);
			$macro = $item['macro'];
			$value = $item['value'];
			$macros[$host][$macro] = $value;
		}

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
						'enabled'               => boolval($macros[$host][self::MACRO_RDAP_ENABLED]),
					],
					[
						'service'               => 'rdds',
						'enabled'               => boolval($macros[$host][self::MACRO_RDDS_ENABLED]),
					],
				],
				'zabbixProxyParameters'         => [
					'ipv4Enable'                => boolval($macros[$host][self::MACRO_IP4_ENABLED]),
					'ipv6Enable'                => boolval($macros[$host][self::MACRO_IP6_ENABLED]),
					'ipResolver'                => $macros[$host][self::MACRO_RESOLVER],
					'proxyIp'                   => $interfaces[$host]['ip'],
					'proxyPort'                 => $interfaces[$host]['port'],
					'proxyPskIdentity'          => null,
					'proxyPsk'                  => null,
				],
				'online'                        => boolval($status[$hostid]),
				'zabbixMonitoringCentralServer' => 'TODO',            // TODO: fill with real value
			];
		}

		return $result;
	}
}
