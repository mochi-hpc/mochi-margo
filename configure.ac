#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.63])

# IMPORTANT: when editing the version  number, edit both the
# MARGO_VERSION_ variables and the argument provided to AC_INIT
AC_INIT([margo], [0.19.2], [],[],[])

MARGO_VERSION_MAJOR=0
MARGO_VERSION_MINOR=19
MARGO_VERSION_PATCH=2
MARGO_VERSION="$MARGO_VERSION_MAJOR.$MARGO_VERSION_MINOR.$MARGO_VERSION_PATCH"
MARGO_VERSION_NUM=$((MARGO_VERSION_MAJOR*100000+MARGO_VERSION_MINOR*100+MARGO_VERSION_PATCH))

AC_SUBST([MARGO_VERSION], ["$MARGO_VERSION"])
AC_SUBST([MARGO_VERSION_MAJOR], ["$MARGO_VERSION_MAJOR"])
AC_SUBST([MARGO_VERSION_MINOR], ["$MARGO_VERSION_MINOR"])
AC_SUBST([MARGO_VERSION_PATCH], ["$MARGO_VERSION_PATCH"])
AC_SUBST([MARGO_VERSION_NUM], ["$MARGO_VERSION_NUM"])

AC_CONFIG_MACRO_DIR([m4])
LT_INIT

AC_CANONICAL_TARGET
AC_CANONICAL_SYSTEM
AC_CANONICAL_HOST

AM_INIT_AUTOMAKE([foreign subdir-objects -Wall])

# we should remove this soon, only needed for automake 1.10 and older
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_CONFIG_SRCDIR([README.md])
AC_CONFIG_HEADERS([margo-config-private.h])

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_CXX
AC_PROG_CXXCPP

AC_PROG_MKDIR_P

# coreutils checks for OSX
AC_ARG_VAR([TIMEOUT], timeout program)
AC_ARG_VAR([MKTEMP], mktemp program)

if test -z "$TIMEOUT" ; then
    AC_CHECK_PROGS(TIMEOUT, [timeout gtimeout])
    if test -z "$TIMEOUT" ; then
        AC_MSG_WARN([Could not find timeout command (can optionally provide via the TIMEOUT variable): tests disabled])
    fi
else
    AC_SUBST([TIMEOUT], ["$TIMEOUT"])
fi
AM_CONDITIONAL([BUILD_TESTS], [test x$TIMEOUT != x])

if test -z "$MKTEMP" ; then
    AC_CHECK_PROGS(MKTEMP, [mktemp gmktemp])
    if test -z "$MKTEMP" ; then
        AC_MSG_ERROR([Could not find mktemp command (can optionally provide via the MKTEMP variable)])
    fi
else
    AC_SUBST([MKTEMP], ["$MKTEMP"])
fi

AC_REQUIRE_CPP

AC_CHECK_SIZEOF([long int])

dnl
dnl margo relies on pthreads because of prio_pool implementation and
dnl argobots profiling shim
dnl
AX_PTHREAD([],
    [AC_MSG_ERROR([Could not find working pthreads])])
LIBS="$PTHREAD_LIBS $LIBS"
CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
dnl subst for .pc file generation
AC_SUBST([PTHREAD_LIBS], ["$PTHREAD_LIBS"])
AC_SUBST([PTHREAD_CFLAGS], ["$PTHREAD_CFLAGS"])

dnl
dnl Verify pkg-config
dnl
PKG_PROG_PKG_CONFIG
if test "x$PKG_CONFIG" == "x"; then
   AC_MSG_ERROR([Could not find pkg-config utility!])
fi

PKG_CHECK_MODULES([MERCURY],[mercury],[],
   [AC_MSG_ERROR([Could not find working mercury installation!])])
LIBS="$MERCURY_LIBS $LIBS"
CPPFLAGS="$MERCURY_CFLAGS $CPPFLAGS"
CFLAGS="$MERCURY_CFLAGS $CFLAGS"
# find the path containing the Mercury libraries
MERCURY_LIB_PATH=`pkg-config --libs-only-L mercury | sed -e 's/-L//g'`
AC_SUBST([MERCURY_LIB_PATH], ["$MERCURY_LIB_PATH"])

PKG_CHECK_MODULES([ARGOBOTS],[argobots >= 1.1],[],
   [AC_MSG_ERROR([Could not find working argobots installation!])])
LIBS="$ARGOBOTS_LIBS $LIBS"
CPPFLAGS="$ARGOBOTS_CFLAGS $CPPFLAGS"
CFLAGS="$ARGOBOTS_CFLAGS $CFLAGS"

PKG_CHECK_MODULES([JSONC],[json-c],[],
   [AC_MSG_ERROR([Could not find working json-c installation!])])
