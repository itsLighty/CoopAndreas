#pragma once

#include "config.h"

#include <array>
#include <cstdint>
#include <vector>

class CNetworkPlayer;
class CNetworkPed;

class CNetworkPedManager
{
public:
    static std::vector<CNetworkPed*> m_pPeds;
    static std::array<uint16_t, Config::MAX_SERVER_PEDS> m_anGroupRevisions;
    static void Add(CNetworkPed* ped);
    static void Remove(CNetworkPed* ped);
    static CNetworkPed* GetPed(int pedid);
    static int GetFreeId();
    static void ReleaseVehicleUsage(CNetworkPed* ped);
    static uint16_t AdvanceGroupRevision(CNetworkPed* ped);
    static void ClearGroupMembership(CNetworkPed* ped);
    static void RemoveAllHostedAndNotify(CNetworkPlayer* player);
};
