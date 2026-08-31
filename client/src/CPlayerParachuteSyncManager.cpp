#include "stdafx.h"
#include "CPlayerParachuteSyncManager.h"
#include <CAnimBlendHierarchy.h>
#include <CObject.h>
#include <CPools.h>

namespace
{
// GTA SA 1.0 US uses the streamed IFP resource range beginning at 25575. The stock PLAYER_PARACHUTE
// external script drives these exact PARACHUTE block animations and model 3131 for the canopy.
constexpr int IFP_RESOURCE_BASE = 25575;

bool IsActiveAssociation(const CAnimBlendAssociation* association)
{
    return association && association->m_pHierarchy && association->m_bPlaying &&
           association->m_fBlendAmount > 0.0f && association->m_fBlendDelta >= 0.0f;
}

Packets::Players::ePlayerParachuteState StateForAnimationHash(uint32_t hash)
{
    struct AnimationState
    {
        const char* name;
        Packets::Players::ePlayerParachuteState state;
    };
    static const AnimationState STATES[] = {
        { "FALL_SKYDIVE", Packets::Players::PLAYER_PARACHUTE_FREEFALL },
        { "FALL_SKYDIVE_L", Packets::Players::PLAYER_PARACHUTE_FREEFALL_LEFT },
        { "FALL_SKYDIVE_R", Packets::Players::PLAYER_PARACHUTE_FREEFALL_RIGHT },
        { "FALL_SKYDIVE_ACCEL", Packets::Players::PLAYER_PARACHUTE_FREEFALL_ACCEL },
        { "PARA_OPEN", Packets::Players::PLAYER_PARACHUTE_OPENING },
        { "PARA_FLOAT", Packets::Players::PLAYER_PARACHUTE_DEPLOYED },
        { "PARA_STEERL", Packets::Players::PLAYER_PARACHUTE_DEPLOYED_LEFT },
        { "PARA_STEERR", Packets::Players::PLAYER_PARACHUTE_DEPLOYED_RIGHT },
        { "PARA_DECEL", Packets::Players::PLAYER_PARACHUTE_DEPLOYED_FLARE },
        { "PARA_LAND", Packets::Players::PLAYER_PARACHUTE_LANDING },
        { "PARA_LAND_WATER", Packets::Players::PLAYER_PARACHUTE_LANDING_WATER },
    };

    for (const AnimationState& candidate : STATES)
    {
        if (hash == CKeyGen::GetUppercaseKey(candidate.name))
        {
            return candidate.state;
        }
    }
    return Packets::Players::PLAYER_PARACHUTE_NONE;
}

CAnimBlendAssociation* FindCanopyRipAssociation(CPlayerPed* player)
{
    if (!player || !CPools::ms_pObjectPool)
    {
        return nullptr;
    }

    const uint32_t ripHash = CKeyGen::GetUppercaseKey("PARA_RIP_LOOP_O");
    for (int i = 0; i < CPools::ms_pObjectPool->m_nSize; ++i)
    {
        CObject* object = CPools::ms_pObjectPool->GetAt(i);
        if (!object || object->m_nModelIndex != MODEL_PARACHUTE || object->m_pAttachedTo != player ||
            !object->m_pRwClump)
        {
            continue;
        }

        for (CAnimBlendAssociation* association = RpAnimBlendClumpGetFirstAssociation(object->m_pRwClump);
             association; association = RpAnimBlendGetNextAssociation(association))
        {
            if (IsActiveAssociation(association) && association->m_pHierarchy->m_hashKey == ripHash)
            {
                return association;
            }
        }
    }
    return nullptr;
}
}

void CPlayerParachuteSyncManager::ProcessLocal()
{
    if (!CNetwork::m_bAuthenticated || CWorld::PlayerInFocus != 0)
    {
        ResetLocalState();
        return;
    }

    CAnimBlendAssociation* association = nullptr;
    const Packets::Players::ePlayerParachuteState state = ObserveLocalState(association);
    const uint32_t now = GetTickCount();

    if (!ms_initialized)
    {
        ms_initialized = true;
        ms_lastState = state;
        if (state != Packets::Players::PLAYER_PARACHUTE_NONE)
        {
            SendState(state, association, now);
        }
        return;
    }

    const bool changed = state != ms_lastState;
    const bool heartbeat = state != Packets::Players::PLAYER_PARACHUTE_NONE &&
        now - ms_lastSentAt >= HEARTBEAT_INTERVAL_MS;
    if (changed || heartbeat)
    {
        SendState(state, association, now);
    }
}

void CPlayerParachuteSyncManager::ResetLocalState()
{
    ms_lastState = Packets::Players::PLAYER_PARACHUTE_NONE;
    ms_sequence = 0;
    ms_lastSentAt = 0;
    ms_initialized = false;
}

void CPlayerParachuteSyncManager::AcquireResources()
{
    if (ms_resourceUsers >= Config::MAX_SERVER_PLAYERS)
    {
        return;
    }

    if (ms_resourceUsers++ == 0)
    {
        ms_animationBlockId = CAnimManager::GetAnimationBlockIndex("PARACHUTE");
        if (ms_animationBlockId >= 0)
        {
            CStreaming::RequestModel(IFP_RESOURCE_BASE + ms_animationBlockId, GAME_REQUIRED);
        }
        CStreaming::RequestModel(MODEL_PARACHUTE, GAME_REQUIRED);
    }
}

