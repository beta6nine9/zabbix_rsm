<?php

namespace Modules\RSM\Actions;

use CControllerProfileUpdate as Action;

/**
 * Dirty hack mode on:
 *
 * To be able to store filter active tab on RSM custom pages we have to add custom
 * profiles key to CControllerProfileUpdate controller checkInput method for this we overwrite native "profile.update"
 * action and make "proxy" class for RSM custom profile keys validation.
 */
class ProfileUpdateAction extends Action {

	protected function checkInput() {
		$ret = parent::checkInput();

		$ret = $ret || in_array($this->getInput('idx'), ['web.rsm.slareports.filter.state',
			'web.rsm.tests.filter.state', 'web.rsm.incidentsdetails.filter.state',
			'web.rsm.rollingweekstatus.filter.active', 'web.rsm.slareports.filter.active',
			'web.rsm.incidents.filter.active', 'web.rsm.incidentsdetails.filter.active',
			'web.rsm.tests.filter.active', 'web.rsm.rollingweekstatus.active'
		]);

		if ($ret) {
			$this->setResponse(null);
		}

		return $ret;
	}
}
