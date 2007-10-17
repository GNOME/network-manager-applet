noinst_LTLIBRARIES = libwireless-security.la

libwireless_security_la_SOURCES = \
	wireless-security.h \
	wireless-security.c \
	ws-wep-key.h \
	ws-wep-key.c \
	ws-wep-passphrase.h \
	ws-wep-passphrase.c \
	ws-wpa-psk.h \
	ws-wpa-psk.c \
	sha1.c \
	sha1.h \
	ws-leap.h \
	ws-leap.c \
	ws-wpa-eap.h \
	ws-wpa-eap.c \
	eap-method.h \
	eap-method.c \
	eap-method-tls.h \
	eap-method-tls.c \
	eap-method-leap.h \
	eap-method-leap.c \
	eap-method-ttls.h \
	eap-method-ttls.c \
	eap-method-simple.h \
	eap-method-simple.c

libwireless_security_la_CPPFLAGS = \
	$(NMA_CFLAGS) \
	-I${top_srcdir}/src/utils

libwireless_security_la_LIBADD = \
	$(NMA_LIBS) \
	${top_builddir}/src/utils/libutils.la

