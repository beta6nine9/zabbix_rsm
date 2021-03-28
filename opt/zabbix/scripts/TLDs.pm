package TLDs;

use strict;
use warnings;

use RSM;
use RSMSLV;
use Zabbix;
use TLD_constants qw(:general :templates :groups :api :config :tls :items);
use Data::Dumper;
use base 'Exporter';

use constant RSMHOST_DNS_NS_LOG_ACTION_CREATE  => 0;
use constant RSMHOST_DNS_NS_LOG_ACTION_ENABLE  => 1;
use constant RSMHOST_DNS_NS_LOG_ACTION_DISABLE => 2;

our @EXPORT = qw(zbx_connect check_api_error get_proxies_list
		get_api_error zbx_need_relogin
		CONFIG_HISTORY_TEMPLATEID
		DNS_TEST_TEMPLATEID
		DNS_STATUS_TEMPLATEID
		DNSSEC_STATUS_TEMPLATEID
		RDDS_TEST_TEMPLATEID
		RDDS_STATUS_TEMPLATEID
		RDAP_TEST_TEMPLATEID
		RDAP_STATUS_TEMPLATEID
		PROBE_STATUS_TEMPLATEID
		PROXY_HEALTH_TEMPLATEID
		create_probe_template create_host create_group create_template
		create_item create_trigger create_macro update_root_server_macros
		create_passive_proxy probe_exists get_host_group get_template get_template_id get_probe get_host
		remove_templates remove_hosts remove_hostgroups remove_probes remove_items
		disable_host disable_hosts link_template_to_host
		update_items_status enable_items disable_items set_service_items_status
		disable_triggers
		rename_host rename_proxy rename_template rename_hostgroup
		macro_value get_global_macro_value get_host_macro
		set_proxy_status
		get_items_like get_host_items set_tld_type get_triggers_by_items
		add_dependency
		update_rsmhost_config_times
		pfail);

our ($zabbix, $result);

sub zbx_connect($$$;$)
{
	my $url      = shift;
	my $user     = shift;
	my $password = shift;
	my $debug    = shift;

	$zabbix = Zabbix->new({'url' => $url, user => $user, password => $password, 'debug' => $debug});

	if (defined($zabbix->{'error'}) && $zabbix->{'error'} ne '')
	{
		return $zabbix->{'error'};
	}

	return true;
}

sub check_api_error($)
{
	my $result = shift;

	if (ref($result) eq 'HASH' && (defined($result->{'error'}) || defined($result->{'code'})))
	{
		return true;
	}

	return false;
}

sub get_api_error($)
{
	my $result = shift;

	if (check_api_error($result) eq true)
	{
		return $result->{'error'}{'data'};
	}

	return;
}

sub zbx_need_relogin($)
{
	my $result = shift;

	if (check_api_error($result) eq true && $result->{'error'}{'data'} =~ /Session terminated/)
	{
		return true;
	}

	return false;
}

sub get_proxies_list
{
	my $proxies_list;

	$proxies_list = $zabbix->get('proxy', {'output' => ['proxyid', 'host', 'status'], 'selectInterface' => ['ip'], 'preservekeys' => 1});

	return $proxies_list;
}

sub probe_exists($)
{
	my $name = shift;

	my $result = $zabbix->get('proxy', {'output' => ['proxyid'], 'filter' => {'host' => $name}, 'preservekeys' => 1});

	return keys(%{$result}) ? true : false;
}

sub get_probe($$)
{
	my $probe_name  = shift;
	my $selectHosts = shift;

	my $options = {
		'output' => ['proxyid', 'host'],
		'filter' => {'host' => $probe_name},
		'selectInterface' => ['interfaceid']
	};

	if (defined($selectHosts) && $selectHosts eq true)
	{
		$options->{'selectHosts'} = ['hostid', 'name', 'host'];
	}

	my $result = $zabbix->get('proxy', $options);

	return $result;
}

