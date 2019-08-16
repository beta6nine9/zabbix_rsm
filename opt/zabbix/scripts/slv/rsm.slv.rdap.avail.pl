#!/usr/bin/perl
#
# RDAP availability

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

use constant SLV_ITEM_KEY_RDAP_AVAIL	=> 'rsm.slv.rdap.avail';

my $cfg_rdap_key_in = 'rdap[';
my $cfg_value_type = ITEM_VALUE_TYPE_UINT64;

parse_slv_opts();
fail_if_running();

set_slv_config(get_rsm_config());

db_connect();

slv_exit(SUCCESS) if (!is_rdap_standalone(getopt('now')));

# get cycle length
my $delay = get_rdap_delay(getopt('now') // time() - AVAIL_SHIFT_BACK);

# get timestamp of the beginning of the latest cycle
my (undef, undef, $max_clock) = get_cycle_bounds($delay, getopt('now'));

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

# assume all cycles calculated by collect_slv_cycles() fall into period after standalone RDAP switch
# because rsm.slv.rdap.avail should not have any values collected before the switch

my $cycles_ref = collect_slv_cycles(
	$tlds_ref,
	$delay,
	SLV_ITEM_KEY_RDAP_AVAIL,
	ITEM_VALUE_TYPE_UINT64,
	$max_clock,
	(opt('cycles') ? getopt('cycles') : slv_max_cycles('rdap'))
);

slv_exit(SUCCESS) if (keys(%{$cycles_ref}) == 0);

my $probes_ref = get_probes('RDAP');

# process cycles before standalone RDAP switch, if any

process_slv_avail_cycles(
	$cycles_ref,
	$probes_ref,
	$delay,
	undef,			# input keys are unknown
	\&cfg_keys_in_cb,	# callback to get input keys
	SLV_ITEM_KEY_RDAP_AVAIL,
	$cfg_minonline,
	\&check_probe_values,
	$cfg_value_type
);

slv_exit(SUCCESS);

my $rdap_items;

sub cfg_keys_in_cb($)
{
	my $tld = shift;

	$rdap_items = get_templated_items_like("RDAP", $cfg_rdap_key_in) unless (defined($rdap_items));

	return $rdap_items;
}

# SUCCESS - no values or at least one successful value
# E_FAIL  - all values unsuccessful
sub check_probe_values
{
	my $values_ref = shift;

	# E. g.:
	#
	# {
	#       rsm.rdds[{$RSM.TLD},"rdds43.example.com","web.whois.example.com"] => [1],
	#       rdap[...] => [0, 0],
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
