# Statically linked database clients

Only client code is included in FullTerminal.exe. No server, service, system
driver registration or extracted vendor DLL is required at runtime.

| Library | Version | Purpose | License |
| --- | --- | --- | --- |
| PostgreSQL libpq | 18.6 | PostgreSQL protocol, SCRAM, TLS | PostgreSQL |
| MariaDB Connector/C | 3.4.11 | MySQL/MariaDB protocol and authentication | LGPL-2.1-or-later |
| FreeTDS DB-Library | 1.5.19 | SQL Server TDS/SSPI/TLS | LGPL-2.0-or-later |
| OpenSSL | 3.6.5 | libpq/FreeTDS TLS and cryptography | Apache-2.0 |
| zlib | 1.3.2 | Connector/C compression | Zlib |

Original source archives and SHA-256 hashes are in this directory. Copyright
and license texts are in `licenses`. MariaDB uses Windows Schannel for TLS.
Only FreeTDS client libraries are built; its GPL tools and server code are excluded.

Build from the repository root:

```powershell
./tools/Build-NativeClients.ps1
./build.bat
```

The pinned vcpkg revision is recorded in `vcpkg.json`. Overlay ports contain all
build modifications. `x64-ft-static` builds release libraries with the static
MSVC runtime. Dependencies are installed into `build/native-installed`, never
into Windows. The app's CMake build links their `.lib` files directly. Rebuilding
the application from source relinks it with rebuilt or modified LGPL libraries;
release source packages include these archives, overlays and application source.

FreeTDS has a small per-login CA-bundle extension (dbsetlname field 1007), with
configuration copying patched accordingly. This avoids process-wide environment
changes when different tabs use different certificate authorities. PostgreSQL
and FreeTDS use a temporary bundle of public Windows trusted-root certificates
when no PEM file is specified. The bundle is deleted after the handshake.

MariaDB external auth plugins are disabled: Ed25519, PARSEC, GSSAPI and old-password
authentication are not shipped. Standard native/SHA-256/caching-SHA-256 auth is
built in. `LOCAL INFILE` is disabled. MySQL query cancellation intentionally
closes the connection; the UI requires explicit reconnect and never replays writes.

Oracle is not a built-in adapter. Generic ODBC remains optional and uses an
explicitly installed driver. Windows system DLLs such as Schannel/Winsock and
ODBC32 are OS components; no third-party DLL is loaded for the three native engines.
