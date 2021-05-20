<?php

require_once('constants.php');
require_once('App.php');
require_once('Database.php');
require_once('Input.php');
require_once('RsmException.php');
require_once('User.php');

function main(): void
{
	$app = new App();
	$app->setErrorHandler();

	try
	{
		User::validate();
		Input::validate();
		handleRequest();
	}
	catch (RsmException $e)
	{
		$details = $e->getDetails();

		if (is_null($details) && $e->getResultCode() === 500)
		{
			$details = getExceptionDetails($e);
		}

		setCommonResponse(
			$e->getResultCode(),
			$e->getTitle(),
			$e->getDescription(),
			$details,
			$e->getUpdatedObject()
		);
	}
	catch (Throwable $e)
	{
		setCommonResponse(500, 'General error', $e->getMessage(), getExceptionDetails($e), null);
	}
}

function getExceptionDetails(Throwable $e)
{
	$details = [];

	$details['exception'] = get_class($e);
	if ($e instanceof ErrorException)
	{
		$details['severity'] = getSeverityString($e->getSeverity());
	}
	$details['code']     = $e->getCode();
	$details['file']     = $e->getFile();
	$details['line']     = $e->getLine();
	$details['trace']    = explode("\n", $e->getTraceAsString());
	$details['messages'] = [];

	return $details;
}

function getSeverityString(int $severity): string
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

function setCommonResponse(int $resultCode, string $title, ?string $description, ?array $details, ?array $updatedObject)
{
	// must be kept in sync with frontends
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

	sendResponse(
		$resultCode,
		[
			'resultCode'    => $resultCode,
			'title'         => $title,
			'description'   => $description,
			'details'       => $details,
			'updatedObject' => $updatedObject,
		]
	);
}

function sendResponse(int $resultCode, array $json)
{
	$options = JSON_UNESCAPED_SLASHES;

	if (App::getConfig('settings')['prettify_output_list'])
	{
		$options |= JSON_PRETTY_PRINT;
	}
	if (App::getConfig('settings')['prettify_output_object'] && !is_int(key($json)))
	{
		$options |= JSON_PRETTY_PRINT;
	}

	$output = json_encode($json, $options);

	$output = preg_replace('/\{[\s\r\n]*("ns": "[\w.]+"),[\s\r\n]*("ip": "[\w.:]+")[\s\r\n]*\}/', '{ $1, $2 }', $output);
	$output = preg_replace('/\{[\s\r\n]*("service": "\w+"),[\s\r\n]*("enabled": \w+)[\s\r\n]*\}/', '{ $1, $2 }', $output);

	http_response_code($resultCode);
	header('Content-Type: application/json');
	header('Cache-Control: no-store');
	echo $output;
}

function handleRequest(): void
{
	$objectType = Input::getObjectType();
	$objectId   = Input::getObjectId();

	switch ($_SERVER['REQUEST_METHOD'])
	{
		case REQUEST_METHOD_GET:
			handleGetRequest($objectType, $objectId);
			break;

		case REQUEST_METHOD_DELETE:
			handleDeleteRequest($objectType, $objectId);
			break;

		case REQUEST_METHOD_PUT:
			handlePutRequest($objectType, $objectId, Input::getPayload());
			break;

		default:
			// this should have been handled by User::validate()
			throw new RsmException(500, 'General error');
	}
}

function handleGetRequest(string $objectType, ?string $objectId)
{
	if (is_null($objectId))
	{
		forwardRequestMulti($objectType);
	}
	else
	{
		$serverId = findObject($objectType, $objectId);

		if (is_null($serverId))
		{
			throw new RsmException(404, 'The object does not exist in Zabbix');
		}

		forwardRequest($serverId, $objectType, $objectId, REQUEST_METHOD_GET, null);
	}
}

function handleDeleteRequest(string $objectType, ?string $objectId)
{
	if (is_null($objectId))
	{
		throw new RsmException(500, 'General error');
	}

	$serverId = findObject($objectType, $objectId);

	if (is_null($serverId))
	{
		throw new RsmException(404, 'The object does not exist in Zabbix');
	}

	forwardRequest($serverId, $objectType, $objectId, REQUEST_METHOD_DELETE, null);
}

