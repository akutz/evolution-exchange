INCLUDES =						\
	-DG_LOG_DOMAIN=\"e-book-backend-exchange\"	\
	$(ADDRESSBOOK_CFLAGS)				\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/shell				\
	-I$(top_srcdir)/lib				\
	-I$(top_srcdir)/storage				\
	-I$(top_builddir)/shell				\
	-I$(top_builddir)/lib				\
	$(LDAP_CFLAGS)					\
	$(LIBEXCHANGE_CFLAGS)

noinst_LIBRARIES = libexchangeaddressbook.a

libexchangeaddressbook_a_SOURCES =	\
	e-book-backend-exchange.c	\
	e-book-backend-exchange.h	\
	e-book-backend-gal.c		\
	e-book-backend-gal.h

