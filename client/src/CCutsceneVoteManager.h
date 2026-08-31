#pragma once

#include "network/packets/scripts.h"

class CCutsceneVoteManager
{
public:
    static void NotifySynchronizedCutsceneStarted();
    static void NotifySynchronizedCutsceneEnded();
    static void SkipCurrentCutsceneImmediately();
    static bool HandleSkipButton(bool bPressed);
    static void HandleState(const Packets::Scripts::CutsceneVoteState& state);
    static void Process();
    static void HandleMissionSessionReset();
    static void Reset();

private:
    static Packets::Scripts::CutsceneVoteState m_State;
    static uint32_t m_nNextStartRequestId;
    static uint32_t m_nPendingStartRequestId;
    static uint64_t m_nPendingSessionId;
    static uint32_t m_nPendingMissionEpoch;
    static uint64_t m_nLastSessionId;
    static uint32_t m_nLastCutsceneEpoch;
    static bool m_bLocalVoteSent;
    static bool m_bSkipApplied;
    static bool m_bEndRequestSent;
    static bool m_bEndWhenAcknowledged;
    static bool m_bObservedCutsceneRunning;

    static uint32_t NextStartRequestId();
    static bool IsStateForCurrentMission(const Packets::Scripts::CutsceneVoteState& state);
    static bool IsSameCutscene(const Packets::Scripts::CutsceneVoteState& state);
    static void SendEndRequest();
    static void ApplyAuthoritativeSkip();
    static void RememberCurrentEpoch();
    static void ClearActiveState();
};
