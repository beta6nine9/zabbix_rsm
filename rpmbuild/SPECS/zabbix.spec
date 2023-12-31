# we don't need the build-id files
%define _build_id_links none

Name:		zabbix%{namespace}
Version:	6.0.8%{rsmversion}
Release: 	%{?rsmprereleasetag:0.}1%{?rsmprereleasetag:%{rsmprereleasetag}}%{?dist}
Summary:	The Enterprise-class open source monitoring solution
Group:		Applications/Internet
License:	GPLv2+
URL:		http://www.zabbix.com/
Source0:	zabbix-%{version}%{?rsmprereleasetag:%{rsmprereleasetag}}.tar.gz
Source1:	selinux
Source3:	zabbix-logrotate.in
Source6:	zabbix-server.init
Source7:	zabbix-proxy.init
Source10:	zabbix-agent.service
Source11:	zabbix-server.service
Source12:	zabbix-proxy.service
Source15:	zabbix-tmpfiles.conf
Source16:	partitioning.sql
%if 0%{?rhel} >= 8
Source17:	zbx_vhost.conf
Source18:	zbx_php.conf
%else
# CentOS 7 specifics start
Source17:	zbx_vhost-rh-php73.conf
Source18:	zbx_php-rh-php73.conf
# CentOS 7 specifics end
%endif
Source19:	nginx.conf
Source20:	rsyslog.d-rsm.slv.conf
Source21:	zabbix_server.conf
Source22:	zabbix_proxy_common.conf
Source23:	zabbix_proxy_N.conf

Buildroot:	%{_tmppath}/zabbix-%{version}-%{release}-root-%(%{__id_u} -n)

%global selinuxtype	targeted
%global moduletype	services

%global modulenames	zabbix%{namespace}_proxy zabbix%{namespace}_server zabbix%{namespace}_agent zbx_php-fpm zbx_nginx
# Version of distribution SELinux policy package.
%global selinux_policyver	3.13.1-102.el7_3.13

%global _format() export %1=""; for x in %{modulenames}; do %1+=%2; %1+=" "; done;

%global namespace_list /opt/zabbix /var/log/zabbix /etc/zabbix /usr/lib/zabbix /run/zabbix /usr/sbin/zabbix /etc/sysconfig/zabbix /tmp/zabbix /etc/cron.d/rsm Description=Zabbix
%global _set_namespace_pattern() export %1=""; for x in %{namespace_list}; do %1+=s,$x,%2,g; %1+=";"; done;

# Relabel files
%global relabel_files() \ # ADD files in *.fc file

%if 0%{?rhel} >= 8
BuildRequires:	mariadb-connector-c-devel
BuildRequires:	sqlite-devel
# TODO: temporary solution for deployment, add ldns version back after DNS Reboot is deployed
#BuildRequires:	ldns-devel >= 1.7.1
BuildRequires:	ldns%{namespace}-devel
%else
BuildRequires:	mysql-devel
# TODO: temporary solution for deployment, add ldns version back after DNS Reboot is deployed
#BuildRequires:	ldns-devel >= 1.6.17
BuildRequires:	ldns%{namespace}-devel
%endif
BuildRequires:	libevent-devel
BuildRequires:	pcre-devel
BuildRequires:	curl-devel >= 7.13.1
BuildRequires:	openssl-devel >= 1.0.1
BuildRequires:	systemd
BuildRequires:	selinux-policy selinux-policy-devel

%description
Zabbix is the ultimate enterprise-level software designed for
real-time monitoring of millions of metrics collected from tens of
thousands of servers, virtual machines and network devices.

%package proxy-sqlite
Summary:			Zabbix proxy for SQLite database
Group:				Applications/Internet
Requires(post):		systemd
Requires(preun):	systemd
Requires(postun):	systemd
%if 0%{?rhel} >= 8
# TODO: temporary solution for deployment, add ldns version back after DNS Reboot is deployed
#Requires:		ldns >= 1.7.1
Requires:		ldns%{namespace}
%else
# TODO: temporary solution for deployment, add ldns version back after DNS Reboot is deployed
#Requires:		ldns >= 1.6.17
Requires:		ldns%{namespace}
%endif
Provides:		zabbix%{namespace}-proxy = %{version}-%{release}
Provides:		zabbix%{namespace}-proxy-implementation = %{version}-%{release}
Obsoletes:		zabbix%{namespace}
Obsoletes:		zabbix%{namespace}-proxy

%description proxy-sqlite
Zabbix proxy with SQLite database support.

%package proxy-sqlite-selinux
Summary:		SELinux Policies for Zabbix proxy
Group:			System Environment/Base
Requires(post):		selinux-policy-base >= %{selinux_policyver}, selinux-policy-targeted >= %{selinux_policyver}, policycoreutils, libselinux-utils
%if 0%{?rhel} >= 8
Requires(post):		policycoreutils-python-utils
%else
Requires(post):		policycoreutils-python
%endif
Requires:		zabbix%{namespace}-proxy = %{version}-%{release}

%description proxy-sqlite-selinux
SELinux policy modules for use with Zabbix proxy.

%package server-mysql
Summary:			Zabbix server for MySQL or MariaDB database
Group:				Applications/Internet
Requires:			fping
Requires(post):		systemd
Requires(preun):	systemd
Requires(postun):	systemd
%if 0%{?rhel} >= 8
# TODO: temporary solution for deployment, add ldns version back after DNS Reboot is deployed
#Requires:		ldns >= 1.7.1
Requires:		ldns%{namespace}
%else
# TODO: temporary solution for deployment, add ldns version back after DNS Reboot is deployed
#Requires:		ldns >= 1.6.17
Requires:		ldns%{namespace}
%endif
Requires:		perl-Data-Dumper
Requires:		perl-DBD-MySQL
Requires:		perl-Devel-StackTrace
Provides:		zabbix%{namespace}-server = %{version}-%{release}
Provides:		zabbix%{namespace}-server-implementation = %{version}-%{release}
Obsoletes:		zabbix%{namespace}
Obsoletes:		zabbix%{namespace}-server

