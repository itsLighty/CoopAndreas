#include <windows.h>

#include "launcher.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    Launcher::LaunchProcess("\"gta_sa.exe\" --coop");
    return 0;
}
