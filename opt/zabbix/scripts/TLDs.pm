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

sub zbx_connect($$$;$) {
    my $url = shift;
    my $user = shift;
    my $password = shift;
    my $debug = shift;

    $zabbix = Zabbix->new({'url' => $url, user => $user, password => $password, 'debug' => $debug});

    return $zabbix->{'error'} if defined($zabbix->{'error'}) and $zabbix->{'error'} ne '';

    return true;
}

sub check_api_error($) {
    my $result = shift;

    return true if ('HASH' eq ref($result) && (defined($result->{'error'}) || defined($result->{'code'})));

    return false;
}

sub get_api_error($) {
    my $result = shift;

    return $result->{'error'}->{'data'} if (check_api_error($result) eq true);

    return;
}

sub zbx_need_relogin($) {
    my $result = shift;

    if (check_api_error($result) eq true) {
	return true if ($result->{'error'}->{'data'} =~ /Session terminated/);
    }

    return false;
}

sub get_proxies_list {
    my $proxies_list;

    $proxies_list = $zabbix->get('proxy',{'output' => ['proxyid', 'host', 'status'], 'selectInterface' => ['ip'],
					  'preservekeys' => 1 });

    return $proxies_list;
}

sub probe_exists($) {
    my $name = shift;

    my $result = $zabbix->get('proxy',{'output' => ['proxyid'], 'filter' => {'host' => $name}, 'preservekeys' => 1 });

    return (keys %{$result}) ? true : false;
}

sub get_probe($$) {
    my $probe_name = shift;
    my $selectHosts = shift;

    my $options = {'output' => ['proxyid', 'host'], 'filter' => {'host' => $probe_name}, 'selectInterface' => ['interfaceid']};

    $options->{'selectHosts'} = ['hostid', 'name', 'host'] if (defined($selectHosts) and $selectHosts eq true);

    my $result = $zabbix->get('proxy', $options);

    return $result;
}

sub get_host_group($$$) {
    my $group_name = shift;
    my $selectHosts = shift;
    my $selectType = shift;

    my $options = {'output' => 'extend', 'filter' => {'name' => $group_name}};

    $options->{'selectHosts'} = ['hostid', 'host', 'name'] if (defined($selectHosts) and $selectHosts eq true);

    my $result = $zabbix->get('hostgroup', $options);

    if ($selectType eq true && scalar(@{$result->{'hosts'}}) != 0)
    {
	    foreach my $tld (@{$result->{'hosts'}}) {
		    my $hostid = $tld->{'hostid'};
		    $options = {'output' => 'extend', 'filter' => {'hostid' => $hostid}};
		    $options->{'selectGroups'} = ['name'];
		    my $result2 = $zabbix->get('host', $options);
		    foreach my $group (@{$result2->{'groups'}}) {
			    my $name = $group->{'name'};
			    next unless ($name =~ /^[a-z]+TLD$/);
			    $tld->{'type'} = $group->{'name'};
			    last;
		    }
		    die("cannot get TLD type of \"", $tld->{'host'}, "\"") unless (defined($tld->{'type'}));
	    }
    }

    return $result;
}

sub get_template($$$) {
    my $template_name = shift;
    my $selectMacros = shift;
    my $selectHosts = shift;

    my $options = {'output' => ['templateid', 'host'], 'filter' => {'host' => $template_name}};

    $options->{'selectMacros'} = 'extend' if (defined($selectMacros) and $selectMacros eq true);

    $options->{'selectHosts'} = ['hostid', 'host'] if (defined($selectHosts) and $selectHosts eq true);

    my $result = $zabbix->get('template', $options);

    return $result;
}

sub remove_templates($) {
    my @templateids = shift;

    return unless scalar(@templateids);

    my $result = $zabbix->remove('template', @templateids);

    return $result;
}

sub remove_hosts($) {
    my @hosts = shift;

    return unless scalar(@hosts);

    my $result = $zabbix->remove('host', @hosts);

    return $result;
}

sub disable_hosts($) {
    my @hosts = shift;

    return unless scalar(@hosts);

    my $result = $zabbix->massupdate('host', {'hosts' => @hosts, 'status' => HOST_STATUS_NOT_MONITORED});

    return $result;
}

sub remove_hostgroups($) {
    my @hostgroupids = shift;

    return unless scalar(@hostgroupids);

    my $result = $zabbix->remove('hostgroup', @hostgroupids);

    return $result;
}

