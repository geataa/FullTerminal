#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
VENDOR_DIR="${ROOT_DIR}/vendor/native"
WORK_DIR="${ROOT_DIR}/build/native-work-linux"
TOOLS_DIR="${ROOT_DIR}/build/native-tools-linux"
PREFIX="${ROOT_DIR}/build/native-installed/x64-linux-static"
JOBS="$(nproc 2>/dev/null || echo 4)"

if [ -f "${PREFIX}/lib/libpq.a" ] && \
   [ -f "${PREFIX}/lib/libmariadbclient.a" ] && \
   [ -f "${PREFIX}/lib/libsybdb.a" ] && \
   [ -f "${PREFIX}/lib/libssl.a" ] && \
   [ -f "${PREFIX}/lib/libcrypto.a" ] && \
   [ -f "${PREFIX}/lib/libz.a" ]; then
    echo "Static Linux database client libraries already built in ${PREFIX}"
    exit 0
fi

mkdir -p "${WORK_DIR}" "${TOOLS_DIR}" "${PREFIX}/lib" "${PREFIX}/include"

# Ensure cmake, bison, and flex are available (fetch user-local .debs if not installed system-wide)
if [ ! -x "${TOOLS_DIR}/usr/bin/cmake" ] || [ ! -x "${TOOLS_DIR}/usr/bin/bison" ] || [ ! -x "${TOOLS_DIR}/usr/bin/flex" ] || [ ! -f "${TOOLS_DIR}/usr/lib/x86_64-linux-gnu/libsigsegv.so.2" ]; then
    echo "[0/5] Fetching user-local build tools (cmake, bison, flex)..."
    mkdir -p "${TOOLS_DIR}/debs"
    (
        cd "${TOOLS_DIR}/debs"
        apt-get download cmake cmake-data libjsoncpp25 librhash0 bison flex m4 libsigsegv2 >/dev/null
        for deb in ./*.deb; do
            dpkg-deb -x "${deb}" "${TOOLS_DIR}"
        done
    )
fi
export PATH="${TOOLS_DIR}/usr/bin:${PATH}"
export LD_LIBRARY_PATH="${TOOLS_DIR}/usr/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH:-}"
export BISON_PKGDATADIR="${TOOLS_DIR}/usr/share/bison"
export M4="${TOOLS_DIR}/usr/bin/m4"
if [ -d "${TOOLS_DIR}/usr/share/cmake-3.22" ]; then
    export CMAKE_ROOT="${TOOLS_DIR}/usr/share/cmake-3.22"
fi
CMAKE_BIN="$(command -v cmake)"

# 1. zlib 1.4 (static)
if [ ! -f "${PREFIX}/lib/libz.a" ]; then
    echo "[1/5] Building static zlib 1.4..."
    rm -rf "${WORK_DIR}/zlib"
    mkdir -p "${WORK_DIR}/zlib"
    tar -xzf "${VENDOR_DIR}/madler-zlib-v1.4.tar.gz" -C "${WORK_DIR}/zlib" --strip-components=1
    (
        cd "${WORK_DIR}/zlib"
        CFLAGS="-O2 -fPIC" ./configure --prefix="${PREFIX}" --static >/dev/null
        make -j"${JOBS}" >/dev/null
        make install >/dev/null
    )
fi

# 2. OpenSSL 3.6.5 (static libs only, no apps/tests/docs)
if [ ! -f "${PREFIX}/lib/libssl.a" ] || [ ! -f "${PREFIX}/lib/libcrypto.a" ]; then
    echo "[2/5] Building static OpenSSL 3.6.5..."
    rm -rf "${WORK_DIR}/openssl"
    mkdir -p "${WORK_DIR}/openssl"
    tar -xzf "${VENDOR_DIR}/openssl-openssl-openssl-3.6.5.tar.gz" -C "${WORK_DIR}/openssl" --strip-components=1
    (
        cd "${WORK_DIR}/openssl"
        CFLAGS="-O2 -fPIC" ./Configure linux-x86_64 no-shared no-tests no-docs no-module \
            --prefix="${PREFIX}" --libdir=lib --openssldir="/etc/ssl" \
            -I"${PREFIX}/include" -L"${PREFIX}/lib" >/dev/null
        make -j"${JOBS}" build_libs >/dev/null
        make install_dev >/dev/null
    )
fi

# 3. PostgreSQL 18.7 libpq (static)
if [ ! -f "${PREFIX}/lib/libpq.a" ]; then
    echo "[3/5] Building static PostgreSQL libpq 18.7..."
    rm -rf "${WORK_DIR}/postgresql"
    mkdir -p "${WORK_DIR}/postgresql"
    tar -xjf "${VENDOR_DIR}/postgresql-18.7.tar.bz2" -C "${WORK_DIR}/postgresql" --strip-components=1
    (
        cd "${WORK_DIR}/postgresql"
        CFLAGS="-O2 -fPIC" CPPFLAGS="-I${PREFIX}/include" LDFLAGS="-L${PREFIX}/lib" \
            LIBS="-lssl -lcrypto -lz -ldl -lpthread" \
            ./configure --prefix="${PREFIX}" \
            --with-openssl --with-zlib \
            --without-readline --without-icu --without-lz4 --without-zstd --without-gssapi --without-ldap \
            --disable-rpath >/dev/null
        make -j"${JOBS}" submake-generated-headers >/dev/null
        make -j"${JOBS}" -C src/common libpgcommon_shlib.a >/dev/null
        make -j"${JOBS}" -C src/port libpgport_shlib.a >/dev/null
        make -j"${JOBS}" -C src/interfaces/libpq libpq.a >/dev/null
        cp src/interfaces/libpq/libpq.a "${PREFIX}/lib/libpq.a"
        cp src/common/libpgcommon_shlib.a "${PREFIX}/lib/libpgcommon.a"
        cp src/port/libpgport_shlib.a "${PREFIX}/lib/libpgport.a"
        cp src/interfaces/libpq/libpq-fe.h src/interfaces/libpq/pqexpbuffer.h \
           src/include/postgres_ext.h "${PREFIX}/include/"
    )
fi

# 4. FreeTDS 1.5.21 db-lib (static, with per-login CA bundle option 1007)
if [ ! -f "${PREFIX}/lib/libsybdb.a" ]; then
    echo "[4/5] Building static FreeTDS 1.5.21..."
    rm -rf "${WORK_DIR}/freetds"
    mkdir -p "${WORK_DIR}/freetds"
    tar -xzf "${VENDOR_DIR}/freetds-1.5.21.tar.gz" -C "${WORK_DIR}/freetds" --strip-components=1
    python3 - "${WORK_DIR}/freetds" << 'PYEOF'
import pathlib, sys
src = pathlib.Path(sys.argv[1])
dblib = src / "src/dblib/dblib.c"
txt = dblib.read_text(encoding="utf-8")
assert "case DBSETENCRYPTION:" in txt
txt = txt.replace(
    "case DBSETENCRYPTION:",
    "case 1007: /* FullTerminal per-login CA bundle */\n        copy_ret = !!tds_dstr_copy(&login->tds_login->cafile, value_nonull);\n        break;\n    case DBSETENCRYPTION:",
    1,
)
dblib.write_text(txt, encoding="utf-8")

