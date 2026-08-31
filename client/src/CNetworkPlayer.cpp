#include "stdafx.h"
#include <CTaskSimpleCarSetPedOut.h>
#include <CCarEnterExit.h>
#include <CTaskSimpleCarSetPedInAsPassenger.h>
#include <CTaskComplexEnterCarAsPassenger.h>
#include <CAnimBlendHierarchy.h>
#include <CPlayerAnimationSyncManager.h>
#include <CPlayerParachuteSyncManager.h>
#include <CObject.h>
#include <CTaskSimpleRunNamedAnim.h>
#include "CNetworkEntityStreamManager.h"
#include "CEntryExitTransitionSync.h"
#include "CServerTime.h"

namespace
{
constexpr float PLAYER_TELEPORT_DISTANCE = 30.0f;

CNetworkTransformSnapshot MakePlayerOnFootTransform(
    const Packets::Players::OnFootUpdate& snapshot, int playerId, uint8_t area)
{
    CNetworkTransformSnapshot transform{};
    transform.serverTime = snapshot.serverTime;
    transform.position = snapshot.vecPos;
    transform.velocity = snapshot.vecMoveSpeed;
    transform.currentRotation = snapshot.currentRotation.m_angle;
    transform.aimingRotation = snapshot.aimingRotation.m_angle;
    transform.sourceId = static_cast<uint32_t>(std::max(playerId, 0));
    transform.area = area;
    transform.source = eNetworkTransformSource::PLAYER_ON_FOOT;
    return transform;
}

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

struct SyncedParachuteDefinition
{
    const char* pedAnimation;
    const char* canopyAnimation;
    float blendDelta;
    float canopySpeed;
    bool looped;
    bool showCanopy;
    bool detachCanopy;
};

bool GetSyncedParachuteDefinition(int state, SyncedParachuteDefinition& definition)
{
    switch (state)
    {
        case Packets::Players::PLAYER_PARACHUTE_FREEFALL:
            definition = { "FALL_SKYDIVE", nullptr, 1.0f, 0.0f, true, false, false };
            return true;
        case Packets::Players::PLAYER_PARACHUTE_FREEFALL_LEFT:
            definition = { "FALL_SKYDIVE_L", nullptr, 1.0f, 0.0f, true, false, false };
            return true;
        case Packets::Players::PLAYER_PARACHUTE_FREEFALL_RIGHT:
            definition = { "FALL_SKYDIVE_R", nullptr, 1.0f, 0.0f, true, false, false };
            return true;
        case Packets::Players::PLAYER_PARACHUTE_FREEFALL_ACCEL:
            definition = { "FALL_SKYDIVE_ACCEL", nullptr, 1.0f, 0.0f, true, false, false };
            return true;
        case Packets::Players::PLAYER_PARACHUTE_OPENING:
            definition = { "PARA_OPEN", "PARA_OPEN_O", 8.0f, 1000.0f, false, true, false };
            return true;
        case Packets::Players::PLAYER_PARACHUTE_DEPLOYED:
            definition = { "PARA_FLOAT", "PARA_FLOAT_O", 2.0f, 2.0f, true, true, false };
            return true;
        case Packets::Players::PLAYER_PARACHUTE_DEPLOYED_LEFT:
            definition = { "PARA_STEERL", "PARA_STEERL_O", 1.0f, 1.0f, true, true, false };
            return true;
        case Packets::Players::PLAYER_PARACHUTE_DEPLOYED_RIGHT:
            definition = { "PARA_STEERR", "PARA_STEERR_O", 1.0f, 1.0f, true, true, false };
            return true;
        case Packets::Players::PLAYER_PARACHUTE_DEPLOYED_FLARE:
            definition = { "PARA_DECEL", "PARA_DECEL_O", 1.0f, 1.0f, true, true, false };
            return true;
        case Packets::Players::PLAYER_PARACHUTE_COLLAPSED:
            definition = { nullptr, "PARA_RIP_LOOP_O", 8.0f, 8.0f, true, true, false };
            return true;
        case Packets::Players::PLAYER_PARACHUTE_LANDING:
            definition = { "PARA_LAND", "PARA_LAND_O", 8.0f, 1000.0f, false, true, true };
            return true;
        case Packets::Players::PLAYER_PARACHUTE_LANDING_WATER:
            definition = { "PARA_LAND_WATER", "PARA_LAND_WATER_O", 8.0f, 1000.0f, false, true, false };
            return true;
        default:
            return false;
    }
}

CAnimBlendAssociation* FindNamedParachuteAssociation(RpClump* clump, const char* animationName)
{
    if (!clump || !animationName)
    {
        return nullptr;
    }

    const int blockId = CAnimManager::GetAnimationBlockIndex("PARACHUTE");
    const uint32_t animationHash = CKeyGen::GetUppercaseKey(animationName);
    for (CAnimBlendAssociation* association = RpAnimBlendClumpGetFirstAssociation(clump); association;
         association = RpAnimBlendGetNextAssociation(association))
    {
        if (association->m_pHierarchy && association->m_pHierarchy->m_nAnimBlockId == blockId &&
            association->m_pHierarchy->m_hashKey == animationHash)
        {
            return association;
        }
    }
    return nullptr;
}

void CorrectParachuteAnimationProgress(CAnimBlendAssociation* association, uint8_t progress,
    uint32_t receivedAt, bool looped)
{
    if (!association || !association->m_pHierarchy || association->m_pHierarchy->m_fTotalTime <= 0.0f)
    {
        return;
    }

    const float totalTime = association->m_pHierarchy->m_fTotalTime;
    float targetTime = totalTime * (static_cast<float>(progress) / 255.0f);
    targetTime += static_cast<float>(GetTickCount() - receivedAt) / 1000.0f * association->m_fSpeed;
    if (looped)
    {
        while (targetTime >= totalTime)
        {
            targetTime -= totalTime;
        }
    }
    else
    {
        targetTime = std::min(targetTime, totalTime);
    }

    float drift = fabsf(association->m_fCurrentTime - targetTime);
    if (looped && drift > totalTime * 0.5f)
    {
        drift = totalTime - drift;
    }
    if (drift > 0.15f)
    {
        association->SetCurrentTime(targetTime);
    }
}
}

