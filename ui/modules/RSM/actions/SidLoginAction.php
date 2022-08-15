<?php

namespace Modules\RSM\Actions;

use API;
use CWebUser;
use CSessionHelper;
use CControllerResponseRedirect;
use Modules\RSM\Helpers\UrlHelper as Url;

class SidLoginAction extends Action {

	protected $fields = [
		'i'		=> 'required|string',
		's'		=> 'required|string',
		'back'	=> 'required|string'
	];

	protected function checkPermissions() {
		return true;
	}

	protected function doAction() {
		if (URL::sidLoginSignature($this->getInput('i')) === $this->getInput('s')) {
			/**
			 * Delete all possible "sessionid" errors that might appear when switching
			 * to a frontend that not has a session. In the future if getMessages()
			 * function changes this part might need to be revised.
			 */
			getMessages();

			CSessionHelper::set('sessionid', $this->getInput('i'));
			API::getWrapper()->auth = $this->getInput('i');

			$redirect = $this->getInput('back');
		}
		else {
			$redirect = '/';
		}

		$this->setResponse(new CControllerResponseRedirect($redirect));
	}
}
