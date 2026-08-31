#pragma once
#include <eModelID.h>
#include <ePedType.h>
#include <CPed.h>
#include <CVehicle.h>
#include <eGlobalSpeechContexts.h>
#include <cmath>

namespace Packets::Peds
{
enum class ePedTaskSyncType
{
    NONE = 0,
    STAND_STILL,
    WANDER,
    KILL_PED_ON_FOOT,
    JUMP,
    CLIMB,
};

inline bool IsFiniteWorldPosition(const WorldPositionCompressed& position)
{
    return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z) &&
           position.x >= -3000.0f && position.x <= 3000.0f && position.y >= -3000.0f &&
           position.y <= 3000.0f && position.z >= -120.0f && position.z <= 1000.0f;
}

inline bool IsAimCapableWeapon(uint8_t weaponType)
{
    return weaponType >= WEAPON_PISTOL && weaponType <= WEAPON_CAMERA && weaponType != WEAPON_SATCHEL_CHARGE &&
           weaponType != WEAPON_DETONATOR;
}

inline bool IsSirenCapableVehicleModel(int modelId)
{
    // Mirrors GTA SA CVehicle::UsesSiren (0x6D8470): the three explicit service models plus
    // IsLawEnforcementVehicle(), except for its explicit RHINO exclusion.
    switch (modelId)
    {
    case MODEL_FIRETRUK:
    case MODEL_AMBULAN:
    case MODEL_MRWHOOP:
    case MODEL_ENFORCER:
    case MODEL_PREDATOR:
    case MODEL_BARRACKS:
    case MODEL_FBIRANCH:
    case MODEL_COPBIKE:
    case MODEL_FBITRUCK:
    case MODEL_COPCARLA:
    case MODEL_COPCARSF:
    case MODEL_COPCARVG:
    case MODEL_COPCARRU:
    case MODEL_SWATVAN:
        return true;
    default:
        return false;
    }
}

struct SPedTaskSnapshot
{
    static constexpr size_t MAX_SERIALIZED_BYTES = 32;

    uint16_t revision = 0;
    ePedTaskSyncType type = ePedTaskSyncType::NONE;

    int standTime = 0;
    bool standLooped = false;
    bool standUseIdleStance = false;

    eMoveState wanderMoveState = PEDMOVE_STILL;
    uint8_t wanderDirection = 0;
    bool wanderSensibly = true;
    float wanderRadius = 0.5f;

    CNetworkEntitySerializer target{};
    int killTime = -1;
    uint8_t killAttackFlags = 0;
    int killActionDelay = 0;
    uint8_t killActionChance = 0;
    bool killUnknownFlag = false;

    uint8_t jumpType = 0;

    bool HasSamePayload(const SPedTaskSnapshot& other) const
    {
        if (type != other.type)
            return false;

        switch (type)
        {
        case ePedTaskSyncType::NONE:
        case ePedTaskSyncType::CLIMB:
            return true;
        case ePedTaskSyncType::STAND_STILL:
            return standTime == other.standTime && standLooped == other.standLooped &&
                   standUseIdleStance == other.standUseIdleStance;
        case ePedTaskSyncType::WANDER:
            return wanderMoveState == other.wanderMoveState && wanderDirection == other.wanderDirection &&
                   wanderSensibly == other.wanderSensibly && std::abs(wanderRadius - other.wanderRadius) < 0.001f;
        case ePedTaskSyncType::KILL_PED_ON_FOOT:
            return target.entityType == other.target.entityType && target.entityId == other.target.entityId &&
                   killTime == other.killTime && killAttackFlags == other.killAttackFlags &&
                   killActionDelay == other.killActionDelay && killActionChance == other.killActionChance &&
                   killUnknownFlag == other.killUnknownFlag;
        case ePedTaskSyncType::JUMP:
            return jumpType == other.jumpType;
        }
        return false;
    }

