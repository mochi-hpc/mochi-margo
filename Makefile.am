AUTOMAKE_OPTIONS = foreign
ACLOCAL_AMFLAGS = -I m4

bin_PROGRAMS =
bin_SCRIPTS =
dist_bin_SCRIPTS = scripts/margo-gen-profile
dist_noinst_SCRIPTS = scripts/margo-gen-profile

noinst_PROGRAMS =
noinst_HEADERS =
TESTS =
XFAIL_TESTS = 
check_PROGRAMS =
EXTRA_PROGRAMS =
CLEANFILES = $(bin_SCRIPTS)
MAINTAINERCLEANFILES =
EXTRA_DIST =
BUILT_SOURCES =
include_HEADERS = \
 include/margo.h \
 include/margo-bulk-pool.h \
 include/margo-diag.h
 
TESTS_ENVIRONMENT =

EXTRA_DIST += \
 prepare.sh 

AM_CPPFLAGS = -I$(top_srcdir)/include

AM_CFLAGS =

AM_LIBS =

AM_CXXFLAGS = $(AM_CFLAGS)

lib_LTLIBRARIES = src/libmargo.la
src_libmargo_la_SOURCES =

LDADD = src/libmargo.la

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = maint/margo.pc

include Make.rules

include $(top_srcdir)/src/Makefile.subdir
include $(top_srcdir)/examples/Makefile.subdir
include $(top_srcdir)/examples/multiplex/Makefile.subdir
include $(top_srcdir)/examples/composition/Makefile.subdir
include $(top_srcdir)/tests/Makefile.subdir

