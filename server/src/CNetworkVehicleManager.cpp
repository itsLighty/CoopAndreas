#include "stdafx.h"
#include <network/packets/vehicles.h>

std::vector<CNetworkVehicle*> CNetworkVehicleManager::m_pVehicles;

void CNetworkVehicleManager::Add(CNetworkVehicle* vehicle)
{
    m_pVehicles.push_back(vehicle);
}

void CNetworkVehicleManager::Remove(CNetworkVehicle* vehicle)
{
    if (vehicle)
    {
        ClearVehicleRelations(vehicle->m_nVehicleId);
    }

    auto it = std::find(m_pVehicles.begin(), m_pVehicles.end(), vehicle);
    if (it != m_pVehicles.end())
    {
        m_pVehicles.erase(it);
    }
}

void CNetworkVehicleManager::ClearVehicleRelations(int vehicleid)
{
    for (auto* vehicle : m_pVehicles)
    {
        if (!vehicle)
            continue;

        if (vehicle->m_auxState.trailerId == vehicleid)
        {
            vehicle->m_auxState.trailerId = Packets::Vehicles::VEHICLE_TRAILER_NONE;
        }
        if (vehicle->m_nPendingTrailerId == vehicleid)
        {
            vehicle->m_nPendingTrailerId = Packets::Vehicles::VEHICLE_TRAILER_NONE;
            vehicle->m_nPendingTrailerSinceMs = 0;
        }
    }
}

CNetworkVehicle* CNetworkVehicleManager::GetVehicle(int vehicleid)
{
    for (int i = 0; i != m_pVehicles.size(); i++)
    {
        if (m_pVehicles[i]->m_nVehicleId == vehicleid)
        {
            return m_pVehicles[i];
        }
    }
    return nullptr;
}

int CNetworkVehicleManager::GetFreeID()
{
    for (int i = 0; i < Config::MAX_SERVER_VEHICLES; i++)
    {
        if (CNetworkVehicleManager::GetVehicle(i) == nullptr)
            return i;
    }

    return -1;
}

void CNetworkVehicleManager::RemoveAllHostedAndNotify(CNetworkPlayer* player)
{
    for (auto it = CNetworkVehicleManager::m_pVehicles.begin(); it != CNetworkVehicleManager::m_pVehicles.end();)
    {
        if ((*it)->m_pSyncer == player)
        {
            Packets::Vehicles::VehicleRemove packet{};
            packet.vehicleid = (*it)->m_nVehicleId;
            GetPacketFactory().SendToAll(packet, player);

            ClearVehicleRelations((*it)->m_nVehicleId);
            delete *it;
            it = CNetworkVehicleManager::m_pVehicles.erase(it);
        }
        else
        {
            ++it;
        }
    }
}
