#!/usr/bin/env bash

set -o errexit
set -o nounset
set -o pipefail
#set -o xtrace

. test-wrapper.conf

rm -rf build logs source source@tmp
./test-wrapper --build-server --build-proxy

cp -f source/ui/conf/zabbix.conf.php.example source/ui/conf/zabbix.conf.php

sed -i "s|.*DATABASE.*= .*|\$DB['DATABASE'] = '$ZBX_SERVER_DB_NAME';|"     source/ui/conf/zabbix.conf.php
sed -i "s|.*DATABASE.*=> .*|'DATABASE' => '$ZBX_SERVER_DB_NAME',|"         source/ui/conf/zabbix.conf.php
sed -i "s|.*USER.*= .*|\$DB['USER'] = '$ZBX_SERVER_DB_USER';|"             source/ui/conf/zabbix.conf.php
sed -i "s|.*USER.*=> .*|'USER' => '$ZBX_SERVER_DB_USER',|"                 source/ui/conf/zabbix.conf.php
sed -i "s|.*PASSWORD.*= .*|\$DB['PASSWORD'] = '$ZBX_SERVER_DB_PASSWORD';|" source/ui/conf/zabbix.conf.php
sed -i "s|.*PASSWORD.*=> .*|'PASSWORD' => '$ZBX_SERVER_DB_PASSWORD',|"     source/ui/conf/zabbix.conf.php
sed -i "s|.*URL.*=> .*|'URL' => '$ZBX_FRONTEND_URL',|"                     source/ui/conf/zabbix.conf.php