sub remove_probes($) {
    my @probes = shift;

    return unless scalar(@probes);

    my $result = $zabbix->remove('proxy', @probes);

    return $result;
}

sub update_items_status($$) {
	my $items = shift;
	my $status = shift;

	return unless scalar(@{$items});

	my $result;

	foreach my $itemid (@{$items}) {
		my $rsmhost_dns_ns_log_action;

		my $item = $zabbix->get('item', {'itemids' => [$itemid], 'output' => ['key_', 'status']});
		if ($item->{'key_'} =~ '^rsm\.slv\.dns\.ns\.downtime\[.*,.*\]$' && $item->{'status'} != $status) {
			$rsmhost_dns_ns_log_action = RSMHOST_DNS_NS_LOG_ACTION_ENABLE  if $status == 0;
			$rsmhost_dns_ns_log_action = RSMHOST_DNS_NS_LOG_ACTION_DISABLE if $status == 1;
		}

		$result->{$itemid} = $zabbix->update('item', {'itemid' => $itemid, 'status' => $status});

		if (defined($rsmhost_dns_ns_log_action)) {
			rsmhost_dns_ns_log($itemid, $rsmhost_dns_ns_log_action);
		}
	}

	return $result;
}

sub enable_items($) {
    my $items = shift;

    return update_items_status($items, ITEM_STATUS_ACTIVE);
}

sub disable_items($) {
    my $items = shift;

    return update_items_status($items, ITEM_STATUS_DISABLED);
}

sub disable_triggers($) {
    my $triggers = shift;

    return unless scalar(@{$triggers});

    my $result;

    foreach my $triggerid (@{$triggers}) {
	$result->{$triggerid} = $zabbix->update('trigger', {'triggerid' => $triggerid, 'status' => TRIGGER_STATUS_DISABLED});
    }

    return $result;
}

sub remove_items($) {
    my $items = shift;

    return unless scalar(@{$items});

    my $result = $zabbix->remove('item', $items );

    return $result;
}


sub disable_host($) {
    my $hostid = shift;

    return unless defined($hostid);

    my $result = $zabbix->update('host', {'hostid' => $hostid, 'status' => HOST_STATUS_NOT_MONITORED});

    return $result;
}

sub rename_template($$) {
    my $templateid = shift;
    my $template_name = shift;

    return unless defined($templateid);
    return unless defined($template_name);

    my $result = $zabbix->update('template', {'templateid' => $templateid, 'host' => $template_name});

    return $result;
}

sub rename_host($$) {
    my $hostid = shift;
    my $host_name = shift;

    return unless defined($hostid);
    return unless defined($host_name);

    my $result = $zabbix->update('host', {'hostid' => $hostid, 'host' => $host_name});

    return $result;
}

sub rename_hostgroup($$) {
    my $groupid = shift;
    my $group_name = shift;

    return unless defined($groupid);
    return unless defined($group_name);

    my $result = $zabbix->update('hostgroup', {'groupid' => $groupid, 'name' => $group_name});

    return $result;
}

sub macro_value($$) {
    my $hostmacroid = shift;
    my $value = shift;

    return if !defined($hostmacroid) or !defined($value);

    my $result = $zabbix->update('usermacro', {'hostmacroid' => $hostmacroid, 'value' => $value});

    return $result;
}

sub set_proxy_status($$) {
    my $proxyid = shift;
    my $status = shift;

    return if !defined($proxyid) or !defined($status);

    return if $status != HOST_STATUS_PROXY_ACTIVE and $status != HOST_STATUS_PROXY_PASSIVE;

    my $result = $zabbix->update('proxy', { 'proxyid' => $proxyid, 'status' => $status});

    return $result;
}

sub rename_proxy($$) {
    my $proxyid = shift;
    my $proxy_name = shift;

    return if !defined($proxyid) or !defined($proxy_name);

    my $result = $zabbix->update('proxy', { 'proxyid' => $proxyid, 'host' => $proxy_name});

    return $result;
}

sub get_host($$) {
    my $host_name = shift;
    my $selectGroups = shift;

    my $options = {'output' => ['hostid', 'host', 'status'], 'filter' => {'host' => $host_name} };

    $options->{'selectGroups'} = 'extend' if (defined($selectGroups) and $selectGroups eq true);

    my $result = $zabbix->get('host', $options);

    return $result;
}

