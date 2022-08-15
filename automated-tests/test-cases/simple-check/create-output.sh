#!/usr/bin/env bash
#
# usage in a test case:
#
# [execute]
#
# "","test-cases/simple-check/create-output.sh 001"
#
# This will create archive:
#
# test-cases/simple-check/001-output.tar.gz

set -e

case=$1

if [ -z "$case" ]; then
	echo "usage: $0 <case>"
	echo "e. g.: $0 001"
	exit 1
fi

OUTPUT_DIR=$(realpath $(dirname $0))

pushd /tmp/simple-check-test
tar czvf $OUTPUT_DIR/$case-output.tar.gz *
popd