sub get_host_group($$$;$)
{
	my $group_name  = shift;
	my $selectHosts = shift;
	my $selectType  = shift;
	my $fields      = shift // [];

	my $options = {
		'output' => 'extend',
		'filter' => {'name' => $group_name}
	};

	if (defined($selectHosts) && $selectHosts eq true)
	{
		$options->{'selectHosts'} = ['hostid', 'host', 'name', @{$fields}];
	}

	my $result = $zabbix->get('hostgroup', $options);

	if ($selectType eq true && scalar(@{$result->{'hosts'}}) != 0)
	{
		foreach my $tld (@{$result->{'hosts'}})
		{
			my $hostid = $tld->{'hostid'};
			$options = {
				'output' => 'extend',
				'filter' => {'hostid' => $hostid},
				'selectGroups' => ['name']
			};
			my $result2 = $zabbix->get('host', $options);
			foreach my $group (@{$result2->{'groups'}})
			{
				my $name = $group->{'name'};
				if ($name =~ /^[a-z]+TLD$/)
				{
					$tld->{'type'} = $group->{'name'};
					last;
				}
			}
			unless (defined($tld->{'type'}))
			{
				die("cannot get TLD type of \"", $tld->{'host'}, "\"");
			}
		}
	}

	return $result;
}

sub get_template($$$)
{
	my $template_name = shift;
	my $selectMacros  = shift;
	my $selectHosts   = shift;

	my $options = {
		'output' => ['templateid', 'host'],
		'filter' => {'host' => $template_name}
	};

	if (defined($selectMacros) && $selectMacros eq true)
	{
		$options->{'selectMacros'} = 'extend';
	}

	if (defined($selectHosts) && $selectHosts eq true)
	{
		$options->{'selectHosts'} = ['hostid', 'host'];
	}

	my $result = $zabbix->get('template', $options);

	return $result;
}

my %_saved_template_ids = ();

sub get_template_id($)
{
	my $template_name = shift;

	if (!exists($_saved_template_ids{$template_name}))
	{
		my $result = get_template($template_name, false, false);
		pfail("'" . $template_name . "' does not exist") unless ($result->{'templateid'});
		$_saved_template_ids{$template_name} = $result->{'templateid'};
	}

	return $_saved_template_ids{$template_name};
}

sub CONFIG_HISTORY_TEMPLATEID
{
	return get_template_id(TEMPLATE_CONFIG_HISTORY);
}

sub DNS_TEST_TEMPLATEID
{
	return get_template_id(TEMPLATE_DNS_TEST);
}

sub DNS_STATUS_TEMPLATEID
{
	return get_template_id(TEMPLATE_DNS_STATUS);
}

sub DNSSEC_STATUS_TEMPLATEID
{
	return get_template_id(TEMPLATE_DNSSEC_STATUS);
}

sub RDDS_TEST_TEMPLATEID
{
	return get_template_id(TEMPLATE_RDDS_TEST);
}

sub RDDS_STATUS_TEMPLATEID
{
	return get_template_id(TEMPLATE_RDDS_STATUS);
}

sub RDAP_TEST_TEMPLATEID
{
	return get_template_id(TEMPLATE_RDAP_TEST);
}

sub RDAP_STATUS_TEMPLATEID
{
	return get_template_id(TEMPLATE_RDAP_STATUS);
}

sub PROBE_STATUS_TEMPLATEID
{
	return get_template_id(TEMPLATE_PROBE_STATUS);
}

sub PROXY_HEALTH_TEMPLATEID
{
	return get_template_id(TEMPLATE_PROXY_HEALTH);
}

sub remove_templates($)
{
	my @templateids = shift;

	unless (scalar(@templateids))
	{
		return;
	}

	my $result = $zabbix->remove('template', @templateids);

	return $result;
}

sub remove_hosts($)
{
	my @hosts = shift;

	unless (scalar(@hosts))
	{
		return;
	}

	my $result = $zabbix->remove('host', @hosts);

	return $result;
}

sub disable_hosts($)
{
	my @hosts = shift;

	unless (scalar(@hosts))
	{
		return;
	}

	my $result = $zabbix->massupdate('host', {'hosts' => @hosts, 'status' => HOST_STATUS_NOT_MONITORED});

	return $result;
}

