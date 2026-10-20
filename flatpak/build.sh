#!/bin/sh
# Build Flatpak 1.18.4 and the libraries Portage doesn't have into
# ~/.local/opt/flatpak (0 Portage packages). m4/bison/gperf/pyparsing are
# build-only and go to $W/hostbin; sources and build trees live in $W
# (FLATPAK_WORK, default ~/.cache/flatpak-local). CFLAGS default to this
# laptop's make.conf; set OPT_CFLAGS to use others.
# Usage: build.sh [step...]   steps: fetch tools glibtools gnupg libs
#   appstream ostree flatpak trim (default: all; trim drops headers/.pc, so a
#   single step after a trim needs the earlier ones again)
set -eu
W=${FLATPAK_WORK:-$HOME/.cache/flatpak-local}
S=$W/src
B=$W/build
T=$W/hostbin
P=$HOME/.local/opt/flatpak
J=$(nproc)

NOHARDEN="-fno-stack-protector -fcf-protection=none -fno-stack-clash-protection -U_FORTIFY_SOURCE"
export CFLAGS="${OPT_CFLAGS:--march=native -O3 -pipe -flto -fno-semantic-interposition $NOHARDEN}"
export CXXFLAGS="$CFLAGS"
export LDFLAGS="$CFLAGS -Wl,-O1 -Wl,--as-needed -Wl,-z,pack-relative-relocs -Wl,-z,lazy -Wl,-z,norelro -Wl,-rpath,$P/lib"
export PATH="$T/bin:$P/bin:$PATH"
export PKG_CONFIG_PATH="$T/pkgconfig:$P/lib/pkgconfig:$P/share/pkgconfig"
export PYTHONPATH="$T/py"
mkdir -p "$B" "$T" "$P"

# at <srcdir> <prefix> [configure args...]
at() {
	src=$S/$1 pre=$2; shift 2
	rm -rf "$B/${src##*/}"; mkdir -p "$B/${src##*/}"; cd "$B/${src##*/}"
	"$src/configure" --prefix="$pre" --disable-static "$@" >configure.log
	make -j$J >make.log 2>&1 || { tail -40 make.log; exit 1; }
	make install >install.log
}

# ms <srcdir> [meson args...]
ms() {
	src=$S/$1; shift
	rm -rf "$B/${src##*/}"
	meson setup "$B/${src##*/}" "$src" --prefix="$P" --libdir=lib --buildtype=plain \
		-Ddefault_library=shared "$@" >"$B/${src##*/}.setup.log" || { tail -40 "$B/${src##*/}.setup.log"; exit 1; }
	ninja -C "$B/${src##*/}" >"$B/${src##*/}.ninja.log" || { tail -40 "$B/${src##*/}.ninja.log"; exit 1; }
	meson install -C "$B/${src##*/}" --no-rebuild >/dev/null
}

gpgpre="--with-libgpg-error-prefix=$P --with-libassuan-prefix=$P --with-npth-prefix=$P --with-libgcrypt-prefix=$P --with-ksba-prefix=$P"

