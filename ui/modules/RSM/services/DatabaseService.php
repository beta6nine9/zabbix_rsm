<?php

namespace Modules\RSM\Services;

use Exception;

class DatabaseService {

	const SKIP_CURRENT_DB = 0x1;

	/**
	 * Execute callback function for every database server available in $DB['SERVERS']. Code will silently ignore exception
	 * thrown in callback.
	 *
	 * @param callable $callback    Function to be called for every database server.
	 * @param int      $flag        Additional flags.
	 */
	public function exec($callback, $flag) {
		global $DB;

		$error = '';
		$current = $DB;
		$databases = [];

		if ($flag & self::SKIP_CURRENT_DB) {
			foreach ($DB['SERVERS'] as $db) {
				if (array_diff_assoc(array_intersect_key($db, $DB), $DB)) {
					$databases[] = $db;
				}
			}
		}
		else {
			$databases = $DB['SERVERS'];
		}

		foreach ($databases as $server) {
			if (!multiDBconnect($server, $error)) {
				continue;
			}

			try {
				$callback($server);
			}
			catch (Exception $e) {
				// Silently ignore exceptions.
			}
		}

		$DB = $current;
		DBconnect($error);
	}
}