    bool HasValidSemantics() const
    {
        switch (type)
        {
        case ePedTaskSyncType::NONE:
        case ePedTaskSyncType::CLIMB:
            return true;
        case ePedTaskSyncType::STAND_STILL:
            return standTime >= -1 && standTime <= 600000;
        case ePedTaskSyncType::WANDER:
            return wanderMoveState >= PEDMOVE_STILL && wanderMoveState <= PEDMOVE_SPRINT && wanderDirection <= 7 &&
                   std::isfinite(wanderRadius) && wanderRadius >= 0.1f && wanderRadius <= 50.0f;
        case ePedTaskSyncType::KILL_PED_ON_FOOT:
            return (target.entityType == NETWORK_ENTITY_TYPE_PLAYER || target.entityType == NETWORK_ENTITY_TYPE_PED) &&
                   killTime >= -1 && killTime <= 600000 && killActionDelay >= 0 && killActionDelay <= 60000 &&
                   killActionChance <= 100;
        case ePedTaskSyncType::JUMP:
            return jumpType <= 1;
        }
        return false;
    }

    size_t MeasureSerializedBytes() const
    {
        SPedTaskSnapshot measured = *this;
        serialize::MeasureStream stream;
        if (!measured.Serialize(stream))
            return 0;
        return stream.GetBytesProcessed();
    }

    bool FitsSerializedBudget() const
    {
        const size_t measuredBytes = MeasureSerializedBytes();
        return measuredBytes > 0 && measuredBytes <= MAX_SERIALIZED_BYTES;
    }

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_uint16(stream, revision);
        serialize_int(stream, (int&)type, (int)ePedTaskSyncType::NONE, (int)ePedTaskSyncType::CLIMB);

        switch (type)
        {
        case ePedTaskSyncType::NONE:
        case ePedTaskSyncType::CLIMB:
            // CTaskComplexClimb has a zero-argument constructor; the game derives ledge geometry from the ped and
            // nearby collision data. Serializing engine pointers or transient climb probes would be unsafe.
            break;
        case ePedTaskSyncType::STAND_STILL:
            serialize_int(stream, standTime, -1, 600000);
            serialize_bool(stream, standLooped);
            serialize_bool(stream, standUseIdleStance);
            break;
        case ePedTaskSyncType::WANDER:
            serialize_int(stream, (int&)wanderMoveState, PEDMOVE_STILL, PEDMOVE_SPRINT);
            serialize_int(stream, wanderDirection, 0, 7);
            serialize_bool(stream, wanderSensibly);
            serialize_compressed_float(stream, wanderRadius, 0.1f, 50.0f, 0.1f);
            break;
        case ePedTaskSyncType::KILL_PED_ON_FOOT:
            serialize_object(stream, target);
            serialize_int(stream, killTime, -1, 600000);
            serialize_uint8(stream, killAttackFlags);
            serialize_int(stream, killActionDelay, 0, 60000);
            serialize_int(stream, killActionChance, 0, 100);
            serialize_bool(stream, killUnknownFlag);
            break;
        case ePedTaskSyncType::JUMP:
            serialize_int(stream, jumpType, 0, 1);
            break;
        }

        return !Stream::IsReading || HasValidSemantics();
    }
};

static_assert(SPedTaskSnapshot::MAX_SERIALIZED_BYTES <= 64, "NPC task snapshots must remain bounded for SYNC packets");

struct SPedGroupMembershipSnapshot
{
    static constexpr uint8_t MAX_FOLLOWERS = 7;
    static constexpr uint8_t LEADER_MEMBER_SLOT = 7;
    static constexpr size_t MAX_GROUP_SERIALIZED_BYTES = 4;

    uint16_t revision = 0;
    bool hasGroup = false;
    SenderPlayerId leaderPlayerId{};
    uint8_t followerSlot = 0;

    bool HasSameMembership(const SPedGroupMembershipSnapshot& other) const
    {
        return hasGroup == other.hasGroup &&
               (!hasGroup || (leaderPlayerId.value == other.leaderPlayerId.value && followerSlot == other.followerSlot));
    }

    bool HasValidSemantics() const
    {
        return !hasGroup || (leaderPlayerId.value >= 0 && leaderPlayerId.value < Config::MAX_SERVER_PLAYERS &&
                                followerSlot < MAX_FOLLOWERS && followerSlot != LEADER_MEMBER_SLOT);
    }

