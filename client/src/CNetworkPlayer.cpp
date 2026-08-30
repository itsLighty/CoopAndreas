#include "stdafx.h"
#include <CTaskSimpleCarSetPedOut.h>
#include <CCarEnterExit.h>
#include <CTaskSimpleCarSetPedInAsPassenger.h>
#include <CTaskComplexEnterCarAsPassenger.h>
#include <CAnimBlendHierarchy.h>
#include <CPlayerAnimationSyncManager.h>

namespace
{
struct SyncedAnimationDefinition
{
    int groupId;
    int animationId;
    float blendDelta;
    bool isIdle;
    bool isLooped;
};

bool GetSyncedAnimationDefinition(int state, SyncedAnimationDefinition& definition)
{
    switch (state)
    {
        case Packets::Players::PLAYER_ANIMATION_IDLE_STRETCH:
            definition = { ANIM_GROUP_PLAYIDLES, ANIM_PLAYIDLES_STRETCH, 8.0f, true, false };
            return true;
        case Packets::Players::PLAYER_ANIMATION_IDLE_TIME:
            definition = { ANIM_GROUP_PLAYIDLES, ANIM_PLAYIDLES_TIME, 8.0f, true, false };
            return true;
        case Packets::Players::PLAYER_ANIMATION_IDLE_SHOULDER:
            definition = { ANIM_GROUP_PLAYIDLES, ANIM_PLAYIDLES_SHLDR, 8.0f, true, false };
            return true;
        case Packets::Players::PLAYER_ANIMATION_IDLE_STRETCH_LEG:
            definition = { ANIM_GROUP_PLAYIDLES, ANIM_PLAYIDLES_STRLEG, 8.0f, true, false };
            return true;
        case Packets::Players::PLAYER_ANIMATION_FUNNY_TURN_LEFT:
            definition = { ANIM_GROUP_DEFAULT, ANIM_DEFAULT_TURN_L, 16.0f, false, true };
            return true;
        case Packets::Players::PLAYER_ANIMATION_FUNNY_TURN_RIGHT:
            definition = { ANIM_GROUP_DEFAULT, ANIM_DEFAULT_TURN_R, 16.0f, false, true };
            return true;
        default:
            return false;
    }
}

bool IsSyncedIdleAnimation(int state)
{
    return state >= Packets::Players::PLAYER_ANIMATION_IDLE_STRETCH &&
           state <= Packets::Players::PLAYER_ANIMATION_IDLE_STRETCH_LEG;
}

CAnimBlendAssociation* FindSyncedAnimationAssociation(CPlayerPed* ped, const SyncedAnimationDefinition& definition)
{
    if (!ped || !ped->m_pRwClump)
    {
        return nullptr;
    }

    for (CAnimBlendAssociation* association = RpAnimBlendClumpGetFirstAssociation(ped->m_pRwClump); association;
         association = RpAnimBlendGetNextAssociation(association))
    {
        if (association->m_nAnimGroup == definition.groupId && association->m_nAnimId == definition.animationId)
        {
            return association;
        }
    }
    return nullptr;
}

bool IsSequenceNewer(uint16_t incoming, uint16_t previous)
{
    return static_cast<int16_t>(incoming - previous) > 0;
}
}

CNetworkPlayer::~CNetworkPlayer()
{
    ClearSyncedAnimationState();
    if (m_pPed == nullptr)
        return;

    this->DestroyPed();
}

CNetworkPlayer::CNetworkPlayer(int id, CVector position)
{
    m_iPlayerId = id;

    m_pPedClothesDesc.SetTextureAndModel("VEST", "VEST", 0);
    m_pPedClothesDesc.SetTextureAndModel("JEANSDENIM", "JEANS", 2);
    m_pPedClothesDesc.SetTextureAndModel("SNEAKERBINCBLK", "SNEAKER", 3);
    m_pPedClothesDesc.SetTextureAndModel("PLAYER_FACE", "HEAD", 1);

    CreatePed(id, position);
}

