<?php

/**
 * Based on timestamp value stored in {$RSM.RDAP.STANDALONE}, check if RDAP at given time $timestamp is configured as
 * standalone service or as dependent sub-service of RDDS. It is expected that switch from RDAP as sub-service of RDDS
 * to RDAP as standalone service will be done only once and will never be switched back to initial state.
 *
 * @param integer|string  $timestamp  Optional timestamp value.
 *
 * @return bool
 */
function is_rdap_standalone($timestamp = null) {
	static $rsm_rdap_standalone_ts;

	if (is_null($rsm_rdap_standalone_ts)) {
		$db_macro = API::UserMacro()->get([
			'output' => ['value'],
			'filter' => ['macro' => RSM_RDAP_STANDALONE],
			'globalmacro' => true
		]);

		$rsm_rdap_standalone_ts = $db_macro ? (int) $db_macro[0]['value'] : 0;
	}

	$timestamp = is_null($timestamp) ? time() : (int) $timestamp;

	return ($rsm_rdap_standalone_ts > 0 && $rsm_rdap_standalone_ts <= $timestamp);
}

/**
 * Return current type of RSM monitoring.
 *
 * @return int
 */
function get_rsm_monitoring_type() {
	static $type;

	if ($type === null) {
		$db_macro = API::UserMacro()->get([
			'output' => ['value'],
			'filter' => ['macro' => RSM_MONITORING_TARGET],
			'globalmacro' => true
		]);

		if ($db_macro) {
			$type = $db_macro[0]['value'];
		}
	}

	return $type;
}

/**
 * Get first item value from history_uint table.
 *
 * @param int $itemId
 * @param int $startTime
 *
 * @return string
 */
function getFirstUintValue($itemId, $startTime) {
	$query = DBfetch(DBselect(DBaddLimit(
		'SELECT h.value'.
		' FROM history_uint h'.
		' WHERE h.itemid='.$itemId.
			' AND h.clock>='.$startTime.
		' ORDER BY h.clock ASC',
		1
	)));

	return $query ? $query['value'] : 0;
}

/**
 * Returned boolean indicates if the result of the test is to be treated as unsuccessful for service provider.
 *
 * @param int $rtt		Result of the test (Round-Time Trip in milliseconds or error code).
 * @param int $type		Type of service, e. g. RSM_DNS, RSM_DNSSEC etc.
 *
 * @return bool
 */
function isServiceErrorCode($rtt, $type) {
	if ($type == RSM_DNSSEC) {
		if (ZBX_EC_DNS_UDP_DNSSEC_FIRST >= $rtt && $rtt >= ZBX_EC_DNS_UDP_DNSSEC_LAST)
			return true;

		if (ZBX_EC_DNS_TCP_DNSSEC_FIRST >= $rtt && $rtt >= ZBX_EC_DNS_TCP_DNSSEC_LAST)
			return true;

		return false;
	}

	return ($rtt < ZBX_EC_INTERNAL_LAST);
}

function getEventFalsePositiveness($eventid) {
	$row = DBfetch(DBselect(
		'SELECT status'.
		' FROM rsm_false_positive'.
		' WHERE eventid='.$eventid.
		' ORDER BY rsm_false_positiveid DESC',
		1
	));

	if ($row === false) {
	    return INCIDENT_FLAG_NORMAL;
	}

	return $row['status'];
}

/**
 * Get last eventid from the events.
 *
 * @param int $problemTrigger
 *
 * @return int
 */
function getLastEvent($problemTrigger) {
	$result = null;

	$lastProblemEvent = DBfetch(DBselect(
		'SELECT e.eventid,e.clock'.
		' FROM events e'.
		' WHERE e.objectid='.$problemTrigger.
			' AND e.source='.EVENT_SOURCE_TRIGGERS.
			' AND e.object='.EVENT_OBJECT_TRIGGER.
			' AND e.value='.TRIGGER_VALUE_TRUE.
		' ORDER BY e.clock DESC',
		1
	));

	$lastProblemEvent['false_positive'] = getEventFalsePositiveness($lastProblemEvent['eventid']);

	if ($lastProblemEvent && $lastProblemEvent['false_positive'] == INCIDENT_FLAG_NORMAL) {
		$result = getPreEvents($problemTrigger, $lastProblemEvent['clock'], $lastProblemEvent['eventid']);
	}

	return $result;
}

/**
 * Get previos open event
 *
 * @param int $objectid
 * @param int $clock
 * @param int $eventid
 *
 * @return int
 */
function getPreEvents($objectid, $clock, $eventid) {
	$result = $eventid;

	$beforeEvent = DBfetch(DBselect(
		'SELECT e.eventid,e.clock,e.value'.
		' FROM events e'.
		' WHERE e.objectid='.$objectid.
			' AND e.source='.EVENT_SOURCE_TRIGGERS.
			' AND e.object='.EVENT_OBJECT_TRIGGER.
			' AND e.clock<='.$clock.
			' AND e.eventid!='.$eventid.
		' ORDER BY e.clock DESC',
		1
	));

	if ($beforeEvent && $beforeEvent['value'] == TRIGGER_VALUE_TRUE) {
		$result = getPreEvents($objectid, $beforeEvent['clock'], $beforeEvent['eventid']);
	}

	return $result;
}

