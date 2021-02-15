<?php

namespace Modules\RsmProvisioningApi\Actions;

class Registrar extends MonitoringTarget
{
	protected function getInputRules(): array
	{
		switch ($_SERVER['REQUEST_METHOD'])
		{
			case self::REQUEST_METHOD_GET:
				return [
					'type' => API_OBJECT, 'fields' => [
						'registrar'                     => ['type' => API_UINT64],
					]
				];

			case self::REQUEST_METHOD_DELETE:
				return [
					'type' => API_OBJECT, 'fields' => [
						'registrar'                     => ['type' => API_UINT64     , 'flags' => API_REQUIRED],
					]
				];

			case self::REQUEST_METHOD_PUT:
				return [
					'type' => API_OBJECT, 'fields' => [
						'registrar'                     => ['type' => API_UINT64     , 'flags' => API_REQUIRED],
						'registrarName'                 => ['type' => API_STRING_UTF8, 'flags' => API_REQUIRED],
						'registrarFamily'               => ['type' => API_STRING_UTF8, 'flags' => API_REQUIRED],
						'servicesStatus'                => ['type' => API_OBJECTS    , 'flags' => API_REQUIRED, 'uniq' => [['service']], 'fields' => [  // TODO: all services (i.e. rdds, rdap) must be specified
							'service'                   => ['type' => API_STRING_UTF8, 'flags' => API_REQUIRED, 'in' => 'rdap,rdds'],
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
		TODO: make sure this instance monitors registrars
	}
	*/

	protected function handleGetRequest()
	{
		if ($this->hasInput('registrar'))
		{
			$data = $this->getRegistrars($this->getInput('registrar'));
			if (empty($data))
			{
				// TODO: registrar not found
			}
			else
			{
				$data = $data[0];
			}
		}
		else
		{
			$data = $this->getRegistrars(null);
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

	private function getRegistrars($registrar)
	{
		return [];
	}
}