void CNetworkPlayer::CreatePed(int id, CVector position)
{
    unsigned int actorId = 0;
    int playerId = id + 2;

    plugin::Command<Commands::CREATE_PLAYER>(playerId, position.x, position.y, position.z, &actorId);
    plugin::Command<Commands::GET_PLAYER_CHAR>(playerId, &actorId);

    m_pPed = (CPlayerPed*)CPools::GetPed(actorId);

    m_pPed->SetOrientation(0.0f, 0.0f, 0.0f);

    // set player immunies, he doesn't care about the pain now
    Command<Commands::SET_CHAR_PROOFS>(actorId, 0, 1, 1, 0, 0);

    *m_pPed->m_pPlayerData->m_pPedClothesDesc = m_pPedClothesDesc;

    CClothes::RebuildPlayer(m_pPed, false);

    // THIS IS AN EXPERIMENTAL SOLUTION FOR THE 0x4D68BA CRASH
    m_pPed->m_bStreamingDontDelete = true;

    if (m_bHasGameplayState)
    {
        ApplyGameplayState(m_gameplayState);
    }

    ApplySyncedAnimation();
}

void CNetworkPlayer::DestroyPed()
{
    ClearLaserScopeDotState();
    if (!m_pPed)
    {
        return;
    }

    if (m_pPed->m_pVehicle)
    {
        plugin::Command<Commands::WARP_CHAR_FROM_CAR_TO_COORD>(CPools::GetPedRef(m_pPed), 0.f, 0.f, 0.f);
    }

    uintptr_t pedPtr = (uintptr_t)m_pPed;
    if (m_pPed->IsVTableValid())
    {
        CWorld::Remove(m_pPed);

        // destroy the ped
        __asm
        {
			mov ecx, pedPtr
			mov ebx, [ecx]  // vtable addr
			push 1  // unused arg
			call[ebx]  // call destructor
        }
    }
    m_pPed = nullptr;
}

void CNetworkPlayer::Respawn()
{
    ClearSyncedAnimationState();
    if (m_pPed)
    {
        this->DestroyPed();
    }

    this->CreatePed(m_iPlayerId, m_onFootSnapshotInterpolated.vecPos);
}

int CNetworkPlayer::GetInternalId()  // most used for CWorld::PlayerInFocus
{
    byte playerNumber = 0;

    for (; playerNumber < Config::MAX_SERVER_PLAYERS + 2; playerNumber++)
    {
        if (m_pPed == CWorld::Players[playerNumber].m_pPed)
        {
            return playerNumber;
        }
    }

    return -1;
}

std::string CNetworkPlayer::GetName()
{
    if (m_Name[0] == '\0')
    {
        char buffer[32 + 1];
        sprintf(buffer, "player %d", m_iPlayerId);
        return buffer;
    }

    return m_Name;
}

void CNetworkPlayer::ApplyGameplayState(const Packets::Players::PlayerGameplayState& gameplayState)
{
    if (gameplayState.playerid.value != m_iPlayerId)
    {
        return;
    }

    m_gameplayState = gameplayState;
    m_bHasGameplayState = true;

    if (m_pPed == nullptr || m_pPed->m_pPlayerData == nullptr)
    {
        return;
    }

    m_pPed->m_fMaxHealth = gameplayState.maximumHealth;
    m_pPed->m_pPlayerData->m_fBreath = gameplayState.breath;
    if (m_pPed->m_pPlayerData->m_pWanted != nullptr)
    {
        m_pPed->m_pPlayerData->m_pWanted->m_nWantedLevel = gameplayState.wantedLevel;
    }

    if (CPlayerInfo* pPlayerInfo = m_pPed->GetPlayerInfoForThisPlayerPed())
    {
        pPlayerInfo->m_nMoney = gameplayState.money;
        pPlayerInfo->m_nDisplayMoney = gameplayState.money;
    }
}

