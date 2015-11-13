#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.67])
AC_INIT([margo], [0.1], [],[],[]) 

AC_CANONICAL_TARGET
AC_CANONICAL_SYSTEM
AC_CANONICAL_HOST

AM_INIT_AUTOMAKE([foreign subdir-objects -Wall])

# we should remove this soon, only needed for automake 1.10 and older
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_CONFIG_SRCDIR([README.md])
AC_CONFIG_HEADERS([margo-config.h])

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_CXX
AC_PROG_CXXCPP
AC_PROG_RANLIB

AC_PROG_MKDIR_P

AC_REQUIRE_CPP

AC_CHECK_SIZEOF([long int])

dnl
dnl Verify pkg-config
dnl
PKG_PROG_PKG_CONFIG
PKG_CONFIG="pkg-config --static"

PKG_CHECK_MODULES_STATIC([MERCURY],[mercury],[],
   [AC_MSG_ERROR([Could not find working mercury installation!])])
LIBS="$MERCURY_LIBS $LIBS"
CPPFLAGS="$MERCURY_CFLAGS $CPPFLAGS"
CFLAGS="$MERCURY_CFLAGS $CFLAGS"

PKG_CHECK_MODULES_STATIC([ARGOBOTS],[argobots],[],
   [AC_MSG_ERROR([Could not find working argobots installation!])])
LIBS="$ARGOBOTS_LIBS $LIBS"
CPPFLAGS="$ARGOBOTS_CFLAGS $CPPFLAGS"
CFLAGS="$ARGOBOTS_CFLAGS $CFLAGS"

PKG_CHECK_MODULES_STATIC([ABT_SNOOZER],[abt-snoozer],[],
   [AC_MSG_ERROR([Could not find working abt-snoozer installation!])])
LIBS="$ABT_SNOOZER_LIBS $LIBS"
CPPFLAGS="$ABT_SNOOZER_CFLAGS $CPPFLAGS"
CFLAGS="$ABT_SNOOZER_CFLAGS $CFLAGS"

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

AC_CONFIG_FILES([Makefile maint/margo.pc])
AC_OUTPUT
 
