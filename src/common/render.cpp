#include "render.h"

#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

#define RETURN_IF_FAILED(expr) do { HRESULT _hr = (expr); if (FAILED(_hr)) return _hr; } while (0)

constexpr size_t kMaxInputBytes = 256ull * 1024ull * 1024ull;
constexpr size_t kMaxDecodedBytes = 256ull * 1024ull * 1024ull;

struct DecodedImage {
    UINT width = 0;
    UINT height = 0;
    std::vector<BYTE> premultipliedBgra;
};

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

BYTE Premultiply(BYTE value, BYTE alpha)
{
    return static_cast<BYTE>((static_cast<UINT>(value) * alpha + 127) / 255);
}

bool CheckedImageBytes(UINT width, UINT height, size_t* bytes)
{
    if (width == 0 || height == 0) {
        return false;
    }
    const uint64_t total = static_cast<uint64_t>(width) * height * 4;
    if (total > kMaxDecodedBytes || total > std::numeric_limits<size_t>::max()) {
        return false;
    }
    *bytes = static_cast<size_t>(total);
    return true;
}

uint16_t ReadLe16(const BYTE* data)
{
    return static_cast<uint16_t>(data[0] | (data[1] << 8));
}

uint16_t ReadBe16(const BYTE* data)
{
    return static_cast<uint16_t>((data[0] << 8) | data[1]);
}

uint32_t ReadBe32(const BYTE* data)
{
    return (static_cast<uint32_t>(data[0]) << 24)
        | (static_cast<uint32_t>(data[1]) << 16)
        | (static_cast<uint32_t>(data[2]) << 8)
        | static_cast<uint32_t>(data[3]);
}

HRESULT ReadStreamBytes(IStream* stream, std::vector<BYTE>* bytes)
{
    bytes->clear();
    LARGE_INTEGER zero = {};
    RETURN_IF_FAILED(stream->Seek(zero, STREAM_SEEK_SET, nullptr));

    STATSTG stat = {};
    if (SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME)) && stat.cbSize.QuadPart > 0) {
        if (static_cast<ULONGLONG>(stat.cbSize.QuadPart) > kMaxInputBytes) {
            return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        }
        bytes->reserve(static_cast<size_t>(stat.cbSize.QuadPart));
    }

    BYTE chunk[64 * 1024] = {};
    for (;;) {
        ULONG read = 0;
        HRESULT hr = stream->Read(chunk, sizeof(chunk), &read);
        if (FAILED(hr)) {
            return hr;
        }
        if (read == 0) {
            break;
        }
        if (bytes->size() + read > kMaxInputBytes) {
            return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        }
        bytes->insert(bytes->end(), chunk, chunk + read);
    }
    return bytes->empty() ? E_FAIL : S_OK;
}

HRESULT DecodeTga(const std::vector<BYTE>& bytes, DecodedImage* image)
{
    if (bytes.size() < 18) {
        return E_FAIL;
    }

    const BYTE idLength = bytes[0];
    const BYTE colorMapType = bytes[1];
    const BYTE imageType = bytes[2];
    const UINT width = ReadLe16(bytes.data() + 12);
    const UINT height = ReadLe16(bytes.data() + 14);
    const BYTE bitsPerPixel = bytes[16];
    const BYTE descriptor = bytes[17];
    const bool trueColor = imageType == 2 || imageType == 10;
    const bool grayscale = imageType == 3 || imageType == 11;
    const bool rle = imageType == 10 || imageType == 11;

    size_t outBytes = 0;
    if (colorMapType != 0 || (!trueColor && !grayscale) || !CheckedImageBytes(width, height, &outBytes)) {
        return E_FAIL;
    }
    const UINT sourceBytesPerPixel = grayscale ? 1u : bitsPerPixel / 8u;
    if ((!grayscale && sourceBytesPerPixel != 3 && sourceBytesPerPixel != 4) || (grayscale && bitsPerPixel != 8)) {
        return E_FAIL;
    }

    size_t pos = 18 + idLength;
    if (pos > bytes.size()) {
        return E_FAIL;
    }

    image->width = width;
    image->height = height;
    image->premultipliedBgra.assign(outBytes, 0);

    const uint64_t pixels = static_cast<uint64_t>(width) * height;
    uint64_t pixelIndex = 0;
    auto writePixel = [&](const BYTE* pixel) -> bool {
        if (pixelIndex >= pixels) {
            return false;
        }
        const UINT sourceY = static_cast<UINT>(pixelIndex / width);
        const UINT x = static_cast<UINT>(pixelIndex % width);
        const UINT y = (descriptor & 0x20) ? sourceY : (height - 1 - sourceY);
        BYTE* out = image->premultipliedBgra.data() + ((static_cast<size_t>(y) * width + x) * 4);
        const BYTE alpha = (trueColor && sourceBytesPerPixel == 4) ? pixel[3] : 255;
        const BYTE blue = grayscale ? pixel[0] : pixel[0];
        const BYTE green = grayscale ? pixel[0] : pixel[1];
        const BYTE red = grayscale ? pixel[0] : pixel[2];
        out[0] = Premultiply(blue, alpha);
        out[1] = Premultiply(green, alpha);
        out[2] = Premultiply(red, alpha);
        out[3] = alpha;
        ++pixelIndex;
        return true;
    };

    if (!rle) {
        const uint64_t needed = pixels * sourceBytesPerPixel;
        if (needed > bytes.size() - pos) {
            return E_FAIL;
        }
        while (pixelIndex < pixels) {
            if (!writePixel(bytes.data() + pos)) {
                return E_FAIL;
            }
            pos += sourceBytesPerPixel;
        }
        return S_OK;
    }

    while (pixelIndex < pixels && pos < bytes.size()) {
        const BYTE packet = bytes[pos++];
        const UINT count = (packet & 0x7f) + 1;
        if (packet & 0x80) {
            if (sourceBytesPerPixel > bytes.size() - pos) {
                return E_FAIL;
            }
            for (UINT i = 0; i < count; ++i) {
                if (!writePixel(bytes.data() + pos)) {
                    return E_FAIL;
                }
            }
            pos += sourceBytesPerPixel;
        } else {
            const uint64_t needed = static_cast<uint64_t>(count) * sourceBytesPerPixel;
            if (needed > bytes.size() - pos) {
                return E_FAIL;
            }
            for (UINT i = 0; i < count; ++i) {
                if (!writePixel(bytes.data() + pos)) {
                    return E_FAIL;
                }
                pos += sourceBytesPerPixel;
            }
        }
    }
    return pixelIndex == pixels ? S_OK : E_FAIL;
}

