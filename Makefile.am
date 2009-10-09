ACLOCAL_AMFLAGS=-I m4

SUBDIRS = \
	server \
	eplugin \
	tools \
	camel \
	addressbook \
	calendar \
	po \
	docs

DISTCLEANFILES = \
	intltool-extract \
	intltool-update \
	intltool-merge

MAINTAINERCLEANFILES = \
	$(srcdir)/INSTALL \
	$(srcdir)/aclocal.m4 \
	$(srcdir)/autoscan.log \
	$(srcdir)/config.guess \
	$(srcdir)/config.h.in \
	$(srcdir)/config.sub \
	$(srcdir)/configure.scan \
	$(srcdir)/depcomp \
	$(srcdir)/install-sh \
	$(srcdir)/ltmain.sh \
	$(srcdir)/missing \
	$(srcdir)/mkinstalldirs

EXTRA_DIST = \
	MAINTAINERS \
	intltool-extract.in \
	intltool-merge.in \
	intltool-update.in \
	evolution-exchange-zip.in

DISTCHECK_CONFIGURE_FLAGS = --enable-gtk-doc

-include $(top_srcdir)/git.mk