sub get_global_macro_value($) {
    my $macro_name = shift;

    my $options = {'globalmacro' => true, output => 'extend', 'filter' => {'macro' => $macro_name}};

    my $result = $zabbix->get('usermacro', $options);

    return $result->{'value'} if defined($result->{'value'});
}


sub update_root_servers(;$) {
    my $root_servers = shift;

    my $macro_value_v4 = "";
    my $macro_value_v6 = "";

    if ($root_servers)
    {
	($macro_value_v4, $macro_value_v6)  = split(';', $root_servers);

	create_macro('{$RSM.IP4.ROOTSERVERS1}', $macro_value_v4, undef, 1);	# global, force
	create_macro('{$RSM.IP6.ROOTSERVERS1}', $macro_value_v6, undef, 1);	# global, force
    }

    return '"{$RSM.IP4.ROOTSERVERS1}","{$RSM.IP6.ROOTSERVERS1}"';
}

sub create_host {
    my $options = shift;

    my $hostid;

    unless ($hostid = $zabbix->exist('host',{'filter' => {'host' => $options->{'host'}}})) {
        my $result = $zabbix->create('host', $options);

        return $result->{'hostids'}[0];
    }

    $options->{'hostid'} = $hostid;
    delete($options->{'interfaces'});
    $result = $zabbix->update('host', $options);

    $hostid = $result->{'hostids'}[0] ? $result->{'hostids'}[0] : $options->{'hostid'};

    return $hostid;
}

sub create_group {
    my $name = shift;

    my $groupid = $zabbix->exist('hostgroup',{'filter' => {'name' => $name}});

    return $groupid if (check_api_error($groupid) eq true);

    unless ($groupid) {
        my $result = $zabbix->create('hostgroup', {'name' => $name});
	$groupid = $result->{'groupids'}[0];
    }

    return $groupid;
}

sub create_template {
    my $name = shift;
    my $child_templateid = shift;

    my ($result, $templateid, $options);

    unless ($templateid = $zabbix->exist('template',{'filter' => {'host' => $name}})) {
        $options = {'groups'=> {'groupid' => TEMPLATES_TLD_GROUPID}, 'host' => $name};

        $options->{'templates'} = [{'templateid' => $child_templateid}] if defined $child_templateid;

        $result = $zabbix->create('template', $options);

        $templateid = $result->{'templateids'}[0];
    }
    else {
        $options = {'templateid' => $templateid, 'groups'=> {'groupid' => TEMPLATES_TLD_GROUPID}, 'host' => $name};
        $options->{'templates'} = [{'templateid' => $child_templateid}] if defined $child_templateid;

        $result = $zabbix->update('template', $options);
        $templateid = $result->{'templateids'}[0];
    }

    return $zabbix->last_error if defined $zabbix->last_error;

    return $templateid;
}

sub create_item {
    my $options = shift;
    my ($result, $itemid);
    my $rsmhost_dns_ns_log_action;

    if ($itemid = $zabbix->exist('item', {'filter' => {'hostid' => $options->{'hostid'}, 'key_' => $options->{'key_'}}})) {
	$options->{'itemid'} = $itemid;
	if ($options->{'key_'} =~ '^rsm\.slv\.dns\.ns\.downtime\[.*,.*\]$') {
	    if ($zabbix->get('item', {'itemids' => [$itemid], 'output' => ['status']})->{'status'} != ITEM_STATUS_ACTIVE)
	    {
		$rsmhost_dns_ns_log_action = RSMHOST_DNS_NS_LOG_ACTION_ENABLE;
	    }
	}
	$result = $zabbix->update('item', $options);
    }
    else {
	if ($options->{'key_'} =~ '^rsm\.slv\.dns\.ns\.downtime\[.*,.*\]$') {
	    $rsmhost_dns_ns_log_action = RSMHOST_DNS_NS_LOG_ACTION_CREATE;
	}
        $result = $zabbix->create('item', $options);
    }

    return $zabbix->last_error if defined $zabbix->last_error;

    $result = ${$result->{'itemids'}}[0] if (defined(${$result->{'itemids'}}[0]));

#    pfail("cannot create item:\n", Dumper($options)) if (ref($result) ne '' or $result eq '');

    if (defined($rsmhost_dns_ns_log_action)) {
	rsmhost_dns_ns_log($result, $rsmhost_dns_ns_log_action);
    }

    return $result;
}

