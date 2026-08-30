#pragma once
#include <CEntryExit.h>
#include <CGame.h>

namespace Packets::Scripts
{
constexpr uint16_t MISSION_ID_UNKNOWN = UINT16_MAX;
constexpr uint16_t MISSION_ID_COUNT = 135;
constexpr uint16_t MISSION_ID_MAX = MISSION_ID_COUNT - 1;
constexpr uint8_t MISSION_PLAYER_ID_INVALID = UINT8_MAX;
// Current SCM globals expose the host plus only three remote players. The wire roster intentionally remains
// Config::MAX_SERVER_PLAYERS-wide so the protocol does not bake that gameplay limitation into its capacity.
constexpr uint8_t MISSION_SCM_GAMEPLAY_PLAYER_CAP = 4;

static_assert(Config::MAX_SERVER_PLAYERS > 0, "Mission sessions require at least one player slot");
static_assert(Config::MAX_SERVER_PLAYERS < MISSION_PLAYER_ID_INVALID,
    "Mission participant IDs must fit in one byte with an invalid sentinel");
static_assert(MISSION_SCM_GAMEPLAY_PLAYER_CAP <= Config::MAX_SERVER_PLAYERS,
    "The SCM gameplay subset cannot exceed the session roster capacity");

inline bool IsMissionIdKnownOrUnknown(uint16_t missionId)
{
    return missionId <= MISSION_ID_MAX || missionId == MISSION_ID_UNKNOWN;
}

inline bool IsSequenceNumberNewer(uint32_t candidate, uint32_t reference)
{
    const uint32_t distance = candidate - reference;
    return distance != 0 && distance < (UINT32_MAX / 2u + 1u);
}

inline bool IsSequenceNumberNewer(uint64_t candidate, uint64_t reference)
{
    const uint64_t distance = candidate - reference;
    return distance != 0 && distance < (UINT64_MAX / 2ull + 1ull);
}

enum class eMissionSessionLifecycle
{
    INACTIVE = 0,
    RUNNING,
    ENDED,
    ABORTED
};

enum class eMissionSessionResult
{
    NONE = 0,
    COMPLETED,
    SUCCEEDED,
    FAILED,
    ABORTED_BY_HOST,
    HOST_DISCONNECTED
};

enum class eMissionSessionRequestAction
{
    LAUNCH = 0,
    UPDATE_STAGE,
    END,
    ABORT
};

class MissionSessionState : public Packet
{
    DEFINE_PACKET_TYPE(MissionSessionState, ePacketType::MISSION_SESSION_STATE, ePacketChannel::SCRIPT);

public:
    uint64_t sessionId = 0;
    uint32_t epoch = 0;
    uint32_t acknowledgedRequestId = 0;
    uint8_t acknowledgedPlayerId = MISSION_PLAYER_ID_INVALID;
    bool acknowledgedRequestAccepted = false;
    uint16_t missionId = MISSION_ID_UNKNOWN;
    uint8_t hostId = MISSION_PLAYER_ID_INVALID;
    // participantIds is the complete frozen session roster. Only its first gameplayParticipantCount entries can
    // currently participate through SCM; remaining roster members are spectators until the SCM API is expanded.
    uint8_t participantCount = 0;
    uint8_t gameplayParticipantCount = 0;
    uint8_t participantIds[Config::MAX_SERVER_PLAYERS]{};
    eMissionSessionLifecycle lifecycle = eMissionSessionLifecycle::INACTIVE;
    eMissionSessionResult result = eMissionSessionResult::NONE;
    uint32_t stage = 0;

    bool IsActive() const { return lifecycle == eMissionSessionLifecycle::RUNNING; }

    bool HasSameAuthoritativeState(const MissionSessionState& other) const
    {
        if (sessionId != other.sessionId || epoch != other.epoch || missionId != other.missionId ||
            hostId != other.hostId || participantCount != other.participantCount ||
            gameplayParticipantCount != other.gameplayParticipantCount || lifecycle != other.lifecycle ||
            result != other.result || stage != other.stage)
        {
            return false;
        }

        for (size_t i = 0; i < participantCount; ++i)
        {
            if (participantIds[i] != other.participantIds[i])
            {
                return false;
            }
        }
        return true;
    }

    bool AcknowledgesRequest(int playerId, uint32_t requestId) const
    {
        return requestId != 0 && playerId >= 0 && playerId < Config::MAX_SERVER_PLAYERS &&
               acknowledgedPlayerId == playerId && acknowledgedRequestId == requestId;
    }

