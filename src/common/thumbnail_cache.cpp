#include "thumbnail_cache.h"

#include <windows.h>
#include <knownfolders.h>
#include <shellapi.h>
#include <shlobj.h>

#include <string>

namespace {

bool WaitForShellWindow(bool present, DWORD timeoutMs)
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

bool StartExplorerShell()
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

bool StopShellExplorer()
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

int DeleteThumbcacheDbs()
{
    PWSTR localAppData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData))) {
        return -1;
    }

    std::wstring dir = localAppData;
    CoTaskMemFree(localAppData);
    dir += L"\\Microsoft\\Windows\\Explorer\\";

    WIN32_FIND_DATAW data = {};
    HANDLE find = FindFirstFileW((dir + L"thumbcache_*.db").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) {
        return 0;
    }

    int failures = 0;
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
    return failures;
}

}

std::wstring ForceDeleteThumbcacheDbs()
{
    const bool stopped = StopShellExplorer();
    if (stopped) {
        WaitForShellWindow(false, 5000);
    }

    int failures = DeleteThumbcacheDbs();
    if (failures < 0) {
        if (stopped) {
            StartExplorerShell();
        }
        return L"Could not find the thumbnail cache folder.";
    }

    const bool restarted = stopped ? StartExplorerShell() : FindWindowW(L"Shell_TrayWnd", nullptr) != nullptr;
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    if (!stopped) {
        return failures == 0
            ? L"Thumbnail cache DB files were deleted without restarting Explorer."
            : L"Explorer could not be stopped, and some thumbnail cache DB files could not be deleted.";
    }
    if (!restarted) {
        return L"Thumbnail cache DB files were deleted, but Explorer did not restart automatically.";
    }
    if (failures == 0) {
        return L"Explorer was restarted and thumbnail cache DB files were deleted.";
    }
    return L"Explorer was restarted, but some thumbnail cache DB files could not be deleted.";
}
