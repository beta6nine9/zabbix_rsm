<?php

require_once('constants.php');
require_once('RsmException.php');

class Input
{
	private static string $objectType;
	private static ?string $objectId;

	public static function validate()
	{
		$urlBase = dirname($_SERVER['SCRIPT_NAME']) . '/';
		$url = $_SERVER['REQUEST_URI'];

		if (strncmp($url, $urlBase, strlen($urlBase)))
		{
			$descr = 'Failed to parse URL, SCRIPT_NAME: "' . $_SERVER['SCRIPT_NAME'] . '", REQUEST_URI: "' . $_SERVER['REQUEST_URI'] . '"';
			throw new RsmException(500, 'General error', $descr);
		}

		$urlComponents = parse_url(substr($url, strlen($urlBase)));

		$endPoint = explode('/', $urlComponents['path']);

		if (count($endPoint) > 2)
		{
			throw new RsmException(400, 'The end-point does not exist');
		}

		self::$objectType = $endPoint[0];
		self::$objectId   = $endPoint[1] ?? null;

		if (!in_array($endPoint[0], [OBJECT_TYPE_TLDS, OBJECT_TYPE_REGISTRARS, OBJECT_TYPE_PROBES]))
		{
			throw new RsmException(400, 'The end-point does not exist');
		}

		if (array_key_exists('query', $urlComponents))
		{
			throw new RsmException(400, 'The end-point does not support parameters');
		}
	}

	public static function getObjectType(): string
	{
		return self::$objectType;
	}

	public static function getObjectId(): ?string
	{
		return self::$objectId;
	}

	public static function getPayload(): string
	{
		return file_get_contents('php://input');
	}


}