    bool ContainsParticipant(int playerId) const
    {
        if (playerId < 0 || playerId >= Config::MAX_SERVER_PLAYERS ||
            participantCount > Config::MAX_SERVER_PLAYERS)
        {
            return false;
        }

        for (size_t i = 0; i < participantCount; ++i)
        {
            if (participantIds[i] == playerId)
            {
                return true;
            }
        }
        return false;
    }

    bool ContainsGameplayParticipant(int playerId) const
    {
        if (playerId < 0 || playerId >= Config::MAX_SERVER_PLAYERS ||
            gameplayParticipantCount > participantCount)
        {
            return false;
        }

        for (size_t i = 0; i < gameplayParticipantCount; ++i)
        {
            if (participantIds[i] == playerId)
            {
                return true;
            }
        }
        return false;
    }

    bool HasValidParticipantRoster() const
    {
        if (!IsMissionIdKnownOrUnknown(missionId) ||
            (acknowledgedRequestId == 0 &&
                (acknowledgedPlayerId != MISSION_PLAYER_ID_INVALID || acknowledgedRequestAccepted)) ||
            (acknowledgedRequestId != 0 && acknowledgedPlayerId >= Config::MAX_SERVER_PLAYERS))
        {
            return false;
        }

        if (participantCount > Config::MAX_SERVER_PLAYERS || gameplayParticipantCount > participantCount ||
            gameplayParticipantCount > MISSION_SCM_GAMEPLAY_PLAYER_CAP)
        {
            return false;
        }

        bool seen[Config::MAX_SERVER_PLAYERS]{};
        for (size_t i = 0; i < participantCount; ++i)
        {
            const uint8_t playerId = participantIds[i];
            if (playerId >= Config::MAX_SERVER_PLAYERS || seen[playerId])
            {
                return false;
            }
            seen[playerId] = true;
        }

        if (lifecycle == eMissionSessionLifecycle::INACTIVE)
        {
            return sessionId == 0 && epoch == 0 && hostId == MISSION_PLAYER_ID_INVALID && participantCount == 0 &&
                   gameplayParticipantCount == 0 && result == eMissionSessionResult::NONE;
        }

        if (sessionId == 0 || epoch == 0 || hostId >= Config::MAX_SERVER_PLAYERS ||
            gameplayParticipantCount == 0 || !ContainsGameplayParticipant(hostId))
        {
            return false;
        }

        if (lifecycle == eMissionSessionLifecycle::RUNNING)
        {
            return result == eMissionSessionResult::NONE;
        }

        if (lifecycle == eMissionSessionLifecycle::ENDED)
        {
            return result == eMissionSessionResult::COMPLETED || result == eMissionSessionResult::SUCCEEDED ||
                   result == eMissionSessionResult::FAILED;
        }

        if (lifecycle == eMissionSessionLifecycle::ABORTED)
        {
            return result == eMissionSessionResult::ABORTED_BY_HOST ||
                   result == eMissionSessionResult::HOST_DISCONNECTED;
        }

        return false;
    }

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_uint64(stream, sessionId);
        serialize_uint32(stream, epoch);
        serialize_uint32(stream, acknowledgedRequestId);
        serialize_uint8(stream, acknowledgedPlayerId);
        serialize_bool(stream, acknowledgedRequestAccepted);
        serialize_uint16(stream, missionId);
        serialize_uint8(stream, hostId);

        int serializedParticipantCount = participantCount;
        serialize_int(stream, serializedParticipantCount, 0, Config::MAX_SERVER_PLAYERS);
        if (Stream::IsReading)
        {
            participantCount = static_cast<uint8_t>(serializedParticipantCount);
            for (size_t i = participantCount; i < Config::MAX_SERVER_PLAYERS; ++i)
            {
                participantIds[i] = MISSION_PLAYER_ID_INVALID;
            }
        }
        int serializedGameplayParticipantCount = gameplayParticipantCount;
        serialize_int(stream, serializedGameplayParticipantCount, 0, MISSION_SCM_GAMEPLAY_PLAYER_CAP);
        if (Stream::IsReading)
        {
            gameplayParticipantCount = static_cast<uint8_t>(serializedGameplayParticipantCount);
        }
        for (size_t i = 0; i < participantCount; ++i)
        {
            int participantId = participantIds[i];
            serialize_int(stream, participantId, 0, Config::MAX_SERVER_PLAYERS - 1);
            if (Stream::IsReading)
            {
                participantIds[i] = static_cast<uint8_t>(participantId);
            }
        }

