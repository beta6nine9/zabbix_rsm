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
        { "service": "dnsUDP", "enabled": $dnsudp },
        { "service": "dnsTCP", "enabled": $dnstcp },
        { "service": "rdds43", "enabled": $rdds43 },
        { "service": "rdds80", "enabled": $rdds80 },
        { "service": "rdap"  , "enabled": $rdap }
    ],
    "dnsParameters": {
JSON
        echo -n '        "nsIps": ['
	if [ $ipv4 = "true" ]; then
		echo -n '
            { "ns": "a.ns.tld'${i}'", "ip": "192.168.3.11" },
            { "ns": "b.ns.tld'${i}'", "ip": "192.168.3.130" },
            { "ns": "c.ns.tld'${i}'", "ip": "192.168.6.85" }'
		if [ $ipv6 = "true" ]; then
			echo -n ','
		fi
	fi
	if [ $ipv6 = "true" ]; then
		echo -n '
            { "ns": "b.ns.tld'${i}'", "ip": "0:0:0:0:0:ffff:7f00:0004" },
            { "ns": "c.ns.tld'${i}'", "ip": "0:0:0:0:0:ffff:7f00:0005" }'
	fi

	echo

	cat << JSON
        ],
        "dnssecEnabled": $dnssec,
        "nsTestPrefix": "www.zz--icann-monitoring.example",
        "minNs": $minns
JSON
	if [[ $rdds43 = "true" || $rdds80 = "true" || $rdap = "true" ]]; then
		echo -n '    },
    "rddsParameters": {
'
	fi

	if [ $rdds43 = "true" ]; then
		echo -n '        "rdds43Server": "whois.nic.example",
        "rdds43TestedDomain": "nic.example",
        "rdds43NsString": "Name Server:"'
	fi
	if [ $rdds80 = "true" ]; then
		[[ $rdds43 = "true" ]] && echo ','
		echo -n '        "rdds80Url": "https://whois.nic.example"'
	fi
	if [ $rdap = "true" ]; then
		[[ $rdds43 = "true" || $rdds80 = "true" ]] && echo ','
		echo -n '        "rdapUrl": "https://www.nic.example/domain",
        "rdapTestedDomain": "nic.example"'
	fi

	if [[ $rdds43 = "true" || $rdds80 = "true" || $rdap = "true" ]]; then
		echo
	fi

	cat << JSON
    }
}
JSON
}

for ((i = $min; i <= $max; i++)); do
	provisioning_api readwrite put tlds "tld${i}" "$(get_json)"
done
