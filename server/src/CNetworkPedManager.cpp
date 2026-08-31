#include "stdafx.h"
#include <network/packets/peds.h>

std::vector<CNetworkPed*> CNetworkPedManager::m_pPeds;
std::array<uint16_t, Config::MAX_SERVER_PEDS> CNetworkPedManager::m_anGroupRevisions{};

void CNetworkPedManager::Add(CNetworkPed* ped)
{
    if (ped && ped->m_nPedId >= 0 && ped->m_nPedId < Config::MAX_SERVER_PEDS)
        ped->m_nGroupRevision = m_anGroupRevisions[ped->m_nPedId];
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
                ClearGroupMembership(ped);
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

void CNetworkPedManager::ClearGroupMembership(CNetworkPed* ped)
{
    if (!ped || !ped->m_bGroupSnapshotInitialized || !ped->m_groupSnapshot.hasGroup)
        return;

    ped->m_groupSnapshot = {};
    ped->m_groupSnapshot.revision = AdvanceGroupRevision(ped);

    // This is an existing reliable ped event in a group-only mode. It avoids forging an on-foot transform when
    // ownership changes while the follower is driving or riding in a vehicle.
    Packets::Peds::AssignPedSyncer clear{};
    clear.pedid = ped->m_nPedId;
    clear.toggleOwnership = false;
    clear.group = ped->m_groupSnapshot;
    GetPacketFactory().SendToAll(clear);
}

uint16_t CNetworkPedManager::AdvanceGroupRevision(CNetworkPed* ped)
{
    if (!ped || ped->m_nPedId < 0 || ped->m_nPedId >= Config::MAX_SERVER_PEDS)
        return 0;

    uint16_t& revision = m_anGroupRevisions[ped->m_nPedId];
    ++revision;
    if (revision == 0)
        ++revision;
    ped->m_nGroupRevision = revision;
    return revision;
}
