#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

VERSION="$(sed -n 's/^#define FT_VERSION "\(.*\)"/\1/p' "${ROOT_DIR}/src/core/Version.h" | tr -d '\r')"
VERSION="${VERSION:-1.2.0}"
OUT_DIR="${ROOT_DIR}/dist"

mkdir -p "${OUT_DIR}"

# 1. Build AppImage & Portable Tarball into dist/
chmod +x "${SCRIPT_DIR}/build_portable.sh"
"${SCRIPT_DIR}/build_portable.sh"

# 2. Build Debian (.deb) package into build/ and copy into dist/
chmod +x "${SCRIPT_DIR}/build_deb.sh"
"${SCRIPT_DIR}/build_deb.sh"

DEB_NAME="fullterminal_${VERSION}_amd64.deb"
APPIMAGE_NAME="FullTerminal-${VERSION}-x86_64.AppImage"
TARBALL_NAME="FullTerminal-${VERSION}-linux-x64-portable.tar.gz"

cp -f "${ROOT_DIR}/build/${DEB_NAME}" "${OUT_DIR}/${DEB_NAME}"
cp -f "${ROOT_DIR}/build/fullterminal" "${OUT_DIR}/fullterminal"
chmod 755 "${OUT_DIR}/fullterminal" "${OUT_DIR}/${APPIMAGE_NAME}"

# 3. Generate SHA256SUMS.txt matching Windows release conventions
(
    cd "${OUT_DIR}"
    sha256sum \
        "${DEB_NAME}" \
        "${APPIMAGE_NAME}" \
        "${TARBALL_NAME}" \
        "fullterminal" > SHA256SUMS.txt
)

echo "=========================================================="
echo "  FullTerminal v${VERSION} Debian/Linux Release Artifacts"
echo "=========================================================="
ls -lh \
    "${OUT_DIR}/${DEB_NAME}" \
    "${OUT_DIR}/${APPIMAGE_NAME}" \
    "${OUT_DIR}/${TARBALL_NAME}" \
    "${OUT_DIR}/fullterminal" \
    "${OUT_DIR}/SHA256SUMS.txt"
cat "${OUT_DIR}/SHA256SUMS.txt"
