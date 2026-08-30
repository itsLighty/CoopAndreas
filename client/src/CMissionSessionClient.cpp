#include "stdafx.h"
#include "CMissionSessionClient.h"

#include <CNetworkCheckpoint.h>
#include <CNetworkEntityBlip.h>
#include <COpCodeSync.h>

using namespace Packets::Scripts;

namespace
{
constexpr uint8_t MAX_LAUNCH_REQUEST_RETRIES = 3;
constexpr uint16_t MAX_LOCAL_LAUNCH_ROSTER_WAIT_FRAMES = 600;
}

MissionSessionState CMissionSessionClient::m_State{};
MissionSessionState CMissionSessionClient::m_DeferredState{};
bool CMissionSessionClient::m_bHasDeferredState = false;
bool CMissionSessionClient::m_bObservedMissionFlagInitialized = false;
bool CMissionSessionClient::m_bLastObservedMissionFlag = false;
bool CMissionSessionClient::m_bLaunchRequestPending = false;
bool CMissionSessionClient::m_bLaunchLocallyOnApproval = false;
bool CMissionSessionClient::m_bScmLaunchRequestPending = false;
uint16_t CMissionSessionClient::m_nPendingMissionId = MISSION_ID_UNKNOWN;
uint32_t CMissionSessionClient::m_nPendingLaunchRequestId = 0;
uint8_t CMissionSessionClient::m_nLaunchRetryCount = 0;
bool CMissionSessionClient::m_bApprovedLocalLaunchPending = false;
uint64_t CMissionSessionClient::m_nApprovedLocalLaunchSessionId = 0;
uint16_t CMissionSessionClient::m_nApprovedLocalMissionId = MISSION_ID_UNKNOWN;
uint16_t CMissionSessionClient::m_nApprovedLocalLaunchWaitFrames = 0;
bool CMissionSessionClient::m_bStageRequestPending = false;
uint32_t CMissionSessionClient::m_nPendingStageRequestId = 0;
uint32_t CMissionSessionClient::m_nPendingStage = 0;
bool CMissionSessionClient::m_bTerminalRequestPending = false;
uint32_t CMissionSessionClient::m_nPendingTerminalRequestId = 0;
eMissionSessionRequestAction CMissionSessionClient::m_PendingTerminalAction = eMissionSessionRequestAction::END;
eMissionSessionResult CMissionSessionClient::m_PendingTerminalResult = eMissionSessionResult::NONE;
uint32_t CMissionSessionClient::m_nNextRequestId = 0;
eMissionSessionResult CMissionSessionClient::m_PendingEndAfterLaunchResult = eMissionSessionResult::NONE;
eMissionSessionResult CMissionSessionClient::m_ObservedScmMissionResult = eMissionSessionResult::NONE;
uint64_t CMissionSessionClient::m_nObservedScmMissionResultSessionId = 0;

void CMissionSessionClient::Process()
{
    if (m_bHasDeferredState && CNetwork::m_bAuthenticated)
    {
        const MissionSessionState deferredState = m_DeferredState;
        m_bHasDeferredState = false;
        ApplyState(deferredState);
    }

    if (CNetwork::m_bAuthenticated)
    {
        ProcessApprovedLocalLaunch();
    }

    if (!CNetwork::m_bAuthenticated || !CTheScripts::OnAMissionFlag)
    {
        return;
    }

    const bool bOnMission = static_cast<bool>(CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag]);
    if (!m_bObservedMissionFlagInitialized)
    {
        m_bObservedMissionFlagInitialized = true;
        m_bLastObservedMissionFlag = bOnMission;
        if (CLocalPlayer::m_bIsHost && bOnMission && !IsLocalPlayerSessionHost() && !m_bLaunchRequestPending)
        {
            // Authentication/reconnection can complete while a local mission is already running. Announce it
            // immediately instead of treating the first true value as a silent baseline.
            RequestLaunch(MISSION_ID_UNKNOWN);
        }
        return;
    }

    if (!CLocalPlayer::m_bIsHost || bOnMission == m_bLastObservedMissionFlag)
    {
        m_bLastObservedMissionFlag = bOnMission;
        return;
    }

    m_bLastObservedMissionFlag = bOnMission;
    if (bOnMission)
    {
        if (!IsLocalPlayerSessionHost() && !m_bLaunchRequestPending)
        {
            RequestLaunch(MISSION_ID_UNKNOWN);
        }
    }
    else if (IsLocalPlayerSessionHost())
    {
        RequestEnd(ResolveScmMissionResult());
    }
    else if (m_bLaunchRequestPending)
    {
        // A native local mission can finish before its reliable launch acknowledgement arrives.
        m_PendingEndAfterLaunchResult = ResolveScmMissionResult();
    }
}

