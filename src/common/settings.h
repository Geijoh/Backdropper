#pragma once

#include <windows.h>
#include <string>

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
};

BackdropperSettings LoadBackdropperSettings();
HRESULT SaveBackdropperSettings(const BackdropperSettings& settings);
std::wstring FormatColor(COLORREF color);
bool ParseColor(const wchar_t* text, COLORREF* color);
