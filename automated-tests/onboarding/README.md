## Onboarding Test Automation

Scripts in this directory is a Proof-of-Concept for test automation for onboarding.

- `run_tests.pl` - Perl script to run tests
- `rts_util.pm` - Perl module, shared between `run_tests.pl` and test case modules
- `rts_cases.pm` - Example Perl module describing a set of test cases

# How It Works

`rts_cases.pm` is a module that specifies a set of tests ("test set"). Each test set contains:

- **Setup function**. This function can be used to prepare the test set.
- **Test set name**
- **Initialization command**. The command can be any valid shell string (so multiple commands chained by `;`, `&&` etc allowed). If the command returns `0` (success) the initialization considered successful, any non-zero value is a failure and the tests won't start.
- **List of cases** with each case consisting of:
    - Case name
    - Case command. The command can be any valid shell string. The same rules as for Initialization command apply.
    - List of checks. Check is an action that is performed after the Command to validate the results. For instance, if you have the command that tests addition of a probe, then checks will run database queries to make sure that all hosts and templates are present and configured properly in the database.

**Initialization command** can be used to prepare test environment (e.g. set up a database, load data). The tests won't run if the Initialization command fails. All cases are processed sequentially in the same order as they appear in the list. If case command fails then the checks for this case are not run and the test is considered failed. The checks for every case are also processed sequentially, however if one of the checks fails the other checks are not executed and the case is considered failed. The case is successfull only when all checks succeed.

# Invocation

Run tests by invoking `run_tests.pl` with a test set:
```
perl ./run_tests.pl -u zabbix -p password -d database rts_cases.pm
```
`-u`, `-p`, `-d` are the database credentials and `rts_cases.pm` is a test set.
