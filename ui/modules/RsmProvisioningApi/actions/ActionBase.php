<?php

//declare(strict_types=1); // TODO: enable strict_types

namespace Modules\RsmProvisioningApi\Actions;

require_once __DIR__ . '/../validators/validators.inc.php';

use API;
use CApiInputValidator;
use CController;
use CControllerResponseData;
use DB;
use Exception;
use ErrorException;
use Throwable;
use Modules\RsmProvisioningApi\RsmException;

abstract class ActionBase extends CController
{
	private const USER_READONLY  = 'provisioning-api-readonly';
	private const USER_READWRITE = 'provisioning-api-readwrite';

	protected const REQUEST_METHOD_GET    = 'GET';
	protected const REQUEST_METHOD_DELETE = 'DELETE';
	protected const REQUEST_METHOD_PUT    = 'PUT';

	protected $oldObject = null;
	protected $newObject = null;

	abstract protected function checkMonitoringTarget(): bool;
	abstract protected function requestConfigCacheReload(): void;
	abstract protected function getInputRules(): array;
	abstract protected function isObjectDisabled(array $object): bool;
	abstract protected function getObjects(?string $objectId): array;
	abstract protected function createObject(): void;
	abstract protected function updateObject(): void;
	abstract protected function disableObject(): void;
	abstract protected function deleteObject(): void;

	public function __construct()
	{
		parent::__construct();
		$this->disableSIDvalidation();
	}