sub create_trigger {
    my $options = shift;
    my $host_name = shift;
    my $created_ref = shift;	# optional: 0 - updated, 1 - created

    my ($result, $filter, $triggerid);

    $filter->{'description'} = $options->{'description'};
    $filter->{'host'} = $host_name if ($host_name);

    if ($triggerid = $zabbix->exist('trigger',{'filter' => $filter})) {
	$options->{'triggerid'} = $triggerid;
        $result = $zabbix->update('trigger', $options);
	$$created_ref = 0 if ($created_ref);
    }
    else {
        $result = $zabbix->create('trigger', $options);
	$$created_ref = 1 if ($created_ref);
    }

#    pfail("cannot create trigger:\n", Dumper($options)) if (ref($result) ne '' or $result eq '');

    return $result;
}

sub create_macro {
    my $name = shift;
    my $value = shift;
    my $templateid = shift;
    my $force_update = shift;

    my ($result, $error);

    if (defined($templateid)) {
	if ($zabbix->get('usermacro',{'countOutput' => 1, 'hostids' => $templateid, 'filter' => {'macro' => $name}})) {
	    $result = $zabbix->get('usermacro',{'output' => 'hostmacroid', 'hostids' => $templateid, 'filter' => {'macro' => $name}} );
    	    $zabbix->update('usermacro',{'hostmacroid' => $result->{'hostmacroid'}, 'value' => $value}) if defined $result->{'hostmacroid'}
														     and defined($force_update);
	}
	else {
	    $result = $zabbix->create('usermacro',{'hostid' => $templateid, 'macro' => $name, 'value' => $value});
        }

	return $result->{'hostmacroids'}[0];
    }
    else {
	    $result = $zabbix->get('usermacro',{'countOutput' => 1, 'globalmacro' => 1, 'filter' => {'macro' => $name}});

	return $result if (check_api_error($result) eq true);

	if ($result) {
            $result = $zabbix->get('usermacro',{'output' => ['globalmacroid','value'], 'globalmacro' => 1, 'filter' => {'macro' => $name}} );

            $zabbix->macro_global_update({'globalmacroid' => $result->{'globalmacroid'}, 'value' => $value})
		    if (defined($force_update) && defined($result->{'globalmacroid'}) && ($value ne $result->{'value'}));
        }
        else {
            $result = $zabbix->macro_global_create({'macro' => $name, 'value' => $value});
        }

	return $result->{'globalmacroids'}[0];
    }

}

sub get_host_macro {
    my $templateid = shift;
    my $name = shift;

    my $result;

    $result = $zabbix->get('usermacro',{'hostids' => $templateid, 'output' => 'extend', 'filter' => {'macro' => $name}});

    return $result;
}

sub create_passive_proxy($$$$$) {
    my $probe_name = shift;
    my $probe_ip = shift;
    my $probe_port = shift;
    my $probe_psk_identity = shift;
    my $probe_psk = shift;

    my $probe = get_probe($probe_name, false);

    if (defined($probe->{'proxyid'})) {
	my $vars = {'proxyid' => $probe->{'proxyid'}, 'status' => HOST_STATUS_PROXY_PASSIVE};

	if (defined($probe->{'interface'}) and 'HASH' eq ref($probe->{'interface'})) {
		$vars->{'interface'} = {'interfaceid' => $probe->{'interface'}->{'interfaceid'},
					'ip' => $probe_ip, 'dns' => '', 'useip' => true, 'port' => $probe_port};
	}
	else {
		$vars->{'interface'} = {'ip' => $probe_ip, 'dns' => '', 'useip' => true, 'port' => $probe_port};
	}

	if (defined($probe_psk_identity)) {
		$vars->{'tls_psk_identity'} = $probe_psk_identity;
		$vars->{'tls_psk'} = $probe_psk;
		$vars->{'tls_connect'} = HOST_ENCRYPTION_PSK;
	}

	my $result = $zabbix->update('proxy', $vars);

	if (scalar($result->{'proxyids'})) {
            return $result->{'proxyids'}[0];
        }
    }
    else {
	my $vars = {'host' => $probe_name, 'status' => HOST_STATUS_PROXY_PASSIVE,
                                        'interface' => {'ip' => $probe_ip, 'dns' => '', 'useip' => true, 'port' => $probe_port},
                                        'hosts' => []};

	if (defined($probe_psk_identity)) {
		$vars->{'tls_psk_identity'} = $probe_psk_identity;
		$vars->{'tls_psk'} = $probe_psk;
		$vars->{'tls_connect'} = HOST_ENCRYPTION_PSK;
	}

        my $result = $zabbix->create('proxy', $vars);
	if (scalar($result->{'proxyids'})) {
	    return $result->{'proxyids'}[0];
	}
    }

    return;
}

