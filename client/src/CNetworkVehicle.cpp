#include "stdafx.h"
#include "CNetworkVehicle.h"
#include <CAEAudioHardware.h>
#include <CAERadioTrackManager.h>
#include "CNetworkEntityStreamManager.h"
#include "CServerTime.h"

namespace
{
constexpr float VEHICLE_TELEPORT_DISTANCE = 100.0f;
constexpr uint32_t TRAILER_STREAM_TIMEOUT_MS = 5000;
constexpr uint32_t RADIO_CORRECTION_INTERVAL_MS = 500;
constexpr int RADIO_DRIFT_TOLERANCE_MS = 2000;
constexpr int RADIO_USER_TRACKS_ID = 12;

CNetworkTransformSnapshot MakeVehicleTransform(server_time_t serverTime, const CVector& position,
    const CVector& velocity, const CVector& turnSpeed, const CVector& right, const CVector& forward,
    eNetworkTransformSource source, uint32_t sourceId, uint8_t area)
{
    CNetworkTransformSnapshot transform{};
    transform.serverTime = serverTime;
    transform.position = position;
    transform.velocity = velocity;
    transform.turnSpeed = turnSpeed;
    transform.right = right;
    transform.forward = forward;
    transform.source = source;
    transform.sourceId = sourceId;
    transform.area = area;
    transform.hasVehicleOrientation = true;
    return transform;
}

int GetRadioTrackPlayTime()
{
    return plugin::CallMethodAndReturn<int, 0x4D8F60, CAEAudioHardware*>(&AEAudioHardware);
}

void ForceRadioTrack(int station, int trackId, int playTimeMs)
{
    const auto populateSettings = [station, trackId, playTimeMs](tRadioSettings& settings)
    {
        for (int& queuedTrack : settings.m_djIndex)
        {
            queuedTrack = -1;
        }
        settings.m_djIndex[0] = trackId;
        settings.field_10 = -1;
        settings.trackId = trackId;
        settings.trackPlayTime = playTimeMs;
        settings.field_24 = 2;
        settings.m_nCurrentRadioStation = static_cast<char>(station);
    };

    populateSettings(AERadioTrackManager.m_TempSettings);
    populateSettings(AERadioTrackManager.m_Settings);
    plugin::CallMethod<0x4D8F10, CAEAudioHardware*, unsigned int, int, unsigned int, unsigned char, bool, bool>(
        &AEAudioHardware, static_cast<unsigned int>(trackId), -1, static_cast<unsigned int>(playTimeMs), 2, false,
        false);
}
}

CNetworkVehicle::CNetworkVehicle(int vehicleid, int modelid, CVector pos, float rotation, unsigned char color1, unsigned char color2, unsigned char createdBy)
{
    if (auto vehicle = CNetworkVehicleManager::GetVehicle(vehicleid))
    {
        CNetworkVehicleManager::Remove(vehicle);
        delete vehicle;
    }

    m_nVehicleId = vehicleid;
    m_bSyncing = false;
    m_nTempId = 255;
    m_nModelId = modelid;
    m_nCreatedBy = createdBy;
    m_vecLogicalPosition = pos;
    m_fSpawnRotation = rotation;
    m_nSpawnColor1 = color1;
    m_nSpawnColor2 = color2;
    m_nLogicalArea = static_cast<uint8_t>(CGame::currArea);
    m_pendingComponents.fill(-1);
}

