#include "settings.h"

#include <algorithm>
#include <cwchar>

namespace {

constexpr wchar_t kSettingsKey[] = L"Software\\Backdropper";

std::wstring ReadString(HKEY key, const wchar_t* name, const wchar_t* fallback)
{
    wchar_t buffer[64] = {};
    DWORD bytes = sizeof(buffer);
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, buffer, &bytes) != ERROR_SUCCESS) {
        return fallback;
    }
    return buffer;
}

DWORD ReadDword(HKEY key, const wchar_t* name, DWORD fallback)
{
    DWORD value = fallback;
    DWORD bytes = sizeof(value);
    RegGetValueW(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &value, &bytes);
    return value;
}

void WriteString(HKEY key, const wchar_t* name, const std::wstring& value)
{
    RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

void WriteDword(HKEY key, const wchar_t* name, DWORD value)
{
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
}

BackdropMode ParseMode(const std::wstring& value)
{
    if (value == L"none") {
        return BackdropMode::None;
    }
    if (value == L"solid") {
        return BackdropMode::Solid;
    }
    return BackdropMode::Checker;
}

std::wstring FormatMode(BackdropMode mode)
{
    if (mode == BackdropMode::None) {
        return L"none";
    }
    if (mode == BackdropMode::Solid) {
        return L"solid";
    }
    return L"checker";
}

}

BackdropperSettings LoadBackdropperSettings()
{
    BackdropperSettings settings;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return settings;
    }

    settings.mode = ParseMode(ReadString(key, L"Mode", L"checker"));
    ParseColor(ReadString(key, L"SolidColor", L"#FFFFFF").c_str(), &settings.solidColor);
    ParseColor(ReadString(key, L"CheckerColorA", L"#FFFFFF").c_str(), &settings.checkerA);
    ParseColor(ReadString(key, L"CheckerColorB", L"#C8C8C8").c_str(), &settings.checkerB);
    settings.checkerSize = std::max(1u, std::min(64u, static_cast<UINT>(ReadDword(key, L"CheckerSize", 8))));
    settings.deleteThumbnailDbsOnSave = ReadDword(key, L"DeleteThumbnailDbsOnSave", 1) != 0;
    RegCloseKey(key);
    return settings;
}

HRESULT SaveBackdropperSettings(const BackdropperSettings& settings)
{
    HKEY key = nullptr;
    const LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr, 0, KEY_WRITE,
        nullptr, &key, nullptr);
    if (status != ERROR_SUCCESS) {
        return HRESULT_FROM_WIN32(status);
    }

    WriteString(key, L"Mode", FormatMode(settings.mode));
    WriteString(key, L"SolidColor", FormatColor(settings.solidColor));
    WriteString(key, L"CheckerColorA", FormatColor(settings.checkerA));
    WriteString(key, L"CheckerColorB", FormatColor(settings.checkerB));
    WriteDword(key, L"CheckerSize", std::max(1u, std::min(64u, settings.checkerSize)));
    WriteDword(key, L"DeleteThumbnailDbsOnSave", settings.deleteThumbnailDbsOnSave ? 1 : 0);
    RegCloseKey(key);
    return S_OK;
}

std::wstring FormatColor(COLORREF color)
{
    wchar_t buffer[8] = {};
    swprintf_s(buffer, L"#%02X%02X%02X", GetRValue(color), GetGValue(color), GetBValue(color));
    return buffer;
}

bool ParseColor(const wchar_t* text, COLORREF* color)
{
    if (!text || !color) {
        return false;
    }

    unsigned int r = 0;
    unsigned int g = 0;
    unsigned int b = 0;
    if (swscanf_s(text, L"#%02x%02x%02x", &r, &g, &b) != 3 &&
        swscanf_s(text, L"%02x%02x%02x", &r, &g, &b) != 3) {
        return false;
    }

    if (r > 255 || g > 255 || b > 255) {
        return false;
    }

    *color = RGB(r, g, b);
    return true;
}
