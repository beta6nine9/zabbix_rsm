#!/usr/bin/env bash

set -e

TMP_FILE="/tmp/test-case-data.txt"

rm -f $TMP_FILE

LINE=
RV=
DEBUG=0
D_STATUS=1

D_DNS_UDP_RTT=5
D_DNS_TCP_RTT=15
D_RDDS43_RTT=20
D_RDDS80_RTT=50
D_RDAP_RTT=70
D_DNS_DOWN_CYCLES=10
D_RDDS_DOWN_CYCLES=2

dns_udp_rtt=
dns_tcp_rtt=
rdds43_rtt=
rdds80_rtt=
rdap_rtt=

usage()
{
	if [ -n "$1" ]; then
		echo "Error: $*"
	fi

	echo "usage: $0 [--down-cycles <dns>,<rdds>] [--dns-udp-rtt <rtt>] [--dns-tcp-rtt <rtt>] [--rdds43-rtt <rtt>] [--rdds80-rtt <rtt>] [--rdap-rtt <rtt>] [-d|--debug] [-h|--help]"
	exit 1
}

parse_opts()
{
	while [[ "$#" -gt 0 ]]; do
		case $1 in
			--down-cycles)
				shift

				if [ -z "$1" ]; then
					usage "--down-cycles requires an option"
				fi

				D_DNS_DOWN_CYCLES=${1%,*}
				D_RDDS_DOWN_CYCLES=${1#*,}

				D_STATUS=0

				[ -z "$dns_udp_rtt" ] && dns_udp_rtt=-416
				[ -z "$dns_tcp_rtt" ] && dns_tcp_rtt=-816
				[ -z "$rdds43_rtt" ]  && rdds43_rtt=-227
				[ -z "$rdds80_rtt" ]  && rdds80_rtt=-255
				[ -z "$rdap_rtt" ]    && rdap_rtt=-405

				;;
			--dns-udp-rtt)
				shift

				if [ -z "$1" ]; then
					usage "--dns-udp-rtt requires an option"
				fi

				dns_udp_rtt=$1

				;;
			--dns-tcp-rtt)
				shift

				if [ -z "$1" ]; then
					usage "--dns-tcp-rtt requires an option"
				fi

				dns_tcp_rtt=$1

				;;
			--rdds43-rtt)
				shift

				if [ -z "$1" ]; then
					usage "--rdds43-rtt requires an option"
				fi

				rdds43_rtt=$1

				;;
			--rdds80-rtt)
				shift

				if [ -z "$1" ]; then
					usage "--rdds80-rtt requires an option"
				fi

				rdds80_rtt=$1

				;;
			--rdap-rtt)
				shift

				if [ -z "$1" ]; then
					usage "--rdap-rtt requires an option"
				fi

				rdap_rtt=$1

				;;
			-d|--debug)
				DEBUG=1
				;;
			-h|--help)
				usage
				;;
			*)
				usage "Unknown command-line option: $1"
				;;
		esac
		shift
	done

	[ -z "$dns_udp_rtt" ] && dns_udp_rtt=$D_DNS_UDP_RTT
	[ -z "$dns_tcp_rtt" ] && dns_tcp_rtt=$D_DNS_TCP_RTT
	[ -z "$rdds43_rtt" ]  && rdds43_rtt=$D_RDDS43_RTT
	[ -z "$rdds80_rtt" ]  && rdds80_rtt=$D_RDDS80_RTT
	[ -z "$rdap_rtt" ]    && rdap_rtt=$D_RDAP_RTT

	# this is needed in order for the previous conditional
	# assignments work, otherwise "set -e" makes script fail
	true
}

dbg()
{
	if [ $DEBUG -ne 1 ]; then
		return
	fi
	echo DBG $*
}

ask()
{
	local question="$1"
	local default="$2"

	local ans=

	echo -n "$question [$default] "
	read ans
	[ -z "$ans" ] && ans="$default"

	RV="$ans"
}

ask_and_append_repeat()
{
	local question="$1"
	local line="$2"
	local default="$3"
	local repetitions="$4"

	if [ $repetitions -lt 1 ]; then
		return
	fi

	local ans=

	echo -n "$question [$default] "
	read ans
	[ -z "$ans" ] && ans="$default"

	LINE=$line
	for i in $(seq 1 $repetitions); do
		LINE+=,$ans
	done

	dbg $LINE

	echo $LINE >> $TMP_FILE
}

set_and_append_repeat()
{
	local question="$1"
	local value="$2"
	local repetitions="$3"

	if [ $repetitions -lt 1 ]; then
		return
	fi

	LINE=$question
	for i in $(seq 1 $repetitions); do
		LINE+=,$value
	done

	dbg $LINE

	echo $LINE >> $TMP_FILE
}

