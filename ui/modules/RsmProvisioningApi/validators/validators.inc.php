<?php

//declare(strict_types=1); // TODO: enable strict_types

function RsmValidateInvalid($rule, &$data, $path, &$error): bool
{
	$error = $rule['error'];
	return false;
}

function RsmValidateInt($rule, &$data, $path, &$error): bool
{
	if (!is_int($data))
	{
		$error = $rule['error'];
		return false;
	}
	if (array_key_exists('min', $rule) && $data < $rule['min'])
	{
		$error = $rule['error'];
		return false;
	}
	if (array_key_exists('max', $rule) && $data > $rule['max'])
	{
		$error = $rule['error'];
		return false;
	}
	return true;
}

function RsmValidateBoolean($rule, &$data, $path, &$error): bool
{
	if (!is_bool($data))
	{
		$error = $rule['error'];
		return false;
	}
	return true;
}

function RsmValidateEnum($rule, &$data, $path, &$error): bool
{
	if (!is_string($data))
	{
		$error = $rule['error'];
		return false;
	}
	if (!in_array($data, $rule['in']))
	{
		$error = $rule['error'];
		return false;
	}
	return true;
}

function RsmValidateIP($rule, &$data, $path, &$error): bool
{
	if (filter_var($data, FILTER_VALIDATE_IP, FILTER_FLAG_IPV4 | FILTER_FLAG_IPV6) === false)
	{
		$error = $rule['error'];
		return false;
	}
	return true;
}

function RsmValidateIPv4($rule, &$data, $path, &$error): bool
{
	if (filter_var($data, FILTER_VALIDATE_IP, FILTER_FLAG_IPV4) === false)
	{
		$error = $rule['error'];
		return false;
	}
	return true;
}

function RsmValidateIPv6($rule, &$data, $path, &$error): bool
{
	if (filter_var($data, FILTER_VALIDATE_IP, FILTER_FLAG_IPV6) === false)
	{
		$error = $rule['error'];
		return false;
	}
	return true;
}

function RsmValidateDomainName($rule, &$data, $path, &$error): bool
{
	// see rfc8499 for the definition of Domain name
	if (filter_var($data, FILTER_VALIDATE_DOMAIN) === false)
	{
		$error = $rule['error'];
		return false;
	}
	return true;
}

function RsmValidateHostname($rule, &$data, $path, &$error): bool
{
	// see rfc8499 for the definition of hostname
	if (filter_var($data, FILTER_VALIDATE_DOMAIN, FILTER_FLAG_HOSTNAME) === false)
	{
		$error = $rule['error'];
		return false;
	}
	return true;
}

function RsmValidateUrl($rule, &$data, $path, &$error): bool
{
	// see rfc3986 for the definition of a URL
	if (filter_var($data, FILTER_VALIDATE_URL) === false)
	{
		$error = $rule['error'];
		return false;
	}
	return true;
}

function RsmValidateRdapUrl($rule, &$data, $path, &$error): bool
{
	if ($data === 'not listed')
	{
		return true;
	}
	if ($data === 'no https')
	{
		return true;
	}
	return RsmValidateUrl($rule, $data, $path, $error);
}

function RsmValidateProbeIdentifier($rule, &$data, $path, &$error): bool
{
	if (!preg_match('/^[a-zA-Z0-9_\-]+$/', $data))
	{
		$error = $rule['error'];
		return false;
	}
	return true;
}

function RsmValidateTldIdentifier($rule, &$data, $path, &$error): bool
{
	// allow "." TLD
	if ($data === '.')
	{
		return true;
	}
	// trim trailing "."
	if (mb_substr($data, -1) === '.')
	{
		$data = mb_substr($data, 0, -1);
	}
	// min length 2, max length 63, may contain only a-z,0-9,'-', must start and end with a-z,0-9
	if (!preg_match('/^[a-z0-9][a-z0-9-]{0,61}[a-z0-9]$/', $data))
	{
		$error = $rule['error'];
		return false;
	}
	// if 3rd and 4th characters are '--', then 1st and 2nd characters must be 'xn' (i.e., 'xn--')
    if (($data[0] != 'x' || $data[1] != 'n') && $data[2] == '-' && $data[3] == '-')
	{
		$error = $rule['error'];
		return false;
	}
	return true;
}
