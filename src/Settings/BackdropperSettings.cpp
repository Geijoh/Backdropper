#include "settings.h"
#include "thumbnail_cache.h"
#include "resource.h"

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <shlwapi.h>
#include <urlmon.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

using namespace Gdiplus;

namespace {

constexpr int IdSolidColor = 200;
constexpr int IdCheckerA = 201;
constexpr int IdCheckerB = 202;
constexpr int IdCheckerSize = 203;

constexpr wchar_t kThumbHandlerKey[] = L"{E357FCCD-A995-4576-B01F-234630154E96}";
constexpr wchar_t kBackdropperClsid[] = L"{7F08B58C-8D1C-44D3-9A73-AB554FF53B1D}";
constexpr std::array<const wchar_t*, 12> kExtensions = {
    L".png", L".webp", L".gif", L".ico", L".svg", L".psd",
    L".ai", L".eps", L".pdf", L".avif", L".tga", L".dds",
};

#define WIDEN_TEXT2(value) L##value
#define WIDEN_TEXT(value) WIDEN_TEXT2(value)
#ifndef BACKDROPPER_VERSION
#define BACKDROPPER_VERSION "0.0.0"
#endif
constexpr wchar_t kBackdropperVersion[] = WIDEN_TEXT(BACKDROPPER_VERSION);
constexpr wchar_t kGithubUrl[] = L"https://github.com/Geijoh/Backdropper";
constexpr wchar_t kPrivacyUrl[] = L"https://github.com/Geijoh/Backdropper/blob/main/PRIVACY.md";
constexpr wchar_t kUpdaterExeName[] = L"BackdropperUpdater.exe";
constexpr wchar_t kLatestVersionUrl[] = L"https://github.com/Geijoh/Backdropper/releases/latest/download/backdropper-version.txt";

enum class Hit {
    None,
    MatchSystem,
    Theme,
    Minimize,
    Maximize,
    Close,
    SegNone,
    SegSolid,
    SegChecker,
    SolidSwatch,
    CheckerASwatch,
    CheckerBSwatch,
    SizeDown,
    SizeUp,
    RestartToggle,
    CheckUpdates,
    InstallUpdate,
    ViewButton,
    About,
    AboutClose,
    AboutUpdate,
    AboutGithub,
    AboutPrivacy,
    Register,
    Unregister,
    Save,
    DialogOk,
    MenuExtraLarge,
    MenuLarge,
    MenuMedium,
    MenuSmall,
    MenuList,
    MenuDetails,
    MenuTiles,
};

enum class ViewMode {
    ExtraLarge,
    Large,
    Medium,
    Small,
    List,
    Details,
    Tiles,
};

enum class AboutActionIcon {
    Update,
    Github,
    Privacy,
};

struct Theme {
    Color bg;
    Color winBorder;
    Color titlebar;
    Color stroke;
    Color fg;
    Color fg2;
    Color card;
    Color cardBorder;
    Color ctrl;
    Color ctrlBorder;
    Color ctrlBottom;
    Color footer;
    Color dialogBg;
    Color dialogBorder;
    Color previewWell;
    Color explorerBg;
    Color explorerHeader;
    Color menuBg;
    Color rowHover;
    Color toggleOff;
    Color accent;
    Color accentText;
};

struct Layout {
    RECT matchSystem {};
    RECT theme {};
    RECT minimize {};
    RECT maximize {};
    RECT close {};
    RECT segNone {};
    RECT segSolid {};
    RECT segChecker {};
    RECT solidSwatch {};
    RECT checkerASwatch {};
    RECT checkerBSwatch {};
    RECT solidEdit {};
    RECT checkerAEdit {};
    RECT checkerBEdit {};
    RECT sizeBox {};
    RECT sizeEdit {};
    RECT sizeDown {};
    RECT sizeUp {};
    RECT restartToggle {};
    RECT checkUpdatesBtn {};
    RECT installUpdateBtn {};
    RECT viewButton {};
    RECT viewMenu {};
    std::array<RECT, 7> menuItems {};
    RECT aboutBtn {};
    RECT aboutDialog {};
    RECT aboutClose {};
    RECT aboutUpdate {};
    RECT aboutGithub {};
    RECT aboutPrivacy {};
    RECT registerBtn {};
    RECT unregisterBtn {};
    RECT saveBtn {};
    RECT dialogOk {};
    RECT titlebar {};
    RECT content {};
    RECT leftPane {};
    RECT rightPane {};
    RECT previewFrame {};
};

struct AppState {
    BackdropperSettings settings;
    std::wstring solidText;
    std::wstring checkerAText;
    std::wstring checkerBText;
    std::wstring sizeText;
    bool dark = false;
    bool matchSystemTheme = true;
    bool registered = false;
    bool viewMenuOpen = false;
    ViewMode view = ViewMode::Large;
    std::wstring dialogTitle;
    std::wstring dialogBody;
    std::wstring updateStatus;
    std::wstring latestVersion;
    bool updateAvailable = false;
    bool aboutOpen = false;
    bool syncingEdits = false;
};

HINSTANCE g_instance = nullptr;
ULONG_PTR g_gdiplusToken = 0;
UINT g_dpi = 96;
double g_scale = 1.0;
HWND g_solidEdit = nullptr;
HWND g_checkerAEdit = nullptr;
HWND g_checkerBEdit = nullptr;
HWND g_sizeEdit = nullptr;
HFONT g_editFont = nullptr;
HBRUSH g_editBrush = nullptr;
Layout g_layout;
AppState g_state;
Hit g_hover = Hit::None;
bool g_trackingMouse = false;

void LayoutChildWindows(HWND window);
void OpenDialog(HWND window, const std::wstring& title, const std::wstring& body);

int Px(double dip)
{
    return static_cast<int>(std::lround(dip * g_scale));
}

double Dip(int px)
{
    return px / g_scale;
}

RECT RectDip(double x, double y, double w, double h)
{
    return { Px(x), Px(y), Px(x + w), Px(y + h) };
}

RectF RectFOf(const RECT& r)
{
    return RectF(
        static_cast<REAL>(r.left),
        static_cast<REAL>(r.top),
        static_cast<REAL>(r.right - r.left),
        static_cast<REAL>(r.bottom - r.top));
}

bool IsEmptyRect(const RECT& r)
{
    return r.right <= r.left || r.bottom <= r.top;
}

bool PtIn(const RECT& r, POINT pt)
{
    return !IsEmptyRect(r) && PtInRect(&r, pt);
}

Color Rgba(int r, int g, int b, int alpha = 255)
{
    return Color(alpha, r, g, b);
}

COLORREF SolidColorRef(const Color& color, COLORREF fallback = RGB(255, 255, 255))
{
    if (color.GetA() == 0) {
        return fallback;
    }
    return RGB(color.GetR(), color.GetG(), color.GetB());
}

bool SystemUsesDarkTheme()
{
    DWORD appsUseLightTheme = 1;
    DWORD bytes = sizeof(appsUseLightTheme);
    RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &appsUseLightTheme, &bytes);
    return appsUseLightTheme == 0;
}

bool EffectiveDark()
{
    return g_state.matchSystemTheme ? SystemUsesDarkTheme() : g_state.dark;
}

Theme CurrentTheme()
{
    if (EffectiveDark()) {
        return {
            Rgba(39, 39, 39),
            Rgba(255, 255, 255, 26),
            Rgba(43, 43, 43),
            Rgba(255, 255, 255, 22),
            Rgba(255, 255, 255),
            Rgba(255, 255, 255, 158),
            Rgba(47, 47, 47),
            Rgba(255, 255, 255, 18),
            Rgba(255, 255, 255, 13),
            Rgba(255, 255, 255, 26),
            Rgba(255, 255, 255, 41),
            Rgba(43, 43, 43),
            Rgba(44, 44, 44),
            Rgba(255, 255, 255, 26),
            Rgba(33, 33, 33),
            Rgba(32, 32, 32),
            Rgba(42, 42, 42),
            Rgba(44, 44, 44),
            Rgba(255, 255, 255, 15),
            Rgba(255, 255, 255, 140),
            Rgba(76, 194, 255),
            Rgba(0, 0, 0),
        };
    }

    return {
        Rgba(243, 243, 243),
        Rgba(0, 0, 0, 26),
        Rgba(238, 240, 244),
        Rgba(0, 0, 0, 20),
        Rgba(26, 26, 26),
        Rgba(0, 0, 0, 153),
        Rgba(255, 255, 255),
        Rgba(0, 0, 0, 14),
        Rgba(255, 255, 255),
        Rgba(0, 0, 0, 20),
        Rgba(0, 0, 0, 41),
        Rgba(250, 250, 250),
        Rgba(251, 251, 251),
        Rgba(0, 0, 0, 26),
        Rgba(234, 235, 238),
        Rgba(255, 255, 255),
        Rgba(247, 248, 250),
        Rgba(251, 251, 251),
        Rgba(0, 0, 0, 12),
        Rgba(0, 0, 0, 115),
        Rgba(0, 95, 184),
        Rgba(255, 255, 255),
    };
}

Color ColorFromRef(COLORREF color)
{
    return Rgba(GetRValue(color), GetGValue(color), GetBValue(color));
}

COLORREF ColorFromTextOr(const std::wstring& text, COLORREF fallback)
{
    COLORREF parsed = fallback;
    return ParseColor(text.c_str(), &parsed) ? parsed : fallback;
}

std::wstring GetWindowTextString(HWND hwnd)
{
    wchar_t buffer[96] = {};
    GetWindowTextW(hwnd, buffer, ARRAYSIZE(buffer));
    return buffer;
}

void SetEditText(HWND hwnd, const std::wstring& text)
{
    g_state.syncingEdits = true;
    SetWindowTextW(hwnd, text.c_str());
    g_state.syncingEdits = false;
}

std::wstring DllPath()
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
    PathRemoveFileSpecW(path);
    PathAppendW(path, L"BackdropperThumb.dll");
    return path;
}

std::wstring AppDirectory()
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
    PathRemoveFileSpecW(path);
    return path;
}

std::wstring UpdaterPath()
{
    wchar_t path[MAX_PATH] = {};
    wcscpy_s(path, AppDirectory().c_str());
    PathAppendW(path, kUpdaterExeName);
    return path;
}

std::wstring TempUpdaterPath()
{
    wchar_t tempDir[MAX_PATH] = {};
    GetTempPathW(ARRAYSIZE(tempDir), tempDir);
    return std::wstring(tempDir) + L"BackdropperUpdate-" + std::to_wstring(GetCurrentProcessId()) + L".exe";
}

std::wstring QuoteArg(const std::wstring& value)
{
    return std::wstring(L"\"") + value + L"\"";
}

bool LaunchUpdater(HWND owner)
{
    const std::wstring updater = UpdaterPath();
    if (GetFileAttributesW(updater.c_str()) == INVALID_FILE_ATTRIBUTES) {
        g_state.aboutOpen = false;
        OpenDialog(owner, L"Updater not found",
            L"BackdropperUpdater.exe was not found next to BackdropperSettings.exe. Download the latest build from GitHub Releases.");
        return false;
    }

    const std::wstring tempUpdater = TempUpdaterPath();
    if (!CopyFileW(updater.c_str(), tempUpdater.c_str(), FALSE)) {
        g_state.aboutOpen = false;
        OpenDialog(owner, L"Update failed",
            L"Backdropper could not copy the updater to a temporary file.");
        return false;
    }

    const std::wstring args =
        std::wstring(L"--install-dir ") + QuoteArg(AppDirectory())
        + L" --current-pid " + std::to_wstring(GetCurrentProcessId())
        + L" --current-version " + QuoteArg(kBackdropperVersion);

    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.hwnd = owner;
    info.lpVerb = L"open";
    info.lpFile = tempUpdater.c_str();
    info.lpParameters = args.c_str();
    info.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&info)) {
        g_state.aboutOpen = false;
        OpenDialog(owner, L"Update failed",
            L"Backdropper could not start the updater.");
        return false;
    }

    return true;
}

std::wstring TrimVersion(std::wstring text)
{
    if (!text.empty() && text[0] == L'v') {
        text.erase(text.begin());
    }
    while (!text.empty() && iswspace(text.front())) {
        text.erase(text.begin());
    }
    while (!text.empty() && iswspace(text.back())) {
        text.pop_back();
    }
    return text;
}

bool ParseVersion(const std::wstring& text, int parts[3])
{
    return swscanf_s(text.c_str(), L"%d.%d.%d", &parts[0], &parts[1], &parts[2]) == 3;
}

int CompareVersions(const std::wstring& left, const std::wstring& right)
{
    int a[3] = {};
    int b[3] = {};
    if (!ParseVersion(left, a) || !ParseVersion(right, b)) {
        return 0;
    }
    for (int i = 0; i < 3; ++i) {
        if (a[i] != b[i]) {
            return a[i] < b[i] ? -1 : 1;
        }
    }
    return 0;
}

