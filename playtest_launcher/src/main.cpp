#include <windows.h>
#include <tlhelp32.h>
#include <wincrypt.h>

#include <array>
#include <cstdint>
#include <cwchar>
#include <sstream>
#include <string>
#include <vector>

namespace
{
constexpr wchar_t RELEASE_BASE_URL[] =
    L"https://github.com/itsLighty/CoopAndreas/releases/download/playtest-latest/";
constexpr wchar_t MANIFEST_NAME[] = L"playtest-manifest.txt";
constexpr int DOWNLOAD_ATTEMPTS = 3;

struct Asset
{
    const wchar_t* packagePath;
    const wchar_t* destination;
};

constexpr std::array<Asset, 8> ASSETS{{
    {L"eax.dll", L"eax.dll"},
    {L"CoopAndreasSA.dll", L"CoopAndreasSA.dll"},
    {L"LaunchCoopAndreas.exe", L"LaunchCoopAndreas.exe"},
    {L"LaunchCoopAndreas.exe.manifest", L"LaunchCoopAndreas.exe.manifest"},
    {L"server.exe", L"server.exe"},
    {L"CoopAndreas\\main.scm", L"CoopAndreas\\main.scm"},
    {L"CoopAndreas\\script.img", L"CoopAndreas\\script.img"},
    {L"CoopAndreas\\playtest-build.txt", L"CoopAndreas\\playtest-build.txt"},
}};

struct ReleaseManifest
{
    std::string commit;
    std::wstring package;
    std::string sha256;
    uint64_t size = 0;
};

std::wstring JoinPath(const std::wstring& left, const std::wstring& right)
{
    return left + (left.empty() || left.back() == L'\\' ? L"" : L"\\") + right;
}

std::wstring GetExecutableDirectory()
{
    std::vector<wchar_t> path(MAX_PATH);
    for (;;)
    {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0)
            return {};
        if (length < path.size() - 1)
        {
            std::wstring directory(path.data(), length);
            const size_t separator = directory.find_last_of(L"\\/");
            return separator == std::wstring::npos ? std::wstring{} : directory.substr(0, separator);
        }
        path.resize(path.size() * 2);
    }
}

