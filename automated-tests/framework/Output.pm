package Output;

use strict;
use warnings;

use Exporter qw(import);
our @EXPORT = qw(
	initialize_log
	log_debug_messages
	dbg
	info
	wrn
	fail
);

use Data::Dumper;
use Devel::StackTrace;
use Fcntl qw(:flock SEEK_END);

use constant LOG_FILE => 'output.log';

use constant LOG_LEVEL_DEBUG   => 1;
use constant LOG_LEVEL_INFO    => 2;
use constant LOG_LEVEL_WARNING => 3;
use constant LOG_LEVEL_FAILURE => 4;

my $log_time_str;
my $log_time = 0;

my $log_debug_messages = 0;
my $log_timestamps     = undef;
my $finalize_func_ref  = undef;

$SIG{__WARN__} = sub
{
	local *__ANON__ = 'perl-warn';
	__log(LOG_LEVEL_WARNING, $_[0] =~ s/(\r?\n)+$//r);
};

$SIG{__DIE__} = sub
{
	# Archive::Tar checks presence of IO::String in a way that just dies
	if ([caller()]->[0] eq 'Archive::Tar')
	{
		die($_[0]);
	}

	local *__ANON__ = 'perl-die';
	__log(LOG_LEVEL_FAILURE, $_[0] =~ s/(\r?\n)+$//r);
	$finalize_func_ref->() if (defined($finalize_func_ref));
	exit(255); # Perl's default exit code on die()
};

sub initialize_log($$$)
{
	my $use_log_file = shift;

	$log_timestamps    = shift; # store as global variable
	$finalize_func_ref = shift; # store as global variable

	if ($use_log_file)
	{
		close(STDOUT) or fail("cannot close STDOUT: $!");
		close(STDERR) or fail("cannot close STDERR: $!");

		open(STDOUT, '>>', LOG_FILE) or fail("cannot open ${\LOG_FILE}: $!");
		open(STDERR, '>>', LOG_FILE) or fail("cannot open ${\LOG_FILE}: $!");
	}
}

sub log_debug_messages(;$)
{
	my $log_debug_messages_tmp = $log_debug_messages;
	if (@_)
	{
		$log_debug_messages = shift;
	}
	return $log_debug_messages_tmp;
}

sub dbg
{
	__log(LOG_LEVEL_DEBUG, @_) if ($log_debug_messages);
}

sub info
{
	__log(LOG_LEVEL_INFO, @_);
}

sub wrn
{
	__log(LOG_LEVEL_WARNING, @_);
}

sub fail
{
	__log(LOG_LEVEL_FAILURE, @_);
	$finalize_func_ref->() if (defined($finalize_func_ref));
	exit(1);
}

sub __log
{
	my $message_log_level = shift;
	my $message = (@_ <= 1 ? shift // "" : sprintf(shift, @_)) . "\n";

	if ($message_log_level != LOG_LEVEL_DEBUG && $message_log_level != LOG_LEVEL_INFO)
	{
		# 'skip_frames' => 1 removes call to the __log() from the stack trace
		$message .= Devel::StackTrace->new('skip_frames' => 1, 'indent' => 1)->as_string() =~ s/^\t/> /mgr;
	}

	if ($log_timestamps)
	{
		my $log_time_tmp = time();
		if ($log_time != $log_time_tmp)
		{
			my ($sec, $min, $hour, $mday, $mon, $year) = localtime($log_time_tmp);
			$log_time_str = sprintf("%.4d%.2d%.2d:%.2d%.2d%.2d", $year + 1900, $mon + 1, $mday, $hour, $min, $sec);
			$log_time = $log_time_tmp;
		}
	}

	my $log_level_str;
	$log_level_str = 'DBG' if ($message_log_level == LOG_LEVEL_DEBUG);
	$log_level_str = 'INF' if ($message_log_level == LOG_LEVEL_INFO);
	$log_level_str = 'WRN' if ($message_log_level == LOG_LEVEL_WARNING);
	$log_level_str = 'ERR' if ($message_log_level == LOG_LEVEL_FAILURE);

	my $caller = (caller(2))[3] // "";
	if ($caller)
	{
		$caller = " " . ($caller =~ s/^.*:://r) . "()";
	}

	my $message_prefix;
	if ($log_timestamps)
	{
		$message_prefix = sprintf("%7d:%s [%s]%s", $$, $log_time_str, $log_level_str, $caller);
	}
	else
	{
		$message_prefix = sprintf("%7d [%s]%s", $$, $log_level_str, $caller);
	}

	my $output_handle;

	$output_handle = *STDOUT if ($message_log_level == LOG_LEVEL_DEBUG);
	$output_handle = *STDOUT if ($message_log_level == LOG_LEVEL_INFO);
	$output_handle = *STDERR if ($message_log_level == LOG_LEVEL_WARNING);
	$output_handle = *STDERR if ($message_log_level == LOG_LEVEL_FAILURE);

	flock($output_handle, LOCK_EX); # ignore errors, don't fail() to avoid recursion
	print($output_handle $message =~ s/^/$message_prefix /mgr);
	flock($output_handle, LOCK_UN); # ignore errors, don't fail() to avoid recursion
}

1;
