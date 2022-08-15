#!/usr/bin/env perl
#
# RDDS rolling week

use FindBin;
use lib "$FindBin::RealBin/..";

use strict;
use warnings;

use RSM;
use RSMSLV;
use TLD_constants qw(:api);

my $cfg_key_in = 'rsm.slv.rdds.avail';
my $cfg_key_out = 'rsm.slv.rdds.rollweek';

parse_slv_opts();
fail_if_running();

log_execution_time(1, 1);

set_slv_config(get_rsm_config());

db_connect();

# we don't know the rollweek bounds yet so we assume it ends at least few minutes back
my $delay = get_rdds_delay();

my $max_clock = cycle_start(getopt('now') // time(), $delay);

my $cfg_sla = get_macro_rdds_rollweek_sla();

slv_exit(E_FAIL) unless ($cfg_sla > 0);

my $tlds_ref;
if (opt('tld'))
{
	fail("TLD ", getopt('tld'), " does not exist.") if (tld_exists(getopt('tld')) == 0);

	$tlds_ref = [ getopt('tld') ];
}
else
{
	$tlds_ref = get_tlds('RDDS', $max_clock);
}

slv_exit(SUCCESS) if (scalar(@{$tlds_ref}) == 0);

my $cycles_ref = collect_slv_cycles(
	$tlds_ref,
	$delay,
	$cfg_key_out,
	ITEM_VALUE_TYPE_FLOAT,
	$max_clock,
	(opt('cycles') ? getopt('cycles') : slv_max_cycles('rdds'))
);

slv_exit(SUCCESS) if (scalar(keys(%{$cycles_ref})) == 0);

process_slv_rollweek_cycles($cycles_ref, $delay, $cfg_key_in, $cfg_key_out, $cfg_sla);

slv_exit(SUCCESS);