# This function returns reference to array of hash references with all host's
# template ids. This reference can be used for subsequent calls as third parameter.
sub link_template_to_host($$;$)
{
	my $hostid         = shift;
	my $new_templateid = shift;
	my $all_templates  = shift;

	my $options;
	my $result;

	if (!defined($all_templates))
	{
		$options = {
			'output'                => [],
			'filter'                => {'hostid' => $hostid},
			'selectParentTemplates' => ['templateid']
		};

		$result = $zabbix->get('host', $options);

		return undef unless (%{$result});

		$all_templates = $result->{'parentTemplates'};
	}

	push @{$all_templates}, {'templateid' => $new_templateid};

	$options = {
		'hostid'    => $hostid,
		'templates' => $all_templates
	};

	$result = $zabbix->update('host', $options);

	return keys(%{$result}) ? $all_templates : undef;
}

sub remove_hostgroups($)
{
	my @hostgroupids = shift;

	unless (scalar(@hostgroupids))
	{
		return;
	}

	my $result = $zabbix->remove('hostgroup', @hostgroupids);

	return $result;
}

sub remove_probes($)
{
	my @probes = shift;

	unless (scalar(@probes))
	{
		return;
	}

	my $result = $zabbix->remove('proxy', @probes);

	return $result;
}

sub update_items_status($$)
{
	my $itemids = shift;
	my $status  = shift;

	unless (scalar(@{$itemids}))
	{
		return;
	}

	my $result;

	foreach my $itemid (@{$itemids})
	{
		my $rsmhost_dns_ns_log_action;

		my $item = $zabbix->get('item', {'itemids' => [$itemid], 'output' => ['key_', 'status']});
		if ($item->{'key_'} =~ '^rsm\.slv\.dns\.ns\.downtime\[.*,.*\]$' && $item->{'status'} != $status)
		{
			$rsmhost_dns_ns_log_action = RSMHOST_DNS_NS_LOG_ACTION_ENABLE  if ($status == 0);
			$rsmhost_dns_ns_log_action = RSMHOST_DNS_NS_LOG_ACTION_DISABLE if ($status == 1);
		}

		$result->{$itemid} = $zabbix->update('item', {'itemid' => $itemid, 'status' => $status});

		if (defined($rsmhost_dns_ns_log_action))
		{
			rsmhost_dns_ns_log($itemid, $rsmhost_dns_ns_log_action);
		}
	}

	return $result;
}

sub enable_items($)
{
	my $itemids = shift;

	return update_items_status($itemids, ITEM_STATUS_ACTIVE);
}

sub disable_items($)
{
	my $itemids = shift;

	return update_items_status($itemids, ITEM_STATUS_DISABLED);
}

sub set_service_items_status($$$)
{
	my $host_items     = shift; # list of {key, itemid, status} hashes, result of get_host_items($hostid)
	my $template_id    = shift; # template id
	my $service_status = shift; # 1 for "enable", 0 for "disable"

	my $item_status = $service_status ? ITEM_STATUS_ACTIVE : ITEM_STATUS_DISABLED;

	set_templated_items_status($host_items, $template_id, $item_status);
}

my %template_items_cache;

sub set_templated_items_status($$$)
{
	my $host_items  = shift; # list of {key, itemid, status} hashes, result of get_host_items($hostid)
	my $template_id = shift; # template id
	my $status      = shift; # ITEM_STATUS_ACTIVE or ITEM_STATUS_DISABLED

	if (!exists($template_items_cache{$template_id}))
	{
		my $items = get_items_like($template_id, undef, 1);
		$template_items_cache{$template_id} = { map { $_->{'key_'} => undef } values(%{$items}) };
	}

	my $template_items = $template_items_cache{$template_id};

	my $itemids = [];

	foreach my $item (@{$host_items})
	{
		if ($item->{'status'} != $status && exists($template_items->{$item->{'key'}}))
		{
			push(@{$itemids}, $item->{'itemid'});
		}
	}

	if (@{$itemids})
	{
		update_items_status($itemids, $status);
	}
}