set_and_append_multi_repeat()
{
	local question="$1"
	local value1="$2"
	local value2="$3"
	local repetitions_value2="$4"
	local repetitions="$5"

	if [ $repetitions -lt 1 ]; then
		return
	fi
	
	LINE=$question
	for i in $(seq 1 $repetitions); do
		LINE+=,$value1
		for j in $(seq 1 $repetitions_value2); do
			LINE+=,$value2
		done
	done

	dbg $LINE

	echo $LINE >> $TMP_FILE
}

add_section()
{
	[ "$2" != "0" ] && echo >> $TMP_FILE
	echo "[$1]"             >> $TMP_FILE
	echo                    >> $TMP_FILE
}

add_header()
{
	cat << EOF >> $TMP_FILE
[test-case]

"<NAME>"

[prepare-server-database]

[set-global-macro]

"{\$RSM.MONITORING.TARGET}","registry"
"{\$RSM.IP4.ROOTSERVERS1}","193.0.14.129,192.5.5.241,199.7.83.42,198.41.0.4,192.112.36.4"
"{\$RSM.IP6.ROOTSERVERS1}","2001:7fe::53,2001:500:2f::f,2001:500:9f::42,2001:503:ba3e::2:30,2001:500:12::d0d"
"{\$RSM.DNS.PROBE.ONLINE}","2"
"{\$RSM.RDDS.PROBE.ONLINE}","2"
"{\$RSM.RDAP.PROBE.ONLINE}","2"
"{\$RSM.EPP.PROBE.ONLINE}","0"
"{\$RSM.IP4.MIN.PROBE.ONLINE}","2"
"{\$RSM.IP6.MIN.PROBE.ONLINE}","2"
"{\$RSM.INCIDENT.DNS.FAIL}","2"
"{\$RSM.INCIDENT.DNS.RECOVER}","2"
"{\$RSM.INCIDENT.DNSSEC.FAIL}","2"
"{\$RSM.INCIDENT.DNSSEC.RECOVER}","2"
"{\$RSM.INCIDENT.RDDS.FAIL}","2"
"{\$RSM.INCIDENT.RDDS.RECOVER}","2"
"{\$RSM.INCIDENT.RDAP.FAIL}","2"
"{\$RSM.INCIDENT.RDAP.RECOVER}","2"

EOF
}

add_probes()
{
	add_section "create-probe" 0

	# ipv4, ipv6, rdds, rdap
	cat << EOF >> $TMP_FILE
"Probe1-Server1","127.0.0.1","10061",1,0,1,1
"Probe2-Server1","127.0.0.1","10062",1,0,1,1
EOF
}

add_disable_triggers()
{
	cat << EOF >> $TMP_FILE

# disable other than RSM Service Availability triggers to ensure their event IDs are consistent
[execute-sql-query]

"update triggers set status=1 where templateid is not null"
EOF
}

add_tlds()
{
	# tld,dns_test_prefix,type,dnssec,dns_udp,dns_tcp,ns_servers_v4,ns_servers_v6,rdds43_servers,rdds80_servers,rdap_base_url,rdap_test_domain,rdds_test_prefix
	add_section "create-tld"

	cat << EOF >> $TMP_FILE
"tld1","test","ccTLD",0,1,1,"ns1.tld1,192.0.2.11 ns2.tld1,192.0.2.12 ns3.tld1,192.0.2.13 ns4.tld1,192.0.2.14","","","","","",""
"tld2","test","ccTLD",1,1,1,"ns1.tld2,192.0.2.31 ns2.tld2,192.0.2.32 ns3.tld2,192.0.2.33 ns4.tld2,192.0.2.34 ns5.tld2,192.0.2.35","","rdds43.nic.tld2","whois.nic.tld2","","","nic.tld2"
"tld3","test","ccTLD",1,1,1,"ns1.tld3,192.0.2.51 ns2.tld3,192.0.2.52 ns3.tld3,192.0.2.53 ns4.tld3,192.0.2.54 ns5.tld3,192.0.2.55 ns6.tld3,192.0.2.56","","rdds43.nic.tld3","whois.nic.tld3","https://rdap.nic.tld3:8443/rdap/","nic.tld3.","nic.tld3"
EOF
}

