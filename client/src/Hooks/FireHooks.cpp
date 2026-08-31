#include "stdafx.h"

#include "FireHooks.h"

#include "CNetworkFireManager.h"
#include "Events.h"

#include <CFireManager.h>

void FireHooks::InjectHooks()
{
    // Do not detour the three overloaded CFireManager entry points. The event trampoline for StartScriptFire
    // can lose its original target when an RPG explosion and synchronized fire replay overlap, producing the
    // observed EIP=0 crash. A frame-wide before/after observation catches native gameplay births without
    // replacing any retail function; explicit network materialization is bracketed at its direct call site.
    Events::gameProcessEvent.before += [] {
        CNetworkFireManager::ProcessAreaTransitions();
        CNetworkFireManager::BeginNativeBirthObservation(nullptr, true);
    };
    Events::gameProcessEvent.after += [] {
        CNetworkFireManager::EndNativeBirthObservation();
        CNetworkFireManager::Process();
    };
}
