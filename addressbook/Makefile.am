INCLUDES =						\
	-DG_LOG_DOMAIN=\"pas-backend-exchange\"		\
	$(ADDRESSBOOK_CFLAGS)				\
	-I$(top_srcdir)/lib				\
	-I$(top_srcdir)/storage				\
	$(LDAP_CFLAGS)					\
	$(LIBEXCHANGE_CFLAGS)

noinst_LIBRARIES = libexchangeaddressbook.a

libexchangeaddressbook_a_SOURCES =	\
	pas-backend-ad.c		\
	pas-backend-ad.h		\
	pas-backend-exchange.c		\
	pas-backend-exchange.h

