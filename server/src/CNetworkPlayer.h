#pragma once

#include "enet/enet.h"

#include "CVector.h"
#include "CNetwork.h"
#include <string>
#include <network/packets/players.h>
#include <CPedClothesDesc.h>
class CNetworkPed;
class CNetworkPlayer
{
public:
    CNetworkPlayer(ENetPeer* peer, int playerid);

    std::string GetName();

    ENetPeer* m_pPeer;
    int m_iPlayerId;
    char m_Name[32 + 1] = {0};
    bool m_bIsHost = false;
    int8_t m_nSeatId = -1;
    int m_nVehicleId = -1;
    bool m_bCorrectVersion = false;
    float m_afStats[Packets::Players::PLAYER_SKILL_STATS_COUNT]{};
    Packets::Players::PlayerGameplayState m_gameplayState{};
    Packets::Players::PlayerPlaceWaypoint m_waypointState{};
    CPedClothesDesc m_pPedClothesDesc{};

    struct
    {
        uint8_t bStatsModified : 1;
        uint8_t bGameplayStateModified : 1;
        uint8_t bClothesModified : 1;
    } m_ucSyncFlags{};
    std::vector<CNetworkPed*> m_vPedClaims;

    void RemoveFromVehicle();

    ~CNetworkPlayer();
};
