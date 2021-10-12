#!/usr/bin/env bash

set -o errexit
set -o nounset
set -o pipefail
#set -o xtrace

DATADIR="/tmp"

if [ $# -ne 1 ]; then
	echo "usage: $0 <test-number>"
	echo "e. g.: $0 003"
	echo "will dump database data to $DATADIR/003.sql"
	exit 1
fi

num="$1"

. $(dirname $(readlink -f $0))/test-wrapper.conf

cmd="mysqldump -u $ZBX_SERVER_DB_USER --no-create-info $ZBX_SERVER_DB_NAME
events 
event_recovery
functions
hosts
triggers
items
"

[ -n "$ZBX_SERVER_DB_PASSWORD" ] && cmd+=" -p$ZBX_SERVER_DB_PASSWORD"

$cmd  > $DATADIR/$num.sql

echo "Database dump available in $DATADIR/$num.sql ."