bool CNetworkVehicle::CreateVehicle(int vehicleid, int modelid, CVector pos, float rotation, unsigned char color1, unsigned char color2)
{
    if (modelid < MODEL_LANDSTAL || modelid > MODEL_UTILTR1 ||
        CStreaming::ms_aInfoForModel[modelid].m_nLoadState != LOADSTATE_LOADED)
        return false;

    CBaseModelInfo* modelInfo = CModelInfo::ms_modelInfoPtrs[modelid];
    if (!modelInfo || !modelInfo->m_pRwObject || !CTxdStore::ms_pTxdPool || modelInfo->m_nTxdIndex < 0)
        return false;
    TxdDef* txd = CTxdStore::ms_pTxdPool->GetAt(modelInfo->m_nTxdIndex);
    if (!txd || !txd->m_pRwDictionary)
        return false;

    switch (static_cast<CVehicleModelInfo*>(modelInfo)->m_nVehicleType)
    {
    case VEHICLE_MTRUCK:
        m_pVehicle = new CMonsterTruck(modelid, MISSION_VEHICLE); break;

    case VEHICLE_QUAD:
        m_pVehicle = new CQuadBike(modelid, MISSION_VEHICLE); break;

    case VEHICLE_HELI:
        m_pVehicle = new CHeli(modelid, MISSION_VEHICLE); break;

    case VEHICLE_PLANE:
        m_pVehicle = new CPlane(modelid, MISSION_VEHICLE); break;

    case VEHICLE_BIKE:
        m_pVehicle = new CBike(modelid, MISSION_VEHICLE);
        ((CBike*)m_pVehicle)->m_nDamageFlags |= 0x10; break;

    case VEHICLE_BMX:
        m_pVehicle = new CBmx(modelid, MISSION_VEHICLE);
        ((CBmx*)m_pVehicle)->m_nDamageFlags |= 0x10; break;

    case VEHICLE_TRAILER:
        m_pVehicle = new CTrailer(modelid, MISSION_VEHICLE); break;

    case VEHICLE_BOAT:
        m_pVehicle = new CBoat(modelid, MISSION_VEHICLE); break;

    case VEHICLE_TRAIN:
        return false;

    default:
        m_pVehicle = new CAutomobile(modelid, MISSION_VEHICLE, true); break;
    }

    if (!m_pVehicle)
        return false;

    m_pVehicle->SetPosn(pos);
    m_pVehicle->SetOrientation(0.0f, 0.0f, rotation);
    m_pVehicle->m_nStatus = 4;
    m_pVehicle->m_eDoorLock = DOORLOCK_UNLOCKED;
    m_pVehicle->m_nPrimaryColor = color1;
    m_pVehicle->m_nSecondaryColor = color2;
    m_pVehicle->m_nAreaCode = m_nLogicalArea;
    CWorld::Add(m_pVehicle);

    return true;
}

CNetworkVehicle::~CNetworkVehicle()
{
    ResetTransformInterpolation();
    if (m_bSyncing)
    {
        Packets::Vehicles::VehicleRemove vehicleRemovePacket{};
        vehicleRemovePacket.vehicleid = m_nVehicleId;
        GetPacketFactory().Send(vehicleRemovePacket);
    }
    else
    {
        StreamOut();
    }
    CNetworkEntityStreamManager::Forget(this);
}

bool CNetworkVehicle::Materialize()
{
    if (m_pVehicle)
        return true;
    if (!CreateVehicle(m_nVehicleId, m_nModelId, GetLogicalPosition(), m_fSpawnRotation,
            m_nSpawnColor1, m_nSpawnColor2))
        return false;

    m_nLastPresentationChangeAt = GetTickCount();
    if (m_bSyncing)
        m_pVehicle->SetVehicleCreatedBy(m_nCreatedBy);
    m_pendingComponentApplied.fill(false);
    m_bPendingDamageApplied = false;
    m_transformInterpolator.ClearSnapshots();
    ApplyCachedPresentation();
    return true;
}

