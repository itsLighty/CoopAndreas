#pragma once

#include "config.h"
#include "network/packet.h"
#include "serialize.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Packets::Cheats
{
// These values deliberately match GTA SA 1.0 US's 92-entry stock cheat table. The wire protocol carries
// only this bounded enum; keyboard strings, hashes, native addresses and function pointers never cross it.
enum class eStockCheat : uint8_t
{
    WEAPON_SET_1 = 0,
    WEAPON_SET_2,
    WEAPON_SET_3,
    HEALTH_ARMOUR_MONEY,
    WANTED_LEVEL_UP,
    WANTED_LEVEL_CLEAR,
    WEATHER_SUNNY,
    WEATHER_EXTRA_SUNNY,
    WEATHER_CLOUDY,
    WEATHER_RAINY,
    WEATHER_FOGGY,
    FASTER_CLOCK,
    FASTER_GAMEPLAY,
    SLOWER_GAMEPLAY,
    MAYHEM,
    EVERYBODY_ATTACKS_PLAYER,
    EVERYONE_ARMED,
    SPAWN_RHINO,
    SPAWN_BLOODRING_BANGER,
    SPAWN_RANCHER,
    SPAWN_HOTRING_A,
    SPAWN_HOTRING_B,
    SPAWN_ROMERO,
    SPAWN_STRETCH,
    SPAWN_TRASHMASTER,
    SPAWN_CADDY,
    BLOW_UP_ALL_CARS,
    INVISIBLE_CARS,
    PERFECT_HANDLING,
    SUICIDE,
    GREEN_LIGHTS,
    AGGRESSIVE_DRIVERS,
    PINK_TRAFFIC,
    BLACK_TRAFFIC,
    CARS_ON_WATER,
    BOATS_FLY,
    FAT_PLAYER,
    MAX_MUSCLE,
    SKINNY_PLAYER,
    ELVIS_EVERYWHERE,
    PEDS_ATTACK_WITH_ROCKETS,
    BEACH_PARTY,
    GANG_MEMBERS_EVERYWHERE,
    GANGS_CONTROL_STREETS,
    NINJA_THEME,
    LOVE_CONQUERS_ALL,
    CHEAP_TRAFFIC,
    FAST_TRAFFIC,
    CARS_FLY,
    HUGE_BUNNY_HOP,
    SPAWN_HYDRA,
    SPAWN_VORTEX,
    SMASH_AND_BOOM,
    ALL_CARS_NITRO,
    CARS_FLOAT_WHEN_HIT,
    ALWAYS_MIDNIGHT,
    ORANGE_SKY,
    THUNDERSTORM,
    SANDSTORM,
    UNUSED_PREDATOR,
    MEGA_JUMP,
    INFINITE_HEALTH,
    INFINITE_OXYGEN,
    GIVE_PARACHUTE,
    GIVE_JETPACK,
    NEVER_WANTED,
    SIX_STAR_WANTED,
    MEGA_PUNCH,
    NEVER_HUNGRY,
    RIOT_MODE,
    FUNHOUSE_THEME,
    ADRENALINE_MODE,
    INFINITE_AMMO,
    DRIVEBY_AIMING,
    REDUCED_TRAFFIC,
    COUNTRY_TRAFFIC,
    RECRUIT_ANYONE,
    RECRUIT_WITH_PISTOLS,
    RECRUIT_WITH_ROCKETS,
    MAX_RESPECT,
    MAX_SEX_APPEAL,
    MAX_STAMINA,
    MAX_WEAPON_SKILLS,
    MAX_VEHICLE_SKILLS,
    SPAWN_HUNTER,
    SPAWN_QUAD,
    SPAWN_TANKER,
    SPAWN_DOZER,
    SPAWN_STUNT_PLANE,
    SPAWN_MONSTER,
    PROSTITUTES_PAY_PLAYER,
    ALL_TAXIS_NITRO,
    COUNT
};

constexpr uint8_t STOCK_CHEAT_COUNT = static_cast<uint8_t>(eStockCheat::COUNT);
constexpr size_t CHEAT_MASK_BYTES = (STOCK_CHEAT_COUNT + 7u) / 8u;
constexpr uint8_t CHEAT_INVALID_PLAYER_ID = UINT8_MAX;
constexpr int8_t GAMEPLAY_SPEED_STEP_MIN = -2;
constexpr int8_t GAMEPLAY_SPEED_STEP_MAX = 2;
using CheatMask = std::array<uint8_t, CHEAT_MASK_BYTES>;

static_assert(STOCK_CHEAT_COUNT == 92, "The wire allowlist must match GTA SA's stock cheat table");

inline bool IsStockCheatValid(eStockCheat cheat)
{
    return static_cast<uint8_t>(cheat) < STOCK_CHEAT_COUNT && cheat != eStockCheat::UNUSED_PREDATOR;
}

inline bool IsPersistentCheat(eStockCheat cheat)
{
    switch (cheat)
    {
    case eStockCheat::FASTER_CLOCK:
    case eStockCheat::MAYHEM:
    case eStockCheat::EVERYBODY_ATTACKS_PLAYER:
    case eStockCheat::EVERYONE_ARMED:
    case eStockCheat::INVISIBLE_CARS:
    case eStockCheat::PERFECT_HANDLING:
    case eStockCheat::GREEN_LIGHTS:
    case eStockCheat::AGGRESSIVE_DRIVERS:
    case eStockCheat::PINK_TRAFFIC:
    case eStockCheat::BLACK_TRAFFIC:
    case eStockCheat::CARS_ON_WATER:
    case eStockCheat::BOATS_FLY:
    case eStockCheat::ELVIS_EVERYWHERE:
    case eStockCheat::PEDS_ATTACK_WITH_ROCKETS:
    case eStockCheat::BEACH_PARTY:
    case eStockCheat::GANG_MEMBERS_EVERYWHERE:
    case eStockCheat::GANGS_CONTROL_STREETS:
    case eStockCheat::NINJA_THEME:
    case eStockCheat::LOVE_CONQUERS_ALL:
    case eStockCheat::CHEAP_TRAFFIC:
    case eStockCheat::FAST_TRAFFIC:
    case eStockCheat::CARS_FLY:
    case eStockCheat::HUGE_BUNNY_HOP:
    case eStockCheat::SMASH_AND_BOOM:
    case eStockCheat::ALL_CARS_NITRO:
    case eStockCheat::CARS_FLOAT_WHEN_HIT:
    case eStockCheat::ALWAYS_MIDNIGHT:
    case eStockCheat::ORANGE_SKY:
    case eStockCheat::MEGA_JUMP:
    case eStockCheat::INFINITE_HEALTH:
    case eStockCheat::INFINITE_OXYGEN:
    case eStockCheat::NEVER_WANTED:
    case eStockCheat::MEGA_PUNCH:
    case eStockCheat::NEVER_HUNGRY:
    case eStockCheat::RIOT_MODE:
    case eStockCheat::FUNHOUSE_THEME:
    case eStockCheat::ADRENALINE_MODE:
    case eStockCheat::INFINITE_AMMO:
    case eStockCheat::DRIVEBY_AIMING:
    case eStockCheat::REDUCED_TRAFFIC:
    case eStockCheat::COUNTRY_TRAFFIC:
    case eStockCheat::RECRUIT_ANYONE:
    case eStockCheat::RECRUIT_WITH_PISTOLS:
    case eStockCheat::RECRUIT_WITH_ROCKETS:
    case eStockCheat::PROSTITUTES_PAY_PLAYER:
    case eStockCheat::ALL_TAXIS_NITRO:
        return true;
    default:
        return false;
    }
}

// These stock cheats multiply/divide CTimer's scale and therefore need a canonical, replayable value,
// but they are not boolean entries in the persistent cheat mask.
inline bool IsCanonicalGameplaySpeedCheat(eStockCheat cheat)
{
    return cheat == eStockCheat::FASTER_GAMEPLAY || cheat == eStockCheat::SLOWER_GAMEPLAY;
}

// GTA implements these two entries with null handlers. Their effective stat changes are irreversible,
// so online action replay latches the stock flag instead of making a second request undo the first.
inline bool IsLatchedStatSetter(eStockCheat cheat)
{
    return cheat == eStockCheat::MAX_RESPECT || cheat == eStockCheat::MAX_SEX_APPEAL;
}

inline bool IsGameplaySpeedStepValid(int8_t step)
{
    return step >= GAMEPLAY_SPEED_STEP_MIN && step <= GAMEPLAY_SPEED_STEP_MAX;
}

inline float GameplaySpeedStepToScale(int8_t step)
{
    constexpr float scales[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f};
    return IsGameplaySpeedStepValid(step) ? scales[step - GAMEPLAY_SPEED_STEP_MIN] : 1.0f;
}

inline bool IsAuthorityOnlyTransient(eStockCheat cheat)
{
    switch (cheat)
    {
    case eStockCheat::WEATHER_SUNNY:
    case eStockCheat::WEATHER_EXTRA_SUNNY:
    case eStockCheat::WEATHER_CLOUDY:
    case eStockCheat::WEATHER_RAINY:
    case eStockCheat::WEATHER_FOGGY:
    case eStockCheat::SPAWN_RHINO:
    case eStockCheat::SPAWN_BLOODRING_BANGER:
    case eStockCheat::SPAWN_RANCHER:
    case eStockCheat::SPAWN_HOTRING_A:
    case eStockCheat::SPAWN_HOTRING_B:
    case eStockCheat::SPAWN_ROMERO:
    case eStockCheat::SPAWN_STRETCH:
    case eStockCheat::SPAWN_TRASHMASTER:
    case eStockCheat::SPAWN_CADDY:
    case eStockCheat::BLOW_UP_ALL_CARS:
    case eStockCheat::SPAWN_HYDRA:
    case eStockCheat::SPAWN_VORTEX:
    case eStockCheat::THUNDERSTORM:
    case eStockCheat::SANDSTORM:
    case eStockCheat::SPAWN_HUNTER:
    case eStockCheat::SPAWN_QUAD:
    case eStockCheat::SPAWN_TANKER:
    case eStockCheat::SPAWN_DOZER:
    case eStockCheat::SPAWN_STUNT_PLANE:
    case eStockCheat::SPAWN_MONSTER:
        return true;
    default:
        return false;
    }
}

inline bool IsCheatSerialNewer(uint32_t candidate, uint32_t reference)
{
    const uint32_t distance = candidate - reference;
    return distance != 0 && distance < 0x80000000u;
}

inline bool GetCheatMaskBit(const CheatMask& mask, eStockCheat cheat)
{
    const uint8_t value = static_cast<uint8_t>(cheat);
    return value < STOCK_CHEAT_COUNT && (mask[value / 8u] & (1u << (value % 8u))) != 0;
}

inline void SetCheatMaskBit(CheatMask& mask, eStockCheat cheat, bool enabled)
{
    const uint8_t value = static_cast<uint8_t>(cheat);
    if (value >= STOCK_CHEAT_COUNT)
        return;
    const uint8_t bit = static_cast<uint8_t>(1u << (value % 8u));
    if (enabled)
        mask[value / 8u] |= bit;
    else
        mask[value / 8u] &= static_cast<uint8_t>(~bit);
}

inline bool HasValidPersistentMask(const CheatMask& mask)
{
    for (uint8_t value = 0; value < STOCK_CHEAT_COUNT; ++value)
    {
        const eStockCheat cheat = static_cast<eStockCheat>(value);
        if (GetCheatMaskBit(mask, cheat) && (!IsStockCheatValid(cheat) || !IsPersistentCheat(cheat)))
            return false;
    }
    constexpr uint8_t usedBitsInLastByte = STOCK_CHEAT_COUNT % 8u;
    const uint8_t unusedMask = static_cast<uint8_t>(~((1u << usedBitsInLastByte) - 1u));
    return usedBitsInLastByte == 0 || (mask.back() & unusedMask) == 0;
}

template <typename Stream>
inline bool SerializeCheat(Stream& stream, eStockCheat& cheat)
{
    int value = static_cast<int>(cheat);
    serialize_int(stream, value, 0, STOCK_CHEAT_COUNT - 1);
    if (Stream::IsReading)
        cheat = static_cast<eStockCheat>(value);
    return IsStockCheatValid(cheat);
}

class CheatRequest : public Packet
{
    DEFINE_PACKET_TYPE(CheatRequest, ePacketType::CHEAT_REQUEST, ePacketChannel::EVENT);

public:
    uint32_t requestSequence = 0;
    eStockCheat cheat = eStockCheat::WEAPON_SET_1;

    bool HasValidPayload() const { return requestSequence != 0 && IsStockCheatValid(cheat); }

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        if (Stream::IsWriting && !HasValidPayload())
            return false;
        serialize_uint32(stream, requestSequence);
        if (!SerializeCheat(stream, cheat))
            return false;
        return !Stream::IsReading || HasValidPayload();
    }
};

