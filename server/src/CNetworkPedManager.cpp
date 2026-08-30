#include "stdafx.h"
#include <network/packets/peds.h>

std::vector<CNetworkPed*> CNetworkPedManager::m_pPeds;

void CNetworkPedManager::Add(CNetworkPed* ped)
{
    m_pPeds.push_back(ped);
}

void CNetworkPedManager::Remove(CNetworkPed* ped)
{
    ReleaseVehicleUsage(ped);
    auto it = std::find(m_pPeds.begin(), m_pPeds.end(), ped);
    // std::find()
    if (it != m_pPeds.end())
    {
        m_pPeds.erase(it);
    }
}

CNetworkPed* CNetworkPedManager::GetPed(int pedid)
{
    for (int i = 0; i != m_pPeds.size(); i++)
    {
        if (m_pPeds[i]->m_nPedId == pedid)
        {
            return m_pPeds[i];
        }
    }
    return nullptr;
}

int CNetworkPedManager::GetFreeId()
{
    for (int i = 0; i < Config::MAX_SERVER_PEDS; i++)
    {
        if (CNetworkPedManager::GetPed(i) == nullptr)
            return i;
    }

    return -1;
}

void CNetworkPedManager::RemoveAllHostedAndNotify(CNetworkPlayer* player)
{
    Packets::Peds::PedRemove packet{};

    for (auto it = CNetworkPedManager::m_pPeds.begin(); it != CNetworkPedManager::m_pPeds.end();)
    {
        if ((*it)->m_pSyncer == player)
        {
            CNetworkPed* ped = *it;
            CNetworkPlayer* replacement = nullptr;
            for (auto* candidate : CNetworkPlayerManager::m_pPlayers)
            {
                if (candidate != player &&
                    std::find(candidate->m_vPedClaims.begin(), candidate->m_vPedClaims.end(), ped) !=
                        candidate->m_vPedClaims.end())
                {
                    replacement = candidate;
                    break;
                }
            }

            if (replacement)
            {
                ped->m_pSyncer = replacement;
                Packets::Peds::AssignPedSyncer assign{};
                assign.pedid = ped->m_nPedId;
                GetPacketFactory().Send(assign, replacement);

                auto claim = std::find(replacement->m_vPedClaims.begin(), replacement->m_vPedClaims.end(), ped);
                if (claim != replacement->m_vPedClaims.end())
                    replacement->m_vPedClaims.erase(claim);

                if (auto* vehicle = CNetworkVehicleManager::GetVehicle(ped->m_nVehicleId))
                    vehicle->ReassignSyncer(replacement);
                ++it;
                continue;
            }

            for (auto* candidate : CNetworkPlayerManager::m_pPlayers)
            {
                auto claim = std::find(candidate->m_vPedClaims.begin(), candidate->m_vPedClaims.end(), ped);
                if (claim != candidate->m_vPedClaims.end())
                    candidate->m_vPedClaims.erase(claim);
            }

            packet.pedid = ped->m_nPedId;
            GetPacketFactory().SendToAll(packet, player);
            ReleaseVehicleUsage(ped);
            delete ped;
            it = CNetworkPedManager::m_pPeds.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void CNetworkPedManager::ReleaseVehicleUsage(CNetworkPed* ped)
{
    if (!ped || ped->m_nVehicleId < 0)
        return;

    if (auto* vehicle = CNetworkVehicleManager::GetVehicle(ped->m_nVehicleId))
        vehicle->m_bUsedByPed = false;
    ped->m_nVehicleId = -1;
}