void CNetworkVehicle::StreamOut()
{
    if (!m_pVehicle || m_bSyncing)
        return;

    m_transformInterpolator.ClearSnapshots();

    CVehicle* vehicle = m_pVehicle;
    // Vehicle deletion must never leave a ped pointing at freed vehicle memory. Iterating the native pool covers
    // the local player, remote players, network peds, and any stock occupant that entered this presentation.
    for (CPed* ped : CPools::ms_pPedPool)
    {
        if (!ped || !IsPedPointerValid(ped) || !ped->IsVTableValid() || ped->m_pVehicle != vehicle)
            continue;

        const CVector safePosition = ped->GetPosition();
        plugin::Command<Commands::WARP_CHAR_FROM_CAR_TO_COORD>(
            CPools::GetPedRef(ped), safePosition.x, safePosition.y, safePosition.z);
        if (ped->m_pVehicle == vehicle)
        {
            if (vehicle->m_pDriver == ped)
                vehicle->m_pDriver = nullptr;
            for (CPed*& passenger : vehicle->m_apPassengers)
            {
                if (passenger == ped)
                    passenger = nullptr;
            }
            ped->m_pVehicle = nullptr;
            ped->m_nPedFlags.bInVehicle = false;
        }
    }

    DetachTrailerLinks();
    if (m_nBlipHandle != -1 && vehicle->IsVTableValid())
    {
        CRadar::ClearBlipForEntity(eBlipType::BLIP_CAR, CPools::GetVehicleRef(vehicle));
        m_nBlipHandle = -1;
    }
    if (IsVehiclePointerValid(vehicle) && vehicle->IsVTableValid())
    {
        CWorld::Remove(vehicle);
        CWorld::RemoveReferencesToDeletedObject(vehicle);
        delete vehicle;
    }
    m_pVehicle = nullptr;
    m_pendingComponentApplied.fill(false);
    m_nAppliedTrailerId = Packets::Vehicles::VEHICLE_TRAILER_NONE;
    m_nPendingTrailerId = m_lastAuxState.trailerId;
    m_nPendingTrailerSince = 0;
    m_nLastPresentationChangeAt = GetTickCount();
}

bool CNetworkVehicle::CacheIdleSnapshot(const Packets::Vehicles::VehicleIdleUpdate& snapshot)
{
    const CNetworkTransformSnapshot transform = MakeVehicleTransform(snapshot.serverTime, snapshot.pos,
        snapshot.velocity, snapshot.turnSpeed, snapshot.roll, snapshot.rot,
        eNetworkTransformSource::VEHICLE_IDLE, static_cast<uint32_t>(std::max(m_nVehicleId, 0)), m_nLogicalArea);
    if (!m_transformInterpolator.Push(transform, VEHICLE_TELEPORT_DISTANCE))
        return false;
    m_idleSnapshot = snapshot;
    m_bHasIdleSnapshot = true;
    m_bHasDriverSnapshot = false;
    m_bHasPedDriverSnapshot = false;
    m_vecLogicalPosition = snapshot.pos;
    m_nSpawnColor1 = snapshot.color1;
    m_nSpawnColor2 = snapshot.color2;
    m_nPaintJob = snapshot.paintjob;
    m_lastAuxState = snapshot.auxState;
    return true;
}

bool CNetworkVehicle::CacheDriverSnapshot(const Packets::Vehicles::VehicleDriverUpdate& snapshot)
{
    uint8_t area = m_nLogicalArea;
    if (CNetworkPlayer* player = CNetworkPlayerManager::GetPlayer(snapshot.playerid))
        area = player->m_nLogicalArea;
    const CNetworkTransformSnapshot transform = MakeVehicleTransform(snapshot.serverTime, snapshot.pos,
        snapshot.velocity, snapshot.turnSpeed, snapshot.roll, snapshot.rot,
        eNetworkTransformSource::VEHICLE_PLAYER_DRIVER,
        static_cast<uint32_t>(std::max(snapshot.playerid.value, 0)), area);
    if (!m_transformInterpolator.Push(transform, VEHICLE_TELEPORT_DISTANCE))
        return false;
    m_playerDriverSnapshot = snapshot;
    m_bHasDriverSnapshot = true;
    m_bHasPedDriverSnapshot = false;
    m_vecLogicalPosition = snapshot.pos;
    m_nSpawnColor1 = snapshot.color1;
    m_nSpawnColor2 = snapshot.color2;
    m_nPaintJob = snapshot.paintjob;
    m_lastAuxState = snapshot.auxState;
    if (CNetworkPlayer* player = CNetworkPlayerManager::GetPlayer(snapshot.playerid))
        m_nLogicalArea = player->m_nLogicalArea;
    return true;
}

