INCLUDES =							\
	-DG_LOG_DOMAIN=\"evolution-exchange-storage\"		\
	$(EXCHANGE_STORAGE_CFLAGS)				\
	-DPREFIX=\"$(prefix)\"					\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 			\
	-DDATADIR=\""$(datadir)"\"	 			\
	-DLIBDIR=\""$(datadir)"\"	 			\
	-DCONNECTOR_GLADEDIR=\""$(gladedir)"\"			\
	-DCONNECTOR_IMAGESDIR=\""$(imagesdir)"\"		\
	-DCONNECTOR_LOCALEDIR=\""$(localedir)\""		\
	-I$(top_srcdir)						\
	-I$(top_srcdir)/lib					\
	-I$(top_srcdir)/mail					\
	-I$(top_srcdir)/camel					\
	-I$(top_srcdir)/calendar

EVOLUTION_privlibexec_PROGRAMS =				\
	evolution-exchange-storage

evolution_exchange_storage_SOURCES =				\
	e-folder-exchange.c					\
	e-folder-exchange.h					\
	exchange-account.c					\
	exchange-account.h					\
	exchange-autoconfig-wizard.c				\
	exchange-autoconfig-wizard.h				\
	exchange-component.h					\
	exchange-config-listener.c				\
	exchange-config-listener.h				\
	exchange-delegates-control.c				\
	exchange-delegates-control.h				\
	exchange-delegates-delegates.c				\
	exchange-delegates-delegators.c				\
	exchange-delegates-user.c				\
	exchange-delegates-user.h				\
	exchange-hierarchy.c					\
	exchange-hierarchy.h					\
	exchange-hierarchy-foreign.c				\
	exchange-hierarchy-foreign.h				\
	exchange-hierarchy-gal.c				\
	exchange-hierarchy-gal.h				\
	exchange-hierarchy-webdav.c				\
	exchange-hierarchy-webdav.h				\
	exchange-offline-handler.c				\
	exchange-offline-handler.h				\
	exchange-oof.c						\
	exchange-oof.h						\
	exchange-permissions-dialog.c				\
	exchange-permissions-dialog.h				\
	exchange-storage.c					\
	exchange-storage.h					\
	exchange-types.h					\
	main.c

evolution_exchange_storage_LDADD =				\
	$(top_builddir)/mail/libexchangemail.a			\
	$(top_builddir)/calendar/libexchangecalendar.a		\
	$(top_builddir)/addressbook/libexchangeaddressbook.a	\
	$(top_builddir)/lib/libexchange.a			\
	$(top_builddir)/xntlm/libxntlm.a			\
	$(EXCHANGE_STORAGE_LIBS)				\
	$(LDAP_LIBS)

bin_PROGRAMS = ximian-connector-setup

ximian_connector_setup_SOURCES =				\
	ximian-connector-setup.c				\
	exchange-autoconfig-wizard.c

ximian_connector_setup_LDADD =					\
	$(top_builddir)/lib/libexchange.a			\
	$(top_builddir)/xntlm/libxntlm.a			\
	$(EXCHANGE_STORAGE_LIBS)				\
	$(LDAP_LIBS)

serverdir = $(libdir)/bonobo/servers
server_in_files = GNOME_Evolution_Exchange_Storage.server.in.in
server_DATA = $(server_in_files:.server.in.in=.server)

%.server.in: %.server.in.in Makefile
	sed -e "s;\@CONNECTOR_PATH\@;${EVOLUTION_privlibexecdir};g" < $< > $@

@INTLTOOL_SERVER_RULE@


if ENABLE_PURIFY
PLINK = $(LIBTOOL) --mode=link $(PURIFY) $(CCLD) $(AM_CFLAGS) $(CFLAGS) $(LDFLAGS) -o $@

all-local: evolution-exchange-storage.pure

evolution-exchange-storage.pure: evolution-exchange-storage
	@rm -f evolution-exchange-storage.pure
	$(PLINK) $(evolution_exchange_storage_OBJECTS) $(evolution_exchange_storage_LDADD) $(LIBS)

endif

EVOLUTION_images_DATA =			\
	connector.png			\
	exchange-delegates-48.png	\
	exchange-oof-48.png

imagesdir = $(CONNECTOR_DATADIR)/images
images_DATA = connector.png

gladedir   = $(CONNECTOR_DATADIR)/glade
glade_DATA = exchange-permissions-dialog.glade exchange-delegates.glade \
	exchange-oof.glade exchange-autoconfig-wizard.glade

# GConf schemas

schemadir   = $(GCONF_SCHEMA_FILE_DIR)
schema_DATA = apps_evolution_exchange.schemas

install-data-local: $(noinst_DATA)
	if test -z "$(DESTDIR)" ; then					    \
		export GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE);   \
		for p in $(schema_DATA) ; do				    \
			$(GCONFTOOL) --makefile-install-rule $(srcdir)/$$p; \
		done							    \
	fi;

EXTRA_DIST =				\
	mail.xpm			\
	$(server_in_files)		\
	$(server_DATA)			\
	$(EVOLUTION_images_DATA)	\
	$(glade_DATA)			\
	$(schema_DATA)