sub get_application_id {
    my $name = shift;
    my $templateid = shift;

    my $applicationid;

    unless ($applicationid = $zabbix->exist('application',{'filter' => {'name' => $name, 'hostid' => $templateid}})) {
	my $result = $zabbix->create('application', {'name' => $name, 'hostid' => $templateid});
	return $result->{'applicationids'}[0];
    }

    return $applicationid;
}



sub create_probe_template {
    my $root_name = shift;
    my $epp = shift;
    my $ipv4 = shift;
    my $ipv6 = shift;
    my $rdds = shift;
    my $resolver = shift;

    my $templateid = create_template('Template '.$root_name);

    create_macro('{$RSM.IP4.ENABLED}', defined($ipv4) ? $ipv4 : '1', $templateid, defined($ipv4) ? 1 : undef);
    create_macro('{$RSM.IP6.ENABLED}', defined($ipv6) ? $ipv6 : '1', $templateid, defined($ipv6) ? 1 : undef);
    create_macro('{$RSM.RESOLVER}', defined($resolver) ? $resolver : '127.0.0.1', $templateid, defined($resolver) ? 1 : undef);
    create_macro('{$RSM.RDDS.ENABLED}', defined($rdds) ? $rdds : '1', $templateid, defined($rdds) ? 1 : undef);
    create_macro('{$RSM.EPP.ENABLED}', defined($epp) ? $epp : '1', $templateid, defined($epp) ? 1 : undef);

    return $templateid;
}

sub create_probe_status_template {
    my $probe_name = shift;
    my $child_templateid = shift;
    my $root_servers_macros = shift;

    my $template_name = 'Template '.$probe_name.' Status';

    my $templateid = create_template($template_name, $child_templateid);

    my $options = {
	'name' => 'Probe status ($1)',
	'key_'=> 'rsm.probe.status[automatic,'.$root_servers_macros.']',
	'hostid' => $templateid,
	'applications' => [get_application_id('Probe status', $templateid)],
	'type' => 3, 'value_type' => 3, 'delay' => cfg_probe_status_delay,
	'valuemapid' => rsm_value_mappings->{'rsm_probe'}
    };

    create_item($options);

    $options = {
	'name' => 'Probe status ($1)',
	'key_'=> 'rsm.probe.status[manual]',
	'hostid' => $templateid,
	'applications' => [get_application_id('Probe status', $templateid)],
	'type' => 2, 'value_type' => 3,
	'valuemapid' => rsm_value_mappings->{'rsm_probe'}
    };

    create_item($options);

    $options = {
	'name' => 'Local resolver status ($1)',
	'key_'=> 'resolver.status[{$RSM.RESOLVER},{$RESOLVER.STATUS.TIMEOUT},{$RESOLVER.STATUS.TRIES},{$RSM.IP4.ENABLED},{$RSM.IP6.ENABLED}]',
	'hostid' => $templateid,
	'applications' => [get_application_id('Probe status', $templateid)],
	'type' => 3, 'value_type' => 3, 'delay' => cfg_probe_status_delay,
	'valuemapid' => rsm_value_mappings->{'service_state'}
    };

    create_item($options);

    $options = {
	'description' => 'Probe {HOST.NAME} has been knocked out',
	'expression' => '{'.$template_name.':rsm.probe.status[manual].last(0)}=0',
	'priority' => '4',
    };

    create_trigger($options, $template_name);

    $options = {
	'description' => 'Probe {HOST.NAME} has been disabled for more than {$RSM.PROBE.MAX.OFFLINE}',
	'expression' => '{'.$template_name.':rsm.probe.status[manual].max({$RSM.PROBE.MAX.OFFLINE})}=0',
	'priority' => '3',
    };

    create_trigger($options, $template_name);

    $options = {
	'description' => 'Probe {HOST.NAME} has been disabled by tests',
	'expression' => '{'.$template_name.':rsm.probe.status[automatic,"{$RSM.IP4.ROOTSERVERS1}","{$RSM.IP6.ROOTSERVERS1}"].last(0)}=0',
	'priority' => '4',
    };

    create_trigger($options, $template_name);

    return $templateid;
}

