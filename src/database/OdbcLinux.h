#pragma once
//
// FullTerminal — Linux ODBC koprusu (unixODBC, calisma zamaninda dlopen)
//
// Windows'ta OdbcBackend.cpp dogrudan odbc32'ye baglanir ve SQLWCHAR == wchar_t (UTF-16)
// varsayar. Linux'ta:
//   * unixODBC (libodbc.so.2) calisma zamaninda yuklenir: kurulu degilse uygulama yine calisir,
//     sadece ODBC baglantilari "surucu yok" der (SQLite yerlesik oldugu icin etkilenmez).
//   * unixODBC'nin genis karakter API'si UTF-16 (unsigned short) kullanir, wchar_t ise 32 bit.
//     Bu baslik SQLWCHAR'i wchar_t olarak tanimlar ve her *W cagrisinda UTF-32 <-> UTF-16
//     donusumu yapar; boylece OdbcBackend.cpp Windows ile ayni kaynaktan derlenir.
//   * SQLBindParameter ertelenmis baglama oldugu icin UTF-16 kopyalar statement serbest
//     birakilana kadar saklanir.
//
#if !defined(_WIN32)
#include "core/Platform.h"

#include <dlfcn.h>
#include <cstdint>
#include <cwchar>
#include <map>
#include <memory>
#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

using SQLRETURN    = short;
using SQLSMALLINT  = short;
using SQLUSMALLINT = unsigned short;
using SQLINTEGER   = int;
using SQLUINTEGER  = unsigned int;
using SQLLEN       = long;
using SQLULEN      = unsigned long;
using SQLPOINTER   = void*;
using SQLHANDLE    = void*;
using SQLHENV      = void*;
using SQLHDBC      = void*;
using SQLHSTMT     = void*;
using SQLHWND      = void*;
using SQLWCHAR     = wchar_t;   // uygulama tarafi; surucuye UTF-16 olarak gecer
using SQLCHAR      = unsigned char;

#define SQL_SUCCESS                0
#define SQL_SUCCESS_WITH_INFO      1
#define SQL_NO_DATA                100
#define SQL_ERROR                  (-1)
#define SQL_INVALID_HANDLE         (-2)
#define SQL_SUCCEEDED(rc)          (((rc) & (~1)) == 0)
#define SQL_NULL_DATA              (-1)
#define SQL_NTS                    (-3)
#define SQL_HANDLE_ENV             1
#define SQL_HANDLE_DBC             2
#define SQL_HANDLE_STMT            3
#define SQL_NULL_HANDLE            nullptr
#define SQL_NULL_HENV              nullptr
#define SQL_NULL_HDBC              nullptr
#define SQL_NULL_HSTMT             nullptr
#define SQL_ATTR_ODBC_VERSION      200
#define SQL_OV_ODBC3               3UL
#define SQL_FETCH_NEXT             1
#define SQL_FETCH_FIRST            2
#define SQL_ATTR_CONNECTION_DEAD   1209
#define SQL_CD_TRUE                1L
#define SQL_CD_FALSE               0L
#define SQL_COMMIT                 0
#define SQL_ROLLBACK               1
#define SQL_ATTR_QUERY_TIMEOUT     0
#define SQL_NO_NULLS               0
#define SQL_DESC_TYPE_NAME         14
#define SQL_CHAR                   1
#define SQL_VARCHAR                12
#define SQL_WCHAR                  (-8)
#define SQL_WVARCHAR               (-9)
#define SQL_WLONGVARCHAR           (-10)
#define SQL_BINARY                 (-2)
#define SQL_VARBINARY              (-3)
#define SQL_LONGVARBINARY          (-4)
#define SQL_C_BINARY               SQL_BINARY
#define SQL_C_WCHAR                SQL_WCHAR
#define SQL_PARAM_INPUT            1
#define SQL_SEARCH_PATTERN_ESCAPE  14
#define SQL_DBMS_NAME              17
#define SQL_LOGIN_TIMEOUT          103
#define SQL_ATTR_LOGIN_TIMEOUT     103
#define SQL_ATTR_CONNECTION_TIMEOUT 113
#define SQL_ATTR_ACCESS_MODE       101
#define SQL_MODE_READ_ONLY         1UL
#define SQL_ATTR_AUTOCOMMIT        102
#define SQL_AUTOCOMMIT_OFF         0UL
#define SQL_AUTOCOMMIT_ON          1UL
#define SQL_DRIVER_NOPROMPT        0

