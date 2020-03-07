#!/bin/bash

die() {
    echo ">>> FAIL: $@"
    exit 1
}

is_in_set() {
    local v="$1"
    shift
    for w; do
        [[ "$v" == "$w" ]] && return 0
    done
    return 1
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

fedora_install_libnma() {
    pushd /etc/yum.repos.d
        $CMD curl 'https://gitlab.gnome.org/thaller/network-manager-applet/-/raw/96bd01b5ef1a7e24bcf4bc945aa7465ac02915a8/libnma-local.tgz' --output ./libnma-local.tgz
        $CMD tar -xvzf ./libnma-local.tgz
        $CMD dnf -y install --nogpgcheck --enablerepo=libnma-local libnma-devel
    popd
}

fedora_install_minimal() {
    $CMD dnf -y install $(fedora_pkg_minimal) $(fedora_pkg_build "$1")
    fedora_install_libnma
}

fedora_install_full() {
    $CMD dnf -y install $(fedora_pkg_full) $(fedora_pkg_build "$1")
    fedora_install_libnma
}


set -xe

if is_in_set "$1" fedora_install_minimal fedora_install_full; then
    is_in_set "$2" autotools make meson || die "invalid argument \$2 (\"$2\")"
    "$1" "$2"
    exit 0
fi

die "Unknown command \"$1\""
