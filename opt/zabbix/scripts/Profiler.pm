package Profiler;

use strict;
use warnings;

use Exporter qw(import);
our @EXPORT = qw(
	prof_start
	prof_end
);

use Time::HiRes;

my %profiler;

END {
	foreach my $id (sort(keys(%profiler)))
	{
		printf("profiler stats - id '%s', count %d, time %.3f\n", $id, $profiler{$id}{'count'}, $profiler{$id}{'time'});
	}
}

sub prof_start($)
{
	my $id = shift;

	if (!exists($profiler{$id}))
	{
		$profiler{$id} = {
			'count' => 0,
			'time'  => 0.0,
		};
	}

	$profiler{$id}{'count'}++;
	$profiler{$id}{'time'} -= Time::HiRes::time();
}

sub prof_end($)
{
	my $id = shift;

	if (!exists($profiler{$id}))
	{
		fail("profiler stats for '$id' do not exist");
	}

	$profiler{$id}{'time'} += Time::HiRes::time();
}

1;
