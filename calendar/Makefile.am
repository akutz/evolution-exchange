INCLUDES =                                                      \
	$(CALENDAR_CFLAGS)					\
	$(LIBEXCHANGE_CFLAGS)					\
        -I$(top_srcdir)/lib					\
	-I$(top_builddir)/lib					\
        -I$(top_srcdir)/storage					\
	-I$(top_builddir)/storage				\
	-DG_LOG_DOMAIN=\"cal-backend-exchange\"

noinst_LIBRARIES =						\
        libexchangecalendar.a

libexchangecalendar_a_SOURCES = 				\
	cal-backend-exchange.c					\
	cal-backend-exchange.h					\
	cal-backend-exchange-utils.c				\
	e2k-cache.c						\
	e2k-cache.h						\
	e2k-cal-component.c					\
	e2k-cal-component.h					\
	e2k-cal-component-todo.c
 