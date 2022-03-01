<?php

namespace Modules\RSM;

use APP;
use DB;
use CWebUser;
use Core\CModule as CModule;
use CTag;
use CController as CAction;
use CLegacyAction;
use Modules\RSM\Actions\AuthAction;
use Modules\RSM\Services\MacroService;
use Modules\RSM\Services\DatabaseService;
use Modules\RSM\Services\Navigation;
use Modules\RSM\Security\Permission;
use Modules\RSM\Actions\Action;
use Modules\RSM\Helpers\UrlHelper as Url;

require_once __DIR__.'/defines.inc.php';
require_once __DIR__.'/functions.inc.php';

class Module extends CModule {

	/** @var string $rsm_monitoring_mode */
	protected $rsm_monitoring_mode;

	/** @var int $before_authaction_userid */
	protected $before_authaction_userid;

	/** @var DatabaseService $db */
	protected $db;

	public function init(): void {
		$macro = new MacroService;
		APP::Component()->register('rsm.macro', $macro);
		$this->db = new DatabaseService;
		$this->permission = new Permission;
		// Register module instance as component RSM.
		APP::Component()->RSM = $this;

		if (!CWebUser::isLoggedIn()) {
			return;
		}

		$macro->read([RSM_MONITORING_TARGET, RSM_RDAP_STANDALONE]);
		$this->rsm_monitoring_mode = $macro->get(RSM_MONITORING_TARGET);
		$actions = $this->getZabbixActions();
		$cmenu = APP::Component()->get('menu.main');

		// Monitoring target must be set.
		if ($this->rsm_monitoring_mode !== MONITORING_TARGET_REGISTRY
				&& $this->rsm_monitoring_mode !== MONITORING_TARGET_REGISTRAR) {
			error('Unknown monitoring target.');
		}
		else {
			$actions = array_merge(array_keys($this->getActions()), $actions);
		}

		$actions = array_filter($actions, [$this->permission, 'canAccessRoute']);
		$nav = new Navigation($actions);
		$nav->build($cmenu);
		$nav->addServersMenu($this->getServersList(), $cmenu);
		$nav->buildUserMenu(APP::Component()->get('menu.user'));
	}

	/**
	 * Before action event handler.
	 *
	 * @param CAction $action    Current request handler object.
	 */
	public function onBeforeAction(CAction $action): void {
		if ($action instanceof AuthAction) {
			$this->before_authaction_userid = CWebUser::$data['userid'];
		}
		// Check permissions only for standart Zabbix actions and RSM module actionsю
		else if (($action instanceof CLegacyAction || $action instanceof Action)
				&& !$this->permission->canAccessRoute($action->getAction())) {
			access_deny(ACCESS_DENY_PAGE);
		}
	}

	/**
	 * For login/logout actions update user seession state in multiple databases.
	 */
	public function onTerminate(CAction $action): void {
		$userid = CWebUser::$data['userid'] ?? $this->before_authaction_userid;

		if ($action instanceof AuthAction && $userid > 0) {
			$this->syncUserSessionAcrossDatabases($userid);

			// Force to send user session cookie only when user is logged in.
			if (isset(CWebUser::$data['sessionid'])) {
				CSessionHelper::set('sessionid', CWebUser::$data['sessionid']);
				API::getWrapper()->auth = CWebUser::$data['sessionid'];
			}
		}
	}

	/**
	 * On user login/logout action synchronize 'sessions' table data for this user across databases.
	 *
	 * @param int $userid        Session of this user will be updated in all databases.
	 */
	public function syncUserSessionAcrossDatabases($userid) {
		// Select sessions from current database.
		$sessions = DB::select('sessions', [
			'output' => ['sessionid', 'userid', 'lastaccess', 'status'],
			'filter' => ['userid' => $userid]
		]);

		// Update sessions data in all other databases.
		$this->db->exec(function ($db) use ($sessions, $userid) {
			DB::delete('sessions', [
				'userid' => $userid
			]);

			if ($sessions) {
				DB::insert('sessions', $sessions, false);
			}
		}, DatabaseService::SKIP_CURRENT_DB);
	}

