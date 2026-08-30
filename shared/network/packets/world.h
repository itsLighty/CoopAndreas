#pragma once
#include "CWeather.h"
#include <CExplosion.h>
#include <array>
#include <cmath>
#include <cstring>

namespace Packets::World
{
// TODO: proper weather sync
class GameWeatherTime : public Packet
{
    DEFINE_PACKET_TYPE(GameWeatherTime, ePacketType::GAME_WEATHER_TIME, ePacketChannel::EVENT);

public:
    eWeatherType newWeather;
    eWeatherType oldWeather;
    // eWeatherType forcedWeather;
    uint8_t currentMonth;
    uint8_t currentDay;
    uint8_t currentHour;
    uint8_t currentMinute;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        if (Stream::IsWriting)
        {
            newWeather = VCLAMP(newWeather, WEATHER_EXTRASUNNY_LA, WEATHER_EXTRACOLOURS_2);
            oldWeather = VCLAMP(oldWeather, WEATHER_EXTRASUNNY_LA, WEATHER_EXTRACOLOURS_2);
            // forcedWeather = VCLAMP(forcedWeather, WEATHER_EXTRASUNNY_LA, WEATHER_EXTRACOLOURS_2);
            currentMonth = VCLAMP(currentMonth, 1, 12);
            currentDay = VCLAMP(currentDay, 1, 31);
            currentHour = VCLAMP(currentHour, 0, 23);
            currentMinute = VCLAMP(currentMinute, 0, 59);
        }
        serialize_int(stream, (int&)newWeather, WEATHER_EXTRASUNNY_LA, WEATHER_EXTRACOLOURS_2);
        serialize_int(stream, (int&)oldWeather, WEATHER_EXTRASUNNY_LA, WEATHER_EXTRACOLOURS_2);
        // serialize_int(stream, (int&)forcedWeather, WEATHER_EXTRASUNNY_LA, WEATHER_EXTRACOLOURS_2);

        serialize_int(stream, currentMonth, 1, 12);
        serialize_int(stream, currentDay, 1, 31);
        serialize_int(stream, currentHour, 0, 23);
        serialize_int(stream, currentMinute, 0, 59);

        return true;
    }
};

// unused for now
class AddExplosion : public Packet
{
    DEFINE_PACKET_TYPE(AddExplosion, ePacketType::ADD_EXPLOSION, ePacketChannel::EVENT);

public:
    eExplosionType type;
    WorldPositionCompressed pos;
    int time;
    bool usesSound;
    float cameraShake;
    bool isVisible;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, (int&)type, EXPLOSION_GRENADE, EXPLOSION_RC_VEHICLE);
        serialize_object(stream, pos);
        serialize_int(stream, time, 0, 100);
        serialize_bool(stream, usesSound);
        serialize_compressed_float(stream, cameraShake, -1.0f, 1.0f, 0.01f);
        serialize_bool(stream, isVisible);

        return true;
    }
};

class TagUpdate : public Packet
{
    DEFINE_PACKET_TYPE(TagUpdate, ePacketType::TAG_UPDATE, ePacketChannel::EVENT);

public:
    struct Payload
    {
        int16_t pos_x = 0;
        int16_t pos_y = 0;
        int16_t pos_z = 0;
        bool bFullySprayed = false;
        uint8_t alpha = 0;
    } payload;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, payload.pos_x, -3000, 3000);
        serialize_int(stream, payload.pos_y, -3000, 3000);
        serialize_int(stream, payload.pos_z, -3000, 3000);

        serialize_bool(stream, payload.bFullySprayed);
        if (payload.bFullySprayed)
        {
            payload.alpha = 255;
        }
        else
        {
            if (Stream::IsWriting)
            {
                uint8_t temp = payload.alpha / 8;
                serialize_int(stream, temp, 0, 31);
            }
            else if (Stream::IsReading)
            {
                uint8_t temp;
                serialize_int(stream, temp, 0, 31);
                payload.alpha = temp * 8;
            }
        }

        return true;
    }
};

