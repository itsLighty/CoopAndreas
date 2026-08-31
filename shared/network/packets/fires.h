#pragma once

#include "config.h"
#include "network/packet.h"
#include "network/serializable_types.h"
#include "serialize.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Packets::Fires
{
constexpr uint8_t FIRE_SLOT_CAPACITY = 64;
constexpr uint8_t FIRE_INVALID_PLAYER_ID = UINT8_MAX;
constexpr uint16_t FIRE_INVALID_ATTACHMENT_ID = UINT16_MAX;
constexpr uint32_t FIRE_MIN_LIFETIME_MS = 250;
constexpr uint32_t FIRE_MAX_LIFETIME_MS = 600000;
constexpr float FIRE_MAX_STRENGTH = 100.0f;
constexpr int8_t FIRE_MAX_GENERATIONS = 127;
constexpr float FIRE_MAX_EXTINGUISH_DISTANCE = 30.0f;

inline bool IsFireSerialNewer(uint32_t candidate, uint32_t reference)
{
    const uint32_t distance = candidate - reference;
    return distance != 0 && distance < 0x80000000u;
}

inline bool IsFiniteFirePosition(const WorldPositionCompressed& position)
{
    return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z) &&
           position.x >= -3000.0f && position.x <= 3000.0f && position.y >= -3000.0f &&
           position.y <= 3000.0f && position.z >= -120.0f && position.z <= 1000.0f;
}

enum class eFireAttachmentType : uint8_t
{
    WORLD = 0,
    PLAYER,
    PED,
    VEHICLE,
    COUNT
};

struct FireId
{
    uint8_t slot = 0;
    uint32_t generation = 0;

    bool IsValid() const { return slot < FIRE_SLOT_CAPACITY && generation != 0; }
    bool operator==(const FireId& other) const
    {
        return slot == other.slot && generation == other.generation;
    }
    bool operator!=(const FireId& other) const { return !(*this == other); }

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, slot, 0, FIRE_SLOT_CAPACITY - 1);
        serialize_uint32(stream, generation);
        return !Stream::IsReading || IsValid();
    }
};

struct FireDescriptor
{
    WorldPositionCompressed fallbackPosition{};
    eFireAttachmentType attachmentType = eFireAttachmentType::WORLD;
    uint16_t attachmentId = FIRE_INVALID_ATTACHMENT_ID;
    uint8_t area = AREA_MAIN_MAP;
    float strength = 1.0f;
    uint32_t remainingLifetimeMs = 30000;
    int8_t generationsAllowed = 0;
    uint8_t removalDistance = 60;
    bool createdByScript = false;
    bool makesNoise = true;

    bool HasValidSemantics() const
    {
        if (!IsFiniteFirePosition(fallbackPosition) || !std::isfinite(strength) || strength < 0.0f ||
            strength > FIRE_MAX_STRENGTH || area >= MAX_VISIBLE_AREAS ||
            remainingLifetimeMs < FIRE_MIN_LIFETIME_MS ||
            remainingLifetimeMs > FIRE_MAX_LIFETIME_MS || generationsAllowed < 0 ||
            generationsAllowed > FIRE_MAX_GENERATIONS ||
            static_cast<uint8_t>(attachmentType) >= static_cast<uint8_t>(eFireAttachmentType::COUNT))
        {
            return false;
        }
        switch (attachmentType)
        {
        case eFireAttachmentType::WORLD:
            return attachmentId == FIRE_INVALID_ATTACHMENT_ID;
        case eFireAttachmentType::PLAYER:
            return attachmentId < Config::MAX_SERVER_PLAYERS;
        case eFireAttachmentType::PED:
            return attachmentId < Config::MAX_SERVER_PEDS;
        case eFireAttachmentType::VEHICLE:
            return attachmentId < Config::MAX_SERVER_VEHICLES;
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
        serialize_object(stream, fallbackPosition);
        int attachment = static_cast<int>(attachmentType);
        serialize_int(stream, attachment, 0, static_cast<int>(eFireAttachmentType::COUNT) - 1);
        if (Stream::IsReading)
        {
            attachmentType = static_cast<eFireAttachmentType>(attachment);
        }
        if (attachmentType != eFireAttachmentType::WORLD)
        {
            int maxAttachmentId = Config::MAX_SERVER_VEHICLES - 1;
            if (attachmentType == eFireAttachmentType::PLAYER)
                maxAttachmentId = Config::MAX_SERVER_PLAYERS - 1;
            else if (attachmentType == eFireAttachmentType::PED)
                maxAttachmentId = Config::MAX_SERVER_PEDS - 1;
            serialize_int(stream, attachmentId, 0, maxAttachmentId);
        }
        else if (Stream::IsReading)
        {
            attachmentId = FIRE_INVALID_ATTACHMENT_ID;
        }
        serialize_int(stream, area, AREA_MAIN_MAP, MAX_VISIBLE_AREAS - 1);
        serialize_compressed_float(stream, strength, 0.0f, FIRE_MAX_STRENGTH, 0.01f);
        serialize_int(stream, remainingLifetimeMs, FIRE_MIN_LIFETIME_MS, FIRE_MAX_LIFETIME_MS);
        serialize_int(stream, generationsAllowed, 0, FIRE_MAX_GENERATIONS);
        serialize_uint8(stream, removalDistance);
        serialize_bool(stream, createdByScript);
        serialize_bool(stream, makesNoise);
        return !Stream::IsReading || HasValidSemantics();
    }
};

enum class eFireMutation : uint8_t
{
    UPSERT = 0,
    EXTINGUISH,
    COUNT
};

class FireStateIntent : public Packet
{
    DEFINE_PACKET_TYPE(FireStateIntent, ePacketType::FIRE_STATE_INTENT, ePacketChannel::EVENT);

public:
    uint32_t authoritySequence = 0;
    eFireMutation mutation = eFireMutation::UPSERT;
    FireId id{};
    FireDescriptor descriptor{};

