# Idea

The idea of how to test SLA API or Data Export scripts is rather simple:

- set up the **database**
- if needed, start zabbix server, run **SLV scripts** and stop zabbix server
- **extract** data needed by scripts (e. g. /opt/zabbix/sla/last_update.txt)
- **run** the scripts being tested
- **compare** the data produced by scripts with pre-created archive

# One test case

Single test case usually contains 4 files:

1. test-cases/sla-api/003-all-services-enabled-all-down.txt
2. test-cases/sla-api/003-sla-input.tar.gz
3. test-cases/sla-api/003-sla-output.tar.gz
4. test-cases/sla-api/003-cache-output.tar.gz

In the test case above, the first file contains the test case instructions, such as:

- test case name (which usually is part of file name)
- set up the database
- start/stop zabbix server (optional)
- run SLV scripts (optional)
- prepare /opt/zabbix directory
- run the scripts being tested
- check if they produced correct output

The second is an archive of /opt/zabbix/sla directory, that is extracted before SLA API scripts are run, this archive is mentioned in the first file.
The third file is an archive that must exactly match files in /opt/zabbix/sla produced by SLA API scripts, it is also mentioned in the first file.
And the last one is an archive that must exactly match files in /opt/zabbix/cache after SLA API scripts are run, it is mentioned in the first file as well.

The file name syntax *003-name.extension* is just a convention that is there for convenience. To easily see which file belongs to which test case when browsing test case files.

# SLA API tests

With the above described logic of the test case it seemed that some of the test cases mentioned in the requirement specification were worth grouping. See below which such case (mentioned in braces) is covered by which of the test case file:

- 001-all-services-disabled.txt
    - (1) TLD state all disabled
- 002-all-services-enabled-all-up.txt
    - (2) TLD state all enabled, all up v1/v2
- 003-all-services-enabled-all-down.txt
    - (3) TLD state all enabled all down active incidents v1/v2
- 004-all-services-enabled-all-up-inconclusive.txt
    - (4) TLD state all enabled all up-inconclusive v1/v2
- 005-incidents-within-rolling-week.txt
    - (5) TLD state all enabled, incidents within the rolling week only v1/v2
- 006-active-dns-incident.txt
    - (6) TLD monitoring dns alarmed yes v1/v2
    - (7) TLD monitoring dns downtime > 0 v1/v2
    - (8) TLD monitoring incidents dns state active v1/v2
    - (9) TLD monitoring incidents dns falsePositive not toggled v1/v2
    - (10) TLD Monitoring incident dns measurements down v1/v2
    - (11) TLD Monitoring dns measurements failure v1/v2
- 007-active-dnssec-incident.txt
    - (12) TLD monitoring dnssec alarmed yes v1/v2
    - (13) TLD monitoring incidents dnssec state active v1/v2
    - (14) TLD monitoring incidents dnssec falsePositive not toggled v1/v2
    - (15) TLD Monitoring incident dnssec measurements down v1/v2
    - (16) TLD Monitoring dnssec measurements failure v1/v2
- 008-active-rdds-incident.txt
    - (17) TLD monitoring rdds alarmed  yes v1/v2
    - (18) TLD monitoring rdds downtime > 0 v1/v2
    - (19) TLD monitoring incidents rdds state active v1/v2
    - (20) TLD monitoring incidents rdds falsePositive not toggled v1/v2
    - (21) TLD Monitoring incident rdds measurements down v1/v2
    - (22) TLD Monitoring rdds measurements failure v1/v2
- 009-active-rdap-incident.txt
    - (23) TLD monitoring rdap alarmed  yes v1/v2
    - (24) TLD monitoring rdap downtime > 0 v1/v2
    - (25) TLD monitoring incidents rdap state active v1/v2
    - (26) TLD monitoring incidents rdap falsePositive not toggled v1/v2
    - (27) TLD Monitoring incident rdap measurements down v1/v2
    - (28) TLD Monitoring rdap measurements failure v1/v2
- 010-no-dns-incident.txt
    - (29) TLD monitoring dns alarmed no v1/v2
    - (30) TLD Monitoring incident dns measurements all up v1/v2
    - (31) TLD Monitoring dnssec measurements up v1/v2
- 011-no-dnssec-incident.txt
    - (32) TLD monitoring dnssec alarmed no v1/v2
    - (33) TLD Monitoring incident dnssec measurements all up v1/v2
    - (34) TLD Monitoring dns measurements up v1/v2
- 012-no-rdds-incident.txt
    - (35) TLD monitoring rdds alarmed no v1/v2
    - (36) TLD Monitoring incident rdds measurements all up v1/v2
    - (37) TLD Monitoring rdds measurements up v1/v2
- 013-no-rdap-incident.txt
    - (38) TLD monitoring rdap alarmed no v1/v2
    - (39) TLD Monitoring incident rdap measurements all up v1/v2
    - (40) TLD Monitoring rdap measurements up v1/v2
- 014-resolved-dns-incident.txt
    - (41) TLD monitoring incidents dns state resolved v1/v2
- 015-resolved-dnssec-incident.txt
    - (42) TLD monitoring incidents dnssec state resolved v1/v2
- 016-resolved-rdds-incident.txt
    - (43) TLD monitoring incidents rdds state resolved v1/v2
- 017-resolved-rdap-incident.txt
    - (44) TLD monitoring incidents rdap state resolved v1/v2
- 018-resolved-false-positive-dns-incident.txt
    - (45) TLD monitoring incidents dns falsePositive toggled v1/v2
    - (46) TLD monitoring incidents dns state falsePositive True v1/v2
- 019-resolved-false-positive-dnssec-incident.txt
    - (47) TLD monitoring incidents dnssec falsePositive toggled v1/v2
    - (48) TLD monitoring incidents dnssec state falsePositive True v1/v2
- 020-resolved-false-positive-rdds-incident.txt
    - (49) TLD monitoring incidents rdds falsePositive toggled v1/v2
    - (50) TLD monitoring incidents rdds state falsePositive True v1/v2
- 021-resolved-false-positive-rdap-incident.txt
    - (51) TLD monitoring incidents rdap falsePositive toggled v1/v2
    - (52) TLD monitoring incidents rdap state falsePositive True v1/v2
- 022-dns-dnssec-up-inconclusive-no-data.txt
    - (53) TLD Monitoring incident dns measurements all up-inconclusive (no-probes|no-data) v1/v2
    - (54) TLD Monitoring dns measurements up-inconclusive (no-probes|no-data) v1/v2
    - (55) TLD Monitoring incident dnssec measurements all up-inconclusive (no-probes|no-data) v1/v2
    - (56) TLD Monitoring dnssec measurements up-inconclusive (no-probes|no-data) v1/v2
- 023-rdds-up-inconclusive-no-data.txt
    - (57) TLD Monitoring incident rdds measurements all up-inconclusive (no-probes|no-data) v1/v2
    - (58) TLD Monitoring rdds measurements up-inconclusive (no-probes|no-data) v1/v2
024-rdap-up-inconclusive-no-data.txt
    - (59) TLD Monitoring incident rdap measurements all up-inconclusive (no-probes|no-data) v1/v2
    - (60) TLD Monitoring rdap measurements up-inconclusive (no-probes|no-data) v1/v2# New Document

# Data Export tests

- 001-ids-generation.txt
    - generation of Catalog IDs
- 002-pre-filled-ids.txt
    - using pre-created Catalog IDs
