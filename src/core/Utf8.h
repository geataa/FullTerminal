#pragma once
#include <string>
#include <string_view>
#include <cstdint>

namespace ft {

std::wstring Utf8ToWide(std::string_view s);
std::string  WideToUtf8(std::wstring_view s);

// Bir kod noktasini UTF-8 olarak ekler.
void AppendUtf8(std::string& out, char32_t cp);

// Terminal hucre genisligi: 0 (birlesen), 1 (dar) veya 2 (genis).
int CharWidth(char32_t cp);

} // namespace ft
