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
	$(srcdir)/compile \
	$(srcdir)/config.guess \
	$(srcdir)/config.h.in \
	$(srcdir)/config.sub \
	$(srcdir)/configure.scan \
	$(srcdir)/depcomp \
	$(srcdir)/install-sh \
	$(srcdir)/ltmain.sh \
	$(srcdir)/missing \
	$(srcdir)/mkinstalldirs \
	$(srcdir)/gtk-doc.make \
	$(srcdir)/m4/gtk-doc.m4 \
	$(srcdir)/m4/intltool.m4 \
	$(srcdir)/m4/libtool.m4 \
	$(srcdir)/m4/ltoptions.m4 \
	$(srcdir)/m4/ltsugar.m4 \
	$(srcdir)/m4/ltversion.m4 \
	$(srcdir)/m4/lt~obsolete.m4

EXTRA_DIST = \
	MAINTAINERS \
	intltool-extract.in \
	intltool-merge.in \
	intltool-update.in \
	evolution-exchange-zip.in

DISTCHECK_CONFIGURE_FLAGS = --enable-gtk-doc

-include $(top_srcdir)/git.mk