add_globalmacro_history()
{
	cat << EOF >> $TMP_FILE
"Global macro history","rsm.configvalue[RSM.DNS.DELAY]",60,1611252048,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60
"Global macro history","rsm.configvalue[RSM.DNS.ROLLWEEK.SLA]",60,1611252053,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240
"Global macro history","rsm.configvalue[RSM.DNS.TCP.RTT.HIGH]",60,1611252035,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500,7500
"Global macro history","rsm.configvalue[RSM.DNS.TCP.RTT.LOW]",60,1611252007,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500,1500
"Global macro history","rsm.configvalue[RSM.DNS.UDP.RTT.HIGH]",60,1611252051,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500,2500
"Global macro history","rsm.configvalue[RSM.DNS.UDP.RTT.LOW]",60,1611252008,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500,500
"Global macro history","rsm.configvalue[RSM.EPP.DELAY]",60,1611252050,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60
"Global macro history","rsm.configvalue[RSM.EPP.ROLLWEEK.SLA]",60,1611252055,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60
"Global macro history","rsm.configvalue[RSM.INCIDENT.DNS.FAIL]",60,1611252040,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3
"Global macro history","rsm.configvalue[RSM.INCIDENT.DNS.RECOVER]",60,1611252041,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3
"Global macro history","rsm.configvalue[RSM.INCIDENT.DNSSEC.FAIL]",60,1611252042,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3
"Global macro history","rsm.configvalue[RSM.INCIDENT.DNSSEC.RECOVER]",60,1611252043,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3
"Global macro history","rsm.configvalue[RSM.INCIDENT.EPP.FAIL]",60,1611252046,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2
"Global macro history","rsm.configvalue[RSM.INCIDENT.EPP.RECOVER]",60,1611252047,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2
"Global macro history","rsm.configvalue[RSM.INCIDENT.RDAP.FAIL]",60,1611252012,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2
"Global macro history","rsm.configvalue[RSM.INCIDENT.RDAP.RECOVER]",60,1611252013,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2
"Global macro history","rsm.configvalue[RSM.INCIDENT.RDDS.FAIL]",60,1611252044,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2
"Global macro history","rsm.configvalue[RSM.INCIDENT.RDDS.RECOVER]",60,1611252045,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2
"Global macro history","rsm.configvalue[RSM.RDAP.DELAY]",60,1611252014,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300
"Global macro history","rsm.configvalue[RSM.RDAP.MAXREDIRS]",60,1611252015,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10
"Global macro history","rsm.configvalue[RSM.RDAP.ROLLWEEK.SLA]",60,1611252017,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440
"Global macro history","rsm.configvalue[RSM.RDAP.RTT.HIGH]",60,1611252018,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000
"Global macro history","rsm.configvalue[RSM.RDAP.RTT.LOW]",60,1611252019,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000,5000
"Global macro history","rsm.configvalue[RSM.RDDS.DELAY]",60,1611252049,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300
"Global macro history","rsm.configvalue[RSM.RDDS.ROLLWEEK.SLA]",60,1611252054,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440,1440
"Global macro history","rsm.configvalue[RSM.RDDS.RTT.HIGH]",60,1611252036,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000,10000
"Global macro history","rsm.configvalue[RSM.RDDS.RTT.LOW]",60,1611252011,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000,2000
"Global macro history","rsm.configvalue[RSM.SLV.DNS.DOWNTIME]",60,1611252006,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
"Global macro history","rsm.configvalue[RSM.SLV.DNS.NS.UPD]",60,1611252002,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99
"Global macro history","rsm.configvalue[RSM.SLV.DNS.TCP.RTT]",60,1611252057,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5
"Global macro history","rsm.configvalue[RSM.SLV.DNS.UDP.RTT]",60,1611252056,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5
"Global macro history","rsm.configvalue[RSM.SLV.EPP.INFO]",60,1611252005,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99
"Global macro history","rsm.configvalue[RSM.SLV.EPP.LOGIN]",60,1611252003,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99
"Global macro history","rsm.configvalue[RSM.SLV.EPP.UPDATE]",60,1611252004,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99
"Global macro history","rsm.configvalue[RSM.SLV.NS.DOWNTIME]",60,1611252058,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432,432
"Global macro history","rsm.configvalue[RSM.SLV.RDAP.DOWNTIME]",60,1611252020,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864
"Global macro history","rsm.configvalue[RSM.SLV.RDAP.RTT]",60,1611252021,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5
"Global macro history","rsm.configvalue[RSM.SLV.RDDS.DOWNTIME]",60,1611252009,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864,864
"Global macro history","rsm.configvalue[RSM.SLV.RDDS.RTT]",60,1611252010,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5
"Global macro history","rsm.configvalue[RSM.SLV.RDDS.UPD]",60,1611252001,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99
"Global macro history","rsm.configvalue[RSM.SLV.RDDS43.RTT]",60,1611252059,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5
"Global macro history","rsm.configvalue[RSM.SLV.RDDS80.RTT]",60,1611252000,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5
EOF
}

add_probe_statuses_history()
{
	ts=1611252205
	for service in dns epp ip4 ip6 rdds rdap; do
		cat << EOF >> $TMP_FILE
"Probe statuses","online.nodes.pl[total,$service]",300,$ts,2,2,2,2,2,2
EOF
		let ts=$ts+1
	done

	ts=1611252020
	for service in dns epp ip4 ip6 rdds rdap; do
		ask_and_append_repeat "ONLINE probes for $service" '"Probe statuses","online.nodes.pl[online,'$service']",60,'$ts 2 30

		let ts=$ts+1
	done
}

