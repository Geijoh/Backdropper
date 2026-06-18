#include "render.h"

#include <d2d1_3.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <roapi.h>
#include <shcore.h>
#include <shlwapi.h>
#include <winrt/base.h>
#include <winrt/Windows.Data.Pdf.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.UI.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
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

ComPtr<IStream> StreamFromBytes(const std::vector<BYTE>& bytes)
{
    ComPtr<IStream> stream;
    stream.Attach(SHCreateMemStream(bytes.data(), static_cast<UINT>(bytes.size())));
    return stream;
}

HRESULT DecodeWicStream(IWICImagingFactory* factory, IStream* stream, DecodedImage* image)
{
    LARGE_INTEGER zero = {};
    RETURN_IF_FAILED(stream->Seek(zero, STREAM_SEEK_SET, nullptr));

    ComPtr<IWICBitmapDecoder> decoder;
    RETURN_IF_FAILED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder));

    ComPtr<IWICBitmapFrameDecode> frame;
    RETURN_IF_FAILED(decoder->GetFrame(0, &frame));

    UINT width = 0;
    UINT height = 0;
    frame->GetSize(&width, &height);
    size_t outBytes = 0;
    if (!CheckedImageBytes(width, height, &outBytes)) {
        return E_FAIL;
    }

    ComPtr<IWICFormatConverter> converter;
    RETURN_IF_FAILED(factory->CreateFormatConverter(&converter));
    RETURN_IF_FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom));

    image->width = width;
    image->height = height;
    image->premultipliedBgra.resize(outBytes);
    return converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(outBytes), image->premultipliedBgra.data());
}

HRESULT DecodeWicBytes(IWICImagingFactory* factory, const std::vector<BYTE>& bytes, DecodedImage* image)
{
    ComPtr<IStream> stream = StreamFromBytes(bytes);
    return stream ? DecodeWicStream(factory, stream.Get(), image) : E_OUTOFMEMORY;
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

template <typename Draw>
HRESULT RenderD2D(UINT width, UINT height, Draw draw, DecodedImage* image)
{
    size_t outBytes = 0;
    if (!CheckedImageBytes(width, height, &outBytes)) {
        return E_FAIL;
    }

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };
    ComPtr<ID3D11Device> d3dDevice;
    ComPtr<ID3D11DeviceContext> d3dContext;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &d3dDevice, nullptr, &d3dContext);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
            levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &d3dDevice, nullptr, &d3dContext);
    }
    if (FAILED(hr)) {
        return hr;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    RETURN_IF_FAILED(d3dDevice.As(&dxgiDevice));

    ComPtr<ID2D1Factory1> d2dFactory;
    RETURN_IF_FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&d2dFactory)));

    ComPtr<ID2D1Device> d2dDevice;
    RETURN_IF_FAILED(d2dFactory->CreateDevice(dxgiDevice.Get(), &d2dDevice));

    ComPtr<ID2D1DeviceContext> baseContext;
    RETURN_IF_FAILED(d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &baseContext));

    ComPtr<ID2D1DeviceContext5> context;
    RETURN_IF_FAILED(baseContext.As(&context));

    D3D11_TEXTURE2D_DESC textureDesc = {};
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    ComPtr<ID3D11Texture2D> texture;
    RETURN_IF_FAILED(d3dDevice->CreateTexture2D(&textureDesc, nullptr, &texture));

    ComPtr<IDXGISurface> surface;
    RETURN_IF_FAILED(texture.As(&surface));

    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    ComPtr<ID2D1Bitmap1> target;
    RETURN_IF_FAILED(context->CreateBitmapFromDxgiSurface(surface.Get(), &props, &target));

    context->SetTarget(target.Get());
    context->BeginDraw();
    context->Clear(D2D1::ColorF(0, 0.0f));
    hr = draw(context.Get());
    HRESULT endHr = context->EndDraw();
    if (FAILED(hr)) {
        return hr;
    }
    if (FAILED(endHr)) {
        return endHr;
    }

    D3D11_TEXTURE2D_DESC stagingDesc = textureDesc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    RETURN_IF_FAILED(d3dDevice->CreateTexture2D(&stagingDesc, nullptr, &staging));
    d3dContext->CopyResource(staging.Get(), texture.Get());

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    RETURN_IF_FAILED(d3dContext->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));

    image->width = width;
    image->height = height;
    image->premultipliedBgra.resize(outBytes);
    const BYTE* src = static_cast<const BYTE*>(mapped.pData);
    BYTE* dst = image->premultipliedBgra.data();
    const UINT stride = width * 4;
    for (UINT y = 0; y < height; ++y) {
        memcpy(dst + (static_cast<size_t>(y) * stride), src + (static_cast<size_t>(y) * mapped.RowPitch), stride);
    }
    d3dContext->Unmap(staging.Get(), 0);
    return S_OK;
}

