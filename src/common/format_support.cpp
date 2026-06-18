#include "format_support.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cwctype>

using Microsoft::WRL::ComPtr;

namespace {

std::wstring Lower(std::wstring value)
{
    for (wchar_t& ch : value) {
        ch = static_cast<wchar_t>(std::towlower(ch));
    }
    return value;
}

bool ExtensionListContains(const std::wstring& extensions, const wchar_t* extension)
{
    const std::wstring wanted = Lower(extension);
    std::wstring token;
    for (const wchar_t ch : extensions + L",") {
        if (ch == L'\0' || ch == L',' || ch == L';' || iswspace(ch)) {
            if (Lower(token) == wanted) {
                return true;
            }
            token.clear();
        } else {
            token.push_back(ch);
        }
    }
    return false;
}

std::wstring FindGhostscriptUnder(const wchar_t* base, const wchar_t* exe)
{
    std::wstring pattern = std::wstring(base) + L"\\gs*";
    WIN32_FIND_DATAW data = {};
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    while (find != INVALID_HANDLE_VALUE) {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && wcscmp(data.cFileName, L".") != 0 && wcscmp(data.cFileName, L"..") != 0) {
            std::wstring candidate = std::wstring(base) + L"\\" + data.cFileName + L"\\bin\\" + exe;
            if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
                FindClose(find);
                return candidate;
            }
        }
        if (!FindNextFileW(find, &data)) {
            break;
        }
    }
    if (find != INVALID_HANDLE_VALUE) {
        FindClose(find);
    }
    return {};
}

}

std::wstring BackdropperFormatLabel(const wchar_t* extension)
{
    std::wstring label;
    for (const wchar_t* ch = extension; *ch; ++ch) {
        if (*ch != L'.') {
            label.push_back(static_cast<wchar_t>(std::towupper(*ch)));
        }
    }
    return label;
}

bool BackdropperHasBuiltInRenderer(const wchar_t* extension)
{
    return wcscmp(extension, L".png") == 0 || wcscmp(extension, L".svg") == 0
        || wcscmp(extension, L".pdf") == 0 || wcscmp(extension, L".ai") == 0
        || wcscmp(extension, L".psd") == 0 || wcscmp(extension, L".tga") == 0;
}

bool BackdropperWicSupportsExtension(const wchar_t* extension)
{
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitialize = init == S_OK || init == S_FALSE;
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) {
        return wcscmp(extension, L".png") == 0;
    }

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        if (uninitialize) {
            CoUninitialize();
        }
        return wcscmp(extension, L".png") == 0;
    }

    ComPtr<IEnumUnknown> decoders;
    hr = factory->CreateComponentEnumerator(WICDecoder, WICComponentEnumerateDefault, &decoders);
    bool supported = false;
    if (SUCCEEDED(hr)) {
        ComPtr<IUnknown> unknown;
        while (!supported && decoders->Next(1, &unknown, nullptr) == S_OK) {
            ComPtr<IWICBitmapCodecInfo> info;
            if (SUCCEEDED(unknown.As(&info))) {
                UINT length = 0;
                if (SUCCEEDED(info->GetFileExtensions(0, nullptr, &length)) && length > 0) {
                    std::wstring list(length, L'\0');
                    if (SUCCEEDED(info->GetFileExtensions(length, list.data(), &length))) {
                        supported = ExtensionListContains(list, extension);
                    }
                }
            }
            unknown.Reset();
        }
    }

    if (uninitialize) {
        CoUninitialize();
    }
    return supported;
}

std::wstring FindGhostscriptExecutable()
{
    wchar_t path[MAX_PATH] = {};
    if (SearchPathW(nullptr, L"gswin64c.exe", nullptr, ARRAYSIZE(path), path, nullptr)) {
        return path;
    }
    if (SearchPathW(nullptr, L"gswin32c.exe", nullptr, ARRAYSIZE(path), path, nullptr)) {
        return path;
    }

    for (const auto& candidate : {
        FindGhostscriptUnder(L"C:\\Program Files\\gs", L"gswin64c.exe"),
        FindGhostscriptUnder(L"C:\\Program Files (x86)\\gs", L"gswin32c.exe") }) {
        if (!candidate.empty()) {
            return candidate;
        }
    }
    return {};
}

bool BackdropperHasGhostscript()
{
    return !FindGhostscriptExecutable().empty();
}

bool CanRegisterBackdropperFormat(const wchar_t* extension)
{
    if (wcscmp(extension, L".eps") == 0) {
        // ponytail: Ghostscript is optional; don't steal EPS thumbnails when it is not installed.
        return BackdropperHasGhostscript();
    }
    return BackdropperHasBuiltInRenderer(extension) || BackdropperWicSupportsExtension(extension);
}
