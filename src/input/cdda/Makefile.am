VERSION = 0.1.0
module_LTLIBRARIES = cdda.la
cdda_la_SOURCES = cdda.c
nodist_cdda_la_SOURCES = cdda_tc2.c
cdda_la_LDFLAGS = -module -release $(VERSION) $(MODLIBS)
BUILT_SOURCES = cdda_tc2.c cdda_tc2.h
EXTRA_DIST = TC2Module
TC2MAKE_CMD = $(TC2MAKE) --srcdir=$(srcdir) $(TC2MAKE_FLAGS)

CLEANFILES = $(BUILT_SOURCES) .makefile.tc2

export CC

cdda_tc2.c cdda_tc2.h: $(srcdir)/TC2Module
	$(TC2MAKE_CMD)

-include .makefile.tc2
MODLIBS = -lcdda_interface -lcdda_paranoia