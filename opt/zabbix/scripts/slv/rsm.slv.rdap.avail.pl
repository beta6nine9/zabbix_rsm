#!/usr/bin/env perl
#
# RDAP availability

use FindBin;
use lib "$FindBin::RealBin/..";

use strict;
use warnings;

use RSM;
use RSMSLV;
use TLD_constants qw(:api);

my $keys_in = ['rdap.status'];
my $cfg_key_out = 'rsm.slv.rdap.avail';

my $cfg_value_type = ITEM_VALUE_TYPE_UINT64;

parse_slv_opts();
fail_if_running();

log_execution_time(1, 1);

set_slv_config(get_rsm_config());

db_connect();

slv_exit(SUCCESS) if (!is_rdap_standalone(getopt('now')));

# we don't know the cycle bounds yet so we assume it ends at least few minutes back
my $delay = get_rdap_delay();

# get timestamp of the beginning of the latest cycle
my $max_clock = cycle_start(getopt('now') // time(), $delay);

my $cfg_minonline = get_macro_rdap_probe_online();

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

# clean up cycles before the switch
# TODO: remove this cleanup after migrating to Standalone RDAP
foreach my $clock (sort { $a <=> $b } keys(%{$cycles_ref}))
{
	if (is_rdap_standalone($clock))
	{
		last;
	}
	delete($cycles_ref->{$clock});
}

slv_exit(SUCCESS) if (keys(%{$cycles_ref}) == 0);

my $probes_ref = get_probes('RDAP');

# process cycles before standalone RDAP switch, if any

process_slv_avail_cycles(
	$cycles_ref,
	$probes_ref,
	$delay,
	$keys_in,		# input keys
	undef,			# callback to get input keys is not needed
	$cfg_key_out,
	$cfg_minonline,
	\&check_probe_values,
	$cfg_value_type
);

slv_exit(SUCCESS);

# SUCCESS - no values or at least one successful value
# E_FAIL  - all values unsuccessful
sub check_probe_values
{
	my $values_ref = shift;

	# E. g.:
	#
	# {
	#       rdap.status => [1],
	#       rdds.status => [0, 0],
	# }

	if (scalar(keys(%{$values_ref})) == 0)
	{
		fail("THIS SHOULD NEVER HAPPEN rsm.slv.rdap.avail.pl:check_probe_values()");
	}

	# all of received items (rsm.rdds, rdap) must have status UP in order for RDDS to be considered UP
	foreach my $statuses (values(%{$values_ref}))
	{
		foreach (@{$statuses})
		{
			return E_FAIL if ($_ != UP);
		}
	}

	return SUCCESS;
}