    bool HasValidPayload() const
    {
        return authoritySequence != 0 &&
               static_cast<uint8_t>(mutation) < static_cast<uint8_t>(eFireMutation::COUNT) &&
               id.IsValid() && descriptor.HasValidSemantics();
    }

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        if (Stream::IsWriting && !HasValidPayload())
            return false;
        serialize_uint32(stream, authoritySequence);
        int value = static_cast<int>(mutation);
        serialize_int(stream, value, 0, static_cast<int>(eFireMutation::COUNT) - 1);
        if (Stream::IsReading)
            mutation = static_cast<eFireMutation>(value);
        serialize_object(stream, id);
        serialize_object(stream, descriptor);
        return !Stream::IsReading || HasValidPayload();
    }
};

class FireStateEvent : public Packet
{
    DEFINE_PACKET_TYPE(FireStateEvent, ePacketType::FIRE_STATE, ePacketChannel::EVENT);

public:
    static constexpr size_t MAX_SERIALIZED_BYTES = 192;

    uint64_t serverRunId = 0;
    uint32_t revision = 0;
    uint8_t authorityPlayerId = FIRE_INVALID_PLAYER_ID;
    FireId id{};
    bool active = false;
    FireDescriptor descriptor{};

    bool HasValidPayload() const
    {
        return serverRunId != 0 && revision != 0 && authorityPlayerId < Config::MAX_SERVER_PLAYERS &&
               id.IsValid() && descriptor.HasValidSemantics();
    }

    size_t MeasureSerializedBytes() const
    {
        FireStateEvent measured = *this;
        serialize::MeasureStream stream;
        if (!measured.SerializeMeasure(stream))
            return 0;
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
            return false;
        serialize_uint64(stream, serverRunId);
        serialize_uint32(stream, revision);
        serialize_int(stream, authorityPlayerId, 0, Config::MAX_SERVER_PLAYERS - 1);
        serialize_object(stream, id);
        serialize_bool(stream, active);
        serialize_object(stream, descriptor);
        return !Stream::IsReading || HasValidPayload();
    }
};

class FireExtinguishRequest : public Packet
{
    DEFINE_PACKET_TYPE(FireExtinguishRequest, ePacketType::FIRE_EXTINGUISH_REQUEST, ePacketChannel::EVENT);

public:
    uint32_t requestId = 0;
    SenderPlayerId requesterPlayerId{};
    FireId id{};
    WorldPositionCompressed requesterPosition{};

    bool HasValidPayload() const
    {
        return requestId != 0 && id.IsValid() && IsFiniteFirePosition(requesterPosition);
    }

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        if (Stream::IsWriting && !HasValidPayload())
            return false;
        serialize_uint32(stream, requestId);
        serialize_object(stream, requesterPlayerId);
        serialize_object(stream, id);
        serialize_object(stream, requesterPosition);
        return !Stream::IsReading || HasValidPayload();
    }
};

static_assert(FireStateEvent::MAX_SERIALIZED_BYTES <= 10 * 1024,
    "Fire state events must fit CPacketFactory's fixed packet buffer");
}  // namespace Packets::Fires