char CNetworkPlayer::GetWeaponSkill(eWeaponType weaponType)
{
    if (weaponType < WEAPON_PISTOL || weaponType > WEAPON_TEC9)
        return 1;

    eStats weaponStatId = plugin::CallAndReturn<eStats, 0x743CD0>(weaponType);  // CWeaponInfo::GetSkillStatIndex
    int statSyncId = CStatsSync::GetSyncIdByInternal(weaponStatId);
    float weaponStat = m_stats[weaponStatId];

    if (CWeaponInfo::GetWeaponInfo(weaponType, 2)->m_nReqStatLevel <= weaponStat)
        return 2;

    return CWeaponInfo::GetWeaponInfo(weaponType, 1)->m_nReqStatLevel <= weaponStat;
}

void CNetworkPlayer::WarpIntoVehicleDriver(CVehicle* vehicle)
{
    assert(m_pPed != nullptr);

    if (!vehicle->IsVTableValid() || !m_pPed->IsVTableValid())
    {
        return;
    }

    if (m_pPed->m_nPedFlags.bInVehicle && m_pPed->m_pVehicle)
    {
        RemoveFromVehicle(m_pPed->m_pVehicle);
    }

    m_pPed->m_pIntelligence->FlushImmediately(false);

    m_pPed->m_nPedFlags.CantBeKnockedOffBike = 1;  // 1 - never

    auto task = CTaskSimpleCarSetPedInAsDriver(vehicle, nullptr);
    task.m_bWarpingInToCar = true;
    task.ProcessPed(m_pPed);
}

void CNetworkPlayer::WarpIntoVehiclePassenger(CVehicle* vehicle, int seatid)
{
    assert(m_pPed != nullptr);

    if (!vehicle->IsVTableValid() || !m_pPed->IsVTableValid())
    {
        return;
    }

    if (m_pPed->m_nPedFlags.bInVehicle && m_pPed->m_pVehicle)
    {
        RemoveFromVehicle(m_pPed->m_pVehicle);
    }

    m_pPed->m_pIntelligence->FlushImmediately(false);

    m_pPed->m_nPedFlags.CantBeKnockedOffBike = 1;  // 1 - never

    int doorId = CCarEnterExit::ComputeTargetDoorToEnterAsPassenger(vehicle, seatid);
    auto task = CTaskSimpleCarSetPedInAsPassenger(vehicle, doorId, nullptr);
    task.m_bWarpingInToCar = true;
    task.ProcessPed(m_pPed);
}

void CNetworkPlayer::EnterVehiclePassenger(CVehicle* vehicle, int seatid)
{
    assert(m_pPed != nullptr);

    if (!vehicle->IsVTableValid() || !m_pPed->IsVTableValid())
    {
        return;
    }

    if (m_pPed->m_nPedFlags.bInVehicle && m_pPed->m_pVehicle)
    {
        RemoveFromVehicle(m_pPed->m_pVehicle);
    }

    m_pPed->m_pIntelligence->FlushImmediately(false);

    m_pPed->m_nPedFlags.CantBeKnockedOffBike = 1;  // 1 - never

    int doorId = CCarEnterExit::ComputeTargetDoorToEnterAsPassenger(vehicle, seatid);
    auto task = new CTaskComplexEnterCarAsPassenger(vehicle, doorId, false);
    m_pPed->m_pIntelligence->m_TaskMgr.SetTask(task, 3, false);
}

void CNetworkPlayer::RemoveFromVehicle(CVehicle* vehicle)
{
    assert(m_pPed != nullptr);

    if (!vehicle->IsVTableValid() || !m_pPed->IsVTableValid())
    {
        return;
    }

    m_pPed->m_pIntelligence->m_TaskMgr.SetTask(nullptr, TASK_PRIMARY_PRIMARY, false);

    m_pPed->m_nPedFlags.CantBeKnockedOffBike = 2;  // 2 - normal

    auto task = CTaskSimpleCarSetPedOut(vehicle, 1, false);
    task.m_bWarpingOutOfCar = true;
    task.ProcessPed(m_pPed);
}

