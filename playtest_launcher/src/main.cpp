#include <windows.h>
#include <tlhelp32.h>

#include <array>
#include <cwchar>
#include <string>

namespace
{
enum ResourceId
{
    IDR_EAX = 201,
    IDR_CLIENT,
    IDR_LAUNCHER,
    IDR_LAUNCHER_MANIFEST,
    IDR_SERVER,
    IDR_MAIN_SCM,
    IDR_SCRIPT_IMG,
};

struct Asset
{
    int resourceId;
    const wchar_t* name;
    const wchar_t* destination;
};

constexpr std::array<Asset, 7> ASSETS{{
    {IDR_EAX, L"eax.dll", L"eax.dll"},
    {IDR_CLIENT, L"CoopAndreasSA.dll", L"CoopAndreasSA.dll"},
    {IDR_LAUNCHER, L"LaunchCoopAndreas.exe", L"LaunchCoopAndreas.exe"},
    {IDR_LAUNCHER_MANIFEST, L"LaunchCoopAndreas.exe.manifest", L"LaunchCoopAndreas.exe.manifest"},
    {IDR_SERVER, L"server.exe", L"server.exe"},
    {IDR_MAIN_SCM, L"main.scm", L"CoopAndreas\\main.scm"},
    {IDR_SCRIPT_IMG, L"script.img", L"CoopAndreas\\script.img"},
}};

std::wstring JoinPath(const std::wstring& left, const std::wstring& right)
{
    return left + (left.empty() || left.back() == L'\\' ? L"" : L"\\") + right;
}

std::wstring GetExecutableDirectory()
{
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length == MAX_PATH)
        return {};
    std::wstring directory(path, length);
    const size_t separator = directory.find_last_of(L"\\/");
    return separator == std::wstring::npos ? std::wstring{} : directory.substr(0, separator);
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
    return DirectoryExists(path) || CreateDirectoryW(path.c_str(), nullptr) != FALSE || GetLastError() == ERROR_ALREADY_EXISTS;
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
    SetWindowTextW(window, message.c_str());
    UpdateWindow(window);
    PumpMessages();
}

bool ExtractAsset(HINSTANCE instance, const Asset& asset, const std::wstring& path, std::wstring& error)
{
    HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(asset.resourceId), MAKEINTRESOURCEW(10));
    HGLOBAL loaded = resource ? LoadResource(instance, resource) : nullptr;
    const void* bytes = loaded ? LockResource(loaded) : nullptr;
    const DWORD size = resource ? SizeofResource(instance, resource) : 0;
    if (!resource || !loaded || !bytes || size == 0)
    {
        error = L"The embedded " + std::wstring(asset.name) + L" payload is missing.";
        return false;
    }

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        error = L"Could not stage " + std::wstring(asset.name) + L". Windows error: " + std::to_wstring(GetLastError());
        return false;
    }
    DWORD written = 0;
    const bool success = WriteFile(file, bytes, size, &written, nullptr) != FALSE && written == size;
    CloseHandle(file);
    if (!success)
    {
        DeleteFileW(path.c_str());
        error = L"Could not extract " + std::wstring(asset.name) + L". Windows error: " + std::to_wstring(GetLastError());
    }
    return success;
}

bool PrepareEaxProxy(const std::wstring& gameDirectory, std::wstring& error)
{
    const std::wstring eax = JoinPath(gameDirectory, L"eax.dll");
    const std::wstring original = JoinPath(gameDirectory, L"eax_orig.dll");
    if (FileExists(original))
        return true;
    if (!FileExists(eax))
    {
        error = L"The original eax.dll was not found beside gta_sa.exe.";
        return false;
    }
    if (!MoveFileExW(eax.c_str(), original.c_str(), MOVEFILE_WRITE_THROUGH))
    {
        error = L"Could not preserve eax.dll as eax_orig.dll. Windows error: " + std::to_wstring(GetLastError());
        return false;
    }
    return true;
}

