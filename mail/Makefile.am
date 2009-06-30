## Process this file with automake to produce Makefile.in
noinst_LTLIBRARIES =						\
	libexchangemail.la

AM_CPPFLAGS =							\
	-I$(top_srcdir)						\
	-I$(top_srcdir)/camel					\
	-I$(top_srcdir)/storage  				\
	-DG_LOG_DOMAIN=\"exchange-mail\"			\
	$(EXTRA_GNOME_CFLAGS)					\
	$(BONOBO_GNOME_CFLAGS)					\
	$(MAIL_CFLAGS)						\
	$(LIBEXCHANGE_CFLAGS)					\
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

-include $(top_srcdir)/git.mk
