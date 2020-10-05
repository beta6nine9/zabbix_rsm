<?php

namespace Modules\RSM\Helpers;

use CTabView;

/**
 * This calss fix CTabView unability to work with multiple tabs on same page, because multiple tabs will overwrite
 * active tab setting of each other, because:
 * - active tab number is set to {ZBX_SESSION_NAME + "_tab"}
 * - cookie "tab" is use to store active tab
 * This class allow to set custom cookie name which also will be used as session storage property name.
 */
class TabView extends CTabView {

	private $tab_change_js = '';

	public $cookie_name = 'tab';

	public function setCookieName($name) {
		$this->cookie_name = $name;
		return $this;
	}

	public function makeJavascript() {
		$create_event = '';
		$storage = sprintf('ZBX_SESSION_NAME + "_%s"', $this->cookie_name);

		if ($this->selectedTab !== null) {
			$create_event = 'create: function() {'.
				'sessionStorage.setItem('.$storage.', '.json_encode($this->selectedTab).');'.
			'},';
			$active_tab = 'active: '.json_encode($this->selectedTab).',';
		}
		else {
			$active_tab = 'active: function() {'.
				'return sessionStorage.getItem('.$storage.') || 0;'.
			'}(),';
		}

		$disabled_tabs = ($this->disabledTabs === null) ? '' : 'disabled: '.json_encode($this->disabledTabs).',';

		return
			'jQuery("#'.$this->id.'").tabs({'.
				$create_event.
				$disabled_tabs.
				$active_tab.
				'activate: function(event, ui) {'.
					'sessionStorage.setItem('.$storage.', ui.newTab.index().toString());'.
					'jQuery.cookie("'.$this->cookie_name.'", ui.newTab.index().toString());'.
					$this->tab_change_js.
				'}'.
			'})';
	}
}
