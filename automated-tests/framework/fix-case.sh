#!/usr/bin/env bash

set -o errexit
set -o nounset
set -o pipefail
#set -o xtrace

if [ $# -lt 1 ]; then
	echo "usage: $0 <test-case-file>"
	echo
	echo "Run a test case, create new sla and cache outputs and replace them"
	exit 1
fi

test_case_file="$1"
shift

new_test_case_file="$test_case_file.new.txt"

function cleanup {
	rm -f $new_test_case_file
}

trap cleanup EXIT

cp -f $test_case_file $new_test_case_file

num=$(basename $test_case_file)
num=${num%%-*}

create_script=
if [[ $new_test_case_file =~ /sla-api/ ]]; then
	create_script="/home/vl/git/icann/qa/automated-tests/test-cases/sla-api/create-sla-and-cache-output.sh"
elif [[ $new_test_case_file =~ /data-export/ ]]; then
	create_script="/home/vl/git/icann/qa/automated-tests/test-cases/data-export/create-output.sh"
else
	echo "unexpected test case file \"$test_case_file\""
	exit 1
fi

sed -i -r "s|(\[compare-files\])|\"\",\"$create_script $num\"\n\1|" $new_test_case_file

TZ=UTC automated-tests/framework/run-tests.pl --skip-build --test-case-file $new_test_case_file $*
