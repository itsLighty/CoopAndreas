#pragma once

#include "network/packets/scripts.h"

class CMissionSessionClient
{
public:
    static void Process();
    static void HandleState(const Packets::Scripts::MissionSessionState& state);
    static void HandleLegacyMissionFlag(bool bOnMission);
    static void Reset();

    static bool RequestLaunch(uint16_t missionId, bool bLaunchLocallyOnApproval = false);
    static bool RequestStage(uint32_t stage);
    static bool RequestEnd(Packets::Scripts::eMissionSessionResult result);
    static bool RequestAbort();

    // The returned state retains the full frozen roster. IsSpectator reflects the narrower SCM gameplay subset.
    static const Packets::Scripts::MissionSessionState& GetState();
    static bool IsLaunchPending();
    static bool IsSpectator();

private:
    static Packets::Scripts::MissionSessionState m_State;
    static Packets::Scripts::MissionSessionState m_DeferredState;
    static bool m_bHasDeferredState;
    static bool m_bObservedMissionFlagInitialized;
    static bool m_bLastObservedMissionFlag;
    static bool m_bLaunchRequestPending;
    static bool m_bLaunchLocallyOnApproval;
    static uint16_t m_nPendingMissionId;
    static uint32_t m_nPendingLaunchRequestId;
    static uint8_t m_nLaunchRetryCount;
    static bool m_bApprovedLocalLaunchPending;
    static uint64_t m_nApprovedLocalLaunchSessionId;
    static uint16_t m_nApprovedLocalMissionId;
    static uint16_t m_nApprovedLocalLaunchWaitFrames;
    static bool m_bStageRequestPending;
    static uint32_t m_nPendingStageRequestId;
    static uint32_t m_nPendingStage;
    static bool m_bTerminalRequestPending;
    static uint32_t m_nPendingTerminalRequestId;
    static Packets::Scripts::eMissionSessionRequestAction m_PendingTerminalAction;
    static Packets::Scripts::eMissionSessionResult m_PendingTerminalResult;
    static uint32_t m_nNextRequestId;
    static Packets::Scripts::eMissionSessionResult m_PendingEndAfterLaunchResult;

    static void ApplyState(const Packets::Scripts::MissionSessionState& state);
    static void ApplyLocalMissionFlag(bool bOnMission);
    static void CleanupLocalMissionState();
    static bool ShouldAcceptState(const Packets::Scripts::MissionSessionState& current,
        const Packets::Scripts::MissionSessionState& candidate, bool& bIsNewerRevision);
    static bool PrepareGameplayRoster(const Packets::Scripts::MissionSessionState& state);
    static void ProcessApprovedLocalLaunch();
    static void ClearPendingLaunch();
    static void ClearPendingStage();
    static void ClearPendingTerminal();
    static bool SendPendingLaunchRequest();
    static bool SendPendingStageRequest();
    static bool SendPendingTerminalRequest();
    static bool IsLocalPlayerSessionHost();
    static uint32_t NextRequestId();
    static void FillRequestFromCurrentState(Packets::Scripts::MissionSessionRequest& request);
};
