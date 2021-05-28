<?php

class App
{
	public function __construct()
	{
	}

	public function __destruct() {
	}

	public function setErrorHandler()
	{
		set_error_handler(
			function(int $errno, string $errstr, string $errfile, int $errline): void
			{
				// turn PHP errors, warnings, notices into exceptions
				throw new ErrorException($errstr, 0, $errno, $errfile, $errline);
			},
			E_ALL | E_STRICT
		);
	}

	public static function getConfig(string $section): array
	{
		static $config = null;

		if (is_null($config))
		{
			$config = require('config.php');
		}

		return $config[$section];
	}

}
