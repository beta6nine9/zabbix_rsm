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
		create_probe_template create_probe_status_template create_host create_group create_template create_item create_trigger create_macro update_root_servers
		create_passive_proxy probe_exists get_host_group get_template get_probe get_host
		remove_templates remove_hosts remove_hostgroups remove_probes remove_items
		disable_host disable_hosts
		enable_items
		disable_items disable_triggers
		rename_host rename_proxy rename_template rename_hostgroup
		macro_value get_global_macro_value get_host_macro
		set_proxy_status
		get_application_id get_items_like set_tld_type get_triggers_by_items
		add_dependency
		create_cron_jobs
		create_probe_health_tmpl
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
	my $items  = shift;
	my $status = shift;

	unless (scalar(@{$items}))
	{
		return;
	}

	my $result;

	foreach my $itemid (@{$items})
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
	my $items = shift;

	return update_items_status($items, ITEM_STATUS_ACTIVE);
}

sub disable_items($)
{
	my $items = shift;

	return update_items_status($items, ITEM_STATUS_DISABLED);
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

sub update_root_servers(;$)
{
	my $root_servers = shift;

	my $macro_value_v4 = "";
	my $macro_value_v6 = "";

	if ($root_servers)
	{
		($macro_value_v4, $macro_value_v6)  = split(';', $root_servers);

		create_macro('{$RSM.IP4.ROOTSERVERS1}', $macro_value_v4, undef, 1); # global, force
		create_macro('{$RSM.IP6.ROOTSERVERS1}', $macro_value_v6, undef, 1); # global, force
	}

	return '"{$RSM.IP4.ROOTSERVERS1}","{$RSM.IP6.ROOTSERVERS1}"';
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
	my $name             = shift;
	my $child_templateid = shift;

	my $result;
	my $templateid;
	my $options;

	# TODO: reduce amount of copy-pasted code

	unless ($templateid = $zabbix->exist('template', {'filter' => {'host' => $name}}))
	{
		$options = {
			'groups'=> {'groupid' => TEMPLATES_TLD_GROUPID},
			'host'  => $name
		};

		if (defined($child_templateid))
		{
			$options->{'templates'} = [{'templateid' => $child_templateid}];
		}

		$result = $zabbix->create('template', $options);

		$templateid = $result->{'templateids'}[0];
	}
	else
	{
		$options = {
			'templateid' => $templateid,
			'groups'     => {'groupid' => TEMPLATES_TLD_GROUPID},
			'host'       => $name
		};
		if (defined($child_templateid))
		{
			$options->{'templates'} = [{'templateid' => $child_templateid}];
		}

		$result = $zabbix->update('template', $options);
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

sub create_macro
{
	my $name         = shift;
	my $value        = shift;
	my $templateid   = shift;
	my $force_update = shift;

	my $result;
	my $error;

	if (defined($templateid))
	{
		if ($zabbix->get('usermacro', {'countOutput' => 1, 'hostids' => $templateid, 'filter' => {'macro' => $name}}))
		{
			$result = $zabbix->get('usermacro', {'output' => 'hostmacroid', 'hostids' => $templateid, 'filter' => {'macro' => $name}});
			if (defined($result->{'hostmacroid'}) && defined($force_update))
			{
				$zabbix->update('usermacro', {'hostmacroid' => $result->{'hostmacroid'}, 'value' => $value});
			}
		}
		else
		{
			my $params = {'hostid' => $templateid, 'macro' => $name, 'value' => $value};

			$params->{'description'} = CFG_MACRO_DESCRIPTION->{$name} if (defined(CFG_MACRO_DESCRIPTION->{$name}));
			$result = $zabbix->create('usermacro', $params);
		}

		return $result->{'hostmacroids'}[0];
	}
	else
	{
		$result = $zabbix->get('usermacro', {'countOutput' => 1, 'globalmacro' => 1, 'filter' => {'macro' => $name}});

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

sub get_application_id
{
	my $name       = shift;
	my $templateid = shift;

	my $applicationid;

	unless ($applicationid = $zabbix->exist('application', {'filter' => {'name' => $name, 'hostid' => $templateid}}))
	{
		my $result = $zabbix->create('application', {'name' => $name, 'hostid' => $templateid});
		return $result->{'applicationids'}[0];
	}

	return $applicationid;
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

	my $templateid = create_template('Template ' . $root_name);

	create_macro('{$RSM.IP4.ENABLED}' , defined($ipv4)     ? $ipv4     : '1'        , $templateid, defined($ipv4)     ? 1 : undef);
	create_macro('{$RSM.IP6.ENABLED}' , defined($ipv6)     ? $ipv6     : '1'        , $templateid, defined($ipv6)     ? 1 : undef);
	create_macro('{$RSM.RESOLVER}'    , defined($resolver) ? $resolver : '127.0.0.1', $templateid, defined($resolver) ? 1 : undef);
	create_macro('{$RSM.RDDS.ENABLED}', defined($rdds)     ? $rdds     : '1'        , $templateid, defined($rdds)     ? 1 : undef);
	create_macro('{$RSM.RDAP.ENABLED}', defined($rdap)     ? $rdap     : '1'        , $templateid, defined($rdap)     ? 1 : undef);
	create_macro('{$RSM.EPP.ENABLED}' , defined($epp)      ? $epp      : '1'        , $templateid, defined($epp)      ? 1 : undef);

	return $templateid;
}

sub create_probe_status_template
{
	my $probe_name          = shift;
	my $child_templateid    = shift;
	my $root_servers_macros = shift;

	my $template_name = 'Template ' . $probe_name . ' Status';

	my $templateid = create_template($template_name, $child_templateid);

	create_item({
		'name'         => 'Probe status ($1)',
		'key_'         => 'rsm.probe.status[automatic,' . $root_servers_macros . ']',
		'hostid'       => $templateid,
		'applications' => [get_application_id('Probe status', $templateid)],
		'type'         => ITEM_TYPE_SIMPLE,
		'value_type'   => ITEM_VALUE_TYPE_UINT64,
		'delay'        => CFG_PROBE_STATUS_DELAY,
		'valuemapid'   => RSM_VALUE_MAPPINGS->{'rsm_probe'}
	});

	create_item({
		'name'         => 'Probe status ($1)',
		'key_'         => 'rsm.probe.status[manual]',
		'hostid'       => $templateid,
		'applications' => [get_application_id('Probe status', $templateid)],
		'type'         => ITEM_TYPE_TRAPPER,
		'value_type'   => ITEM_VALUE_TYPE_UINT64,
		'valuemapid'   => RSM_VALUE_MAPPINGS->{'rsm_probe'}
	});

	create_item({
		'name'         => 'Local resolver status ($1)',
		'key_'         => 'resolver.status[{$RSM.RESOLVER},{$RESOLVER.STATUS.TIMEOUT},{$RESOLVER.STATUS.TRIES},{$RSM.IP4.ENABLED},{$RSM.IP6.ENABLED}]',
		'hostid'       => $templateid,
		'applications' => [get_application_id('Probe status', $templateid)],
		'type'         => ITEM_TYPE_SIMPLE,
		'value_type'   => ITEM_VALUE_TYPE_UINT64,
		'delay'        => CFG_PROBE_STATUS_DELAY,
		'valuemapid'   => RSM_VALUE_MAPPINGS->{'service_state'}
	});

	create_trigger(
		{
			'description' => 'Probe {HOST.NAME} has been knocked out',
			'expression'  => '{' . $template_name . ':rsm.probe.status[manual].last(0)}=0',
			'priority'    => '4',
		},
		$template_name
	);

	create_trigger(
		{
			'description' => 'Probe {HOST.NAME} has been disabled for more than {$RSM.PROBE.MAX.OFFLINE}',
			'expression'  => '{' . $template_name . ':rsm.probe.status[manual].max({$RSM.PROBE.MAX.OFFLINE})}=0',
			'priority'    => '3',
		},
		$template_name
	);

	create_trigger(
		{
			'description' => 'Probe {HOST.NAME} has been disabled by tests',
			'expression'  => '{' . $template_name . ':rsm.probe.status[automatic,"{$RSM.IP4.ROOTSERVERS1}","{$RSM.IP6.ROOTSERVERS1}"].last(0)}=0',
			'priority'    => '4',
		},
		$template_name
	);

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
		$result = $zabbix->get('item', {'hostids' => [$hostid], 'output' => ['itemid', 'name', 'hostid', 'key_', 'status'], 'search' => {'key_' => $like}, 'preservekeys' => true});
		return $result;
	}

	$result = $zabbix->get('item', {'templateids' => [$hostid], 'output' => ['itemid', 'name', 'hostid', 'key_', 'status'], 'search' => {'key_' => $like}, 'preservekeys' => true});

	return $result;
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

sub create_cron_jobs($)
{
	my $slv_path = shift;

	my $errlog = '/var/log/zabbix/rsm.slv.err';

	use constant CRON_D_PATH => '/etc/cron.d';
	my $slv_file;

	my $rv = opendir DIR, CRON_D_PATH;

	unless ($rv)
	{
		pfail("cannot open " . CRON_D_PATH);
	}

	# first remove current entries
	while (($slv_file = readdir DIR))
	{
		if ($slv_file !~ /^rsm.slv/ && $slv_file !~ /^rsm.probe/)
		{
			next;
		}

		$slv_file = CRON_D_PATH . "/$slv_file";

		__exec("/bin/rm -f $slv_file");
	}

	my $avail_shift = 0;
	my $avail_step = 1;
	my $avail_limit = 5;
	my $avail_cur = $avail_shift;

	my $rollweek_shift = 3;
	my $rollweek_step = 1;
	my $rollweek_limit = 8;
	my $rollweek_cur = $rollweek_shift;

	my $downtime_shift = 6;
	my $downtime_step = 1;
	my $downtime_limit = 11;
	my $downtime_cur = $downtime_shift;

	my $rtt_shift = 10;
	my $rtt_step = 1;
	my $rtt_limit = 20;
	my $rtt_cur = $rtt_shift;

	$rv = opendir DIR, $slv_path;

	unless ($rv)
	{
		pfail("cannot open $slv_path");
	}

	# set up what's needed
	while (($slv_file = readdir DIR))
	{
		unless ($slv_file =~ /^rsm\..*\.pl$/)
		{
			next;
		}

		my $cron_file = $slv_file;
		$cron_file =~ s/\./-/g;

		my $err;

		if ($slv_file =~ /\.slv\..*\.rtt\.pl$/)
		{
			# monthly RTT data
			if (write_file(CRON_D_PATH . "/$cron_file", "* * * * * root sleep $rtt_cur; $slv_path/$slv_file >> $errlog 2>&1\n", \$err) != SUCCESS)
			{
				pfail($err);
			}

			$rtt_cur += $rtt_step;
			$rtt_cur = $rtt_shift if ($rtt_cur >= $rtt_limit);
		}
		elsif ($slv_file =~ /\.slv\..*\.downtime\.pl$/)
		{
			# downtime
			if (write_file(CRON_D_PATH . "/$cron_file", "* * * * * root sleep $downtime_cur; $slv_path/$slv_file >> $errlog 2>&1\n", \$err) != SUCCESS)
			{
				pfail($err);
			}

			$downtime_cur += $downtime_step;
			$downtime_cur = $downtime_shift if ($downtime_cur >= $downtime_limit);
		}
		elsif ($slv_file =~ /\.slv\..*\.avail\.pl$/)
		{
			# service availability
			if (write_file(CRON_D_PATH . "/$cron_file", "* * * * * root sleep $avail_cur; $slv_path/$slv_file >> $errlog 2>&1\n", \$err) != SUCCESS)
			{
				pfail($err);
			}

			$avail_cur += $avail_step;
			$avail_cur = $avail_shift if ($avail_cur >= $avail_limit);
		}
		elsif ($slv_file =~ /\.slv\..*\.rollweek\.pl$/)
		{
			# rolling week
			if (write_file(CRON_D_PATH . "/$cron_file", "* * * * * root sleep $rollweek_cur; $slv_path/$slv_file >> $errlog 2>&1\n", \$err) != SUCCESS)
			{
				pfail($err);
			}

			$rollweek_cur += $rollweek_step;
			$rollweek_cur = $rollweek_shift if ($rollweek_cur >= $rollweek_limit);
		}
		else
		{
			# everything else
			if (write_file(CRON_D_PATH . "/$cron_file", "* * * * * root $slv_path/$slv_file >> $errlog 2>&1\n", \$err) != SUCCESS)
			{
				pfail($err);
			}
		}
	}
}

sub create_probe_health_tmpl()
{
	my $host_name = 'Template Proxy Health';
	my $templateid = create_template($host_name, LINUX_TEMPLATEID);

	my $item_key = 'zabbix[proxy,{$RSM.PROXY_NAME},lastaccess]';

	create_item({
		'name'         => 'Availability of probe',
		'key_'         => $item_key,
		'status'       => ITEM_STATUS_ACTIVE,
		'hostid'       => $templateid,
		'applications' => [get_application_id('Probe Availability', $templateid)],
		'type'         => ITEM_TYPE_INTERNAL,
		'value_type'   => ITEM_VALUE_TYPE_UINT64,
		'units'        => 'unixtime',
		'delay'        => '60'
	});

	create_trigger(
		{
			'description' => 'Probe {$RSM.PROXY_NAME} is unavailable',
			'expression'  => "{TRIGGER.VALUE}=0 and {$host_name:$item_key.fuzzytime(2m)}=0 or\r\n" .
					 "{TRIGGER.VALUE}=1 and (\r\n" .
					 "\t{$host_name:$item_key.now()}-{$host_name:$item_key.last(#1)}>1m or\r\n" .
					 "\t{$host_name:$item_key.now()}-{$host_name:$item_key.last(#2)}>2m or\r\n" .
					 "\t{$host_name:$item_key.now()}-{$host_name:$item_key.last(#3)}>3m\r\n" .
					 ")",
			'priority'    => 4
		},
		$host_name
	);

	create_item({
		'name'         => 'Probe main status',
		'key_'         => PROBE_KEY_ONLINE,
		'status'       => ITEM_STATUS_ACTIVE,
		'hostid'       => $templateid,
		'applications' => [get_application_id('Probe Availability', $templateid)],
		'type'         => ITEM_TYPE_TRAPPER,
		'value_type'   => ITEM_VALUE_TYPE_UINT64,
		'valuemapid'   => RSM_VALUE_MAPPINGS->{'rsm_probe'}
	});

	return $templateid;
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

sub pfail
{
	print("Error: ", @_, "\n");
	exit(-1);
}

1;
