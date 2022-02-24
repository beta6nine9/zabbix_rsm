package Tools;

use strict;
use warnings;

use FindBin;
use lib "$FindBin::RealBin/../lib/perl";

use Net::DNS::NameserverCustom;

use IO::Socket::INET;
use threads;

use Exporter qw(import);
our @EXPORT = qw(
	read_json_file
	write_file
	start_dns_server
	start_tcp_server
	inf err
);

use JSON::XS;

my $_instance_name;

sub read_json_file($)
{
	my $file = shift;

	my $contents = do
	{
		local $/ = undef;

		die("$!") unless (open my $fh, "<", $file);

		<$fh>;
	};

	return decode_json($contents);
}

sub write_file($$)
{
	my $filename = shift;
	my $text     = shift;

	my $fh;

	open($fh, '>', $filename) or fail("cannot open file '$filename': $!");
	print({$fh} $text)        or fail("cannot write to '$filename': $!");
	close($fh)                or fail("cannot close file '$filename': $!");
}

sub start_dns_server($$$$$$)
{
	my $name          = shift;
	my $port          = shift;
	my $pid_file      = shift;
	my $verbose       = shift;
	my $reply_handler = shift;
	my $custom_opts   = shift;

	$_instance_name = $name;

	my $pid = fork();

	if ($pid < 0)
	{
		die("cannot fork(): $!");
	}

	if ($pid != 0)
	{
		# parent
		inf("writing pid $pid to $pid_file");
		write_file($pid_file, $pid);

		exit();
	}

	# child
	my %opts = (
		LocalAddr    => '127.0.0.1',
		LocalPort    => $port,
		ReplyHandler => $reply_handler,
		Verbose      => $verbose,
	);

	foreach (keys(%{$custom_opts}))
	{
		$opts{$_} = $custom_opts->{$_};
	}

	my $ns = Net::DNS::NameserverCustom->new(%opts) || die("cannot create nameserver object\n");

	inf("started");

	$ns->main_loop();
}

sub start_tcp_server($$$$)
{
	my $name          = shift;
	my $port          = shift;
	my $pid_file      = shift;
	my $reply_handler = shift;

	$_instance_name = $name;

	my $pid = fork();

	if ($pid < 0)
	{
		die("cannot fork(): $!");
	}

	if ($pid != 0)
	{
		# parent
		inf("writing pid $pid to $pid_file");
		write_file($pid_file, $pid);

		exit();
	}

	# child
	my $socket = IO::Socket::INET->new(
		LocalHost   => '127.0.0.1',
		LocalPort   =>  $port,
		Proto       => 'tcp',
		Listen      =>  5,
		Reuse       =>  1
	) or die("cannot create socket: $!");

	inf("started on port $port");

	while (1)
	{
		my $client_socket  = $socket->accept();
		my $client_address = $client_socket->peerhost;
		my $client_port    = $client_socket->peerport;

		inf("client $client_address:$client_port connected");

		threads->create($reply_handler, $client_socket);
	}

	$socket->close();
}

sub inf(@)
{
	printf("%s INF: %s\n", $_instance_name, join(',', @_));
}

sub err(@)
{
	printf("%s ERR: %s\n", $_instance_name, join(',', @_));
}

1;