bool CNetworkVehicle::CachePedDriverSnapshot(const Packets::Peds::PedDriverUpdate& snapshot)
{
    uint8_t area = m_nLogicalArea;
    if (CNetworkPed* ped = CNetworkPedManager::GetPed(snapshot.pedid))
        area = ped->m_nLogicalArea;
    const CNetworkTransformSnapshot transform = MakeVehicleTransform(snapshot.serverTime, snapshot.pos,
        snapshot.velocity, snapshot.turnSpeed, snapshot.roll, snapshot.rot,
        eNetworkTransformSource::VEHICLE_PED_DRIVER,
        static_cast<uint32_t>(std::max(snapshot.pedid, 0)), area);
    if (!m_transformInterpolator.Push(transform, VEHICLE_TELEPORT_DISTANCE))
        return false;
    m_pedDriverSnapshot = snapshot;
    m_bHasPedDriverSnapshot = true;
    m_bHasDriverSnapshot = false;
    m_bHasIdleSnapshot = false;
    m_vecLogicalPosition = snapshot.pos;
    m_nSpawnColor1 = snapshot.color1;
    m_nSpawnColor2 = snapshot.color2;
    m_nPaintJob = snapshot.paintjob;
    m_nLogicalArea = area;
    return true;
}

void CNetworkVehicle::CacheDamageState(const CDamageManager& damage)
{
    m_pendingDamageState = damage;
    m_bHasPendingDamageState = true;
    m_bPendingDamageApplied = false;
}

void CNetworkVehicle::CacheComponentAdd(int modelId)
{
    for (uint8_t index = 0; index < m_nPendingComponentCount; ++index)
    {
        if (m_pendingComponents[index] == modelId)
            return;
    }
    if (m_nPendingComponentCount < MAX_PENDING_COMPONENTS)
    {
        m_pendingComponents[m_nPendingComponentCount] = modelId;
        m_pendingComponentApplied[m_nPendingComponentCount] = false;
        ++m_nPendingComponentCount;
    }
}

void CNetworkVehicle::CacheComponentRemove(int modelId)
{
    for (uint8_t index = 0; index < m_nPendingComponentCount; ++index)
    {
        if (m_pendingComponents[index] != modelId)
            continue;
        for (uint8_t next = index + 1; next < m_nPendingComponentCount; ++next)
        {
            m_pendingComponents[next - 1] = m_pendingComponents[next];
            m_pendingComponentApplied[next - 1] = m_pendingComponentApplied[next];
        }
        --m_nPendingComponentCount;
        m_pendingComponents[m_nPendingComponentCount] = -1;
        m_pendingComponentApplied[m_nPendingComponentCount] = false;
        break;
    }
    if (m_pVehicle && m_pVehicle->IsVTableValid())
        m_pVehicle->RemoveVehicleUpgrade(modelId);
}

CVector CNetworkVehicle::GetLogicalPosition() const
{
    return m_vecLogicalPosition;
}

