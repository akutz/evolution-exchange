## Process this file with automake to produce Makefile.in
noinst_LTLIBRARIES =						\
	libexchangemail.la

INCLUDES =							\
	-I$(top_srcdir)						\
	-I$(top_srcdir)/camel					\
	-DG_LOG_DOMAIN=\"exchange-mail\"			\
	$(EXTRA_GNOME_CFLAGS)					\
	$(BONOBO_GNOME_CFLAGS)					\
	$(MAIL_CFLAGS)						\
	$(LIBEXCHANGE_CFLAGS)					\
	-I$(top_srcdir)/storage  				\
	$(LDAP_CFLAGS)						

libexchangemail_la_SOURCES =					\
	mail-stub.c						\
	mail-stub.h						\
	mail-stub-listener.c					\
	mail-stub-listener.h					\
	mail-stub-exchange.c					\
	mail-stub-exchange.h					\
	mail-utils.c						\
	mail-utils.h
