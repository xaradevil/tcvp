VERSION = 0.1.0
module_LTLIBRARIES = avformat.la
avformat_la_SOURCES = avformat.c avformat_tc2.c
avformat_la_LDFLAGS = -module -release $(VERSION) $(MODLIBS)
BUILT_SOURCES = avformat_tc2.c avformat_tc2.h
TC2_CPPFLAGS = $(DEFS) $(INCLUDES) $(DEFAULT_INCLUDES) $(CPPFLAGS) $(AM_CPPFLAGS)

CLEANFILES = $(BUILT_SOURCES) .makefile.tc2

export CC

avformat_tc2.c avformat_tc2.h: $(srcdir)/TC2Module
	$(TC2MAKE) --srcdir=$(srcdir) --cflags="$(TC2_CPPFLAGS)" $(TC2MAKE_FLAGS)

-include .makefile.tc2
AM_CPPFLAGS = -I$(FFMPEGSRC)/libavformat -I$(FFMPEGSRC)/libavcodec
MODLIBS = -L$(FFMPEGBLD)/libavformat -lavformat -lavcodec
