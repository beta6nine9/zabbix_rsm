<?php
// Zabbix GUI configuration file.
global $DB;

// Default db settings.
$DB = [
	'SERVER'			=> '127.0.0.1',
	'PORT'				=> '33006',
	'ENCRYPTION'		=> false,
	'TYPE'				=> 'MYSQL',
	'SCHEMA'			=> '',
	'USER'				=> 'root',
	'PASSWORD'			=> 'zabbix',
	'DB_CONN_TIMEOUT'	=> 5,
];

$ZBX_SERVER				= 'localhost';
$ZBX_SERVER_PORT		= '10051';
$ZBX_SERVER_NAME		= '';
$IMAGE_FORMAT_DEFAULT	= IMAGE_FORMAT_PNG;

$DB['SERVERS'] = [
	[
		'NR' => '1',
		'NAME' => 'local-server',
		'DATABASE' => 'rsm.5.0',
		'URL' => 'http://local.ica/dev-upgrade4/ui/',
//		'DB_KEY_FILE' => null,
//		'DB_CERT_FILE' => null,
//		'DB_CA_FILE' => null,
//		'DB_CA_PATH' => null,
//		'DB_CA_CIPHER' => null
	],
	// [
	// 	'NR' => '2',
	// 	'NAME' => 'db-slam1',
	// 	'DATABASE' => 'icann-dev-upgrade4-2',
	// 	'USER' => 'root',
	// 	'PASSWORD' => 'zabbix',
	// 	'SCHEMA' => '',
	// 	'URL' => 'http://db-slam1.ica/dev-upgrade4/ui/',
	// ],
	// [
	// 	'NR' => '3',
	// 	'NAME' => 'db-slam2',
	// 	'SERVER' => 'localhost',
	// 	'PORT' => '33006',
	// 	'DATABASE' => 'icann-ica616',
	// 	'USER' => 'root',
	// 	'PASSWORD' => 'zabbix',
	// 	'SCHEMA' => '',
	// 	'URL' => 'http://db-slam2.ica/dev-upgrade4/ui/'
	// ]
];

$DB['SERVERS'] = array_column($DB['SERVERS'], null, 'NR');

foreach ($DB['SERVERS'] as &$server) {
	$server += $DB;
}
unset($server);

/**
 * Multiple servers emulatioin via apache SetEnv ZBX_WEB_CONF
 * If environment variable is set overwriting already defined data for $DB, $ZBX_SERVER_NAME
 */
if (getenv('ZBX_WEB_NAME') !== false) {
	$ZBX_SERVER_NAME = getenv('ZBX_WEB_NAME');
	header('X-ICANN: '.$ZBX_SERVER_NAME);

	foreach ($DB['SERVERS'] as &$server) {
		if ($server['NAME'] !== $ZBX_SERVER_NAME) {
			continue;
		}

		$DB = array_merge($DB, $server);

		break;
	}
}
