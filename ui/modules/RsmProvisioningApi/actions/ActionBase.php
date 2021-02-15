<?php

namespace Modules\RsmProvisioningApi\Actions;

require_once __DIR__ . '/../validators/validators.inc.php';

use API;
use CApiInputValidator;
use CController;
use CControllerResponseData;
use CWebUser;
use Exception;

abstract class ActionBase extends CController
{
	const USER_READONLY         = 'provisioning-api-readonly';
	const USER_READWRITE        = 'provisioning-api-readwrite';

	const REQUEST_METHOD_GET    = 'GET';
	const REQUEST_METHOD_DELETE = 'DELETE';
	const REQUEST_METHOD_PUT    = 'PUT';

	abstract protected function getInputRules(): array;
	abstract protected function handleGetRequest();
	abstract protected function handleDeleteRequest();
	abstract protected function handlePutRequest();

	public function __construct()
	{
		$this->disableSIDvalidation();
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
		switch ($_SERVER['REQUEST_METHOD'])
		{
			case self::REQUEST_METHOD_GET:
				return $this->handleGetRequest();

			case self::REQUEST_METHOD_DELETE:
				return $this->handleDeleteRequest();

			case self::REQUEST_METHOD_PUT:
				return $this->handlePutRequest();

			default:
				throw new Exception('Unsupported request method');
		}
	}

	protected function returnJson(array $json)
	{
		$options = 0;

		if (!is_int(key($json)))
		{
			$options |= JSON_PRETTY_PRINT;
		}

		$this->setResponse(new CControllerResponseData([
			'main_block' => json_encode($json, $options)
		]));
	}
}