	/**
	 * Get array of available frontends.
	 *
	 * @return array
	 */
	public function getServersList() {
		global $DB, $ZBX_SERVER_NAME;

		$list = [];

		foreach ($DB['SERVERS'] as $server) {
			$list[] = [
				'name' => $server['NAME'],
				'url' => Url::getFor($server['URL'], 'rsm.rollingweekstatus', []),
				'selected' => ($ZBX_SERVER_NAME == $server['NAME'])
			];
		}

		return $list;
	}

	/**
	 * Get HTML <link> wrappers for RSM styles. Support current user theme. Theme file name is build from theme name and
	 * suffix '.rsm.css'.
	 */
	public function getStyle(): array {
		$theme_node = null;
		$module_dir = $this->getDir();
		$theme_file = getUserTheme(CWebUser::$data).'.rsm.css';
		$assets_dir = substr($module_dir, strrpos($module_dir, 'modules/')).'/assets';

		if (file_exists($module_dir.'/assets/'.$theme_file)) {
			$theme_node = (new CTag('link', false))
				->setAttribute('rel', 'stylesheet')
				->setAttribute('type', 'text/css')
				->setAttribute('href', $assets_dir.'/'.$theme_file);
		}

		return [
			$theme_node,
			(new CTag('link', false))
				->setAttribute('rel', 'stylesheet')
				->setAttribute('type', 'text/css')
				->setAttribute('href', $assets_dir.'/rsm.style.css'),
		];
	}

	/**
	 * Return Zabbix default actions list as array.
	 */
	public function getZabbixActions(): array {
		return [
			// Monitoring - Dashboard
			'dashboard.view', 'dashboard.list',
			// Monitoring - Problem
			'problem.view', 'tr_events.php',
			// Monitoring - Hosts
			'host.view', 'web.view', 'charts.view', 'chart2.php', 'chart3.php', 'chart6.php', 'chart7.php', 'httpdetails.php',
			// Monitoring - Overview
			'overview.php', 'history.php', 'chart.php',
			// Monitoring - Screens
			'screens.php', 'screenconf.php', 'screenedit.php', 'screen.import.php', 'slides.php', 'slideconf.php',
			// Monitoring - Maps
			'map.view', 'image.php', 'sysmaps.php', 'sysmap.php', 'map.php', 'map.import.php',
			// Monitoring - Discovery
			'discovery.view',
			// Monitoring - Services
			'srv_status.php', 'report.services', 'chart5.php',
			// Inventory - Overview
			'hostinventoriesoverview.php',
			// Inventory - Hosts
			'hostinventories.php',
			// Reports - Availability report
			'report2.php', 'chart4.php',
			// Reports - System information
			'report.status',
			// Reports - Triggers top 100
			'toptriggers.php',
			// Reports - Audit
			'auditlog.list',
			// Reports - Action log
			'auditacts.php',
			// Reports - Notifications
			'report4.php',
			// Configuration - Host groups
			'hostgroups.php',
			// Configuration - Templates
			'templates.php',
			// Configuration - Hosts
			'hosts.php', 'items.php', 'triggers.php', 'graphs.php', 'applications.php', 'host_discovery.php',
			'disc_prototypes.php', 'trigger_prototypes.php', 'host_prototypes.php', 'httpconf.php',
			// Configuration - Maintenance
			'maintenance.php',
			// Configuration - Actions
			'actionconf.php',
			// Configuration - Event corelation
			'correlation.php',
			// Configuration - Discovery
			'discoveryconf.php',
			// Configuration - Services
			'services.php',
			// Administration - General
			'gui.edit', 'autoreg.edit', 'housekeeping.edit', 'image.list', 'image.edit', 'iconmap.list', 'iconmap.edit',
			'regex.list', 'regex.edit', 'macros.edit', 'valuemap.list', 'valuemap.edit', 'workingtime.edit',
			'trigseverity.edit', 'trigdisplay.edit', 'miscconfig.edit', 'module.list', 'module.edit', 'module.scan',
			// Administration - Proxies
			'proxy.list', 'proxy.edit',
			// Administration - Authentication
			'authentication.edit', 'authentication.update',
			// Administration - User groups
			'usergroup.list', 'usergroup.edit',
			// Administration - Users
			'user.list', 'user.edit',
			// Administration - Media types
			'mediatype.list', 'mediatype.edit',
			// Administration - Scripts
			'script.list', 'script.edit',
			// Administration - Queue
			'queue.php'
		];
	}
}