void CNetworkVehicle::ApplyCachedPresentation()
{
    if (!m_pVehicle || !m_pVehicle->IsVTableValid() || !m_pVehicle->m_matrix)
        return;

    if (m_bHasDriverSnapshot)
    {
        m_pVehicle->m_matrix->pos = m_playerDriverSnapshot.pos;
        m_pVehicle->m_matrix->right = m_playerDriverSnapshot.roll;
        m_pVehicle->m_matrix->up = m_playerDriverSnapshot.rot;
        m_pVehicle->m_matrix->Reorthogonalise();
        m_pVehicle->m_vecMoveSpeed = m_playerDriverSnapshot.velocity;
        m_pVehicle->m_vecTurnSpeed = m_playerDriverSnapshot.turnSpeed;
        m_pVehicle->m_nPrimaryColor = m_playerDriverSnapshot.color1;
        m_pVehicle->m_nSecondaryColor = m_playerDriverSnapshot.color2;
        m_pVehicle->m_fHealth = m_playerDriverSnapshot.health;
        m_pVehicle->m_eDoorLock = m_playerDriverSnapshot.locked;
        if (m_pVehicle->GetRemapIndex() != m_playerDriverSnapshot.paintjob)
            m_pVehicle->SetRemap(m_playerDriverSnapshot.paintjob);
    }
    else if (m_bHasPedDriverSnapshot)
    {
        m_pVehicle->m_matrix->pos = m_pedDriverSnapshot.pos;
        m_pVehicle->m_matrix->right = m_pedDriverSnapshot.roll;
        m_pVehicle->m_matrix->up = m_pedDriverSnapshot.rot;
        m_pVehicle->m_matrix->Reorthogonalise();
        m_pVehicle->m_vecMoveSpeed = m_pedDriverSnapshot.velocity;
        m_pVehicle->m_vecTurnSpeed = m_pedDriverSnapshot.turnSpeed;
        m_pVehicle->m_nPrimaryColor = m_pedDriverSnapshot.color1;
        m_pVehicle->m_nSecondaryColor = m_pedDriverSnapshot.color2;
        m_pVehicle->m_fHealth = m_pedDriverSnapshot.health;
        m_pVehicle->m_eDoorLock = m_pedDriverSnapshot.locked;
        if (m_pVehicle->GetRemapIndex() != m_pedDriverSnapshot.paintjob)
            m_pVehicle->SetRemap(m_pedDriverSnapshot.paintjob);
    }
    else if (m_bHasIdleSnapshot)
    {
        m_pVehicle->m_matrix->pos = m_idleSnapshot.pos;
        m_pVehicle->m_matrix->right = m_idleSnapshot.roll;
        m_pVehicle->m_matrix->up = m_idleSnapshot.rot;
        m_pVehicle->m_matrix->Reorthogonalise();
        m_pVehicle->m_vecMoveSpeed = m_idleSnapshot.velocity;
        m_pVehicle->m_vecTurnSpeed = m_idleSnapshot.turnSpeed;
        m_pVehicle->m_nPrimaryColor = m_idleSnapshot.color1;
        m_pVehicle->m_nSecondaryColor = m_idleSnapshot.color2;
        m_pVehicle->m_fHealth = m_idleSnapshot.health;
        m_pVehicle->m_eDoorLock = m_idleSnapshot.locked;
        if (m_pVehicle->GetRemapIndex() != m_idleSnapshot.paintjob)
            m_pVehicle->SetRemap(m_idleSnapshot.paintjob);
    }

    m_pVehicle->m_nAreaCode = m_nLogicalArea;

    if (m_pVehicle->m_nVehicleType == VEHICLE_AUTOMOBILE)
    {
        auto* automobile = static_cast<CAutomobile*>(m_pVehicle);
        if (m_bHasDriverSnapshot)
            automobile->m_wMiscComponentAngle = m_playerDriverSnapshot.miscComponentAngle;
        else if (m_bHasPedDriverSnapshot)
            automobile->m_wMiscComponentAngle = m_pedDriverSnapshot.miscComponentAngle;
    }
    if (m_pVehicle->m_nVehicleType == VEHICLE_BIKE)
    {
        auto* bike = static_cast<CBike*>(m_pVehicle);
        if (m_bHasDriverSnapshot)
            bike->m_rideAnimData.m_fDesiredLeanAngle = m_playerDriverSnapshot.bikeLean;
        else if (m_bHasPedDriverSnapshot)
            bike->m_rideAnimData.m_fDesiredLeanAngle = m_pedDriverSnapshot.bikeLean;
    }
    if (m_pVehicle->m_nVehicleSubType == VEHICLE_PLANE)
    {
        auto* plane = static_cast<CPlane*>(m_pVehicle);
        if (m_bHasDriverSnapshot)
            plane->m_fLandingGearStatus = m_playerDriverSnapshot.planeGearState;
        else if (m_bHasPedDriverSnapshot)
            plane->m_fLandingGearStatus = m_pedDriverSnapshot.planeGearState;
        else if (m_bHasIdleSnapshot)
            plane->m_fLandingGearStatus = m_idleSnapshot.planeGearState;
    }
    ProcessPendingPresentation();
}

