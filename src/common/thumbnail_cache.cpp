#include "thumbnail_cache.h"

#include <windows.h>
#include <knownfolders.h>
#include <shellapi.h>
#include <shlobj.h>

namespace {

bool RunHidden(const wchar_t* file, wchar_t* args)
{
    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"open";
    info.lpFile = file;
    info.lpParameters = args;
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
    // ponytail: Explorer keeps thumbnails in memory; restart first so DB deletion actually sticks.
    wchar_t killArgs[] = L"/f /t /im explorer.exe";
    RunHidden(L"taskkill.exe", killArgs);

    int failures = DeleteThumbcacheDbs();
    if (failures < 0) {
        ShellExecuteW(nullptr, L"open", L"explorer.exe", nullptr, nullptr, SW_SHOWNORMAL);
        return L"Could not find the thumbnail cache folder.";
    }

    ShellExecuteW(nullptr, L"open", L"explorer.exe", nullptr, nullptr, SW_SHOWNORMAL);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    return failures == 0
        ? L"Explorer was restarted and thumbnail cache DB files were deleted."
        : L"Explorer was restarted, but some thumbnail cache DB files could not be deleted.";
}