bool FileExists(const std::wstring& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool DirectoryExists(const std::wstring& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool EnsureDirectory(const std::wstring& path)
{
    return DirectoryExists(path) || CreateDirectoryW(path.c_str(), nullptr) != FALSE ||
           GetLastError() == ERROR_ALREADY_EXISTS;
}

bool RemoveTree(const std::wstring& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    {
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
        return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0
            ? RemoveDirectoryW(path.c_str()) != FALSE
            : DeleteFileW(path.c_str()) != FALSE;
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
    {
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
        return DeleteFileW(path.c_str()) != FALSE;
    }

    WIN32_FIND_DATAW entry{};
    HANDLE search = FindFirstFileW(JoinPath(path, L"*").c_str(), &entry);
    if (search != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (wcscmp(entry.cFileName, L".") == 0 || wcscmp(entry.cFileName, L"..") == 0)
                continue;
            if (!RemoveTree(JoinPath(path, entry.cFileName)))
            {
                FindClose(search);
                return false;
            }
        } while (FindNextFileW(search, &entry));
        FindClose(search);
    }
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    return RemoveDirectoryW(path.c_str()) != FALSE;
}

bool ResetDirectory(const std::wstring& path)
{
    return (!DirectoryExists(path) || RemoveTree(path)) && EnsureDirectory(path);
}

bool IsProcessRunning(const wchar_t* executableName)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return false;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            if (_wcsicmp(entry.szExeFile, executableName) == 0)
            {
                found = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

void PumpMessages()
{
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

void SetProgress(HWND window, const std::wstring& message)
{
    if (window)
    {
        SetWindowTextW(window, message.c_str());
        UpdateWindow(window);
    }
    PumpMessages();
}

void RetryPause()
{
    for (int index = 0; index < 10; ++index)
    {
        Sleep(100);
        PumpMessages();
    }
}

std::wstring HexCode(HRESULT result)
{
    std::wstringstream stream;
    stream << L"0x" << std::hex << static_cast<unsigned long>(result);
    return stream.str();
}

bool DownloadFile(const std::wstring& url, const std::wstring& destination, std::wstring& error)
{
    using DownloadFunction = HRESULT(WINAPI*)(void*, LPCWSTR, LPCWSTR, DWORD, void*);
    HMODULE urlmon = LoadLibraryW(L"urlmon.dll");
    auto download = urlmon ? reinterpret_cast<DownloadFunction>(GetProcAddress(urlmon, "URLDownloadToFileW")) : nullptr;
    if (!download)
    {
        if (urlmon)
            FreeLibrary(urlmon);
        error = L"Windows URL download support (urlmon.dll) is unavailable.";
        return false;
    }

    DeleteFileW(destination.c_str());
    const HRESULT result = download(nullptr, url.c_str(), destination.c_str(), 0, nullptr);
    FreeLibrary(urlmon);
    if (FAILED(result) || !FileExists(destination))
    {
        DeleteFileW(destination.c_str());
        error = L"Download failed with HRESULT " + HexCode(result) + L".";
        return false;
    }
    return true;
}

bool ReadSmallTextFile(const std::wstring& path, std::string& text, std::wstring& error)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        error = L"Could not read the release manifest. Windows error: " + std::to_wstring(GetLastError());
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 65536)
    {
        CloseHandle(file);
        error = L"The release manifest has an invalid size.";
        return false;
    }
    text.resize(static_cast<size_t>(size.QuadPart));
    DWORD bytesRead = 0;
    const bool success = ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &bytesRead, nullptr) != FALSE &&
                         bytesRead == text.size();
    CloseHandle(file);
    if (!success)
    {
        error = L"The release manifest could not be read completely.";
        return false;
    }
    return true;
}

bool IsLowerHex(const std::string& value, size_t expectedLength)
{
    if (value.size() != expectedLength)
        return false;
    for (char character : value)
    {
        if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f')))
            return false;
    }
    return true;
}

std::wstring AsciiToWide(const std::string& value)
{
    return std::wstring(value.begin(), value.end());
}

bool ParseManifest(const std::string& text, ReleaseManifest& manifest, std::wstring& error)
{
    std::string format;
    std::string package;
    std::string size;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const size_t separator = line.find('=');
        if (separator == std::string::npos)
            continue;
        const std::string key = line.substr(0, separator);
        const std::string value = line.substr(separator + 1);
        if (key == "format")
            format = value;
        else if (key == "commit")
            manifest.commit = value;
        else if (key == "package")
            package = value;
        else if (key == "sha256")
            manifest.sha256 = value;
        else if (key == "size")
            size = value;
    }

    const std::string expectedPackage = "CoopAndreas-playtest-" + manifest.commit + ".zip";
    if (format != "1" || !IsLowerHex(manifest.commit, 40) || package != expectedPackage ||
        !IsLowerHex(manifest.sha256, 64) || size.empty())
    {
        error = L"The rolling release manifest is malformed or untrusted.";
        return false;
    }
    try
    {
        size_t consumed = 0;
        manifest.size = std::stoull(size, &consumed);
        if (consumed != size.size() || manifest.size == 0)
            throw std::invalid_argument("size");
    }
    catch (...)
    {
        error = L"The rolling release manifest contains an invalid package size.";
        return false;
    }
    manifest.package = AsciiToWide(package);
    return true;
}

bool LoadManifest(const std::wstring& path, ReleaseManifest& manifest, std::wstring& error)
{
    std::string text;
    if (!ReadSmallTextFile(path, text, error))
        return false;
    manifest = {};
    return ParseManifest(text, manifest, error);
}

