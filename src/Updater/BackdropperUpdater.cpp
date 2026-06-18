#include <windows.h>
#include <shellapi.h>
#include <urlmon.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr wchar_t kVersionUrl[] = L"https://github.com/Geijoh/Backdropper/releases/latest/download/backdropper-version.txt";
constexpr wchar_t kZipUrl[] = L"https://github.com/Geijoh/Backdropper/releases/latest/download/Backdropper-latest-windows-x64.zip";

std::wstring Quote(const std::wstring& value)
{
    return L"\"" + value + L"\"";
}

void WriteStep(const std::wstring& message)
{
    std::wcout << L"[Backdropper] " << message << std::endl;
}

bool ParseVersion(const std::wstring& text, std::array<int, 3>& parts)
{
    return swscanf_s(text.c_str(), L"%d.%d.%d", &parts[0], &parts[1], &parts[2]) == 3;
}

int CompareVersions(const std::wstring& left, const std::wstring& right)
{
    std::array<int, 3> a {};
    std::array<int, 3> b {};
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

std::wstring ReadTextFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return TrimVersion(std::wstring(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()));
}

fs::path TempPath(const std::wstring& name)
{
    wchar_t temp[MAX_PATH] = {};
    GetTempPathW(ARRAYSIZE(temp), temp);
    return fs::path(temp) / name;
}

bool Download(const std::wstring& url, const fs::path& path)
{
    return SUCCEEDED(URLDownloadToFileW(nullptr, url.c_str(), path.c_str(), 0, nullptr));
}

std::wstring TarPath()
{
    wchar_t windowsDir[MAX_PATH] = {};
    if (GetWindowsDirectoryW(windowsDir, ARRAYSIZE(windowsDir))) {
        fs::path tar = fs::path(windowsDir) / L"System32" / L"tar.exe";
        if (fs::exists(tar)) {
            return tar.wstring();
        }
    }
    return L"tar.exe";
}

bool RunAndWait(const std::wstring& exe, const std::wstring& args)
{
    std::wstring command = Quote(exe) + L" " + args;
    STARTUPINFOW startup {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process {};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
        return false;
    }

    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return exitCode == 0;
}

bool ExtractZip(const fs::path& zip, const fs::path& outDir)
{
    fs::create_directories(outDir);
    return RunAndWait(TarPath(), L"-xf " + Quote(zip.wstring()) + L" -C " + Quote(outDir.wstring()));
}

fs::path FindFile(const fs::path& root, const std::wstring& filename)
{
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && _wcsicmp(entry.path().filename().c_str(), filename.c_str()) == 0) {
            return entry.path();
        }
    }
    return {};
}

void CopyTreeItem(const fs::path& source, const fs::path& target)
{
    if (fs::is_directory(source)) {
        fs::create_directories(target);
        for (const auto& entry : fs::directory_iterator(source)) {
            CopyTreeItem(entry.path(), target / entry.path().filename());
        }
    } else {
        fs::create_directories(target.parent_path());
        fs::copy_file(source, target, fs::copy_options::overwrite_existing);
    }
}

void CopyPayload(const fs::path& payloadDir, const fs::path& installDir)
{
    for (const auto& entry : fs::directory_iterator(payloadDir)) {
        CopyTreeItem(entry.path(), installDir / entry.path().filename());
    }
}

void CheckWritable(const fs::path& installDir)
{
    const fs::path probe = installDir / L".backdropper-update-write-test";
    {
        std::ofstream out(probe);
        if (!out) {
            throw std::runtime_error("Install directory is not writable.");
        }
        out << "ok";
    }
    fs::remove(probe);
}

void WaitForProcess(DWORD pid)
{
    if (!pid) {
        return;
    }
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (process) {
        WaitForSingleObject(process, 30000);
        CloseHandle(process);
    }
}

void StartBackdropper(const fs::path& installDir)
{
    const fs::path exe = installDir / L"BackdropperSettings.exe";
    if (fs::exists(exe)) {
        ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, installDir.c_str(), SW_SHOWNORMAL);
    }
}

bool StopShellExplorer()
{
    HWND shell = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!shell) {
        return true;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(shell, &pid);
    HANDLE process = pid ? OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid) : nullptr;
    if (!process) {
        return false;
    }

    // ponytail: shell process only; no process-tree kill.
    const bool stopped = TerminateProcess(process, 0) != FALSE;
    if (stopped) {
        WaitForSingleObject(process, 5000);
    }
    CloseHandle(process);
    return stopped;
}