bool InstallEmbeddedBuild(
    HINSTANCE instance, HWND progressWindow, const std::wstring& gameDirectory, std::wstring& error)
{
    const std::wstring stagingDirectory = JoinPath(gameDirectory, L"CoopAndreas.install");
    if (!EnsureDirectory(stagingDirectory) || !EnsureDirectory(JoinPath(gameDirectory, L"CoopAndreas")))
    {
        error = L"Could not create the CoopAndreas installation directories.";
        return false;
    }

    for (size_t index = 0; index < ASSETS.size(); ++index)
    {
        const Asset& asset = ASSETS[index];
        SetProgress(progressWindow, L"Installing CoopAndreas " + std::to_wstring(index + 1) + L"/" +
                std::to_wstring(ASSETS.size()) + L": " + asset.name);
        const std::wstring staged = JoinPath(stagingDirectory, asset.name);
        if (!ExtractAsset(instance, asset, staged, error))
            return false;
    }
    if (!PrepareEaxProxy(gameDirectory, error))
        return false;

    for (const Asset& asset : ASSETS)
    {
        const std::wstring staged = JoinPath(stagingDirectory, asset.name);
        const std::wstring destination = JoinPath(gameDirectory, asset.destination);
        if (!MoveFileExW(staged.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            error = L"Could not install " + std::wstring(asset.destination) +
                    L". Close GTA San Andreas and try again. Windows error: " + std::to_wstring(GetLastError());
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
    const BOOL created = CreateProcessW(executable.c_str(), nullptr, nullptr, nullptr, FALSE, creationFlags, nullptr,
        workingDirectory.c_str(), &startup, &process);
    if (!created)
        return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}
}  // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int)
{
    const bool installOnly = std::wcsstr(GetCommandLineW(), L"--install-only") != nullptr;
    const HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\CoopAndreasStandalonePlaytest");
    if (mutex == nullptr || GetLastError() == ERROR_ALREADY_EXISTS)
    {
        MessageBoxW(nullptr, L"The CoopAndreas installer is already running.", L"CoopAndreas Playtest",
            MB_OK | MB_ICONINFORMATION);
        if (mutex)
            CloseHandle(mutex);
        return 1;
    }

    const std::wstring gameDirectory = GetExecutableDirectory();
    if (gameDirectory.empty() || !FileExists(JoinPath(gameDirectory, L"gta_sa.exe")))
    {
        MessageBoxW(nullptr, L"Place this EXE beside gta_sa.exe and run it again.", L"CoopAndreas Playtest",
            MB_OK | MB_ICONERROR);
        CloseHandle(mutex);
        return 1;
    }
    if (IsProcessRunning(L"gta_sa.exe"))
    {
        MessageBoxW(nullptr, L"Close GTA San Andreas before installing CoopAndreas.", L"CoopAndreas Playtest",
            MB_OK | MB_ICONWARNING);
        CloseHandle(mutex);
        return 1;
    }

    const HWND progressWindow = CreateWindowExW(WS_EX_APPWINDOW, L"STATIC", L"Installing CoopAndreas...",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | SS_CENTER, CW_USEDEFAULT, CW_USEDEFAULT, 620, 110, nullptr, nullptr,
        instance, nullptr);
    if (progressWindow)
    {
        ShowWindow(progressWindow, SW_SHOW);
        UpdateWindow(progressWindow);
    }

    std::wstring error;
    const bool success = InstallEmbeddedBuild(instance, progressWindow, gameDirectory, error);
    if (progressWindow)
        DestroyWindow(progressWindow);
    if (!success)
    {
        MessageBoxW(nullptr, error.c_str(), L"CoopAndreas Installation Failed", MB_OK | MB_ICONERROR);
        CloseHandle(mutex);
        return 1;
    }

    if (installOnly)
    {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 0;
    }

    const int choice = MessageBoxW(nullptr,
        L"CoopAndreas is installed.\n\nYes: Host & Play\nNo: Join & Play\nCancel: Install only",
        L"CoopAndreas Playtest", MB_YESNOCANCEL | MB_ICONINFORMATION);
    if (choice == IDYES && !IsProcessRunning(L"server.exe") &&
        !Launch(JoinPath(gameDirectory, L"server.exe"), gameDirectory, CREATE_NO_WINDOW))
    {
        MessageBoxW(nullptr, L"The server could not be started.", L"CoopAndreas Playtest", MB_OK | MB_ICONERROR);
        CloseHandle(mutex);
        return 1;
    }
    if ((choice == IDYES || choice == IDNO) &&
        !Launch(JoinPath(gameDirectory, L"LaunchCoopAndreas.exe"), gameDirectory, 0))
    {
        MessageBoxW(nullptr, L"GTA San Andreas could not be started.", L"CoopAndreas Playtest", MB_OK | MB_ICONERROR);
        CloseHandle(mutex);
        return 1;
    }

    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}