void CMissionSessionClient::HandleState(const MissionSessionState& state)
{
    if (!state.HasValidParticipantRoster())
    {
        logger::warn("Rejected invalid mission-session state");
        return;
    }

    if (!CNetwork::m_bAuthenticated)
    {
        bool bIsNewerRevision = false;
        if (!m_bHasDeferredState || ShouldAcceptState(m_DeferredState, state, bIsNewerRevision))
        {
            m_DeferredState = state;
            m_bHasDeferredState = true;
        }
        return;
    }

    ApplyState(state);
}

void CMissionSessionClient::HandleLegacyMissionFlag(bool bOnMission)
{
    if (!CLocalPlayer::m_bIsHost && m_State.epoch == 0 &&
        (!m_bHasDeferredState || m_DeferredState.epoch == 0))
    {
        ApplyLocalMissionFlag(bOnMission);
    }
}

void CMissionSessionClient::Reset()
{
    ApplyLocalMissionFlag(false);
    CancelPendingMissionMedia();
    m_State = MissionSessionState{};
    m_DeferredState = MissionSessionState{};
    m_bHasDeferredState = false;
    m_bObservedMissionFlagInitialized = false;
    m_bLastObservedMissionFlag = false;
    ClearPendingLaunch();
    ClearPendingStage();
    ClearPendingTerminal();
    m_bApprovedLocalLaunchPending = false;
    m_nApprovedLocalLaunchSessionId = 0;
    m_nApprovedLocalMissionId = MISSION_ID_UNKNOWN;
    m_nApprovedLocalLaunchWaitFrames = 0;
    m_nNextRequestId = 0;
    m_PendingEndAfterLaunchResult = eMissionSessionResult::NONE;
    m_ObservedScmMissionResult = eMissionSessionResult::NONE;
    m_nObservedScmMissionResultSessionId = 0;
}

bool CMissionSessionClient::RequestLaunch(uint16_t missionId, bool bLaunchLocallyOnApproval)
{
    if (!CNetwork::m_bAuthenticated || !CLocalPlayer::m_bIsHost || m_State.IsActive() ||
        m_bLaunchRequestPending || !IsMissionIdKnownOrUnknown(missionId))
    {
        return false;
    }

    if (bLaunchLocallyOnApproval &&
        (missionId == MISSION_ID_UNKNOWN || missionId >= CTheScripts::NumberOfMissionScripts))
    {
        return false;
    }

    m_bLaunchRequestPending = true;
    m_bLaunchLocallyOnApproval = bLaunchLocallyOnApproval;
    m_nPendingMissionId = missionId;
    m_nPendingLaunchRequestId = 0;
    m_nLaunchRetryCount = 0;
    m_PendingEndAfterLaunchResult = eMissionSessionResult::NONE;
    m_ObservedScmMissionResult = eMissionSessionResult::NONE;
    m_nObservedScmMissionResultSessionId = 0;
    if (!SendPendingLaunchRequest())
    {
        ClearPendingLaunch();
        return false;
    }
    return true;
}

