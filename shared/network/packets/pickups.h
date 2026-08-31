#pragma once

#include "config.h"
#include "network/packet.h"
#include "network/serializable_types.h"
#include "serialize.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Packets::Pickups
{
constexpr uint16_t MAX_PICKUPS = 620;
constexpr uint16_t MAX_PICKUP_MODEL_ID = 19999;
constexpr uint8_t MIN_PICKUP_TYPE = 1;
constexpr uint8_t MAX_PICKUP_TYPE = 22;
constexpr uint32_t MAX_PICKUP_AMMO_OR_MONEY = 1000000;
constexpr uint32_t MAX_REGENERATION_MS = 86400000;
constexpr uint8_t COLLECTIBLE_KIND_COUNT = 4;
constexpr uint8_t MAX_COLLECTIBLE_PROGRESS = 100;
constexpr uint8_t MAX_SNAPSHOT_ENTRIES = 64;
constexpr uint8_t MAX_SNAPSHOT_CHUNKS = (MAX_PICKUPS + MAX_SNAPSHOT_ENTRIES - 1) / MAX_SNAPSHOT_ENTRIES;
constexpr size_t MAX_SNAPSHOT_BYTES = 8 * 1024;
constexpr size_t MAX_SNAPSHOT_TOTAL_BYTES = 24 * 1024;
constexpr float MAX_PICKUP_REQUEST_DISTANCE = 6.0f;
constexpr float MAX_SNAPSHOT_REQUEST_DISTANCE = 100.0f;
constexpr uint8_t MAX_COLLECTION_REQUESTS_PER_SECOND = 8;
constexpr uint32_t COLLECTION_REQUEST_WINDOW_MS = 1000;
constexpr uint32_t COLLECTION_REQUEST_TIMEOUT_MS = 2500;

inline bool IsRevisionNewer(uint32_t candidate, uint32_t reference)
{
    const uint32_t distance = candidate - reference;
    return distance != 0 && distance < 0x80000000u;
}

struct PickupIdentity
{
    uint16_t slot = 0;
    uint16_t generation = 0;

    bool IsValid() const { return slot < MAX_PICKUPS && generation != 0; }

    bool operator==(const PickupIdentity& other) const
    {
        return slot == other.slot && generation == other.generation;
    }

    bool operator!=(const PickupIdentity& other) const { return !(*this == other); }

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, slot, 0, MAX_PICKUPS - 1);
        serialize_int(stream, generation, 1, UINT16_MAX);
        return !Stream::IsReading || IsValid();
    }
};

enum class ePickupLifecycle : uint8_t
{
    ACTIVE = 0,
    DISABLED,
    REMOVED
};

enum class ePickupRemovalReason : uint8_t
{
    COLLECTED = 0,
    EXPIRED,
    SCRIPT,
    REPLACED,
    SESSION_RESET
};

enum class eCollectibleKind : uint8_t
{
    NONE = 0,
    HORSESHOE,
    SNAPSHOT,
    OYSTER
};

struct PickupDescriptor
{
    PickupIdentity identity{};
    uint32_t authorityEpoch = 0;
    uint32_t revision = 0;
    WorldPositionCompressed position{};
    uint16_t modelId = 0;
    uint8_t pickupType = 0;
    uint32_t ammoOrMoney = 0;
    uint16_t moneyPerDay = 0;
    uint32_t regenerationRemainingMs = 0;
    float revenueValue = 0.0f;
    uint8_t areaCode = 0;
    ePickupLifecycle lifecycle = ePickupLifecycle::ACTIVE;
    bool empty = false;
    bool visible = true;