class UpdateAllTags : public Packet
{
    DEFINE_PACKET_TYPE(UpdateAllTags, ePacketType::UPDATE_ALL_TAGS, ePacketChannel::EVENT);

public:
    TagUpdate::Payload tags[100];

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        for (size_t i = 0; i < ARRAY_SIZE(tags); i++)
        {
            serialize_int(stream, tags[i].pos_x, -3000, 3000);
            serialize_int(stream, tags[i].pos_y, -3000, 3000);
            serialize_int(stream, tags[i].pos_z, -3000, 3000);
            bool bIsNull = false;
            bool bFullySprayed = false;
            if (Stream::IsWriting)
            {
                if (tags[i].alpha == 0)
                {
                    bIsNull = true;
                }
                serialize_bool(stream, bIsNull);
                if (bIsNull)
                {
                    continue;
                }

                if (tags[i].alpha == 255)
                {
                    bFullySprayed = true;
                }
                serialize_bool(stream, bFullySprayed);
                if (bFullySprayed)
                {
                    continue;
                }
                uint8_t temp = tags[i].alpha / 8;
                serialize_int(stream, temp, 0, 31);
            }
            else if (Stream::IsReading)
            {
                serialize_bool(stream, bIsNull);
                if (bIsNull)
                {
                    tags[i].alpha = 0;
                    continue;
                }

                serialize_bool(stream, bFullySprayed);
                if (bFullySprayed)
                {
                    tags[i].alpha = 255;
                    continue;
                }

                uint8_t temp;
                serialize_int(stream, temp, 0, 31);
                tags[i].alpha = temp * 8;
            }
        }
        return true;
    }
};

class UpdateMoonSize : public Packet
{
    DEFINE_PACKET_TYPE(UpdateMoonSize, ePacketType::UPDATE_MOON_SIZE, ePacketChannel::EVENT);

public:
    uint8_t moonSize;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, moonSize, 0, 7);
        return true;
    }
};

constexpr uint16_t MAX_GANG_ZONE_INFOS = 380;
constexpr uint16_t MAX_GANG_NAVIGATION_ZONES = 380;
constexpr uint8_t GANG_DENSITY_COUNT = 10;
constexpr uint8_t MAX_GANG_DENSITY = UINT8_MAX;
constexpr uint8_t MAX_SPECIFIC_GANG_ZONES = 6;
constexpr int16_t INVALID_GANG_ZONE_INDEX = -1;
constexpr uint8_t INVALID_GANG_AUTHORITY = UINT8_MAX;
constexpr size_t GANG_ZONE_DENSITY_BYTES = MAX_GANG_ZONE_INFOS * GANG_DENSITY_COUNT;
constexpr size_t GANG_WORLD_EVENT_HEADER_BYTES = sizeof(uint16_t) + sizeof(server_time_t);

inline bool IsGangWorldRevisionNewer(uint32_t candidate, uint32_t reference)
{
    const uint32_t distance = candidate - reference;
    return distance != 0 && distance < (UINT32_MAX / 2u + 1u);
}

enum class eGangWarLifecycle : uint8_t
{
    NOT_IN_WAR = 0,
    PRE_FIRST_WAVE,
    FIRST_WAVE,
    PRE_SECOND_WAVE,
    SECOND_WAVE,
    PRE_THIRD_WAVE,
    THIRD_WAVE
};

enum class eGangAttackLifecycle : uint8_t
{
    NO_ATTACK = 0,
    WAR_NOTIFIED,
    PLAYER_CAME_TO_WAR
};

class GangZoneState : public Packet
{
    DEFINE_PACKET_TYPE(GangZoneState, ePacketType::GANG_ZONE_STATE, ePacketChannel::EVENT);

public:
    static constexpr size_t MAX_SERIALIZED_BYTES = 4096;

    uint32_t revision = 0;
    uint8_t authorityPlayerId = INVALID_GANG_AUTHORITY;
    uint16_t zoneInfoCount = 0;
    std::array<std::array<uint8_t, GANG_DENSITY_COUNT>, MAX_GANG_ZONE_INFOS> gangDensities{};

    bool HasValidState() const
    {
        return revision != 0 && authorityPlayerId < Config::MAX_SERVER_PLAYERS && zoneInfoCount > 0 &&
               zoneInfoCount <= MAX_GANG_ZONE_INFOS;
    }

    size_t MeasureSerializedBytes() const
    {
        GangZoneState measuredState = *this;
        serialize::MeasureStream stream;
        if (!measuredState.SerializeMeasure(stream))
        {
            return 0;
        }
        return GANG_WORLD_EVENT_HEADER_BYTES + stream.GetBytesProcessed();
    }

    bool FitsSerializedBudget() const
    {
        const size_t measuredBytes = MeasureSerializedBytes();
        return measuredBytes >= GANG_WORLD_EVENT_HEADER_BYTES && measuredBytes <= MAX_SERIALIZED_BYTES;
    }

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        if (Stream::IsWriting && !HasValidState())
        {
            return false;
        }

