<?php

//declare(strict_types=1); // TODO: enable strict_types

namespace Modules\RsmProvisioningApi\Actions;

require_once __DIR__ . '/../validators/validators.inc.php';

use API;
use CApiInputValidator;
use CController;
use CControllerResponseData;
use Exception;
use ErrorException;
use Throwable;

abstract class ActionBase extends CController {

	private const USER_READONLY  = 'provisioning-api-readonly';
	private const USER_READWRITE = 'provisioning-api-readwrite';

	protected const REQUEST_METHOD_GET    = 'GET';
	protected const REQUEST_METHOD_DELETE = 'DELETE';
	protected const REQUEST_METHOD_PUT    = 'PUT';

	protected $oldObject = null;
	protected $newObject = null;

	abstract protected function checkMonitoringTarget();
	abstract protected function requestConfigCacheReload();
	abstract protected function getInputRules(): array;
	abstract protected function getObjects(?string $objectId);
	abstract protected function createObject();
	abstract protected function updateObject();
	abstract protected function deleteObject();

	public function __construct() {
		parent::__construct();
		$this->disableSIDvalidation();
	}

	public function __destruct() {
		if (!headers_sent())
		{
			$stats = $this->getStats();
			$stats = explode("\n", $stats);

			foreach ($stats as $i => $line)
			{
				header(sprintf("Rsm-Stats-%02d: %s", $i, $line));
			}
		}
	}

	protected function getStats(): string {
		$format = fn($n) => sprintf('%7s', number_format($n, 0, '.', '\''));

		// gather stats

		$dbStats = mysqli_get_connection_stats($GLOBALS['DB']['DB']);

		$time_spent = microtime(true) - $_SERVER['REQUEST_TIME_FLOAT'];

		$data = [
			null,
			'total'          => $format($dbStats['com_query']),
			'selects'        => $format($dbStats['result_set_queries'    ]) . sprintf(" (%.1f%%)", $dbStats['result_set_queries'    ] / $dbStats['com_query'] * 100),
			'updates'        => $format($dbStats['non_result_set_queries']) . sprintf(" (%.1f%%)", $dbStats['non_result_set_queries'] / $dbStats['com_query'] * 100),
			null,
			'rows fetched'   => $format($dbStats['rows_fetched_from_server_normal']),
			null,
			'fetched int'    => $format($dbStats['proto_text_fetched_int']),
			'fetched bigint' => $format($dbStats['proto_text_fetched_bigint']),
			'fetched string' => $format($dbStats['proto_text_fetched_string']),
			'fetched enum'   => $format($dbStats['proto_text_fetched_enum']),
			null,
			'memory used'    => sprintf("%.2f MB", memory_get_peak_usage(true) / 1024 / 1024),
			'time spent'     => sprintf("%.2f seconds", $time_spent),
			#'select time'    => sprintf("%.2f seconds (%.1f%%)", $GLOBALS['select_time' ], $GLOBALS['select_time' ] / $time_spent * 100),
			#'execute time'   => sprintf("%.2f seconds (%.1f%%)", $GLOBALS['execute_time'], $GLOBALS['execute_time'] / $time_spent * 100),
			#'fetch time'     => sprintf("%.2f seconds (%.1f%%)", $GLOBALS['fetch_time'  ], $GLOBALS['fetch_time'  ] / $time_spent * 100),
			null,
		];

		// format stats as table

		$k_len = max(array_map('strlen', array_keys($data)));
		$v_len = max(array_map('strlen', array_values($data)));

		$line = '+' . str_repeat('-', $k_len + 2) . '+' . str_repeat('-', $v_len + 2) . '+';

		$output = '';

		foreach ($data as $k => $v)
		{
			if (is_null($v))
			{
				$output .= $line . "\n";
			}
			else
			{
				$output .= sprintf("| %-{$k_len}s | %-{$v_len}s |\n", $k, $v);
			}
		}

		return substr($output, 0, -1);
	}

	protected function check(bool $condition, string $message): void
	{
		if (!$condition)
		{
			throw new Exception($message);
		}
	}

	public function errorHandler(int $errno, string $errstr, string $errfile, int $errline): void {
		// turn PHP errors, warnings, notices into exceptions
		throw new ErrorException($errstr, 0, $errno, $errfile, $errline);
	}