sub add_dependency($$) {
    my $triggerid = shift;
    my $depend_down = shift;

    my $result = $zabbix->trigger_dep_add({'triggerid' => $depend_down, 'dependsOnTriggerid' => $triggerid});

    return $result;
}

sub get_items_like($$$) {
    my $hostid = shift;
    my $like = shift;
    my $is_template = shift;

    my $result;

    if (!defined($is_template) or $is_template == false) {
	$result = $zabbix->get('item', {'hostids' => [$hostid], 'output' => ['itemid', 'name', 'hostid', 'key_', 'status'], 'search' => {'key_' => $like}, 'preservekeys' => true});
	return $result;
    }

    $result = $zabbix->get('item', {'templateids' => [$hostid], 'output' => ['itemid', 'name', 'hostid', 'key_', 'status'], 'search' => {'key_' => $like}, 'preservekeys' => true});

    return $result;
}

sub get_triggers_by_items($) {
    my @itemids = shift;

    my $result;

    $result = $zabbix->get('trigger', {'itemids' => @itemids, 'output' => ['triggerid'], 'preservekeys' => true});

    return $result;
}

sub set_tld_type($$$) {
	my $tld = shift;
	my $tld_type = shift;
	my $tld_type_probe_results_groupid = shift;

	my %tld_type_groups = (@{[TLD_TYPE_G]} => undef, @{[TLD_TYPE_CC]} => undef, @{[TLD_TYPE_OTHER]} => undef, @{[TLD_TYPE_TEST]} => undef);

	foreach my $group (keys(%tld_type_groups))
	{
		my $groupid = create_group($group);

		pfail($groupid->{'data'}) if (check_api_error($groupid) == true);

		$tld_type_groups{$group} = int($groupid);
	}

	my $result = get_host($tld, true);

	pfail($result->{'data'}) if (check_api_error($result) == true);

	pfail("host \"$tld\" not found") unless ($result->{'hostid'});

	my $hostid = $result->{'hostid'};
	my $hostgroups_ref = $result->{'groups'};
	my $current_tld_type;
	my $alreadyset = false;

	my $options = {'hostid' => $hostid, 'host' => $tld, 'groups' => []};

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

				pfail("TLD \"$tld\" linked to more than one TLD type") if (defined($current_tld_type));

				$current_tld_type = $hostgroupid;
				$skip_hostgroup = true;

				last;
			}
		}

		push(@{$options->{'groups'}}, {'groupid' => $hostgroupid}) if ($skip_hostgroup == false);
	}

	return false if ($alreadyset == true);

	# add new group to the options
	push(@{$options->{'groups'}}, {'groupid' => $tld_type_groups{$tld_type}});
	push(@{$options->{'groups'}}, {'groupid' => $tld_type_probe_results_groupid});

	$result = create_host($options);

	pfail($result->{'data'}) if (check_api_error($result) == true);

	return true;
}

sub __exec($)
{
	my $cmd = shift;

	my $err = `$cmd 2>&1 1>/dev/null`;
	chomp($err);

	pfail($err) unless ($? == 0);
}