        int serializedLifecycle = static_cast<int>(lifecycle);
        serialize_int(stream, serializedLifecycle, static_cast<int>(eMissionSessionLifecycle::INACTIVE),
            static_cast<int>(eMissionSessionLifecycle::ABORTED));
        int serializedResult = static_cast<int>(result);
        serialize_int(stream, serializedResult, static_cast<int>(eMissionSessionResult::NONE),
            static_cast<int>(eMissionSessionResult::HOST_DISCONNECTED));
        if (Stream::IsReading)
        {
            lifecycle = static_cast<eMissionSessionLifecycle>(serializedLifecycle);
            result = static_cast<eMissionSessionResult>(serializedResult);
        }

        serialize_uint32(stream, stage);
        return HasValidParticipantRoster();
    }
};

class MissionSessionRequest : public Packet
{
    DEFINE_PACKET_TYPE(MissionSessionRequest, ePacketType::MISSION_SESSION_REQUEST, ePacketChannel::SCRIPT);

public:
    eMissionSessionRequestAction action = eMissionSessionRequestAction::LAUNCH;
    uint32_t requestId = 0;
    uint64_t sessionId = 0;
    uint32_t epoch = 0;
    uint16_t missionId = MISSION_ID_UNKNOWN;
    uint32_t stage = 0;
    eMissionSessionResult result = eMissionSessionResult::NONE;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        int serializedAction = static_cast<int>(action);
        serialize_int(stream, serializedAction, static_cast<int>(eMissionSessionRequestAction::LAUNCH),
            static_cast<int>(eMissionSessionRequestAction::ABORT));
        serialize_uint32(stream, requestId);
        serialize_uint64(stream, sessionId);
        serialize_uint32(stream, epoch);
        serialize_uint16(stream, missionId);
        serialize_uint32(stream, stage);
        int serializedResult = static_cast<int>(result);
        serialize_int(stream, serializedResult, static_cast<int>(eMissionSessionResult::NONE),
            static_cast<int>(eMissionSessionResult::HOST_DISCONNECTED));
        if (Stream::IsReading)
        {
            action = static_cast<eMissionSessionRequestAction>(serializedAction);
            result = static_cast<eMissionSessionResult>(serializedResult);
        }
        return IsMissionIdKnownOrUnknown(missionId);
    }
};

class OnMissionFlagSync : public Packet
{
    DEFINE_PACKET_TYPE(OnMissionFlagSync, ePacketType::ON_MISSION_FLAG_SYNC, ePacketChannel::SCRIPT);

public:
    bool bOnMission = false;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_bool(stream, bOnMission);
        return true;
    }
};

struct _EnExPayload
{
    uint8_t areaId = 0;
    SEntryExitFlags flags{};
    int16_t rectLeft = 0;
    int16_t rectBottom = 0;

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, areaId, AREA_MAIN_MAP, MAX_VISIBLE_AREAS - 1);
        serialize_object(stream, flags);
        serialize_int(stream, rectLeft, -3000, 3000);
        serialize_int(stream, rectBottom, -3000, 3000);
        return true;
    }
};

class EnExSync : public Packet
{
    DEFINE_PACKET_TYPE(EnExSync, ePacketType::ENEX_SYNC, ePacketChannel::EVENT);

public:
    static constexpr size_t MAX_ENEX_COUNT = 400;  // max length of the pool, got it here 0x43F927

    bool bDisabled = false;
    bool bBurglary = false;
    int count = 0;
    _EnExPayload enexes[MAX_ENEX_COUNT];

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_bool(stream, bDisabled);
        serialize_bool(stream, bBurglary);
        serialize_int(stream, count, 0, MAX_ENEX_COUNT);
        for (size_t i = 0; i < count; i++)
        {
            serialize_object(stream, enexes[i]);
        }
        return true;
    }
};

#ifdef COOP_SERVER
inline static EnExSync g_lastEnExData{};
inline static CNetworkPlayer* g_pLastEnExPlayerOwner = nullptr;
#endif

