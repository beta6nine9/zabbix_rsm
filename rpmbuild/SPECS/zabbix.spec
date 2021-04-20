Name:		zabbix
Version:	3.0.10%{rsmversion}
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
Source11:	zabbix-server.service
Source12:	zabbix-proxy.service
Source15:	zabbix-tmpfiles.conf
Source16:	partitioning.sql
Source17:	zbx_vhost.conf
Source18:	zbx_php.conf
Source19:	nginx.conf
Source20:	rsyslog.d-rsm.slv.conf
Source21:	zabbix_server.conf
Source22:	zabbix_proxy_common.conf
Source23:	zabbix_proxy_N.conf
Source24:	zabbix-slv-logrotate
Source25:	cron.d
Patch0:		config.patch
Patch1:		fonts-config.patch
Patch2:		fping3-sourceip-option.patch

Buildroot:	%{_tmppath}/zabbix-%{version}-%{release}-root-%(%{__id_u} -n)

%global selinuxtype	targeted
%global moduletype	services

%global modulenames	zabbix_proxy zabbix_server zabbix_agent zbx_php-fpm zbx_nginx
# Version of distribution SELinux policy package.
%global selinux_policyver	3.13.1-102.el7_3.13

%global _format() export %1=""; for x in %{modulenames}; do %1+=%2; %1+=" "; done;

# Relabel files
%global relabel_files() \ # ADD files in *.fc file

BuildRequires:	mariadb-connector-c-devel
BuildRequires:	ldns-devel >= 1.6.17
BuildRequires:	curl-devel >= 7.13.1
BuildRequires:	openssl-devel >= 1.0.1
BuildRequires:	systemd
BuildRequires:	selinux-policy selinux-policy-devel

%description
Zabbix is the ultimate enterprise-level software designed for
real-time monitoring of millions of metrics collected from tens of
thousands of servers, virtual machines and network devices.

%package proxy-mysql
Summary:			Zabbix proxy for MySQL or MariaDB database
Group:				Applications/Internet
Requires(post):		systemd
Requires(preun):	systemd
Requires(postun):	systemd
Requires:		ldns >= 1.6.17
Provides:		zabbix-proxy = %{version}-%{release}
Provides:		zabbix-proxy-implementation = %{version}-%{release}
Obsoletes:		zabbix
Obsoletes:		zabbix-proxy

%description proxy-mysql
Zabbix proxy with MySQL or MariaDB database support.

%package proxy-mysql-selinux
Summary:		SELinux Policies for Zabbix proxy
Group:			System Environment/Base
Requires(post):		selinux-policy-base >= %{selinux_policyver}, selinux-policy-targeted >= %{selinux_policyver}, policycoreutils, policycoreutils-python-utils libselinux-utils
Requires:		zabbix-proxy = %{version}-%{release}

%description proxy-mysql-selinux
SELinux policy modules for use with Zabbix proxy

%package server-mysql
Summary:			Zabbix server for MySQL or MariaDB database
Group:				Applications/Internet
Requires:			fping
Requires(post):		systemd
Requires(preun):	systemd
Requires(postun):	systemd
Requires:		ldns >= 1.6.17
Requires:		perl-Data-Dumper
Requires:		perl-DBD-MySQL
Requires:		perl-Devel-StackTrace
Provides:		zabbix-server = %{version}-%{release}
Provides:		zabbix-server-implementation = %{version}-%{release}
Obsoletes:		zabbix
Obsoletes:		zabbix-server

%description server-mysql
Zabbix server with MySQL or MariaDB database support.

%package server-mysql-selinux
Summary:		SELinux Policies for Zabbix server
Group:			System Environment/Base
Requires(post):		selinux-policy-base >= %{selinux_policyver}, selinux-policy-targeted >= %{selinux_policyver}, policycoreutils, policycoreutils-python-utils libselinux-utils
Requires:		zabbix-server = %{version}-%{release}

%description server-mysql-selinux
SELinux policy modules for use with Zabbix server


%package web
Summary:			Zabbix web frontend common package
Group:				Application/Internet
BuildArch:			noarch
Requires:			nginx
Requires:			php-fpm >= 5.4
Requires:			php-gd
Requires:			php-bcmath
Requires:			php-mbstring
Requires:			php-xml
Requires:			php-ldap
Requires:			php-json
Requires:			dejavu-sans-fonts
Requires:			zabbix-web-database = %{version}-%{release}
Requires(post):		%{_sbindir}/update-alternatives
Requires(preun):	%{_sbindir}/update-alternatives

%description web
Zabbix web frontend common package

