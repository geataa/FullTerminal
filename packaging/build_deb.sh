#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

VERSION="$(sed -n 's/^#define FT_VERSION "\(.*\)"/\1/p' "${ROOT_DIR}/src/core/Version.h" | tr -d '\r')"
VERSION="${VERSION:-1.2.0}"
ARCH="amd64"
PKG_NAME="fullterminal"
PKG_DIR="${SCRIPT_DIR}/deb/${PKG_NAME}_${VERSION}_${ARCH}"
DEB_FILE="${ROOT_DIR}/build/${PKG_NAME}_${VERSION}_${ARCH}.deb"

echo "=== Building Debian Package: ${PKG_NAME}_${VERSION}_${ARCH}.deb ==="

# 1. Clean previous build artifacts & old version packages (keep only latest)
rm -rf "${SCRIPT_DIR}/deb"
rm -f "${ROOT_DIR}/build/${PKG_NAME}"_*_"${ARCH}.deb"
mkdir -p "${PKG_DIR}/DEBIAN"
mkdir -p "${PKG_DIR}/usr/bin"
mkdir -p "${PKG_DIR}/usr/share/applications"
mkdir -p "${PKG_DIR}/usr/share/doc/${PKG_NAME}"

# 2. Binary
if [ ! -f "${ROOT_DIR}/build/fullterminal" ]; then
    echo "Binary not found, building..."
    make -C "${ROOT_DIR}" -j$(nproc)
fi

cp "${ROOT_DIR}/build/fullterminal" "${PKG_DIR}/usr/bin/fullterminal"
chmod 755 "${PKG_DIR}/usr/bin/fullterminal"

# 3. Desktop Entry
cat << 'DESKTOP_EOF' > "${PKG_DIR}/usr/share/applications/fullterminal.desktop"
[Desktop Entry]
Version=1.0
Type=Application
Name=FullTerminal
GenericName=Terminal Emulator
Comment=Advanced GPU-Accelerated C++20 Terminal with BSP Panes, MCP & Multi-Agent Swarm
Exec=fullterminal %F
Icon=fullterminal
Terminal=false
Categories=System;TerminalEmulator;Development;
Keywords=terminal;shell;ssh;console;quake;bsp;mcp;
StartupNotify=true
StartupWMClass=FullTerminal
DESKTOP_EOF
chmod 644 "${PKG_DIR}/usr/share/applications/fullterminal.desktop"

# 4. High-Resolution Application Icons
python3 -c "
from PIL import Image
import os

src_ico = '${ROOT_DIR}/resources/app.ico'
sizes = [16, 32, 48, 64, 128, 256]

if os.path.exists(src_ico):
    im = Image.open(src_ico)
    for s in sizes:
        d = '${PKG_DIR}/usr/share/icons/hicolor/' + str(s) + 'x' + str(s) + '/apps'
        os.makedirs(d, exist_ok=True)
        resized = im.resize((s, s), Image.Resampling.LANCZOS if hasattr(Image, 'Resampling') else Image.ANTIALIAS)
        resized.save(os.path.join(d, 'fullterminal.png'))
"

# 5. Copyright / License
cat << 'COPYRIGHT_EOF' > "${PKG_DIR}/usr/share/doc/${PKG_NAME}/copyright"
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: FullTerminal
Source: https://github.com/geataa/FullTerminal

Files: *
Copyright: 2026 FullTerminal Team
License: MIT
COPYRIGHT_EOF
chmod 644 "${PKG_DIR}/usr/share/doc/${PKG_NAME}/copyright"

# 6. Control File
INSTALLED_SIZE=$(du -sk "${PKG_DIR}/usr" | cut -f1)
cat << CONTROL_EOF > "${PKG_DIR}/DEBIAN/control"
Package: ${PKG_NAME}
Version: ${VERSION}
Section: utils
Priority: optional
Architecture: ${ARCH}
Installed-Size: ${INSTALLED_SIZE}
Depends: libc6 (>= 2.34), libstdc++6 (>= 12), libx11-6, libgl1, libfreetype6, libfontconfig1, openssh-client
Recommends: zenity | kdialog, libxi6
Maintainer: FullTerminal Team <support@fullterminal.org>
Description: High-performance GPU-accelerated C++20 terminal emulator
 FullTerminal is a modern, blazing fast, native terminal emulator written in
 pure C++20 with hardware-accelerated OpenGL rendering, dynamic FreeType2/Fontconfig
 typography, Herdr binary space partitioning (BSP) split panes, Quake visor
 drop-down console, SSH fleet manager, dual-pane SFTP file explorer, Kubernetes
 manager, background PTY session persistence daemon, and Model Context Protocol
 (MCP) multi-agent swarm orchestration.
CONTROL_EOF
chmod 644 "${PKG_DIR}/DEBIAN/control"

# 7. Build .deb package
mkdir -p "${ROOT_DIR}/build"
dpkg-deb --build --root-owner-group "${PKG_DIR}" "${DEB_FILE}"

echo "Debian package successfully built at: ${DEB_FILE}"
echo "Package info:"
dpkg-deb -I "${DEB_FILE}"
echo "Package contents:"
dpkg-deb -c "${DEB_FILE}"