sub disable_triggers($)
{
	my $triggers = shift;

	unless (scalar(@{$triggers}))
	{
		return;
	}

	my $result;

	foreach my $triggerid (@{$triggers})
	{
		$result->{$triggerid} = $zabbix->update('trigger', {'triggerid' => $triggerid, 'status' => TRIGGER_STATUS_DISABLED});
	}

	return $result;
}

sub remove_items($)
{
	my $items = shift;

	unless (scalar(@{$items}))
	{
		return;
	}

	my $result = $zabbix->remove('item', $items);

	return $result;
}

sub disable_host($)
{
	my $hostid = shift;

	unless (defined($hostid))
	{
		return;
	}

	my $result = $zabbix->update('host', {'hostid' => $hostid, 'status' => HOST_STATUS_NOT_MONITORED});

	return $result;
}

sub rename_template($$)
{
	my $templateid    = shift;
	my $template_name = shift;

	unless (defined($templateid) && defined($template_name))
	{
		return;
	}

	my $result = $zabbix->update('template', {'templateid' => $templateid, 'host' => $template_name});

	return $result;
}

sub rename_host($$)
{
	my $hostid    = shift;
	my $host_name = shift;

	unless (defined($hostid) && defined($host_name))
	{
		return;
	}

	my $result = $zabbix->update('host', {'hostid' => $hostid, 'host' => $host_name});

	return $result;
}

sub rename_hostgroup($$)
{
	my $groupid    = shift;
	my $group_name = shift;

	unless (defined($groupid) && defined($group_name))
	{
		return;
	}

	my $result = $zabbix->update('hostgroup', {'groupid' => $groupid, 'name' => $group_name});

	return $result;
}

sub macro_value($$)
{
	my $hostmacroid = shift;
	my $value       = shift;

	if (!defined($hostmacroid) || !defined($value))
	{
		return;
	}

	my $result = $zabbix->update('usermacro', {'hostmacroid' => $hostmacroid, 'value' => $value});

	return $result;
}

sub set_proxy_status($$)
{
	my $proxyid = shift;
	my $status  = shift;

	if (!defined($proxyid) || !defined($status))
	{
		return;
	}

	if ($status != HOST_STATUS_PROXY_ACTIVE && $status != HOST_STATUS_PROXY_PASSIVE)
	{
		return;
	}

	my $result = $zabbix->update('proxy', {'proxyid' => $proxyid, 'status' => $status});

	return $result;
}

sub rename_proxy($$)
{
	my $proxyid    = shift;
	my $proxy_name = shift;

	if (!defined($proxyid) || !defined($proxy_name))
	{
		return;
	}

	my $result = $zabbix->update('proxy', {'proxyid' => $proxyid, 'host' => $proxy_name});

	return $result;
}

sub get_host($$)
{
	my $host_name    = shift;
	my $selectGroups = shift;

	my $options = {
		'output' => ['hostid', 'host', 'status'],
		'filter' => {'host' => $host_name}
	};

	if (defined($selectGroups) && $selectGroups eq true)
	{
		$options->{'selectGroups'} = 'extend';
	}

	my $result = $zabbix->get('host', $options);

	return $result;
}

sub get_global_macro_value($)
{
	my $macro_name = shift;

	my $options = {
		'globalmacro' => true,
		'output' => 'extend',
		'filter' => {'macro' => $macro_name}
	};

	my $result = $zabbix->get('usermacro', $options);

	return $result->{'value'}; # may be undef
}

sub update_root_server_macros(;$)
{
	my $root_servers = shift;

	if ($root_servers)
	{
		my ($macro_value_v4, $macro_value_v6)  = split(';', $root_servers);

		create_macro('{$RSM.IP4.ROOTSERVERS1}', $macro_value_v4, undef, 1); # global, force
		create_macro('{$RSM.IP6.ROOTSERVERS1}', $macro_value_v6, undef, 1); # global, force
	}
}

