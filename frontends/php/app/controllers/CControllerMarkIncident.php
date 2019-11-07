<?php
/*
** Zabbix
** Copyright (C) 2001-2019 Zabbix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/


Class CControllerMarkIncident extends CController {
	/**
	 * @var array
	 */
	protected $event;

	protected function init() {
		$this->disableSIDValidation();
	}

	protected function checkInput() {
		$fields = [
			'mark_as'		=>	'required|in '.implode(',', [INCIDENT_FALSE_POSITIVE, INCIDENT_ACTIVE, INCIDENT_RESOLVED]),
			'eventid'		=>	'required|db events.eventid',
			'host'			=>	'required|db hosts.host',
			'availItemId'	=>	'required|db items.itemid',
			'slvItemId'		=>	'required|db items.itemid',
			'from'			=>	'string',
			'to'			=>	'string',
			'type'			=>	'required|in '.implode(',', [RSM_DNS, RSM_DNSSEC, RSM_RDDS, RSM_RDAP, RSM_EPP])
		];

		$ret = $this->validateInput($fields);

		if (!$ret) {
			$this->setResponse(new CControllerResponseFatal());
		}

		return $ret;
	}

	protected function checkPermissions() {
		$valid_users = [USER_TYPE_ZABBIX_ADMIN, USER_TYPE_SUPER_ADMIN, USER_TYPE_POWER_USER];

		$event = API::Event()->get([
			'output' => ['eventid', 'objectid', 'clock', 'false_positive'],
			'eventids' => $this->getInput('eventid'),
			'filter' => [
				'value' => TRIGGER_VALUE_TRUE
			]
		]);

		$this->event = reset($event);

		return $this->event && in_array($this->getUserType(), $valid_users);
	}

	protected function doAction() {
		if ($this->getInput('mark_as') == INCIDENT_ACTIVE || $this->getInput('mark_as') == INCIDENT_RESOLVED) {
			$change_incident_type = INCIDENT_FLAG_NORMAL;
			$audit_log = _('Unmarked as false positive');
		}
		else {
			$change_incident_type = INCIDENT_FLAG_FALSE_POSITIVE;
			$audit_log = _('Marked as false positive');
		}

		if ($this->event['false_positive'] != $change_incident_type) {
			// get next ok event
			$next_ok_event = DBfetch(DBselect(
				'SELECT e.clock'.
				' FROM events e'.
				' WHERE e.objectid='.$this->event['objectid'].
					' AND e.clock>='.$this->event['clock'].
					' AND e.object='.EVENT_OBJECT_TRIGGER.
					' AND e.source='.EVENT_SOURCE_TRIGGERS.
					' AND e.value='.TRIGGER_VALUE_FALSE.
				' ORDER BY e.clock,e.ns',
				1
			));

			if ($next_ok_event) {
				$marked_events = DBselect(
					'SELECT e.eventid'.
					' FROM events e'.
					' WHERE e.objectid='.$this->event['objectid'].
						' AND e.clock>='.$this->event['clock'].
						' AND e.clock<='.$next_ok_event['clock'].
						' AND e.object='.EVENT_OBJECT_TRIGGER.
						' AND e.source='.EVENT_SOURCE_TRIGGERS.
						' AND e.value='.TRIGGER_VALUE_TRUE
				);

				while ($marked_event = DBfetch($marked_events)){
					$eventids[] = $marked_event['eventid'];
				}
			}
			else {
				$eventids = array($this->event['eventid']);
			}

			DBstart();
			$res = DBexecute('UPDATE events SET false_positive='.zbx_dbstr($change_incident_type).' WHERE '.
				dbConditionInt('eventid', $eventids)
			);
			$result = DBend($res);

			if ($res) {
				add_audit_details(
					AUDIT_ACTION_UPDATE,
					AUDIT_RESOURCE_INCIDENT,
					$this->event['eventid'],
					$this->getInput('host'),
					$this->event['eventid'].': '.$audit_log
				);
			}

			$response = new CControllerResponseRedirect((new CUrl('zabbix.php'))
				->setArgument('action', 'rsm.incidentdetails')
				->setArgument('host', $this->getInput('host'))
				->setArgument('from', $this->getInput('from'))
				->setArgument('to', $this->getInput('to'))
				->setArgument('availItemId', $this->getInput('availItemId'))
				->setArgument('slvItemId', $this->getInput('slvItemId'))
				->setArgument('eventid', $this->event['eventid'])
				->getUrl()
			);

			if ($result) {
				$response->setMessageOk(_('Status updated'));
			}
			else {
				$response->setMessageError(_('Cannot update status'));
			}
			$this->setResponse($response);
		}
	}
}
