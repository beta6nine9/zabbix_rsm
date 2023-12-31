<?php

require_once('constants.php');
require_once('Input.php');
require_once('RsmException.php');

class User
{
	private static string $user;
	private static string $username;
	private static string $password;

	public static function initialize(): void
	{
		if (!isset($_SERVER['PHP_AUTH_USER']) || $_SERVER['PHP_AUTH_USER'] === '')
		{
			throw new RsmException(401, 'Username is not specified');
		}
		if (!isset($_SERVER['PHP_AUTH_PW']) || $_SERVER['PHP_AUTH_USER'] === '')
		{
			throw new RsmException(401, 'Password is not specified');
		}

		self::$username = $_SERVER['PHP_AUTH_USER'];
		self::$password = $_SERVER['PHP_AUTH_PW'];
	}

	public static function validate(): void
	{
		self::validateCredentials();
		self::validatePermissions();
	}

	public static function getUsername(): string
	{
		return self::$username;
	}

	public static function getPassword(): string
	{
		return self::$password;
	}

	private static function validateCredentials(): void
	{
		$config = getConfig('users');

		foreach ($config as $user => $credentials)
		{
			if (self::$username == $credentials['username'] && password_verify(self::$password, $credentials['password']) === true)
			{
				self::$user = $user;
				break;
			}
		}

		if (!isset(self::$user))
		{
			throw new RsmException(401, 'Invalid username or password');
		}
	}

	private static function validatePermissions(): void
	{
		$valid = false;

		$permissions = getConfig('users', self::$user, 'permissions');

		foreach ($permissions as $endpointPattern => $requestMethods)
		{
			if (preg_match('#^' . $endpointPattern . '$#', Input::getEndpoint()) === 1)
			{
				if (in_array($_SERVER['REQUEST_METHOD'], $requestMethods))
				{
					$valid = true;
				}
				break;
			}
		}

		if (!$valid)
		{
			throw new RsmException(403, 'Forbidden');
		}
	}
}