class AddMessageGXT : public Packet
{
    DEFINE_PACKET_TYPE(AddMessageGXT, ePacketType::ADD_MESSAGE_GXT, ePacketChannel::SCRIPT);

public:
    enum eGXTMsgType
    {
        sync_COMMAND_PRINT = 0,
        sync_COMMAND_PRINT_BIG,
        sync_COMMAND_PRINT_NOW,
        sync_COMMAND_PRINT_HELP
    };

    int forWhoPlayerId = 0;  // TODO(v0.3.1-alpha): dont send s2c
    eGXTMsgType type = sync_COMMAND_PRINT;
    uint32_t time = 0;
    uint8_t flag = 0;
    char gxt[8]{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, forWhoPlayerId, 0, Config::MAX_SERVER_PLAYERS - 1);
        serialize_int(stream, (int&)type, sync_COMMAND_PRINT, sync_COMMAND_PRINT_HELP);
        if (type != sync_COMMAND_PRINT_HELP)
        {
            serialize_uint32(stream, time);
            serialize_uint8(stream, flag);
        }

        serialize_string(stream, gxt, 8);
        gxt[ARRAY_SIZE(gxt) - 1] = '\0';
        return true;
    }
};

class RemoveMessageGXT : public Packet
{
    DEFINE_PACKET_TYPE(RemoveMessageGXT, ePacketType::REMOVE_MESSAGE_GXT, ePacketChannel::SCRIPT);

public:
    int forWhoPlayerId = 0;
    char gxt[8]{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, forWhoPlayerId, 0, Config::MAX_SERVER_PLAYERS - 1);

        serialize_string(stream, gxt, 8);
        gxt[ARRAY_SIZE(gxt) - 1] = '\0';
        return true;
    }
};

class StartCutscene : public Packet
{
    DEFINE_PACKET_TYPE(StartCutscene, ePacketType::START_CUTSCENE, ePacketChannel::SCRIPT);

public:
    char name[8];
    eVisibleArea currArea;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_string(stream, name, 8);
        name[ARRAY_SIZE(name) - 1] = '\0';
        serialize_int(stream, (int&)currArea, AREA_MAIN_MAP, MAX_VISIBLE_AREAS - 1);
        return true;
    }
};

class SkipCutscene : public Packet
{
    DEFINE_PACKET_TYPE(SkipCutscene, ePacketType::SKIP_CUTSCENE, ePacketChannel::SCRIPT);

public:
    SenderPlayerId playerid{};
    int votes = 0;  // temporary unused

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, playerid);
        serialize_int(stream, votes, 0, Config::MAX_SERVER_PLAYERS);
        return true;
    }
};

// Cutscene playback itself remains synchronized by the SCM opcode stream. These packets carry only the
// server-authoritative vote lifecycle and can never execute a cutscene start on a client.
enum class eCutsceneVoteLifecycle
{
    ACTIVE = 0,
    SKIPPED,
    ENDED
};

class CutsceneStartRequest : public Packet
{
    DEFINE_PACKET_TYPE(CutsceneStartRequest, ePacketType::CUTSCENE_START_REQUEST, ePacketChannel::SCRIPT);

public:
    uint64_t sessionId = 0;
    uint32_t missionEpoch = 0;
    uint32_t requestId = 0;
    char name[8]{};
    eVisibleArea currArea = AREA_MAIN_MAP;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_uint64(stream, sessionId);
        serialize_uint32(stream, missionEpoch);
        serialize_uint32(stream, requestId);
        serialize_string(stream, name, ARRAY_SIZE(name));
        name[ARRAY_SIZE(name) - 1] = '\0';
        int serializedArea = static_cast<int>(currArea);
        serialize_int(stream, serializedArea, AREA_MAIN_MAP, MAX_VISIBLE_AREAS - 1);
        if (Stream::IsReading)
        {
            currArea = static_cast<eVisibleArea>(serializedArea);
        }
        return sessionId != 0 && missionEpoch != 0 && requestId != 0;
    }
};

class CutsceneVoteRequest : public Packet
{
    DEFINE_PACKET_TYPE(CutsceneVoteRequest, ePacketType::CUTSCENE_VOTE_REQUEST, ePacketChannel::SCRIPT);

public:
    uint64_t sessionId = 0;
    uint32_t missionEpoch = 0;
    uint32_t cutsceneEpoch = 0;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_uint64(stream, sessionId);
        serialize_uint32(stream, missionEpoch);
        serialize_uint32(stream, cutsceneEpoch);
        return sessionId != 0 && missionEpoch != 0 && cutsceneEpoch != 0;
    }
};

