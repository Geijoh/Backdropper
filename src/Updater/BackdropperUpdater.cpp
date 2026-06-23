#include "thumbnail_cache.h"
#include "version.h"

#include <windows.h>
#include <softpub.h>
#include <shellapi.h>
#include <urlmon.h>
#include <wincrypt.h>
#include <wintrust.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr wchar_t kVersionUrl[] = L"https://github.com/Geijoh/Backdropper/releases/latest/download/backdropper-version.txt";
constexpr wchar_t kLatestDownloadBaseUrl[] = L"https://github.com/Geijoh/Backdropper/releases/latest/download/";
constexpr wchar_t kExpectedSigner[] = L"Christopher Johnson";

const std::vector<std::wstring>& RequiredPayloadFiles()
{
    static const std::vector<std::wstring> files {
        L"BackdropperSettings.exe",
        L"BackdropperThumb.dll",
        L"BackdropperUpdater.exe",
    };
    return files;
}

std::wstring Quote(const std::wstring& value)
{
    return L"\"" + value + L"\"";
}

void WriteStep(const std::wstring& message)
{
    std::wcout << L"[Backdropper] " << message << std::endl;
}

bool ShouldInstallUpdate(const std::wstring& latest, const std::wstring& current, bool force)
{
    return force || CompareVersions(latest, current) > 0;
}

std::wstring ZipAssetName(const std::wstring& version)
{
    return L"Backdropper-" + version + L"-windows-x64.zip";
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

void CopyPayload(const fs::path& payloadDir, const fs::path& installDir)
{
    fs::create_directories(installDir);
    for (const auto& entry : fs::directory_iterator(payloadDir)) {
        fs::copy(entry.path(), installDir / entry.path().filename(),
            fs::copy_options::recursive | fs::copy_options::overwrite_existing);
    }
}

bool VerifyAuthenticodeSignature(const fs::path& file)
{
    WINTRUST_FILE_INFO fileInfo {};
    fileInfo.cbStruct = sizeof(fileInfo);
    const std::wstring path = file.wstring();
    fileInfo.pcwszFilePath = path.c_str();

    GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_DATA data {};
    data.cbStruct = sizeof(data);
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &fileInfo;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    data.dwProvFlags = WTD_REVOCATION_CHECK_CHAIN;

    const LONG status = WinVerifyTrust(nullptr, &policy, &data);
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &policy, &data);
    return status == ERROR_SUCCESS;
}

bool SignerMatches(const fs::path& file)
{
    HCERTSTORE store = nullptr;
    HCRYPTMSG message = nullptr;
    DWORD encoding = 0;
    DWORD contentType = 0;
    DWORD formatType = 0;
    const std::wstring path = file.wstring();
    if (!CryptQueryObject(CERT_QUERY_OBJECT_FILE, path.c_str(),
            CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED, CERT_QUERY_FORMAT_FLAG_BINARY,
            0, &encoding, &contentType, &formatType, &store, &message, nullptr)) {
        return false;
    }

    DWORD signerInfoSize = 0;
    bool ok = CryptMsgGetParam(message, CMSG_SIGNER_INFO_PARAM, 0, nullptr, &signerInfoSize) != FALSE;
    std::vector<BYTE> signerInfoBuffer(signerInfoSize);
    ok = ok && CryptMsgGetParam(message, CMSG_SIGNER_INFO_PARAM, 0, signerInfoBuffer.data(), &signerInfoSize) != FALSE;
    if (ok) {
        auto* signerInfo = reinterpret_cast<PCMSG_SIGNER_INFO>(signerInfoBuffer.data());
        CERT_INFO certInfo {};
        certInfo.Issuer = signerInfo->Issuer;
        certInfo.SerialNumber = signerInfo->SerialNumber;
        PCCERT_CONTEXT cert = CertFindCertificateInStore(store, encoding, 0, CERT_FIND_SUBJECT_CERT, &certInfo, nullptr);
        if (cert) {
            wchar_t subject[256] = {};
            CertGetNameStringW(cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, subject, ARRAYSIZE(subject));
            ok = wcsstr(subject, kExpectedSigner) != nullptr;
            CertFreeCertificateContext(cert);
        } else {
            ok = false;
        }
    }

    if (message) {
        CryptMsgClose(message);
    }
    if (store) {
        CertCloseStore(store, 0);
    }
    return ok;
}

void VerifyPayload(const fs::path& payloadDir)
{
    for (const std::wstring& name : RequiredPayloadFiles()) {
        const fs::path file = payloadDir / name;
        if (!fs::exists(file)) {
            throw std::runtime_error("The downloaded ZIP is missing a required Backdropper binary.");
        }
        if (!VerifyAuthenticodeSignature(file) || !SignerMatches(file)) {
            throw std::runtime_error("The downloaded Backdropper binaries are not signed by the expected publisher.");
        }
    }
}

void RestoreBackup(const fs::path& backupDir, const fs::path& installDir)
{
    if (!fs::exists(backupDir)) {
        return;
    }
    CopyPayload(backupDir, installDir);
}

