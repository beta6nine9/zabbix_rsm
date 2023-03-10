#!/usr/bin/env bash

set -o errexit
set -o nounset
set -o pipefail
#set -o xtrace

. config

if [[ $# -ne 2 ]]; then
	echo "Usage:"
	echo "$0 <alert-type> <value>"
	exit 1
fi

declare -r USER="alerts"
declare -r REQUEST="POST"
declare -r OBJECT_TYPE="alerts"
declare -r OBJECT_ID="$1"
declare    JSON="{\"value\":\"$2\"}"

declare -r URL="${FORWARDER_URL}/${OBJECT_TYPE}/${OBJECT_ID}"

declare -r LINE="$(printf "=%.0s" {1..120})"

echo "${LINE}"
echo "${REQUEST} ${URL}"
echo "${LINE}"

curl                                         \
	--no-progress-meter                  \
	--no-buffer                          \
	--include                            \
	--styled-output                      \
	--header 'Expect:'                   \
	--basic                              \
	--user "alerts:password"             \
	--request "POST"                     \
	${JSON:+"--data"} ${JSON:+"${JSON}"} \
	"${URL}"
echo
echo "${LINE}"
