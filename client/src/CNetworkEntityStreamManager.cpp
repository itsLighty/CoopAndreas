#include "stdafx.h"
#include "CNetworkEntityStreamManager.h"
#include "CNetworkEntityBlip.h"
#include "CNetworkPedGroupSyncManager.h"
#include "CMissionSessionClient.h"
#include <unordered_set>

namespace
{
constexpr float STREAM_IN_DISTANCE = 250.0f;
constexpr float STREAM_OUT_DISTANCE = 320.0f;
constexpr uint32_t MODEL_RETRY_INTERVAL_MS = 250;
constexpr uint32_t MODEL_LOAD_TIMEOUT_MS = 10000;
constexpr uint32_t MODEL_RETRY_COOLDOWN_MS = 5000;
constexpr uint32_t MIN_PRESENTATION_LIFETIME_MS = 1000;
constexpr size_t MAX_MODEL_REQUESTS_PER_FRAME = 8;
constexpr size_t MAX_MATERIALIZATIONS_PER_FRAME = 4;

struct ModelLease
{
    size_t references = 0;
    uint8_t originalFlags = 0;
    std::string specialName;
};

std::unordered_map<int, ModelLease> g_modelLeases;

float DistanceSquared(const CVector& left, const CVector& right)
{
    const float x = left.x - right.x;
    const float y = left.y - right.y;
    const float z = left.z - right.z;
    return x * x + y * y + z * z;
}

bool HasReadyTxd(CBaseModelInfo* modelInfo)
{
    if (!modelInfo || modelInfo->m_nTxdIndex < 0 || !CTxdStore::ms_pTxdPool)
        return false;
    TxdDef* txd = CTxdStore::ms_pTxdPool->GetAt(modelInfo->m_nTxdIndex);
    return txd && txd->m_pRwDictionary;
}

bool IsModelReady(int modelId)
{
    if (modelId < 0 || modelId > MODEL_UTILTR1 ||
        CStreaming::ms_aInfoForModel[modelId].m_nLoadState != LOADSTATE_LOADED)
        return false;
    CBaseModelInfo* modelInfo = CModelInfo::ms_modelInfoPtrs[modelId];
    return modelInfo && modelInfo->m_pRwObject && HasReadyTxd(modelInfo);
}

bool AcquireModelLease(int modelId, const char* specialName, bool special)
{
    auto [it, inserted] = g_modelLeases.emplace(modelId, ModelLease{});
    ModelLease& lease = it->second;
    const std::string requestedSpecialName = special && specialName ? specialName : "";
    if (!inserted && special && !lease.specialName.empty() && lease.specialName != requestedSpecialName)
        return false;
    if (lease.references == 0)
    {
        lease.originalFlags = CStreaming::ms_aInfoForModel[modelId].m_nFlags;
        lease.specialName = requestedSpecialName;
    }
    ++lease.references;
    return true;
}

void ReleaseModelLease(int modelId)
{
    auto it = g_modelLeases.find(modelId);
    if (it == g_modelLeases.end())
        return;
    ModelLease& lease = it->second;
    if (lease.references > 1)
    {
        --lease.references;
        return;
    }

    const uint8_t originalFlags = lease.originalFlags;
    g_modelLeases.erase(it);
    CStreaming::ms_aInfoForModel[modelId].m_nFlags = originalFlags;
    if ((originalFlags & (GAME_REQUIRED | MISSION_REQUIRED | KEEP_IN_MEMORY)) == 0)
    {
        CStreaming::SetModelIsDeletable(modelId);
        CStreaming::SetModelTxdIsDeletable(modelId);
    }
}

void RequestModel(int modelId, const char* specialName, bool special)
{
    if (special)
        CStreaming::RequestSpecialModel(modelId, specialName, MISSION_REQUIRED | PRIORITY_REQUEST);
    else
        CStreaming::RequestModel(modelId, MISSION_REQUIRED | PRIORITY_REQUEST);
}

template <typename Entity>
bool PrepareModel(Entity* entity, int modelId, const char* specialName, bool special, uint32_t now,
    size_t& requestsThisFrame)
{
    if (!entity->m_bModelLeaseHeld)
    {
        if (now < entity->m_nNextModelRequestAt || requestsThisFrame >= MAX_MODEL_REQUESTS_PER_FRAME)
            return false;
        if (!AcquireModelLease(modelId, specialName, special))
        {
            entity->m_nNextModelRequestAt = now + MODEL_RETRY_INTERVAL_MS;
            return false;
        }
        entity->m_bModelLeaseHeld = true;
        entity->m_nModelRequestStartedAt = now;
        entity->m_nModelRequestAttempts = 0;
    }

    if (IsModelReady(modelId))
        return true;

    if (now - entity->m_nModelRequestStartedAt >= MODEL_LOAD_TIMEOUT_MS)
    {
        ReleaseModelLease(modelId);
        entity->m_bModelLeaseHeld = false;
        entity->m_nModelRequestStartedAt = 0;
        entity->m_nModelRequestAttempts = 0;
        entity->m_nNextModelRequestAt = now + MODEL_RETRY_COOLDOWN_MS;
        return false;
    }

    if (now >= entity->m_nNextModelRequestAt && requestsThisFrame < MAX_MODEL_REQUESTS_PER_FRAME)
    {
        RequestModel(modelId, specialName, special);
        ++requestsThisFrame;
        ++entity->m_nModelRequestAttempts;
        entity->m_nNextModelRequestAt = now + MODEL_RETRY_INTERVAL_MS;
    }
    return false;
}

template <typename Entity>
void ReleaseEntityModel(Entity* entity, int modelId)
{
    if (!entity || !entity->m_bModelLeaseHeld)
        return;
    ReleaseModelLease(modelId);
    entity->m_bModelLeaseHeld = false;
    entity->m_nModelRequestStartedAt = 0;
    entity->m_nModelRequestAttempts = 0;
}

bool IsPlayerResourceReady()
{
    CPlayerPed* localPlayer = FindPlayerPed(0);
    if (!localPlayer || !localPlayer->m_pRwClump || !CTxdStore::ms_pTxdPool)
        return false;
    const int playerTxd = CTxdStore::FindTxdSlot("player");
    TxdDef* txd = playerTxd >= 0 ? CTxdStore::ms_pTxdPool->GetAt(playerTxd) : nullptr;
    return txd && txd->m_pRwDictionary;
}

bool WithinPresentationRange(const CNetworkPlayer* entity, const CVector& localPosition)
{
    const float distance = entity->m_pPed ? STREAM_OUT_DISTANCE : STREAM_IN_DISTANCE;
    return DistanceSquared(entity->GetLogicalPosition(), localPosition) <= distance * distance;
}

bool WithinPresentationRange(const CNetworkPed* entity, const CVector& localPosition)
{
    const float distance = entity->m_pPed ? STREAM_OUT_DISTANCE : STREAM_IN_DISTANCE;
    return DistanceSquared(entity->GetLogicalPosition(), localPosition) <= distance * distance;
}

bool WithinPresentationRange(const CNetworkVehicle* entity, const CVector& localPosition)
{
    const float distance = entity->m_pVehicle ? STREAM_OUT_DISTANCE : STREAM_IN_DISTANCE;
    return DistanceSquared(entity->GetLogicalPosition(), localPosition) <= distance * distance;
}

void MarkSerializedTarget(const CNetworkEntitySerializer& target,
    std::unordered_set<CNetworkPlayer*>& players, std::unordered_set<CNetworkPed*>& peds)
{
    if (target.entityType == NETWORK_ENTITY_TYPE_PLAYER)
    {
        if (CNetworkPlayer* player = CNetworkPlayerManager::GetPlayer(target.entityId))
            players.insert(player);
    }
    else if (target.entityType == NETWORK_ENTITY_TYPE_PED)
    {
        if (CNetworkPed* ped = CNetworkPedManager::GetPed(target.entityId))
            peds.insert(ped);
    }
}

bool HasLocalOccupant(CNetworkVehicle* networkVehicle)
{
    CPlayerPed* localPlayer = FindPlayerPed(0);
    return localPlayer && networkVehicle && networkVehicle->m_pVehicle && localPlayer->m_pVehicle == networkVehicle->m_pVehicle &&
           localPlayer->m_nPedFlags.bInVehicle;
}
}  // namespace