class CutsceneEndRequest : public Packet
{
    DEFINE_PACKET_TYPE(CutsceneEndRequest, ePacketType::CUTSCENE_END_REQUEST, ePacketChannel::SCRIPT);

public:
    uint64_t sessionId = 0;
    uint32_t missionEpoch = 0;
    uint32_t cutsceneEpoch = 0;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_uint64(stream, sessionId);
        serialize_uint32(stream, missionEpoch);
        serialize_uint32(stream, cutsceneEpoch);
        return sessionId != 0 && missionEpoch != 0 && cutsceneEpoch != 0;
    }
};

class CutsceneVoteState : public Packet
{
    DEFINE_PACKET_TYPE(CutsceneVoteState, ePacketType::CUTSCENE_VOTE_STATE, ePacketChannel::SCRIPT);

public:
    uint64_t sessionId = 0;
    uint32_t missionEpoch = 0;
    uint32_t cutsceneEpoch = 0;
    uint32_t startRequestId = 0;
    uint8_t hostId = MISSION_PLAYER_ID_INVALID;
    uint8_t eligibleCount = 0;
    uint8_t voteCount = 0;
    uint8_t requiredVotes = 0;
    eCutsceneVoteLifecycle lifecycle = eCutsceneVoteLifecycle::ACTIVE;
    char name[8]{};
    eVisibleArea currArea = AREA_MAIN_MAP;

    bool HasValidVoteState() const
    {
        if (sessionId == 0 || missionEpoch == 0 || cutsceneEpoch == 0 || startRequestId == 0 ||
            hostId >= Config::MAX_SERVER_PLAYERS || eligibleCount == 0 ||
            eligibleCount > MISSION_SCM_GAMEPLAY_PLAYER_CAP || voteCount > eligibleCount ||
            requiredVotes != static_cast<uint8_t>(eligibleCount / 2 + 1))
        {
            return false;
        }

        if (lifecycle == eCutsceneVoteLifecycle::ACTIVE)
        {
            return voteCount < requiredVotes;
        }
        if (lifecycle == eCutsceneVoteLifecycle::SKIPPED)
        {
            return voteCount >= requiredVotes;
        }
        return lifecycle == eCutsceneVoteLifecycle::ENDED;
    }

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_uint64(stream, sessionId);
        serialize_uint32(stream, missionEpoch);
        serialize_uint32(stream, cutsceneEpoch);
        serialize_uint32(stream, startRequestId);

        int serializedHostId = hostId;
        serialize_int(stream, serializedHostId, 0, Config::MAX_SERVER_PLAYERS - 1);
        int serializedEligibleCount = eligibleCount;
        serialize_int(stream, serializedEligibleCount, 1, MISSION_SCM_GAMEPLAY_PLAYER_CAP);
        int serializedVoteCount = voteCount;
        serialize_int(stream, serializedVoteCount, 0, MISSION_SCM_GAMEPLAY_PLAYER_CAP);
        int serializedRequiredVotes = requiredVotes;
        serialize_int(stream, serializedRequiredVotes, 1, MISSION_SCM_GAMEPLAY_PLAYER_CAP);

        int serializedLifecycle = static_cast<int>(lifecycle);
        serialize_int(stream, serializedLifecycle, static_cast<int>(eCutsceneVoteLifecycle::ACTIVE),
            static_cast<int>(eCutsceneVoteLifecycle::ENDED));
        serialize_string(stream, name, ARRAY_SIZE(name));
        name[ARRAY_SIZE(name) - 1] = '\0';
        int serializedArea = static_cast<int>(currArea);
        serialize_int(stream, serializedArea, AREA_MAIN_MAP, MAX_VISIBLE_AREAS - 1);

        if (Stream::IsReading)
        {
            hostId = static_cast<uint8_t>(serializedHostId);
            eligibleCount = static_cast<uint8_t>(serializedEligibleCount);
            voteCount = static_cast<uint8_t>(serializedVoteCount);
            requiredVotes = static_cast<uint8_t>(serializedRequiredVotes);
            lifecycle = static_cast<eCutsceneVoteLifecycle>(serializedLifecycle);
            currArea = static_cast<eVisibleArea>(serializedArea);
        }
        return HasValidVoteState();
    }
};

