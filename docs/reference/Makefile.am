## Process this file with automake to produce Makefile.in

# The name of the module.
DOC_MODULE=evolution-exchange

# The top-level SGML file.
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.sgml

# Extra options to supply to gtkdoc-scan
SCAN_OPTIONS=

# Files/directories to ignore when scanning.
IGNORE_HFILES=					\
	config.h				\
	config-defines.h			\
	config-renaming.h			\
	Evolution-Addressbook-SelectNames.h	\
	connector-public-key.h			\
	e2k-marshal.h				\
	e2k-propnames.h				\
	e2k-proptags.h				\
	e2k-types.h				\
	addressbook				\
	calendar				\
	camel					\
	mail					\
	shell					\
	storage

# The directory containing the source code. Relative to $(srcdir)
DOC_SOURCE_DIR=../../

HFILE_GLOB=$(top_srcdir)/*/*.h
CFILE_GLOB=$(top_srcdir)/*/*.c

# CFLAGS and LDFLAGS for compiling scan program. Only needed
# if $(DOC_MODULE).types is non-empty.
INCLUDES =					\
	-I$(top_srcdir)/lib			\
	-I$(top_builddir)/lib			\
	$(LIBEXCHANGE_CFLAGS)			\
	$(LDAP_CFLAGS)

GTKDOC_LIBS =					\
	$(LIBEXCHANGE_LIBS)			\
	$(LDAP_LIBS)

# Extra options to supply to gtkdoc-mkdb
MKDB_OPTIONS=--ignore-files="$(IGNORE_HFILES)"

# Images to copy into HTML directory
HTML_IMAGES =

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE)
content_files =

# Extra options to supply to gtkdoc-fixref
FIXXREF_OPTIONS=

include gtk-doc.make

# Other files to distribute
EXTRA_DIST +=					\
	gtk-doc.make

dist-hook-local:
	mkdir $(distdir)/features
	-cp $(srcdir)/features/*.xml $(distdir)/features


-include $(top_srcdir)/git.mk