void CNetworkVehicle::ProcessPendingPresentation()
{
    if (!m_pVehicle || !m_pVehicle->IsVTableValid())
        return;
    ProcessTransformInterpolation();
    ApplyAuxState(m_lastAuxState);
    if (m_bHasPendingDamageState && !m_bPendingDamageApplied && m_pVehicle->m_nVehicleType == VEHICLE_AUTOMOBILE)
    {
        auto* automobile = static_cast<CAutomobile*>(m_pVehicle);
        automobile->m_damageManager = m_pendingDamageState;
        automobile->SetupDamageAfterLoad();
        m_oldDamageState = m_pendingDamageState;
        m_bPendingDamageApplied = true;
    }

    // Vehicle upgrades are independently streamed. Request one missing dependency per frame and never block the
    // packet loop; the presentation remains valid without the optional component until it is ready.
    for (uint8_t index = 0; index < m_nPendingComponentCount; ++index)
    {
        if (m_pendingComponentApplied[index])
            continue;
        const int component = m_pendingComponents[index];
        if (!CStreaming::HasVehicleUpgradeLoaded(component))
        {
            CStreaming::RequestModel(component, MISSION_REQUIRED | PRIORITY_REQUEST);
            CStreaming::RequestVehicleUpgrade(component, MISSION_REQUIRED | PRIORITY_REQUEST);
            break;
        }
        m_pVehicle->AddVehicleUpgrade(component);
        m_pendingComponentApplied[index] = true;
        break;
    }
}

void CNetworkVehicle::ProcessTransformInterpolation()
{
    if (!m_pVehicle || !m_pVehicle->IsVTableValid() || !m_pVehicle->m_matrix || m_bSyncing)
        return;

    uint32_t sourceRtt = 0;
    if (m_bHasDriverSnapshot)
    {
        if (CNetworkPlayer* player = CNetworkPlayerManager::GetPlayer(m_playerDriverSnapshot.playerid))
            sourceRtt = player->m_nRTT;
    }

    CNetworkTransformSample sample{};
    if (!m_transformInterpolator.Sample(g_serverTime, sourceRtt, sample))
        return;
    m_pVehicle->m_matrix->pos = sample.position;
    if (sample.hasVehicleOrientation)
    {
        m_pVehicle->m_matrix->right = sample.right;
        m_pVehicle->m_matrix->up = sample.forward;
        m_pVehicle->m_matrix->at = sample.up;
    }
    m_pVehicle->m_vecMoveSpeed = sample.velocity;
    m_pVehicle->m_vecTurnSpeed = sample.turnSpeed;
}

bool CNetworkVehicle::ResetTransformInterpolation(server_time_t boundaryTime)
{
    if (boundaryTime == 0)
    {
        m_transformInterpolator.Reset();
        return true;
    }
    return m_transformInterpolator.ResetAt(boundaryTime);
}

bool CNetworkVehicle::HasDriver()
{
    if (m_pVehicle == nullptr)
        return false;

    return m_pVehicle->m_pDriver != nullptr;
}

CNetworkVehicle* CNetworkVehicle::CreateHosted(CVehicle* vehicle)
{
    vehicle->m_nTimeTillWeNeedThisCar += 5000;

    CNetworkVehicle* networkVehicle = new CNetworkVehicle();
    networkVehicle->m_pVehicle = vehicle;
    networkVehicle->m_nVehicleId = -1;
    networkVehicle->m_bSyncing = true;
    networkVehicle->m_nModelId = vehicle->m_nModelIndex;
    networkVehicle->m_nPaintJob = (char)vehicle->GetRemapIndex();
    networkVehicle->m_nTempId = CNetworkVehicleManager::AddToTempList(networkVehicle);
    networkVehicle->m_nCreatedBy = vehicle->m_nCreatedBy;

    Packets::Vehicles::VehicleSpawn vehicleSpawnPacket{};
    vehicleSpawnPacket.vehicleid = 0;
    vehicleSpawnPacket.tempid = networkVehicle->m_nTempId;
    vehicleSpawnPacket.modelid = vehicle->m_nModelIndex;
    vehicleSpawnPacket.pos = vehicle->m_matrix->pos;
    vehicleSpawnPacket.rot = vehicle->GetHeading();
    vehicleSpawnPacket.color1 = vehicle->m_nPrimaryColor;
    vehicleSpawnPacket.color2 = vehicle->m_nSecondaryColor;
    vehicleSpawnPacket.createdBy = (eVehicleCreatedBy)vehicle->m_nCreatedBy;
    GetPacketFactory().Send(vehicleSpawnPacket);

    return networkVehicle;
}

