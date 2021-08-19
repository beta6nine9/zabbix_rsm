#!/usr/bin/env perl

use FindBin;
use lib "$FindBin::RealBin/..";

use strict;
use warnings;

use RSM;
use RSMSLV;
use Data::Dumper;
use Scalar::Util qw(blessed);
use XML::LibXML;

sub main()
{
	parse_cli_opts();

	my $old_config = get_rsm_config(getopt('old-config'));
	my $new_config = get_rsm_config(getopt('new-config'));

	my $server_id = getopt('server-id');

	info('reading data from the old database...');
	my $old_data = get_data($old_config, $server_id);

	info('reading data from the new database...');
	my $new_data = get_data($new_config, $server_id);

	info('merging reports...');
	my $data = merge_data($old_data, $new_data);

	info('generating new reports...');
	generate_reports($new_config, $server_id, $data);

	slv_exit(0);
}

sub get_data($)
{
	my $config    = shift;
	my $server_id = shift;

	my $data = {};

	set_slv_config($config);

	info('connecting to the database...');
	db_connect(get_rsm_server_key($server_id));

	info('getting reports...');
	get_reports($data);

	info('getting history of items that are required for calculating rtt failure ratio...');
	get_rtt_ratios($data);

	info('disconnecting from the database...');
	db_disconnect();

	return $data;
}

sub get_reports($)
{
	my $data = shift;

	my $sql = "select" .
			" hosts.host," .
			"sla_reports.report_xml" .
		" from" .
			" hosts" .
			" inner join sla_reports on sla_reports.hostid=hosts.hostid" .
		" where" .
			" sla_reports.year=? and" .
			" sla_reports.month=?";
	my $params = [getopt('year'), getopt('month')];

	my $rows = db_select($sql, $params);

	foreach my $row (@{$rows})
	{
		my ($tld, $report) = @{$row};

		$data->{$tld} = parse_report($report);
	}
}

sub parse_report($)
{
	my $report = shift;

	my $xml = XML::LibXML->load_xml('string' => $report);

	my $data = {
		'report' => {
			'id'                 => to_str($xml->findnodes('/reportSLA/@id')),
			'generationDateTime' => to_int($xml->findnodes('/reportSLA/@generationDateTime')),
			'reportPeriodFrom'   => to_int($xml->findnodes('/reportSLA/@reportPeriodFrom')),
			'reportPeriodTo'     => to_int($xml->findnodes('/reportSLA/@reportPeriodTo')),
		},
		'DNS' => {
			'serviceAvailability' => {
				'downtimeSLR' => to_int($xml->findnodes('/reportSLA/DNS/serviceAvailability/@downtimeSLR')),
				'value'       => to_int($xml->findnodes('/reportSLA/DNS/serviceAvailability')),
			},
			'nsAvailability' => {
				map {
					to_str($_->findnodes('./@hostname')) . ',' . to_str($_->findnodes('./@ipAddress')) => {
						'hostname'    => to_str($_->findnodes('./@hostname')),
						'ipAddress'   => to_str($_->findnodes('./@ipAddress')),
						'from'        => to_int($_->findnodes('./@from')),
						'to'          => to_int($_->findnodes('./@to')),
						'downtimeSLR' => to_int($_->findnodes('./@downtimeSLR')),
						'value'       => to_int($_),
					}
				} $xml->findnodes('/reportSLA/DNS/nsAvailability')
			},
			'rttUDP' => {
				'rttSLR'        => to_int($xml->findnodes('/reportSLA/DNS/rttUDP/@rttSLR')),
				'percentageSLR' => to_int($xml->findnodes('/reportSLA/DNS/rttUDP/@percentageSLR')),
				'value'         => to_flt($xml->findnodes('/reportSLA/DNS/rttUDP')),
			},
			'rttTCP' => {
				'rttSLR'        => to_int($xml->findnodes('/reportSLA/DNS/rttTCP/@rttSLR')),
				'percentageSLR' => to_int($xml->findnodes('/reportSLA/DNS/rttTCP/@percentageSLR')),
				'value'         => to_flt($xml->findnodes('/reportSLA/DNS/rttTCP')),
			},
		},
		'RDDS' => {
			'serviceAvailability' => {
				'downtimeSLR' => to_int($xml->findnodes('/reportSLA/RDDS/serviceAvailability/@downtimeSLR')),
				'value'       => to_str($xml->findnodes('/reportSLA/RDDS/serviceAvailability')),
			},
			'rtt' => {
				'rttSLR'        => to_int($xml->findnodes('/reportSLA/RDDS/rtt/@rttSLR')),
				'percentageSLR' => to_int($xml->findnodes('/reportSLA/RDDS/rtt/@percentageSLR')),
				'value'         => to_str($xml->findnodes('/reportSLA/RDDS/rtt')),
			},
		},
	};

	return $data;
}

