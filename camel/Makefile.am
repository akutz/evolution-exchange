## Process this file with automake to produce Makefile.in

CAMEL_provider_LTLIBRARIES = libcamelexchange.la
CAMEL_provider_DATA = libcamelexchange.urls

INCLUDES =					\
	$(WARN_CFLAGS)				\
	-I$(top_srcdir)				\
	$(CAMEL_CFLAGS)				\
	$(SHELL_CFLAGS)				\
	-DCONNECTOR_LOCALEDIR=\"$(localedir)\"	\
	-DG_LOG_DOMAIN=\"camel-exchange-provider\"	

libcamelexchange_la_SOURCES = 			\
	camel-exchange-folder.c			\
	camel-exchange-provider.c		\
	camel-exchange-search.c			\
	camel-exchange-store.c			\
	camel-exchange-summary.c		\
	camel-exchange-transport.c		\
	camel-stub-marshal.c			\
	camel-stub.c

noinst_HEADERS =				\
	camel-exchange-folder.h			\
	camel-exchange-search.h			\
	camel-exchange-store.h			\
	camel-exchange-summary.h		\
	camel-exchange-transport.h		\
	camel-stub-constants.h			\
	camel-stub-marshal.h			\
	camel-stub.h

libcamelexchange_la_LDFLAGS = -avoid-version -module

EXTRA_DIST = libcamelexchange.urls
