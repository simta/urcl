ACLOCAL_AMFLAGS = -I m4
AM_CPPFLAGS = -I.

pkgconfigdir = $(libdir)/pkgconfig

lib_LTLIBRARIES = liburcl.la
liburcl_la_CFLAGS = -Wall $(HIREDIS_CFLAGS)
liburcl_la_LIBADD = $(HIREDIS_LIBS)
liburcl_la_SOURCES = urcl.c urcl.h
liburcl_la_LDFLAGS = -version-info 2:0:0

include_HEADERS = urcl.h

nodist_pkgconfig_DATA = packaging/pkgconfig/urcl.pc

EXTRA_DIST = VERSION packaging/rpm/urcl.spec

rpm: dist-gzip
	rpmbuild -ta $(distdir).tar.gz