sub xml_value_to_str($)
{
	my $value = shift;

	# extract string value from XML blessed value

	my $package = blessed($value);

	if (defined($package))
	{
		$value = $value->value()      if ($package eq 'XML::LibXML::Attr');
		$value = $value->value()      if ($package eq 'XML::LibXML::Literal');
		$value = $value->to_literal() if ($package eq 'XML::LibXML::Element');
	}

	# make sure that value is plain string rather than some kind of object

	if (defined(blessed($value)))
	{
		fail('unhandled type of variable: ' . Dumper($value));
	}

	return $value;
}

sub to_int($)
{
	my $value = shift;

	$value = xml_value_to_str($value);

	return int($value);
}

sub to_flt($)
{
	my $value = shift;

	$value = xml_value_to_str($value);

	return $value * 1.0;
}

sub to_str($)
{
	my $value = shift;

	$value = xml_value_to_str($value);

	return "${value}";
}

sub get_rtt_ratios($)
{
	my $data = shift;

	my %itemids = get_rtt_itemids();

	foreach my $rsmhost (sort(keys(%{$data})))
	{
		foreach my $service (sort(keys(%{$itemids{$rsmhost}})))
		{
			my $sql = 'select max(clock) from history_uint where itemid=? and clock between ? and ?';
			my $params = [
				$itemids{$rsmhost}{$service}{'performed'},
				$data->{$rsmhost}{'report'}{'reportPeriodFrom'},
				$data->{$rsmhost}{'report'}{'reportPeriodTo'},
			];

			my $clock = db_select_value($sql, $params);

			if (defined($clock))
			{
				$data->{$rsmhost}{'raw_data'}{$service} = {
					'performed' => get_history_value('history_uint', $clock, $itemids{$rsmhost}{$service}{'performed'}),
					'failed'    => get_history_value('history_uint', $clock, $itemids{$rsmhost}{$service}{'failed'}),
					'pfailed'   => get_history_value('history'     , $clock, $itemids{$rsmhost}{$service}{'pfailed'}),
				};
			}
		}
	}
}

sub get_rtt_itemids()
{
	my $sql = 'select' .
			' hosts.host,' .
			'items.itemid,' .
			'items.key_' .
		' from' .
			' hosts' .
			' inner join items on items.hostid=hosts.hostid' .
		' where' .
			' items.key_ like "rsm.slv.%.rtt.%"';

	my $rows = db_select($sql);

	my %itemids;

	foreach my $row (@{$rows})
	{
		my ($rsmhost, $itemid, $key) = @{$row};

		if ($key !~ /^rsm\.slv\.(dns\.tcp|dns\.udp|rdds|rdap)\.rtt\.(performed|failed|pfailed)$/)
		{
			fail("invalid key (host: '$rsmhost', itemid: '$itemid', key: '$key')");
		}

		my ($service, $item) = ($1, $2);

		$itemids{$rsmhost}{$service}{$item} = $itemid;
	}

	return %itemids;
}

