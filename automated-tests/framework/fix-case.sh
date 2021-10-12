#!/usr/bin/env bash

set -o errexit
set -o nounset
set -o pipefail
#set -o xtrace

if [ $# -ne 1 ]; then
	echo "usage: $0 <test-case-file>"
	echo
	echo "Run a test case, create new sla and cache outputs and replace them"
	exit 1
fi

test_case_file="$1"
new_test_case_file="$test_case_file.new.txt"

cp -f $test_case_file $new_test_case_file

num=$(basename $test_case_file)
num=${num%%-*}

sed -i -r "s|(\[compare-files\])|\"\",\"../test-cases/sla-api/create-sla-and-cache-output.sh $num\"\n\1|" $new_test_case_file

TZ=UTC faketime '2021-01-23 00:00:00' ./test-wrapper --skip-build --test-case-file $new_test_case_file

rm -rf $new_test_case_file