bool CMissionSessionClient::RequestScmLaunch(int missionId)
{
    if (missionId < 0 || missionId > MISSION_ID_MAX || missionId >= CTheScripts::NumberOfMissionScripts)
    {
        RollbackScmMissionLaunch();
        return false;
    }

    const uint16_t validatedMissionId = static_cast<uint16_t>(missionId);

    // MAIN launches its two bootstrap missions before multiplayer authentication can be guaranteed.
    // Keeping the native fallback here preserves offline/pre-network game initialization without
    // creating an authenticated path around the authoritative mission-session handshake.
    if (!CNetwork::m_bAuthenticated)
    {
        Command<Commands::LOAD_AND_LAUNCH_MISSION_INTERNAL>(validatedMissionId);
        return true;
    }

    // A repeated launcher tick must not disturb the request/session that already owns $onmission.
    // This also prevents an unrelated ambient launcher from clearing an active co-op mission.
    if (m_State.IsActive() || IsLaunchPending())
    {
        return (m_bLaunchRequestPending && m_bLaunchLocallyOnApproval &&
                m_nPendingMissionId == validatedMissionId) ||
               (m_bApprovedLocalLaunchPending && m_nApprovedLocalMissionId == validatedMissionId);
    }

    if (!CLocalPlayer::m_bIsHost)
    {
        RollbackScmMissionLaunch();
        return false;
    }

    m_bScmLaunchRequestPending = true;
    if (!RequestLaunch(validatedMissionId, true))
    {
        // RequestLaunch clears all pending-launch metadata when sending fails.
        RollbackScmMissionLaunch();
        return false;
    }
    return true;
}

bool CMissionSessionClient::RequestStage(uint32_t stage)
{
    if (!IsLocalPlayerSessionHost() || m_bTerminalRequestPending)
    {
        return false;
    }

    m_bStageRequestPending = true;
    m_nPendingStage = stage;
    if (m_nPendingStageRequestId == 0)
    {
        return SendPendingStageRequest();
    }
    return true;
}

bool CMissionSessionClient::RequestEnd(eMissionSessionResult result)
{
    if (!IsLocalPlayerSessionHost() ||
        (result != eMissionSessionResult::COMPLETED && result != eMissionSessionResult::SUCCEEDED &&
            result != eMissionSessionResult::FAILED))
    {
        return false;
    }

    ClearPendingStage();
    m_bTerminalRequestPending = true;
    m_PendingTerminalAction = eMissionSessionRequestAction::END;
    m_PendingTerminalResult = result;
    if (m_nPendingTerminalRequestId == 0)
    {
        return SendPendingTerminalRequest();
    }
    return true;
}

bool CMissionSessionClient::RequestAbort()
{
    if (!IsLocalPlayerSessionHost())
    {
        return false;
    }

    CancelPendingMissionMedia();
    ClearPendingStage();
    m_bTerminalRequestPending = true;
    m_PendingTerminalAction = eMissionSessionRequestAction::ABORT;
    m_PendingTerminalResult = eMissionSessionResult::ABORTED_BY_HOST;
    if (m_nPendingTerminalRequestId == 0)
    {
        return SendPendingTerminalRequest();
    }
    return true;
}

void CMissionSessionClient::ReportScmMissionResult(eMissionSessionResult result)
{
    if ((result != eMissionSessionResult::SUCCEEDED && result != eMissionSessionResult::FAILED) ||
        !CNetwork::m_bAuthenticated || !CLocalPlayer::m_bIsHost ||
        (!IsLocalPlayerSessionHost() && !m_bLaunchRequestPending) ||
        m_ObservedScmMissionResult != eMissionSessionResult::NONE)
    {
        return;
    }

    m_ObservedScmMissionResult = result;
    m_nObservedScmMissionResultSessionId = m_State.IsActive() ? m_State.sessionId : 0;
}

bool CMissionSessionClient::IsDeferredMediaSessionCurrent(uint64_t sessionId)
{
    if (sessionId == 0 || !m_State.IsActive() || m_State.sessionId != sessionId || IsSpectator())
    {
        return false;
    }

    return CTheScripts::OnAMissionFlag &&
           static_cast<bool>(CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag]);
}

void CMissionSessionClient::CancelPendingMissionMedia()
{
    for (uint8_t slot = 0; slot < 4; ++slot)
    {
        if (COpCodeSync::ms_abLoadingMissionAudio[slot])
        {
            // The audio-engine arrays are zero-based; the SCM opcode uses slots 1 through 4.
            Command<Commands::CLEAR_MISSION_AUDIO>(slot + 1);
        }
        COpCodeSync::ms_abLoadingMissionAudio[slot] = false;
        COpCodeSync::ms_anLoadingMissionAudioSessionIds[slot] = 0;
    }

    if (COpCodeSync::ms_bLoadingCutscene)
    {
        Command<Commands::CLEAR_CUTSCENE>();
    }
    COpCodeSync::ms_bLoadingCutscene = false;
    COpCodeSync::ms_nLoadingCutsceneSessionId = 0;
}

