#include "network/packet_types.h"
#include "stdafx.h"
#include "network/packet_handler.h"
#include "network/packets/vehicles.h"
#include <chrono>

namespace
{
constexpr uint64_t TRAILER_PENDING_TIMEOUT_MS = 5000;
constexpr uint64_t VEHICLE_TRANSITION_RATE_WINDOW_MS = 1000;
constexpr uint8_t MAX_VEHICLE_TRANSITIONS_PER_WINDOW = 8;

uint64_t GetSteadyTimeMs()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

bool IsExactOccupant(const CNetworkVehicle* vehicle, const CNetworkPlayer* player, int seat)
{
    return vehicle && player && seat >= 0 && seat < static_cast<int>(ARRAY_SIZE(vehicle->m_pPlayers)) &&
        vehicle->m_pPlayers[seat] == player && player->m_nVehicleId == vehicle->m_nVehicleId &&
        player->m_nSeatId == seat;
}

bool CanApplyVehicleTransition(CNetworkPlayer* player)
{
    const uint64_t now = GetSteadyTimeMs();
    if (now - player->m_nVehicleTransitionWindowStartMs >= VEHICLE_TRANSITION_RATE_WINDOW_MS)
    {
        player->m_nVehicleTransitionWindowStartMs = now;
        player->m_nVehicleTransitionCount = 0;
    }
    if (player->m_nVehicleTransitionCount >= MAX_VEHICLE_TRANSITIONS_PER_WINDOW)
    {
        return false;
    }
    ++player->m_nVehicleTransitionCount;
    return true;
}

bool RemovePlayerFromAllVehicleSeats(CNetworkPlayer* player)
{
    bool removed = player->m_nVehicleId >= 0 || player->m_nSeatId >= 0;
    for (CNetworkVehicle* vehicle : CNetworkVehicleManager::m_pVehicles)
    {
        if (!vehicle)
            continue;
        for (CNetworkPlayer*& occupant : vehicle->m_pPlayers)
        {
            if (occupant == player)
            {
                occupant = nullptr;
                removed = true;
            }
        }
    }
    player->m_nVehicleId = -1;
    player->m_nSeatId = -1;
    return removed;
}

bool HasPlayerVehicleSeatReference(const CNetworkPlayer* player)
{
    for (const CNetworkVehicle* vehicle : CNetworkVehicleManager::m_pVehicles)
    {
        if (!vehicle)
            continue;
        for (const CNetworkPlayer* occupant : vehicle->m_pPlayers)
        {
            if (occupant == player)
                return true;
        }
    }
    return false;
}

bool WouldCreateTrailerCycle(const CNetworkVehicle* tractor, int trailerId)
{
    int cursorId = trailerId;
    for (int depth = 0; depth < Config::MAX_SERVER_VEHICLES; ++depth)
    {
        if (cursorId == Packets::Vehicles::VEHICLE_TRAILER_NONE)
            return false;
        if (cursorId == tractor->m_nVehicleId)
            return true;

        const CNetworkVehicle* cursor = CNetworkVehicleManager::GetVehicle(cursorId);
        if (!cursor)
            return false;
        cursorId = cursor->m_auxState.trailerId;
    }
    return true;
}

bool TrailerAlreadyOwnedByAnotherTractor(const CNetworkVehicle* tractor, int trailerId)
{
    for (const auto* vehicle : CNetworkVehicleManager::m_pVehicles)
    {
        if (vehicle && vehicle != tractor && vehicle->m_auxState.trailerId == trailerId)
            return true;
    }
    return false;
}

void ClearPendingTrailer(CNetworkVehicle* vehicle)
{
    vehicle->m_nPendingTrailerId = Packets::Vehicles::VEHICLE_TRAILER_NONE;
    vehicle->m_nPendingTrailerSinceMs = 0;
}

void SanitizeTrailerState(CNetworkVehicle* vehicle, CNetworkPlayer* sender,
    Packets::Vehicles::VehicleAuxState& state)
{
    const int requestedTrailerId = state.trailerId;
    if (requestedTrailerId == Packets::Vehicles::VEHICLE_TRAILER_NONE)
    {
        vehicle->m_auxState.trailerId = Packets::Vehicles::VEHICLE_TRAILER_NONE;
        ClearPendingTrailer(vehicle);
        return;
    }

    if (!Packets::Vehicles::CanTowTrailerModel(vehicle->m_nModelId) ||
        requestedTrailerId == vehicle->m_nVehicleId ||
        TrailerAlreadyOwnedByAnotherTractor(vehicle, requestedTrailerId) ||
        WouldCreateTrailerCycle(vehicle, requestedTrailerId))
    {
        ClearPendingTrailer(vehicle);
        state.trailerId = vehicle->m_auxState.trailerId;
        return;
    }

    CNetworkVehicle* trailer = CNetworkVehicleManager::GetVehicle(requestedTrailerId);
    if (!trailer)
    {
        const uint64_t now = GetSteadyTimeMs();
        if (vehicle->m_nPendingTrailerId != requestedTrailerId)
        {
            vehicle->m_auxState.trailerId = Packets::Vehicles::VEHICLE_TRAILER_NONE;
            vehicle->m_nPendingTrailerId = requestedTrailerId;
            vehicle->m_nPendingTrailerSinceMs = now;
        }
        else if (now - vehicle->m_nPendingTrailerSinceMs >= TRAILER_PENDING_TIMEOUT_MS)
        {
            vehicle->m_nPendingTrailerId = Packets::Vehicles::VEHICLE_TRAILER_NONE;
            vehicle->m_nPendingTrailerSinceMs = 0;
        }
        state.trailerId = vehicle->m_auxState.trailerId;
        return;
    }

    if (!Packets::Vehicles::IsTrailerModel(trailer->m_nModelId))
    {
        ClearPendingTrailer(vehicle);
        state.trailerId = vehicle->m_auxState.trailerId;
        return;
    }
    if (!Packets::Vehicles::CanAttachTrailerModel(vehicle->m_nModelId, trailer->m_nModelId))
    {
        ClearPendingTrailer(vehicle);
        state.trailerId = vehicle->m_auxState.trailerId;
        return;
    }
    if (trailer->m_pSyncer != sender)
    {
        logger::warn("%s tried to attach trailer %d owned by another syncer", sender->GetName().c_str(),
            requestedTrailerId);
        ClearPendingTrailer(vehicle);
        state.trailerId = vehicle->m_auxState.trailerId;
        return;
    }

    vehicle->m_auxState.trailerId = requestedTrailerId;
    ClearPendingTrailer(vehicle);
}

void SetRadioOff(Packets::Vehicles::VehicleAuxState& state)
{
    state.radioActive = false;
    state.radioStation = Packets::Vehicles::VEHICLE_RADIO_OFF;
    state.radioTrackId = Packets::Vehicles::VEHICLE_RADIO_TRACK_NONE;
    state.radioPlayTimeMs = 0;
}

void SanitizeAuxState(CNetworkVehicle* vehicle, CNetworkPlayer* sender,
    Packets::Vehicles::VehicleAuxState& state, bool allowAuxAuthority, bool allowRadioAuthority)
{
    if (!allowAuxAuthority)
    {
        state = vehicle->m_auxState;
    }
    else if (!Packets::Vehicles::IsHydraulicSyncModel(vehicle->m_nModelId))
    {
        state.hydraulicsActive = false;
        state.hydraulicControlAngle = 0;
        for (float& wheel : state.hydraulicSuspension)
            wheel = 1.0f;
    }
    else if (state.hydraulicsActive)
    {
        state.hydraulicControlAngle = std::min<uint16_t>(
            state.hydraulicControlAngle, Packets::Vehicles::VEHICLE_HYDRAULIC_ANGLE_MAX);
        for (float& wheel : state.hydraulicSuspension)
            wheel = std::isfinite(wheel) ? std::clamp(wheel, 0.0f, 1.0f) : 1.0f;
    }

    if (allowAuxAuthority)
    {
        SanitizeTrailerState(vehicle, sender, state);
    }

    if (!allowRadioAuthority)
    {
        SetRadioOff(state);
    }
    else if (state.radioActive)
    {
        state.radioStation = std::clamp(state.radioStation, 0, Packets::Vehicles::VEHICLE_RADIO_OFF - 1);
        state.radioTrackId = std::clamp(state.radioTrackId, Packets::Vehicles::VEHICLE_RADIO_TRACK_NONE,
            Packets::Vehicles::VEHICLE_RADIO_TRACK_MAX);
        state.radioPlayTimeMs = state.radioTrackId == Packets::Vehicles::VEHICLE_RADIO_TRACK_NONE
            ? 0
            : std::clamp(state.radioPlayTimeMs, 0, Packets::Vehicles::VEHICLE_RADIO_PLAY_TIME_MAX_MS);
    }
    else
    {
        SetRadioOff(state);
    }

    vehicle->m_auxState = state;
}
}