function handlePutRequest(string $objectType, ?string $objectId, string $payload)
{
	if (is_null($objectId))
	{
		throw new RsmException(500, 'General error');
	}

	$json = json_decode($payload, true);
	if (is_null($json))
	{
		// TODO: add python helper for getting some more info
		throw new RsmException(400, 'JSON syntax is invalid');
	}

	$serverIdRequested = array_key_exists('centralServer', $json) ? $json['centralServer'] : null;
	$serverIdActual = findObject($objectType, $objectId);

	unset($json['centralServer']);
	$payload = json_encode($json, JSON_UNESCAPED_SLASHES);

	if (is_null($serverIdRequested))
	{
		if ($objectType === 'probeNodes')
		{
			throw new RsmException(400, 'The centralServer must be specified.');
		}
	}
	else
	{
		if (!array_key_exists($serverIdRequested, App::getConfig('frontends')))
		{
			throw new RsmException(400, 'The centralServer does not exist in the system.');
		}
		if (!is_null($serverIdActual) && $serverIdRequested !== $serverIdActual)
		{
			throw new RsmException(400, 'The centralServer specified for the object is not the same as in the system.');
		}
	}

	if (is_null($serverIdActual))
	{
		$serverIds = is_null($serverIdRequested) ? array_keys(App::getConfig('frontends')) : [$serverIdRequested];

		$counts = getObjectCounts($serverIds, $objectType);
		$serverId = array_search(min($counts), $counts);
		$maxCount = getMaxObjectCount($objectType);

		if ($counts[$serverId] >= $maxCount)
		{
			throw new RsmException(400, 'The maximum number of objects has been reached in the central server', 'Maximum number: ' . $maxCount);
		}

		forwardRequest($serverId, $objectType, $objectId, REQUEST_METHOD_PUT, $payload);
	}
	else
	{
		forwardRequest($serverIdActual, $objectType, $objectId, REQUEST_METHOD_PUT, $payload);
	}
}

function forwardRequest(int $serverId, string $objectType, ?string $objectId, string $method, ?string $payload): void
{
	$ch = createCurlHandle($serverId, $objectType, $objectId, $method, $payload);

	$output = curl_exec($ch);

	if ($output === false)
	{
		throw new RsmException(500, 'General error', 'curl_exec() failed: ' . curl_error($ch));
	}

	list($head, $body) = explode("\r\n\r\n", $output, 2);

	$contentType  = curl_getinfo($ch, CURLINFO_CONTENT_TYPE);
	$responseCode = curl_getinfo($ch, CURLINFO_RESPONSE_CODE);
	//$privateData  = curl_getinfo($ch, CURLINFO_PRIVATE);

	curl_close($ch);

	if ($contentType !== 'application/json')
	{
		$details = [
			'server-id'       => $serverId,
			'url'             => curl_getinfo($ch, CURLINFO_EFFECTIVE_URL),
			'content-type'    => $contentType,
			'response-code'   => $responseCode,
			'message-headers' => explode("\r\n", $head),
			'message-body'    => $body,
		];
		throw new RsmException(500, 'General error', 'Frontend returned unexpected Content-Type', $details);
	}

	$json = json_decode($body, true);
	if (is_null($json))
	{
		// TODO: add python helper for getting some more info
		throw new RsmException(500, 'General error', 'Cannot parse frontend\'s response');
	}

	if (array_key_exists('resultCode', $json))
	{
		$json['details']['centralServer'] = $serverId;
	}
	else
	{
		$json['centralServer'] = $serverId;
	}

	sendResponse($responseCode, $json);
}

function createCurlMultiHandle(array $chList)//: resource
{
	$mh = curl_multi_init();
	if ($mh === false)
	{
		throw new RsmException(500, 'General error', 'curl_multi_init() failed');
	}

	foreach ($chList as $ch)
	{
		$res = curl_multi_add_handle($mh, $ch);

		if ($res !== CURLM_OK)
		{
			$descr = sprintf('curl_multi_add_handle() failed: %s (%d)', curl_multi_strerror($res), $res);
			throw new RsmException(500, 'General error', $descr);
		}
	}

	return $mh;
}

function execCurlMultiHandle($mh): void
{
	$active = 1;

	while ($active)
	{
		$res = curl_multi_exec($mh, $active);

		if ($res !== CURLM_OK)
		{
			$descr = sprintf('curl_multi_exec() failed: %s (%d)', curl_multi_strerror($res), $res);
			throw new RsmException(500, 'General error', $descr);
		}

		if ($active)
		{
			$res = curl_multi_select($mh);

			if ($res === -1)
			{
				throw new RsmException(500, 'General error', 'curl_multi_select() failed');
			}
		}
	}

	// undocumented: calling curl_multi_info_read() makes it possible to read errors with curl_errno() and curl_error()
	while (curl_multi_info_read($mh) !== false)
	{
	}
}