const MissionSessionState& CMissionSessionClient::GetState()
{
    return m_State;
}

bool CMissionSessionClient::IsLaunchPending()
{
    return m_bLaunchRequestPending || m_bApprovedLocalLaunchPending;
}

bool CMissionSessionClient::IsSpectator()
{
    return m_State.IsActive() && !m_State.ContainsGameplayParticipant(CNetworkPlayerManager::m_nMyId);
}

void CMissionSessionClient::ApplyState(const MissionSessionState& state)
{
    bool bIsNewerRevision = false;
    if (!ShouldAcceptState(m_State, state, bIsNewerRevision))
    {
        logger::warn("Rejected stale or conflicting mission-session state");
        return;
    }

    const int localPlayerId = CNetworkPlayerManager::m_nMyId;
    const bool bLaunchAcknowledged = m_bLaunchRequestPending &&
                                      state.AcknowledgesRequest(localPlayerId, m_nPendingLaunchRequestId);
    const bool bStageAcknowledged = m_bStageRequestPending &&
                                     state.AcknowledgesRequest(localPlayerId, m_nPendingStageRequestId);
    const bool bTerminalAcknowledged = m_bTerminalRequestPending &&
                                        state.AcknowledgesRequest(localPlayerId, m_nPendingTerminalRequestId);

    if (!bIsNewerRevision && state.acknowledgedRequestAccepted &&
        (bLaunchAcknowledged || bStageAcknowledged || bTerminalAcknowledged))
    {
        logger::warn("Rejected a mission-session acceptance without an authoritative revision change");
        return;
    }

    const bool bLocalPlayerIsHost = state.hostId == localPlayerId;
    const bool bLocalPlayerParticipates = state.ContainsGameplayParticipant(localPlayerId);

    if (!state.IsActive())
    {
        CancelPendingMissionMedia();
        m_ObservedScmMissionResult = eMissionSessionResult::NONE;
        m_nObservedScmMissionResultSessionId = 0;
    }
    else if (state.sessionId != m_State.sessionId)
    {
        if (!(bLaunchAcknowledged && m_ObservedScmMissionResult != eMissionSessionResult::NONE &&
              m_nObservedScmMissionResultSessionId == 0))
        {
            m_ObservedScmMissionResult = eMissionSessionResult::NONE;
        }
        m_nObservedScmMissionResultSessionId = state.sessionId;
    }
    m_State = state;

    if (bLaunchAcknowledged)
    {
        const bool bLaunchApproved = state.acknowledgedRequestAccepted && bIsNewerRevision && state.IsActive() &&
                                     bLocalPlayerIsHost && state.missionId == m_nPendingMissionId;
        if (bLaunchApproved)
        {
            const bool bLaunchLocally = m_bLaunchLocallyOnApproval;
            const uint16_t approvedMissionId = m_nPendingMissionId;
            ClearPendingLaunch();

            if (bLaunchLocally)
            {
                m_bApprovedLocalLaunchPending = true;
                m_nApprovedLocalLaunchSessionId = state.sessionId;
                m_nApprovedLocalMissionId = approvedMissionId;
                m_nApprovedLocalLaunchWaitFrames = 0;
            }

            if (m_PendingEndAfterLaunchResult != eMissionSessionResult::NONE)
            {
                const eMissionSessionResult result = m_PendingEndAfterLaunchResult;
                m_PendingEndAfterLaunchResult = eMissionSessionResult::NONE;
                m_bApprovedLocalLaunchPending = false;
                RequestEnd(result);
            }
        }
        else if (!state.acknowledgedRequestAccepted && !state.IsActive() &&
                 m_nLaunchRetryCount < MAX_LAUNCH_REQUEST_RETRIES)
        {
            m_nPendingLaunchRequestId = 0;
            ++m_nLaunchRetryCount;
            SendPendingLaunchRequest();
        }
        else
        {
            const bool bRejectedLocalLaunch = m_bLaunchLocallyOnApproval;
            const bool bRejectedScmLaunch = m_bScmLaunchRequestPending;
            ClearPendingLaunch();
            m_PendingEndAfterLaunchResult = eMissionSessionResult::NONE;
            if (bRejectedScmLaunch && !state.IsActive())
            {
                RollbackScmMissionLaunch();
            }
            if (bRejectedLocalLaunch && state.IsActive() && bLocalPlayerIsHost && CTheScripts::OnAMissionFlag &&
                !static_cast<bool>(CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag]))
            {
                RequestAbort();
            }
        }
    }

    if (!state.IsActive())
    {
        ClearPendingStage();
        ClearPendingTerminal();
        m_bApprovedLocalLaunchPending = false;
        m_nApprovedLocalLaunchSessionId = 0;
        m_nApprovedLocalMissionId = MISSION_ID_UNKNOWN;
        m_nApprovedLocalLaunchWaitFrames = 0;
    }
    else
    {
        if (m_bStageRequestPending && state.stage == m_nPendingStage)
        {
            ClearPendingStage();
        }
        else if (bStageAcknowledged)
        {
            m_nPendingStageRequestId = 0;
            if (IsLocalPlayerSessionHost())
            {
                SendPendingStageRequest();
            }
            else
            {
                ClearPendingStage();
            }
        }

        if (bTerminalAcknowledged)
        {
            m_nPendingTerminalRequestId = 0;
            if (IsLocalPlayerSessionHost())
            {
                SendPendingTerminalRequest();
            }
            else
            {
                ClearPendingTerminal();
            }
        }
    }

    if (!state.IsActive() || !bLocalPlayerIsHost)
    {
        ApplyLocalMissionFlag(state.IsActive() && bLocalPlayerParticipates);
    }
}