PACKET_HANDLER(
    ePacketType::VEHICLE_SPAWN, Packets::Vehicles::VehicleSpawn* pVehicleSpawn, CNetworkPlayer* pNetworkPlayer)
{
    pVehicleSpawn->vehicleid = CNetworkVehicleManager::GetFreeID();
    GetPacketFactory().SendToAll(*pVehicleSpawn, pNetworkPlayer);

    // send it back to the syncer of the vehicle so that he knows the id
    Packets::Vehicles::VehicleConfirm vehicleConfirmPacket{};
    vehicleConfirmPacket.tempid = pVehicleSpawn->tempid;
    vehicleConfirmPacket.vehicleid = pVehicleSpawn->vehicleid;
    GetPacketFactory().Send(vehicleConfirmPacket, pNetworkPlayer);

    CNetworkVehicle* vehicle = new CNetworkVehicle(
        pVehicleSpawn->vehicleid, pVehicleSpawn->modelid, pVehicleSpawn->pos, pVehicleSpawn->rot.m_angle);

    vehicle->m_pSyncer = pNetworkPlayer;
    vehicle->m_nPrimaryColor = pVehicleSpawn->color1;
    vehicle->m_nSecondaryColor = pVehicleSpawn->color2;
    vehicle->m_nCreatedBy = pVehicleSpawn->createdBy;

    CNetworkVehicleManager::Add(vehicle);
}

