#include "stdafx.h"
#include "CCutsceneVoteManager.h"
#include "CMissionSessionClient.h"

using namespace Packets::Scripts;

CutsceneVoteState CCutsceneVoteManager::m_State{};
uint32_t CCutsceneVoteManager::m_nNextStartRequestId = 0;
uint32_t CCutsceneVoteManager::m_nPendingStartRequestId = 0;
uint64_t CCutsceneVoteManager::m_nPendingSessionId = 0;
uint32_t CCutsceneVoteManager::m_nPendingMissionEpoch = 0;
uint64_t CCutsceneVoteManager::m_nLastSessionId = 0;
uint32_t CCutsceneVoteManager::m_nLastCutsceneEpoch = 0;
bool CCutsceneVoteManager::m_bLocalVoteSent = false;
bool CCutsceneVoteManager::m_bSkipApplied = false;
bool CCutsceneVoteManager::m_bEndRequestSent = false;
bool CCutsceneVoteManager::m_bEndWhenAcknowledged = false;
bool CCutsceneVoteManager::m_bObservedCutsceneRunning = false;
bool CCutsceneVoteManager::m_bDisabledCutscenePending = false;

void CCutsceneVoteManager::BeginDisabledCutscene()
{
    m_bDisabledCutscenePending = true;
    RestoreGameplayPresentation();
}

void CCutsceneVoteManager::EndDisabledCutscene()
{
    SkipCurrentCutsceneImmediately();
    m_bDisabledCutscenePending = false;
    RestoreGameplayPresentation();
}

void CCutsceneVoteManager::NotifySynchronizedCutsceneStarted()
{
    if (!CNetwork::m_bAuthenticated || !CLocalPlayer::m_bIsHost)
    {
        return;
    }

    const MissionSessionState& mission = CMissionSessionClient::GetState();
    const int localPlayerId = CNetworkPlayerManager::m_nMyId;
    if (!mission.IsActive() || !mission.ContainsGameplayParticipant(localPlayerId) ||
        mission.hostId != localPlayerId)
    {
        return;
    }

    RememberCurrentEpoch();
    ClearActiveState();
    m_nPendingStartRequestId = NextStartRequestId();
    m_nPendingSessionId = mission.sessionId;
    m_nPendingMissionEpoch = mission.epoch;
    m_bObservedCutsceneRunning = CCutsceneMgr::ms_running;

    CutsceneStartRequest request{};
    request.sessionId = mission.sessionId;
    request.missionEpoch = mission.epoch;
    request.requestId = m_nPendingStartRequestId;
    request.currArea = static_cast<eVisibleArea>(CGame::currArea);
    snprintf(request.name, sizeof(request.name), "%s", CCutsceneMgr::ms_cutsceneName);
    GetPacketFactory().Send(request);
}

void CCutsceneVoteManager::NotifySynchronizedCutsceneEnded()
{
    if (!CNetwork::m_bAuthenticated || !CLocalPlayer::m_bIsHost)
    {
        return;
    }

    if (m_State.cutsceneEpoch != 0)
    {
        SendEndRequest();
    }
    else if (m_nPendingStartRequestId != 0)
    {
        m_bEndWhenAcknowledged = true;
    }
}

void CCutsceneVoteManager::SkipCurrentCutsceneImmediately()
{
    m_bDisabledCutscenePending = true;
    CHud::m_BigMessage[1][0] = 0;
    CCutsceneMgr::ms_wasCutsceneSkipped = true;
    if (CCutsceneMgr::ms_running)
    {
        m_bObservedCutsceneRunning = true;
        CCutsceneMgr::FinishCutscene();
    }
    RestoreGameplayPresentation();
}

bool CCutsceneVoteManager::HandleSkipButton(bool bPressed)
{
    if (!bPressed)
    {
        return false;
    }

    const MissionSessionState& mission = CMissionSessionClient::GetState();
    if (!CNetwork::m_bAuthenticated || !mission.IsActive())
    {
        return true;
    }

    // Authenticated mission clients never skip locally. Spectators and clients awaiting an authoritative
    // cutscene epoch simply suppress input; eligible voters submit at most one request for that epoch.
    if (CMissionSessionClient::IsSpectator() || m_State.cutsceneEpoch == 0 ||
        m_State.lifecycle != eCutsceneVoteLifecycle::ACTIVE || !IsStateForCurrentMission(m_State))
    {
        return false;
    }

    if (!m_bLocalVoteSent)
    {
        CutsceneVoteRequest request{};
        request.sessionId = m_State.sessionId;
        request.missionEpoch = m_State.missionEpoch;
        request.cutsceneEpoch = m_State.cutsceneEpoch;
        GetPacketFactory().Send(request);
        m_bLocalVoteSent = true;
    }
    return false;
}

