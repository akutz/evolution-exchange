noinst_LTLIBRARIES = libfoldertree.la

INCLUDES = 						\
	-I$(top_srcdir)					\
	-DG_LOG_DOMAIN=\"evolution-exchange-shell\"	\
	$(EXCHANGE_STORAGE_CFLAGS)			\
	-DCONNECTOR_ETSPECDIR=\""$(etspecdir)"\"	\
	-DCONNECTOR_GLADEDIR=\""$(gladedir)"\"		\
	-DEVOLUTION_IMAGESDIR=\""$(EVOLUTION_imagesdir)"\"

libfoldertree_la_SOURCES =				\
	$(MARSHAL_GENERATED)				\
	e-folder.c					\
	e-folder.h					\
	e-folder-creation-dialog.c			\
	e-folder-creation-dialog.h			\
	e-folder-dnd-bridge.c				\
	e-folder-dnd-bridge.h				\
	e-folder-misc-dialogs.c				\
	e-folder-misc-dialogs.h				\
	e-folder-selection-dialog.c			\
	e-folder-selection-dialog.h			\
	e-folder-tree.h					\
	e-folder-tree.c					\
	e-folder-type-registry.c			\
	e-folder-type-registry.h			\
	e-storage-set.c					\
	e-storage-set.h					\
	e-storage-set-view.c				\
	e-storage-set-view.h				\
	e-storage.c					\
	e-storage.h

e-shell-marshal.h: e-shell-marshal.list
	( @GLIB_GENMARSHAL@ --prefix=e_shell_marshal $(srcdir)/e-shell-marshal.list --header > e-shell-marshal.h.tmp \
	&& mv e-shell-marshal.h.tmp e-shell-marshal.h ) \
	|| ( rm -f e-shell-marshal.h.tmp && exit 1 )

e-shell-marshal.c: e-shell-marshal.h
	( (echo '#include "e-shell-marshal.h"'; @GLIB_GENMARSHAL@ --prefix=e_shell_marshal $(srcdir)/e-shell-marshal.list --body) > e-shell-marshal.c.tmp \
	&& mv e-shell-marshal.c.tmp e-shell-marshal.c ) \
	|| ( rm -f e-shell-marshal.c.tmp && exit 1 )

MARSHAL_GENERATED = e-shell-marshal.c e-shell-marshal.h

BUILT_SOURCES = $(MARSHAL_GENERATED)
CLEANFILES = $(MARSHAL_GENERATED)
NODIST_FILES = $(MARSHAL_GENERATED)

etspecdir=$(CONNECTOR_DATADIR)/etspec
etspec_DATA = e-storage-set-view.etspec

gladedir=$(CONNECTOR_DATADIR)/glade
glade_DATA =					\
	e-folder-creation-dialog.glade		\
	e-foreign-folder-dialog.glade

imagesdir = $(CONNECTOR_DATADIR)/images

EXTRA_DIST = 					\
	$(etspec_DATA)				\
	$(glade_DATA)				\
	e-shell-marshal.list			\
	check-empty.xpm				\
	check-filled.xpm			\
	check-missing.xpm

dist-hook:
	cd $(distdir); rm -f $(NODIST_FILES)
