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
        { "service": "rdds43", "enabled": true },
        { "service": "rdds80", "enabled": true },
        { "service": "rdap"  , "enabled": true }
    ],
    "rddsParameters": {
        "rdds43Server": "whois.nic.example",
        "rdds43TestedDomain": "nic.example",
        "rdds80Url": "https://whois.nic.example",
        "rdapUrl": "https://www.nic.example/domain",
        "rdapTestedDomain": "nic.example",
        "rdds43NsString": "Name Server:"
    }
}
JSON
}

for ((i = $min; i <= $max; i++)); do
	provisioning_api readwrite put registrars "${i}" "$(get_json)"
done
