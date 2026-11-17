#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

VERSION="$(sed -n 's/^#define FT_VERSION "\(.*\)"/\1/p' "${ROOT_DIR}/src/core/Version.h" | tr -d '\r')"
VERSION="${VERSION:-1.2.0}"
ARCH="x86_64"
APPDIR="/tmp/FullTerminal.AppDir"
OUT_DIR="${ROOT_DIR}/dist"
APPIMAGE="${OUT_DIR}/FullTerminal-${VERSION}-${ARCH}.AppImage"
PORTABLE_TAR="${OUT_DIR}/FullTerminal-${VERSION}-linux-x64-portable.tar.gz"
PORTABLE_DIR="${OUT_DIR}/FullTerminal-portable"

echo "=========================================================="
echo "  FullTerminal v${VERSION} Portable Linux Build"
echo "=========================================================="

mkdir -p "${OUT_DIR}" "${ROOT_DIR}/build"

# Eski surum AppImage / portable arsiv kalintilarini temizle (sadece son surum kalsin)
rm -f "${OUT_DIR}"/FullTerminal-*.AppImage "${OUT_DIR}"/FullTerminal-*-portable.tar.gz
rm -f "${ROOT_DIR}/build"/FullTerminal-*.AppImage "${ROOT_DIR}/build"/FullTerminal-*-portable.tar.gz

# 1. Compile binary if needed
if [ ! -f "${ROOT_DIR}/build/fullterminal" ]; then
    echo "Compiling fullterminal..."
    make -C "${ROOT_DIR}" -j$(nproc)
fi

# 2. Build AppDir
rm -rf "${APPDIR}"
mkdir -p "${APPDIR}/usr/bin"
mkdir -p "${APPDIR}/usr/share/applications"
mkdir -p "${APPDIR}/usr/share/icons/hicolor/256x256/apps"

cp "${ROOT_DIR}/build/fullterminal" "${APPDIR}/usr/bin/fullterminal"
chmod 755 "${APPDIR}/usr/bin/fullterminal"

# Icons
if [ -f "${ROOT_DIR}/resources/app.png" ]; then
    cp "${ROOT_DIR}/resources/app.png" "${APPDIR}/fullterminal.png"
    cp "${ROOT_DIR}/resources/app.png" "${APPDIR}/usr/share/icons/hicolor/256x256/apps/fullterminal.png"
fi

# Desktop Entry
cat << 'DESKTOP_EOF' > "${APPDIR}/fullterminal.desktop"
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
cp "${APPDIR}/fullterminal.desktop" "${APPDIR}/usr/share/applications/fullterminal.desktop"
chmod 644 "${APPDIR}/fullterminal.desktop" "${APPDIR}/usr/share/applications/fullterminal.desktop"

# AppRun launcher script with portable auto-detection
cat << 'APPRUN_EOF' > "${APPDIR}/AppRun"
#!/bin/sh
set -e
HERE="$(dirname "$(readlink -f "${0}")")"
export PATH="${HERE}/usr/bin:${PATH}"

# AppImage portable mode:
# If portable_data directory exists next to the .AppImage, use it automatically!
if [ -n "${APPIMAGE}" ]; then
    APPIMAGE_DIR="$(dirname "${APPIMAGE}")"
    if [ -d "${APPIMAGE_DIR}/portable_data" ] || [ "${FULLTERMINAL_PORTABLE}" = "1" ]; then
        mkdir -p "${APPIMAGE_DIR}/portable_data"
        export FULLTERMINAL_DATA_DIR="${APPIMAGE_DIR}/portable_data"
    fi
fi

exec "${HERE}/usr/bin/fullterminal" "$@"
APPRUN_EOF
chmod 755 "${APPDIR}/AppRun"

# 3. Download appimagetool if not cached
APPIMAGETOOL="/tmp/appimagetool"
if [ ! -f "${APPIMAGETOOL}" ]; then
    echo "Fetching appimagetool..."
    curl -sSL -o "${APPIMAGETOOL}" https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
    chmod +x "${APPIMAGETOOL}"