add_probe_online_history()
{
	ask_and_append_repeat "Probe1-Server1 online" '"Probe1-Server1 - mon","rsm.probe.online",60,1611252000' 1 30
	cat << EOF >> $TMP_FILE
"Probe1-Server1 - mon","zabbix[proxy,{\$RSM.PROXY_NAME},lastaccess]",60,1611252045,1611252044,1611252104,1611252164,1611252224,1611252284,1611252344,1611252404,1611252464,1611252524,1611252584,1611252644,1611252704,1611252764,1611252824,1611252884,1611252944,1611253004,1611253065,1611253119,1611253179,1611253239,1611253299,1611253365,1611253419,1611253479,1611253539,1611253599,1611253660,1611253720,1611253780,1611253839,1611253899,1611253960,1611254020,1611254080,1611254140,1611254200,1611254260,1611254321,1611254381,1611254441,1611254501,1611254561,1611254621,1611254682,1611254742,1611254801,1611254861,1611254922,1611254982,1611255043,1611255103,1611255163,1611255223,1611255283,1611255343,1611255403,1611255462,1611255522,1611255583
EOF

	ask_and_append_repeat "Probe2-Server1 online" '"Probe2-Server1 - mon","rsm.probe.online",60,1611252000' 1 30
	cat << EOF >> $TMP_FILE
"Probe2-Server1 - mon","zabbix[proxy,{\$RSM.PROXY_NAME},lastaccess]",60,1611252045,1611252044,1611252104,1611252164,1611252224,1611252284,1611252344,1611252404,1611252464,1611252524,1611252584,1611252644,1611252704,1611252764,1611252824,1611252884,1611252944,1611253004,1611253065,1611253119,1611253179,1611253239,1611253299,1611253365,1611253419,1611253479,1611253539,1611253599,1611253660,1611253720,1611253780,1611253839,1611253899,1611253960,1611254020,1611254080,1611254140,1611254200,1611254260,1611254321,1611254381,1611254441,1611254501,1611254561,1611254621,1611254682,1611254742,1611254801,1611254861,1611254922,1611254982,1611255043,1611255103,1611255163,1611255223,1611255283,1611255343,1611255403,1611255462,1611255522,1611255583
EOF
}

add_tld_enabled_history()
{
	empty_line=0
	for tld in tld1 tld2 tld3; do
		if [ $empty_line -eq 1 ]; then
			echo >> $TMP_FILE
		else
			empty_line=1
		fi
		for service in dns.tcp dns.udp dnssec rdds rdap; do
			status=
			if [[ $tld = tld1 && $service = dnssec ]]; then
				status=0
			elif [[ $tld = tld1 && $service = rdds ]]; then
				status=0
			elif [[ $tld != tld3 && $service = rdap ]]; then
				status=0
			else
				status=1
			fi

			ask_and_append_repeat "$tld $service.enabled" '"'$tld'","'$service'.enabled",60,1611252054' $status 30
		done
	done
}

