#!/usr/bin/env perl

use FindBin;
use lib $FindBin::RealBin;

use strict;
use warnings;

use RSM;
use RSMSLV;
use TLD_constants qw(:api :groups);
use Data::Dumper;

use constant MACRO_CONFIG_CACHE_RELOAD => '{$RSM.CONFIG.CACHE.RELOAD.REQUESTED}';

use constant ZABBIX_SERVER_CMD => '/usr/sbin/zabbix_server';
use constant ZABBIX_SERVER_CFG => '/etc/zabbix/zabbix_server.conf';

sub main()
{
	parse_opts();
	setopt('nolog');

	set_slv_config(get_rsm_config());

	db_connect();

	my $macro_value = db_select_value('select value from globalmacro where macro=?', [MACRO_CONFIG_CACHE_RELOAD]);

	if ($macro_value)
	{
		db_exec('update globalmacro set value=? where macro=?', [0, MACRO_CONFIG_CACHE_RELOAD]);
	}

	db_disconnect();

	if ($macro_value)
	{
		my $cmd = ZABBIX_SERVER_CMD;
		my @args = ();

		push(@args, '--config', ZABBIX_SERVER_CFG);
		push(@args, '--runtime-control', 'config_cache_reload');

		@args = map('"' . $_ . '"', @args);

		dbg("executing $cmd @args");
		my $out = qx($cmd @args 2>&1);

		if ($out)
		{
			info("output of $cmd:\n" . $out);
		}

		if ($? == -1)
		{
			fail("failed to execute $cmd: $!");
		}
		if ($? != 0)
		{
			fail("command $cmd exited with value " . ($? >> 8));
		}
	}

	slv_exit(SUCCESS);
}

main();

__END__

=head1 NAME

config-cache-reload.pl - perform config-cache-reload if provisioning scripts have made any changes recently.

=head1 SYNOPSIS

config-cache-reload.pl [--debug] [--help]

=head1 OPTIONS

=over 8

=item B<--debug>

Run the script in debug mode. This means printing more information.

=item B<--help>

Print a brief help message and exit.

=back

=cut
