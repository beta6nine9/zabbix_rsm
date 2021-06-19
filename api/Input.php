<?php

require_once('constants.php');
require_once('RsmException.php');

class Input
{
	private static string $objectType;
	private static ?string $objectId;

	public static function validate(): void
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

		switch (self::$objectType)
		{
			case OBJECT_TYPE_TLDS:
				if (!is_null(self::$objectId) && !self::isValidTldId(self::$objectId))
				{
					throw new RsmException(400, 'A valid DNS label was not provided in the TLD field in the URL');
				}
				break;

			case OBJECT_TYPE_REGISTRARS:
				if (!is_null(self::$objectId) && !self::isValidRegistrarId(self::$objectId))
				{
					throw new RsmException(400, 'The IANAID must be a positive integer in the URL');
				}
				break;

			case OBJECT_TYPE_PROBES:
				if (!is_null(self::$objectId) && !self::isValidProbeId(self::$objectId))
				{
					throw new RsmException(400, 'The syntax of the probe node in the URL is invalid');
				}
				break;

			default:
				throw new RsmException(400, 'The end-point does not exist');
		}

		if (array_key_exists('query', $urlComponents))
		{
			throw new RsmException(400, 'The end-point does not support parameters');
		}
	}

	private static function isValidTldId(string $id): bool
	{
		// Must be kept in sync with checks in module's RsmValidateTldIdentifier().

		// allow "." TLD
		if ($id === '.')
		{
			return true;
		}
		// trim trailing "."
		if (mb_substr($id, -1) === '.')
		{
			$id = mb_substr($id, 0, -1);
		}
		// min length 2, max length 63, may contain only a-z,0-9,'-', must start and end with a-z,0-9
		if (!preg_match('/^[a-z0-9][a-z0-9-]{0,61}[a-z0-9]$/', $id))
		{
			return false;
		}
		// if 3rd and 4th characters are '--', then 1st and 2nd characters must be 'xn' (i.e., 'xn--')
		if (($id[0] != 'x' || $id[1] != 'n') && $id[2] == '-' && $id[3] == '-')
		{
			return false;
		}
		return true;
	}

	private static function isValidRegistrarId(string $id): bool
	{
		// Must be kept in sync with module's check.

		return preg_match('/^[1-9][0-9]*$/', $id);
	}

	private static function isValidProbeId(string $id): bool
	{
		// Must be kept in sync with checks in module's RsmValidateProbeIdentifier().

		return preg_match('/^[a-zA-Z0-9_\-]+$/', $id);
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
