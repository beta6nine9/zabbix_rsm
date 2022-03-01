<?php

namespace Modules\RSM\Helpers;

use CUrl;
use CWebUser;

class UrlHelper {

	/**
	 * Get URL to specified $action.
	 *
	 * @param string $action    Action name, or php file name to be called.
	 * @param array  $params    Action params.
	 */
	static public function get(string $action, array $params = []): string {
		if (substr($action, -4) !== '.php') {
			$params['action'] = $action;
			$action = 'zabbix.php';
		}

		$url = new CUrl($action);

		foreach ($params as $name => $value) {
			$url->setArgument($name, $value);
		}

		return $url->getUrl();
	}

	/**
	 * Get link to action on remote RSM server.
	 *
	 * @param string $server_url    Host name of remote RSM server.
	 * @param string $action        Action to redirect to on remote RSM server.
	 * @param array  $params        Array of $action params.
	 */
	static public function getFor(string $host, string $action, array $params = []): string {
		$sid = CWebUser::$data['sessionid'];

		return static::get($host.'zabbix.php', [
			'i' => $sid,
			's' => static::sidLoginSignature($sid),
			'back' => static::get($action, $params),
			'action' => 'rsm.sidlogin'
		]);
	}

	/**
	 * Get sid signature for link to another RSM server.
	 *
	 * @param string $original    String to be signed.
	 */
	static public function sidLoginSignature(string $original): string {
		return md5($original.RSM_SECRET_KEY);
	}
}
