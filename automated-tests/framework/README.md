# Test Framework

## How it works

Test framework is written in Perl. The testing process is failry linear:
- prepare workspace:
    - remove `test-results.xml`, if it exists;
    - prepare empty `build/` directory for Zabbix installation;
    - prepare empty `logs/` directory for Zabbix server and proxy logs;
    - create SQL files for creating DB;
    - build Zabbix server;
    - create configuration file for Zabbix server (`zabbix_server.conf`);
    - create configuration file for scripts (`rsm.conf`);
- collect test case files:
    - loop through all files specified by `--test-case-file` option(s);
    - loop through all directories specified by `--test-case-dir` option(s), recursively collect test case files;
    - sort files by filename to have consistent order between the runs;
- execute test cases;
- generate `test-results.xml`.

Before executing each test case, test framework connects to the database. After executing the test case, framework disconnects from the database. This helps to ensure that there aren't any invalid states in DB connection across the test cases.

To ensure that failures in test case steps don't stop the framework from executing other test cases and generating the report, test framework process is forked before each step and step is executed in the child process. If the step fails for some reason, test case is marked as "failed" and rest of the steps are not executed.

When all test cases are executed, test framework generates JUnit test result report in XML format that can be used by Jenkins to collect statistics about passed, failed and skipped test cases.

Test framework writes output to STDOUT and STDERR. Output of each test case is separated by a sequence of dash characters and has "test case succeeded", "test case failed" or "test case skipped" pharse at the end of the output. When some test case fails, this output can be analyzed to get some clues about the failure.

## Test cases

Test cases are described in text files with `.txt` extension (e.g., `001-test-case.txt`). To temporarily disabe specific test case, prepend `.` to the beginning of the filename (e.g., `.001-test-case.txt`). Disabled test cases will be read by the framework, but reported as "skipped". Files with other extensions are ignored and can be used as additional files for test cases (e.g., `001-test-case-input-files.tar.gz`).

Test framework sorts test cases in alphabetical order by filename before executing them. Test case files can be grouped by putting them into subdirectories.

Each line in the test case file describes either a command, or arguments. Commands are enclosed in square brackets. Arguments are written in CSV format. Command can have multiple lines with arguments, in that case command will be executed multiple times. Arguments that are optional also must be specified. If value of an optional argument is an empty string, this argument is ignored. Some commands don't require any arguments. Comments start with `#`. Comments and empty lines are ignored.

Each test case file must start with a `test-case` command.

Example test case:
```
[test-case]

"Example test case 1"

[execute]

# execute "date" command
"2020-12-01 09:00:00","date","+'%F %T %Z'"
"2020-12-02 09:00:00","date","+'%F %T %Z'"
"2020-12-03 09:00:00","date","+'%F %T %Z'"
```

## Variables

When parsing the test case file, framework looks for any variables in command arguments and tries to expand them. The syntax of the variables is `${variable}`.

There are two types of variables:
* named variables - these variables can be set by using `set-variable` command and expand to a constant string;
* special variables - these variables are available without using `set-variable` command and expand to a value that depends on the current environment (e.g., current time).

Supported special variables are:
* `${cfg:<section>:<property>}` - returns value from framework's configuration file;
* `${file:<filename>}` - returns contents of the file, `filename` must be relative to the test case file;
* `${ts:<datetime>}` - returns unix timestamp for the given datetime, see https://metacpan.org/pod/Date::Parse for supported formats.

If named variable does not exist, it won't be expanded. If special variable cannot be expanded, the result is undefined (e.g., it can expand to unexpected values or fail the test case).

Example test case:
```
[test-case]

"Example test case 1"

[set-variable]

"str1","first string"
"str2","second string"

[execute]

# execute "date" command
"","echo 'this is ${str1}'"
"","echo 'this is ${str2}'"
"","echo 'this is ${str3}'"
"","echo '${ts:2020-01-01 12:00:00}'"
"","echo 'build_dir is ${cfg:paths:build_dir}'"
```

## Commands

### test-case

*name*

Sets the name of the test case.

### set-variable

*name,value*

