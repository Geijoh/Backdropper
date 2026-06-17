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

int Fail(const char* message, HRESULT hr = E_FAIL)
{
    std::printf("FAIL: %s (0x%08lx)\n", message, static_cast<unsigned long>(hr));
    return 1;
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

    HBITMAP bitmap = nullptr;
    WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
    hr = RenderBackdropperThumbnail(stream.Get(), 32, settings, &bitmap, &alpha);
    if (FAILED(hr)) {
        CoUninitialize();
        return Fail("RenderBackdropperThumbnail", hr);
    }

    DIBSECTION section = {};
    if (!GetObjectW(bitmap, sizeof(section), &section) || !section.dsBm.bmBits) {
        DeleteObject(bitmap);
        CoUninitialize();
        return Fail("GetObject");
    }

    const BYTE* bits = static_cast<const BYTE*>(section.dsBm.bmBits);
    const bool transparentBecameBackground = bits[0] == 3 && bits[1] == 2 && bits[2] == 1 && bits[3] == 255;
    DeleteObject(bitmap);

    BYTE junk[] = { 1, 2, 3, 4 };
    ComPtr<IStream> bad;
    bad.Attach(SHCreateMemStream(junk, sizeof(junk)));
    hr = RenderBackdropperThumbnail(bad.Get(), 32, settings, &bitmap, &alpha);
    if (SUCCEEDED(hr)) {
        DeleteObject(bitmap);
        CoUninitialize();
        return Fail("corrupt input unexpectedly rendered");
    }

    CoUninitialize();
    if (!transparentBecameBackground) {
        return Fail("transparent pixel was not composited");
    }

    std::puts("OK: PNG transparency composited, corrupt input rejected.");
    return 0;
}
