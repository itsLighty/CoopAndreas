#include "stdafx.h"
#include "CNetworkVehicle.h"
#include <CAEAudioHardware.h>
#include <CAERadioTrackManager.h>

namespace
{
constexpr uint32_t TRAILER_STREAM_TIMEOUT_MS = 5000;
constexpr uint32_t RADIO_CORRECTION_INTERVAL_MS = 500;
constexpr int RADIO_DRIFT_TOLERANCE_MS = 2000;
constexpr int RADIO_USER_TRACKS_ID = 12;

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
        CVehicle* oldVehicle = vehicle->m_pVehicle;
        CNetworkVehicleManager::Remove(vehicle);
        if (oldVehicle && oldVehicle->IsVTableValid())
        {
            CWorld::Remove(oldVehicle);
            CWorld::RemoveReferencesToDeletedObject(oldVehicle);
            delete oldVehicle;
        }
        vehicle->m_pVehicle = nullptr;
        vehicle->m_bSyncing = false;
        delete vehicle;
    }

    m_nVehicleId = vehicleid;
    m_bSyncing = false;
    m_nTempId = 255;
    m_nModelId = modelid;
    m_nCreatedBy = createdBy;

    if (!CreateVehicle(vehicleid, modelid, pos, rotation, color1, color2))
        return;

}

bool CNetworkVehicle::CreateVehicle(int vehicleid, int modelid, CVector pos, float rotation, unsigned char color1, unsigned char color2)
{
    unsigned char oldFlags = CStreaming::ms_aInfoForModel[modelid].m_nFlags;
    CStreaming::RequestModel(modelid, GAME_REQUIRED);
    CStreaming::LoadAllRequestedModels(false);

    if (!(oldFlags & GAME_REQUIRED))
    {
        CStreaming::SetModelIsDeletable(modelid);
        CStreaming::SetModelTxdIsDeletable(modelid);
    }

    switch (((CVehicleModelInfo*)CModelInfo::ms_modelInfoPtrs[modelid])->m_nVehicleType)
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
    CWorld::Add(m_pVehicle);

    return true;
}

CNetworkVehicle::~CNetworkVehicle()
{
    DetachTrailerLinks();

    if (m_bSyncing)
    {
        Packets::Vehicles::VehicleRemove vehicleRemovePacket{};
        vehicleRemovePacket.vehicleid = m_nVehicleId;
        GetPacketFactory().Send(vehicleRemovePacket);
    }
    else
    {
        if (m_pVehicle && m_pVehicle->IsVTableValid())
        {
            if (m_nBlipHandle != -1)
            {
                CRadar::ClearBlipForEntity(eBlipType::BLIP_CAR, CPools::GetVehicleRef(m_pVehicle));
            }
            CWorld::Remove(m_pVehicle);
            CWorld::RemoveReferencesToDeletedObject(m_pVehicle); // ?
            delete m_pVehicle;
        }
    }
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
