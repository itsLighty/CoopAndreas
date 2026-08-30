#include "stdafx.h"
#include "CMissionSessionServer.h"
#include "CCutsceneVoteManager.h"

using namespace Packets::Scripts;

MissionSessionState CMissionSessionServer::m_State{};
uint64_t CMissionSessionServer::m_nNextSessionId = 0;
uint32_t CMissionSessionServer::m_nNextEpoch = 0;

const MissionSessionState& CMissionSessionServer::GetState()
{
    return m_State;
}

bool CMissionSessionServer::IsAuthoritativeHost(const CNetworkPlayer* pNetworkPlayer)
{
    return pNetworkPlayer != nullptr && pNetworkPlayer->m_bIsHost &&
           CNetworkPlayerManager::GetHost() == pNetworkPlayer;
}

bool CMissionSessionServer::IsSessionParticipant(const CNetworkPlayer* pNetworkPlayer)
{
    return pNetworkPlayer != nullptr && m_State.IsActive() &&
           m_State.ContainsParticipant(pNetworkPlayer->m_iPlayerId);
}

bool CMissionSessionServer::IsGameplayParticipant(const CNetworkPlayer* pNetworkPlayer)
{
    return pNetworkPlayer != nullptr && m_State.IsActive() &&
           m_State.ContainsGameplayParticipant(pNetworkPlayer->m_iPlayerId);
}

bool CMissionSessionServer::HandleRequest(CNetworkPlayer* pNetworkPlayer, const MissionSessionRequest& request)
{
    if (!IsAuthoritativeHost(pNetworkPlayer))
    {
        logger::warn("%s tried to change mission-session state without host authority",
            pNetworkPlayer ? pNetworkPlayer->GetName().c_str() : "An unknown player");
        return false;
    }

    bool accepted = false;
    switch (request.action)
    {
        case eMissionSessionRequestAction::LAUNCH:
            accepted = BeginSession(pNetworkPlayer, request);
            break;
        case eMissionSessionRequestAction::UPDATE_STAGE:
            accepted = UpdateStage(pNetworkPlayer, request);
            break;
        case eMissionSessionRequestAction::END:
            accepted = EndSession(pNetworkPlayer, request);
            break;
        case eMissionSessionRequestAction::ABORT:
            if (RequestMatchesCurrentSession(request) && request.stage == m_State.stage &&
                request.result == eMissionSessionResult::ABORTED_BY_HOST)
            {
                accepted = AbortSession(eMissionSessionResult::ABORTED_BY_HOST);
            }
            break;
    }

    SetAcknowledgement(pNetworkPlayer, request.requestId, accepted);
    if (accepted)
    {
        BroadcastState();
    }
    else
    {
        logger::warn("Rejected an invalid or stale mission-session request from %s",
            pNetworkPlayer->GetName().c_str());
        SendSnapshot(pNetworkPlayer);
    }
    return accepted;
}

bool CMissionSessionServer::HandleLegacyMissionFlag(CNetworkPlayer* pNetworkPlayer, bool bOnMission)
{
    MissionSessionRequest request{};
    if (bOnMission)
    {
        request.action = eMissionSessionRequestAction::LAUNCH;
        request.sessionId = m_State.sessionId;
        request.epoch = m_State.epoch;
        request.missionId = MISSION_ID_UNKNOWN;
    }
    else
    {
        request.action = eMissionSessionRequestAction::END;
        request.sessionId = m_State.sessionId;
        request.epoch = m_State.epoch;
        request.missionId = m_State.missionId;
        request.stage = m_State.stage;
        request.result = eMissionSessionResult::COMPLETED;
    }
    return HandleRequest(pNetworkPlayer, request);
}

void CMissionSessionServer::HandlePlayerDisconnected(CNetworkPlayer* pNetworkPlayer)
{
    if (pNetworkPlayer != nullptr && m_State.IsActive() && m_State.hostId == pNetworkPlayer->m_iPlayerId &&
        AbortSession(eMissionSessionResult::HOST_DISCONNECTED))
    {
        ClearAcknowledgement();
        BroadcastState(pNetworkPlayer);
    }
}

void CMissionSessionServer::SendSnapshot(CNetworkPlayer* pNetworkPlayer)
{
    if (pNetworkPlayer == nullptr)
    {
        return;
    }

    m_State.serverTime = 0;
    GetPacketFactory().Send(m_State, pNetworkPlayer);

    // This remains a one-way compatibility projection; it never mutates versioned state on a new client.
    OnMissionFlagSync legacyMissionFlag{};
    legacyMissionFlag.bOnMission = m_State.IsActive();
    GetPacketFactory().Send(legacyMissionFlag, pNetworkPlayer);
}

