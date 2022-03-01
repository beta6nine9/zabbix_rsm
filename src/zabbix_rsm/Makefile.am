## Process this file with automake to produce Makefile.in

bin_PROGRAMS = rsm_epp_gen rsm_epp_enc rsm_epp_dec

rsm_epp_gen_SOURCES = gen.c missingconf.h
rsm_epp_enc_SOURCES = enc.c missingconf.h
rsm_epp_dec_SOURCES = dec.c missingconf.h

common_ldflags = \
	-Wl,--start-group \
	$(top_srcdir)/src/libs/zbxrsm/libzbxrsm.a \
	$(top_srcdir)/src/libs/zbxcommon/libzbxcommon.a \
	$(top_srcdir)/src/libs/zbxcomms/libzbxcomms.a \
	$(top_srcdir)/src/libs/zbxcrypto/libzbxcrypto.a \
	$(top_srcdir)/src/libs/zbxconf/libzbxconf.a \
	$(top_srcdir)/src/libs/zbxalgo/libzbxalgo.a \
	$(top_srcdir)/src/libs/zbxlog/libzbxlog.a \
	$(top_srcdir)/src/libs/zbxsys/libzbxsys.a \
	$(top_srcdir)/src/libs/zbxnix/libzbxnix.a \
	$(top_srcdir)/src/libs/zbxcompress/libzbxcompress.a

LIBS = -lssl -lcrypto -lpthread -lz -Wl,--end-group

rsm_epp_gen_LDFLAGS = $(common_ldflags)
rsm_epp_enc_LDFLAGS = $(common_ldflags)
rsm_epp_dec_LDFLAGS = $(common_ldflags)
