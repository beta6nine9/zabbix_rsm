<?php

namespace Modules\RSM\Security;

use CWebUser;

/**
 * User permissions filter service.
 */
class Permission {

	// System routes allowed to any user: ajax, server switch and etc.
	const ALL_ALLOWED = [
		'index.php',
		'profile.update',
		'rsm.sidlogin',
		'timeselector.update',
		'jsrpc.php',
		'chart.php',
		'chart2.php',
		'chart3.php',
		'chart4.php',
		'chart5.php',
		'chart6.php',
		'chart7.php',
	];

	// RSM modules routes.
	const MODULE_ROUTES = [
		'rsm.rollingweekstatus',
		'rsm.incidents',
		'rsm.slareports',
		'rsm.incidentdetails',
		'rsm.tests',
		'rsm.particulartests',
		'rsm.aggregatedetails',
		'export.rsm.slareports',
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
			case USER_TYPE_SUPER_ADMIN:
				return true;

			case USER_TYPE_READ_ONLY:
				return in_array($route, self::MODULE_ROUTES) || in_array($route, [
					'history.php',
					'userprofile.edit',
				]);

			case USER_TYPE_COMPLIANCE:
			case USER_TYPE_POWER_USER:
				return in_array($route, self::MODULE_ROUTES) || !in_array($route, [
					'actionconf.php',
					'applications.php',
					'conf.import.php',
					'disc_prototypes.php',
					'discoveryconf.php',
					'graphs.php',
					'host_discovery.php',
					'host_prototypes.php',
					'hostgroups.php',
					'hosts.php',
					'httpconf.php',
					'items.php',
					'maintenance.php',
					'report4.php',
					'services.php',
					'templates.php',
					'trigger_prototypes.php',
					'triggers.php',
					'auditacts.php',
					'correlation.php',
					'queue.php',
					'rsm.probes',
				]);
		}

		return false;
	}
}