    size_t MeasureSerializedBytes() const
    {
        SPedGroupMembershipSnapshot measured = *this;
        serialize::MeasureStream stream;
        if (!measured.Serialize(stream))
            return 0;
        return stream.GetBytesProcessed();
    }

    bool FitsSerializedBudget() const
    {
        const size_t measuredBytes = MeasureSerializedBytes();
        return measuredBytes > 0 && measuredBytes <= MAX_GROUP_SERIALIZED_BYTES;
    }

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_uint16(stream, revision);
        serialize_bool(stream, hasGroup);
        if (hasGroup)
        {
            // SenderPlayerId deliberately omits this field C2S. The server binds every membership to the
            // authenticated owner, then includes the canonical player ID when relaying S2C.
            serialize_object(stream, leaderPlayerId);
            serialize_int(stream, followerSlot, 0, MAX_FOLLOWERS - 1);
        }
        else if (Stream::IsReading)
        {
            leaderPlayerId.value = 0;
            followerSlot = 0;
        }

        return !Stream::IsReading || HasValidSemantics();
    }
};

static_assert(SPedGroupMembershipSnapshot::MAX_FOLLOWERS + 1 == 8,
    "GTA SA groups contain seven followers and one leader");
static_assert(SPedGroupMembershipSnapshot::MAX_GROUP_SERIALIZED_BYTES <= 4,
    "Ped group membership must remain a tiny addition to PED_ONFOOT");

class PedSpawn : public Packet
{
    DEFINE_PACKET_TYPE(PedSpawn, ePacketType::PED_SPAWN, ePacketChannel::EVENT);

public:
    int pedid = 0;
    uint8_t tempid = 255;
    eModelID modelId = MODEL_MALE01;
    ePedType pedType = PED_TYPE_CIVMALE;
    WorldPositionCompressed pos{};
    eCharCreatedBy createdBy = MISSION_CHAR;
    char specialModelName[8] = {'\0'};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, pedid, 0, Config::MAX_SERVER_PEDS - 1);
        serialize_uint8(stream, tempid);
        serialize_int(stream, (int&)modelId, MODEL_NULL, MODEL_SPECIAL10);
        serialize_int(stream, (int&)pedType, PED_TYPE_PLAYER1, PED_TYPE_MISSION8);
        serialize_object(stream, pos);
        serialize_int(stream, (int&)createdBy, UNUSED_CHAR, REPLAY_CHAR);
        serialize_string(stream, specialModelName, 8);
        specialModelName[ARRAY_SIZE(specialModelName) - 1] = '\0';
        return true;
    }
};

class PedConfirm : public Packet
{
    DEFINE_PACKET_TYPE(PedConfirm, ePacketType::PED_CONFIRM, ePacketChannel::EVENT);

public:
    uint8_t tempid = 255;
    int pedid = 0;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_uint8(stream, tempid);
        serialize_int(stream, pedid, 0, Config::MAX_SERVER_PEDS - 1);
        return true;
    }
};

class PedRemove : public Packet
{
    DEFINE_PACKET_TYPE(PedRemove, ePacketType::PED_REMOVE, ePacketChannel::EVENT);

public:
    int pedid = 0;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, pedid, 0, Config::MAX_SERVER_PEDS - 1);
        return true;
    }
};

class AssignPedSyncer : public Packet
{
    DEFINE_PACKET_TYPE(AssignPedSyncer, ePacketType::ASSIGN_PED, ePacketChannel::EVENT);

public:
    int pedid = 0;
    bool toggleOwnership = true;
    SPedGroupMembershipSnapshot group{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, pedid, 0, Config::MAX_SERVER_PEDS - 1);
        serialize_bool(stream, toggleOwnership);
        if (!toggleOwnership)
            serialize_object(stream, group);
        return toggleOwnership || !Stream::IsReading ||
            (group.revision != 0 && group.HasValidSemantics() && group.FitsSerializedBudget());
    }
};