void CMissionSessionClient::ApplyLocalMissionFlag(bool bOnMission)
{
    if (!CTheScripts::OnAMissionFlag)
    {
        return;
    }

    const bool bWasOnMission = static_cast<bool>(CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag]);
    CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag] = bOnMission;

    if (bWasOnMission && !bOnMission)
    {
        CleanupLocalMissionState();
    }

    m_bObservedMissionFlagInitialized = true;
    m_bLastObservedMissionFlag = bOnMission;
}

void CMissionSessionClient::RollbackScmMissionLaunch()
{
    ApplyLocalMissionFlag(false);
}

void CMissionSessionClient::CleanupLocalMissionState()
{
    CancelPendingMissionMedia();
    CNetworkCheckpoint::Remove();
    CNetworkEntityBlip::ClearEntityBlips();
    TheCamera.SetWideScreenOff();
    TheCamera.RestoreWithJumpCut();
    TheCamera.SetCameraDirectlyBehindForFollowPed_CamOnAString();
    CPad::GetPad(0)->SetDrunkInputDelay(0);
    CPad::GetPad(0)->DisablePlayerControls = 0;
    CPad::GetPad(0)->bApplyBrakes = 0;
    CPad::GetPad(0)->bDisablePlayerEnterCar = 0;
    CPad::GetPad(0)->bDisablePlayerDuck = 0;
    CPad::GetPad(0)->bDisablePlayerFireWeapon = 0;
    CPad::GetPad(0)->bDisablePlayerFireWeaponWithL1 = 0;
    CPad::GetPad(0)->bDisablePlayerCycleWeapon = 0;
    CPad::GetPad(0)->bDisablePlayerJump = 0;
    CPad::GetPad(0)->bDisablePlayerDisplayVitalStats = 0;
    CDraw::FadeValue = 0;
}

bool CMissionSessionClient::ShouldAcceptState(
    const MissionSessionState& current, const MissionSessionState& candidate, bool& bIsNewerRevision)
{
    bIsNewerRevision = false;
    if (candidate.sessionId == current.sessionId)
    {
        if (candidate.epoch == current.epoch)
        {
            return candidate.HasSameAuthoritativeState(current);
        }
        bIsNewerRevision = IsSequenceNumberNewer(candidate.epoch, current.epoch);
        return bIsNewerRevision;
    }

    if (current.sessionId == 0)
    {
        bIsNewerRevision = candidate.sessionId != 0;
        return bIsNewerRevision;
    }
    if (candidate.sessionId == 0)
    {
        return false;
    }

    bIsNewerRevision = IsSequenceNumberNewer(candidate.sessionId, current.sessionId);
    return bIsNewerRevision;
}