/**
 * Convert SLA service name.
 *
 * @param string $name
 *
 * @return int
 */
function convertSlaServiceName($name) {
	$services = array(
		'dns' => RSM_DNS,
		'dnssec' => RSM_DNSSEC,
		'rdds' => RSM_RDDS,
		'epp' => RSM_EPP
	);

	return $services[$name];
}

/**
 * Get failed tests count.
 *
 * @param int 		$itemId
 * @param int 		$endTime
 * @param int 		$incidentStartTime
 * @param int 		$incidentEndTime
 *
 * @return int
 */
function getFailedTestsCount($itemId, $endTime, $incidentStartTime, $incidentEndTime = null) {
	$to = $incidentEndTime ? $incidentEndTime : $endTime;

	$getFailedTestsCount = DBfetch(DBselect(
		'SELECT COUNT(itemid) AS count'.
		' FROM history_uint h'.
		' WHERE h.itemid='.$itemId.
			' AND h.clock>='.$incidentStartTime.
			' AND h.clock<='.$to.
			' AND h.value=0'
	));

	return $getFailedTestsCount['count'];
}

/**
 * Get total tests count.
 *
 * @param int 		$itemId
 * @param int 		$startTime
 * @param int 		$endTime
 * @param int 		$incidentStartTime
 * @param int 		$incidentEndTime
 *
 * @return int
 */
function getTotalTestsCount($itemId, $startTime, $endTime, $incidentStartTime = null, $incidentEndTime = null) {
	$from = $incidentStartTime ? $incidentStartTime : $startTime;
	$to = $incidentEndTime ? $incidentEndTime : $endTime;

	$getTotalTestsCount = DBfetch(DBselect(
		'SELECT COUNT(itemid) AS count'.
		' FROM history_uint h'.
		' WHERE h.itemid='.$itemId.
			' AND h.clock>='.$from.
			' AND h.clock<='.$to
	));

	return $getTotalTestsCount['count'];
}

/**
 * Return incident status.
 *
 * @param int 		$falsePositive
 * @param int 		$status
 *
 * @return string
 */
function getIncidentStatus($falsePositive, $status) {
	if ($falsePositive) {
		$incidentStatus = _('False positive');
	}
	else {
		if ($status == TRIGGER_VALUE_TRUE) {
			$incidentStatus = _('Active');
		}
		elseif ($status == TRIGGER_VALUE_FALSE) {
			$incidentStatus = _('Resolved');
		}
		else {
			$incidentStatus = _('Resolved (no data)');
		}
	}

	return $incidentStatus;
}

/**
 * Generate data to be displayed in details widget. The output is an array to be passed to CWidget::addItem().
 * Expects array of key/value pairs, each element is to be displayed on one line with key in bold style. The
 * value can be either string or an array of CTag objects.
 *
 * @param array
 *
 * @return string
 */
function gen_details_item(array $details) {
	$output = [];

	foreach ($details as $key => $value) {
		if (!isset($value)) {
			continue;
		}

		$output[] = bold($key);
		$output[] = ': ';
		$output[] = $value;
		$output[] = BR();
	}

	if ($output) {
		array_pop($output);
	}

	return $output;
}

/**
 * Create DB connection to other DB server.
 *
 * @param array  $server       Database server parameters
 * @param string $error        returns a message in case of an error
 *
 * @return bool
 */
function multiDBconnect($server, &$error) {
	global $DB;

	unset($DB['DB']);
	$DB = array_merge($DB, $server);

	return DBconnect($error);
}

/**
 * Convert elapsed time to human readable string.
 *
 * @param DateTime $datetime   any supported date and time format (http://www.php.net/manual/en/datetime.formats.php)
 * @param bool     $full       report full string even when zeroes
 *
 * @return string
 */
function elapsedTime($datetime, $full = false) {
	$now = new DateTime;
	$ago = new DateTime($datetime);

	$diff = $now->diff($ago);

	$diff->w = floor($diff->d / 7);
	$diff->d -= $diff->w * 7;

	$string = array(
		'y' => 'year',
		'm' => 'month',
		'w' => 'week',
		'd' => 'day',
		'h' => 'hour',
		'i' => 'minute',
		's' => 'second',
	);

	foreach ($string as $k => &$v) {
		if ($diff->$k) {
			$v = $diff->$k . ' ' . $v . ($diff->$k > 1 ? 's' : '');
		} else {
			unset($string[$k]);
		}
	}

	if (!$full) {
		$string = array_slice($string, 0, 1);
	}

	return $string ? implode(', ', $string) . ' ago' : 'just now';
}
