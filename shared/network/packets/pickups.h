#pragma once

#include "config.h"
#include "eWeaponType.h"
#include "network/packet.h"
#include "network/serializable_types.h"
#include "serialize.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Packets::Pickups
{
constexpr uint16_t PICKUP_POOL_CAPACITY = 620;
constexpr uint8_t PICKUP_INVALID_PLAYER_ID = UINT8_MAX;
constexpr int16_t PICKUP_INVALID_COLLECTIBLE_INDEX = -1;
constexpr int16_t MAX_PICKUP_MODEL_ID = 20000;
constexpr int32_t MAX_PICKUP_REWARD = 2000000;
constexpr uint16_t MAX_PICKUP_AMMO = 9999;
constexpr uint32_t MAX_PICKUP_EXPIRY_MS = 3600000;
constexpr uint32_t MAX_PICKUP_RESPAWN_MS = 86400000;
constexpr float MAX_PICKUP_COLLECT_DISTANCE = 12.0f;

enum class ePickupKind : uint8_t
{
    TAG = 0,
    HORSESHOE,
    SNAPSHOT,
    OYSTER,
    STATIC_WEAPON,
    STATIC_ARMOUR,
    STATIC_BRIBE,
    DROPPED_MONEY,
    DROPPED_WEAPON,
    JETPACK,
    COUNT
};

inline bool IsKnownPickupKind(ePickupKind kind)
{
    return static_cast<uint8_t>(kind) < static_cast<uint8_t>(ePickupKind::COUNT);
}

inline bool IsCollectiblePickupKind(ePickupKind kind)
{
    return kind == ePickupKind::TAG || kind == ePickupKind::HORSESHOE ||
           kind == ePickupKind::SNAPSHOT || kind == ePickupKind::OYSTER;
}

inline bool IsCreationIntentPickupKind(ePickupKind kind)
{
    return kind == ePickupKind::DROPPED_MONEY || kind == ePickupKind::DROPPED_WEAPON ||
           kind == ePickupKind::JETPACK;
}

inline bool IsGrantablePickupWeaponId(int32_t weaponId)
{
    // CUtil::GiveWeaponByPacket resolves CWeaponInfo and streams its inventory model before calling
    // CPed::GiveWeapon. Keep only IDs with that concrete grant path: ordinary melee/throwables, inventory
    // firearms through satchels, and the three model-backed tools. IDs 19..21 are projectile entities rather
    // than inventory weapons; DETONATOR is granted as SATCHEL_CHARGE's helper; vision goggles and parachutes
    // have dedicated state flows and are not standalone weapon-pickup grants.
    return (weaponId >= WEAPON_BRASSKNUCKLE && weaponId <= WEAPON_MOLOTOV) ||
           (weaponId >= WEAPON_PISTOL && weaponId <= WEAPON_SATCHEL_CHARGE) ||
           (weaponId >= WEAPON_SPRAYCAN && weaponId <= WEAPON_CAMERA);
}

inline bool IsPickupRevisionNewer(uint32_t candidate, uint32_t reference)
{
    const uint32_t distance = candidate - reference;
    return distance != 0 && distance < 0x80000000u;
}

inline bool IsPickupGenerationNewer(uint16_t candidate, uint16_t reference)
{
    const uint16_t distance = static_cast<uint16_t>(candidate - reference);
    return distance != 0 && distance < 0x8000u;
}

inline bool IsFinitePickupPosition(const WorldPositionCompressed& position)
{
    return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z) &&
           position.x >= -3000.0f && position.x <= 3000.0f && position.y >= -3000.0f &&
           position.y <= 3000.0f && position.z >= -120.0f && position.z <= 1000.0f;
}

struct PickupId
{
    uint16_t slot = 0;
    uint16_t generation = 0;

    bool IsValid() const { return slot < PICKUP_POOL_CAPACITY && generation != 0; }