CNetworkPlayer::~CNetworkPlayer()
{
    ResetTransformInterpolation();
    ClearSyncedParachuteState();
    ClearSyncedAnimationState();
    if (m_pPed)
        DestroyPed();
    CNetworkEntityStreamManager::Forget(this);
}

CNetworkPlayer::CNetworkPlayer(int id, CVector position)
{
    m_iPlayerId = id;

    m_pPedClothesDesc.SetTextureAndModel("VEST", "VEST", 0);
    m_pPedClothesDesc.SetTextureAndModel("JEANSDENIM", "JEANS", 2);
    m_pPedClothesDesc.SetTextureAndModel("SNEAKERBINCBLK", "SNEAKER", 3);
    m_pPedClothesDesc.SetTextureAndModel("PLAYER_FACE", "HEAD", 1);
    m_vecLogicalPosition = position;
    m_vecMapPosition = position;
    m_nLogicalArea = AREA_MAIN_MAP;
}

void CNetworkPlayer::CreatePed(int id, CVector position)
{
    if (m_pPed)
        return;

    CPlayerPed* localPlayer = FindPlayerPed(0);
    if (!localPlayer || !localPlayer->m_pRwClump || !CTxdStore::ms_pTxdPool)
        return;
    const int playerTxd = CTxdStore::FindTxdSlot("player");
    TxdDef* txd = playerTxd >= 0 ? CTxdStore::ms_pTxdPool->GetAt(playerTxd) : nullptr;
    if (!txd || !txd->m_pRwDictionary)
        return;

    unsigned int actorId = 0;
    int playerId = id + 2;

    plugin::Command<Commands::CREATE_PLAYER>(playerId, position.x, position.y, position.z, &actorId);
    plugin::Command<Commands::GET_PLAYER_CHAR>(playerId, &actorId);

    m_pPed = (CPlayerPed*)CPools::GetPed(actorId);

    if (!m_pPed || !m_pPed->m_pPlayerData)
    {
        m_pPed = nullptr;
        return;
    }

    m_pPed->SetOrientation(0.0f, 0.0f, 0.0f);

    // set player immunies, he doesn't care about the pain now
    Command<Commands::SET_CHAR_PROOFS>(actorId, 0, 1, 1, 0, 0);

    // THIS IS AN EXPERIMENTAL SOLUTION FOR THE 0x4D68BA CRASH
    m_pPed->m_bStreamingDontDelete = true;
    m_pPed->m_nAreaCode = m_nLogicalArea;

    if (m_bHasGameplayState)
    {
        ApplyGameplayState(m_gameplayState);
    }

    ApplySyncedAnimation();
    ApplySyncedParachute();
    m_nLastPresentationChangeAt = GetTickCount();
}

