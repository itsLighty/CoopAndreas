#include <windows.h>
#include <tlhelp32.h>
#include <urlmon.h>

#include <array>
#include <string>

namespace
{
constexpr wchar_t RELEASE_BASE_URL[] =
    L"https://github.com/itsLighty/CoopAndreas/releases/download/playtest-latest/";

struct Asset
{
    const wchar_t* name;
    const wchar_t* destination;
};

constexpr std::array<Asset, 7> ASSETS{{
    {L"eax.dll", L"eax.dll"},
    {L"CoopAndreasSA.dll", L"CoopAndreasSA.dll"},
    {L"LaunchCoopAndreas.exe", L"LaunchCoopAndreas.exe"},
    {L"LaunchCoopAndreas.exe.manifest", L"LaunchCoopAndreas.exe.manifest"},
    {L"server.exe", L"server.exe"},
    {L"main.scm", L"CoopAndreas\\main.scm"},
    {L"script.img", L"CoopAndreas\\script.img"},
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

bool HasContent(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes))
        return false;
    return attributes.nFileSizeHigh != 0 || attributes.nFileSizeLow != 0;
}

bool DownloadAssets(HWND progressWindow, const std::wstring& stagingDirectory, std::wstring& error)
{
    for (size_t index = 0; index < ASSETS.size(); ++index)
    {
        const Asset& asset = ASSETS[index];
        SetProgress(progressWindow,
            L"CoopAndreas playtest update - downloading " + std::to_wstring(index + 1) + L"/" +
                std::to_wstring(ASSETS.size()) + L": " + asset.name);

        const std::wstring destination = JoinPath(stagingDirectory, asset.name);
        DeleteFileW(destination.c_str());
        const std::wstring url = std::wstring(RELEASE_BASE_URL) + asset.name;
        const HRESULT result = URLDownloadToFileW(nullptr, url.c_str(), destination.c_str(), 0, nullptr);
        if (FAILED(result) || !HasContent(destination))
        {
            error = L"Could not download " + std::wstring(asset.name) +
                    L" from the latest GitHub playtest release.\n\nHRESULT: " + std::to_wstring(result);
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
        error = L"The original eax.dll was not found beside gta_sa.exe.\n\n"
                L"Restore a clean GTA San Andreas 1.0 US installation before installing CoopAndreas.";
        return false;
    }
    if (!MoveFileExW(eax.c_str(), original.c_str(), MOVEFILE_WRITE_THROUGH))
    {
        error = L"Could not preserve eax.dll as eax_orig.dll.\n\nWindows error: " +
                std::to_wstring(GetLastError());
        return false;
    }
    return true;
}

bool DeployAssets(
    HWND progressWindow, const std::wstring& gameDirectory, const std::wstring& stagingDirectory, std::wstring& error)
{
    if (!EnsureDirectory(JoinPath(gameDirectory, L"CoopAndreas")))
    {
        error = L"Could not create the CoopAndreas data directory.\n\nWindows error: " +
                std::to_wstring(GetLastError());
        return false;
    }
    if (!PrepareEaxProxy(gameDirectory, error))
        return false;

    for (size_t index = 0; index < ASSETS.size(); ++index)
    {
        const Asset& asset = ASSETS[index];
        SetProgress(progressWindow,
            L"CoopAndreas playtest update - installing " + std::to_wstring(index + 1) + L"/" +
                std::to_wstring(ASSETS.size()) + L": " + asset.name);
        const std::wstring source = JoinPath(stagingDirectory, asset.name);
        const std::wstring destination = JoinPath(gameDirectory, asset.destination);
        if (!MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            error = L"Could not install " + std::wstring(asset.destination) +
                    L". Close GTA San Andreas and try again.\n\nWindows error: " + std::to_wstring(GetLastError());
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
    const HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\CoopAndreasPlaytestUpdater");
    if (mutex == nullptr || GetLastError() == ERROR_ALREADY_EXISTS)
    {
        MessageBoxW(nullptr, L"The CoopAndreas updater is already running.", L"CoopAndreas Playtest", MB_OK | MB_ICONINFORMATION);
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
    if (IsProcessRunning(L"gta_sa.exe"))
    {
        MessageBoxW(nullptr, L"Close GTA San Andreas before updating CoopAndreas.", L"CoopAndreas Playtest",
            MB_OK | MB_ICONWARNING);
        CloseHandle(mutex);
        return 1;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const HWND progressWindow = CreateWindowExW(WS_EX_APPWINDOW, L"STATIC",
        L"CoopAndreas playtest update - preparing...", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | SS_CENTER,
        CW_USEDEFAULT, CW_USEDEFAULT, 620, 110, nullptr, nullptr, instance, nullptr);
    if (progressWindow)
    {
        ShowWindow(progressWindow, SW_SHOW);
        UpdateWindow(progressWindow);
    }

    const std::wstring stagingDirectory = JoinPath(gameDirectory, L"CoopAndreas.update");
    std::wstring error;
    bool success = EnsureDirectory(stagingDirectory);
    if (!success)
        error = L"Could not create the update staging directory.\n\nWindows error: " + std::to_wstring(GetLastError());
    if (success)
        success = DownloadAssets(progressWindow, stagingDirectory, error);
    if (success)
        success = DeployAssets(progressWindow, gameDirectory, stagingDirectory, error);

    if (progressWindow)
        DestroyWindow(progressWindow);
    CoUninitialize();

    if (!success)
    {
        MessageBoxW(nullptr, error.c_str(), L"CoopAndreas Playtest Update Failed", MB_OK | MB_ICONERROR);
        CloseHandle(mutex);
        return 1;
    }

    const int choice = MessageBoxW(nullptr,
        L"CoopAndreas is up to date.\n\nYes: Host & Play (starts the local server)\n"
        L"No: Join & Play\nCancel: Update only",
        L"CoopAndreas Playtest", MB_YESNOCANCEL | MB_ICONINFORMATION);

    if (choice == IDYES && !IsProcessRunning(L"server.exe"))
    {
        if (!Launch(JoinPath(gameDirectory, L"server.exe"), gameDirectory, CREATE_NO_WINDOW))
        {
            MessageBoxW(nullptr, L"The server could not be started.", L"CoopAndreas Playtest", MB_OK | MB_ICONERROR);
            CloseHandle(mutex);
            return 1;
        }
    }
    if (choice == IDYES || choice == IDNO)
    {
        if (!Launch(JoinPath(gameDirectory, L"LaunchCoopAndreas.exe"), gameDirectory, 0))
        {
            MessageBoxW(nullptr, L"GTA San Andreas could not be started.", L"CoopAndreas Playtest", MB_OK | MB_ICONERROR);
            CloseHandle(mutex);
            return 1;
        }
    }

    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}
