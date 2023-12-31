#!/usr/bin/env bash

set -o errexit
set -o nounset
set -o pipefail
#set -o xtrace

FRAMEWORK_DIR=$(realpath $(dirname $0))

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

sed -i '/\[compare-files\]/d' $new_test_case_file

create_script=
if [[ $new_test_case_file =~ /sla-api/ ]]; then
	create_script="$FRAMEWORK_DIR/../test-cases/sla-api/create-sla-and-cache-output.sh"

	sed -i -r 's|^("/opt/zabbix/sla","'$num'-sla-output.tar.gz")$|[execute]\n"","'$create_script' '$num'"\n[compare-files]\n\1|' $new_test_case_file
	sed -i -r 's|^("/opt/zabbix/sla","'$num'-sla-output-2.tar.gz")$|[execute]\n"","'$create_script' '$num' 2"\n[compare-files]\n\1|' $new_test_case_file
elif [[ $new_test_case_file =~ /data-export/ ]]; then
	create_script="$FRAMEWORK_DIR/../test-cases/data-export/create-output.sh"

	sed -i -r 's|^("/opt/zabbix/export","'$num'-output.tar.gz")$|[execute]\n"","'$create_script' '$num'"\n[compare-files]\n\1|' $new_test_case_file
elif [[ $new_test_case_file =~ /simple-check/ ]]; then
	create_script="$FRAMEWORK_DIR/../test-cases/simple-check/create-output.sh"

	sed -i -r 's|^("/tmp/simple-check-test","'$num'-output.tar.gz")$|[execute]\n"","'$create_script' '$num'"\n[compare-files]\n\1|' $new_test_case_file
else
	echo "unexpected test case file \"$test_case_file\""
	exit 1
fi

TZ=UTC automated-tests/framework/run-tests.pl --skip-build --test-case-file $new_test_case_file $*
