privsolib_LTLIBRARIES = libxntlm.la

libxntlm_la_SOURCES = \
	xntlm.c \
	xntlm.h \
	xntlm-des.c \
	xntlm-des.h \
	xntlm-md4.c \
	xntlm-md4.h

libxntlm_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	$(GLIB_CFLAGS)

libxntlm_la_LIBADD = \
	$(GLIB_LIBS)

-include $(top_srcdir)/git.mk
