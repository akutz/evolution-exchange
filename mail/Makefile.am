## Process this file with automake to produce Makefile.in
noinst_LTLIBRARIES =						\
	libexchangemail.la

INCLUDES =							\
	-I$(top_srcdir)						\
	-I$(top_builddir)/lib					\
	-DG_LOG_DOMAIN=\"exchange-mail\"			\
	$(EXTRA_GNOME_CFLAGS)					\
	$(BONOBO_GNOME_CFLAGS)					\
	$(MAIL_CFLAGS)						\
        $(LIBEXCHANGE_CFLAGS)					\
	$(LDAP_CFLAGS)						\
	-I$(top_srcdir)/lib					\
	-I$(top_srcdir)/storage					\
	-I$(top_srcdir)/shell					\
	-I$(top_srcdir)/camel

libexchangemail_la_SOURCES =					\
	mail-stub.c						\
	mail-stub.h						\
	mail-stub-listener.c					\
	mail-stub-listener.h					\
	mail-stub-exchange.c					\
	mail-stub-exchange.h					\
	mail-utils.c						\
	mail-utils.h