%description server-mysql
Zabbix server with MySQL or MariaDB database support.

%package server-mysql-selinux
Summary:		SELinux Policies for Zabbix server
Group:			System Environment/Base
Requires(post):		selinux-policy-base >= %{selinux_policyver}, selinux-policy-targeted >= %{selinux_policyver}, policycoreutils, libselinux-utils
%if 0%{?rhel} >= 8
Requires(post):		policycoreutils-python-utils
%else
Requires(post):		policycoreutils-python
%endif
Requires:		zabbix%{namespace}-server = %{version}-%{release}

%description server-mysql-selinux
SELinux policy modules for use with Zabbix server.

%package web
Summary:			Zabbix web frontend common package
Group:				Application/Internet
BuildArch:			noarch
Requires:			nginx
%if 0%{?rhel} >= 8
Requires:			php-gd >= 7.2
Requires:			php-bcmath >= 7.2
Requires:			php-mbstring >= 7.2
Requires:			php-xml >= 7.2
Requires:			php-ldap >= 7.2
Requires:			php-json >= 7.2
Requires:			php-fpm >= 7.2
%else
# CentOS 7 specifics start
Requires:			rh-php73
Requires:			rh-php73-php-gd
Requires:			rh-php73-php-bcmath
Requires:			rh-php73-php-mbstring
Requires:			rh-php73-php-xml
Requires:			rh-php73-php-ldap
Requires:			rh-php73-php-json
Requires:			rh-php73-php-fpm
Obsoletes:			php
Obsoletes:			php-common
Obsoletes:			php-gd
Obsoletes:			php-bcmath
Obsoletes:			php-mbstring
Obsoletes:			php-xml
Obsoletes:			php-ldap
Obsoletes:			php-json
Obsoletes:			php-fpm
%endif
# CentOS 7 specifics end
Requires:			dejavu-sans-fonts
Requires:			zabbix%{namespace}-web-database = %{version}-%{release}
Requires(post):			%{_sbindir}/update-alternatives
Requires(preun):		%{_sbindir}/update-alternatives

%description web
This package provides Zabbix web frontend (with few monidications)
and includes the following frontend modules:
 - RSM (frontend modifications, including menu and custom pages)
 - RsmProvisioningApi (REST API for managing SLAM configuration)

%package web-mysql
Summary:			Zabbix web frontend for MySQL
Group:				Applications/Internet
BuildArch:			noarch
%if 0%{?rhel} >= 8
Requires:			php-mysqlnd
%else
# CentOS 7 specifics start
Requires:			rh-php73-php-mysqlnd
Obsoletes:			php-mysqlnd
# CentOS 7 specifics end
%endif
Requires:			zabbix%{namespace}-web = %{version}-%{release}
Provides:			zabbix%{namespace}-web-database = %{version}-%{release}

%description web-mysql
Zabbix web frontend for MySQL.

%package web-mysql-selinux
Summary:		SELinux Policies for Zabbix web frontend
Group:			System Environment/Base
BuildArch:		noarch
Requires(post):		selinux-policy-base >= %{selinux_policyver}, selinux-policy-targeted >= %{selinux_policyver}, policycoreutils, libselinux-utils
%if 0%{?rhel} >= 8
Requires(post):		policycoreutils-python-utils
%else
Requires(post):		policycoreutils-python
%endif
Requires:		zabbix%{namespace}-web-mysql = %{version}-%{release}

%description web-mysql-selinux
SELinux policy modules for use with Zabbix web frontend.

%package agent-selinux
Summary:		SELinux Policies for Zabbix agent
Group:			System Environment/Base
BuildArch:		noarch
Requires(post):		selinux-policy-base >= %{selinux_policyver}, selinux-policy-targeted >= %{selinux_policyver}, policycoreutils, libselinux-utils
%if 0%{?rhel} >= 8
Requires(post):		policycoreutils-python-utils
%else
Requires(post):		policycoreutils-python
%endif
Requires:		zabbix-agent

%description agent-selinux
SELinux policy modules for Zabbix agent.

%if 0%{?rhel} >= 8
%package agent
Summary:                        Zabbix Agent
Group:                          Applications/Internet
Requires:                       logrotate
Requires(pre):                  /usr/sbin/useradd
Requires(post):                 systemd
Requires(preun):                systemd
Requires(preun):                systemd
Obsoletes:                      zabbix

%description agent
Zabbix agent to be installed on monitored systems.

%package get
Summary:                        Zabbix Get
Group:                          Applications/Internet

%description get
Zabbix get command line utility

%package sender
Summary:                        Zabbix Sender
Group:                          Applications/Internet

%description sender
Zabbix sender command line utility
%endif

%package scripts
Summary:			Zabbix scripts for RSM
Group:				Applications/Internet
BuildArch:			noarch
%if 0%{?rhel} < 8
Requires:			perl-File-Pid
%endif
Requires:			perl-Data-Dumper, perl-DBD-MySQL, perl-Sys-Syslog
Requires:			perl-DateTime, perl-Config-Tiny, perl-libwww-perl
Requires:			perl-LWP-Protocol-https, perl-JSON-XS, perl-Expect
Requires:			perl-Redis, perl-DateTime-Format-RFC3339
Requires:			perl-Text-CSV_XS, perl-Types-Serialiser
Requires:			perl-Path-Tiny
Requires:			perl-Parallel-ForkManager
Requires:			perl-Devel-StackTrace
%if 0%{?rhel} >= 8
Requires:			php-cli
Requires:			php-pdo
Requires:			php-mysqlnd
Requires:			php-xml
Requires:			php-json
%else
# CentOS 7 specifics start
Requires:			rh-php73-php-cli
Requires:			rh-php73-php-pdo
Requires:			rh-php73-php-mysqlnd
Requires:			rh-php73-php-xml
Requires:			rh-php73-php-json
Obsoletes:			php-cli
Obsoletes:			php-pdo
Obsoletes:			php-mysqlnd
Obsoletes:			php-xml
Obsoletes:			php-json
# CentOS 7 specifics start
%endif
AutoReq:			no