add_probe_history()
{
	local tld1_nss=(
		ns1.tld1
		ns2.tld1
		ns3.tld1
		ns4.tld1
	)

	local tld1_nsips=(
		ns1.tld1,192.0.2.11
		ns2.tld1,192.0.2.12
		ns3.tld1,192.0.2.13
		ns4.tld1,192.0.2.14
	)

	local tld2_nss=(
		ns1.tld2
		ns2.tld2
		ns3.tld2
		ns4.tld2
		ns5.tld2
	)

	local tld2_nsips=(
		ns1.tld2,192.0.2.31
		ns2.tld2,192.0.2.32
		ns3.tld2,192.0.2.33
		ns4.tld2,192.0.2.34
		ns5.tld2,192.0.2.35
	)

	local tld3_nss=(
		ns1.tld3
		ns2.tld3
		ns3.tld3
		ns4.tld3
		ns5.tld3
		ns6.tld3
	)

	local tld3_nsips=(
		ns1.tld3,192.0.2.51
		ns2.tld3,192.0.2.52
		ns3.tld3,192.0.2.53
		ns4.tld3,192.0.2.54
		ns5.tld3,192.0.2.55
		ns6.tld3,192.0.2.56
	)

	local tld1_nprotocol=0
	local tld2_nprotocol=1
	local tld3_nprotocol=0

	local tld1_protocol=udp
	local tld2_protocol=tcp
	local tld3_protocol=udp

	local nprotocol
	local protocol
	local unused_protocol

	local fail_cycles
	local ok_cycles

	local ts
	local rtt
	local default_rtt

	local dns_cycles_period

	for tld in tld1 tld2 tld3; do
		declare -n nss=${tld}_nss
		declare -n nsips=${tld}_nsips
		declare nprotocolp=${tld}_nprotocol
		declare protocolp=${tld}_protocol

		nprotocol=${!nprotocolp}
		protocol=${!protocolp}

		[ $protocol = udp ] && unused_protocol=tcp || unused_protocol=udp

		for probe in Probe1-Server1 Probe2-Server1; do
			[ $nprotocol = 0 ] && default_rtt=$dns_udp_rtt || default_rtt=$dns_tcp_rtt

			ask "$tld $probe dns rtt (critical $protocol)" $default_rtt

			rtt="$RV"

			ts=1611252030
			ok_cycles=30

			if [ "$rtt" -lt 0 ]; then
				ask "cycles long (0-30)" $D_DNS_DOWN_CYCLES
				fail_cycles=$RV
				ok_cycles=$((30-fail_cycles))

				# FAIL cycles
				set_and_append_repeat '"'$tld' '$probe'","rsm.dns.mode",60,'$ts 1 $fail_cycles

				for ns in "${nss[@]}"; do
					set_and_append_repeat '"'$tld' '$probe'","rsm.dns.ns.status['$ns']",60,'$ts 0 $fail_cycles
				done

				set_and_append_repeat '"'$tld' '$probe'","rsm.dns.nssok",60,'$ts 0 $fail_cycles
				set_and_append_repeat '"'$tld' '$probe'","rsm.dns.protocol",60,'$ts $nprotocol $fail_cycles

				for nsip in "${nsips[@]}"; do
					set_and_append_repeat '"'$tld' '$probe'","rsm.dns.rtt['$nsip','$protocol']",60,'$ts $rtt $fail_cycles
					set_and_append_repeat '"'$tld' '$probe'","rsm.dns.rtt['$nsip','$unused_protocol']",60,'$ts '' $fail_cycles
				done

				set_and_append_repeat '"'$tld' '$probe'","rsm.dns.status",60,'$ts 0 $fail_cycles

				# DNSSEC
				if [[ $rtt -le -401 && $rtt -ge -427 ]] || [[ $rtt -le -801 && $rtt -ge -827 ]]; then
					set_and_append_repeat '"'$tld' '$probe'","rsm.dnssec.status",60,'$ts 0 $fail_cycles
				fi

				((ts+=fail_cycles*60))
			fi

			# OK cycles
			if [ $(($ok_cycles % 10)) -ne 0 ]; then
				echo "error: dns cycles should be devisible by 10"
				exit 1
			fi

			if [ "$rtt" -lt 0 ]; then
				rtt=5
			fi

			dns_cycles_period=$(($ok_cycles/10))

			set_and_append_repeat '"'$tld' '$probe'","rsm.dns.mode",60,'$ts 0 $ok_cycles

			for ns in "${nss[@]}"; do
				set_and_append_repeat '"'$tld' '$probe'","rsm.dns.ns.status['$ns']",60,'$ts 1 $ok_cycles
			done

			set_and_append_repeat '"'$tld' '$probe'","rsm.dns.nssok",60,'$ts "${#p[@]}" $ok_cycles
			set_and_append_multi_repeat '"'$tld' '$probe'","rsm.dns.protocol",60,'$ts 1 0 9 $dns_cycles_period

			for nsip in "${nsips[@]}"; do
				set_and_append_multi_repeat '"'$tld' '$probe'","rsm.dns.rtt['$nsip',udp]",60,'$ts '' $rtt 9 $dns_cycles_period
				set_and_append_multi_repeat '"'$tld' '$probe'","rsm.dns.rtt['$nsip',tcp]",60,'$ts $rtt '' 9 $dns_cycles_period
			done

			set_and_append_repeat '"'$tld' '$probe'","rsm.dns.status",60,'$ts 1 $ok_cycles
			set_and_append_repeat '"'$tld' '$probe'","rsm.dnssec.status",60,'$ts 1 $ok_cycles

			if [ $tld = "tld1" ]; then
				continue
			fi

			ask "$tld $probe rdds status" $D_STATUS

			ts=1611252150
			ok_cycles=6
			fail_cycles=0

			if [ "$RV" = 0 ]; then
				ask "cycles long (0-6)" $D_RDDS_DOWN_CYCLES
				fail_cycles=$RV
				ok_cycles=$((6-fail_cycles))

				# FAIL cycles
				set_and_append_repeat '"'$tld' '$probe'","rsm.rdds.status",300,'$ts 0 $fail_cycles

				((ts+=fail_cycles*300))
			fi

			# OK cycles
			set_and_append_repeat '"'$tld' '$probe'","rsm.rdds.status",300,'$ts 1 $ok_cycles

			ask "$tld $probe rdds43 rtt" $rdds43_rtt

			rtt="$RV"

			ts=1611252150

			if [ "$rtt" -lt 0 ]; then
				# FAIL cycles
				set_and_append_repeat '"'$tld' '$probe'","rsm.rdds.43.rtt",300,'$ts $rtt $fail_cycles
				set_and_append_repeat '"'$tld' '$probe'","rsm.rdds.43.status",300,'$ts 0 $fail_cycles

				((ts+=fail_cycles*300))
				rtt=20
			fi

			set_and_append_repeat '"'$tld' '$probe'","rsm.rdds.43.rtt",300,'$ts $rtt $ok_cycles
			set_and_append_repeat '"'$tld' '$probe'","rsm.rdds.43.status",300,'$ts 1 $ok_cycles

			ask "$tld $probe rdds80 rtt" $rdds80_rtt

			rtt="$RV"

			ts=1611252150

			if [ "$rtt" -lt 0 ]; then
				# FAIL cycles
				set_and_append_repeat '"'$tld' '$probe'","rsm.rdds.80.rtt",300,'$ts $rtt $fail_cycles
				set_and_append_repeat '"'$tld' '$probe'","rsm.rdds.80.status",300,'$ts 0 $fail_cycles

				((ts+=fail_cycles*300))
				rtt=50
			fi

			set_and_append_repeat '"'$tld' '$probe'","rsm.rdds.80.rtt",300,'$ts $rtt $ok_cycles
			set_and_append_repeat '"'$tld' '$probe'","rsm.rdds.80.status",300,'$ts 1 $ok_cycles

			cat << EOF >> $TMP_FILE
"$tld $probe","rsm.rdds.43.ip",300,1611252150,127.0.0.1,127.0.0.1,127.0.0.1,127.0.0.1,127.0.0.1,127.0.0.1
"$tld $probe","rsm.rdds.43.target",300,1611252150,whois.nic.$tld,whois.nic.$tld,whois.nic.$tld,whois.nic.$tld,whois.nic.$tld,whois.nic.$tld
"$tld $probe","rsm.rdds.43.testedname",300,1611252150,nic.$tld,nic.$tld,nic.$tld,nic.$tld,nic.$tld,nic.$tld
"$tld $probe","rsm.rdds.80.ip",300,1611252150,127.0.0.1,127.0.0.1,127.0.0.1,127.0.0.1,127.0.0.1,127.0.0.1
"$tld $probe","rsm.rdds.80.target",300,1611252150,whois.nic.$tld,whois.nic.$tld,whois.nic.$tld,whois.nic.$tld,whois.nic.$tld,whois.nic.$tld
EOF
			if [ $tld != "tld3" ]; then
				continue
			fi

			ask "$tld $probe rdap rtt" $rdap_rtt

			rtt="$RV"

			ts=1611252150
			ok_cycles=6
			fail_cycles=0

			if [ "$rtt" -lt 0 ]; then
				ask "cycles long (0-6)" $D_RDDS_DOWN_CYCLES
				fail_cycles=$RV
				ok_cycles=$((6-fail_cycles))

				# FAIL cycles
				set_and_append_repeat '"'$tld' '$probe'","rdap.rtt",300,'$ts $rtt $fail_cycles
				set_and_append_repeat '"'$tld' '$probe'","rdap.status",300,'$ts 0 $fail_cycles

				((ts+=fail_cycles*300))
				rtt=70
			fi

			set_and_append_repeat '"'$tld' '$probe'","rdap.rtt",300,'$ts $rtt $ok_cycles
			set_and_append_repeat '"'$tld' '$probe'","rdap.status",300,'$ts 1 $ok_cycles

			cat << EOF >> $TMP_FILE
"$tld $probe","rdap.ip",300,1611252150,195.287.30.90,195.287.30.90,195.287.30.90,195.287.30.90,195.287.30.90,195.287.30.90
"$tld $probe","rdap.target",300,1611252150,https://rdap.nic.$tld:8443/rdap/,https://rdap.nic.$tld:8443/rdap/,https://rdap.nic.$tld:8443/rdap/,https://rdap.nic.$tld:8443/rdap/,https://rdap.nic.$tld:8443/rdap/,https://rdap.nic.$tld:8443/rdap/
"$tld $probe","rdap.testedname",300,1611252150,nic.$tld.,nic.$tld.,nic.$tld.,nic.$tld.,nic.$tld.,nic.$tld.
EOF
		done
	done

	cat << EOF >> $TMP_FILE

"tld1 Probe1-Server1","rsm.dns.nsid[ns1.tld1,192.0.2.11]",60,1611252030,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61
"tld1 Probe1-Server1","rsm.dns.nsid[ns2.tld1,192.0.2.12]",60,1611252030,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
"tld1 Probe1-Server1","rsm.dns.nsid[ns3.tld1,192.0.2.13]",60,1611252030,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76
"tld1 Probe1-Server1","rsm.dns.nsid[ns4.tld1,192.0.2.14]",60,1611252030,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761
"tld1 Probe1-Server1","rsm.dns.protocol",60,1611252030,1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0
"tld1 Probe1-Server1","rsm.dns.testedname",60,1611252030,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.
"tld1 Probe2-Server1","rsm.dns.nsid[ns1.tld1,192.0.2.11]",60,1611252000,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61,6163322e61
"tld1 Probe2-Server1","rsm.dns.nsid[ns2.tld1,192.0.2.12]",60,1611252000,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
"tld1 Probe2-Server1","rsm.dns.nsid[ns3.tld1,192.0.2.13]",60,1611252000,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76,622e6e69632e6c76
"tld1 Probe2-Server1","rsm.dns.nsid[ns4.tld1,192.0.2.14]",60,1611252000,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761,61632d72696761
"tld1 Probe2-Server1","rsm.dns.testedname",60,1611252000,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.,test.tld1.

"tld2 Probe1-Server1","rsm.dns.nsid[ns1.tld2,192.0.2.31]",60,1611252048,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
"tld2 Probe1-Server1","rsm.dns.nsid[ns2.tld2,192.0.2.32]",60,1611252048,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
"tld2 Probe1-Server1","rsm.dns.nsid[ns3.tld2,192.0.2.33]",60,1611252048,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368
"tld2 Probe1-Server1","rsm.dns.nsid[ns4.tld2,192.0.2.34]",60,1611252048,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c
"tld2 Probe1-Server1","rsm.dns.nsid[ns5.tld2,192.0.2.35]",60,1611252048,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
"tld2 Probe1-Server1","rsm.dns.testedname",60,1611252048,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.
"tld2 Probe2-Server1","rsm.dns.nsid[ns1.tld2,192.0.2.31]",60,1611252018,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
"tld2 Probe2-Server1","rsm.dns.nsid[ns2.tld2,192.0.2.32]",60,1611252018,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
"tld2 Probe2-Server1","rsm.dns.nsid[ns3.tld2,192.0.2.33]",60,1611252018,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368,312e746c6c2e706368
"tld2 Probe2-Server1","rsm.dns.nsid[ns4.tld2,192.0.2.34]",60,1611252018,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c,73322e746c6c
"tld2 Probe2-Server1","rsm.dns.nsid[ns5.tld2,192.0.2.35]",60,1611252018,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
"tld2 Probe2-Server1","rsm.dns.testedname",60,1611252018,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.,test.tld2.

"tld3 Probe1-Server1","rsm.dns.nsid[ns1.tld3,192.0.2.51]",60,1611252010,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
"tld3 Probe1-Server1","rsm.dns.nsid[ns2.tld3,192.0.2.52]",60,1611252010,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
"tld3 Probe1-Server1","rsm.dns.nsid[ns3.tld3,192.0.2.53]",60,1611252010,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354
"tld3 Probe1-Server1","rsm.dns.nsid[ns4.tld3,192.0.2.54]",60,1611252010,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
"tld3 Probe1-Server1","rsm.dns.nsid[ns5.tld3,192.0.2.55]",60,1611252010,4141,4141,4142,4142,4141,4141,4142,4141,4141,4141,4141,4141,4141,4142,4142,4142,4141,4142,4142,4141,4141,4141,4141,4141,4142,4141,4142,4142,4142,4141,4141,4142,4141,4142,4142,4141,4141,4142,4142,4141,4142,4141,4142,4142,4141,4141,4141,4141,4141,4142,4141,4141,4141,4141,4141,4142,4142,4142,4141,4141
"tld3 Probe1-Server1","rsm.dns.nsid[ns6.tld3,192.0.2.56]",60,1611252010,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
"tld3 Probe1-Server1","rsm.dns.testedname",60,1611252010,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.
"tld3 Probe2-Server1","rsm.dns.nsid[ns1.tld3,192.0.2.51]",60,1611252040,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
"tld3 Probe2-Server1","rsm.dns.nsid[ns2.tld3,192.0.2.52]",60,1611252040,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
"tld3 Probe2-Server1","rsm.dns.nsid[ns3.tld3,192.0.2.53]",60,1611252040,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354,424c4354
"tld3 Probe2-Server1","rsm.dns.nsid[ns4.tld3,192.0.2.54]",60,1611252040,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
"tld3 Probe2-Server1","rsm.dns.nsid[ns5.tld3,192.0.2.55]",60,1611252040,4142,4141,4142,4142,4141,4141,4142,4141,4142,4141,4141,4141,4142,4142,4142,4141,4141,4141,4141,4142,4142,4141,4142,4141,4142,4141,4142,4142,4141,4141,4141,4142,4142,4141,4142,4141,4141,4141,4141,4141,4142,4142,4141,4142,4142,4141,4141,4141,4142,4142,4141,4141,4141,4141,4142,4142,4142,4142,4141,4141
"tld3 Probe2-Server1","rsm.dns.nsid[ns6.tld3,192.0.2.56]",60,1611252040,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
"tld3 Probe2-Server1","rsm.dns.testedname",60,1611252040,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.,test.tld3.
EOF
}