void CNetworkPlayer::DestroyPed()
{
    ClearLaserScopeDotState();
    DestroySyncedParachutePresentation();
    if (!m_pPed)
    {
        return;
    }

    if (m_pPed->m_pVehicle)
    {
        CVehicle* oldVehicle = m_pPed->m_pVehicle;
        const CVector safePosition = m_pPed->GetPosition();
        plugin::Command<Commands::WARP_CHAR_FROM_CAR_TO_COORD>(CPools::GetPedRef(m_pPed),
            safePosition.x, safePosition.y, safePosition.z);
        if (m_pPed->m_pVehicle == oldVehicle)
        {
            if (oldVehicle->m_pDriver == m_pPed)
                oldVehicle->m_pDriver = nullptr;
            for (CPed*& passenger : oldVehicle->m_apPassengers)
            {
                if (passenger == m_pPed)
                    passenger = nullptr;
            }
            m_pPed->m_pVehicle = nullptr;
            m_pPed->m_nPedFlags.bInVehicle = false;
        }
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
    m_nAppliedTaskGeneration = 0;
    m_nAppliedEnExTransitionGeneration = 0;
    m_nLastPresentationChangeAt = GetTickCount();
}

bool CNetworkPlayer::Materialize(CVector position)
{
    if (m_pPed)
        return true;
    CreatePed(m_iPlayerId, position);
    if (!m_pPed)
        return false;
    m_transformInterpolator.ClearSnapshots();
    ApplyCachedPresentation();
    return true;
}

void CNetworkPlayer::StreamOut()
{
    m_transformInterpolator.ClearSnapshots();
    if (m_pPed)
        DestroyPed();
}

bool CNetworkPlayer::CacheOnFootSnapshot(const Packets::Players::OnFootUpdate& snapshot)
{
    const uint8_t previousArea = m_nLogicalArea;
    m_nLogicalArea = snapshot.areaId;
    const CNetworkTransformSnapshot transform = MakePlayerOnFootTransform(snapshot, m_iPlayerId, m_nLogicalArea);
    if (!m_transformInterpolator.Push(transform, PLAYER_TELEPORT_DISTANCE))
    {
        m_nLogicalArea = previousArea;
        return false;
    }

    m_onFootSnapshotInterpolated = snapshot;
    m_bHasOnFootSnapshot = true;
    m_vecLogicalPosition = snapshot.vecPos;
    if (m_nLogicalArea == AREA_MAIN_MAP)
        m_vecMapPosition = snapshot.vecPos;
    m_oldControllerState = snapshot.keySnapshot.oldControllerState;
    m_newControllerState = snapshot.keySnapshot.newControllerState;
    if (m_pPed && m_pPed->IsVTableValid() && m_pPed->m_nAreaCode != m_nLogicalArea)
    {
        m_pPed->m_nAreaCode = m_nLogicalArea;
        m_pPed->UpdateRwMatrix();
    }
    ClearVehicleRelation(false);
    if (snapshot.healthSnapshot.iHealth == 0)
    {
        m_transformInterpolator.Reset();
        m_transformInterpolator.Push(transform, PLAYER_TELEPORT_DISTANCE);
    }
    return true;
}

bool CNetworkPlayer::CacheVehicleDriverSnapshot(const Packets::Vehicles::VehicleDriverUpdate& snapshot)
{
    if (!ResetTransformInterpolation(snapshot.serverTime))
        return false;
    m_vehicleDriverSnapshot = snapshot;
    m_bHasVehicleDriverSnapshot = true;
    m_bHasVehiclePassengerSnapshot = false;
    m_onFootSnapshotInterpolated.healthSnapshot = snapshot.playerHealth;
    m_oldControllerState = snapshot.playerKeys.oldControllerState;
    m_newControllerState = snapshot.playerKeys.newControllerState;
    CacheVehicleRelation(snapshot.vehicleid, -1, false, true, true);
    if (CNetworkVehicle* vehicle = CNetworkVehicleManager::GetVehicle(snapshot.vehicleid))
    {
        m_vecLogicalPosition = vehicle->GetLogicalPosition();
        m_nLogicalArea = vehicle->m_nLogicalArea;
        if (m_nLogicalArea == AREA_MAIN_MAP)
            m_vecMapPosition = m_vecLogicalPosition;
    }
    return true;
}

bool CNetworkPlayer::CacheVehiclePassengerSnapshot(const Packets::Vehicles::VehiclePassengerUpdate& snapshot)
{
    if (!ResetTransformInterpolation(snapshot.serverTime))
        return false;
    m_vehiclePassengerSnapshot = snapshot;
    m_bHasVehiclePassengerSnapshot = true;
    m_bHasVehicleDriverSnapshot = false;
    m_onFootSnapshotInterpolated.healthSnapshot = snapshot.playerHealth;
    m_oldControllerState = snapshot.playerKeys.oldControllerState;
    m_newControllerState = snapshot.playerKeys.newControllerState;
    CacheVehicleRelation(snapshot.vehicleid, snapshot.seatid, true, true, true);
    if (CNetworkVehicle* vehicle = CNetworkVehicleManager::GetVehicle(snapshot.vehicleid))
    {
        m_vecLogicalPosition = vehicle->GetLogicalPosition();
        m_nLogicalArea = vehicle->m_nLogicalArea;
        if (m_nLogicalArea == AREA_MAIN_MAP)
            m_vecMapPosition = m_vecLogicalPosition;
    }
    return true;
}

void CNetworkPlayer::CacheOnFootRotation(server_time_t serverTime, float currentRotation, float aimingRotation)
{
    m_onFootSnapshotInterpolated.currentRotation = currentRotation;
    m_onFootSnapshotInterpolated.aimingRotation = aimingRotation;
    m_transformInterpolator.UpdateNewestAngles(serverTime, currentRotation, aimingRotation);
}

void CNetworkPlayer::CacheVehicleRelation(int vehicleId, int seatId, bool passenger, bool force, bool confirmed)
{
    m_nPendingVehicleId = vehicleId;
    m_nPendingVehicleSeat = static_cast<int8_t>(seatId);
    m_bPendingVehiclePassenger = passenger;
    m_bPendingVehicleForce = force;
    m_bPendingVehicleConfirmed = confirmed;
    m_bHasPendingVehicleRelation = true;
}

void CNetworkPlayer::ClearVehicleRelation(bool resetInterpolation)
{
    const bool changed = m_bHasPendingVehicleRelation || m_bHasVehicleDriverSnapshot || m_bHasVehiclePassengerSnapshot;
    m_nPendingVehicleId = -1;
    m_nPendingVehicleSeat = -1;
    m_bPendingVehiclePassenger = false;
    m_bPendingVehicleForce = false;
    m_bPendingVehicleConfirmed = false;
    m_bHasPendingVehicleRelation = false;
    m_bHasVehicleDriverSnapshot = false;
    m_bHasVehiclePassengerSnapshot = false;
    if (changed && resetInterpolation)
        ResetTransformInterpolation();
}

CVector CNetworkPlayer::GetLogicalPosition() const
{
    if (m_bHasPendingVehicleRelation)
    {
        if (CNetworkVehicle* vehicle = CNetworkVehicleManager::GetVehicle(m_nPendingVehicleId))
            return vehicle->GetLogicalPosition();
    }
    return m_vecLogicalPosition;
}

CVector CNetworkPlayer::GetMapPosition() const
{
    return m_nLogicalArea == AREA_MAIN_MAP ? GetLogicalPosition() : m_vecMapPosition;
}

void CNetworkPlayer::ApplyCachedPresentation()
{
    if (!m_pPed || !m_pPed->IsVTableValid())
        return;

    m_pPed->m_nAreaCode = m_nLogicalArea;
    if (m_bHasOnFootSnapshot && (!m_bHasPendingVehicleRelation || !m_bPendingVehicleConfirmed))
    {
        m_pPed->SetPosn(m_onFootSnapshotInterpolated.vecPos);
        m_pPed->m_vecMoveSpeed = m_onFootSnapshotInterpolated.vecMoveSpeed;
        m_pPed->m_fCurrentRotation = m_onFootSnapshotInterpolated.currentRotation.m_angle;
        m_pPed->m_fAimingRotation = m_onFootSnapshotInterpolated.aimingRotation.m_angle;
        m_pPed->m_fHealth = m_onFootSnapshotInterpolated.healthSnapshot.iHealth;
        m_pPed->m_fArmour = m_onFootSnapshotInterpolated.healthSnapshot.iArmour;
        ApplyWeaponSnapshot(m_onFootSnapshotInterpolated.weaponSnapshot);
        if (CUtil::IsPedHasJetpack(m_pPed) != m_onFootSnapshotInterpolated.bHasJetpack)
            CUtil::SetPlayerJetpack(this, m_onFootSnapshotInterpolated.bHasJetpack);
        if (CUtil::IsDucked(m_pPed) != m_onFootSnapshotInterpolated.bDucking)
        {
            if (m_onFootSnapshotInterpolated.bDucking)
                m_pPed->m_pIntelligence->SetTaskDuckSecondary(0);
            else
                m_pPed->m_pIntelligence->ClearTaskDuckSecondary();
        }
        m_pPed->m_nFightingStyle = m_onFootSnapshotInterpolated.iFightingStyle;
        m_pPed->m_nAllowedAttackMoves |= 15u;
    }

    ReconcilePendingVehiclePresentation();

    if (m_bHasVehicleDriverSnapshot)
    {
        ApplyWeaponSnapshot(m_vehicleDriverSnapshot.playerWeapon);
        m_pPed->m_fHealth = m_vehicleDriverSnapshot.playerHealth.iHealth;
        m_pPed->m_fArmour = m_vehicleDriverSnapshot.playerHealth.iArmour;
    }
    else if (m_bHasVehiclePassengerSnapshot)
    {
        ApplyWeaponSnapshot(m_vehiclePassengerSnapshot.playerWeapon);
        m_pPed->m_fHealth = m_vehiclePassengerSnapshot.playerHealth.iHealth;
        m_pPed->m_fArmour = m_vehiclePassengerSnapshot.playerHealth.iArmour;
        if (m_vehiclePassengerSnapshot.driveby && !CDriveBy::IsPedInDriveby(m_pPed))
            CDriveBy::StartDriveby(m_pPed);
    }

    ProcessPendingPresentation();
    if (m_bHasGameplayState)
        ApplyGameplayState(m_gameplayState);
    ApplySyncedAnimation();
    ApplySyncedParachute();
}

void CNetworkPlayer::ReconcilePendingVehiclePresentation()
{
    if (!m_pPed || !m_bHasPendingVehicleRelation ||
        (!m_bPendingVehicleConfirmed && !m_bPendingVehicleForce))
        return;
    CNetworkVehicle* networkVehicle = CNetworkVehicleManager::GetVehicle(m_nPendingVehicleId);
    CVehicle* vehicle = networkVehicle ? networkVehicle->m_pVehicle : nullptr;
    if (!vehicle || !vehicle->IsVTableValid())
        return;

    if (m_bPendingVehiclePassenger)
    {
        const int seat = m_nPendingVehicleSeat;
        const bool inExpectedSeat = seat >= 0 && seat < vehicle->m_nMaxPassengers &&
            m_pPed->m_nPedFlags.bInVehicle && m_pPed->m_pVehicle == vehicle &&
            vehicle->m_apPassengers[seat] == m_pPed;
        if (!inExpectedSeat)
            WarpIntoVehiclePassenger(vehicle, seat);
    }
    else if (!m_pPed->m_nPedFlags.bInVehicle || m_pPed->m_pVehicle != vehicle || vehicle->m_pDriver != m_pPed)
    {
        WarpIntoVehicleDriver(vehicle);
    }
}

void CNetworkPlayer::ProcessPendingPresentation()
{
    if (!m_pPed)
        return;
    ReconcilePendingVehiclePresentation();
    ApplyPendingTaskOnce();
    CEntryExitTransitionSync::ReplayPending(this);
    ProcessTransformInterpolation();
}

void CNetworkPlayer::ProcessTransformInterpolation()
{
    if (!m_pPed || !m_pPed->IsVTableValid() || m_iPlayerId == CNetworkPlayerManager::m_nMyId ||
        m_bHasPendingVehicleRelation || m_pPed->m_nPedFlags.bInVehicle)
        return;

    CNetworkTransformSample sample{};
    if (!m_transformInterpolator.Sample(g_serverTime, m_nRTT, sample))
        return;
    m_pPed->SetPosn(sample.position);
    m_pPed->m_vecMoveSpeed = sample.velocity;
    m_pPed->m_fCurrentRotation = sample.currentRotation;
    m_pPed->m_fAimingRotation = sample.aimingRotation;
}

bool CNetworkPlayer::ResetTransformInterpolation(server_time_t boundaryTime)
{
    if (boundaryTime == 0)
    {
        m_transformInterpolator.Reset();
        return true;
    }
    return m_transformInterpolator.ResetAt(boundaryTime);
}

bool CNetworkPlayer::ResetTransformInterpolationForCrossChannelBoundary(server_time_t boundaryTime)
{
    return m_transformInterpolator.ResetForCrossChannelBoundary(boundaryTime);
}

bool CNetworkPlayer::SnapOnFootTransform(
    CVector position, float currentRotation, float aimingRotation, server_time_t boundaryTime)
{
    if (!ResetTransformInterpolation(boundaryTime))
        return false;
    m_vecLogicalPosition = position;
    if (m_nLogicalArea == AREA_MAIN_MAP)
        m_vecMapPosition = position;
    m_onFootSnapshotInterpolated.vecPos = position;
    m_onFootSnapshotInterpolated.vecMoveSpeed = CVector{};
    m_onFootSnapshotInterpolated.currentRotation = currentRotation;
    m_onFootSnapshotInterpolated.aimingRotation = aimingRotation;
    if (m_pPed && m_pPed->IsVTableValid())
    {
        m_pPed->SetPosn(position);
        m_pPed->m_vecMoveSpeed = CVector{};
        m_pPed->m_fCurrentRotation = currentRotation;
        m_pPed->m_fAimingRotation = aimingRotation;
    }
    return true;
}

void CNetworkPlayer::Respawn(server_time_t boundaryTime)
{
    if (!ResetTransformInterpolation(boundaryTime))
        return;
    ClearSyncedParachuteState();
    ClearSyncedAnimationState();
    m_bHasPendingTask = false;
    m_nPendingTaskGeneration = 0;
    m_nAppliedTaskGeneration = 0;
    m_bHasPendingEnExTransition = false;
    m_nPendingEnExTransitionGeneration = 0;
    m_nAppliedEnExTransitionGeneration = 0;
    if (m_pPed)
        DestroyPed();
    ClearVehicleRelation(false);
    m_vecLogicalPosition = m_onFootSnapshotInterpolated.vecPos;
    m_onFootSnapshotInterpolated.healthSnapshot.iHealth = 100;
    m_onFootSnapshotInterpolated.healthSnapshot.iArmour = 0;
    m_bHasOnFootSnapshot = true;
}

int CNetworkPlayer::GetInternalId()  // most used for CWorld::PlayerInFocus
{
    // Do not match a streamed-out/respawning player to the first unused (also null) GTA player slot.
    if (m_pPed == nullptr)
        return -1;

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

    ClearSyncedParachuteState();

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

    ClearSyncedParachuteState();

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

    ClearSyncedParachuteState();

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
    m_pendingTask = packet;
    m_bHasPendingTask = true;
    ++m_nPendingTaskGeneration;
    if (m_nPendingTaskGeneration == 0)
        ++m_nPendingTaskGeneration;
    ApplyTaskPresentation(packet);
    if (m_pPed)
        m_nAppliedTaskGeneration = m_nPendingTaskGeneration;
}

void CNetworkPlayer::ApplyPendingTaskOnce()
{
    if (!m_pPed || !m_bHasPendingTask || m_nAppliedTaskGeneration == m_nPendingTaskGeneration)
        return;
    ApplyTaskPresentation(m_pendingTask);
    m_nAppliedTaskGeneration = m_nPendingTaskGeneration;
}

void CNetworkPlayer::ApplyTaskPresentation(Packets::Players::SetPlayerTask& packet)
{

    if (packet.hasParachuteState)
    {
        HandleSyncedParachute(packet);
        return;
    }

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

    if (!SnapOnFootTransform(
            packet.vecPos, packet.currentRotation.m_angle, packet.aimingRotation.m_angle, packet.serverTime))
        return;

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

void CNetworkPlayer::HandleSyncedParachute(const Packets::Players::SetPlayerTask& packet)
{
    if (!packet.IsParachuteStateSemanticallyValid() ||
        (m_bHasSyncedParachuteSequence &&
            !IsSequenceNewer(packet.parachuteSequence, m_nSyncedParachuteSequence)))
    {
        return;
    }

    m_bHasSyncedParachuteSequence = true;
    m_nSyncedParachuteSequence = packet.parachuteSequence;
    m_nSyncedParachuteProgress = packet.parachuteProgress;
    m_fSyncedParachutePitch = packet.parachutePitch;
    m_fSyncedParachuteRoll = packet.parachuteRoll;
    m_fSyncedParachuteHeading = packet.currentRotation.m_angle;
    m_nSyncedParachuteReceivedAt = GetTickCount();

    if (packet.parachuteState == Packets::Players::PLAYER_PARACHUTE_NONE)
    {
        ClearSyncedParachuteState();
        return;
    }

    if (!m_bParachuteResourcesAcquired)
    {
        CPlayerParachuteSyncManager::AcquireResources();
        m_bParachuteResourcesAcquired = true;
    }
    m_nSyncedParachuteState = packet.parachuteState;
    ApplySyncedParachute();
}

void CNetworkPlayer::ProcessSyncedParachute()
{
    if (m_nSyncedParachuteState == Packets::Players::PLAYER_PARACHUTE_NONE)
    {
        return;
    }
    if (!m_pPed || !m_pPed->IsAlive() || m_pPed->m_nPedFlags.bInVehicle || m_pPed->m_pVehicle)
    {
        ClearSyncedParachuteState();
        return;
    }
    if (GetTickCount() - m_nSyncedParachuteReceivedAt > CPlayerParachuteSyncManager::STALE_TIMEOUT_MS)
    {
        ClearSyncedParachuteState();
        return;
    }
    ApplySyncedParachute();
}

void CNetworkPlayer::ApplySyncedParachute()
{
    if (!m_pPed || !m_pPed->IsAlive() || m_pPed->m_nPedFlags.bInVehicle || m_pPed->m_pVehicle ||
        m_nSyncedParachuteState == Packets::Players::PLAYER_PARACHUTE_NONE)
    {
        DestroySyncedParachutePresentation();
        return;
    }

    SyncedParachuteDefinition definition{};
    if (!GetSyncedParachuteDefinition(m_nSyncedParachuteState, definition))
    {
        ClearSyncedParachuteState();
        return;
    }
    if (!CPlayerParachuteSyncManager::EnsureResourcesLoaded())
    {
        return;
    }

    const bool stateChanged = m_nAppliedParachuteState != m_nSyncedParachuteState;
    const bool missingPedAnimation = definition.pedAnimation &&
        !FindNamedParachuteAssociation(m_pPed->m_pRwClump, definition.pedAnimation);
    if (definition.pedAnimation && (stateChanged || missingPedAnimation))
    {
        Command<Commands::TASK_PLAY_ANIM_NON_INTERRUPTABLE>(m_pPed, definition.pedAnimation, "PARACHUTE",
            definition.blendDelta, definition.looped, false, false, !definition.looped, -2);
    }

    if (definition.showCanopy && !m_pSyncedParachuteCanopy)
    {
        const CVector position = m_pPed->GetPosition();
        Command<Commands::CREATE_OBJECT>(MODEL_PARACHUTE, position.x, position.y, position.z,
            &m_pSyncedParachuteCanopy);
        if (m_pSyncedParachuteCanopy)
        {
            Command<Commands::ATTACH_OBJECT_TO_CHAR>(m_pSyncedParachuteCanopy, m_pPed,
                0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
            m_bParachuteCanopyAttached = true;
        }
    }

    if (m_pSyncedParachuteCanopy)
    {
        Command<Commands::SET_OBJECT_VISIBLE>(m_pSyncedParachuteCanopy, definition.showCanopy);
        if (stateChanged && definition.canopyAnimation)
        {
            Command<Commands::PLAY_OBJECT_ANIM>(m_pSyncedParachuteCanopy, definition.canopyAnimation,
                "PARACHUTE", definition.canopySpeed, definition.looped, true);
        }
        if (definition.detachCanopy && m_bParachuteCanopyAttached)
        {
            Command<Commands::DETACH_OBJECT>(m_pSyncedParachuteCanopy, 0.0f, 0.0f, 0.0f, false);
            m_bParachuteCanopyAttached = false;
        }
    }

    m_nAppliedParachuteState = m_nSyncedParachuteState;
    m_pPed->SetOrientation(m_fSyncedParachutePitch, m_fSyncedParachuteRoll, m_fSyncedParachuteHeading);

    CorrectParachuteAnimationProgress(
        FindNamedParachuteAssociation(m_pPed->m_pRwClump, definition.pedAnimation),
        m_nSyncedParachuteProgress, m_nSyncedParachuteReceivedAt, definition.looped);
    if (m_pSyncedParachuteCanopy)
    {
        CorrectParachuteAnimationProgress(
            FindNamedParachuteAssociation(m_pSyncedParachuteCanopy->m_pRwClump, definition.canopyAnimation),
            m_nSyncedParachuteProgress, m_nSyncedParachuteReceivedAt, definition.looped);
    }
}

void CNetworkPlayer::DestroySyncedParachutePresentation()
{
    if (m_pPed && m_pPed->m_pIntelligence)
    {
        CTask* namedTask = m_pPed->m_pIntelligence->m_TaskMgr.FindActiveTaskByType(TASK_SIMPLE_NAMED_ANIM);
        CTaskSimpleRunNamedAnim* parachuteTask = static_cast<CTaskSimpleRunNamedAnim*>(namedTask);
        if (parachuteTask && _stricmp(parachuteTask->m_animGroupName, "PARACHUTE") == 0)
        {
            m_pPed->m_pIntelligence->m_TaskMgr.SetTask(nullptr, TASK_PRIMARY_PRIMARY, false);
        }

        const int blockId = CAnimManager::GetAnimationBlockIndex("PARACHUTE");
        if (m_pPed->m_pRwClump && blockId >= 0)
        {
            for (CAnimBlendAssociation* association = RpAnimBlendClumpGetFirstAssociation(m_pPed->m_pRwClump);
                 association; association = RpAnimBlendGetNextAssociation(association))
            {
                if (association->m_pHierarchy && association->m_pHierarchy->m_nAnimBlockId == blockId)
                {
                    association->m_fBlendDelta = -8.0f;
                }
            }
        }
    }

    if (m_pSyncedParachuteCanopy)
    {
        Command<Commands::DELETE_OBJECT>(m_pSyncedParachuteCanopy);
        m_pSyncedParachuteCanopy = nullptr;
    }
    m_bParachuteCanopyAttached = false;
    m_nAppliedParachuteState = Packets::Players::PLAYER_PARACHUTE_NONE;
}

void CNetworkPlayer::ClearSyncedParachuteState()
{
    DestroySyncedParachutePresentation();
    if (m_bParachuteResourcesAcquired)
    {
        CPlayerParachuteSyncManager::ReleaseResources();
        m_bParachuteResourcesAcquired = false;
    }
    m_nSyncedParachuteState = Packets::Players::PLAYER_PARACHUTE_NONE;
    m_nSyncedParachuteProgress = 0;
    m_fSyncedParachutePitch = 0.0f;
    m_fSyncedParachuteRoll = 0.0f;
    m_fSyncedParachuteHeading = 0.0f;
    m_nSyncedParachuteReceivedAt = 0;
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
    if (weaponSnapshot.iWeaponType != WEAPON_PARACHUTE)
    {
        ClearSyncedParachuteState();
    }
    // TODO refactor CUtil
    CUtil::GiveWeaponByPacket(this, weaponSnapshot.iWeaponType, weaponSnapshot.nAmmo);
    m_pPed->m_aWeapons[m_pPed->m_nActiveWeaponSlot].m_nState = static_cast<eWeaponState>(weaponSnapshot.iWeaponState);
}