Sets named variable that can later be used in command arguments.

### enable-debug-mode

*(ignored)*

Enables debug mode.

### disable-debug-mode

*(ignored)*

Disables debug mode.

### empty-directory

*directory*

Prepares empty directory. If directory does not exist, creates it. If directory exists, deletes everything in it.

### extract-files

*directory,archive*

Extracts archive file into a directory. Archive should be filename of a compressed tar file, relative to the test case file (e.g., `001-test-case-input-files.tar.gz`).

### compare-files

*directory,archive*

Compares contents of an archive file with contents on the filesystem. Archive should be filename of a compressed tar file, relative to the test case file (e.g., `001-test-case-input-files.tar.gz`).

### prepare-server-database

*(ignored)*

Drops the database, then creates new database with initial data in it.

### execute-sql-query

*query,param,param,param,...*

Executes query (update, insert, delete). Use `?` in the query as placeholders for params.

### compare-sql-query

*query,value,value,value,...*

Selects data from the database and compares it with expected values.

### fill-history

*host,item,delay,clock,value,value,value,...*

Fills history table with values for specific item.

Argument `clock` specifies clock of the first value.

Argument `delay` specifies delay that should be added to the clock for each subsequent value. To make a gap, leave a value empty.

Example:
```
"example-host","example-item",3,1000,2,4,,8
```

This would result into something like this:
```
insert into history set itemid = 123, clock = 1000 + (3 * 0), value = 2
insert into history set itemid = 123, clock = 1000 + (3 * 1), value = 4
insert into history set itemid = 123, clock = 1000 + (3 * 3), value = 8
```

### compare-history

*host,item,delay,clock,value,value,value,...*

Checks if history table contains specified values. See the meaning of arguments in the description of the `fill-history` command.

### set-lastvalue

*host,item,clock,value*

Sets custom last value for an item.

### fix-lastvalue-tables

*(ignored)*

Fixes database tables that hold last values of items.

### set-global-macro

*macro,value*

Sets global macro.

### set-host-macro

*host,macro,value*

Sets host or template macro.

### execute

*datetime,command*

*datetime,command,argument,argument,argument,...*

Executes external command.

Argument `datetime` must be either empty string, or date and time in `yyyy-mm-dd hh:mm:ss` format to fake the system time for the command.

Argument `command` can be either one value (command with or without arguments), or multiple values (one for command, one for each argument).

Examples:
```
"",date
"2020-12-03 09:00:00","date +'%F %T %Z'"
"2020-12-01 09:00:00","date","+'%F %T %Z'"
```

### execute-ex

*datetime,status,expected_stdout,expected_stderr,command[,arg,arg,arg,...]*

Executes external command and validates exit status, STDOUT and STDERR.

Arguments `expected_stdout` and `expected_stderr` are optional. If they contain a string that is enclosed in `//`, this string is used as a regex pattern, otherwise the whole output has to be the same as the string (in this case, trailing newlines are ignored).

### start-server

*datetime*

*datetime,key=value,key=value,...*

Starts Zabbix server.

Arguments `key=value` are optional key-value pairs to be updated in `zabbix_server.conf` configuration file before starting the server, mostly to be used while writing/debugging test case.

### stop-server

*(ignored)*

Stops Zabbix server.

### update-rsm-conf

*section,property,value*

Updates configuration value in `rsm.conf`.

### ~~create-probe~~ (obsolete)

~~*probe,ip,port,ipv4,ipv6,rdds,rdap*~~

~~Onboards a probe.~~

### ~~create-tld~~ (obsolete)

~~*tld,dns_test_prefix,type,dnssec,dns_udp,dns_tcp,ns_servers_v4,ns_servers_v6,rdds43_servers,rdds80_servers,rdap_base_url,rdap_test_domain,rdds_test_prefix*~~

~~Onboards a TLD.~~

### ~~disable-tld~~ (obsolete)

~~*tld*~~

~~Disables a TLD.~~

### create-incident

*rsmhost,description,from,till,false_positive*

Creates an incident.

Argument `description` must match trigger's description.