%description scripts
Zabbix scripts for Registry/Registrar SLA Monitoring.

%package js
Summary:			Zabbix JS
Group:				Applications/Internet

%description js
Zabbix js command line utility.

%package rsm-api
Summary:			Zabbix RSM API
Group:				Applications/Internet
BuildArch:			noarch

%description rsm-api
This package provides RSM API, that works with
Provisioning API (frontend module) and implements Alerts API.

%package probe-scripts
Summary:			Set of scripts for running on a probe node
Group:				Applications/Internet
BuildArch:			noarch

%description probe-scripts
This package provides the set of scripts for running on a probe node.


%prep

# set NAMESPACE_PATTERN for prep section
%_set_namespace_pattern NAMESPACE_PATTERN ${x}%{namespace}

%setup0 -q -n zabbix-%{version}%{?rsmprereleasetag:%{rsmprereleasetag}}

sed -r -i.bak "s,^(\s*const CONFIG_FILE_PATH =).*,\1 '/etc/zabbix/web/zabbix.conf.php';," \
	ui/include/classes/core/CConfigFile.php

sed -r -i.bak "s,^(\s*require_once ).*conf/maintenance\.inc\.php.*,\1 '/etc/zabbix/web/maintenance.inc.php';," \
	ui/include/classes/core/ZBase.php

sed -r -i.bak 's,^(\s*\$configFile =).*CONFIG_FILE_PATH.*,\1 CConfigFile::CONFIG_FILE_PATH;,' \
	ui/include/classes/core/ZBase.php

cp -r %{SOURCE1}/ ./

# remove font file
rm -f ui/assets/fonts/DejaVuSans.ttf

# replace font in defines.inc.php
sed -i -r "s/(define\(.*_FONT_NAME.*)DejaVuSans/\1graphfont/" \
	ui/include/defines.inc.php

# traceroute command path for global script
sed -i -e 's|/usr/bin/traceroute|/bin/traceroute|' database/mysql/data.sql

# copy sql files for server
cat database/mysql/schema.sql > database/mysql/create.sql
cat database/mysql/images.sql >> database/mysql/create.sql
cat database/mysql/data.sql >> database/mysql/create.sql
cat %{SOURCE16} >> database/mysql/create.sql

# copy sql file for proxy
mv database/sqlite3/schema.sql database/sqlite3/proxy.sql

# fix scripts path
sed -i "$NAMESPACE_PATTERN" database/mysql/create.sql

gzip database/mysql/create.sql

cp %{SOURCE19} nginx.conf

%build
build_flags="
	-q
	--enable-dependency-tracking
	--sysconfdir=/etc/zabbix%{namespace}
	--libdir=%{_libdir}/zabbix%{namespace}
	--with-libcurl
	--enable-ipv6
	--with-openssl
	--with-libevent
	--with-libpcre
"

CFLAGS="$RPM_OPT_FLAGS -fPIC -pie -Wl,-z,relro -Wl,-z,now"
# GCC 9
#CFLAGS="-fPIC -pie -g -O3 -m64 -march=westmere -mtune=haswell -feliminate-unused-debug-types -pipe -Wall      \
#-Wp,-D_FORTIFY_SOURCE=2 -fexceptions -fstack-protector --param=ssp-buffer-size=32 -Wformat -Wformat-security  \
#-fasynchronous-unwind-tables -Wp,-D_REENTRANT -ftree-loop-distribute-patterns -Wl,-z -Wl,now -Wl,-z -Wl,relro \
#-fno-semantic-interposition -ffat-lto-objects -fno-trapping-math -Wl,-sort-common -Wl,--enable-new-dtags      \
#-Wa,-mbranches-within-32B-boundaries"

CXXFLAGS="$CFLAGS"

export CFLAGS
export CXXFLAGS

#
# Build proxy with SQLite support
#

%configure $build_flags --with-sqlite3 --enable-proxy
make -s %{?_smp_mflags}

# save binary compiled with SQLite
mv src/zabbix_proxy/zabbix_proxy src/zabbix_proxy/zabbix%{namespace}_proxy_sqlite

#
# Build server with MySQL support and everything else
#

%if 0%{?rhel} >= 8
build_flags="$build_flags --enable-agent"
%endif

%configure $build_flags --with-mysql --enable-server
make -s %{?_smp_mflags}

mv src/zabbix_server/zabbix_server src/zabbix_server/zabbix%{namespace}_server_mysql

touch src/zabbix_server/zabbix%{namespace}_server

cd selinux

# add namespace to selinux modules
%if "%{namespace}" != "%{nil}"
sed -i "s,module zabbix_proxy,module zabbix%{namespace}_proxy,"   zabbix_proxy.te
sed -i "s,module zabbix_server,module zabbix%{namespace}_server," zabbix_server.te
sed -i "s,module zabbix_agent,module zabbix%{namespace}_agent,"   zabbix_agent.te

mv zabbix_proxy.te zabbix%{namespace}_proxy.te
mv zabbix_server.te zabbix%{namespace}_server.te
mv zabbix_agent.te zabbix%{namespace}_agent.te
%endif

