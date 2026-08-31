#pragma once
#include <string>
#include <windows.h>

class Launcher
{
public:
    static void LaunchProcess(const std::string& cmdLine)
    {
        STARTUPINFO si = {sizeof(si)};
        PROCESS_INFORMATION pi = {0};

        char launcherPath[MAX_PATH];
        const DWORD launcherPathLength = GetModuleFileNameA(NULL, launcherPath, MAX_PATH);
        if (launcherPathLength == 0 || launcherPathLength >= MAX_PATH)
        {
            MessageBoxA(NULL,
                        "Could not determine the CoopAndreas launcher folder.",
                        "CoopAndreas Launcher Error",
                        MB_ICONERROR | MB_OK);
            return;
        }

        std::string launcherDirectory(launcherPath, launcherPathLength);
        const size_t separator = launcherDirectory.find_last_of("\\/");
        if (separator == std::string::npos)
        {
            MessageBoxA(NULL,
                        "Could not determine the CoopAndreas launcher folder.",
                        "CoopAndreas Launcher Error",
                        MB_ICONERROR | MB_OK);
            return;
        }
        launcherDirectory.resize(separator);

        const std::string executablePath = launcherDirectory + "\\gta_sa.exe";

        char args[512];
        strcpy_s(args, sizeof(args), cmdLine.c_str());

        BOOL success = CreateProcessA(executablePath.c_str(),
                                      args,
                                      NULL,
                                      NULL,
                                      FALSE,
                                      0,
                                      NULL,
                                      launcherDirectory.c_str(),
                                      &si,
                                      &pi);

        if (success)
        {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        else
        {
            std::string outErrorMessage;
            DWORD err = GetLastError();
            switch (err)
            {
                case ERROR_FILE_NOT_FOUND:
                case ERROR_PATH_NOT_FOUND:
                    if (GetFileAttributesA((launcherDirectory + "\\gta-sa.exe").c_str()) != INVALID_FILE_ATTRIBUTES)
                    {
                        outErrorMessage = "Steam version of GTA San Andreas detected (gta-sa.exe)!\n\n"
                                          "This mod requires GTA:SA v1.0 US HOODLUM.\n\n"
                                          "Please use a downgrader to convert your game to v1.0 US HOODLUM.";
                    }
                    else
                    {
                        outErrorMessage =
                            "Could not find gta_sa.exe!\n\n"
                            "Please make sure the launcher is placed in your GTA San Andreas installation folder.";
                    }
                    break;

                case ERROR_ACCESS_DENIED:
                case ERROR_ELEVATION_REQUIRED:
                    outErrorMessage = "Windows blocked GTA San Andreas from launching.\n\n"
                                      "Try right-clicking the launcher and selecting \"Run as administrator\".";
                    break;

                case ERROR_BAD_EXE_FORMAT:
                    outErrorMessage =
                        "The gta_sa.exe file appears to be corrupted or invalid.\n\n"
                        "If you are using a modified executable, make sure it is a valid v1.0 US HOODLUM gta_sa.exe.";
                    break;

                case ERROR_CANCELLED:
                    outErrorMessage = "Launch canceled. The Windows Administrator prompt (UAC) was declined.";
                    break;

                case ERROR_SHARING_VIOLATION:
                case ERROR_LOCK_VIOLATION:
                    outErrorMessage =
                        "GTA San Andreas is already running!\n\n"
                        "Please close any open instances of GTA:SA or check Task Manager before trying again.";
                    break;

                default:
                    outErrorMessage = "Failed to launch GTA San Andreas (Error Code: " + std::to_string(err) +
                                      ").\n\n"
                                      "If this keeps happening, ask for help in our Discord server!";
                    break;
            }
            MessageBox(NULL, outErrorMessage.c_str(), "CoopAndreas Launcher Error", MB_ICONERROR | MB_OK);
        }
    }
};