/**
 * Forwards GET request to all frontends and joins all responses into a single response.
 *
 * @param string $objectType
 *
 * @return void
 *
 * @throws RsmException
 */
function forwardRequestMulti(string $objectType): void
{
	$serverIds = array_keys(App::getConfig('frontends'));

	$chList = [];

	foreach ($serverIds as $serverId)
	{
		$chList[] = createCurlHandle($serverId, $objectType, null, REQUEST_METHOD_GET, null);
	}

	$mh = createCurlMultiHandle($chList);

	execCurlMultiHandle($mh);

	$objects = [];

	foreach ($chList as $ch)
	{
		$error          = null;
		$messageHeaders = null;
		$messageBody    = null;
		$contentType    = curl_getinfo($ch, CURLINFO_CONTENT_TYPE);
		$responseCode   = curl_getinfo($ch, CURLINFO_RESPONSE_CODE);
		$serverId       = (int)curl_getinfo($ch, CURLINFO_PRIVATE);

		// curl_getinfo() should have returned null if server did not send valid Content-Type, but somehow it returned false
		if ($contentType === false)
		{
			$contentType = null;
		}

		$errNo = curl_errno($ch);

		if (is_null($error) && $errNo)
		{
			$error = 'Curl failed: ' . curl_strerror($errNo) . ': ' . curl_error($ch);
		}
		else
		{
			$output = curl_multi_getcontent($ch);
			list($messageHeaders, $messageBody) = explode("\r\n\r\n", $output, 2);
			$json = json_decode($messageBody, true);
		}

		if (is_null($error) && $contentType !== 'application/json')
		{
			$error = 'Frontend returned unexpected Content-Type';
		}
		if (is_null($error) && $responseCode !== 200)
		{
			$error = 'Frontend returned unexpected response code';
		}
		if (is_null($error) && is_null($json))
		{
			// TODO: add python helper for getting some more info
			$error = 'Cannot parse frontend\'s response';
		}
		if (is_null($error) && count($json) > 0 && !is_int(key($json)))
		{
			$error = 'Unexpected response from frontend, expected array, got object';
		}

		if (!is_null($error))
		{
			$details = [
				'server-id'       => $serverId,
				'url'             => curl_getinfo($ch, CURLINFO_EFFECTIVE_URL),
				'content-type'    => $contentType,
				'response-code'   => $responseCode,
				'message-headers' => is_null($messageHeaders) ? null : explode("\r\n", $messageHeaders),
				'message-body'    => $messageBody,
			];

			throw new RsmException(500, 'General error', $error, $details);
		}

		foreach ($json as &$object)
		{
			$object['centralServer'] = $serverId;
		}
		unset($object);

		$objects = array_merge($objects, $json);
	}

	foreach ($chList as $ch)
	{
		$res = curl_multi_remove_handle($mh, $ch);

		if ($res !== CURLM_OK)
		{
			$descr = sprintf('curl_multi_add_handle() failed: %s (%d)', curl_multi_strerror($res), $res);
			throw new RsmException(500, 'General error', $descr);
		}

		curl_close($ch);
	}

	curl_multi_close($mh);

	usort(
		$objects,
		function(array $a, array $b) use ($objectType): int
		{
			switch ($objectType)
			{
				case OBJECT_TYPE_TLDS:
					return strcmp($a['tld'], $b['tld']);

				case OBJECT_TYPE_REGISTRARS:
					return $a['registrar'] <=> $b['registrar'];

				case OBJECT_TYPE_PROBES:
					return strcmp($a['probe'], $b['probe']);

				default:
					throw new RsmException(500, 'General error', 'Unexpected object type: ' . $objectType);
			}
		}
	);

	sendResponse($responseCode, $objects);
}

/**
 * Initializes curl handle and sets all options.
 *
 * @param int $serverId
 * @param string $objectType
 * @param string|null $objectId
 * @param string $method
 * @param string|null $payload
 *
 * @return resource
 *
 * @throws RsmException
 */
