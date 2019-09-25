#!/usr/bin/perl

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

parse_slv_opts();
fail_if_running();

set_slv_config(get_rsm_config());

db_connect();

slv_exit(SUCCESS) if (!is_rdap_standalone(getopt('now')));

my $single_tld;

if (opt("tld"))
{
	$single_tld = getopt("tld");

	fail("TLD '$single_tld' not found") unless tld_exists($single_tld);
}

use constant SLV_ITEM_KEY_RDAP_PERFORMED        => "rsm.slv.rdap.rtt.performed";
use constant SLV_ITEM_KEY_RDAP_FAILED           => "rsm.slv.rdap.rtt.failed";
use constant SLV_ITEM_KEY_RDAP_PFAILED          => "rsm.slv.rdap.rtt.pfailed";
use constant RTT_ITEM_KEY_PATTERN_RDAP          => 'rdap.rtt';
use constant RTT_TIMEOUT_ERROR_RDAP             => -405;

my $rtt_low_rdap = get_rtt_low("rdap");
my $now = getopt('now') // time();
my $rtt_params_list =
[
	{
		'probes'                  => get_probes("RDAP"),
		'tlds_service'            => "rdap",
		'rtt_item_key_pattern'    => RTT_ITEM_KEY_PATTERN_RDAP,
		'timeout_error_value'     => RTT_TIMEOUT_ERROR_RDAP,
		'timeout_threshold_value' => $rtt_low_rdap
	}
];

update_slv_rtt_monthly_stats(
	$now,
	opt('cycles') ? getopt('cycles') : slv_max_cycles('rdap'),
	$single_tld,
	SLV_ITEM_KEY_RDAP_PERFORMED,
	SLV_ITEM_KEY_RDAP_FAILED,
	SLV_ITEM_KEY_RDAP_PFAILED,
	get_rdap_delay(),
	$rtt_params_list
);

slv_exit(SUCCESS);