    bool HasFiniteBoundedPosition() const
    {
        return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z) &&
               position.x >= -3000.0f && position.x <= 3000.0f &&
               position.y >= -3000.0f && position.y <= 3000.0f &&
               position.z >= -120.0f && position.z <= 1000.0f;
    }

    bool HasValidState() const
    {
        const int lifecycleValue = static_cast<int>(lifecycle);
        return identity.IsValid() && authorityEpoch != 0 && revision != 0 &&
               HasFiniteBoundedPosition() && modelId > 0 && modelId <= MAX_PICKUP_MODEL_ID &&
               pickupType >= MIN_PICKUP_TYPE && pickupType <= MAX_PICKUP_TYPE &&
               ammoOrMoney <= MAX_PICKUP_AMMO_OR_MONEY &&
               regenerationRemainingMs <= MAX_REGENERATION_MS &&
               std::isfinite(revenueValue) && revenueValue >= 0.0f &&
               revenueValue <= static_cast<float>(MAX_PICKUP_AMMO_OR_MONEY) &&
               lifecycleValue >= static_cast<int>(ePickupLifecycle::ACTIVE) &&
               lifecycleValue <= static_cast<int>(ePickupLifecycle::REMOVED) &&
               (lifecycle != ePickupLifecycle::ACTIVE || regenerationRemainingMs == 0) &&
               (lifecycle != ePickupLifecycle::REMOVED ||
                   (regenerationRemainingMs == 0 && revenueValue == 0.0f));
    }

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        if (Stream::IsWriting && !HasValidState())
        {
            return false;
        }

        serialize_object(stream, identity);
        serialize_uint32(stream, authorityEpoch);
        serialize_uint32(stream, revision);
        serialize_object(stream, position);
        serialize_int(stream, modelId, 1, MAX_PICKUP_MODEL_ID);
        serialize_int(stream, pickupType, MIN_PICKUP_TYPE, MAX_PICKUP_TYPE);
        serialize_int(stream, ammoOrMoney, 0, MAX_PICKUP_AMMO_OR_MONEY);
        serialize_uint16(stream, moneyPerDay);
        serialize_int(stream, regenerationRemainingMs, 0, MAX_REGENERATION_MS);
        serialize_float(stream, revenueValue);
        serialize_uint8(stream, areaCode);
        int lifecycleValue = static_cast<int>(lifecycle);
        serialize_int(stream, lifecycleValue, static_cast<int>(ePickupLifecycle::ACTIVE),
            static_cast<int>(ePickupLifecycle::REMOVED));
        if (Stream::IsReading)
        {
            lifecycle = static_cast<ePickupLifecycle>(lifecycleValue);
        }
        serialize_bool(stream, empty);
        serialize_bool(stream, visible);
        return !Stream::IsReading || HasValidState();
    }
};

inline eCollectibleKind GetCollectibleKind(const PickupDescriptor& pickup)
{
    // Stock San Andreas model IDs: horseshoe 954, camera/snapshot 1253, oysters 953 and 2782.
    if (pickup.modelId == 954)
        return eCollectibleKind::HORSESHOE;
    if (pickup.pickupType == 20 || pickup.modelId == 1253)
        return eCollectibleKind::SNAPSHOT;
    if (pickup.modelId == 953 || pickup.modelId == 2782)
        return eCollectibleKind::OYSTER;
    return eCollectibleKind::NONE;
}

class PickupSpawn : public Packet
{
    DEFINE_PACKET_TYPE(PickupSpawn, ePacketType::PICKUP_SPAWN, ePacketChannel::EVENT);

public:
    PickupDescriptor pickup{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, pickup);
        return true;
    }
};

class PickupState : public Packet
{
    DEFINE_PACKET_TYPE(PickupState, ePacketType::PICKUP_STATE, ePacketChannel::EVENT);

public:
    PickupDescriptor pickup{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, pickup);
        return true;
    }
};

class PickupRemove : public Packet
{
    DEFINE_PACKET_TYPE(PickupRemove, ePacketType::PICKUP_REMOVE, ePacketChannel::EVENT);

public:
    PickupIdentity identity{};
    uint32_t authorityEpoch = 0;
    uint32_t observedRevision = 0;
    ePickupRemovalReason reason = ePickupRemovalReason::SCRIPT;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, identity);
        serialize_uint32(stream, authorityEpoch);
        serialize_uint32(stream, observedRevision);
        int reasonValue = static_cast<int>(reason);
        serialize_int(stream, reasonValue, static_cast<int>(ePickupRemovalReason::COLLECTED),
            static_cast<int>(ePickupRemovalReason::SESSION_RESET));
        if (Stream::IsReading)
            reason = static_cast<ePickupRemovalReason>(reasonValue);
        return identity.IsValid() && authorityEpoch != 0 && observedRevision != 0;
    }
};

class PickupCollectRequest : public Packet
{
    DEFINE_PACKET_TYPE(PickupCollectRequest, ePacketType::PICKUP_COLLECT_REQUEST, ePacketChannel::EVENT);

public:
    PickupIdentity identity{};
    uint32_t authorityEpoch = 0;
    uint32_t observedRevision = 0;
    uint32_t requestNonce = 0;
    WorldPositionCompressed claimantPosition{};
    uint8_t areaCode = 0;
    bool cameraAttempt = false;