PACKET_HANDLER(
    ePacketType::VEHICLE_REMOVE, Packets::Vehicles::VehicleRemove* pVehicleRemove, CNetworkPlayer* pNetworkPlayer)
{
    if (auto vehicle = CNetworkVehicleManager::GetVehicle(pVehicleRemove->vehicleid))
    {
        if (vehicle->m_pSyncer == pNetworkPlayer)
        {
            // Clear canonical seats before the vehicle object disappears. This also prevents passengers
            // from retaining an impossible vehicle ID when the vehicle host disconnects or deletes it.
            for (CNetworkPlayer* occupant : vehicle->m_pPlayers)
            {
                if (!occupant)
                    continue;
                RemovePlayerFromAllVehicleSeats(occupant);
                Packets::Vehicles::VehicleExit exit{};
                exit.playerid = occupant->m_iPlayerId;
                exit.bForce = true;
                GetPacketFactory().SendToAll(exit, occupant);
            }

            GetPacketFactory().SendToAll(*pVehicleRemove, pNetworkPlayer);

            CNetworkVehicleManager::Remove(vehicle);
            delete vehicle;
        }
        else
        {
            logger::warn("%s tried to delete someone else's vehicle", pNetworkPlayer->GetName().c_str());
        }
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_IDLE_UPDATE, Packets::Vehicles::VehicleIdleUpdate* pVehicleIdleUpdate,
    CNetworkPlayer* pNetworkPlayer)
{
    if (auto vehicle = CNetworkVehicleManager::GetVehicle(pVehicleIdleUpdate->vehicleid))
    {
        if (vehicle->m_pSyncer == pNetworkPlayer && vehicle->m_pPlayers[0] == nullptr)
        {
            vehicle->m_bUsedByPed = false;
            vehicle->m_vecPosition = pVehicleIdleUpdate->pos;
            vehicle->m_vecRotation = pVehicleIdleUpdate->rot;
            const bool senderIsPassenger = pNetworkPlayer->m_nVehicleId == vehicle->m_nVehicleId &&
                pNetworkPlayer->m_nSeatId > 0;
            SanitizeAuxState(vehicle, pNetworkPlayer, pVehicleIdleUpdate->auxState, !senderIsPassenger, false);
            GetPacketFactory().SendToAll(*pVehicleIdleUpdate, pNetworkPlayer);
        }
        else
        {
            logger::warn("%s tried to update someone else's vehicle (idle)", pNetworkPlayer->GetName().c_str());
        }
    }
    else
    {
        logger::warn("%s tried to update a vehicle (idle) that didn't exist", pNetworkPlayer->GetName().c_str());
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_DRIVER_UPDATE, Packets::Vehicles::VehicleDriverUpdate* pVehicleDriverUpdate,
    CNetworkPlayer* pNetworkPlayer)
{
    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleDriverUpdate->vehicleid))
    {
        if (!IsExactOccupant(pNetworkVehicle, pNetworkPlayer, 0))
        {
            logger::warn("%s tried to spoof driver authority for vehicle %d", pNetworkPlayer->GetName().c_str(),
                pVehicleDriverUpdate->vehicleid);
            return;
        }

        pNetworkVehicle->m_vecPosition = pVehicleDriverUpdate->pos;
        pNetworkVehicle->m_vecRotation = pVehicleDriverUpdate->rot;
        pNetworkVehicle->m_bUsedByPed = false;
        pNetworkVehicle->ReassignSyncer(pNetworkPlayer);
        SanitizeAuxState(pNetworkVehicle, pNetworkPlayer, pVehicleDriverUpdate->auxState, true, true);

        pVehicleDriverUpdate->playerid = pNetworkPlayer->m_iPlayerId;
        GetPacketFactory().SendToAll(*pVehicleDriverUpdate, pNetworkPlayer);
    }
    else
    {
        logger::warn("%s tried to update a vehicle (driver) that didn't exist", pNetworkPlayer->GetName().c_str());
    }
}

