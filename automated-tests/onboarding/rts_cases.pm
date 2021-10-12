#!/usr/bin/perl -w

package rts_cases;

use strict;
use warnings;
use base 'Exporter';

use lib '.';
use rts_util;

my ($dbname, $dbuser, $dbpassword);

#
# This is a test set definition. It consists of Name, Init conmmand and
# the list of cases to test. Each case also has Name (R_NAME), command
# to test (R_RUN) and the list of checks to validate results.
#

sub rts_get_test 
{
	return {
		R_NAME => "RSM onboarding tests",

		R_INIT => <<EOF,
mysql -u$dbuser -p$dbpassword <<END && ../init_macros.sh
drop database if exists $dbname;
create database $dbname character set utf8 collate utf8_bin;
use $dbname;
\\. schema.sql
\\. images.sql
\\. data.sql
END
EOF
		R_CASES => [
			{
				R_NAME => "case 1 - probes.pl",
				R_RUN => <<EOF,
/opt/zabbix/scripts/probes.pl --probe=Elgin --server-id=1 --add \\
 --ipv4 --rdds --rdap --ip 172.19.16.2 --resolver 172.19.18.2
EOF
				R_CHECKS => [
					{
						R_TYPE => R_TYPE_DBSELECT,
						R_DESCR => "if proxy added",
						R_RUN => "select count(*) from hosts where host='Elgin' and status=6",
						R_EXPECT => "1"
					},
					{
						R_TYPE => R_TYPE_DBSELECT,
						R_DESCR => "if probe host added",
						R_RUN => "select count(*) from hosts where host='Elgin' and status=0",
						R_EXPECT => "1"
					},
					{
						R_TYPE => R_TYPE_DBSELECT,
						R_DESCR => "if probe config template added",
						R_RUN => "select count(*) from hosts where host='Template Probe Config Elgin' and status=3",
						R_EXPECT => "1"
					},
				]
			},
			{
				R_NAME => "case 2 - tld.pl",
				R_RUN => <<EOF,
/opt/zabbix/scripts/tld.pl --server-id=1 --tld longrow --dns-test-prefix=nonexistent \\
 --type=gTLD --ipv4 --ns-servers-v4='ns1.longrow,172.19.0.3 ns2.longrow,172.19.15.2' \\
 --dnssec --rdap-base-url="http://whois.longrow" --dns-minns=2 \\
 --rdds43-servers=whois.longrow --rdds80-servers=whois.longrow --rdds-test-prefix=whois \\
 --rdap-test-domain=whois.longrow
EOF
				R_CHECKS => [
					{
						R_TYPE => R_TYPE_DBSELECT,
						R_DESCR => "if tld template added",
						R_RUN => "select count(*) from hosts where name='Template Rsmhost Config longrow' and status=3",
						R_EXPECT => "1"
					},
					{
						R_TYPE => R_TYPE_DBSELECT,
						R_DESCR => "if tld host and tld probe host added",
						R_RUN => "select count(*) from hosts where (name='longrow' or name='longrow Elgin') and status=0",
						R_EXPECT => "2"
					},
					{
						R_TYPE => R_TYPE_DBSELECT,
						R_DESCR => "if tld has DNS template linked",
						R_RUN => <<EOF,
select count(*) from hosts_templates ht, hosts h, hosts t 
 where ht.hostid=h.hostid and t.hostid=ht.templateid 
 and h.status=0 and h.name='longrow' 
 and t.name='Template DNS Status' and t.status=3
EOF
						R_EXPECT => "1"
					},
				]
			},

		]
	};
}

#
# This functions is executed before any test cases. It's needed to set up
# a proper testing context, such as database access details. The database
# details are not required for R_TYPE_DBSELECT checks but may be required
# to connect to the database from initialization scripts etc.
#
# This function is optional. Feel free to delete it if it's not needed.
#

sub rts_setup
{
	my $context = shift;

	$dbname = $context->{'dbname'};
	$dbuser = $context->{'dbuser'};
	$dbpassword = $context->{'dbpassword'};

	# return 1 for success and 0 for failure
	return 1;
}


our @EXPORT = qw(rts_get_test rts_setup);

1;
