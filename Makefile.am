SUBDIRS =				\
	camel mail			\
	addressbook calendar		\
	storage				\
	po docs

DISTCLEANFILES =				\
	intltool-extract			\
	intltool-update				\
	intltool-merge

EXTRA_DIST =				\
	MAINTAINERS			\
	intltool-extract.in		\
	intltool-merge.in		\
	intltool-update.in		\
	evolution-exchange-zip.in

DISTCHECK_CONFIGURE_FLAGS = --enable-gtk-doc


-include $(top_srcdir)/git.mk
