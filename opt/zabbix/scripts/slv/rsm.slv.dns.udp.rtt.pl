#!/usr/bin/env perl

use FindBin;
use lib "$FindBin::RealBin/..";

use strict;
use warnings;

use RSM;
use RSMSLV;
use TLD_constants qw(:api);

parse_slv_opts();
fail_if_running();

log_execution_time(1, 1);

set_slv_config(get_rsm_config());

db_connect();

slv_exit(SUCCESS) if (get_monitoring_target() ne MONITORING_TARGET_REGISTRY);

my $single_tld;

if (opt("tld"))
{
	$single_tld = getopt("tld");

	fail("TLD '$single_tld' not found") unless tld_exists($single_tld);
}

use constant SLV_ITEM_KEY_DNS_UDP_PERFORMED     => "rsm.slv.dns.udp.rtt.performed";
use constant SLV_ITEM_KEY_DNS_UDP_FAILED        => "rsm.slv.dns.udp.rtt.failed";
use constant SLV_ITEM_KEY_DNS_UDP_PFAILED       => "rsm.slv.dns.udp.rtt.pfailed";

use constant RTT_ITEM_KEY_PATTERN_DNS_UDP_IPV4  => 'rsm.dns.rtt[%,%.%,udp]';
use constant RTT_ITEM_KEY_PATTERN_DNS_UDP_IPV6  => 'rsm.dns.rtt[%,%:%,udp]';

use constant RTT_ITEM_KEY_LASTCLOCK_CONTROL     => 'rsm.dns.status';

use constant RTT_TIMEOUT_ERROR_DNS_UDP          => -200;

my $rtt_low_dns_udp = get_rtt_low("dns", PROTO_UDP);

update_slv_rtt_monthly_stats(
	getopt('now') // time(),
	opt('cycles') ? getopt('cycles') : slv_max_cycles('dns'),
	$single_tld,
	SLV_ITEM_KEY_DNS_UDP_PERFORMED,
	SLV_ITEM_KEY_DNS_UDP_FAILED,
	SLV_ITEM_KEY_DNS_UDP_PFAILED,
	get_dns_delay(),
	[
		{
			'probes'                     => get_probes("IP4"),
			'tlds_service'               => "dns",
			'rtt_item_key_pattern'       => RTT_ITEM_KEY_PATTERN_DNS_UDP_IPV4,
			'lastclock_control_item_key' => RTT_ITEM_KEY_LASTCLOCK_CONTROL,
			'timeout_error_value'        => RTT_TIMEOUT_ERROR_DNS_UDP,
			'timeout_threshold_value'    => $rtt_low_dns_udp,
		},
		{
			'probes'                     => get_probes("IP6"),
			'tlds_service'               => "dns",
			'rtt_item_key_pattern'       => RTT_ITEM_KEY_PATTERN_DNS_UDP_IPV6,
			'lastclock_control_item_key' => RTT_ITEM_KEY_LASTCLOCK_CONTROL,
			'timeout_error_value'        => RTT_TIMEOUT_ERROR_DNS_UDP,
			'timeout_threshold_value'    => $rtt_low_dns_udp,
		},
	]
);

slv_exit(SUCCESS);
