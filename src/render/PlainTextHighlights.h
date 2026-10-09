#pragma once
#include "vt/Cell.h"
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace ft {

// Presentation only. These are palette indices, not RGB or changes to the VT
// screen. A resize, copy, recording or theme change still sees the original data.
// 0 means unchanged; other values encode ANSI index + 1.
inline void PlainTextHighlights(const Cell* row, int cols, std::vector<uint8_t>& colors) {
    colors.assign(cols, 0);
    std::u32string text;
    text.reserve(cols);
    for (int x = 0; x < cols; ++x)
        text += (row[x].flags & CF_WideTail) || !row[x].ch ? U' ' : row[x].ch;
    const auto end = text.find_last_not_of(U" \t");
    if (end == std::u32string::npos) return;
    text.resize(end + 1);
    auto mark = [&](size_t start, size_t stop, uint8_t ansi) {
        for (size_t x = start; x < std::min(stop, text.size()); ++x) {
            const auto flags = row[x].flags;
            if ((flags & (CF_FgDefault | CF_BgDefault)) == (CF_FgDefault | CF_BgDefault) &&
                !(flags & (CF_FgIndexed | CF_BgIndexed | CF_Inverse | CF_Hidden | CF_Dim | CF_WideTail)))
                colors[x] = ansi + 1;
        }
    };
    const auto start = text.find_first_not_of(U" \t");
    const std::u32string_view trimmed(text.data() + start, text.size() - start);
    if (trimmed.starts_with(U"Welcome to ") || trimmed.starts_with(U"System information"))
        mark(start, text.size(), 6);
    if (trimmed.starts_with(U"Last login:")) mark(start, start + 11, 4);
    if (trimmed.starts_with(U"*** System restart required") || trimmed.starts_with(U"New release '"))
        mark(start, text.size(), 3);

    for (const auto label : {U"System load:", U"Temperature:", U"Usage of /:", U"Processes:",
                            U"Memory usage:", U"Users logged in:", U"Swap usage:", U"IPv4 address for"}) {
        const size_t at = text.find(label);
        if (at != std::u32string::npos) mark(at, at + std::char_traits<char32_t>::length(label), 6);
    }

    // Numeric values, addresses and versions, delimited like shell output tokens.
    for (size_t x = 0; x < text.size(); ++x) {
        if (text[x] < U'0' || text[x] > U'9' ||
            (x && text[x-1] != U' ' && text[x-1] != U'\t' && text[x-1] != U':' && text[x-1] != U'\'')) continue;
        size_t to = x + 1;
        while (to < text.size() && ((text[to] >= U'0' && text[to] <= U'9') ||
                                   text[to] == U'.' || text[to] == U'%' || text[to] == U'/')) ++to;
        mark(x, to, 3);
        x = to - 1;
    }
    // Prompt: color user@host separately from the working directory. Typed
    // commands following the prompt are left intact (including explicit ANSI).
    const size_t marker = text.find_first_of(U"$#>%❯➜", start);
    if (marker != std::u32string::npos && (marker + 1 == text.size() || text[marker+1] == U' ')) {
        const auto prefix = text.substr(start, marker - start);
        const size_t at = prefix.find(U'@');
        const size_t colon = prefix.find(U':', at == std::u32string::npos ? 0 : at);
        if (marker == start || (at != std::u32string::npos && colon != std::u32string::npos) ||
            prefix.starts_with(U"PS ") || (prefix.size() > 2 && prefix[1] == U':' && prefix[2] == U'\\')) {
            mark(start, marker, 4);
            if (at != std::u32string::npos && colon != std::u32string::npos) mark(start, start + colon, 2);
            mark(marker, marker + 1, 5);
        }
    }
    for (const auto scheme : {U"https://", U"http://"}) {
        size_t at = 0;
        while ((at = text.find(scheme, at)) != std::u32string::npos) {
            size_t to = text.find_first_of(U" \t<>\"", at);
            if (to == std::u32string::npos) to = text.size();
            mark(at, to, 6);
            at = to;
        }
    }
}
} // namespace ft