	public function __destruct()
	{
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

	protected function getStats(): string
	{
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

	public function errorHandler(int $errno, string $errstr, string $errfile, int $errline): void
	{
		// turn PHP errors, warnings, notices into exceptions
		throw new ErrorException($errstr, 0, $errno, $errfile, $errline);
	}

	final protected function checkPermissions(): bool
	{
		// permissions have to be validated in doAction(), otherwise frontend will output HTML
		return true;
	}

	final protected function checkInput(): bool
	{
		// input has to be validated in doAction(), otherwise frontend will output HTML
		return true;
	}

	final public function validateInput($validationRules): bool
	{
		// normally, validateInput() is supposed to be called from checkInput(), but not in this module
		throw new Exception('[Internal error] ' . __FUNCTION__ . '() should never be called');
	}

	protected function rsmValidateUser(): void
	{
		if (!isset($_SERVER['PHP_AUTH_USER']) || $_SERVER['PHP_AUTH_USER'] === '')
		{
			throw new RsmException(401, 'Username is not specified');
		}
		if (!isset($_SERVER['PHP_AUTH_PW']) || $_SERVER['PHP_AUTH_PW'] === '')
		{
			throw new RsmException(401, 'Password is not specified');
		}

		$username = $_SERVER['PHP_AUTH_USER'];
		$password = $_SERVER['PHP_AUTH_PW'];

		$userData = API::User()->login(['user' => $username, 'password' => $password, 'userData' => true]);

		if ($userData === false)
		{
			throw new RsmException(401, 'Invalid username or password');
		}

		// required hack for API
		API::getWrapper()->auth = $userData['sessionid'];

		DBexecute('DELETE FROM sessions WHERE userid=' . $userData['userid'] . ' AND lastaccess<' . (time() - 12 * SEC_PER_HOUR));

		switch ($_SERVER['REQUEST_METHOD'])
		{
			case self::REQUEST_METHOD_GET:
				if ($username !== self::USER_READWRITE && $username !== self::USER_READONLY)
				{
					throw new RsmException(403, 'Forbidden');
				}
				break;

			case self::REQUEST_METHOD_DELETE:
				if ($username !== self::USER_READWRITE)
				{
					throw new RsmException(403, 'Forbidden');
				}
				break;

			case self::REQUEST_METHOD_PUT:
				if ($username !== self::USER_READWRITE)
				{
					throw new RsmException(403, 'Forbidden');
				}
				break;

			default:
				header('Allow: GET,DELETE,PUT');
				throw new RsmException(405, 'Method Not Allowed');
		}
	}

	protected function rsmValidateInput(): void
	{
		if ($_SERVER['REQUEST_METHOD'] === self::REQUEST_METHOD_PUT)
		{
			array_walk_recursive(
				$this->input,
				function($v, $k)
				{
					if (is_null($v))
					{
						throw new RsmException(400, 'JSON does not comply with definition', 'Value of the "' . $k . '" element is NULL');
					}
				}
			);
		}

		$rules = $this->getInputRules();
		$error = null;

		if (!CApiInputValidator::validate($rules, $this->input, '', $error))
		{
			if (preg_match('/^Invalid parameter "/', $error))
			{
				$error = preg_replace_callback('/(?<=^Invalid parameter ")\/(.*?)(?=")/', fn($m) => str_replace('/', '.', $m[1]), $error);
				throw new RsmException(400, 'JSON does not comply with definition', $error);
			}
			else
			{
				throw new RsmException(400, $error);
			}
		}
	}

	/**
	 * Checks if status is specified in input for all known services, based on input rules.
	 */
	protected function validateInputServices(): void
	{
		$rules = $this->getInputRules();

		$inputServices = array_column($this->input['servicesStatus'], 'service');
		$rulesServices = $rules['fields']['servicesStatus']['fields']['service']['in'];

		$diff = array_diff($rulesServices, $inputServices);
		if ($diff)
		{
			$rulesServicesStr = implode(', ', $rulesServices);
			$rulesServicesStr = preg_replace('/, ([^,]+)$/', ' and $1', $rulesServicesStr);

			$title = 'All services (i.e., ' . $rulesServicesStr . ') must be specified';
			$descr = 'Missing services: ' . implode(', ', $diff);

			throw new RsmException(400, $title, $descr);
		}
	}

	/**
	 * Checks if array contains all required keys.
	 *
	 * @param array $keys
	 * @param array $array
	 * @param string $errorMessage
	 */
	protected function requireArrayKeys(array $keys, array $array, string $errorMessage): void
	{
		$missing = array_diff($keys, array_keys($array));

		if (!empty($missing))
		{
			throw new RsmException(400, $errorMessage);
		}
	}

	/**
	 * Checks if array contains any key that is not allowed.
	 *
	 * @param array $keys
	 * @param array $array
	 * @param string $errorMessage
	 */
	protected function forbidArrayKeys(array $keys, array $array, string $errorMessage): void
	{
		$forbidden = array_intersect($keys, array_keys($array));

		if ($forbidden)
		{
			throw new RsmException(400, $errorMessage);
		}
	}

	/**
	 * Checks if any JSON object has duplicate keys.
	 *
	 * @param string $json
	 *
	 * @return bool
	 */
	private function jsonHasDuplicateKeys(string $json): bool
	{
		$code = "import sys"                                     . "\n"
			  . "import json"                                    . "\n"
			  . ""                                               . "\n"
			  . "def fun(kv_pairs):"                             . "\n"
			  . "    keys = [kv[0] for kv in kv_pairs]"          . "\n"
			  . "    if len(keys) != len(set(keys)):"            . "\n"
			  . "        exit(1)"                                . "\n"
			  . "    return kv_pairs"                            . "\n"
			  . ""                                               . "\n"
			  . "json.loads(sys.argv[1], object_pairs_hook=fun)" . "\n";

		$execCode = escapeshellarg($code);
		$execJson = escapeshellarg($json);

		$output = null;
		$result = null;

		$ret = exec("python -c $execCode $execJson 2>&1", $output, $result);

		if ($ret === false || $result !== 0)
		{
			return true;
		}

		return false;
	}

	private function jsonParsingError(string $json): string
	{
		$code = "import sys"                  . "\n"
			  . "import json"                 . "\n"
			  . ""                            . "\n"
			  . "try:"                        . "\n"
			  . "    json.loads(sys.argv[1])" . "\n"
			  . "except ValueError as e:"     . "\n"
			  . "    print(e.message)"        . "\n";


		$execCode = escapeshellarg($code);
		$execJson = escapeshellarg($json);

		$output = null;

		exec("python -c $execCode $execJson 2>&1", $output);

		return implode("\n", $output);
	}

	protected function getRequestInput(): array
	{
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
				$json = file_get_contents('php://input');
				$input = json_decode($json, true);

				if (is_null($input))
				{
					$descr = json_last_error_msg() . "\n" . $this->jsonParsingError($json);
					throw new RsmException(400, 'JSON syntax is invalid', $descr);
				}
				if ($this->jsonHasDuplicateKeys($json))
				{
					throw new RsmException(400, 'JSON does not comply with definition', 'JSON contains duplicate keys');
				}
				if (array_key_exists('id', $input))
				{
					throw new RsmException(400, 'JSON does not comply with definition');
				}
				if (!array_key_exists('id', $_GET))
				{
					throw new RsmException(500, 'General error');
				}

				$input = ['id' => $_GET['id']] + $input;
				break;

			default:
				throw new RsmException(500, 'General error');
		}

		return $input;
	}

	protected function getSeverityString(int $severity): string
	{
		switch ($severity)
		{
			case E_ERROR:
				return 'E_ERROR';
			case E_WARNING:
				return 'E_WARNING';
			case E_PARSE:
				return 'E_PARSE';
			case E_NOTICE:
				return 'E_NOTICE';
			case E_CORE_ERROR:
				return 'E_CORE_ERROR';
			case E_CORE_WARNING:
				return 'E_CORE_WARNING';
			case E_COMPILE_ERROR:
				return 'E_COMPILE_ERROR';
			case E_COMPILE_WARNING:
				return 'E_COMPILE_WARNING';
			case E_USER_ERROR:
				return 'E_USER_ERROR';
			case E_USER_WARNING:
				return 'E_USER_WARNING';
			case E_USER_NOTICE:
				return 'E_USER_NOTICE';
			case E_STRICT:
				return 'E_STRICT';
			case E_RECOVERABLE_ERROR:
				return 'E_RECOVERABLE_ERROR';
			case E_DEPRECATED:
				return 'E_DEPRECATED';
			case E_USER_DEPRECATED:
				return 'E_USER_DEPRECATED';
			default:
				return (string)$severity;
		}
	}

	protected function doAction(): void
	{
		set_error_handler([$this, 'errorHandler'], E_ALL | E_STRICT);

		try
		{
			$this->rsmValidateUser();

			$this->input = $this->getRequestInput();

			$this->rsmValidateInput();

			DBstart();

			if (!$this->checkMonitoringTarget())
			{
				throw new Exception('Invalid monitoring target');
			}

			$this->handleRequest();

			if (!empty($GLOBALS['ZBX_MESSAGES']))
			{
				throw new Exception('Internal error');
			}

			$this->logSuccess();

			DBend(true);
		}
		catch (RsmException $e)
		{
			$details = $e->getDetails();

			if (is_null($details) && $e->getResultCode() === 500)
			{
				$details = $this->getExceptionDetails($e);
			}

			$this->setCommonResponse(
				$e->getResultCode(),
				$e->getTitle(),
				$e->getDescription(),
				$details,
				$e->getUpdatedObject()
			);

			DBend(false);

			$this->logFailure();
		}
		catch (Throwable $e)
		{
			$this->setCommonResponse(500, 'General error', $e->getMessage(), $this->getExceptionDetails($e), null);

			DBend(false);

			$this->logFailure();
		}

		restore_error_handler();
	}

	private function getExceptionDetails(Throwable $e)
	{
		$details = [];

		$details['exception'] = get_class($e);
		if ($e instanceof ErrorException)
		{
			$details['severity'] = $this->getSeverityString($e->getSeverity());
		}
		$details['code']     = $e->getCode();
		$details['file']     = $e->getFile();
		$details['line']     = $e->getLine();
		$details['trace']    = explode("\n", $e->getTraceAsString());
		$details['messages'] = $GLOBALS['ZBX_MESSAGES'];

		return $details;
	}

	private function handleRequest(): void
	{
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
	}

	private function handleGetRequest(): void
	{
		if ($this->hasInput('id'))
		{
			$data = $this->getObjects($this->getInput('id'));

			if (empty($data))
			{
				throw new RsmException(404, 'The object does not exist in Zabbix');
			}

			$data = $data[0];
		}
		else
		{
			$data = $this->getObjects(null);
		}

		$this->returnJson($data);
	}

	private function handleDeleteRequest(): void
	{
		global $ZBX_MESSAGES;

		$data = $this->getObjects($this->getInput('id'));

		if (empty($data))
		{
			throw new RsmException(404, 'The object does not exist in Zabbix');
		}

		$this->oldObject = $data[0];

		$this->deleteObject();
		$this->requestConfigCacheReload();

		// when deleting objects, API puts informative messages into $ZBX_MESSAGES; move them to $details of the response
		$details = [];
		while (count($ZBX_MESSAGES) > 0 && $ZBX_MESSAGES[0]['type'] === 'info' && preg_match('/^Deleted: /', $ZBX_MESSAGES[0]['message']))
		{
			$message = array_shift($ZBX_MESSAGES);
			$details['info'][] = $message['message'];
		}

		$this->setCommonResponse(200, 'Update executed successfully', null, $details, null);
	}

	private function handlePutRequest(): void
	{
		// TODO: changes to Zabbix shall only be executed if the configuration information changes
		// between the current configuration in Zabbix and the object provided to the API
		global $ZBX_MESSAGES;

		$this->newObject = $this->getInputAll();

		$objects = $this->getObjects($this->newObject['id']);

		if (empty($objects))
		{
			$this->createObject();
			$this->requestConfigCacheReload();
		}
		else
		{
			$this->oldObject = $objects[0];

			if ($this->isObjectDisabled($this->newObject))
			{
				if (!$this->isObjectDisabled($this->oldObject))
				{
					$this->disableObject();
					$this->requestConfigCacheReload();
				}
			}
			else
			{
				$this->updateObject();
				$this->requestConfigCacheReload();
			}
		}

		// when disabling objects, API puts informative messages into $ZBX_MESSAGES; move them to $details of the response
		$details = null;
		while (count($ZBX_MESSAGES) > 0 && $ZBX_MESSAGES[0]['type'] === 'info' && preg_match('/^Updated status of host /', $ZBX_MESSAGES[0]['message']))
		{
			if (is_null($details))
			{
				$details = [];
			}
			$message = array_shift($ZBX_MESSAGES);
			$details['info'][] = $message['message'];
		}

		$objects = $this->getObjects($this->newObject['id']);
		$this->setCommonResponse(200, 'Update executed successfully', null, $details, $objects[0]);
	}

	private function logSuccess(): void
	{
		if (in_array($_SERVER['REQUEST_METHOD'], [self::REQUEST_METHOD_PUT, self::REQUEST_METHOD_DELETE]))
		{
			$operation = null;
			$objectType = null;

			if (is_null($this->oldObject) && !is_null($this->newObject))
			{
				$operation = 'add';
			}
			else if (!is_null($this->oldObject) && !is_null($this->newObject))
			{
				$operation = 'update';
			}
			else if (!is_null($this->oldObject) && is_null($this->newObject))
			{
				$operation = 'delete';
			}

			if ($this instanceof Tld)
			{
				$objectType = 'tld';
			}
			else if ($this instanceof Registrar)
			{
				$objectType = 'registrar';
			}
			else if ($this instanceof Probe)
			{
				$objectType = 'probeNode';
			}

			$values = [
				'provisioning_api_logid' => DB::reserveIds('provisioning_api_log', 1),
				'clock'                  => $_SERVER['REQUEST_TIME'],
				'user'                   => $_SERVER['PHP_AUTH_USER'],
				'interface'              => 'internal',
				'identifier'             => $this->getInput('id'),
				'operation'              => $operation,
				'object_type'            => $objectType,
				'object_before'          => is_null($this->oldObject) ? null : json_encode($this->oldObject, JSON_UNESCAPED_SLASHES),
				'object_after'           => is_null($this->newObject) ? null : json_encode($this->newObject, JSON_UNESCAPED_SLASHES),
				'remote_addr'            => $_SERVER['REMOTE_ADDR'],
				'x_forwarded_for'        => array_key_exists('HTTP_X_FORWARDED_FOR', $_SERVER) ? $_SERVER['HTTP_X_FORWARDED_FOR'] : null,
			];

			DB::insert('provisioning_api_log', [$values], false);
		}
	}

	private function logFailure()
	{
		openlog('ProvisioningAPI', LOG_CONS | LOG_NDELAY | LOG_PID | LOG_PERROR, LOG_LOCAL0);

		$this->log($_SERVER['REQUEST_METHOD'] . ' ' . $_SERVER['REQUEST_URI']);

		$input = file_get_contents('php://input');

		if ($input !== '')
		{
			$this->log('');
			$this->log('INPUT:');
			$this->log($input);
		}

		$output = $this->getResponse()->getData()['main_block'];

		if ($output)
		{
			$this->log('');
			$this->log('OUTPUT:');
			$this->log($output);
		}

		closelog();
	}

	private function log(string $message): void
	{
		static $rand = null;

		if (is_null($rand))
		{
			$rand = rand(0x10000000, 0xFFFFFFFF);
		}

		$ts = date('Y-m-d H:i:s');

		$lines = explode("\n", trim($message));

		if (count($lines) === 1)
		{
			$line = sprintf('[%s] [%08x] %s', $ts, $rand, $lines[0]);
			syslog(LOG_ERR, $line);
		}
		else
		{
			foreach ($lines as $i => $line)
			{
				$line = sprintf('[%s] [%08x] %03d: %s', $ts, $rand, $i + 1, $line);
				syslog(LOG_ERR, $line);
			}
		}
	}

	protected function returnJson(array $json): void
	{
		$options = JSON_UNESCAPED_SLASHES;

		if (!is_int(key($json)))
		{
			$options |= JSON_PRETTY_PRINT;
		}

		$output = json_encode($json, $options);

		$output = preg_replace('/\{[\s\r\n]*("ns": "[\w.]+"),[\s\r\n]*("ip": "[\w.:]+")[\s\r\n]*\}/', '{ $1, $2 }', $output);
		$output = preg_replace('/\{[\s\r\n]*("service": "\w+"),[\s\r\n]*("enabled": \w+)[\s\r\n]*\}/', '{ $1, $2 }', $output);

		$this->setResponse(new CControllerResponseData([
			'main_block' => $output
		]));
	}

	protected function setCommonResponse(int $resultCode, string $title, ?string $description, ?array $details, ?array $updatedObject)
	{
		$supportedResultCodes = [
			200, // OK
			400, // Bad Request
			401, // Unauthorized
			403, // Forbidden
			404, // Not Found
			500, // Internal Server Error
		];

		if (!in_array($resultCode, $supportedResultCodes))
		{
			throw new Exception("Result code '$resultCode' is not supported");
		}

		http_response_code($resultCode);

		$this->returnJson([
			'resultCode'    => $resultCode,
			'title'         => $title,
			'description'   => $description,
			'details'       => $details,
			'updatedObject' => $updatedObject,
		]);
	}
}
