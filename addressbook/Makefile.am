INCLUDES =						\
	-DG_LOG_DOMAIN=\"e-book-backend-exchange\"	\
	$(ADDRESSBOOK_CFLAGS)				\
	$(LIBEXCHANGE_CFLAGS)				\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/storage				\
	$(LDAP_CFLAGS)					
# GConf schemas

schemadir   = $(GCONF_SCHEMA_FILE_DIR)
schema_in_files = apps_exchange_addressbook.schemas.in.in
schema_DATA = $(schema_in_files:.schemas.in.in=-$(BASE_VERSION).schemas)
%-$(BASE_VERSION).schemas.in: %.schemas.in.in
	cp $< $@

@INTLTOOL_SCHEMAS_RULE@

install-data-local:
	if test -z "$(DESTDIR)" ; then \
		for p in $(schema_DATA) ; do \
			GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $$p; \
		done \
	fi

noinst_LTLIBRARIES = libexchangeaddressbook.la

libexchangeaddressbook_la_SOURCES =	\
	e-book-backend-exchange.c	\
	e-book-backend-exchange.h	\
	e-book-backend-exchange-factory.c	\
	e-book-backend-exchange-factory.h	\
	e-book-backend-gal.c		\
	e-book-backend-gal.h		\
	e-book-backend-gal-factory.c	\
	e-book-backend-gal-factory.h

EXTRA_DIST =				\
	$(schema_in_files)
 