HRESULT DecodeSvg(const std::vector<BYTE>& bytes, UINT maxSize, DecodedImage* image)
{
    ComPtr<IStream> stream = StreamFromBytes(bytes);
    if (!stream) {
        return E_OUTOFMEMORY;
    }

    const UINT size = std::max(1u, maxSize);
    return RenderD2D(size, size, [&](ID2D1DeviceContext5* context) -> HRESULT {
        ComPtr<ID2D1SvgDocument> document;
        RETURN_IF_FAILED(context->CreateSvgDocument(stream.Get(), D2D1::SizeF(static_cast<float>(size), static_cast<float>(size)), &document));
        context->DrawSvgDocument(document.Get());
        return S_OK;
    }, image);
}

class RoInitializeScope {
public:
    RoInitializeScope()
        : hr_(RoInitialize(RO_INIT_SINGLETHREADED))
        , uninitialize_(hr_ == S_OK || hr_ == S_FALSE)
    {
        if (hr_ == RPC_E_CHANGED_MODE) {
            hr_ = S_OK;
        }
    }

    ~RoInitializeScope()
    {
        if (uninitialize_) {
            RoUninitialize();
        }
    }

    HRESULT Result() const { return hr_; }

private:
    HRESULT hr_;
    bool uninitialize_;
};

HRESULT DecodePdf(IWICImagingFactory* factory, const std::vector<BYTE>& bytes, UINT maxSize, DecodedImage* image)
{
    try {
        RoInitializeScope ro;
        RETURN_IF_FAILED(ro.Result());

        ComPtr<IStream> baseStream = StreamFromBytes(bytes);
        if (!baseStream) {
            return E_OUTOFMEMORY;
        }
        auto inputStream = winrt::capture<winrt::Windows::Storage::Streams::IRandomAccessStream>(
            CreateRandomAccessStreamOverStream, baseStream.Get(), BSOS_DEFAULT);
        auto document = winrt::Windows::Data::Pdf::PdfDocument::LoadFromStreamAsync(inputStream).get();
        if (!document || document.PageCount() == 0) {
            return E_FAIL;
        }

        auto page = document.GetPage(0);
        const auto size = page.Size();
        const double pageWidth = std::max(1.0, static_cast<double>(size.Width));
        const double pageHeight = std::max(1.0, static_cast<double>(size.Height));
        const double scale = static_cast<double>(std::max(1u, maxSize)) / std::max(pageWidth, pageHeight);
        const uint32_t width = std::max(1u, static_cast<uint32_t>(pageWidth * scale));
        const uint32_t height = std::max(1u, static_cast<uint32_t>(pageHeight * scale));

        winrt::Windows::Data::Pdf::PdfPageRenderOptions options;
        options.DestinationWidth(width);
        options.DestinationHeight(height);
        options.BitmapEncoderId(winrt::Windows::Graphics::Imaging::BitmapEncoder::PngEncoderId());
        options.BackgroundColor(winrt::Windows::UI::Color{ 0, 255, 255, 255 });
        options.IsIgnoringHighContrast(true);

        winrt::Windows::Storage::Streams::InMemoryRandomAccessStream output;
        page.RenderToStreamAsync(output, options).get();
        const uint64_t renderedSize = output.Size();
        if (renderedSize == 0 || renderedSize > kMaxInputBytes) {
            return E_FAIL;
        }

        std::vector<BYTE> rendered(static_cast<size_t>(renderedSize));
        auto reader = winrt::Windows::Storage::Streams::DataReader(output.GetInputStreamAt(0));
        reader.LoadAsync(static_cast<uint32_t>(rendered.size())).get();
        reader.ReadBytes(rendered);
        reader.Close();
        page.Close();
        output.Close();
        return DecodeWicBytes(factory, rendered, image);
    } catch (const winrt::hresult_error& error) {
        return error.code();
    } catch (...) {
        return E_FAIL;
    }
}

bool LooksPostScript(const std::vector<BYTE>& bytes)
{
    if (bytes.size() >= 4 && memcmp(bytes.data(), "%!PS", 4) == 0) {
        return true;
    }
    return bytes.size() >= 4 && bytes[0] == 0xC5 && bytes[1] == 0xD0 && bytes[2] == 0xD3 && bytes[3] == 0xC6;
}

bool LooksPdf(const std::vector<BYTE>& bytes)
{
    return bytes.size() >= 4 && memcmp(bytes.data(), "%PDF", 4) == 0;
}

bool LooksSvg(const std::vector<BYTE>& bytes)
{
    const size_t limit = std::min<size_t>(bytes.size(), 1024);
    std::string head;
    head.reserve(limit);
    for (size_t i = 0; i < limit; ++i) {
        const char ch = static_cast<char>(bytes[i]);
        head.push_back(ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch + 32) : ch);
    }
    return head.find("<svg") != std::string::npos;
}

std::wstring FindGhostscript()
{
    wchar_t path[MAX_PATH] = {};
    if (SearchPathW(nullptr, L"gswin64c.exe", nullptr, ARRAYSIZE(path), path, nullptr)) {
        return path;
    }
    if (SearchPathW(nullptr, L"gswin32c.exe", nullptr, ARRAYSIZE(path), path, nullptr)) {
        return path;
    }

    auto findUnder = [](const wchar_t* base, const wchar_t* exe) -> std::wstring {
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
    };

    for (const auto& candidate : {
        findUnder(L"C:\\Program Files\\gs", L"gswin64c.exe"),
        findUnder(L"C:\\Program Files (x86)\\gs", L"gswin32c.exe") }) {
        if (!candidate.empty()) {
            return candidate;
        }
    }
    return {};
}

