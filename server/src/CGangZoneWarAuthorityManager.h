#pragma once

#include "network/packets/world.h"

class CNetworkPlayer;

class CGangZoneWarAuthorityManager
{
public:
    static bool HandleZoneState(CNetworkPlayer* pNetworkPlayer, const Packets::World::GangZoneState& state);
    static bool HandleWarState(CNetworkPlayer* pNetworkPlayer, const Packets::World::GangWarState& state);
    static void SendSnapshot(CNetworkPlayer* pNetworkPlayer);
    static void ResetForAuthorityChange();

private:
    static Packets::World::GangZoneState m_ZoneState;
    static Packets::World::GangWarState m_WarState;
    static bool m_bHasZoneState;
    static bool m_bHasWarState;

    static bool IsCurrentHost(const CNetworkPlayer* pNetworkPlayer);
};
