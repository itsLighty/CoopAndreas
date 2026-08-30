#include "stdafx.h"
#include "CPlayerAnimationSyncManager.h"
#include <CAnimBlendHierarchy.h>

namespace
{
constexpr uint32_t ANIMATION_HEARTBEAT_MS = 1000;

CAnimBlendAssociation* FindActiveAssociation(CPlayerPed* ped, int groupId, int animationId)
{
    if (!ped || !ped->m_pRwClump)
    {
        return nullptr;
    }

    for (CAnimBlendAssociation* association = RpAnimBlendClumpGetFirstAssociation(ped->m_pRwClump); association;
         association = RpAnimBlendGetNextAssociation(association))
    {
        if (association->m_nAnimGroup == groupId && association->m_nAnimId == animationId &&
            association->m_bPlaying && association->m_fBlendAmount > 0.0f && association->m_fBlendDelta >= 0.0f)
        {
            return association;
        }
    }
    return nullptr;
}
}

void CPlayerAnimationSyncManager::Process()
{
    if (!CNetwork::m_bAuthenticated)
    {
        ResetNetworkState();
        return;
    }

    CAnimBlendAssociation* association = nullptr;
    const Packets::Players::ePlayerAnimationState state = ObserveLocalAnimation(association);
    const uint32_t now = GetTickCount();

    if (!ms_initialized)
    {
        ms_initialized = true;
        ms_lastState = state;
        if (state != Packets::Players::PLAYER_ANIMATION_NONE)
        {
            SendState(state, association, now);
        }
        return;
    }

    if (state != ms_lastState ||
        (state != Packets::Players::PLAYER_ANIMATION_NONE && now - ms_lastSentAt >= ANIMATION_HEARTBEAT_MS))
    {
        SendState(state, association, now);
    }
}

void CPlayerAnimationSyncManager::ResetNetworkState()
{
    ms_lastState = Packets::Players::PLAYER_ANIMATION_NONE;
    ms_sequence = 0;
    ms_lastSentAt = 0;
    ms_initialized = false;
}

void CPlayerAnimationSyncManager::AcquirePlayIdles()
{
    if (ms_playIdlesUsers >= Config::MAX_SERVER_PLAYERS)
    {
        return;
    }

    if (ms_playIdlesUsers++ == 0)
    {
        Command<Commands::REQUEST_ANIMATION>("PLAYIDLES");
    }
}

void CPlayerAnimationSyncManager::ReleasePlayIdles()
{
    if (ms_playIdlesUsers == 0)
    {
        return;
    }

    if (--ms_playIdlesUsers == 0)
    {
        Command<Commands::REMOVE_ANIMATION>("PLAYIDLES");
    }
}

bool CPlayerAnimationSyncManager::EnsurePlayIdlesLoaded()
{
    if (ms_playIdlesUsers == 0)
    {
        return false;
    }
    if (Command<Commands::HAS_ANIMATION_LOADED>("PLAYIDLES"))
    {
        return true;
    }

    // Requesting an already-requested block is idempotent; no association reference is added here.
    Command<Commands::REQUEST_ANIMATION>("PLAYIDLES");
    return false;
}

Packets::Players::ePlayerAnimationState CPlayerAnimationSyncManager::ObserveLocalAnimation(
    CAnimBlendAssociation*& association)
{
    CPlayerPed* ped = FindPlayerPed(0);
    if (!ped || !ped->m_pRwClump)
    {
        return Packets::Players::PLAYER_ANIMATION_NONE;
    }

    const CControllerState& controls = CPad::GetPad(0)->NewState;
    if (controls.LeftShoulder1 && controls.RightStickX < 0)
    {
        association = FindActiveAssociation(ped, ANIM_GROUP_DEFAULT, ANIM_DEFAULT_TURN_L);
        if (association)
        {
            return Packets::Players::PLAYER_ANIMATION_FUNNY_TURN_LEFT;
        }
    }
    else if (controls.LeftShoulder1 && controls.RightStickX > 0)
    {
        association = FindActiveAssociation(ped, ANIM_GROUP_DEFAULT, ANIM_DEFAULT_TURN_R);
        if (association)
        {
            return Packets::Players::PLAYER_ANIMATION_FUNNY_TURN_RIGHT;
        }
    }

    struct IdleAnimation
    {
        int animationId;
        Packets::Players::ePlayerAnimationState state;
    };
    static constexpr IdleAnimation IDLE_ANIMATIONS[] = {
        { ANIM_PLAYIDLES_STRETCH, Packets::Players::PLAYER_ANIMATION_IDLE_STRETCH },
        { ANIM_PLAYIDLES_TIME, Packets::Players::PLAYER_ANIMATION_IDLE_TIME },
        { ANIM_PLAYIDLES_SHLDR, Packets::Players::PLAYER_ANIMATION_IDLE_SHOULDER },
        { ANIM_PLAYIDLES_STRLEG, Packets::Players::PLAYER_ANIMATION_IDLE_STRETCH_LEG },
    };

    for (const IdleAnimation& idle : IDLE_ANIMATIONS)
    {
        association = FindActiveAssociation(ped, ANIM_GROUP_PLAYIDLES, idle.animationId);
        if (association)
        {
            return idle.state;
        }
    }

    association = nullptr;
    return Packets::Players::PLAYER_ANIMATION_NONE;
}

uint8_t CPlayerAnimationSyncManager::QuantizeProgress(const CAnimBlendAssociation* association)
{
    if (!association || !association->m_pHierarchy || association->m_pHierarchy->m_fTotalTime <= 0.0f)
    {
        return 0;
    }

    float progress = association->m_fCurrentTime / association->m_pHierarchy->m_fTotalTime;
    if (progress < 0.0f)
    {
        progress = 0.0f;
    }
    else if (progress > 1.0f)
    {
        progress = 1.0f;
    }
    return static_cast<uint8_t>(progress * 255.0f + 0.5f);
}

void CPlayerAnimationSyncManager::SendState(Packets::Players::ePlayerAnimationState state,
    const CAnimBlendAssociation* association, uint32_t now)
{
    ++ms_sequence;
    CLocalPlayer::BuildAnimationTaskPacket(state, ms_sequence, QuantizeProgress(association));
    ms_lastState = state;
    ms_lastSentAt = now;
}
