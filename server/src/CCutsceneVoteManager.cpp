#include "stdafx.h"
#include "CCutsceneVoteManager.h"
#include "CMissionSessionServer.h"

using namespace Packets::Scripts;

CutsceneVoteState CCutsceneVoteManager::m_State{};
std::array<bool, Config::MAX_SERVER_PLAYERS> CCutsceneVoteManager::m_abEligiblePlayers{};
std::array<bool, Config::MAX_SERVER_PLAYERS> CCutsceneVoteManager::m_abPlayerVotes{};
uint32_t CCutsceneVoteManager::m_nNextCutsceneEpoch = 0;
bool CCutsceneVoteManager::m_bSkipBroadcast = false;

bool CCutsceneVoteManager::HandleStartRequest(
    CNetworkPlayer* pNetworkPlayer, const CutsceneStartRequest& request)
{
    const MissionSessionState& mission = CMissionSessionServer::GetState();
    if (!CMissionSessionServer::IsAuthoritativeHost(pNetworkPlayer) || !mission.IsActive() ||
        !CMissionSessionServer::IsGameplayParticipant(pNetworkPlayer) || request.sessionId != mission.sessionId ||
        request.missionEpoch != mission.epoch || request.requestId == 0)
    {
        logger::warn("Rejected an unauthorized or stale cutscene-start request");
        return false;
    }

    // Reliable retries acknowledge the same authoritative epoch instead of creating a second cutscene identity.
    if (m_State.cutsceneEpoch != 0 && m_State.sessionId == request.sessionId &&
        m_State.missionEpoch == request.missionEpoch && m_State.startRequestId == request.requestId)
    {
        SendSnapshot(pNetworkPlayer);
        return true;
    }

    ClearState();
    m_State.sessionId = mission.sessionId;
    m_State.missionEpoch = mission.epoch;
    m_State.cutsceneEpoch = NextCutsceneEpoch();
    m_State.startRequestId = request.requestId;
    m_State.hostId = mission.hostId;
    m_State.eligibleCount = mission.gameplayParticipantCount;
    m_State.requiredVotes = static_cast<uint8_t>(m_State.eligibleCount / 2 + 1);
    m_State.lifecycle = eCutsceneVoteLifecycle::ACTIVE;
    m_State.currArea = request.currArea;
    snprintf(m_State.name, sizeof(m_State.name), "%s", request.name);

    for (size_t rosterIndex = 0; rosterIndex < mission.gameplayParticipantCount; ++rosterIndex)
    {
        const uint8_t playerId = mission.participantIds[rosterIndex];
        if (playerId >= Config::MAX_SERVER_PLAYERS)
        {
            ClearState();
            return false;
        }
        m_abEligiblePlayers[playerId] = true;
    }

    if (!m_State.HasValidVoteState())
    {
        ClearState();
        return false;
    }

    BroadcastState();
    return true;
}

bool CCutsceneVoteManager::HandleVoteRequest(
    CNetworkPlayer* pNetworkPlayer, const CutsceneVoteRequest& request)
{
    if (m_State.lifecycle != eCutsceneVoteLifecycle::ACTIVE ||
        !MatchesCurrentCutscene(request.sessionId, request.missionEpoch, request.cutsceneEpoch) ||
        !IsEligibleConnectedPlayer(pNetworkPlayer))
    {
        logger::warn("Rejected an unauthorized, stale, or replayed cutscene vote");
        return false;
    }

    const int playerId = pNetworkPlayer->m_iPlayerId;
    if (m_abPlayerVotes[playerId])
    {
        logger::warn("Rejected a duplicate cutscene vote from player %d", playerId);
        return false;
    }

    m_abPlayerVotes[playerId] = true;
    ++m_State.voteCount;
    if (m_State.voteCount >= m_State.requiredVotes)
    {
        m_State.lifecycle = eCutsceneVoteLifecycle::SKIPPED;
        if (m_bSkipBroadcast)
        {
            return false;
        }
        m_bSkipBroadcast = true;
    }

    BroadcastState();
    return true;
}

bool CCutsceneVoteManager::HandleEndRequest(
    CNetworkPlayer* pNetworkPlayer, const CutsceneEndRequest& request)
{
    if (!CMissionSessionServer::IsAuthoritativeHost(pNetworkPlayer) ||
        !MatchesCurrentCutscene(request.sessionId, request.missionEpoch, request.cutsceneEpoch))
    {
        logger::warn("Rejected an unauthorized or stale cutscene-end request");
        return false;
    }

    m_State.lifecycle = eCutsceneVoteLifecycle::ENDED;
    BroadcastState();
    ClearState();
    return true;
}

void CCutsceneVoteManager::SendSnapshot(CNetworkPlayer* pNetworkPlayer)
{
    if (m_State.cutsceneEpoch == 0 || !IsEligibleConnectedPlayer(pNetworkPlayer))
    {
        return;
    }
    GetPacketFactory().Send(m_State, pNetworkPlayer);
}

void CCutsceneVoteManager::ResetForMissionSession()
{
    ClearState();
}

bool CCutsceneVoteManager::MatchesCurrentCutscene(
    uint64_t sessionId, uint32_t missionEpoch, uint32_t cutsceneEpoch)
{
    if (m_State.cutsceneEpoch == 0 || sessionId != m_State.sessionId ||
        missionEpoch != m_State.missionEpoch || cutsceneEpoch != m_State.cutsceneEpoch)
    {
        return false;
    }

    const MissionSessionState& mission = CMissionSessionServer::GetState();
    return mission.IsActive() && mission.sessionId == sessionId;
}

bool CCutsceneVoteManager::IsEligibleConnectedPlayer(CNetworkPlayer* pNetworkPlayer)
{
    if (pNetworkPlayer == nullptr || pNetworkPlayer->m_iPlayerId < 0 ||
        pNetworkPlayer->m_iPlayerId >= Config::MAX_SERVER_PLAYERS ||
        CNetworkPlayerManager::GetPlayer(pNetworkPlayer->m_iPlayerId) != pNetworkPlayer ||
        !m_abEligiblePlayers[pNetworkPlayer->m_iPlayerId])
    {
        return false;
    }

    // Packet handlers run only for authenticated player objects. Requiring the still-frozen gameplay roster here
    // additionally excludes spectators and ordinary replacement IDs; only credential-backed reclaim can reuse a
    // reserved participant ID while the mission remains active.
    return CMissionSessionServer::IsGameplayParticipant(pNetworkPlayer);
}

uint32_t CCutsceneVoteManager::NextCutsceneEpoch()
{
    do
    {
        ++m_nNextCutsceneEpoch;
    } while (m_nNextCutsceneEpoch == 0);
    return m_nNextCutsceneEpoch;
}

void CCutsceneVoteManager::BroadcastState()
{
    for (CNetworkPlayer* pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (IsEligibleConnectedPlayer(pNetworkPlayer))
        {
            GetPacketFactory().Send(m_State, pNetworkPlayer);
        }
    }
}

void CCutsceneVoteManager::ClearState()
{
    m_State = CutsceneVoteState{};
    m_abEligiblePlayers.fill(false);
    m_abPlayerVotes.fill(false);
    m_bSkipBroadcast = false;
}