cfg = src / "src/tds/config.c"
ctxt = cfg.read_text(encoding="utf-8")
assert "if (res && !tds_dstr_isempty(&login->server_spn))" in ctxt
ctxt = ctxt.replace(
    "if (res && !tds_dstr_isempty(&login->server_spn))",
    "if (res) res = tds_dstr_dup(&connection->cafile, &login->cafile);\n\tif (res && !tds_dstr_isempty(&login->server_spn))",
    1,
)
cfg.write_text(ctxt, encoding="utf-8")
PYEOF
    (
        cd "${WORK_DIR}/freetds"
        CFLAGS="-O2 -fPIC" CPPFLAGS="-I${PREFIX}/include" LDFLAGS="-L${PREFIX}/lib" \
            LIBS="-lssl -lcrypto -lz -ldl -lpthread" \
            ./configure --prefix="${PREFIX}" \
            --enable-static --disable-shared \
            --with-openssl="${PREFIX}" \
            --without-krb5 --without-odbc --without-libiconv \
            --disable-apps --disable-server --disable-pool >/dev/null
        make -j"${JOBS}" -C include install >/dev/null
        make -j"${JOBS}" -C src/utils >/dev/null
        make -j"${JOBS}" -C src/replacements >/dev/null
        make -j"${JOBS}" -C src/tds >/dev/null
        make -j"${JOBS}" -C src/dblib install >/dev/null
        # Remove any .la files so linker picks static archives cleanly
        rm -f "${PREFIX}/lib/"*.la
    )