bool HashFileSha256(const std::wstring& path, std::string& digest, uint64_t& size, std::wstring& error)
{
    using AcquireContext = BOOL(WINAPI*)(HCRYPTPROV*, LPCWSTR, LPCWSTR, DWORD, DWORD);
    using CreateHash = BOOL(WINAPI*)(HCRYPTPROV, ALG_ID, HCRYPTKEY, DWORD, HCRYPTHASH*);
    using HashData = BOOL(WINAPI*)(HCRYPTHASH, const BYTE*, DWORD, DWORD);
    using GetHashParam = BOOL(WINAPI*)(HCRYPTHASH, DWORD, BYTE*, DWORD*, DWORD);
    using DestroyHash = BOOL(WINAPI*)(HCRYPTHASH);
    using ReleaseContext = BOOL(WINAPI*)(HCRYPTPROV, DWORD);

    HMODULE crypto = LoadLibraryW(L"advapi32.dll");
    auto acquireContext = crypto ? reinterpret_cast<AcquireContext>(GetProcAddress(crypto, "CryptAcquireContextW")) : nullptr;
    auto createHash = crypto ? reinterpret_cast<CreateHash>(GetProcAddress(crypto, "CryptCreateHash")) : nullptr;
    auto hashData = crypto ? reinterpret_cast<HashData>(GetProcAddress(crypto, "CryptHashData")) : nullptr;
    auto getHashParam = crypto ? reinterpret_cast<GetHashParam>(GetProcAddress(crypto, "CryptGetHashParam")) : nullptr;
    auto destroyHash = crypto ? reinterpret_cast<DestroyHash>(GetProcAddress(crypto, "CryptDestroyHash")) : nullptr;
    auto releaseContext = crypto ? reinterpret_cast<ReleaseContext>(GetProcAddress(crypto, "CryptReleaseContext")) : nullptr;
    if (!acquireContext || !createHash || !hashData || !getHashParam || !destroyHash || !releaseContext)
    {
        if (crypto)
            FreeLibrary(crypto);
        error = L"Windows SHA-256 support is unavailable.";
        return false;
    }

    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    bool success = file != INVALID_HANDLE_VALUE &&
                   acquireContext(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) != FALSE &&
                   createHash(provider, CALG_SHA_256, 0, 0, &hash) != FALSE;
    size = 0;
    std::array<BYTE, 64 * 1024> buffer{};
    while (success)
    {
        DWORD bytesRead = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr))
        {
            success = false;
            break;
        }
        if (bytesRead == 0)
            break;
        size += bytesRead;
        success = hashData(hash, buffer.data(), bytesRead, 0) != FALSE;
    }

    std::array<BYTE, 32> hashBytes{};
    DWORD hashLength = static_cast<DWORD>(hashBytes.size());
    success = success && getHashParam(hash, HP_HASHVAL, hashBytes.data(), &hashLength, 0) != FALSE &&
              hashLength == hashBytes.size();
    if (hash)
        destroyHash(hash);
    if (provider)
        releaseContext(provider, 0);
    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);
    FreeLibrary(crypto);

    if (!success)
    {
        error = L"The downloaded package could not be hashed. Windows error: " + std::to_wstring(GetLastError());
        return false;
    }
    static constexpr char HEX[] = "0123456789abcdef";
    digest.clear();
    digest.reserve(hashBytes.size() * 2);
    for (BYTE byte : hashBytes)
    {
        digest.push_back(HEX[byte >> 4]);
        digest.push_back(HEX[byte & 0x0f]);
    }
    return true;
}

std::wstring CacheBustedUrl(const std::wstring& asset, int attempt)
{
    return std::wstring(RELEASE_BASE_URL) + asset + L"?coop_updater=" +
           std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(attempt);
}

bool ManifestsMatch(const ReleaseManifest& left, const ReleaseManifest& right)
{
    return left.commit == right.commit && left.package == right.package && left.sha256 == right.sha256 &&
           left.size == right.size;
}

bool DownloadVerifiedPackage(HWND progressWindow, const std::wstring& stagingDirectory,
    ReleaseManifest& acceptedManifest, std::wstring& packagePath, std::wstring& error)
{
    const std::wstring manifestPath = JoinPath(stagingDirectory, L"manifest.download");
    const std::wstring confirmationPath = JoinPath(stagingDirectory, L"manifest.confirm");
    std::wstring lastError;
    for (int attempt = 1; attempt <= DOWNLOAD_ATTEMPTS; ++attempt)
    {
        SetProgress(progressWindow, L"CoopAndreas update - checking latest main build (attempt " +
            std::to_wstring(attempt) + L"/" + std::to_wstring(DOWNLOAD_ATTEMPTS) + L")...");
        ReleaseManifest manifest;
        if (!DownloadFile(CacheBustedUrl(MANIFEST_NAME, attempt), manifestPath, lastError) ||
            !LoadManifest(manifestPath, manifest, lastError))
        {
            if (attempt < DOWNLOAD_ATTEMPTS)
                RetryPause();
            continue;
        }

        packagePath = JoinPath(stagingDirectory, manifest.package);
        SetProgress(progressWindow, L"CoopAndreas update - downloading build " +
            AsciiToWide(manifest.commit.substr(0, 8)) + L"...");
        if (!DownloadFile(CacheBustedUrl(manifest.package, attempt), packagePath, lastError))
        {
            if (attempt < DOWNLOAD_ATTEMPTS)
                RetryPause();
            continue;
        }

        std::string actualHash;
        uint64_t actualSize = 0;
        if (!HashFileSha256(packagePath, actualHash, actualSize, lastError) || actualHash != manifest.sha256 ||
            actualSize != manifest.size)
        {
            DeleteFileW(packagePath.c_str());
            lastError = L"The downloaded package did not match the rolling release SHA-256 and size.";
            if (attempt < DOWNLOAD_ATTEMPTS)
                RetryPause();
            continue;
        }

        ReleaseManifest confirmation;
        if (!DownloadFile(CacheBustedUrl(MANIFEST_NAME, attempt + DOWNLOAD_ATTEMPTS), confirmationPath, lastError) ||
            !LoadManifest(confirmationPath, confirmation, lastError) || !ManifestsMatch(manifest, confirmation))
        {
            DeleteFileW(packagePath.c_str());
            lastError = L"A newer main build appeared while downloading; retrying with that build.";
            if (attempt < DOWNLOAD_ATTEMPTS)
                RetryPause();
            continue;
        }

        acceptedManifest = manifest;
        return true;
    }

    error = L"Could not retrieve a coherent, verified playtest build after three attempts.\n\n" + lastError +
            L"\n\nCheck your internet connection. If main is currently building, wait a minute and run this EXE again.";
    return false;
}