void CCutsceneVoteManager::HandleState(const CutsceneVoteState& state)
{
    if (!state.HasValidVoteState() || !IsStateForCurrentMission(state))
    {
        logger::warn("Rejected invalid cutscene vote state");
        return;
    }

    const bool bStateAcknowledgesPendingStart = m_nPendingStartRequestId != 0 &&
        state.startRequestId == m_nPendingStartRequestId && state.sessionId == m_nPendingSessionId &&
        state.missionEpoch == m_nPendingMissionEpoch;
    const bool bEndAcknowledgedCutscene = bStateAcknowledgesPendingStart && m_bEndWhenAcknowledged;
    const bool bSameCutscene = IsSameCutscene(state);
    if (!bSameCutscene)
    {
        if (CLocalPlayer::m_bIsHost && m_nPendingStartRequestId != 0 &&
            !bStateAcknowledgesPendingStart)
        {
            logger::warn("Rejected cutscene state that did not acknowledge the host start request");
            return;
        }
        if (m_nLastSessionId == state.sessionId && m_nLastCutsceneEpoch != 0 &&
            !IsSequenceNumberNewer(state.cutsceneEpoch, m_nLastCutsceneEpoch))
        {
            logger::warn("Rejected stale cutscene vote epoch");
            return;
        }
        if (state.lifecycle == eCutsceneVoteLifecycle::ENDED)
        {
            return;
        }

        const bool bObservedCutsceneRunning = m_bObservedCutsceneRunning || CCutsceneMgr::ms_running;
        RememberCurrentEpoch();
        ClearActiveState();
        m_State = state;
        m_bObservedCutsceneRunning = bObservedCutsceneRunning;
    }
    else
    {
        if (state.voteCount < m_State.voteCount ||
            static_cast<int>(state.lifecycle) < static_cast<int>(m_State.lifecycle))
        {
            logger::warn("Rejected regressive cutscene vote state");
            return;
        }
        m_State = state;
    }

    if (state.lifecycle == eCutsceneVoteLifecycle::SKIPPED)
    {
        ApplyAuthoritativeSkip();
    }
    else if (state.lifecycle == eCutsceneVoteLifecycle::ENDED)
    {
        RememberCurrentEpoch();
        ClearActiveState();
        return;
    }

    if (bEndAcknowledgedCutscene && m_State.cutsceneEpoch != 0)
    {
        SendEndRequest();
    }
}

void CCutsceneVoteManager::Process()
{
    // This playtest build intentionally disables cinematic playback. Catch asynchronous starts as well as
    // synchronized start opcodes so a delayed cutscene load cannot leave either peer on a black frame.
    if (CCutsceneMgr::ms_running)
    {
        SkipCurrentCutsceneImmediately();
    }
    else if (m_bDisabledCutscenePending)
    {
        // LOAD_CUTSCENE may fade the host before START_CUTSCENE is reached. Keep the presentation playable
        // while the stock mission script advances to CLEAR_CUTSCENE.
        RestoreGameplayPresentation();
    }

    if (!CNetwork::m_bAuthenticated)
    {
        Reset();
        return;
    }

    if (m_State.cutsceneEpoch == 0)
    {
        return;
    }
    if (!IsStateForCurrentMission(m_State))
    {
        RememberCurrentEpoch();
        ClearActiveState();
        return;
    }

    if (CCutsceneMgr::ms_running)
    {
        m_bObservedCutsceneRunning = true;
    }
    else if (m_bObservedCutsceneRunning && CLocalPlayer::m_bIsHost)
    {
        SendEndRequest();
    }
}