class PedOnFoot : public Packet
{
    DEFINE_PACKET_TYPE(PedOnFoot, ePacketType::PED_ONFOOT, ePacketChannel::SYNC);

public:
    int pedid = 0;
    WorldPositionCompressed pos{};
    MoveSpeedCompressed velocity{};
    Packets::Players::SHealthSnapshot healthSnapshot{};
    Packets::Players::SWeaponSnapshot weaponSnapshot{};
    RadianAngleCompressed aimingRotation{};
    RadianAngleCompressed currentRotation{};
    RadianAngleCompressed lookDirection{};  // this one isnt needed i think
    eMoveState moveState = PEDMOVE_STILL;
    bool bDucked = false;
    bool bAiming = false;
    uint8_t fightingStyle = 4;
    WorldPositionCompressed weaponAim{};
    SPedGroupMembershipSnapshot group{};
    SPedTaskSnapshot task{};

    bool HasValidAimState() const
    {
        return !bAiming || (healthSnapshot.iHealth > 0 && IsAimCapableWeapon(weaponSnapshot.iWeaponType) &&
                              IsFiniteWorldPosition(weaponAim));
    }

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, pedid, 0, Config::MAX_SERVER_PEDS - 1);
        serialize_object(stream, pos);
        // serialize_object(stream, velocity); // not used for now
        serialize_object(stream, healthSnapshot);
        serialize_object(stream, weaponSnapshot);
        serialize_object(stream, aimingRotation);
        serialize_object(stream, currentRotation);
        serialize_object(stream, lookDirection);
        serialize_int(stream, (int&)moveState, PEDMOVE_NONE, PEDMOVE_SPRINT);
        serialize_bool(stream, bDucked);
        serialize_bool(stream, bAiming);
        serialize_uint8(stream, fightingStyle);  // todo
        if (bAiming)
        {
            serialize_object(stream, weaponAim);
        }
        serialize_object(stream, group);
        serialize_object(stream, task);
        return !Stream::IsReading || (HasValidAimState() && group.HasValidSemantics() && group.FitsSerializedBudget() &&
                                         task.HasValidSemantics() && task.FitsSerializedBudget() &&
                                         (healthSnapshot.iHealth > 0 || task.type == ePedTaskSyncType::NONE));
    }
};

class PedDriverUpdate : public Packet
{
    DEFINE_PACKET_TYPE(PedDriverUpdate, ePacketType::PED_DRIVER_UPDATE, ePacketChannel::SYNC);

public:
    int pedid = 0;
    int vehicleid = 0;
    eVehicleType vehicleSubType = VEHICLE_AUTOMOBILE;

    WorldPositionCompressed pos{};
    NormalizedVector rot{};
    NormalizedVector roll{};
    MoveSpeedCompressed velocity{};
    CVector turnSpeed{};

    uint8_t color1{};
    uint8_t color2{};
    int8_t paintjob{};

    Packets::Players::SHealthSnapshot pedHealth{};
    Packets::Players::SWeaponSnapshot pedWeapon{};

    float health{};

    eDoorLock locked = DOORLOCK_UNLOCKED;
    float gasPedal = 0.0f;
    float breakPedal = 0.0f;
    float steerAngle = 0.0f;

