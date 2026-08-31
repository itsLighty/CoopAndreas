#pragma once

#include "network/packets/stunts.h"

#include <array>
#include <cstdint>

class CStuntJumpSyncManager
{
public:
    static void InjectHook();
    static void ProcessNativeUpdate();
    static void HandleAttemptResult(const Packets::Stunts::StuntAttemptResult& result);
    static void HandleState(const Packets::Stunts::StuntStateEvent& state);
    static void ResetNetworkState();
    static void HandleAuthorityChanged();

private:
    struct CachedState
    {
        bool valid = false;
        Packets::Stunts::StuntStateEvent state{};
    };

    struct NativeBaseline
    {
        bool valid = false;
        uint32_t fingerprint = 0;
        bool done = false;
        bool found = false;
    };

    static std::array<CachedState, Packets::Stunts::STUNT_JUMP_CAPACITY> m_states;
    static std::array<NativeBaseline, Packets::Stunts::STUNT_JUMP_CAPACITY> m_baseline;
    static std::array<uint32_t, Packets::Stunts::STUNT_JUMP_CAPACITY> m_appliedAwardSequences;
    static uintptr_t m_nativeUpdateAddress;
    static bool m_wasAuthenticated;
    static bool m_wasHost;
    static bool m_baselineCaptured;
    static uint64_t m_serverRunId;
    static uint64_t m_clientSessionNonce;
    static uint16_t m_announcedCatalogCount;
    static uint32_t m_announcedCatalogHash;
    static uint16_t m_publishCursor;
    static uint32_t m_nextPublishAt;
    static uint32_t m_nextCatalogRefreshAt;
    static uint32_t m_nextRequestId;
    static bool m_attemptActive;
    static Packets::Stunts::StuntId m_attemptId;
    static uint32_t m_attemptRequestId;
    static uint32_t m_attemptStartedAt;
    static bool m_attemptStartAccepted;
    static bool m_attemptHitFinish;
    static bool m_attemptHitFinishAccepted;
    static bool m_attemptLanded;
    static bool m_pendingActionActive;
    static Packets::Stunts::eStuntAttemptAction m_pendingAction;
    static uint32_t m_pendingActionSentAt;
    static uint32_t m_pendingActionRetryAt;
    static uint8_t m_pendingActionRetryCount;
    static uint64_t m_attemptMissionSessionId;
    static bool m_attemptMissionWasActive;
    static bool m_attemptMissionFlag;
    static bool m_presentationActive;
    static float m_previousTimeScale;
    static uint32_t m_previousBlurRed;
    static uint32_t m_previousBlurGreen;
    static uint32_t m_previousBlurBlue;
    static uint32_t m_previousBlurType;
    static uint32_t m_previousMotionBlur;
    static uint32_t m_previousMotionBlurAddAlpha;

    static uint64_t EnsureClientSessionNonce();
    static uint32_t NextRequestId();
    static void CaptureNativeBaseline();
    static void RestoreNativeBaseline();
    static void BeginServerRun(uint64_t serverRunId);
    static void PublishCatalog();
    static void ApplyCachedStates();
    static void ApplyStateToNative(const Packets::Stunts::StuntStateEvent& state);
    static void RecountNativeCompleted();
    static void ProcessOnlineJump();
    static bool StartAttempt(uint16_t slot);
    static void FinishAttempt(bool completed, bool notifyServer);
    static bool SendAttempt(Packets::Stunts::eStuntAttemptAction action);
    static void QueueAttemptAction(Packets::Stunts::eStuntAttemptAction action);
    static void ProcessPendingAttempt();
    static void AdvanceAttemptProtocol();
    static bool IsRetryableResult(Packets::Stunts::eStuntAttemptResultReason reason);
    static bool MissionContextChanged();
    static void StartLocalSlowMotionPresentation(const CVector& camera, CVehicle* vehicle);
    static void ApplyLocalSlowMotionPresentation();
    static void StopLocalSlowMotionPresentation();
    static void ApplyAwardOnce(const Packets::Stunts::StuntStateEvent& state);
};
