#!/usr/bin/env bash

set -o errexit
set -o nounset
set -o pipefail
#set -o xtrace

. core

provisioning_api readonly get tlds "${1+tld$1}"