HRESULT DecodePackBitsRow(const BYTE* input, size_t inputSize, BYTE* output, size_t outputSize)
{
    size_t in = 0;
    size_t out = 0;
    while (out < outputSize && in < inputSize) {
        const int8_t n = static_cast<int8_t>(input[in++]);
        if (n >= 0) {
            const size_t count = static_cast<size_t>(n) + 1;
            if (count > inputSize - in || count > outputSize - out) {
                return E_FAIL;
            }
            memcpy(output + out, input + in, count);
            in += count;
            out += count;
        } else if (n != -128) {
            const size_t count = static_cast<size_t>(1 - n);
            if (in >= inputSize || count > outputSize - out) {
                return E_FAIL;
            }
            memset(output + out, input[in++], count);
            out += count;
        }
    }
    return out == outputSize ? S_OK : E_FAIL;
}

HRESULT DecodePsd(const std::vector<BYTE>& bytes, DecodedImage* image)
{
    if (bytes.size() < 28 || memcmp(bytes.data(), "8BPS", 4) != 0 || ReadBe16(bytes.data() + 4) != 1) {
        return E_FAIL;
    }

    const UINT channels = ReadBe16(bytes.data() + 12);
    const UINT height = ReadBe32(bytes.data() + 14);
    const UINT width = ReadBe32(bytes.data() + 18);
    const UINT depth = ReadBe16(bytes.data() + 22);
    const UINT colorMode = ReadBe16(bytes.data() + 24);
    const bool grayscale = colorMode == 1;
    const bool rgb = colorMode == 3;

    size_t outBytes = 0;
    if (channels == 0 || channels > 8 || depth != 8 || (!grayscale && !rgb)
        || (rgb && channels < 3) || !CheckedImageBytes(width, height, &outBytes)) {
        return E_FAIL;
    }

    size_t pos = 26;
    for (int block = 0; block < 3; ++block) {
        if (bytes.size() - pos < 4) {
            return E_FAIL;
        }
        const uint32_t length = ReadBe32(bytes.data() + pos);
        pos += 4;
        if (length > bytes.size() - pos) {
            return E_FAIL;
        }
        pos += length;
    }
    if (bytes.size() - pos < 2) {
        return E_FAIL;
    }
    const UINT compression = ReadBe16(bytes.data() + pos);
    pos += 2;

    const size_t pixels = static_cast<size_t>(width) * height;
    if (pixels > kMaxDecodedBytes || channels > kMaxDecodedBytes / pixels) {
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    }
    std::vector<BYTE> channelData(pixels * channels);

    if (compression == 0) {
        const size_t needed = channelData.size();
        if (needed > bytes.size() - pos) {
            return E_FAIL;
        }
        memcpy(channelData.data(), bytes.data() + pos, needed);
    } else if (compression == 1) {
        const size_t rowCount = static_cast<size_t>(channels) * height;
        if (rowCount > (bytes.size() - pos) / 2) {
            return E_FAIL;
        }
        std::vector<uint16_t> rowBytes(rowCount);
        for (size_t i = 0; i < rowCount; ++i) {
            rowBytes[i] = ReadBe16(bytes.data() + pos);
            pos += 2;
        }
        for (UINT channel = 0; channel < channels; ++channel) {
            for (UINT y = 0; y < height; ++y) {
                const size_t row = static_cast<size_t>(channel) * height + y;
                const size_t compressed = rowBytes[row];
                if (compressed > bytes.size() - pos) {
                    return E_FAIL;
                }
                BYTE* output = channelData.data() + (static_cast<size_t>(channel) * pixels) + (static_cast<size_t>(y) * width);
                RETURN_IF_FAILED(DecodePackBitsRow(bytes.data() + pos, compressed, output, width));
                pos += compressed;
            }
        }
    } else {
        return E_FAIL;
    }

    image->width = width;
    image->height = height;
    image->premultipliedBgra.assign(outBytes, 0);
    const BYTE* c0 = channelData.data();
    const BYTE* c1 = channels > 1 ? c0 + pixels : nullptr;
    const BYTE* c2 = channels > 2 ? c1 + pixels : nullptr;
    const BYTE* alphaChannel = grayscale ? (channels > 1 ? c1 : nullptr) : (channels > 3 ? c2 + pixels : nullptr);

    for (size_t i = 0; i < pixels; ++i) {
        const BYTE alpha = alphaChannel ? alphaChannel[i] : 255;
        const BYTE red = grayscale ? c0[i] : c0[i];
        const BYTE green = grayscale ? c0[i] : c1[i];
        const BYTE blue = grayscale ? c0[i] : c2[i];
        BYTE* out = image->premultipliedBgra.data() + (i * 4);
        out[0] = Premultiply(blue, alpha);
        out[1] = Premultiply(green, alpha);
        out[2] = Premultiply(red, alpha);
        out[3] = alpha;
    }
    return S_OK;
}

