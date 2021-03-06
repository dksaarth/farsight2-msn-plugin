include $(top_srcdir)/common/check.mak

CHECK_REGISTRY = $(top_builddir)/tests/check/test-registry.xml

REGISTRY_ENVIRONMENT = \
	GST_REGISTRY=$(CHECK_REGISTRY)

TESTS_ENVIRONMENT = \
	CK_DEFAULT_TIMEOUT=120					\
	STATE_IGNORE_ELEMENTS="$(STATE_IGNORE_ELEMENTS)"	\
	$(REGISTRY_ENVIRONMENT)					\
	GST_PLUGIN_PATH=$(top_builddir)/gst:${GST_PLUGIN_PATH}	\
	FS_PLUGIN_PATH=$(top_builddir)/transmitters/rawudp/.libs:$(top_builddir)/transmitters/multicast/.libs \
	LD_LIBRARY_PATH=$(top_builddir)/gst-libs/gst/farsight/.libs:${LD_LIBRARY_PATH}


# ths core dumps of some machines have PIDs appended
CLEANFILES = core* test-registry.xml

clean-local: clean-local-check

$(CHECK_REGISTRY):
	$(TESTS_ENVIRONMENT)


TESTS = $(check_PROGRAMS)


SUPPRESSIONS = $(top_srcdir)/common/gst.supp

#######
# From here.. Its a list of our tests and their sub stuff
#
check_PROGRAMS = \
	base/fscodec \
	base/fstransmitter \
	transmitter/rawudp \
	transmitter/multicast \
	rtp/codecs \
	rtp/sendcodecs \
	rtp/conference \
	utils/binadded


AM_CFLAGS = \
	$(CFLAGS) \
	$(FS2_INTERNAL_CFLAGS) \
	$(FS2_CFLAGS) \
	$(GST_CHECK_CFLAGS) \
	$(GST_CFLAGS)

LDADD = \
	$(top_builddir)/gst-libs/gst/farsight/libgstfarsight-0.10.la \
	$(GST_CHECK_LIBS) \
	$(GST_LIBS)

transmitter_rawudp_CFLAGS = $(AM_CFLAGS)
transmitter_rawudp_SOURCES = \
	check-threadsafe.h  \
	transmitter/generic.c \
	transmitter/generic.h \
	transmitter/rawudp.c 

transmitter_multicast_CFLAGS = $(AM_CFLAGS)
transmitter_multicast_SOURCES = \
	check-threadsafe.h  \
	transmitter/generic.c \
	transmitter/generic.h \
	transmitter/multicast.c 

rtp_conference_CFLAGS = $(AM_CFLAGS)
rtp_conference_SOURCES = \
	check-threadsafe.h  \
	rtp/generic.c \
	rtp/generic.h \
	rtp/conference.c

rtp_codecs_CFLAGS = $(AM_CFLAGS)
rtp_codecs_SOURCES = \
	rtp/generic.c \
	rtp/generic.h \
	rtp/codecs.c

rtp_sendcodecs_CFLAGS = $(AM_CFLAGS)
rtp_sendcodecs_LDADD = $(LDADD) -lgstrtp-0.10
rtp_sendcodecs_SOURCES = \
	rtp/generic.c \
	rtp/generic.h \
	rtp/sendcodecs.c

utils_binadded_CFLAGS = $(AM_CFLAGS)
utils_binadded_SOURCES = \
	utils/binadded.c

