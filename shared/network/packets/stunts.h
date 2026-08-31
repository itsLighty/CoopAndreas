#pragma once

#include "config.h"
#include "network/packet.h"
#include "network/serializable_types.h"
#include "serialize.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Packets::Stunts
{
constexpr uint16_t STUNT_JUMP_CAPACITY = 256;
constexpr uint8_t STUNT_INVALID_PLAYER_ID = UINT8_MAX;
constexpr int32_t STUNT_MAX_REWARD = 100000;
constexpr float STUNT_MAX_BOX_EXTENT = 512.0f;
constexpr float STUNT_MAX_REPORTED_SPEED = 8.0f;

inline bool IsStuntRevisionNewer(uint32_t candidate, uint32_t reference)
{
    const uint32_t distance = candidate - reference;
    return distance != 0 && distance < 0x80000000u;
}

inline bool IsFiniteStuntPosition(const WorldPositionCompressed& position)
{
    return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z) &&
           position.x >= -3000.0f && position.x <= 3000.0f && position.y >= -3000.0f &&
           position.y <= 3000.0f && position.z >= -120.0f && position.z <= 1000.0f;
}

inline uint32_t HashStuntWord(uint32_t hash, uint32_t value)
{
    for (uint8_t byteIndex = 0; byteIndex < 4; ++byteIndex)
    {
        hash ^= static_cast<uint8_t>(value >> (byteIndex * 8));
        hash *= 16777619u;
    }
    return hash;
}

inline uint32_t HashStuntFloat(uint32_t hash, float value)
{
    // The wire codec has millimetre precision. Centimetre quantisation makes the stable identity tolerant of
    // the final compression round-trip while still distinguishing distinct map definitions.
    return HashStuntWord(hash, static_cast<uint32_t>(static_cast<int32_t>(std::lround(value * 100.0f))));
}

struct StuntBox
{
    WorldPositionCompressed minimum{};
    WorldPositionCompressed maximum{};

    bool HasValidSemantics() const
    {
        if (!IsFiniteStuntPosition(minimum) || !IsFiniteStuntPosition(maximum) ||
            minimum.x > maximum.x || minimum.y > maximum.y || minimum.z > maximum.z)
        {
            return false;
        }
        return maximum.x - minimum.x <= STUNT_MAX_BOX_EXTENT &&
               maximum.y - minimum.y <= STUNT_MAX_BOX_EXTENT &&
               maximum.z - minimum.z <= STUNT_MAX_BOX_EXTENT;
    }

    bool Contains(const CVector& position, float tolerance = 0.0f) const
    {
        return position.x >= minimum.x - tolerance && position.x <= maximum.x + tolerance &&
               position.y >= minimum.y - tolerance && position.y <= maximum.y + tolerance &&
               position.z >= minimum.z - tolerance && position.z <= maximum.z + tolerance;
    }

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        if (Stream::IsWriting && !HasValidSemantics())
        {
            return false;
        }
        serialize_object(stream, minimum);
        serialize_object(stream, maximum);
        return !Stream::IsReading || HasValidSemantics();
    }
};

struct StuntDefinition
{
    StuntBox start{};
    StuntBox finish{};
    WorldPositionCompressed camera{};
    int32_t reward = 0;

    bool HasValidSemantics() const
    {
        return start.HasValidSemantics() && finish.HasValidSemantics() &&
               IsFiniteStuntPosition(camera) && reward >= 0 && reward <= STUNT_MAX_REWARD;
    }

    uint32_t CalculateFingerprint() const
    {
        uint32_t hash = 2166136261u;
        const WorldPositionCompressed* positions[] = {
            &start.minimum, &start.maximum, &finish.minimum, &finish.maximum, &camera};
        for (const WorldPositionCompressed* position : positions)
        {
            hash = HashStuntFloat(hash, position->x);
            hash = HashStuntFloat(hash, position->y);
            hash = HashStuntFloat(hash, position->z);
        }
        hash = HashStuntWord(hash, static_cast<uint32_t>(reward));
        return hash == 0 ? 1u : hash;
    }

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        if (Stream::IsWriting && !HasValidSemantics())
        {
            return false;
        }
        serialize_object(stream, start);
        serialize_object(stream, finish);
        serialize_object(stream, camera);
        serialize_int(stream, reward, 0, STUNT_MAX_REWARD);
        return !Stream::IsReading || HasValidSemantics();
    }
};

struct StuntId
{
    uint16_t slot = 0;
    uint32_t fingerprint = 0;

    bool IsValid() const { return slot < STUNT_JUMP_CAPACITY && fingerprint != 0; }

    bool operator==(const StuntId& other) const
    {
        return slot == other.slot && fingerprint == other.fingerprint;
    }

    bool operator!=(const StuntId& other) const { return !(*this == other); }

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, slot, 0, STUNT_JUMP_CAPACITY - 1);
        serialize_uint32(stream, fingerprint);
        return !Stream::IsReading || IsValid();
    }
};

