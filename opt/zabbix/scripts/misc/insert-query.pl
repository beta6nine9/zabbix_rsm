#!/usr/bin/perl

use lib '/opt/zabbix/scripts';

use strict;
use warnings;

use RSM;
use RSMSLV;
use Data::Dumper;
use List::Util qw(max);

sub main()
{
	setopt('stats');
	setopt('nolog');
	parse_opts('table=s', 'c!', 'oneline!');

	if (!opt('table'))
	{
		fail("missing --table");
	}

	if (opt('c') && opt('oneline'))
	{
		fail("cannot use both --c and --oneline at the same time");
	}

	set_slv_config(get_rsm_config());

	my $table = getopt('table');

	my $sql = "
		select
			\@is_text := data_type in ('varchar', 'text') as is_text,
			\@quote := if(\@is_text, '''', '') as quote,
			column_name,
			case isnull(column_default)
				when 1 then if(is_nullable='YES', 'NULL', if(\@is_text, '''''', '0'))
				when 0 then concat(\@quote, column_default, \@quote)
			end as default_value
		from
			information_schema.columns
		where
			table_schema = ? and
			table_name = ?
		order by
			ordinal_position asc
	";

	db_connect();
	my $rows = db_select($sql, [db_select_value("select database()"), $table]);
	db_disconnect();

	my $query;

	if (opt('c'))
	{
		my @fields = map(sprintf("%s=%s", $_->[2], $_->[3]), @{$rows});
		$query = "\"insert into $table set\"\n\t\" " . join(",\"\n\t\"", @fields) . "\"\n";
	}
	elsif (opt('oneline'))
	{
		my @fields = map(sprintf("%s=%s", $_->[2], $_->[3]), @{$rows});
		$query = "insert into $table set " . join(",", @fields) . "\n";
	}
	else
	{
		my $max_field_name_len = 0;
		map { $max_field_name_len = max($max_field_name_len, length($_->[2])); } @{$rows};

		my @fields = map(sprintf("\t%-${max_field_name_len}s = %s", $_->[2], $_->[3]), @{$rows});
		$query = "insert into $table set\n" . join(",\n", @fields) . "\n";
	}

	print $query;
}

main();

__END__

=head1 NAME

insert-into.pl - generates "insert" sql query.

=head1 SYNOPSIS

insert-query.pl --table <table> [--c] [--oneline]

=head1 OPTIONS

=over 8

=item B<--table> string

Specify name of the table.

=item B<--c>

Format output as a C string.

=item B<--oneline>

Format output as a single line.

=back

=cut