    // subType specific fields
    float bikeLean{};               // bike/bmx
    float controlPedaling{};        // bmx
    float planeGearState{};         // plane
    uint16_t miscComponentAngle{};  // automobile/mtruck/plane
    bool bHorn = false;
    bool bSiren = false;
    SPedGroupMembershipSnapshot group{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
#pragma region IDs
        serialize_int(stream, pedid, 0, Config::MAX_SERVER_PEDS - 1);
        serialize_int(stream, vehicleid, 0, Config::MAX_SERVER_VEHICLES - 1);
        serialize_int(stream, (int&)vehicleSubType, VEHICLE_AUTOMOBILE, VEHICLE_TRAILER);
#pragma endregion
#pragma region clamp stuff
        if (Stream::IsWriting)
        {
            turnSpeed.x = std::clamp(turnSpeed.x, -0.5f, 0.5f);
            turnSpeed.y = std::clamp(turnSpeed.y, -0.5f, 0.5f);
            turnSpeed.z = std::clamp(turnSpeed.z, -0.5f, 0.5f);
            health = std::clamp(health, 0.0f, 1000.0f);
            gasPedal = std::clamp(gasPedal, -1.0f, 1.0f);
            breakPedal = std::clamp(breakPedal, -1.0f, 1.0f);
            steerAngle = std::clamp(steerAngle, -1.0f, 1.0f);
            bikeLean = std::clamp(bikeLean, -1.0f, 1.0f);
            controlPedaling = std::clamp(controlPedaling, -5.0f, 5.0f);
        }
#pragma endregion
#pragma region matrix speeds
        serialize_object(stream, pos);
        serialize_object(stream, rot);
        serialize_object(stream, roll);
        serialize_object(stream, velocity);

        bool sendTurnSpeed = false;
        if (Stream::IsWriting)
        {
            if (std::abs(turnSpeed.x) >= 0.0001f || std::abs(turnSpeed.y) >= 0.0001f ||
                std::abs(turnSpeed.z) >= 0.0001f)
            {
                sendTurnSpeed = true;
            }
        }
        serialize_bool(stream, sendTurnSpeed);
        if (sendTurnSpeed)
        {
            serialize_compressed_float(stream, turnSpeed.x, -0.5f, 0.5f, 0.0001f);
            serialize_compressed_float(stream, turnSpeed.y, -0.5f, 0.5f, 0.0001f);
            serialize_compressed_float(stream, turnSpeed.z, -0.5f, 0.5f, 0.0001f);
        }

#pragma endregion
#pragma region painjob
        serialize_uint8(stream, color1);
        serialize_uint8(stream, color2);
        if (Stream::IsWriting)
        {
            if (paintjob < -1 || paintjob > 2)  // got limits here https://wiki.multitheftauto.com/wiki/Paintjob
            {
                paintjob = -1;
            }
        }
        serialize_int(stream, paintjob, -1, 2);

#pragma endregion
#pragma region ped health
        {
            serialize_object(stream, pedHealth);
            serialize_object(stream, pedWeapon);
        }
#pragma endregion
#pragma region health doors
        serialize_compressed_float(stream, health, 0.0f, 1000.0f, 1.0f);

        serialize_int(stream, (int&)locked, DOORLOCK_NOT_USED, DOORLOCK_SKIP_SHUT_DOORS);
#pragma endregion
#pragma region pedals
        {
            serialize_compressed_float(stream, gasPedal, -1.0f, 1.0f, 0.01f);
            serialize_compressed_float(stream, breakPedal, -1.0f, 1.0f, 0.01f);
            serialize_compressed_float(stream, steerAngle, -1.0f, 1.0f, 0.01f);
        }
#pragma endregion
#pragma region type specific

        if (vehicleSubType == VEHICLE_BIKE || vehicleSubType == VEHICLE_BMX)
        {
            serialize_compressed_float(stream, bikeLean, -1.0f, 1.0f, 0.01f);
        }
        if (vehicleSubType == VEHICLE_BMX)
        {
            serialize_compressed_float(stream, controlPedaling, -5.0f, 5.0f, 0.01f);
        }
        if (vehicleSubType == VEHICLE_PLANE)
        {
            if (Stream::IsWriting)
            {
                bool temp = planeGearState > 0.0f;
                serialize_bool(stream, temp);
            }
            else if (Stream::IsReading)
            {
                bool temp;
                serialize_bool(stream, temp);
                planeGearState = temp ? 1.0f : 0.0f;
            }
        }
        if (vehicleSubType == VEHICLE_AUTOMOBILE || vehicleSubType == VEHICLE_MTRUCK || vehicleSubType == VEHICLE_PLANE)
        {
            bool syncAngle = false;
            if (Stream::IsWriting && miscComponentAngle != 0)
            {
                syncAngle = true;
            }
            serialize_bool(stream, syncAngle);
            if (syncAngle)
            {
                serialize_uint16(stream, miscComponentAngle);
            }
        }
#pragma endregion

        serialize_bool(stream, bHorn);
        serialize_bool(stream, bSiren);
        serialize_object(stream, group);

        return !Stream::IsReading ||
            ((pedHealth.iHealth > 0 || (!bHorn && !bSiren)) && group.HasValidSemantics() && group.FitsSerializedBudget());
    }
};

