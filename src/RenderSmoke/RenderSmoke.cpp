#include "render.h"

#include <shlwapi.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdio>

using Microsoft::WRL::ComPtr;

namespace {

HRESULT CreatePngStream(IStream** stream)
{
    *stream = SHCreateMemStream(nullptr, 0);
    if (!*stream) {
        return E_OUTOFMEMORY;
    }

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        return hr;
    }

    ComPtr<IWICBitmapEncoder> encoder;
    hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (FAILED(hr)) {
        return hr;
    }

    hr = encoder->Initialize(*stream, WICBitmapEncoderNoCache);
    if (FAILED(hr)) {
        return hr;
    }

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> bag;
    hr = encoder->CreateNewFrame(&frame, &bag);
    if (FAILED(hr)) {
        return hr;
    }

    hr = frame->Initialize(bag.Get());
    if (FAILED(hr)) {
        return hr;
    }

    hr = frame->SetSize(2, 2);
    if (FAILED(hr)) {
        return hr;
    }

    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    hr = frame->SetPixelFormat(&format);
    if (FAILED(hr)) {
        return hr;
    }

    BYTE pixels[] = {
        0, 0, 0, 0,      0, 0, 255, 255,
        0, 255, 0, 255,  255, 0, 0, 255
    };
    hr = frame->WritePixels(2, 8, sizeof(pixels), pixels);
    if (FAILED(hr)) {
        return hr;
    }

    hr = frame->Commit();
    if (FAILED(hr)) {
        return hr;
    }

    hr = encoder->Commit();
    LARGE_INTEGER zero = {};
    (*stream)->Seek(zero, STREAM_SEEK_SET, nullptr);
    return hr;
}

HRESULT CreateMemoryStream(const BYTE* data, UINT size, IStream** stream)
{
    *stream = SHCreateMemStream(data, size);
    return *stream ? S_OK : E_OUTOFMEMORY;
}

HRESULT CreateTgaStream(IStream** stream)
{
    static constexpr BYTE data[] = {
        0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 32, 0x28,
        0, 0, 0, 0,       0, 0, 255, 255,
        0, 255, 0, 255,   255, 0, 0, 255
    };
    return CreateMemoryStream(data, sizeof(data), stream);
}

HRESULT CreatePsdStream(IStream** stream)
{
    static constexpr BYTE data[] = {
        '8', 'B', 'P', 'S', 0, 1, 0, 0, 0, 0, 0, 0,
        0, 4, 0, 0, 0, 1, 0, 0, 0, 1, 0, 8, 0, 3,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 255, 0, 0, 0
    };
    return CreateMemoryStream(data, sizeof(data), stream);
}

int Fail(const char* message, HRESULT hr = E_FAIL)
{
    std::printf("FAIL: %s (0x%08lx)\n", message, static_cast<unsigned long>(hr));
    return 1;
}

bool TransparentPixelBecameBackground(IStream* stream, const BackdropperSettings& settings)
{
    HBITMAP bitmap = nullptr;
    WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
    HRESULT hr = RenderBackdropperThumbnail(stream, 32, settings, &bitmap, &alpha);
    if (FAILED(hr)) {
        std::printf("FAIL: RenderBackdropperThumbnail (0x%08lx)\n", static_cast<unsigned long>(hr));
        return false;
    }

    DIBSECTION section = {};
    if (!GetObjectW(bitmap, sizeof(section), &section) || !section.dsBm.bmBits) {
        DeleteObject(bitmap);
        std::puts("FAIL: GetObject");
        return false;
    }

    const BYTE* bits = static_cast<const BYTE*>(section.dsBm.bmBits);
    const bool ok = bits[0] == 3 && bits[1] == 2 && bits[2] == 1 && bits[3] == 255;
    DeleteObject(bitmap);
    return ok;
}

}

int main()
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        return Fail("CoInitializeEx", hr);
    }

    ComPtr<IStream> stream;
    hr = CreatePngStream(&stream);
    if (FAILED(hr)) {
        CoUninitialize();
        return Fail("CreatePngStream", hr);
    }

    BackdropperSettings settings;
    settings.mode = BackdropMode::Checker;
    settings.checkerA = RGB(1, 2, 3);
    settings.checkerB = RGB(4, 5, 6);
    settings.checkerSize = 1;

    const bool pngTransparentBecameBackground = TransparentPixelBecameBackground(stream.Get(), settings);

    ComPtr<IStream> tga;
    hr = CreateTgaStream(&tga);
    if (FAILED(hr)) {
        CoUninitialize();
        return Fail("CreateTgaStream", hr);
    }
    const bool tgaTransparentBecameBackground = TransparentPixelBecameBackground(tga.Get(), settings);

    ComPtr<IStream> psd;
    hr = CreatePsdStream(&psd);
    if (FAILED(hr)) {
        CoUninitialize();
        return Fail("CreatePsdStream", hr);
    }
    const bool psdTransparentBecameBackground = TransparentPixelBecameBackground(psd.Get(), settings);

    BYTE junk[] = { 1, 2, 3, 4 };
    ComPtr<IStream> bad;
    bad.Attach(SHCreateMemStream(junk, sizeof(junk)));
    HBITMAP bitmap = nullptr;
    WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
    hr = RenderBackdropperThumbnail(bad.Get(), 32, settings, &bitmap, &alpha);
    if (SUCCEEDED(hr)) {
        DeleteObject(bitmap);
        CoUninitialize();
        return Fail("corrupt input unexpectedly rendered");
    }

    CoUninitialize();
    if (!pngTransparentBecameBackground || !tgaTransparentBecameBackground || !psdTransparentBecameBackground) {
        return Fail("transparent pixel was not composited");
    }

    std::puts("OK: PNG/TGA/PSD transparency composited, corrupt input rejected.");
    return 0;
}
