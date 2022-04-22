<?php
// Zabbix GUI configuration file.

// $DB['TYPE']				= 'MYSQL';
// $DB['SERVER']			= '127.0.0.1';
// $DB['PORT']				= '33006';
// $DB['DATABASE']			= 'fr1.ica.local';
// $DB['USER']				= 'root';
// $DB['PASSWORD']			= 'zabbix';

// Schema name. Used for PostgreSQL.
// $DB['SCHEMA']			= '';

// Used for TLS connection.
// $DB['ENCRYPTION']		= false;
// $DB['KEY_FILE']			= '';
// $DB['CERT_FILE']		= '';
// $DB['CA_FILE']			= '';
// $DB['VERIFY_HOST']		= false;
// $DB['CIPHER_LIST']		= '';

// Vault configuration. Used if database credentials are stored in Vault secrets manager.
$DB['VAULT_URL']		= '';
$DB['VAULT_DB_PATH']	= '';
$DB['VAULT_TOKEN']		= '';

// Use IEEE754 compatible value range for 64-bit Numeric (float) history values.
// This option is enabled by default for new Zabbix installations.
// For upgraded installations, please read database upgrade notes before enabling this option.
$DB['DOUBLE_IEEE754']	= true;

// Uncomment and set to desired values to override Zabbix hostname/IP and port.
// $ZBX_SERVER				= 'localhost';
// $ZBX_SERVER_PORT		= '{SERVER_PORT}';

$ZBX_SERVER_NAME		= '';

$IMAGE_FORMAT_DEFAULT	= IMAGE_FORMAT_PNG;

// Uncomment this block only if you are using Elasticsearch.
// Elasticsearch url (can be string if same url is used for all types).
//$HISTORY['url'] = [
//	'uint' => 'http://localhost:9200',
//	'text' => 'http://localhost:9200'
//];
// Value types stored in Elasticsearch.
//$HISTORY['types'] = ['uint', 'text'];

// Used for SAML authentication.
// Uncomment to override the default paths to SP private key, SP and IdP X.509 certificates, and to set extra settings.
//$SSO['SP_KEY']			= 'conf/certs/sp.key';
//$SSO['SP_CERT']			= 'conf/certs/sp.crt';
//$SSO['IDP_CERT']		= 'conf/certs/idp.crt';
//$SSO['SETTINGS']		= [];

// $DB['DB_CONN_TIMEOUT']	= 5;

$DB['SERVERS'] = [
	'1' => [
		'NR' => '1',
		'NAME' => 'fr1',
		'SERVER' => '{DBHOST}',
		'TYPE' => '{DBTYPE}',
		'DB_CONN_TIMEOUT' => 5,
		'PORT' => '0',
		'DATABASE' => '{DBNAME}',
		'USER' => '{DBUSER}',
		'PASSWORD' => '{DBPASSWORD}',
		'SCHEMA' => '',
		'URL' => '{URL}',
		'ENCRYPTION'	=> false,
		'KEY_FILE'	=> '',
		'CERT_FILE'	=> '',
		'CA_FILE'	=> '',
		'VERIFY_HOST'	=> false,
		'CIPHER_LIST'	=> '',
	]
];

// Select current server
$DB += $DB['SERVERS']['1'];

// PHP runtime error log file for unit tests.
define('PHPUNIT_ERROR_LOG', '{PHPUNIT_ERROR_LOG}');

if (!defined('PHPUNIT_BASEDIR')) {
	// Runtime error collection block.
	if (!file_exists(PHPUNIT_ERROR_LOG)) {
		file_put_contents(PHPUNIT_ERROR_LOG, '');
		chmod(PHPUNIT_ERROR_LOG, 0666);
	}

	function formatCallStack() {
		$calls = debug_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS);

		// never show the call to this method
		array_shift($calls);

		$functions = [];
		$call_with_file = [];
		$root_dir = realpath(dirname(__FILE__).'/..');

		$calls = array_reverse($calls);
		$first_call = reset($calls);

		foreach ($calls as $call) {
			// do not show the call to the error handler function
			if ($call['function'] != 'zbx_err_handler') {
				if (array_key_exists('class', $call)) {
					$functions[] = $call['class'].$call['type'].$call['function'].'()';
				}
				else {
					$functions[] = $call['function'].'()';
				}
			}

			if (array_key_exists('file', $call)) {
				$call_with_file = $call;
			}
		}

		$call_stack_string = '';

		if ($functions) {
			$call_stack_string .= pathinfo($first_call['file'], PATHINFO_BASENAME).':'.$first_call['line'].' -> ';
			$call_stack_string .= implode(' -> ', $functions);
		}

		if ($call_with_file) {
			$file_name = $call_with_file['file'];

			if (substr_compare($file_name, $root_dir, 0, strlen($root_dir)) === 0) {
				$file_name = substr($file_name, strlen($root_dir) + 1);
			}
			$call_stack_string .= ' in '.$file_name.':'.$call_with_file['line'];
		}

		return $call_stack_string;
	}

	set_error_handler(function ($errno, $errstr, $errfile, $errline) {
		// Check if error control operator was used.
		if (error_reporting() & $errno) {
			file_put_contents(PHPUNIT_ERROR_LOG, $errstr.' ['.formatCallStack()."]\n", FILE_APPEND);
		}

		return zbx_err_handler($errno, $errstr, $errfile, $errline);
	}, E_ALL | E_STRICT);

	set_exception_handler(function ($exception) {
		file_put_contents(PHPUNIT_ERROR_LOG, $exception."\n", FILE_APPEND);
	});
}