bool ReadAsciiFile(const std::wstring& path, std::wstring* text)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD size = GetFileSize(file, nullptr);
    if (size == INVALID_FILE_SIZE || size == 0 || size > 128) {
        CloseHandle(file);
        return false;
    }

    std::string bytes(size, '\0');
    DWORD read = 0;
    const bool ok = ReadFile(file, bytes.data(), size, &read, nullptr) && read == size;
    CloseHandle(file);
    if (!ok) {
        return false;
    }

    text->assign(bytes.begin(), bytes.end());
    return true;
}

bool FetchLatestVersion(std::wstring* version)
{
    wchar_t tempDir[MAX_PATH] = {};
    wchar_t tempFile[MAX_PATH] = {};
    if (!GetTempPathW(ARRAYSIZE(tempDir), tempDir)
        || !GetTempFileNameW(tempDir, L"bdp", 0, tempFile)) {
        return false;
    }

    const std::wstring url = std::wstring(kLatestVersionUrl) + L"?t=" + std::to_wstring(GetTickCount64());
    const HRESULT hr = URLDownloadToFileW(nullptr, url.c_str(), tempFile, 0, nullptr);
    if (FAILED(hr)) {
        DeleteFileW(tempFile);
        return false;
    }

    std::wstring text;
    const bool ok = ReadAsciiFile(tempFile, &text);
    DeleteFileW(tempFile);
    if (!ok) {
        return false;
    }

    *version = TrimVersion(text);
    int parts[3] = {};
    return ParseVersion(*version, parts);
}

