AUTOMAKE_OPTIONS = foreign
ACLOCAL_AMFLAGS = -I m4

bin_PROGRAMS =
bin_SCRIPTS =

noinst_PROGRAMS =
noinst_HEADERS =
TESTS =
XFAIL_TESTS =
check_PROGRAMS =
EXTRA_PROGRAMS =
CLEANFILES = $(bin_SCRIPTS)
DISTCLEANFILES =
MAINTAINERCLEANFILES =
EXTRA_DIST =
BUILT_SOURCES =
include_HEADERS = \
 include/margo.h \
 include/margo-config.h \
 include/margo-logging.h \
 include/margo-bulk-pool.h \
 include/margo-util.h \
 include/margo-bulk-util.h \
 include/margo-timer.h \
 include/margo-monitoring.h \
 include/margo-version.h \
 include/margo-hg-shim.h

TESTS_ENVIRONMENT =

EXTRA_DIST += \
 prepare.sh

AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_builddir)/include

AM_CFLAGS =

AM_LIBS =

AM_CXXFLAGS = $(AM_CFLAGS)

lib_LTLIBRARIES = src/libmargo.la src/libmargo-hg-shim.la
src_libmargo_la_SOURCES =
src_libmargo_hg_shim_la_SOURCES =

LDADD = src/libmargo.la src/libmargo-hg-shim.la

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = maint/margo.pc maint/margo-hg-shim.pc

include Make.rules

include $(top_srcdir)/src/Makefile.subdir
include $(top_srcdir)/examples/Makefile.subdir
include $(top_srcdir)/examples/multiplex/Makefile.subdir
include $(top_srcdir)/examples/composition/Makefile.subdir
include $(top_srcdir)/tests/Makefile.subdir
include $(top_srcdir)/tests/unit-tests/Makefile.subdir

# Modify the .la file once installed to indicate that codes linking against
# the Margo library should also set rpath to the specific Mercury library
# that we compiled against to make sure that Mercury function calls from
# margo.h are resolved correctly. This is only relevant for other packages
# that use libtool.
install-exec-hook:
	sed -i -e "s+inherited_linker_flags=''+inherited_linker_flags='-Wl,-rpath=@MERCURY_LIB_PATH@'+" $(DESTDIR)$(libdir)/libmargo.la