inline uint32_t AccumulateCatalogHash(uint32_t hash, const StuntId& id)
{
    hash = HashStuntWord(hash, id.slot);
    return HashStuntWord(hash, id.fingerprint);
}

class StuntDefinitionAnnounce : public Packet
{
    DEFINE_PACKET_TYPE(StuntDefinitionAnnounce, ePacketType::STUNT_DEFINITION, ePacketChannel::EVENT);

public:
    uint16_t catalogCount = 0;
    uint32_t catalogHash = 0;
    StuntId id{};
    StuntDefinition definition{};
    bool initiallyCompleted = false;

    bool HasValidPayload() const
    {
        return catalogCount > 0 && catalogCount <= STUNT_JUMP_CAPACITY && catalogHash != 0 &&
               id.IsValid() && definition.HasValidSemantics() &&
               id.fingerprint == definition.CalculateFingerprint();
    }

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        if (Stream::IsWriting && !HasValidPayload())
        {
            return false;
        }
        serialize_int(stream, catalogCount, 1, STUNT_JUMP_CAPACITY);
        serialize_uint32(stream, catalogHash);
        serialize_object(stream, id);
        serialize_object(stream, definition);
        serialize_bool(stream, initiallyCompleted);
        return !Stream::IsReading || HasValidPayload();
    }
};

enum class eStuntAttemptAction : uint8_t
{
    START = 0,
    HIT_FINISH,
    CANCEL,
    COMPLETE,
    COUNT
};

enum class eStuntAttemptResultReason : uint8_t
{
    NONE = 0,
    RATE_LIMITED,
    CATALOG_NOT_READY,
    INVALID_STUNT,
    ALREADY_COMPLETED,
    DRIVER_SNAPSHOT_NOT_READY,
    OUT_OF_RANGE,
    INVALID_TRANSITION,
    TIMEOUT,
    COUNT
};

class StuntAttempt : public Packet
{
    DEFINE_PACKET_TYPE(StuntAttempt, ePacketType::STUNT_ATTEMPT, ePacketChannel::EVENT);

public:
    uint32_t requestId = 0;
    SenderPlayerId playerId{};
    uint64_t clientSessionNonce = 0;
    eStuntAttemptAction action = eStuntAttemptAction::START;
    StuntId id{};
    uint16_t vehicleId = 0;
    WorldPositionCompressed position{};
    MoveSpeedCompressed moveSpeed{};

    bool HasValidPayload() const
    {
        const float speedSquared = moveSpeed.x * moveSpeed.x + moveSpeed.y * moveSpeed.y +
                                   moveSpeed.z * moveSpeed.z;
        return requestId != 0 && clientSessionNonce != 0 &&
               static_cast<uint8_t>(action) < static_cast<uint8_t>(eStuntAttemptAction::COUNT) &&
               id.IsValid() && vehicleId < Config::MAX_SERVER_VEHICLES &&
               IsFiniteStuntPosition(position) && std::isfinite(speedSquared) &&
               speedSquared <= STUNT_MAX_REPORTED_SPEED * STUNT_MAX_REPORTED_SPEED;
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
        serialize_object(stream, playerId);
        serialize_uint64(stream, clientSessionNonce);
        int actionValue = static_cast<int>(action);
        serialize_int(stream, actionValue, 0, static_cast<int>(eStuntAttemptAction::COUNT) - 1);
        serialize_object(stream, id);
        serialize_int(stream, vehicleId, 0, Config::MAX_SERVER_VEHICLES - 1);
        serialize_object(stream, position);
        serialize_object(stream, moveSpeed);
        if (Stream::IsReading)
        {
            action = static_cast<eStuntAttemptAction>(actionValue);
        }
        return !Stream::IsReading || HasValidPayload();
    }
};

class StuntAttemptResult : public Packet
{
    DEFINE_PACKET_TYPE(StuntAttemptResult, ePacketType::STUNT_ATTEMPT_RESULT, ePacketChannel::EVENT);

public:
    static constexpr uint16_t MAX_RETRY_AFTER_MS = 1000;

    uint32_t requestId = 0;
    uint64_t clientSessionNonce = 0;
    eStuntAttemptAction action = eStuntAttemptAction::START;
    StuntId id{};
    bool accepted = false;
    eStuntAttemptResultReason reason = eStuntAttemptResultReason::INVALID_TRANSITION;
    uint16_t retryAfterMs = 0;

