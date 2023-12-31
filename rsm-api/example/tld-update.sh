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
        { "service": "dnsUDP", "enabled": true },
        { "service": "dnsTCP", "enabled": true },
        { "service": "rdds43", "enabled": true },
        { "service": "rdds80", "enabled": true },
        { "service": "rdap"  , "enabled": true }
    ],
    "dnsParameters": {
        "nsIps": [
            { "ns": "a.ns.tld${i}", "ip": "127.0.0.1" },
            { "ns": "b.ns.tld${i}", "ip": "127.0.0.3" },
            { "ns": "b.ns.tld${i}", "ip": "0:0:0:0:0:ffff:7f00:0004" },
            { "ns": "b.ns.tld${i}", "ip": "127.0.0.2" },
            { "ns": "c.ns.tld${i}", "ip": "127.0.0.1" },
            { "ns": "d.ns.tld${i}", "ip": "127.0.0.3" },
            { "ns": "d.ns.tld${i}", "ip": "0:0:0:0:0:ffff:7f00:0004" },
            { "ns": "d.ns.tld${i}", "ip": "127.0.0.2" },
            { "ns": "e.ns.tld${i}", "ip": "127.0.0.1" },
            { "ns": "f.ns.tld${i}", "ip": "127.0.0.3" },
            { "ns": "f.ns.tld${i}", "ip": "0:0:0:0:0:ffff:7f00:0004" },
            { "ns": "f.ns.tld${i}", "ip": "127.0.0.2" }
        ],
        "dnssecEnabled": true,
        "nsTestPrefix": "www.zz--rsm-monitoring.example",
        "minNs": 2
    },
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
	provisioning_api readwrite put tlds "tld${i}" "$(get_json)"
done
