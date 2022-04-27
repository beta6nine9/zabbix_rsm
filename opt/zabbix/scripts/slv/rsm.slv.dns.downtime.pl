#!/usr/bin/env perl
#
# Minutes of DNS downtime during running month

use FindBin;
use lib "$FindBin::RealBin/..";

use strict;
use warnings;

use RSM;
use RSMSLV;
use TLD_constants qw(:api);

my $cfg_key_in = 'rsm.slv.dns.avail';
my $cfg_key_out = 'rsm.slv.dns.downtime';

parse_slv_opts();
fail_if_running();

set_slv_config(get_rsm_config());

db_connect();

slv_exit(SUCCESS) if (get_monitoring_target() ne MONITORING_TARGET_REGISTRY);

if (!opt('dry-run'))
{
	# TODO: this is one time operation, remove on the next project iteration
	if (-f "/opt/zabbix/data/rsm.slv.dns.downtime.auditlog.txt")
	{
		unlink("/opt/zabbix/data/rsm.slv.dns.downtime.auditlog.txt") or
			die("cannot remove file \"/opt/zabbix/data/rsm.slv.dns.downtime.auditlog.txt\": $!");
	}

	recalculate_downtime(
		"/opt/zabbix/data/rsm.slv.dns.downtime.false-positive.txt",
		"rsm.slv.dns.avail",
		"rsm.slv.dns.downtime",
		get_macro_incident_dns_fail(),
		get_macro_incident_dns_recover(),
		get_dns_delay()
	);
}

# we don't know the cycle bounds yet so we assume it ends at least few minutes back
my $delay = get_dns_delay();

my $max_clock = cycle_start(getopt('now') // time(), $delay);

my $tlds_ref;
if (opt('tld'))
{
        fail("TLD ", getopt('tld'), " does not exist.") if (tld_exists(getopt('tld')) == 0);

        $tlds_ref = [ getopt('tld') ];
}
else
{
	$tlds_ref = get_tlds('DNS', $max_clock);
}

slv_exit(SUCCESS) if (scalar(@{$tlds_ref}) == 0);

my $cycles_ref = collect_slv_cycles(
	$tlds_ref,
	$delay,
	$cfg_key_out,
	ITEM_VALUE_TYPE_UINT64,
	$max_clock,
	(opt('cycles') ? getopt('cycles') : slv_max_cycles('dns'))
);

slv_exit(SUCCESS) if (scalar(keys(%{$cycles_ref})) == 0);

process_slv_downtime_cycles($cycles_ref, $delay, $cfg_key_in, $cfg_key_out);

slv_exit(SUCCESS);
