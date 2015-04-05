# Process this file with autoconf to produce a configure script.

AC_INIT(urcl, m4_esyscmd([bash version.sh]), paul.arthur@flowerysong.com)
AM_INIT_AUTOMAKE([foreign])
AC_PREREQ(2.63)
AC_CONFIG_HEADER(config.h)
AC_COPYRIGHT([Copyright (c) 2015 Paul Arthur MacIain])

AC_CONFIG_MACRO_DIR([m4])

AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LIBTOOL

PKG_CHECK_MODULES([HIREDIS], [hiredis])

AC_CONFIG_FILES(Makefile packaging/pkgconfig/urcl.pc packaging/rpm/urcl.spec)
AC_OUTPUT
