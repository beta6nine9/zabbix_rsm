#!/usr/bin/env bash
#
# usage in a test case:
#
# [execute]
#
# "","test-cases/data-export/create-output.sh 001"
#
# This will create archive:
#
# test-cases/data-export/001-output.tar.gz

set -e

case=$1

if [ -z "$case" ]; then
	echo "usage: $0 <case>"
	echo "e. g.: $0 001"
	exit 1
fi

OUTPUT_DIR=$(realpath $(dirname $0))

rm -rf /tmp/export

pushd /opt/zabbix/export
tar czvf $OUTPUT_DIR/$case-output.tar.gz *
popd