add_history()
{
	add_section "fill-history"
	add_globalmacro_history

	echo >> $TMP_FILE
	add_probe_statuses_history

	echo >> $TMP_FILE
	add_probe_online_history

	echo >> $TMP_FILE
	add_tld_enabled_history

	echo >> $TMP_FILE
	add_probe_history
}

add_lastvalues()
{
	add_section "fix-lastvalue-tables"
	add_section "set-lastvalue" 0

	cat << EOF >> $TMP_FILE
"tld1","rsm.slv.dns.avail","1611251940","0"
"tld1","rsm.slv.dns.rollweek","1611251940","0"
"tld1","rsm.slv.dnssec.avail","1611251940","0"
"tld1","rsm.slv.dnssec.rollweek","1611251940","0"
"tld1","rsm.slv.rdds.avail","1611251700","0"
"tld1","rsm.slv.rdds.rollweek","1611251700","0"
"tld1","rsm.slv.dns.ns.avail[ns1.tld1,192.0.2.11]","1611251940","0"
"tld1","rsm.slv.dns.ns.avail[ns2.tld1,192.0.2.12]","1611251940","0"
"tld1","rsm.slv.dns.ns.avail[ns3.tld1,192.0.2.13]","1611251940","0"
"tld1","rsm.slv.dns.ns.avail[ns4.tld1,192.0.2.14]","1611251940","0"

"tld2","rsm.slv.dns.avail","1611251940","0"
"tld2","rsm.slv.dns.rollweek","1611251940","0"
"tld2","rsm.slv.dnssec.avail","1611251940","0"
"tld2","rsm.slv.dnssec.rollweek","1611251940","0"
"tld2","rsm.slv.rdds.avail","1611251700","0"
"tld2","rsm.slv.rdds.rollweek","1611251700","0"
"tld2","rsm.slv.dns.ns.avail[ns1.tld2,192.0.2.31]","1611251940","0"
"tld2","rsm.slv.dns.ns.avail[ns2.tld2,192.0.2.32]","1611251940","0"
"tld2","rsm.slv.dns.ns.avail[ns3.tld2,192.0.2.33]","1611251940","0"
"tld2","rsm.slv.dns.ns.avail[ns4.tld2,192.0.2.34]","1611251940","0"
"tld2","rsm.slv.dns.ns.avail[ns5.tld2,192.0.2.35]","1611251940","0"

"tld3","rsm.slv.dns.avail","1611251940","0"
"tld3","rsm.slv.dns.rollweek","1611251940","0"
"tld3","rsm.slv.dnssec.avail","1611251940","0"
"tld3","rsm.slv.dnssec.rollweek","1611251940","0"
"tld3","rsm.slv.rdds.avail","1611251700","0"
"tld3","rsm.slv.rdds.rollweek","1611251700","0"
"tld3","rsm.slv.rdap.avail","1611251700","0"
"tld3","rsm.slv.rdap.rollweek","1611251700","0"
"tld3","rsm.slv.dns.ns.avail[ns1.tld3,192.0.2.51]","1611251940","0"
"tld3","rsm.slv.dns.ns.avail[ns2.tld3,192.0.2.52]","1611251940","0"
"tld3","rsm.slv.dns.ns.avail[ns3.tld3,192.0.2.53]","1611251940","0"
"tld3","rsm.slv.dns.ns.avail[ns4.tld3,192.0.2.54]","1611251940","0"
"tld3","rsm.slv.dns.ns.avail[ns5.tld3,192.0.2.55]","1611251940","0"
"tld3","rsm.slv.dns.ns.avail[ns6.tld3,192.0.2.56]","1611251940","0"
EOF
}

