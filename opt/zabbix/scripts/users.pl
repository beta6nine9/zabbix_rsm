#!/usr/bin/env perl

use FindBin;
use lib $FindBin::RealBin;

use strict;
use warnings;

use Zabbix;
use RSM;
use RSMSLV;

use constant USER_ROLE_READ_ONLY   => 100;	# User type "Read-only user"
use constant USER_ROLE_POWER_USER  => 110;	# User type "Power user"
use constant USER_ROLE_SUPER_ADMIN => 3;	# User type "Zabbix Super Admin"

# NB! Keep these values in sync with DB schema!
use constant READ_ONLY_USER_GROUPID => 100;	# User group "Read-only user"
use constant POWER_USER_GROUPID     => 110;	# User group "Power user"
use constant SUPER_ADMIN_GROUPID    => 7;	# User group "Zabbix administrators"

use constant USER_TYPES =>
{
	'read-only-user' =>
	{
		'roleid'   => USER_ROLE_READ_ONLY,
		'usrgrpid' => READ_ONLY_USER_GROUPID,
		'url'      => 'zabbix.php?action=rsm.rollingweekstatus',
	},
	'power-user' =>
	{
		'roleid'   => USER_ROLE_POWER_USER,
		'usrgrpid' => POWER_USER_GROUPID,
		'url'      => 'zabbix.php?action=rsm.rollingweekstatus',
	},
	'admin' =>
	{
		'roleid'   => USER_ROLE_SUPER_ADMIN,
		'usrgrpid' => SUPER_ADMIN_GROUPID,
		'url'      => 'zabbix.php?action=dashboard.view',
	}
};

sub __get_userid($$$$$);

parse_opts('add', 'delete', 'modify', 'user=s', 'type=s', 'password=s', 'firstname=s', 'lastname=s', 'server-id=i');

__validate_opts();

my $config = get_rsm_config();

my @server_keys = get_rsm_server_keys($config);

my $modified = 0;
foreach my $server_key (@server_keys)
{
	my $server_id = get_rsm_server_id($server_key);

	if (opt('server-id'))
	{
		next if (getopt('server-id') != $server_id);

		unsetopt('server-id');
	}

	my $section = $config->{$server_key};

	print("Processing $server_key\n");

	my $zabbix = Zabbix->new({
		'url'      => $section->{'za_url'},
		'user'     => $section->{'za_user'},
		'password' => $section->{'za_password'},
		'debug'    => getopt('debug'),
	});

	if (opt('add'))
	{
		my $options = {
			'username' => getopt('user'),
			'roleid'   => USER_TYPES->{getopt('type')}->{'roleid'},
			'passwd'   => getopt('password'),
			'name'     => getopt('firstname'),
			'surname'  => getopt('lastname'),
			'url'      => USER_TYPES->{getopt('type')}->{'url'},
			'usrgrps'  => [{'usrgrpid' => USER_TYPES->{getopt('type')}->{'usrgrpid'}}],
		};

		my $result = $zabbix->create('user', $options);

		if ($result->{'error'})
		{
			if ($result->{'error'}->{'data'} =~ /Session terminated/)
			{
				print("Session terminated. Please re-run the same command again");
				print(" with option \"--server-id $server_id\"")  if ($modified == 1);
				print(".\n");
			}
			else
			{
				print("Error: cannot add user \"", getopt('user'), "\". ", $result->{'error'}->{'data'}, "\n");

				if ($modified == 1)
				{
					print("Please fix the issue and re-run the same command with \"--server-id $server_id\"\n");
				}
			}

			exit(-1);
		}

		print("  user with userid ", $result->{'userids'}->[0], " added\n");
	}
	elsif (opt('modify'))
	{
		my $userid = __get_userid($server_key, $zabbix, $server_id, getopt('user'), $modified);

		my $result = $zabbix->update('user', {'userid' => $userid, 'passwd' => getopt('password')});

		if ($result->{'error'})
		{
			print("Error: cannot change password of user \"", getopt('user'), "\". ", $result->{'error'}->{'data'}, "\n");

			if ($modified == 1)
			{
				print("Please fix the issue and re-run the same command with \"--server-id $server_id\"\n");
			}

			exit(-1);
		}

		print("  user modified\n");
	}
	else
	{
		my $userid = __get_userid($server_key, $zabbix, $server_id, getopt('user'), $modified);

		my $result = $zabbix->remove('user', [$userid]);

		if ($result->{'error'})
		{
			print("Error: cannot delete user \"", getopt('user'), "\". ", $result->{'error'}->{'data'}, "\n");

			if ($modified == 1)
			{
				print("Please fix the issue and re-run the same command with \"--server-id $server_id\"\n");
			}

			exit(-1);
		}

		print("  user deleted\n");
	}

	$modified = 1;
}

