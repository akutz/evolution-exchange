INCLUDES =                                                      \
	-DCONNECTOR_PREFIX=\""$(prefix)"\"			\
	-DCONNECTOR_LOCALEDIR=\""$(localedir)"\"		\
	-DCONNECTOR_DATADIR=\""$(CONNECTOR_DATADIR)"\"		\
	-DEVOLUTION_VERSION=\"$(EVOLUTION_VERSION)\"		\
	$(LDAP_CFLAGS)						\
        $(LIBEXCHANGE_CFLAGS)					\
	$(SHELL_CFLAGS)						\
	-I$(top_srcdir)						\
        -I$(top_srcdir)/xntlm

noinst_LIBRARIES =						\
        libexchange.a


IDLS = $(EVOLUTION_idldir)/Evolution-Addressbook-SelectNames.idl

IDL_GENERATED =                 			\
	Evolution-Addressbook-SelectNames.h		\
	Evolution-Addressbook-SelectNames-common.c	\
	Evolution-Addressbook-SelectNames-skels.c	\
	Evolution-Addressbook-SelectNames-stubs.c

$(IDL_GENERATED): $(IDLS)
	$(ORBIT_IDL) $(EVOLUTION_IDL_INCLUDES) \
                $(EVOLUTION_idldir)/Evolution-Addressbook-SelectNames.idl

BUILT_SOURCES = $(IDL_GENERATED)
NODIST_FILES = $(IDL_GENERATED)
CLEANFILES = $(IDL_GENERATED)


PROP_GENERATED = e2k-propnames.h e2k-propnames.c

mapi_properties    = $(top_srcdir)/docs/mapi-properties
e2k_propnames_h_in = $(srcdir)/e2k-propnames.h.in 
e2k_propnames_c_in = $(srcdir)/e2k-propnames.c.in 

e2k-propnames.h: $(e2k_propnames_h_in) $(mapi_properties)
	@echo Building $@
	@( awk '/^@AUTOGENERATE@/ {exit;} {print;}' $(e2k_propnames_h_in);					\
	  awk '/^x/ { printf "#define %-39s E2K_NS_MAPI_PROPTAG \"%s\"\n", $$2, $$1; }' $(mapi_properties);	\
	  awk '{if (tail) { print; }} /^@AUTOGENERATE@/ {tail=1;}' $(e2k_propnames_h_in) )			\
	> $@

e2k-propnames.c: $(e2k_propnames_c_in) $(mapi_properties)
	@echo Building $@
	@( awk '/^@AUTOGENERATE@/ {exit;} {print;}' $(e2k_propnames_c_in);					\
	  awk '/^x/ { print "\t{ \"" $$1 "\", \"" $$2 "\" },"; }' $(mapi_properties);				\
	  awk '{if (tail) { print; }} /^@AUTOGENERATE@/ {tail=1;}' $(e2k_propnames_c_in) )			\
	> $@

BUILT_SOURCES += $(PROP_GENERATED)
NODIST_FILES += $(PROP_GENERATED)
CLEANFILES += $(PROP_GENERATED)


MARSHAL_GENERATED = e2k-marshal.c e2k-marshal.h

e2k-marshal.h: e2k-marshal.list
	( @GLIB_GENMARSHAL@ --prefix=e2k_marshal e2k-marshal.list --header > e2k-marshal.tmp \
	&& mv e2k-marshal.tmp e2k-marshal.h ) \
	|| ( rm -f e2k-marshal.tmp && exit 1 )

e2k-marshal.c: e2k-marshal.h
	( (echo '#include "e2k-marshal.h"'; @GLIB_GENMARSHAL@ --prefix=e2k_marshal e2k-marshal.list --body) > e2k-marshal.tmp \
	&& mv e2k-marshal.tmp e2k-marshal.c ) \
	|| ( rm -f e2k-marshal.tmp && exit 1 )

BUILT_SOURCES += $(MARSHAL_GENERATED)
NODIST_FILES += $(MARSHAL_GENERATED)
CLEANFILES += $(MARSHAL_GENERATED)


libexchange_a_SOURCES =						\
	$(IDL_GENERATED)					\
	$(MARSHAL_GENERATED)					\
	$(PROP_GENERATED)					\
	e2k-propnames.h						\
	e2k-autoconfig.c					\
	e2k-autoconfig.h					\
	e2k-connection.c					\
	e2k-connection.h					\
	e2k-encoding-utils.c					\
	e2k-encoding-utils.h					\
	e2k-freebusy.c						\
	e2k-freebusy.h						\
	e2k-global-catalog.c					\
	e2k-global-catalog.h					\
	e2k-license.c						\
	e2k-license.h						\
	e2k-properties.c					\
	e2k-properties.h					\
	e2k-restriction.c					\
	e2k-restriction.h					\
	e2k-result.c						\
	e2k-result.h						\
	e2k-security-descriptor.c				\
	e2k-security-descriptor.h				\
	e2k-sid.c						\
	e2k-sid.h						\
	e2k-types.h						\
	e2k-uri.c						\
	e2k-uri.h						\
	e2k-utils.c						\
	e2k-utils.h						\
	e2k-xml-utils.c						\
	e2k-xml-utils.h						\
	e2k-user-dialog.c					\
	e2k-user-dialog.h					\
	mapi.h

noinst_PROGRAMS =						\
	ebrowse							\
	actest							\
	fbtest							\
	gctest

progs_libadd =							\
	libexchange.a						\
	$(top_builddir)/xntlm/libxntlm.a			\
	-L$(EVOLUTION_privlibdir) -leutil			\
	$(LIBEXCHANGE_LIBS)

ebrowse_SOURCES =						\
	ebrowse.c

ebrowse.c: e2k-propnames.c

ebrowse_LDADD =							\
	$(progs_libadd)

gctest_SOURCES =						\
	gctest.c

gctest_LDADD =							\
	$(progs_libadd)						\
	$(LDAP_LIBS)

actest_SOURCES =						\
	actest.c

actest_LDADD =							\
	$(progs_libadd)						\
	$(LDAP_LIBS)

fbtest_SOURCES =						\
        fbtest.c

fbtest_LDADD =							\
	$(progs_libadd)						\
	$(CALENDAR_LIBS)					\
	$(LDAP_LIBS)

EXTRA_DIST =							\
	e2k-propnames.c.in					\
	e2k-propnames.h.in					\
	e2k-marshal.list

dist-hook:
	cd $(distdir); rm -f $(NODIST_FILES)
