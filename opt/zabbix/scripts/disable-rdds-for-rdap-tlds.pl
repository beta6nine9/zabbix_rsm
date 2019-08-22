#!/usr/bin/perl

use warnings;
use strict;

use Pod::Usage qw(pod2usage);
use Path::Tiny;
use lib path($0)->parent()->realpath()->stringify();

use Zabbix;
use RSM;
use RSMSLV;
use TLD_constants qw(:general :api);
use TLDs;

sub main()
{
	my ($tlds_processed, $items_processed) = (0, 0);
	my ($single_tld, $now, $config, $server_key) = init_and_check_opts();

	my $tlds_ref = defined($single_tld) ? [ $single_tld ] : get_tlds('RDAP', $now);

	foreach my $tld (@{$tlds_ref})
	{
		if (tld_interface_enabled($tld, "rdds43", $now))
		{
			dbg("skipping TLD $tld");
			next;
		}

		dbg("processing TLD: $tld");

		# process hosts "$tld", "Template $tld" and "$tld $probe"

		my $probes_ref = get_probes('RDDS');

		my %names_item_keys = (
			"$tld" => "rsm.slv.rdds.%",
			"Template $tld" => "rsm.rdds%",
		);

		$names_item_keys{"$tld $_"} = "rsm.rdds%" foreach (keys(%{$probes_ref}));

		my $sql = "select i.itemid, i.name, h.name from items i, hosts h where i.hostid = h.hostid and ".
				"h.name = ? and i.key_ like ? and i.status = ?";
		my $items_for_tld = 0;

		while (my ($host_name, $item_key_mask) = each(%names_item_keys))
		{
			my $params = [ $host_name, $item_key_mask, ITEM_STATUS_ACTIVE ];
			my $rows = db_select($sql, $params);

			$items_for_tld += disable_items_by_rows($rows);
		}

		$items_processed += $items_for_tld;
		$tlds_processed++ if ($items_for_tld > 0);
	}

	db_disconnect();

	info("$tlds_processed TLD(s) with total of $items_processed item(s) processed");

	return SUCCESS;
}

sub init_and_check_opts()
{
	my ($tld_opt, $now, $config, $server_key);

	parse_opts('tld=s', 'now=n', 'server-id=s');

	fail_if_running();

	$config = get_rsm_config();
	set_slv_config($config);

	$server_key = opt('server-id') ? get_rsm_server_key(getopt('server-id')) : get_rsm_local_key($config);
	db_connect($server_key);

	$now = getopt('now') // time();
	$tld_opt = getopt('tld');

	if (defined($tld_opt) && !tld_exists($tld_opt))
	{
		fail("TLD $tld_opt not found on server $server_key");
	}

	unless (is_rdap_standalone($now))
	{
		info("RDAP is not standalone yet");
		slv_exit(SUCCESS);
	}

	init_zabbix_api($config, $server_key);

	return ($tld_opt, $now, $config, $server_key);
}

sub disable_items_by_rows($)
{
	my $rows = shift;
	my $item_counter = 0;

	foreach my $row (@{$rows})
	{
		my ($item, $name, $host_name) = @{$row};

		dbg("processing '$host_name': $item ($name)");

		unless (opt("dry-run"))
		{
			$result = disable_items([ $item ]);

			if (defined($result->{$item}->{'error'}))
			{
				fail("Failed to disable item $item for host '$host_name'");
			}

			$item_counter++;
		}
	}

	return $item_counter;
}

# copied from tld.pl

sub init_zabbix_api($$)
{
	my $config     = shift;
	my $server_key = shift;

	my $section = $config->{$server_key};

	fail("Zabbix API URL is not specified. Please check configuration file") unless defined $section->{'za_url'};
	fail("Username for Zabbix API is not specified. Please check configuration file") unless defined $section->{'za_user'};
	fail("Password for Zabbix API is not specified. Please check configuration file") unless defined $section->{'za_password'};

	my $attempts = 3;
	my $result;
	my $error;

	RELOGIN:
	$result = zbx_connect($section->{'za_url'}, $section->{'za_user'}, $section->{'za_password'}, getopt('debug'));

	if ($result ne true)
	{
		fail("Could not connect to Zabbix API. " . $result->{'data'});
	}

	# make sure we re-login in case of session invalidation
	my $tld_probe_results_groupid = create_group('TLD Probe results');

	$error = get_api_error($tld_probe_results_groupid);

	if (defined($error))
	{
		if (zbx_need_relogin($tld_probe_results_groupid) eq true)
		{
			goto RELOGIN if (--$attempts);
		}

		fail($error);
	}
}

slv_exit(main());

__END__

=head1 NAME

disable-rdds-for-rdap-tlds.pl - disable unused RDDS SLV items after switch to Standalone RDAP.

=head1 SYNOPSIS

disable-rdds-for-rdap-tlds.pl [--tld=TLD] [--dry-run] [--now=TIME] [--server-id=ID] [--debug] [--help]

=head1 OPTIONS

=over 8

=item B<--tld=TLD>

Execute script for specific TLD.

=item B<--now=TIME>

Specify Unix timestamp to be assumed as current time.

=item B<--server-id=ID>

Specify server id to connect instead of local one.

=item B<--dry-run>

Scan TLDs and their relevant items but do not change item statuses.

=item B<--debug>

Run the script in debug mode.

=item B<--help>

Display this help message and exit.

=back

=cut