Arguments `from` specifies the time when an incident started and must be either empty string, or date and time in `yyyy-mm-dd hh:mm:ss` format.

Arguments `till` specifies the time when an incident ended and must be either empty string, or date and time in `yyyy-mm-dd hh:mm:ss` format.

Argument `false_positive` must be either `0` (false positive incident) or `1` (regular incident).

Examples:
```
# false positive incident
"tld1","RDDS service is down","2021-03-01 09:00:00","2021-03-01 13:00:00",1
# incident that hasn't ended yet
"tld2","RDDS service is down","2021-03-01 09:00:00","",0
```

### check-incident

*rsmhost,description,from,till*

Validates incident / problem. This command should be used in conjunction with `check-event-count`.

Argument `description` must match trigger's description rather than actual event's description.

Arguments `from` specifies the time when a problem should have started and must be either empty string, or date and time in `yyyy-mm-dd hh:mm:ss` format.

Arguments `till` specifies the time when a problem should have ended and must be either empty string, or date and time in `yyyy-mm-dd hh:mm:ss` format.

Examples:
```
"tld1","RDDS rolling week is over 10%" ,"2021-03-01 09:30:00","2021-03-01 16:20:00"
"tld1","RDDS rolling week is over 25%" ,"2021-03-01 10:30:00","2021-03-01 15:20:00"
"tld1","RDDS rolling week is over 50%" ,"2021-03-01 12:10:00","2021-03-01 13:40:00"

"tld1","RDDS rolling week is over 10%",2
"tld1","RDDS rolling week is over 25%",2
"tld1","RDDS rolling week is over 50%",2
"tld1","RDDS rolling week is over 75%",0
"tld1","RDDS rolling week is over 100%",0
```

### check-event-count

*rsmhost,description,count*

Checks the number of events. This command should be used in conjunction with `check-incident`.

Argument `description` must match trigger's description rather than actual event's description.

See the example in the description of the `check-incident` command.

### provisioning-api

*endpoint,method,expected_code,user,request,response*

Sends request to Porvisioning API. For this to work, framework must be properly configured.

Argument `endpoint` points to an endpoint of Provisioning API (e.g., `"/tlds/tld1"`).

Argument `method` describes HTTP request method, usually `GET`, `PUT` or `DELETE`.

Argument `expected_code` describes expected HTTP response status code, e.g., `200` (for "OK") or `404` (for "Not Found").

Argument `user` specifies user for Basic Authentication. Exact usernames and passwords are configured in framework's configuration file. Supported users are:
* `readonly` - user with "read only" permissions;
* `readwrite` - user with "read and write" permissions;
* `invalid_password` - user that is registered, but with invalid password;
* `nonexistent` - user that is not registered in Provisioning API;
* `''` (empty string) - for skipping authentication).

Argument `request` specifies filename of the payload for the request. This argument is optional. It is usually used only with `PUT` requests.

Argument `response` specifies filename of the expected response's payload. This argument is optional. If this argument is not specified, validation of the response's payload is skipped.

Tip: when massive changes are required in expected responses, the handler of `provisioning-api` command can be modified to write the response files before doing the validation.

### start-tool

*tool_name,pid-file,input-file*

Starts a tool that is shipped with the test framework.

### stop-tool

*tool_name,pid-file*

Stops a tool that is shipped with the test framework.

### check-proxy

*proxy,status,ip,port,psk-identity,psk*

Checks if proxy exists and has correct properties.

Argument `status` must be either `enabled` or `disabled`. Value of this argument affects validation of IP, port and encryption.

### check-host

*host,status,info_1,info_2,proxy,template_count,host_group_count,macro_count,item_count*

Checks if host or template exists and has correct properties.

Argument `status` must be `enabled`, `disabled` or `template`.

Arguments `info_1` and `info_2` are used for registrars (registrar name and registrar family).

Argument `proxy` specifies name of the proxy.

Arguments `template_count`, `host_group_count`, `macro_count` and `item_count` specify numbers of linked items of each type.

### check-host-count

*type,count*

Validates number of hosts, templates or proxies in the database.