PACKET_HANDLER(
    ePacketType::VEHICLE_ENTER, Packets::Vehicles::VehicleEnter* pVehicleEnter, CNetworkPlayer* pNetworkPlayer)
{
    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleEnter->vehicleid))
    {
        const int seat = pVehicleEnter->bPassenger ? pVehicleEnter->seatid + 1 : 0;
        if (seat < 0 || seat >= static_cast<int>(ARRAY_SIZE(pNetworkVehicle->m_pPlayers)) ||
            (pNetworkVehicle->m_pPlayers[seat] && pNetworkVehicle->m_pPlayers[seat] != pNetworkPlayer))
        {
            logger::warn("%s tried to enter an invalid or occupied vehicle seat", pNetworkPlayer->GetName().c_str());
            return;
        }

        // Never trust the serialized player ID or non-canonical driver/passenger seat fields.
        pVehicleEnter->playerid = pNetworkPlayer->m_iPlayerId;
        pVehicleEnter->bPassenger = seat > 0;
        pVehicleEnter->seatid = seat > 0 ? static_cast<int8_t>(seat - 1) : 0;

        if (IsExactOccupant(pNetworkVehicle, pNetworkPlayer, seat))
        {
            // Bounded client retries are confirmation-only and must not replay remote entry animations.
            return;
        }
        if (!CanApplyVehicleTransition(pNetworkPlayer))
        {
            logger::warn("%s exceeded the vehicle transition rate", pNetworkPlayer->GetName().c_str());
            return;
        }

        if (!pVehicleEnter->bForce)
        {
            // Task constructors only announce a visual attempt. A canceled attempt must not reserve a seat.
            if (pNetworkPlayer->m_nVehicleId >= 0 || pNetworkPlayer->m_nSeatId >= 0)
                return;
            GetPacketFactory().SendToAll(*pVehicleEnter, pNetworkPlayer);
            return;
        }

        RemovePlayerFromAllVehicleSeats(pNetworkPlayer);
        pNetworkVehicle->SetOccupant(static_cast<int8_t>(seat), pNetworkPlayer);
        GetPacketFactory().SendToAll(*pVehicleEnter, pNetworkPlayer);
    }
    else
    {
        logger::warn("%s tried to enter a vehicle that didn't exist", pNetworkPlayer->GetName().c_str());
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_EXIT, Packets::Vehicles::VehicleExit* pVehicleExit, CNetworkPlayer* pNetworkPlayer)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pNetworkPlayer->m_nVehicleId);
    const bool exactOccupancy =
        IsExactOccupant(pNetworkVehicle, pNetworkPlayer, pNetworkPlayer->m_nSeatId);
    pVehicleExit->playerid = pNetworkPlayer->m_iPlayerId;

    if (!pVehicleExit->bForce)
    {
        // Preserve the remote leave animation while keeping the canonical seat until native confirmation.
        if (exactOccupancy && CanApplyVehicleTransition(pNetworkPlayer))
            GetPacketFactory().SendToAll(*pVehicleExit, pNetworkPlayer);
        return;
    }

    if (!exactOccupancy && pNetworkPlayer->m_nVehicleId < 0 && pNetworkPlayer->m_nSeatId < 0 &&
        !HasPlayerVehicleSeatReference(pNetworkPlayer))
        return;  // idempotent confirmation
    if (!CanApplyVehicleTransition(pNetworkPlayer))
        return;

    // Remove every stale reference, even if the vehicle was deleted before this reliable confirmation arrived.
    if (RemovePlayerFromAllVehicleSeats(pNetworkPlayer))
        GetPacketFactory().SendToAll(*pVehicleExit, pNetworkPlayer);
}