void StartExplorerShell()
{
    wchar_t windowsDir[MAX_PATH] = {};
    if (GetWindowsDirectoryW(windowsDir, ARRAYSIZE(windowsDir))) {
        const fs::path explorer = fs::path(windowsDir) / L"explorer.exe";
        ShellExecuteW(nullptr, L"open", explorer.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else {
        ShellExecuteW(nullptr, L"open", L"explorer.exe", nullptr, nullptr, SW_SHOWNORMAL);
    }
}

int SelfTest()
{
    if (CompareVersions(L"1.2.3", L"1.2.2") <= 0) return 1;
    if (CompareVersions(L"1.2.3", L"1.2.3") != 0) return 1;
    if (CompareVersions(L"1.2.3", L"1.3.0") >= 0) return 1;
    if (TrimVersion(L"v0.5.3\r\n") != L"0.5.3") return 1;
    if (!RunAndWait(TarPath(), L"--version")) return 1;
    std::wcout << L"OK" << std::endl;
    return 0;
}

struct Args {
    fs::path installDir;
    DWORD currentPid = 0;
    std::wstring currentVersion = L"0.0.0";
    bool selfTest = false;
};

Args ParseArgs()
{
    Args args;
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--self-test") {
            args.selfTest = true;
        } else if (arg == L"--install-dir" && i + 1 < argc) {
            args.installDir = argv[++i];
        } else if (arg == L"--current-pid" && i + 1 < argc) {
            args.currentPid = wcstoul(argv[++i], nullptr, 10);
        } else if (arg == L"--current-version" && i + 1 < argc) {
            args.currentVersion = argv[++i];
        }
    }
    LocalFree(argv);
    return args;
}

}

int wmain()
{
    fs::path tempRoot;
    fs::path installDir;
    bool stoppedExplorer = false;

    try {
        const Args args = ParseArgs();
        if (args.selfTest) {
            return SelfTest();
        }

        installDir = args.installDir;
        if (installDir.empty() || !fs::exists(installDir)) {
            throw std::runtime_error("Install directory does not exist.");
        }
        CheckWritable(installDir);

        tempRoot = TempPath(L"BackdropperUpdate-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        fs::create_directories(tempRoot);
        const fs::path versionFile = tempRoot / L"backdropper-version.txt";
        const fs::path zipPath = tempRoot / L"Backdropper-latest-windows-x64.zip";
        const fs::path extractDir = tempRoot / L"extract";

        WriteStep(L"Checking latest GitHub release...");
        if (!Download(std::wstring(kVersionUrl) + L"?t=" + std::to_wstring(GetTickCount64()), versionFile)) {
            throw std::runtime_error("Could not download version metadata.");
        }
        const std::wstring latestVersion = ReadTextFile(versionFile);
        if (CompareVersions(latestVersion, args.currentVersion) <= 0) {
            WriteStep(L"Already up to date. Current version: " + args.currentVersion + L".");
            Sleep(2000);
            StartBackdropper(installDir);
            fs::remove_all(tempRoot);
            return 0;
        }

        WriteStep(L"Downloading Backdropper-latest-windows-x64.zip...");
        if (!Download(kZipUrl, zipPath)) {
            throw std::runtime_error("Could not download update ZIP.");
        }

        WriteStep(L"Extracting update...");
        if (!ExtractZip(zipPath, extractDir)) {
            throw std::runtime_error("Could not extract update ZIP.");
        }

        const fs::path settingsExe = FindFile(extractDir, L"BackdropperSettings.exe");
        if (settingsExe.empty()) {
            throw std::runtime_error("The downloaded ZIP does not contain BackdropperSettings.exe.");
        }
        const fs::path payloadDir = settingsExe.parent_path();

        WriteStep(L"Waiting for Backdropper to close...");
        WaitForProcess(args.currentPid);

        WriteStep(L"Replacing Backdropper files...");
        try {
            CopyPayload(payloadDir, installDir);
        } catch (...) {
            WriteStep(L"Files are still in use. Restarting Explorer and retrying...");
            stoppedExplorer = StopShellExplorer();
            Sleep(2000);
            CopyPayload(payloadDir, installDir);
        }

        if (stoppedExplorer) {
            StartExplorerShell();
        }

        WriteStep(L"Update complete. Starting Backdropper...");
        StartBackdropper(installDir);
        fs::remove_all(tempRoot);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "[Backdropper] Update failed: " << ex.what() << std::endl;
        if (stoppedExplorer) {
            StartExplorerShell();
        }
        if (!installDir.empty()) {
            StartBackdropper(installDir);
        }
        if (!tempRoot.empty()) {
            std::error_code ignored;
            fs::remove_all(tempRoot, ignored);
        }
        std::cout << "Press Enter to close";
        std::cin.get();
        return 1;
    }
}
