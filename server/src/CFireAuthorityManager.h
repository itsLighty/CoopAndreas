#pragma once

#include <cstdint>

class CNetworkPlayer;
class CVector;

namespace Packets::Fires
{
class FireStateIntent;
class FireExtinguishRequest;
}

class CFireAuthorityManager
{
public:
    static void HandleStateIntent(CNetworkPlayer* player, const Packets::Fires::FireStateIntent& intent);
    static void HandleExtinguishRequest(
        CNetworkPlayer* player, Packets::Fires::FireExtinguishRequest request);
    static void HandlePlayerDisconnected(CNetworkPlayer* player);
    static void ObservePlayerMovement(CNetworkPlayer* player, const CVector& position, bool alive);
    static void ObservePlayerArea(CNetworkPlayer* player, uint8_t area);
    static void MarkPlayerUnavailable(CNetworkPlayer* player);
    static void HandleAuthorityChange(CNetworkPlayer* newAuthority);
    static void SendSnapshot(CNetworkPlayer* player);
    static void Update();
};