fi

# 5. MariaDB Connector/C 3.4.11 (static mariadbclient)
if [ ! -f "${PREFIX}/lib/libmariadbclient.a" ]; then
    echo "[5/5] Building static MariaDB Connector/C 3.4.11..."
    rm -rf "${WORK_DIR}/mariadb" "${WORK_DIR}/mariadb-build"
    mkdir -p "${WORK_DIR}/mariadb" "${WORK_DIR}/mariadb-build"
    tar -xzf "${VENDOR_DIR}/mariadb-corporation-mariadb-connector-c-v3.4.11.tar.gz" -C "${WORK_DIR}/mariadb" --strip-components=1
    rm -rf "${WORK_DIR}/mariadb/external/zlib"
    (
        cd "${WORK_DIR}/mariadb-build"
        "${CMAKE_BIN}" "${WORK_DIR}/mariadb" \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
            -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
            -DCMAKE_COMPILE_WARNING_AS_ERROR=OFF \
            -DINSTALL_INCLUDEDIR=include/mysql \
            -DINSTALL_LIBDIR=lib \
            -DINSTALL_PLUGINDIR=plugins/libmariadb \
            -DWITH_CURL=OFF \
            -DWITH_ICONV=OFF \
            -DWITH_EXTERNAL_ZLIB=ON \
            -DZLIB_INCLUDE_DIR="${PREFIX}/include" \
            -DZLIB_LIBRARY="${PREFIX}/lib/libz.a" \
            -DWITH_SSL=OPENSSL \
            -DOPENSSL_ROOT_DIR="${PREFIX}" \
            -DOPENSSL_INCLUDE_DIR="${PREFIX}/include" \
            -DOPENSSL_SSL_LIBRARY="${PREFIX}/lib/libssl.a" \
            -DOPENSSL_CRYPTO_LIBRARY="${PREFIX}/lib/libcrypto.a" \
            -DWITH_UNIT_TESTS=OFF \
            -DCLIENT_PLUGIN_AUTH_GSSAPI_CLIENT=OFF \
            -DCLIENT_PLUGIN_CACHING_SHA2_PASSWORD=STATIC \
            -DCLIENT_PLUGIN_CLIENT_ED25519=OFF \
            -DCLIENT_PLUGIN_DIALOG=STATIC \
            -DCLIENT_PLUGIN_PARSEC=OFF \
            -DCLIENT_PLUGIN_MYSQL_CLEAR_PASSWORD=STATIC \
            -DCLIENT_PLUGIN_MYSQL_OLD_PASSWORD=OFF \
            -DCLIENT_PLUGIN_SHA256_PASSWORD=STATIC \
            -DCLIENT_PLUGIN_ZSTD=OFF \
            -DAUTH_GSSAPI_PLUGIN_TYPE=OFF \
            -DREMOTEIO_PLUGIN_TYPE=OFF >/dev/null
        make -j"${JOBS}" mariadbclient >/dev/null
        cp libmariadb/libmariadbclient.a "${PREFIX}/lib/"
        mkdir -p "${PREFIX}/include/mysql"
        cp -r "${WORK_DIR}/mariadb/include/"* "${PREFIX}/include/mysql/"
        cp include/ma_config.h include/mariadb_version.h "${PREFIX}/include/mysql/"
    )
fi

echo "Static Linux database client libraries ready in ${PREFIX}"