make SHARE="%{_datadir}" TARGETS="%{modulenames}"

%install

# set NAMESPACE_PATTERN for install section
%_set_namespace_pattern NAMESPACE_PATTERN ${x}%{namespace}

rm -rf $RPM_BUILD_ROOT

# install
make DESTDIR=$RPM_BUILD_ROOT ALERT_SCRIPTS_PATH=/opt/zabbix%{namespace}/alertscripts EXTERNAL_SCRIPTS_PATH=/opt/zabbix%{namespace}/externalscripts install

# install necessary directories
mkdir -p $RPM_BUILD_ROOT%{_localstatedir}/log/zabbix%{namespace}
mkdir -p $RPM_BUILD_ROOT%{_localstatedir}/log/zabbix%{namespace}/slv
mkdir -p $RPM_BUILD_ROOT%{_localstatedir}/run/zabbix%{namespace}

# install proxy stuff
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/zabbix%{namespace}/zabbix_proxy.d
mv man/zabbix_proxy.man     $RPM_BUILD_ROOT%{_mandir}/man8/zabbix%{namespace}_proxy.8
install -m 0755 -p src/zabbix_proxy/zabbix%{namespace}_proxy_sqlite $RPM_BUILD_ROOT%{_sbindir}/

# install server binaries
install -m 0755 -p src/zabbix_server/zabbix%{namespace}_server_* $RPM_BUILD_ROOT%{_sbindir}/
rm $RPM_BUILD_ROOT%{_sbindir}/zabbix_server
rm $RPM_BUILD_ROOT%{_sysconfdir}/zabbix%{namespace}/zabbix_server.conf

# add namespace prefix to the man pages
%if "%{namespace}" != "%{nil}"
mv $RPM_BUILD_ROOT%{_mandir}/man8/zabbix_server.8 $RPM_BUILD_ROOT%{_mandir}/man8/zabbix%{namespace}_server.8
%endif

# remove unneeded utilities
rm -f $RPM_BUILD_ROOT%{_bindir}/rsm_epp_dec
rm -f $RPM_BUILD_ROOT%{_bindir}/rsm_epp_enc
rm -f $RPM_BUILD_ROOT%{_bindir}/rsm_epp_gen
rm -f $RPM_BUILD_ROOT%{_bindir}/t_rsm_dns
rm -f $RPM_BUILD_ROOT%{_bindir}/t_rsm_rdds
rm -f $RPM_BUILD_ROOT%{_bindir}/t_rsm_rdap
rm -f $RPM_BUILD_ROOT%{_libdir}/debug/%{_bindir}/rsm_epp_*.debug
rm -f $RPM_BUILD_ROOT%{_libdir}/debug/%{_bindir}/t_rsm_*.debug

# install frontend files
find ui -name '*.orig' | xargs rm -f
install -d $RPM_BUILD_ROOT%{_datadir}
install -d $RPM_BUILD_ROOT%{_datadir}/zabbix
cp -a ui/* $RPM_BUILD_ROOT%{_datadir}/zabbix/

chmod -x opt/zabbix/scripts/CSlaReport.php

cp opt/zabbix/scripts/CSlaReport.php $RPM_BUILD_ROOT%{_datadir}/zabbix/include/classes/services/CSlaReport.php

# since CentOS 8 this directory belongs to php-fpm
%if 0%{?rhel} < 8
mkdir -p $RPM_BUILD_ROOT%{_sharedstatedir}/php/session
%endif

# install frontend configuration files
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/zabbix/web
touch $RPM_BUILD_ROOT%{_sysconfdir}/zabbix/web/zabbix.conf.php
mv $RPM_BUILD_ROOT%{_datadir}/zabbix/conf/maintenance.inc.php $RPM_BUILD_ROOT%{_sysconfdir}/zabbix/web/

# drop config files in place
install -Dm 0644 -p %{SOURCE17} $RPM_BUILD_ROOT%{_sysconfdir}/nginx/conf.d/zbx_vhost.conf
%if 0%{?rhel} >= 8
install -Dm 0644 -p %{SOURCE18} $RPM_BUILD_ROOT%{_sysconfdir}/php-fpm.d/zabbix.conf
%else
# CentOS 7 specifics start
install -Dm 0644 -p %{SOURCE18} $RPM_BUILD_ROOT%{_sysconfdir}/opt/rh/rh-php73/php-fpm.d/zabbix.conf
# CentOS 7 specifics end
%endif

# rename configuration directories
mv $RPM_BUILD_ROOT%{_sysconfdir}/zabbix%{namespace}/zabbix_server.conf.d $RPM_BUILD_ROOT%{_sysconfdir}/zabbix%{namespace}/zabbix_server.d

%if 0%{?rhel} >= 8
mv $RPM_BUILD_ROOT%{_sysconfdir}/zabbix%{namespace}/zabbix_agentd.conf.d $RPM_BUILD_ROOT%{_sysconfdir}/zabbix%{namespace}/zabbix_agentd.d

install -dm 755 $RPM_BUILD_ROOT%{_docdir}/zabbix-agent-%{version}
install -m 0644 conf/zabbix_agentd/userparameter_mysql.conf    $RPM_BUILD_ROOT%{_sysconfdir}/zabbix%{namespace}/zabbix_agentd.d
install -m 0644 conf/zabbix_agentd/userparameter_examples.conf $RPM_BUILD_ROOT%{_docdir}/zabbix-agent-%{version}

sed -i "$NAMESPACE_PATTERN" conf/zabbix_agentd.conf
%endif

# install scripts
install -d $RPM_BUILD_ROOT/opt/zabbix%{namespace}
cp -r opt/zabbix/* $RPM_BUILD_ROOT/opt/zabbix%{namespace}/

# install probe-scripts
cp -r probe-scripts $RPM_BUILD_ROOT/opt/zabbix%{namespace}/

# directory for proxy package
install -d $RPM_BUILD_ROOT%{_libdir}/zabbix%{namespace}/externalscripts

# fix namespace
sed -i "$NAMESPACE_PATTERN" $(find $RPM_BUILD_ROOT/opt/zabbix%{namespace} -type f -name '*.pl' -o -name '*.pm' -o -name '*.php' -o -name '*.sh')

# install rsyslog configuration file
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/rsyslog.d
cp %{SOURCE20}              $RPM_BUILD_ROOT%{_sysconfdir}/rsyslog.d/rsm%{namespace}.slv.conf
sed -i "$NAMESPACE_PATTERN" $RPM_BUILD_ROOT%{_sysconfdir}/rsyslog.d/*.conf

# in addition, we need to rename rsyslog template names because of the namespace
sed -i "s/RSM/RSM%{namespace}/g" $RPM_BUILD_ROOT%{_sysconfdir}/rsyslog.d/*.conf
sed -i -r "s/rsm\.slv\./rsm%{namespace}.slv./g;s/rsm\.probe\./rsm%{namespace}.probe./g" $RPM_BUILD_ROOT%{_sysconfdir}/rsyslog.d/*.conf

# and rsyslog ident
sed -i -r "s/^(use constant.*ZABBIX_NAMESPACE.*=>).*/\1 '%{namespace}';/" $RPM_BUILD_ROOT/opt/zabbix%{namespace}/scripts/RSMSLV.pm