void CNetworkPlayer::HandleTask(Packets::Players::SetPlayerTask& packet)
{
    if (packet.hasAnimationState)
    {
        HandleSyncedAnimation(packet);
        return;
    }

    if (!m_pPed)
    {
        return;
    }

#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("HandleTask %d toggle %d", packet.taskType, packet.toggle);
#endif

    m_pPed->SetPosn(packet.vecPos);
    m_pPed->m_fCurrentRotation = packet.currentRotation.m_angle;
    m_pPed->m_fAimingRotation = packet.aimingRotation.m_angle;

    CTask* activeTask = m_pPed->m_pIntelligence->m_TaskMgr.GetActiveTask();
    eTaskType activeTaskType = activeTask ? activeTask->GetTaskType() : TASK_NONE;
    switch ((eTaskType)packet.taskType)
    {
        case eTaskType::TASK_COMPLEX_JUMP:
        {
            m_pPed->ClearWeaponTarget();
            if (activeTask && activeTaskType == packet.taskType)  // if the jump task is active
            {
                return;
            }

            if (m_pPed->m_pIntelligence->GetTaskDuck(false))
            {
                CTaskSimpleDuckToggle(0).ProcessPed(m_pPed);
            }

            m_pPed->m_pIntelligence->m_TaskMgr.SetTask(new CTaskComplexJump(0), 3, false);

            break;
        }
        case eTaskType::TASK_SIMPLE_DUCK:
        {
            m_bRequestedDuckTask = true;
            //if (packet.toggle)
            //{
            //    m_pPed->m_pIntelligence->SetTaskDuckSecondary(0);

            //    CTaskSimpleFight* pFightingTask = m_pPed->m_pIntelligence->GetTaskFighting();
            //    if (pFightingTask)
            //    {
            //        // abort fighting task (0x6879C2)
            //        plugin::CallMethodAndReturn<bool, 0x61C5E0, CTaskSimpleFight*, CEntity*, int8_t>(pFightingTask, nullptr, 18);
            //    }
            //}
            //else
            //{
            //    m_pPed->m_pIntelligence->ClearTaskDuckSecondary();
            //}
            break;
        }
    }
}

void CNetworkPlayer::HandleSyncedAnimation(const Packets::Players::SetPlayerTask& packet)
{
    if (!packet.IsAnimationStateSemanticallyValid() ||
        (m_bHasSyncedAnimationSequence &&
            !IsSequenceNewer(packet.animationSequence, m_nSyncedAnimationSequence)))
    {
        return;
    }

    const bool replacing = m_nSyncedAnimationState != packet.animationState;
    if (replacing)
    {
        FadeSyncedAnimation();
        const bool wasIdle = IsSyncedIdleAnimation(m_nSyncedAnimationState);
        const bool isIdle = IsSyncedIdleAnimation(packet.animationState);
        if (!wasIdle && isIdle)
        {
            CPlayerAnimationSyncManager::AcquirePlayIdles();
        }
        else if (wasIdle && !isIdle)
        {
            CPlayerAnimationSyncManager::ReleasePlayIdles();
        }
    }

    m_bHasSyncedAnimationSequence = true;
    m_nSyncedAnimationSequence = packet.animationSequence;
    m_nSyncedAnimationProgress = packet.animationProgress;
    m_nSyncedAnimationReceivedAt = GetTickCount();
    m_nSyncedAnimationState = packet.animationState;

    if (m_nSyncedAnimationState != Packets::Players::PLAYER_ANIMATION_NONE)
    {
        ApplySyncedAnimation();
    }
}