sub merge_data($$)
{
	my %old_data = %{+shift};
	my %new_data = %{+shift};

	my %data;

	# pre-fill hash with rsmhosts from both data sets

	@data{keys(%old_data)} = ();
	@data{keys(%new_data)} = ();

	# merge data from both data sets

	foreach my $rsmhost (sort(keys(%data)))
	{
		info("merging report for $rsmhost...");

		# rsmhost does not exist in the new dataset, just copy from the old data set

		if (!exists($new_data{$rsmhost}))
		{
			$data{$rsmhost} = $old_data{$rsmhost};
			next;
		}

		# rsmhost does not exist in the old dataset, just copy from the new data set

		if (!exists($old_data{$rsmhost}))
		{
			$data{$rsmhost} = $new_data{$rsmhost};
			next;
		}

		# rsmhost exists in both data sets, validate 'constants' and merge data sets

		my @paths = (
			'report.id',
			'DNS.serviceAvailability.downtimeSLR',
			'DNS.rttUDP.rttSLR',
			'DNS.rttUDP.percentageSLR',
			'DNS.rttTCP.rttSLR',
			'DNS.rttTCP.percentageSLR',
			'RDDS.serviceAvailability.downtimeSLR',
			'RDDS.rtt.rttSLR',
			'RDDS.rtt.percentageSLR',
		);

		foreach my $path (@paths)
		{
			my $old = $old_data{$rsmhost};
			my $new = $new_data{$rsmhost};

			foreach my $path_component (split(/\./, $path))
			{
				$old = $old->{$path_component};
				$new = $new->{$path_component};
			}

			if ($old ne $new)
			{
				fail("value at '$path' for '$rsmhost' differs (old: '$old', new: '$new')");
			}
		}

		$data{$rsmhost} = {
			'report' => {
				'id'                 => $rsmhost,
				'generationDateTime' => $^T,
				'reportPeriodFrom'   => $old_data{$rsmhost}{'report'}{'reportPeriodFrom'},
				'reportPeriodTo'     => $new_data{$rsmhost}{'report'}{'reportPeriodTo'},
			},
			'DNS' => {
				'serviceAvailability' => {
					'downtimeSLR' => $old_data{$rsmhost}{'DNS'}{'serviceAvailability'}{'downtimeSLR'},
					'value'       => $old_data{$rsmhost}{'DNS'}{'serviceAvailability'}{'value'} + $new_data{$rsmhost}{'DNS'}{'serviceAvailability'}{'value'},
				},
				'nsAvailability' => {
				},
				'rttUDP' => {
					'rttSLR'        => $old_data{$rsmhost}{'DNS'}{'rttUDP'}{'rttSLR'},
					'percentageSLR' => $old_data{$rsmhost}{'DNS'}{'rttUDP'}{'percentageSLR'},
					'value'         => calculate_rtt(\%old_data, \%new_data, $rsmhost, 'dns.udp'),
				},
				'rttTCP' => {
					'rttSLR'        => $old_data{$rsmhost}{'DNS'}{'rttTCP'}{'rttSLR'},
					'percentageSLR' => $old_data{$rsmhost}{'DNS'}{'rttTCP'}{'percentageSLR'},
					'value'         => calculate_rtt(\%old_data, \%new_data, $rsmhost, 'dns.tcp'),
				},
			},
			'RDDS' => {
				'serviceAvailability' => {
					'downtimeSLR' => $old_data{$rsmhost}{'RDDS'}{'serviceAvailability'}{'downtimeSLR'},
					'value'       => undef,
				},
				'rtt' => {
					'rttSLR'        => $old_data{$rsmhost}{'RDDS'}{'rtt'}{'rttSLR'},
					'percentageSLR' => $old_data{$rsmhost}{'RDDS'}{'rtt'}{'percentageSLR'},
					'value'         => undef,
				},
			},
		};

		my %old_nsips = %{$old_data{$rsmhost}{'DNS'}{'nsAvailability'}};
		my %new_nsips = %{$new_data{$rsmhost}{'DNS'}{'nsAvailability'}};
		my $nsips = $data{$rsmhost}{'DNS'}{'nsAvailability'};

		@{$nsips}{keys(%old_nsips), keys(%new_nsips)} = ();

		foreach my $nsip (sort sort_nsip keys(%{$nsips}))
		{
			# ns,ip does not exist in the new dataset, just copy from the old data set

			if (!exists($new_nsips{$nsip}))
			{
				$nsips->{$nsip} = $old_nsips{$nsip};
				next;
			}

			# ns,ip does not exist in the old dataset, just copy from the new data set

			if (!exists($old_nsips{$nsip}))
			{
				$nsips->{$nsip} = $new_nsips{$nsip};
				next;
			}

			# ns,ip exists in both data sets, merge data sets

			$nsips->{$nsip} = {
				'hostname'    => $old_nsips{$nsip}{'hostname'},
				'ipAddress'   => $old_nsips{$nsip}{'ipAddress'},
				'from'        => $old_nsips{$nsip}{'from'},
				'to'          => $new_nsips{$nsip}{'to'},
				'downtimeSLR' => $old_nsips{$nsip}{'downtimeSLR'},
				'value'       => $old_nsips{$nsip}{'value'} + $new_nsips{$nsip}{'value'},
			}
		}

		my %old_rdds = %{$old_data{$rsmhost}{'RDDS'}};
		my %new_rdds = %{$new_data{$rsmhost}{'RDDS'}};
		my $rdds = $data{$rsmhost}{'RDDS'};

		if ($old_rdds{'serviceAvailability'}{'value'} eq 'disabled' && $new_rdds{'serviceAvailability'}{'value'} eq 'disabled')
		{
			$rdds->{'serviceAvailability'}{'value'} = 'disabled';
			$rdds->{'rtt'}{'value'} = 'disabled';
		}
		elsif ($old_rdds{'serviceAvailability'}{'value'} eq 'disabled')
		{
			$rdds->{'serviceAvailability'}{'value'} = $new_rdds{'serviceAvailability'}{'value'};
			$rdds->{'rtt'}{'value'} = $new_rdds{'rtt'}{'value'};
		}
		elsif ($new_rdds{'serviceAvailability'}{'value'} eq 'disabled')
		{
			$rdds->{'serviceAvailability'}{'value'} = $old_rdds{'serviceAvailability'}{'value'};
			$rdds->{'rtt'}{'value'} = $old_rdds{'rtt'}{'value'};
		}
		else
		{
			$rdds->{'serviceAvailability'}{'value'} = $old_rdds{'serviceAvailability'}{'value'} + $new_rdds{'serviceAvailability'}{'value'};
			$rdds->{'rtt'}{'value'} = calculate_rtt(\%old_data, \%new_data, $rsmhost, 'rdds');
		}
	}

	return \%data;
}