# install zabbix configuration files
cp %{SOURCE21}              $RPM_BUILD_ROOT%{_sysconfdir}/zabbix%{namespace}/zabbix_server.conf
cp %{SOURCE22}              $RPM_BUILD_ROOT%{_sysconfdir}/zabbix%{namespace}/zabbix_proxy_common.conf
cp %{SOURCE23}              $RPM_BUILD_ROOT%{_sysconfdir}/zabbix%{namespace}/zabbix_proxy_N.conf
sed -i "$NAMESPACE_PATTERN" $RPM_BUILD_ROOT%{_sysconfdir}/zabbix%{namespace}/*.conf

# install logrotate configuration files
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/logrotate.d
cat %{SOURCE3} | sed \
	-e 's|COMPONENT|server|g' \
	>                   $RPM_BUILD_ROOT%{_sysconfdir}/logrotate.d/zabbix%{namespace}-server
cat %{SOURCE3} | sed \
	-e 's|COMPONENT|proxy*|g' \
	>                   $RPM_BUILD_ROOT%{_sysconfdir}/logrotate.d/zabbix%{namespace}-proxy
sed -i "$NAMESPACE_PATTERN" $RPM_BUILD_ROOT%{_sysconfdir}/logrotate.d/*

%if 0%{?rhel} >= 8
cat %{SOURCE3} | sed \
	-e 's|COMPONENT|agentd|g' \
	> $RPM_BUILD_ROOT%{_sysconfdir}/logrotate.d/zabbix-agent
%endif

# install startup scripts
install -Dm 0644 -p %{SOURCE11} $RPM_BUILD_ROOT%{_unitdir}/zabbix%{namespace}-server.service
install -Dm 0644 -p %{SOURCE12} $RPM_BUILD_ROOT%{_unitdir}/zabbix%{namespace}-proxy.service
%if 0%{?rhel} >= 8
install -Dm 0644 -p %{SOURCE10} $RPM_BUILD_ROOT%{_unitdir}/zabbix-agent.service
%endif
sed -i "$NAMESPACE_PATTERN"     $RPM_BUILD_ROOT%{_unitdir}/*

# install systemd-tmpfiles conf
install -Dm 0644 -p %{SOURCE15} $RPM_BUILD_ROOT%{_prefix}/lib/tmpfiles.d/zabbix%{namespace}-server.conf
install -Dm 0644 -p %{SOURCE15} $RPM_BUILD_ROOT%{_prefix}/lib/tmpfiles.d/zabbix%{namespace}-proxy.conf
%if 0%{?rhel} >= 8
install -Dm 0644 -p %{SOURCE15} $RPM_BUILD_ROOT%{_prefix}/lib/tmpfiles.d/zabbix-agent.conf
%endif
sed -i "$NAMESPACE_PATTERN"     $RPM_BUILD_ROOT%{_prefix}/lib/tmpfiles.d/*

# Install SELinux policy modules
%_format MODULES selinux/$x.pp.bz2

install -d $RPM_BUILD_ROOT%{_datadir}/selinux/packages
install -m 0644 $MODULES \
    $RPM_BUILD_ROOT%{_datadir}/selinux/packages

# RSM API
mv rsm-api $RPM_BUILD_ROOT%{_datadir}/rsm-api

%clean
rm -rf $RPM_BUILD_ROOT

%if 0%{?rhel} >= 8
%pre agent
getent group zabbix > /dev/null || groupadd -r zabbix
getent passwd zabbix > /dev/null || \
    useradd -r -g zabbix -d %{_localstatedir}/lib/zabbix -s /sbin/nologin \
	    -c "Zabbix Monitoring System" zabbix
:

%post agent
%systemd_post zabbix-agent.service

%preun agent
if [ "$1" = 0 ]; then
    %systemd_preun zabbix-agent.service
fi
:

%postun agent
%systemd_postun_with_restart zabbix-agent.service

%files agent
%defattr(-,root,root,-)
%doc AUTHORS ChangeLog COPYING NEWS README
%config(noreplace) %{_sysconfdir}/zabbix%{namespace}/zabbix_agentd.conf
%config(noreplace) %{_sysconfdir}/logrotate.d/zabbix-agent
%dir %{_sysconfdir}/zabbix%{namespace}/zabbix_agentd.d
%config(noreplace) %{_sysconfdir}/zabbix%{namespace}/zabbix_agentd.d/userparameter_mysql.conf
%doc %{_docdir}/zabbix-agent-%{version}/userparameter_examples.conf
%attr(0755,zabbix,zabbix) %dir %{_localstatedir}/log/zabbix%{namespace}
%attr(0755,zabbix,zabbix) %dir %{_localstatedir}/run/zabbix%{namespace}
%{_sbindir}/zabbix_agentd
%{_mandir}/man8/zabbix_agentd.8*
%{_unitdir}/zabbix-agent.service
%{_prefix}/lib/tmpfiles.d/zabbix-agent.conf

%files get
%defattr(-,root,root,-)
%doc AUTHORS ChangeLog COPYING NEWS README
%{_bindir}/zabbix_get
%{_mandir}/man1/zabbix_get.1*

%files sender
%defattr(-,root,root,-)
%doc AUTHORS ChangeLog COPYING NEWS README
%{_bindir}/zabbix_sender
%{_mandir}/man1/zabbix_sender.1*
%endif

%pre proxy-sqlite
getent group zabbix > /dev/null || groupadd -r zabbix
getent passwd zabbix > /dev/null || \
	useradd -r -g zabbix -d %{_localstatedir}/lib/zabbix%{namespace} -s /sbin/nologin \
	-c "Zabbix Monitoring System" zabbix
:

%pre server-mysql
getent group zabbix > /dev/null || groupadd -r zabbix
getent passwd zabbix > /dev/null || \
	useradd -r -g zabbix -d %{_localstatedir}/lib/zabbix%{namespace} -s /sbin/nologin \
	-c "Zabbix Monitoring System" zabbix
mkdir -p %{_localstatedir}/lib/zabbix
chown -R zabbix:zabbix %{_localstatedir}/lib/zabbix
:

%pre scripts
getent group zabbix > /dev/null || groupadd -r zabbix
getent passwd zabbix > /dev/null || \
	useradd -r -g zabbix -d %{_localstatedir}/lib/zabbix -s /sbin/nologin \
	-c "Zabbix Monitoring System" zabbix
:

%post proxy-sqlite
%systemd_post zabbix%{namespace}-proxy.service
/usr/sbin/update-alternatives --install %{_sbindir}/zabbix%{namespace}_proxy \
	zabbix%{namespace}-proxy %{_sbindir}/zabbix%{namespace}_proxy_sqlite 10
:

%post proxy-sqlite-selinux
%{_sbindir}/semodule -n -s %{selinuxtype} -i %{_datadir}/selinux/packages/zabbix%{namespace}_agent.pp.bz2
%{_sbindir}/semodule -n -s %{selinuxtype} -i %{_datadir}/selinux/packages/zabbix%{namespace}_proxy.pp.bz2
if %{_sbindir}/selinuxenabled ; then
    %{_sbindir}/load_policy
    %relabel_files
fi

%post server-mysql
%systemd_post zabbix%{namespace}-server.service
/usr/sbin/update-alternatives --install %{_sbindir}/zabbix%{namespace}_server \
	zabbix%{namespace}-server %{_sbindir}/zabbix%{namespace}_server_mysql 10
:

%post server-mysql-selinux
%{_sbindir}/semodule -n -s %{selinuxtype} -i %{_datadir}/selinux/packages/zabbix%{namespace}_agent.pp.bz2
%{_sbindir}/semodule -n -s %{selinuxtype} -i %{_datadir}/selinux/packages/zabbix%{namespace}_server.pp.bz2
if %{_sbindir}/selinuxenabled ; then
    %{_sbindir}/load_policy
    %relabel_files
fi

%post web
# The fonts directory was moved into assets subdirectory at one point.
#
# This broke invocation of update-alternatives command below, because the target link for zabbix-web-font changed
# from zabbix/fonts/graphfont.ttf to zabbix/assets/fonts/graphfont.ttf
#
# We handle this movement by deleting /var/lib/alternatives/zabbix-web-font file if it contains the old target link.
# We also remove symlink at zabbix/fonts/graphfont.ttf to have the old fonts directory be deleted during update.
if [ -f /var/lib/alternatives/zabbix-web-font ] && \
	   [ -z "$(grep %{_datadir}/zabbix/assets/fonts/graphfont.ttf /var/lib/alternatives/zabbix-web-font)" ]
then
	rm /var/lib/alternatives/zabbix-web-font
	if [ -h %{_datadir}/zabbix/fonts/graphfont.ttf ]; then
		rm %{_datadir}/zabbix/fonts/graphfont.ttf
	fi
fi

/usr/sbin/update-alternatives --install %{_datadir}/zabbix/assets/fonts/graphfont.ttf \
	zabbix-web-font %{_datadir}/fonts/dejavu/DejaVuSans.ttf 10
:

%post web-mysql-selinux
%{_sbindir}/semodule -n -s %{selinuxtype} -i %{_datadir}/selinux/packages/zabbix%{namespace}_agent.pp.bz2
%{_sbindir}/semodule -n -s %{selinuxtype} -i %{_datadir}/selinux/packages/zbx_nginx.pp.bz2
%{_sbindir}/semodule -n -s %{selinuxtype} -i %{_datadir}/selinux/packages/zbx_php-fpm.pp.bz2
if %{_sbindir}/selinuxenabled ; then
    %{_sbindir}/load_policy
    %relabel_files
fi

%post agent-selinux
%{_sbindir}/semodule -n -s %{selinuxtype} -i %{_datadir}/selinux/packages/zabbix%{namespace}_agent.pp.bz2

if %{_sbindir}/selinuxenabled ; then
    %{_sbindir}/load_policy
    %relabel_files
fi

%post scripts
# TODO: remove in the future, this was renamed to rsm50.slv.conf
rm -f /etc/rsyslog.d/zabbix50-rsm.slv.conf*
systemctl restart rsyslog

%preun proxy-sqlite
if [ "$1" = 0 ]; then
%systemd_preun zabbix-proxy.service
/usr/sbin/update-alternatives --remove zabbix%{namespace}-proxy \
%{_sbindir}/zabbix%{namespace}_proxy_sqlite
fi
:

%preun server-mysql
if [ "$1" = 0 ]; then
%systemd_preun zabbix%{namespace}-server.service
/usr/sbin/update-alternatives --remove zabbix%{namespace}-server \
	%{_sbindir}/zabbix%{namespace}_server_mysql
fi
:

%preun web
if [ "$1" = 0 ]; then
/usr/sbin/update-alternatives --remove zabbix-web-font \
	%{_datadir}/fonts/dejavu/DejaVuSans.ttf
fi
:

%postun proxy-sqlite
%systemd_postun_with_restart zabbix%{namespace}-proxy.service

%postun proxy-sqlite-selinux
if [ $1 -eq 0 ]; then
    %{_sbindir}/semodule -n -r zabbix-proxy &> /dev/null || :
    %{_sbindir}/semodule -n -r zabbix-agent &> /dev/null || :
    if %{_sbindir}/selinuxenabled ; then
	%{_sbindir}/load_policy
	%relabel_files
    fi
fi

%postun server-mysql
%systemd_postun_with_restart zabbix%{namespace}-server.service

%postun server-mysql-selinux
if [ $1 -eq 0 ]; then
    %{_sbindir}/semodule -n -r zabbix-server &> /dev/null || :
    %{_sbindir}/semodule -n -r zabbix-agent &> /dev/null || :
    if %{_sbindir}/selinuxenabled ; then
	%{_sbindir}/load_policy
	%relabel_files
    fi
fi

%postun web-mysql-selinux
if [ $1 -eq 0 ]; then
    %{_sbindir}/semodule -n -r zabbix-agent &> /dev/null || :
    %{_sbindir}/semodule -n -r zbx_nginx &> /dev/null || :
    %{_sbindir}/semodule -n -r zbx_php-fpm &> /dev/null || :
    if %{_sbindir}/selinuxenabled ; then
	%{_sbindir}/load_policy
	%relabel_files
    fi
fi

%postun agent-selinux
if [ $1 -eq 0 ]; then
    %{_sbindir}/semodule -n -r zabbix-agent &> /dev/null || :
    if %{_sbindir}/selinuxenabled ; then
	%{_sbindir}/load_policy
	%relabel_files
    fi
fi

%postun scripts
systemctl restart rsyslog

%files proxy-sqlite
%defattr(-,root,root,-)
%doc AUTHORS ChangeLog COPYING NEWS README
%doc database/sqlite3/proxy.sql
%attr(0640,root,zabbix) %config(noreplace) %{_sysconfdir}/zabbix%{namespace}/zabbix_proxy_common.conf
%attr(0640,root,zabbix) %config(noreplace) %{_sysconfdir}/zabbix%{namespace}/zabbix_proxy_N.conf
%dir %{_libdir}/zabbix%{namespace}/externalscripts
%{_sysconfdir}/logrotate.d/zabbix%{namespace}-proxy
%attr(0755,zabbix,zabbix) %dir %{_localstatedir}/log/zabbix%{namespace}
%attr(0755,zabbix,zabbix) %dir %{_localstatedir}/run/zabbix%{namespace}
%{_mandir}/man8/zabbix%{namespace}_proxy.8*
%{_unitdir}/zabbix%{namespace}-proxy.service
%{_prefix}/lib/tmpfiles.d/zabbix%{namespace}-proxy.conf
%{_sbindir}/zabbix%{namespace}_proxy_sqlite

%files proxy-sqlite-selinux
%defattr(-,root,root,0755)
%attr(0644,root,root) %{_datadir}/selinux/packages/zabbix%{namespace}_proxy.pp.bz2
%attr(0644,root,root) %{_datadir}/selinux/packages/zabbix%{namespace}_agent.pp.bz2

%files server-mysql
%defattr(-,root,root,-)
%doc AUTHORS ChangeLog COPYING NEWS README
%doc database/mysql/create.sql.gz
%attr(0640,root,zabbix) %config(noreplace) %{_sysconfdir}/zabbix%{namespace}/zabbix_server.conf
%dir /opt/zabbix%{namespace}/alertscripts
%dir /opt/zabbix%{namespace}/externalscripts
/opt/zabbix%{namespace}/externalscripts/*
%{_sysconfdir}/logrotate.d/zabbix%{namespace}-server
%attr(0755,zabbix,zabbix) %dir %{_localstatedir}/log/zabbix%{namespace}
%attr(0755,zabbix,zabbix) %dir %{_localstatedir}/log/zabbix%{namespace}/slv
%attr(0755,zabbix,zabbix) %dir %{_localstatedir}/run/zabbix%{namespace}
%{_mandir}/man8/zabbix%{namespace}_server.8*
%{_unitdir}/zabbix%{namespace}-server.service
%{_prefix}/lib/tmpfiles.d/zabbix%{namespace}-server.conf
%{_sbindir}/zabbix%{namespace}_server_mysql

%files server-mysql-selinux
%defattr(-,root,root,0755)
%attr(0644,root,root) %{_datadir}/selinux/packages/zabbix%{namespace}_server.pp.bz2
%attr(0644,root,root) %{_datadir}/selinux/packages/zabbix%{namespace}_agent.pp.bz2

%files web
%defattr(-,root,root,-)
%doc AUTHORS ChangeLog COPYING NEWS README
%doc nginx.conf
%dir %attr(0750,nginx,nginx) %{_sysconfdir}/zabbix/web
# since CentOS 8 this directory belongs to php-fpm
%if 0%{?rhel} < 8
%dir %attr(0770,root,nginx) %{_sharedstatedir}/php/session
%endif
%ghost %attr(0644,nginx,nginx) %config(noreplace) %{_sysconfdir}/zabbix/web/zabbix.conf.php
%config(noreplace) %{_sysconfdir}/zabbix/web/maintenance.inc.php
%config(noreplace) %{_sysconfdir}/nginx/conf.d/zbx_vhost.conf
%if 0%{?rhel} >= 8
%config(noreplace) %{_sysconfdir}/php-fpm.d/zabbix.conf
%else
# CentOS 7 specifics start
%config(noreplace) %{_sysconfdir}/opt/rh/rh-php73/php-fpm.d/zabbix.conf
# CentOS 7 specifics end
%endif
%{_datadir}/zabbix

%files web-mysql
%defattr(-,root,root,-)

%files web-mysql-selinux
%defattr(-,root,root,0755)
%attr(0644,root,root) %{_datadir}/selinux/packages/zabbix%{namespace}_agent.pp.bz2
%attr(0644,root,root) %{_datadir}/selinux/packages/zbx_nginx.pp.bz2
%attr(0644,root,root) %{_datadir}/selinux/packages/zbx_php-fpm.pp.bz2

%files agent-selinux
%defattr(-,root,root,0755)
%attr(0644,root,root) %{_datadir}/selinux/packages/zabbix%{namespace}_agent.pp.bz2

%files scripts
%defattr(-,zabbix,zabbix,0755)
%dir /opt/zabbix%{namespace}/scripts
%dir /opt/zabbix%{namespace}/data
%dir /opt/zabbix%{namespace}/mtr
/opt/zabbix%{namespace}/scripts/*
%defattr(-,root,root,0755)
%{_sysconfdir}/rsyslog.d/rsm%{namespace}.slv.conf
%attr(0755,zabbix,zabbix) %dir %{_localstatedir}/log/zabbix%{namespace}
%attr(0755,zabbix,zabbix) %dir %{_localstatedir}/log/zabbix%{namespace}/slv

%files js
%defattr(-,root,root,-)
%doc AUTHORS ChangeLog COPYING NEWS README
%{_bindir}/zabbix_js

%files rsm-api
%defattr(-,root,root,-)
%{_datadir}/rsm-api/config.php.example
%{_datadir}/rsm-api/Database.php
%{_datadir}/rsm-api/Input.php
%{_datadir}/rsm-api/RsmException.php
%{_datadir}/rsm-api/User.php
%{_datadir}/rsm-api/constants.php
%{_datadir}/rsm-api/index.php
%{_datadir}/rsm-api/example

%files probe-scripts
%dir /opt/zabbix%{namespace}/probe-scripts
/opt/zabbix%{namespace}/probe-scripts/*


%changelog
* Wed Dec 21 2016 Alexey Pustovalov <alexey.pustovalov@zabbix.com> - 3.0.7-1-rsm
- update to RSM version

* Wed Dec 21 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.7-1
- update to 3.0.7

* Thu Dec 08 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.6-1
- update to 3.0.6

* Sun Oct 02 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.5-1
- update to 3.0.5
- use zabbix user and group for Java Gateway
- add SuccessExitStatus=143 for Java Gateway servie file

* Sun Jul 24 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.4-1
- update to 3.0.4

* Sun May 22 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.3-1
- update to 3.0.3
- fix java gateway systemd script to use java options

* Wed Apr 20 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.2-1
- update to 3.0.2
- remove ZBX-10459.patch

* Sat Apr 02 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.1-2
- fix proxy packges doesn't have schema.sql.gz
- add server and web packages for RHEL6
- add ZBX-10459.patch

* Sun Feb 28 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.1-1
- update to 3.0.1
- remove DBSocker parameter

* Sat Feb 20 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.0-2
- agent, proxy and java-gateway for RHEL 5 and 6

* Mon Feb 15 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.0-1
- update to 3.0.0

* Thu Feb 11 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.0rc2
- update to 3.0.0rc2
- add TIMEOUT parameter for java gateway conf

* Thu Feb 04 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.0rc1
- update to 3.0.0rc1

* Sat Jan 30 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.0beta2
- update to 3.0.0beta2

* Thu Jan 21 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.0beta1
- update to 3.0.0beta1

* Thu Jan 14 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.0alpha6
- update to 3.0.0alpla6
- remove zabbix_agent conf and binary

* Wed Jan 13 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.0alpha5
- update to 3.0.0alpha5

* Fri Nov 13 2015 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.0alpha4-1
- update to 3.0.0alpha4

* Thu Oct 29 2015 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.0alpha3-2
- fix web-pgsql package dependency
- add --with-openssl option

* Mon Oct 19 2015 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.0alpha3-1
- update to 3.0.0alpha3

* Tue Sep 29 2015 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.0alpha2-3
- add IfModule for mod_php5 in apache configuration file
- fix missing proxy_mysql alternatives symlink
- chagne snmptrap log filename
- remove include dir from server and proxy conf

* Fri Sep 18 2015 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.0alpha2-2
- fix create.sql doesn't contain schema.sql & images.sql

* Tue Sep 15 2015 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.0alpha2-1
- update to 3.0.0alpha2

* Sat Aug 22 2015 Kodai Terashima <kodai.terashima@zabbix.com> - 2.5.0-1
- create spec file from scratch
- update to 2.5.0
