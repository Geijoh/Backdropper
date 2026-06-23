#include "format_support.h"
#include "settings.h"
#include "thumbnail_cache.h"
#include "version.h"
#include "resource.h"

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <oleacc.h>
#include <shlwapi.h>
#include <UIAutomationClient.h>
#include <UIAutomationCoreApi.h>
#include <uiautomationcore.h>
#include <urlmon.h>

#include "md4c.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace Gdiplus;

namespace {

constexpr int IdSolidColor = 200;
constexpr int IdCheckerA = 201;
constexpr int IdCheckerB = 202;
constexpr int IdCheckerSize = 203;
constexpr UINT WmUpdateCheckResult = WM_APP + 1;
constexpr UINT WmRunUpdateCheck = WM_APP + 2;
constexpr UINT WmAccessibleInvoke = WM_APP + 3;

constexpr wchar_t kThumbHandlerKey[] = L"{E357FCCD-A995-4576-B01F-234630154E96}";
constexpr wchar_t kBackdropperClsid[] = L"{7F08B58C-8D1C-44D3-9A73-AB554FF53B1D}";

#define WIDEN_TEXT2(value) L##value
#define WIDEN_TEXT(value) WIDEN_TEXT2(value)
#ifndef BACKDROPPER_VERSION
#define BACKDROPPER_VERSION "0.0.0"
#endif
constexpr wchar_t kBackdropperVersion[] = WIDEN_TEXT(BACKDROPPER_VERSION);
constexpr wchar_t kGithubUrl[] = L"https://github.com/Geijoh/Backdropper";
constexpr wchar_t kGhostscriptUrl[] = L"https://ghostscript.com/releases/gsdnld.html";
constexpr wchar_t kUpdaterExeName[] = L"BackdropperUpdater.exe";
constexpr wchar_t kLatestVersionUrl[] = L"https://github.com/Geijoh/Backdropper/releases/latest/download/backdropper-version.txt";
constexpr wchar_t kAboutDescription[] = L"Give transparent images a checkerboard or solid background so they're easy to see in File Explorer.";
constexpr wchar_t kAboutCopyright[] = L"\u00a9 2026 Chris Johnson. All rights reserved.";

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
    AutoCheckToggle,
    CheckUpdates,
    InstallUpdate,
    ViewButton,
    About,
    AboutClose,
    AboutGithub,
    AboutPrivacy,
    PrivacyClose,
    FormatManage,
    FormatPng,
    FormatWebp,
    FormatGif,
    FormatIco,
    FormatSvg,
    FormatPsd,
    FormatAi,
    FormatEps,
    FormatPdf,
    FormatAvif,
    FormatTga,
    FormatDds,
    FormatClose,
    FormatDone,
    GhostscriptLink,
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

constexpr std::array<ViewMode, 7> kViewMenuOrder = {
    ViewMode::ExtraLarge, ViewMode::Large, ViewMode::Medium, ViewMode::Small,
    ViewMode::List, ViewMode::Details, ViewMode::Tiles
};

enum class PrivacyMarkdownBlockKind {
    Heading,
    Paragraph,
    Code,
};

struct PrivacyMarkdownBlock {
    PrivacyMarkdownBlockKind kind = PrivacyMarkdownBlockKind::Paragraph;
    unsigned headingLevel = 0;
    std::wstring text;
};

enum class PrivacyTextRole {
    Heading,
    Body,
};

struct PrivacyLayoutText {
    PrivacyTextRole role = PrivacyTextRole::Body;
    std::wstring text;
    double y = 0;
    double h = 0;
    float size = 12.0f;
    int style = FontStyleRegular;
};

struct PrivacyLayoutCode {
    std::wstring text;
    double y = 0;
    double h = 0;
};

struct PrivacyLayoutCache {
    bool valid = false;
    UINT dpi = 0;
    int widthPx = 0;
    std::wstring source;
    std::vector<PrivacyLayoutText> text;
    std::vector<PrivacyLayoutCode> code;
    double contentHeight = 0;
};

struct RegistrationSummary {
    bool currentDll = false;
    size_t expected = 0;
    size_t active = 0;
};

struct UpdateCheckResult {
    bool ok = false;
    std::wstring latest;
};

constexpr std::array<size_t, kBackdropperFormatCount> kFormatsDialogOrder = {
    0, 2, 4, 6, 8, 10,
    1, 3, 5, 7, 9, 11,
};
static_assert(static_cast<int>(Hit::FormatDds) - static_cast<int>(Hit::FormatPng) + 1 == static_cast<int>(kBackdropperFormatCount));
static_assert(static_cast<int>(Hit::MenuTiles) - static_cast<int>(Hit::MenuExtraLarge) + 1 == static_cast<int>(kViewMenuOrder.size()));

enum class FluentIcon {
    Add,
    Checkmark,
    ChevronDown,
    ChevronRight,
    ChevronUp,
    Dismiss,
    Folder,
    Grid,
    Info,
    Maximize,
    Shield,
    Subtract,
    WeatherMoon,
    WeatherSunny,
};

enum class AboutActionIcon {
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
    RECT autoCheckToggle {};
    RECT formatManageBtn {};
    std::array<RECT, kBackdropperFormatCount> formatToggles {};
    RECT formatsDialog {};
    RECT formatsClose {};
    RECT formatsDone {};
    RECT ghostscriptLink {};
    RECT checkUpdatesBtn {};
    RECT installUpdateBtn {};
    RECT viewButton {};
    RECT viewMenu {};
    std::array<RECT, 7> menuItems {};
    RECT aboutBtn {};
    RECT aboutDialog {};
    RECT aboutClose {};
    RECT aboutGithub {};
    RECT aboutPrivacy {};
    RECT privacyDialog {};
    RECT privacyClose {};
    RECT privacyViewport {};
    RECT registerBtn {};
    RECT unregisterBtn {};
    RECT saveBtn {};
    RECT dialogOk {};
    RECT titlebar {};
    RECT content {};
    RECT leftPane {};
    RECT rightPane {};
    RECT previewFrame {};
    RECT leftScrollbarThumb {};
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
    RegistrationSummary registration;
    bool viewMenuOpen = false;
    ViewMode view = ViewMode::Large;
    std::wstring dialogTitle;
    std::wstring dialogBody;
    std::wstring updateStatus;
    std::wstring latestVersion;
    bool updateAvailable = false;
    bool updateCheckInProgress = false;
    bool aboutOpen = false;
    bool privacyOpen = false;
    bool formatsOpen = false;
    bool ghostscriptInstalled = false;
    std::array<bool, kBackdropperFormatCount> formatAvailable {};
    double privacyScroll = 0;
    double privacyContentHeight = 0;
    double leftScroll = 0;
    double leftContentHeight = 0;
    std::wstring privacyMarkdown;
    PrivacyLayoutCache privacyLayout;
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
Hit g_focus = Hit::None;
bool g_keyboardFocusVisible = false;
bool g_trackingMouse = false;

void LayoutChildWindows(HWND window);
void CalculateLayout(HWND window);
Hit HitTest(POINT pt);
void OpenDialog(HWND window, const std::wstring& title, const std::wstring& body);
bool DialogOpen();
bool AboutOpen();
bool PrivacyOpen();
double MeasureWrappedTextHeightDip(const std::wstring& text, double widthDip, float sizeDip, int style);
void InvalidateHoverRect(HWND window, Hit hit);
void ActivateHit(HWND window, Hit hit);

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

void AddRoundedRect(GraphicsPath& path, const RectF& r, REAL radius)
{
    const REAL d = radius * 2;
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    path.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    path.CloseFigure();
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

COLORREF SolidControlRef()
{
    return EffectiveDark() ? RGB(50, 50, 50) : RGB(247, 247, 247);
}

Theme CurrentTheme()
{
    if (EffectiveDark()) {
        return {
            Rgba(32, 32, 32),
            Rgba(255, 255, 255, 38),
            Rgba(43, 43, 43),
            Rgba(255, 255, 255, 22),
            Rgba(255, 255, 255),
            Rgba(255, 255, 255, 158),
            Rgba(43, 43, 43),
            Rgba(255, 255, 255, 22),
            Rgba(50, 50, 50),
            Rgba(255, 255, 255, 38),
            Rgba(255, 255, 255, 38),
            Rgba(43, 43, 43),
            Rgba(43, 43, 43),
            Rgba(255, 255, 255, 38),
            Rgba(33, 33, 33),
            Rgba(32, 32, 32),
            Rgba(43, 43, 43),
            Rgba(43, 43, 43),
            Rgba(255, 255, 255, 26),
            Rgba(255, 255, 255, 115),
            Rgba(0, 120, 212),
            Rgba(255, 255, 255),
        };
    }

    return {
        Rgba(243, 243, 243),
        Rgba(0, 0, 0, 33),
        Rgba(238, 240, 244),
        Rgba(0, 0, 0, 18),
        Rgba(26, 26, 26),
        Rgba(0, 0, 0, 153),
        Rgba(255, 255, 255),
        Rgba(0, 0, 0, 18),
        Rgba(0, 0, 0, 8),
        Rgba(0, 0, 0, 33),
        Rgba(0, 0, 0, 33),
        Rgba(250, 250, 250),
        Rgba(255, 255, 255),
        Rgba(0, 0, 0, 33),
        Rgba(234, 235, 238),
        Rgba(255, 255, 255),
        Rgba(247, 248, 250),
        Rgba(255, 255, 255),
        Rgba(0, 0, 0, 15),
        Rgba(0, 0, 0, 115),
        Rgba(0, 120, 212),
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

bool FileExists(const std::wstring& path)
{
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::wstring Utf8ToWide(const std::string& text)
{
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

std::wstring ReadUtf8File(const std::wstring& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return Utf8ToWide(std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()));
}

std::wstring LoadPrivacyMarkdown()
{
    const std::wstring installed = AppDirectory() + L"\\PRIVACY.md";
    if (FileExists(installed)) {
        return ReadUtf8File(installed);
    }

    wchar_t cwd[MAX_PATH] = {};
    GetCurrentDirectoryW(ARRAYSIZE(cwd), cwd);
    const std::wstring working = std::wstring(cwd) + L"\\PRIVACY.md";
    if (FileExists(working)) {
        return ReadUtf8File(working);
    }

    return L"# Privacy Policy\n\nPRIVACY.md was not found next to BackdropperSettings.exe.";
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

bool ForceUpdateMode()
{
    return GetFocus() != nullptr && (GetKeyState(VK_SHIFT) & 0x8000) != 0;
}

bool ForceUpdateVisible()
{
    return ForceUpdateMode() && !g_state.latestVersion.empty();
}

bool UpdateButtonVisible()
{
    return g_state.updateAvailable || ForceUpdateVisible();
}

bool LaunchUpdater(HWND owner, bool force)
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
        + L" --current-version " + QuoteArg(kBackdropperVersion)
        + (force ? L" --force" : L"");

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
    std::array<int, 3> parts {};
    return ParseVersion(*version, parts);
}

void RefreshFormatAvailability()
{
    g_state.ghostscriptInstalled = BackdropperHasGhostscript();
    for (size_t i = 0; i < kBackdropperFormats.size(); ++i) {
        g_state.formatAvailable[i] = CanRegisterBackdropperFormat(kBackdropperFormats[i]);
    }
}

std::wstring FormatStatus(size_t index)
{
    const wchar_t* extension = kBackdropperFormats[index];
    if (wcscmp(extension, L".eps") == 0) {
        return g_state.ghostscriptInstalled ? L"Ghostscript" : L"Needs Ghostscript";
    }
    if (wcscmp(extension, L".ai") == 0) {
        return g_state.ghostscriptInstalled ? L"PDF + legacy" : L"PDF-based only";
    }
    if (BackdropperHasBuiltInRenderer(extension)) {
        return L"Built in";
    }
    return g_state.formatAvailable[index] ? L"Windows codec" : L"Codec missing";
}

bool EffectiveFormatEnabled(size_t index)
{
    return g_state.settings.enabledFormats[index] && g_state.formatAvailable[index];
}

int FormatIndexFromHit(Hit hit)
{
    const int first = static_cast<int>(Hit::FormatPng);
    const int index = static_cast<int>(hit) - first;
    return index >= 0 && index < static_cast<int>(kBackdropperFormatCount) ? index : -1;
}

RECT HoverRect(Hit hit)
{
    const int formatIndex = FormatIndexFromHit(hit);
    if (formatIndex >= 0) {
        return g_layout.formatToggles[formatIndex];
    }

    const int menuIndex = static_cast<int>(hit) - static_cast<int>(Hit::MenuExtraLarge);
    if (menuIndex >= 0 && menuIndex < static_cast<int>(g_layout.menuItems.size())) {
        return g_layout.menuItems[menuIndex];
    }

    switch (hit) {
    case Hit::MatchSystem: return g_layout.matchSystem;
    case Hit::Theme: return g_layout.theme;
    case Hit::Minimize: return g_layout.minimize;
    case Hit::Maximize: return g_layout.maximize;
    case Hit::Close: return g_layout.close;
    case Hit::SegNone: return g_layout.segNone;
    case Hit::SegSolid: return g_layout.segSolid;
    case Hit::SegChecker: return g_layout.segChecker;
    case Hit::SolidSwatch: return g_layout.solidSwatch;
    case Hit::CheckerASwatch: return g_layout.checkerASwatch;
    case Hit::CheckerBSwatch: return g_layout.checkerBSwatch;
    case Hit::SizeDown: return g_layout.sizeDown;
    case Hit::SizeUp: return g_layout.sizeUp;
    case Hit::RestartToggle: return g_layout.restartToggle;
    case Hit::AutoCheckToggle: return g_layout.autoCheckToggle;
    case Hit::CheckUpdates: return g_layout.checkUpdatesBtn;
    case Hit::InstallUpdate: return g_layout.installUpdateBtn;
    case Hit::ViewButton: return g_layout.viewButton;
    case Hit::About: return g_layout.aboutBtn;
    case Hit::AboutClose: return g_layout.aboutClose;
    case Hit::AboutGithub: return g_layout.aboutGithub;
    case Hit::AboutPrivacy: return g_layout.aboutPrivacy;
    case Hit::PrivacyClose: return g_layout.privacyClose;
    case Hit::FormatManage: return g_layout.formatManageBtn;
    case Hit::FormatClose: return g_layout.formatsClose;
    case Hit::FormatDone: return g_layout.formatsDone;
    case Hit::GhostscriptLink: return g_layout.ghostscriptLink;
    case Hit::Register: return g_layout.registerBtn;
    case Hit::Unregister: return g_layout.unregisterBtn;
    case Hit::Save: return g_layout.saveBtn;
    case Hit::DialogOk: return g_layout.dialogOk;
    default: return {};
    }
}

std::vector<Hit> FocusableHits()
{
    if (PrivacyOpen()) {
        return { Hit::PrivacyClose };
    }
    if (g_state.formatsOpen) {
        std::vector<Hit> hits;
        hits.push_back(Hit::FormatClose);
        for (size_t i = 0; i < kBackdropperFormatCount; ++i) {
            if (g_state.formatAvailable[i]) {
                hits.push_back(static_cast<Hit>(static_cast<int>(Hit::FormatPng) + static_cast<int>(i)));
            }
        }
        if (!g_state.ghostscriptInstalled) {
            hits.push_back(Hit::GhostscriptLink);
        }
        hits.push_back(Hit::FormatDone);
        return hits;
    }
    if (AboutOpen()) {
        return { Hit::AboutClose, Hit::AboutGithub, Hit::AboutPrivacy };
    }
    if (DialogOpen()) {
        return { Hit::DialogOk };
    }

    std::vector<Hit> hits {
        Hit::MatchSystem,
        Hit::About,
    };
    if (!g_state.matchSystemTheme) {
        hits.push_back(Hit::Theme);
    }
    hits.insert(hits.end(), {
        Hit::SegNone,
        Hit::SegSolid,
        Hit::SegChecker,
    });
    if (g_state.settings.mode == BackdropMode::Solid) {
        hits.push_back(Hit::SolidSwatch);
    } else if (g_state.settings.mode == BackdropMode::Checker) {
        hits.push_back(Hit::CheckerASwatch);
        hits.push_back(Hit::CheckerBSwatch);
        hits.push_back(Hit::SizeDown);
        hits.push_back(Hit::SizeUp);
    }
    hits.insert(hits.end(), {
        Hit::FormatManage,
        Hit::RestartToggle,
        Hit::AutoCheckToggle,
        Hit::CheckUpdates,
    });
    if (UpdateButtonVisible()) {
        hits.push_back(Hit::InstallUpdate);
    }
    hits.insert(hits.end(), {
        Hit::ViewButton,
        Hit::Register,
    });
    if (g_state.registered) {
        hits.push_back(Hit::Unregister);
    }
    hits.push_back(Hit::Save);
    return hits;
}

bool FocusableContains(Hit hit, const std::vector<Hit>& hits)
{
    return std::find(hits.begin(), hits.end(), hit) != hits.end();
}

std::wstring AccessibleName(Hit hit)
{
    switch (hit) {
    case Hit::MatchSystem: return L"Match system theme";
    case Hit::Theme: return L"Theme";
    case Hit::SegNone: return L"No background";
    case Hit::SegSolid: return L"Solid background";
    case Hit::SegChecker: return L"Checker background";
    case Hit::SolidSwatch: return L"Choose solid color";
    case Hit::CheckerASwatch: return L"Choose checker color A";
    case Hit::CheckerBSwatch: return L"Choose checker color B";
    case Hit::SizeDown: return L"Decrease checker size";
    case Hit::SizeUp: return L"Increase checker size";
    case Hit::RestartToggle: return L"Restart Explorer and clear thumbnail cache on save";
    case Hit::AutoCheckToggle: return L"Check for updates automatically";
    case Hit::CheckUpdates: return L"Check for updates now";
    case Hit::InstallUpdate: return ForceUpdateVisible() ? L"Force update" : L"Install update";
    case Hit::ViewButton: return L"Preview view menu";
    case Hit::About: return L"About";
    case Hit::AboutClose: return L"Close About";
    case Hit::AboutGithub: return L"Open GitHub";
    case Hit::AboutPrivacy: return L"Open privacy policy";
    case Hit::PrivacyClose: return L"Close privacy policy";
    case Hit::FormatManage: return L"Manage supported formats";
    case Hit::FormatClose: return L"Close supported formats";
    case Hit::FormatDone: return L"Done";
    case Hit::GhostscriptLink: return L"Download Ghostscript";
    case Hit::Register: return L"Register formats";
    case Hit::Unregister: return L"Unregister formats";
    case Hit::Save: return L"Save settings";
    case Hit::DialogOk: return L"OK";
    case Hit::FormatPng:
    case Hit::FormatWebp:
    case Hit::FormatGif:
    case Hit::FormatIco:
    case Hit::FormatSvg:
    case Hit::FormatPsd:
    case Hit::FormatAi:
    case Hit::FormatEps:
    case Hit::FormatPdf:
    case Hit::FormatAvif:
    case Hit::FormatTga:
    case Hit::FormatDds: {
        const int index = FormatIndexFromHit(hit);
        return index >= 0 ? std::wstring(L"Toggle ") + BackdropperFormatLabel(kBackdropperFormats[index]) : L"Toggle format";
    }
    default: return {};
    }
}

bool HitIsToggle(Hit hit)
{
    const int formatIndex = FormatIndexFromHit(hit);
    return hit == Hit::MatchSystem
        || hit == Hit::RestartToggle
        || hit == Hit::AutoCheckToggle
        || formatIndex >= 0;
}

bool HitIsChecked(Hit hit)
{
    const int formatIndex = FormatIndexFromHit(hit);
    if (hit == Hit::MatchSystem) return g_state.matchSystemTheme;
    if (hit == Hit::RestartToggle) return g_state.settings.deleteThumbnailDbsOnSave;
    if (hit == Hit::AutoCheckToggle) return g_state.settings.checkUpdatesAutomatically;
    if (formatIndex >= 0) return EffectiveFormatEnabled(static_cast<size_t>(formatIndex));
    return false;
}

class AccessibleRoot final : public IAccessible {
public:
    explicit AccessibleRoot(HWND window) : window_(window) {}

    IFACEMETHODIMP QueryInterface(REFIID riid, void** object) override
    {
        if (!object) {
            return E_POINTER;
        }
        *object = nullptr;
        if (riid == IID_IUnknown || riid == IID_IDispatch || riid == IID_IAccessible) {
            *object = static_cast<IAccessible*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }

    IFACEMETHODIMP_(ULONG) Release() override
    {
        const ULONG refs = InterlockedDecrement(&refs_);
        if (!refs) {
            delete this;
        }
        return refs;
    }

    IFACEMETHODIMP GetTypeInfoCount(UINT* count) override
    {
        if (!count) return E_POINTER;
        *count = 0;
        return S_OK;
    }
    IFACEMETHODIMP GetTypeInfo(UINT, LCID, ITypeInfo**) override { return E_NOTIMPL; }
    IFACEMETHODIMP GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override { return E_NOTIMPL; }
    IFACEMETHODIMP Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*, UINT*) override { return E_NOTIMPL; }

    IFACEMETHODIMP get_accParent(IDispatch** parent) override
    {
        if (!parent) return E_POINTER;
        *parent = nullptr;
        return S_FALSE;
    }

    IFACEMETHODIMP get_accChildCount(long* count) override
    {
        if (!count) return E_POINTER;
        *count = static_cast<long>(FocusableHits().size());
        return S_OK;
    }

    IFACEMETHODIMP get_accChild(VARIANT, IDispatch** child) override
    {
        if (!child) return E_POINTER;
        *child = nullptr;
        return S_FALSE;
    }

    IFACEMETHODIMP get_accName(VARIANT child, BSTR* name) override
    {
        if (!name) return E_POINTER;
        const Hit hit = HitFromChild(child);
        const std::wstring value = hit == Hit::None ? L"Backdropper Settings" : AccessibleName(hit);
        *name = SysAllocString(value.c_str());
        return *name ? S_OK : E_OUTOFMEMORY;
    }

    IFACEMETHODIMP get_accValue(VARIANT, BSTR* value) override
    {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_FALSE;
    }

    IFACEMETHODIMP get_accDescription(VARIANT child, BSTR* description) override
    {
        return get_accName(child, description);
    }

    IFACEMETHODIMP get_accRole(VARIANT child, VARIANT* role) override
    {
        if (!role) return E_POINTER;
        VariantInit(role);
        role->vt = VT_I4;
        const Hit hit = HitFromChild(child);
        role->lVal = hit == Hit::None ? ROLE_SYSTEM_CLIENT
            : (HitIsToggle(hit) ? ROLE_SYSTEM_CHECKBUTTON : ROLE_SYSTEM_PUSHBUTTON);
        return S_OK;
    }

    IFACEMETHODIMP get_accState(VARIANT child, VARIANT* state) override
    {
        if (!state) return E_POINTER;
        VariantInit(state);
        state->vt = VT_I4;
        const Hit hit = HitFromChild(child);
        long value = hit == Hit::None ? 0 : STATE_SYSTEM_FOCUSABLE;
        if (hit == g_focus) value |= STATE_SYSTEM_FOCUSED;
        if (HitIsToggle(hit) && HitIsChecked(hit)) value |= STATE_SYSTEM_CHECKED;
        state->lVal = value;
        return S_OK;
    }

    IFACEMETHODIMP get_accHelp(VARIANT, BSTR* help) override
    {
        if (!help) return E_POINTER;
        *help = nullptr;
        return S_FALSE;
    }
    IFACEMETHODIMP get_accHelpTopic(BSTR* helpFile, VARIANT, long* topic) override
    {
        if (helpFile) *helpFile = nullptr;
        if (topic) *topic = 0;
        return S_FALSE;
    }
    IFACEMETHODIMP get_accKeyboardShortcut(VARIANT, BSTR* shortcut) override
    {
        if (!shortcut) return E_POINTER;
        *shortcut = SysAllocString(L"Tab");
        return *shortcut ? S_OK : E_OUTOFMEMORY;
    }

    IFACEMETHODIMP get_accFocus(VARIANT* focus) override
    {
        if (!focus) return E_POINTER;
        VariantInit(focus);
        focus->vt = VT_I4;
        focus->lVal = ChildFromHit(g_focus);
        return S_OK;
    }

    IFACEMETHODIMP get_accSelection(VARIANT* selection) override
    {
        if (!selection) return E_POINTER;
        VariantInit(selection);
        selection->vt = VT_EMPTY;
        return S_FALSE;
    }

    IFACEMETHODIMP get_accDefaultAction(VARIANT child, BSTR* action) override
    {
        if (!action) return E_POINTER;
        *action = SysAllocString(HitIsToggle(HitFromChild(child)) ? L"Toggle" : L"Press");
        return *action ? S_OK : E_OUTOFMEMORY;
    }

    IFACEMETHODIMP accSelect(long flags, VARIANT child) override
    {
        if ((flags & SELFLAG_TAKEFOCUS) == 0) {
            return S_FALSE;
        }
        const Hit hit = HitFromChild(child);
        if (hit == Hit::None) {
            return E_INVALIDARG;
        }
        g_focus = hit;
        g_keyboardFocusVisible = true;
        SetFocus(window_);
        NotifyWinEvent(EVENT_OBJECT_FOCUS, window_, OBJID_CLIENT, ChildFromHit(hit));
        InvalidateRect(window_, nullptr, FALSE);
        return S_OK;
    }

    IFACEMETHODIMP accLocation(long* left, long* top, long* width, long* height, VARIANT child) override
    {
        if (!left || !top || !width || !height) return E_POINTER;
        CalculateLayout(window_);
        RECT rect = {};
        const Hit hit = HitFromChild(child);
        if (hit == Hit::None) {
            GetClientRect(window_, &rect);
        } else {
            rect = HoverRect(hit);
        }
        POINT pt { rect.left, rect.top };
        ClientToScreen(window_, &pt);
        *left = pt.x;
        *top = pt.y;
        *width = rect.right - rect.left;
        *height = rect.bottom - rect.top;
        return S_OK;
    }

    IFACEMETHODIMP accNavigate(long, VARIANT, VARIANT* end) override
    {
        if (!end) return E_POINTER;
        VariantInit(end);
        end->vt = VT_EMPTY;
        return S_FALSE;
    }

    IFACEMETHODIMP accHitTest(long x, long y, VARIANT* child) override
    {
        if (!child) return E_POINTER;
        POINT pt { x, y };
        ScreenToClient(window_, &pt);
        CalculateLayout(window_);
        const Hit hit = HitTest(pt);
        VariantInit(child);
        child->vt = VT_I4;
        child->lVal = ChildFromHit(hit);
        return S_OK;
    }

    IFACEMETHODIMP accDoDefaultAction(VARIANT child) override
    {
        const Hit hit = HitFromChild(child);
        if (hit == Hit::None) {
            return E_INVALIDARG;
        }
        g_focus = hit;
        g_keyboardFocusVisible = true;
        ActivateHit(window_, hit);
        NotifyWinEvent(EVENT_OBJECT_FOCUS, window_, OBJID_CLIENT, ChildFromHit(hit));
        return S_OK;
    }

    IFACEMETHODIMP put_accName(VARIANT, BSTR) override { return E_NOTIMPL; }
    IFACEMETHODIMP put_accValue(VARIANT, BSTR) override { return E_NOTIMPL; }

private:
    static long ChildFromHit(Hit hit)
    {
        const std::vector<Hit> hits = FocusableHits();
        const auto found = std::find(hits.begin(), hits.end(), hit);
        return found == hits.end() ? CHILDID_SELF : static_cast<long>((found - hits.begin()) + 1);
    }

    static Hit HitFromChild(VARIANT child)
    {
        if (child.vt != VT_I4 || child.lVal == CHILDID_SELF) {
            return Hit::None;
        }
        const std::vector<Hit> hits = FocusableHits();
        const long index = child.lVal - 1;
        return index >= 0 && static_cast<size_t>(index) < hits.size()
            ? hits[static_cast<size_t>(index)]
            : Hit::None;
    }

    long refs_ = 1;
    HWND window_ = nullptr;
};

class UiaProvider final : public IRawElementProviderSimple,
                          public IRawElementProviderFragmentRoot,
                          public IRawElementProviderFragment,
                          public IInvokeProvider,
                          public IToggleProvider {
public:
    UiaProvider(HWND window, Hit hit) : window_(window), hit_(hit) {}

    IFACEMETHODIMP QueryInterface(REFIID riid, void** object) override
    {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (riid == IID_IUnknown || riid == __uuidof(IRawElementProviderSimple)) {
            *object = static_cast<IRawElementProviderSimple*>(this);
        } else if (riid == __uuidof(IRawElementProviderFragmentRoot) && hit_ == Hit::None) {
            *object = static_cast<IRawElementProviderFragmentRoot*>(this);
        } else if (riid == __uuidof(IRawElementProviderFragment)) {
            *object = static_cast<IRawElementProviderFragment*>(this);
        } else if (riid == __uuidof(IInvokeProvider) && hit_ != Hit::None && !HitIsToggle(hit_)) {
            *object = static_cast<IInvokeProvider*>(this);
        } else if (riid == __uuidof(IToggleProvider) && HitIsToggle(hit_)) {
            *object = static_cast<IToggleProvider*>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }

    IFACEMETHODIMP_(ULONG) Release() override
    {
        const ULONG refs = InterlockedDecrement(&refs_);
        if (!refs) {
            delete this;
        }
        return refs;
    }

    IFACEMETHODIMP get_ProviderOptions(ProviderOptions* value) override
    {
        if (!value) return E_POINTER;
        *value = ProviderOptions_ServerSideProvider;
        return S_OK;
    }

    IFACEMETHODIMP GetPatternProvider(PATTERNID patternId, IUnknown** value) override
    {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (hit_ == Hit::None) {
            return S_OK;
        }
        if (patternId == UIA_TogglePatternId && HitIsToggle(hit_)) {
            *value = static_cast<IToggleProvider*>(this);
        } else if (patternId == UIA_InvokePatternId && !HitIsToggle(hit_)) {
            *value = static_cast<IInvokeProvider*>(this);
        }
        if (*value) {
            AddRef();
        }
        return S_OK;
    }

    IFACEMETHODIMP GetPropertyValue(PROPERTYID propertyId, VARIANT* value) override
    {
        if (!value) return E_POINTER;
        VariantInit(value);
        if (propertyId == UIA_NamePropertyId) {
            value->vt = VT_BSTR;
            value->bstrVal = SysAllocString(hit_ == Hit::None ? L"Backdropper Settings" : AccessibleName(hit_).c_str());
            return value->bstrVal ? S_OK : E_OUTOFMEMORY;
        }
        if (propertyId == UIA_AutomationIdPropertyId) {
            value->vt = VT_BSTR;
            value->bstrVal = SysAllocString(AutomationId().c_str());
            return value->bstrVal ? S_OK : E_OUTOFMEMORY;
        }
        if (propertyId == UIA_ControlTypePropertyId) {
            value->vt = VT_I4;
            value->lVal = ControlType();
            return S_OK;
        }
        if (propertyId == UIA_IsControlElementPropertyId
            || propertyId == UIA_IsContentElementPropertyId
            || propertyId == UIA_IsKeyboardFocusablePropertyId
            || propertyId == UIA_IsEnabledPropertyId) {
            value->vt = VT_BOOL;
            value->boolVal = VARIANT_TRUE;
            return S_OK;
        }
        if (propertyId == UIA_HasKeyboardFocusPropertyId) {
            value->vt = VT_BOOL;
            value->boolVal = hit_ != Hit::None && hit_ == g_focus ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        }
        if (propertyId == UIA_ToggleToggleStatePropertyId) {
            if (HitIsToggle(hit_)) {
                value->vt = VT_I4;
                value->lVal = HitIsChecked(hit_) ? ToggleState_On : ToggleState_Off;
            }
            return S_OK;
        }
        return S_OK;
    }

    IFACEMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** value) override
    {
        if (!value) return E_POINTER;
        *value = nullptr;
        return hit_ == Hit::None ? UiaHostProviderFromHwnd(window_, value) : S_OK;
    }

    IFACEMETHODIMP Navigate(NavigateDirection direction, IRawElementProviderFragment** value) override
    {
        if (!value) return E_POINTER;
        *value = nullptr;

        const std::vector<Hit> hits = FocusableHits();
        if (hit_ == Hit::None) {
            if (direction == NavigateDirection_FirstChild && !hits.empty()) {
                *value = NewFragment(hits.front());
            } else if (direction == NavigateDirection_LastChild && !hits.empty()) {
                *value = NewFragment(hits.back());
            }
            return S_OK;
        }

        if (direction == NavigateDirection_Parent) {
            *value = NewFragment(Hit::None);
            return S_OK;
        }

        const auto found = std::find(hits.begin(), hits.end(), hit_);
        if (found == hits.end()) {
            return S_OK;
        }
        if (direction == NavigateDirection_NextSibling && found + 1 != hits.end()) {
            *value = NewFragment(*(found + 1));
        } else if (direction == NavigateDirection_PreviousSibling && found != hits.begin()) {
            *value = NewFragment(*(found - 1));
        }
        return S_OK;
    }

    IFACEMETHODIMP GetRuntimeId(SAFEARRAY** value) override
    {
        if (!value) return E_POINTER;
        const int id = hit_ == Hit::None ? 0 : static_cast<int>(hit_) + 1;
        *value = MakeRuntimeId({ UiaAppendRuntimeId, id });
        return *value ? S_OK : E_OUTOFMEMORY;
    }

    IFACEMETHODIMP get_BoundingRectangle(UiaRect* value) override
    {
        if (!value) return E_POINTER;
        CalculateLayout(window_);

        RECT rect = {};
        if (hit_ == Hit::None) {
            GetClientRect(window_, &rect);
        } else {
            rect = HoverRect(hit_);
        }

        POINT pt { rect.left, rect.top };
        ClientToScreen(window_, &pt);
        value->left = static_cast<double>(pt.x);
        value->top = static_cast<double>(pt.y);
        value->width = static_cast<double>(rect.right - rect.left);
        value->height = static_cast<double>(rect.bottom - rect.top);
        return S_OK;
    }

    IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** value) override
    {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }

    IFACEMETHODIMP SetFocus() override
    {
        if (hit_ == Hit::None) {
            ::SetFocus(window_);
            return S_OK;
        }
        g_focus = hit_;
        g_keyboardFocusVisible = true;
        ::SetFocus(window_);
        NotifyWinEvent(EVENT_OBJECT_FOCUS, window_, OBJID_CLIENT, static_cast<LONG>(hit_) + 1);
        InvalidateRect(window_, nullptr, FALSE);
        return S_OK;
    }

    IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** value) override
    {
        if (!value) return E_POINTER;
        if (hit_ == Hit::None) {
            *value = static_cast<IRawElementProviderFragmentRoot*>(this);
            AddRef();
        } else {
            *value = new UiaProvider(window_, Hit::None);
        }
        return *value ? S_OK : E_OUTOFMEMORY;
    }

    IFACEMETHODIMP ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** value) override
    {
        if (!value) return E_POINTER;
        POINT pt { static_cast<LONG>(x), static_cast<LONG>(y) };
        ScreenToClient(window_, &pt);
        CalculateLayout(window_);
        const Hit hit = HitTest(pt);
        const std::vector<Hit> hits = FocusableHits();
        *value = FocusableContains(hit, hits) ? NewFragment(hit) : NewFragment(Hit::None);
        return *value ? S_OK : E_OUTOFMEMORY;
    }

    IFACEMETHODIMP GetFocus(IRawElementProviderFragment** value) override
    {
        if (!value) return E_POINTER;
        const std::vector<Hit> hits = FocusableHits();
        *value = FocusableContains(g_focus, hits) ? NewFragment(g_focus) : nullptr;
        return S_OK;
    }

    IFACEMETHODIMP Invoke() override
    {
        return PostAccessibleInvoke();
    }

    IFACEMETHODIMP Toggle() override
    {
        return PostAccessibleInvoke();
    }

    IFACEMETHODIMP get_ToggleState(ToggleState* value) override
    {
        if (!value) return E_POINTER;
        if (!HitIsToggle(hit_)) return E_INVALIDARG;
        *value = HitIsChecked(hit_) ? ToggleState_On : ToggleState_Off;
        return S_OK;
    }

private:
    IRawElementProviderFragment* NewFragment(Hit hit) const
    {
        return static_cast<IRawElementProviderFragment*>(new UiaProvider(window_, hit));
    }

    std::wstring AutomationId() const
    {
        if (hit_ == Hit::None) {
            return L"BackdropperSettings";
        }
        return L"Backdropper." + std::to_wstring(static_cast<int>(hit_));
    }

    CONTROLTYPEID ControlType() const
    {
        if (hit_ == Hit::None) return UIA_PaneControlTypeId;
        return HitIsToggle(hit_) ? UIA_CheckBoxControlTypeId : UIA_ButtonControlTypeId;
    }

    HRESULT PostAccessibleInvoke()
    {
        if (hit_ == Hit::None) {
            return E_INVALIDARG;
        }
        return PostMessageW(window_, WmAccessibleInvoke, static_cast<WPARAM>(hit_), 0) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    }

    static SAFEARRAY* MakeRuntimeId(const std::vector<int>& values)
    {
        SAFEARRAY* array = SafeArrayCreateVector(VT_I4, 0, static_cast<ULONG>(values.size()));
        if (!array) {
            return nullptr;
        }
        for (LONG i = 0; i < static_cast<LONG>(values.size()); ++i) {
            int value = values[static_cast<size_t>(i)];
            if (FAILED(SafeArrayPutElement(array, &i, &value))) {
                SafeArrayDestroy(array);
                return nullptr;
            }
        }
        return array;
    }

    long refs_ = 1;
    HWND window_ = nullptr;
    Hit hit_ = Hit::None;
};

void EnsureKeyboardFocus(HWND window)
{
    const std::vector<Hit> hits = FocusableHits();
    if (hits.empty()) {
        g_focus = Hit::None;
        return;
    }
    if (!FocusableContains(g_focus, hits)) {
        const Hit old = g_focus;
        g_focus = hits.front();
        InvalidateHoverRect(window, old);
        InvalidateHoverRect(window, g_focus);
    }
}

void MoveKeyboardFocus(HWND window, bool reverse)
{
    g_keyboardFocusVisible = true;
    const std::vector<Hit> hits = FocusableHits();
    if (hits.empty()) {
        return;
    }

    auto current = std::find(hits.begin(), hits.end(), g_focus);
    const Hit old = g_focus;
    if (current == hits.end()) {
        g_focus = reverse ? hits.back() : hits.front();
    } else if (reverse) {
        g_focus = current == hits.begin() ? hits.back() : *(current - 1);
    } else {
        ++current;
        g_focus = current == hits.end() ? hits.front() : *current;
    }
    InvalidateHoverRect(window, old);
    InvalidateHoverRect(window, g_focus);
}

void DrawFocusRing(Graphics& g, HWND window, const Theme& t)
{
    if (!g_keyboardFocusVisible) {
        return;
    }
    EnsureKeyboardFocus(window);
    const RECT rect = HoverRect(g_focus);
    if (IsEmptyRect(rect)) {
        return;
    }
    RECT ring = rect;
    InflateRect(&ring, Px(3), Px(3));
    Pen pen(t.accent, static_cast<REAL>(std::max(1, Px(1.5))));
    pen.SetDashStyle(DashStyleDash);
    GraphicsPath path;
    AddRoundedRect(path, RectFOf(ring), static_cast<REAL>(Px(6)));
    g.DrawPath(&pen, &path);
}

void InvalidateHoverRect(HWND window, Hit hit)
{
    RECT rect = HoverRect(hit);
    if (IsEmptyRect(rect)) {
        return;
    }
    InflateRect(&rect, Px(3), Px(3));
    InvalidateRect(window, &rect, FALSE);
}

void SetHover(HWND window, Hit hit)
{
    if (hit == g_hover) {
        return;
    }
    const Hit old = g_hover;
    g_hover = hit;
    InvalidateHoverRect(window, old);
    InvalidateHoverRect(window, hit);
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

RegistrationSummary ReadRegistrationSummary(const BackdropperSettings& settings)
{
    RegistrationSummary summary;
    std::wstring inproc;
    summary.currentDll = ReadStringValue(HKEY_CURRENT_USER, ClsidInprocPath(), nullptr, &inproc)
        && _wcsicmp(inproc.c_str(), DllPath().c_str()) == 0;

    for (size_t i = 0; i < kBackdropperFormats.size(); ++i) {
        if (!settings.enabledFormats[i] || !CanRegisterBackdropperFormat(kBackdropperFormats[i])) {
            continue;
        }
        ++summary.expected;
        if (EffectiveHandlerIsBackdropper(kBackdropperFormats[i])) {
            ++summary.active;
        }
    }
    return summary;
}

bool IsBackdropperHandlerRegistered()
{
    const RegistrationSummary summary = ReadRegistrationSummary(LoadBackdropperSettings());
    return summary.currentDll && summary.active > 0;
}

void RefreshRegistrationState()
{
    g_state.registration = ReadRegistrationSummary(g_state.settings);
    g_state.registered = g_state.registration.active > 0;
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

bool PrivacyOpen()
{
    return g_state.privacyOpen;
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
    g_state.privacyOpen = false;
    LayoutChildWindows(window);
    InvalidateRect(window, nullptr, TRUE);
}

void OpenPrivacy(HWND window)
{
    g_state.privacyMarkdown = LoadPrivacyMarkdown();
    g_state.privacyScroll = 0;
    g_state.privacyContentHeight = 0;
    g_state.privacyLayout = {};
    g_state.privacyOpen = true;
    g_state.viewMenuOpen = false;
    LayoutChildWindows(window);
    InvalidateRect(window, nullptr, TRUE);
}

void ClosePrivacy(HWND window)
{
    g_state.privacyOpen = false;
    LayoutChildWindows(window);
    InvalidateRect(window, nullptr, TRUE);
}

void UpdateEditBrush()
{
    if (g_editBrush) {
        DeleteObject(g_editBrush);
    }
    g_editBrush = CreateSolidBrush(SolidControlRef());
}

void ApplyDpi(HWND window, UINT dpi)
{
    g_dpi = dpi ? dpi : 96;
    g_scale = static_cast<double>(g_dpi) / 96.0;
    g_state.privacyLayout.valid = false;

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
    constexpr double bgCardY = 116;
    double bgCardH = 128;
    if (g_state.settings.mode == BackdropMode::Solid) {
        bgCardH = 146;
    } else if (g_state.settings.mode == BackdropMode::Checker) {
        bgCardH = 232;
    }

    const double formatsYBase = bgCardY + bgCardH + 14;
    const double cacheYBase = formatsYBase + 88;
    const double registrationYBase = cacheYBase + 88;
    const double updateYBase = registrationYBase + 88;
    constexpr double updateCardH = 150;
    const double leftViewportH = Dip(g_layout.leftPane.bottom - g_layout.leftPane.top);
    g_state.leftContentHeight = updateYBase + updateCardH + 26 - 40;
    const double leftMaxScroll = std::max(0.0, g_state.leftContentHeight - leftViewportH);
    g_state.leftScroll = std::max(0.0, std::min(leftMaxScroll, g_state.leftScroll));

    const double scroll = g_state.leftScroll;
    double cardY = bgCardY - scroll;
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

    const double formatsY = cardY + 14;
    g_layout.formatManageBtn = RectDip(cardX + cardW - 19 - 78, formatsY + 21, 78, 32);

    const double cacheY = formatsY + 88;
    g_layout.restartToggle = RectDip(cardX + cardW - 19 - 40, cacheY + 27, 40, 20);
    const double registrationY = cacheY + 88;
    const double updateY = registrationY + 88;
    g_layout.autoCheckToggle = RectDip(cardX + cardW - 19 - 40, updateY + 72, 40, 20);
    g_layout.checkUpdatesBtn = RectDip(cardX + 19, updateY + 101, 96, 32);
    if (UpdateButtonVisible()) {
        const double installW = ForceUpdateVisible() ? 100 : 70;
        g_layout.installUpdateBtn = RectDip(cardX + 19 + 96 + 9, updateY + 101, installW, 32);
    } else {
        g_layout.installUpdateBtn = {};
    }

    if (leftMaxScroll > 0) {
        const double trackTop = Dip(g_layout.leftPane.top) + 58;
        const double trackBottom = Dip(g_layout.leftPane.bottom) - 10;
        const double trackH = std::max(42.0, trackBottom - trackTop);
        const double thumbH = std::max(42.0, trackH * leftViewportH / g_state.leftContentHeight);
        const double thumbY = trackTop + (trackH - thumbH) * g_state.leftScroll / leftMaxScroll;
        g_layout.leftScrollbarThumb = RectDip(leftW - 12, thumbY, 8, thumbH);
    }

    const double footerY = h - 58;
    const double saveW = 70;
    const double unregW = 96;
    const double regW = 90;
    const double saveX = w - 20 - saveW;
    const double unregX = saveX - 9 - unregW;
    const double regX = unregX - 9 - regW;
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

    g_layout.dialogOk = RectDip((w - 292) / 2, (h - 204) / 2 + 148, 292, 34);

    constexpr double aboutDialogW = 394;
    constexpr double aboutDescriptionY = 194;
    const double aboutDescriptionH = std::max(46.0, MeasureWrappedTextHeightDip(kAboutDescription, aboutDialogW - 84, 13, FontStyleRegular) + 6.0);
    const double aboutDividerY = aboutDescriptionY + aboutDescriptionH + 10;
    const double aboutCopyrightY = aboutDividerY + 22;
    const double aboutButtonY = aboutCopyrightY + 37;
    const double aboutDialogH = std::min(std::max(320.0, aboutButtonY + 34 + 17), std::max(320.0, h - 60));
    const double aboutDialogX = (w - aboutDialogW) / 2;
    const double aboutDialogY = (h - aboutDialogH) / 2;
    g_layout.aboutDialog = RectDip(aboutDialogX, aboutDialogY, aboutDialogW, aboutDialogH);
    g_layout.aboutClose = RectDip(aboutDialogX + aboutDialogW - 48, aboutDialogY + 14, 34, 34);
    g_layout.aboutGithub = RectDip(aboutDialogX + 29, aboutDialogY + aboutButtonY, 162, 34);
    g_layout.aboutPrivacy = RectDip(aboutDialogX + 203, aboutDialogY + aboutButtonY, 162, 34);

    const double privacyW = std::min(600.0, std::max(320.0, w - 60));
    const double privacyH = std::min(632.0, std::max(320.0, h - 60));
    const double privacyX = (w - privacyW) / 2;
    const double privacyY = (h - privacyH) / 2;
    g_layout.privacyDialog = RectDip(privacyX, privacyY, privacyW, privacyH);
    g_layout.privacyClose = RectDip(privacyX + privacyW - 58, privacyY + 15, 46, 40);
    g_layout.privacyViewport = RectDip(privacyX + 26, privacyY + 92, privacyW - 52, privacyH - 120);

    constexpr double formatsDialogW = 560;
    constexpr double formatsDialogH = 492;
    const double formatsDialogX = (w - formatsDialogW) / 2;
    const double formatsDialogY = (h - formatsDialogH) / 2;
    g_layout.formatsDialog = RectDip(formatsDialogX, formatsDialogY, formatsDialogW, formatsDialogH);
    g_layout.formatsClose = RectDip(formatsDialogX + formatsDialogW - 62, formatsDialogY + 22, 46, 40);
    g_layout.formatsDone = RectDip(formatsDialogX + formatsDialogW - 104, formatsDialogY + formatsDialogH - 47, 80, 32);
    const double colW = (formatsDialogW - 48 - 14) / 2;
    for (size_t position = 0; position < kFormatsDialogOrder.size(); ++position) {
        const size_t i = kFormatsDialogOrder[position];
        const double col = static_cast<double>(position % 2);
        const double row = static_cast<double>(position / 2);
        g_layout.formatToggles[i] = RectDip(formatsDialogX + 24 + col * (colW + 14), formatsDialogY + 109 + row * 51, colW, 42);
    }
    g_layout.ghostscriptLink = RectDip(formatsDialogX + 52, formatsDialogY + formatsDialogH - 27, 150, 18);
}

void MoveEdit(HWND edit, const RECT& outer)
{
    if (IsEmptyRect(outer)
        || outer.top < g_layout.leftPane.top
        || outer.bottom > g_layout.leftPane.bottom) {
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
    const bool show = !DialogOpen() && !AboutOpen() && !PrivacyOpen() && !g_state.formatsOpen;

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

    GraphicsPath path;
    AddRoundedRect(path, r, radius);
    SolidBrush brush(fill);
    g.FillPath(&brush, &path);
}

void DrawRoundedBorder(Graphics& g, const RECT& rect, float radiusDip, const Color& fill, const Color& border, float borderDip = 1)
{
    const RectF r = RectFOf(rect);
    const REAL radius = static_cast<REAL>(Px(radiusDip));

    GraphicsPath path;
    AddRoundedRect(path, r, radius);

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

float MeasureTextHeight(Graphics& g, const std::wstring& text, float widthDip, float sizeDip, int style, const wchar_t* familyName = L"Segoe UI Variable Text")
{
    FontFamily family(familyName);
    FontFamily fallback(L"Segoe UI");
    const FontFamily* selectedFamily = family.GetLastStatus() == Ok ? &family : &fallback;
    Font font(selectedFamily, static_cast<REAL>(Px(sizeDip)), style, UnitPixel);
    StringFormat format;
    format.SetTrimming(StringTrimmingNone);
    RectF bounds;
    g.MeasureString(text.c_str(), -1, &font, RectF(0, 0, static_cast<REAL>(Px(widthDip)), 10000), &format, &bounds);
    return static_cast<float>(Dip(static_cast<int>(std::ceil(bounds.Height))));
}

double MeasureWrappedTextHeightDip(const std::wstring& text, double widthDip, float sizeDip, int style)
{
    Bitmap bitmap(1, 1, PixelFormat32bppARGB);
    Graphics graphics(&bitmap);
    return MeasureTextHeight(graphics, text, static_cast<float>(widthDip), sizeDip, style);
}

void DrawTextBlockWithFamily(Graphics& g, const std::wstring& text, const RECT& rect, float sizeDip,
    const Color& color, int style, const wchar_t* familyName, bool wrap = true)
{
    FontFamily family(familyName);
    FontFamily fallback(L"Segoe UI");
    const FontFamily* selectedFamily = family.GetLastStatus() == Ok ? &family : &fallback;
    Font font(selectedFamily, static_cast<REAL>(Px(sizeDip)), style, UnitPixel);
    SolidBrush brush(color);
    StringFormat format;
    format.SetTrimming(StringTrimmingNone);
    if (!wrap) {
        format.SetFormatFlags(StringFormatFlagsNoWrap);
    }
    g.DrawString(text.c_str(), -1, &font, RectFOf(rect), &format, &brush);
}

std::vector<std::wstring> LinesOf(const std::wstring& text)
{
    std::vector<std::wstring> lines;
    std::wistringstream stream(text);
    std::wstring line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == L'\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

bool StartsWith(const std::wstring& text, const wchar_t* prefix)
{
    return text.rfind(prefix, 0) == 0;
}

std::wstring StripInlineMarkdown(std::wstring text)
{
    text.erase(std::remove(text.begin(), text.end(), L'`'), text.end());
    return text;
}

void TrimInlineWhitespace(std::wstring& text)
{
    while (!text.empty() && std::iswspace(text.front())) {
        text.erase(text.begin());
    }
    while (!text.empty() && std::iswspace(text.back())) {
        text.pop_back();
    }
}

void TrimTrailingLineBreaks(std::wstring& text)
{
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n')) {
        text.pop_back();
    }
}

struct PrivacyParseContext {
    std::vector<PrivacyMarkdownBlock> blocks;
    PrivacyMarkdownBlock current;
    bool collecting = false;
};

void FinishPrivacyBlock(PrivacyParseContext& context)
{
    if (!context.collecting) {
        return;
    }

    if (context.current.kind == PrivacyMarkdownBlockKind::Code) {
        TrimTrailingLineBreaks(context.current.text);
    } else {
        TrimInlineWhitespace(context.current.text);
    }

    if (!context.current.text.empty()) {
        context.blocks.push_back(std::move(context.current));
    }
    context.current = {};
    context.collecting = false;
}

int PrivacyEnterBlock(MD_BLOCKTYPE type, void* detail, void* userdata)
{
    PrivacyParseContext& context = *static_cast<PrivacyParseContext*>(userdata);
    if (context.collecting) {
        return 0;
    }

    if (type == MD_BLOCK_H) {
        context.current.kind = PrivacyMarkdownBlockKind::Heading;
        context.current.headingLevel = static_cast<MD_BLOCK_H_DETAIL*>(detail)->level;
        context.collecting = true;
    } else if (type == MD_BLOCK_P) {
        context.current.kind = PrivacyMarkdownBlockKind::Paragraph;
        context.collecting = true;
    } else if (type == MD_BLOCK_CODE) {
        context.current.kind = PrivacyMarkdownBlockKind::Code;
        context.collecting = true;
    }
    return 0;
}

int PrivacyLeaveBlock(MD_BLOCKTYPE type, void*, void* userdata)
{
    PrivacyParseContext& context = *static_cast<PrivacyParseContext*>(userdata);
    const bool leavingCurrent =
        (type == MD_BLOCK_H && context.current.kind == PrivacyMarkdownBlockKind::Heading)
        || (type == MD_BLOCK_P && context.current.kind == PrivacyMarkdownBlockKind::Paragraph)
        || (type == MD_BLOCK_CODE && context.current.kind == PrivacyMarkdownBlockKind::Code);
    if (leavingCurrent) {
        FinishPrivacyBlock(context);
    }
    return 0;
}

int PrivacyEnterSpan(MD_SPANTYPE, void*, void*)
{
    return 0;
}

int PrivacyLeaveSpan(MD_SPANTYPE, void*, void*)
{
    return 0;
}

int PrivacyText(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata)
{
    PrivacyParseContext& context = *static_cast<PrivacyParseContext*>(userdata);
    if (!context.collecting) {
        return 0;
    }

    if (type == MD_TEXT_NULLCHAR) {
        context.current.text += L'\xfffd';
    } else if (type == MD_TEXT_BR) {
        context.current.text += L'\n';
    } else if (type == MD_TEXT_SOFTBR && context.current.kind != PrivacyMarkdownBlockKind::Code) {
        context.current.text += L' ';
    } else if (type == MD_TEXT_SOFTBR) {
        context.current.text += L'\n';
    } else {
        context.current.text.append(text, text + size);
    }
    return 0;
}

std::vector<PrivacyMarkdownBlock> ParsePrivacyMarkdownWithMd4c(const std::wstring& markdown)
{
    PrivacyParseContext context;
    MD_PARSER parser {};
    parser.flags = MD_FLAG_COLLAPSEWHITESPACE | MD_FLAG_NOHTML;
    parser.enter_block = PrivacyEnterBlock;
    parser.leave_block = PrivacyLeaveBlock;
    parser.enter_span = PrivacyEnterSpan;
    parser.leave_span = PrivacyLeaveSpan;
    parser.text = PrivacyText;

    const int result = md_parse(markdown.c_str(), static_cast<MD_SIZE>(markdown.size()), &parser, &context);
    if (result != 0 || context.blocks.empty()) {
        return {};
    }
    return context.blocks;
}

std::vector<PrivacyMarkdownBlock> ParsePrivacyMarkdownFallback(const std::wstring& markdown)
{
    const std::vector<std::wstring> lines = LinesOf(markdown);
    std::vector<PrivacyMarkdownBlock> blocks;

    for (size_t i = 0; i < lines.size();) {
        const std::wstring& line = lines[i];
        if (line.empty()) {
            ++i;
            continue;
        }
        if (StartsWith(line, L"# ")) {
            PrivacyMarkdownBlock block;
            block.kind = PrivacyMarkdownBlockKind::Heading;
            block.headingLevel = 1;
            block.text = line.substr(2);
            blocks.push_back(std::move(block));
            ++i;
            continue;
        }
        if (StartsWith(line, L"## ")) {
            PrivacyMarkdownBlock block;
            block.kind = PrivacyMarkdownBlockKind::Heading;
            block.headingLevel = 2;
            block.text = line.substr(3);
            blocks.push_back(std::move(block));
            ++i;
            continue;
        }
        if (StartsWith(line, L"```")) {
            PrivacyMarkdownBlock block;
            block.kind = PrivacyMarkdownBlockKind::Code;
            for (++i; i < lines.size() && !StartsWith(lines[i], L"```"); ++i) {
                if (!block.text.empty()) {
                    block.text += L"\n";
                }
                block.text += lines[i];
            }
            if (i < lines.size()) {
                ++i;
            }
            blocks.push_back(std::move(block));
            continue;
        }

        PrivacyMarkdownBlock block;
        block.kind = PrivacyMarkdownBlockKind::Paragraph;
        block.text = StripInlineMarkdown(line);
        for (++i; i < lines.size() && !lines[i].empty() && !StartsWith(lines[i], L"## ") && !StartsWith(lines[i], L"```"); ++i) {
            block.text += L" " + StripInlineMarkdown(lines[i]);
        }
        blocks.push_back(std::move(block));
    }

    return blocks;
}

std::vector<PrivacyMarkdownBlock> ParsePrivacyMarkdown(const std::wstring& markdown)
{
    std::vector<PrivacyMarkdownBlock> blocks = ParsePrivacyMarkdownWithMd4c(markdown);
    if (blocks.empty()) {
        blocks = ParsePrivacyMarkdownFallback(markdown);
    }
    return blocks;
}

bool VerticallyVisible(double y, double height, const RECT& viewport)
{
    const double top = Dip(viewport.top);
    const double bottom = Dip(viewport.bottom);
    return y + height >= top && y <= bottom;
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

struct FluentSvg {
    double viewBox;
    const wchar_t* path;
};

FluentSvg FluentSvgData(FluentIcon icon)
{
    // Source: microsoft/fluentui-system-icons, MIT, assets/*/SVG/ic_fluent_*.svg.
    switch (icon) {
    case FluentIcon::Add:
        return { 20, LR"(M10 2.5C10.2761 2.5 10.5 2.72386 10.5 3V9.5H17C17.2761 9.5 17.5 9.72386 17.5 10C17.5 10.2761 17.2761 10.5 17 10.5H10.5V17C10.5 17.2761 10.2761 17.5 10 17.5C9.72386 17.5 9.5 17.2761 9.5 17V10.5H3C2.72386 10.5 2.5 10.2761 2.5 10C2.5 9.72386 2.72386 9.5 3 9.5H9.5V3C9.5 2.72386 9.72386 2.5 10 2.5Z)" };
    case FluentIcon::Checkmark:
        return { 16, LR"(M13.8639 3.65511C14.0533 3.85606 14.0439 4.17251 13.8429 4.36191L5.91309 11.8358C5.67573 12.0595 5.30311 12.0526 5.07417 11.8203L2.39384 9.09995C2.20003 8.90325 2.20237 8.58667 2.39907 8.39286C2.59578 8.19905 2.91235 8.2014 3.10616 8.3981L5.51192 10.8398L13.1571 3.63419C13.358 3.44479 13.6745 3.45416 13.8639 3.65511Z)" };
    case FluentIcon::ChevronDown:
        return { 16, LR"(M3.14645 5.64645C3.34171 5.45118 3.65829 5.45118 3.85355 5.64645L8 9.79289L12.1464 5.64645C12.3417 5.45118 12.6583 5.45118 12.8536 5.64645C13.0488 5.84171 13.0488 6.15829 12.8536 6.35355L8.35355 10.8536C8.15829 11.0488 7.84171 11.0488 7.64645 10.8536L3.14645 6.35355C2.95118 6.15829 2.95118 5.84171 3.14645 5.64645Z)" };
    case FluentIcon::ChevronRight:
        return { 16, LR"(M5.64645 3.14645C5.45118 3.34171 5.45118 3.65829 5.64645 3.85355L9.79289 8L5.64645 12.1464C5.45118 12.3417 5.45118 12.6583 5.64645 12.8536C5.84171 13.0488 6.15829 13.0488 6.35355 12.8536L10.8536 8.35355C11.0488 8.15829 11.0488 7.84171 10.8536 7.64645L6.35355 3.14645C6.15829 2.95118 5.84171 2.95118 5.64645 3.14645Z)" };
    case FluentIcon::ChevronUp:
        return { 16, LR"(M3.14645 10.3536C3.34171 10.5488 3.65829 10.5488 3.85355 10.3536L8 6.20711L12.1464 10.3536C12.3417 10.5488 12.6583 10.5488 12.8536 10.3536C13.0488 10.1583 13.0488 9.84171 12.8536 9.64645L8.35355 5.14645C8.15829 4.95118 7.84171 4.95118 7.64645 5.14645L3.14645 9.64645C2.95118 9.84171 2.95118 10.1583 3.14645 10.3536Z)" };
    case FluentIcon::Dismiss:
        return { 20, LR"(M4.08859 4.21569L4.14645 4.14645C4.32001 3.97288 4.58944 3.9536 4.78431 4.08859L4.85355 4.14645L10 9.293L15.1464 4.14645C15.32 3.97288 15.5894 3.9536 15.7843 4.08859L15.8536 4.14645C16.0271 4.32001 16.0464 4.58944 15.9114 4.78431L15.8536 4.85355L10.707 10L15.8536 15.1464C16.0271 15.32 16.0464 15.5894 15.9114 15.7843L15.8536 15.8536C15.68 16.0271 15.4106 16.0464 15.2157 15.9114L15.1464 15.8536L10 10.707L4.85355 15.8536C4.67999 16.0271 4.41056 16.0464 4.21569 15.9114L4.14645 15.8536C3.97288 15.68 3.9536 15.4106 4.08859 15.2157L4.14645 15.1464L9.293 10L4.14645 4.85355C3.97288 4.67999 3.9536 4.41056 4.08859 4.21569L4.14645 4.14645L4.08859 4.21569Z)" };
    case FluentIcon::Folder:
        return { 20, LR"(M2 5.5C2 4.11929 3.11929 3 4.5 3H6.98223C7.44636 3 7.89148 3.18437 8.21967 3.51256L9.5 4.79289L7.43934 6.85355C7.34557 6.94732 7.21839 7 7.08579 7H2V5.5ZM2 8V14.5C2 15.8807 3.11929 17 4.5 17H15.5C16.8807 17 18 15.8807 18 14.5V7.5C18 6.11929 16.8807 5 15.5 5H10.7071L8.14645 7.56066C7.86514 7.84196 7.48361 8 7.08579 8H2Z)" };
    case FluentIcon::Grid:
        return { 20, LR"(M7.5 11C8.32843 11 9 11.6716 9 12.5V16.5C9 17.3284 8.32843 18 7.5 18H3.5C2.67157 18 2 17.3284 2 16.5V12.5C2 11.6716 2.67157 11 3.5 11H7.5ZM16.5 11C17.3284 11 18 11.6716 18 12.5V16.5C18 17.3284 17.3284 18 16.5 18H12.5C11.6716 18 11 17.3284 11 16.5V12.5C11 11.6716 11.6716 11 12.5 11H16.5ZM7.5 2C8.32843 2 9 2.67157 9 3.5V7.5C9 8.32843 8.32843 9 7.5 9H3.5C2.67157 9 2 8.32843 2 7.5V3.5C2 2.67157 2.67157 2 3.5 2H7.5ZM16.5 2C17.3284 2 18 2.67157 18 3.5V7.5C18 8.32843 17.3284 9 16.5 9H12.5C11.6716 9 11 8.32843 11 7.5V3.5C11 2.67157 11.6716 2 12.5 2H16.5Z)" };
    case FluentIcon::Info:
        return { 20, LR"(M10.4921 8.91012C10.4497 8.67687 10.2456 8.49999 10.0001 8.49999C9.72397 8.49999 9.50011 8.72385 9.50011 8.99999V13.5021L9.50817 13.592C9.55051 13.8253 9.75465 14.0021 10.0001 14.0021C10.2763 14.0021 10.5001 13.7783 10.5001 13.5021V8.99999L10.4921 8.91012ZM10.7988 6.74999C10.7988 6.33578 10.463 5.99999 10.0488 5.99999C9.63461 5.99999 9.29883 6.33578 9.29883 6.74999C9.29883 7.16421 9.63461 7.49999 10.0488 7.49999C10.463 7.49999 10.7988 7.16421 10.7988 6.74999ZM18 10C18 5.58172 14.4183 2 10 2C5.58172 2 2 5.58172 2 10C2 14.4183 5.58172 18 10 18C14.4183 18 18 14.4183 18 10ZM3 10C3 6.13401 6.13401 3 10 3C13.866 3 17 6.13401 17 10C17 13.866 13.866 17 10 17C6.13401 17 3 13.866 3 10Z)" };
    case FluentIcon::Maximize:
        return { 20, LR"(M3 5C3 3.89543 3.89543 3 5 3H15C16.1046 3 17 3.89543 17 5V15C17 16.1046 16.1046 17 15 17H5C3.89543 17 3 16.1046 3 15V5ZM5 4C4.44772 4 4 4.44772 4 5V15C4 15.5523 4.44772 16 5 16H15C15.5523 16 16 15.5523 16 15V5C16 4.44772 15.5523 4 15 4H5Z)" };
    case FluentIcon::Shield:
        return { 20, LR"(M9.72265 2.08397C9.8906 1.97201 10.1094 1.97201 10.2774 2.08397C12.2155 3.3761 14.3117 4.1823 16.5707 4.50503C16.817 4.54021 17 4.75117 17 5V9.5C17 13.3913 14.693 16.2307 10.1795 17.9667C10.064 18.0111 9.93605 18.0111 9.82051 17.9667C5.30699 16.2307 3 13.3913 3 9.5V5C3 4.75117 3.18296 4.54021 3.42929 4.50503C5.68833 4.1823 7.78446 3.3761 9.72265 2.08397ZM9.59914 3.34583C7.85275 4.39606 5.98541 5.09055 4 5.42787V9.5C4 12.892 5.96795 15.3634 10 16.9632C14.0321 15.3634 16 12.892 16 9.5V5.42787C14.0146 5.09055 12.1473 4.39606 10.4009 3.34583L10 3.09715L9.59914 3.34583Z)" };
    case FluentIcon::Subtract:
        return { 20, LR"(M3 10C3 9.72386 3.22386 9.5 3.5 9.5H16.5C16.7761 9.5 17 9.72386 17 10C17 10.2761 16.7761 10.5 16.5 10.5H3.5C3.22386 10.5 3 10.2761 3 10Z)" };
    case FluentIcon::WeatherMoon:
        return { 20, LR"(M15.4932 13.4967C13.5653 16.8358 9.2957 17.9798 5.95663 16.052C5.20013 15.6152 4.54451 15.0515 4.01047 14.3887C6.8412 13.3015 8.56844 11.9681 9.60339 9.99249C10.651 7.99273 10.9395 5.83183 10.3628 3.08319C11.2605 3.20148 12.1328 3.49537 12.9378 3.96018C16.2769 5.88799 17.421 10.1576 15.4932 13.4967ZM5.45663 16.918C9.27399 19.122 14.1552 17.8141 16.3592 13.9967C18.5631 10.1793 17.2552 5.2981 13.4378 3.09415C12.3371 2.45863 11.1233 2.10173 9.88082 2.03507C9.4801 2.01357 9.17217 2.38477 9.26732 2.77462C9.95545 5.59395 9.70125 7.65076 8.71759 9.52844C7.78322 11.312 6.17301 12.559 3.16661 13.635C2.79667 13.7674 2.65251 14.2143 2.87537 14.538C3.54192 15.5059 4.41706 16.3178 5.45663 16.918Z)" };
    case FluentIcon::WeatherSunny:
        return { 20, LR"(M10 2C10.2761 2 10.5 2.22386 10.5 2.5V3.5C10.5 3.77614 10.2761 4 10 4C9.72386 4 9.5 3.77614 9.5 3.5V2.5C9.5 2.22386 9.72386 2 10 2ZM10 14C12.2091 14 14 12.2091 14 10C14 7.79086 12.2091 6 10 6C7.79086 6 6 7.79086 6 10C6 12.2091 7.79086 14 10 14ZM10 13C8.34315 13 7 11.6569 7 10C7 8.34315 8.34315 7 10 7C11.6569 7 13 8.34315 13 10C13 11.6569 11.6569 13 10 13ZM17.5 10.5C17.7761 10.5 18 10.2761 18 10C18 9.72386 17.7761 9.5 17.5 9.5H16.5C16.2239 9.5 16 9.72386 16 10C16 10.2761 16.2239 10.5 16.5 10.5H17.5ZM10 16C10.2761 16 10.5 16.2239 10.5 16.5V17.5C10.5 17.7761 10.2761 18 10 18C9.72386 18 9.5 17.7761 9.5 17.5V16.5C9.5 16.2239 9.72386 16 10 16ZM3.5 10.5C3.77614 10.5 4 10.2761 4 10C4 9.72386 3.77614 9.5 3.5 9.5H2.46289C2.18675 9.5 1.96289 9.72386 1.96289 10C1.96289 10.2761 2.18675 10.5 2.46289 10.5H3.5ZM4.14645 4.14645C4.34171 3.95118 4.65829 3.95118 4.85355 4.14645L5.85355 5.14645C6.04882 5.34171 6.04882 5.65829 5.85355 5.85355C5.65829 6.04882 5.34171 6.04882 5.14645 5.85355L4.14645 4.85355C3.95118 4.65829 3.95118 4.34171 4.14645 4.14645ZM4.85355 15.8536C4.65829 16.0488 4.34171 16.0488 4.14645 15.8536C3.95118 15.6583 3.95118 15.3417 4.14645 15.1464L5.14645 14.1464C5.34171 13.9512 5.65829 13.9512 5.85355 14.1464C6.04882 14.3417 6.04882 14.6583 5.85355 14.8536L4.85355 15.8536ZM15.8536 4.14645C15.6583 3.95118 15.3417 3.95118 15.1464 4.14645L14.1464 5.14645C13.9512 5.34171 13.9512 5.65829 14.1464 5.85355C14.3417 6.04882 14.6583 6.04882 14.8536 5.85355L15.8536 4.85355C16.0488 4.65829 16.0488 4.34171 15.8536 4.14645ZM15.1464 15.8536C15.3417 16.0488 15.6583 16.0488 15.8536 15.8536C16.0488 15.6583 16.0488 15.3417 15.8536 15.1464L14.8536 14.1464C14.6583 13.9512 14.3417 13.9512 14.1464 14.1464C13.9512 14.3417 13.9512 14.6583 14.1464 14.8536L15.1464 15.8536Z)" };
    }
    return { 20, L"" };
}

bool IsSvgCommand(wchar_t ch)
{
    return (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z');
}

void SkipSvgSeparators(const wchar_t*& cursor)
{
    while (*cursor == L',' || iswspace(*cursor)) {
        ++cursor;
    }
}

bool ReadSvgNumber(const wchar_t*& cursor, double& value)
{
    SkipSvgSeparators(cursor);
    if (!*cursor || IsSvgCommand(*cursor)) {
        return false;
    }
    wchar_t* end = nullptr;
    value = wcstod(cursor, &end);
    if (end == cursor) {
        return false;
    }
    cursor = end;
    return true;
}

void DrawFluentIcon(Graphics& g, const RECT& rect, FluentIcon icon, const Color& color)
{
    const FluentSvg svg = FluentSvgData(icon);
    const double boxW = Dip(rect.right - rect.left);
    const double boxH = Dip(rect.bottom - rect.top);
    const double size = std::min(boxW, boxH);
    const double x = Dip(rect.left) + (boxW - size) / 2;
    const double y = Dip(rect.top) + (boxH - size) / 2;
    const double scale = size / svg.viewBox;

    auto point = [&](double px, double py) {
        return PointF(static_cast<REAL>(Px(x + px * scale)), static_cast<REAL>(Px(y + py * scale)));
    };

    GraphicsPath path(FillModeWinding);
    const wchar_t* cursor = svg.path;
    wchar_t command = 0;
    double cx = 0;
    double cy = 0;
    double sx = 0;
    double sy = 0;
    bool havePoint = false;

    while (*cursor) {
        SkipSvgSeparators(cursor);
        if (IsSvgCommand(*cursor)) {
            command = *cursor++;
        }
        const bool relative = command >= L'a' && command <= L'z';
        switch (towupper(command)) {
        case L'M': {
            double px = 0;
            double py = 0;
            if (!ReadSvgNumber(cursor, px) || !ReadSvgNumber(cursor, py)) {
                cursor += wcslen(cursor);
                break;
            }
            if (relative && havePoint) {
                px += cx;
                py += cy;
            }
            cx = sx = px;
            cy = sy = py;
            havePoint = true;
            command = relative ? L'l' : L'L';
            break;
        }
        case L'L': {
            double px = 0;
            double py = 0;
            while (ReadSvgNumber(cursor, px)) {
                if (!ReadSvgNumber(cursor, py)) {
                    return;
                }
                if (relative) {
                    px += cx;
                    py += cy;
                }
                path.AddLine(point(cx, cy), point(px, py));
                cx = px;
                cy = py;
            }
            break;
        }
        case L'H': {
            double px = 0;
            while (ReadSvgNumber(cursor, px)) {
                if (relative) {
                    px += cx;
                }
                path.AddLine(point(cx, cy), point(px, cy));
                cx = px;
            }
            break;
        }
        case L'V': {
            double py = 0;
            while (ReadSvgNumber(cursor, py)) {
                if (relative) {
                    py += cy;
                }
                path.AddLine(point(cx, cy), point(cx, py));
                cy = py;
            }
            break;
        }
        case L'C': {
            double x1 = 0, y1 = 0, x2 = 0, y2 = 0, x3 = 0, y3 = 0;
            while (ReadSvgNumber(cursor, x1)) {
                if (!ReadSvgNumber(cursor, y1) || !ReadSvgNumber(cursor, x2) || !ReadSvgNumber(cursor, y2)
                    || !ReadSvgNumber(cursor, x3) || !ReadSvgNumber(cursor, y3)) {
                    return;
                }
                if (relative) {
                    x1 += cx; y1 += cy; x2 += cx; y2 += cy; x3 += cx; y3 += cy;
                }
                path.AddBezier(point(cx, cy), point(x1, y1), point(x2, y2), point(x3, y3));
                cx = x3;
                cy = y3;
            }
            break;
        }
        case L'Z':
            path.CloseFigure();
            cx = sx;
            cy = sy;
            command = 0;
            break;
        default:
            ++cursor;
            break;
        }
    }

    SolidBrush brush(color);
    g.FillPath(&brush, &path);
}

void DrawChevronDown(Graphics& g, double cx, double cy, const Color& color)
{
    DrawFluentIcon(g, RectDip(cx - 8, cy - 8, 16, 16), FluentIcon::ChevronDown, color);
}

void DrawCheck(Graphics& g, double x, double y, const Color& color)
{
    DrawFluentIcon(g, RectDip(x - 2, y - 3, 16, 16), FluentIcon::Checkmark, color);
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
    DrawFluentIcon(g, RectDip(x, y, 15, 15), FluentIcon::Folder, Rgba(232, 178, 58));
}

void DrawGridIcon(Graphics& g, double x, double y, const Color& color)
{
    DrawFluentIcon(g, RectDip(x, y - 1, 18, 18), FluentIcon::Grid, color);
}

void DrawThemeIcon(Graphics& g, const RECT& rect, const Theme& t)
{
    DrawFluentIcon(g, RectDip(Dip(rect.left) + 8, Dip(rect.top) + 5, 18, 18),
        EffectiveDark() ? FluentIcon::WeatherSunny : FluentIcon::WeatherMoon, t.fg2);
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
    if (!primary && !disabled) {
        const RECT bottom = { rect.left + Px(1), rect.bottom - Px(1), rect.right - Px(1), rect.bottom };
        SolidBrush brush(t.ctrlBottom);
        g.FillRectangle(&brush, RectFOf(bottom));
    }
    DrawTextBlock(g, text, rect, 13, fg, primary ? FontStyleBold : FontStyleRegular,
        StringAlignmentCenter, StringAlignmentCenter);
}

std::wstring RegistrationText()
{
    const RegistrationSummary& r = g_state.registration;
    if (!r.currentDll && r.active > 0) {
        return L"Registered to a different Backdropper build";
    }
    if (r.expected == 0) {
        return L"No available formats selected";
    }
    if (r.currentDll && r.active == r.expected) {
        return L"Registered for all selected formats";
    }
    if (r.currentDll && r.active > 0) {
        return L"Partially registered \u2014 " + std::to_wstring(r.active) + L" of " + std::to_wstring(r.expected) + L" formats active";
    }
    return L"Not registered \u2014 using default handlers";
}

Color RegistrationDot(const Theme& t)
{
    const RegistrationSummary& r = g_state.registration;
    if (r.currentDll && r.expected > 0 && r.active == r.expected) {
        return EffectiveDark() ? Rgba(108, 203, 95) : Rgba(15, 123, 15);
    }
    if (r.active > 0) {
        return Rgba(216, 145, 32);
    }
    return t.toggleOff;
}

size_t EnabledFormatCount()
{
    size_t count = 0;
    for (size_t i = 0; i < kBackdropperFormatCount; ++i) {
        if (EffectiveFormatEnabled(i)) {
            ++count;
        }
    }
    return count;
}

void DrawSwitch(Graphics& g, const RECT& rect, bool on, const Theme& t)
{
    DrawRoundedBorder(g, rect, 10, on ? t.accent : Color(0, 0, 0, 0), on ? t.accent : t.toggleOff);
    const double thumbX = Dip(rect.left) + (on ? 22 : 4);
    DrawRounded(g, RectDip(thumbX, Dip(rect.top) + 4, 12, 12), 6, on ? t.accentText : t.toggleOff);
}

void DrawFormatSwitch(Graphics& g, const RECT& rect, bool on, bool enabled, const Theme& t)
{
    const Color track = enabled ? (on ? t.accent : Color(0, 0, 0, 0)) : Color(70, t.toggleOff.GetR(), t.toggleOff.GetG(), t.toggleOff.GetB());
    const Color border = enabled ? (on ? t.accent : t.toggleOff) : Color(90, t.toggleOff.GetR(), t.toggleOff.GetG(), t.toggleOff.GetB());
    DrawRoundedBorder(g, rect, 10, track, border);
    const double thumbX = Dip(rect.left) + (on ? 22 : 4);
    const Color thumb = enabled ? (on ? t.accentText : t.toggleOff) : Color(110, t.toggleOff.GetR(), t.toggleOff.GetG(), t.toggleOff.GetB());
    DrawRounded(g, RectDip(thumbX, Dip(rect.top) + 4, 12, 12), 6, thumb);
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
    DrawFluentIcon(g, rect, FluentIcon::Info, t.fg2);
}

void DrawShieldIcon(Graphics& g, const RECT& rect, const Theme& t)
{
    DrawFluentIcon(g, rect, FluentIcon::Shield, t.fg);
}

void DrawAboutActionButton(Graphics& g, const RECT& rect, const std::wstring& text, const Theme& t, Hit hit, AboutActionIcon iconKind)
{
    const bool hovered = g_hover == hit;
    DrawRoundedBorder(g, rect, 5,
        hovered ? (EffectiveDark() ? Rgba(255, 255, 255, 20) : Rgba(0, 0, 0, 10)) : t.ctrl,
        t.ctrlBorder);

    const double groupW = iconKind == AboutActionIcon::Privacy ? 122 : 88;
    const double iconSize = 18;
    const double x = Dip(rect.left) + (Dip(rect.right - rect.left) - groupW) / 2;
    const double y = Dip(rect.top) + (Dip(rect.bottom - rect.top) - iconSize) / 2;
    const RECT icon = RectDip(x, y, iconSize, iconSize);
    if (iconKind == AboutActionIcon::Github) {
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

int ViewIndex(ViewMode view)
{
    for (int i = 0; i < static_cast<int>(kViewMenuOrder.size()); ++i) {
        if (kViewMenuOrder[i] == view) {
            return i;
        }
    }
    return 1;
}

void SetPreviewView(HWND window, ViewMode view)
{
    g_state.view = view;
    g_state.viewMenuOpen = false;
    InvalidateRect(window, nullptr, TRUE);
}

void StepPreviewView(HWND window, int delta)
{
    const int index = ViewIndex(g_state.view);
    const int next = std::max(0, std::min(static_cast<int>(kViewMenuOrder.size()) - 1, index + delta));
    SetPreviewView(window, kViewMenuOrder[next]);
}

bool PreviewShortcutsEnabled()
{
    return !DialogOpen() && !AboutOpen() && !PrivacyOpen() && !g_state.formatsOpen;
}

bool HandlePreviewKeyboardShortcut(HWND window, WPARAM key)
{
    if (!PreviewShortcutsEnabled() || !(GetKeyState(VK_CONTROL) & 0x8000)) {
        return false;
    }

    if ((GetKeyState(VK_SHIFT) & 0x8000) && key >= L'1' && key <= L'7') {
        SetPreviewView(window, kViewMenuOrder[static_cast<size_t>(key - L'1')]);
        return true;
    }

    switch (key) {
    case VK_OEM_PLUS:
    case VK_ADD:
        StepPreviewView(window, -1);
        return true;
    case VK_OEM_MINUS:
    case VK_SUBTRACT:
        StepPreviewView(window, 1);
        return true;
    default:
        return false;
    }
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
    AddRoundedRect(clip, rf, radius);
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
    DrawFluentIcon(g, RectDip(x + 108, y + 14, 16, 16), FluentIcon::ChevronRight, t.fg2);
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

    const double x = Dip(g_layout.viewMenu.left);
    const double y = Dip(g_layout.viewMenu.top);
    const double w = Dip(g_layout.viewMenu.right - g_layout.viewMenu.left);
    const double h = Dip(g_layout.viewMenu.bottom - g_layout.viewMenu.top);
    DrawRounded(g, RectDip(x + 1, y + 8, w - 2, h), 8, EffectiveDark() ? Rgba(0, 0, 0, 115) : Rgba(0, 0, 0, 28));
    DrawRounded(g, RectDip(x, y + 3, w, h), 8, EffectiveDark() ? Rgba(0, 0, 0, 77) : Rgba(0, 0, 0, 12));
    DrawRoundedBorder(g, g_layout.viewMenu, 8, t.menuBg, t.stroke);
    for (int i = 0; i < 7; ++i) {
        const RECT item = g_layout.menuItems[i];
        if (g_hover == static_cast<Hit>(static_cast<int>(Hit::MenuExtraLarge) + i)) {
            DrawRounded(g, item, 5, t.rowHover);
        }
        if (g_state.view == kViewMenuOrder[i]) {
            DrawCheck(g, Dip(item.left) + 9, Dip(item.top) + 10, t.accent);
        }
        DrawTextBlock(g, ViewLabel(kViewMenuOrder[i]), RectDip(Dip(item.left) + 34, Dip(item.top), Dip(item.right - item.left) - 40, 34),
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
    DrawInfoIcon(g, RectDip(Dip(g_layout.aboutBtn.left) + 11, Dip(g_layout.aboutBtn.top) + 7, 14, 14), t);
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
    DrawFluentIcon(g, RectDip(Dip(g_layout.minimize.left) + 14, 10, 20, 20), FluentIcon::Subtract, t.fg2);
    DrawFluentIcon(g, RectDip(Dip(g_layout.maximize.left) + 14, 10, 20, 20), FluentIcon::Maximize, t.fg2);
    DrawFluentIcon(g, RectDip(Dip(g_layout.close.left) + 14, 10, 20, 20), FluentIcon::Dismiss, cap);
}

void DrawLeftScrollbar(Graphics& g, const Theme& t)
{
    if (IsEmptyRect(g_layout.leftScrollbarThumb)) {
        return;
    }

    const Color scrollColor = EffectiveDark() ? Rgba(255, 255, 255, 115) : Rgba(0, 0, 0, 95);
    const double leftW = Dip(g_layout.leftPane.right - g_layout.leftPane.left);
    const double top = Dip(g_layout.leftPane.top) + 58;
    const double bottom = Dip(g_layout.leftPane.bottom) - 10;
    DrawFluentIcon(g, RectDip(leftW - 17, top - 19, 16, 16), FluentIcon::ChevronUp, scrollColor);
    DrawRounded(g, g_layout.leftScrollbarThumb, 4, scrollColor);
    DrawFluentIcon(g, RectDip(leftW - 17, bottom + 3, 16, 16), FluentIcon::ChevronDown, scrollColor);
}

void DrawLeftPane(Graphics& g, const Theme& t)
{
    const double leftW = Dip(g_layout.leftPane.right - g_layout.leftPane.left);
    const double cardX = 22;
    const double cardW = leftW - 44;
    const double scroll = g_state.leftScroll;

    Region oldClip;
    g.GetClip(&oldClip);
    g.SetClip(RectFOf(g_layout.leftPane), CombineModeReplace);

    DrawTextBlock(g, L"Backdropper", RectDip(22, 61 - scroll, 140, 28), 21, t.fg, FontStyleBold);
    DrawTextBlock(g, L"Transparent image backgrounds", RectDip(22, 91 - scroll, 280, 20), 13, t.fg2);

    double cardY = 116 - scroll;
    double bgCardH = 128;
    if (g_state.settings.mode == BackdropMode::Solid) {
        bgCardH = 146;
    } else if (g_state.settings.mode == BackdropMode::Checker) {
        bgCardH = 232;
    }
    DrawRoundedBorder(g, RectDip(cardX, cardY, cardW, bgCardH), 7, t.card, t.cardBorder);
    DrawTextBlock(g, L"Background", RectDip(cardX + 19, cardY + 16, 160, 20), 14, t.fg, FontStyleBold);

    DrawRoundedBorder(g, RectDip(cardX + 19, cardY + 47, 222, 34), 6, t.ctrl, t.ctrlBorder);
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
        DrawFluentIcon(g, RectDip(Dip(g_layout.sizeDown.left) + 5, Dip(g_layout.sizeDown.top) + 5, 20, 20), FluentIcon::Subtract, t.fg);
        DrawFluentIcon(g, RectDip(Dip(g_layout.sizeUp.left) + 5, Dip(g_layout.sizeUp.top) + 5, 20, 20), FluentIcon::Add, t.fg);
        DrawTextBlock(g, L"px", RectDip(Dip(g_layout.sizeBox.right) + 12, Dip(g_layout.sizeBox.top) + 6, 24, 18), 12.5f, t.fg2);
    }

    const double formatsY = cardY + bgCardH + 14;
    DrawRoundedBorder(g, RectDip(cardX, formatsY, cardW, 74), 7, t.card, t.cardBorder);
    DrawTextBlock(g, L"Supported formats", RectDip(cardX + 19, formatsY + 13, cardW - 120, 20), 14, t.fg, FontStyleBold);
    std::wstring formatSummary = std::to_wstring(EnabledFormatCount()) + L" formats enabled";
    if (!g_state.ghostscriptInstalled) {
        formatSummary += L" \u00b7 Ghostscript not installed";
    }
    DrawTextBlock(g, formatSummary, RectDip(cardX + 19, formatsY + 36, cardW - 130, 22), 12.5f, t.fg2,
        FontStyleRegular, StringAlignmentNear, StringAlignmentNear, true);
    DrawButton(g, g_layout.formatManageBtn, L"Manage", t, false, false, g_hover == Hit::FormatManage);

    double nextY = formatsY + 88;
    DrawRoundedBorder(g, RectDip(cardX, nextY, cardW, 74), 7, t.card, t.cardBorder);
    DrawTextBlock(g, L"Restart Explorer & clear cache on save", RectDip(cardX + 19, nextY + 13, cardW - 86, 20), 14, t.fg, FontStyleBold);
    DrawTextBlock(g, L"Clears thumbcache_*.db and restarts the shell on save.",
        RectDip(cardX + 19, nextY + 36, cardW - 86, 28), 12.5f, t.fg2, FontStyleRegular, StringAlignmentNear, StringAlignmentNear, true);
    DrawSwitch(g, g_layout.restartToggle, g_state.settings.deleteThumbnailDbsOnSave, t);

    const double registrationY = nextY + 88;
    DrawRoundedBorder(g, RectDip(cardX, registrationY, cardW, 74), 7, t.card, t.cardBorder);
    SolidBrush regDot(RegistrationDot(t));
    g.FillEllipse(&regDot, RectFOf(RectDip(cardX + 19, registrationY + 32, 9, 9)));
    DrawTextBlock(g, L"Thumbnail handler", RectDip(cardX + 39, registrationY + 18, cardW - 58, 18), 14, t.fg, FontStyleBold);
    DrawTextBlock(g, RegistrationText(), RectDip(cardX + 39, registrationY + 39, cardW - 58, 20), 12.5f, t.fg2);

    const double updateY = registrationY + 88;
    DrawRoundedBorder(g, RectDip(cardX, updateY, cardW, 150), 7, t.card, t.cardBorder);
    DrawTextBlock(g, L"Updates", RectDip(cardX + 19, updateY + 16, cardW - 38, 20), 14, t.fg, FontStyleBold);
    const std::wstring status = g_state.updateStatus.empty()
        ? L"Last checked moments ago"
        : g_state.updateStatus;
    DrawTextBlock(g, status, RectDip(cardX + 19, updateY + 39, cardW - 38, 24), 12.5f, t.fg2,
        FontStyleRegular, StringAlignmentNear, StringAlignmentNear, true);
    DrawTextBlock(g, L"Check automatically", RectDip(cardX + 19, updateY + 72, cardW - 78, 20), 13, t.fg);
    DrawSwitch(g, g_layout.autoCheckToggle, g_state.settings.checkUpdatesAutomatically, t);
    if (UpdateButtonVisible()) {
        DrawButton(g, g_layout.installUpdateBtn, ForceUpdateVisible() ? L"Force Update" : L"Update", t, true, false, g_hover == Hit::InstallUpdate);
    }
    DrawButton(g, g_layout.checkUpdatesBtn, L"Check now", t, false, false, g_hover == Hit::CheckUpdates);
    DrawTextBlock(g, std::wstring(L"v") + kBackdropperVersion,
        RectDip(cardX + cardW - 110, updateY + 108, 91, 18), 11.5f, t.fg2,
        FontStyleRegular, StringAlignmentFar, StringAlignmentCenter);

    g.SetClip(&oldClip, CombineModeReplace);
    DrawLeftScrollbar(g, t);
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

    DrawButton(g, g_layout.registerBtn, L"Register", t, false, false, g_hover == Hit::Register);
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
    const RECT dlg = RectDip((w - 340) / 2, (h - 204) / 2, 340, 204);
    const double x = Dip(dlg.left);
    const double y = Dip(dlg.top);
    DrawRounded(g, RectDip(x + 1, y + 11, 338, 204), 8, Rgba(0, 0, 0, 24));
    DrawRounded(g, RectDip(x, y + 5, 340, 204), 8, Rgba(0, 0, 0, 16));
    DrawRoundedBorder(g, dlg, 8, t.dialogBg, t.dialogBorder);
    DrawTextBlock(g, g_state.dialogTitle, RectDip(x + 24, y + 22, 292, 24), 18, t.fg, FontStyleBold);
    DrawTextBlock(g, g_state.dialogBody, RectDip(x + 24, y + 57, 292, 74), 13, t.fg2,
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
    DrawFluentIcon(g, RectDip(Dip(g_layout.aboutClose.left) + 6, Dip(g_layout.aboutClose.top) + 6, 22, 22), FluentIcon::Dismiss, closeColor);

    DrawAppIcon(g, x + (w - 56) / 2, y + 56, 56, t);
    DrawTextBlock(g, L"Backdropper", RectDip(x + 30, y + 130, w - 60, 30), 22, t.fg,
        FontStyleBold, StringAlignmentCenter, StringAlignmentCenter);
    DrawTextBlock(g, std::wstring(L"Version ") + kBackdropperVersion, RectDip(x + 30, y + 161, w - 60, 18),
        12, t.fg2, FontStyleRegular, StringAlignmentCenter, StringAlignmentCenter);
    const double buttonY = Dip(g_layout.aboutGithub.top) - y;
    const double copyrightY = buttonY - 37;
    const double dividerY = copyrightY - 22;
    const double descriptionH = std::max(0.0, dividerY - 10 - 194);
    DrawTextBlock(g, kAboutDescription,
        RectDip(x + 42, y + 194, w - 84, descriptionH), 13, t.fg2, FontStyleRegular,
        StringAlignmentCenter, StringAlignmentNear, true);

    SolidBrush stroke(t.stroke);
    g.FillRectangle(&stroke, RectFOf(RectDip(x + 30, y + dividerY, w - 60, 1)));
    DrawTextBlock(g, kAboutCopyright,
        RectDip(x + 30, y + copyrightY, w - 60, 18), 12, t.fg2, FontStyleRegular,
        StringAlignmentCenter, StringAlignmentCenter);

    DrawAboutActionButton(g, g_layout.aboutGithub, L"GitHub", t, Hit::AboutGithub, AboutActionIcon::Github);
    DrawAboutActionButton(g, g_layout.aboutPrivacy, L"Privacy Policy", t, Hit::AboutPrivacy, AboutActionIcon::Privacy);
}

void DrawPrivacyMarkdown(Graphics& g, const RECT& viewport, const Theme& t)
{
    const double left = Dip(viewport.left);
    const double top = Dip(viewport.top);
    const double width = Dip(viewport.right - viewport.left) - 12;
    constexpr float bodySize = 12.0f;
    constexpr float headingSize = 12.5f;
    constexpr double headingTopGap = 6.0;
    constexpr double headingLineHeight = 24.0;
    constexpr double headingAfterGap = 4.0;
    constexpr double bodyLineHeight = 24.0;
    constexpr double paragraphGap = 9.0;
    constexpr double codeLineHeight = 22.0;
    constexpr double codeAfterGap = 10.0;

    auto measureLine = [&](const std::wstring& value, float size, int style, const wchar_t* familyName = L"Segoe UI Variable Text") {
        FontFamily family(familyName);
        FontFamily fallback(L"Segoe UI");
        const FontFamily* selectedFamily = family.GetLastStatus() == Ok ? &family : &fallback;
        Font font(selectedFamily, static_cast<REAL>(Px(size)), style, UnitPixel);
        StringFormat format;
        format.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsMeasureTrailingSpaces);
        RectF bounds;
        g.MeasureString(value.c_str(), -1, &font, RectF(0, 0, 10000, 10000), &format, &bounds);
        return Dip(static_cast<int>(std::ceil(bounds.Width)));
    };

    auto layoutWrapped = [&](std::vector<PrivacyLayoutText>& items, double& y, const std::wstring& value,
                             PrivacyTextRole role, float size, int style, double lineHeight, double afterGap) {
        std::wistringstream words(value);
        std::wstring word;
        std::wstring line;
        bool drewLine = false;

        while (words >> word) {
            const std::wstring candidate = line.empty() ? word : line + L" " + word;
            if (!line.empty() && measureLine(candidate, size, style) > width) {
                items.push_back({ role, line, y, lineHeight, size, style });
                y += lineHeight;
                drewLine = true;
                line = word;
            } else {
                line = candidate;
            }
        }

        if (!line.empty()) {
            items.push_back({ role, line, y, lineHeight, size, style });
            y += lineHeight;
            drewLine = true;
        }

        if (drewLine) {
            y += afterGap;
        }
    };

    PrivacyLayoutCache& cache = g_state.privacyLayout;
    const int widthPx = viewport.right - viewport.left;
    if (!cache.valid || cache.dpi != g_dpi || cache.widthPx != widthPx || cache.source != g_state.privacyMarkdown) {
        cache = {};
        cache.valid = true;
        cache.dpi = g_dpi;
        cache.widthPx = widthPx;
        cache.source = g_state.privacyMarkdown;

        double y = 0;
        bool contentDrawn = false;
        for (const PrivacyMarkdownBlock& block : ParsePrivacyMarkdown(g_state.privacyMarkdown)) {
            if (block.kind == PrivacyMarkdownBlockKind::Heading) {
                if (block.headingLevel <= 1) {
                    continue;
                }
                if (contentDrawn) {
                    y += headingTopGap;
                }
                layoutWrapped(cache.text, y, block.text, PrivacyTextRole::Heading,
                    headingSize, FontStyleBold, headingLineHeight, headingAfterGap);
                contentDrawn = true;
            } else if (block.kind == PrivacyMarkdownBlockKind::Code) {
                const std::vector<std::wstring> codeLines = LinesOf(block.text);
                const size_t lineCount = std::max<size_t>(1, codeLines.size());
                const double boxHeight = std::max(42.0, 18.0 + lineCount * codeLineHeight);
                cache.code.push_back({ block.text, y, boxHeight });
                y += boxHeight + codeAfterGap;
                contentDrawn = true;
            } else {
                layoutWrapped(cache.text, y, block.text, PrivacyTextRole::Body,
                    bodySize, FontStyleRegular, bodyLineHeight, paragraphGap);
                contentDrawn = true;
            }
        }

        cache.contentHeight = y;
    }

    const double viewportHeight = Dip(viewport.bottom - viewport.top);
    const double maxScroll = std::max(0.0, cache.contentHeight - viewportHeight);
    g_state.privacyScroll = std::max(0.0, std::min(maxScroll, g_state.privacyScroll));
    g_state.privacyContentHeight = cache.contentHeight;

    Region oldClip;
    g.GetClip(&oldClip);
    g.SetClip(RectFOf(viewport), CombineModeReplace);

    for (const PrivacyLayoutCode& code : cache.code) {
        const double y = top + code.y - g_state.privacyScroll;
        if (VerticallyVisible(y, code.h, viewport)) {
            DrawRoundedBorder(g, RectDip(left, y, width, code.h), 5, t.ctrl, t.ctrlBorder);
            DrawTextBlockWithFamily(g, code.text, RectDip(left + 16, y + 9, width - 32, code.h - 16),
                bodySize, t.fg, FontStyleBold, L"Consolas");
        }
    }

    for (const PrivacyLayoutText& item : cache.text) {
        const double y = top + item.y - g_state.privacyScroll;
        if (!VerticallyVisible(y, item.h, viewport)) {
            continue;
        }
        const Color color = item.role == PrivacyTextRole::Heading ? t.fg : t.fg2;
        DrawTextBlock(g, item.text, RectDip(left, y, width, item.h), item.size, color, item.style);
    }

    g.SetClip(&oldClip, CombineModeReplace);
}

void DrawPrivacyScrollbar(Graphics& g, const Theme& t)
{
    const double viewportHeight = Dip(g_layout.privacyViewport.bottom - g_layout.privacyViewport.top);
    if (g_state.privacyContentHeight <= viewportHeight) {
        return;
    }

    const RECT dlg = g_layout.privacyDialog;
    const double x = Dip(dlg.right) - 12;
    const double top = Dip(dlg.top) + 76;
    const double height = Dip(dlg.bottom) - top - 10;
    const double maxScroll = std::max(1.0, g_state.privacyContentHeight - viewportHeight);
    const double thumbHeight = std::max(42.0, height * viewportHeight / g_state.privacyContentHeight);
    const double thumbY = top + (height - thumbHeight) * std::min(maxScroll, g_state.privacyScroll) / maxScroll;
    const Color scrollColor = EffectiveDark() ? Rgba(255, 255, 255, 115) : Rgba(0, 0, 0, 95);

    DrawFluentIcon(g, RectDip(x - 4, top - 18, 16, 16), FluentIcon::ChevronUp, scrollColor);
    DrawFluentIcon(g, RectDip(x - 4, top + height + 2, 16, 16), FluentIcon::ChevronDown, scrollColor);
    DrawRounded(g, RectDip(x, thumbY, 8, thumbHeight), 4, scrollColor);
}

void DrawPrivacyDialog(Graphics& g, const RECT& client, const Theme& t)
{
    if (!PrivacyOpen()) {
        return;
    }

    SolidBrush overlay(Rgba(0, 0, 0, 112));
    g.FillRectangle(&overlay, RectFOf(client));

    const RECT dlg = g_layout.privacyDialog;
    const double x = Dip(dlg.left);
    const double y = Dip(dlg.top);
    const double w = Dip(dlg.right - dlg.left);
    const double h = Dip(dlg.bottom - dlg.top);
    DrawRounded(g, RectDip(x + 1, y + 11, w - 2, h), 8, Rgba(0, 0, 0, 24));
    DrawRounded(g, RectDip(x, y + 5, w, h), 8, Rgba(0, 0, 0, 16));
    DrawRoundedBorder(g, dlg, 8, t.dialogBg, t.dialogBorder);

    Theme iconTheme = t;
    iconTheme.fg = t.accent;
    DrawShieldIcon(g, RectDip(x + 22, y + 25, 18, 18), iconTheme);
    DrawTextBlock(g, L"Privacy Policy", RectDip(x + 52, y + 20, 260, 30), 15, t.fg, FontStyleBold,
        StringAlignmentNear, StringAlignmentCenter);

    if (g_hover == Hit::PrivacyClose) {
        DrawRounded(g, g_layout.privacyClose, 4, EffectiveDark() ? Rgba(255, 255, 255, 18) : Rgba(0, 0, 0, 8));
    }
    const Color closeColor = g_hover == Hit::PrivacyClose ? t.fg : t.fg2;
    DrawFluentIcon(g, RectDip(Dip(g_layout.privacyClose.left) + 17, Dip(g_layout.privacyClose.top) + 14, 12, 12), FluentIcon::Dismiss, closeColor);

    SolidBrush stroke(t.stroke);
    g.FillRectangle(&stroke, RectFOf(RectDip(x, y + 70, w, 1)));

    DrawPrivacyMarkdown(g, g_layout.privacyViewport, t);
    DrawPrivacyScrollbar(g, t);
}

void DrawFormatsDialog(Graphics& g, const RECT& client, const Theme& t)
{
    if (!g_state.formatsOpen) {
        return;
    }

    SolidBrush overlay(Rgba(0, 0, 0, 112));
    g.FillRectangle(&overlay, RectFOf(client));

    const RECT dlg = g_layout.formatsDialog;
    const double x = Dip(dlg.left);
    const double y = Dip(dlg.top);
    const double w = Dip(dlg.right - dlg.left);
    const double h = Dip(dlg.bottom - dlg.top);
    DrawRounded(g, RectDip(x + 1, y + 11, w - 2, h), 8, Rgba(0, 0, 0, 24));
    DrawRounded(g, RectDip(x, y + 5, w, h), 8, Rgba(0, 0, 0, 16));
    DrawRoundedBorder(g, dlg, 8, t.dialogBg, t.dialogBorder);

    DrawTextBlock(g, L"Supported formats", RectDip(x + 24, y + 27, 260, 26), 20, t.fg, FontStyleBold);
    DrawTextBlock(g, L"Choose which extensions Backdropper registers as the Explorer thumbnail handler.",
        RectDip(x + 24, y + 58, w - 92, 40), 13, t.fg2, FontStyleRegular,
        StringAlignmentNear, StringAlignmentNear, true);

    if (g_hover == Hit::FormatClose) {
        DrawRounded(g, g_layout.formatsClose, 4, EffectiveDark() ? Rgba(255, 255, 255, 18) : Rgba(0, 0, 0, 8));
    }
    const Color closeColor = g_hover == Hit::FormatClose ? t.fg : t.fg2;
    DrawFluentIcon(g, RectDip(Dip(g_layout.formatsClose.left) + 17, Dip(g_layout.formatsClose.top) + 14, 12, 12), FluentIcon::Dismiss, closeColor);

    for (const size_t i : kFormatsDialogOrder) {
        const RECT row = g_layout.formatToggles[i];
        const Hit rowHit = static_cast<Hit>(static_cast<int>(Hit::FormatPng) + static_cast<int>(i));
        const bool available = g_state.formatAvailable[i];
        const bool hovered = available && g_hover == rowHit;
        DrawRoundedBorder(g, row, 6, hovered ? t.rowHover : t.ctrl, t.ctrlBorder);

        const Color title = available ? t.fg : Color(120, t.fg.GetR(), t.fg.GetG(), t.fg.GetB());
        const Color sub = available ? t.fg2 : Color(95, t.fg2.GetR(), t.fg2.GetG(), t.fg2.GetB());
        DrawTextBlock(g, BackdropperFormatLabel(kBackdropperFormats[i]), RectDip(Dip(row.left) + 12, Dip(row.top) + 9, 46, 18), 13, title, FontStyleBold);
        DrawTextBlock(g, FormatStatus(i), RectDip(Dip(row.left) + 69, Dip(row.top) + 10, 112, 16), 11.5f, sub);

        const RECT sw = RectDip(Dip(row.right) - 52, Dip(row.top) + 11, 40, 20);
        DrawFormatSwitch(g, sw, EffectiveFormatEnabled(i), available, t);
    }

    SolidBrush footerStroke(t.stroke);
    g.FillRectangle(&footerStroke, RectFOf(RectDip(x, y + h - 68, w, 1)));
    DrawInfoIcon(g, RectDip(x + 24, y + h - 42, 16, 16), t);

    if (g_state.ghostscriptInstalled) {
        DrawTextBlock(g, L"Ghostscript detected. EPS and AI files render locally.",
            RectDip(x + 52, y + h - 49, 390, 20), 12, t.fg2);
    } else {
        DrawTextBlock(g, L"EPS and PostScript-based AI files need Ghostscript.", RectDip(x + 52, y + h - 49, 380, 18), 12, t.fg2);
        DrawTextBlock(g, L"Download Ghostscript", g_layout.ghostscriptLink, 12, t.accent, FontStyleRegular);
        DrawLine(g, Dip(g_layout.ghostscriptLink.left), Dip(g_layout.ghostscriptLink.top) + 17,
            Dip(g_layout.ghostscriptLink.left) + 106, Dip(g_layout.ghostscriptLink.top) + 17, t.accent, 1);
    }

    DrawButton(g, g_layout.formatsDone, L"Done", t, true, false, g_hover == Hit::FormatDone);
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
    DrawPrivacyDialog(g, client, t);
    DrawFormatsDialog(g, client, t);
    DrawFocusRing(g, window, t);

    BitBlt(hdc, 0, 0, client.right, client.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bitmap);
    DeleteDC(mem);
}

Hit HitTest(POINT pt)
{
    if (PrivacyOpen()) {
        if (PtIn(g_layout.privacyClose, pt)) return Hit::PrivacyClose;
        return Hit::None;
    }

    if (g_state.formatsOpen) {
        if (PtIn(g_layout.formatsClose, pt)) return Hit::FormatClose;
        if (PtIn(g_layout.formatsDone, pt)) return Hit::FormatDone;
        if (!g_state.ghostscriptInstalled && PtIn(g_layout.ghostscriptLink, pt)) return Hit::GhostscriptLink;
        for (size_t i = 0; i < kBackdropperFormatCount; ++i) {
            if (g_state.formatAvailable[i] && PtIn(g_layout.formatToggles[i], pt)) {
                return static_cast<Hit>(static_cast<int>(Hit::FormatPng) + static_cast<int>(i));
            }
        }
        return Hit::None;
    }

    if (AboutOpen()) {
        if (PtIn(g_layout.aboutClose, pt)) return Hit::AboutClose;
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
    if (PtIn(g_layout.autoCheckToggle, pt)) return Hit::AutoCheckToggle;
    if (PtIn(g_layout.formatManageBtn, pt)) return Hit::FormatManage;
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

void ApplyUpdateCheckResult(HWND window, const UpdateCheckResult& result)
{
    g_state.updateCheckInProgress = false;
    if (!result.ok) {
        g_state.updateAvailable = false;
        g_state.latestVersion.clear();
        g_state.updateStatus = L"Could not reach GitHub Releases";
    } else {
        const int versionCompare = CompareVersions(result.latest, kBackdropperVersion);
        g_state.latestVersion = result.latest;
        g_state.updateAvailable = versionCompare > 0;
        if (versionCompare > 0) {
            g_state.updateStatus = std::wstring(L"Version ") + result.latest + L" is available to download";
        } else if (versionCompare < 0) {
            g_state.updateStatus = std::wstring(L"Running newer than latest release: ") + result.latest;
        } else {
            g_state.updateStatus = L"You're on the latest version";
        }
    }
    InvalidateRect(window, nullptr, TRUE);
}

void CheckForUpdates(HWND window)
{
    if (g_state.updateCheckInProgress) {
        return;
    }

    g_state.updateCheckInProgress = true;
    g_state.updateStatus = L"Checking GitHub Releases...";
    InvalidateRect(window, nullptr, TRUE);

    std::thread([window]() {
        auto* result = new UpdateCheckResult();
        result->ok = FetchLatestVersion(&result->latest);
        if (!PostMessageW(window, WmUpdateCheckResult, 0, reinterpret_cast<LPARAM>(result))) {
            delete result;
        }
    }).detach();
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
    if (PrivacyOpen()) {
        if (hit == Hit::PrivacyClose) {
            ClosePrivacy(window);
        }
        return;
    }

    if (g_state.formatsOpen) {
        const int formatIndex = FormatIndexFromHit(hit);
        if (hit == Hit::FormatClose || hit == Hit::FormatDone) {
            g_state.formatsOpen = false;
            LayoutChildWindows(window);
            InvalidateRect(window, nullptr, TRUE);
        } else if (hit == Hit::GhostscriptLink) {
            ShellExecuteW(window, L"open", kGhostscriptUrl, nullptr, nullptr, SW_SHOWNORMAL);
        } else if (formatIndex >= 0 && g_state.formatAvailable[formatIndex]) {
            g_state.settings.enabledFormats[formatIndex] = !g_state.settings.enabledFormats[formatIndex];
            InvalidateRect(window, nullptr, TRUE);
        }
        return;
    }

    if (AboutOpen()) {
        switch (hit) {
        case Hit::AboutClose:
            CloseAbout(window);
            break;
        case Hit::AboutGithub:
            ShellExecuteW(window, L"open", kGithubUrl, nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case Hit::AboutPrivacy:
            OpenPrivacy(window);
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
    case Hit::AutoCheckToggle:
        g_state.settings.checkUpdatesAutomatically = !g_state.settings.checkUpdatesAutomatically;
        InvalidateRect(window, nullptr, TRUE);
        break;
    case Hit::FormatManage:
        RefreshFormatAvailability();
        g_state.formatsOpen = true;
        g_state.viewMenuOpen = false;
        LayoutChildWindows(window);
        InvalidateRect(window, nullptr, TRUE);
        break;
    case Hit::CheckUpdates:
        CheckForUpdates(window);
        break;
    case Hit::InstallUpdate:
        if (LaunchUpdater(window, ForceUpdateVisible())) {
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
            g_state.settings = SettingsFromUi();
            if (FAILED(SaveBackdropperSettings(g_state.settings))) {
                OpenDialog(window, L"Could not save format choices", L"Backdropper could not write HKCU\\Software\\Backdropper.");
                break;
            }
            const bool ok = RunRegsvr(window, false);
            RefreshFormatAvailability();
            RefreshRegistrationState();
            OpenDialog(window, ok ? L"Registered image handlers" : L"Registration failed",
                ok ? L"Backdropper is now the per-user Shell thumbnail handler for the selected available formats."
                   : L"regsvr32 could not register BackdropperThumb.dll.");
        }
        break;
    case Hit::Unregister:
        if (g_state.registered) {
            const bool ok = RunRegsvr(window, true);
            RefreshRegistrationState();
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
        SetPreviewView(window, kViewMenuOrder[index]);
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
    if (GetEnvironmentVariableW(L"BACKDROPPER_SCREENSHOT_LIGHT", nullptr, 0) > 0) {
        g_state.matchSystemTheme = false;
        g_state.dark = false;
    }
    g_state.solidText = FormatColor(g_state.settings.solidColor);
    g_state.checkerAText = FormatColor(g_state.settings.checkerA);
    g_state.checkerBText = FormatColor(g_state.settings.checkerB);
    wchar_t size[16] = {};
    swprintf_s(size, L"%u", g_state.settings.checkerSize);
    g_state.sizeText = size;
    RefreshFormatAvailability();
    RefreshRegistrationState();
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
        if (g_state.settings.checkUpdatesAutomatically) {
            PostMessageW(window, WmRunUpdateCheck, 0, 0);
        }
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
        if (!AboutOpen() && !PrivacyOpen() && pt.y >= 0 && pt.y < Px(40) && hit == Hit::None) {
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
        SetHover(window, hit);
        SetCursor(LoadCursorW(nullptr, hit == Hit::None ? IDC_ARROW : IDC_HAND));
        return 0;
    }

    case WM_MOUSELEAVE:
        g_trackingMouse = false;
        SetHover(window, Hit::None);
        return 0;

    case WM_LBUTTONUP: {
        POINT pt = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        CalculateLayout(window);
        if (PrivacyOpen() && !PtIn(g_layout.privacyDialog, pt)) {
            ClosePrivacy(window);
            return 0;
        }
        if (AboutOpen() && !PtIn(g_layout.aboutDialog, pt)) {
            CloseAbout(window);
            return 0;
        }
        const Hit hit = HitTest(pt);
        if (hit != Hit::None) {
            g_focus = hit;
        }
        g_keyboardFocusVisible = false;
        ActivateHit(window, hit);
        return 0;
    }

    case WM_MOUSEWHEEL:
        if (PrivacyOpen()) {
            const double viewportHeight = Dip(g_layout.privacyViewport.bottom - g_layout.privacyViewport.top);
            const double maxScroll = std::max(0.0, g_state.privacyContentHeight - viewportHeight);
            g_state.privacyScroll = std::max(0.0, std::min(maxScroll,
                g_state.privacyScroll - GET_WHEEL_DELTA_WPARAM(wparam) / static_cast<double>(WHEEL_DELTA) * 54.0));
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        if (PreviewShortcutsEnabled() && (GET_KEYSTATE_WPARAM(wparam) & MK_CONTROL)) {
            CalculateLayout(window);
            POINT pt = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
            ScreenToClient(window, &pt);
            if (PtIn(g_layout.previewFrame, pt)) {
                StepPreviewView(window, GET_WHEEL_DELTA_WPARAM(wparam) > 0 ? -1 : 1);
                return 0;
            }
        }
        if (!DialogOpen() && !AboutOpen() && !g_state.formatsOpen) {
            CalculateLayout(window);
            POINT pt = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
            ScreenToClient(window, &pt);
            const double viewportHeight = Dip(g_layout.leftPane.bottom - g_layout.leftPane.top);
            const double maxScroll = std::max(0.0, g_state.leftContentHeight - viewportHeight);
            if (maxScroll > 0 && PtIn(g_layout.leftPane, pt)) {
                g_state.leftScroll = std::max(0.0, std::min(maxScroll,
                    g_state.leftScroll - GET_WHEEL_DELTA_WPARAM(wparam) / static_cast<double>(WHEEL_DELTA) * 54.0));
                LayoutChildWindows(window);
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
        }
        break;

    case WM_KEYDOWN:
        if (wparam == VK_TAB) {
            MoveKeyboardFocus(window, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
            return 0;
        }
        if (wparam == VK_ESCAPE) {
            if (PrivacyOpen()) {
                ClosePrivacy(window);
            } else if (AboutOpen()) {
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
        if ((wparam == VK_RETURN || wparam == VK_SPACE) && g_focus != Hit::None) {
            ActivateHit(window, g_focus);
            return 0;
        }
        if (HandlePreviewKeyboardShortcut(window, wparam)) {
            return 0;
        }
        break;

    case WmUpdateCheckResult: {
        std::unique_ptr<UpdateCheckResult> result(reinterpret_cast<UpdateCheckResult*>(lparam));
        if (result) {
            ApplyUpdateCheckResult(window, *result);
        }
        return 0;
    }

    case WmRunUpdateCheck:
        CheckForUpdates(window);
        return 0;

    case WmAccessibleInvoke:
        g_focus = static_cast<Hit>(wparam);
        g_keyboardFocusVisible = true;
        ActivateHit(window, g_focus);
        return 0;

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
        SetBkColor(hdc, SolidControlRef());
        return reinterpret_cast<LRESULT>(g_editBrush);
    }

    case WM_GETOBJECT:
        if (static_cast<LONG>(lparam) == UiaRootObjectId) {
            auto* provider = new UiaProvider(window, Hit::None);
            const LRESULT result = UiaReturnRawElementProvider(window, wparam, lparam, provider);
            provider->Release();
            return result;
        }
        if (static_cast<LONG>(lparam) == OBJID_CLIENT) {
            auto* accessible = new AccessibleRoot(window);
            const LRESULT result = LresultFromObject(IID_IAccessible, wparam, accessible);
            accessible->Release();
            return result;
        }
        break;

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
        if ((msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN)
            && window
            && (msg.hwnd == window || IsChild(window, msg.hwnd))
            && HandlePreviewKeyboardShortcut(window, msg.wParam)) {
            continue;
        }
        if ((msg.message == WM_KEYDOWN || msg.message == WM_KEYUP || msg.message == WM_SYSKEYDOWN || msg.message == WM_SYSKEYUP)
            && (msg.wParam == VK_SHIFT || msg.wParam == VK_LSHIFT || msg.wParam == VK_RSHIFT)) {
            InvalidateRect(window, nullptr, FALSE);
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_gdiplusToken) {
        GdiplusShutdown(g_gdiplusToken);
    }
    return static_cast<int>(msg.wParam);
}