    bool operator==(const PickupId& other) const
    {
        return slot == other.slot && generation == other.generation;
    }

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, slot, 0, PICKUP_POOL_CAPACITY - 1);
        serialize_int(stream, generation, 1, UINT16_MAX);
        return !Stream::IsReading || IsValid();
    }
};

struct PickupMetadata
{
    ePickupKind kind = ePickupKind::TAG;
    WorldPositionCompressed position{};
    uint8_t interior = 0;
    int16_t modelId = -1;
    int32_t reward = 0;
    uint16_t ammo = 0;
    int16_t collectibleIndex = PICKUP_INVALID_COLLECTIBLE_INDEX;
    uint32_t expiresAfterMs = 0;
    uint32_t respawnsAfterMs = 0;

    bool HasValidSemantics() const
    {
        if (!IsKnownPickupKind(kind) || !IsFinitePickupPosition(position) || modelId < -1 ||
            modelId > MAX_PICKUP_MODEL_ID || reward < 0 || reward > MAX_PICKUP_REWARD ||
            ammo > MAX_PICKUP_AMMO || expiresAfterMs > MAX_PICKUP_EXPIRY_MS ||
            respawnsAfterMs > MAX_PICKUP_RESPAWN_MS)
        {
            return false;
        }

        if (IsCollectiblePickupKind(kind))
        {
            const int16_t maximumIndex = kind == ePickupKind::TAG ? 99 : 49;
            const bool modelIsValid = kind == ePickupKind::TAG ? modelId >= -1 : modelId >= 0;
            return modelIsValid && collectibleIndex >= 0 && collectibleIndex <= maximumIndex &&
                   reward == 0 && ammo == 0 && expiresAfterMs == 0;
        }

        if (collectibleIndex != PICKUP_INVALID_COLLECTIBLE_INDEX || modelId < 0)
        {
            return false;
        }

        switch (kind)
        {
        case ePickupKind::STATIC_WEAPON:
            return IsGrantablePickupWeaponId(reward) && ammo > 0 && expiresAfterMs == 0;
        case ePickupKind::STATIC_ARMOUR:
        case ePickupKind::STATIC_BRIBE:
            return reward == 0 && ammo == 0 && expiresAfterMs == 0;
        case ePickupKind::DROPPED_MONEY:
            return reward > 0 && ammo == 0 && expiresAfterMs > 0 && respawnsAfterMs == 0;
        case ePickupKind::DROPPED_WEAPON:
            return IsGrantablePickupWeaponId(reward) && ammo > 0 && expiresAfterMs > 0 &&
                   respawnsAfterMs == 0;
        case ePickupKind::JETPACK:
            return reward == 0 && ammo == 0 && respawnsAfterMs == 0;
        default:
            return false;
        }
    }

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        if (Stream::IsWriting && !HasValidSemantics())
        {
            return false;
        }

        int kindValue = static_cast<int>(kind);
        int modelValue = modelId;
        int rewardValue = reward;
        int collectibleValue = collectibleIndex;
        serialize_int(stream, kindValue, 0, static_cast<int>(ePickupKind::COUNT) - 1);
        serialize_object(stream, position);
        serialize_uint8(stream, interior);
        serialize_int(stream, modelValue, -1, MAX_PICKUP_MODEL_ID);
        serialize_int(stream, rewardValue, 0, MAX_PICKUP_REWARD);
        serialize_int(stream, ammo, 0, MAX_PICKUP_AMMO);
        serialize_int(stream, collectibleValue, PICKUP_INVALID_COLLECTIBLE_INDEX, 149);
        serialize_int(stream, expiresAfterMs, 0, MAX_PICKUP_EXPIRY_MS);
        serialize_int(stream, respawnsAfterMs, 0, MAX_PICKUP_RESPAWN_MS);

        if (Stream::IsReading)
        {
            kind = static_cast<ePickupKind>(kindValue);
            modelId = static_cast<int16_t>(modelValue);
            reward = rewardValue;
            collectibleIndex = static_cast<int16_t>(collectibleValue);
        }
        return !Stream::IsReading || HasValidSemantics();
    }
};

