#!/usr/bin/env bash

set -o errexit
set -o nounset
set -o pipefail
#set -o xtrace

DIR="$(dirname "$(readlink -f "$0")")"

grep -v '\[' $DIR/tests.conf > /tmp/ddd
. /tmp/ddd
rm -f /tmp/ddd

rm -rf $build_dir/* $logs_dir/* $db_dumps_dir/*
$DIR/run-tests.pl --build-server --build-proxy

cp -f $source_dir/ui/conf/zabbix.conf.php.example $source_dir/ui/conf/zabbix.conf.php

sed -i "s|.*DATABASE.*= .*|\$DB['DATABASE'] = '$db_name';|"     $source_dir/ui/conf/zabbix.conf.php
sed -i "s|.*DATABASE.*=> .*|'DATABASE' => '$db_name',|"         $source_dir/ui/conf/zabbix.conf.php
sed -i "s|.*USER.*= .*|\$DB['USER'] = '$db_username';|"         $source_dir/ui/conf/zabbix.conf.php
sed -i "s|.*USER.*=> .*|'USER' => '$db_username',|"             $source_dir/ui/conf/zabbix.conf.php
sed -i "s|.*PASSWORD.*= .*|\$DB['PASSWORD'] = '$db_password';|" $source_dir/ui/conf/zabbix.conf.php
sed -i "s|.*PASSWORD.*=> .*|'PASSWORD' => '$db_password',|"     $source_dir/ui/conf/zabbix.conf.php
sed -i "s|.*URL.*=> .*|'URL' => '$url',|"                       $source_dir/ui/conf/zabbix.conf.php
