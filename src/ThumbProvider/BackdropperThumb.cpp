#include "render.h"
#include "thumbnail_cache.h"

#include <shlobj.h>
#include <shlwapi.h>
#include <strsafe.h>
#include <thumbcache.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kClsidString[] = L"{7F08B58C-8D1C-44D3-9A73-AB554FF53B1D}";
constexpr wchar_t kThumbHandlerKey[] = L"{E357FCCD-A995-4576-B01F-234630154E96}";
constexpr wchar_t kExtension[] = L".png";

const CLSID CLSID_BackdropperThumb = {
    0x7f08b58c, 0x8d1c, 0x44d3, {0x9a, 0x73, 0xab, 0x55, 0x4f, 0xf5, 0x3b, 0x1d}
};

HINSTANCE g_instance = nullptr;
long g_dllRefs = 0;

HRESULT HresultFromWin32(LSTATUS status)
{
    return status == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(status);
}

std::wstring ModulePath()
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(g_instance, path, ARRAYSIZE(path));
    return path;
}

std::wstring ExtensionHandlerPath()
{
    return std::wstring(L"Software\\Classes\\") + kExtension + L"\\shellex\\" + kThumbHandlerKey;
}

std::wstring BackupPath()
{
    return std::wstring(L"Software\\Backdropper\\Backup\\") + kExtension;
}

HRESULT SetStringValue(HKEY root, const std::wstring& path, const wchar_t* name, const std::wstring& value)
{
    HKEY key = nullptr;
    const LSTATUS status = RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr);
    if (status != ERROR_SUCCESS) {
        return HRESULT_FROM_WIN32(status);
    }

    const LSTATUS setStatus = RegSetValueExW(key, name, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()),
        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return HresultFromWin32(setStatus);
}

bool ReadStringValue(HKEY root, const std::wstring& path, const wchar_t* name, std::wstring* value)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, path.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }

    wchar_t buffer[512] = {};
    DWORD bytes = sizeof(buffer);
    const LSTATUS status = RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, buffer, &bytes);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        return false;
    }

    *value = buffer;
    return true;
}

void SavePreviousHandler()
{
    HKEY backup = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, BackupPath().c_str(), 0, KEY_READ, &backup) == ERROR_SUCCESS) {
        RegCloseKey(backup);
        return;
    }

    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, BackupPath().c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return;
    }

    std::wstring previous;
    const DWORD hadValue = ReadStringValue(HKEY_CURRENT_USER, ExtensionHandlerPath(), nullptr, &previous) ? 1 : 0;
    RegSetValueExW(key, L"HadValue", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&hadValue), sizeof(hadValue));
    if (hadValue) {
        RegSetValueExW(key, L"Value", 0, REG_SZ, reinterpret_cast<const BYTE*>(previous.c_str()),
            static_cast<DWORD>((previous.size() + 1) * sizeof(wchar_t)));
    }
    RegCloseKey(key);
}

void RestorePreviousHandler()
{
    DWORD hadValue = 0;
    DWORD bytes = sizeof(hadValue);
    RegGetValueW(HKEY_CURRENT_USER, BackupPath().c_str(), L"HadValue", RRF_RT_REG_DWORD, nullptr, &hadValue, &bytes);

    std::wstring previous;
    if (hadValue && ReadStringValue(HKEY_CURRENT_USER, BackupPath(), L"Value", &previous)) {
        SetStringValue(HKEY_CURRENT_USER, ExtensionHandlerPath(), nullptr, previous);
    } else {
        SHDeleteKeyW(HKEY_CURRENT_USER, ExtensionHandlerPath().c_str());
    }

    SHDeleteKeyW(HKEY_CURRENT_USER, BackupPath().c_str());
}

HRESULT RegisterServer()
{
    const std::wstring clsidPath = std::wstring(L"Software\\Classes\\CLSID\\") + kClsidString;
    const std::wstring inprocPath = clsidPath + L"\\InprocServer32";

    HRESULT hr = SetStringValue(HKEY_CURRENT_USER, clsidPath, nullptr, L"Backdropper PNG Thumbnail Provider");
    if (FAILED(hr)) {
        return hr;
    }

    hr = SetStringValue(HKEY_CURRENT_USER, inprocPath, nullptr, ModulePath());
    if (FAILED(hr)) {
        return hr;
    }

    hr = SetStringValue(HKEY_CURRENT_USER, inprocPath, L"ThreadingModel", L"Apartment");
    if (FAILED(hr)) {
        return hr;
    }

    SavePreviousHandler();
    hr = SetStringValue(HKEY_CURRENT_USER, ExtensionHandlerPath(), nullptr, kClsidString);
    if (SUCCEEDED(hr)) {
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    }
    return hr;
}

