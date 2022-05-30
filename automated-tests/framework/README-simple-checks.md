
Preparation
===========

Checkout `qa` branch:

    git clone -b qa --single-branch ssh://git@git.zabbix.lan:7999/icann/icann.git icann-qa
    cd icann-qa

Go to test framework directory:

    cd 

Prepare create file `tests.conf` and edit it:

    cp automated-tests/framework/tests.conf.example automated-tests/framework/tests.conf
    vi automated-tests/framework/tests.conf

Build Zabbix:

    automated-tests/framework/run-tests.pl --build-server

Running tests
=============

Being in the test framework directory, execute all the Simple Check tests:

    automated-tests/framework/run-tests.pl --skip-build --test-case-dir automated-tests/test-cases/simple-check

View the results, the second line of the results file must indicate the 38 tests executed, 0 errors and 0 failures:

    less test-results.xml
    [...]
    <testsuite timestamp="2022-04-26T11:15:08" tests="38" failures="0" errors="0" skipped="0" time="121.402418851852">

Test case files
===============

You can list the test case files by running the following command:

    ls automated-tests/test-cases/simple-check/*.txt

For each of the testing service (dns-udp, dns-tcp, rdds, rdap) we have the following patterns:

*   `0xx-dns-udp-<error-code>.txt`
*   `1xx-dns-tcp-<error-code>.txt`
*   `2xx-rdds*-<error-code>*.txt`
*   `3xx-rdap-<error-code>.txt`

Each test case has:

|file description  |file extention|
|:-----------------|-------------:|
|test case workflow|        `.txt`|
|tool configuration|       `.json`|
|expected output   |     `.tar.gz`|

*   test case file (`.txt`)
*   test case input file (optional, can be more than 1, `.json`)
*   test case output file (expected output, `.tar.gz`)

Let's consider the specific test case 004:

_DNS UDP - Querying for a non existent domain - Domain name being queried not present in question section (-251)_

|file                     |description                                                                          |
|:------------------------|:------------------------------------------------------------------------------------|
|004-dns-udp-251.txt      |describes how to execute the test                                                    |
|004-resolver-input.json  |configuration for the resolver tool                                                  |
|004-nameserver-input.json|configuration for the nameserver tool                                                |
|004-output.tar.gz        |an archive containing a single file `status.json` - the output of `t_rsm_dns` utility|

Let's see the contents of the input files:

    $ cat 004-resolver-input.json 
    {
        "expected-qname"  : "example",
        "expected-qtypes" : ["DNSKEY"],
        "rcode"           : "NOERROR",
        "flags"           : {
                "ad" : 1
        }
    }

As you can see the resolver expects query name "example", query type "DNSKEY" and replies with "NOERROR" rcode. In addition it sets "ad" flag in order to satisfy the first part of the test.

    $ cat 004-nameserver-input.json 
    {
        "owner"          : "example",
        "override-owner" : "foo",
        "rcode"          : "NOERROR",
        "flags"          : {
                "aa" : 1
        }
    }

After success with the resolver we query nameserver with "example". We tell it to reply with rcode "NOERROR", with "aa" flag set and with the original owner in the question section overridden with "foo".

This is unexpected to Zabbix and the corresponding error code is set. Then, we make sure that Zabbix has generated exactly what we have in the `output.tar.gz` archive:

    $ cat status.json | jq -SC .
    {
        "dnssecstatus": 1,
        "mode": 0,
        "nsips": [
            {
                "ip": "127.0.0.1",
                "ns": "ns1.example",
                "nsid": "666f6f2d6e732d6964",
                "protocol": "udp",
                "rtt": -251
            }
        ],
        "nss": [
            {
                "ns": "ns1.example",
                "status": 0
            }
        ],
        "nssok": 0,
        "protocol": 0,
        "status": 0,
        "testedname": "www.zz--rsm-monitoring.example."
    }

The outputs match, thus, the test case is considered successful.

How does it work
================

Checkout `qa` branch. Let's review the available tools.

    cd automated-tests/tools

You will see the following components:

|Name         |IP it listens on|Port it listens to|Services that use it|
|-------------|----------------|------------------|--------------------|
|resolver     |127.0.0.1       |5053              |all                 |
|nameserver   |127.0.0.1       |5054              |dns                 |
|rdds43-server|127.0.0.1       |4343              |rdds                |
|rdds80-server|127.0.0.1       |4380              |rdds                |
|rdap-server  |127.0.0.1       |4380              |rdap                |

The functionality of Perl Nameserver module had to be extended, which resulted in own implementation located in

    automated-tests/tools/lib/perl/Net/DNS/NameserverCustom.pm

The main differences are:

*   support for overriding reply (to detect _Header section incomplete_ error)
*   support for overriding owner (to detect _Domain name being queried not present in question section)_
*   support for _sleep(n)_ after receiving a request

The simple checks are tested by running utilities that simulate what Probe node is doing when performing a test. These utilities are located under

src/tests

and they start with the `t_`  prefix. In order to see how they can be used:

    $ ./t_rsm_dns -h

You will notice that there is a parameter `-j <file>` which is used by tests to compare the _expected_ output with the output from that utility.