#!/bin/sh
# SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
# Registers KOutNet with the desktop for a build that runs out of build/.
# A real "cmake --install" does this properly; this is the development
# shortcut, writing into ~/.local/share instead of a system prefix.
set -e

repo=$(cd "$(dirname "$0")/.." && pwd)
apps="$HOME/.local/share/applications"
icons="$HOME/.local/share/icons/hicolor/512x512/apps"

mkdir -p "$apps" "$icons"
cp "$repo/NEWCORE-2-CPP/assets/koutnet_logo.png" "$icons/io.github.bitzuka.KOutNet.png"
sed "s|^Exec=.*|Exec=$repo/NEWCORE-2-CPP/build/KOutNet|" \
    "$repo/packaging/io.github.bitzuka.KOutNet.desktop" > "$apps/io.github.bitzuka.KOutNet.desktop"

update-desktop-database "$apps" 2>/dev/null || true
gtk-update-icon-cache -f -t "$HOME/.local/share/icons/hicolor" 2>/dev/null || true

echo "installed $apps/io.github.bitzuka.KOutNet.desktop"
echo "installed $icons/io.github.bitzuka.KOutNet.png"
