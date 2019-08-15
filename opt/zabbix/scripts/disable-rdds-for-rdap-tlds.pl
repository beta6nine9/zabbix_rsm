#!/usr/bin/perl

use warnings;
use strict;

use Data::Dumper;
use Pod::Usage qw(pod2usage);
use Path::Tiny;
use lib path($0)->parent()->realpath()->stringify();

use Zabbix;
use RSM;
use RSMSLV;
use TLD_constants qw(:general :templates :groups :value_types :ec :slv :config :api);
use TLDs;

sub main()
{
	my ($tlds_processed, $items_processed) = (0, 0);
	my ($tld_opt, @server_keys, $now, $config);

	parse_opts('tld=s', 'now=n', 'enable', 'disable');

	if (!opt('enable') and !opt('disable'))
	{
		s_fail("Either --enable or --disable must be provided. Run \"$0 --help\" to get usage information.");
	}

	fail_if_running();

	$config = get_rsm_config();
	set_slv_config($config);
	@server_keys = get_rsm_server_keys($config);

	$now = getopt('now') // time();

	validate_tld($tld_opt = getopt('tld'), \@server_keys) if (opt('tld'));

	fail_unless_rdap_standalone($now);

	foreach (@server_keys)
	{
		$server_key = $_;

		db_connect($server_key);
		init_zabbix_api($config, $server_key);

		my $tlds_ref = defined($tld_opt) ? [ $tld_opt ] : get_tlds('RDAP', $now);

		foreach my $tld (@{$tlds_ref})
		{
			if (tld_interface_enabled($tld, "rdds43", $now))
			{
				dbg("Skipping TLD $tld");
				next;
			}

			my $sql = "select i.itemid,i.name from items i, hosts h where i.hostid=h.hostid and ".
					"h.name=? and i.key_ like 'rsm.slv.rdds.%' and i.status=?";
			my $params = [ $tld, opt('disable') ? ITEM_STATUS_ACTIVE : ITEM_STATUS_DISABLED ];
			my $rows = db_select($sql, $params);

			foreach my $row (@{$rows})
			{
				my ($item, $name) = @{$row};

				dbg("Processing $tld: $item ($name)");

				unless (opt("dry-run"))
				{
					$result = opt('enable') ? enable_items([ $item ]) : disable_items([ $item ]);

					$items_processed++ if (!defined($result->{$item}->{'error'}));
				}
			}

			$tlds_processed++ if (scalar(@{$rows}));
		}

		db_disconnect();
	}

	my $msg = "$tlds_processed TLD(s) with total of $items_processed item(s) processed";
	info($msg);
	print($msg, "\n");

	return SUCCESS;
}

sub fail_unless_rdap_standalone($)
{
	my $now = shift;

	db_connect();

	my $proceed = is_rdap_standalone($now);

	db_disconnect();

	s_fail("Error: RDAP is not standalone yet") unless ($proceed);
}

# copied from tld.pl with minor changes

sub init_zabbix_api($$)
{
	my $config     = shift;
	my $server_key = shift;

	my $section = $config->{$server_key};

	s_fail("Zabbix API URL is not specified. Please check configuration file") unless defined $section->{'za_url'};
	s_fail("Username for Zabbix API is not specified. Please check configuration file") unless defined $section->{'za_user'};
	s_fail("Password for Zabbix API is not specified. Please check configuration file") unless defined $section->{'za_password'};

	my $result;
	my $error;

	$result = zbx_connect($section->{'za_url'}, $section->{'za_user'}, $section->{'za_password'}, getopt('debug'));

	if ($result ne true)
	{
		s_fail("Could not connect to Zabbix API. " . $result->{'data'});
	}
}

sub s_fail($)
{
	my $msg = shift;

	print STDERR ($msg, "\n");

	fail($msg);
}

slv_exit(main());

__END__

=head1 NAME

disable-rdds-for-rdap-tlds.pl - disable unused RDDS SLV items after switch to Standalone RDAP.

=head1 SYNOPSIS

disable-rdds-for-rdap-tlds.pl --disable | --enable [--tld=TLD] [--dry-run] [--now=TIME] [--debug]

This script disables unused RDDS SLV items after switch to Standalone RDAP. The following items are affected:

=item rsm.slv.rdds.avail

=item rsm.slv.rdds.downtime

=item rsm.slv.rdds.rollweek

=item rsm.slv.rdds.rtt.failed

=item rsm.slv.rdds.rtt.performed

=item rsm.slv.rdds.rtt.pfailed

=head1 OPTIONS

=over 8

=item B<--disable>

Disable unused RDDS SLV items for TLD(s).

=item B<--enable>

Enable unused RDDS SLV items for TLD(s). This option should be used for testing only.

=item B<--tld=TLD>

Execute script for specific TLD.

=item B<--dry-run>

Scan TLDs and their relevant items but do not change item statuses.

=item B<--debug>

Run the script in debug mode.

=item B<--help>

Display this help message and exit.

=back

=cut