void CCutsceneVoteManager::HandleMissionSessionReset()
{
    m_State = CutsceneVoteState{};
    m_nPendingStartRequestId = 0;
    m_nPendingSessionId = 0;
    m_nPendingMissionEpoch = 0;
    m_nLastSessionId = 0;
    m_nLastCutsceneEpoch = 0;
    m_bLocalVoteSent = false;
    m_bSkipApplied = false;
    m_bEndRequestSent = false;
    m_bEndWhenAcknowledged = false;
    m_bObservedCutsceneRunning = false;
    m_bDisabledCutscenePending = false;
}

void CCutsceneVoteManager::Reset()
{
    HandleMissionSessionReset();
    m_nNextStartRequestId = 0;
}

uint32_t CCutsceneVoteManager::NextStartRequestId()
{
    do
    {
        ++m_nNextStartRequestId;
    } while (m_nNextStartRequestId == 0);
    return m_nNextStartRequestId;
}

bool CCutsceneVoteManager::IsStateForCurrentMission(const CutsceneVoteState& state)
{
    const MissionSessionState& mission = CMissionSessionClient::GetState();
    const int localPlayerId = CNetworkPlayerManager::m_nMyId;
    return mission.IsActive() && mission.sessionId == state.sessionId && mission.hostId == state.hostId &&
           mission.gameplayParticipantCount == state.eligibleCount &&
           mission.ContainsGameplayParticipant(localPlayerId) &&
           state.requiredVotes == static_cast<uint8_t>(mission.gameplayParticipantCount / 2 + 1);
}

bool CCutsceneVoteManager::IsSameCutscene(const CutsceneVoteState& state)
{
    return m_State.cutsceneEpoch != 0 && m_State.sessionId == state.sessionId &&
           m_State.missionEpoch == state.missionEpoch && m_State.cutsceneEpoch == state.cutsceneEpoch;
}

void CCutsceneVoteManager::SendEndRequest()
{
    if (m_bEndRequestSent || m_State.cutsceneEpoch == 0 || !CLocalPlayer::m_bIsHost ||
        !IsStateForCurrentMission(m_State))
    {
        return;
    }

    m_bEndRequestSent = true;
    CutsceneEndRequest request{};
    request.sessionId = m_State.sessionId;
    request.missionEpoch = m_State.missionEpoch;
    request.cutsceneEpoch = m_State.cutsceneEpoch;
    GetPacketFactory().Send(request);
}

void CCutsceneVoteManager::ApplyAuthoritativeSkip()
{
    if (m_bSkipApplied)
    {
        return;
    }
    m_bSkipApplied = true;
    SkipCurrentCutsceneImmediately();
    if (CLocalPlayer::m_bIsHost)
    {
        SendEndRequest();
    }
}

void CCutsceneVoteManager::RememberCurrentEpoch()
{
    if (m_State.cutsceneEpoch != 0)
    {
        m_nLastSessionId = m_State.sessionId;
        m_nLastCutsceneEpoch = m_State.cutsceneEpoch;
    }
}

void CCutsceneVoteManager::ClearActiveState()
{
    m_State = CutsceneVoteState{};
    m_nPendingStartRequestId = 0;
    m_nPendingSessionId = 0;
    m_nPendingMissionEpoch = 0;
    m_bLocalVoteSent = false;
    m_bSkipApplied = false;
    m_bEndRequestSent = false;
    m_bEndWhenAcknowledged = false;
    m_bObservedCutsceneRunning = false;
}

void CCutsceneVoteManager::RestoreGameplayPresentation()
{
    TheCamera.SetWideScreenOff();
    TheCamera.RestoreWithJumpCut();
    TheCamera.SetCameraDirectlyBehindForFollowPed_CamOnAString();

    if (CPad* pad = CPad::GetPad(0))
    {
        pad->SetDrunkInputDelay(0);
        pad->DisablePlayerControls = 0;
        pad->bApplyBrakes = 0;
        pad->bDisablePlayerEnterCar = 0;
        pad->bDisablePlayerDuck = 0;
        pad->bDisablePlayerFireWeapon = 0;
        pad->bDisablePlayerFireWeaponWithL1 = 0;
        pad->bDisablePlayerCycleWeapon = 0;
        pad->bDisablePlayerJump = 0;
        pad->bDisablePlayerDisplayVitalStats = 0;
    }
    CDraw::FadeValue = 0;
}