std::wstring PowerShellLiteral(const std::wstring& value)
{
    std::wstring escaped;
    escaped.reserve(value.size());
    for (wchar_t character : value)
    {
        escaped.push_back(character);
        if (character == L'\'')
            escaped.push_back(L'\'');
    }
    return L"'" + escaped + L"'";
}

bool ExtractPackage(const std::wstring& packagePath, const std::wstring& payloadDirectory, std::wstring& error)
{
    if (!ResetDirectory(payloadDirectory))
    {
        error = L"Could not reset the update extraction directory. Windows error: " +
                std::to_wstring(GetLastError());
        return false;
    }
    std::vector<wchar_t> systemDirectory(MAX_PATH);
    const UINT systemDirectoryLength =
        GetSystemDirectoryW(systemDirectory.data(), static_cast<UINT>(systemDirectory.size()));
    if (systemDirectoryLength == 0 || systemDirectoryLength >= systemDirectory.size())
    {
        error = L"Could not locate Windows PowerShell. Windows error: " + std::to_wstring(GetLastError());
        return false;
    }
    const std::wstring powershell =
        JoinPath(std::wstring(systemDirectory.data(), systemDirectoryLength), L"WindowsPowerShell\\v1.0\\powershell.exe");
    if (!FileExists(powershell))
    {
        error = L"Windows PowerShell is unavailable at its trusted system location.";
        return false;
    }

    std::wstring command = L"\"" + powershell + L"\" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"";
    command += L"Expand-Archive -LiteralPath " + PowerShellLiteral(packagePath) +
               L" -DestinationPath " + PowerShellLiteral(payloadDirectory) + L" -Force\"";

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(powershell.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
            &startup, &process))
    {
        error = L"Could not start PowerShell to extract the verified package. Windows error: " +
                std::to_wstring(GetLastError());
        return false;
    }
    CloseHandle(process.hThread);
    const DWORD waitResult = WaitForSingleObject(process.hProcess, 300000);
    DWORD exitCode = 1;
    if (waitResult == WAIT_TIMEOUT)
        TerminateProcess(process.hProcess, 1);
    else
        GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    if (waitResult != WAIT_OBJECT_0 || exitCode != 0)
    {
        error = waitResult == WAIT_TIMEOUT
            ? L"Package extraction timed out after five minutes."
            : L"PowerShell could not extract the verified package. Exit code: " + std::to_wstring(exitCode);
        return false;
    }
    for (const Asset& asset : ASSETS)
    {
        if (!FileExists(JoinPath(payloadDirectory, asset.packagePath)))
        {
            error = L"The verified package is incomplete: " + std::wstring(asset.packagePath) + L" is missing.";
            return false;
        }
    }
    return true;
}

