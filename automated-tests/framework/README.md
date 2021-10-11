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

Each line in the test case file describes either a command, or arguments. Commands are enclosed in square brackets. Arguments are written in CSV format. Command can have multiple lines with arguments, in that case command will be executed multiple times. Some commands don't require any arguments. Comments start with `#`. Comments and empty lines are ignored.

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

## Commands

### test-case

*name*

Sets the name of the test case.

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

### execute-sql-query

*query,param,param,param,...*

Executes query (update, insert, delete). Use `?` in the query as placeholders for params.

### compare-sql-query

*query,value,value,value,...*

Selects data from the database and compares it with expected values.

### fix-lastvalue-tables

*(ignored)*

Fixes database tables that hold last values of items.

### set-lastvalue

*host,item,clock,value*

Sets custom last value for an item.

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

### prepare-server-database

*(ignored)*

Drops the database, then creates new database with initial data in it.

### set-global-macro

*macro,value*

Sets global macro.

### set-host-macro

*host,macro,value*

Sets host or template macro.

### start-server

*datetime*

*datetime,key=value,key=value,...*

Starts Zabbix server.

Arguments `key=value` are optional key-value pairs to be updated in `zabbix_server.conf` configuration file before starting the server, mostly to be used while writing/debugging test case.

### stop-server

*(ignored)*

Stops Zabbix server.

### create-probe

*probe,ip,port,ipv4,ipv6,rdds,rdap*

Onboards a probe.

### create-tld

*tld,dns_test_prefix,type,dnssec,dns_udp,dns_tcp,ns_servers_v4,ns_servers_v6,rdds43_servers,rdds80_servers,rdap_base_url,rdap_test_domain,rdds_test_prefix*

Onboards a TLD.

### disable-tld

*tld*

Disables a TLD.

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
perl -e '
    use strict;
    use warnings;

    use Data::Dumper;
    use Config::Tiny;

    my $config_file = $ARGV[0] . "/automated-tests/framework/tests.conf";
    my $source_dir  = $ARGV[0];
    my $work_dir    = $ARGV[1];

    my $config = Config::Tiny->new;
    $config = Config::Tiny->read($config_file);

    $config->{"paths"}{"source_dir"}        = $source_dir;
    $config->{"paths"}{"build_dir"}         = $work_dir;
    $config->{"paths"}{"logs_dir"}          = $work_dir . "/logs";
    $config->{"paths"}{"db_dumps_dir"}      = $work_dir . "/db_logs";
    $config->{"paths"}{"server_socket_dir"} = $work_dir;
    $config->{"zabbix_server"}{"pid_file"}  = $work_dir . "zabbix_server.pid";
    #$config->{"frontend"}{"url"}            = ...;

    $config->write($config_file);
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
