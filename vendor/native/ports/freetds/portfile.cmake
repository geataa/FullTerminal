vcpkg_download_distfile(ARCHIVE
    URLS "https://www.freetds.org/files/stable/freetds-${VERSION}.tar.gz"
    FILENAME "freetds-${VERSION}.tar.gz"
    SHA512 1d622affa6e42e5085bdf3f0e642bef37cfdeb661013af62d08bba4aecad09f4cac2860ac253a584b06432cb24183028a372721bcfcdd2a9802a6b0f6a463837)
vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}")
# DB-Library otherwise has no per-connection CA API. Keep TLS settings local to
# the LOGINREC: never change FREETDSCONF/environment while other tabs connect.
file(READ "${SOURCE_PATH}/src/dblib/dblib.c" content)
string(REPLACE "case DBSETENCRYPTION:" "case 1007: /* FullTerminal per-login CA bundle */\n        copy_ret = !!tds_dstr_copy(&login->tds_login->cafile, value_nonull);\n        break;\n    case DBSETENCRYPTION:" content "${content}")
file(WRITE "${SOURCE_PATH}/src/dblib/dblib.c" "${content}")
file(READ "${SOURCE_PATH}/src/tds/config.c" content)
string(REPLACE "if (res && !tds_dstr_isempty(&login->server_spn))" "if (res) res = tds_dstr_dup(&connection->cafile, &login->cafile);\n\tif (res && !tds_dstr_isempty(&login->server_spn))" content "${content}")
file(WRITE "${SOURCE_PATH}/src/tds/config.c" "${content}")
# Only the LGPL client library is shipped. No server, tools, ODBC DLL or tests.
file(GLOB_RECURSE cmake_files "${SOURCE_PATH}/*CMakeLists.txt")
foreach(path IN LISTS cmake_files)
    file(READ "${path}" content)
    string(REPLACE "add_subdirectory(unittests)" "" content "${content}")
    file(WRITE "${path}" "${content}")
endforeach()
file(READ "${SOURCE_PATH}/CMakeLists.txt" content)
foreach(component ctlib odbc apps server pool)
    string(REPLACE "add_subdirectory(src/${component})" "" content "${content}")
endforeach()
string(REPLACE "install(FILES \${FREETDS_CONF_FILES} DESTINATION \${CMAKE_INSTALL_FULL_SYSCONFDIR})" "" content "${content}")
file(WRITE "${SOURCE_PATH}/CMakeLists.txt" "${content}")
file(WRITE "${SOURCE_PATH}/src/dblib/CMakeLists.txt" [=[
add_library(db-lib STATIC dblib.c dbutil.c rpc.c bcp.c xact.c dbpivot.c)
add_dependencies(db-lib encodings_h)
target_link_libraries(db-lib tds replacements tdsutils ${lib_NETWORK} ${lib_BASE})
install(TARGETS db-lib ARCHIVE DESTINATION lib)
]=])
vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}" OPTIONS
    -DWITH_OPENSSL=ON -DOPENSSL_USE_STATIC_LIBS=ON -DENABLE_KRB5=OFF
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5)
vcpkg_cmake_install()
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING_LIB.txt")
