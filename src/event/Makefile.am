VERSION = 0.1.0
module_LTLIBRARIES = tcvp_event.la
tcvp_event_la_SOURCES = event.c
nodist_tcvp_event_la_SOURCES = tcvp_event_tc2.c
tcvp_event_la_LDFLAGS = -module -release $(VERSION) $(MODLIBS)
BUILT_SOURCES = tcvp_event_tc2.c tcvp_event_tc2.h
EXTRA_DIST = TC2Module
TC2MAKE_CMD = $(TC2MAKE) --srcdir=$(srcdir) $(TC2MAKE_FLAGS)

CLEANFILES = $(BUILT_SOURCES) .makefile.tc2

export CC

tcvp_event_tc2.c tcvp_event_tc2.h: $(srcdir)/TC2Module
	$(TC2MAKE_CMD)

-include .makefile.tc2