void CNetworkPlayer::ApplySyncedAnimation()
{
    if (!m_pPed || !m_pPed->m_pRwClump ||
        m_nSyncedAnimationState == Packets::Players::PLAYER_ANIMATION_NONE)
    {
        return;
    }

    SyncedAnimationDefinition definition{};
    if (!GetSyncedAnimationDefinition(m_nSyncedAnimationState, definition))
    {
        ClearSyncedAnimationState();
        return;
    }

    if (definition.isIdle)
    {
        if (!CPlayerAnimationSyncManager::EnsurePlayIdlesLoaded())
        {
            return;
        }
    }

    CAnimBlendAssociation* association = FindSyncedAnimationAssociation(m_pPed, definition);
    if (!association || association->m_fBlendDelta < 0.0f)
    {
        association = CAnimManager::BlendAnimation(
            m_pPed->m_pRwClump, definition.groupId, definition.animationId, definition.blendDelta);
        if (!association)
        {
            return;
        }
        if (definition.isIdle)
        {
            association->m_nFlags |= ANIMATION_UNUSED_2;
        }
    }

    if (!association->m_pHierarchy || association->m_pHierarchy->m_fTotalTime <= 0.0f)
    {
        return;
    }

    const float totalTime = association->m_pHierarchy->m_fTotalTime;
    float targetTime = totalTime * (static_cast<float>(m_nSyncedAnimationProgress) / 255.0f);
    const float elapsedSeconds = static_cast<float>(GetTickCount() - m_nSyncedAnimationReceivedAt) / 1000.0f;
    targetTime += elapsedSeconds * association->m_fSpeed;
    if (definition.isLooped)
    {
        while (targetTime >= totalTime)
        {
            targetTime -= totalTime;
        }
    }
    else if (targetTime >= totalTime)
    {
        association->m_fBlendDelta = -definition.blendDelta;
        return;
    }

    float drift = association->m_fCurrentTime - targetTime;
    if (drift < 0.0f)
    {
        drift = -drift;
    }
    if (definition.isLooped && drift > totalTime * 0.5f)
    {
        drift = totalTime - drift;
    }
    if (drift > 0.2f)
    {
        association->SetCurrentTime(targetTime);
    }
}

void CNetworkPlayer::FadeSyncedAnimation()
{
    SyncedAnimationDefinition definition{};
    if (!GetSyncedAnimationDefinition(m_nSyncedAnimationState, definition))
    {
        return;
    }

    if (CAnimBlendAssociation* association = FindSyncedAnimationAssociation(m_pPed, definition))
    {
        association->m_fBlendDelta = -definition.blendDelta;
    }
}

void CNetworkPlayer::ClearSyncedAnimationState()
{
    FadeSyncedAnimation();
    if (IsSyncedIdleAnimation(m_nSyncedAnimationState))
    {
        CPlayerAnimationSyncManager::ReleasePlayIdles();
    }
    m_nSyncedAnimationState = Packets::Players::PLAYER_ANIMATION_NONE;
    m_nSyncedAnimationSequence = 0;
    m_nSyncedAnimationProgress = 0;
    m_nSyncedAnimationReceivedAt = 0;
    m_bHasSyncedAnimationSequence = false;
}

void CNetworkPlayer::ClearLaserScopeDotState()
{
    m_cameraSnapshotOld.bLaserScopeDotActive = false;
    m_cameraSnapshotOld.laserScopeDotPosition = {};
    m_cameraSnapshotOld.laserScopeDotSize = 0.0f;
    m_cameraSnapshot.bLaserScopeDotActive = false;
    m_cameraSnapshot.laserScopeDotPosition = {};
    m_cameraSnapshot.laserScopeDotSize = 0.0f;
    m_nLaserScopeDotReceivedAt = 0;
}

void CNetworkPlayer::ApplyWeaponSnapshot(Packets::Players::SWeaponSnapshot& weaponSnapshot)
{
    if (m_pPed == nullptr)
    {
        return;
    }

    m_onFootSnapshotInterpolated.weaponSnapshot = weaponSnapshot;
    if (weaponSnapshot.iWeaponType != WEAPON_SNIPERRIFLE)
    {
        ClearLaserScopeDotState();
    }
    // TODO refactor CUtil
    CUtil::GiveWeaponByPacket(this, weaponSnapshot.iWeaponType, weaponSnapshot.nAmmo);
    m_pPed->m_aWeapons[m_pPed->m_nActiveWeaponSlot].m_nState = static_cast<eWeaponState>(weaponSnapshot.iWeaponState);
}
