#!/usr/bin/perl

BEGIN
{
	our $MYDIR = $0; $MYDIR =~ s,(.*)/.*,$1,; $MYDIR = '.' if ($MYDIR eq $0);
	our $MYDIR2 = $0; $MYDIR2 =~ s,(.*)/.*/.*,$1,; $MYDIR2 = '..' if ($MYDIR2 eq $0);
}
use lib $MYDIR;
use lib $MYDIR2;

use warnings;
use strict;

use RSM;
use RSMSLV;

use constant SLV_ITEM_KEY_RDDS_AVAIL         => 'rsm.slv.rdds.avail';
use constant SLV_ITEM_KEY_RDDS_DOWNTIME      => 'rsm.slv.rdds.downtime';
use constant SLV_ITEM_KEY_RDDS_ROLLWEEK      => 'rsm.slv.rdds.rollweek';
use constant SLV_ITEM_KEY_RDDS_RTT_FAILED    => 'rsm.slv.rdds.rtt.failed';
use constant SLV_ITEM_KEY_RDDS_RTT_PERFORMED => 'rsm.slv.rdds.rtt.performed';
use constant SLV_ITEM_KEY_RDDS_RTT_PFAILED   => 'rsm.slv.rdds.rtt.pfailed';

parse_opts('tld=s', 'now=n');

my $config = get_rsm_config();
set_slv_config($config);

my $now = opt('now') ? getopt('now') : time();
my @server_keys = get_rsm_server_keys($config);
my $tld_opt;

validate_tld($tld_opt = getopt('tld'), \@server_keys) if (opt('tld'));

db_connect();
# fail("RDAP is not standalone yet") if (!is_rdap_standalone($now));
db_disconnect();

foreach (@server_keys)
{
	$server_key = $_;

	db_connect($server_key);

	my $tlds_ref = defined($tld_opt) ? [ $tld_opt ] : get_tlds('RDDS', $now);

	foreach my $tld (@{$tlds_ref})
	{
		dbg("TLD: $tld");
		if (!tld_service_enabled($tld, "rdds". $now))
		{
			my $items = get_templated_items_like($tld, "rsm.slv.rdds");
		}
	}

	db_disconnect();
}