    bool HasValidClaim() const
    {
        return std::isfinite(claimantPosition.x) && std::isfinite(claimantPosition.y) &&
               std::isfinite(claimantPosition.z) && claimantPosition.x >= -3000.0f &&
               claimantPosition.x <= 3000.0f && claimantPosition.y >= -3000.0f &&
               claimantPosition.y <= 3000.0f && claimantPosition.z >= -120.0f &&
               claimantPosition.z <= 1000.0f;
    }

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, identity);
        serialize_uint32(stream, authorityEpoch);
        serialize_uint32(stream, observedRevision);
        serialize_uint32(stream, requestNonce);
        serialize_object(stream, claimantPosition);
        serialize_uint8(stream, areaCode);
        serialize_bool(stream, cameraAttempt);
        return identity.IsValid() && authorityEpoch != 0 && observedRevision != 0 && requestNonce != 0 &&
               HasValidClaim();
    }
};

class PickupCollectForward : public Packet
{
    DEFINE_PACKET_TYPE(PickupCollectForward, ePacketType::PICKUP_COLLECT_FORWARD, ePacketChannel::EVENT);

public:
    PickupIdentity identity{};
    uint32_t authorityEpoch = 0;
    uint32_t observedRevision = 0;
    uint32_t requestNonce = 0;
    uint8_t claimantPlayerId = 0;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, identity);
        serialize_uint32(stream, authorityEpoch);
        serialize_uint32(stream, observedRevision);
        serialize_uint32(stream, requestNonce);
        serialize_int(stream, claimantPlayerId, 0, Config::MAX_SERVER_PLAYERS - 1);
        return identity.IsValid() && authorityEpoch != 0 && observedRevision != 0 && requestNonce != 0;
    }
};

class PickupCollectDecision : public Packet
{
    DEFINE_PACKET_TYPE(PickupCollectDecision, ePacketType::PICKUP_COLLECT_DECISION, ePacketChannel::EVENT);

public:
    PickupIdentity identity{};
    uint32_t authorityEpoch = 0;
    uint32_t observedRevision = 0;
    uint32_t requestNonce = 0;  // zero is reserved for the host's own native collection
    uint8_t claimantPlayerId = 0;
    bool accepted = false;
    PickupDescriptor resolvedPickup{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, identity);
        serialize_uint32(stream, authorityEpoch);
        serialize_uint32(stream, observedRevision);
        serialize_uint32(stream, requestNonce);
        serialize_int(stream, claimantPlayerId, 0, Config::MAX_SERVER_PLAYERS - 1);
        serialize_bool(stream, accepted);
        if (accepted)
            serialize_object(stream, resolvedPickup);
        return identity.IsValid() && authorityEpoch != 0 && observedRevision != 0 &&
               (!accepted || (resolvedPickup.HasValidState() && resolvedPickup.identity == identity &&
                   resolvedPickup.authorityEpoch == authorityEpoch &&
                   resolvedPickup.revision == observedRevision));
    }
};

class PickupCollectResult : public Packet
{
    DEFINE_PACKET_TYPE(PickupCollectResult, ePacketType::PICKUP_COLLECT_RESULT, ePacketChannel::EVENT);

public:
    PickupDescriptor pickup{};
    uint32_t requestNonce = 0;
    uint8_t claimantPlayerId = 0;
    bool accepted = false;
    eCollectibleKind collectibleKind = eCollectibleKind::NONE;
    uint8_t collectibleProgress = 0;

    bool HasValidState() const
    {
        const int collectibleValue = static_cast<int>(collectibleKind);
        return pickup.HasValidState() &&
               claimantPlayerId < Config::MAX_SERVER_PLAYERS &&
               collectibleValue >= static_cast<int>(eCollectibleKind::NONE) &&
               collectibleValue <= static_cast<int>(eCollectibleKind::OYSTER) &&
               collectibleProgress <= MAX_COLLECTIBLE_PROGRESS &&
               (accepted || (collectibleKind == eCollectibleKind::NONE && collectibleProgress == 0)) &&
               (collectibleKind != eCollectibleKind::NONE || collectibleProgress == 0);
    }

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        if (Stream::IsWriting && !HasValidState())
            return false;
        serialize_object(stream, pickup);
        serialize_uint32(stream, requestNonce);
        serialize_int(stream, claimantPlayerId, 0, Config::MAX_SERVER_PLAYERS - 1);
        serialize_bool(stream, accepted);
        int collectibleValue = static_cast<int>(collectibleKind);
        serialize_int(stream, collectibleValue, static_cast<int>(eCollectibleKind::NONE),
            static_cast<int>(eCollectibleKind::OYSTER));
        serialize_int(stream, collectibleProgress, 0, MAX_COLLECTIBLE_PROGRESS);
        if (Stream::IsReading)
        {
            collectibleKind = static_cast<eCollectibleKind>(collectibleValue);
        }
        return !Stream::IsReading || HasValidState();
    }
};