namespace ft::odbc {

// ---- libodbc yukleyici ----------------------------------------------------
struct Api {
    void* lib = nullptr;
    SQLRETURN (*AllocHandle)(SQLSMALLINT, SQLHANDLE, SQLHANDLE*) = nullptr;
    SQLRETURN (*FreeHandle)(SQLSMALLINT, SQLHANDLE) = nullptr;
    SQLRETURN (*SetEnvAttr)(SQLHENV, SQLINTEGER, SQLPOINTER, SQLINTEGER) = nullptr;
    SQLRETURN (*DriversW)(SQLHENV, SQLUSMALLINT, char16_t*, SQLSMALLINT, SQLSMALLINT*, char16_t*, SQLSMALLINT, SQLSMALLINT*) = nullptr;
    SQLRETURN (*GetDiagRecW)(SQLSMALLINT, SQLHANDLE, SQLSMALLINT, char16_t*, SQLINTEGER*, char16_t*, SQLSMALLINT, SQLSMALLINT*) = nullptr;
    SQLRETURN (*GetConnectAttr)(SQLHDBC, SQLINTEGER, SQLPOINTER, SQLINTEGER, SQLINTEGER*) = nullptr;
    SQLRETURN (*SetConnectAttr)(SQLHDBC, SQLINTEGER, SQLPOINTER, SQLINTEGER) = nullptr;
    SQLRETURN (*EndTran)(SQLSMALLINT, SQLHANDLE, SQLSMALLINT) = nullptr;
    SQLRETURN (*Disconnect)(SQLHDBC) = nullptr;
    SQLRETURN (*SetStmtAttr)(SQLHSTMT, SQLINTEGER, SQLPOINTER, SQLINTEGER) = nullptr;
    SQLRETURN (*NumResultCols)(SQLHSTMT, SQLSMALLINT*) = nullptr;
    SQLRETURN (*RowCount)(SQLHSTMT, SQLLEN*) = nullptr;
    SQLRETURN (*DescribeColW)(SQLHSTMT, SQLUSMALLINT, char16_t*, SQLSMALLINT, SQLSMALLINT*, SQLSMALLINT*, SQLULEN*, SQLSMALLINT*, SQLSMALLINT*) = nullptr;
    SQLRETURN (*ColAttributeW)(SQLHSTMT, SQLUSMALLINT, SQLUSMALLINT, SQLPOINTER, SQLSMALLINT, SQLSMALLINT*, SQLLEN*) = nullptr;
    SQLRETURN (*Fetch)(SQLHSTMT) = nullptr;
    SQLRETURN (*GetData)(SQLHSTMT, SQLUSMALLINT, SQLSMALLINT, SQLPOINTER, SQLLEN, SQLLEN*) = nullptr;
    SQLRETURN (*MoreResults)(SQLHSTMT) = nullptr;
    SQLRETURN (*ExecDirectW)(SQLHSTMT, char16_t*, SQLINTEGER) = nullptr;
    SQLRETURN (*PrepareW)(SQLHSTMT, char16_t*, SQLINTEGER) = nullptr;
    SQLRETURN (*DescribeParam)(SQLHSTMT, SQLUSMALLINT, SQLSMALLINT*, SQLULEN*, SQLSMALLINT*, SQLSMALLINT*) = nullptr;
    SQLRETURN (*BindParameter)(SQLHSTMT, SQLUSMALLINT, SQLSMALLINT, SQLSMALLINT, SQLSMALLINT, SQLULEN, SQLSMALLINT, SQLPOINTER, SQLLEN, SQLLEN*) = nullptr;
    SQLRETURN (*Execute)(SQLHSTMT) = nullptr;
    SQLRETURN (*TablesW)(SQLHSTMT, char16_t*, SQLSMALLINT, char16_t*, SQLSMALLINT, char16_t*, SQLSMALLINT, char16_t*, SQLSMALLINT) = nullptr;
    SQLRETURN (*ColumnsW)(SQLHSTMT, char16_t*, SQLSMALLINT, char16_t*, SQLSMALLINT, char16_t*, SQLSMALLINT, char16_t*, SQLSMALLINT) = nullptr;
    SQLRETURN (*PrimaryKeysW)(SQLHSTMT, char16_t*, SQLSMALLINT, char16_t*, SQLSMALLINT, char16_t*, SQLSMALLINT) = nullptr;
    SQLRETURN (*GetInfoW)(SQLHDBC, SQLUSMALLINT, SQLPOINTER, SQLSMALLINT, SQLSMALLINT*) = nullptr;
    SQLRETURN (*Cancel)(SQLHSTMT) = nullptr;
    SQLRETURN (*CancelHandle)(SQLSMALLINT, SQLHANDLE) = nullptr;
    SQLRETURN (*DriverConnectW)(SQLHDBC, SQLHWND, char16_t*, SQLSMALLINT, char16_t*, SQLSMALLINT, SQLSMALLINT*, SQLUSMALLINT) = nullptr;
    bool ok = false;
};

inline Api& Lib() {
    static Api api;
    static std::once_flag once;
    std::call_once(once, [] {
        for (const char* name : {"libodbc.so.2", "libodbc.so.1", "libodbc.so"}) {
            api.lib = dlopen(name, RTLD_NOW | RTLD_LOCAL);
            if (api.lib) break;
        }
        if (!api.lib) return;
        auto sym = [](const char* n) { return dlsym(api.lib, n); };
#define FT_ODBC_LOAD(field, name) api.field = reinterpret_cast<decltype(api.field)>(sym(name))
        FT_ODBC_LOAD(AllocHandle, "SQLAllocHandle");
        FT_ODBC_LOAD(FreeHandle, "SQLFreeHandle");
        FT_ODBC_LOAD(SetEnvAttr, "SQLSetEnvAttr");
        FT_ODBC_LOAD(DriversW, "SQLDriversW");
        FT_ODBC_LOAD(GetDiagRecW, "SQLGetDiagRecW");
        FT_ODBC_LOAD(GetConnectAttr, "SQLGetConnectAttr");
        FT_ODBC_LOAD(SetConnectAttr, "SQLSetConnectAttr");
        FT_ODBC_LOAD(EndTran, "SQLEndTran");
        FT_ODBC_LOAD(Disconnect, "SQLDisconnect");
        FT_ODBC_LOAD(SetStmtAttr, "SQLSetStmtAttr");
        FT_ODBC_LOAD(NumResultCols, "SQLNumResultCols");
        FT_ODBC_LOAD(RowCount, "SQLRowCount");
        FT_ODBC_LOAD(DescribeColW, "SQLDescribeColW");
        FT_ODBC_LOAD(ColAttributeW, "SQLColAttributeW");
        FT_ODBC_LOAD(Fetch, "SQLFetch");
        FT_ODBC_LOAD(GetData, "SQLGetData");
        FT_ODBC_LOAD(MoreResults, "SQLMoreResults");
        FT_ODBC_LOAD(ExecDirectW, "SQLExecDirectW");
        FT_ODBC_LOAD(PrepareW, "SQLPrepareW");
        FT_ODBC_LOAD(DescribeParam, "SQLDescribeParam");
        FT_ODBC_LOAD(BindParameter, "SQLBindParameter");
        FT_ODBC_LOAD(Execute, "SQLExecute");
        FT_ODBC_LOAD(TablesW, "SQLTablesW");
        FT_ODBC_LOAD(ColumnsW, "SQLColumnsW");
        FT_ODBC_LOAD(PrimaryKeysW, "SQLPrimaryKeysW");
        FT_ODBC_LOAD(GetInfoW, "SQLGetInfoW");
        FT_ODBC_LOAD(Cancel, "SQLCancel");
        FT_ODBC_LOAD(CancelHandle, "SQLCancelHandle");
        FT_ODBC_LOAD(DriverConnectW, "SQLDriverConnectW");
#undef FT_ODBC_LOAD
        api.ok = api.AllocHandle && api.FreeHandle && api.SetEnvAttr && api.DriverConnectW && api.ExecDirectW &&
                 api.Fetch && api.GetData && api.GetDiagRecW;
    });
    return api;
}
inline bool Available() { return Lib().ok; }

// ---- UTF-32 <-> UTF-16 ------------------------------------------------------
inline std::u16string To16(const wchar_t* s, long len = SQL_NTS) {
    std::u16string out;
    if (!s) return out;
    const size_t n = len == SQL_NTS ? wcslen(s) : (size_t)std::max<long>(len, 0);
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        uint32_t cp = (uint32_t)s[i];
        if (cp >= 0x10000 && cp <= 0x10FFFF) {
            cp -= 0x10000;
            out.push_back((char16_t)(0xD800 + (cp >> 10)));
            out.push_back((char16_t)(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back((char16_t)cp);
        }
    }
    return out;
}
// n16 UTF-16 birimini wchar_t tampona yazar (cap: wchar_t sayisi, sonlandirici dahil). Yazilan sayi doner.
inline size_t From16(const char16_t* s, size_t n16, wchar_t* out, size_t cap) {
    if (!out || cap == 0) return 0;
    size_t w = 0;
    for (size_t i = 0; i < n16 && w + 1 < cap; ++i) {
        uint32_t c = s[i];
        if (c == 0) break;
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < n16 && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00);
            ++i;
        }
        out[w++] = (wchar_t)c;
    }
    out[w] = 0;
    return w;
}
inline size_t Len16(const char16_t* s, size_t cap) { size_t n = 0; while (n < cap && s[n]) ++n; return n; }

