## Process this file with automake to produce Makefile.in

CAMEL_provider_LTLIBRARIES = libcamelexchange.la
CAMEL_provider_DATA = libcamelexchange.urls

INCLUDES =					\
	$(WARN_CFLAGS)				\
	$(GLIB_CFLAGS)				\
	$(CAMEL_CFLAGS)				\
	$(SHELL_CFLAGS)				\
	$(LIBEXCHANGE_CFLAGS)			\
	-DPREFIX=\"$(prefix)\"			\
	-DCONNECTOR_LOCALEDIR=\"$(localedir)\"	\
	-DG_LOG_DOMAIN=\"camel-exchange-provider\"	

libcamelexchange_la_SOURCES = 			\
	camel-exchange-folder.c			\
	camel-exchange-provider.c		\
	camel-exchange-journal.c		\
	camel-exchange-search.c			\
	camel-exchange-store.c			\
	camel-exchange-summary.c		\
	camel-exchange-transport.c		\
	camel-stub-marshal.c			\
	camel-stub.c

noinst_HEADERS =				\
	camel-exchange-folder.h			\
	camel-exchange-journal.h		\
	camel-exchange-search.h			\
	camel-exchange-store.h			\
	camel-exchange-summary.h		\
	camel-exchange-transport.h		\
	camel-stub-constants.h			\
	camel-stub-marshal.h			\
	camel-stub.h

libcamelexchange_la_LDFLAGS = -avoid-version -module $(NO_UNDEFINED)

libcamelexchange_la_LIBADD = 			\
	$(LDAP_LIBS)				\
	$(LIBEXCHANGE_LIBS)			\
	$(EXCHANGE_STORAGE_LIBS)		\
	$(SOCKET_LIBS)				\
	$(PTHREAD_LIB)

EXTRA_DIST = libcamelexchange.urls

-include $(top_srcdir)/git.mk
