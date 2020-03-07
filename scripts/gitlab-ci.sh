#!/bin/bash

die() {
    echo ">>> FAIL: $@"
    exit 1
}

fedora_pkg_minimal() {
    echo \
        /usr/bin/autopoint \
        NetworkManager-libnm-devel \
        desktop-file-utils \
        fedora-repos-rawhide \
        file \
        findutils \
        gcc \
        gettext-devel \
        glib2-devel \
        gtk3-devel \
        libgudev1-devel \
        libnotify-devel \
        libsecret-devel \
        libtool \
        pkgconfig \
        ;
}

fedora_pkg_full() {
    echo \
        $(fedora_pkg_minimal) \
        redhat-rpm-config \
        jansson-devel \
        ModemManager-glib-devel \
        libselinux-devel \
        ;
}

fedora_install_minimal() {
    $CMD dnf -y install $(fedora_pkg_minimal)
    $CMD dnf -y install --nogpgcheck --enablerepo=rawhide libnma-devel
}

fedora_install_full() {
    $CMD dnf -y install $(fedora_pkg_full)
    $CMD dnf -y install --nogpgcheck --enablerepo=rawhide libnma-devel
}

set -xe

case "$*" in
    fedora_install_minimal)
        fedora_install_minimal
        ;;
    fedora_install_full)
        fedora_install_full
        ;;
    *)
        die "Invalid argument"
        ;;
esac