Argument `type` must be `host`, `template` or `proxy`.

### check-host-template

*host,template*

Check if template is linked to the host.

### check-host-group

*host,group*

Check if host is linked to the host group.

### check-host-macro

*host,macro,value*

Checks if host has a macro with specified name and value.

### check-item

*host,key,name,status,item_type,value_type,delay,history,trends,units,params,master_item,preproc_count,trigger_count*

Checks if host has a specific item.

Argument `status` must be either `enabled` or `disabled`.

Argument `item_type` must be `trapper`, `simple`, `internal`, `external`, `calculated` or `dependent`.

Argument `value_type` must be `float`, `str`, `uint64` or `text`.

Argument `master_item` is optional and specifies the key of the master item.

Arguments `preproc_count` and `trigger_count` specify numbers of preprocessing steps and triggers.

### check-preproc

*host,key,step,type,params,error_handler,error_handler_params*

Checks if item has specified preprocessing step.

Argument `type` must be `delta-speed`, `jsonpath` or `throttle-timed-value`.

Argument `error_handler` must be either `default` or `discard-value`.

### check-trigger

*host,status,priority,trigger,dependency,expression,recovery_expression*

Checks if host has specified trigger.

Argument `status` must be either `enabled` or `disabled`.

Argument `priority` must be `not-classified`, `information`, `warning`, `average`, `high` or `disaster`.

Argument `trigger` specifies trigger's description.

Argument `dependency` specifies description of the linked trigger.

Arguments `expression` and `recovery_expression` sepcify trigger's expressions. Before validating the expression, all whitespaces in the expressions from the DB are "compressed" - converted into a single space. This also applies to the newlines (e.g., `"foo\n  bar"` is converted into `"foo bar"`).

## Running tests

Running tests consists of following steps:
- prepare frontend;
- prepare `tests.conf`;
- build Zabbix server by executing `run-tests.pl`;
- run tests by executing `run-tests.pl`.

Script `run-tests.pl` has two arguments for selecting test cases (each of them can be used multiple times):
- `--test-case-file` - specify single test case file;
- `--test-case-dir` - specify directory that contains test case files.

Script prints output to STDOUT and STDERR. When all tests are executed, it generates `test-results.xml` report that contains summary of all tests cases. Normally, this report is used by Jenkins.

Example (assuming that the sources are available in `/home/$USER/source` and tests are executed in `/home/$USER/tests`):
```
# make sure that the timezone is UTC
export TZ=UTC

# make some variables
SOURCE_DIR="/home/$USER/source"
TESTS_DIR="$SOURCE_DIR/automated-tests"
WORK_DIR="/home/$USER/tests"

# create or update /opt/zabbix
sudo ln -sfn "$SOURCE_DIR/opt/zabbix" /opt/zabbix

# update tests.conf
python3 -c '

import configparser
import sys

config_file = sys.argv[1] + "/automated-tests/framework/tests.conf"
source_dir  = sys.argv[1]
work_dir    = sys.argv[2]

config = configparser.ConfigParser()
config.read(config_file)

config["paths"]["source_dir"]         = source_dir;
config["paths"]["build_dir"]          = work_dir;
config["paths"]["logs_dir"]           = work_dir + "/logs";
config["paths"]["db_dumps_dir"]       = work_dir + "/db_logs";
config["zabbix_server"]["socket_dir"] = work_dir;
config["zabbix_server"]["pid_file"]   = work_dir + "/zabbix_server.pid";
#config["frontend"]["url"]             = ...;

with open(config_file, "w") as f:
    config.write(f, space_around_delimiters=False)

' "$SOURCE_DIR" "$WORK_DIR"

# go to directory where tests will be executed
cd "$WORK_DIR"

# remove everything
rm -rf "$WORK_DIR/*"

# build Zabbix server
"$TESTS_DIR/framework/run-tests.pl" --build-proxy --build-server

# run tests
"$TESTS_DIR/framework/run-tests.pl" --skip-build --test-case-dir "$TESTS_DIR/test-cases/poc/"

# show results
cat test-results.xml
```
