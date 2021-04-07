<?php

namespace Modules\RsmProvisioningApi\Actions;

require_once __DIR__ . '/../validators/validators.inc.php';

use API;
use CApiInputValidator;
use CController;
use CControllerResponseData;
use Exception;

abstract class ActionBase extends CController {

	private const USER_READONLY  = 'provisioning-api-readonly';
	private const USER_READWRITE = 'provisioning-api-readwrite';

	protected const REQUEST_METHOD_GET     = 'GET';
	protected const REQUEST_METHOD_DELETE  = 'DELETE';
	protected const REQUEST_METHOD_PUT     = 'PUT';

	protected $oldObject = null;
	protected $newObject = null;

	abstract protected function checkMonitoringTarget();
	abstract protected function requestConfigCacheReload();
	abstract protected function getInputRules(): array;
	abstract protected function getObjectIdInputField();
	abstract protected function getObjects(?string $objectId);
	abstract protected function createObject();
	abstract protected function updateObject();
	abstract protected function deleteObject();

	public function __construct() {
		parent::__construct();
		$this->disableSIDvalidation();
	}

	public function __destruct() {
		$stats = $this->getStats();
		$stats = explode("\n", $stats);

		foreach ($stats as $i => $line)
		{
			header(sprintf("Rsm-Stats-%02d: %s", $i, $line));
		}
	}

	protected function getStats() {
		$format = fn($n) => sprintf('%7s', number_format($n, 0, '.', '\''));

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

		$k_len = max(array_map('strlen', array_keys($data)));
		$v_len = max(array_map('strlen', array_values($data)));

		$line = '+' . str_pad('', $k_len + 2, '-') . '+' . str_pad('', $v_len + 2, '-') . '+';

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

	protected function checkPermissions() {
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

	protected function checkInput() {
		return $this->validateInput($this->getInputRules());
	}

	public function validateInput($validationRules) {
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

	protected function getRequestInput(): array {
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

	public function errorHandler(int $errno, string $errstr, string $errfile, int $errline, array $errcontext) {
		throw new \ErrorException($errstr, 0, $errno, $errfile, $errline);
	}

	protected function doAction() {
		set_error_handler([$this, 'errorHandler'], E_ALL | E_STRICT);

		try
		{
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

			DBend(true);
		}
		catch (\Throwable $e)
		{
			$this->returnJson([
				'code'    => $e->getCode(),
				'message' => $e->getMessage(),
				'file'    => $e->getFile(),
				'line'    => $e->getLine(),
				'trace'   => $e->getTraceAsString(),
			]);

			DBend(false);
		}

		restore_error_handler();
	}

	protected function handleGetRequest() {
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

	protected function handleDeleteRequest() {
		// TODO: check if object exists
		// TODO: 

		$this->deleteObject();

		$this->requestConfigCacheReload();

		$this->returnJson(['foo' => 'bar']);
	}

	protected function handlePutRequest() {
		$this->newObject = $this->getInputAll();

		$objects = $this->getObjects($this->getInput($this->getObjectIdInputField()));

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

		$this->returnJson([$this->getInput($this->getObjectIdInputField())]);
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