PACKET_HANDLER(
    ePacketType::VEHICLE_DAMAGE, Packets::Vehicles::VehicleDamage* pVehicleDamage, CNetworkPlayer* pNetworkPlayer)
{
    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleDamage->vehicleid))
    {
        pNetworkVehicle->m_damageManager = pVehicleDamage->damageManager;

        GetPacketFactory().SendToAll(*pVehicleDamage, pNetworkPlayer);
    }
    else
    {
        logger::warn("%s tried to damage a vehicle that didn't exist", pNetworkPlayer->GetName().c_str());
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_COMPONENT_ADD, Packets::Vehicles::VehicleComponentAdd* pVehicleComponentAdd,
    CNetworkPlayer* pNetworkPlayer)
{
    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleComponentAdd->vehicleid))
    {
        pNetworkVehicle->m_pComponents.push_back(pVehicleComponentAdd->componentid);

        GetPacketFactory().SendToAll(*pVehicleComponentAdd, pNetworkPlayer);
    }
    else
    {
        logger::warn("%s tried to sync a vehicle (comp add) that didn't exist", pNetworkPlayer->GetName().c_str());
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_COMPONENT_REMOVE,
    Packets::Vehicles::VehicleComponentRemove* pVehicleComponentRemove, CNetworkPlayer* pNetworkPlayer)
{
    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleComponentRemove->vehicleid))
    {
        auto it = std::find(pNetworkVehicle->m_pComponents.begin(), pNetworkVehicle->m_pComponents.end(),
            pVehicleComponentRemove->componentid);
        if (it != pNetworkVehicle->m_pComponents.end())
        {
            pNetworkVehicle->m_pComponents.erase(it);
        }

        GetPacketFactory().SendToAll(*pVehicleComponentRemove, pNetworkPlayer);
    }
    else
    {
        logger::warn("%s tried to sync a vehicle (comp remove) that didn't exist", pNetworkPlayer->GetName().c_str());
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_PASSENGER_UPDATE,
    Packets::Vehicles::VehiclePassengerUpdate* pVehiclePassengerUpdate, CNetworkPlayer* pNetworkPlayer)
{
    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehiclePassengerUpdate->vehicleid))
    {
        const int serverSeat = pVehiclePassengerUpdate->seatid + 1;
        if (!IsExactOccupant(pNetworkVehicle, pNetworkPlayer, serverSeat))
        {
            logger::warn("%s tried to spoof passenger state for vehicle %d", pNetworkPlayer->GetName().c_str(),
                pVehiclePassengerUpdate->vehicleid);
            return;
        }

        pVehiclePassengerUpdate->playerid = pNetworkPlayer->m_iPlayerId;
        GetPacketFactory().SendToAll(*pVehiclePassengerUpdate, pNetworkPlayer);

        if (pNetworkVehicle->m_nCreatedBy == MISSION_VEHICLE)
        {
            return;
        }

        if (!pNetworkVehicle->m_pPlayers[0] && !pNetworkVehicle->m_bUsedByPed)  // if no driver
        {
            for (uint8_t i = 1; i < 8; i++)
            {
                if (auto pNewSyncer = pNetworkVehicle->m_pPlayers[i])
                {
                    pNetworkVehicle->ReassignSyncer(pNewSyncer);
                    break;
                }
            }
        }
    }
    else
    {
        logger::warn("%s tried to sync a vehicle (passenger) that didn't exist", pNetworkPlayer->GetName().c_str());
    }
}

PACKET_HANDLER(ePacketType::SET_VEHICLE_CREATED_BY, Packets::Vehicles::SetVehicleCreatedBy* pSetVehicleCreatedBy,
    CNetworkPlayer* pNetworkPlayer)
{
    if (pNetworkPlayer->m_bIsHost)
    {
        if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pSetVehicleCreatedBy->vehicleid))
        {
            pNetworkVehicle->m_nCreatedBy = pSetVehicleCreatedBy->createdBy;
            GetPacketFactory().SendToAll(*pSetVehicleCreatedBy, pNetworkPlayer);
        }
    }
}