sub create_host
{
	my $options = shift;

	my $hostid;

	unless ($hostid = $zabbix->exist('host', {'filter' => {'host' => $options->{'host'}}}))
	{
		my $result = $zabbix->create('host', $options);

		return $result->{'hostids'}[0];
	}

	$options->{'hostid'} = $hostid;
	delete($options->{'interfaces'});
	$result = $zabbix->update('host', $options);

	$hostid = $result->{'hostids'}[0] ? $result->{'hostids'}[0] : $options->{'hostid'};

	return $hostid;
}

sub create_group
{
	my $name = shift;

	my $groupid = $zabbix->exist('hostgroup', {'filter' => {'name' => $name}});

	if (check_api_error($groupid) eq true)
	{
		return $groupid;
	}

	unless ($groupid)
	{
		my $result = $zabbix->create('hostgroup', {'name' => $name});
		$groupid = $result->{'groupids'}[0];
	}

	return $groupid;
}

sub create_template
{
	my $name               = shift;
	my $linked_templateids = shift; # optional

	my $templateid = $zabbix->exist('template', {'filter' => {'host' => $name}});

	if ($templateid)
	{
		my $config = {
			'templateid' => $templateid,
			'groups'     => {'groupid' => TEMPLATES_TLD_GROUPID},
			'host'       => $name
		};

		if (defined($linked_templateids))
		{
			$config->{'templates'} = [map({'templateid' => $_}, @{$linked_templateids})];
		}

		$zabbix->update('template', $config);
	}
	else
	{
		my $config = {
			'groups'=> {'groupid' => TEMPLATES_TLD_GROUPID},
			'host'  => $name
		};

		if (defined($linked_templateids))
		{
			$config->{'templates'} = [map({'templateid' => $_}, @{$linked_templateids})];
		}

		my $result = $zabbix->create('template', $config);

		$templateid = $result->{'templateids'}[0];
	}

	if (defined($zabbix->last_error))
	{
		return $zabbix->last_error;
	}

	return $templateid;
}

sub create_item
{
	my $options = shift;

	my $result;
	my $itemid;

	my $rsmhost_dns_ns_log_action;

	if ($itemid = $zabbix->exist('item', {'filter' => {'hostid' => $options->{'hostid'}, 'key_' => $options->{'key_'}}}))
	{
		$options->{'itemid'} = $itemid;

		if ($options->{'key_'} =~ '^rsm\.slv\.dns\.ns\.downtime\[.*,.*\]$')
		{
			if ($zabbix->get('item', {'itemids' => [$itemid], 'output' => ['status']})->{'status'} != ITEM_STATUS_ACTIVE)
			{
				$rsmhost_dns_ns_log_action = RSMHOST_DNS_NS_LOG_ACTION_ENABLE;
			}
		}

		$result = $zabbix->update('item', $options);
	}
	else
	{
		if ($options->{'key_'} =~ '^rsm\.slv\.dns\.ns\.downtime\[.*,.*\]$')
		{
			$rsmhost_dns_ns_log_action = RSMHOST_DNS_NS_LOG_ACTION_CREATE;
		}

		$result = $zabbix->create('item', $options);
	}

	if (defined($zabbix->last_error))
	{
		return $zabbix->last_error;
	}

	if (defined(${$result->{'itemids'}}[0]))
	{
		$result = ${$result->{'itemids'}}[0];
	}

	#if (ref($result) ne '' || $result eq '')
	#{
	#	pfail("cannot create item:\n", Dumper($options));
	#}

	if (defined($rsmhost_dns_ns_log_action))
	{
		rsmhost_dns_ns_log($result, $rsmhost_dns_ns_log_action);
	}

	return $result;
}