void CNetworkVehicle::ApplyAuxState(const Packets::Vehicles::VehicleAuxState& state)
{
    if (!m_pVehicle || !m_pVehicle->IsVTableValid())
        return;

    if (state.hydraulicsActive && Packets::Vehicles::IsHydraulicSyncModel(static_cast<uint16_t>(m_nModelId)) &&
        m_pVehicle->m_nVehicleType == VEHICLE_AUTOMOBILE && m_pVehicle->m_nHandlingFlags.bHydraulicInst)
    {
        auto* automobile = static_cast<CAutomobile*>(m_pVehicle);
        automobile->m_wMiscComponentAngle = static_cast<short>(
            std::min<uint16_t>(state.hydraulicControlAngle, Packets::Vehicles::VEHICLE_HYDRAULIC_ANGLE_MAX));
        for (int wheel = 0; wheel < 4; ++wheel)
        {
            const float compression = std::clamp(state.hydraulicSuspension[wheel], 0.0f, 1.0f);
            automobile->wheelsDistancesToGround1[wheel] = compression;
            automobile->wheelsDistancesToGround2[wheel] = compression;
        }
    }

    if (state.trailerId == Packets::Vehicles::VEHICLE_TRAILER_NONE)
    {
        if (m_pVehicle->m_pTrailer && m_pVehicle->m_pTrailer->IsVTableValid())
        {
            m_pVehicle->m_pTrailer->BreakTowLink();
        }
        m_nAppliedTrailerId = Packets::Vehicles::VEHICLE_TRAILER_NONE;
        m_nPendingTrailerId = Packets::Vehicles::VEHICLE_TRAILER_NONE;
        m_nPendingTrailerSince = 0;
    }
    else if (state.trailerId != m_nAppliedTrailerId || m_pVehicle->m_pTrailer == nullptr)
    {
        if (m_nPendingTrailerId != state.trailerId)
        {
            m_nPendingTrailerId = state.trailerId;
            m_nPendingTrailerSince = GetTickCount();
        }
        ResolvePendingTrailer();
    }

    CPlayerPed* localPlayer = FindPlayerPed(0);
    const bool localPassenger = localPlayer && localPlayer->m_nPedFlags.bInVehicle &&
        localPlayer->m_pVehicle == m_pVehicle && m_pVehicle->m_pDriver != localPlayer;
    if (localPassenger && AudioEngine.IsVehicleRadioActive())
    {
        if (!state.radioActive)
        {
            if (AERadioTrackManager.m_TempSettings.m_nCurrentRadioStation != Packets::Vehicles::VEHICLE_RADIO_OFF)
            {
                AudioEngine.RetuneRadio(static_cast<char>(Packets::Vehicles::VEHICLE_RADIO_OFF));
            }
        }
        else if (state.radioStation >= 0 && state.radioStation < Packets::Vehicles::VEHICLE_RADIO_OFF)
        {
            const int currentStation = static_cast<int>(AERadioTrackManager.m_TempSettings.m_nCurrentRadioStation);
            if (currentStation != state.radioStation)
            {
                AudioEngine.RetuneRadio(static_cast<char>(state.radioStation));
            }
            else if (state.radioStation != RADIO_USER_TRACKS_ID &&
                state.radioTrackId != Packets::Vehicles::VEHICLE_RADIO_TRACK_NONE)
            {
                const int localTrackId = AEAudioHardware.GetActiveTrackID();
                const int localPlayTime = GetRadioTrackPlayTime();
                const uint32_t now = GetTickCount();
                if ((localTrackId != state.radioTrackId ||
                        std::abs(localPlayTime - state.radioPlayTimeMs) > RADIO_DRIFT_TOLERANCE_MS) &&
                    now - m_nLastRadioCorrectionAt >= RADIO_CORRECTION_INTERVAL_MS)
                {
                    ForceRadioTrack(state.radioStation, state.radioTrackId,
                        std::clamp(state.radioPlayTimeMs, 0, Packets::Vehicles::VEHICLE_RADIO_PLAY_TIME_MAX_MS));
                    m_nLastRadioCorrectionAt = now;
                }
            }
        }
    }

    m_lastAuxState = state;
}