sub create_cron_jobs($)
{
	my $slv_path = shift;

	my $errlog = '/var/log/zabbix/rsm.slv.err';

	use constant CRON_D_PATH => '/etc/cron.d';
	my $slv_file;

	my $rv = opendir DIR, CRON_D_PATH;

	pfail("cannot open " . CRON_D_PATH) unless ($rv);

	# first remove current entries
	while (($slv_file = readdir DIR))
	{
		next if ($slv_file !~ /^rsm.slv/ && $slv_file !~ /^rsm.probe/);

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

	pfail("cannot open $slv_path") unless ($rv);

	# set up what's needed
	while (($slv_file = readdir DIR))
	{
		next unless ($slv_file =~ /^rsm\..*\.pl$/);

		my $cron_file = $slv_file;
		$cron_file =~ s/\./-/g;

		my $err;

		if ($slv_file =~ /\.slv\..*\.rtt\.pl$/)
		{
			# monthly RTT data
			pfail($err) if (SUCCESS != write_file(CRON_D_PATH . "/$cron_file", "* * * * * root sleep $rtt_cur; $slv_path/$slv_file >> $errlog 2>&1\n", \$err));

			$rtt_cur += $rtt_step;
			$rtt_cur = $rtt_shift if ($rtt_cur >= $rtt_limit);
		}
		elsif ($slv_file =~ /\.slv\..*\.downtime\.pl$/)
		{
			# downtime
			pfail($err) if (SUCCESS != write_file(CRON_D_PATH . "/$cron_file", "* * * * * root sleep $downtime_cur; $slv_path/$slv_file >> $errlog 2>&1\n", \$err));

			$downtime_cur += $downtime_step;
			$downtime_cur = $downtime_shift if ($downtime_cur >= $downtime_limit);
		}
		elsif ($slv_file =~ /\.slv\..*\.avail\.pl$/)
		{
			# service availability
			pfail($err) if (SUCCESS != write_file(CRON_D_PATH . "/$cron_file", "* * * * * root sleep $avail_cur; $slv_path/$slv_file >> $errlog 2>&1\n", \$err));

			$avail_cur += $avail_step;
			$avail_cur = $avail_shift if ($avail_cur >= $avail_limit);
		}
		elsif ($slv_file =~ /\.slv\..*\.rollweek\.pl$/)
		{
			# rolling week
			pfail($err) if (SUCCESS != write_file(CRON_D_PATH . "/$cron_file", "* * * * * root sleep $rollweek_cur; $slv_path/$slv_file >> $errlog 2>&1\n", \$err));

			$rollweek_cur += $rollweek_step;
			$rollweek_cur = $rollweek_shift if ($rollweek_cur >= $rollweek_limit);
		}
		else
		{
			# everything else
			pfail($err) if (SUCCESS != write_file(CRON_D_PATH . "/$cron_file", "* * * * * root $slv_path/$slv_file >> $errlog 2>&1\n", \$err));
		}
	}
}

sub create_probe_health_tmpl()
{
	my $host_name = 'Template Proxy Health';
	my $templateid = create_template($host_name, LINUX_TEMPLATEID);

	my $item_key = 'zabbix[proxy,{$RSM.PROXY_NAME},lastaccess]';

	create_item({
		'name'		=> 'Availability of probe',
		'key_'		=> $item_key,
		'status'	=> ITEM_STATUS_ACTIVE,
		'hostid'	=> $templateid,
		'applications'	=> [
			get_application_id('Probe Availability', $templateid)
		],
		'type'		=> 5,
		'value_type'	=> 3,
		'units'		=> 'unixtime',
		'delay'		=> '60'
	});

	create_trigger(
		{
			'description'	=> 'Probe {$RSM.PROXY_NAME} is unavailable',
			'expression'	=>
					"{TRIGGER.VALUE}=0 and {$host_name:$item_key.fuzzytime(2m)}=0 or\r\n" .
					"{TRIGGER.VALUE}=1 and (\r\n" .
					"\t{$host_name:$item_key.now()}-{$host_name:$item_key.last(#1)}>1m or\r\n" .
					"\t{$host_name:$item_key.now()}-{$host_name:$item_key.last(#2)}>2m or\r\n" .
					"\t{$host_name:$item_key.now()}-{$host_name:$item_key.last(#3)}>3m\r\n" .
					")",
			'priority'	=> 4
		},
		$host_name
	);

	create_item({
		'name'		=> 'Probe main status',
		'key_'		=> PROBE_KEY_ONLINE,
		'status'	=> ITEM_STATUS_ACTIVE,
		'hostid'	=> $templateid,
		'applications'	=> [
			get_application_id('Probe Availability', $templateid)
		],
		'type'		=> 2,
		'value_type'	=> 3,
		'valuemapid'	=> rsm_value_mappings->{'rsm_probe'}
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
	my $server_keyx = opt('server-id') ? print("get_rsm_server_key\n") : print("get_rsm_local_key\n");

	my $sql = "insert into rsmhost_dns_ns_log (itemid,clock,action) values (?,?,?)";
	my $params = [$itemid, time(), $action];

	db_connect($server_key);
	db_exec($sql, $params);
	db_disconnect();
}

sub pfail {
    print("Error: ", @_, "\n");
    exit(-1);
}

1;