        serialize_uint32(stream, revision);
        serialize_int(stream, authorityPlayerId, 0, Config::MAX_SERVER_PLAYERS - 1);
        serialize_int(stream, zoneInfoCount, 1, MAX_GANG_ZONE_INFOS);
        for (size_t zoneIndex = 0; zoneIndex < zoneInfoCount; ++zoneIndex)
        {
            // Stock CZoneInfo::GangStrength and SET_ZONE_GANG_STRENGTH use uint8. Values above 100 are
            // representable and stock enemy-gain arithmetic is not clamped to 100, so the complete byte is data.
            serialize_bytes(stream, gangDensities[zoneIndex].data(), GANG_DENSITY_COUNT);
        }

        return !Stream::IsReading || HasValidState();
    }
};

static_assert(GangZoneState::MAX_SERIALIZED_BYTES <= 10 * 1024,
    "Gang-zone snapshots must fit CPacketFactory's fixed packet buffer");

class GangWarState : public Packet
{
    DEFINE_PACKET_TYPE(GangWarState, ePacketType::GANG_WAR_STATE, ePacketChannel::EVENT);

public:
    static constexpr size_t MAX_SERIALIZED_BYTES = 512;
    // Defensive fight time is distance * 200 + 240,000, then at most +30,000. The synchronized positions are
    // constrained to the 6,000 x 6,000 world square, whose diagonal keeps stock output below two million ms.
    static constexpr uint32_t MAX_FIGHT_TIMER_MS = 2000000;
    // TimeStarted/LastTimeInArea are wrapping uint32 timestamps. We transmit elapsed time and cap pathological
    // or stale values at one day, far beyond the stock 10s pre-wave and 30s outside-area thresholds.
    static constexpr uint32_t MAX_REPLICATED_ELAPSED_MS = 86400000;
    static constexpr float MIN_TIME_TILL_NEXT_ATTACK_MS = -86400000.0f;
    // CalculateTimeTillNextAttack returns [648,000, 1,620,000]; stock may subtract from that value but not exceed it.
    static constexpr float MAX_TIME_TILL_NEXT_ATTACK_MS = 1620000.0f;
    static constexpr float MAX_PROVOCATION = 255.0f;

    uint32_t revision = 0;
    uint8_t authorityPlayerId = INVALID_GANG_AUTHORITY;
    eGangWarLifecycle lifecycle = eGangWarLifecycle::NOT_IN_WAR;
    eGangAttackLifecycle attackLifecycle = eGangAttackLifecycle::NO_ATTACK;
    int8_t primaryGang = -1;
    int8_t secondaryGang = -1;
    int8_t warFerocity = 0;
    int16_t fightZoneInfoIndex = INVALID_GANG_ZONE_INDEX;
    int16_t fightNavigationZoneIndex = INVALID_GANG_ZONE_INDEX;
    int16_t trainingZoneInfoIndex = INVALID_GANG_ZONE_INDEX;
    bool gangWarsActive = false;
    bool trainingMission = false;
    bool playerIsCloseBy = false;
    bool canTriggerWhenOnMission = false;
    bool playerIsOnMission = false;
    uint8_t specificZoneCount = 0;
    std::array<uint16_t, MAX_SPECIFIC_GANG_ZONES> specificNavigationZoneIndices{};
    std::array<uint8_t, 3> gangRatings{};
    std::array<uint16_t, 3> gangRatingStrength{};
    uint32_t fightTimerRemainingMs = 0;
    uint32_t waveElapsedMs = 0;
    uint32_t timeOutsideFightAreaMs = 0;
    float timeTillNextAttackMs = 0.0f;
    float provocation = 0.0f;
    float difficulty = 0.0f;
    float territoryControl = 0.0f;
    WorldPositionCompressed warStartPosition{};
    WorldPositionCompressed pointOfAttack{};