    bool HasValidPayload() const
    {
        const bool validAction =
            static_cast<uint8_t>(action) < static_cast<uint8_t>(eStuntAttemptAction::COUNT);
        const bool validReason =
            static_cast<uint8_t>(reason) < static_cast<uint8_t>(eStuntAttemptResultReason::COUNT);
        return requestId != 0 && clientSessionNonce != 0 && id.IsValid() && validAction && validReason &&
               retryAfterMs <= MAX_RETRY_AFTER_MS &&
               ((accepted && reason == eStuntAttemptResultReason::NONE && retryAfterMs == 0) ||
                   (!accepted && reason != eStuntAttemptResultReason::NONE));
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
        serialize_uint64(stream, clientSessionNonce);
        int actionValue = static_cast<int>(action);
        serialize_int(stream, actionValue, 0, static_cast<int>(eStuntAttemptAction::COUNT) - 1);
        serialize_object(stream, id);
        serialize_bool(stream, accepted);
        int reasonValue = static_cast<int>(reason);
        serialize_int(stream, reasonValue, 0, static_cast<int>(eStuntAttemptResultReason::COUNT) - 1);
        serialize_int(stream, retryAfterMs, 0, MAX_RETRY_AFTER_MS);
        if (Stream::IsReading)
        {
            action = static_cast<eStuntAttemptAction>(actionValue);
            reason = static_cast<eStuntAttemptResultReason>(reasonValue);
        }
        return !Stream::IsReading || HasValidPayload();
    }
};

class StuntStateEvent : public Packet
{
    DEFINE_PACKET_TYPE(StuntStateEvent, ePacketType::STUNT_STATE, ePacketChannel::EVENT);

public:
    static constexpr size_t MAX_SERIALIZED_BYTES = 256;

    uint64_t serverRunId = 0;
    uint16_t catalogCount = 0;
    uint32_t catalogHash = 0;
    uint32_t revision = 0;
    uint8_t authorityPlayerId = STUNT_INVALID_PLAYER_ID;
    StuntId id{};
    StuntDefinition definition{};
    bool completed = false;
    uint8_t completedByPlayerId = STUNT_INVALID_PLAYER_ID;
    uint64_t collectorSessionNonce = 0;
    uint32_t awardSequence = 0;
    int32_t rewardAmount = 0;
    bool allCompleted = false;

    bool HasValidPayload() const
    {
        if (serverRunId == 0 || catalogCount == 0 || catalogCount > STUNT_JUMP_CAPACITY ||
            catalogHash == 0 || revision == 0 || authorityPlayerId >= Config::MAX_SERVER_PLAYERS ||
            !id.IsValid() || !definition.HasValidSemantics() ||
            id.fingerprint != definition.CalculateFingerprint())
        {
            return false;
        }

        const bool hasAward = completedByPlayerId < Config::MAX_SERVER_PLAYERS;
        if (!completed)
        {
            return !hasAward && collectorSessionNonce == 0 && awardSequence == 0 &&
                   rewardAmount == 0 && !allCompleted;
        }
        if (!hasAward)
        {
            // A completed host save has no network collector and must never grant a replay reward.
            return collectorSessionNonce == 0 && awardSequence == 0 && rewardAmount == 0 && !allCompleted;
        }
        return collectorSessionNonce != 0 && awardSequence != 0 && rewardAmount >= 0 &&
               rewardAmount <= STUNT_MAX_REWARD;
    }

    size_t MeasureSerializedBytes() const
    {
        StuntStateEvent measured = *this;
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
        serialize_uint64(stream, serverRunId);
        serialize_int(stream, catalogCount, 1, STUNT_JUMP_CAPACITY);
        serialize_uint32(stream, catalogHash);
        serialize_uint32(stream, revision);
        serialize_int(stream, authorityPlayerId, 0, Config::MAX_SERVER_PLAYERS - 1);
        serialize_object(stream, id);
        serialize_object(stream, definition);
        serialize_bool(stream, completed);
        if (completed)
        {
            bool hasAward = completedByPlayerId < Config::MAX_SERVER_PLAYERS;
            serialize_bool(stream, hasAward);
            if (hasAward)
            {
                serialize_int(stream, completedByPlayerId, 0, Config::MAX_SERVER_PLAYERS - 1);
                serialize_uint64(stream, collectorSessionNonce);
                serialize_uint32(stream, awardSequence);
                serialize_int(stream, rewardAmount, 0, STUNT_MAX_REWARD);
                serialize_bool(stream, allCompleted);
            }
            else if (Stream::IsReading)
            {
                completedByPlayerId = STUNT_INVALID_PLAYER_ID;
                collectorSessionNonce = 0;
                awardSequence = 0;
                rewardAmount = 0;
                allCompleted = false;
            }
        }
        else if (Stream::IsReading)
        {
            completedByPlayerId = STUNT_INVALID_PLAYER_ID;
            collectorSessionNonce = 0;
            awardSequence = 0;
            rewardAmount = 0;
            allCompleted = false;
        }
        return !Stream::IsReading || HasValidPayload();
    }
};

static_assert(StuntStateEvent::MAX_SERIALIZED_BYTES <= 10 * 1024,
    "Stunt state events must fit CPacketFactory's fixed packet buffer");
}  // namespace Packets::Stunts
