componentdir = $(EVOLUTION_privlibdir)/components

component_LTLIBRARIES = libevolution_exchange.la

INCLUDES = 						\
	-I$(top_srcdir)					\
	-DG_LOG_DOMAIN=\"evolution-exchange-component\"	\
	$(EXCHANGE_STORAGE_CFLAGS)			\
	-DPREFIX=\"$(prefix)\"				\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 		\
	-DDATADIR=\""$(datadir)"\"	 		\
	-DLIBDIR=\""$(datadir)"\"


libevolution_exchange_la_SOURCES =	 		\
	$(IDL_GENERATED)				\
	exchange-component.c				\
	exchange-component.h				\
	exchange-component-factory.c

libevolution_exchange_la_LDFLAGS =			\
	-avoid-version					\
	-module

IDLS =							\
	Ximian-Connector.idl

IDL_GENERATED_H =					\
	Ximian-Connector.h

IDL_GENERATED_C =					\
	Ximian-Connector-common.c			\
	Ximian-Connector-skels.c			\
	Ximian-Connector-stubs.c			

IDL_GENERATED = $(IDL_GENERATED_C) $(IDL_GENERATED_H)

$(IDL_GENERATED_H): Ximian-Connector.idl
	$(ORBIT_IDL) $(EVOLUTION_IDL_INCLUDES) $(srcdir)/Ximian-Connector.idl

$(IDL_GENERATED_C): $(IDL_GENERATED_H)


BUILT_SOURCES = $(IDL_GENERATED)
CLEANFILES = $(IDL_GENERATED)
NODIST_FILES = $(IDL_GENERATED)

server_in_files = GNOME_Evolution_Exchange_Component.server.in.in
serverdir = $(libdir)/bonobo/servers
server_DATA = $(server_in_files:.server.in.in=_$(BASE_VERSION).server)

%_$(BASE_VERSION).server.in: %.server.in.in
	sed -e "s;\@COMPONENTDIR\@;${componentdir};g"	\
	    -e "s;\@IMAGESDIR\@;${imagesdir};g"			\
	    -e "s;\@BASE_VERSION\@;${BASE_VERSION};g"		\
	    < $< > $@

@INTLTOOL_SERVER_RULE@

imagesdir = $(CONNECTOR_DATADIR)/images

EXTRA_DIST = 					\
	$(IDLS)					\
	$(server_in_files)

dist-hook:
	cd $(distdir); rm -f $(NODIST_FILES)