sub calculate_rtt($$$$)
{
	my %old_data = %{+shift};
	my %new_data = %{+shift};
	my $rsmhost  = shift;
	my $service  = shift;

	my %old_rtt = %{$old_data{$rsmhost}{'raw_data'}{$service}};
	my %new_rtt = %{$new_data{$rsmhost}{'raw_data'}{$service}};

	my $performed = $old_rtt{'performed'} + $new_rtt{'performed'};
	my $failed    = $old_rtt{'failed'} + $new_rtt{'failed'};
	my $pfailed   = $performed == 0 ? 0 : $failed / $performed;

	return (1 - $pfailed) * 100;
}

sub generate_reports($$$)
{
	my $config    = shift;
	my $server_id = shift;
	my %data      = %{+shift};

	set_slv_config($config);

	info('connecting to the database...');
	db_connect(get_rsm_server_key($server_id));

	foreach my $rsmhost (sort(keys(%data)))
	{
		info("creating report for $rsmhost...");
		my $report = create_report($data{$rsmhost});

		info("saving report for $rsmhost...");
		save_report($rsmhost, $report);
	}

	info('disconnecting from the database...');
	db_disconnect();
}

sub create_report($)
{
	my %data = %{+shift};

	my $xml = XML::LibXML::Document->new('1.0');

	# reportSLA

	my $xml_report = $xml->createElement('reportSLA');
	$xml_report->setAttribute('id', $data{'report'}{'id'});
	$xml_report->setAttribute('generationDateTime', $data{'report'}{'generationDateTime'});
	$xml_report->setAttribute('reportPeriodFrom', $data{'report'}{'reportPeriodFrom'});
	$xml_report->setAttribute('reportPeriodTo', $data{'report'}{'reportPeriodTo'});
	$xml->setDocumentElement($xml_report);

	# reportSLA.DNS

	my $xml_dns = $xml->createElement('DNS');
	$xml_report->appendChild($xml_dns);

	# reportSLA.DNS.serviceAvailability

	my $xml_dns_avail = $xml->createElement('serviceAvailability');
	$xml_dns_avail->setAttribute('downtimeSLR', $data{'DNS'}{'serviceAvailability'}{'downtimeSLR'});
	$xml_dns_avail->appendText($data{'DNS'}{'serviceAvailability'}{'value'});
	$xml_dns->appendChild($xml_dns_avail);

	# reportSLA.DNS.nsAvailability

	foreach my $nsip (sort sort_nsip keys(%{$data{'DNS'}{'nsAvailability'}}))
	{
		my %ns_data = %{$data{'DNS'}{'nsAvailability'}{$nsip}};
		my $xml_ns = $xml->createElement('nsAvailability');
		$xml_ns->setAttribute('hostname', $ns_data{'hostname'});
		$xml_ns->setAttribute('ipAddress', $ns_data{'ipAddress'});
		$xml_ns->setAttribute('from', $ns_data{'from'});
		$xml_ns->setAttribute('to', $ns_data{'to'});
		$xml_ns->setAttribute('downtimeSLR', $ns_data{'downtimeSLR'});
		$xml_ns->appendText($ns_data{'value'});
		$xml_dns->appendChild($xml_ns);
	}
	#my $xml_ns = $xml_dns->appendTextChild('nsAvailability', '');

	# reportSLA.DNS.rttUDP

	my $xml_dns_udp_rtt = $xml->createElement('rttUDP');
	$xml_dns_udp_rtt->setAttribute('rttSLR', $data{'DNS'}{'rttUDP'}{'rttSLR'});
	$xml_dns_udp_rtt->setAttribute('percentageSLR', $data{'DNS'}{'rttUDP'}{'percentageSLR'});
	$xml_dns_udp_rtt->appendText($data{'DNS'}{'rttUDP'}{'value'});
	$xml_dns->appendChild($xml_dns_udp_rtt);

	# reportSLA.DNS.rttTCP

	my $xml_dns_tcp_rtt = $xml->createElement('rttTCP');
	$xml_dns_tcp_rtt->setAttribute('rttSLR', $data{'DNS'}{'rttTCP'}{'rttSLR'});
	$xml_dns_tcp_rtt->setAttribute('percentageSLR', $data{'DNS'}{'rttTCP'}{'percentageSLR'});
	$xml_dns_tcp_rtt->appendText($data{'DNS'}{'rttTCP'}{'value'});
	$xml_dns->appendChild($xml_dns_tcp_rtt);

	# reportSLA.RDDS

	my $xml_rdds = $xml->createElement('RDDS');
	$xml_report->appendChild($xml_rdds);

	# reportSLA.RDDS.serviceAvailability

	my $xml_rdds_avail = $xml->createElement('serviceAvailability');
	$xml_rdds_avail->setAttribute('downtimeSLR', $data{'RDDS'}{'serviceAvailability'}{'downtimeSLR'});
	$xml_rdds_avail->appendText($data{'RDDS'}{'serviceAvailability'}{'value'});
	$xml_rdds->appendChild($xml_rdds_avail);

	# reportSLA.RDDS.rtt

	my $xml_rdds_rtt = $xml->createElement('rtt');
	$xml_rdds_rtt->setAttribute('rttSLR', $data{'RDDS'}{'rtt'}{'rttSLR'});
	$xml_rdds_rtt->setAttribute('percentageSLR', $data{'RDDS'}{'rtt'}{'percentageSLR'});
	$xml_rdds_rtt->appendText($data{'RDDS'}{'rtt'}{'value'});
	$xml_rdds->appendChild($xml_rdds_rtt);


	return $xml->toString(1);
}