class CheatStateEvent : public Packet
{
    DEFINE_PACKET_TYPE(CheatStateEvent, ePacketType::CHEAT_STATE, ePacketChannel::EVENT);

public:
    uint64_t serverRunId = 0;
    uint32_t revision = 0;
    uint8_t authorityPlayerId = CHEAT_INVALID_PLAYER_ID;
    int8_t gameplaySpeedStep = 0;
    bool hasCause = false;
    uint32_t requestSequence = 0;
    eStockCheat cause = eStockCheat::FASTER_CLOCK;
    CheatMask persistentMask{};

    bool HasValidPayload() const
    {
        return serverRunId != 0 && revision != 0 && authorityPlayerId < Config::MAX_SERVER_PLAYERS &&
               IsGameplaySpeedStepValid(gameplaySpeedStep) &&
               HasValidPersistentMask(persistentMask) &&
               ((!hasCause && requestSequence == 0) ||
                   (hasCause && requestSequence != 0 && IsStockCheatValid(cause) &&
                       (IsPersistentCheat(cause) || IsCanonicalGameplaySpeedCheat(cause))));
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
        int speedStep = gameplaySpeedStep;
        serialize_int(stream, speedStep, GAMEPLAY_SPEED_STEP_MIN, GAMEPLAY_SPEED_STEP_MAX);
        if (Stream::IsReading)
            gameplaySpeedStep = static_cast<int8_t>(speedStep);
        serialize_bool(stream, hasCause);
        if (hasCause)
        {
            serialize_uint32(stream, requestSequence);
            if (!SerializeCheat(stream, cause))
                return false;
        }
        else if (Stream::IsReading)
        {
            requestSequence = 0;
            cause = eStockCheat::FASTER_CLOCK;
        }
        serialize_bytes(stream, persistentMask.data(), static_cast<int>(persistentMask.size()));
        return !Stream::IsReading || HasValidPayload();
    }
};

