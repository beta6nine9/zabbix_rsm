#!/usr/bin/env bash

set -o errexit
set -o nounset
set -o pipefail
#set -o xtrace

if [ $# -eq 0 ]; then
	echo "usage: $0 <tarball>"
	exit 1
fi

OUTPUT_DIR="/tmp/extracted-data"

rm -rf $OUTPUT_DIR
mkdir -p $OUTPUT_DIR
tar -C $OUTPUT_DIR -xzvf $1
echo "Successfully extracted to $OUTPUT_DIR"