sub create_trigger
{
	my $options     = shift;
	my $host_name   = shift;
	my $created_ref = shift; # optional: 0 - updated, 1 - created

	my $result;
	my $filter;
	my $triggerid;

	$filter->{'description'} = $options->{'description'};
	if ($host_name)
	{
		$filter->{'host'} = $host_name;
	}

	if ($triggerid = $zabbix->exist('trigger', {'filter' => $filter}))
	{
		$options->{'triggerid'} = $triggerid;
		$result = $zabbix->update('trigger', $options);
		if ($created_ref)
		{
			$$created_ref = 0;
		}
	}
	else
	{
		$result = $zabbix->create('trigger', $options);
		if ($created_ref)
		{
			$$created_ref = 1;
		}
	}

	#if (ref($result) ne '' || $result eq '')
	#{
	#	pfail("cannot create trigger:\n", Dumper($options));
	#}

	return $result;
}

sub create_macro($$;$$)
{
	my $name         = shift;
	my $value        = shift;
	my $templateid   = shift;
	my $force_update = shift;

	my ($result, $params, $error);

	if (defined($templateid))
	{
		$params = {'countOutput' => 1, 'hostids' => $templateid, 'filter' => {'macro' => $name}};

		$result = $zabbix->get('usermacro', $params);

		if ($result)
		{
			$params = {
				'output' => ['hostmacroid'],
				'hostids' => $templateid,
				'filter' => {'macro' => $name},
			};

			$result = $zabbix->get('usermacro', $params);

			if (defined($result->{'hostmacroid'}) && defined($force_update))
			{
				$params = {
					'hostmacroid' => $result->{'hostmacroid'},
					'value' => $value,
				};

				$zabbix->update('usermacro', $params);
			}
		}
		else
		{
			my $description = CFG_MACRO_DESCRIPTION->{$name};

			$params = {'hostid' => $templateid, 'macro' => $name, 'value' => $value};

			$params->{'description'} = $description if (defined($description));

			$result = $zabbix->create('usermacro', $params);
		}

		return $result->{'hostmacroids'}[0];
	}
	else
	{
		$params = {'countOutput' => 1, 'globalmacro' => 1, 'filter' => {'macro' => $name}};

		$result = $zabbix->get('usermacro', $params);

		if (check_api_error($result) eq true)
		{
			return $result;
		}

		if ($result)
		{
			$result = $zabbix->get('usermacro', {'output' => ['globalmacroid', 'value'], 'globalmacro' => 1, 'filter' => {'macro' => $name}});

			if (defined($force_update) && defined($result->{'globalmacroid'}) && ($value ne $result->{'value'}))
			{
				$zabbix->macro_global_update({'globalmacroid' => $result->{'globalmacroid'}, 'value' => $value});
			}
		}
		else
		{
			$result = $zabbix->macro_global_create({'macro' => $name, 'value' => $value});
		}

		return $result->{'globalmacroids'}[0];
	}
}

sub get_host_macro
{
	my $templateid = shift;
	my $name       = shift;

	my $result;

	$result = $zabbix->get('usermacro', {'hostids' => $templateid, 'output' => 'extend', 'filter' => {'macro' => $name}});

	return $result;
}

sub create_passive_proxy($$$$$)
{
	my $probe_name         = shift;
	my $probe_ip           = shift;
	my $probe_port         = shift;
	my $probe_psk_identity = shift;
	my $probe_psk          = shift;

	my $probe = get_probe($probe_name, false);

	# TODO: reduce amount of copy-pasted code

	if (defined($probe->{'proxyid'}))
	{
		my $vars = {
			'proxyid'   => $probe->{'proxyid'},
			'status'    => HOST_STATUS_PROXY_PASSIVE,
			'interface' => {
				'ip'    => $probe_ip,
				'dns'   => '',
				'useip' => true,
				'port'  => $probe_port
			}
		};

		if (defined($probe->{'interface'}) && ref($probe->{'interface'}) eq 'HASH')
		{
			$vars->{'interface'}{'interfaceid'} = $probe->{'interface'}->{'interfaceid'};
		}

		if (defined($probe_psk_identity))
		{
			$vars->{'tls_psk_identity'} = $probe_psk_identity;
			$vars->{'tls_psk'} = $probe_psk;
			$vars->{'tls_connect'} = HOST_ENCRYPTION_PSK;
		}

		my $result = $zabbix->update('proxy', $vars);

		if (scalar($result->{'proxyids'}))
		{
			return $result->{'proxyids'}[0];
		}
	}
	else
	{
		my $vars = {
			'host'      => $probe_name,
			'status'    => HOST_STATUS_PROXY_PASSIVE,
			'interface' => {
				'ip'    => $probe_ip,
				'dns'   => '',
				'useip' => true,
				'port'  => $probe_port
			},
			'hosts'     => []
		};

		if (defined($probe_psk_identity))
		{
			$vars->{'tls_psk_identity'} = $probe_psk_identity;
			$vars->{'tls_psk'} = $probe_psk;
			$vars->{'tls_connect'} = HOST_ENCRYPTION_PSK;
		}

		my $result = $zabbix->create('proxy', $vars);

		if (scalar($result->{'proxyids'}))
		{
			return $result->{'proxyids'}[0];
		}
	}

	return;
}

