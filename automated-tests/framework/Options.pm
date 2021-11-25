package Options;

use strict;
use warnings;

use Exporter qw(import);
our @EXPORT = qw(
	parse_opts
	opt
	getopt
	setopt
	unsetopt
	usage
);

use Data::Dumper;
use Getopt::Long qw(GetOptionsFromArray);
use Pod::Usage;

my %OPTS;

sub parse_opts
{
	my @args = @ARGV;

	my $rv = GetOptionsFromArray(\@args, \%OPTS, @_);

	if (@args)
	{
		usage('Failed to parse all arguments: ' . join(', ', @args), 1);
	}

	if (!$rv || $OPTS{'help'})
	{
		usage(undef, 0);
	}
}

sub opt($)
{
	my $key = shift;

	return exists($OPTS{$key});
}

sub getopt($)
{
	my $key = shift;

	return exists($OPTS{$key}) ? $OPTS{$key} : undef;
}

sub setopt($$)
{
	my $key   = shift;
	my $value = shift;

	$OPTS{$key} = $value;
}

sub unsetopt($)
{
	my $key = shift;

	delete($OPTS{$key});
}

sub usage($$)
{
	my $message = shift;
	my $exitval = shift;

	pod2usage(
		-message => $message,
		-exitval => $exitval,
		-verbose => 2,
		-noperldoc,
		-output  => $exitval == 0 ? \*STDOUT : \*STDERR,
	);
}

1;
