package RSM;

use strict;
use warnings;

use Config::Tiny;
use File::Path qw(make_path);
use base 'Exporter';
use Config '%Config';

use constant SUCCESS => 0;
use constant E_FAIL => -1;	# be careful when changing this, some functions depend on current value

our @EXPORT = qw(
	SUCCESS E_FAIL
	get_rsm_config get_rsm_server_keys get_rsm_server_key get_rsm_server_id get_rsm_local_key
	get_rsm_local_id rsm_targets_prepare rsm_targets_apply get_db_tls_settings
	write_file read_file
	sig_name
	get_sla_api_output_dir
	get_data_export_output_dir
);

use constant RSM_SERVER_KEY_PREFIX => 'server_';
use constant RSM_DEFAULT_CONFIG_FILE => '/opt/zabbix/scripts/rsm.conf';

my ($_TARGET_DIR, $_TMP_DIR, %_TO_DELETE);

sub get_rsm_config
{
	my $config_file = shift;

	$config_file = RSM_DEFAULT_CONFIG_FILE unless ($config_file);

	my $config = Config::Tiny->new;

	$config = Config::Tiny->read($config_file);

	unless (defined($config))
	{
		print STDERR (Config::Tiny->errstr(), "\n");
		exit(-1);
	}

	return $config;
}

sub get_rsm_server_keys
{
	my $config = shift;

	my @keys;

	foreach my $key (sort(keys(%{$config})))
	{
		push(@keys, $key) if ($key =~ /^${\(RSM_SERVER_KEY_PREFIX)}([0-9]+)$/)
	}

	return @keys;
}

sub get_rsm_server_key
{
	my $server_id = shift;

	my (undef, $file, $line) = caller();

	die("Internal error: function get_rsm_server_key() needs a parameter ($file:$line)") unless ($server_id);

	return RSM_SERVER_KEY_PREFIX . $server_id;
}

sub get_rsm_server_id
{
	my $server_id = shift;

	$server_id =~ s/${\RSM_SERVER_KEY_PREFIX}//;

	return $server_id;
}

sub get_rsm_local_key
{
	my $config = shift;

	die("Internal error: no configuration passed to function get_rsm_local_key()") unless ($config);
	die("Configuration error: no \"local\" server defined") unless ($config->{'_'}->{'local'});

	return $config->{'_'}->{'local'};
}

sub get_rsm_local_id
{
	my $config = shift;

	die("Internal error: no configuration passed to function get_rsm_local_key()") unless ($config);
	die("Configuration error: no \"local\" server defined") unless ($config->{'_'}->{'local'});

	my $id = $config->{'_'}->{'local'};

	$id =~ s/^${\(RSM_SERVER_KEY_PREFIX)}//;

	return $id;
}

sub __system
{
	my $cmd = shift;

	my @output = `$cmd 2>&1`;

	if (scalar(@output))
	{
		my $err = $output[0];

		chomp($err);

		$err = $err . ' ...' if (scalar(@output) > 1);

		return $err;
	}

	return undef;
}

sub rsm_targets_apply()
{
	my $strip_components = () = $_TMP_DIR =~ /\//g;

	my $error = __system("tar -cf - $_TMP_DIR 2>/dev/null | tar --ignore-command-error -C $_TARGET_DIR --strip-components=$strip_components -xf -");

	return $error if ($error);

	foreach my $file (keys(%_TO_DELETE))
	{
		my $target_file = $_TARGET_DIR . "/" . $file;

		if (-f $target_file)
		{
			if (!unlink($target_file))
			{
				return __get_file_error($!);
			}
		}
	}

	return __system("rm -rf $_TMP_DIR");
}

sub rsm_targets_prepare($$)
{
	$_TMP_DIR = shift;
	$_TARGET_DIR = shift;

	my $err;

	if (-d $_TMP_DIR)
	{
		$err = __system("rm -rf $_TMP_DIR");

		return $err if ($err);
	}
	else
	{
		$err = __system("rm -rf $_TMP_DIR");

		return $err if ($err);

		make_path($_TMP_DIR, {error => \$err});

		if (@$err)
		{
			return "cannot create temporary directory " . __get_file_error($err);
		}
	}

	if (-f $_TARGET_DIR)
	{
		if (!unlink($_TARGET_DIR))
		{
			return __get_file_error($!);
		}
	}

	make_path($_TARGET_DIR, {error => \$err});

	if (@$err)
	{
		return "cannot create target directory " . __get_file_error($err);
	}

	return undef;
}

sub __get_file_error
{
	my $err = shift;

	my $error_string = "";

	if (ref($err) eq "ARRAY")
	{
		for my $diag (@$err)
		{
			my ($file, $message) = %$diag;
			if ($file eq '')
			{
				$error_string .= "$message. ";
			}
			else
			{
				$error_string .= "$file: $message. ";
			}
		}

		return $error_string;
	}

	return join('', $err, @_);
}

# mapping between configuration file parameters and MySQL driver options
my %mapping = (
	'db_key_file'	=> 'mysql_ssl_client_key',
	'db_cert_file'	=> 'mysql_ssl_client_cert',
	'db_ca_file'	=> 'mysql_ssl_ca_file',
	'db_ca_path'	=> 'mysql_ssl_ca_path',
	'db_cipher'	=> 'mysql_ssl_cipher'
);

# reads database TLS settings from configuration file section
sub get_db_tls_settings($)
{
	my $section = shift;

	my $db_tls_settings = "";

	while (my ($config_param, $mysql_param) = each(%mapping))
	{
		$db_tls_settings .= ";$mysql_param=$section->{$config_param}" if (exists($section->{$config_param}));
	}

	return $db_tls_settings eq "" ? "mysql_ssl=0" : "mysql_ssl=1$db_tls_settings";
}

sub read_file($$;$)
{
	my $file = shift;
	my $buf = shift;
	my $error_buf = shift;

	my $contents = do
	{
		local $/ = undef;

		if (!open my $fh, "<", $file)
		{
			$$error_buf = "$!" if ($error_buf);
			return E_FAIL;
		}

		<$fh>;
	};

	$$buf = $contents;

	return SUCCESS;
}

sub write_file($$;$)
{
	my $file = shift;
	my $text = shift;
	my $error_buf = shift;

	my $OUTFILE;

	if (!open($OUTFILE, '>', $file))
	{
		$$error_buf = "cannot write to file \"$file\": $!" if (defined($error_buf));
		return E_FAIL;
	}

	my $rv = print { $OUTFILE } $text;

	$$error_buf = "cannot write to file \"$file\": $!" if (defined($error_buf));

	close($OUTFILE);

	return E_FAIL unless ($rv);

	return SUCCESS;
}

my @sig_names;
@sig_names[split(' ', $Config{sig_num})] = split(' ', $Config{sig_name});

sub sig_name
{
	return "SIG" . $sig_names[shift];
}

sub get_sla_api_output_dir()
{
	my $config = get_rsm_config();

	return $config->{'sla_api'}{'output_dir'} || die("\"output_dir\" must be specified in \"sla_api\" section of rsm.conf\"");
}

sub get_data_export_output_dir()
{
	my $config = get_rsm_config();

	return $config->{'data_export'}{'output_dir'} || die("\"output_dir\" must be specified in \"data_export\" section of rsm.conf\"");
}

1;
