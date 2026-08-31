#include "stdafx.h"
#include "CKeySync.h"
#include "UI/CChat.h"

static constexpr size_t MAX_KEY_CONTEXT_DEPTH = 16;

struct KeyContextFrame
{
    CControllerState oldState{};
    CControllerState newState{};
    uint16_t disablePlayerControls = 0;
    char disableKeys[8]{};
    bool nightVision = false;
    bool infraredVision = false;
};

std::array<KeyContextFrame, MAX_KEY_CONTEXT_DEPTH> keyContextStack{};
size_t keyContextDepth = 0;
size_t skippedKeyContextDepth = 0;

void CKeySync::ApplyNetworkPlayerContext(CNetworkPlayer* player)
{
    if (player == nullptr || skippedKeyContextDepth > 0 || keyContextDepth >= keyContextStack.size())
    {
        ++skippedKeyContextDepth;
        return;
    }

    CPad* pad = CPad::GetPad(0);
    KeyContextFrame& frame = keyContextStack[keyContextDepth++];

    // Save the currently active context, which can itself be a remote player when a gun task is processed
    // recursively from inside that player's ProcessControl hook.
    frame.oldState = pad->OldState;
    frame.newState = pad->NewState;
    frame.disablePlayerControls = pad->DisablePlayerControls;
    memcpy(frame.disableKeys, &pad->bApplyBrakes, sizeof(frame.disableKeys));

    pad->OldState = player->m_oldControllerState;
    pad->NewState = player->m_newControllerState;
    pad->DisablePlayerControls = 0;
    memset(&pad->bApplyBrakes, 0, 8);

    frame.nightVision = patch::GetUChar(0xC402B8, false);
    patch::SetUChar(0xC402B8, false, false);
    frame.infraredVision = patch::GetUChar(0xC402B9, false);
    patch::SetUChar(0xC402B9, false, false);
}

void CKeySync::ApplyLocalContext()
{
    if (skippedKeyContextDepth > 0)
    {
        --skippedKeyContextDepth;
        return;
    }
    if (keyContextDepth == 0)
    {
        return;
    }

    CPad* pad = CPad::GetPad(0);
    const KeyContextFrame& frame = keyContextStack[--keyContextDepth];

    pad->OldState = frame.oldState;
    pad->NewState = frame.newState;
    pad->DisablePlayerControls = frame.disablePlayerControls;
    memcpy(&pad->bApplyBrakes, frame.disableKeys, sizeof(frame.disableKeys));

    patch::SetUChar(0xC402B8, frame.nightVision, false);
    patch::SetUChar(0xC402B9, frame.infraredVision, false);
}

void CKeySync::CollectState(Packets::Players::SKeySnapshot& keySnapshot)
{
    assert(CWorld::PlayerInFocus == 0);

    CPad* pPad = CPad::GetPad(0);

    keySnapshot.oldControllerState = pPad->OldState;
    keySnapshot.newControllerState = pPad->NewState;

    if (pPad->DisablePlayerControls != 0)
    {
        memset(&keySnapshot.oldControllerState, 0, sizeof(CControllerStateCompressed));
        memset(&keySnapshot.newControllerState, 0, sizeof(CControllerStateCompressed));
        return;
    }

    bool bInVehicle = FindPlayerVehicle(0, false) && FindPlayerPed(0)->m_nPedFlags.bInVehicle;
    bool bOnFoot = !bInVehicle;

    if (pPad->bApplyBrakes && bInVehicle) // force the handbrake
    {
        keySnapshot.oldControllerState.RightShoulder1 = 255;
        keySnapshot.newControllerState.RightShoulder1 = 255;
    }

    if (pPad->bDisablePlayerEnterCar)
    {
        keySnapshot.oldControllerState.ButtonTriangle = 0;
        keySnapshot.newControllerState.ButtonTriangle = 0;
    }

    if (pPad->bDisablePlayerJump && bOnFoot)
    {
        keySnapshot.oldControllerState.ButtonSquare = 0;
        keySnapshot.newControllerState.ButtonSquare = 0;
    }

    if (pPad->bDisablePlayerDuck && bOnFoot)
    {
        keySnapshot.oldControllerState.ShockButtonL = 0;
        keySnapshot.newControllerState.ShockButtonL = 0;
    }

    if (pPad->bDisablePlayerFireWeapon)
    {
        keySnapshot.oldControllerState.ButtonCircle = 0;
        keySnapshot.newControllerState.ButtonCircle = 0;
    }   

    if (pPad->bDisablePlayerFireWeaponWithL1)
    {
        // unused internally
    }

    if (pPad->bDisablePlayerCycleWeapon && bOnFoot)
    {
        keySnapshot.oldControllerState.RightShoulder2 = 0;
        keySnapshot.newControllerState.RightShoulder2 = 0;
        keySnapshot.oldControllerState.LeftShoulder2 = 0;
        keySnapshot.newControllerState.LeftShoulder2 = 0;
    }

    if (pPad->bDisablePlayerJump && bOnFoot)
    {
        keySnapshot.oldControllerState.ButtonSquare = 0;
        keySnapshot.newControllerState.ButtonSquare = 0;
    }

    if (pPad->bDisablePlayerDisplayVitalStats && bOnFoot)
    {
        // useless here
    }
}
