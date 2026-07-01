#include "render.h"

#include <shlwapi.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdio>
#include <string>
#include <vector>

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
    *stream = nullptr;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!memory) {
        return E_OUTOFMEMORY;
    }
    void* dest = GlobalLock(memory);
    if (!dest) {
        GlobalFree(memory);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    memcpy(dest, data, size);
    GlobalUnlock(memory);
    HRESULT hr = CreateStreamOnHGlobal(memory, TRUE, stream);
    if (FAILED(hr)) {
        GlobalFree(memory);
    }
    return hr;
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

struct IcoFrameSpec {
    UINT size = 0;
    BYTE bgra[4] = {};
    std::vector<BYTE> pixels;
};

void AppendBytes(std::vector<BYTE>* data, const void* bytes, size_t count)
{
    const BYTE* src = static_cast<const BYTE*>(bytes);
    data->insert(data->end(), src, src + count);
}

// Classic BMP-in-ICO: ICONDIR + one ICONDIRENTRY per frame, each frame a
// BITMAPINFOHEADER with doubled height followed by bottom-up 32bpp XOR pixels
// (frame.pixels if provided, else a solid frame.bgra fill) and a zeroed 1bpp
// AND mask padded to 32-bit rows.
HRESULT CreateIcoStream(const std::vector<IcoFrameSpec>& frames, IStream** stream)
{
    std::vector<BYTE> data;
    const WORD header[] = { 0, 1, static_cast<WORD>(frames.size()) };
    AppendBytes(&data, header, sizeof(header));

    DWORD offset = 6 + 16 * static_cast<DWORD>(frames.size());
    for (const IcoFrameSpec& frame : frames) {
        const DWORD andStride = ((frame.size + 31) / 32) * 4;
        const DWORD bytesInRes = 40 + frame.size * frame.size * 4 + andStride * frame.size;

        const BYTE entry[] = {
            static_cast<BYTE>(frame.size < 256 ? frame.size : 0),
            static_cast<BYTE>(frame.size < 256 ? frame.size : 0),
            0, 0, 1, 0, 32, 0,
            static_cast<BYTE>(bytesInRes), static_cast<BYTE>(bytesInRes >> 8),
            static_cast<BYTE>(bytesInRes >> 16), static_cast<BYTE>(bytesInRes >> 24),
            static_cast<BYTE>(offset), static_cast<BYTE>(offset >> 8),
            static_cast<BYTE>(offset >> 16), static_cast<BYTE>(offset >> 24),
        };
        AppendBytes(&data, entry, sizeof(entry));
        offset += bytesInRes;
    }

    for (const IcoFrameSpec& frame : frames) {
        BITMAPINFOHEADER info = {};
        info.biSize = sizeof(info);
        info.biWidth = static_cast<LONG>(frame.size);
        info.biHeight = static_cast<LONG>(frame.size * 2);
        info.biPlanes = 1;
        info.biBitCount = 32;
        AppendBytes(&data, &info, sizeof(info));

        if (!frame.pixels.empty()) {
            AppendBytes(&data, frame.pixels.data(), frame.pixels.size());
        } else {
            for (UINT i = 0; i < frame.size * frame.size; ++i) {
                AppendBytes(&data, frame.bgra, 4);
            }
        }
        const DWORD andStride = ((frame.size + 31) / 32) * 4;
        data.insert(data.end(), andStride * frame.size, 0);
    }

    return CreateMemoryStream(data.data(), static_cast<UINT>(data.size()), stream);
}

HRESULT CreateTransparentIcoStream(IStream** stream)
{
    // Bottom-up rows of the PNG test image: transparent top-left, colored elsewhere.
    const std::vector<BYTE> pixels = {
        0, 255, 0, 255,  255, 0, 0, 255,
        0, 0, 0, 0,      0, 0, 255, 255,
    };
    return CreateIcoStream({ { 2, {}, pixels } }, stream);
}

// 8x8 frame of distinct blue shades (all with B >= 192): decodes to many more
// distinct values than any flat frame, standing in for a true-color ICO entry.
std::vector<BYTE> RichBluePixels()
{
    std::vector<BYTE> pixels;
    for (UINT i = 0; i < 64; ++i) {
        pixels.push_back(static_cast<BYTE>(192 + (i % 8) * 8));
        pixels.push_back(0);
        pixels.push_back(0);
        pixels.push_back(255);
    }
    return pixels;
}

HRESULT CreateSvgStream(IStream** stream)
{
    static constexpr char svg[] =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"2\" height=\"2\" viewBox=\"0 0 2 2\">"
        "<rect x=\"1\" y=\"0\" width=\"1\" height=\"2\" fill=\"red\"/>"
        "</svg>";
    return CreateMemoryStream(reinterpret_cast<const BYTE*>(svg), sizeof(svg) - 1, stream);
}

HRESULT CreateSvgSymbolStream(IStream** stream)
{
    static constexpr char svg[] =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"2\" height=\"2\" viewBox=\"0 0 2 2\">"
        "<defs><symbol id=\"mark\" viewBox=\"0 0 100 100\"><rect x=\"50\" y=\"0\" width=\"50\" height=\"100\" fill=\"red\"/></symbol></defs>"
        "<use href=\"#mark\" width=\"2\" height=\"2\"/>"
        "</svg>";
    return CreateMemoryStream(reinterpret_cast<const BYTE*>(svg), sizeof(svg) - 1, stream);
}

HRESULT CreateBlankSvgStream(IStream** stream)
{
    static constexpr char svg[] =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"2\" height=\"2\" viewBox=\"0 0 2 2\"></svg>";
    return CreateMemoryStream(reinterpret_cast<const BYTE*>(svg), sizeof(svg) - 1, stream);
}

HRESULT CreatePdfStream(IStream** stream)
{
    std::string pdf = "%PDF-1.4\n";
    std::vector<size_t> offsets(5);
    auto object = [&](int id, const std::string& body) {
        offsets[id] = pdf.size();
        pdf += std::to_string(id) + " 0 obj\n" + body + "\nendobj\n";
    };

    object(1, "<< /Type /Catalog /Pages 2 0 R >>");
    object(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    object(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 2 2] /Resources << >> /Contents 4 0 R >>");
    const std::string commands = "1 0 0 rg 1 0 1 2 re f\n";
    offsets[4] = pdf.size();
    pdf += "4 0 obj\n<< /Length " + std::to_string(commands.size()) + " >>\nstream\n"
        + commands + "endstream\nendobj\n";

    const size_t xref = pdf.size();
    pdf += "xref\n0 5\n0000000000 65535 f \n";
    for (int i = 1; i <= 4; ++i) {
        char line[32] = {};
        sprintf_s(line, "%010zu 00000 n \n", offsets[i]);
        pdf += line;
    }
    pdf += "trailer\n<< /Size 5 /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
    return CreateMemoryStream(reinterpret_cast<const BYTE*>(pdf.data()), static_cast<UINT>(pdf.size()), stream);
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

bool Renders(IStream* stream, const BackdropperSettings& settings)
{
    HBITMAP bitmap = nullptr;
    WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
    HRESULT hr = RenderBackdropperThumbnail(stream, 32, settings, &bitmap, &alpha);
    if (FAILED(hr)) {
        std::printf("FAIL: RenderBackdropperThumbnail (0x%08lx)\n", static_cast<unsigned long>(hr));
        return false;
    }
    DeleteObject(bitmap);
    return true;
}

bool ContainsColorPixel(IStream* stream, const BackdropperSettings& settings, int bgraIndex)
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
    const LONG stride = section.dsBm.bmWidthBytes;
    bool found = false;
    for (LONG y = 0; y < section.dsBm.bmHeight && !found; ++y) {
        for (LONG x = 0; x < section.dsBm.bmWidth; ++x) {
            const BYTE* pixel = bits + (static_cast<size_t>(y) * stride) + (x * 4);
            bool match = pixel[bgraIndex] > 180;
            for (int channel = 0; channel < 3; ++channel) {
                if (channel != bgraIndex && pixel[channel] >= 80) {
                    match = false;
                }
            }
            if (match) {
                found = true;
                break;
            }
        }
    }

    DeleteObject(bitmap);
    return found;
}

bool ContainsRedPixel(IStream* stream, const BackdropperSettings& settings)
{
    return ContainsColorPixel(stream, settings, 2);
}

bool TransparentPixelStaysTransparent(IStream* stream, const BackdropperSettings& settings)
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
    const bool ok = bits[3] == 0 && alpha == WTSAT_ARGB;
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

    ComPtr<IStream> svg;
    hr = CreateSvgStream(&svg);
    if (FAILED(hr)) {
        CoUninitialize();
        return Fail("CreateSvgStream", hr);
    }
    const bool svgTransparentBecameBackground = TransparentPixelBecameBackground(svg.Get(), settings);
    const bool svgContentRendered = ContainsRedPixel(svg.Get(), settings);

    ComPtr<IStream> svgSymbol;
    hr = CreateSvgSymbolStream(&svgSymbol);
    if (FAILED(hr)) {
        CoUninitialize();
        return Fail("CreateSvgSymbolStream", hr);
    }
    const bool svgSymbolContentRendered = ContainsRedPixel(svgSymbol.Get(), settings);

    ComPtr<IStream> blankSvg;
    hr = CreateBlankSvgStream(&blankSvg);
    if (FAILED(hr)) {
        CoUninitialize();
        return Fail("CreateBlankSvgStream", hr);
    }
    HBITMAP blankBitmap = nullptr;
    WTS_ALPHATYPE blankAlpha = WTSAT_UNKNOWN;
    hr = RenderBackdropperThumbnail(blankSvg.Get(), 32, settings, &blankBitmap, &blankAlpha);
    if (SUCCEEDED(hr)) {
        DeleteObject(blankBitmap);
        CoUninitialize();
        return Fail("blank SVG unexpectedly rendered");
    }

    ComPtr<IStream> pdf;
    hr = CreatePdfStream(&pdf);
    if (FAILED(hr)) {
        CoUninitialize();
        return Fail("CreatePdfStream", hr);
    }
    const bool pdfRendered = Renders(pdf.Get(), settings);

    ComPtr<IStream> ico;
    hr = CreateTransparentIcoStream(&ico);
    if (FAILED(hr)) {
        CoUninitialize();
        return Fail("CreateTransparentIcoStream", hr);
    }
    const bool icoKeptTransparent = TransparentPixelStaysTransparent(ico.Get(), settings);
    const bool icoContentRendered = ContainsRedPixel(ico.Get(), settings);

    BackdropperSettings icoBackdropSettings = settings;
    icoBackdropSettings.protectAppIcons = false;
    const bool icoComposited = TransparentPixelBecameBackground(ico.Get(), icoBackdropSettings);

    // Frame selection: neither 2px nor 4px covers the 32px request, so the
    // larger (green) frame must win over frame 0 (red).
    ComPtr<IStream> icoSmallFrames;
    hr = CreateIcoStream({ { 2, { 0, 0, 255, 255 } }, { 4, { 0, 255, 0, 255 } } }, &icoSmallFrames);
    if (FAILED(hr)) {
        CoUninitialize();
        return Fail("CreateIcoStream small frames", hr);
    }
    const bool icoPickedLargestFrame = ContainsColorPixel(icoSmallFrames.Get(), settings, 1)
        && !ContainsColorPixel(icoSmallFrames.Get(), settings, 2);

    // Both 64px and 128px cover the 32px request, so the smaller covering
    // (blue) frame must win over the largest.
    ComPtr<IStream> icoLargeFrames;
    hr = CreateIcoStream({ { 64, { 255, 0, 0, 255 } }, { 128, { 0, 0, 255, 255 } } }, &icoLargeFrames);
    if (FAILED(hr)) {
        CoUninitialize();
        return Fail("CreateIcoStream large frames", hr);
    }
    const bool icoPickedCoveringFrame = ContainsColorPixel(icoLargeFrames.Get(), settings, 0)
        && !ContainsColorPixel(icoLargeFrames.Get(), settings, 2);

    // Same-size tie-break: the richer (blue multi-shade) frame must beat the
    // flat green frame whichever side of the directory it sits on.
    const std::vector<BYTE> richBlue = RichBluePixels();
    ComPtr<IStream> icoRichFirst;
    hr = CreateIcoStream({ { 8, {}, richBlue }, { 8, { 0, 255, 0, 255 } } }, &icoRichFirst);
    if (FAILED(hr)) {
        CoUninitialize();
        return Fail("CreateIcoStream rich-first frames", hr);
    }
    ComPtr<IStream> icoRichLast;
    hr = CreateIcoStream({ { 8, { 0, 255, 0, 255 } }, { 8, {}, richBlue } }, &icoRichLast);
    if (FAILED(hr)) {
        CoUninitialize();
        return Fail("CreateIcoStream rich-last frames", hr);
    }
    const bool icoPickedRichestFrame =
        ContainsColorPixel(icoRichFirst.Get(), settings, 0) && !ContainsColorPixel(icoRichFirst.Get(), settings, 1)
        && ContainsColorPixel(icoRichLast.Get(), settings, 0) && !ContainsColorPixel(icoRichLast.Get(), settings, 1);

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
    if (!pngTransparentBecameBackground || !tgaTransparentBecameBackground
        || !psdTransparentBecameBackground || !svgTransparentBecameBackground) {
        return Fail("transparent pixel was not composited");
    }
    if (!svgContentRendered) {
        return Fail("SVG content was blank");
    }
    if (!svgSymbolContentRendered) {
        return Fail("SVG symbol/use content was blank");
    }
    if (!pdfRendered) {
        return Fail("PDF did not render");
    }
    if (!icoKeptTransparent || !icoContentRendered) {
        return Fail("ICO did not stay transparent with KeepIcoTransparent on");
    }
    if (!icoComposited) {
        return Fail("ICO was not composited with KeepIcoTransparent off");
    }
    if (!icoPickedLargestFrame) {
        return Fail("ICO frame selection did not pick the largest undersized frame");
    }
    if (!icoPickedCoveringFrame) {
        return Fail("ICO frame selection did not pick the smallest covering frame");
    }
    if (!icoPickedRichestFrame) {
        return Fail("ICO frame selection did not prefer the richest same-size frame");
    }

    std::puts("OK: PNG/TGA/PSD/SVG transparency composited, SVG content rendered, blank SVG rejected, PDF rendered, "
        "ICO transparency preserved and frame selection correct, corrupt input rejected.");
    return 0;
}