bool RunRegsvr(HWND owner, bool unregister)
{
    const std::wstring args = std::wstring(unregister ? L"/u /s \"" : L"/s \"") + DllPath() + L"\"";
    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.hwnd = owner;
    info.lpVerb = L"open";
    info.lpFile = L"regsvr32.exe";
    info.lpParameters = args.c_str();
    info.nShow = SW_HIDE;

    if (!ShellExecuteExW(&info)) {
        return false;
    }

    WaitForSingleObject(info.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(info.hProcess, &exitCode);
    CloseHandle(info.hProcess);
    return exitCode == 0;
}

std::wstring ExtensionHandlerPath(const wchar_t* extension)
{
    return std::wstring(L"Software\\Classes\\") + extension + L"\\shellex\\" + kThumbHandlerKey;
}

std::wstring ClassesExtensionHandlerPath(const wchar_t* extension)
{
    return std::wstring(extension) + L"\\shellex\\" + kThumbHandlerKey;
}

std::wstring ClassesProgIdHandlerPath(const std::wstring& progId)
{
    return progId + L"\\shellex\\" + kThumbHandlerKey;
}

std::wstring ClsidInprocPath()
{
    return std::wstring(L"Software\\Classes\\CLSID\\") + kBackdropperClsid + L"\\InprocServer32";
}

bool ReadStringValue(HKEY root, const std::wstring& path, const wchar_t* name, std::wstring* value)
{
    wchar_t buffer[MAX_PATH] = {};
    DWORD bytes = sizeof(buffer);
    if (RegGetValueW(root, path.c_str(), name, RRF_RT_REG_SZ, nullptr, buffer, &bytes) != ERROR_SUCCESS) {
        return false;
    }
    *value = buffer;
    return true;
}

bool EffectiveHandlerIsBackdropper(const wchar_t* extension)
{
    std::wstring value;
    if (ReadStringValue(HKEY_CLASSES_ROOT, ClassesExtensionHandlerPath(extension), nullptr, &value)
        && _wcsicmp(value.c_str(), kBackdropperClsid) == 0) {
        return true;
    }

    std::wstring progId;
    return ReadStringValue(HKEY_CLASSES_ROOT, extension, nullptr, &progId)
        && !progId.empty()
        && ReadStringValue(HKEY_CLASSES_ROOT, ClassesProgIdHandlerPath(progId), nullptr, &value)
        && _wcsicmp(value.c_str(), kBackdropperClsid) == 0;
}

bool IsBackdropperHandlerRegistered()
{
    std::wstring inproc;
    if (!ReadStringValue(HKEY_CURRENT_USER, ClsidInprocPath(), nullptr, &inproc)
        || _wcsicmp(inproc.c_str(), DllPath().c_str()) != 0) {
        return false;
    }

    for (const wchar_t* extension : kExtensions) {
        if (EffectiveHandlerIsBackdropper(extension)) {
            return true;
        }
    }
    return false;
}

void OpenDialog(HWND window, const std::wstring& title, const std::wstring& body)
{
    g_state.dialogTitle = title;
    g_state.dialogBody = body;
    g_state.viewMenuOpen = false;
    LayoutChildWindows(window);
    InvalidateRect(window, nullptr, TRUE);
}

bool DialogOpen()
{
    return !g_state.dialogTitle.empty();
}

bool AboutOpen()
{
    return g_state.aboutOpen;
}

void CloseDialog(HWND window)
{
    g_state.dialogTitle.clear();
    g_state.dialogBody.clear();
    LayoutChildWindows(window);
    InvalidateRect(window, nullptr, TRUE);
}

void OpenAbout(HWND window)
{
    g_state.aboutOpen = true;
    g_state.viewMenuOpen = false;
    LayoutChildWindows(window);
    InvalidateRect(window, nullptr, TRUE);
}

void CloseAbout(HWND window)
{
    g_state.aboutOpen = false;
    LayoutChildWindows(window);
    InvalidateRect(window, nullptr, TRUE);
}

void UpdateEditBrush()
{
    if (g_editBrush) {
        DeleteObject(g_editBrush);
    }
    const Theme t = CurrentTheme();
    g_editBrush = CreateSolidBrush(SolidColorRef(t.ctrl, EffectiveDark() ? RGB(47, 47, 47) : RGB(255, 255, 255)));
}

void ApplyDpi(HWND window, UINT dpi)
{
    g_dpi = dpi ? dpi : 96;
    g_scale = static_cast<double>(g_dpi) / 96.0;

    if (g_editFont) {
        DeleteObject(g_editFont);
    }
    g_editFont = CreateFontW(-Px(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH, L"Segoe UI Variable Text");

    for (HWND edit : { g_solidEdit, g_checkerAEdit, g_checkerBEdit, g_sizeEdit }) {
        if (edit) {
            SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(g_editFont), TRUE);
        }
    }

    UpdateEditBrush();
    InvalidateRect(window, nullptr, TRUE);
}

void ApplyWindowRegion(HWND window)
{
    if (IsZoomed(window)) {
        SetWindowRgn(window, nullptr, TRUE);
        return;
    }

    RECT r = {};
    GetClientRect(window, &r);
    HRGN region = CreateRoundRectRgn(0, 0, r.right + 1, r.bottom + 1, Px(18), Px(18));
    SetWindowRgn(window, region, TRUE);
}

void CalculateLayout(HWND window)
{
    RECT c = {};
    GetClientRect(window, &c);
    const double w = Dip(c.right - c.left);
    const double h = Dip(c.bottom - c.top);

    g_layout = {};
    g_layout.titlebar = RectDip(0, 0, w, 40);

    constexpr double capW = 46;
    g_layout.close = RectDip(w - capW, 0, capW, 40);
    g_layout.maximize = RectDip(w - capW * 2, 0, capW, 40);
    g_layout.minimize = RectDip(w - capW * 3, 0, capW, 40);
    g_layout.theme = RectDip(w - capW * 3 - 8 - 74, 6, 74, 28);
    g_layout.aboutBtn = RectDip(Dip(g_layout.theme.left) - 8 - 78, 6, 78, 28);
    g_layout.matchSystem = RectDip(Dip(g_layout.aboutBtn.left) - 8 - 152, 6, 152, 28);

    g_layout.content = RectDip(0, 40, w, std::max(0.0, h - 98));

    const double leftW = std::max(360.0, std::min(w - 320.0, w * .45));
    g_layout.leftPane = RectDip(0, 40, leftW, std::max(0.0, h - 98));
    g_layout.rightPane = RectDip(leftW, 40, std::max(0.0, w - leftW), std::max(0.0, h - 98));

    const double cardX = 22;
    const double cardW = leftW - 44;
    double cardY = 116;
    g_layout.segNone = RectDip(cardX + 21, cardY + 49, 65, 30);
    g_layout.segSolid = RectDip(cardX + 88, cardY + 49, 65, 30);
    g_layout.segChecker = RectDip(cardX + 155, cardY + 49, 84, 30);

    const double rowX = cardX + 19;
    const double labelW = 90;
    const double swatchX = rowX + labelW + 12;
    const double editX = swatchX + 30 + 12;
    double rowY = cardY + 99;

    if (g_state.settings.mode == BackdropMode::Solid) {
        g_layout.solidSwatch = RectDip(swatchX, rowY, 30, 30);
        g_layout.solidEdit = RectDip(editX, rowY, 92, 30);
        cardY += 146;
    } else if (g_state.settings.mode == BackdropMode::Checker) {
        g_layout.checkerASwatch = RectDip(swatchX, rowY, 30, 30);
        g_layout.checkerAEdit = RectDip(editX, rowY, 92, 30);
        rowY += 43;
        g_layout.checkerBSwatch = RectDip(swatchX, rowY, 30, 30);
        g_layout.checkerBEdit = RectDip(editX, rowY, 92, 30);
        rowY += 43;
        g_layout.sizeBox = RectDip(swatchX, rowY, 108, 30);
        g_layout.sizeEdit = RectDip(swatchX, rowY, 48, 30);
        g_layout.sizeDown = RectDip(swatchX + 48, rowY, 30, 30);
        g_layout.sizeUp = RectDip(swatchX + 78, rowY, 30, 30);
        cardY += 232;
    } else {
        cardY += 128;
    }

    const double cacheY = cardY + 14;
    g_layout.restartToggle = RectDip(cardX + cardW - 19 - 40, cacheY + 32, 40, 20);
    const double updateY = cacheY + 100;
    g_layout.checkUpdatesBtn = RectDip(cardX + cardW - 19 - 70, updateY + 46, 70, 30);
    if (g_state.updateAvailable) {
        g_layout.installUpdateBtn = RectDip(cardX + cardW - 19 - 70, updateY + 13, 70, 30);
    }

    const double footerY = h - 58;
    const double saveW = 70;
    const double unregW = 96;
    const double regW = 132;
    const double saveX = w - 20 - saveW;
    const double unregX = saveX - 10 - 9 - 10 - unregW;
    const double regX = unregX - 10 - regW;
    g_layout.saveBtn = RectDip(saveX, footerY + 13, saveW, 32);
    g_layout.unregisterBtn = RectDip(unregX, footerY + 13, unregW, 32);
    g_layout.registerBtn = RectDip(regX, footerY + 13, regW, 32);

    const double rightX = leftW;
    const double rightW = w - leftW;
    g_layout.previewFrame = RectDip(rightX + 18, 96, rightW - 36, std::max(120.0, footerY - 114));

    const double viewW = 142;
    const double viewY = 96 + 7;
    g_layout.viewButton = RectDip(rightX + rightW - 18 - 12 - viewW, viewY, viewW, 30);
    g_layout.viewMenu = RectDip(rightX + rightW - 18 - 12 - 204, viewY + 36, 204, 248);
    for (int i = 0; i < 7; ++i) {
        g_layout.menuItems[i] = RectDip(rightX + rightW - 18 - 12 - 199, viewY + 41 + i * 34, 194, 34);
    }

    g_layout.dialogOk = RectDip((w - 312) / 2, (h - 156) / 2 + 98, 312, 34);

    constexpr double aboutDialogW = 394;
    constexpr double aboutDialogH = 376;
    const double aboutDialogX = (w - aboutDialogW) / 2;
    const double aboutDialogY = (h - aboutDialogH) / 2;
    g_layout.aboutDialog = RectDip(aboutDialogX, aboutDialogY, aboutDialogW, aboutDialogH);
    g_layout.aboutClose = RectDip(aboutDialogX + 348, aboutDialogY + 18, 28, 28);
    g_layout.aboutUpdate = RectDip(aboutDialogX + 28, aboutDialogY + 315, 96, 34);
    g_layout.aboutGithub = RectDip(aboutDialogX + 134, aboutDialogY + 315, 96, 34);
    g_layout.aboutPrivacy = RectDip(aboutDialogX + 240, aboutDialogY + 315, 136, 34);
}

void MoveEdit(HWND edit, const RECT& outer)
{
    if (IsEmptyRect(outer)) {
        ShowWindow(edit, SW_HIDE);
        return;
    }

    const RECT inner = {
        outer.left + Px(8),
        outer.top + Px(4),
        outer.right - Px(8),
        outer.bottom - Px(4),
    };
    SetWindowPos(edit, nullptr, inner.left, inner.top, inner.right - inner.left, inner.bottom - inner.top,
        SWP_NOZORDER | SWP_NOACTIVATE);
}

void LayoutChildWindows(HWND window)
{
    CalculateLayout(window);
    const bool show = !DialogOpen() && !AboutOpen();

    ShowWindow(g_solidEdit, show && g_state.settings.mode == BackdropMode::Solid ? SW_SHOW : SW_HIDE);
    ShowWindow(g_checkerAEdit, show && g_state.settings.mode == BackdropMode::Checker ? SW_SHOW : SW_HIDE);
    ShowWindow(g_checkerBEdit, show && g_state.settings.mode == BackdropMode::Checker ? SW_SHOW : SW_HIDE);
    ShowWindow(g_sizeEdit, show && g_state.settings.mode == BackdropMode::Checker ? SW_SHOW : SW_HIDE);

    MoveEdit(g_solidEdit, g_layout.solidEdit);
    MoveEdit(g_checkerAEdit, g_layout.checkerAEdit);
    MoveEdit(g_checkerBEdit, g_layout.checkerBEdit);

    if (!IsEmptyRect(g_layout.sizeEdit)) {
        const RECT sizeInner = {
            g_layout.sizeEdit.left + Px(7),
            g_layout.sizeEdit.top + Px(4),
            g_layout.sizeEdit.right - Px(2),
            g_layout.sizeEdit.bottom - Px(4),
        };
        SetWindowPos(g_sizeEdit, nullptr, sizeInner.left, sizeInner.top,
            sizeInner.right - sizeInner.left, sizeInner.bottom - sizeInner.top,
            SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void DrawRounded(Graphics& g, const RECT& rect, float radiusDip, const Color& fill)
{
    const RectF r = RectFOf(rect);
    const REAL radius = static_cast<REAL>(Px(radiusDip));
    if (radius <= 0) {
        SolidBrush brush(fill);
        g.FillRectangle(&brush, r);
        return;
    }

    const REAL d = radius * 2;
    GraphicsPath path;
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    path.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    path.CloseFigure();
    SolidBrush brush(fill);
    g.FillPath(&brush, &path);
}

void DrawRoundedBorder(Graphics& g, const RECT& rect, float radiusDip, const Color& fill, const Color& border, float borderDip = 1)
{
    const RectF r = RectFOf(rect);
    const REAL radius = static_cast<REAL>(Px(radiusDip));
    const REAL d = radius * 2;

    GraphicsPath path;
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    path.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    path.CloseFigure();

    SolidBrush brush(fill);
    g.FillPath(&brush, &path);
    Pen pen(border, static_cast<REAL>(std::max(1, Px(borderDip))));
    g.DrawPath(&pen, &path);
}

void DrawTextBlock(Graphics& g, const std::wstring& text, const RECT& rect, float sizeDip,
    const Color& color, int style = FontStyleRegular, StringAlignment align = StringAlignmentNear,
    StringAlignment lineAlign = StringAlignmentNear, bool wrap = false)
{
    FontFamily family(L"Segoe UI Variable Text");
    FontFamily fallback(L"Segoe UI");
    const FontFamily* selectedFamily = family.GetLastStatus() == Ok ? &family : &fallback;
    Font font(selectedFamily, static_cast<REAL>(Px(sizeDip)), style, UnitPixel);
    SolidBrush brush(color);
    StringFormat format;
    format.SetAlignment(align);
    format.SetLineAlignment(lineAlign);
    format.SetTrimming(StringTrimmingEllipsisCharacter);
    if (!wrap) {
        format.SetFormatFlags(StringFormatFlagsNoWrap);
    }
    g.DrawString(text.c_str(), -1, &font, RectFOf(rect), &format, &brush);
}

void DrawLine(Graphics& g, double x1, double y1, double x2, double y2, const Color& color, float widthDip = 1)
{
    Pen pen(color, static_cast<REAL>(Px(widthDip)));
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    g.DrawLine(&pen,
        static_cast<REAL>(Px(x1)), static_cast<REAL>(Px(y1)),
        static_cast<REAL>(Px(x2)), static_cast<REAL>(Px(y2)));
}

void DrawRectLine(Graphics& g, const RECT& rect, const Color& color, float widthDip = 1)
{
    Pen pen(color, static_cast<REAL>(Px(widthDip)));
    g.DrawRectangle(&pen, rect.left, rect.top, rect.right - rect.left - 1, rect.bottom - rect.top - 1);
}

void DrawChevronDown(Graphics& g, double cx, double cy, const Color& color)
{
    PointF pts[] = {
        PointF(static_cast<REAL>(Px(cx - 4)), static_cast<REAL>(Px(cy - 2))),
        PointF(static_cast<REAL>(Px(cx + 4)), static_cast<REAL>(Px(cy - 2))),
        PointF(static_cast<REAL>(Px(cx)), static_cast<REAL>(Px(cy + 3))),
    };
    SolidBrush brush(color);
    g.FillPolygon(&brush, pts, 3);
}

void DrawCheck(Graphics& g, double x, double y, const Color& color)
{
    DrawLine(g, x, y + 5, x + 4, y + 9, color, 1.7f);
    DrawLine(g, x + 4, y + 9, x + 11, y, color, 1.7f);
}

void DrawAppIcon(Graphics& g, double x, double y, double size, const Theme&)
{
    auto s = [&](double v) { return v * size / 256.0; };
    auto iconRect = [&](double rx, double ry, double rw, double rh) {
        return RectDip(x + s(rx), y + s(ry), s(rw), s(rh));
    };

    const RectF box = RectFOf(iconRect(26, 26, 204, 204));
    const REAL radius = static_cast<REAL>(Px(s(14)));
    GraphicsPath clip;
    clip.AddArc(box.X, box.Y, radius * 2, radius * 2, 180, 90);
    clip.AddArc(box.X + box.Width - radius * 2, box.Y, radius * 2, radius * 2, 270, 90);
    clip.AddArc(box.X + box.Width - radius * 2, box.Y + box.Height - radius * 2, radius * 2, radius * 2, 0, 90);
    clip.AddArc(box.X, box.Y + box.Height - radius * 2, radius * 2, radius * 2, 90, 90);
    clip.CloseFigure();

    GraphicsState state = g.Save();
    g.SetClip(&clip);

    SolidBrush base(Rgba(235, 235, 235));
    g.FillPath(&base, &clip);

    SolidBrush light(Rgba(235, 235, 235));
    SolidBrush dark(Rgba(175, 181, 190));
    const std::array<std::pair<double, double>, 18> lightRects = {{
        {26, 26}, {26, 94}, {26, 162}, {196, 60}, {196, 128}, {196, 196},
        {94, 26}, {94, 94}, {94, 162}, {128, 60}, {128, 128}, {128, 196},
        {162, 26}, {162, 94}, {162, 162}, {60, 60}, {60, 128}, {60, 196},
    }};
    const std::array<std::pair<double, double>, 18> darkRects = {{
        {60, 26}, {60, 94}, {60, 162}, {162, 60}, {162, 128}, {162, 196},
        {128, 26}, {128, 94}, {128, 162}, {94, 60}, {94, 128}, {94, 196},
        {196, 26}, {196, 94}, {196, 162}, {26, 60}, {26, 128}, {26, 196},
    }};
    for (const auto& [rx, ry] : lightRects) {
        g.FillRectangle(&light, RectFOf(iconRect(rx, ry, 34, 34)));
    }
    for (const auto& [rx, ry] : darkRects) {
        g.FillRectangle(&dark, RectFOf(iconRect(rx, ry, 34, 34)));
    }

    PointF triangle[] = {
        PointF(static_cast<REAL>(Px(x + s(256))), static_cast<REAL>(Px(y + s(68)))),
        PointF(static_cast<REAL>(Px(x + s(68))), static_cast<REAL>(Px(y + s(256)))),
        PointF(static_cast<REAL>(Px(x + s(256))), static_cast<REAL>(Px(y + s(256)))),
    };
    SolidBrush panel(Rgba(243, 243, 243));
    g.FillPolygon(&panel, triangle, 3);

    SolidBrush dot(Rgba(246, 112, 103));
    g.FillEllipse(&dot, RectFOf(iconRect(111, 111, 102, 102)));
    g.Restore(state);
}

void DrawFolderIcon(Graphics& g, double x, double y)
{
    GraphicsPath path;
    path.AddLine(static_cast<REAL>(Px(x + 1)), static_cast<REAL>(Px(y + 5)), static_cast<REAL>(Px(x + 6)), static_cast<REAL>(Px(y + 5)));
    path.AddLine(static_cast<REAL>(Px(x + 8)), static_cast<REAL>(Px(y + 7)), static_cast<REAL>(Px(x + 17)), static_cast<REAL>(Px(y + 7)));
    path.AddLine(static_cast<REAL>(Px(x + 20)), static_cast<REAL>(Px(y + 10)), static_cast<REAL>(Px(x + 20)), static_cast<REAL>(Px(y + 17)));
    path.AddLine(static_cast<REAL>(Px(x + 1)), static_cast<REAL>(Px(y + 17)), static_cast<REAL>(Px(x + 1)), static_cast<REAL>(Px(y + 5)));
    path.CloseFigure();
    SolidBrush fill(Rgba(232, 178, 58));
    Pen pen(Rgba(199, 144, 42), static_cast<REAL>(Px(1)));
    g.FillPath(&fill, &path);
    g.DrawPath(&pen, &path);
}

void DrawGridIcon(Graphics& g, double x, double y, const Color& color)
{
    SolidBrush brush(color);
    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 2; ++col) {
            DrawRounded(g, RectDip(x + col * 10, y + row * 10, 7, 7), 1.4f, color);
        }
    }
}

void DrawThemeIcon(Graphics& g, const RECT& rect, const Theme& t)
{
    const double x = Dip(rect.left) + 10;
    const double y = Dip(rect.top) + 7;
    if (EffectiveDark()) {
        Pen pen(t.fg2, static_cast<REAL>(Px(1.6)));
        g.DrawEllipse(&pen, RectFOf(RectDip(x + 3, y + 3, 7, 7)));
        DrawLine(g, x + 6.5, y, x + 6.5, y + 2, t.fg2, 1.2f);
        DrawLine(g, x + 6.5, y + 12, x + 6.5, y + 14, t.fg2, 1.2f);
        DrawLine(g, x, y + 6.5, x + 2, y + 6.5, t.fg2, 1.2f);
        DrawLine(g, x + 12, y + 6.5, x + 14, y + 6.5, t.fg2, 1.2f);
    } else {
        GraphicsPath moon;
        moon.AddEllipse(RectFOf(RectDip(x + 1, y + 1, 12, 12)));
        GraphicsPath cut;
        cut.AddEllipse(RectFOf(RectDip(x + 6, y - 1, 12, 12)));
        moon.AddPath(&cut, FALSE);
        SolidBrush brush(t.fg2);
        g.FillPath(&brush, &moon);
    }
}

void DrawButton(Graphics& g, const RECT& rect, const std::wstring& text, const Theme& t,
    bool primary = false, bool disabled = false, bool hovered = false)
{
    Color fill = primary ? t.accent : t.ctrl;
    Color fg = primary ? t.accentText : t.fg;
    Color border = primary ? t.accent : t.ctrlBorder;
    if (hovered && !disabled && !primary) {
        fill = EffectiveDark() ? Rgba(255, 255, 255, 20) : Rgba(0, 0, 0, 10);
    }
    if (disabled) {
        fill = Color(115, fill.GetR(), fill.GetG(), fill.GetB());
        fg = Color(115, fg.GetR(), fg.GetG(), fg.GetB());
        border = Color(115, border.GetR(), border.GetG(), border.GetB());
    }
    DrawRoundedBorder(g, rect, 5, fill, border);
    DrawTextBlock(g, text, rect, 13, fg, primary ? FontStyleBold : FontStyleRegular,
        StringAlignmentCenter, StringAlignmentCenter);
}

std::wstring RegistrationText()
{
    return g_state.registered
        ? L"Registered for supported image formats"
        : L"Not registered \u2014 using default Windows handlers";
}

Color RegistrationDot(const Theme& t)
{
    return g_state.registered
        ? (EffectiveDark() ? Rgba(108, 203, 95) : Rgba(15, 123, 15))
        : t.toggleOff;
}

void DrawSwitch(Graphics& g, const RECT& rect, bool on, const Theme& t)
{
    DrawRoundedBorder(g, rect, 10, on ? t.accent : Color(0, 0, 0, 0), on ? t.accent : t.toggleOff);
    const double thumbX = Dip(rect.left) + (on ? 22 : 4);
    DrawRounded(g, RectDip(thumbX, Dip(rect.top) + 4, 12, 12), 6, on ? t.accentText : t.toggleOff);
}

void DrawGithubIcon(Graphics& g, const RECT& rect, const Theme& t)
{
    const double size = 18;
    const double left = Dip(rect.left) + (Dip(rect.right - rect.left) - size) / 2;
    const double top = Dip(rect.top) + (Dip(rect.bottom - rect.top) - size) / 2 + 1;
    const double unit = size / 16.0;
    auto point = [&](double x, double y) {
        return PointF(
            static_cast<REAL>(Px(left + x * unit)),
            static_cast<REAL>(Px(top + y * unit)));
    };

    // ponytail: embedded Octicons SVG path; adding an SVG renderer for one icon is not worth it.
    GraphicsPath path(FillModeWinding);
    double currentX = 8;
    double currentY = 0;
    auto curve = [&](double x1, double y1, double x2, double y2, double x3, double y3) {
        path.AddBezier(point(currentX, currentY), point(x1, y1), point(x2, y2), point(x3, y3));
        currentX = x3;
        currentY = y3;
    };
    curve(3.58, 0, 0, 3.58, 0, 8);
    curve(0, 11.54, 2.29, 14.53, 5.47, 15.59);
    curve(5.87, 15.66, 6.02, 15.42, 6.02, 15.21);
    curve(6.02, 15.02, 6.01, 14.39, 6.01, 13.72);
    curve(4, 14.09, 3.48, 13.23, 3.32, 12.78);
    curve(3.23, 12.55, 2.84, 11.84, 2.5, 11.65);
    curve(2.22, 11.5, 1.82, 11.13, 2.49, 11.12);
    curve(3.12, 11.11, 3.57, 11.7, 3.72, 11.94);
    curve(4.44, 13.15, 5.59, 12.81, 6.05, 12.6);
    curve(6.12, 12.08, 6.33, 11.73, 6.56, 11.53);
    curve(4.78, 11.33, 2.92, 10.64, 2.92, 7.58);
    curve(2.92, 6.71, 3.23, 5.99, 3.74, 5.43);
    curve(3.66, 5.23, 3.38, 4.41, 3.82, 3.31);
    curve(3.82, 3.31, 4.49, 3.1, 6.02, 4.13);
    curve(6.66, 3.95, 7.34, 3.86, 8.02, 3.86);
    curve(8.7, 3.86, 9.38, 3.95, 10.02, 4.13);
    curve(11.55, 3.09, 12.22, 3.31, 12.22, 3.31);
    curve(12.66, 4.41, 12.38, 5.23, 12.3, 5.43);
    curve(12.81, 5.99, 13.12, 6.7, 13.12, 7.58);
    curve(13.12, 10.65, 11.25, 11.33, 9.47, 11.53);
    curve(9.76, 11.78, 10.01, 12.26, 10.01, 13.01);
    curve(10.01, 14.08, 10, 14.94, 10, 15.21);
    curve(10, 15.42, 10.15, 15.67, 10.55, 15.59);
    curve(13.71, 14.53, 16, 11.53, 16, 8);
    curve(16, 3.58, 12.42, 0, 8, 0);
    path.CloseFigure();

    SolidBrush brush(t.fg);
    g.FillPath(&brush, &path);
}

void DrawInfoIcon(Graphics& g, const RECT& rect, const Theme& t)
{
    const double size = 16;
    const double left = Dip(rect.left) + (Dip(rect.right - rect.left) - size) / 2;
    const double top = Dip(rect.top) + (Dip(rect.bottom - rect.top) - size) / 2;
    Pen pen(t.fg2, static_cast<REAL>(Px(1.4)));
    g.DrawEllipse(&pen, RectFOf(RectDip(left, top, size, size)));
    DrawTextBlock(g, L"i", RectDip(left, top - 0.5, size, size), 10.5f, t.fg2,
        FontStyleBold, StringAlignmentCenter, StringAlignmentCenter);
}

void DrawShieldIcon(Graphics& g, const RECT& rect, const Theme& t)
{
    const double size = 16;
    const double left = Dip(rect.left) + (Dip(rect.right - rect.left) - size) / 2;
    const double top = Dip(rect.top) + (Dip(rect.bottom - rect.top) - size) / 2;

    GraphicsPath shield;
    shield.AddLine(static_cast<REAL>(Px(left + 8)), static_cast<REAL>(Px(top + 1.5)),
        static_cast<REAL>(Px(left + 13)), static_cast<REAL>(Px(top + 3.4)));
    shield.AddLine(static_cast<REAL>(Px(left + 13)), static_cast<REAL>(Px(top + 7.1)),
        static_cast<REAL>(Px(left + 11.2)), static_cast<REAL>(Px(top + 11.5)));
    shield.AddLine(static_cast<REAL>(Px(left + 8)), static_cast<REAL>(Px(top + 14.2)),
        static_cast<REAL>(Px(left + 4.8)), static_cast<REAL>(Px(top + 11.5)));
    shield.AddLine(static_cast<REAL>(Px(left + 3)), static_cast<REAL>(Px(top + 7.1)),
        static_cast<REAL>(Px(left + 3)), static_cast<REAL>(Px(top + 3.4)));
    shield.CloseFigure();

    Pen pen(t.fg, static_cast<REAL>(Px(1.25)));
    pen.SetLineJoin(LineJoinRound);
    g.DrawPath(&pen, &shield);
}

void DrawDownloadIcon(Graphics& g, const RECT& rect, const Theme& t)
{
    const double size = 16;
    const double left = Dip(rect.left) + (Dip(rect.right - rect.left) - size) / 2;
    const double top = Dip(rect.top) + (Dip(rect.bottom - rect.top) - size) / 2;

    Pen pen(t.fg, static_cast<REAL>(Px(1.35)));
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    g.DrawLine(&pen, PointF(static_cast<REAL>(Px(left + 8)), static_cast<REAL>(Px(top + 2))),
        PointF(static_cast<REAL>(Px(left + 8)), static_cast<REAL>(Px(top + 10))));
    g.DrawLine(&pen, PointF(static_cast<REAL>(Px(left + 4.5)), static_cast<REAL>(Px(top + 6.8))),
        PointF(static_cast<REAL>(Px(left + 8)), static_cast<REAL>(Px(top + 10.3))));
    g.DrawLine(&pen, PointF(static_cast<REAL>(Px(left + 11.5)), static_cast<REAL>(Px(top + 6.8))),
        PointF(static_cast<REAL>(Px(left + 8)), static_cast<REAL>(Px(top + 10.3))));
    g.DrawLine(&pen, PointF(static_cast<REAL>(Px(left + 3.5)), static_cast<REAL>(Px(top + 13.5))),
        PointF(static_cast<REAL>(Px(left + 12.5)), static_cast<REAL>(Px(top + 13.5))));
}

void DrawAboutActionButton(Graphics& g, const RECT& rect, const std::wstring& text, const Theme& t, Hit hit, AboutActionIcon iconKind)
{
    const bool hovered = g_hover == hit;
    DrawRoundedBorder(g, rect, 5,
        hovered ? (EffectiveDark() ? Rgba(255, 255, 255, 20) : Rgba(0, 0, 0, 10)) : t.ctrl,
        t.ctrlBorder);

    const double groupW =
        iconKind == AboutActionIcon::Privacy ? 112 :
        iconKind == AboutActionIcon::Github ? 88 : 82;
    const double iconSize = 18;
    const double x = Dip(rect.left) + (Dip(rect.right - rect.left) - groupW) / 2;
    const double y = Dip(rect.top) + (Dip(rect.bottom - rect.top) - iconSize) / 2;
    const RECT icon = RectDip(x, y, iconSize, iconSize);
    if (iconKind == AboutActionIcon::Update) {
        DrawDownloadIcon(g, icon, t);
    } else if (iconKind == AboutActionIcon::Github) {
        DrawGithubIcon(g, icon, t);
    } else {
        DrawShieldIcon(g, icon, t);
    }

    DrawTextBlock(g, text, RectDip(x + iconSize + 8, Dip(rect.top), groupW - iconSize - 8, Dip(rect.bottom - rect.top)),
        12.5f, t.fg, FontStyleRegular, StringAlignmentNear, StringAlignmentCenter);
}

void DrawSegment(Graphics& g, const RECT& rect, const std::wstring& text, bool active, const Theme& t)
{
    if (active) {
        DrawRounded(g, rect, 4, t.accent);
    }
    DrawTextBlock(g, text, rect, 13, active ? t.accentText : t.fg,
        active ? FontStyleBold : FontStyleRegular, StringAlignmentCenter, StringAlignmentCenter);
}

void DrawInputFrame(Graphics& g, const RECT& rect, const Theme& t)
{
    DrawRoundedBorder(g, rect, 5, t.ctrl, t.ctrlBorder);
    const RECT bottom = { rect.left + Px(1), rect.bottom - Px(1), rect.right - Px(1), rect.bottom };
    SolidBrush brush(t.ctrlBottom);
    g.FillRectangle(&brush, RectFOf(bottom));
}

void DrawChecker(Graphics& g, const RECT& rect, COLORREF a, COLORREF b, int sizeDip)
{
    SolidBrush base(ColorFromRef(a));
    g.FillRectangle(&base, RectFOf(rect));

    const int s = std::max(1, Px(sizeDip));
    SolidBrush alt(ColorFromRef(b));
    for (int y = rect.top; y < rect.bottom; y += s) {
        for (int x = rect.left; x < rect.right; x += s) {
            const bool useAlt = (((x - rect.left) / s) + ((y - rect.top) / s)) % 2 == 1;
            if (useAlt) {
                g.FillRectangle(&alt, static_cast<REAL>(x), static_cast<REAL>(y),
                    static_cast<REAL>(std::min<int>(s, static_cast<int>(rect.right - x))),
                    static_cast<REAL>(std::min<int>(s, static_cast<int>(rect.bottom - y))));
            }
        }
    }
}

void DrawThumbnailBackground(Graphics& g, const RECT& rect)
{
    if (g_state.settings.mode == BackdropMode::Solid) {
        SolidBrush brush(ColorFromRef(ColorFromTextOr(g_state.solidText, g_state.settings.solidColor)));
        g.FillRectangle(&brush, RectFOf(rect));
    } else if (g_state.settings.mode == BackdropMode::Checker) {
        DrawChecker(g, rect,
            ColorFromTextOr(g_state.checkerAText, g_state.settings.checkerA),
            ColorFromTextOr(g_state.checkerBText, g_state.settings.checkerB),
            static_cast<int>(std::max(2u, g_state.settings.checkerSize)));
    }
}

void DrawAsset(Graphics& g, int index, const RECT& thumb)
{
    GraphicsState saved = g.Save();
    Region clip(RectFOf(thumb));
    g.SetClip(&clip, CombineModeReplace);

    const double x = Dip(thumb.left);
    const double y = Dip(thumb.top);
    const double w = Dip(thumb.right - thumb.left);
    const double h = Dip(thumb.bottom - thumb.top);
    const double s = std::min(w, h);
    const double ox = x + (w - s) / 2.0;
    const double oy = y + (h - s) / 2.0;

    auto X = [&](double v) { return static_cast<REAL>(Px(ox + v * s / 100.0)); };
    auto Y = [&](double v) { return static_cast<REAL>(Px(oy + v * s / 100.0)); };

    if (index == 0) {
        RectF r(X(16), Y(16), X(84) - X(16), Y(84) - Y(16));
        LinearGradientBrush brush(r, Rgba(255, 122, 89), Rgba(255, 45, 135), LinearGradientModeForwardDiagonal);
        GraphicsPath path;
        const REAL rad = static_cast<REAL>(Px(s * .18));
        path.AddArc(r.X, r.Y, rad, rad, 180, 90);
        path.AddArc(r.X + r.Width - rad, r.Y, rad, rad, 270, 90);
        path.AddArc(r.X + r.Width - rad, r.Y + r.Height - rad, rad, rad, 0, 90);
        path.AddArc(r.X, r.Y + r.Height - rad, rad, rad, 90, 90);
        path.CloseFigure();
        g.FillPath(&brush, &path);
        PointF tri[] = { { X(44), Y(38) }, { X(66), Y(50) }, { X(44), Y(62) } };
        SolidBrush white(Rgba(255, 255, 255));
        g.FillPolygon(&white, tri, 3);
    } else if (index == 1) {
        SolidBrush blue(Rgba(60, 157, 246));
        g.FillEllipse(&blue, RectF(X(25), Y(39), X(55) - X(25), Y(69) - Y(39)));
        g.FillEllipse(&blue, RectF(X(37), Y(28), X(77) - X(37), Y(68) - Y(28)));
        g.FillEllipse(&blue, RectF(X(57), Y(44), X(83) - X(57), Y(70) - Y(44)));
        DrawRounded(g, RectDip(ox + 32 * s / 100.0, oy + 54 * s / 100.0, 44 * s / 100.0, 16 * s / 100.0), 8 * static_cast<float>(s / 100.0), Rgba(60, 157, 246));
    } else if (index == 2) {
        PointF pts[] = {
            { X(50), Y(16) }, { X(60), Y(39) }, { X(85), Y(41) }, { X(66), Y(58) },
            { X(72), Y(83) }, { X(50), Y(70) }, { X(28), Y(83) }, { X(34), Y(58) },
            { X(15), Y(41) }, { X(40), Y(39) },
        };
        SolidBrush fill(Rgba(255, 200, 61));
        Pen pen(Rgba(255, 255, 255), static_cast<REAL>(Px(3 * s / 100.0)));
        pen.SetLineJoin(LineJoinRound);
        g.FillPolygon(&fill, pts, 10);
        g.DrawPolygon(&pen, pts, 10);
    } else if (index == 3) {
        RectF r(X(17), Y(17), X(83) - X(17), Y(83) - Y(17));
        LinearGradientBrush brush(r, Rgba(22, 192, 166), Rgba(46, 125, 246), LinearGradientModeForwardDiagonal);
        g.FillEllipse(&brush, r);
        SolidBrush white(Rgba(255, 255, 255));
        g.FillEllipse(&white, RectF(X(39), Y(32), X(61) - X(39), Y(54) - Y(32)));
        GraphicsPath body;
        body.AddArc(RectF(X(31), Y(56), X(69) - X(31), Y(90) - Y(56)), 180, 180);
        body.AddLine(X(69), Y(73), X(31), Y(73));
        body.CloseFigure();
        g.FillPath(&white, &body);
    } else if (index == 4) {
        RECT r = RectDip(ox, oy + 33 * s / 100.0, s, 40 * s / 100.0);
        DrawTextBlock(g, L"ACME", r, static_cast<float>(27 * s / 100.0), Rgba(35, 37, 43),
            FontStyleBold, StringAlignmentCenter, StringAlignmentCenter);
    } else {
        PointF pts[] = {
            { X(50), Y(14) }, { X(82), Y(31) }, { X(82), Y(69) },
            { X(50), Y(86) }, { X(18), Y(69) }, { X(18), Y(31) },
        };
        SolidBrush fill(Rgba(52, 199, 89));
        g.FillPolygon(&fill, pts, 6);
        Pen pen(Rgba(255, 255, 255), static_cast<REAL>(Px(6 * s / 100.0)));
        pen.SetStartCap(LineCapRound);
        pen.SetEndCap(LineCapRound);
        pen.SetLineJoin(LineJoinRound);
        PointF check[] = { { X(37), Y(50) }, { X(47), Y(60) }, { X(65), Y(40) } };
        g.DrawLines(&pen, check, 3);
    }

    g.Restore(saved);
}

struct FileItem {
    const wchar_t* name;
    const wchar_t* kind;
    const wchar_t* size;
    const wchar_t* date;
};

constexpr std::array<FileItem, 6> kFiles = { {
    { L"app-logo.png", L"PNG File", L"48 KB", L"6/14/2026 9:21 AM" },
    { L"cloud.webp", L"WEBP File", L"22 KB", L"6/12/2026 4:03 PM" },
    { L"star-sticker.svg", L"SVG File", L"31 KB", L"6/9/2026 11:47 AM" },
    { L"wordmark.pdf", L"PDF File", L"64 KB", L"5/30/2026 2:15 PM" },
    { L"poster.psd", L"PSD File", L"18 KB", L"5/28/2026 8:52 AM" },
    { L"texture.tga", L"TGA File", L"27 KB", L"5/21/2026 6:30 PM" },
} };

std::wstring ViewLabel(ViewMode view)
{
    switch (view) {
    case ViewMode::ExtraLarge: return L"Extra large icons";
    case ViewMode::Large: return L"Large icons";
    case ViewMode::Medium: return L"Medium icons";
    case ViewMode::Small: return L"Small icons";
    case ViewMode::List: return L"List";
    case ViewMode::Details: return L"Details";
    case ViewMode::Tiles: return L"Tiles";
    }
    return L"Large icons";
}

void DrawThumb(Graphics& g, const RECT& rect, int fileIndex, const Theme& t)
{
    if (g_state.settings.mode != BackdropMode::None) {
        DrawThumbnailBackground(g, rect);
        DrawRoundedBorder(g, rect, Dip(rect.right - rect.left) >= 48 ? 3 : 2, Color(0, 0, 0, 0), Color(0, 0, 0, 22));
    }
    DrawAsset(g, fileIndex, rect);
}

void DrawPreviewGrid(Graphics& g, const RECT& content, const Theme& t)
{
    int thumb = 72;
    int cellW = 104;
    if (g_state.view == ViewMode::ExtraLarge) {
        thumb = 96;
        cellW = 132;
    } else if (g_state.view == ViewMode::Medium) {
        thumb = 48;
        cellW = 86;
    }

    const double startX = Dip(content.left);
    const double startY = Dip(content.top);
    const double maxX = Dip(content.right);
    double x = startX;
    double y = startY;
    const double cellH = thumb + 46;

    for (int i = 0; i < static_cast<int>(kFiles.size()); ++i) {
        if (x + cellW > maxX && x > startX) {
            x = startX;
            y += cellH + 4;
        }
        const RECT cell = RectDip(x, y, cellW, cellH);
        const RECT thumbRect = RectDip(x + (cellW - thumb) / 2.0, y + 9, thumb, thumb);
        DrawThumb(g, thumbRect, i, t);
        const RECT nameRect = RectDip(x + 5, y + 9 + thumb + 7, cellW - 10, 32);
        DrawTextBlock(g, kFiles[i].name, nameRect, 12, t.fg, FontStyleRegular, StringAlignmentCenter, StringAlignmentNear, true);
        x += cellW + 4;
    }
}

void DrawPreviewRows(Graphics& g, const RECT& content, const Theme& t, bool vertical)
{
    const double startX = Dip(content.left);
    const double startY = Dip(content.top);
    double x = startX;
    double y = startY;

    for (int i = 0; i < static_cast<int>(kFiles.size()); ++i) {
        const RECT cell = RectDip(x, y, 214, 26);
        const RECT thumb = RectDip(x + 7, y + 5, 16, 16);
        DrawThumb(g, thumb, i, t);
        const RECT name = RectDip(x + 32, y + 4, 170, 18);
        DrawTextBlock(g, kFiles[i].name, name, 13, t.fg);
        if (vertical) {
            y += 26;
            if (y + 26 > Dip(content.bottom)) {
                y = startY;
                x += 242;
            }
        } else {
            x += 242;
            if (x + 214 > Dip(content.right)) {
                x = startX;
                y += 27;
            }
        }
    }
}

void DrawPreviewTiles(Graphics& g, const RECT& content, const Theme& t)
{
    const double startX = Dip(content.left);
    const double startY = Dip(content.top);
    double x = startX;
    double y = startY;

    for (int i = 0; i < static_cast<int>(kFiles.size()); ++i) {
        if (x + 236 > Dip(content.right) && x > startX) {
            x = startX;
            y += 74;
        }
        const RECT thumb = RectDip(x + 10, y + 9, 48, 48);
        DrawThumb(g, thumb, i, t);
        DrawTextBlock(g, kFiles[i].name, RectDip(x + 69, y + 10, 150, 18), 13, t.fg, FontStyleBold);
        DrawTextBlock(g, kFiles[i].kind, RectDip(x + 69, y + 29, 150, 16), 12, t.fg2);
        DrawTextBlock(g, kFiles[i].size, RectDip(x + 69, y + 45, 150, 16), 12, t.fg2);
        x += 244;
    }
}

void DrawPreviewDetails(Graphics& g, const RECT& content, const Theme& t)
{
    DrawTextBlock(g, L"Name", RectDip(Dip(content.left) + 8, Dip(content.top), Dip(content.right - content.left) - 326, 30), 12.5f, t.fg2, FontStyleRegular, StringAlignmentNear, StringAlignmentCenter);
    DrawTextBlock(g, L"Date modified", RectDip(Dip(content.right) - 318, Dip(content.top), 150, 30), 12.5f, t.fg2, FontStyleRegular, StringAlignmentNear, StringAlignmentCenter);
    DrawTextBlock(g, L"Type", RectDip(Dip(content.right) - 168, Dip(content.top), 96, 30), 12.5f, t.fg2, FontStyleRegular, StringAlignmentNear, StringAlignmentCenter);
    DrawTextBlock(g, L"Size", RectDip(Dip(content.right) - 72, Dip(content.top), 64, 30), 12.5f, t.fg2, FontStyleRegular, StringAlignmentFar, StringAlignmentCenter);
    SolidBrush stroke(t.stroke);
    g.FillRectangle(&stroke, RectFOf(RectDip(Dip(content.left), Dip(content.top) + 30, Dip(content.right - content.left), 1)));

    double y = Dip(content.top) + 31;
    for (int i = 0; i < static_cast<int>(kFiles.size()); ++i) {
        const RECT thumb = RectDip(Dip(content.left) + 8, y + 7, 16, 16);
        DrawThumb(g, thumb, i, t);
        DrawTextBlock(g, kFiles[i].name, RectDip(Dip(content.left) + 33, y, Dip(content.right - content.left) - 360, 30), 13, t.fg, FontStyleRegular, StringAlignmentNear, StringAlignmentCenter);
        DrawTextBlock(g, kFiles[i].date, RectDip(Dip(content.right) - 318, y, 150, 30), 13, t.fg2, FontStyleRegular, StringAlignmentNear, StringAlignmentCenter);
        DrawTextBlock(g, kFiles[i].kind, RectDip(Dip(content.right) - 168, y, 96, 30), 13, t.fg2, FontStyleRegular, StringAlignmentNear, StringAlignmentCenter);
        DrawTextBlock(g, kFiles[i].size, RectDip(Dip(content.right) - 72, y, 64, 30), 13, t.fg2, FontStyleRegular, StringAlignmentFar, StringAlignmentCenter);
        y += 30;
    }
}

void DrawExplorerPreview(Graphics& g, const RECT& frame, const Theme& t)
{
    DrawRoundedBorder(g, frame, 8, t.explorerBg, t.stroke);

    GraphicsState saved = g.Save();
    GraphicsPath clip;
    const RectF rf = RectFOf(frame);
    const REAL radius = static_cast<REAL>(Px(8));
    const REAL d = radius * 2;
    clip.AddArc(rf.X, rf.Y, d, d, 180, 90);
    clip.AddArc(rf.X + rf.Width - d, rf.Y, d, d, 270, 90);
    clip.AddArc(rf.X + rf.Width - d, rf.Y + rf.Height - d, d, d, 0, 90);
    clip.AddArc(rf.X, rf.Y + rf.Height - d, d, d, 90, 90);
    clip.CloseFigure();
    g.SetClip(&clip, CombineModeReplace);

    const double x = Dip(frame.left);
    const double y = Dip(frame.top);
    const double w = Dip(frame.right - frame.left);
    const double h = Dip(frame.bottom - frame.top);

    SolidBrush headerBrush(t.explorerHeader);
    g.FillRectangle(&headerBrush, RectFOf(RectDip(x, y, w, 44)));
    g.FillRectangle(&headerBrush, RectFOf(RectDip(x, y + h - 27, w, 27)));
    SolidBrush strokeBrush(t.stroke);
    g.FillRectangle(&strokeBrush, RectFOf(RectDip(x, y + 44, w, 1)));
    g.FillRectangle(&strokeBrush, RectFOf(RectDip(x, y + h - 28, w, 1)));

    DrawFolderIcon(g, x + 12, y + 14);
    DrawTextBlock(g, L"Pictures", RectDip(x + 37, y + 13, 70, 18), 13, t.fg2);
    DrawLine(g, x + 112, y + 17, x + 116, y + 22, t.fg2, 1.2f);
    DrawLine(g, x + 116, y + 22, x + 112, y + 27, t.fg2, 1.2f);
    DrawTextBlock(g, L"Transparent assets", RectDip(x + 124, y + 13, 160, 18), 13, t.fg);

    DrawRoundedBorder(g, g_layout.viewButton, 5, g_state.viewMenuOpen ? t.rowHover : t.ctrl, t.ctrlBorder);
    DrawGridIcon(g, Dip(g_layout.viewButton.left) + 11, Dip(g_layout.viewButton.top) + 8, t.fg);
    DrawTextBlock(g, ViewLabel(g_state.view), RectDip(Dip(g_layout.viewButton.left) + 33, Dip(g_layout.viewButton.top), 88, 30), 13, t.fg, FontStyleRegular, StringAlignmentNear, StringAlignmentCenter);
    DrawChevronDown(g, Dip(g_layout.viewButton.right) - 13, Dip(g_layout.viewButton.top) + 15, t.fg2);

    const RECT content = RectDip(x + 14, y + 58, w - 28, h - 93);
    if (g_state.view == ViewMode::Small) {
        DrawPreviewRows(g, content, t, false);
    } else if (g_state.view == ViewMode::List) {
        DrawPreviewRows(g, content, t, true);
    } else if (g_state.view == ViewMode::Tiles) {
        DrawPreviewTiles(g, content, t);
    } else if (g_state.view == ViewMode::Details) {
        DrawPreviewDetails(g, content, t);
    } else {
        DrawPreviewGrid(g, content, t);
    }

    DrawTextBlock(g, L"6 items", RectDip(x + 14, y + h - 24, 100, 18), 12, t.fg2);
    g.Restore(saved);
}

void DrawViewMenu(Graphics& g, const Theme& t)
{
    if (!g_state.viewMenuOpen) {
        return;
    }

    DrawRoundedBorder(g, g_layout.viewMenu, 8, t.menuBg, t.stroke);
    const std::array<ViewMode, 7> order = {
        ViewMode::ExtraLarge, ViewMode::Large, ViewMode::Medium, ViewMode::Small,
        ViewMode::List, ViewMode::Details, ViewMode::Tiles
    };

    for (int i = 0; i < 7; ++i) {
        const RECT item = g_layout.menuItems[i];
        if (g_hover == static_cast<Hit>(static_cast<int>(Hit::MenuExtraLarge) + i)) {
            DrawRounded(g, item, 5, t.rowHover);
        }
        if (g_state.view == order[i]) {
            DrawCheck(g, Dip(item.left) + 9, Dip(item.top) + 10, t.accent);
        }
        DrawTextBlock(g, ViewLabel(order[i]), RectDip(Dip(item.left) + 34, Dip(item.top), Dip(item.right - item.left) - 40, 34),
            13, t.fg, FontStyleRegular, StringAlignmentNear, StringAlignmentCenter);
    }
}

void DrawTitlebar(Graphics& g, const RECT& client, const Theme& t)
{
    SolidBrush titlebar(t.titlebar);
    g.FillRectangle(&titlebar, RectFOf(g_layout.titlebar));
    SolidBrush stroke(t.stroke);
    g.FillRectangle(&stroke, RectFOf(RectDip(0, 39, Dip(client.right), 1)));

    HICON titleIcon = static_cast<HICON>(LoadImageW(g_instance, MAKEINTRESOURCEW(IDI_BACKDROPPER),
        IMAGE_ICON, Px(18), Px(18), LR_DEFAULTCOLOR));
    if (titleIcon) {
        HDC hdc = g.GetHDC();
        DrawIconEx(hdc, Px(13), Px(11), titleIcon, Px(18), Px(18), 0, nullptr, DI_NORMAL);
        g.ReleaseHDC(hdc);
        DestroyIcon(titleIcon);
    } else {
        DrawAppIcon(g, 13, 11, 18, t);
    }
    DrawTextBlock(g, L"Backdropper Settings", RectDip(41, 11, 180, 20), 12.5f, t.fg);

    const bool dark = EffectiveDark();
    const Color hoverFill = dark ? Rgba(255, 255, 255, 15) : Rgba(0, 0, 0, 10);

    DrawRoundedBorder(g, g_layout.matchSystem, 5,
        g_hover == Hit::MatchSystem ? hoverFill : Color(0, 0, 0, 0),
        t.ctrlBorder);
    DrawTextBlock(g, L"Match System",
        RectDip(Dip(g_layout.matchSystem.left) + 10, Dip(g_layout.matchSystem.top), 88, 28),
        12, t.fg2, FontStyleRegular, StringAlignmentNear, StringAlignmentCenter);
    DrawSwitch(g, RectDip(Dip(g_layout.matchSystem.right) - 50, Dip(g_layout.matchSystem.top) + 4, 40, 20),
        g_state.matchSystemTheme, t);

    DrawRoundedBorder(g, g_layout.aboutBtn, 5,
        g_hover == Hit::About ? hoverFill : Color(0, 0, 0, 0),
        t.ctrlBorder);
    DrawInfoIcon(g, RectDip(Dip(g_layout.aboutBtn.left) + 10, Dip(g_layout.aboutBtn.top) + 5, 18, 18), t);
    DrawTextBlock(g, L"About",
        RectDip(Dip(g_layout.aboutBtn.left) + 31, Dip(g_layout.aboutBtn.top), 38, 28),
        12, t.fg2, FontStyleRegular, StringAlignmentNear, StringAlignmentCenter);

    if (g_state.matchSystemTheme) {
        DrawRounded(g, g_layout.theme, 5, Color(0, 0, 0, 0));
    } else {
        DrawRoundedBorder(g, g_layout.theme, 5,
            g_hover == Hit::Theme ? hoverFill : Color(0, 0, 0, 0),
            t.ctrlBorder);
    }
    DrawThemeIcon(g, g_layout.theme, t);
    DrawTextBlock(g, dark ? L"Dark" : L"Light",
        RectDip(Dip(g_layout.theme.left) + 31, Dip(g_layout.theme.top), 38, 28), 12, t.fg2,
        FontStyleRegular, StringAlignmentNear, StringAlignmentCenter);

    auto drawCap = [&](const RECT& r, Hit hit) {
        if (g_hover == hit) {
            DrawRounded(g, r, 0, hit == Hit::Close ? Rgba(196, 43, 28) : (dark ? Rgba(255, 255, 255, 15) : Rgba(0, 0, 0, 13)));
        }
    };
    drawCap(g_layout.minimize, Hit::Minimize);
    drawCap(g_layout.maximize, Hit::Maximize);
    drawCap(g_layout.close, Hit::Close);

    const Color cap = g_hover == Hit::Close ? Rgba(255, 255, 255) : t.fg2;
    DrawLine(g, Dip(g_layout.minimize.left) + 17, 20, Dip(g_layout.minimize.left) + 29, 20, t.fg2);
    DrawRectLine(g, RectDip(Dip(g_layout.maximize.left) + 18, 15, 10, 10), t.fg2);
    DrawLine(g, Dip(g_layout.close.left) + 18, 15, Dip(g_layout.close.left) + 28, 25, cap);
    DrawLine(g, Dip(g_layout.close.left) + 28, 15, Dip(g_layout.close.left) + 18, 25, cap);
}

void DrawLeftPane(Graphics& g, const Theme& t)
{
    const double leftW = Dip(g_layout.leftPane.right - g_layout.leftPane.left);
    const double cardX = 22;
    const double cardW = leftW - 44;

    DrawTextBlock(g, L"Backdropper", RectDip(22, 61, 140, 28), 21, t.fg, FontStyleBold);
    DrawTextBlock(g, L"Transparent image backgrounds", RectDip(22, 91, 280, 20), 13, t.fg2);

    double cardY = 116;
    double bgCardH = 128;
    if (g_state.settings.mode == BackdropMode::Solid) {
        bgCardH = 146;
    } else if (g_state.settings.mode == BackdropMode::Checker) {
        bgCardH = 232;
    }
    DrawRoundedBorder(g, RectDip(cardX, cardY, cardW, bgCardH), 7, t.card, t.cardBorder);
    DrawTextBlock(g, L"Background", RectDip(cardX + 19, cardY + 16, 160, 20), 14, t.fg, FontStyleBold);

    DrawRoundedBorder(g, RectDip(cardX + 19, cardY + 47, 222, 36), 6, t.ctrl, t.ctrlBorder);
    DrawSegment(g, g_layout.segNone, L"None", g_state.settings.mode == BackdropMode::None, t);
    DrawSegment(g, g_layout.segSolid, L"Solid", g_state.settings.mode == BackdropMode::Solid, t);
    DrawSegment(g, g_layout.segChecker, L"Checker", g_state.settings.mode == BackdropMode::Checker, t);

    if (g_state.settings.mode == BackdropMode::None) {
        DrawTextBlock(g, L"Thumbnails keep their full transparency.",
            RectDip(cardX + 19, cardY + 98, cardW - 38, 20), 13, t.fg2);
    } else if (g_state.settings.mode == BackdropMode::Solid) {
        DrawTextBlock(g, L"Solid color", RectDip(cardX + 19, cardY + 104, 90, 20), 13, t.fg);
        DrawRoundedBorder(g, g_layout.solidSwatch, 5, ColorFromRef(ColorFromTextOr(g_state.solidText, g_state.settings.solidColor)), t.stroke);
        DrawInputFrame(g, g_layout.solidEdit, t);
    } else {
        DrawTextBlock(g, L"Checker A", RectDip(cardX + 19, cardY + 104, 90, 20), 13, t.fg);
        DrawRoundedBorder(g, g_layout.checkerASwatch, 5, ColorFromRef(ColorFromTextOr(g_state.checkerAText, g_state.settings.checkerA)), t.stroke);
        DrawInputFrame(g, g_layout.checkerAEdit, t);

        DrawTextBlock(g, L"Checker B", RectDip(cardX + 19, cardY + 147, 90, 20), 13, t.fg);
        DrawRoundedBorder(g, g_layout.checkerBSwatch, 5, ColorFromRef(ColorFromTextOr(g_state.checkerBText, g_state.settings.checkerB)), t.stroke);
        DrawInputFrame(g, g_layout.checkerBEdit, t);

        DrawTextBlock(g, L"Checker size", RectDip(cardX + 19, cardY + 190, 90, 20), 13, t.fg);
        DrawRoundedBorder(g, g_layout.sizeBox, 5, t.ctrl, t.ctrlBorder);
        DrawInputFrame(g, g_layout.sizeBox, t);
        SolidBrush line(t.ctrlBorder);
        g.FillRectangle(&line, RectFOf(RectDip(Dip(g_layout.sizeDown.left), Dip(g_layout.sizeDown.top), 1, 30)));
        g.FillRectangle(&line, RectFOf(RectDip(Dip(g_layout.sizeUp.left), Dip(g_layout.sizeUp.top), 1, 30)));
        DrawLine(g, Dip(g_layout.sizeDown.left) + 10, Dip(g_layout.sizeDown.top) + 15, Dip(g_layout.sizeDown.left) + 20, Dip(g_layout.sizeDown.top) + 15, t.fg, 1.4f);
        DrawLine(g, Dip(g_layout.sizeUp.left) + 10, Dip(g_layout.sizeUp.top) + 15, Dip(g_layout.sizeUp.left) + 20, Dip(g_layout.sizeUp.top) + 15, t.fg, 1.4f);
        DrawLine(g, Dip(g_layout.sizeUp.left) + 15, Dip(g_layout.sizeUp.top) + 10, Dip(g_layout.sizeUp.left) + 15, Dip(g_layout.sizeUp.top) + 20, t.fg, 1.4f);
        DrawTextBlock(g, L"px", RectDip(Dip(g_layout.sizeBox.right) + 12, Dip(g_layout.sizeBox.top) + 6, 24, 18), 12.5f, t.fg2);
    }

    double nextY = cardY + bgCardH + 14;
    DrawRoundedBorder(g, RectDip(cardX, nextY, cardW, 86), 7, t.card, t.cardBorder);
    DrawTextBlock(g, L"Restart Explorer & clear cache on save", RectDip(cardX + 19, nextY + 17, cardW - 86, 20), 14, t.fg, FontStyleBold);
    DrawTextBlock(g, L"Clears thumbcache_*.db and restarts the shell on save.",
        RectDip(cardX + 19, nextY + 42, cardW - 86, 34), 12.5f, t.fg2, FontStyleRegular, StringAlignmentNear, StringAlignmentNear, true);
    DrawRoundedBorder(g, g_layout.restartToggle, 10,
        g_state.settings.deleteThumbnailDbsOnSave ? t.accent : Color(0, 0, 0, 0),
        g_state.settings.deleteThumbnailDbsOnSave ? t.accent : t.toggleOff);
    const double thumbX = Dip(g_layout.restartToggle.left) + (g_state.settings.deleteThumbnailDbsOnSave ? 22 : 4);
    DrawRounded(g, RectDip(thumbX, Dip(g_layout.restartToggle.top) + 4, 12, 12), 6,
        g_state.settings.deleteThumbnailDbsOnSave ? t.accentText : t.toggleOff);

    const double updateY = nextY + 100;
    DrawRoundedBorder(g, RectDip(cardX, updateY, cardW, 86), 7, t.card, t.cardBorder);
    DrawTextBlock(g, L"Check for updates", RectDip(cardX + 19, updateY + 17, cardW - 120, 20), 14, t.fg, FontStyleBold);
    const std::wstring status = g_state.updateStatus.empty()
        ? std::wstring(L"Current version ") + kBackdropperVersion
        : g_state.updateStatus;
    DrawTextBlock(g, status, RectDip(cardX + 19, updateY + 42, cardW - 120, 34), 12.5f, t.fg2,
        FontStyleRegular, StringAlignmentNear, StringAlignmentNear, true);
    if (g_state.updateAvailable) {
        DrawButton(g, g_layout.installUpdateBtn, L"Update", t, true, false, g_hover == Hit::InstallUpdate);
    }
    DrawButton(g, g_layout.checkUpdatesBtn, L"Check", t, false, false, g_hover == Hit::CheckUpdates);
}

void DrawRightPane(Graphics& g, const Theme& t)
{
    SolidBrush preview(t.previewWell);
    g.FillRectangle(&preview, RectFOf(g_layout.rightPane));
    SolidBrush stroke(t.stroke);
    g.FillRectangle(&stroke, RectFOf(RectDip(Dip(g_layout.rightPane.left), 40, 1, Dip(g_layout.rightPane.bottom - g_layout.rightPane.top))));

    const double x = Dip(g_layout.rightPane.left);
    DrawTextBlock(g, L"Live preview", RectDip(x + 20, 54, 180, 18), 13, t.fg, FontStyleBold);
    DrawTextBlock(g, L"Switch Explorer views to see the effect at every size", RectDip(x + 20, 75, 300, 16), 12, t.fg2);
    DrawExplorerPreview(g, g_layout.previewFrame, t);
    DrawViewMenu(g, t);
}

void DrawFooter(Graphics& g, const RECT& client, const Theme& t)
{
    const double w = Dip(client.right);
    const double y = Dip(client.bottom) - 58;
    SolidBrush footer(t.footer);
    g.FillRectangle(&footer, RectFOf(RectDip(0, y, w, 58)));
    SolidBrush stroke(t.stroke);
    g.FillRectangle(&stroke, RectFOf(RectDip(0, y, w, 1)));

    SolidBrush dot(RegistrationDot(t));
    g.FillEllipse(&dot, RectFOf(RectDip(20, y + 25, 8, 8)));
    DrawTextBlock(g, RegistrationText(), RectDip(38, y + 19, 420, 20), 12.5f, t.fg2);

    DrawButton(g, g_layout.registerBtn, L"Register formats", t, false, false, g_hover == Hit::Register);
    DrawButton(g, g_layout.unregisterBtn, L"Unregister", t, false, !g_state.registered, g_hover == Hit::Unregister);
    g.FillRectangle(&stroke, RectFOf(RectDip(Dip(g_layout.saveBtn.left) - 15, y + 17, 1, 24)));
    DrawButton(g, g_layout.saveBtn, L"Save", t, true, false, g_hover == Hit::Save);
}

void DrawDialog(Graphics& g, const RECT& client, const Theme& t)
{
    if (!DialogOpen()) {
        return;
    }

    SolidBrush overlay(Rgba(0, 0, 0, 102));
    g.FillRectangle(&overlay, RectFOf(client));

    const double w = Dip(client.right);
    const double h = Dip(client.bottom);
    const RECT dlg = RectDip((w - 360) / 2, (h - 218) / 2, 360, 218);
    DrawRoundedBorder(g, dlg, 8, t.dialogBg, t.dialogBorder);
    DrawTextBlock(g, g_state.dialogTitle, RectDip(Dip(dlg.left) + 24, Dip(dlg.top) + 22, 312, 28), 20, t.fg, FontStyleBold);
    DrawTextBlock(g, g_state.dialogBody, RectDip(Dip(dlg.left) + 24, Dip(dlg.top) + 61, 312, 86), 13.5f, t.fg2,
        FontStyleRegular, StringAlignmentNear, StringAlignmentNear, true);
    DrawButton(g, g_layout.dialogOk, L"OK", t, true, false, g_hover == Hit::DialogOk);
}

void DrawAboutDialog(Graphics& g, const RECT& client, const Theme& t)
{
    if (!AboutOpen()) {
        return;
    }

    SolidBrush overlay(Rgba(0, 0, 0, 112));
    g.FillRectangle(&overlay, RectFOf(client));

    const RECT dlg = g_layout.aboutDialog;
    const double x = Dip(dlg.left);
    const double y = Dip(dlg.top);
    const double w = Dip(dlg.right - dlg.left);
    const double h = Dip(dlg.bottom - dlg.top);

    DrawRounded(g, RectDip(x + 1, y + 11, w - 2, h), 8, Rgba(0, 0, 0, 24));
    DrawRounded(g, RectDip(x, y + 5, w, h), 8, Rgba(0, 0, 0, 16));
    DrawRoundedBorder(g, dlg, 8, t.dialogBg, t.dialogBorder);

    if (g_hover == Hit::AboutClose) {
        DrawRounded(g, g_layout.aboutClose, 4, EffectiveDark() ? Rgba(255, 255, 255, 18) : Rgba(0, 0, 0, 8));
    }
    const Color closeColor = g_hover == Hit::AboutClose ? t.fg : t.fg2;
    DrawLine(g, x + 357, y + 26, x + 365, y + 34, closeColor, 1.2f);
    DrawLine(g, x + 365, y + 26, x + 357, y + 34, closeColor, 1.2f);

    DrawAppIcon(g, x + 174, y + 57, 46, t);
    DrawTextBlock(g, L"Backdropper", RectDip(x + 64, y + 129, 266, 30), 21, t.fg,
        FontStyleBold, StringAlignmentCenter, StringAlignmentCenter);
    DrawTextBlock(g, std::wstring(L"Version ") + kBackdropperVersion, RectDip(x + 64, y + 160, 266, 20),
        12.5f, t.fg2, FontStyleRegular, StringAlignmentCenter, StringAlignmentCenter);
    DrawTextBlock(g, L"Composite transparent image thumbnails over a\nbackground so they're easy to see in File Explorer.",
        RectDip(x + 30, y + 188, 334, 52), 12.5f, t.fg2,
        FontStyleRegular, StringAlignmentCenter, StringAlignmentNear, true);

    SolidBrush divider(t.stroke);
    g.FillRectangle(&divider, RectFOf(RectDip(x + 28, y + 244, 338, 1)));
    DrawTextBlock(g, L"\x00A9 2026 Chris Johnson. All rights reserved.",
        RectDip(x + 64, y + 264, 266, 20), 12.5f, t.fg2,
        FontStyleRegular, StringAlignmentCenter, StringAlignmentCenter);

    DrawAboutActionButton(g, g_layout.aboutUpdate, L"Update", t, Hit::AboutUpdate, AboutActionIcon::Update);
    DrawAboutActionButton(g, g_layout.aboutGithub, L"GitHub", t, Hit::AboutGithub, AboutActionIcon::Github);
    DrawAboutActionButton(g, g_layout.aboutPrivacy, L"Privacy Policy", t, Hit::AboutPrivacy, AboutActionIcon::Privacy);
}

void Paint(HWND window, HDC hdc)
{
    CalculateLayout(window);
    RECT client = {};
    GetClientRect(window, &client);

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bitmap = CreateCompatibleBitmap(hdc, client.right, client.bottom);
    HGDIOBJ old = SelectObject(mem, bitmap);

    Graphics g(mem);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.SetCompositingQuality(CompositingQualityHighQuality);

    const Theme t = CurrentTheme();
    SolidBrush bg(t.bg);
    g.FillRectangle(&bg, RectFOf(client));
    DrawRectLine(g, client, t.winBorder);

    DrawTitlebar(g, client, t);
    DrawLeftPane(g, t);
    DrawRightPane(g, t);
    DrawFooter(g, client, t);
    DrawDialog(g, client, t);
    DrawAboutDialog(g, client, t);

    BitBlt(hdc, 0, 0, client.right, client.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bitmap);
    DeleteDC(mem);
}

Hit HitTest(POINT pt)
{
    if (AboutOpen()) {
        if (PtIn(g_layout.aboutClose, pt)) return Hit::AboutClose;
        if (PtIn(g_layout.aboutUpdate, pt)) return Hit::AboutUpdate;
        if (PtIn(g_layout.aboutGithub, pt)) return Hit::AboutGithub;
        if (PtIn(g_layout.aboutPrivacy, pt)) return Hit::AboutPrivacy;
        return Hit::None;
    }

    if (DialogOpen()) {
        return PtIn(g_layout.dialogOk, pt) ? Hit::DialogOk : Hit::None;
    }

    if (g_state.viewMenuOpen) {
        for (int i = 0; i < 7; ++i) {
            if (PtIn(g_layout.menuItems[i], pt)) {
                return static_cast<Hit>(static_cast<int>(Hit::MenuExtraLarge) + i);
            }
        }
        if (!PtIn(g_layout.viewMenu, pt) && !PtIn(g_layout.viewButton, pt)) {
            return Hit::None;
        }
    }

    if (PtIn(g_layout.matchSystem, pt)) return Hit::MatchSystem;
    if (!g_state.matchSystemTheme && PtIn(g_layout.theme, pt)) return Hit::Theme;
    if (PtIn(g_layout.minimize, pt)) return Hit::Minimize;
    if (PtIn(g_layout.maximize, pt)) return Hit::Maximize;
    if (PtIn(g_layout.close, pt)) return Hit::Close;
    if (PtIn(g_layout.segNone, pt)) return Hit::SegNone;
    if (PtIn(g_layout.segSolid, pt)) return Hit::SegSolid;
    if (PtIn(g_layout.segChecker, pt)) return Hit::SegChecker;
    if (PtIn(g_layout.solidSwatch, pt)) return Hit::SolidSwatch;
    if (PtIn(g_layout.checkerASwatch, pt)) return Hit::CheckerASwatch;
    if (PtIn(g_layout.checkerBSwatch, pt)) return Hit::CheckerBSwatch;
    if (PtIn(g_layout.sizeDown, pt)) return Hit::SizeDown;
    if (PtIn(g_layout.sizeUp, pt)) return Hit::SizeUp;
    if (PtIn(g_layout.restartToggle, pt)) return Hit::RestartToggle;
    if (PtIn(g_layout.installUpdateBtn, pt)) return Hit::InstallUpdate;
    if (PtIn(g_layout.checkUpdatesBtn, pt)) return Hit::CheckUpdates;
    if (PtIn(g_layout.viewButton, pt)) return Hit::ViewButton;
    if (PtIn(g_layout.aboutBtn, pt)) return Hit::About;
    if (PtIn(g_layout.registerBtn, pt)) return Hit::Register;
    if (PtIn(g_layout.unregisterBtn, pt)) return Hit::Unregister;
    if (PtIn(g_layout.saveBtn, pt)) return Hit::Save;
    return Hit::None;
}

void PickColor(HWND window, std::wstring& text, HWND edit, COLORREF current)
{
    static COLORREF customColors[16] = {};
    CHOOSECOLORW choose = {};
    choose.lStructSize = sizeof(choose);
    choose.hwndOwner = window;
    choose.Flags = CC_FULLOPEN | CC_RGBINIT;
    choose.rgbResult = ColorFromTextOr(text, current);
    choose.lpCustColors = customColors;

    if (ChooseColorW(&choose)) {
        text = FormatColor(choose.rgbResult);
        SetEditText(edit, text);
        InvalidateRect(window, nullptr, TRUE);
    }
}

void SyncStateFromEdits()
{
    if (g_state.syncingEdits) {
        return;
    }

    g_state.solidText = GetWindowTextString(g_solidEdit);
    g_state.checkerAText = GetWindowTextString(g_checkerAEdit);
    g_state.checkerBText = GetWindowTextString(g_checkerBEdit);
    g_state.sizeText = GetWindowTextString(g_sizeEdit);
    const int size = _wtoi(g_state.sizeText.c_str());
    g_state.settings.checkerSize = static_cast<UINT>(std::max(2, std::min(64, size > 0 ? size : 8)));
}

void SyncEditsFromState()
{
    SetEditText(g_solidEdit, g_state.solidText);
    SetEditText(g_checkerAEdit, g_state.checkerAText);
    SetEditText(g_checkerBEdit, g_state.checkerBText);
    wchar_t size[16] = {};
    swprintf_s(size, L"%u", g_state.settings.checkerSize);
    g_state.sizeText = size;
    SetEditText(g_sizeEdit, g_state.sizeText);
}

BackdropperSettings SettingsFromUi()
{
    SyncStateFromEdits();
    BackdropperSettings settings = g_state.settings;

    COLORREF color = 0;
    if (ParseColor(g_state.solidText.c_str(), &color)) {
        settings.solidColor = color;
    }
    if (ParseColor(g_state.checkerAText.c_str(), &color)) {
        settings.checkerA = color;
    }
    if (ParseColor(g_state.checkerBText.c_str(), &color)) {
        settings.checkerB = color;
    }

    settings.checkerSize = static_cast<UINT>(std::max(2, std::min(64, _wtoi(g_state.sizeText.c_str()))));
    if (settings.checkerSize == 0) {
        settings.checkerSize = 8;
    }
    return settings;
}

void SaveSettings(HWND window)
{
    g_state.settings = SettingsFromUi();
    if (FAILED(SaveBackdropperSettings(g_state.settings))) {
        OpenDialog(window, L"Could not save settings", L"Backdropper could not write HKCU\\Software\\Backdropper.");
        return;
    }

    if (g_state.settings.deleteThumbnailDbsOnSave) {
        SetCursor(LoadCursorW(nullptr, IDC_WAIT));
        const std::wstring cacheResult = ForceDeleteThumbcacheDbs();
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        OpenDialog(window, L"Settings saved",
            L"Settings saved to HKCU\\Software\\Backdropper. " + cacheResult);
    } else {
        OpenDialog(window, L"Settings saved",
            L"Settings saved to HKCU\\Software\\Backdropper. Existing thumbnails may keep their previous background until the thumbnail cache refreshes.");
    }
}

void CheckForUpdates(HWND window)
{
    SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    std::wstring latest;
    const bool ok = FetchLatestVersion(&latest);
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));

    if (!ok) {
        g_state.updateAvailable = false;
        g_state.latestVersion.clear();
        g_state.updateStatus = L"Could not reach GitHub Releases.";
    } else {
        const int versionCompare = CompareVersions(latest, kBackdropperVersion);
        g_state.latestVersion = latest;
        g_state.updateAvailable = versionCompare > 0;
        if (versionCompare > 0) {
            g_state.updateStatus = std::wstring(L"Version ") + latest + L" is available.";
        } else if (versionCompare < 0) {
            g_state.updateStatus = std::wstring(L"Running newer than latest release: ") + latest + L".";
        } else {
            g_state.updateStatus = std::wstring(L"Up to date. Latest release: ") + latest + L".";
        }
    }
    InvalidateRect(window, nullptr, TRUE);
}

