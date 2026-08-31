#include "stdafx.h"
#include "CNetworkEntityBlip.h"
#include <unordered_map>

namespace
{
using EntityBlipState = Packets::Blips::UpdateEntityBlip;

std::unordered_map<int, EntityBlipState> g_desiredPedBlips;
std::unordered_map<int, EntityBlipState> g_desiredVehicleBlips;

void ApplyBlipSettings(int handle, const EntityBlipState& state)
{
    if (handle < 0)
        return;
    CRadar::ChangeBlipScale(handle, state.scale);
    CRadar::ChangeBlipColour(handle, state.color);
    CRadar::ChangeBlipDisplay(handle, state.display);
    CRadar::SetBlipFriendly(handle, state.isFriendly);
}

void ApplyDesiredPedBlip(int pedId)
{
    auto desired = g_desiredPedBlips.find(pedId);
    CNetworkPed* networkPed = CNetworkPedManager::GetPed(pedId);
    if (desired == g_desiredPedBlips.end() || !networkPed || !networkPed->m_pPed ||
        !networkPed->m_pPed->IsVTableValid())
    {
        return;
    }
    if (networkPed->m_nBlipHandle == -1)
    {
        networkPed->m_nBlipHandle = CRadar::SetEntityBlip(
            BLIP_CHAR, CPools::GetPedRef(networkPed->m_pPed), 0, desired->second.display);
    }
    ApplyBlipSettings(networkPed->m_nBlipHandle, desired->second);
}

void ApplyDesiredVehicleBlip(int vehicleId)
{
    auto desired = g_desiredVehicleBlips.find(vehicleId);
    CNetworkVehicle* networkVehicle = CNetworkVehicleManager::GetVehicle(vehicleId);
    if (desired == g_desiredVehicleBlips.end() || !networkVehicle || !networkVehicle->m_pVehicle ||
        !networkVehicle->m_pVehicle->IsVTableValid())
    {
        return;
    }
    if (networkVehicle->m_nBlipHandle == -1)
    {
        networkVehicle->m_nBlipHandle = CRadar::SetEntityBlip(
            BLIP_CAR, CPools::GetVehicleRef(networkVehicle->m_pVehicle), 0, desired->second.display);
    }
    ApplyBlipSettings(networkVehicle->m_nBlipHandle, desired->second);
}
}

void CNetworkEntityBlip::UpdateEntityBlip(Packets::Blips::UpdateEntityBlip* packet)
{
    switch (packet->entity.entityType)
    {
        case eNetworkEntityType::NETWORK_ENTITY_TYPE_PED:
            g_desiredPedBlips[packet->entity.entityId] = *packet;
            ApplyDesiredPedBlip(packet->entity.entityId);
            break;
        case eNetworkEntityType::NETWORK_ENTITY_TYPE_VEHICLE:
            g_desiredVehicleBlips[packet->entity.entityId] = *packet;
            ApplyDesiredVehicleBlip(packet->entity.entityId);
            break;
        default:
            break;
    }
}

void CNetworkEntityBlip::RemoveEntityBlip(Packets::Blips::RemoveEntityBlip* packet)
{
    switch (packet->entity.entityType)
    {
        case eNetworkEntityType::NETWORK_ENTITY_TYPE_PED:
            g_desiredPedBlips.erase(packet->entity.entityId);
            if (auto networkPed = CNetworkPedManager::GetPed(packet->entity.entityId))
            {
                if (networkPed->m_nBlipHandle >= 0)
                    CRadar::ClearBlip(networkPed->m_nBlipHandle);
                networkPed->m_nBlipHandle = -1;
            }
            break;
        case eNetworkEntityType::NETWORK_ENTITY_TYPE_VEHICLE:
            g_desiredVehicleBlips.erase(packet->entity.entityId);
            if (auto networkVehicle = CNetworkVehicleManager::GetVehicle(packet->entity.entityId))
            {
                if (networkVehicle->m_nBlipHandle >= 0)
                    CRadar::ClearBlip(networkVehicle->m_nBlipHandle);
                networkVehicle->m_nBlipHandle = -1;
            }
            break;
        default:
            break;
    }
}

void CNetworkEntityBlip::ClearEntityBlips()
{
    g_desiredPedBlips.clear();
    g_desiredVehicleBlips.clear();

    for (auto pNetworkPed : CNetworkPedManager::m_pPeds)
    {
        if (!pNetworkPed)
            continue;
        if (pNetworkPed->m_nBlipHandle >= 0)
            CRadar::ClearBlip(pNetworkPed->m_nBlipHandle);
        pNetworkPed->m_nBlipHandle = -1;
    }

    for (auto pNetworkVehicle : CNetworkVehicleManager::m_pVehicles)
    {
        if (!pNetworkVehicle)
            continue;
        if (pNetworkVehicle->m_nBlipHandle >= 0)
            CRadar::ClearBlip(pNetworkVehicle->m_nBlipHandle);
        pNetworkVehicle->m_nBlipHandle = -1;
    }
}

bool CNetworkEntityBlip::HasDesiredPedBlip(int pedId)
{
    return g_desiredPedBlips.find(pedId) != g_desiredPedBlips.end();
}

bool CNetworkEntityBlip::HasDesiredVehicleBlip(int vehicleId)
{
    return g_desiredVehicleBlips.find(vehicleId) != g_desiredVehicleBlips.end();
}

void CNetworkEntityBlip::Update()
{
    for (auto pNetworkPed : CNetworkPedManager::m_pPeds)
    {
        if (!pNetworkPed)
            continue;
        ApplyDesiredPedBlip(pNetworkPed->m_nPedId);
        if (pNetworkPed->m_nBlipHandle == -1 || !pNetworkPed->m_pPed)
        {
            continue;
        }

        if (pNetworkPed->m_pPed->m_fHealth <= 0.0f)
        {
            CRadar::ClearBlipForEntity(eBlipType::BLIP_CHAR, CPools::GetPedRef(pNetworkPed->m_pPed));
            pNetworkPed->m_nBlipHandle = -1;
            g_desiredPedBlips.erase(pNetworkPed->m_nPedId);
        }
    }

    for (auto pNetworkVehicle : CNetworkVehicleManager::m_pVehicles)
    {
        if (!pNetworkVehicle)
            continue;
        ApplyDesiredVehicleBlip(pNetworkVehicle->m_nVehicleId);
        if (pNetworkVehicle->m_nBlipHandle == -1 || !pNetworkVehicle->m_pVehicle)
        {
            continue;
        }

        if (pNetworkVehicle->m_pVehicle->m_fHealth <= 0.0f)
        {
            CRadar::ClearBlipForEntity(eBlipType::BLIP_CAR, CPools::GetVehicleRef(pNetworkVehicle->m_pVehicle));
            pNetworkVehicle->m_nBlipHandle = -1;
            g_desiredVehicleBlips.erase(pNetworkVehicle->m_nVehicleId);
        }
    }
}
