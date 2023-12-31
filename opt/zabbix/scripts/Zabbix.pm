package Zabbix;
# by DotNeft with UTF-8 support

use strict;
use warnings;

use JSON::XS;
use Encode;
use Carp;
use LWP::UserAgent;
use LWP::Protocol::https;

use Data::Dumper;
$Data::Dumper::Sortkeys = 1;

require Exporter;
our @ISA = qw(Exporter);

@Zabbix::EXPORT = qw( new ua get remove update event_ack create exist massadd massremove );

BEGIN {
    $Zabbix::VERSION = '1.0';
    $Zabbix::DEBUG   = 0 unless (defined $Zabbix::DEBUG);
}

use constant true => 1;
use constant false => 0;

sub to_ascii($);
sub to_utf8($);

sub get_authid();
sub set_authid($);
sub delete_authid();

use constant _LOGIN_TIMEOUT => 5;

use constant _DEFAULT_REQUEST_TIMEOUT => 60;  # seconds, passed to LWP::UserAgent
use constant _DEFAULT_REQUEST_ATTEMPTS => 10;

my ($_OPTIONS, $AUTH_FILE);

sub new($$) {
    my $class;
    ($class, $_OPTIONS) = @_;

    my $ua = LWP::UserAgent->new();

#    $ua->ssl_opts(verify_hostname => 0);

    $ua->agent("Net::Zabbix");

    my $req = HTTP::Request->new(POST => $_OPTIONS->{'url'}."/api_jsonrpc.php");

    $req->authorization_basic($_OPTIONS->{user}, $_OPTIONS->{password}) if ($_OPTIONS->{auth_basic});

    $req->content_type('application/json-rpc');

    my $domain = $_OPTIONS->{'url'};
    $domain =~ s,^https*\://(.+)/*$,$1,;
    $domain =~ s,/,-,g;
    $AUTH_FILE = '/tmp/'.$domain.'.tmp';

    print("AUTH_FILE: $AUTH_FILE\n") if ($_OPTIONS->{'debug'});

    if (my $authid = get_authid()) {

	print("Checking previous authid: $authid\n") if ($_OPTIONS->{'debug'});

	my $request = encode_json({
		jsonrpc => "2.0",
		method  => "user.checkAuthentication",
		params  => {
			sessionid => $authid,
		},
		id => 1,
	});

	print("REQUEST:\n", Dumper($request), "\n") if ($_OPTIONS->{'debug'});

	$req->content($request);

	$ua->timeout(_LOGIN_TIMEOUT);

	my $res = $ua->request($req);

	croak "cannot connect to Zabbix: " . $res->status_line unless ($res->is_success);

	my $result;

	eval { $result = decode_json($res->content) };
	croak "Zabbix API returned invalid JSON: " . $@ if $@;

	print("REPLY:\n", Dumper($result), "\n") if ($_OPTIONS->{'debug'});

	if (!defined($result->{'error'}))
	{
		print("Using previous authid: $authid\n") if ($_OPTIONS->{'debug'});

		$ua->timeout($_OPTIONS->{request_timeout} // _DEFAULT_REQUEST_TIMEOUT);

		my $self = {
			UserAgent => $ua,
			request   => $req,
			count     => 0,
			auth      => $authid,
			error => undef,
		};

		bless($self, $class);

		return bless($self, $class) if (defined($self->api_version()));
        }
    }

    print("no or invalid authid in the file, logging in...\n") if ($_OPTIONS->{'debug'});

    my $request = encode_json( {
	    jsonrpc => "2.0",
       method => "user.login",
        params => {
            user => $_OPTIONS->{user},
            password => $_OPTIONS->{password},
        },
        id => 1,
    });

    my $result;

    my $login_attempts = 2;

    while ($login_attempts--)
    {
	print("REQUEST:\n", Dumper($request), "\n") if ($_OPTIONS->{'debug'});

	$req->content($request);

	$ua->timeout(_LOGIN_TIMEOUT);

	my $res = $ua->request($req);

	$ua->timeout($_OPTIONS->{request_timeout} // _DEFAULT_REQUEST_TIMEOUT);

	croak "cannot connect to Zabbix: " . $res->status_line unless ($res->is_success);

	eval { $result = decode_json($res->content) };
	croak "Zabbix API returned invalid JSON: " . $@ if $@;

	print("REPLY:\n", Dumper($result), "\n") if ($_OPTIONS->{'debug'});

	if (defined($result->{'error'}))
	{
	    last unless ($result->{'error'}->{'data'} =~ /Session terminated/);
	}
   }

    croak "cannot connect to Zabbix: " . $result->{'error'}->{'message'} . ' ' . $result->{'error'}->{'data'} if (defined($result->{'error'}));

    my $auth = $result->{'result'};

    set_authid($auth);

    return bless {
        UserAgent => $ua,
        request   => $req,
        count     => 1,
        auth      => $auth,
	error => undef,
    }, $class;
}

sub get_authid() {
    my $authid;

    if (-e $AUTH_FILE) {

        open(TMP, '<', $AUTH_FILE);

        my @lines = <TMP>;

        close(TMP);

        $authid = shift (@lines);
    }

    return $authid;
}

sub set_authid($) {
    my $authid = shift;

    open(TMP, '>', $AUTH_FILE) || print("cannot open file \"$AUTH_FILE\": $!\n");

    print TMP $authid;

    close(TMP);
}

sub delete_authid() {
    unlink($AUTH_FILE);
}

sub ua {
    return shift->{'UserAgent'};
}

sub req {
    return shift->{'request'};
}

sub auth {
    return shift->{'auth'};
}

sub next_id {
    return ++shift->{'count'};
}

sub last_error {
    return shift->{'error'};
}

sub set_last_error {
    my ($self, $error) = @_;

    shift->{'error'} = $error if defined $error;
    return if defined $error;

    shift->{'error'} = undef;
}


sub api_version {
    my ($self) = @_;

    return $self->__execute('apiinfo', 'version', {});
}

sub create {
    my ($self, $class, $params) = @_;

    return $self->__execute($class, 'create', $params);
}

sub get {
    my ($self, $class, $params) = @_;

    return $self->__fetch($class, 'get', $params);
}

sub remove {
    my ($self, $class, $params) = @_;

    return $self->__execute($class, 'delete', $params);
}

sub update {
    my ($self, $class, $params) = @_;

    return $self->__execute($class, 'update', $params);
}

sub objects {
    my ($self, $class, $params) = @_;

    return $self->__fetch($class, 'getobjects', $params);
}

my $objectids =
{
	'trigger' => 'triggerid',
	'item' => 'itemid',
	'host' => 'hostid',
	'template' => 'templateid',
	'hostgroup' => 'groupid',
};

sub exist {
    my ($self, $class, $params) = @_;

    $params->{'output'} = [$objectids->{$class}];

    return $self->__fetch_id($class, 'get', $params);
}

sub is_readable {
    my ($self, $class, $params) = @_;

    return $self->__fetch_bool($class, 'isreadable', $params);
}

sub is_writeable {
    my ($self, $class, $params) = @_;

    return $self->__fetch_bool($class, 'iswriteable', $params);
}


sub massadd {
    my ($self, $class, $params) = @_;

    return $self->__execute($class, 'massAdd', $params);
}

sub massremove {
    my ($self, $class, $params) = @_;

    return $self->__execute($class, 'massremove', $params);
}

sub massupdate {
    my ($self, $class, $params) = @_;

    return $self->__execute($class, 'massupdate', $params);
}

sub event_ack {
    my ($self, $params) = @_;

    return $self->__execute('event', 'acknowledge', $params);
}

#####################################

sub conf_export {
    my ($self, $params) = @_;

    return $self->__fetch('configuration', 'export', $params);
}

sub conf_import {
    my ($self, $params) = @_;

    die "Is not implemented yet!\n";
}

#####################################

sub replace_interfaces {
    my ($self, $params) = @_;

    return $self->__execute('hostinterface', 'replacehostinterfaces', $params);
}

sub execute_script {
    my ($self, $params) = @_;

    die "Is not implemented yet!\n";
}

sub trigger_dep_add {
    my ($self, $params) = @_;

    return $self->__execute('trigger', 'adddependencies', $params);
}

sub trigger_dep_delete {
    my ($self, $params) = @_;

    return $self->__execute('trigger', 'deleteDependencies', $params);
}

sub user_media_add {
    my ($self, $params) = @_;

    return $self->__execute('user', 'addMedia', $params);
}

sub user_media_delete {
    my ($self, $params) = @_;

    return $self->__execute('user', 'deleteMedia', $params);
}

sub user_media_update {
    my ($self, $params) = @_;

    return $self->__execute('user', 'updateMedia', $params);
}

sub user_profile_update {
    my ($self, $params) = @_;

    return $self->__execute('user', 'updateProfile', $params);
}

sub macro_global_create {
    my ($self, $params) = @_;

    return $self->__execute('usermacro', 'createGlobal', $params);
}

sub macro_global_delete {
    my ($self, $params) = @_;

    return $self->__execute('usermacro', 'deleteGlobal', $params);
}

sub macro_global_update {
    my ($self, $params) = @_;

    return $self->__execute('usermacro', 'updateGlobal', $params);
}

#####################################

sub to_ascii($) {
    my $json = shift;

    if (is_hash($json)) {
        foreach my $key (keys %{$json}) {
            ${$json}{$key} = to_ascii(${$json}{$key});
        }
    }
    elsif(is_array($json)) {
        for(my $i=0; $i<@{$json}; $i++) {
            ${$json}[$i] = to_ascii(${$json}[$i]);
        }
    }
    else {
        $json = decode_utf8($json) if utf8::valid($json);
    }

    return $json;
}


sub to_utf8($) {
    my $json = shift;

    if (is_hash($json)) {
        foreach my $key (keys %{$json}) {
            ${$json}{$key} = to_utf8(${$json}{$key});
        }
    }
    elsif(is_array($json)) {
        for(my $i=0; $i<@{$json}; $i++) {
            ${$json}[$i] = to_utf8(${$json}[$i]);
        }
    }
    else {
        $json = encode_utf8($json);
    }

    return $json;
}

sub is_array($) {
    my ($ref) = @_;

    return 0 unless ref $ref;

    if ( $ref =~ /^ARRAY/ ) { return 1; } else { return 0; }
}

sub is_hash($) {
    my $ref = shift;

    return 0 unless ref $ref;

    if ( $ref =~ /^HASH/ ) { return 1; } else { return 0; }
}

sub __execute($$$) {
    my ($self, $class, $method, $params) = @_;

    my $result = $self->__send_request($class, $method, $params);

    if (defined($result->{'error'})) {
	$self->set_last_error($result->{'error'});
	return $result;
    }

    $self->set_last_error();

    return $result->{'result'};
}


sub __fetch($$$) {
    my ($self, $class, $method, $params) = @_;

    my $result = to_utf8($self->__send_request($class, $method, $params));

    if (defined($result->{'error'})) {
	$self->set_last_error($result->{'error'});
	return $result;
    }

    $self->set_last_error();

    return ${$result->{'result'}}[0] if (is_array($result->{'result'}) and (scalar @{$result->{'result'}} == 1));
    return {} if (is_array($result->{'result'}) and (scalar @{$result->{'result'}} == 0));

    return $result->{'result'};
}

sub __fetch_bool($$$) {
    my ($self, $class, $method, $params) = @_;

    my $result = $self->__send_request($class, $method, $params);

    if (defined($result->{'error'})) {
	$self->set_last_error($result->{'error'});
	return;
    }

    if (@{$result->{'result'}} > 1) {
	$self->set_last_error('more than one entry found when checking '.$class.':'."\nREQUEST:\n".Dumper($params)."\nREPLY:\n".Dumper($result->{'result'})."\n");
	return false;
    }

    $self->set_last_error();

    return false if (@{$result->{'result'}} == 0);

    return true;
}

sub __fetch_id($$$) {
    my ($self, $class, $method, $params) = @_;

    my $result = $self->__send_request($class, $method, $params);

    if (defined($result->{'error'})) {
	$self->set_last_error($result->{'error'});
	return $result;
    }

    if (@{$result->{'result'}} > 1) {
	$self->set_last_error('more than one entry found when checking '.$class.':'."\nREQUEST:\n".Dumper($params)."\nREPLY:\n".Dumper($result->{'result'})."\n");
	return false;
    }

    $self->set_last_error();

    return 0 if (@{$result->{'result'}} == 0);

    return $result->{'result'}->[0]->{$objectids->{$class}};
}

sub __send_request {
    my ($self, $class, $method, $params) = @_;

    my $req = $self->req;

    my $request = {
                jsonrpc => "2.0",
                method => "$class.$method",
                params => $params,
                id => $self->next_id
            };

    if ( $method ne 'version' ) {
	$request->{'auth'} = $self->auth
    }

    $req->content(to_ascii(encode_json($request)));

    print("REQUEST:\n", Dumper($req), "\n") if ($_OPTIONS->{'debug'});

    my $res;
    my $attempts = $_OPTIONS->{request_attempts} // _DEFAULT_REQUEST_ATTEMPTS;
    my $sleep = 1;

    while ($attempts-- > 0) {
	$res = $self->ua->request($req);

	last if ($res->is_success);

	sleep($sleep);

	$sleep *= 1.3;
	$sleep = 3 if ($sleep > 3);
    }

    die("Can't connect to Zabbix: " . $res->status_line) unless ($res->is_success);

    my $result = decode_json($res->content);

    if ($_OPTIONS->{'debug'}) {
        if (defined($result->{'error'})) {
            print("REQUEST FAILED! ");
        }

        print("REPLY:\n", Dumper($result));
    }

    if (defined($result->{'error'}) && $result->{'error'}{'data'} =~ /Session terminated/) {
        delete_authid();
    }

    return $result;
}

1;