bool CMissionSessionServer::BeginSession(CNetworkPlayer* pNetworkPlayer, const MissionSessionRequest& request)
{
    if (m_State.IsActive() || request.sessionId != m_State.sessionId || request.epoch != m_State.epoch ||
        request.result != eMissionSessionResult::NONE || request.stage != 0 ||
        !IsMissionIdKnownOrUnknown(request.missionId) || pNetworkPlayer->m_iPlayerId < 0 ||
        pNetworkPlayer->m_iPlayerId >= Config::MAX_SERVER_PLAYERS)
    {
        return false;
    }

    MissionSessionState state{};
    state.sessionId = NextSessionId();
    state.epoch = NextEpoch();
    state.missionId = request.missionId;
    state.hostId = static_cast<uint8_t>(pNetworkPlayer->m_iPlayerId);
    state.lifecycle = eMissionSessionLifecycle::RUNNING;
    state.result = eMissionSessionResult::NONE;
    state.stage = 0;

    bool seenPlayerIds[Config::MAX_SERVER_PLAYERS]{};
    state.participantIds[state.participantCount++] = state.hostId;
    seenPlayerIds[state.hostId] = true;

    // Preserve connection order because the SCM bridge consumes the first three remote player objects.
    for (const CNetworkPlayer* player : CNetworkPlayerManager::m_pPlayers)
    {
        if (player == nullptr || player == pNetworkPlayer)
        {
            continue;
        }
        if (player->m_iPlayerId < 0 || player->m_iPlayerId >= Config::MAX_SERVER_PLAYERS ||
            seenPlayerIds[player->m_iPlayerId] || state.participantCount >= Config::MAX_SERVER_PLAYERS)
        {
            return false;
        }
        state.participantIds[state.participantCount++] = static_cast<uint8_t>(player->m_iPlayerId);
        seenPlayerIds[player->m_iPlayerId] = true;
    }

    state.gameplayParticipantCount = static_cast<uint8_t>(
        std::min(static_cast<size_t>(state.participantCount),
            static_cast<size_t>(MISSION_SCM_GAMEPLAY_PLAYER_CAP)));
    if (!state.HasValidParticipantRoster())
    {
        return false;
    }

    m_State = state;
    CCutsceneVoteManager::ResetForMissionSession();
    return true;
}

bool CMissionSessionServer::UpdateStage(CNetworkPlayer*, const MissionSessionRequest& request)
{
    if (!RequestMatchesCurrentSession(request) || request.result != eMissionSessionResult::NONE)
    {
        return false;
    }

    m_State.stage = request.stage;
    m_State.epoch = NextEpoch();
    return true;
}

bool CMissionSessionServer::EndSession(CNetworkPlayer*, const MissionSessionRequest& request)
{
    if (!RequestMatchesCurrentSession(request) || request.stage != m_State.stage ||
        (request.result != eMissionSessionResult::COMPLETED &&
            request.result != eMissionSessionResult::SUCCEEDED && request.result != eMissionSessionResult::FAILED))
    {
        return false;
    }

    m_State.lifecycle = eMissionSessionLifecycle::ENDED;
    m_State.result = request.result;
    m_State.epoch = NextEpoch();
    CCutsceneVoteManager::ResetForMissionSession();
    return true;
}

bool CMissionSessionServer::AbortSession(eMissionSessionResult result)
{
    if (!m_State.IsActive() ||
        (result != eMissionSessionResult::ABORTED_BY_HOST && result != eMissionSessionResult::HOST_DISCONNECTED))
    {
        return false;
    }

    m_State.lifecycle = eMissionSessionLifecycle::ABORTED;
    m_State.result = result;
    m_State.epoch = NextEpoch();
    CCutsceneVoteManager::ResetForMissionSession();
    return true;
}

bool CMissionSessionServer::RequestMatchesCurrentSession(const MissionSessionRequest& request)
{
    return m_State.IsActive() && request.sessionId == m_State.sessionId && request.epoch == m_State.epoch &&
           request.missionId == m_State.missionId;
}

uint64_t CMissionSessionServer::NextSessionId()
{
    ++m_nNextSessionId;
    if (m_nNextSessionId == 0)
    {
        ++m_nNextSessionId;
    }
    return m_nNextSessionId;
}

uint32_t CMissionSessionServer::NextEpoch()
{
    ++m_nNextEpoch;
    if (m_nNextEpoch == 0)
    {
        ++m_nNextEpoch;
    }
    return m_nNextEpoch;
}

void CMissionSessionServer::SetAcknowledgement(CNetworkPlayer* pNetworkPlayer, uint32_t requestId, bool bAccepted)
{
    m_State.acknowledgedRequestId = requestId;
    m_State.acknowledgedPlayerId = requestId == 0 || pNetworkPlayer == nullptr
                                       ? MISSION_PLAYER_ID_INVALID
                                       : static_cast<uint8_t>(pNetworkPlayer->m_iPlayerId);
    m_State.acknowledgedRequestAccepted = requestId != 0 && pNetworkPlayer != nullptr && bAccepted;
}

void CMissionSessionServer::ClearAcknowledgement()
{
    m_State.acknowledgedRequestId = 0;
    m_State.acknowledgedPlayerId = MISSION_PLAYER_ID_INVALID;
    m_State.acknowledgedRequestAccepted = false;
}

void CMissionSessionServer::BroadcastState(CNetworkPlayer* pNetworkPlayerToIgnore)
{
    m_State.serverTime = 0;
    GetPacketFactory().SendToAll(m_State, pNetworkPlayerToIgnore);

    OnMissionFlagSync legacyMissionFlag{};
    legacyMissionFlag.bOnMission = m_State.IsActive();
    GetPacketFactory().SendToAll(legacyMissionFlag, pNetworkPlayerToIgnore);
}
