#include "stdafx.h"
#include "PickupHooks.h"

#include "CNetworkPickupManager.h"

#include <CPickups.h>
#include <CReplay.h>

namespace
{
constexpr int PICKUP_POOL_CAPACITY = Packets::Pickups::PICKUP_POOL_CAPACITY;

void __cdecl CPickups__PictureTaken_Hook()
{
    if (CNetwork::m_bAuthenticated)
    {
        CNetworkPickupManager::RequestLocalSnapshotCapture();
        return;
    }
    CPickups::PictureTaken();
}

void __cdecl CPickups__Update_Hook()
{
    if (CReplay::Mode != 0)
    {
        return;
    }

    CNetworkPickupManager::ProcessBeforeNativeUpdate();

    int start = PICKUP_POOL_CAPACITY * (CTimer::m_FrameCounter % 32) / 32;
    int end = PICKUP_POOL_CAPACITY * (CTimer::m_FrameCounter % 32 + 1) / 32;
    for (int i = start; i < end; ++i)
    {
        CPickup& pickup = CPickups::aPickUps[i];
        if (pickup.m_nPickupType == PICKUP_NONE)
        {
            continue;
        }
        const bool visible = pickup.IsVisible();
        pickup.m_nFlags.bVisible = visible;
        if (visible && CNetworkPickupManager::CanRenderNativeSlot(i))
        {
            if (!pickup.m_nFlags.bDisabled && pickup.m_pObject == nullptr)
            {
                pickup.GiveUsAPickUpObject(&pickup.m_pObject, -1);
                if (pickup.m_pObject != nullptr)
                {
                    CWorld::Add(pickup.m_pObject);
                }
            }
        }
        else
        {
            pickup.GetRidOfObjects();
        }
    }

    CPad* pad = CPad::GetPad(0);
    if (pad->CollectPickupJustDown())
    {
        CollectPickupBuffer = 6;
    }
    else if (CollectPickupBuffer != 0)
    {
        --CollectPickupBuffer;
    }
    if (CPickups::PlayerOnWeaponPickup != 0)
    {
        --CPickups::PlayerOnWeaponPickup;
    }
    if (pad->GetTarget())
    {
        CollectPickupBuffer = 0;
    }

    CPlayerPed* playerOne = FindPlayerPed(0);
    const bool playerOneBusy = playerOne != nullptr && playerOne->m_pIntelligence != nullptr &&
        (playerOne->m_pIntelligence->FindTaskByType(TASK_COMPLEX_ENTER_CAR_AS_DRIVER) != nullptr ||
            playerOne->m_pIntelligence->FindTaskByType(TASK_COMPLEX_USE_MOBILE_PHONE) != nullptr);
    start = PICKUP_POOL_CAPACITY * (CTimer::m_FrameCounter % 6) / 6;
    end = PICKUP_POOL_CAPACITY * (CTimer::m_FrameCounter % 6 + 1) / 6;
    for (int i = start; i < end; ++i)
    {
        CPickup& pickup = CPickups::aPickUps[i];
        if (pickup.m_nPickupType == PICKUP_NONE || !pickup.m_nFlags.bVisible ||
            CNetworkPickupManager::IsManagedNativeSlot(i))
        {
            continue;
        }
        bool collected = false;
        if (!playerOneBusy && playerOne != nullptr)
        {
            collected = pickup.Update(playerOne, FindPlayerVehicle(0, false), CWorld::PlayerInFocus);
        }
        else
        {
            CPlayerPed* playerTwo = FindPlayerPed(1);
            if (playerTwo != nullptr)
            {
                collected = pickup.Update(playerTwo, FindPlayerVehicle(1, false), 1);
            }
        }
        if (collected)
        {
            CPickups::AddToCollectedPickupsArray(i);
        }
    }
}
}  // namespace

void PickupHooks::InjectHooks()
{
    patch::RedirectJump(0x458DE0, CPickups__Update_Hook);

    // These are all 1.0 US camera call sites for CPickups::PictureTaken. The function itself stays intact so
    // offline play can call the original implementation without a trampoline.
    patch::RedirectCall(0x456B1B, CPickups__PictureTaken_Hook);
    patch::RedirectCall(0x456B90, CPickups__PictureTaken_Hook);
    patch::RedirectCall(0x456BB6, CPickups__PictureTaken_Hook);
}