void CPlayerParachuteSyncManager::ReleaseResources()
{
    if (ms_resourceUsers == 0)
    {
        return;
    }

    if (--ms_resourceUsers != 0)
    {
        return;
    }

    if (ms_animationBlockReferenced && ms_animationBlockId >= 0)
    {
        CAnimManager::RemoveAnimBlockRef(ms_animationBlockId);
        ms_animationBlockReferenced = false;
    }
    if (ms_animationBlockId >= 0)
    {
        CStreaming::SetModelIsDeletable(IFP_RESOURCE_BASE + ms_animationBlockId);
    }
    CStreaming::SetModelIsDeletable(MODEL_PARACHUTE);
    ms_animationBlockId = -1;
}

bool CPlayerParachuteSyncManager::EnsureResourcesLoaded()
{
    if (ms_resourceUsers == 0)
    {
        return false;
    }

    if (ms_animationBlockId < 0)
    {
        ms_animationBlockId = CAnimManager::GetAnimationBlockIndex("PARACHUTE");
    }
    if (ms_animationBlockId < 0)
    {
        return false;
    }

    const int animationModelId = IFP_RESOURCE_BASE + ms_animationBlockId;
    if (CStreaming::ms_aInfoForModel[animationModelId].m_nLoadState != LOADSTATE_LOADED ||
        CStreaming::ms_aInfoForModel[MODEL_PARACHUTE].m_nLoadState != LOADSTATE_LOADED)
    {
        CStreaming::RequestModel(animationModelId, GAME_REQUIRED);
        CStreaming::RequestModel(MODEL_PARACHUTE, GAME_REQUIRED);
        return false;
    }

    if (!ms_animationBlockReferenced)
    {
        CAnimManager::AddAnimBlockRef(ms_animationBlockId);
        ms_animationBlockReferenced = true;
    }
    return true;
}

Packets::Players::ePlayerParachuteState CPlayerParachuteSyncManager::ObserveLocalState(
    CAnimBlendAssociation*& association)
{
    CPlayerPed* player = FindPlayerPed(0);
    if (!player || !player->IsAlive() || player->m_nPedFlags.bInVehicle || player->m_pVehicle)
    {
        return Packets::Players::PLAYER_PARACHUTE_NONE;
    }

    if (CAnimBlendAssociation* rip = FindCanopyRipAssociation(player))
    {
        association = rip;
        return Packets::Players::PLAYER_PARACHUTE_COLLAPSED;
    }

    if (player->GetWeapon().m_eWeaponType != WEAPON_PARACHUTE || !player->m_pRwClump)
    {
        return Packets::Players::PLAYER_PARACHUTE_NONE;
    }

    const int parachuteBlockId = CAnimManager::GetAnimationBlockIndex("PARACHUTE");
    if (parachuteBlockId < 0)
    {
        return Packets::Players::PLAYER_PARACHUTE_NONE;
    }

    for (CAnimBlendAssociation* candidate = RpAnimBlendClumpGetFirstAssociation(player->m_pRwClump);
         candidate; candidate = RpAnimBlendGetNextAssociation(candidate))
    {
        if (!IsActiveAssociation(candidate) || candidate->m_pHierarchy->m_nAnimBlockId != parachuteBlockId)
        {
            continue;
        }

        const Packets::Players::ePlayerParachuteState state =
            StateForAnimationHash(candidate->m_pHierarchy->m_hashKey);
        if (state != Packets::Players::PLAYER_PARACHUTE_NONE)
        {
            association = candidate;
            return state;
        }
    }
    return Packets::Players::PLAYER_PARACHUTE_NONE;
}

uint8_t CPlayerParachuteSyncManager::QuantizeProgress(const CAnimBlendAssociation* association)
{
    if (!association || !association->m_pHierarchy || association->m_pHierarchy->m_fTotalTime <= 0.0f)
    {
        return 0;
    }

    const float progress = std::clamp(
        association->m_fCurrentTime / association->m_pHierarchy->m_fTotalTime, 0.0f, 1.0f);
    return static_cast<uint8_t>(progress * 255.0f + 0.5f);
}

void CPlayerParachuteSyncManager::SendState(Packets::Players::ePlayerParachuteState state,
    const CAnimBlendAssociation* association, uint32_t now)
{
    float pitch = 0.0f;
    float roll = 0.0f;
    if (state != Packets::Players::PLAYER_PARACHUTE_NONE)
    {
        if (CPlayerPed* player = FindPlayerPed(0))
        {
            float heading = 0.0f;
            player->GetOrientation(pitch, roll, heading);
            pitch = std::clamp(pitch, -Packets::Players::SetPlayerTask::PARACHUTE_TILT_LIMIT,
                Packets::Players::SetPlayerTask::PARACHUTE_TILT_LIMIT);
            roll = std::clamp(roll, -Packets::Players::SetPlayerTask::PARACHUTE_TILT_LIMIT,
                Packets::Players::SetPlayerTask::PARACHUTE_TILT_LIMIT);
        }
    }

    ++ms_sequence;
    CLocalPlayer::BuildParachuteTaskPacket(state, ms_sequence, QuantizeProgress(association), pitch, roll);
    ms_lastState = state;
    ms_lastSentAt = now;
}
