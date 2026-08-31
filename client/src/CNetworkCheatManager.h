#pragma once

#include "network/packets/cheats.h"

#include <array>
#include <cstdint>

class CNetworkCheatManager
{
public:
    static void AddToCheatStringHook(char lastPressedKey);
    static void BeginNetworkSession();
    static void ResetNetworkState();
    static void HandleAuthorityChanged(int authorityPlayerId, bool localPlayerIsAuthority);
    static void HandleState(const Packets::Cheats::CheatStateEvent& state);
    static void HandleAction(const Packets::Cheats::CheatActionEvent& action);
    static void Process();
    static bool IsApplyingCanonicalCheat();

private:
    static constexpr size_t PENDING_ACTION_CAPACITY = 16;
    static constexpr uint32_t PENDING_ACTION_LIFETIME_MS = 5000;

    struct PendingAction
    {
        bool valid = false;
        uint32_t eventSequence = 0;
        uint32_t receivedAt = 0;
        Packets::Cheats::eStockCheat cheat = Packets::Cheats::eStockCheat::WEAPON_SET_1;
    };

    static void RequestCheat(Packets::Cheats::eStockCheat cheat);
    static void ApplyPersistentMask(const Packets::Cheats::CheatMask& mask,
        bool hasCause = false,
        Packets::Cheats::eStockCheat cause = Packets::Cheats::eStockCheat::FASTER_CLOCK);
    static void ApplyGameplaySpeedStep(int8_t step);
    static void ExecuteNative(Packets::Cheats::eStockCheat cheat);
    static void ExecuteTransient(Packets::Cheats::eStockCheat cheat);
    static void QueueTransient(uint32_t eventSequence, Packets::Cheats::eStockCheat cheat);
    static void ShowAcceptedFeedback(Packets::Cheats::eStockCheat cheat, bool enabled);
    static bool HasLocalPlayer();
    static uint32_t NextRequestSequence();

    static bool m_sessionActive;
    static bool m_localPlayerIsAuthority;
    static bool m_pendingPersistentApply;
    static int m_authorityPlayerId;
    static uint64_t m_serverRunId;
    static uint32_t m_lastStateRevision;
    static uint32_t m_lastEventSequence;
    static uint32_t m_requestSequence;
    static uint8_t m_canonicalApplyDepth;
    static int8_t m_canonicalGameplaySpeedStep;
    static float m_offlineTimeScale;
    static Packets::Cheats::CheatMask m_offlineBaseline;
    static Packets::Cheats::CheatMask m_canonicalState;
    static std::array<PendingAction, PENDING_ACTION_CAPACITY> m_pendingActions;
};