// Ertelenmis parametre tamponlari (statement basina)
inline std::mutex& BindMutex() { static std::mutex m; return m; }
inline std::map<SQLHSTMT, std::vector<std::unique_ptr<std::u16string>>>& BoundStore() {
    static std::map<SQLHSTMT, std::vector<std::unique_ptr<std::u16string>>> m;
    return m;
}

// Bir *W giris parametresi: nullptr kalir, aksi halde UTF-16 kopya
struct In16 {
    std::u16string s; bool null = true;
    In16(const SQLWCHAR* p, long len) { if (p) { s = To16(p, len); null = false; } }
    char16_t* ptr() { return null ? nullptr : s.data(); }
    SQLSMALLINT len16(long orig) const { return null ? (SQLSMALLINT)0 : (orig == SQL_NTS ? (SQLSMALLINT)SQL_NTS : (SQLSMALLINT)s.size()); }
};

} // namespace ft::odbc

// ---- Uygulamanin kullandigi ODBC fonksiyonlari (Windows imzalariyla) ----------
#define FT_ODBC_REQUIRE(fn) if (!ft::odbc::Lib().ok || !ft::odbc::Lib().fn) return SQL_ERROR

inline SQLRETURN SQLAllocHandle(SQLSMALLINT type, SQLHANDLE input, SQLHANDLE* output) {
    if (output) *output = nullptr;
    FT_ODBC_REQUIRE(AllocHandle);
    return ft::odbc::Lib().AllocHandle(type, input, output);
}
inline SQLRETURN SQLFreeHandle(SQLSMALLINT type, SQLHANDLE h) {
    FT_ODBC_REQUIRE(FreeHandle);
    if (type == SQL_HANDLE_STMT) {
        std::lock_guard<std::mutex> lock(ft::odbc::BindMutex());
        ft::odbc::BoundStore().erase(h);
    }
    return ft::odbc::Lib().FreeHandle(type, h);
}
inline SQLRETURN SQLSetEnvAttr(SQLHENV env, SQLINTEGER attr, SQLPOINTER value, SQLINTEGER len) {
    FT_ODBC_REQUIRE(SetEnvAttr);
    return ft::odbc::Lib().SetEnvAttr(env, attr, value, len);
}
inline SQLRETURN SQLDriversW(SQLHENV env, SQLUSMALLINT dir, SQLWCHAR* name, SQLSMALLINT nameCap, SQLSMALLINT* nameLen,
                             SQLWCHAR* attrs, SQLSMALLINT attrCap, SQLSMALLINT* attrLen) {
    FT_ODBC_REQUIRE(DriversW);
    std::vector<char16_t> n16((size_t)std::max<int>(nameCap, 1) * 2), a16((size_t)std::max<int>(attrCap, 1) * 2);
    SQLSMALLINT nl = 0, al = 0;
    const SQLRETURN rc = ft::odbc::Lib().DriversW(env, dir, n16.data(), (SQLSMALLINT)(n16.size() - 1), &nl,
                                                  a16.data(), (SQLSMALLINT)(a16.size() - 1), &al);
    if (SQL_SUCCEEDED(rc)) {
        const size_t wn = ft::odbc::From16(n16.data(), (size_t)std::max<int>(nl, 0), name, (size_t)nameCap);
        // Ozellik listesi NUL ile ayrilmis ciftler; ilk ogeyi donusturmek yeterli (kullanilmiyor).
        if (attrs && attrCap > 0) ft::odbc::From16(a16.data(), ft::odbc::Len16(a16.data(), a16.size()), attrs, (size_t)attrCap);
        if (nameLen) *nameLen = (SQLSMALLINT)wn;
        if (attrLen) *attrLen = al;
    }
    return rc;
}
inline SQLRETURN SQLGetDiagRecW(SQLSMALLINT kind, SQLHANDLE h, SQLSMALLINT rec, SQLWCHAR* state, SQLINTEGER* native,
                                SQLWCHAR* message, SQLSMALLINT cap, SQLSMALLINT* len) {
    FT_ODBC_REQUIRE(GetDiagRecW);
    char16_t st16[6]{};
    std::vector<char16_t> msg16((size_t)std::max<int>(cap, 1) * 2 + 2);
    SQLSMALLINT l16 = 0;
    const SQLRETURN rc = ft::odbc::Lib().GetDiagRecW(kind, h, rec, st16, native, message ? msg16.data() : nullptr,
                                                     message ? (SQLSMALLINT)std::min<size_t>(msg16.size() - 1, 32767) : 0, &l16);
    if (SQL_SUCCEEDED(rc)) {
        if (state) ft::odbc::From16(st16, 5, state, 6);
        size_t wn = 0;
        if (message && cap > 0) wn = ft::odbc::From16(msg16.data(), (size_t)std::max<int>(l16, 0), message, (size_t)cap);
        if (len) *len = (SQLSMALLINT)(message ? wn : l16);
    }
    return rc;
}
inline SQLRETURN SQLGetConnectAttr(SQLHDBC db, SQLINTEGER attr, SQLPOINTER value, SQLINTEGER cap, SQLINTEGER* len) {
    FT_ODBC_REQUIRE(GetConnectAttr);
    return ft::odbc::Lib().GetConnectAttr(db, attr, value, cap, len);
}
inline SQLRETURN SQLSetConnectAttr(SQLHDBC db, SQLINTEGER attr, SQLPOINTER value, SQLINTEGER len) {
    FT_ODBC_REQUIRE(SetConnectAttr);
    return ft::odbc::Lib().SetConnectAttr(db, attr, value, len);
}
inline SQLRETURN SQLEndTran(SQLSMALLINT type, SQLHANDLE h, SQLSMALLINT completion) {
    FT_ODBC_REQUIRE(EndTran);
    return ft::odbc::Lib().EndTran(type, h, completion);
}
inline SQLRETURN SQLDisconnect(SQLHDBC db) { FT_ODBC_REQUIRE(Disconnect); return ft::odbc::Lib().Disconnect(db); }
inline SQLRETURN SQLSetStmtAttr(SQLHSTMT s, SQLINTEGER attr, SQLPOINTER value, SQLINTEGER len) {
    FT_ODBC_REQUIRE(SetStmtAttr);
    return ft::odbc::Lib().SetStmtAttr(s, attr, value, len);
}
inline SQLRETURN SQLNumResultCols(SQLHSTMT s, SQLSMALLINT* count) {
    if (count) *count = 0;
    FT_ODBC_REQUIRE(NumResultCols);
    return ft::odbc::Lib().NumResultCols(s, count);
}
inline SQLRETURN SQLRowCount(SQLHSTMT s, SQLLEN* count) { FT_ODBC_REQUIRE(RowCount); return ft::odbc::Lib().RowCount(s, count); }
inline SQLRETURN SQLDescribeColW(SQLHSTMT s, SQLUSMALLINT col, SQLWCHAR* name, SQLSMALLINT cap, SQLSMALLINT* len,
                                 SQLSMALLINT* type, SQLULEN* size, SQLSMALLINT* scale, SQLSMALLINT* nullable) {
    FT_ODBC_REQUIRE(DescribeColW);
    std::vector<char16_t> n16((size_t)std::max<int>(cap, 1) * 2 + 2);
    SQLSMALLINT l16 = 0;
    const SQLRETURN rc = ft::odbc::Lib().DescribeColW(s, col, n16.data(), (SQLSMALLINT)std::min<size_t>(n16.size() - 1, 32767),
                                                      &l16, type, size, scale, nullable);
    if (SQL_SUCCEEDED(rc)) {
        const size_t wn = ft::odbc::From16(n16.data(), (size_t)std::max<int>(l16, 0), name, (size_t)cap);
        if (len) *len = (SQLSMALLINT)wn;
    }
    return rc;
}
// Karakter ozellikleri icin bufferBytes wchar_t cinsinden bayt (Windows'taki sizeof(buffer) kullanimi).
inline SQLRETURN SQLColAttributeW(SQLHSTMT s, SQLUSMALLINT col, SQLUSMALLINT field, SQLPOINTER buffer, SQLSMALLINT bufferBytes,
                                  SQLSMALLINT* outBytes, SQLLEN* numeric) {
    FT_ODBC_REQUIRE(ColAttributeW);
    if (field != SQL_DESC_TYPE_NAME || !buffer) return ft::odbc::Lib().ColAttributeW(s, col, field, buffer, bufferBytes, outBytes, numeric);
    const size_t capW = (size_t)std::max<int>(bufferBytes, 0) / sizeof(wchar_t);
    std::vector<char16_t> b16(capW * 2 + 2);
    SQLSMALLINT ob = 0;
    const SQLRETURN rc = ft::odbc::Lib().ColAttributeW(s, col, field, b16.data(), (SQLSMALLINT)((b16.size() - 1) * 2), &ob, numeric);
    if (SQL_SUCCEEDED(rc)) {
        const size_t wn = ft::odbc::From16(b16.data(), (size_t)std::max<int>(ob, 0) / 2, (wchar_t*)buffer, capW);
        if (outBytes) *outBytes = (SQLSMALLINT)(wn * sizeof(wchar_t));
    }
    return rc;
}
inline SQLRETURN SQLFetch(SQLHSTMT s) { FT_ODBC_REQUIRE(Fetch); return ft::odbc::Lib().Fetch(s); }
inline SQLRETURN SQLGetData(SQLHSTMT s, SQLUSMALLINT col, SQLSMALLINT ctype, SQLPOINTER buffer, SQLLEN bufferBytes, SQLLEN* length) {
    FT_ODBC_REQUIRE(GetData);
    if (ctype != SQL_C_WCHAR || !buffer) return ft::odbc::Lib().GetData(s, col, ctype, buffer, bufferBytes, length);
    // wchar_t tampon (UTF-32) -> ayni karakter kapasitesiyle UTF-16 parca oku
    const size_t capW = (size_t)std::max<SQLLEN>(bufferBytes, 0) / sizeof(wchar_t);
    if (capW == 0) return SQL_ERROR;
    std::vector<char16_t> b16(capW);
    SQLLEN l16 = 0;
    const SQLRETURN rc = ft::odbc::Lib().GetData(s, col, SQL_C_WCHAR, b16.data(), (SQLLEN)(b16.size() * sizeof(char16_t)), &l16);
    if (SQL_SUCCEEDED(rc)) {
        if (l16 == SQL_NULL_DATA) { ((wchar_t*)buffer)[0] = 0; if (length) *length = SQL_NULL_DATA; return rc; }
        const size_t got16 = ft::odbc::Len16(b16.data(), b16.size());
        const size_t wn = ft::odbc::From16(b16.data(), got16, (wchar_t*)buffer, capW);
        if (length) *length = l16 >= 0 ? (SQLLEN)(wn * sizeof(wchar_t)) : l16;
    }
    return rc;
}
inline SQLRETURN SQLMoreResults(SQLHSTMT s) { FT_ODBC_REQUIRE(MoreResults); return ft::odbc::Lib().MoreResults(s); }
inline SQLRETURN SQLExecDirectW(SQLHSTMT s, SQLWCHAR* sql, SQLINTEGER len) {
    FT_ODBC_REQUIRE(ExecDirectW);
    auto t = ft::odbc::To16(sql, len);
    return ft::odbc::Lib().ExecDirectW(s, t.data(), (SQLINTEGER)t.size());
}
inline SQLRETURN SQLPrepareW(SQLHSTMT s, SQLWCHAR* sql, SQLINTEGER len) {
    FT_ODBC_REQUIRE(PrepareW);
    auto t = ft::odbc::To16(sql, len);
    return ft::odbc::Lib().PrepareW(s, t.data(), (SQLINTEGER)t.size());
}
inline SQLRETURN SQLDescribeParam(SQLHSTMT s, SQLUSMALLINT n, SQLSMALLINT* type, SQLULEN* size, SQLSMALLINT* scale, SQLSMALLINT* nullable) {
    FT_ODBC_REQUIRE(DescribeParam);
    return ft::odbc::Lib().DescribeParam(s, n, type, size, scale, nullable);
}
inline SQLRETURN SQLBindParameter(SQLHSTMT s, SQLUSMALLINT n, SQLSMALLINT io, SQLSMALLINT ctype, SQLSMALLINT sqltype, SQLULEN size,
                                  SQLSMALLINT scale, SQLPOINTER value, SQLLEN bufferBytes, SQLLEN* lenInd) {
    FT_ODBC_REQUIRE(BindParameter);
    if (ctype != SQL_C_WCHAR || !value) return ft::odbc::Lib().BindParameter(s, n, io, ctype, sqltype, size, scale, value, bufferBytes, lenInd);
    // Ertelenmis baglama: UTF-16 kopya statement serbest birakilana kadar yasamali.
    auto copy = std::make_unique<std::u16string>(ft::odbc::To16((const wchar_t*)value));
    char16_t* data = copy->data();
    const SQLLEN bytes = (SQLLEN)((copy->size() + 1) * sizeof(char16_t));
    {
        std::lock_guard<std::mutex> lock(ft::odbc::BindMutex());
        ft::odbc::BoundStore()[s].push_back(std::move(copy));
    }
    return ft::odbc::Lib().BindParameter(s, n, io, ctype, sqltype, size, scale, data, bytes, lenInd);
}
inline SQLRETURN SQLExecute(SQLHSTMT s) { FT_ODBC_REQUIRE(Execute); return ft::odbc::Lib().Execute(s); }
inline SQLRETURN SQLTablesW(SQLHSTMT s, SQLWCHAR* c, SQLSMALLINT cl, SQLWCHAR* sc, SQLSMALLINT sl, SQLWCHAR* t, SQLSMALLINT tl,
                            SQLWCHAR* ty, SQLSMALLINT tyl) {
    FT_ODBC_REQUIRE(TablesW);
    ft::odbc::In16 a(c, cl), b(sc, sl), d(t, tl), e(ty, tyl);
    return ft::odbc::Lib().TablesW(s, a.ptr(), a.len16(cl), b.ptr(), b.len16(sl), d.ptr(), d.len16(tl), e.ptr(), e.len16(tyl));
}
inline SQLRETURN SQLColumnsW(SQLHSTMT s, SQLWCHAR* c, SQLSMALLINT cl, SQLWCHAR* sc, SQLSMALLINT sl, SQLWCHAR* t, SQLSMALLINT tl,
                             SQLWCHAR* col, SQLSMALLINT coll) {
    FT_ODBC_REQUIRE(ColumnsW);
    ft::odbc::In16 a(c, cl), b(sc, sl), d(t, tl), e(col, coll);
    return ft::odbc::Lib().ColumnsW(s, a.ptr(), a.len16(cl), b.ptr(), b.len16(sl), d.ptr(), d.len16(tl), e.ptr(), e.len16(coll));
}
inline SQLRETURN SQLPrimaryKeysW(SQLHSTMT s, SQLWCHAR* c, SQLSMALLINT cl, SQLWCHAR* sc, SQLSMALLINT sl, SQLWCHAR* t, SQLSMALLINT tl) {
    FT_ODBC_REQUIRE(PrimaryKeysW);
    ft::odbc::In16 a(c, cl), b(sc, sl), d(t, tl);
    return ft::odbc::Lib().PrimaryKeysW(s, a.ptr(), a.len16(cl), b.ptr(), b.len16(sl), d.ptr(), d.len16(tl));
}
// Yalnizca metin donen bilgi tipleri kullaniliyor (SQL_DBMS_NAME, SQL_SEARCH_PATTERN_ESCAPE).
inline SQLRETURN SQLGetInfoW(SQLHDBC db, SQLUSMALLINT type, SQLPOINTER buffer, SQLSMALLINT bufferBytes, SQLSMALLINT* outBytes) {
    FT_ODBC_REQUIRE(GetInfoW);
    if (!buffer) return ft::odbc::Lib().GetInfoW(db, type, buffer, bufferBytes, outBytes);
    const size_t capW = (size_t)std::max<int>(bufferBytes, 0) / sizeof(wchar_t);
    std::vector<char16_t> b16(capW * 2 + 2);
    SQLSMALLINT ob = 0;
    const SQLRETURN rc = ft::odbc::Lib().GetInfoW(db, type, b16.data(), (SQLSMALLINT)((b16.size() - 1) * 2), &ob);
    if (SQL_SUCCEEDED(rc)) {
        const size_t wn = ft::odbc::From16(b16.data(), (size_t)std::max<int>(ob, 0) / 2, (wchar_t*)buffer, capW);
        if (outBytes) *outBytes = (SQLSMALLINT)(wn * sizeof(wchar_t));
    }
    return rc;
}
inline SQLRETURN SQLCancel(SQLHSTMT s) { FT_ODBC_REQUIRE(Cancel); return ft::odbc::Lib().Cancel(s); }
inline SQLRETURN SQLCancelHandle(SQLSMALLINT type, SQLHANDLE h) {
    FT_ODBC_REQUIRE(Cancel);
    if (ft::odbc::Lib().CancelHandle) return ft::odbc::Lib().CancelHandle(type, h);
    return type == SQL_HANDLE_STMT ? ft::odbc::Lib().Cancel(h) : SQL_ERROR;
}
inline SQLRETURN SQLDriverConnectW(SQLHDBC db, SQLHWND hwnd, SQLWCHAR* in, SQLSMALLINT inLen, SQLWCHAR* out, SQLSMALLINT outCap,
                                   SQLSMALLINT* outLen, SQLUSMALLINT completion) {
    FT_ODBC_REQUIRE(DriverConnectW);
    auto conn = ft::odbc::To16(in, inLen);
    std::vector<char16_t> o16(out && outCap > 0 ? (size_t)outCap * 2 + 2 : 0);
    SQLSMALLINT ol = 0;
    const SQLRETURN rc = ft::odbc::Lib().DriverConnectW(db, hwnd, conn.data(), (SQLSMALLINT)conn.size(),
                                                        o16.empty() ? nullptr : o16.data(), (SQLSMALLINT)(o16.empty() ? 0 : o16.size() - 1),
                                                        &ol, completion);
    std::fill(conn.begin(), conn.end(), u'\0'); // baglanti dizesi parola icerir
    if (SQL_SUCCEEDED(rc) && out && outCap > 0) {
        const size_t wn = ft::odbc::From16(o16.data(), (size_t)std::max<int>(ol, 0), out, (size_t)outCap);
        if (outLen) *outLen = (SQLSMALLINT)wn;
    }
    return rc;
}
#undef FT_ODBC_REQUIRE
#endif
