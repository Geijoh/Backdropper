#include "render.h"

#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

BYTE Blend(BYTE srcPremultiplied, BYTE bg, BYTE alpha)
{
    return static_cast<BYTE>(srcPremultiplied + ((bg * (255 - alpha) + 127) / 255));
}

COLORREF BackgroundAt(const BackdropperSettings& settings, UINT x, UINT y)
{
    if (settings.mode == BackdropMode::Solid) {
        return settings.solidColor;
    }

    const UINT size = std::max(1u, settings.checkerSize);
    return (((x / size) + (y / size)) % 2 == 0) ? settings.checkerA : settings.checkerB;
}

HRESULT CreateWicFactory(IWICImagingFactory** factory)
{
    return CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(factory));
}

}

HRESULT RenderBackdropperThumbnail(IStream* stream, UINT maxSize, const BackdropperSettings& settings,
    HBITMAP* bitmap, WTS_ALPHATYPE* alphaType)
{
    if (!stream || !bitmap || !alphaType || maxSize == 0) {
        return E_INVALIDARG;
    }

    *bitmap = nullptr;
    *alphaType = WTSAT_UNKNOWN;

    LARGE_INTEGER zero = {};
    stream->Seek(zero, STREAM_SEEK_SET, nullptr);

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CreateWicFactory(&factory);
    if (FAILED(hr)) {
        return hr;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) {
        return hr;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) {
        return hr;
    }

    UINT sourceWidth = 0;
    UINT sourceHeight = 0;
    frame->GetSize(&sourceWidth, &sourceHeight);
    if (sourceWidth == 0 || sourceHeight == 0) {
        return E_FAIL;
    }

    const double scale = std::min(1.0, static_cast<double>(maxSize) / static_cast<double>(std::max(sourceWidth, sourceHeight)));
    const UINT width = std::max(1u, static_cast<UINT>(sourceWidth * scale));
    const UINT height = std::max(1u, static_cast<UINT>(sourceHeight * scale));

    ComPtr<IWICBitmapSource> source = frame;
    ComPtr<IWICBitmapScaler> scaler;
    if (width != sourceWidth || height != sourceHeight) {
        hr = factory->CreateBitmapScaler(&scaler);
        if (FAILED(hr)) {
            return hr;
        }
        hr = scaler->Initialize(frame.Get(), width, height, WICBitmapInterpolationModeFant);
        if (FAILED(hr)) {
            return hr;
        }
        source = scaler;
    }

    ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr)) {
        return hr;
    }

    hr = converter->Initialize(source.Get(), GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        return hr;
    }

    const UINT stride = width * 4;
    std::vector<BYTE> pixels(stride * height);
    hr = converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data());
    if (FAILED(hr)) {
        return hr;
    }

    if (settings.mode != BackdropMode::None) {
        for (UINT y = 0; y < height; ++y) {
            for (UINT x = 0; x < width; ++x) {
                BYTE* pixel = pixels.data() + (y * stride) + (x * 4);
                const BYTE alpha = pixel[3];
                const COLORREF bg = BackgroundAt(settings, x, y);
                pixel[0] = Blend(pixel[0], GetBValue(bg), alpha);
                pixel[1] = Blend(pixel[1], GetGValue(bg), alpha);
                pixel[2] = Blend(pixel[2], GetRValue(bg), alpha);
                pixel[3] = 255;
            }
        }
        *alphaType = WTSAT_RGB;
    } else {
        *alphaType = WTSAT_ARGB;
    }

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = static_cast<LONG>(width);
    info.bmiHeader.biHeight = -static_cast<LONG>(height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    memcpy(bits, pixels.data(), pixels.size());
    *bitmap = dib;
    return S_OK;
}
