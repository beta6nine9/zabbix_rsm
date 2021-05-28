<?php

require_once('constants.php');
require_once('RsmException.php');

class Input
{
	public static function validate()
	{
		if (count($_GET) > 2)
		{
			throw new RsmException(500, 'General error');
		}
	}

	public static function getObjectType(): string
	{
		if (!array_key_exists('object_type', $_GET))
		{
			throw new RsmException(500, 'General error');
		}

		$objectType = $_GET['object_type'];

		if (!in_array($objectType, [OBJECT_TYPE_TLDS, OBJECT_TYPE_REGISTRARS, OBJECT_TYPE_PROBES]))
		{
			throw new RsmException(500, 'General error', 'Unsupported object type: ' . $objectType);
		}

		return $objectType;
	}

	public static function getObjectId(): ?string
	{
		$objectId = null;

		if (count($_GET) === 2)
		{
			if (!array_key_exists('id', $_GET))
			{
				throw new RsmException(500, 'General error', 'Object ID not specified');
			}
			$objectId = $_GET['id'];
		}

		return $objectId;
	}

	public static function getPayload(): string
	{
		return file_get_contents('php://input');
	}


}
