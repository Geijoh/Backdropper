#pragma once

#include <windows.h>

#include <array>
#include <cstddef>
#include <string>

inline constexpr std::array<const wchar_t*, 12> kBackdropperFormats = {
    L".png", L".webp", L".gif", L".ico", L".svg", L".psd",
    L".ai", L".eps", L".pdf", L".avif", L".tga", L".dds",
};

inline constexpr size_t kBackdropperFormatCount = kBackdropperFormats.size();

enum class BackdropMode {
    None,
    Solid,
    Checker,
};

struct BackdropperSettings {
    BackdropMode mode = BackdropMode::Checker;
    COLORREF solidColor = RGB(255, 255, 255);
    COLORREF checkerA = RGB(255, 255, 255);
    COLORREF checkerB = RGB(200, 200, 200);
    UINT checkerSize = 8;
    bool deleteThumbnailDbsOnSave = true;
    std::array<bool, kBackdropperFormatCount> enabledFormats = {
        true, true, true, true, true, true,
        true, true, true, true, true, true,
    };
};

BackdropperSettings LoadBackdropperSettings();
HRESULT SaveBackdropperSettings(const BackdropperSettings& settings);
std::wstring FormatColor(COLORREF color);
bool ParseColor(const wchar_t* text, COLORREF* color);