class PlayMissionAudio : public Packet
{
    DEFINE_PACKET_TYPE(PlayMissionAudio, ePacketType::PLAY_MISSION_AUDIO, ePacketChannel::SCRIPT);

public:
    uint8_t slotid = 0;
    int audioid = 0;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, slotid, 0, 3);
        serialize_uint32(stream, audioid);
        return true;
    }
};

class TeleportPlayerScripted : public Packet
{
    DEFINE_PACKET_TYPE(TeleportPlayerScripted, ePacketType::TELEPORT_PLAYER_SCRIPTED, ePacketChannel::SCRIPT);

public:
    int playerid = 0;
    WorldPositionCompressed pos{};
    RadianAngleCompressed heading{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, playerid, 0, Config::MAX_SERVER_PLAYERS - 1);
        serialize_object(stream, pos);
        serialize_object(stream, heading);
        return true;
    }
};

class OpCodeSync : public Packet
{
    DEFINE_PACKET_TYPE(OpCodeSync, ePacketType::OPCODE_SYNC, ePacketChannel::SCRIPT);

public:
    // this number is the biggest possible serialized opcode in theory, the usual opcode size is ~4-50 bytes
    // sizeof(OpcodeSyncHeader) + NUM_SYNCED_PARAMS * sizeof(int) + NUM_SYNCED_PARAMS * sizeof(uint8_t) +
    // NUM_SYNCED_PARAMS * 256
    static constexpr int MAX_BUFFER_SIZE = 4 + 15 * sizeof(int) + 15 * sizeof(uint8_t) + 15 * 256;

    int size = 0;
    uint8_t buffer[MAX_BUFFER_SIZE]{};

    bool HasValidPayload() const
    {
        constexpr size_t HEADER_SIZE = 4;
        if (size < static_cast<int>(HEADER_SIZE) || size > MAX_BUFFER_SIZE)
        {
            return false;
        }

        // OpcodeSyncHeader stores its two four-bit counts in byte 2 and has one byte of tail padding.
        const uint8_t parameterCounts = buffer[2];
        const size_t integerParameterCount = parameterCounts & 0x0F;
        const size_t stringParameterCount = parameterCounts >> 4;
        size_t offset = HEADER_SIZE + integerParameterCount * sizeof(int32_t);
        if (offset > static_cast<size_t>(size))
        {
            return false;
        }

        for (size_t i = 0; i < stringParameterCount; ++i)
        {
            if (offset >= static_cast<size_t>(size))
            {
                return false;
            }
            const size_t stringLength = buffer[offset++];
            if (stringLength > static_cast<size_t>(size) - offset)
            {
                return false;
            }
            offset += stringLength;
        }
        return offset == static_cast<size_t>(size);
    }

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, size, 4, MAX_BUFFER_SIZE);
        serialize_bytes(stream, buffer, size);
        return HasValidPayload();
    }

public:
    std::string ToString() const override
    {
        static char str[32];
        snprintf(str, sizeof(str), "Opcode: 0x%x", *(uint16_t*)&buffer[0]);
        return str;
    }
};

class PerformTaskSequence : public Packet
{
    DEFINE_PACKET_TYPE(PerformTaskSequence, ePacketType::PERFORM_TASK_SEQUENCE, ePacketChannel::SCRIPT);

public:
    static constexpr int MAX_BUFFER_SIZE = 9 + 8 * 256;

    int size = 0;
    uint8_t buffer[MAX_BUFFER_SIZE]{};

    bool HasValidPayload() const
    {
        if (size < 9 || size > MAX_BUFFER_SIZE)
        {
            return false;
        }

        const size_t taskCount = buffer[8];
        if (taskCount == 0 || taskCount > 8)
        {
            return false;
        }

        size_t offset = 9;
        for (size_t i = 0; i < taskCount; ++i)
        {
            if (offset >= static_cast<size_t>(size))
            {
                return false;
            }

            const size_t taskLength = buffer[offset++];
            if (taskLength < 4 || taskLength > static_cast<size_t>(size) - offset)
            {
                return false;
            }

            OpCodeSync task{};
            task.size = static_cast<int>(taskLength);
            memcpy(task.buffer, buffer + offset, taskLength);
            if (!task.HasValidPayload())
            {
                return false;
            }
            offset += taskLength;
        }
        return offset == static_cast<size_t>(size);
    }

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, size, 9, MAX_BUFFER_SIZE);
        serialize_bytes(stream, buffer, size);
        return HasValidPayload();
    }
};

}  // namespace Packets::Scripts
