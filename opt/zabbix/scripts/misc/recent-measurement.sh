#!/bin/bash

BASE="/opt/zabbix/sla"

d_version="2"
d_tld="tld1"
d_service="dns"
d_date=$(date +%Y/%m/%d)
d_last=1
d_move=0

usage()
{
	[ -n "$1" ] && echo "Error: $*"
	echo "usage   : $0 [-v version] [-t tld] [-s service] [-d YYYY/MM/DD] [-l LAST] [-m MOVE]"
	echo "options :"
	echo "    -v VERSION (default: $d_version)"
	echo "    -t TLD (default: $d_tld)"
	echo "    -s SERVICE (default: $d_service)"
	echo "    -d DATE (default: $d_date)"
	echo "    -l LAST measurements (default: $d_last)"
	echo "    -m CYCLES to go back from the LAST  measurements (default: $d_move)"
	exit 1
}

die()
{
	echo "Error: $*"
	exit 1
}

version=$d_version
tld=$d_tld
service=$d_service
date=$d_date
last=$d_last
move=$d_move

while [ -n "$1" ]; do
	case "$1" in
		-v)
			shift
			[ -z "$1" ] && usage
			version=$1
			;;
		-t)
			shift
			[ -z "$1" ] && usage
			tld=$1
			;;
		-s)
			shift
			[ -z "$1" ] && usage
			[[ $1 = "dns" || $1 = "dnssec" || $1 = "rdds" || $1 = "rdap" || $1 = "epp" ]] || usage "$1: unknown Service (expected: dns, dnssec, rdds, rdap or epp)"
			service=$1
			;;
		-d)
			shift
			[ -z "$1" ] && usage
			date=$1
			;;
		-l)
			shift
			[ -z "$1" ] && usage
			[ $1 -gt 0 ] || usage "-l $1: last value must be greater than 0"
			last=$1
			;;
		-m)
			shift
			[ -z "$1" ] && usage
			move=$1
			;;
		--)
			shift
			# stop parsing args
			break
			;;
		*)
			usage
			;;
	esac

	shift
done

[[ -n "$1" && $1 = "-h" ]] && usage

base="$BASE/v$version/$tld/monitoring/$service/measurements/$date"

[ -d $base ] || usage "$base - no such directory"

files=
if [ $move -eq 0 ]; then
	files=$(ls $base/*.json | tail -$last)
else
	let last_with_move=$last+move

	files=$(ls $base/*.json | tail -$last_with_move | head -$last)
fi

[ -n "$files" ] || die "directory $base is empty"

for file in $files; do
	ts=${file##*/}
	ts=${ts%.json}

	date="$(date '+%F %X' -d @$ts)"

	echo -n "$date; "
	ls -l $file

	# fix timestamps
	cp $file -f $file.new
	egrep -o --color=none '1[0-9]{9}' $file | while read ts; do
		hr=$(date +'%Y-%m-%d %H:%M:%S' -d @$ts)
		sed -ri "s/$ts/\"$hr\"/g" $file.new
	done

	cat $file.new | jq -SC .
	rm -f $file.new
done
