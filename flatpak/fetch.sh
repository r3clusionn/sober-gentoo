#!/bin/sh
# Download every source tarball for the ~/.local/opt/flatpak build into $1
# and check it against the upstream sha256 (TLS only where upstream has none).
set -eu
cd "$1"
G=https://gnupg.org/ftp/gcrypt
GH=https://github.com
while read -r sum url; do
	f=${url##*/}
	[ -f "$f" ] || curl -sSfLo "$f" "$url"
	if [ "$sum" = - ]; then
		echo "tls-only $f"
	else
		echo "$sum  $f" | sha256sum -c --quiet
	fi
done <<EOF
7a85413f2bc354f4f8aa832b718af122e48965e9e0eb9012ee659c13c6385c93 $G/libgpg-error/libgpg-error-1.61.tar.bz2
d2931cdad266e633510f9970e1a2f346055e351bb19f9b78912475b8074c36f6 $G/libassuan/libassuan-3.0.2.tar.bz2
8bd24b4f23a3065d6e5b26e98aba9ce783ea4fd781069c1b35d149694e90ca3e $G/npth/npth-1.8.tar.bz2
d77f68f48879510e79a2f65977ccc68981781ea0923e5bdffac2a193ea3d660e $G/libgcrypt/libgcrypt-1.12.4.tar.bz2
c2f84393011827219ae117131dba8e7684c2bed0961eed11b0642c2acba440b5 $G/libksba/libksba-1.8.1.tar.bz2
bf149d01a2b9fcc0e4589b8ae8697d3d5c557ea48ed95a3fa55dd3b1187e6039 $G/gnupg/gnupg-2.5.24.tar.bz2
7160e80e84dafd00d956c84891c533bb7ab16a6a54fbe1574b2f3acf0496977b $G/gpgme/gpgme-2.2.0.tar.bz2
501f66c667225d53791b97e1d7cf85ab764c297d04881f60f38f451c4b0ee1be $GH/seccomp/libseccomp/releases/download/v2.6.1/libseccomp-2.6.1.tar.gz
56cad765459d5ee25a3b761f90c47d99379d28297fbfb72d908880d6b875fb96 $GH/libarchive/libarchive/releases/download/v3.8.10/libarchive-3.8.10.tar.xz
55c5c141a564245b8f8fbe7698663c87a45a7333c2a2c56f06f811ab73b212dd https://download.gnome.org/sources/json-glib/1.10/json-glib-1.10.8.tar.xz
448294be33bfae62f00fa66e506f1cae80237ce71b7ab6530aefa75005eeb08a $GH/hughsie/libxmlb/releases/download/0.3.29/libxmlb-0.3.29.tar.xz
a59cc3331e2eb903ec36933ad52a45888041cac31e44f553a00511131242c483 $GH/pantoniou/libfyaml/releases/download/v0.9.6/libfyaml-0.9.6.tar.gz
- https://www.freedesktop.org/software/appstream/releases/AppStream-1.2.1.tar.xz
b26c9016eb03bb4ee52cc00c642d56e00fc79ae7faac6bf4aa317d7451339ef7 $GH/ostreedev/ostree/releases/download/v2026.4/libostree-2026.4.tar.xz
b899a7a00c48d2c626cb8ec33fe556720b376c25805d7222430c0fdca4c6ac8d $GH/flatpak/flatpak/releases/download/1.18.4/flatpak-1.18.4.tar.xz
cf3f215a640c8a4257f14317586b8f1fdd25a10a93cb4bdda147c0f9ad88e74f https://download.gnome.org/sources/glib/2.88/glib-2.88.2.tar.xz
- https://ftp.gnu.org/gnu/m4/m4-1.4.21.tar.xz
- https://ftp.gnu.org/gnu/bison/bison-3.8.2.tar.xz
- https://ftp.gnu.org/gnu/gperf/gperf-3.3.tar.gz
ece8c00a69cf01b45d0b1dedabb469c90d8caf996d4fda40f147627a122849a4 https://files.pythonhosted.org/packages/38/bb/d215ee7c73b61497b28a5503f9f53523f294fcc936762b7caf90e0c1c2b5/pyparsing-3.3.3-py3-none-any.whl
EOF