HRESULT DecodeFallbackImage(IStream* stream, DecodedImage* image)
{
    std::vector<BYTE> bytes;
    RETURN_IF_FAILED(ReadStreamBytes(stream, &bytes));
    if (SUCCEEDED(DecodeTga(bytes, image))) {
        return S_OK;
    }
    return DecodePsd(bytes, image);
}

HRESULT RenderWicSource(IWICImagingFactory* factory, IWICBitmapSource* source, UINT sourceWidth, UINT sourceHeight,
    UINT maxSize, const BackdropperSettings& settings, HBITMAP* bitmap, WTS_ALPHATYPE* alphaType)
{
    if (sourceWidth == 0 || sourceHeight == 0) {
        return E_FAIL;
    }

    const double scale = std::min(1.0, static_cast<double>(maxSize) / static_cast<double>(std::max(sourceWidth, sourceHeight)));
    const UINT width = std::max(1u, static_cast<UINT>(sourceWidth * scale));
    const UINT height = std::max(1u, static_cast<UINT>(sourceHeight * scale));

    ComPtr<IWICBitmapSource> scaledSource = source;
    ComPtr<IWICBitmapScaler> scaler;
    if (width != sourceWidth || height != sourceHeight) {
        HRESULT hr = factory->CreateBitmapScaler(&scaler);
        if (FAILED(hr)) {
            return hr;
        }
        hr = scaler->Initialize(source, width, height, WICBitmapInterpolationModeFant);
        if (FAILED(hr)) {
            return hr;
        }
        scaledSource = scaler;
    }

    ComPtr<IWICFormatConverter> converter;
    HRESULT hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr)) {
        return hr;
    }

    hr = converter->Initialize(scaledSource.Get(), GUID_WICPixelFormat32bppPBGRA,
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
    if (SUCCEEDED(hr)) {
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
        return RenderWicSource(factory.Get(), frame.Get(), sourceWidth, sourceHeight, maxSize, settings, bitmap, alphaType);
    }

    DecodedImage decoded;
    RETURN_IF_FAILED(DecodeFallbackImage(stream, &decoded));
    ComPtr<IWICBitmap> fallbackSource;
    const UINT stride = decoded.width * 4;
    hr = factory->CreateBitmapFromMemory(decoded.width, decoded.height, GUID_WICPixelFormat32bppPBGRA,
        stride, static_cast<UINT>(decoded.premultipliedBgra.size()), decoded.premultipliedBgra.data(), &fallbackSource);
    if (FAILED(hr)) {
        return hr;
    }
    return RenderWicSource(factory.Get(), fallbackSource.Get(), decoded.width, decoded.height, maxSize, settings, bitmap, alphaType);
}