void CNetworkVehicle::ResolvePendingTrailer()
{
    if (m_nPendingTrailerId == Packets::Vehicles::VEHICLE_TRAILER_NONE || !m_pVehicle ||
        !m_pVehicle->IsVTableValid())
    {
        return;
    }

    const uint32_t now = GetTickCount();
    if (now - m_nPendingTrailerSince >= TRAILER_STREAM_TIMEOUT_MS)
    {
        m_nPendingTrailerId = Packets::Vehicles::VEHICLE_TRAILER_NONE;
        m_nPendingTrailerSince = 0;
        return;
    }

    if (!Packets::Vehicles::CanTowTrailerModel(static_cast<uint16_t>(m_nModelId)) ||
        m_nPendingTrailerId == m_nVehicleId)
    {
        m_nPendingTrailerId = Packets::Vehicles::VEHICLE_TRAILER_NONE;
        m_nPendingTrailerSince = 0;
        return;
    }

    CNetworkVehicle* trailer = CNetworkVehicleManager::GetVehicle(m_nPendingTrailerId);
    if (!trailer || !trailer->m_pVehicle || !trailer->m_pVehicle->IsVTableValid())
        return;

    if (trailer == this || trailer->m_pVehicle->m_nVehicleType != VEHICLE_TRAILER)
    {
        m_nPendingTrailerId = Packets::Vehicles::VEHICLE_TRAILER_NONE;
        m_nPendingTrailerSince = 0;
        return;
    }
    if (!Packets::Vehicles::CanAttachTrailerModel(static_cast<uint16_t>(m_nModelId),
            static_cast<uint16_t>(trailer->m_nModelId)))
    {
        m_nPendingTrailerId = Packets::Vehicles::VEHICLE_TRAILER_NONE;
        m_nPendingTrailerSince = 0;
        return;
    }

    CNetworkVehicle* cursor = trailer;
    for (size_t depth = 0; cursor && depth < CNetworkVehicleManager::m_pVehicles.size(); ++depth)
    {
        if (cursor == this)
        {
            m_nPendingTrailerId = Packets::Vehicles::VEHICLE_TRAILER_NONE;
            m_nPendingTrailerSince = 0;
            return;
        }
        const int nextId = cursor->m_nAppliedTrailerId != Packets::Vehicles::VEHICLE_TRAILER_NONE
            ? cursor->m_nAppliedTrailerId
            : cursor->m_nPendingTrailerId;
        cursor = nextId == Packets::Vehicles::VEHICLE_TRAILER_NONE
            ? nullptr
            : CNetworkVehicleManager::GetVehicle(nextId);
    }

    if (m_pVehicle->m_pTrailer && m_pVehicle->m_pTrailer != trailer->m_pVehicle &&
        m_pVehicle->m_pTrailer->IsVTableValid())
    {
        m_pVehicle->m_pTrailer->BreakTowLink();
    }
    if (trailer->m_pVehicle->m_pTractor && trailer->m_pVehicle->m_pTractor != m_pVehicle)
    {
        trailer->m_pVehicle->BreakTowLink();
    }

    if (m_pVehicle->m_pTrailer == trailer->m_pVehicle || trailer->m_pVehicle->SetTowLink(m_pVehicle, false))
    {
        m_nAppliedTrailerId = trailer->m_nVehicleId;
        m_nPendingTrailerId = Packets::Vehicles::VEHICLE_TRAILER_NONE;
        m_nPendingTrailerSince = 0;
    }
}

void CNetworkVehicle::DetachTrailerLinks()
{
    if (!m_pVehicle || !m_pVehicle->IsVTableValid())
        return;

    if (m_pVehicle->m_pTrailer && m_pVehicle->m_pTrailer->IsVTableValid())
    {
        m_pVehicle->m_pTrailer->BreakTowLink();
    }
    if (m_pVehicle->m_pTractor && m_pVehicle->m_pTractor->IsVTableValid())
    {
        m_pVehicle->BreakTowLink();
    }

    m_nAppliedTrailerId = Packets::Vehicles::VEHICLE_TRAILER_NONE;
    m_nPendingTrailerId = Packets::Vehicles::VEHICLE_TRAILER_NONE;
    m_nPendingTrailerSince = 0;
}