sub __get_userid($$$$$)
{
	my $server_key = shift;
	my $zabbix     = shift;
	my $server_id  = shift;
	my $username   = shift;
	my $modified   = shift;

	my $options = {'output' => ['userid'], 'filter' => {'username' => $username}};

	my $result = $zabbix->get('user', $options);

	if (ref($result) ne "HASH")
	{
		print("Error: cannot get user \"$username\": reply is not a HASH reference. Please run with \"--debug\"");
		exit(-1);
	}

	if ($result->{'error'})
	{
		if ($result->{'error'}{'data'} =~ /Session terminated/)
		{
			print("Session terminated. Please re-run the same command again");
			print(" with option \"--server-id $server_id\"") if ($modified == 1);
			print(".\n");
		}
		else
		{
			print("Error: cannot get user \"$username\". ", $result->{'error'}{'data'}, "\n");

			if ($modified == 1)
			{
				print("Please fix the issue and re-run the same command with \"--server-id $server_id\"\n");
			}
		}

		exit(-1);
	}

	my $userid = $result->{'userid'};

	if (!$userid)
	{
		print("Error: user \"$username\" not found on $server_key\n");

		if ($modified == 1)
		{
			print("Please fix the issue and re-run the same command with \"--server-id $server_id\"\n");
		}

		exit(-1);
	}

	return $userid;
}

sub __opts_fail
{
	print("Invalid parameters:\n");
	print(join("\n", @_), "\n");
	exit(-1);
}

sub __validate_opts
{
	my @errors;

	my $actions_specified = 0;

	foreach my $opt ('add', 'delete', 'modify')
	{
		$actions_specified++ if (opt($opt));
	}

	if ($actions_specified == 0)
	{
		push(@errors, "\tone of \"--add\", \"--delete\" or \"--modify\" must be specified");
	}
	elsif ($actions_specified != 1)
	{
		push(@errors, "\tonly one of \"--add\", \"--delete\" or \"--modify\" must be specified");
	}

	__opts_fail(@errors) if (0 != scalar(@errors));

	push(@errors, "\tuser name must be specified with \"--user\"") if (!opt('user'));

	if (opt('add'))
	{
		foreach my $opt ('type', 'password', 'firstname', 'lastname')
		{
			push(@errors, "\toption \"--$opt\" must be specified") if (!opt($opt));
		}

		if (opt('type'))
		{
			my $type = getopt('type');

			push(@errors, "\tunknown user type \"$type\", it must be one of: read-only-user, power-user, admin")
				if ($type ne 'read-only-user' && $type ne 'power-user' && $type ne 'admin');
		}
	}
	elsif (opt('modify'))
	{
		foreach my $opt ('type', 'firstname', 'lastname')
		{
			push(@errors, "\toption \"--$opt\" is currently not supported with \"--modify\"") if (opt($opt));
		}

		push(@errors, "\tnew password must be specified with \"--password\"") if (!opt('password'));
	}

	__opts_fail(@errors) if (0 != scalar(@errors));
}

__END__

=head1 NAME

users.pl - manage users in Zabbix

=head1 SYNOPSIS

users.pl --add|--delete|--modify --user <user> [--type <read-only-user|power-user|admin>] [--password <password>] [--firstname <firstname>] [--lastname <lastname>] [--server-id id] [--debug] [--help]

=head1 OPTIONS

=head2 REQUIRED OPTIONS

=over 8

=item B<--add>

Add a new user.

=item B<--delete>

Delete existing user.

=item B<--modify>

Change password of existing user. This option requires --password.

=item B<--user> user

Specify username of the user account.

=head2 REQUIRED OPTIONS FOR ADDING A USER OR CHANGING PASSWORD

=item B<--password> password

Specify user password.

=head2 REQUIRED OPTIONS FOR ADDING A USER

=item B<--type> type

Specify user type, accepted values: read-only-user, power-user or admin.

=item B<--firstname> firstname

Specify first name of a user.

=item B<--lastname> lastname

Specify last name of a user.

=head2 OTHER OPTIONS

=item B<--server-id> id

Specify id of the server to continue the operation from. This option is useful when action was successful on part of the servers.

=item B<--debug>

Run the script in debug mode. This means printing more information.

=item B<--help>

Print a brief help message and exit.

=back

=head1 DESCRIPTION

B<This program> will manage users in Zabbix.

=head1 EXAMPLES

./users.pl --add john --type read-only-user --password secret --firstname John --lastname Doe

This will add a new Read-only user with specified details.

=cut
