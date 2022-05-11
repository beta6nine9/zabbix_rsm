#!/usr/bin/env perl
#
# DNSSEC availability

use FindBin;
use lib "$FindBin::RealBin/..";

use strict;
use warnings;

use RSM;
use RSMSLV;
use TLD_constants qw(:ec :api);

my $cfg_keys_in = ['rsm.dnssec.status'];
my $cfg_key_out = 'rsm.slv.dnssec.avail';
my $cfg_value_type = ITEM_VALUE_TYPE_UINT64;

parse_slv_opts();
fail_if_running();

log_execution_time(1);

set_slv_config(get_rsm_config());

db_connect();

slv_exit(SUCCESS) if (get_monitoring_target() ne MONITORING_TARGET_REGISTRY);

# we don't know the rollweek bounds yet so we assume it ends at least few minutes back
my $delay = get_dns_delay();

my $max_clock = cycle_start(getopt('now') // time(), $delay);

my $cfg_minonline = get_macro_dns_probe_online();

my $tlds_ref;
if (opt('tld'))
{
	fail("TLD ", getopt('tld'), " does not exist.") if (tld_exists(getopt('tld')) == 0);

	$tlds_ref = [ getopt('tld') ];
}
else
{
	$tlds_ref = get_tlds('DNSSEC', $max_clock);
}

slv_exit(SUCCESS) if (scalar(@{$tlds_ref}) == 0);

my $cycles_ref = collect_slv_cycles(
	$tlds_ref,
	$delay,
	$cfg_key_out,
	ITEM_VALUE_TYPE_UINT64,
	$max_clock,
	(opt('cycles') ? getopt('cycles') : slv_max_cycles('dnssec'))
);

slv_exit(SUCCESS) if (scalar(keys(%{$cycles_ref})) == 0);

my $probes_ref = get_probes('DNSSEC');

process_slv_avail_cycles(
	$cycles_ref,
	$probes_ref,
	$delay,
	$cfg_keys_in,
	undef,			# callback to get input keys, ignored
	$cfg_key_out,
	$cfg_minonline,
	\&check_probe_values,
	$cfg_value_type
);

slv_exit(SUCCESS);

# SUCCESS - DNS Test on the probe returned DNSSEC status 1
# E_FAIL  - otherwise
sub check_probe_values
{
	my $values_ref = shift;

	# E. g.:
	#
	# {
	#	'rsm.dnssec.status' => [1]
	# }

	if (scalar(keys(%{$values_ref})) == 0)
	{
		fail("THIS SHOULD NEVER HAPPEN rsm.slv.dnssec.avail.pl:check_probe_values()");
	}

	# stay on the safe side: if more than one value in cycle, use the positive one
	foreach my $values (values(%{$values_ref}))
	{
		foreach (@{$values})
		{
			return SUCCESS if ($_ == 1);
		}
	}

	return E_FAIL;
}
