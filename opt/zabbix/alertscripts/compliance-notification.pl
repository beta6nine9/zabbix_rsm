#!/usr/bin/env perl

use strict;
use warnings;

use Getopt::Long qw(GetOptionsFromArray);
use Pod::Usage;
use DBI;
use Devel::StackTrace;
use Data::Dumper;

################################################################################
# main
################################################################################

sub main()
{
	parse_opts();

	if (!opt("event-id"))
	{
		usage("--event-id is missing", 1);
	}

	if (opt("debug"))
	{
		log_debug_messages(1);
		dbg("command line: %s %s", $0, join(" ", map(index($_, " ") == -1 ? $_ : "'$_'", @ARGV)));
	}

	db_connect();

	print Dumper db_select("select * from events where eventid=?", [getopt('event-id')]);

	db_disconnect();
}

################################################################################
# output
################################################################################

use constant LOG_LEVEL_DEBUG   => 1;
use constant LOG_LEVEL_INFO    => 2;
use constant LOG_LEVEL_WARNING => 3;
use constant LOG_LEVEL_FAILURE => 4;

my $log_time_str;
my $log_time = 0;

my $log_debug_messages = 0;

$SIG{__WARN__} = sub
{
	local *__ANON__ = "perl-warn";
	__log(LOG_LEVEL_WARNING, $_[0] =~ s/(\r?\n)+$//r);
};

$SIG{__DIE__} = sub
{
	local *__ANON__ = "perl-die";
	__log(LOG_LEVEL_FAILURE, $_[0] =~ s/(\r?\n)+$//r);
	exit(255); # Perl's default exit code on die()
};

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
	exit(1);
}

sub log_debug_messages($)
{
	$log_debug_messages = shift;
}

sub __log
{
	my $message_log_level = shift;
	my $message = (@_ == 1 ? shift : sprintf(shift, @_)) . "\n";

	if ($message_log_level != LOG_LEVEL_DEBUG && $message_log_level != LOG_LEVEL_INFO)
	{
		# 'skip_frames' => 1 removes call to the __log() from the stack trace
		$message .= Devel::StackTrace->new('skip_frames' => 1, 'indent' => 1)->as_string() =~ s/^\t/> /mgr;
	}

	my $log_time_tmp = time();
	if ($log_time != $log_time_tmp)
	{
		my ($sec, $min, $hour, $mday, $mon, $year) = localtime($log_time_tmp);
		$log_time_str = sprintf("%.4d%.2d%.2d:%.2d%.2d%.2d", $year + 1900, $mon + 1, $mday, $hour, $min, $sec);
		$log_time = $log_time_tmp;
	}

	my $log_level_str;
	$log_level_str = "DBG" if ($message_log_level == LOG_LEVEL_DEBUG);
	$log_level_str = "INF" if ($message_log_level == LOG_LEVEL_INFO);
	$log_level_str = "WRN" if ($message_log_level == LOG_LEVEL_WARNING);
	$log_level_str = "ERR" if ($message_log_level == LOG_LEVEL_FAILURE);

	my $caller = (caller(2))[3] // "";
	if ($caller)
	{
		$caller = " " . ($caller =~ s/^.*:://r) . "()";
	}

	my $message_prefix = sprintf("%6d:%s [%s]%s", $$, $log_time_str, $log_level_str, $caller);

	my $output_handle;
	$output_handle = *STDOUT if ($message_log_level == LOG_LEVEL_DEBUG);
	$output_handle = *STDOUT if ($message_log_level == LOG_LEVEL_INFO);
	$output_handle = *STDERR if ($message_log_level == LOG_LEVEL_WARNING);
	$output_handle = *STDERR if ($message_log_level == LOG_LEVEL_FAILURE);

	print($output_handle $message =~ s/^/$message_prefix /mgr);
}

################################################################################
# command-line options
################################################################################

my %OPTS;

sub parse_opts()
{
	my $rv = GetOptionsFromArray([@ARGV], \%OPTS, "event-id=n", "dry-run!", "debug!", "help!");

	if (!$rv || $OPTS{"help"})
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

################################################################################
# database
################################################################################

use constant ZABBIX_SERVER_CONF_FILE => "/etc/zabbix/zabbix_server.conf";

my $db_handle;

sub db_connect()
{
	if (defined($db_handle))
	{
		fail("already connected to the database");
	}

	my ($db_host, $db_name, $db_user, $db_pswd, $db_tls_settings) = __get_db_params();

	my $data_source = "DBI:mysql:";

	$data_source .= "host=$db_host;";
	$data_source .= "database=$db_name;";

	$data_source .= "mysql_connect_timeout=30;";
	$data_source .= "mysql_write_timeout=30;";
	$data_source .= "mysql_read_timeout=30;";

	$data_source .= $db_tls_settings;

	my $connect_opts = {
		'PrintError'           => 0,
		'HandleError'          => \&__handle_db_error,
		'mysql_auto_reconnect' => 1,
	};

	$db_handle = DBI->connect($data_source, $db_user, $db_pswd, $connect_opts);

	# verify that established database connection uses TLS if there was any hint that it is required in the config
	unless ($db_tls_settings eq "mysql_ssl=0;")
	{
		my $rows = db_select("show status like 'Ssl_cipher'");

		fail("established connection is not secure") if ($rows->[0][1] eq "");

		dbg("established connection uses \"" . $rows->[0][1] . "\" cipher");
	}
	else
	{
		dbg("established connection is unencrypted");
	}

	# improve performance of selects, see
	# http://search.cpan.org/~capttofu/DBD-mysql-4.028/lib/DBD/mysql.pm
	# for details
	$db_handle->{'mysql_use_result'} = 1;
}

sub db_disconnect()
{
	if (!defined($db_handle))
	{
		wrn("not connected to the database");
		return;
	}

	my @active_handles = ();

	foreach my $handle (@{$db_handle->{'ChildHandles'}})
	{
		if (defined($handle) && $handle->{'Type'} eq "st" && $handle->{'Active'})
		{
			push(@active_handles, $handle);
		}
	}

	if (@active_handles)
	{
		wrn("called while having " . scalar(@active_handles) . " active statement handle(s)");

		foreach my $handle (@active_handles)
		{
			wrn(__generate_db_error($handle, "active statement"));
			$handle->finish();
		}
	}

	$db_handle->disconnect() or wrn($db_handle->errstr);
	undef($db_handle);
}

sub db_select($;$)
{
	my $query = shift;
	my $bind_values = shift; # optional; reference to an array

	my $sth = $db_handle->prepare($query);

	if (defined($bind_values))
	{
		dbg("[$query] " . join(",", @{$bind_values}));

		$sth->execute(@{$bind_values});
	}
	else
	{
		dbg("[$query]");

		$sth->execute();
	}

	my $rows = $sth->fetchall_arrayref();

	if (scalar(@{$rows}) == 1)
	{
		dbg(join(",", map($_ // "UNDEF", @{$rows->[0]})));
	}
	else
	{
		dbg(scalar(@{$rows}) . " rows");
	}

	return $rows;
}

sub db_select_col($;$)
{
	my $query = shift;
	my $bind_values = shift; # optional; reference to an array

	my $rows = db_select($query, $bind_values);

	fail("query returned more than one column") if (scalar(@{$rows}) > 0 && scalar(@{$rows->[0]}) > 1);

	return [map($_->[0], @{$rows})];
}

sub db_select_row($;$)
{
	my $query = shift;
	my $bind_values = shift; # optional; reference to an array

	my $rows = db_select($query, $bind_values);

	fail("query did not return any row") if (scalar(@{$rows}) == 0);
	fail("query returned more than one row") if (scalar(@{$rows}) > 1);

	return $rows->[0];
}

sub db_select_value($;$)
{
	my $query = shift;
	my $bind_values = shift; # optional; reference to an array

	my $row = db_select_row($query, $bind_values);

	fail("query returned more than one value") if (scalar(@{$row}) > 1);

	return $row->[0];
}

sub db_exec($;$)
{
	my $query = shift;
	my $bind_values = shift; # optional; reference to an array

	my $sth = $db_handle->prepare($query);

	if (defined($bind_values))
	{
		dbg("[$query] " . join(",", @{$bind_values}));

		$sth->execute(@{$bind_values});
	}
	else
	{
		dbg("[$query]");

		$sth->execute();
	}

	return $sth->{mysql_insertid};
}

sub __get_db_params()
{
	my $db_host = "localhost";
	my $db_name = undef;
	my $db_user = "";
	my $db_pswd = "";

	my $db_tls_key_file  = undef;
	my $db_tls_cert_file = undef;
	my $db_tls_ca_file   = undef;
	my $db_tls_ca_path   = undef;
	my $db_tls_cipher    = undef;

	open(my $fh, "<", ZABBIX_SERVER_CONF_FILE) or fail("cannot open ${\ZABBIX_SERVER_CONF_FILE}: $!");

	while (<$fh>)
	{
		if (/^(DB.*)=(.*)$/)
		{
			my $key   = $1;
			my $value = $2;

			$db_host = $value if ($key eq "DBHost");
			$db_name = $value if ($key eq "DBName");
			$db_user = $value if ($key eq "DBUser");
			$db_pswd = $value if ($key eq "DBPassword");

			$db_tls_key_file  = $value if ($key eq "DBKeyFile");
			$db_tls_cert_file = $value if ($key eq "DBCertFile");
			$db_tls_ca_file   = $value if ($key eq "DBCAFile");
			$db_tls_ca_path   = $value if ($key eq "DBCAPath");
			$db_tls_cipher    = $value if ($key eq "DBCipher");
		}
	}

	close($fh) or fail("cannot close ${\ZABBIX_SERVER_CONF_FILE}: $!");

	my $db_tls_settings = "";

	$db_tls_settings .= "mysql_ssl_client_key="  . $db_tls_key_file  . ";" if (defined($db_tls_key_file));
	$db_tls_settings .= "mysql_ssl_client_cert=" . $db_tls_cert_file . ";" if (defined($db_tls_cert_file));
	$db_tls_settings .= "mysql_ssl_ca_file="     . $db_tls_ca_file   . ";" if (defined($db_tls_ca_file));
	$db_tls_settings .= "mysql_ssl_ca_path="     . $db_tls_ca_path   . ";" if (defined($db_tls_ca_path));
	$db_tls_settings .= "mysql_ssl_cipher="      . $db_tls_cipher    . ";" if (defined($db_tls_cipher));

	if ($db_tls_settings)
	{
		$db_tls_settings = "mysql_ssl=1;" . $db_tls_settings;
	}
	else
	{
		$db_tls_settings = "mysql_ssl=0;";
	}

	return $db_host, $db_name, $db_user, $db_pswd, $db_tls_settings;
}

sub __handle_db_error($$$)
{
	my $message = shift;
	my $handle  = shift;

	fail(__generate_db_error($handle, undef));
}

sub __generate_db_error($$)
{
	my $handle  = shift;
	my $message = shift // $handle->errstr;

	my @message_parts = ();

	push(@message_parts, "database error:");

	push(@message_parts, $message);

	if (defined($handle->{'Statement'}))
	{
		push(@message_parts, "(query: [$handle->{'Statement'}])");
	}

	if (defined($handle->{'ParamValues'}) && %{$handle->{'ParamValues'}})
	{
		my $params = join(",", values(%{$handle->{'ParamValues'}}));
		push(@message_parts, "(params: [$params])");
	}

	if (defined($handle->{'ParamArrays'}) && %{$handle->{'ParamArrays'}})
	{
		my $params = join(",", values(%{$handle->{'ParamArrays'}}));
		push(@message_parts, "(params 2: [$params])");
	}

	return join(" ", @message_parts);
}

################################################################################
# end of script
################################################################################

main();

__END__

=head1 NAME

compliance-notification.pl - calls script.py.

=head1 SYNOPSIS

compliance-notification.pl --event-id <event-id> [--dry-run] [--debug] [--help]

=head1 OPTIONS

=over 8

=item B<--event-id> int

Specify year. If year is specified, month also has to be specified.

=item B<--dry-run>

Print data to the screen, do not change anything in the system.

=item B<--debug>

Run the script in debug mode. This means printing more information.

=item B<--help>

Print a brief help message and exit.

=back

=cut
