#include "stdafx.h"
#include "CLocalVehicleOccupancySync.h"

namespace
{
constexpr uint32_t OCCUPANCY_CONFIRM_RETRY_MS = 500;
constexpr uint8_t MAX_OCCUPANCY_CONFIRMATIONS = 3;
constexpr int MAX_NETWORK_PASSENGER_SEATS = 7;
}

CLocalVehicleOccupancySync::Occupancy CLocalVehicleOccupancySync::CaptureNativeOccupancy()
{
    CPlayerPed* player = FindPlayerPed(0);
    if (!player || !player->IsAlive() || !player->m_nPedFlags.bInVehicle || !player->m_pVehicle ||
        CPools::ms_pVehiclePool == nullptr || !CPools::ms_pVehiclePool->IsObjectValid(player->m_pVehicle))
    {
        return {};
    }

    CVehicle* vehicle = player->m_pVehicle;
    CNetworkVehicle* networkVehicle = CNetworkVehicleManager::GetVehicle(vehicle);
    if (!networkVehicle)
    {
        return {};
    }

    if (vehicle->m_pDriver == player)
    {
        return {networkVehicle->m_nVehicleId, 0};
    }

    const int passengerCount = std::clamp<int>(vehicle->m_nMaxPassengers, 0, MAX_NETWORK_PASSENGER_SEATS);
    for (int passengerSeat = 0; passengerSeat < passengerCount; ++passengerSeat)
    {
        if (vehicle->m_apPassengers[passengerSeat] == player)
        {
            return {networkVehicle->m_nVehicleId, static_cast<int8_t>(passengerSeat + 1)};
        }
    }

    // GTA has not committed this transition to a native seat yet. Constructor/task intent must not
    // become authoritative while the ped is between the door animation and the actual seat array.
    return {};
}

void CLocalVehicleOccupancySync::SendEnterConfirmation(const Occupancy& occupancy)
{
    Packets::Vehicles::VehicleEnter packet{};
    packet.vehicleid = occupancy.vehicleId;
    packet.bForce = true;
    packet.bPassenger = occupancy.serverSeat > 0;
    packet.seatid = packet.bPassenger ? occupancy.serverSeat - 1 : 0;
    GetPacketFactory().Send(packet);
}

void CLocalVehicleOccupancySync::SendExitConfirmation()
{
    Packets::Vehicles::VehicleExit packet{};
    packet.bForce = true;
    GetPacketFactory().Send(packet);
}

void CLocalVehicleOccupancySync::Process()
{
    if (!CNetwork::m_bAuthenticated)
    {
        Reset();
        return;
    }

    const Occupancy nativeOccupancy = CaptureNativeOccupancy();
    const uint32_t now = GetTickCount();
    if (nativeOccupancy != ms_confirmedOccupancy)
    {
        if (ms_confirmedOccupancy.IsValid())
        {
            SendExitConfirmation();
        }

        ms_confirmedOccupancy = nativeOccupancy;
        ms_nConfirmationCount = 0;
        ms_nLastConfirmationAt = now;
        if (ms_confirmedOccupancy.IsValid())
        {
            SendEnterConfirmation(ms_confirmedOccupancy);
            ms_nConfirmationCount = 1;
        }
        return;
    }

    // EVENT and SYNC use different ENet channels, so the first driver/passenger snapshot can race the
    // reliable confirmation. A small bounded retry window repairs that race without continuous spam.
    if (ms_confirmedOccupancy.IsValid() && ms_nConfirmationCount < MAX_OCCUPANCY_CONFIRMATIONS &&
        now - ms_nLastConfirmationAt >= OCCUPANCY_CONFIRM_RETRY_MS)
    {
        SendEnterConfirmation(ms_confirmedOccupancy);
        ++ms_nConfirmationCount;
        ms_nLastConfirmationAt = now;
    }
}

void CLocalVehicleOccupancySync::Reset()
{
    ms_confirmedOccupancy = {};
    ms_nLastConfirmationAt = 0;
    ms_nConfirmationCount = 0;
}