    bool HasValidState() const
    {
        const int lifecycleValue = static_cast<int>(lifecycle);
        const int attackValue = static_cast<int>(attackLifecycle);
        if (revision == 0 || authorityPlayerId >= Config::MAX_SERVER_PLAYERS ||
            lifecycleValue < static_cast<int>(eGangWarLifecycle::NOT_IN_WAR) ||
            lifecycleValue > static_cast<int>(eGangWarLifecycle::THIRD_WAVE) ||
            attackValue < static_cast<int>(eGangAttackLifecycle::NO_ATTACK) ||
            attackValue > static_cast<int>(eGangAttackLifecycle::PLAYER_CAME_TO_WAR) ||
            primaryGang < -1 || primaryGang >= GANG_DENSITY_COUNT || secondaryGang < -1 ||
            secondaryGang >= GANG_DENSITY_COUNT || warFerocity < 0 || warFerocity > 5 ||
            fightZoneInfoIndex < INVALID_GANG_ZONE_INDEX || fightZoneInfoIndex >= MAX_GANG_ZONE_INFOS ||
            fightNavigationZoneIndex < INVALID_GANG_ZONE_INDEX ||
            fightNavigationZoneIndex >= MAX_GANG_NAVIGATION_ZONES ||
            trainingZoneInfoIndex < INVALID_GANG_ZONE_INDEX || trainingZoneInfoIndex >= MAX_GANG_ZONE_INFOS ||
            specificZoneCount > MAX_SPECIFIC_GANG_ZONES || fightTimerRemainingMs > MAX_FIGHT_TIMER_MS ||
            waveElapsedMs > MAX_REPLICATED_ELAPSED_MS ||
            timeOutsideFightAreaMs > MAX_REPLICATED_ELAPSED_MS || !std::isfinite(timeTillNextAttackMs) ||
            timeTillNextAttackMs < MIN_TIME_TILL_NEXT_ATTACK_MS ||
            timeTillNextAttackMs > MAX_TIME_TILL_NEXT_ATTACK_MS ||
            !std::isfinite(provocation) || provocation < 0.0f || provocation > MAX_PROVOCATION ||
            !std::isfinite(difficulty) || difficulty < 0.0f || difficulty > 1.0f ||
            !std::isfinite(territoryControl) || territoryControl < 0.0f || territoryControl > 1.0f ||
            !HasFiniteWorldPositions())
        {
            return false;
        }

        for (size_t index = 0; index < specificZoneCount; ++index)
        {
            if (specificNavigationZoneIndices[index] >= MAX_GANG_NAVIGATION_ZONES)
            {
                return false;
            }
        }
        for (size_t index = 0; index < gangRatings.size(); ++index)
        {
            if (gangRatings[index] >= gangRatings.size() ||
                gangRatingStrength[index] > MAX_GANG_NAVIGATION_ZONES)
            {
                return false;
            }
        }

        const bool lifecycleNeedsFightZone = lifecycle != eGangWarLifecycle::NOT_IN_WAR ||
                                             attackLifecycle != eGangAttackLifecycle::NO_ATTACK;
        const bool inactiveTimersAreCanonical =
            (attackLifecycle != eGangAttackLifecycle::NO_ATTACK || fightTimerRemainingMs == 0) &&
            (lifecycle != eGangWarLifecycle::NOT_IN_WAR ||
                (waveElapsedMs == 0 && timeOutsideFightAreaMs == 0));
        return inactiveTimersAreCanonical && (!lifecycleNeedsFightZone ||
               (primaryGang >= 0 && fightZoneInfoIndex >= 0 && fightNavigationZoneIndex >= 0));
    }

    size_t MeasureSerializedBytes() const
    {
        GangWarState measuredState = *this;
        serialize::MeasureStream stream;
        if (!measuredState.SerializeMeasure(stream))
        {
            return 0;
        }
        return GANG_WORLD_EVENT_HEADER_BYTES + stream.GetBytesProcessed();
    }