fi

# 4. Generate AppImage
echo "[1/3] Generating Standalone AppImage..."
ARCH=x86_64 "${APPIMAGETOOL}" --appimage-extract-and-run "${APPDIR}" "${APPIMAGE}"
chmod +x "${APPIMAGE}"
ln -sf "FullTerminal-${VERSION}-${ARCH}.AppImage" "${OUT_DIR}/FullTerminal.AppImage"
cp -f "${APPIMAGE}" "${ROOT_DIR}/build/FullTerminal-${VERSION}-${ARCH}.AppImage"
chmod +x "${ROOT_DIR}/build/FullTerminal-${VERSION}-${ARCH}.AppImage"
echo "  -> AppImage hazır: ${APPIMAGE} ($(du -h "${APPIMAGE}" | cut -f1))"

# 5. Build Extracted Portable Directory
echo "[2/3] Generating Extracted Portable Directory..."
rm -rf "${PORTABLE_DIR}"
mkdir -p "${PORTABLE_DIR}/portable_data"
cp "${ROOT_DIR}/build/fullterminal" "${PORTABLE_DIR}/fullterminal"
chmod 755 "${PORTABLE_DIR}/fullterminal"

if [ -f "${ROOT_DIR}/resources/app.png" ]; then
    mkdir -p "${PORTABLE_DIR}/resources"
    cp "${ROOT_DIR}/resources/app.png" "${PORTABLE_DIR}/resources/app.png"
fi

cat << 'LAUNCHER_EOF' > "${PORTABLE_DIR}/FullTerminal"
#!/bin/sh
SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"
export FULLTERMINAL_DATA_DIR="${SCRIPT_DIR}/portable_data"
exec "${SCRIPT_DIR}/fullterminal" "$@"
LAUNCHER_EOF
chmod 755 "${PORTABLE_DIR}/FullTerminal"

cat << 'README_EOF' > "${PORTABLE_DIR}/README.txt"
FullTerminal Portable for Linux (x86_64)
========================================

Bu klasor tamamen bagimsiz ve tasinabilir (portable) olarak calisir.

Kullanim:
  - Terminali baslatmak icin ./FullTerminal dosyasini calistirin veya cift tiklayin.
  - Tum ayarlar, SSH sunucu listeleri, anahtarlar, oturumlar ve snippet'lar
    bu klasordeki "portable_data" altinda saklanir.
  - Sisteminize (root / ~/.config) hicbir dosya yazilmaz.
  - USB bellege atip istediginiz Linux bilgisayarda (Ubuntu, Debian, Fedora, Arch, vb.)
    kurulum yapmadan dogrudan kullanabilirsiniz.
README_EOF

if [ -d "${ROOT_DIR}/vendor/native/licenses" ]; then
    mkdir -p "${PORTABLE_DIR}/licenses"
    cp -r "${ROOT_DIR}/vendor/native/licenses/"* "${PORTABLE_DIR}/licenses/"
fi

# 6. Build Portable Directory Archive (.tar.gz)
echo "[3/3] Generating Portable Tarball..."
tar -czf "${PORTABLE_TAR}" -C "${OUT_DIR}" "FullTerminal-portable"
echo "  -> Portable tarball hazır: ${PORTABLE_TAR} ($(du -h "${PORTABLE_TAR}" | cut -f1))"

echo "=========================================================="
echo "  PORTABLE BUILDS HAZIR! ('dist/' klasorunde):"
echo "  1. dist/FullTerminal-portable/FullTerminal (Dogrudan calistirilabilir klasor)"
echo "  2. dist/FullTerminal-${VERSION}-${ARCH}.AppImage (Tek dosya calistirilabilir dosya)"
echo "  3. dist/FullTerminal-${VERSION}-linux-x64-portable.tar.gz (Zip/tar arsivi)"
echo "=========================================================="