class PedPassengerSync : public Packet
{
    DEFINE_PACKET_TYPE(PedPassengerSync, ePacketType::PED_PASSENGER_UPDATE, ePacketChannel::SYNC);

public:
    int pedid{};
    int vehicleid{};

    Packets::Players::SHealthSnapshot healthSnapshot{};
    Packets::Players::SWeaponSnapshot weaponSnapshot{};

    int8_t seatid;
    SPedGroupMembershipSnapshot group{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, pedid, 0, Config::MAX_SERVER_PEDS - 1);
        serialize_int(stream, vehicleid, 0, Config::MAX_SERVER_VEHICLES - 1);
        serialize_object(stream, healthSnapshot);
        serialize_object(stream, weaponSnapshot);
        serialize_int(stream, seatid, -1, 7);  // TODO test properly TODO(v0.3.1-alpha): limits
        serialize_object(stream, group);
        return !Stream::IsReading || (group.HasValidSemantics() && group.FitsSerializedBudget());
    }
};

class PedShotSync : public Packet
{
    DEFINE_PACKET_TYPE(PedShotSync, ePacketType::PED_SHOT_SYNC, ePacketChannel::EVENT);

public:
    int pedid{};
    eWeaponType weaponType = WEAPON_UNARMED;
    WorldPositionCompressed origin{};
    WorldPositionCompressed effect{};
    WorldPositionCompressed target{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, pedid, 0, Config::MAX_SERVER_PEDS - 1);
        serialize_int(stream, (int&)weaponType, WEAPON_UNARMED, WEAPON_FLARE);
        serialize_object(stream, origin);
        serialize_object(stream, effect);
        serialize_object(stream, target);
        return true;
    }
};

class PedSay : public Packet
{
    DEFINE_PACKET_TYPE(PedSay, ePacketType::PED_SAY, ePacketChannel::EVENT);

public:
    CNetworkEntitySerializer entity{};
    eGlobalSpeechContexts phraseId = CONTEXT_GLOBAL_NO_SPEECH;
    uint32_t startTimeDelay = 0;
    bool overrideSilence = false;
    bool isForceAudible = false;
    bool isFrontEnd = false;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, entity);
        if (entity.entityType != NETWORK_ENTITY_TYPE_PLAYER && entity.entityType != NETWORK_ENTITY_TYPE_PED)
        {
            return false;
        }
        serialize_int(stream, (int&)phraseId, CONTEXT_GLOBAL_NO_SPEECH + 1, CONTEXT_GLOBAL_END - 1);
        serialize_uint32(stream, startTimeDelay);
        serialize_bool(stream, overrideSilence);
        serialize_bool(stream, isForceAudible);
        serialize_bool(stream, isFrontEnd);
        return true;
    }
};

class PedClaimOnRelease : public Packet
{
    DEFINE_PACKET_TYPE(PedClaimOnRelease, ePacketType::PED_CLAIM_ON_RELEASE, ePacketChannel::EVENT);

public:
    int pedid = 0;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, pedid, 0, Config::MAX_SERVER_PEDS - 1);
        return true;
    }
};

class PedCancelClaim : public Packet
{
    DEFINE_PACKET_TYPE(PedCancelClaim, ePacketType::PED_CANCEL_CLAIM, ePacketChannel::EVENT);

public:
    int pedid = 0;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, pedid, 0, Config::MAX_SERVER_PEDS - 1);
        return true;
    }
};

class PedResetAllClaims : public Packet
{
    DEFINE_PACKET_TYPE(PedResetAllClaims, ePacketType::PED_RESET_ALL_CLAIMS, ePacketChannel::EVENT);

public:
    int pedid = 0;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, pedid, 0, Config::MAX_SERVER_PEDS - 1);
        return true;
    }
};

class PedTakeHost : public Packet
{
    DEFINE_PACKET_TYPE(PedTakeHost, ePacketType::PED_TAKE_HOST, ePacketChannel::EVENT);

public:
    int pedid = 0;
    bool allowReturnToPreviousHost = false;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, pedid, 0, Config::MAX_SERVER_PEDS - 1);
        serialize_bool(stream, allowReturnToPreviousHost);
        return true;
    }
};

}  // namespace Packets::Peds
