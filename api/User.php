<?php

require_once('constants.php');
require_once('RsmException.php');

class User
{
	private static ?string $username = null;
	private static ?string $password = null;

	public static function validate(): void
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

		$config = getConfig('users');

		if (!(self::$username == $config['readonly']['username'] && password_verify(self::$password, $config['readonly']['password']) === true) &&
			!(self::$username == $config['readwrite']['username'] && password_verify(self::$password, $config['readwrite']['password']) === true))
		{
			throw new RsmException(401, 'Invalid username or password');
		}

		switch ($_SERVER['REQUEST_METHOD'])
		{
			case REQUEST_METHOD_GET:
				if (self::$username !== $config['readwrite']['username'] && self::$username !== $config['readonly']['username'])
				{
					throw new RsmException(403, 'Forbidden');
				}
				break;

			case REQUEST_METHOD_DELETE:
				if (self::$username !== $config['readwrite']['username'])
				{
					throw new RsmException(403, 'Forbidden');
				}
				break;

			case REQUEST_METHOD_PUT:
				if (self::$username !== $config['readwrite']['username'])
				{
					throw new RsmException(403, 'Forbidden');
				}
				break;

			default:
				header('Allow: GET,DELETE,PUT');
				throw new RsmException(405, 'Method Not Allowed');
		}
	}

	public static function getUsername(): string
	{
		return self::$username;
	}

	public static function getPassword(): string
	{
		return self::$password;
	}
}