step_fetch() {
	mkdir -p "$W/dl" "$S"
	sh "$(dirname "$(realpath "$0")")/fetch.sh" "$W/dl"
	for f in "$W"/dl/*.tar.*; do tar -C "$S" -xf "$f"; done
}
step_tools() {
	at m4-1.4.21 "$T"
	at bison-3.8.2 "$T" --disable-nls
	at gperf-3.3 "$T"
	rm -rf "$T/py"; mkdir -p "$T/py"
	python3 -I -m zipfile -e "$W/dl/pyparsing-3.3.3-py3-none-any.whl" "$T/py"
}
# Gentoo splits glib-mkenums/genmarshal and gdbus-codegen into separate
# packages; take them from the matching glib tarball and point glib's .pc at them.
step_glibtools() {
	g=$S/glib-2.88.2 v=2.88.2
	mkdir -p "$T/bin" "$T/share/glib-2.0/codegen" "$T/pkgconfig"
	for t in glib-mkenums glib-genmarshal; do
		sed "s|@PYTHON@|/usr/bin/python3|; s|@VERSION@|$v|" "$g/gobject/$t.in" >"$T/bin/$t"
	done
	sed "s|@PYTHON@|/usr/bin/python3|; s|@DATADIR@|$T/share|" "$g/gio/gdbus-2.0/codegen/gdbus-codegen.in" >"$T/bin/gdbus-codegen"
	chmod +x "$T/bin/glib-mkenums" "$T/bin/glib-genmarshal" "$T/bin/gdbus-codegen"
	cp "$g"/gio/gdbus-2.0/codegen/*.py "$T/share/glib-2.0/codegen/"
	sed "s|@VERSION@|$v|; s|@MAJOR_VERSION@|2|; s|@MINOR_VERSION@|88|" "$g/gio/gdbus-2.0/codegen/config.py.in" >"$T/share/glib-2.0/codegen/config.py"
	for pc in glib-2.0 gio-2.0; do
		sed -E "s#^(glib_mkenums|glib_genmarshal|gdbus_codegen)=.*/([a-z-]+)\$#\\1=$T/bin/\\2#" /usr/lib64/pkgconfig/$pc.pc >"$T/pkgconfig/$pc.pc"
	done
}
step_gnupg() {
	at libgpg-error-1.61 "$P" --disable-doc --disable-tests --disable-nls --disable-languages
	at libassuan-3.0.2 "$P" --disable-doc $gpgpre
	at npth-1.8 "$P" --disable-tests
	at libgcrypt-1.12.4 "$P" --disable-doc $gpgpre
	at libksba-1.8.1 "$P" $gpgpre
	at gnupg-2.5.24 "$P" $gpgpre --disable-doc --disable-nls --disable-gpgsm --disable-scdaemon \
		--disable-dirmngr --disable-keyboxd --disable-tpm2d --disable-gpgtar --disable-wks-tools \
		--disable-card-support --disable-ccid-driver --disable-sqlite --disable-ntbtls --disable-gnutls \
		--disable-ldap --disable-photo-viewers --disable-bzip2 --without-readline
	at gpgme-2.2.0 "$P" $gpgpre --disable-gpg-test --disable-gpgsm-test --disable-gpgconf-test \
		--disable-g13-test
}
step_libs() {
	at libseccomp-2.6.1 "$P" --disable-python
	at libarchive-3.8.10 "$P" --disable-bsdtar --disable-bsdcpio --disable-bsdcat --disable-bsdunzip \
		--disable-acl --without-bz2lib --without-libb2 --without-iconv --without-lz4 --without-cng \
		--without-openssl --without-nettle --without-xml2 --without-expat --without-mbedtls
	ms json-glib-1.10.8 -Dintrospection=disabled -Ddocumentation=disabled -Dgtk_doc=disabled \
		-Dman=false -Dtests=false -Dconformance=false -Dnls=disabled -Dinstalled_tests=false
	ms libxmlb-0.3.29 -Dgtkdoc=false -Dintrospection=false -Dtests=false -Dcli=false \
		-Dstemmer=false -Dlzma=enabled -Dzstd=enabled
	at libfyaml-0.9.6 "$P"
	libs_appstream
}
libs_appstream() {
	# po/ and data/ need gettext + itstool + appstreamcli for translated
	# metainfo; flatpak only needs the library and its default config.
	sed -i "s|^subdir('po/')|# &|; s|^subdir('tests/')|# &|; s|^subdir('data/')|install_data('data/appstream.conf', install_dir: as_data_installdir)|" \
		"$S/AppStream-1.2.1/meson.build"
	ms AppStream-1.2.1 -Dstemming=false -Dsystemd=false -Dgir=false -Dtools=false \
		-Dzstd-support=true -Dblake3-support=false -Ddisplay-detection=none -Ddocs=false \
		-Dapidocs=false -Dinstall-docs=false -Dman=false -Dbash-completion=false
}
step_ostree() {
	# ostree only takes the chattr ioctl names from e2fsprogs' header.
	mkdir -p "$T/include/ext2fs"
	cat >"$T/include/ext2fs/ext2_fs.h" <<-EOF
	#include <linux/fs.h>
	#define EXT2_IOC_GETFLAGS FS_IOC_GETFLAGS
	#define EXT2_IOC_SETFLAGS FS_IOC_SETFLAGS
	#define EXT2_IMMUTABLE_FL FS_IMMUTABLE_FL
	EOF
	# configure still checks for libe2p (e2fsprogs) but no source uses it.
	# The ostree CLI target misses the gpgme cflags.
	OT_DEP_E2P_CFLAGS=" " OT_DEP_E2P_LIBS=" " CPPFLAGS="-I$P/include -I$T/include" \
	at libostree-2026.4 "$P" --with-curl --without-soup --without-soup3 --with-gpgme-prefix="$P" \
		--with-openssl --without-composefs --without-libarchive --without-selinux \
		--without-libsystemd --without-libmount --without-avahi --disable-rofiles-fuse \
		--disable-man --disable-gtk-doc --disable-introspection
}
step_flatpak() {
	cd "$S/flatpak-1.18.4"
	# FUSE is only for revokefs, which only the system helper (disabled) uses.
	grep -q "fuse_api = 0" meson.build || {
		sed -i "s|fuse_dep = dependency('fuse', version : '>=2.9.2')|fuse_dep = dependency('fuse', version : '>=2.9.2', required : false)\n  fuse_api = 0|" meson.build
		sed -i "s|^subdir('revokefs')|if build_system_helper\n  subdir('revokefs')\nendif|" meson.build
	}
	ms flatpak-1.18.4 -Dsystem_helper=disabled -Dseccomp=enabled -Dgir=disabled -Dgtkdoc=disabled \
		-Ddocbook_docs=disabled -Dman=disabled -Dtests=false -Dinstalled_tests=false \
		-Dmalcontent=disabled -Dselinux_module=disabled -Dwayland_security_context=disabled \
		-Dsystemd=disabled -Ddconf=disabled -Dxauth=enabled -Dlibzstd=enabled \
		-Dsystem_fusermount=/bin/false -Dbubblewrap:selinux=disabled \
		-Dbubblewrap:man=disabled -Dbubblewrap:tests=false -Ddbus-proxy:man=disabled \
		-Ddbus-proxy:tests=false
}