std::wstring Quote(const std::wstring& value)
{
    return L"\"" + value + L"\"";
}

HRESULT ReadFileBytes(const std::wstring& path, std::vector<BYTE>* bytes)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || static_cast<ULONGLONG>(size.QuadPart) > kMaxInputBytes) {
        CloseHandle(file);
        return E_FAIL;
    }
    bytes->resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    const BOOL ok = ReadFile(file, bytes->data(), static_cast<DWORD>(bytes->size()), &read, nullptr);
    CloseHandle(file);
    return ok && read == bytes->size() ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}

HRESULT WriteFileBytes(const std::wstring& path, const std::vector<BYTE>& bytes)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(file);
    return ok && written == bytes.size() ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}

HRESULT DecodePostScriptWithGhostscript(IWICImagingFactory* factory, const std::vector<BYTE>& bytes, UINT maxSize, DecodedImage* image)
{
    if (!LooksPostScript(bytes)) {
        return E_FAIL;
    }

    const std::wstring gs = FindGhostscript();
    if (gs.empty()) {
        return E_FAIL;
    }

    wchar_t tempDir[MAX_PATH] = {};
    if (!GetTempPathW(ARRAYSIZE(tempDir), tempDir)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    wchar_t tempFile[MAX_PATH] = {};
    if (!GetTempFileNameW(tempDir, L"bdp", 0, tempFile)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    const std::wstring input = std::wstring(tempFile) + L".eps";
    const std::wstring output = std::wstring(tempFile) + L".png";
    MoveFileW(tempFile, input.c_str());

    HRESULT hr = WriteFileBytes(input, bytes);
    if (SUCCEEDED(hr)) {
        const UINT size = std::max(1u, maxSize);
        std::wstring command = Quote(gs)
            + L" -dSAFER -dBATCH -dNOPAUSE -dNOPROMPT -dQUIET -dFirstPage=1 -dLastPage=1 -dEPSCrop"
            + L" -sDEVICE=pngalpha -dGraphicsAlphaBits=4 -dTextAlphaBits=4"
            + L" -g" + std::to_wstring(size) + L"x" + std::to_wstring(size)
            + L" -sOutputFile=" + Quote(output) + L" " + Quote(input);

        STARTUPINFOW startup = {};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process = {};
        if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
            const DWORD wait = WaitForSingleObject(process.hProcess, 10000);
            DWORD exitCode = 1;
            GetExitCodeProcess(process.hProcess, &exitCode);
            if (wait == WAIT_TIMEOUT) {
                TerminateProcess(process.hProcess, 1);
                hr = HRESULT_FROM_WIN32(WAIT_TIMEOUT);
            } else if (wait != WAIT_OBJECT_0 || exitCode != 0) {
                hr = E_FAIL;
            }
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        } else {
            hr = HRESULT_FROM_WIN32(GetLastError());
        }
    }

    if (SUCCEEDED(hr)) {
        std::vector<BYTE> png;
        hr = ReadFileBytes(output, &png);
        if (SUCCEEDED(hr)) {
            hr = DecodeWicBytes(factory, png, image);
        }
    }

    DeleteFileW(input.c_str());
    DeleteFileW(output.c_str());
    return hr;
}

HRESULT DecodeFallbackImage(IWICImagingFactory* factory, IStream* stream, UINT maxSize, DecodedImage* image)
{
    std::vector<BYTE> bytes;
    RETURN_IF_FAILED(ReadStreamBytes(stream, &bytes));
    if (SUCCEEDED(DecodeTga(bytes, image))) {
        return S_OK;
    }
    if (SUCCEEDED(DecodePsd(bytes, image))) {
        return S_OK;
    }
    if (LooksSvg(bytes) && SUCCEEDED(DecodeSvg(bytes, maxSize, image))) {
        return S_OK;
    }
    if (LooksPdf(bytes) && SUCCEEDED(DecodePdf(factory, bytes, maxSize, image))) {
        return S_OK;
    }
    return DecodePostScriptWithGhostscript(factory, bytes, maxSize, image);
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
    RETURN_IF_FAILED(DecodeFallbackImage(factory.Get(), stream, maxSize, &decoded));
    ComPtr<IWICBitmap> fallbackSource;
    const UINT stride = decoded.width * 4;
    hr = factory->CreateBitmapFromMemory(decoded.width, decoded.height, GUID_WICPixelFormat32bppPBGRA,
        stride, static_cast<UINT>(decoded.premultipliedBgra.size()), decoded.premultipliedBgra.data(), &fallbackSource);
    if (FAILED(hr)) {
        return hr;
    }
    return RenderWicSource(factory.Get(), fallbackSource.Get(), decoded.width, decoded.height, maxSize, settings, bitmap, alphaType);
}