void InstallPayloadWithBackup(const fs::path& payloadDir, const fs::path& installDir, const fs::path& backupDir)
{
    std::error_code ignored;
    fs::remove_all(backupDir, ignored);
    fs::create_directories(backupDir);

    for (const auto& entry : fs::directory_iterator(payloadDir)) {
        const fs::path target = installDir / entry.path().filename();
        if (fs::exists(target)) {
            fs::copy(target, backupDir / target.filename(),
                fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        }
    }

    try {
        CopyPayload(payloadDir, installDir);
    } catch (...) {
        RestoreBackup(backupDir, installDir);
        throw;
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

int SelfTest()
{
    if (CompareVersions(L"1.2.3", L"1.2.2") <= 0) return 1;
    if (CompareVersions(L"1.2.3", L"1.2.3") != 0) return 1;
    if (CompareVersions(L"1.2.3", L"1.3.0") >= 0) return 1;
    if (!ShouldInstallUpdate(L"1.2.3", L"1.2.3", true)) return 1;
    if (ShouldInstallUpdate(L"1.2.3", L"1.2.3", false)) return 1;
    if (TrimVersion(L"v0.5.3\r\n") != L"0.5.3") return 1;
    if (ZipAssetName(L"0.5.8") != L"Backdropper-0.5.8-windows-x64.zip") return 1;
    if (!RunAndWait(TarPath(), L"--version")) return 1;

    const fs::path copyRoot = TempPath(L"BackdropperUpdaterSelfTest-" + std::to_wstring(GetCurrentProcessId()));
    fs::remove_all(copyRoot);
    fs::create_directories(copyRoot / L"payload" / L"nested");
    std::ofstream(copyRoot / L"payload" / L"file.txt") << "ok";
    std::ofstream(copyRoot / L"payload" / L"nested" / L"file.txt") << "ok";
    InstallPayloadWithBackup(copyRoot / L"payload", copyRoot / L"install", copyRoot / L"backup");
    const bool copied = fs::exists(copyRoot / L"install" / L"file.txt")
        && fs::exists(copyRoot / L"install" / L"nested" / L"file.txt");
    fs::remove_all(copyRoot);
    if (!copied) return 1;

    std::wcout << L"OK" << std::endl;
    return 0;
}

struct Args {
    fs::path installDir;
    DWORD currentPid = 0;
    std::wstring currentVersion = L"0.0.0";
    bool selfTest = false;
    bool force = false;
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
        } else if (arg == L"--force") {
            args.force = true;
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
        const fs::path extractDir = tempRoot / L"extract";
        const fs::path backupDir = tempRoot / L"backup";

        WriteStep(L"Checking latest GitHub release...");
        if (!Download(std::wstring(kVersionUrl) + L"?t=" + std::to_wstring(GetTickCount64()), versionFile)) {
            throw std::runtime_error("Could not download version metadata.");
        }
        const std::wstring latestVersion = ReadTextFile(versionFile);
        if (!ShouldInstallUpdate(latestVersion, args.currentVersion, args.force)) {
            WriteStep(L"Already up to date. Current version: " + args.currentVersion + L".");
            Sleep(2000);
            StartBackdropper(installDir);
            fs::remove_all(tempRoot);
            return 0;
        }

        const std::wstring zipAssetName = ZipAssetName(latestVersion);
        const fs::path zipPath = tempRoot / zipAssetName;
        WriteStep(L"Downloading " + zipAssetName + L"...");
        if (!Download(std::wstring(kLatestDownloadBaseUrl) + zipAssetName, zipPath)) {
            throw std::runtime_error("Could not download update ZIP.");
        }

        WriteStep(L"Extracting update...");
        if (!ExtractZip(zipPath, extractDir)) {
            throw std::runtime_error("Could not extract update ZIP.");
        }

        const fs::path settingsExe = extractDir / L"BackdropperSettings.exe";
        if (!fs::exists(settingsExe)) {
            throw std::runtime_error("The downloaded ZIP does not contain BackdropperSettings.exe.");
        }
        const fs::path payloadDir = settingsExe.parent_path();
        WriteStep(L"Verifying publisher signatures...");
        VerifyPayload(payloadDir);

        WriteStep(L"Waiting for Backdropper to close...");
        WaitForProcess(args.currentPid);

        WriteStep(L"Replacing Backdropper files...");
        try {
            InstallPayloadWithBackup(payloadDir, installDir, backupDir);
        } catch (...) {
            WriteStep(L"Files are still in use. Restarting Explorer and retrying...");
            stoppedExplorer = StopBackdropperExplorerShell();
            Sleep(2000);
            InstallPayloadWithBackup(payloadDir, installDir, backupDir);
        }

        if (stoppedExplorer) {
            StartBackdropperExplorerShell();
        }

        WriteStep(L"Update complete. Starting Backdropper...");
        StartBackdropper(installDir);
        fs::remove_all(tempRoot);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "[Backdropper] Update failed: " << ex.what() << std::endl;
        if (stoppedExplorer) {
            StartBackdropperExplorerShell();
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
