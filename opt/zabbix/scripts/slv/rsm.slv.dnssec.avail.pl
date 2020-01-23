#!/usr/bin/perl
#
# DNSSEC availability

BEGIN
{
	our $MYDIR = $0; $MYDIR =~ s,(.*)/.*/.*,$1,; $MYDIR = '..' if ($MYDIR eq $0);
}
use lib $MYDIR;

use strict;
use warnings;
use RSM;
use RSMSLV;
use TLD_constants qw(:ec :api);

my $cfg_keys_in_pattern = 'rsm.dns.rtt[';
my $cfg_key_out = 'rsm.slv.dnssec.avail';
my $cfg_value_type = ITEM_VALUE_TYPE_FLOAT;

parse_slv_opts();
fail_if_running();

set_slv_config(get_rsm_config());

db_connect();

slv_exit(SUCCESS) if (get_monitoring_target() ne MONITORING_TARGET_REGISTRY);

# we don't know the rollweek bounds yet so we assume it ends at least few minutes back
# we use both tcp and udp rtt values, but take the delay value from the udp macro only
my $delay = get_dns_udp_delay(getopt('now') // time() - AVAIL_SHIFT_BACK);

my (undef, undef, $max_clock) = get_cycle_bounds($delay, getopt('now'));

my $cfg_minonline = get_macro_dns_probe_online();
my $cfg_minns = get_macro_minns();

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

my $probes_ref = get_probes('DNS');

process_slv_avail_cycles(
	$cycles_ref,
	$probes_ref,
	$delay,
	undef,			# input keys are unknown
	\&cfg_keys_in_cb,	# callback to get input keys
	$cfg_key_out,
	$cfg_minonline,
	\&check_probe_values,
	$cfg_value_type
);

slv_exit(SUCCESS);

sub cfg_keys_in_cb($)
{
	my $tld = shift;

	return get_items_like($tld, $cfg_keys_in_pattern);
}

sub get_items_like($$)
{
	my $tld = shift;
	my $key_in = shift;
	my $params = ["$key_in%","$tld %"];

	my $result = db_select_col(
		"select i.key_".
		" from items i,hosts h".
		" where i.key_ like ?".
		" and h.host like ?".
		" and i.templateid is NULL".
		" and i.hostid=h.hostid".
		" and i.status<>".ITEM_STATUS_DISABLED, $params);

	return $result;
}

# SUCCESS - more than or equal to $cfg_minns Name Servers returned no DNSSEC errors
# E_FAIL  - otherwise
sub check_probe_values
{
	my $values_ref = shift;

	# E. g.:
	#
	# {
	#	rsm.dns.rtt[ns1.hazelburn,172.19.0.4,tcp] => [1]
	#	rsm.dns.rtt[ns1.hazelburn,172.19.0.4,udp] => [-650]
	# }

	if (scalar(keys(%{$values_ref})) == 0)
	{
		fail("THIS SHOULD NEVER HAPPEN rsm.slv.dnssec.avail.pl:check_probe_values()");
	}

	if (1 > $cfg_minns)
	{
		wrn("number of required working Name Servers is configured as $cfg_minns");

		return SUCCESS;
	}

	my %name_servers;

	# stay on the safe side: if more than one value in cycle, use the positive one
	foreach my $key (keys(%{$values_ref}))
	{
		my $ns = $key;
		$ns =~ s/[^,]+,([^,]+),.*/$1/;	# 2nd parameter

		# check if Name Server already marked as Down
		next if (defined($name_servers{$ns}) && $name_servers{$ns} == DOWN);

		foreach my $rtt (@{$values_ref->{$key}})
		{
			$name_servers{$ns} = (is_service_error('dnssec', $rtt) ? DOWN : UP);
		}
	}

	my $name_servers_up = 0;

	foreach (values(%name_servers))
	{
		$name_servers_up++ if ($_ == UP);

		return SUCCESS if ($name_servers_up == $cfg_minns);
	}

	return E_FAIL;
}
