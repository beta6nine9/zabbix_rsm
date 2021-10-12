package Database;

use strict;
use warnings;

use Exporter qw(import);
our @EXPORT = qw(
	db_connect
	db_disconnect
	db_select
	db_select_col
	db_select_row
	db_select_value
	db_exec
	db_begin
	db_commit
	db_rollback
	db_create_dump
	db_compare_dumps
);

use DBI;
use Data::Dumper;
use Scalar::Util qw(looks_like_number);

use Configuration;
use Output;

use constant ZABBIX_SERVER_CONF_FILE => '/etc/zabbix/zabbix_server.conf';

my $db_handle;

sub db_connect()
{
	if (defined($db_handle))
	{
		fail("already connected to the database");
	}

	my $db_host = get_config("zabbix_server", "db_host");
	my $db_user = get_config("zabbix_server", "db_username");
	my $db_pswd = get_config("zabbix_server", "db_password");
	my $db_tls_settings = "mysql_ssl=0;";

	my $data_source = "DBI:mysql:";

	$data_source .= "host=$db_host;";

	$data_source .= "mysql_connect_timeout=30;";
	$data_source .= "mysql_write_timeout=30;";
	$data_source .= "mysql_read_timeout=30;";

	$data_source .= $db_tls_settings;

	my $connect_opts = {
		'PrintError'           => 0,
		'HandleError'          => \&__handle_db_error,
		'mysql_auto_reconnect' => 1,
	};

	info("connecting to database");
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

	# avoid destroying handle in child processes, see
	# https://metacpan.org/pod/DBI#AutoInactiveDestroy
	$db_handle->{'AutoInactiveDestroy'} = 1;
}

sub db_disconnect()
{
	if (!defined($db_handle))
	{
		return;
	}

	my @active_handles = ();

	foreach my $handle (@{$db_handle->{'ChildHandles'}})
	{
		if (defined($handle) && $handle->{'Type'} eq 'st' && $handle->{'Active'})
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

	info("disconnecting from database");
	$db_handle->disconnect() || wrn($db_handle->errstr);

	undef($db_handle);
}

sub db_select($;$)
{
	my $query = shift;
	my $bind_values = shift; # optional; reference to an array

	my $sth = $db_handle->prepare($query);

	if (defined($bind_values))
	{
		dbg("[$query] " . join(',', @{$bind_values})) if (log_debug_messages());

		$sth->execute(@{$bind_values});
	}
	else
	{
		dbg("[$query]") if (log_debug_messages());

		$sth->execute();
	}

	my $rows = $sth->fetchall_arrayref();

	if (log_debug_messages())
	{
		if (scalar(@{$rows}) == 1)
		{
			dbg(join(',', map($_ // 'UNDEF', @{$rows->[0]})));
		}
		else
		{
			dbg(scalar(@{$rows}) . " rows");
		}
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

	fail("query did not return any row: [$query] ".join(',', @{$bind_values})) if (scalar(@{$rows}) == 0);
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
		dbg("[$query] " . join(',', @{$bind_values})) if (log_debug_messages());

		$sth->execute(@{$bind_values});
	}
	else
	{
		dbg("[$query]") if (log_debug_messages());

		$sth->execute();
	}

	return $sth->{mysql_insertid};
}

sub db_begin()
{
	$db_handle->begin_work();
}

sub db_commit()
{
	$db_handle->commit();
}

sub db_rollback()
{
	$db_handle->rollback();
}

sub db_create_dump()
{
	my $dump = {};

	foreach my $table (@{db_select_col('show tables')})
	{
		next if ($table eq 'auditlog');
		next if ($table eq 'config');
		next if ($table eq 'history');
		next if ($table eq 'history_log');
		next if ($table eq 'history_str');
		next if ($table eq 'history_text');
		next if ($table eq 'history_uint');
		next if ($table eq 'housekeeper');
		next if ($table eq 'ids');
		next if ($table eq 'lastvalue');
		next if ($table eq 'lastvalue_str');
		next if ($table eq 'profiles');
		next if ($table eq 'sessions');
		next if ($table eq 'trends');
		next if ($table eq 'trends_uint');

		$dump->{$table} = {};

		my $fields = __get_fields_for_dump($table);
		my $rows = db_select("select $fields from $table");

		foreach my $row (@{$rows})
		{
			$row = [map(defined($_) && !looks_like_number($_) ? "'" . ($_ =~ s/'/''/gr) . "'" : $_, @{$row})];
			$row = [map(defined($_) ? $_ : 'NULL', @{$row})];
			$row = join(',', @{$row});

			$dump->{$table}{$row} = undef;
		}
	}

	return $dump;
}

sub db_compare_dumps($$)
{
	my $dump_1 = shift;
	my $dump_2 = shift;

	foreach my $table (keys(%{$dump_1}))
	{
		foreach my $row (keys(%{$dump_1->{$table}}))
		{
			if (exists($dump_2->{$table}{$row}))
			{
				delete($dump_1->{$table}{$row});
				delete($dump_2->{$table}{$row});
			}
		}
	}

	foreach my $table (sort(keys(%{$dump_1})))
	{
		if (%{$dump_1->{$table}} || %{$dump_2->{$table}})
		{
			my $fields = __get_fields_for_dump($table);

			print('#' x 160 . "\n");
			print("# $table\n");
			print("# $fields\n");
			print('#' x 160 . "\n");
			print("\n");

			if (%{$dump_1->{$table}})
			{
				print('*' x 76 . ' before ' . '*' x 76 . "\n");
				foreach my $row (sort(keys(%{$dump_1->{$table}})))
				{
					db_select_row("select $row"); # test syntax
					print($row . "\n");
				}
				print("\n");
			}

			if (%{$dump_2->{$table}})
			{
				print('*' x 76 . ' after ' . '*' x 77 . "\n");
				foreach my $row (sort(keys(%{$dump_2->{$table}})))
				{
					db_select_row("select $row"); # test syntax
					print($row . "\n");
				}
				print("\n");
			}
		}
	}
}

sub __get_fields_for_dump($)
{
	my $table = shift;

	my $ignore = {
		'hosts'          => ['disable_until', 'error', 'errors_from', 'lastaccess'],
		'item_discovery' => ['lastcheck'],
		'triggers'       => ['lastchange', 'error'],
	};

	my $ignored_fields = join(',', map("'$_'", @{$ignore->{$table}}));
	my $rows;

	if ($ignored_fields)
	{
		$rows = db_select("show fields in $table where field not in ($ignored_fields)");
	}
	else
	{
		$rows = db_select("show fields in $table");
	}

	return join(',', map($_->[0], @{$rows}));
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
		my $params = $handle->{'ParamValues'};
		my @params = @{$params}{sort {$a <=> $b} keys(%{$params})};
		my $params_str = join(',', map($_ // 'undef', @params));

		push(@message_parts, "(params: [$params_str])");
	}

	if (defined($handle->{'ParamArrays'}) && %{$handle->{'ParamArrays'}})
	{
		my $params = join(',', values(%{$handle->{'ParamArrays'}}));
		push(@message_parts, "(params 2: [$params])");
	}

	return join(' ', @message_parts);
}

1;
