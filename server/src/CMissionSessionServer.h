#pragma once

#include "network/packets/scripts.h"

class CNetworkPlayer;

class CMissionSessionServer
{
public:
    static const Packets::Scripts::MissionSessionState& GetState();
    static bool HandleRequest(CNetworkPlayer* pNetworkPlayer,
        const Packets::Scripts::MissionSessionRequest& request);
    static bool HandleLegacyMissionFlag(CNetworkPlayer* pNetworkPlayer, bool bOnMission);
    static void HandlePlayerDisconnected(CNetworkPlayer* pNetworkPlayer);
    static void SendSnapshot(CNetworkPlayer* pNetworkPlayer);

    static bool IsAuthoritativeHost(const CNetworkPlayer* pNetworkPlayer);
    static bool IsSessionParticipant(const CNetworkPlayer* pNetworkPlayer);
    static bool IsGameplayParticipant(const CNetworkPlayer* pNetworkPlayer);

private:
    static Packets::Scripts::MissionSessionState m_State;
    static uint64_t m_nNextSessionId;
    static uint32_t m_nNextEpoch;

    static bool BeginSession(CNetworkPlayer* pNetworkPlayer,
        const Packets::Scripts::MissionSessionRequest& request);
    static bool UpdateStage(CNetworkPlayer* pNetworkPlayer,
        const Packets::Scripts::MissionSessionRequest& request);
    static bool EndSession(CNetworkPlayer* pNetworkPlayer,
        const Packets::Scripts::MissionSessionRequest& request);
    static bool AbortSession(Packets::Scripts::eMissionSessionResult result);
    static bool RequestMatchesCurrentSession(const Packets::Scripts::MissionSessionRequest& request);
    static uint64_t NextSessionId();
    static uint32_t NextEpoch();
    static void SetAcknowledgement(CNetworkPlayer* pNetworkPlayer, uint32_t requestId, bool bAccepted);
    static void ClearAcknowledgement();
    static void BroadcastState(CNetworkPlayer* pNetworkPlayerToIgnore = nullptr);
};
