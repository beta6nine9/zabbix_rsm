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
        { "service": "rdds", "enabled": true },
        { "service": "rdap", "enabled": true }
    ],
    "zabbixProxyParameters": {
        "ipv4Enable": true,
        "ipv6Enable": false,
        "proxyIp": "192.168.6.85",
        "proxyPort": 1005${i},
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

db-exec.sh "update hosts set tls_connect=1 where status=6"
