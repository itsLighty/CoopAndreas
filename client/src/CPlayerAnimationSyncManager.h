#pragma once

class CAnimBlendAssociation;

class CPlayerAnimationSyncManager
{
public:
    static void Process();
    static void ResetNetworkState();
    static void AcquirePlayIdles();
    static void ReleasePlayIdles();
    static bool EnsurePlayIdlesLoaded();

private:
    static Packets::Players::ePlayerAnimationState ObserveLocalAnimation(CAnimBlendAssociation*& association);
    static uint8_t QuantizeProgress(const CAnimBlendAssociation* association);
    static void SendState(Packets::Players::ePlayerAnimationState state, const CAnimBlendAssociation* association,
        uint32_t now);

    static inline Packets::Players::ePlayerAnimationState ms_lastState = Packets::Players::PLAYER_ANIMATION_NONE;
    static inline uint16_t ms_sequence = 0;
    static inline uint32_t ms_lastSentAt = 0;
    static inline bool ms_initialized = false;
    static inline uint16_t ms_playIdlesUsers = 0;
};