%package web-mysql
Summary:			Zabbix web frontend for MySQL
Group:				Applications/Internet
BuildArch:			noarch
Requires:			php-mysqlnd
Requires:			zabbix-web = %{version}-%{release}
Provides:			zabbix-web-database = %{version}-%{release}

%description web-mysql
Zabbix web frontend for MySQL

%package web-mysql-selinux
Summary:		SELinux Policies for Zabbix web frontend
Group:			System Environment/Base
BuildArch:		noarch
Requires(post):		selinux-policy-base >= %{selinux_policyver}, selinux-policy-targeted >= %{selinux_policyver}, policycoreutils, policycoreutils-python-utils libselinux-utils
Requires:		zabbix-web-mysql = %{version}-%{release}

%description web-mysql-selinux
SELinux policy modules for use with Zabbix web frontend

%package agent-selinux
Summary:		SELinux Policies for Zabbix agent
Group:			System Environment/Base
BuildArch:		noarch
Requires(post):		selinux-policy-base >= %{selinux_policyver}, selinux-policy-targeted >= %{selinux_policyver}, policycoreutils, policycoreutils-python-utils libselinux-utils
Requires:		zabbix-agent

%description agent-selinux
SELinux policy modules for use with Zabbix agent

%package scripts
Summary:			Zabbix scripts for RSM
Group:				Applications/Internet
BuildArch:			noarch
Requires:			perl-Data-Dumper, perl-DBD-MySQL, perl-Sys-Syslog
Requires:			perl-DateTime, perl-Config-Tiny, perl-libwww-perl
Requires:			perl-LWP-Protocol-https, perl-JSON-XS, perl-Expect
Requires:			perl-Redis, perl-DateTime-Format-RFC3339
Requires:			perl-Text-CSV_XS, perl-Types-Serialiser
Requires:			perl-Path-Tiny
Requires:			perl-Parallel-ForkManager
Requires:			perl-Devel-StackTrace
Requires:			php-cli php-pdo php-mysqlnd php-xml php-json
AutoReq:			no

%description scripts
Zabbix scripts for RSM

%prep
%setup0 -q -n zabbix-%{version}%{?rsmprereleasetag:%{rsmprereleasetag}}
%patch0 -p1
%patch1 -p1
%patch2 -p1

cp -r %{SOURCE1}/ ./
cp -r %{SOURCE25}/ ./

# traceroute command path for global script
sed -i -e 's|/usr/bin/traceroute|/bin/traceroute|' database/mysql/data.sql

# copy sql files for servers
cat database/mysql/schema.sql > database/mysql/create.sql
cat database/mysql/images.sql >> database/mysql/create.sql
cat database/mysql/data.sql >> database/mysql/create.sql
cat %{SOURCE16} >> database/mysql/create.sql
gzip database/mysql/create.sql

cp %{SOURCE19} frontends/nginx.conf

# sql files for proxyes
gzip database/mysql/schema.sql

%build
build_flags="
	-q
	--enable-dependency-tracking
	--sysconfdir=/etc/zabbix
	--libdir=%{_libdir}/zabbix
	--with-openssl
	--with-libcurl
	--enable-proxy
	--enable-ipv6
	--with-openssl
	--enable-server
"

CFLAGS="$RPM_OPT_FLAGS -fPIC -pie -Wl,-z,relro -Wl,-z,now"
CXXFLAGS="$RPM_OPT_FLAGS -fPIC -pie -Wl,-z,relro -Wl,-z,now"

export CFLAGS
export CXXFLAGS
%configure $build_flags --with-mysql --enable-dbtls
make -s %{?_smp_mflags}
mv src/zabbix_server/zabbix_server src/zabbix_server/zabbix_server_mysql
mv src/zabbix_proxy/zabbix_proxy src/zabbix_proxy/zabbix_proxy_mysql

touch src/zabbix_server/zabbix_server
touch src/zabbix_proxy/zabbix_proxy

cd selinux && make SHARE="%{_datadir}" TARGETS="%{modulenames}"

%install

rm -rf $RPM_BUILD_ROOT

# install
make DESTDIR=$RPM_BUILD_ROOT install

# install necessary directories
mkdir -p $RPM_BUILD_ROOT%{_localstatedir}/log/zabbix
mkdir -p $RPM_BUILD_ROOT%{_localstatedir}/log/zabbix/slv
mkdir -p $RPM_BUILD_ROOT%{_localstatedir}/run/zabbix

