TARGETS?=zbx_nginx zabbix_server5 zabbix_proxy5 zabbix_agent zbx_php-fpm
MODULES?=${TARGETS:=.pp.bz2}

all: ${TARGETS:=.pp.bz2}

%.pp.bz2: %.pp
	@echo Compressing $^ -\ $@
	bzip2 -9 $^

%.pp: %.te
	make -f /usr/share/selinux/devel/Makefile $@

clean:
	rm -f *~ *.tc *.pp *.pp.bz2
	rm -rf tmp
