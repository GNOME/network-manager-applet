#!/bin/bash

die() {
    echo ">>> FAIL: $@"
    exit 1
}

is_in_set() {
    local v="$1"
    shift
    for w ; do
        [[ "$v" == "$w" ]] && return 0
    done
    return 1
}

fedora_pkg_minimal() {
    echo \
        /usr/bin/autopoint \
        NetworkManager-libnm-devel \
        libnma-devel \
        desktop-file-utils \
        fedora-repos-rawhide \
        file \
        findutils \
        gcc \
        gettext-devel \
        glib2-devel \
        gtk3-devel \
        libsecret-devel \
        libtool \
        pkgconfig \
        #
}

fedora_pkg_full() {
    echo \
        $(fedora_pkg_minimal) \
        redhat-rpm-config \
        jansson-devel \
        ModemManager-glib-devel \
        libselinux-devel \
        libappindicator-gtk3-devel \
        libdbusmenu-gtk3-devel \
        #
}

fedora_pkg_build() {
    case "$1" in
        autotools)
            echo autoconf automake make
            ;;
        make)
            echo make
            ;;
        meson)
            echo meson
            ;;
        "")
            ;;
        *)
            die "Unknown build option \"$1\""
            ;;
    esac
}

fedora_install_minimal() {
    $CMD dnf -y install $(fedora_pkg_minimal) $(fedora_pkg_build "$1")
}

fedora_install_full() {
    $CMD dnf -y install $(fedora_pkg_full) $(fedora_pkg_build "$1")
}


set -xe

if is_in_set "$1" fedora_install_minimal fedora_install_full ; then
    is_in_set "$2" autotools make meson || die "invalid argument \$2 (\"$2\")"
    "$1" "$2"
    exit 0
fi

die "Unknown command \"$1\""