sub sort_nsip($$)
{
	my ($a_ns, $a_ip) = split(/,/, shift);
	my ($b_ns, $b_ip) = split(/,/, shift);

	# sort by hostname

	if ($a_ns ne $b_ns)
	{
		return $a_ns cmp $b_ns;
	}

	# sort by ip version (put ipv4 before ipv6)

	my $a_is_ipv4 = ($a_ip =~ /^\d+\.\d+\.\d+\.\d+$/) ? 1 : 0;
	my $b_is_ipv4 = ($b_ip =~ /^\d+\.\d+\.\d+\.\d+$/) ? 1 : 0;

	if ($a_is_ipv4 != $b_is_ipv4)
	{
		return $b_is_ipv4 - $a_is_ipv4;
	}

	# sort by ip address

	return $a_ip cmp $b_ip;
}

sub save_report($$)
{
	my $rsmhost = shift;
	my $report  = shift;

	my $rsmhostid = get_rsmhostid($rsmhost);

	my $sql = "update sla_reports set report_xml=?,report_json=? where hostid=? and year=? and month=?";
	my $params = [$report, '', $rsmhostid, getopt('year'), getopt('month')];

	db_exec($sql, $params);
}

sub get_history_value($$$)
{
	my $table  = shift;
	my $clock  = shift;
	my $itemid = shift;

	my $sql = "select value from $table where itemid=? and clock=?";
	my $params = [$itemid, $clock];

	return db_select_value($sql, $params);
}

