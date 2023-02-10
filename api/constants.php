<?php

const REQUEST_METHOD_GET     = 'GET';
const REQUEST_METHOD_DELETE  = 'DELETE';
const REQUEST_METHOD_PUT     = 'PUT';
const REQUEST_METHOD_POST    = 'POST';

const OBJECT_TYPE_TLDS       = 'tlds';
const OBJECT_TYPE_REGISTRARS = 'registrars';
const OBJECT_TYPE_PROBES     = 'probeNodes';
const OBJECT_TYPE_ALERTS     = 'alerts';

const FRONTEND_ACTIONS = [
	OBJECT_TYPE_TLDS         => 'rsm.provisioningapi.tld',
	OBJECT_TYPE_REGISTRARS   => 'rsm.provisioningapi.registrar',
	OBJECT_TYPE_PROBES       => 'rsm.provisioningapi.probe',
];
