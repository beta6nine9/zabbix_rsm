<?php

namespace Modules\RSM\Actions;

use CControllerTimeSelectorUpdate as Action;

/**
 * Dirty hack mode on:
 *
 * To be able to store filter active tab on RSM custom pages we have to add custom
 * profiles key to CControllerTimeSelectorUpdate controller checkInput method for this we overwrite
 * native "timeselector.update" action and make "proxy" class for RSM custom profile keys validation.
 */
class ProfileTimeselectorUpdateAction extends Action {

	protected function checkInput() {
		$ret = parent::checkInput();

		if (!$ret) {
			$ret = in_array($this->getInput('idx'), [
				'web.rsm.incidents.filter',
				'web.rsm.incidentsdetails.filter',
				'web.rsm.tests.filter',
			]) && $this->validateInputDateRange();
		}

		return $ret;
	}
}