struct PickupState
{
    PickupId id{};
    uint32_t revision = 0;
    uint8_t authorityPlayerId = PICKUP_INVALID_PLAYER_ID;
    bool active = false;
    // Inactive completion states are retained by the server without granting anything. They either preserve a
    // permanent collectible tombstone or carry the remaining delay for a server-scheduled static respawn.
    bool hasCompletionState = false;
    uint32_t respawnRemainingMs = 0;
    uint8_t creatorPlayerId = PICKUP_INVALID_PLAYER_ID;
    uint32_t sourceIntentRequestId = 0;
    PickupMetadata metadata{};

    bool HasValidSemantics() const
    {
        if (!id.IsValid() || revision == 0 || authorityPlayerId >= Config::MAX_SERVER_PLAYERS)
        {
            return false;
        }
        if (!active && !hasCompletionState)
        {
            return respawnRemainingMs == 0 && creatorPlayerId == PICKUP_INVALID_PLAYER_ID &&
                   sourceIntentRequestId == 0;
        }
        if (!active)
        {
            const bool permanentCollectible = IsCollectiblePickupKind(metadata.kind) &&
                metadata.respawnsAfterMs == 0 && respawnRemainingMs == 0;
            const bool scheduledRespawn = metadata.respawnsAfterMs > 0 && respawnRemainingMs > 0 &&
                respawnRemainingMs <= metadata.respawnsAfterMs;
            return creatorPlayerId == PICKUP_INVALID_PLAYER_ID && sourceIntentRequestId == 0 &&
                   metadata.HasValidSemantics() && (permanentCollectible || scheduledRespawn);
        }
        return !hasCompletionState && respawnRemainingMs == 0 &&
               creatorPlayerId < Config::MAX_SERVER_PLAYERS && metadata.HasValidSemantics();
    }

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        if (Stream::IsWriting && !HasValidSemantics())
        {
            return false;
        }

        serialize_object(stream, id);
        serialize_uint32(stream, revision);
        serialize_int(stream, authorityPlayerId, 0, Config::MAX_SERVER_PLAYERS - 1);
        serialize_bool(stream, active);
        if (active)
        {
            serialize_int(stream, creatorPlayerId, 0, Config::MAX_SERVER_PLAYERS - 1);
            serialize_uint32(stream, sourceIntentRequestId);
            serialize_object(stream, metadata);
            if (Stream::IsReading)
            {
                hasCompletionState = false;
                respawnRemainingMs = 0;
            }
        }
        else
        {
            serialize_bool(stream, hasCompletionState);
            if (hasCompletionState)
            {
                serialize_int(stream, respawnRemainingMs, 0, MAX_PICKUP_RESPAWN_MS);
                serialize_object(stream, metadata);
            }
            else if (Stream::IsReading)
            {
                respawnRemainingMs = 0;
                metadata = {};
            }
            if (Stream::IsReading)
            {
                creatorPlayerId = PICKUP_INVALID_PLAYER_ID;
                sourceIntentRequestId = 0;
            }
        }
        return !Stream::IsReading || HasValidSemantics();
    }
};

class PickupStateEvent : public Packet
{
    DEFINE_PACKET_TYPE(PickupStateEvent, ePacketType::PICKUP_STATE, ePacketChannel::EVENT);

public:
    static constexpr size_t MAX_SERIALIZED_BYTES = 128;
    PickupState state{};

    bool HasValidPayload() const { return state.HasValidSemantics(); }

    size_t MeasureSerializedBytes() const
    {
        PickupStateEvent measured = *this;
        serialize::MeasureStream stream;
        if (!measured.SerializeMeasure(stream))
        {
            return 0;
        }
        return sizeof(uint16_t) + sizeof(server_time_t) + stream.GetBytesProcessed();
    }

    bool FitsSerializedBudget() const
    {
        const size_t bytes = MeasureSerializedBytes();
        return bytes >= sizeof(uint16_t) + sizeof(server_time_t) && bytes <= MAX_SERIALIZED_BYTES;
    }

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        if (Stream::IsWriting && !HasValidPayload())
        {
            return false;
        }
        serialize_object(stream, state);
        return !Stream::IsReading || HasValidPayload();
    }
};

