insert into globalmacro (globalmacroid, macro, value) values (1, '{$RSM.MONITORING.TARGET}', 'registry');

insert into hosts (hostid, host, status) values (1, 'Template Probe Status', 3);
insert into hosts (hostid, host, status) values (2, 'Template Probe Config Probe1-Server1', 3);
insert into hosts (hostid, host, status) values (3, 'Probe1-Server1', 6);
insert into hosts (hostid, host, status) values (4, 'Template Rsmhost Config tld1', 3);
insert into hosts (hostid, host, status) values (5, 'Template Rsmhost Config tld2', 3);
insert into hosts (hostid, host, status) values (6, 'Template Rsmhost Config tld3', 3);
insert into hosts (hostid, host, status) values (7, 'Template Rsmhost Config tld4', 3);
insert into hosts (hostid, host, status) values (8, 'Template Rsmhost Config tld5', 3);

insert into hosts_templates (hosttemplateid, hostid, templateid) values (1, 3, 1);
insert into hosts_templates (hosttemplateid, hostid, templateid) values (2, 3, 2);

insert into hostmacro (hostmacroid, hostid, macro, value) values (101, 2, '{$RSM.IP4.ENABLED}', 0);
insert into hostmacro (hostmacroid, hostid, macro, value) values (102, 2, '{$RSM.IP6.ENABLED}', 0);
insert into hostmacro (hostmacroid, hostid, macro, value) values (103, 2, '{$RSM.RDAP.ENABLED}', 0);
insert into hostmacro (hostmacroid, hostid, macro, value) values (104, 2, '{$RSM.RDDS.ENABLED}', 0);

insert into hostmacro (hostmacroid, hostid, macro, value) values (201, 4, '{$RSM.TLD}'                , 'tld1');
insert into hostmacro (hostmacroid, hostid, macro, value) values (202, 4, '{$RSM.TLD.DNS.TCP.ENABLED}', 0);
insert into hostmacro (hostmacroid, hostid, macro, value) values (203, 4, '{$RSM.TLD.DNS.UDP.ENABLED}', 0);
insert into hostmacro (hostmacroid, hostid, macro, value) values (204, 4, '{$RSM.DNS.NAME.SERVERS}'   , '');
insert into hostmacro (hostmacroid, hostid, macro, value) values (205, 4, '{$RSM.TLD.RDDS43.ENABLED}' , 0);
insert into hostmacro (hostmacroid, hostid, macro, value) values (206, 4, '{$RSM.TLD.RDDS43.SERVER}'  , '');
insert into hostmacro (hostmacroid, hostid, macro, value) values (207, 4, '{$RSM.TLD.RDDS80.ENABLED}' , 0);
insert into hostmacro (hostmacroid, hostid, macro, value) values (208, 4, '{$RSM.TLD.RDDS80.URL}'     , '');
insert into hostmacro (hostmacroid, hostid, macro, value) values (209, 4, '{$RDAP.TLD.ENABLED}'       , 0);
insert into hostmacro (hostmacroid, hostid, macro, value) values (210, 4, '{$RDAP.BASE.URL}'          , '');

insert into hostmacro (hostmacroid, hostid, macro, value) values (301, 5, '{$RSM.TLD}'                , 'tld2');
insert into hostmacro (hostmacroid, hostid, macro, value) values (302, 5, '{$RSM.TLD.DNS.TCP.ENABLED}', 0);
insert into hostmacro (hostmacroid, hostid, macro, value) values (303, 5, '{$RSM.TLD.DNS.UDP.ENABLED}', 0);
insert into hostmacro (hostmacroid, hostid, macro, value) values (304, 5, '{$RSM.DNS.NAME.SERVERS}'   , '');
insert into hostmacro (hostmacroid, hostid, macro, value) values (305, 5, '{$RSM.TLD.RDDS43.ENABLED}' , 0);
insert into hostmacro (hostmacroid, hostid, macro, value) values (306, 5, '{$RSM.TLD.RDDS43.SERVER}'  , '');
insert into hostmacro (hostmacroid, hostid, macro, value) values (307, 5, '{$RSM.TLD.RDDS80.ENABLED}' , 0);
insert into hostmacro (hostmacroid, hostid, macro, value) values (308, 5, '{$RSM.TLD.RDDS80.URL}'     , '');
insert into hostmacro (hostmacroid, hostid, macro, value) values (309, 5, '{$RDAP.TLD.ENABLED}'       , 0);
insert into hostmacro (hostmacroid, hostid, macro, value) values (310, 5, '{$RDAP.BASE.URL}'          , '');

