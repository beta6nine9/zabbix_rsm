<?php

namespace Modules\RsmProvisioningApi\Actions;

class Tld extends MonitoringTarget
{
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

	/*
	protected function doAction()
	{
		TODO: make sure this instance monitors registries
	}
	*/

	protected function handleGetRequest()
	{
		if ($this->hasInput('tld'))
		{
			$data = $this->getTlds($this->getInput('tld'));
			if (empty($data))
			{
				// TODO: tld not found
			}
			else
			{
				$data = $data[0];
			}
		}
		else
		{
			$data = $this->getTlds(null);
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

	private function getTlds($tld)
	{
		return [];
	}
}