sub get_rsmhostid($)
{
	my $rsmhost = shift;

	my $sql = 'select hostid from hosts where host=?';
	my $params = [$rsmhost];

	return db_select_value($sql, $params);
}

sub parse_cli_opts($$)
{
	setopt('stats');
	setopt('nolog');

	parse_opts('old-config=s', 'new-config=s', 'server-id=i', 'year=i', 'month=i');

	fail('missing option: --old-config') if (!opt('old-config'));
	fail('missing option: --new-config') if (!opt('new-config'));
	fail('missing option: --server-id')  if (!opt('server-id'));
	fail('missing option: --year')       if (!opt('year'));
	fail('missing option: --month')      if (!opt('month'));

	fail('invalid value for option: --old-config') if (!getopt('old-config'));
	fail('invalid value for option: --new-config') if (!getopt('new-config'));
}

main();

__END__

=head1 NAME

merge-monthly-reports.pl - merge monthly reports from old and new databases, store them in the new database.

=head1 SYNOPSIS

merge-monthly-reports.pl --old-config <filename> --new-config <filename> --server-id <server_id> --year <year> --month <month>

=head1 OPTIONS

=over 8

=item B<--old-config> filename

Full path to the old rsm.conf file.

=item B<--new-config> filename

Full path to the new rsm.conf file.

=item B<--server-id> server_id

ID of Zabbix server.

=item B<--year> year

Year when the report was generated.

=item B<--month> month

Month when the report was generated.

=item B<--help>

Print a brief help message and exit.

=back

=cut