bool PrepareEaxProxy(const std::wstring& gameDirectory, std::wstring& error)
{
    const std::wstring eax = JoinPath(gameDirectory, L"eax.dll");
    const std::wstring original = JoinPath(gameDirectory, L"eax_orig.dll");
    if (FileExists(original))
        return true;
    if (!FileExists(eax))
    {
        error = L"The original eax.dll was not found beside gta_sa.exe. Restore a clean GTA San Andreas 1.0 US "
                L"installation before installing CoopAndreas.";
        return false;
    }
    if (FileExists(JoinPath(gameDirectory, L"CoopAndreasSA.dll")))
    {
        error = L"CoopAndreasSA.dll exists but eax_orig.dll does not, so the updater cannot safely identify the "
                L"original loader. Restore the game's original eax.dll and try again.";
        return false;
    }
    if (!CopyFileW(eax.c_str(), original.c_str(), TRUE))
    {
        error = L"Could not safely preserve eax.dll as eax_orig.dll. Windows error: " +
                std::to_wstring(GetLastError());
        return false;
    }
    return true;
}

bool ReplaceFromStaging(const std::wstring& source, const std::wstring& destination, std::wstring& error)
{
    const std::wstring temporary = destination + L".coop-new";
    DeleteFileW(temporary.c_str());
    if (!CopyFileW(source.c_str(), temporary.c_str(), FALSE) ||
        !MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        const DWORD windowsError = GetLastError();
        DeleteFileW(temporary.c_str());
        error = L"Could not install " + destination + L". Windows error: " + std::to_wstring(windowsError);
        return false;
    }
    return true;
}

bool RollBack(const std::wstring& gameDirectory, const std::wstring& backupDirectory,
    const std::array<bool, ASSETS.size()>& existed, size_t count)
{
    bool restored = true;
    for (size_t reverse = count; reverse > 0; --reverse)
    {
        const size_t index = reverse - 1;
        const std::wstring destination = JoinPath(gameDirectory, ASSETS[index].destination);
        if (existed[index])
        {
            std::wstring ignored;
            if (!ReplaceFromStaging(
                    JoinPath(backupDirectory, std::to_wstring(index) + L".bak"), destination, ignored))
                restored = false;
        }
        else if (FileExists(destination) && !DeleteFileW(destination.c_str()))
        {
            restored = false;
        }
    }
    return restored;
}

bool DeployAssets(HWND progressWindow, const std::wstring& gameDirectory, const std::wstring& payloadDirectory,
    const std::wstring& backupDirectory, std::wstring& error)
{
    if (!EnsureDirectory(JoinPath(gameDirectory, L"CoopAndreas")) || !ResetDirectory(backupDirectory))
    {
        error = L"Could not prepare the CoopAndreas update directories. Windows error: " +
                std::to_wstring(GetLastError());
        return false;
    }
    if (!PrepareEaxProxy(gameDirectory, error))
        return false;

    std::array<bool, ASSETS.size()> existed{};
    for (size_t index = 0; index < ASSETS.size(); ++index)
    {
        const std::wstring destination = JoinPath(gameDirectory, ASSETS[index].destination);
        existed[index] = FileExists(destination);
        if (existed[index] && !CopyFileW(destination.c_str(),
                                  JoinPath(backupDirectory, std::to_wstring(index) + L".bak").c_str(), FALSE))
        {
            error = L"Could not back up " + destination + L". Windows error: " + std::to_wstring(GetLastError());
            return false;
        }
    }

    for (size_t index = 0; index < ASSETS.size(); ++index)
    {
        SetProgress(progressWindow, L"CoopAndreas update - installing " + std::to_wstring(index + 1) + L"/" +
            std::to_wstring(ASSETS.size()) + L": " + ASSETS[index].destination);
        const std::wstring source = JoinPath(payloadDirectory, ASSETS[index].packagePath);
        const std::wstring destination = JoinPath(gameDirectory, ASSETS[index].destination);
        if (!ReplaceFromStaging(source, destination, error))
        {
            const std::wstring installError = error;
            const bool restored = RollBack(gameDirectory, backupDirectory, existed, index + 1);
            error = installError + (restored
                    ? L"\n\nThe previous CoopAndreas build was restored."
                    : L"\n\nRollback could not restore every previous file. Keep GTA closed and run the updater again.");
            return false;
        }
    }
    return true;
}