    bool FitsSerializedBudget() const
    {
        const size_t measuredBytes = MeasureSerializedBytes();
        return measuredBytes >= GANG_WORLD_EVENT_HEADER_BYTES && measuredBytes <= MAX_SERIALIZED_BYTES;
    }

private:
    bool HasFiniteWorldPositions() const
    {
        const auto inWorld = [](const WorldPositionCompressed& position) {
            return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z) &&
                   position.x >= -3000.0f && position.x <= 3000.0f && position.y >= -3000.0f &&
                   position.y <= 3000.0f && position.z >= -120.0f && position.z <= 1000.0f;
        };
        return inWorld(warStartPosition) && inWorld(pointOfAttack);
    }

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        if (Stream::IsWriting && !HasValidState())
        {
            return false;
        }

        serialize_uint32(stream, revision);
        serialize_int(stream, authorityPlayerId, 0, Config::MAX_SERVER_PLAYERS - 1);

        int lifecycleValue = static_cast<int>(lifecycle);
        int attackValue = static_cast<int>(attackLifecycle);
        int primaryGangValue = primaryGang;
        int secondaryGangValue = secondaryGang;
        int ferocityValue = warFerocity;
        int fightZoneInfoValue = fightZoneInfoIndex;
        int fightNavigationZoneValue = fightNavigationZoneIndex;
        int trainingZoneValue = trainingZoneInfoIndex;
        serialize_int(stream, lifecycleValue, static_cast<int>(eGangWarLifecycle::NOT_IN_WAR),
            static_cast<int>(eGangWarLifecycle::THIRD_WAVE));
        serialize_int(stream, attackValue, static_cast<int>(eGangAttackLifecycle::NO_ATTACK),
            static_cast<int>(eGangAttackLifecycle::PLAYER_CAME_TO_WAR));
        serialize_int(stream, primaryGangValue, -1, GANG_DENSITY_COUNT - 1);
        serialize_int(stream, secondaryGangValue, -1, GANG_DENSITY_COUNT - 1);
        serialize_int(stream, ferocityValue, 0, 5);
        serialize_int(stream, fightZoneInfoValue, INVALID_GANG_ZONE_INDEX, MAX_GANG_ZONE_INFOS - 1);
        serialize_int(stream, fightNavigationZoneValue, INVALID_GANG_ZONE_INDEX,
            MAX_GANG_NAVIGATION_ZONES - 1);
        serialize_int(stream, trainingZoneValue, INVALID_GANG_ZONE_INDEX, MAX_GANG_ZONE_INFOS - 1);
        if (Stream::IsReading)
        {
            lifecycle = static_cast<eGangWarLifecycle>(lifecycleValue);
            attackLifecycle = static_cast<eGangAttackLifecycle>(attackValue);
            primaryGang = static_cast<int8_t>(primaryGangValue);
            secondaryGang = static_cast<int8_t>(secondaryGangValue);
            warFerocity = static_cast<int8_t>(ferocityValue);
            fightZoneInfoIndex = static_cast<int16_t>(fightZoneInfoValue);
            fightNavigationZoneIndex = static_cast<int16_t>(fightNavigationZoneValue);
            trainingZoneInfoIndex = static_cast<int16_t>(trainingZoneValue);
        }

        serialize_bool(stream, gangWarsActive);
        serialize_bool(stream, trainingMission);
        serialize_bool(stream, playerIsCloseBy);
        serialize_bool(stream, canTriggerWhenOnMission);
        serialize_bool(stream, playerIsOnMission);
        serialize_int(stream, specificZoneCount, 0, MAX_SPECIFIC_GANG_ZONES);
        for (size_t index = 0; index < specificZoneCount; ++index)
        {
            serialize_int(stream, specificNavigationZoneIndices[index], 0, MAX_GANG_NAVIGATION_ZONES - 1);
        }
        for (size_t index = 0; index < gangRatings.size(); ++index)
        {
            // GangRatings stores each of the three turf gangs' rank (0..2), not a gang id. Strength is the
            // controlled-navigation-zone count at that rank, so its bound is the navigation array capacity.
            serialize_int(stream, gangRatings[index], 0, 2);
            serialize_int(stream, gangRatingStrength[index], 0, MAX_GANG_NAVIGATION_ZONES);
        }
        serialize_int(stream, fightTimerRemainingMs, 0, MAX_FIGHT_TIMER_MS);
        serialize_uint32(stream, waveElapsedMs);
        serialize_uint32(stream, timeOutsideFightAreaMs);
        serialize_float(stream, timeTillNextAttackMs);
        serialize_compressed_float(stream, provocation, 0.0f, MAX_PROVOCATION, 0.01f);
        // UpdateTerritoryUnderControlPercentage stores groveZones / allGangZones and war Difficulty copies it.
        serialize_compressed_float(stream, difficulty, 0.0f, 1.0f, 0.001f);
        serialize_compressed_float(stream, territoryControl, 0.0f, 1.0f, 0.001f);
        serialize_object(stream, warStartPosition);
        serialize_object(stream, pointOfAttack);

        return !Stream::IsReading || HasValidState();
    }
};

static_assert(GangWarState::MAX_SERIALIZED_BYTES <= 10 * 1024,
    "Gang-war snapshots must fit CPacketFactory's fixed packet buffer");

}  // namespace Packets::World
