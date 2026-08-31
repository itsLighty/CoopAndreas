#pragma once

class CNetworkPlayer;

namespace Packets::Cheats
{
class CheatRequest;
}

class CCheatAuthorityManager
{
public:
    static void HandleRequest(CNetworkPlayer* player, const Packets::Cheats::CheatRequest& request);
    static void HandlePlayerDisconnected(CNetworkPlayer* player);
    static void HandleAuthorityChange(CNetworkPlayer* newAuthority);
    static void SendSnapshot(CNetworkPlayer* player);
};