sub create_probe_template
{
	my $root_name = shift;
	my $epp       = shift;
	my $ipv4      = shift;
	my $ipv6      = shift;
	my $rdds      = shift;
	my $rdap      = shift;
	my $resolver  = shift;

	my $templateid = create_template(TEMPLATE_PROBE_CONFIG_PREFIX . $root_name);

	create_macro('{$RSM.IP4.ENABLED}' , defined($ipv4)     ? $ipv4     : '1'        , $templateid, defined($ipv4)     ? 1 : undef);
	create_macro('{$RSM.IP6.ENABLED}' , defined($ipv6)     ? $ipv6     : '1'        , $templateid, defined($ipv6)     ? 1 : undef);
	create_macro('{$RSM.RESOLVER}'    , defined($resolver) ? $resolver : '127.0.0.1', $templateid, defined($resolver) ? 1 : undef);
	create_macro('{$RSM.RDDS.ENABLED}', defined($rdds)     ? $rdds     : '1'        , $templateid, defined($rdds)     ? 1 : undef);
	create_macro('{$RSM.RDAP.ENABLED}', defined($rdap)     ? $rdap     : '1'        , $templateid, defined($rdap)     ? 1 : undef);
	create_macro('{$RSM.EPP.ENABLED}' , defined($epp)      ? $epp      : '1'        , $templateid, defined($epp)      ? 1 : undef);

	return $templateid;
}

sub add_dependency($$)
{
	my $triggerid   = shift;
	my $depend_down = shift;

	my $result = $zabbix->trigger_dep_add({'triggerid' => $depend_down, 'dependsOnTriggerid' => $triggerid});

	return $result;
}

sub get_items_like($$$)
{
	my $hostid      = shift;
	my $like        = shift;
	my $is_template = shift;

	my $result;

	if (!defined($is_template) || $is_template == false)
	{
		$result = $zabbix->get(
			'item',
			{
				'hostids' => [$hostid],
				'output' => ['itemid', 'name', 'hostid', 'key_', 'status'],
				'search' => {'key_' => $like},
				'preservekeys' => true
			}
		);

		return $result;
	}

	$result = $zabbix->get(
		'item',
		{
			'templateids' => [$hostid],
			'output' => ['itemid', 'name', 'hostid', 'key_', 'status'],
			'search' => {'key_' => $like},
			'preservekeys' => true
		}
	);

	return $result;
}

# returns list of hashes - [{'key' => $key, 'itemid' => $itemid, 'status' => $status}, ...]
sub get_host_items($)
{
	my $hostid = shift;

	my $items_ref = get_items_like($hostid, undef, 0);
	my @items_arr = map {
		{
			'key'    => $_->{'key_'},
			'itemid' => $_->{'itemid'},
			'status' => $_->{'status'},
		}
	} values(%{$items_ref});

	return \@items_arr;
}

sub get_triggers_by_items($)
{
	my @itemids = shift;

	my $result;

	$result = $zabbix->get('trigger', {'itemids' => @itemids, 'output' => ['triggerid'], 'preservekeys' => true});

	return $result;
}

