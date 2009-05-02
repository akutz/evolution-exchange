INCLUDES =							\
	-I$(top_srcdir)						\
	-I$(top_srcdir)/mail					\
	-I$(top_srcdir)/camel					\
	-I$(top_srcdir)/calendar				\
	-DG_LOG_DOMAIN=\"evolution-exchange-storage\"		\
	$(LIBEXCHANGE_CFLAGS)				\
	$(EXCHANGE_STORAGE_CFLAGS)				\
	$(LDAP_CFLAGS)						\
	-DPREFIX=\"$(prefix)\"					\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 			\
	-DDATADIR=\""$(datadir)"\"	 			\
	-DLIBDIR=\""$(datadir)"\"	 			\
	-DCONNECTOR_GLADEDIR=\""$(gladedir)"\"			\
	-DCONNECTOR_IMAGESDIR=\""$(imagesdir)"\"		\
	-DCONNECTOR_UIDIR=\""$(uidir)"\"			\
	-DCONNECTOR_LOCALEDIR=\""$(localedir)\""

EVOLUTION_privlibexec_PROGRAMS =				\
	evolution-exchange-storage

noinst_PROGRAMS =						\
	exchange-connector-setup				\
	migr-test

evolution_exchange_storage_SOURCES =				\
	exchange-autoconfig-wizard.c				\
	exchange-autoconfig-wizard.h				\
	exchange-component.c					\
	exchange-component.h					\
	exchange-config-listener.c				\
	exchange-config-listener.h				\
	exchange-change-password.c				\
	exchange-change-password.h				\
	exchange-migrate.c					\
	exchange-migrate.h					\
	exchange-storage.c					\
	exchange-storage.h					\
	main.c							

evolution_exchange_storage_LDADD =				\
	$(top_builddir)/mail/libexchangemail.la 		\
	$(top_builddir)/addressbook/libexchangeaddressbook.la	\
	$(top_builddir)/calendar/libexchangecalendar.la		\
	$(top_builddir)/camel/camel-stub-marshal.lo		\
	$(LDAP_LIBS)						\
	$(EXCHANGE_STORAGE_LIBS)				\
	$(LIBEXCHANGE_LIBS)					\
	$(SOCKET_LIBS)


exchange_connector_setup_SOURCES =				\
	ximian-connector-setup.c				\
	exchange-config-listener.c				\
	exchange-config-listener.h				\
	exchange-autoconfig-wizard.c

exchange_connector_setup_LDADD =				\
	$(LDAP_LIBS)						\
	$(EXCHANGE_STORAGE_LIBS)				\
	$(LIBEXCHANGE_LIBS)

install-exec-local: exchange-connector-setup
	$(mkinstalldirs) $(DESTDIR)$(bindir)
	$(LIBTOOL) --mode=install $(INSTALL_PROGRAM) exchange-connector-setup $(DESTDIR)$(bindir)/exchange-connector-setup-$(BASE_VERSION)

uninstall-local:
	rm -rf $(DESTDIR)$(bindir)/exchange-connector-setup-$(BASE_VERSION)

migr_test_SOURCES =						\
	migr-test.c						\
	exchange-config-listener.c				\
	exchange-config-listener.h				\
	exchange-migrate.c					\
	exchange-migrate.h

migr_test_LDADD = 						\
	$(LDAP_LIBS)						\
	$(EXCHANGE_STORAGE_LIBS)				\
	$(LIBEXCHANGE_LIBS)

serverdir = $(libdir)/bonobo/servers
server_in_files = GNOME_Evolution_Exchange_Storage.server.in.in
server_DATA = $(server_in_files:.server.in.in=_$(BASE_VERSION).server)

if OS_WIN32
CONNECTOR_PATH_IN_SERVER_FILE=../../../libexec/evolution/${BASE_VERSION}
else
CONNECTOR_PATH_IN_SERVER_FILE=${EVOLUTION_privlibexecdir}
endif

%_$(BASE_VERSION).server.in: %.server.in.in
	sed -e "s;\@CONNECTOR_PATH\@;${CONNECTOR_PATH_IN_SERVER_FILE};g"	\
	    -e "s;\@IMAGESDIR\@;${imagesdir};g"				\
	    -e "s;\@BASE_VERSION\@;${BASE_VERSION};g"			\
	    -e "s;\@EXEEXT\@;${EXEEXT};g"				\
	    -e "s;\@API_VERSION\@;${API_VERSION};g"			\
	    -e "s;\@EDS_BASE_VERSION\@;${EDS_BASE_VERSION};g"		\
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
	exchange-permissions-dialog.glade	\
	exchange-folder-tree.glade		\
	exchange-passwd-expiry.glade

uidir   = $(CONNECTOR_DATADIR)/ui
ui_DATA = ximian-connector.xml

DISTCLEANFILES = \
	$(server_DATA)

EXTRA_DIST =				\
	$(server_in_files)		\
	$(images_DATA)			\
	$(glade_DATA)			\
	$(ui_DATA)

-include $(top_srcdir)/git.mk