add_slv_calls()
{
	cat << EOF >> $TMP_FILE

[start-server]

"2021-01-23 00:00:00"

[execute]

"2021-01-21 18:40:00","/opt/zabbix/scripts/slv/rsm.slv.dns.avail.pl       --cycles 30"
"2021-01-21 18:40:00","/opt/zabbix/scripts/slv/rsm.slv.dns.rollweek.pl    --cycles 30"
"2021-01-21 18:40:00","/opt/zabbix/scripts/slv/rsm.slv.dnssec.avail.pl    --cycles 30"
"2021-01-21 18:40:00","/opt/zabbix/scripts/slv/rsm.slv.dnssec.rollweek.pl --cycles 30"
"2021-01-21 18:40:00","/opt/zabbix/scripts/slv/rsm.slv.rdds.avail.pl      --cycles 6"
"2021-01-21 18:40:00","/opt/zabbix/scripts/slv/rsm.slv.rdds.rollweek.pl   --cycles 6"
"2021-01-21 18:40:00","/opt/zabbix/scripts/slv/rsm.slv.rdap.avail.pl      --cycles 6"
"2021-01-21 18:40:00","/opt/zabbix/scripts/slv/rsm.slv.rdap.rollweek.pl   --cycles 6"
"2021-01-21 18:40:00","/opt/zabbix/scripts/slv/rsm.slv.dns.ns.avail.pl    --cycles 30"

[stop-server]
EOF
}

parse_opts $@

echo dns_udp_rtt=$dns_udp_rtt
echo dns_tcp_rtt=$dns_tcp_rtt
echo rdds43_rtt=$rdds43_rtt
echo rdds80_rtt=$rdds80_rtt
echo rdap_rtt=$rdap_rtt

add_header
add_probes
add_disable_triggers
add_tlds
add_history
add_lastvalues
add_slv_calls

cat $TMP_FILE
