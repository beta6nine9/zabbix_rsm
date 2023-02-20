#!/bin/bash

set -o errexit

for tool in nameserver rdap-server rdds43-server rdds80-server resolver; do
	pid_file="/tmp/$tool.pid"

	if [ -f $pid_file ]; then
		pid=$(cat $pid_file)

		if [ -n "$pid" ]; then
			if ps $pid >/dev/null 2>&1; then
				kill $pid
			fi
		fi

		rm $pid_file
	fi
done
