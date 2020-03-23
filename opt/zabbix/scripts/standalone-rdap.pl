#!/usr/bin/env perl

use strict;
use warnings;

use Path::Tiny;
use lib path($0)->parent->realpath()->stringify();

use RSM;
use RSMSLV;
use TLD_constants qw(:api);
use DateTime;

use constant MACRO_RDAP_STANDALONE           => '{$RSM.RDAP.STANDALONE}';

use constant SLV_ITEM_KEY_RDAP_AVAIL         => 'rsm.slv.rdap.avail';
use constant SLV_ITEM_KEY_RDAP_DOWNTIME      => 'rsm.slv.rdap.downtime';
use constant SLV_ITEM_KEY_RDAP_ROLLWEEK      => 'rsm.slv.rdap.rollweek';
use constant SLV_ITEM_KEY_RDAP_RTT_FAILED    => 'rsm.slv.rdap.rtt.failed';
use constant SLV_ITEM_KEY_RDAP_RTT_PERFORMED => 'rsm.slv.rdap.rtt.performed';
use constant SLV_ITEM_KEY_RDAP_RTT_PFAILED   => 'rsm.slv.rdap.rtt.pfailed';

sub main()
{
	my $config = get_rsm_config();

	set_slv_config($config);

	parse_opts("state", "enable", "disable");
	check_opts();
	setopt("nolog");

	my @server_keys = get_rsm_server_keys($config);

	if (opt("state"))
	{
		print_state(\@server_keys);
	}
	else
	{
		check_state(\@server_keys);
		set_state(\@server_keys);
	}

	print("\n");
	print("If you enabled Standalone RDAP by mistake, you can disable it before it has started by running:\n");
	print("    $0 --disable\n");

	slv_exit(SUCCESS);
}

sub check_opts()
{
	my $opt_count = 0;

	$opt_count++ if (opt("state"));
	$opt_count++ if (opt("enable"));
	$opt_count++ if (opt("disable"));

	if ($opt_count == 0)
	{
		pfail("Missing option: --state, --enable, --disable");
	}

	if ($opt_count > 1)
	{
		pfail("Only one option may be used: --state, --enable, --disable");
	}
}

sub print_state($)
{
	my $server_keys = shift;

	foreach my $server_key (@{$server_keys})
	{
		db_connect($server_key);

		my $start_time = get_rdap_standalone_ts();

		if ($start_time)
		{
			print("$server_key: Standalone RDAP is enabled (" . DateTime->from_epoch('epoch' => $start_time) . ")\n");
		}
		else
		{
			print("$server_key: Standalone RDAP is disabled\n");
		}

		db_disconnect($server_key);
	}
}

sub check_state($)
{
	my $server_keys = shift;

	foreach my $server_key (@{$server_keys})
	{
		db_connect($server_key);

		if (opt("enable"))
		{
			if (get_rdap_standalone_ts())
			{
				pfail("$server_key: Standalone RDAP is already enabled");
			}
		}
		elsif (opt("disable"))
		{
			if (!get_rdap_standalone_ts())
			{
				pfail("$server_key: Standalone RDAP is already disabled");
			}
			if (is_rdap_standalone())
			{
				pfail("$server_key: Treating RDAP as a standalone service has already started");
			}
		}

		db_disconnect();
	}
}

sub set_state($)
{
	my $server_keys = shift;

	my $start_time;
	my $item_status;

	if (opt("enable"))
	{
		$start_time = DateTime->now()->truncate('to' => 'month')->add('months' => 1)->epoch();
		$item_status = ITEM_STATUS_ACTIVE;

		print("Enabling Standalone RDAP\n");
		print("Treating RDAP as a standalone service will start at " . DateTime->from_epoch('epoch' => $start_time) . "\n");
	}
	elsif (opt("disable"))
	{
		$start_time = 0;
		$item_status = ITEM_STATUS_DISABLED;

		print("Disabling Standalone RDAP\n");
	}

	if (!opt("dry-run"))
	{
		my $items_sql = "update items set status = ? where key_ in (?, ?, ?, ?, ?, ?)";
		my $items_params = [
			$item_status,
			SLV_ITEM_KEY_RDAP_AVAIL,
			SLV_ITEM_KEY_RDAP_DOWNTIME,
			SLV_ITEM_KEY_RDAP_ROLLWEEK,
			SLV_ITEM_KEY_RDAP_RTT_FAILED,
			SLV_ITEM_KEY_RDAP_RTT_PERFORMED,
			SLV_ITEM_KEY_RDAP_RTT_PFAILED
		];

		my $macro_sql = "update globalmacro set value=? where macro=?";
		my $macro_params = [
			$start_time,
			MACRO_RDAP_STANDALONE
		];

		foreach my $server_key (@{$server_keys})
		{
			db_connect($server_key);
			db_exec($items_sql, $items_params);
			db_exec($macro_sql, $macro_params);
			db_disconnect();
		}
	}
}

sub pfail
{
	print("Error: @_\n");
	slv_exit(E_FAIL);
}

main();

__END__

=head1 NAME

standalone-rdap.pl - treat RDAP as a standalone service.

=head1 SYNOPSIS

standalone-rdap.pl [--state] [--enable] [--disable] [--dry-run] [--debug] [--help]

=head1 OPTIONS

=over 8

=item B<--state>

Check current state of Standalone RDAP.

=item B<--enable>

Enable Standalone RDAP.

=item B<--disable>

Disable Standalone RDAP.

=item B<--dry-run>

Check if Standalone RDAP can be enabled or disabled, do not change anything in the system.

=item B<--debug>

Run the script in debug mode. This means printing more information.

=item B<--help>

Print a brief help message and exit.

=back

=cut