HRESULT UnregisterServer()
{
    RestorePreviousHandler();
    const std::wstring clsidPath = std::wstring(L"Software\\Classes\\CLSID\\") + kClsidString;
    SHDeleteKeyW(HKEY_CURRENT_USER, clsidPath.c_str());
    ForceDeleteThumbcacheDbs();
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

class ThumbnailProvider final : public IInitializeWithStream, public IThumbnailProvider {
public:
    ThumbnailProvider() { InterlockedIncrement(&g_dllRefs); }
    ~ThumbnailProvider() { InterlockedDecrement(&g_dllRefs); }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** object) override
    {
        if (!object) {
            return E_POINTER;
        }

        *object = nullptr;
        if (riid == IID_IUnknown || riid == IID_IInitializeWithStream) {
            *object = static_cast<IInitializeWithStream*>(this);
        } else if (riid == IID_IThumbnailProvider) {
            *object = static_cast<IThumbnailProvider*>(this);
        } else {
            return E_NOINTERFACE;
        }

        AddRef();
        return S_OK;
    }

    IFACEMETHODIMP_(ULONG) AddRef() override
    {
        return InterlockedIncrement(&refs_);
    }

    IFACEMETHODIMP_(ULONG) Release() override
    {
        const ULONG refs = InterlockedDecrement(&refs_);
        if (!refs) {
            delete this;
        }
        return refs;
    }

    IFACEMETHODIMP Initialize(IStream* stream, DWORD) override
    {
        if (!stream) {
            return E_INVALIDARG;
        }
        if (stream_) {
            return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        }
        stream_ = stream;
        return S_OK;
    }

    IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP* bitmap, WTS_ALPHATYPE* alphaType) override
    {
        return RenderBackdropperThumbnail(stream_.Get(), cx, LoadBackdropperSettings(), bitmap, alphaType);
    }

private:
    long refs_ = 1;
    ComPtr<IStream> stream_;
};

class ClassFactory final : public IClassFactory {
public:
    ClassFactory() { InterlockedIncrement(&g_dllRefs); }
    ~ClassFactory() { InterlockedDecrement(&g_dllRefs); }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** object) override
    {
        if (!object) {
            return E_POINTER;
        }

        *object = nullptr;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *object = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    IFACEMETHODIMP_(ULONG) AddRef() override
    {
        return InterlockedIncrement(&refs_);
    }

    IFACEMETHODIMP_(ULONG) Release() override
    {
        const ULONG refs = InterlockedDecrement(&refs_);
        if (!refs) {
            delete this;
        }
        return refs;
    }

    IFACEMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** object) override
    {
        if (outer) {
            return CLASS_E_NOAGGREGATION;
        }

        auto provider = new (std::nothrow) ThumbnailProvider();
        if (!provider) {
            return E_OUTOFMEMORY;
        }

        const HRESULT hr = provider->QueryInterface(riid, object);
        provider->Release();
        return hr;
    }

    IFACEMETHODIMP LockServer(BOOL lock) override
    {
        if (lock) {
            InterlockedIncrement(&g_dllRefs);
        } else {
            InterlockedDecrement(&g_dllRefs);
        }
        return S_OK;
    }

private:
    long refs_ = 1;
};

}

STDAPI DllCanUnloadNow()
{
    return g_dllRefs == 0 ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** object)
{
    if (clsid != CLSID_BackdropperThumb) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    auto factory = new (std::nothrow) ClassFactory();
    if (!factory) {
        return E_OUTOFMEMORY;
    }

    const HRESULT hr = factory->QueryInterface(riid, object);
    factory->Release();
    return hr;
}

STDAPI DllRegisterServer()
{
    return RegisterServer();
}

STDAPI DllUnregisterServer()
{
    return UnregisterServer();
}

BOOL APIENTRY DllMain(HINSTANCE instance, DWORD reason, void*)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_instance = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
