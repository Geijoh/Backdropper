#include "thumbnail_cache.h"

#include <windows.h>
#include <knownfolders.h>
#include <shellapi.h>
#include <shlobj.h>

#include <string>
#include <utility>

static bool WaitForShellWindow(bool present, DWORD timeoutMs)
{
    const DWORD start = GetTickCount();
    do {
        const bool found = FindWindowW(L"Shell_TrayWnd", nullptr) != nullptr;
        if (found == present) {
            return true;
        }
        Sleep(100);
    } while (GetTickCount() - start < timeoutMs);
    return false;
}

bool StartBackdropperExplorerShell()
{
    wchar_t windowsDir[MAX_PATH] = {};
    if (!GetWindowsDirectoryW(windowsDir, ARRAYSIZE(windowsDir))) {
        return false;
    }

    const std::wstring explorer = std::wstring(windowsDir) + L"\\explorer.exe";
    for (int attempt = 0; attempt < 2; ++attempt) {
        const HINSTANCE result = ShellExecuteW(nullptr, L"open", explorer.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) > 32 && WaitForShellWindow(true, 5000)) {
            return true;
        }
    }
    return false;
}

bool StopBackdropperExplorerShell()
{
    HWND shell = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!shell) {
        return true;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(shell, &pid);
    if (!pid) {
        return false;
    }

    HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
    if (!process) {
        return false;
    }

    // ponytail: stop only the shell process; never taskkill /t, which can kill apps Explorer launched.
    const bool stopped = TerminateProcess(process, 0) != FALSE;
    if (stopped) {
        WaitForSingleObject(process, 5000);
    }
    CloseHandle(process);
    return stopped;
}

static int DeleteThumbcacheDbs()
{
    PWSTR localAppData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData))) {
        return -1;
    }

    std::wstring base = localAppData;
    CoTaskMemFree(localAppData);
    // Pinned taskbar icons render from the legacy IconCache.db, not the
    // per-size Explorer DBs, so stale icons survive without this delete.
    const std::wstring legacyIconCache = base + L"\\IconCache.db";

    int failures = 0;
    // iconcache_*.db holds shortcut tiles rendered from .ico sources, and the
    // AppResolver cache (Microsoft\Windows\Caches) holds baked taskbar/Start
    // app tiles, so stale backdropped icons survive a thumbcache-only purge.
    const std::pair<std::wstring, const wchar_t*> sweeps[] = {
        { base + L"\\Microsoft\\Windows\\Explorer\\", L"thumbcache_*.db" },
        { base + L"\\Microsoft\\Windows\\Explorer\\", L"iconcache_*.db" },
        { base + L"\\Microsoft\\Windows\\Caches\\", L"*.db" },
    };
    for (const auto& [dir, pattern] : sweeps) {
        WIN32_FIND_DATAW data = {};
        HANDLE find = FindFirstFileW((dir + pattern).c_str(), &data);
        if (find == INVALID_HANDLE_VALUE) {
            continue;
        }

        do {
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                const std::wstring path = dir + data.cFileName;
                SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
                if (!DeleteFileW(path.c_str())) {
                    ++failures;
                }
            }
        } while (FindNextFileW(find, &data));

        FindClose(find);
    }

    SetFileAttributesW(legacyIconCache.c_str(), FILE_ATTRIBUTE_NORMAL);
    if (!DeleteFileW(legacyIconCache.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) {
        ++failures;
    }
    return failures;
}

std::wstring ForceDeleteThumbcacheDbs()
{
    const bool stopped = StopBackdropperExplorerShell();
    if (stopped) {
        WaitForShellWindow(false, 5000);
    }

    int failures = DeleteThumbcacheDbs();
    if (failures < 0) {
        if (stopped) {
            StartBackdropperExplorerShell();
        }
        return L"Could not find the thumbnail cache folder.";
    }

    const bool restarted = stopped ? StartBackdropperExplorerShell() : FindWindowW(L"Shell_TrayWnd", nullptr) != nullptr;
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    if (!stopped) {
        return failures == 0
            ? L"Thumbnail and icon cache DB files were deleted without restarting Explorer."
            : L"Explorer could not be stopped, and some thumbnail and icon cache DB files could not be deleted.";
    }
    if (!restarted) {
        return L"Thumbnail and icon cache DB files were deleted, but Explorer did not restart automatically.";
    }
    if (failures == 0) {
        return L"Explorer was restarted and thumbnail and icon cache DB files were deleted.";
    }
    return L"Explorer was restarted, but some thumbnail and icon cache DB files could not be deleted.";
}