LIBS="$JSONC_LIBS $LIBS"
dnl
dnl Note that pkg-config may report an include path that contains a
dnl "/json-c" component.  If so, strip it out.  We prefer to use an explicit
dnl subdir path in the source to to avoid potential header name conflicts
dnl with other json libraries.
dnl
JSONC_CFLAGS=`echo $JSONC_CFLAGS | sed 's/\/include\/json-c/\/include/g'`
CPPFLAGS="$JSONC_CFLAGS $CPPFLAGS"
CFLAGS="$JSONC_CFLAGS -DMARGO_USE_JSON_C $CFLAGS"

PC_REQUIRES="mercury argobots json-c"
AC_SUBST([PC_REQUIRES], ["$PC_REQUIRES"])

# turn off a warning that is tripping up our use of utlist at the moment
CFLAGS="-Wno-address $CFLAGS"

# TODO: the mercury tests should probably be in their own .m4
# make sure that mercury has boost preprocessors enabled
AC_MSG_CHECKING(if Boost preprocessing is enabled in Mercury)
AC_TRY_COMPILE([
#include <mercury_macros.h>
#ifndef HG_HAS_BOOST
#error HG_HAS_BOOST not set
#endif
], [],
AC_MSG_RESULT(yes),
AC_MSG_RESULT(no)
AC_MSG_ERROR([Boost preprocessing not enabled in Mercury])
)

# make sure that argobots has the ABT_SCHED_BASIC_WAIT scheduler
AC_MSG_CHECKING(for ABT_SCHED_BASIC_WAIT in Argobots)
AC_TRY_COMPILE([
#include <abt.h>
], [
enum ABT_sched_predef sched = ABT_SCHED_BASIC_WAIT;
],
AC_MSG_RESULT(yes),
AC_MSG_RESULT(no)
AC_MSG_ERROR([ABT_SCHED_BASIC_WAIT not available in Argobots])
)

# check if this version of Argobots supports querying lazy stack allocation
# status
AC_MSG_CHECKING(for ABT_INFO_QUERY_KIND_ENABLED_LAZY_STACK_ALLOC in Argobots)
AC_TRY_COMPILE([
#include <abt.h>
], [
enum ABT_info_query_kind query_kind = ABT_INFO_QUERY_KIND_ENABLED_LAZY_STACK_ALLOC;
],
AC_MSG_RESULT(yes)
AC_DEFINE(HAVE_ABT_INFO_QUERY_KIND_ENABLED_LAZY_STACK_ALLOC, 1, [can query lazy stack alloc support]),
AC_MSG_RESULT(no)
)

# check if this version of Argobots supports ABT_pool_pop_threads
AC_CHECK_FUNC(ABT_pool_pop_threads,
AC_DEFINE(HAVE_ABT_POOL_POP_THREADS, 1, [can use ABT_pool_pop_threads]),
[]
)

# check if dl_iterate_phdr() is available
AC_MSG_CHECKING(for dl_iterate_phdr)
AC_TRY_LINK([
#define _GNU_SOURCE
#include <link.h>
#include <stdio.h>
], [
dl_iterate_phdr(NULL, NULL);
],
AC_MSG_RESULT(yes)
AC_DEFINE(HAVE_DL_ITERATE_PHDR, 1, [dl_iterate_phdr function is available]),
AC_MSG_RESULT(no)
)

AC_ARG_ENABLE(coverage,
              [AS_HELP_STRING([--enable-coverage],[Enable code coverage @<:@default=no@:>@])],
              [case "${enableval}" in
                yes) enable_coverage="yes" ;;
                no) enable_coverage="no" ;;
                *) AC_MSG_ERROR(bad value ${enableval} for --enable-coverage) ;;
               esac],
              [enable_coverage="no"]
)

if test "$enable_coverage" = "yes" ; then
    CPPFLAGS="$CPPFLAGS --coverage -O0"
    LDFLAGS="--coverage -lgcov"
fi

AC_ARG_WITH([mochi-plumber],
[AS_HELP_STRING([--with-mochi-plumber],
  [enable support for mochi-plumber])],
[],
[with_mochi_plumber=no])

AS_IF([test "x$with_mochi_plumber" != xno],
    [PKG_CHECK_MODULES([MOCHI_PLUMBER],[mochi-plumber],[],
        [AC_MSG_ERROR([Could not find working mochi-plumber installation!])])
    AC_DEFINE(HAVE_MOCHI_PLUMBER, 1, [mochi-plumber support enabled])
    LIBS="$MOCHI_PLUMBER_LIBS $LIBS"
    CPPFLAGS="$MOCHI_PLUMBER_CFLAGS $CPPFLAGS"
    CFLAGS="$MOCHI_PLUMBER_CFLAGS $CFLAGS"])

AC_CONFIG_LINKS([tests/unit-tests/test-configs.json:tests/unit-tests/test-configs.json])

AC_CONFIG_FILES([Makefile maint/margo.pc maint/margo-hg-shim.pc include/margo-version.h])
AC_OUTPUT
