INCLUDES =							\
	-DG_LOG_DOMAIN=\"evolution-exchange-storage\"		\
	$(EXCHANGE_STORAGE_CFLAGS)				\
	$(KRB5_CFLAGS)						\
	$(LDAP_CFLAGS)						\
	-DPREFIX=\"$(prefix)\"					\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 			\
	-DDATADIR=\""$(datadir)"\"	 			\
	-DLIBDIR=\""$(datadir)"\"	 			\
	-DCONNECTOR_GLADEDIR=\""$(gladedir)"\"			\
	-DCONNECTOR_IMAGESDIR=\""$(imagesdir)"\"		\
	-DCONNECTOR_UIDIR=\""$(uidir)"\"			\
	-DCONNECTOR_LOCALEDIR=\""$(localedir)\""		\
	-I$(top_srcdir)						\
	-I$(top_srcdir)/lib					\
	-I$(top_srcdir)/xntlm					\
	-I$(top_srcdir)/mail					\
	-I$(top_srcdir)/shell					\
	-I$(top_srcdir)/component				\
	-I$(top_srcdir)/camel					\
	-I$(top_srcdir)/calendar				\
	-I$(top_builddir)/shell					\
	-I$(top_builddir)/component				\
	-I$(top_builddir)/lib

EVOLUTION_privlibexec_PROGRAMS =				\
	evolution-exchange-storage

noinst_PROGRAMS =						\
	ximian-connector-setup					\
	migr-test

evolution_exchange_storage_SOURCES =				\
	$(IDL_GENERATED)					\
	e-folder-exchange.c					\
	e-folder-exchange.h					\
	exchange-account.c					\
	exchange-account.h					\
	exchange-autoconfig-wizard.c				\
	exchange-autoconfig-wizard.h				\
	exchange-component.h					\
	exchange-change-password.c				\
	exchange-change-password.h				\
	exchange-delegates.c					\
	exchange-delegates.h					\
	exchange-delegates-user.c				\
	exchange-delegates-user.h				\
	exchange-hierarchy.c					\
	exchange-hierarchy.h					\
	exchange-hierarchy-favorites.c				\
	exchange-hierarchy-favorites.h				\
	exchange-hierarchy-foreign.c				\
	exchange-hierarchy-foreign.h				\
	exchange-hierarchy-gal.c				\
	exchange-hierarchy-gal.h				\
	exchange-hierarchy-somedav.c				\
	exchange-hierarchy-somedav.h				\
	exchange-hierarchy-webdav.c				\
	exchange-hierarchy-webdav.h				\
	exchange-offline-handler.c				\
	exchange-offline-handler.h				\
	exchange-oof.c						\
	exchange-oof.h						\
	exchange-permissions-dialog.c				\
	exchange-permissions-dialog.h				\
	exchange-config-listener.c				\
	exchange-config-listener.h				\
	exchange-storage.c					\
	exchange-storage.h					\
	exchange-types.h					\
	exchange-migrate.c					\
	exchange-migrate.h					\
	xc-backend-component.c					\
	xc-backend-component.h					\
	xc-backend-view.c					\
	xc-backend-view.h					\
	xc-backend.c						\
	xc-backend.h						\
	xc-commands.c						\
	xc-commands.h						\
	main.c

evolution_exchange_storage_LDADD =				\
	$(top_builddir)/shell/libfoldertree.a			\
	$(top_builddir)/mail/libexchangemail.a			\
	$(top_builddir)/addressbook/libexchangeaddressbook.a	\
	$(top_builddir)/calendar/libexchangecalendar.a		\
	$(top_builddir)/lib/libexchange.a			\
	$(top_builddir)/xntlm/libxntlm.a			\
	$(top_builddir)/camel/camel-stub-marshal.lo		\
	$(EXCHANGE_STORAGE_LIBS)				\
	$(LDAP_LIBS)						\
	$(KRB5_LIBS)

IDLS =							\
	$(top_srcdir)/component/Ximian-Connector.idl

IDL_GENERATED_H =					\
	Ximian-Connector.h

IDL_GENERATED_C =					\
	Ximian-Connector-common.c			\
	Ximian-Connector-skels.c			\
	Ximian-Connector-stubs.c			

IDL_GENERATED = $(IDL_GENERATED_C) $(IDL_GENERATED_H)

$(IDL_GENERATED_H): $(IDLS)
	$(ORBIT_IDL) $(EVOLUTION_IDL_INCLUDES) $(IDLS)

$(IDL_GENERATED_C): $(IDL_GENERATED_H)


ximian_connector_setup_SOURCES =				\
	ximian-connector-setup.c				\
	exchange-autoconfig-wizard.c

ximian_connector_setup_LDADD =					\
	$(top_builddir)/lib/libexchange.a			\
	$(top_builddir)/xntlm/libxntlm.a			\
	$(EXCHANGE_STORAGE_LIBS)				\
	$(LDAP_LIBS)

install-exec-local: ximian-connector-setup
	$(mkinstalldirs) $(DESTDIR)$(bindir)
	$(LIBTOOL) --mode=install $(INSTALL_PROGRAM) ximian-connector-setup $(DESTDIR)$(bindir)/ximian-connector-setup-$(BASE_VERSION)

migr_test_SOURCES =						\
	migr-test.c						\
	exchange-migrate.c					\
	exchange-migrate.h

migr_test_LDADD = 						\
	$(top_builddir)/lib/libexchange.a			\
	$(evolution_exchange_storage_LDADD)

serverdir = $(libdir)/bonobo/servers
server_in_files = GNOME_Evolution_Exchange_Storage.server.in.in
server_DATA = $(server_in_files:.server.in.in=_$(BASE_VERSION).server)

%_$(BASE_VERSION).server.in: %.server.in.in
	sed -e "s;\@CONNECTOR_PATH\@;${EVOLUTION_privlibexecdir};g"	\
	    -e "s;\@IMAGESDIR\@;${imagesdir};g"				\
	    -e "s;\@BASE_VERSION\@;${BASE_VERSION};g"			\
	    < $< > $@

@INTLTOOL_SERVER_RULE@

imagesdir = $(CONNECTOR_DATADIR)/images
images_DATA =				\
	connector.png			\
	connector-mini.png		\
	exchange-delegates-48.png	\
	exchange-oof-48.png

gladedir   = $(CONNECTOR_DATADIR)/glade
glade_DATA =					\
	exchange-autoconfig-wizard.glade	\
	exchange-change-password.glade		\
	exchange-delegates.glade		\
	exchange-oof.glade			\
	exchange-permissions-dialog.glade

uidir   = $(CONNECTOR_DATADIR)/ui
ui_DATA = ximian-connector.xml

schemadir   = $(GCONF_SCHEMA_FILE_DIR)
schema_DATA = apps_evolution_exchange.schemas

install-data-local: $(noinst_DATA)
	if test -z "$(DESTDIR)" ; then					    \
		export GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE);   \
		for p in $(schema_DATA) ; do				    \
			$(GCONFTOOL) --makefile-install-rule $(srcdir)/$$p; \
		done							    \
	fi



EXTRA_DIST =				\
	mail.xpm			\
	$(server_in_files)		\
	$(server_DATA)			\
	$(images_DATA)			\
	$(glade_DATA)			\
	$(ui_DATA)			\
	$(schema_DATA)
