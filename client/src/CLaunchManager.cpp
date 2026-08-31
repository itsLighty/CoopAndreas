#include "stdafx.h"
#include <CLaunchManager.h>

void CLaunchManager::CollectCommandLineArgs()
{
    // The proxy already limits loading to explicit --coop launches. Keeping
    // startup key-free makes local multiplayer testing immediate and avoids
    // depending on an external key service.
}
