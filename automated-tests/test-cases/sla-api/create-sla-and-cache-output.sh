#!/usr/bin/env bash
#
# usage in a test case:
#
# [execute]
#
# "","test-cases/sla-api/create-sla-and-cache-output.sh 001 [2]"
#
# This will create archives:
#
# test-cases/sla-api/001-sla-output.tar.gz
# test-cases/sla-api/001-cache-output.tar.gz

set -e

case=$1
suffix=$2

if [ -z "$case" ]; then
	echo "usage: $0 <case> [2]"
	echo "e. g.: $0 001"
	exit 1
fi

OUTPUT_DIR=$(realpath $(dirname $0))

if [ -n "$suffix" ]; then
	suffix="-$suffix"
fi

set -x
for entity in cache sla; do
	f=$OUTPUT_DIR/$case-$entity-output$suffix.tar.gz

	if [ ! -f $f ]; then
		continue;
	fi

	pushd /opt/zabbix/$entity
	tar czvf $f *
	popd
done
