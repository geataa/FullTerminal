// VT ayristirici + ekran modeli hiz olcumu (NFR-007).
// ConPTY'yi devreden cikarir: dosyayi dogrudan ayristiriciya verir.
//
//   bench_vt.exe <dosya> [sutun] [satir]
//
#include "vt/Screen.h"
#include "vt/VtParser.h"

#include <windows.h>
#include <cstdio>
#include <vector>
#include <string>

namespace {

double Seconds(LARGE_INTEGER a, LARGE_INTEGER b) {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return (double)(b.QuadPart - a.QuadPart) / (double)f.QuadPart;
}

std::vector<char> ReadAll(const wchar_t* path) {
    std::vector<char> data;
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) return data;
    LARGE_INTEGER size{};
    GetFileSizeEx(h, &size);
    data.resize((size_t)size.QuadPart);
    size_t off = 0;
    while (off < data.size()) {
        DWORD want = (DWORD)std::min<size_t>(1u << 20, data.size() - off);
        DWORD got = 0;
        if (!ReadFile(h, data.data() + off, want, &got, nullptr) || got == 0) break;
        off += got;
    }
    data.resize(off);
    CloseHandle(h);
    return data;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    const wchar_t* path = (argc > 1) ? argv[1] : L"bin\\bench.txt";
    const int cols = (argc > 2) ? _wtoi(argv[2]) : 126;
    const int rows = (argc > 3) ? _wtoi(argv[3]) : 36;

    std::vector<char> data = ReadAll(path);
    if (data.empty()) {
        wprintf(L"Dosya okunamadi: %s\n", path);
        return 1;
    }
    wprintf(L"Girdi   : %s\n", path);
    wprintf(L"Boyut   : %.1f MB\n", (double)data.size() / (1024.0 * 1024.0));
    wprintf(L"Izgara  : %dx%d\n\n", cols, rows);

    for (int pass = 0; pass < 3; ++pass) {
        ft::Screen screen;
        screen.Resize(cols, rows);
        ft::VtParser parser(screen);

        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);

        // ConPTY okuyucusuyla ayni parca boyutu.
        const size_t chunk = 64 * 1024;
        for (size_t off = 0; off < data.size(); off += chunk) {
            const size_t n = (std::min)(chunk, data.size() - off);
            parser.Feed(data.data() + off, n);
        }

        QueryPerformanceCounter(&t1);
        const double sec = Seconds(t0, t1);
        const double mbs = (double)data.size() / (1024.0 * 1024.0) / sec;
        wprintf(L"Tur %d   : %7.3f s   %8.1f MB/s   scrollback %d satir\n",
                pass + 1, sec, mbs, screen.ScrollbackRows());
    }
    return 0;
}