class PickupSnapshotChunk : public Packet
{
    DEFINE_PACKET_TYPE(PickupSnapshotChunk, ePacketType::PICKUP_SNAPSHOT_CHUNK, ePacketChannel::EVENT);

public:
    uint32_t authorityEpoch = 0;
    uint32_t snapshotRevision = 0;
    uint8_t authorityPlayerId = 0;
    uint8_t chunkIndex = 0;
    uint8_t chunkCount = 1;
    uint8_t entryCount = 0;
    std::array<uint8_t, COLLECTIBLE_KIND_COUNT> collectibleProgress{};
    std::array<PickupDescriptor, MAX_SNAPSHOT_ENTRIES> entries{};

    bool HasValidState() const
    {
        if (authorityEpoch == 0 || snapshotRevision == 0 || authorityPlayerId >= Config::MAX_SERVER_PLAYERS ||
            chunkCount == 0 ||
            chunkCount > MAX_SNAPSHOT_CHUNKS || chunkIndex >= chunkCount ||
            entryCount > MAX_SNAPSHOT_ENTRIES)
        {
            return false;
        }
        for (uint8_t index = 0; index < COLLECTIBLE_KIND_COUNT; ++index)
        {
            if (collectibleProgress[index] > MAX_COLLECTIBLE_PROGRESS)
                return false;
        }
        for (uint8_t index = 0; index < entryCount; ++index)
        {
            if (!entries[index].HasValidState() || entries[index].authorityEpoch != authorityEpoch)
            {
                return false;
            }
            for (uint8_t other = 0; other < index; ++other)
            {
                if (entries[other].identity.slot == entries[index].identity.slot)
                    return false;
            }
        }
        return true;
    }

    size_t MeasureSerializedBytes() const
    {
        PickupSnapshotChunk measured = *this;
        serialize::MeasureStream stream;
        if (!measured.SerializeMeasure(stream))
            return 0;
        return sizeof(uint16_t) + sizeof(server_time_t) + stream.GetBytesProcessed();
    }

    bool FitsSerializedBudget() const
    {
        const size_t measuredBytes = MeasureSerializedBytes();
        return measuredBytes > 0 && measuredBytes <= MAX_SNAPSHOT_BYTES;
    }

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        if (Stream::IsWriting && !HasValidState())
            return false;
        serialize_uint32(stream, authorityEpoch);
        serialize_uint32(stream, snapshotRevision);
        serialize_int(stream, authorityPlayerId, 0, Config::MAX_SERVER_PLAYERS - 1);
        serialize_int(stream, chunkIndex, 0, MAX_SNAPSHOT_CHUNKS - 1);
        serialize_int(stream, chunkCount, 1, MAX_SNAPSHOT_CHUNKS);
        serialize_int(stream, entryCount, 0, MAX_SNAPSHOT_ENTRIES);
        for (uint8_t index = 0; index < COLLECTIBLE_KIND_COUNT; ++index)
            serialize_int(stream, collectibleProgress[index], 0, MAX_COLLECTIBLE_PROGRESS);
        for (uint8_t index = 0; index < entryCount; ++index)
            serialize_object(stream, entries[index]);
        return !Stream::IsReading || HasValidState();
    }
};

static_assert(MAX_SNAPSHOT_BYTES <= 10 * 1024, "Pickup snapshots must fit CPacketFactory's fixed packet buffer");
static_assert(MAX_SNAPSHOT_TOTAL_BYTES <= 24 * 1024, "A complete pickup snapshot must have a fixed byte ceiling");
static_assert(MAX_SNAPSHOT_ENTRIES * MAX_SNAPSHOT_CHUNKS >= MAX_PICKUPS,
    "Pickup snapshot chunking must cover the complete native pool");
}  // namespace Packets::Pickups