function createCurlHandle(int $serverId, string $objectType, ?string $objectId, string $method, ?string $payload)//: resource
{
	$ch = curl_init();

	if ($ch === false)
	{
		throw new RsmException(500, 'General error', 'curl_init() failed');
	}

	$url = App::getConfig('frontends')[$serverId]['url'] . '/zabbix.php';
	$url .= '?action=' . curl_escape($ch, FRONTEND_ACTIONS[$objectType]);
	if (!is_null($objectId))
	{
		$url .= '&id=' . curl_escape($ch, $objectId);
	}

	$options = [
		CURLOPT_URL            => $url,
		CURLOPT_HTTPAUTH       => CURLAUTH_BASIC,
		CURLOPT_USERNAME       => User::getUsername(),
		CURLOPT_PASSWORD       => User::getPpassword(),
		CURLOPT_RETURNTRANSFER => true,
		CURLOPT_HEADER         => true,
		CURLOPT_HTTPHEADER     => ['Expect:'],
		CURLOPT_ENCODING       => '',
		CURLOPT_CONNECTTIMEOUT => App::getConfig('settings')['curl_timeout'],
		CURLOPT_PRIVATE        => $serverId,
	];

	if ($method === REQUEST_METHOD_DELETE)
	{
		$options += [
			CURLOPT_CUSTOMREQUEST => 'DELETE',
		];
	}

	if ($method === REQUEST_METHOD_PUT)
	{
		$fh = fopen('php://memory', 'rw');
		fwrite($fh, $payload);
		rewind($fh);

		$options += [
			CURLOPT_PUT        => true,
			CURLOPT_INFILE     => $fh,
			CURLOPT_INFILESIZE => strlen($payload),
		];
	}

	$ret = curl_setopt_array($ch, $options);

	if ($ret === false)
	{
		throw new RsmException(500, 'General error', 'curl_setopt_array() failed: ' . curl_error($ch));
	}

	return $ch;
}

/**
 * Finds serverId where the object is onboarded.
 *
 * @param string $objectType
 * @param string|null $objectId
 *
 * @return int|null
 */
function findObject(string $objectType, string $objectId): ?int
{
	$sql = 'select' .
				' 1' .
			' from' .
				' hosts' .
				' inner join hosts_groups on hosts_groups.hostid = hosts.hostid' .
				' inner join hstgrp on hstgrp.groupid = hosts_groups.groupid' .
			' where' .
				' hosts.host = ? and' .
				' hstgrp.name = ?';

	$hostGroup = ($objectType === OBJECT_TYPE_TLDS || $objectType === OBJECT_TYPE_REGISTRARS) ? 'TLDs' : 'Probes';

	$config = App::getConfig('databases');

	$serverIds = [];

	foreach ($config as $serverId => $serverConfig)
	{
		$db = new Database($serverConfig);
		$rows = $db->select($sql, [$objectId, $hostGroup]);
		unset($db);

		if ($rows)
		{
			$serverIds[] = $serverId;
		}
	}

	switch (count($serverIds))
	{
		case 0:
			return null;

		case 1:
			return $serverIds[0];

		default:
			throw new RsmException(500, 'General error', 'Found object on multiple servers: ' . implode(', ', $serverIds));
	}
}

/**
 * Returns number of objects on all specified servers.
 *
 * @param array $serverIds
 * @param string $objectType
 *
 * @return array
 */
function getObjectCounts(array $serverIds, string $objectType): array
{
	$sql = 'select' .
				' count(*)' .
			' from' .
				' hstgrp' .
				' inner join hosts_groups on hosts_groups.groupid = hstgrp.groupid' .
			' where' .
				' hstgrp.name = ?';

	$hostGroup = ($objectType === OBJECT_TYPE_TLDS || $objectType === OBJECT_TYPE_REGISTRARS) ? 'TLDs' : 'Probes';

	$config = App::getConfig('databases');

	$counts = [];

	foreach ($serverIds as $serverId)
	{
		$db = new Database($config[$serverId]);
		$counts[$serverId] = $db->selectValue($sql, [$hostGroup]);
		unset($db);
	}

	return $counts;
}

/**
 * Returns maximum number of objects.
 *
 * @param string $objectType
 *
 * @return int
 *
 * @throws RsmException
 */
function getMaxObjectCount(string $objectType): int
{
	$config = App::getConfig('settings');

	switch ($objectType)
	{
		case OBJECT_TYPE_TLDS:
			return $config['max_tlds'];

		case OBJECT_TYPE_REGISTRARS:
			return $config['max_registrars'];

		case OBJECT_TYPE_PROBES:
			return $config['max_probes'];

		default:
			throw new RsmException(500, 'General error', 'Unsupported $objectType: ' . $objectType);
	}
}

main();
