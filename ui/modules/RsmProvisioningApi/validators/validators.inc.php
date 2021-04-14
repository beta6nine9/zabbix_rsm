<?php

//declare(strict_types=1); // TODO: enable strict_types

function RsmValidateProbeIdentifier($rule, &$data, $path, &$error) {
	if (!preg_match('/^[a-zA-Z0-9_\-]+$/', $data))
	{
		$error = _s('Invalid parameter "%1$s": %2$s.', $path, _('may include only a-z, A-Z, 0-9, "_" and "-"'));
		return false;
	}

	return true;
}

function RsmValidateTldIdentifier($rule, &$data, $path, &$error) {
	// TODO: add validation: "This element is one valid DNS label in A-label format."

	return true;
}

function RsmValidateIP($rule, &$data, $path, &$error) {
	if (filter_var($data, FILTER_VALIDATE_IP, FILTER_FLAG_IPV4 | FILTER_FLAG_IPV6) === false)
	{
		$error = _s('Invalid parameter "%1$s": %2$s.', $path, _('must be valid IP address'));
		return false;
	}

	return true;
}

function RsmValidateIPv4($rule, &$data, $path, &$error) {
	if (filter_var($data, FILTER_VALIDATE_IP, FILTER_FLAG_IPV4) === false)
	{
		$error = _s('Invalid parameter "%1$s": %2$s.', $path, _('must be valid IPv4 address'));
		return false;
	}

	return true;
}

function RsmValidateIPv6($rule, &$data, $path, &$error) {
	if (filter_var($data, FILTER_VALIDATE_IP, FILTER_FLAG_IPV6) === false)
	{
		$error = _s('Invalid parameter "%1$s": %2$s.', $path, _('must be valid IPv6 address'));
		return false;
	}

	return true;
}
