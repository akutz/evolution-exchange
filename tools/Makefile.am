AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/camel \
	-DG_LOG_DOMAIN=\"evolution-exchange-storage\" \
	$(LIBEXCHANGE_CFLAGS) \
	$(EXCHANGE_STORAGE_CFLAGS) \
	$(LDAP_CFLAGS) \
	-DPREFIX=\"$(prefix)\" \
	-DSYSCONFDIR=\""$(sysconfdir)"\" \
	-DDATADIR=\""$(datadir)"\" \
	-DLIBDIR=\""$(datadir)"\" \
	-DCONNECTOR_IMAGESDIR=\""$(imagesdir)"\" \
	-DCONNECTOR_UIDIR=\""$(uidir)"\" \
	-DCONNECTOR_LOCALEDIR=\""$(localedir)\""

noinst_PROGRAMS = \
	exchange-connector-setup
#	migr-test

noinst_LTLIBRARIES = libevolution-exchange-shared.la

libevolution_exchange_shared_la_SOURCES = 	\
	exchange-share-config-listener.h	\
	exchange-share-config-listener.c

libevolution_exchange_shared_la_LIBADD = 	\
	$(EXCHANGE_STORAGE_LIBS)		\
	$(LIBEXCHANGE_LIBS)

libevolution_exchange_shared_la_LDFLAGS = $(NO_UNDEFINED)

exchange_connector_setup_SOURCES =	\
	ximian-connector-setup.c	\
	exchange-autoconfig-wizard.h	\
	exchange-autoconfig-wizard.c

exchange_connector_setup_LDADD = \
	$(LDAP_LIBS) \
	$(EXCHANGE_STORAGE_LIBS) \
	$(LIBEXCHANGE_LIBS)

install-exec-local: exchange-connector-setup
	$(mkinstalldirs) $(DESTDIR)$(bindir)
	$(LIBTOOL) --mode=install $(INSTALL_PROGRAM) exchange-connector-setup $(DESTDIR)$(bindir)/exchange-connector-setup-$(BASE_VERSION)

uninstall-local:
	rm -rf $(DESTDIR)$(bindir)/exchange-connector-setup-$(BASE_VERSION)

#migr_test_SOURCES = \
#	migr-test.c \
#	exchange-config-listener.c \
#	exchange-config-listener.h \
#	exchange-migrate.c \
#	exchange-migrate.h
#
#migr_test_LDADD = \
#	$(LDAP_LIBS) \
#	$(EXCHANGE_STORAGE_LIBS) \
#	$(LIBEXCHANGE_LIBS)
#

imagesdir = $(CONNECTOR_DATADIR)/images
images_DATA = \
	connector.png \
	connector-mini.png \
	exchange-delegates-48.png \
	exchange-oof-48.png

uidir   = $(CONNECTOR_DATADIR)/ui
ui_DATA = ximian-connector.xml

#DISTCLEANFILES =

EXTRA_DIST = \
	$(images_DATA) \
	$(ui_DATA)

-include $(top_srcdir)/git.mk