sub set_tld_type($$$)
{
	my $tld      = shift;
	my $tld_type = shift;
	my $tld_type_probe_results_groupid = shift;

	my %tld_type_groups = (@{[TLD_TYPE_G]} => undef, @{[TLD_TYPE_CC]} => undef, @{[TLD_TYPE_OTHER]} => undef, @{[TLD_TYPE_TEST]} => undef);

	foreach my $group (keys(%tld_type_groups))
	{
		my $groupid = create_group($group);

		if (check_api_error($groupid) == true)
		{
			pfail($groupid->{'data'});
		}

		$tld_type_groups{$group} = int($groupid);
	}

	my $result = get_host($tld, true);

	if (check_api_error($result) == true)
	{
		pfail($result->{'data'});
	}

	unless ($result->{'hostid'})
	{
		pfail("host \"$tld\" not found");
	}

	my $hostid = $result->{'hostid'};
	my $hostgroups_ref = $result->{'groups'};
	my $current_tld_type;
	my $alreadyset = false;

	my $options = {
		'hostid' => $hostid,
		'host'   => $tld,
		'groups' => []
	};

	foreach my $hostgroup_ref (@$hostgroups_ref)
	{
		my $hostgroupname = $hostgroup_ref->{'name'};
		my $hostgroupid = $hostgroup_ref->{'groupid'};

		my $skip_hostgroup = false;

		foreach my $group (keys(%tld_type_groups))
		{
			my $groupid = $tld_type_groups{$group};

			if ($hostgroupid == $groupid)
			{
				if ($tld_type eq $hostgroupname)
				{
					$alreadyset = true;
				}

				if (defined($current_tld_type))
				{
					pfail("TLD \"$tld\" linked to more than one TLD type");
				}

				$current_tld_type = $hostgroupid;
				$skip_hostgroup = true;

				last;
			}
		}

		if ($skip_hostgroup == false)
		{
			push(@{$options->{'groups'}}, {'groupid' => $hostgroupid});
		}
	}

	if ($alreadyset == true)
	{
		return false;
	}

	# add new group to the options
	push(@{$options->{'groups'}}, {'groupid' => $tld_type_groups{$tld_type}});
	push(@{$options->{'groups'}}, {'groupid' => $tld_type_probe_results_groupid});

	$result = create_host($options);

	if (check_api_error($result) == true)
	{
		pfail($result->{'data'});
	}

	return true;
}

sub __exec($)
{
	my $cmd = shift;

	my $err = `$cmd 2>&1 1>/dev/null`;
	chomp($err);

	unless ($? == 0)
	{
		pfail($err);
	}
}

sub rsmhost_dns_ns_log($$)
{
	my $itemid = shift;
	my $action = shift;

	my $config = get_rsm_config();
	set_slv_config($config);

	my $server_key = opt('server-id') ? get_rsm_server_key(getopt('server-id')) : get_rsm_local_key($config);

	my $sql = "insert into rsmhost_dns_ns_log (itemid,clock,action) values (?,?,?)";
	my $params = [$itemid, time(), $action];

	db_connect($server_key);
	db_exec($sql, $params);
	db_disconnect();
}

sub update_rsmhost_config_times($)
{
	my $rsmhost = shift;

	my $config_template = get_template(TEMPLATE_RSMHOST_CONFIG_PREFIX . $rsmhost, 1, 0);

	if (!%{$config_template})
	{
		# don't do anything for new rsmhosts
		return;
	}

	my ($macro) = grep { $_->{'macro'} eq '{$RSM.TLD.CONFIG.TIMES}' } @{$config_template->{'macros'}};

	if (!defined($macro))
	{
		pfail("macro \"{\$RSM.TLD.CONFIG.TIMES}\" not found for rsmhost \"$rsmhost\"");
	}

	my @times = split(/;/, $macro->{'value'});

	# remove entries that are more than 6 months old
	@times = grep { $_ >= $^T - 180 * 86400 } @times;

	# add current time
	push(@times, $^T);

	create_macro('{$RSM.TLD.CONFIG.TIMES}', join(';', @times), $config_template->{'templateid'}, 1);
}

sub pfail
{
	print("Error: ", @_, "\n");
	exit(-1);
}

1;
