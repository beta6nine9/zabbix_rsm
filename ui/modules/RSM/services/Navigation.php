<?php

namespace Modules\RSM\Services;

use APP;
use CMenu;
use CMenuItem;
use CUrl;

/**
 * Service responsible for Zabbix navigation modifications accoring logged in user permission level.
 */
class Navigation {

	/** @var array $actions    Array with module actions. */
	protected $actions;

	public function __construct(array $actions) {
		$this->actions = $actions;
	}

	/**
	 * Build module navigation.
	 *
	 * @param CMenu $node    Zabbix navigation object.
	 */
	public function build(CMenu $node) {
		$menu = [];
		$macro = APP::component()->get('rsm.macro');
		$label = ($macro->get(RSM_MONITORING_TARGET) == MONITORING_TARGET_REGISTRAR)
			? _('Registrar monitoring')
			: _('Registry monitoring');

		if (in_array('rsm.rollingweekstatus', $this->actions)) {
			$menu[] = (new CMenuItem(_('Rolling week status')))->setAction('rsm.rollingweekstatus');
		}

		if (in_array('rsm.incidents', $this->actions)) {
			$url = (new CUrl('zabbix.php'))
				->setArgument('filter_rst', '1')
				->setArgument('action', 'rsm.incidents')
				->setArgument('rolling_week', 1);
			$menu[] = (new CMenuItem(_('Incidents')))
				->setUrl($url, 'rsm.incidents')
				->setAliases([
					'rsm.incidents', 'rsm.incidentdetails', 'rsm.tests',
					'rsm.particulartests', 'rsm.aggregatedetails'
				]);
		}

		if (in_array('rsm.slareports', $this->actions)) {
			$menu[] = (new CMenuItem(_('SLA reports')))->setAction('rsm.slareports');
		}

		if (in_array('rsm.probes', $this->actions)) {
			$menu[] = (new CMenuItem(_('Probes')))->setAction('rsm.probes');
		}

		if ($menu) {
			$node->add((new CMenuItem($label))
				->setId('rsm_monitoring')
				->setIcon('icon-monitoring')
				->setSubMenu(new Cmenu($menu))
			);
		}

		// Remove Zabbix default menu entries if not available.
		$this->modify($node, _('Monitoring'), [
			_('Dashboard') => 'dashboard.view',
			_('Discovery') => 'discovery.view',
		]);
		$this->modify($node, _('Inventory'), [
			_('Overview') => 'hostinventoriesoverview.php',
		]);
		$this->modify($node, _('Reports'), [
			_('Availability report') => 'report2.php',
			_('Notifications') => 'report4.php',
			_('System information') => 'report.status',
			_('Audit') => 'auditlog.list',
		]);
		$this->modify($node, _('Configuration'), [
			_('Host groups') => 'hostgroups.php',
		]);
		$this->modify($node, _('Administration'), [
			_('General') => 'gui.edit',
		]);
	}

	/**
	 * Build user menu.
	 *
	 * @param CMenu $node    User menu node.
	 */
	public function buildUserMenu(CMenu $node) {
		$node->remove(_('Support'));
		$node->remove(_('Share'));
		$node->remove(_('Integrations'));
		$node->remove(_('Help'));
	}

	/**
	 * Modify passed $node removing from submenu entries disallowed by actions. If passed $entries array contains
	 * only disallowed entries submenu will be alsow removed.
	 *
	 * @param CMenu  $node     Menu node to be modified.
	 * @param string $label    Modified submenu label, translated name should be used.
	 * @param array  $entries  Associative array where key is sub menu entry translated name and value is action/route.
	 */
	public function modify(CMenu $node, string $label, array $entries) {
		$submenu = $node->find($label);

		if (is_null($submenu)) {
			return;
		}

		$submenu = $submenu->getSubMenu();
		$disallowed = array_diff($entries, $this->actions);

		if ($disallowed == $entries) {
			$node->remove($label);
		}
		else {
			array_map([$submenu, 'remove'], array_keys($disallowed));
		}
	}

	/**
	 * Add server menu list to navigation menu node.
	 *
	 * @param array $servers    Array of servers to add.
	 * @param CMenu $node       Menu node to add servers list.
	 */
	public function addServersMenu(array $servers, CMenu $node): void {
		$menu = [];

		foreach ($servers as $server) {
			$menuitem = (new CMenuItem($server['name']))->setUrl(new CUrl($server['url']));

			if ($server['selected']) {
				$menuitem->setSelected();
			}

			$menu[] = $menuitem;
		}

		$node->add((new CMenuItem(_('Servers')))
			->setId('rsm_servers')
			->setIcon('icon-inventory')
			->setSubMenu(new Cmenu($menu))
		);
	}
}