bool CMissionSessionClient::PrepareGameplayRoster(const MissionSessionState& state)
{
    const int localPlayerId = CNetworkPlayerManager::m_nMyId;
    if (!state.IsActive() || state.gameplayParticipantCount == 0 || state.participantIds[0] != localPlayerId)
    {
        return false;
    }

    bool seenPlayerIds[Config::MAX_SERVER_PLAYERS]{};
    std::vector<CNetworkPlayer*> orderedPlayers;
    orderedPlayers.reserve(CNetworkPlayerManager::m_pPlayers.size());

    for (CNetworkPlayer* player : CNetworkPlayerManager::m_pPlayers)
    {
        if (player == nullptr || player->m_iPlayerId < 0 || player->m_iPlayerId >= Config::MAX_SERVER_PLAYERS ||
            player->m_iPlayerId == localPlayerId || seenPlayerIds[player->m_iPlayerId])
        {
            return false;
        }
        seenPlayerIds[player->m_iPlayerId] = true;
    }

    for (size_t rosterIndex = 1; rosterIndex < state.gameplayParticipantCount; ++rosterIndex)
    {
        const int participantId = state.participantIds[rosterIndex];
        CNetworkPlayer* participant = CNetworkPlayerManager::GetPlayer(participantId);
        if (participant == nullptr || participant->m_pPed == nullptr)
        {
            return false;
        }
        orderedPlayers.push_back(participant);
    }

    for (size_t rosterIndex = state.gameplayParticipantCount; rosterIndex < state.participantCount; ++rosterIndex)
    {
        if (CNetworkPlayer* spectator = CNetworkPlayerManager::GetPlayer(state.participantIds[rosterIndex]))
        {
            orderedPlayers.push_back(spectator);
        }
    }

    for (CNetworkPlayer* player : CNetworkPlayerManager::m_pPlayers)
    {
        if (std::find(orderedPlayers.begin(), orderedPlayers.end(), player) == orderedPlayers.end())
        {
            orderedPlayers.push_back(player);
        }
    }

    CNetworkPlayerManager::m_pPlayers.swap(orderedPlayers);
    return true;
}

void CMissionSessionClient::ProcessApprovedLocalLaunch()
{
    if (!m_bApprovedLocalLaunchPending)
    {
        return;
    }

    if (!IsLocalPlayerSessionHost() || m_State.sessionId != m_nApprovedLocalLaunchSessionId ||
        m_State.missionId != m_nApprovedLocalMissionId)
    {
        m_bApprovedLocalLaunchPending = false;
        return;
    }

    if (m_nApprovedLocalMissionId == MISSION_ID_UNKNOWN ||
        m_nApprovedLocalMissionId >= CTheScripts::NumberOfMissionScripts)
    {
        logger::warn("Server approved an invalid local mission ID");
        m_bApprovedLocalLaunchPending = false;
        RequestAbort();
        return;
    }

    if (!PrepareGameplayRoster(m_State))
    {
        if (++m_nApprovedLocalLaunchWaitFrames >= MAX_LOCAL_LAUNCH_ROSTER_WAIT_FRAMES)
        {
            logger::warn("Timed out waiting for the approved mission roster");
            m_bApprovedLocalLaunchPending = false;
            RequestAbort();
        }
        return;
    }

    const uint16_t missionId = m_nApprovedLocalMissionId;
    m_bApprovedLocalLaunchPending = false;
    m_nApprovedLocalLaunchSessionId = 0;
    m_nApprovedLocalMissionId = MISSION_ID_UNKNOWN;
    m_nApprovedLocalLaunchWaitFrames = 0;
    Command<Commands::LOAD_AND_LAUNCH_MISSION_INTERNAL>(missionId);

    if (CTheScripts::OnAMissionFlag)
    {
        m_bObservedMissionFlagInitialized = true;
        m_bLastObservedMissionFlag =
            static_cast<bool>(CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag]);
    }
}