insert into hostmacro (hostmacroid, hostid, macro, value) values (401, 6, '{$RSM.TLD}'                , 'tld3');
insert into hostmacro (hostmacroid, hostid, macro, value) values (402, 6, '{$RSM.TLD.DNS.TCP.ENABLED}', 0);
insert into hostmacro (hostmacroid, hostid, macro, value) values (403, 6, '{$RSM.TLD.DNS.UDP.ENABLED}', 0);
insert into hostmacro (hostmacroid, hostid, macro, value) values (404, 6, '{$RSM.DNS.NAME.SERVERS}'   , '');
insert into hostmacro (hostmacroid, hostid, macro, value) values (405, 6, '{$RSM.TLD.RDDS43.ENABLED}' , 0);
insert into hostmacro (hostmacroid, hostid, macro, value) values (406, 6, '{$RSM.TLD.RDDS43.SERVER}'  , '');
insert into hostmacro (hostmacroid, hostid, macro, value) values (407, 6, '{$RSM.TLD.RDDS80.ENABLED}' , 0);
insert into hostmacro (hostmacroid, hostid, macro, value) values (408, 6, '{$RSM.TLD.RDDS80.URL}'     , '');
insert into hostmacro (hostmacroid, hostid, macro, value) values (409, 6, '{$RDAP.TLD.ENABLED}'       , 0);
insert into hostmacro (hostmacroid, hostid, macro, value) values (410, 6, '{$RDAP.BASE.URL}'          , '');

insert into hostmacro (hostmacroid, hostid, macro, value) values (501, 7, '{$RSM.TLD}'                , 'tld4');
insert into hostmacro (hostmacroid, hostid, macro, value) values (502, 7, '{$RSM.TLD.DNS.TCP.ENABLED}', 0);
insert into hostmacro (hostmacroid, hostid, macro, value) values (503, 7, '{$RSM.TLD.DNS.UDP.ENABLED}', 0);
insert into hostmacro (hostmacroid, hostid, macro, value) values (504, 7, '{$RSM.DNS.NAME.SERVERS}'   , '');
insert into hostmacro (hostmacroid, hostid, macro, value) values (505, 7, '{$RSM.TLD.RDDS43.ENABLED}' , 0);
insert into hostmacro (hostmacroid, hostid, macro, value) values (506, 7, '{$RSM.TLD.RDDS43.SERVER}'  , '');
insert into hostmacro (hostmacroid, hostid, macro, value) values (507, 7, '{$RSM.TLD.RDDS80.ENABLED}' , 0);
insert into hostmacro (hostmacroid, hostid, macro, value) values (508, 7, '{$RSM.TLD.RDDS80.URL}'     , '');
insert into hostmacro (hostmacroid, hostid, macro, value) values (509, 7, '{$RDAP.TLD.ENABLED}'       , 0);
insert into hostmacro (hostmacroid, hostid, macro, value) values (510, 7, '{$RDAP.BASE.URL}'          , '');

insert into hostmacro (hostmacroid, hostid, macro, value) values (601, 8, '{$RSM.TLD}'                , 'tld5');
insert into hostmacro (hostmacroid, hostid, macro, value) values (602, 8, '{$RSM.TLD.DNS.TCP.ENABLED}', 0);
insert into hostmacro (hostmacroid, hostid, macro, value) values (603, 8, '{$RSM.TLD.DNS.UDP.ENABLED}', 0);
insert into hostmacro (hostmacroid, hostid, macro, value) values (604, 8, '{$RSM.DNS.NAME.SERVERS}'   , '');
insert into hostmacro (hostmacroid, hostid, macro, value) values (605, 8, '{$RSM.TLD.RDDS43.ENABLED}' , 0);
insert into hostmacro (hostmacroid, hostid, macro, value) values (606, 8, '{$RSM.TLD.RDDS43.SERVER}'  , '');
insert into hostmacro (hostmacroid, hostid, macro, value) values (607, 8, '{$RSM.TLD.RDDS80.ENABLED}' , 0);
insert into hostmacro (hostmacroid, hostid, macro, value) values (608, 8, '{$RSM.TLD.RDDS80.URL}'     , '');
insert into hostmacro (hostmacroid, hostid, macro, value) values (609, 8, '{$RDAP.TLD.ENABLED}'       , 0);
insert into hostmacro (hostmacroid, hostid, macro, value) values (610, 8, '{$RDAP.BASE.URL}'          , '');
