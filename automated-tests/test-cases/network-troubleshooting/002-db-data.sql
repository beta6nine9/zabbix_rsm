update hostmacro set value = 1 where hostid = 2 and macro = '{$RSM.IP4.ENABLED}';
update hostmacro set value = 1 where hostid = 2 and macro = '{$RSM.IP6.ENABLED}';
update hostmacro set value = 1 where hostid = 2 and macro = '{$RSM.RDAP.ENABLED}';
update hostmacro set value = 1 where hostid = 2 and macro = '{$RSM.RDDS.ENABLED}';

update hostmacro set value = 'ns1.tld1,127.0.1.1'                     where hostid = 4 and macro = '{$RSM.DNS.NAME.SERVERS}';

update hostmacro set value = 'ns1.tld2,127.0.2.1'                     where hostid = 5 and macro = '{$RSM.DNS.NAME.SERVERS}';
update hostmacro set value = 'rdds.tld2'                              where hostid = 5 and macro = '{$RSM.TLD.RDDS43.SERVER}';

update hostmacro set value = 'ns1.tld3,127.0.3.1'                     where hostid = 6 and macro = '{$RSM.DNS.NAME.SERVERS}';
update hostmacro set value = 'http://rdds.tld2/'                      where hostid = 6 and macro = '{$RSM.TLD.RDDS80.URL}';

update hostmacro set value = 'ns1.tld1,127.0.1.1 ns1.tld4,127.0.4.1'  where hostid = 7 and macro = '{$RSM.DNS.NAME.SERVERS}';
update hostmacro set value = 'http://rdap.tld4/'                      where hostid = 7 and macro = '{$RDAP.BASE.URL}';

update hostmacro set value = 'ns1.tld1,127.0.1.1'                     where hostid = 8 and macro = '{$RSM.DNS.NAME.SERVERS}';
update hostmacro set value = 'rdds.tld5'                              where hostid = 8 and macro = '{$RSM.TLD.RDDS43.SERVER}';
update hostmacro set value = 'http://rdds.tld5/'                      where hostid = 8 and macro = '{$RSM.TLD.RDDS80.URL}';
update hostmacro set value = 'http://rdap.tld5/'                      where hostid = 8 and macro = '{$RDAP.BASE.URL}';
