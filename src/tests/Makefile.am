## Process this file with automake to produce Makefile.in

bin_PROGRAMS = t_rsm_dns t_rsm_rdds t_rsm_rdap

t_rsm_dns_SOURCES  = t_rsm_decl.h t_rsm.h t_rsm.c t_rsm_dns.c
t_rsm_rdds_SOURCES = t_rsm_decl.h t_rsm.h t_rsm.c t_rsm_rdds.c
t_rsm_rdap_SOURCES = t_rsm_decl.h t_rsm.h t_rsm.c t_rsm_rdap.c

COMMON_OBJS = \
	$(top_srcdir)/src/zabbix_server/poller/libzbxpoller.a \
	$(top_srcdir)/src/libs/zbxcommon/libzbxcommon.a \
	$(top_srcdir)/src/libs/zbxalgo/libzbxalgo.a \
	$(top_srcdir)/src/libs/zbxdbcache/libzbxdbcache.a \
	$(top_srcdir)/src/libs/zbxavailability/libzbxavailability.a \
	$(top_srcdir)/src/libs/zbxvault/libzbxvault.a \
	$(top_srcdir)/src/libs/zbxmodules/libzbxmodules.a \
	$(top_srcdir)/src/libs/zbxhistory/libzbxhistory.a \
	$(top_srcdir)/src/libs/zbxsysinfo/libzbxserversysinfo.a \
	$(top_srcdir)/src/libs/zbxserver/libzbxserver.a \
	$(top_srcdir)/src/libs/zbxtrends/libzbxtrends_baseline.a \
	$(top_srcdir)/src/libs/zbxtrends/libzbxtrends.a \
	$(top_srcdir)/src/libs/zbxlog/libzbxlog.a \
	$(top_srcdir)/src/zabbix_server/libzbxserver.a \
	$(top_srcdir)/src/libs/zbxservice/libzbxservice.a \
	$(top_srcdir)/src/libs/zbxipcservice/libzbxipcservice.a \
	$(top_srcdir)/src/libs/zbxdbhigh/libzbxdbhigh.a \
	$(top_srcdir)/src/libs/zbxaudit/libzbxaudit.a \
	$(top_srcdir)/src/libs/zbxeval/libzbxeval.a \
	$(top_srcdir)/src/libs/zbxconf/libzbxconf.a \
	$(top_srcdir)/src/libs/zbxalgo/libzbxalgo.a \
	$(top_srcdir)/src/libs/zbxjson/libzbxjson.a \
	$(top_srcdir)/src/libs/zbxcomms/libzbxcomms.a \
	$(top_srcdir)/src/libs/zbxcompress/libzbxcompress.a \
	$(top_srcdir)/src/libs/zbxcrypto/libzbxcrypto.a\
	$(top_srcdir)/src/libs/zbxsys/libzbxsys.a \
	$(top_srcdir)/src/libs/zbxalgo/libzbxalgo.a \
	$(top_srcdir)/src/libs/zbxmemory/libzbxmemory.a \
	$(top_srcdir)/src/libs/zbxnix/libzbxnix.a \
	$(top_srcdir)/src/libs/zbxsysinfo/common/libcommonsysinfo.a \
	$(top_srcdir)/src/libs/zbxhttp/libzbxhttp.a \
	$(top_srcdir)/src/libs/zbxsysinfo/simple/libsimplesysinfo.a \
	$(top_srcdir)/src/libs/zbxcomms/libzbxcomms.a \
	$(top_srcdir)/src/libs/zbxcommon/libzbxcommon.a \
	$(top_srcdir)/src/libs/zbxdb/libzbxdb.a \
	$(top_srcdir)/src/libs/zbxexec/libzbxexec.a \
	$(top_srcdir)/src/libs/zbxregexp/libzbxregexp.a \
	$(top_srcdir)/src/libs/zbxcrypto/libzbxcrypto.a

t_rsm_dns_LDADD = $(COMMON_OBJS) @PROXY_LIBS@
t_rsm_dns_LDFLAGS = @PROXY_LDFLAGS@

t_rsm_rdds_LDADD = $(COMMON_OBJS) @PROXY_LIBS@
t_rsm_rdds_LDFLAGS = @PROXY_LDFLAGS@

t_rsm_rdap_LDADD = $(COMMON_OBJS) @PROXY_LIBS@
t_rsm_rdap_LDFLAGS = @PROXY_LDFLAGS@
