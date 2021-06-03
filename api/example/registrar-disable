#!/usr/bin/env bash

set -o errexit
set -o nounset
set -o pipefail
#set -o xtrace

. core

function get_json()
{
	cat << JSON
{
    "registrarName": "Registrar Name $i",
    "registrarFamily": "Registrar Family $i",
    "servicesStatus": [
        { "service": "rdds43", "enabled": false },
        { "service": "rdds80", "enabled": false },
        { "service": "rdap"  , "enabled": false }
    ]
}
JSON
}

for ((i = $min; i <= $max; i++)); do
	provisioning_api readwrite put registrars "${i}" "$(get_json)"
done