void StepSize(HWND window, int delta)
{
    SyncStateFromEdits();
    int size = _wtoi(g_state.sizeText.c_str());
    if (size <= 0) {
        size = 8;
    }
    size = std::max(2, std::min(64, size + delta));
    g_state.settings.checkerSize = static_cast<UINT>(size);
    wchar_t buffer[16] = {};
    swprintf_s(buffer, L"%d", size);
    g_state.sizeText = buffer;
    SetEditText(g_sizeEdit, g_state.sizeText);
    InvalidateRect(window, nullptr, TRUE);
}

void ActivateHit(HWND window, Hit hit)
{
    if (AboutOpen()) {
        switch (hit) {
        case Hit::AboutClose:
            CloseAbout(window);
            break;
        case Hit::AboutUpdate:
            if (LaunchUpdater(window)) {
                DestroyWindow(window);
            }
            break;
        case Hit::AboutGithub:
            ShellExecuteW(window, L"open", kGithubUrl, nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case Hit::AboutPrivacy:
            ShellExecuteW(window, L"open", kPrivacyUrl, nullptr, nullptr, SW_SHOWNORMAL);
            break;
        default:
            break;
        }
        return;
    }

    if (DialogOpen()) {
        if (hit == Hit::DialogOk || hit == Hit::None) {
            CloseDialog(window);
        }
        return;
    }

    const bool clickedOutsideMenu = g_state.viewMenuOpen
        && hit < Hit::MenuExtraLarge
        && hit != Hit::ViewButton;
    if (clickedOutsideMenu) {
        g_state.viewMenuOpen = false;
        InvalidateRect(window, nullptr, TRUE);
        if (hit == Hit::None) {
            return;
        }
    }

    switch (hit) {
    case Hit::MatchSystem:
        if (g_state.matchSystemTheme) {
            g_state.dark = SystemUsesDarkTheme();
            g_state.matchSystemTheme = false;
        } else {
            g_state.matchSystemTheme = true;
        }
        UpdateEditBrush();
        InvalidateRect(window, nullptr, TRUE);
        break;
    case Hit::Theme:
        if (g_state.matchSystemTheme) {
            break;
        }
        g_state.dark = !g_state.dark;
        UpdateEditBrush();
        InvalidateRect(window, nullptr, TRUE);
        break;
    case Hit::Minimize:
        ShowWindow(window, SW_MINIMIZE);
        break;
    case Hit::Maximize:
        ShowWindow(window, IsZoomed(window) ? SW_RESTORE : SW_MAXIMIZE);
        ApplyWindowRegion(window);
        break;
    case Hit::Close:
        DestroyWindow(window);
        break;
    case Hit::SegNone:
        g_state.settings.mode = BackdropMode::None;
        LayoutChildWindows(window);
        InvalidateRect(window, nullptr, TRUE);
        break;
    case Hit::SegSolid:
        g_state.settings.mode = BackdropMode::Solid;
        LayoutChildWindows(window);
        InvalidateRect(window, nullptr, TRUE);
        break;
    case Hit::SegChecker:
        g_state.settings.mode = BackdropMode::Checker;
        LayoutChildWindows(window);
        InvalidateRect(window, nullptr, TRUE);
        break;
    case Hit::SolidSwatch:
        PickColor(window, g_state.solidText, g_solidEdit, g_state.settings.solidColor);
        break;
    case Hit::CheckerASwatch:
        PickColor(window, g_state.checkerAText, g_checkerAEdit, g_state.settings.checkerA);
        break;
    case Hit::CheckerBSwatch:
        PickColor(window, g_state.checkerBText, g_checkerBEdit, g_state.settings.checkerB);
        break;
    case Hit::SizeDown:
        StepSize(window, -2);
        break;
    case Hit::SizeUp:
        StepSize(window, 2);
        break;
    case Hit::RestartToggle:
        g_state.settings.deleteThumbnailDbsOnSave = !g_state.settings.deleteThumbnailDbsOnSave;
        InvalidateRect(window, nullptr, TRUE);
        break;
    case Hit::CheckUpdates:
        CheckForUpdates(window);
        break;
    case Hit::InstallUpdate:
        if (LaunchUpdater(window)) {
            DestroyWindow(window);
        }
        break;
    case Hit::ViewButton:
        g_state.viewMenuOpen = !g_state.viewMenuOpen;
        InvalidateRect(window, nullptr, TRUE);
        break;
    case Hit::About:
        OpenAbout(window);
        break;
    case Hit::Register:
        {
            const bool ok = RunRegsvr(window, false);
            g_state.registered = IsBackdropperHandlerRegistered();
            OpenDialog(window, ok ? L"Registered image handlers" : L"Registration failed",
                ok ? L"Backdropper is now the per-user Shell thumbnail handler for supported formats with WIC, native, or optional Ghostscript rendering."
                   : L"regsvr32 could not register BackdropperThumb.dll.");
        }
        break;
    case Hit::Unregister:
        if (g_state.registered) {
            const bool ok = RunRegsvr(window, true);
            g_state.registered = IsBackdropperHandlerRegistered();
            OpenDialog(window, ok ? L"Unregistered image handlers" : L"Unregister failed",
                ok ? L"The handlers were removed, previous thumbnail handlers were restored, and the thumbnail cache was cleared."
                   : L"regsvr32 could not unregister BackdropperThumb.dll.");
        }
        break;
    case Hit::Save:
        SaveSettings(window);
        break;
    case Hit::MenuExtraLarge:
    case Hit::MenuLarge:
    case Hit::MenuMedium:
    case Hit::MenuSmall:
    case Hit::MenuList:
    case Hit::MenuDetails:
    case Hit::MenuTiles: {
        const int index = static_cast<int>(hit) - static_cast<int>(Hit::MenuExtraLarge);
        const std::array<ViewMode, 7> order = {
            ViewMode::ExtraLarge, ViewMode::Large, ViewMode::Medium, ViewMode::Small,
            ViewMode::List, ViewMode::Details, ViewMode::Tiles
        };
        g_state.view = order[index];
        g_state.viewMenuOpen = false;
        InvalidateRect(window, nullptr, TRUE);
        break;
    }
    default:
        break;
    }
}

HWND CreateEdit(HWND parent, int id)
{
    return CreateWindowExW(0, WC_EDITW, L"", WS_CHILD | ES_AUTOHSCROLL,
        0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_instance, nullptr);
}

void LoadInitialState()
{
    g_state.settings = LoadBackdropperSettings();
    g_state.solidText = FormatColor(g_state.settings.solidColor);
    g_state.checkerAText = FormatColor(g_state.settings.checkerA);
    g_state.checkerBText = FormatColor(g_state.settings.checkerB);
    wchar_t size[16] = {};
    swprintf_s(size, L"%u", g_state.settings.checkerSize);
    g_state.sizeText = size;
    g_state.registered = IsBackdropperHandlerRegistered();
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_CREATE:
        LoadInitialState();
        g_solidEdit = CreateEdit(window, IdSolidColor);
        g_checkerAEdit = CreateEdit(window, IdCheckerA);
        g_checkerBEdit = CreateEdit(window, IdCheckerB);
        g_sizeEdit = CreateEdit(window, IdCheckerSize);
        ApplyDpi(window, GetDpiForWindow(window));
        SyncEditsFromState();
        LayoutChildWindows(window);
        return 0;

    case WM_SIZE:
        ApplyWindowRegion(window);
        LayoutChildWindows(window);
        InvalidateRect(window, nullptr, TRUE);
        return 0;

    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        info->ptMinTrackSize.x = Px(760);
        info->ptMinTrackSize.y = Px(620);
        return 0;
    }

    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        ScreenToClient(window, &pt);
        CalculateLayout(window);
        const Hit hit = HitTest(pt);
        if (pt.y >= 0 && pt.y < Px(40) && hit == Hit::None) {
            return HTCAPTION;
        }
        return HTCLIENT;
    }

    case WM_DPICHANGED: {
        const UINT dpi = HIWORD(wparam);
        const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(window, nullptr, suggested->left, suggested->top,
            suggested->right - suggested->left, suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        ApplyDpi(window, dpi);
        LayoutChildWindows(window);
        ApplyWindowRegion(window);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(window, &ps);
        Paint(window, hdc);
        EndPaint(window, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!g_trackingMouse) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, window, 0 };
            TrackMouseEvent(&tme);
            g_trackingMouse = true;
        }
        POINT pt = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        CalculateLayout(window);
        const Hit hit = HitTest(pt);
        if (hit != g_hover) {
            g_hover = hit;
            InvalidateRect(window, nullptr, FALSE);
        }
        SetCursor(LoadCursorW(nullptr, hit == Hit::None ? IDC_ARROW : IDC_HAND));
        return 0;
    }

    case WM_MOUSELEAVE:
        g_trackingMouse = false;
        g_hover = Hit::None;
        InvalidateRect(window, nullptr, FALSE);
        return 0;

    case WM_LBUTTONUP: {
        POINT pt = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        CalculateLayout(window);
        ActivateHit(window, HitTest(pt));
        return 0;
    }

    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE) {
            if (AboutOpen()) {
                CloseAbout(window);
            } else if (DialogOpen()) {
                CloseDialog(window);
            } else if (g_state.viewMenuOpen) {
                g_state.viewMenuOpen = false;
                InvalidateRect(window, nullptr, TRUE);
            }
            return 0;
        }
        if (wparam == VK_RETURN && DialogOpen()) {
            CloseDialog(window);
            return 0;
        }
        break;

    case WM_SETTINGCHANGE:
        if (g_state.matchSystemTheme) {
            UpdateEditBrush();
            InvalidateRect(window, nullptr, TRUE);
        }
        break;

    case WM_COMMAND:
        if (HIWORD(wparam) == EN_CHANGE) {
            SyncStateFromEdits();
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        break;

    case WM_CTLCOLOREDIT: {
        const Theme t = CurrentTheme();
        HDC hdc = reinterpret_cast<HDC>(wparam);
        SetTextColor(hdc, SolidColorRef(t.fg, RGB(26, 26, 26)));
        SetBkColor(hdc, SolidColorRef(t.ctrl, EffectiveDark() ? RGB(47, 47, 47) : RGB(255, 255, 255)));
        return reinterpret_cast<LRESULT>(g_editBrush);
    }

    case WM_DESTROY:
        if (g_editFont) {
            DeleteObject(g_editFont);
            g_editFont = nullptr;
        }
        if (g_editBrush) {
            DeleteObject(g_editBrush);
            g_editBrush = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, wchar_t*, int show)
{
    g_instance = instance;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    GdiplusStartupInput gdiplusInput;
    GdiplusStartup(&g_gdiplusToken, &gdiplusInput, nullptr);

    WNDCLASSW wc = {};
    wc.hInstance = instance;
    wc.lpszClassName = L"BackdropperSettingsWindow";
    wc.lpfnWndProc = WindowProc;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_BACKDROPPER),
        IMAGE_ICON, 256, 256, LR_DEFAULTCOLOR));
    RegisterClassW(&wc);

    RECT work = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);

    HWND window = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"Backdropper Settings",
        WS_POPUP | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
        work.left, work.top, 1060, 692, nullptr, nullptr, instance, nullptr);

    if (window) {
        ApplyDpi(window, GetDpiForWindow(window));
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        const int width = Px(1060);
        const int height = Px(692);
        const int x = static_cast<int>(work.left) + std::max<int>(0, (static_cast<int>(work.right - work.left) - width) / 2);
        const int y = static_cast<int>(work.top) + std::max<int>(0, (static_cast<int>(work.bottom - work.top) - height) / 2);
        SetWindowPos(window, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);

        BOOL value = TRUE;
        DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
        DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
        DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
        ApplyWindowRegion(window);
        ShowWindow(window, show);
        UpdateWindow(window);
    }

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_gdiplusToken) {
        GdiplusShutdown(g_gdiplusToken);
    }
    return static_cast<int>(msg.wParam);
}
