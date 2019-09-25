#!/usr/bin/perl
#
# Minutes of RDAP downtime during running month

BEGIN
{
	our $MYDIR = $0; $MYDIR =~ s,(.*)/.*/.*,$1,; $MYDIR = '..' if ($MYDIR eq $0);
}
use lib $MYDIR;

use strict;
use warnings;
use RSM;
use RSMSLV;
use TLD_constants qw(:api);

my $cfg_key_in = 'rsm.slv.rdap.avail';
my $cfg_key_out = 'rsm.slv.rdap.downtime';

parse_slv_opts();
fail_if_running();

set_slv_config(get_rsm_config());

db_connect();

slv_exit(SUCCESS) if (!is_rdap_standalone(getopt('now')));

if (!opt('dry-run'))
{
	recalculate_downtime(
		"/opt/zabbix/data/rsm.slv.rdap.downtime.auditlog.txt",
		$cfg_key_in,
		$cfg_key_out,
		get_macro_incident_rdap_fail(),
		get_macro_incident_rdap_recover(),
		get_rdap_delay(getopt('now') // time() - AVAIL_SHIFT_BACK)
	);
}

# we don't know the cycle bounds yet so we assume it ends at least few minutes back
my $delay = get_rdap_delay(getopt('now') // time() - AVAIL_SHIFT_BACK);

my (undef, undef, $max_clock) = get_cycle_bounds($delay, getopt('now'));

my $tlds_ref;
if (opt('tld'))
{
	fail("TLD ", getopt('tld'), " does not exist.") if (tld_exists(getopt('tld')) == 0);

	$tlds_ref = [ getopt('tld') ];
}
else
{
	$tlds_ref = get_tlds('RDAP', $max_clock);
}

slv_exit(SUCCESS) if (scalar(@{$tlds_ref}) == 0);

my $cycles_ref = collect_slv_cycles(
	$tlds_ref,
	$delay,
	$cfg_key_out,
	ITEM_VALUE_TYPE_UINT64,
	$max_clock,
	(opt('cycles') ? getopt('cycles') : slv_max_cycles('rdap'))
);

slv_exit(SUCCESS) if (keys(%{$cycles_ref}) == 0);

process_slv_downtime_cycles($cycles_ref, $delay, $cfg_key_in, $cfg_key_out);

slv_exit(SUCCESS);
