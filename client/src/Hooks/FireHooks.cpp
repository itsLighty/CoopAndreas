#include "stdafx.h"

#include "FireHooks.h"

#include "CNetworkFireManager.h"
#include "Events.h"

#include <CFireManager.h>

namespace
{
static ThiscallEvent<AddressList<0x539F00, H_JUMP>, PRIORITY_AFTER, ArgPickNone,
    CFire*(CFireManager*, CVector, float, uint8_t, CEntity*, uint32_t, int8_t, uint8_t)>
    startWorldFireEvent;
static ThiscallEvent<AddressList<0x53A050, H_JUMP>, PRIORITY_AFTER, ArgPickN<CEntity*, 1>,
    CFire*(CFireManager*, CEntity*, CEntity*, float, uint8_t, uint32_t, int8_t)>
    startAttachedFireEvent;
static ThiscallEvent<AddressList<0x53A270, H_JUMP>, PRIORITY_AFTER, ArgPickN<CEntity*, 2>,
    int(CFireManager*, const CVector&, CEntity*, float, uint8_t, int8_t, int)>
    startScriptFireEvent;
}  // namespace

void FireHooks::InjectHooks()
{
    startWorldFireEvent.before += [] { CNetworkFireManager::BeginNativeBirthObservation(nullptr, false); };
    startWorldFireEvent.after += [] { CNetworkFireManager::EndNativeBirthObservation(); };
    startAttachedFireEvent.before += [](CEntity*) {
        CNetworkFireManager::BeginNativeBirthObservation(nullptr, false);
    };
    startAttachedFireEvent.after += [](CEntity*) { CNetworkFireManager::EndNativeBirthObservation(); };
    startScriptFireEvent.before += [](CEntity* target) {
        CNetworkFireManager::BeginNativeBirthObservation(target, true);
    };
    startScriptFireEvent.after += [](CEntity*) { CNetworkFireManager::EndNativeBirthObservation(); };

    // Observe after GTA's own CFireManager update. The manager is inert while offline and applies incoming
    // presentations under an explicit remote-mutation guard, preserving all stock script/weapon call paths.
    Events::gameProcessEvent.before += [] { CNetworkFireManager::ProcessAreaTransitions(); };
    Events::gameProcessEvent.after += [] { CNetworkFireManager::Process(); };
}
