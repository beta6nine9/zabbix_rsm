#!/bin/bash

RSM_VERSION="rsm1.3.2"	# MAJOR.PROD.QA
RPMDIR="rpmbuild"
SRV_VERSION_FILE="include/version.h"
FE_VERSION_FILE="frontends/php/include/defines.inc.php"
AC_VERSION_FILE="configure.ac"
SPEC="$RPMDIR/SPECS/zabbix.spec"
FAILURE=1
SUCCESS=0

usage()
{
	[ -n "$1" ] && echo "$*"

	echo "usage: $0 [-f] [-c] [-r] [-h]"
	echo "       -f|--force      force all compilation steps"
	echo "       -c|--clean      clean all previously generated files"
	echo "       -r|--restore    restore the versions and exit"
	echo "       -h|--help       print this help message"

	exit $FAILURE
}

msg()
{
	echo "BUILD-RPMS $*"
}

restore_bak_files()
{
	for i in $SRV_VERSION_FILE $FE_VERSION_FILE $AC_VERSION_FILE $SPEC; do
		[ -f $i.rpmbak ] && mv $i.rpmbak $i
	done
}

remove_bak_files()
{
	for i in $SRV_VERSION_FILE $FE_VERSION_FILE $AC_VERSION_FILE $SPEC; do
		[ -f $i.rpmbak ] && rm $i.rpmbak
	done
}

fail()
{
	[ -n "$1" ] && echo "$*"

	restore_bak_files

	exit $FAILURE
}

OPT_FORCE=0
OPT_CLEAN=0
while [ -n "$1" ]; do
	case "$1" in
		-f|--force)
			OPT_FORCE=1
			;;
		-c|--clean)
			OPT_CLEAN=1
			;;
		-r|--restore)
			restore_bak_files
			exit $SUCCESS
			;;
		-h|--help)
			usage
			;;
		-*)
			usage "unknown option: \"$1\""
			;;
		*)
			usage
			;;
	esac
	shift
done

[ ! -f $SPEC ] && echo "Error: spec file \"$SPEC\" not found" && fail
[ ! -f $SRV_VERSION_FILE ] && echo "Error: server file \"$SRV_VERSION_FILE\" not found" && fail
[ ! -f $FE_VERSION_FILE ] && echo "Error: frontend file \"$FE_VERSION_FILE\" not found" && fail
[ ! -f $AC_VERSION_FILE ] && echo "Error: autoconf file \"$AC_VERSION_FILE\" not found" && fail

remove_bak_files

if [[ $OPT_CLEAN -eq 1 ]]; then
	msg "cleaning up"
	make -s clean > /dev/null 2>&1
	make -s distclean > /dev/null 2>&1
	for i in RPMS SRPMS BUILD BUILDROOT; do
		rm -rf $RPMDIR/$i || fail
	done
fi

msg "setting server version ($RSM_VERSION)"
sed -i.rpmbak -r "s/(ZBX_STR\(ZABBIX_VERSION_PATCH\).*ZABBIX_VERSION_RC)/\1 \"$RSM_VERSION\"/" $SRV_VERSION_FILE || fail

msg "setting frontend version ($RSM_VERSION)"
sed -i.rpmbak -r "s/(ZABBIX_VERSION',\s+'[0-9\.]+)'.*/\1$RSM_VERSION');/" $FE_VERSION_FILE || fail

msg "setting version for autoconf ($RSM_VERSION)"
sed -i.rpmbak -r "s/^(AC_INIT\(\[Zabbix\],\[[0-9\.]+)\]\)/\1$RSM_VERSION])/;s/^AM_INIT_AUTOMAKE.*$/AM_INIT_AUTOMAKE([1.9 tar-pax])/" $AC_VERSION_FILE || fail

rsmversion=$(echo $RSM_VERSION | sed -r 's/^(rsm[1-9][0-9]*\.[0-9]+\.[0-9]+).*/\1/')
rsmprereleasetag=$(echo $RSM_VERSION | sed -r 's/^rsm[1-9][0-9]*\.[0-9]+\.[0-9]+//')

version_for_msg="$rsmversion"
[ -n "$rsmprereleasetag" ] && version_for_msg="$version_for_msg, pre-release tag: $rsmprereleasetag"

msg "setting version for rpm ($version_for_msg)"
sed -i.rpmbak -r "s/(^Version:\s+[0-9\.]+)$/\1$rsmversion/" $SPEC || fail

if [[ $OPT_FORCE -eq 1 || ! -f configure ]]; then
	msg "running ./bootstrap.sh"
	./bootstrap.sh > /dev/null || fail
fi

if [[ $OPT_FORCE -eq 1 || ! -f Makefile ]]; then
	msg "running ./configure"
	./configure > /dev/null || fail
fi

make -s dbschema > /dev/null || fail

if [[ $OPT_FORCE -eq 1 ]] || ! ls zabbix-*.tar.gz > /dev/null 2>&1; then
	msg "making dist"
	make -s dist > /dev/null || fail
fi

mv zabbix-*.tar.gz $RPMDIR/SOURCES/ || fail

if [[ -x /usr/bin/yum-builddep ]]; then
	msg "installing build dependencies"
	/usr/bin/yum-builddep --assumeyes --quiet $SPEC > /tmp/yum-builddep.log || (cat /tmp/yum-builddep.log && fail)
fi

msg "building RPMs, this can take a while"
if [ -z "$rsmprereleasetag" ]; then
	rpmbuild -ba $SPEC --quiet --define "_topdir ${PWD}/$RPMDIR" --define "rsmversion $rsmversion" >/dev/null || fail
else
	rpmbuild -ba $SPEC --quiet --define "_topdir ${PWD}/$RPMDIR" --define "rsmversion $rsmversion" --define "rsmprereleasetag $rsmprereleasetag" >/dev/null || fail
fi

msg "RPM files are available in $RPMDIR/RPMS/x86_64 and $RPMDIR/RPMS/noarch"

restore_bak_files

exit $SUCCESS