# install server and proxy binaries
install -m 0755 -p src/zabbix_server/zabbix_server_* $RPM_BUILD_ROOT%{_sbindir}/
rm $RPM_BUILD_ROOT%{_sbindir}/zabbix_server
install -m 0755 -p src/zabbix_proxy/zabbix_proxy_* $RPM_BUILD_ROOT%{_sbindir}/
rm $RPM_BUILD_ROOT%{_sbindir}/zabbix_proxy
rm $RPM_BUILD_ROOT%{_sysconfdir}/zabbix/zabbix_proxy.conf

# install scripts and modules directories
mkdir -p $RPM_BUILD_ROOT/usr/lib/zabbix
mv $RPM_BUILD_ROOT%{_datadir}/zabbix/alertscripts $RPM_BUILD_ROOT/usr/lib/zabbix
mv $RPM_BUILD_ROOT%{_datadir}/zabbix/externalscripts $RPM_BUILD_ROOT/usr/lib/zabbix
mkdir $RPM_BUILD_ROOT%{_libdir}/zabbix/modules

# install frontend files
find frontends/php -name '*.orig' | xargs rm -f
cp -a frontends/php/* $RPM_BUILD_ROOT%{_datadir}/zabbix
cp opt/zabbix/scripts/CSlaReport.php $RPM_BUILD_ROOT%{_datadir}/zabbix/include/classes/services/CSlaReport.php
mkdir -p $RPM_BUILD_ROOT%{_sharedstatedir}/php/session

# install frontend configuration files
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/zabbix/web
touch $RPM_BUILD_ROOT%{_sysconfdir}/zabbix/web/zabbix.conf.php
mv $RPM_BUILD_ROOT%{_datadir}/zabbix/conf/maintenance.inc.php $RPM_BUILD_ROOT%{_sysconfdir}/zabbix/web/

# drop config files in place
install -Dm 0644 -p %{SOURCE17} $RPM_BUILD_ROOT%{_sysconfdir}/nginx/conf.d/zbx_vhost.conf
install -Dm 0644 -p %{SOURCE18} $RPM_BUILD_ROOT%{_sysconfdir}/php-fpm.d/zabbix.conf

# install configuration files
mv $RPM_BUILD_ROOT%{_sysconfdir}/zabbix/zabbix_proxy.conf.d $RPM_BUILD_ROOT%{_sysconfdir}/zabbix/zabbix_proxy.d
mv $RPM_BUILD_ROOT%{_sysconfdir}/zabbix/zabbix_server.conf.d $RPM_BUILD_ROOT%{_sysconfdir}/zabbix/zabbix_server.d

mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/rsyslog.d
cp %{SOURCE20} $RPM_BUILD_ROOT%{_sysconfdir}/rsyslog.d/rsm.slv.conf
cp %{SOURCE21} $RPM_BUILD_ROOT%{_sysconfdir}/zabbix/zabbix_server.conf

cp %{SOURCE22} $RPM_BUILD_ROOT%{_sysconfdir}/zabbix/zabbix_proxy_common.conf
cp %{SOURCE23} $RPM_BUILD_ROOT%{_sysconfdir}/zabbix/zabbix_proxy_N.conf

# install logrotate configuration files
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/logrotate.d
cat %{SOURCE3} | sed \
	-e 's|COMPONENT|server|g' \
	> $RPM_BUILD_ROOT%{_sysconfdir}/logrotate.d/zabbix-server
cat %{SOURCE3} | sed \
	-e 's|COMPONENT|proxy*|g' \
	> $RPM_BUILD_ROOT%{_sysconfdir}/logrotate.d/zabbix-proxy

# install startup scripts
install -Dm 0644 -p %{SOURCE11} $RPM_BUILD_ROOT%{_unitdir}/zabbix-server.service
install -Dm 0644 -p %{SOURCE12} $RPM_BUILD_ROOT%{_unitdir}/zabbix-proxy.service

# install systemd-tmpfiles conf
install -Dm 0644 -p %{SOURCE15} $RPM_BUILD_ROOT%{_prefix}/lib/tmpfiles.d/zabbix-server.conf
install -Dm 0644 -p %{SOURCE15} $RPM_BUILD_ROOT%{_prefix}/lib/tmpfiles.d/zabbix-proxy.conf

# Install policy modules
%_format MODULES selinux/$x.pp.bz2
echo $MODULES
install -d $RPM_BUILD_ROOT%{_datadir}/selinux/packages
install -m 0644 $MODULES \
    $RPM_BUILD_ROOT%{_datadir}/selinux/packages

install -d $RPM_BUILD_ROOT/opt/zabbix
install -d $RPM_BUILD_ROOT/opt/zabbix/data
cp -r opt/zabbix/* $RPM_BUILD_ROOT/opt/zabbix/

install -d $RPM_BUILD_ROOT%{_sysconfdir}/cron.d/
cp -r cron.d/* $RPM_BUILD_ROOT%{_sysconfdir}/cron.d/

install -Dm 0644 -p %{SOURCE24} $RPM_BUILD_ROOT%{_sysconfdir}/logrotate.d/zabbix-slv

%clean
rm -rf $RPM_BUILD_ROOT

%pre proxy-mysql
getent group zabbix > /dev/null || groupadd -r zabbix
getent passwd zabbix > /dev/null || \
	useradd -r -g zabbix -d %{_localstatedir}/lib/zabbix -s /sbin/nologin \
	-c "Zabbix Monitoring System" zabbix
:

%pre server-mysql
getent group zabbix > /dev/null || groupadd -r zabbix
getent passwd zabbix > /dev/null || \
	useradd -r -g zabbix -d %{_localstatedir}/lib/zabbix -s /sbin/nologin \
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

%post proxy-mysql
%systemd_post zabbix-proxy.service
/usr/sbin/update-alternatives --install %{_sbindir}/zabbix_proxy \
	zabbix-proxy %{_sbindir}/zabbix_proxy_mysql 10
:

%post proxy-mysql-selinux
%{_sbindir}/semodule -n -s %{selinuxtype} -i %{_datadir}/selinux/packages/zabbix_agent.pp.bz2
%{_sbindir}/semodule -n -s %{selinuxtype} -i %{_datadir}/selinux/packages/zabbix_proxy.pp.bz2
if %{_sbindir}/selinuxenabled ; then
    %{_sbindir}/load_policy
    %relabel_files
fi

%post server-mysql
%systemd_post zabbix-server.service
/usr/sbin/update-alternatives --install %{_sbindir}/zabbix_server \
	zabbix-server %{_sbindir}/zabbix_server_mysql 10
:

%post server-mysql-selinux
%{_sbindir}/semodule -n -s %{selinuxtype} -i %{_datadir}/selinux/packages/zabbix_agent.pp.bz2
%{_sbindir}/semodule -n -s %{selinuxtype} -i %{_datadir}/selinux/packages/zabbix_server.pp.bz2
if %{_sbindir}/selinuxenabled ; then
    %{_sbindir}/load_policy
    %relabel_files
fi

%post web
/usr/sbin/update-alternatives --install %{_datadir}/zabbix/fonts/graphfont.ttf \
	zabbix-web-font %{_datadir}/fonts/dejavu/DejaVuSans.ttf 10
:

%post web-mysql-selinux
%{_sbindir}/semodule -n -s %{selinuxtype} -i %{_datadir}/selinux/packages/zabbix_agent.pp.bz2
%{_sbindir}/semodule -n -s %{selinuxtype} -i %{_datadir}/selinux/packages/zbx_nginx.pp.bz2
%{_sbindir}/semodule -n -s %{selinuxtype} -i %{_datadir}/selinux/packages/zbx_php-fpm.pp.bz2
if %{_sbindir}/selinuxenabled ; then
    %{_sbindir}/load_policy
    %relabel_files
fi

%post agent-selinux
%{_sbindir}/semodule -n -s %{selinuxtype} -i %{_datadir}/selinux/packages/zabbix_agent.pp.bz2
if %{_sbindir}/selinuxenabled ; then
    %{_sbindir}/load_policy
    %relabel_files
fi

%post scripts
systemctl restart rsyslog

%preun proxy-mysql
if [ "$1" = 0 ]; then
%systemd_preun zabbix-proxy.service
/usr/sbin/update-alternatives --remove zabbix-proxy \
%{_sbindir}/zabbix_proxy_mysql
fi
:

%preun server-mysql
if [ "$1" = 0 ]; then
%systemd_preun zabbix-server.service
/usr/sbin/update-alternatives --remove zabbix-server \
	%{_sbindir}/zabbix_server_mysql
fi
:

%preun web
if [ "$1" = 0 ]; then
/usr/sbin/update-alternatives --remove zabbix-web-font \
	%{_datadir}/fonts/dejavu/DejaVuSans.ttf
fi
:

%postun proxy-mysql
%systemd_postun_with_restart zabbix-proxy.service

%postun proxy-mysql-selinux
if [ $1 -eq 0 ]; then
    %{_sbindir}/semodule -n -r zabbix-proxy &> /dev/null || :
    %{_sbindir}/semodule -n -r zabbix-agent &> /dev/null || :
    if %{_sbindir}/selinuxenabled ; then
	%{_sbindir}/load_policy
	%relabel_files
    fi
fi

%postun server-mysql
%systemd_postun_with_restart zabbix-server.service

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

%files proxy-mysql
%defattr(-,root,root,-)
%doc AUTHORS ChangeLog COPYING NEWS README
%doc database/mysql/schema.sql.gz
%attr(0640,root,zabbix) %config(noreplace) %{_sysconfdir}/zabbix/zabbix_proxy_common.conf
%attr(0640,root,zabbix) %config(noreplace) %{_sysconfdir}/zabbix/zabbix_proxy_N.conf
%dir /usr/lib/zabbix/externalscripts
%{_sysconfdir}/logrotate.d/zabbix-proxy
%attr(0755,zabbix,zabbix) %dir %{_localstatedir}/log/zabbix
%attr(0755,zabbix,zabbix) %dir %{_localstatedir}/run/zabbix
%{_mandir}/man8/zabbix_proxy.8*
%{_unitdir}/zabbix-proxy.service
%{_prefix}/lib/tmpfiles.d/zabbix-proxy.conf
%{_sbindir}/zabbix_proxy_mysql

%files proxy-mysql-selinux
%defattr(-,root,root,0755)
%attr(0644,root,root) %{_datadir}/selinux/packages/zabbix_proxy.pp.bz2
%attr(0644,root,root) %{_datadir}/selinux/packages/zabbix_agent.pp.bz2

%files server-mysql
%defattr(-,root,root,-)
%doc AUTHORS ChangeLog COPYING NEWS README
%doc database/mysql/create.sql.gz
%attr(0640,root,zabbix) %config(noreplace) %{_sysconfdir}/zabbix/zabbix_server.conf
%dir /usr/lib/zabbix/alertscripts
%dir /usr/lib/zabbix/externalscripts
%{_sysconfdir}/logrotate.d/zabbix-server
%attr(0755,zabbix,zabbix) %dir %{_localstatedir}/log/zabbix
%attr(0755,zabbix,zabbix) %dir %{_localstatedir}/log/zabbix/slv
%attr(0755,zabbix,zabbix) %dir %{_localstatedir}/run/zabbix
%{_mandir}/man8/zabbix_server.8*
%{_unitdir}/zabbix-server.service
%{_prefix}/lib/tmpfiles.d/zabbix-server.conf
%{_sbindir}/zabbix_server_mysql
%{_bindir}/rsm_epp_dec
%{_bindir}/rsm_epp_enc
%{_bindir}/rsm_epp_gen

%files server-mysql-selinux
%defattr(-,root,root,0755)
%attr(0644,root,root) %{_datadir}/selinux/packages/zabbix_server.pp.bz2
%attr(0644,root,root) %{_datadir}/selinux/packages/zabbix_agent.pp.bz2

%files web
%defattr(-,root,root,-)
%doc AUTHORS ChangeLog COPYING NEWS README
%doc frontends/nginx.conf
%dir %attr(0750,nginx,nginx) %{_sysconfdir}/zabbix/web
%dir %attr(0750,nginx,nginx) %{_sharedstatedir}/php/session
%ghost %attr(0644,nginx,nginx) %config(noreplace) %{_sysconfdir}/zabbix/web/zabbix.conf.php
%config(noreplace) %{_sysconfdir}/zabbix/web/maintenance.inc.php
%config(noreplace) %{_sysconfdir}/nginx/conf.d/zbx_vhost.conf
%config(noreplace) %{_sysconfdir}/php-fpm.d/zabbix.conf
%{_datadir}/zabbix

%files web-mysql
%defattr(-,root,root,-)

%files web-mysql-selinux
%defattr(-,root,root,0755)
%attr(0644,root,root) %{_datadir}/selinux/packages/zabbix_agent.pp.bz2
%attr(0644,root,root) %{_datadir}/selinux/packages/zbx_nginx.pp.bz2
%attr(0644,root,root) %{_datadir}/selinux/packages/zbx_php-fpm.pp.bz2

%files agent-selinux
%defattr(-,root,root,0755)
%attr(0644,root,root) %{_datadir}/selinux/packages/zabbix_agent.pp.bz2

%files scripts
%defattr(-,zabbix,zabbix,0755)
/opt/zabbix/*
%defattr(-,root,root,0755)
/etc/cron.d/*
%{_sysconfdir}/logrotate.d/zabbix-slv
%{_sysconfdir}/rsyslog.d/rsm.slv.conf


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