	final protected function checkPermissions(): bool {
		// permissions have to be validated in doAction(), otherwise frontend will output HTML
		return true;
	}

	final protected function checkInput(): bool {
		// input has to be validated in doAction(), otherwise frontend will output HTML
		return true;
	}

	final public function validateInput($validationRules): bool {
		// normally, validateInput() is supposed to be called from checkInput(), but not in this module
		throw new Exception('Internal error');
	}

	protected function isValidUser(&$error): bool {
		if (!isset($_SERVER['PHP_AUTH_USER']))
		{
			$error = "Username is not specified";
			return false;
		}
		if (!isset($_SERVER['PHP_AUTH_PW']))
		{
			$error = "Password is not specified";
			return false;
		}

		$username = $_SERVER['PHP_AUTH_USER'];
		$password = $_SERVER['PHP_AUTH_PW'];

		$userData = API::User()->login(['user' => $username, 'password' => $password, 'userData' => true]);

		if ($userData === false)
		{
			$error = "Invalid username or password";
			return false;
		}

		// required hack for API
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
				$error = "Invalid request method";
				return false;
		}
	}

	protected function isValidInput(&$error): bool {
		$this->input = $this->getRequestInput();

		$validationRules = $this->getInputRules();

		if (!CApiInputValidator::validate($validationRules, $this->input, '', $error))
		{
			return false;
		}

		return true;
	}

	private function getRequestInput(): array {
		$input = null;

		switch ($_SERVER['REQUEST_METHOD'])
		{
			case self::REQUEST_METHOD_GET:
				$input = $_GET;
				unset($input['action']);
				break;

			case self::REQUEST_METHOD_DELETE:
				$input = $_GET;
				unset($input['action']);
				break;

			case self::REQUEST_METHOD_PUT:
				$input = json_decode(file_get_contents('php://input'), true);
				if (array_key_exists('id', $this->input))
				{
					throw new Exception('Unexpected parameter: "id"');
				}
				if (!array_key_exists('id', $_GET))
				{
					throw new Exception('Missing parameter: "id"');
				}
				$input = ['id' => $_GET['id']] + $input;
				break;

			default:
				throw new Exception('Unsupported request method');
		}

		return $input;
	}

	protected function doAction() {
		set_error_handler([$this, 'errorHandler'], E_ALL | E_STRICT);

		try
		{
			$error = null;

			if (!$this->isValidUser($error))
			{
				throw new Exception($error);
			}

			if (!$this->isValidInput($error))
			{
				throw new Exception($error);
			}

			DBstart();

			if (!$this->checkMonitoringTarget())
			{
				throw new Exception('Invalid monitoring target');
			}

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

			if (!empty($GLOBALS['ZBX_MESSAGES']))
			{
				throw new Exception('Internal error');
			}

			DBend(true);
		}
		catch (Throwable $e)
		{
			$this->returnJson([
				'code'     => $e->getCode(),
				'message'  => $e->getMessage(),
				'file'     => $e->getFile(),
				'line'     => $e->getLine(),
				'trace'    => explode("\n", $e->getTraceAsString()),
				'messages' => $GLOBALS['ZBX_MESSAGES'],
			]);

			DBend(false);
		}

		restore_error_handler();
	}

	protected function handleGetRequest() {
		if ($this->hasInput('id'))
		{
			$data = $this->getObjects($this->getInput('id'));

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

	protected function handleDeleteRequest() {
		// TODO: check if object exists
		// TODO: 

		$this->deleteObject();

		$this->requestConfigCacheReload();

		$this->returnJson(['foo' => 'bar']);
	}

	protected function handlePutRequest() {
		$this->newObject = $this->getInputAll();

		$objects = $this->getObjects($this->newObject['id']);

		if (empty($objects))
		{
			$this->createObject();
		}
		else
		{
			$this->oldObject = $objects[0];
			$this->updateObject();
		}

		$this->requestConfigCacheReload();

		$data = $this->getObjects($this->newObject['id']);
		$data = $data[0];
		$this->returnJson($data);
	}

	protected function returnJson(array $json) {
		$options = JSON_UNESCAPED_SLASHES;

		if (!is_int(key($json)))
		{
			$options |= JSON_PRETTY_PRINT;
		}

		$this->setResponse(new CControllerResponseData([
			'main_block' => json_encode($json, $options)
		]));
	}
}
