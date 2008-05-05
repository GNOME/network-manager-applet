noinst_LTLIBRARIES = libmarshallers.la

BUILT_SOURCES = \
	nma-marshal.h \
	nma-marshal.c

libmarshallers_la_SOURCES = \
	nma-marshal-main.c

libmarshallers_la_CPPFLAGS = \
	$(GOBJECT_CFLAGS) \
	-g \
	-Wall \
	-DG_DISABLE_DEPRECATED


libmarshallers_la_LIBADD = $(GOBJECT_LIBS)

EXTRA_DIST = nma-marshal.list
CLEANFILES = $(BUILT_SOURCES)

nma-marshal.h: nma-marshal.list
	$(GLIB_GENMARSHAL) --prefix=nma_marshal $(srcdir)/nma-marshal.list --header > \
		xgen-gmh \
	&& (cmp -s xgen-gmh nma-marshal.h || cp xgen-gmh nma-marshal.h) \
	&& rm -f xgen-gmh xgen-gmh~

nma-marshal.c: nma-marshal.list
	$(GLIB_GENMARSHAL) --prefix=nma_marshal $(srcdir)/nma-marshal.list --body > \
		xgen-gmc \
	&& cp xgen-gmc nma-marshal.c \
	&& rm -f xgen-gmc xgen-gmc~

nma-marshal-main.c: nma-marshal.c nma-marshal.h

