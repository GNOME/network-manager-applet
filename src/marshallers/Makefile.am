noinst_LTLIBRARIES = libmarshallers.la

BUILT_SOURCES = \
	nma-marshal.h \
	nma-marshal.c

libmarshallers_la_SOURCES = \
	nma-marshal-main.c

libmarshallers_la_CPPFLAGS = \
	$(GOBJECT_CFLAGS) \
	-DG_DISABLE_DEPRECATED


libmarshallers_la_LIBADD = $(GOBJECT_LIBS)

EXTRA_DIST = nma-marshal.list
CLEANFILES = $(BUILT_SOURCES)

nma-marshal.h: nma-marshal.list
	$(GLIB_GENMARSHAL) $< --prefix=nma_marshal --header > $@

nma-marshal.c: nma-marshal.list
	$(GLIB_GENMARSHAL) $< --prefix=nma_marshal --body > $@

nma-marshal-main.c: nma-marshal.c nma-marshal.h

