#pragma once

#include <array>
#include <cwctype>
#include <cwchar>
#include <string>

inline std::wstring TrimVersion(std::wstring text)
{
    if (!text.empty() && text[0] == L'v') {
        text.erase(text.begin());
    }
    while (!text.empty() && iswspace(text.front())) {
        text.erase(text.begin());
    }
    while (!text.empty() && iswspace(text.back())) {
        text.pop_back();
    }
    return text;
}

inline bool ParseVersion(const std::wstring& text, std::array<int, 3>& parts)
{
    return swscanf_s(text.c_str(), L"%d.%d.%d", &parts[0], &parts[1], &parts[2]) == 3;
}

inline int CompareVersions(const std::wstring& left, const std::wstring& right)
{
    std::array<int, 3> a {};
    std::array<int, 3> b {};
    if (!ParseVersion(left, a) || !ParseVersion(right, b)) {
        return 0;
    }
    for (int i = 0; i < 3; ++i) {
        if (a[i] != b[i]) {
            return a[i] < b[i] ? -1 : 1;
        }
    }
    return 0;
}