# runtime only: no headers, pkg-config, libtool archives, docs or the
# gpg/ostree extras flatpak never calls
step_trim() {
	rm -rf "$P/include" "$P/lib/pkgconfig" "$P/share/pkgconfig" "$P"/lib/*.la \
		"$P/share/man" "$P/share/info" "$P/share/doc" "$P/share/gtk-doc" "$P/share/aclocal" \
		"$P/share/bash-completion" "$P/share/fish" "$P/share/zsh" "$P/share/gettext" "$P/share/vala" "$P/share/common-lisp"
	for b in dumpsexp gnupg-key-manage gpg-authcode-sign.sh gpg-card gpg-error gpg-mail-tube \
		gpgme-config gpgme-json gpgme-tool gpgparsemail gpgrt-config gpgscm gpgsplit gpg-wks-client \
		hmac256 kbxutil ksba-config libassuan-config libgcrypt-config mpicalc watchgnupg \
		yamlfmt fy-tool fy-dump fy-filter fy-testsuite fy-join fy-ypath fy-compose rofiles-fuse json-glib-format json-glib-validate scmp_sys_resolver; do
		rm -f "$P/bin/$b"
	done
	rm -f "$P/libexec/gpg-wks-client" "$P/libexec/gpg-pair-tool"
}

[ $# -gt 0 ] || set -- fetch tools glibtools gnupg libs ostree flatpak trim
for s; do echo "== $s"; case $s in appstream) libs_appstream ;; *) "step_$s" ;; esac; done
