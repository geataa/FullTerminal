#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

VERSION="$(sed -n 's/^#define FT_VERSION "\(.*\)"/\1/p' "${ROOT_DIR}/src/core/Version.h" | tr -d '\r')"
VERSION="${VERSION:-1.2.0}"
TAG="v${VERSION}-debian"
TITLE="FullTerminal v${VERSION} (Debian / Ubuntu Linux x64) — Database & Terminal Workspace"
NOTES="${ROOT_DIR}/docs/releases/${TAG}.md"
OUT_DIR="${ROOT_DIR}/dist"

GH_BIN="$(command -v gh || true)"
if [ -z "${GH_BIN}" ] && [ -x "${HOME}/.local/bin/gh" ]; then
    GH_BIN="${HOME}/.local/bin/gh"
elif [ -z "${GH_BIN}" ] && [ -x "/tmp/gh_cli/bin/gh" ]; then
    GH_BIN="/tmp/gh_cli/bin/gh"
fi

if [ -z "${GH_BIN}" ]; then
    echo "Error: 'gh' CLI not found." >&2
    exit 1
fi

if ! "${GH_BIN}" auth status >/dev/null 2>&1; then
    echo "GitHub oturumu aciliyor..."
    "${GH_BIN}" auth login --hostname github.com --git-protocol https --web -s repo,workflow
fi

"${GH_BIN}" auth setup-git

if [ ! -f "${OUT_DIR}/SHA256SUMS.txt" ] || [ ! -f "${OUT_DIR}/fullterminal_${VERSION}_amd64.deb" ]; then
    "${SCRIPT_DIR}/package_release_linux.sh"
fi

echo "Removing any previous '${TAG}' release/tag on GitHub before fresh publish..."
if "${GH_BIN}" release view "${TAG}" >/dev/null 2>&1; then
    "${GH_BIN}" release delete "${TAG}" --yes --cleanup-tag || true
fi

git -C "${ROOT_DIR}" tag -fa "${TAG}" -m "FullTerminal v${VERSION} (Debian / Ubuntu Linux x64)"

echo "Pushing 'debian' branch and fresh '${TAG}' tag to origin..."
git -C "${ROOT_DIR}" push -u origin debian
git -C "${ROOT_DIR}" push -f origin "${TAG}"

ASSETS=(
    "${OUT_DIR}/fullterminal_${VERSION}_amd64.deb"
    "${OUT_DIR}/FullTerminal-${VERSION}-x86_64.AppImage"
    "${OUT_DIR}/FullTerminal-${VERSION}-linux-x64-portable.tar.gz"
    "${OUT_DIR}/fullterminal"
    "${OUT_DIR}/SHA256SUMS.txt"
)

echo "Publishing fresh GitHub Release ${TAG}..."
"${GH_BIN}" release create "${TAG}" "${ASSETS[@]}" \
    --target debian \
    --title "${TITLE}" \
    --notes-file "${NOTES}"

echo "Done! Release URL: https://github.com/geataa/FullTerminal/releases/tag/${TAG}"
