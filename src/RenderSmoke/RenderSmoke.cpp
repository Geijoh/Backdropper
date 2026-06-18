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

bool ContainsRedPixel(IStream* stream, const BackdropperSettings& settings)
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
            if (pixel[2] > 180 && pixel[1] < 80 && pixel[0] < 80) {
                found = true;
                break;
            }
        }
    }

    DeleteObject(bitmap);
    return found;
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

    ComPtr<IStream> pdf;
    hr = CreatePdfStream(&pdf);
    if (FAILED(hr)) {
        CoUninitialize();
        return Fail("CreatePdfStream", hr);
    }
    const bool pdfRendered = Renders(pdf.Get(), settings);

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

    std::puts("OK: PNG/TGA/PSD/SVG transparency composited, SVG content rendered, PDF rendered, corrupt input rejected.");
    return 0;
}
