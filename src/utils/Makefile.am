noinst_LTLIBRARIES = libutils.la

libutils_la_SOURCES = \
	gnome-keyring-md5.h \
	gnome-keyring-md5.c \
	utils.c \
	utils.h \
	crypto.c \
	crypto.h

libutils_la_CPPFLAGS = \
	$(NMA_CFLAGS) \
	-I${top_srcdir}/src/gconf-helpers

libutils_la_LIBADD = $(NMA_LIBS)

if WITH_GNUTLS
libutils_la_SOURCES += crypto_gnutls.c
libutils_la_CPPFLAGS += $(LIBGCRYPT_CFLAGS) $(GNUTLS_CFLAGS)
libutils_la_LIBADD += $(LIBGCRYPT_LIBS) $(GNUTLS_LIBS)
endif

if WITH_NSS
libutils_la_SOURCES += crypto_nss.c
libutils_la_CPPFLAGS += $(NSS_CFLAGS)
libutils_la_LIBADD += $(NSS_LIBS)
endif

noinst_PROGRAMS = test-crypto

test_crypto_SOURCES = test-crypto.c
test_crypto_CPPFLAGS = $(NMA_CFLAGS) -D_GNU_SOURCE
test_crypto_LDADD = $(NMA_LIBS) ${top_builddir}/src/utils/libutils.la
