#!/usr/bin/env bash

set -o errexit
set -o nounset
set -o pipefail
#set -o xtrace

. core

for ((i = $min; i <= $max; i++)); do
	provisioning_api readwrite delete registrars "${i}"
done