class CheatActionEvent : public Packet
{
    DEFINE_PACKET_TYPE(CheatActionEvent, ePacketType::CHEAT_ACTION, ePacketChannel::EVENT);

public:
    uint64_t serverRunId = 0;
    uint32_t eventSequence = 0;
    uint8_t authorityPlayerId = CHEAT_INVALID_PLAYER_ID;
    uint32_t requestSequence = 0;
    eStockCheat cheat = eStockCheat::WEAPON_SET_1;

    bool HasValidPayload() const
    {
        return serverRunId != 0 && eventSequence != 0 && authorityPlayerId < Config::MAX_SERVER_PLAYERS &&
               requestSequence != 0 && IsStockCheatValid(cheat) && !IsPersistentCheat(cheat) &&
               !IsCanonicalGameplaySpeedCheat(cheat);
    }

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        if (Stream::IsWriting && !HasValidPayload())
            return false;
        serialize_uint64(stream, serverRunId);
        serialize_uint32(stream, eventSequence);
        serialize_int(stream, authorityPlayerId, 0, Config::MAX_SERVER_PLAYERS - 1);
        serialize_uint32(stream, requestSequence);
        if (!SerializeCheat(stream, cheat))
            return false;
        return !Stream::IsReading || HasValidPayload();
    }
};
}  // namespace Packets::Cheats
