<?php

namespace Modules\RsmProvisioningApi\Actions;

require_once __DIR__ . '/../validators/validators.inc.php';

use API;
use CApiInputValidator;
use CController;
use CControllerResponseData;
use Exception;

abstract class ActionBase extends CController
{
	private const USER_READONLY  = 'provisioning-api-readonly';
	private const USER_READWRITE = 'provisioning-api-readwrite';

	protected const REQUEST_METHOD_GET     = 'GET';
	protected const REQUEST_METHOD_DELETE  = 'DELETE';
	protected const REQUEST_METHOD_PUT     = 'PUT';

	abstract protected function checkMonitoringTarget();
	abstract protected function getInputRules(): array;
	abstract protected function getObjectIdInputField();
	abstract protected function getObjects(?string $objectId);
	abstract protected function createObject();
	abstract protected function updateObject();
	abstract protected function deleteObject();

	public function __construct()
	{
		parent::__construct();
		$this->disableSIDvalidation();
	}

	public function __destruct()
	{
		printf("\n\nTime-Spent: %f\n", microtime(true) - $_SERVER['REQUEST_TIME_FLOAT']);
	}

	protected function checkPermissions()
	{
		if (!isset($_SERVER['PHP_AUTH_USER']))
		{
			return false;
		}
		if (!isset($_SERVER['PHP_AUTH_PW']))
		{
			return false;
		}

		$username = $_SERVER['PHP_AUTH_USER'];
		$password = $_SERVER['PHP_AUTH_PW'];

		$userData = API::User()->login(['user' => $username, 'password' => $password, 'userData' => true]);

		if ($userData === false)
		{
			return false;
		}

		API::getWrapper()->auth = $userData['sessionid'];

		DBexecute('DELETE FROM sessions WHERE userid=' . $userData['userid'] . ' AND lastaccess<' . (time() - 12 * SEC_PER_HOUR));

		switch ($_SERVER['REQUEST_METHOD'])
		{
			case self::REQUEST_METHOD_GET:
				return $username === self::USER_READWRITE || $username === self::USER_READONLY;

			case self::REQUEST_METHOD_DELETE:
				return $username === self::USER_READWRITE;

			case self::REQUEST_METHOD_PUT:
				return $username === self::USER_READWRITE;

			default:
				return false;
		}
	}

	protected function checkInput()
	{
		return $this->validateInput($this->getInputRules());
	}

	public function validateInput($validationRules)
	{
		$this->input = $this->getRequestInput();

		if (!CApiInputValidator::validate($validationRules, $this->input, '', $error))
		{
			$this->returnJson([
				'error' => $error
			]);
			return false;
		}

		return true;
	}

	protected function getRequestInput(): array
	{
		switch ($_SERVER['REQUEST_METHOD'])
		{
			case self::REQUEST_METHOD_GET:
				$input = $_GET;
				unset($input['action']);
				return $input;

			case self::REQUEST_METHOD_DELETE:
				$input = $_GET;
				unset($input['action']);
				return $input;

			case self::REQUEST_METHOD_PUT:
				return json_decode(file_get_contents('php://input'), true);

			default:
				throw new Exception('Unsupported request method');
		}
	}

	protected function doAction()
	{
		if (!$this->checkMonitoringTarget())
		{
			throw new Exception('Invalid monitoring target');
		}

		DBstart();

		switch ($_SERVER['REQUEST_METHOD'])
		{
			case self::REQUEST_METHOD_GET:
				$this->handleGetRequest();
				break;

			case self::REQUEST_METHOD_DELETE:
				$this->handleDeleteRequest();
				break;

			case self::REQUEST_METHOD_PUT:
				$this->handlePutRequest();
				break;

			default:
				throw new Exception('Unsupported request method');
		}

		DBend(false);
	}

	protected function returnJson(array $json)
	{
		$options = JSON_UNESCAPED_SLASHES;

		if (!is_int(key($json)))
		{
			$options |= JSON_PRETTY_PRINT;
		}

		$this->setResponse(new CControllerResponseData([
			'main_block' => json_encode($json, $options)
		]));
	}

	protected function handleGetRequest()
	{
		if ($this->hasInput($this->getObjectIdInputField()))
		{
			$data = $this->getObjects($this->getInput($this->getObjectIdInputField()));

			if (empty($data))
			{
				throw new Exception("Requested object does not exist");
			}

			$data = $data[0];
		}
		else
		{
			$data = $this->getObjects(null);
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
		$objects = $this->getObjects($this->getInput($this->getObjectIdInputField()));

		if (empty($objects))
		{
			$this->createObject();
		}
		else
		{
			$this->updateObject();
		}

		$this->returnJson([$this->getInput($this->getObjectIdInputField())]);
	}
}
