## Process this file with automake to produce Makefile.in

INCLUDES =							\
	-DG_LOG_DOMAIN=\"exchange-mail\"			\
	$(EXTRA_GNOME_CFLAGS)					\
	$(BONOBO_GNOME_CFLAGS)					\
        $(LIBEXCHANGE_CFLAGS)					\
	-I$(top_srcdir)/lib					\
	-I$(top_srcdir)/storage					\
	-I$(top_srcdir)/camel					\
	$(SHELL_CFLAGS)

noinst_LIBRARIES =						\
	libexchangemail.a

libexchangemail_a_SOURCES =					\
	mail-stub.c						\
	mail-stub.h						\
	mail-stub-listener.c					\
	mail-stub-listener.h					\
	mail-stub-exchange.c					\
	mail-stub-exchange.h					\
	mail-utils.c						\
	mail-utils.h

libexchangemail_a_LIBADD =					\
	$(top_builddir)/camel/camel-stub-marshal.o

