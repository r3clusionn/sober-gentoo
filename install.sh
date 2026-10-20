#!/bin/sh
# Install the launchers to ~/.local/bin and the Flatpak build scripts to
# ~/.local/opt/flatpak/tools. Building is a separate step (see README).
set -eu
cd "$(dirname "$0")"
mkdir -p "$HOME/.local/bin" "$HOME/.local/opt/flatpak/tools"
install -m 755 bin/flatpak bin/sober bin/voidstrap "$HOME/.local/bin/"
install -m 755 flatpak/build.sh flatpak/fetch.sh "$HOME/.local/opt/flatpak/tools/"
echo "installed; next: ~/.local/opt/flatpak/tools/build.sh"
