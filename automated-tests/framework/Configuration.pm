package Configuration;

use strict;
use warnings;

use Exporter qw(import);
our @EXPORT = qw(
	initialize_config
	get_config
);

use Config::Tiny;
use Data::Dumper;
use FindBin;

use Output;

use constant CONFIG_FILE => "$FindBin::RealBin/tests.conf";

my $config;

sub initialize_config()
{
	$config = Config::Tiny->new;
	$config = Config::Tiny->read(CONFIG_FILE);

	if (!defined($config))
	{
		fail(Config::Tiny->errstr());
	}
}

sub get_config($$)
{
	my $section  = shift;
	my $property = shift;

	if (!defined($config))
	{
		fail('config has not been initialized');
	}
	if (!exists($config->{$section}))
	{
		fail("section '$section' does not exist in the config file");
	}
	if (!exists($config->{$section}{$property}))
	{
		fail("property '$section.$property' does not exist in the config file");
	}

	my $value = $config->{$section}{$property};

	if ($section eq 'paths' && $value eq '')
	{
		fail("property '$section.$property' is empty in the config file");
	}

	return $value;
}

1;
