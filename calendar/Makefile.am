AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/storage \
	$(CALENDAR_CFLAGS) \
	$(LIBEXCHANGE_CFLAGS) \
	$(LDAP_CFLAGS) \
	-DG_LOG_DOMAIN=\"e-cal-backend-exchange\"

extension_LTLIBRARIES = \
        libecalbackendexchange.la

libecalbackendexchange_la_SOURCES = \
	e-cal-backend-exchange.c \
	e-cal-backend-exchange.h \
	e-cal-backend-exchange-calendar.c \
	e-cal-backend-exchange-calendar.h \
	e-cal-backend-exchange-tasks.c \
	e-cal-backend-exchange-tasks.h \
	e-cal-backend-exchange-factory.c \
	e-cal-backend-exchange-factory.h \
	e2k-cal-query.c \
	e2k-cal-query.h \
	e2k-cal-utils.c \
	e2k-cal-utils.h

libecalbackendexchange_la_LDFLAGS = \
	-module -avoid-version $(NO_UNDEFINED)

-include $(top_srcdir)/git.mk
