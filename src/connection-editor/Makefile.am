bin_PROGRAMS = nm-connection-editor

nm_connection_editor_CPPFLAGS = \
	$(GTK_CFLAGS) \
	$(LIBNM_CFLAGS) \
	$(JANSSON_CFLAGS) \
	-DNM_VERSION_MIN_REQUIRED=NM_VERSION_1_4 \
	-DNM_VERSION_MAX_ALLOWED=NM_VERSION_1_4 \
	-DICONDIR=\""$(datadir)/icons"\" \
	-DBINDIR=\""$(bindir)"\" \
	-DSYSCONFDIR=\""$(sysconfdir)"\" \
	-DLIBDIR=\""$(libdir)"\" \
	-DDATADIR=\""$(datadir)"\" \
	-DNMALOCALEDIR=\"$(datadir)/locale\" \
	"-I${top_srcdir}/shared/" \
	-I${top_srcdir}/src/utils \
	-I${top_srcdir}/src/wireless-security \
	-I${top_srcdir}/src/libnma

BUILT_SOURCES = \
	ce-resources.h \
	ce-resources.c

nm_connection_editor_SOURCES = \
	ce-resources.h \
	ce-resources.c \
	nm-connection-editor.c \
	nm-connection-editor.h \
	nm-connection-list.c \
	nm-connection-list.h \
	main.c \
	ce-page.h \
	ce-page.c \
	page-general.h \
	page-general.c \
	page-ethernet.h \
	page-ethernet.c \
	page-8021x-security.h \
	page-8021x-security.c \
	page-wifi.h \
	page-wifi.c \
	page-wifi-security.h \
	page-wifi-security.c \
	page-infiniband.h \
	page-infiniband.c \
	page-ip-tunnel.h \
	page-ip-tunnel.c \
	page-ip4.h \
	page-ip4.c \
	page-ip6.h \
	page-ip6.c \
	page-dsl.h \
	page-dsl.c \
	page-mobile.h \
	page-mobile.c \
	page-bluetooth.h \
	page-bluetooth.c \
	page-ppp.h \
	page-ppp.c \
	page-proxy.h \
	page-proxy.c \
	page-vpn.h \
	page-vpn.c \
	page-master.h \
	page-master.c \
	page-bond.h \
	page-bond.c \
	page-team.h \
	page-team.c \
	page-team-port.h \
	page-team-port.c \
	page-bridge.h \
	page-bridge.c \
	page-bridge-port.h \
	page-bridge-port.c \
	page-vlan.h \
	page-vlan.c \
	page-dcb.c \
	page-dcb.h \
	vpn-helpers.h \
	vpn-helpers.c \
	ip4-routes-dialog.h \
	ip4-routes-dialog.c \
	ip6-routes-dialog.h \
	ip6-routes-dialog.c \
	ppp-auth-methods-dialog.c \
	ppp-auth-methods-dialog.h \
	ce-polkit-button.c \
	ce-polkit-button.h \
	connection-helpers.c \
	connection-helpers.h

nm_connection_editor_LDADD = \
	${top_builddir}/src/wireless-security/libwireless-security-libnm.la \
	${top_builddir}/src/utils/libutils-libnm.la \
	${top_builddir}/src/libnma/libnma.la \
	$(GTK_LIBS) \
	$(LIBNM_LIBS) \
	$(JANSSON_LIBS) \
	-lm

ce-resources.h: $(srcdir)/ce.gresource.xml
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) $< --target=$@ --sourcedir=$(srcdir) --generate-header --internal

ce-resources.c: $(srcdir)/ce.gresource.xml $(shell $(GLIB_COMPILE_RESOURCES) --sourcedir=$(srcdir) --generate-dependencies $(srcdir)/ce.gresource.xml)
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) $< --target=$@ --sourcedir=$(srcdir) --generate-source --internal

EXTRA_DIST = \
	ce-ip4-routes.ui \
	ce-ip6-routes.ui \
	ce-new-connection.ui \
	ce-page-bluetooth.ui \
	ce-page-bond.ui \
	ce-page-bridge-port.ui \
	ce-page-bridge.ui \
	ce-page-dcb.ui \
	ce-page-dsl.ui \
	ce-page-ethernet.ui \
	ce-page-general.ui \
	ce-page-infiniband.ui \
	ce-page-ip4.ui \
	ce-page-ip6.ui \
	ce-page-ip-tunnel.ui \
	ce-page-mobile.ui \
	ce-page-ppp.ui \
	ce-page-proxy.ui \
	ce-page-team-port.ui \
	ce-page-team.ui \
	ce-page-vlan.ui \
	ce-page-wifi-security.ui \
	ce-page-wifi.ui \
	ce-ppp-auth-methods.ui \
	nm-connection-editor.ui \
	ce.gresource.xml
