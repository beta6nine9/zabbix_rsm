<?php

namespace Modules\RSM\Security;

use CWebUser;

/**
 * User permissions filter service.
 */
class Permission {

	// System routes allowed to any user: ajax, server switch and etc.
	const ALL_ALLOWED = [
		'index.php', 'profile.update', 'rsm.sidlogin', 'timeselector.update', 'jsrpc.php',
		'chart.php', 'chart2.php', 'chart3.php', 'chart4.php', 'chart5.php', 'chart6.php', 'chart7.php',
	];

	// RSM modules routes.
	const MODULE_ROUTES = [
		'rsm.rollingweekstatus', 'rsm.incidents', 'rsm.slareports', 'rsm.incidentdetails', 'rsm.tests',
		'rsm.particulartests', 'rsm.aggregatedetails', 'rsm.particularproxys', 'export.rsm.slareports',
		'rsm.markincident',
	];

	/** @var array $user    Logged in user data array. */
	protected $user = [];

	public function __construct() {
		$this->user = CWebUser::$data;
	}

	/**
	 * Check does the user have access to specified route.
	 *
	 * @param string $route    Checked route.
	 */
	public function canAccessRoute(string $route): bool {
		if (in_array($route, static::ALL_ALLOWED)) {
			return true;
		}

		switch (CWebUser::getType()) {
			case USER_TYPE_ZABBIX_USER:
			case USER_TYPE_ZABBIX_ADMIN:
				return !in_array($route, self::MODULE_ROUTES);

			case USER_TYPE_SUPER_ADMIN:
				return true;

			case USER_TYPE_READ_ONLY:
				return in_array($route, ['rsm.rollingweekstatus', 'rsm.incidents', 'rsm.slareports']);

			case USER_TYPE_POWER_USER:
				return in_array($route, [
					// Monitoring.
					'dashboard.view', 'overview.php', 'web.view', 'latest.php', 'tr_status.php', 'events.php',
					'charts.php', 'screens.php', 'map.view', 'srv_status.php',
					// Registrar/Registry monitoring.
					'rsm.rollingweekstatus', 'rsm.incidents', 'rsm.slareports',
					// Inventory.
					'hostinventoriesoverview.php', 'hostinventories.php',
					// Reports.
					'report2.php', 'toptriggers.php',
				]);

			case USER_TYPE_COMPLIANCE:
				return in_array($route, [
					// Monitoring
					'dashboard.view', 'overview.php', 'web.view', 'latest.php', 'tr_status.php', 'events.php',
					'charts.view', 'charts.view.json', 'screens.php', 'map.view', 'srv_status.php',
					// Registrar/Registry monitoring.
					'rsm.rollingweekstatus', 'rsm.incidents', 'rsm.slareports',
					// Inventory
					'hostinventoriesoverview.php', 'hostinventories.php',
					// Reports
					'report2.php', 'toptriggers.php'
				]);
		}

		return false;
	}
}
