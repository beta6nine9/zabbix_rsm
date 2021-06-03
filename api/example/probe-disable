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
    "servicesStatus": [
        { "service": "rdds", "enabled": false },
        { "service": "rdap", "enabled": false }
    ],
    "zabbixProxyParameters": {
        "ipv4Enable": false,
        "ipv6Enable": false,
        "proxyIp": "127.0.1.$i",
        "proxyPort": 1234,
        "proxyPskIdentity": "test",
        "proxyPsk": "b23f30b5aa0d274c88c2d1ebf17ee48e"
    },
    "centralServer": 1
}
JSON
}

for ((i = $min; i <= $max; i++)); do
	provisioning_api readwrite put probeNodes "Probe${i}-Server1" "$(get_json)"
done
