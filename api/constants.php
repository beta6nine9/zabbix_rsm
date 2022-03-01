<?php

const REQUEST_METHOD_GET     = 'GET';
const REQUEST_METHOD_DELETE  = 'DELETE';
const REQUEST_METHOD_PUT     = 'PUT';

const OBJECT_TYPE_TLDS       = 'tlds';
const OBJECT_TYPE_REGISTRARS = 'registrars';
const OBJECT_TYPE_PROBES     = 'probeNodes';

const FRONTEND_ACTIONS = [
	OBJECT_TYPE_TLDS         => 'rsm.provisioningapi.tld',
	OBJECT_TYPE_REGISTRARS   => 'rsm.provisioningapi.registrar',
	OBJECT_TYPE_PROBES       => 'rsm.provisioningapi.probe',
];