void CNetworkEntityStreamManager::Process()
{
    if (!CNetwork::m_bAuthenticated)
        return;

    CPlayerPed* localPlayer = FindPlayerPed(0);
    if (!localPlayer)
        return;
    const CVector localPosition = localPlayer->GetPosition();
    const uint8_t localArea = static_cast<uint8_t>(CGame::currArea);
    const uint32_t now = GetTickCount();

    std::unordered_set<CNetworkPlayer*> requiredPlayers;
    std::unordered_set<CNetworkPed*> requiredPeds;
    std::unordered_set<CNetworkVehicle*> requiredVehicles;

    for (CNetworkPlayer* player : CNetworkPlayerManager::m_pPlayers)
    {
        const auto& mission = CMissionSessionClient::GetState();
        const bool missionParticipant = player && mission.IsActive() &&
            mission.ContainsGameplayParticipant(player->m_iPlayerId);
        if (player && player->m_nLogicalArea == localArea &&
            (player->m_bIsHost || missionParticipant ||
                CNetworkPedGroupSyncManager::IsPlayerPresentationRequired(player->m_iPlayerId) ||
                WithinPresentationRange(player, localPosition)))
            requiredPlayers.insert(player);
    }
    for (CNetworkPed* ped : CNetworkPedManager::m_pPeds)
    {
        if (ped && (ped->m_bSyncing || (ped->m_nLogicalArea == localArea &&
            (ped->m_nBlipHandle != -1 || CNetworkEntityBlip::HasDesiredPedBlip(ped->m_nPedId) ||
                CNetworkPedGroupSyncManager::IsPedPresentationRequired(ped->m_nPedId) ||
                WithinPresentationRange(ped, localPosition)))))
            requiredPeds.insert(ped);
    }
    for (CNetworkVehicle* vehicle : CNetworkVehicleManager::m_pVehicles)
    {
        if (vehicle && (vehicle->m_bSyncing || (vehicle->m_nLogicalArea == localArea &&
            (vehicle->m_nBlipHandle != -1 || CNetworkEntityBlip::HasDesiredVehicleBlip(vehicle->m_nVehicleId) ||
                HasLocalOccupant(vehicle) ||
                WithinPresentationRange(vehicle, localPosition)))))
            requiredVehicles.insert(vehicle);
    }

    // Close dependencies to a fixed point. Four passes exceed the longest supported chain
    // (task target -> occupant -> tractor -> trailer) without allowing an unbounded graph walk.
    for (int pass = 0; pass < 4; ++pass)
    {
        for (CNetworkPlayer* player : CNetworkPlayerManager::m_pPlayers)
        {
            if (!player || !player->m_bHasPendingVehicleRelation)
                continue;
            CNetworkVehicle* vehicle = CNetworkVehicleManager::GetVehicle(player->m_nPendingVehicleId);
            if (requiredPlayers.count(player) && vehicle)
                requiredVehicles.insert(vehicle);
            if (vehicle && requiredVehicles.count(vehicle))
                requiredPlayers.insert(player);
        }
        for (CNetworkPed* ped : CNetworkPedManager::m_pPeds)
        {
            if (!ped)
                continue;
            int vehicleId = -1;
            if (ped->m_presentationMode == CNetworkPed::PresentationMode::DRIVER && ped->m_bHasDriverSnapshot)
                vehicleId = ped->m_driverSnapshot.vehicleid;
            else if (ped->m_presentationMode == CNetworkPed::PresentationMode::PASSENGER && ped->m_bHasPassengerSnapshot)
                vehicleId = ped->m_passengerSnapshot.vehicleid;
            CNetworkVehicle* vehicle = vehicleId >= 0 ? CNetworkVehicleManager::GetVehicle(vehicleId) : nullptr;
            if (requiredPeds.count(ped) && vehicle)
                requiredVehicles.insert(vehicle);
            if (vehicle && requiredVehicles.count(vehicle))
                requiredPeds.insert(ped);
            if (requiredPeds.count(ped) && ped->m_bHasOnFootSnapshot &&
                ped->m_onFootSnapshot.task.type == Packets::Peds::ePedTaskSyncType::KILL_PED_ON_FOOT)
                MarkSerializedTarget(ped->m_onFootSnapshot.task.target, requiredPlayers, requiredPeds);
        }
        for (CNetworkVehicle* vehicle : CNetworkVehicleManager::m_pVehicles)
        {
            if (!vehicle)
                continue;
            const int trailerId = vehicle->m_lastAuxState.trailerId;
            CNetworkVehicle* trailer = trailerId == Packets::Vehicles::VEHICLE_TRAILER_NONE
                ? nullptr : CNetworkVehicleManager::GetVehicle(trailerId);
            if (requiredVehicles.count(vehicle) && trailer)
                requiredVehicles.insert(trailer);
            if (trailer && requiredVehicles.count(trailer))
                requiredVehicles.insert(vehicle);
        }
    }

    // Occupants must disappear before a vehicle presentation. Logical relations stay cached for re-materialization.
    for (CNetworkPlayer* player : CNetworkPlayerManager::m_pPlayers)
    {
        if (!player || !player->m_pPed)
            continue;
        if ((!requiredPlayers.count(player) || player->m_nLogicalArea != localArea) &&
            now - player->m_nLastPresentationChangeAt >= MIN_PRESENTATION_LIFETIME_MS)
        {
            player->StreamOut();
        }
    }
    for (CNetworkPed* ped : CNetworkPedManager::m_pPeds)
    {
        if (!ped || ped->m_bSyncing)
            continue;
        if (ped->m_pPed && (!requiredPeds.count(ped) || ped->m_nLogicalArea != localArea) &&
            now - ped->m_nLastPresentationChangeAt >= MIN_PRESENTATION_LIFETIME_MS)
            ped->StreamOut();
        if ((!requiredPeds.count(ped) || ped->m_nLogicalArea != localArea) && !ped->m_pPed)
            ReleaseEntityModel(ped, ped->m_nModelId);
    }
    for (CNetworkVehicle* vehicle : CNetworkVehicleManager::m_pVehicles)
    {
        if (!vehicle || vehicle->m_bSyncing)
            continue;
        if (vehicle->m_pVehicle && (!requiredVehicles.count(vehicle) || vehicle->m_nLogicalArea != localArea) &&
            now - vehicle->m_nLastPresentationChangeAt >= MIN_PRESENTATION_LIFETIME_MS)
            vehicle->StreamOut();
        if ((!requiredVehicles.count(vehicle) || vehicle->m_nLogicalArea != localArea) && !vehicle->m_pVehicle)
            ReleaseEntityModel(vehicle, vehicle->m_nModelId);
    }

    size_t requestsThisFrame = 0;
    size_t materializationsThisFrame = 0;
    for (CNetworkVehicle* vehicle : CNetworkVehicleManager::m_pVehicles)
    {
        if (!vehicle || vehicle->m_pVehicle || !requiredVehicles.count(vehicle) ||
            (!vehicle->m_bSyncing && vehicle->m_nLogicalArea != localArea))
            continue;
        if (PrepareModel(vehicle, vehicle->m_nModelId, nullptr, false, now, requestsThisFrame) &&
            materializationsThisFrame < MAX_MATERIALIZATIONS_PER_FRAME && vehicle->Materialize())
            ++materializationsThisFrame;
    }
    for (CNetworkPlayer* player : CNetworkPlayerManager::m_pPlayers)
    {
        if (!player || player->m_pPed || !requiredPlayers.count(player) || player->m_nLogicalArea != localArea)
            continue;
        if (IsPlayerResourceReady() && materializationsThisFrame < MAX_MATERIALIZATIONS_PER_FRAME &&
            player->Materialize(player->GetLogicalPosition()))
            ++materializationsThisFrame;
    }
    for (CNetworkPed* ped : CNetworkPedManager::m_pPeds)
    {
        if (!ped || ped->m_pPed || !requiredPeds.count(ped) || (!ped->m_bSyncing && ped->m_nLogicalArea != localArea))
            continue;
        const bool special = ped->m_nModelId >= MODEL_SPECIAL01 && ped->m_nModelId <= MODEL_SPECIAL10;
        if (PrepareModel(ped, ped->m_nModelId, ped->m_szSpecialModelName, special, now, requestsThisFrame) &&
            materializationsThisFrame < MAX_MATERIALIZATIONS_PER_FRAME && ped->Materialize())
            ++materializationsThisFrame;
    }

    for (CNetworkVehicle* vehicle : CNetworkVehicleManager::m_pVehicles)
    {
        if (vehicle && vehicle->m_pVehicle && !vehicle->m_bSyncing)
            vehicle->ProcessPendingPresentation();
    }
    for (CNetworkPlayer* player : CNetworkPlayerManager::m_pPlayers)
    {
        if (player && player->m_pPed)
            player->ProcessPendingPresentation();
    }
    for (CNetworkPed* ped : CNetworkPedManager::m_pPeds)
    {
        if (ped && ped->m_pPed && !ped->m_bSyncing)
            ped->ProcessPendingPresentation();
    }
}

void CNetworkEntityStreamManager::Forget(CNetworkPlayer* player)
{
    (void)player;
}

void CNetworkEntityStreamManager::Forget(CNetworkPed* ped)
{
    if (ped)
        ReleaseEntityModel(ped, ped->m_nModelId);
}

void CNetworkEntityStreamManager::Forget(CNetworkVehicle* vehicle)
{
    if (vehicle)
        ReleaseEntityModel(vehicle, vehicle->m_nModelId);
}

void CNetworkEntityStreamManager::Reset()
{
    for (const auto& [modelId, lease] : g_modelLeases)
    {
        CStreaming::ms_aInfoForModel[modelId].m_nFlags = lease.originalFlags;
        if ((lease.originalFlags & (GAME_REQUIRED | MISSION_REQUIRED | KEEP_IN_MEMORY)) == 0)
        {
            CStreaming::SetModelIsDeletable(modelId);
            CStreaming::SetModelTxdIsDeletable(modelId);
        }
    }
    g_modelLeases.clear();
}
