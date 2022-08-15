#!/usr/bin/env perl
#
# RDAP rolling week

use FindBin;
use lib "$FindBin::RealBin/..";

use strict;
use warnings;

use RSM;
use RSMSLV;
use TLD_constants qw(:api);

use constant SLV_ITEM_KEY_RDAP_AVAIL	=> 'rsm.slv.rdap.avail';
use constant SLV_ITEM_KEY_RDAP_ROLLWEEK	=> 'rsm.slv.rdap.rollweek';

parse_slv_opts();
fail_if_running();

log_execution_time(1, 1);

set_slv_config(get_rsm_config());

db_connect();

slv_exit(SUCCESS) if (!is_rdap_standalone(getopt('now')));

# we don't know the rollweek bounds yet so we assume it ends at least few minutes back
my $delay = get_rdap_delay();

my $max_clock = cycle_start(getopt('now') // time(), $delay);

my $cfg_sla = get_macro_rdap_rollweek_sla();

slv_exit(E_FAIL) unless ($cfg_sla > 0);

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
	SLV_ITEM_KEY_RDAP_ROLLWEEK,
	ITEM_VALUE_TYPE_FLOAT,
	$max_clock,
	(opt('cycles') ? getopt('cycles') : slv_max_cycles('rdap'))
);

slv_exit(SUCCESS) if (keys(%{$cycles_ref}) == 0);

process_slv_rollweek_cycles(
	$cycles_ref,
	$delay,
	SLV_ITEM_KEY_RDAP_AVAIL,
	SLV_ITEM_KEY_RDAP_ROLLWEEK,
	$cfg_sla
);

slv_exit(SUCCESS);
