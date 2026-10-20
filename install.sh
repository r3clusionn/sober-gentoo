#!/bin/sh
# Build sober-pointer, install it and the launchers to ~/.local/bin and the Flatpak build scripts to
# ~/.local/opt/flatpak/tools. Building is a separate step (see README).
set -eu
cd "$(dirname "$0")"
mkdir -p "$HOME/.local/bin" "$HOME/.local/opt/flatpak/tools"
cc -O2 -o sober-pointer sober-pointer.c $(pkg-config --cflags --libs x11 xi xfixes)
install -m 755 bin/flatpak bin/sober bin/voidstrap sober-pointer "$HOME/.local/bin/"
install -m 755 flatpak/build.sh flatpak/fetch.sh "$HOME/.local/opt/flatpak/tools/"
echo "installed; next: ~/.local/opt/flatpak/tools/build.sh"
