#!/usr/bin/env bash

set -o errexit
set -o nounset
set -o pipefail
#set -o xtrace

TEST_CASE_DIR="automated-tests/test-cases"

test=
target=

stop=0
while [ $# -gt 0 ]; do
	case $1 in
		-s)
			target="sla-api"
			;;
		-d)
			target="data-export"
			;;
		-h|--help)
			echo "usage: $0 <-s|-d> n -- [test-wrapper.sh options]"
			exit
			;;
		--)
			stop=1
			;;
		*)
			test=$1
			;;
	esac

	shift

	if [ $stop -eq 1 ]; then
		break
	fi
done

if [ -z "$target" ]; then
	echo "usage: $0 [-s|-d] [number] [-- options]"
	echo
	echo "Run a test case. Specify additional options to test-wrapper script after --."
	echo " -s      SLA API test case"
	echo " -d      Data Export test case"
	echo " number  0-prefixed number of the test"
	echo
	echo "E. g.: $0 -s 3"
	echo
	echo "will run"
	echo
	echo "./test-wrapper --skip-build --test-case-file $TEST_CASE_DIR/sla-api/003-*.txt"
	exit 1
fi

EXTRA_ARGS=
while [ $# -gt 0 ]; do
	EXTRA_ARGS+="$1"
	shift
	if [ $# -gt 0 ]; then
		EXTRA_ARGS+=" "
	fi
done

sudo opt/zabbix/scripts/setup-cron.pl --disable-all --user vl 2>/dev/null || true
pkill zabbix_ 2>/dev/null || true

if [ -z "$test" ]; then
	automated-tests/framework/run-tests.pl --skip-build --test-case-dir $TEST_CASE_DIR/$target $EXTRA_ARGS
else
	test=$(printf "%03d" $test)
	automated-tests/framework/run-tests.pl --skip-build --test-case-file $TEST_CASE_DIR/$target/$test-*.txt $EXTRA_ARGS
fi

grep 'testcase name' test-results.xml -A1 | grep 'failure' -B1
