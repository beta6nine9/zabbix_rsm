<?php

require_once('constants.php');
require_once('RsmException.php');

class User
{
	// TODO: move usernames and passwords to config.php

	// must be kept in sync with frontends
	private const USER_READONLY_USERNAME  = 'provisioning-api-readonly';
	private const USER_READWRITE_USERNAME = 'provisioning-api-readwrite';
	private const USER_READONLY_PASSWORD  = '$2y$10$nD9fJYeWFsktv7wOMcg52Ob0LRgwrj4JWevPpemErLUo78FDp7WXK'; # php -r 'echo password_hash($argv[1], PASSWORD_BCRYPT, ["cost" => 10]) . "\n";' -- 'password'
	private const USER_READWRITE_PASSWORD = '$2y$10$nD9fJYeWFsktv7wOMcg52Ob0LRgwrj4JWevPpemErLUo78FDp7WXK'; # php -r 'echo password_hash($argv[1], PASSWORD_BCRYPT, ["cost" => 10]) . "\n";' -- 'password'

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

		if (!(self::$username == self::USER_READONLY_USERNAME && password_verify(self::$password, self::USER_READONLY_PASSWORD) === true) &&
			!(self::$username == self::USER_READWRITE_USERNAME && password_verify(self::$password, self::USER_READWRITE_PASSWORD) === true))
		{
			throw new RsmException(401, 'Invalid username or password');
		}

		switch ($_SERVER['REQUEST_METHOD'])
		{
			case REQUEST_METHOD_GET:
				if (self::$username !== self::USER_READWRITE_USERNAME && self::$username !== self::USER_READONLY_USERNAME)
				{
					throw new RsmException(403, 'Forbidden');
				}
				break;

			case REQUEST_METHOD_DELETE:
				if (self::$username !== self::USER_READWRITE_USERNAME)
				{
					throw new RsmException(403, 'Forbidden');
				}
				break;

			case REQUEST_METHOD_PUT:
				if (self::$username !== self::USER_READWRITE_USERNAME)
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
