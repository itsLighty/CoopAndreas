#pragma once

class CAnimBlendAssociation;

class CPlayerParachuteSyncManager
{
public:
    static constexpr uint32_t HEARTBEAT_INTERVAL_MS = 250;
    static constexpr uint32_t STALE_TIMEOUT_MS = 1500;

    static void ProcessLocal();
    static void ResetLocalState();

    static void AcquireResources();
    static void ReleaseResources();
    static bool EnsureResourcesLoaded();

private:
    static Packets::Players::ePlayerParachuteState ObserveLocalState(CAnimBlendAssociation*& association);
    static uint8_t QuantizeProgress(const CAnimBlendAssociation* association);
    static void SendState(Packets::Players::ePlayerParachuteState state,
        const CAnimBlendAssociation* association, uint32_t now);

    static inline Packets::Players::ePlayerParachuteState ms_lastState =
        Packets::Players::PLAYER_PARACHUTE_NONE;
    static inline uint16_t ms_sequence = 0;
    static inline uint32_t ms_lastSentAt = 0;
    static inline bool ms_initialized = false;
    static inline uint16_t ms_resourceUsers = 0;
    static inline int ms_animationBlockId = -1;
    static inline bool ms_animationBlockReferenced = false;
};