bool Launch(const std::wstring& executable, const std::wstring& workingDirectory, DWORD creationFlags)
{
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::wstring command = L"\"" + executable + L"\"";
    const BOOL created = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, creationFlags,
        nullptr, workingDirectory.c_str(), &startup, &process);
    if (!created)
        return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}
}  // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int)
{
    const bool updateOnly = std::wcsstr(GetCommandLineW(), L"--update-only") != nullptr ||
                            std::wcsstr(GetCommandLineW(), L"--install-only") != nullptr;
    const HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\CoopAndreasPlaytestUpdater");
    if (mutex == nullptr || GetLastError() == ERROR_ALREADY_EXISTS)
    {
        MessageBoxW(nullptr, L"The CoopAndreas updater is already running.", L"CoopAndreas Playtest",
            MB_OK | MB_ICONINFORMATION);
        if (mutex)
            CloseHandle(mutex);
        return 1;
    }

    const std::wstring gameDirectory = GetExecutableDirectory();
    if (gameDirectory.empty() || !FileExists(JoinPath(gameDirectory, L"gta_sa.exe")))
    {
        MessageBoxW(nullptr, L"Place CoopAndreasPlaytest.exe beside gta_sa.exe and run it again.",
            L"CoopAndreas Playtest", MB_OK | MB_ICONERROR);
        CloseHandle(mutex);
        return 1;
    }
    if (IsProcessRunning(L"gta_sa.exe") || IsProcessRunning(L"server.exe"))
    {
        MessageBoxW(nullptr, L"Close GTA San Andreas and server.exe before updating CoopAndreas.",
            L"CoopAndreas Playtest", MB_OK | MB_ICONWARNING);
        CloseHandle(mutex);
        return 1;
    }

    const HWND progressWindow = CreateWindowExW(WS_EX_APPWINDOW, L"STATIC",
        L"CoopAndreas update - preparing...", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | SS_CENTER,
        CW_USEDEFAULT, CW_USEDEFAULT, 680, 110, nullptr, nullptr, instance, nullptr);
    if (progressWindow)
    {
        ShowWindow(progressWindow, SW_SHOW);
        UpdateWindow(progressWindow);
    }

    const std::wstring stagingDirectory = JoinPath(gameDirectory, L"CoopAndreas.update");
    const std::wstring payloadDirectory = JoinPath(stagingDirectory, L"payload");
    const std::wstring backupDirectory = JoinPath(stagingDirectory, L"backup");
    std::wstring packagePath;
    std::wstring error;
    ReleaseManifest manifest;
    bool success = ResetDirectory(stagingDirectory);
    if (!success)
        error = L"Could not reset the update staging directory. Windows error: " + std::to_wstring(GetLastError());
    if (success)
        success = DownloadVerifiedPackage(progressWindow, stagingDirectory, manifest, packagePath, error);
    if (success)
    {
        SetProgress(progressWindow, L"CoopAndreas update - extracting verified build...");
        success = ExtractPackage(packagePath, payloadDirectory, error);
    }
    if (success)
        success = DeployAssets(progressWindow, gameDirectory, payloadDirectory, backupDirectory, error);

    if (progressWindow)
        DestroyWindow(progressWindow);
    if (!success)
    {
        MessageBoxW(nullptr, error.c_str(), L"CoopAndreas Playtest Update Failed", MB_OK | MB_ICONERROR);
        CloseHandle(mutex);
        return 1;
    }

    if (updateOnly)
    {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 0;
    }

    const std::wstring shortCommit = AsciiToWide(manifest.commit.substr(0, 8));
    const int choice = MessageBoxW(nullptr,
        (L"CoopAndreas is up to date with main build " + shortCommit +
            L".\n\nYes: Host & Play (starts the local server)\nNo: Join & Play\nCancel: Update only").c_str(),
        L"CoopAndreas Playtest", MB_YESNOCANCEL | MB_ICONINFORMATION);
    if (choice == IDYES && !Launch(JoinPath(gameDirectory, L"server.exe"), gameDirectory, CREATE_NO_WINDOW))
    {
        MessageBoxW(nullptr, L"The server could not be started.", L"CoopAndreas Playtest", MB_OK | MB_ICONERROR);
        CloseHandle(mutex);
        return 1;
    }
    if ((choice == IDYES || choice == IDNO) &&
        !Launch(JoinPath(gameDirectory, L"LaunchCoopAndreas.exe"), gameDirectory, 0))
    {
        MessageBoxW(nullptr, L"GTA San Andreas could not be started.", L"CoopAndreas Playtest",
            MB_OK | MB_ICONERROR);
        CloseHandle(mutex);
        return 1;
    }

    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}
