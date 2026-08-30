#pragma once

#include <array>
#include "network/packets/scripts.h"

class CNetworkPlayer;

class CCutsceneVoteManager
{
public:
    static bool HandleStartRequest(CNetworkPlayer* pNetworkPlayer,
        const Packets::Scripts::CutsceneStartRequest& request);
    static bool HandleVoteRequest(CNetworkPlayer* pNetworkPlayer,
        const Packets::Scripts::CutsceneVoteRequest& request);
    static bool HandleEndRequest(CNetworkPlayer* pNetworkPlayer,
        const Packets::Scripts::CutsceneEndRequest& request);
    static void SendSnapshot(CNetworkPlayer* pNetworkPlayer);
    static void ResetForMissionSession();

private:
    static Packets::Scripts::CutsceneVoteState m_State;
    static std::array<bool, Config::MAX_SERVER_PLAYERS> m_abEligiblePlayers;
    static std::array<bool, Config::MAX_SERVER_PLAYERS> m_abPlayerVotes;
    static uint32_t m_nNextCutsceneEpoch;
    static bool m_bSkipBroadcast;

    static bool MatchesCurrentCutscene(uint64_t sessionId, uint32_t missionEpoch,
        uint32_t cutsceneEpoch);
    static bool IsEligibleConnectedPlayer(CNetworkPlayer* pNetworkPlayer);
    static uint32_t NextCutsceneEpoch();
    static void BroadcastState();
    static void ClearState();
};