void CMissionSessionClient::ClearPendingLaunch()
{
    m_bLaunchRequestPending = false;
    m_bLaunchLocallyOnApproval = false;
    m_bScmLaunchRequestPending = false;
    m_nPendingMissionId = MISSION_ID_UNKNOWN;
    m_nPendingLaunchRequestId = 0;
    m_nLaunchRetryCount = 0;
}

void CMissionSessionClient::ClearPendingStage()
{
    m_bStageRequestPending = false;
    m_nPendingStageRequestId = 0;
    m_nPendingStage = 0;
}

void CMissionSessionClient::ClearPendingTerminal()
{
    m_bTerminalRequestPending = false;
    m_nPendingTerminalRequestId = 0;
    m_PendingTerminalAction = eMissionSessionRequestAction::END;
    m_PendingTerminalResult = eMissionSessionResult::NONE;
}

bool CMissionSessionClient::SendPendingLaunchRequest()
{
    if (!m_bLaunchRequestPending || !CNetwork::m_bAuthenticated || !CLocalPlayer::m_bIsHost || m_State.IsActive())
    {
        return false;
    }

    MissionSessionRequest request{};
    FillRequestFromCurrentState(request);
    request.action = eMissionSessionRequestAction::LAUNCH;
    request.missionId = m_nPendingMissionId;
    request.stage = 0;
    request.result = eMissionSessionResult::NONE;
    m_nPendingLaunchRequestId = request.requestId;
    GetPacketFactory().Send(request);
    return true;
}

bool CMissionSessionClient::SendPendingStageRequest()
{
    if (!m_bStageRequestPending || !IsLocalPlayerSessionHost())
    {
        return false;
    }

    MissionSessionRequest request{};
    FillRequestFromCurrentState(request);
    request.action = eMissionSessionRequestAction::UPDATE_STAGE;
    request.stage = m_nPendingStage;
    request.result = eMissionSessionResult::NONE;
    m_nPendingStageRequestId = request.requestId;
    GetPacketFactory().Send(request);
    return true;
}

bool CMissionSessionClient::SendPendingTerminalRequest()
{
    if (!m_bTerminalRequestPending || !IsLocalPlayerSessionHost())
    {
        return false;
    }

    MissionSessionRequest request{};
    FillRequestFromCurrentState(request);
    request.action = m_PendingTerminalAction;
    request.result = m_PendingTerminalResult;
    m_nPendingTerminalRequestId = request.requestId;
    GetPacketFactory().Send(request);
    return true;
}

bool CMissionSessionClient::IsLocalPlayerSessionHost()
{
    return CNetwork::m_bAuthenticated && CLocalPlayer::m_bIsHost && m_State.IsActive() &&
           m_State.hostId == CNetworkPlayerManager::m_nMyId &&
           m_State.ContainsGameplayParticipant(CNetworkPlayerManager::m_nMyId);
}

eMissionSessionResult CMissionSessionClient::ResolveScmMissionResult()
{
    const bool bMatchesActiveSession = m_State.IsActive() &&
                                       m_nObservedScmMissionResultSessionId == m_State.sessionId;
    const bool bMatchesPendingLaunch = m_bLaunchRequestPending &&
                                       m_nObservedScmMissionResultSessionId == 0;
    if (m_ObservedScmMissionResult != eMissionSessionResult::NONE &&
        (bMatchesActiveSession || bMatchesPendingLaunch))
    {
        return m_ObservedScmMissionResult;
    }
    return eMissionSessionResult::COMPLETED;
}

uint32_t CMissionSessionClient::NextRequestId()
{
    ++m_nNextRequestId;
    if (m_nNextRequestId == 0)
    {
        ++m_nNextRequestId;
    }
    return m_nNextRequestId;
}

void CMissionSessionClient::FillRequestFromCurrentState(MissionSessionRequest& request)
{
    request.requestId = NextRequestId();
    request.sessionId = m_State.sessionId;
    request.epoch = m_State.epoch;
    request.missionId = m_State.missionId;
    request.stage = m_State.stage;
}
