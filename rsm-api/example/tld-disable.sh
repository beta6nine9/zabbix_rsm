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
    "tldType": "gTLD",
    "servicesStatus": [
        { "service": "dnsUDP", "enabled": false },
        { "service": "dnsTCP", "enabled": false },
        { "service": "rdds43", "enabled": false },
        { "service": "rdds80", "enabled": false },
        { "service": "rdap"  , "enabled": false }
    ]
}
JSON
}

for ((i = $min; i <= $max; i++)); do
	provisioning_api readwrite put tlds "tld${i}" "$(get_json)"
done
