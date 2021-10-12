#!/usr/bin/env bash

set -o errexit
set -o nounset
set -o pipefail
#set -o xtrace

. $(dirname $(readlink -f $0))/test-wrapper.conf

SQL="
	select distinct e.eventid,from_unixtime(e.clock) as clock,h.host,e.name,e.value
	from events e,triggers t,functions f,items i,hosts h
	where e.source=0
		and e.objectid=t.triggerid
		and t.triggerid=f.triggerid
		and f.itemid=i.itemid
		and i.hostid=h.hostid
	order by e.eventid
"

cmd="mysql -t -u $ZBX_SERVER_DB_USER $ZBX_SERVER_DB_NAME"

[ -n "$ZBX_SERVER_DB_PASSWORD" ] && cmd+=" -p$ZBX_SERVER_DB_PASSWORD"

echo "$SQL" | $cmd