class PickupCollectRequest : public Packet
{
    DEFINE_PACKET_TYPE(PickupCollectRequest, ePacketType::PICKUP_COLLECT_REQUEST, ePacketChannel::EVENT);

public:
    uint32_t requestId = 0;
    SenderPlayerId requesterPlayerId{};
    PickupId id{};
    WorldPositionCompressed requesterPosition{};
    uint8_t interior = 0;

    bool HasValidPayload() const
    {
        return id.IsValid() && IsFinitePickupPosition(requesterPosition);
    }

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        if (Stream::IsWriting && !HasValidPayload())
        {
            return false;
        }
        serialize_uint32(stream, requestId);
        serialize_object(stream, requesterPlayerId);
        serialize_object(stream, id);
        serialize_object(stream, requesterPosition);
        serialize_uint8(stream, interior);
        return !Stream::IsReading || HasValidPayload();
    }
};

class PickupCollectDecision : public Packet
{
    DEFINE_PACKET_TYPE(PickupCollectDecision, ePacketType::PICKUP_COLLECT_DECISION, ePacketChannel::EVENT);

public:
    uint32_t requestId = 0;
    PickupId id{};
    bool approved = false;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        if (Stream::IsWriting && (requestId == 0 || !id.IsValid()))
        {
            return false;
        }
        serialize_uint32(stream, requestId);
        serialize_object(stream, id);
        serialize_bool(stream, approved);
        return !Stream::IsReading || (requestId != 0 && id.IsValid());
    }
};

class PickupCollectResult : public Packet
{
    DEFINE_PACKET_TYPE(PickupCollectResult, ePacketType::PICKUP_COLLECT_RESULT, ePacketChannel::EVENT);

public:
    uint32_t requestId = 0;
    PickupId id{};
    uint8_t collectorPlayerId = PICKUP_INVALID_PLAYER_ID;
    bool approved = false;
    PickupState grantedState{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        const bool valid = requestId != 0 && id.IsValid() &&
            collectorPlayerId < Config::MAX_SERVER_PLAYERS &&
            (!approved || (grantedState.active && grantedState.id == id && grantedState.HasValidSemantics()));
        if (Stream::IsWriting && !valid)
        {
            return false;
        }
        serialize_uint32(stream, requestId);
        serialize_object(stream, id);
        serialize_int(stream, collectorPlayerId, 0, Config::MAX_SERVER_PLAYERS - 1);
        serialize_bool(stream, approved);
        if (approved)
        {
            serialize_object(stream, grantedState);
        }
        else if (Stream::IsReading)
        {
            grantedState = {};
        }
        return !Stream::IsReading || (requestId != 0 && id.IsValid() &&
            collectorPlayerId < Config::MAX_SERVER_PLAYERS &&
            (!approved || (grantedState.active && grantedState.id == id && grantedState.HasValidSemantics())));
    }
};

class PickupCreateIntent : public Packet
{
    DEFINE_PACKET_TYPE(PickupCreateIntent, ePacketType::PICKUP_CREATE_INTENT, ePacketChannel::EVENT);

public:
    uint32_t requestId = 0;
    SenderPlayerId requesterPlayerId{};
    PickupMetadata metadata{};

    bool HasValidPayload() const
    {
        return IsCreationIntentPickupKind(metadata.kind) && metadata.HasValidSemantics();
    }

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        if (Stream::IsWriting && !HasValidPayload())
        {
            return false;
        }
        serialize_uint32(stream, requestId);
        serialize_object(stream, requesterPlayerId);
        serialize_object(stream, metadata);
        return !Stream::IsReading || HasValidPayload();
    }
};

static_assert(PickupStateEvent::MAX_SERIALIZED_BYTES <= 10 * 1024,
    "Pickup state events must fit CPacketFactory's fixed packet buffer");
}  // namespace Packets::Pickups
