VERSION = 0.1.0
module_LTLIBRARIES = audiofile.la
audiofile_la_SOURCES = af.c
nodist_audiofile_la_SOURCES = audiofile_tc2.c
audiofile_la_LDFLAGS = -module -release $(VERSION) $(MODLIBS)
BUILT_SOURCES = audiofile_tc2.c audiofile_tc2.h
EXTRA_DIST = TC2Module
TC2MAKE_CMD = $(TC2MAKE) --srcdir=$(srcdir) $(TC2MAKE_FLAGS)

CLEANFILES = $(BUILT_SOURCES) .makefile.tc2

export CC

audiofile_tc2.c audiofile_tc2.h: $(srcdir)/TC2Module
	$(TC2MAKE_CMD)

-include .makefile.tc2
MODLIBS = @AUDIOFILE_LIBS@
AM_CFLAGS = @AUDIOFILE_CFLAGS@
