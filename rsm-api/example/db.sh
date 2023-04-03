#!/usr/bin/env bash

set -o errexit
set -o nounset
set -o pipefail
#set -o xtrace

: ${DB_NAME_SERVER:=rsm_server}
: ${DB_NAME_PROXY_PROBE1:=rsm_proxy_probe1}
: ${DB_NAME_PROXY_PROBE2:=rsm_proxy_probe2}
: ${DB_USER:=zabbix}
: ${DB_PASSWORD=password}

: ${DB_NAME_SERVER:=dimir_icann_feature_dev_921_extended_logging}
: ${DB_NAME_PROXY_PROBE1:=dimir_icann_feature_dev_921_extended_logging_prx}
: ${DB_NAME_PROXY_PROBE2:=dimir_icann_qa_test_prx2}
: ${DB_USER:=zabbix}
: ${DB_PASSWORD:=password}

declare monitoring_target
declare source_path

if [[ $# -eq 2 ]]; then
	source_path=$1
	monitoring_target=$2
else
	echo "Usage:"
	echo "$0 <src_path> <registry|registrar>"
	exit 1
fi

(
	cd "${source_path}/database/mysql/"

	export MYSQL_PWD="$DB_PASSWORD"

	echo 'Dropping databases...'
	mysql -u $DB_USER -e 'drop database '$DB_NAME_SERVER       || true
	mysql -u $DB_USER -e 'drop database '$DB_NAME_PROXY_PROBE1 || true
	mysql -u $DB_USER -e 'drop database '$DB_NAME_PROXY_PROBE2 || true

	echo 'Creating databases...'
	mysql -u $DB_USER -e 'create database '$DB_NAME_SERVER' character set utf8 collate utf8_bin'
	mysql -u $DB_USER -e 'create database '$DB_NAME_PROXY_PROBE1' character set utf8 collate utf8_bin'
	mysql -u $DB_USER -e 'create database '$DB_NAME_PROXY_PROBE2' character set utf8 collate utf8_bin'

	echo 'Filling server database...'
	mysql -u $DB_USER $DB_NAME_SERVER < schema.sql
	mysql -u $DB_USER $DB_NAME_SERVER < images.sql
	mysql -u $DB_USER $DB_NAME_SERVER < data.sql

	echo 'Filling proxy databases...'
	mysql -u $DB_USER $DB_NAME_PROXY_PROBE1 < schema.sql
	mysql -u $DB_USER $DB_NAME_PROXY_PROBE2 < schema.sql
)

echo 'Updating macros...'
/opt/zabbix/scripts/change-macro.pl --macro '{$RSM.MONITORING.TARGET}' --value "${monitoring_target}"
/opt/zabbix/scripts/change-macro.pl --macro '{$RSM.IP4.ROOTSERVERS1}' --value 193.0.14.129,192.5.5.241,199.7.83.42,198.41.0.4,192.112.36.4
/opt/zabbix/scripts/change-macro.pl --macro '{$RSM.IP6.ROOTSERVERS1}' --value 2001:7fe::53,2001:500:2f::f,2001:500:9f::42,2001:503:ba3e::2:30,2001:500:12::d0d
/opt/zabbix/scripts/change-macro.pl --macro '{$RSM.DNS.PROBE.ONLINE}' --value 1
/opt/zabbix/scripts/change-macro.pl --macro '{$RSM.RDDS.PROBE.ONLINE}' --value 1
/opt/zabbix/scripts/change-macro.pl --macro '{$RSM.RDAP.PROBE.ONLINE}' --value 1
/opt/zabbix/scripts/change-macro.pl --macro '{$RSM.EPP.PROBE.ONLINE}' --value 1
/opt/zabbix/scripts/change-macro.pl --macro '{$RSM.IP4.MIN.PROBE.ONLINE}' --value 1
/opt/zabbix/scripts/change-macro.pl --macro '{$RSM.IP6.MIN.PROBE.ONLINE}' --value 1
