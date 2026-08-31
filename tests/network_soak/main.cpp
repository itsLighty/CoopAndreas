#include <string>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#endif

#include "config.h"
#include "enet/enet.h"
#include "network/packet.h"
#include "network/packets/players.h"
#include "network/packets/stunts.h"
#include "network/packets/system.h"
#include "semver.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>

void RegisterPacketPrototype(Packet* packet)
{
    delete packet;
}

namespace
{
using Clock = std::chrono::steady_clock;
using namespace Packets::Players;
using namespace Packets::Stunts;

constexpr size_t PACKET_BUFFER_SIZE = 10 * 1024;

template <typename PacketT>
bool DecodePacket(const uint8_t* data, size_t size, PacketT& packet)
{
    Packet& basePacket = packet;
    serialize::ReadStream stream(data, static_cast<int>(size));
    uint32_t packetType = 0;
    if (!stream.SerializeBits(packetType, 16) ||
        packetType != static_cast<uint16_t>(basePacket.GetType()))
    {
        return false;
    }
    if (basePacket.GetChannel() != ePacketChannel::SYSTEM)
    {
        uint32_t serverTime = 0;
        if (!stream.SerializeBits(serverTime, 32))
        {
            return false;
        }
        packet.serverTime = serverTime;
    }
    return basePacket.SerializeRead(stream);
}

template <typename Predicate>
bool WaitUntil(Predicate&& predicate, int timeoutMs, const std::function<void()>& pump)
{
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (Clock::now() < deadline)
    {
        pump();
        if (predicate())
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    pump();
    return predicate();
}

void Require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

class Bot
{
public:
    explicit Bot(std::string name) : m_name(std::move(name)) {}

    ~Bot() { DestroyTransport(); }

    void Connect(const std::string& hostName, uint16_t port, bool reconnect)
    {
        DestroyTransport();
        m_hostName = hostName;
        m_port = port;
        m_useReconnect = reconnect && m_hasCredential;
        m_transportConnected = false;
        m_authenticated = false;
        m_disconnected = false;
        m_id = -1;

        m_host = enet_host_create(nullptr, 1, static_cast<size_t>(ePacketChannel::COUNT), 0, 0);
        Require(m_host != nullptr, m_name + ": enet_host_create failed");

        ENetAddress address{};
        Require(enet_address_set_host(&address, m_hostName.c_str()) == 0,
            m_name + ": could not resolve server");
        address.port = m_port;
        const uint32_t version = semver_parse(COOPANDREAS_VERSION, nullptr);
        m_peer = enet_host_connect(
            m_host, &address, static_cast<size_t>(ePacketChannel::COUNT), version);
        Require(m_peer != nullptr, m_name + ": enet_host_connect failed");
    }

    void Pump()
    {
        if (!m_host)
        {
            return;
        }
        ENetEvent event{};
        while (enet_host_service(m_host, &event, 0) > 0)
        {
            switch (event.type)
            {
                case ENET_EVENT_TYPE_CONNECT:
                    m_transportConnected = true;
                    SendAuthentication();
                    break;
                case ENET_EVENT_TYPE_RECEIVE:
                    HandlePacket(event.packet->data, event.packet->dataLength);
                    enet_packet_destroy(event.packet);
                    break;
                case ENET_EVENT_TYPE_DISCONNECT:
                    m_transportConnected = false;
                    m_authenticated = false;
                    m_disconnected = true;
                    m_peer = nullptr;
                    break;
                default:
                    break;
            }
        }
    }

    template <typename PacketT>
    bool Send(PacketT& packet)
    {
        if (!m_host || !m_peer || !m_transportConnected)
        {
            return false;
        }

        Packet& basePacket = packet;
        std::array<uint8_t, PACKET_BUFFER_SIZE> buffer{};
        serialize::WriteStream stream(buffer.data(), static_cast<int>(buffer.size()));
        uint32_t packetType = static_cast<uint16_t>(basePacket.GetType());
        if (!stream.SerializeBits(packetType, 16))
        {
            return false;
        }
        if (basePacket.GetChannel() != ePacketChannel::SYSTEM)
        {
            uint32_t serverTime = enet_time_get();
            if (!stream.SerializeBits(serverTime, 32))
            {
                return false;
            }
        }
        if (!basePacket.SerializeWrite(stream))
        {
            return false;
        }
        stream.Flush();

        const uint32_t flags = GetChannelReliability(basePacket.GetChannel()) == ePacketReliability::RELIABLE
            ? ENET_PACKET_FLAG_RELIABLE
            : 0;
        ENetPacket* enetPacket =
            enet_packet_create(stream.GetData(), stream.GetBytesProcessed(), flags);
        if (!enetPacket)
        {
            return false;
        }
        if (enet_peer_send(m_peer, static_cast<uint8_t>(basePacket.GetChannel()), enetPacket) != 0)
        {
            enet_packet_destroy(enetPacket);
            return false;
        }
        enet_host_flush(m_host);
        return true;
    }

    void Disconnect()
    {
        if (m_peer && m_transportConnected)
        {
            enet_peer_disconnect(m_peer, 0);
            enet_host_flush(m_host);
        }
    }

    void DestroyTransport()
    {
        if (m_host)
        {
            if (m_peer)
            {
                enet_peer_reset(m_peer);
            }
            enet_host_destroy(m_host);
        }
        m_host = nullptr;
        m_peer = nullptr;
        m_transportConnected = false;
        m_authenticated = false;
    }

    void SendGameplayFrame(float offset)
    {
        OnFootUpdate onFoot{};
        onFoot.vecPos = CVector(2246.0f + offset, -1259.0f + offset, 24.0f);
        onFoot.vecMoveSpeed = CVector(0.01f, 0.0f, 0.0f);
        onFoot.currentRotation = 0.25f;
        onFoot.aimingRotation = 0.5f;
        onFoot.healthSnapshot.iHealth = 100;
        Require(Send(onFoot), m_name + ": failed to send on-foot update");

        PlayerCameraSync camera{};
        camera.bFullUpdate = true;
        camera.cameraMode = MODE_FOLLOWPED;
        camera.cameraFov = 70.0f;
        camera.front = CVector(0.0f, 1.0f, 0.0f);
        camera.source = CVector(2246.0f + offset, -1264.0f, 27.0f);
        camera.up = CVector(0.0f, 0.0f, 1.0f);
        camera.lookPitch = 0.0f;
        Require(Send(camera), m_name + ": failed to send camera update");

        KeyPressed keys{};
        keys.currentRotation = 0.25f;
        keys.aimingRotation = 0.5f;
        Require(Send(keys), m_name + ": failed to send key snapshot");
    }

    void SendClothes()
    {
        RebuildPlayer packet{};
        packet.clothesDesc.m_fFatStat = 0.0f;
        packet.clothesDesc.m_fMuscleStat = 0.0f;
        Require(Send(packet), m_name + ": failed to send clothes snapshot");
    }

    void SendStuntCatalog()
    {
        StuntDefinition definition = MakeBoundaryStuntDefinition();
        StuntDefinitionAnnounce packet{};
        packet.catalogCount = 1;
        packet.id = {0, definition.CalculateFingerprint()};
        packet.catalogHash = AccumulateCatalogHash(2166136261u, packet.id);
        packet.definition = definition;
        Require(packet.HasValidPayload(), m_name + ": stunt catalog self-validation failed");
        Require(Send(packet), m_name + ": failed to send stunt definition");
    }

    bool IsAuthenticated() const { return m_authenticated; }
    bool HasCredential() const { return m_hasCredential; }
    bool WasDisconnected() const { return m_disconnected; }
    int Id() const { return m_id; }
    int HostId() const { return m_hostId; }
    const std::string& Name() const { return m_name; }
    size_t OnFootCount() const { return m_onFootCount; }
    size_t CameraCount() const { return m_cameraCount; }
    size_t KeyCount() const { return m_keyCount; }
    size_t ClothesCount() const { return m_clothesCount; }
    size_t StuntStateCount() const { return m_stuntStateCount; }
    size_t DisconnectCount() const { return m_disconnectCount; }
    int LastOnFootPlayerId() const { return m_lastOnFootPlayerId; }
    int LastCameraPlayerId() const { return m_lastCameraPlayerId; }
    int LastKeyPlayerId() const { return m_lastKeyPlayerId; }

    bool HasSeen(const std::string& name) const
    {
        return std::any_of(m_players.begin(), m_players.end(),
            [&](const auto& entry) { return entry.second == name; });
    }

    static StuntDefinition MakeBoundaryStuntDefinition()
    {
        StuntDefinition definition{};
        definition.start.minimum = CVector(10.0045f, 20.0055f, 5.0045f);
        definition.start.maximum = CVector(14.0045f, 24.0055f, 9.0045f);
        definition.finish.minimum = CVector(30.0045f, 40.0055f, 10.0045f);
        definition.finish.maximum = CVector(34.0045f, 44.0055f, 14.0045f);
        definition.camera = CVector(22.0045f, 32.0055f, 18.0045f);
        definition.reward = 500;
        return definition;
    }

private:
    void SendAuthentication()
    {
        const uint32_t version = semver_parse(COOPANDREAS_VERSION, nullptr);
        if (m_useReconnect)
        {
            Packets::System::PlayerReconnectRequest request{};
            request.requestedPlayerId = m_credentialPlayerId;
            request.version = version;
            request.credential = m_credential;
            std::snprintf(request.name, sizeof(request.name), "%s", m_name.c_str());
            Require(Send(request), m_name + ": failed to send reconnect request");
            return;
        }

        Packets::System::PlayerConnected connected{};
        connected.payload.playerid = -1;
        connected.payload.isAlreadyConnected = false;
        connected.payload.version = version;
        std::snprintf(connected.payload.name, sizeof(connected.payload.name), "%s", m_name.c_str());
        Require(Send(connected), m_name + ": failed to send connection request");
    }

    void HandlePacket(const uint8_t* data, size_t size)
    {
        Require(size >= sizeof(uint16_t), m_name + ": received truncated packet header");
        serialize::ReadStream header(data, static_cast<int>(size));
        uint32_t rawType = 0;
        Require(header.SerializeBits(rawType, 16), m_name + ": failed to read packet type");
        const auto type = static_cast<ePacketType>(rawType);

        switch (type)
        {
            case ePacketType::PLAYER_HANDSHAKE:
            {
                Packets::System::PlayerHandshake packet{};
                Require(DecodePacket(data, size, packet), m_name + ": invalid handshake");
                m_id = packet.yourid;
                m_authenticated = true;
                break;
            }
            case ePacketType::PLAYER_RECONNECT_CREDENTIAL:
            {
                Packets::System::PlayerReconnectCredential packet{};
                Require(DecodePacket(data, size, packet), m_name + ": invalid reconnect credential");
                m_hasCredential = true;
                m_credentialPlayerId = packet.playerId;
                m_credential = packet.credential;

                Packets::System::PlayerReconnectCredentialAck ack{};
                ack.playerId = packet.playerId;
                ack.credential = packet.credential;
                Require(Send(ack), m_name + ": failed to acknowledge reconnect credential");
                break;
            }
            case ePacketType::PLAYER_CONNECTED:
            {
                Packets::System::PlayerConnected packet{};
                Require(DecodePacket(data, size, packet), m_name + ": invalid player-connected packet");
                m_players[packet.payload.playerid] = packet.payload.name;
                break;
            }
            case ePacketType::PLAYER_DISCONNECTED:
            {
                Packets::System::PlayerDisconnected packet{};
                Require(DecodePacket(data, size, packet), m_name + ": invalid player-disconnected packet");
                m_players.erase(packet.payload.playerid);
                ++m_disconnectCount;
                break;
            }
            case ePacketType::PLAYER_ASSIGN_HOST:
            {
                Packets::System::PlayerAssignHost packet{};
                Require(DecodePacket(data, size, packet), m_name + ": invalid host assignment");
                m_hostId = packet.playerid;
                break;
            }
            case ePacketType::PLAYER_ONFOOT_UPDATE:
            {
                OnFootUpdate packet{};
                Require(DecodePacket(data, size, packet), m_name + ": invalid relayed on-foot packet");
                m_lastOnFootPlayerId = packet.playerid.value;
                ++m_onFootCount;
                break;
            }
            case ePacketType::PLAYER_CAMERA_SYNC:
            {
                PlayerCameraSync packet{};
                Require(DecodePacket(data, size, packet), m_name + ": invalid relayed camera packet");
                m_lastCameraPlayerId = packet.playerid.value;
                ++m_cameraCount;
                break;
            }
            case ePacketType::PLAYER_KEY_SYNC:
            {
                KeyPressed packet{};
                Require(DecodePacket(data, size, packet), m_name + ": invalid relayed key packet");
                m_lastKeyPlayerId = packet.playerid.value;
                ++m_keyCount;
                break;
            }
            case ePacketType::REBUILD_PLAYER:
            {
                RebuildPlayer packet{};
                Require(DecodePacket(data, size, packet), m_name + ": invalid relayed clothes packet");
                ++m_clothesCount;
                break;
            }
            case ePacketType::STUNT_STATE:
            {
                StuntStateEvent packet{};
                Require(DecodePacket(data, size, packet), m_name + ": invalid stunt-state packet");
                ++m_stuntStateCount;
                break;
            }
            default:
                break;
        }
    }

    std::string m_name;
    std::string m_hostName;
    uint16_t m_port = 0;
    ENetHost* m_host = nullptr;
    ENetPeer* m_peer = nullptr;
    bool m_useReconnect = false;
    bool m_transportConnected = false;
    bool m_authenticated = false;
    bool m_disconnected = false;
    bool m_hasCredential = false;
    int m_id = -1;
    int m_hostId = -1;
    int m_credentialPlayerId = -1;
    Packets::System::ReconnectCredential m_credential{};
    std::map<int, std::string> m_players;
    size_t m_onFootCount = 0;
    size_t m_cameraCount = 0;
    size_t m_keyCount = 0;
    size_t m_clothesCount = 0;
    size_t m_stuntStateCount = 0;
    size_t m_disconnectCount = 0;
    int m_lastOnFootPlayerId = -1;
    int m_lastCameraPlayerId = -1;
    int m_lastKeyPlayerId = -1;
};

void RunStuntCodecRegression()
{
    const StuntDefinition definition = Bot::MakeBoundaryStuntDefinition();
    StuntDefinitionAnnounce source{};
    source.catalogCount = 1;
    source.id = {0, definition.CalculateFingerprint()};
    source.catalogHash = AccumulateCatalogHash(2166136261u, source.id);
    source.definition = definition;

    std::array<uint8_t, PACKET_BUFFER_SIZE> buffer{};
    serialize::WriteStream writer(buffer.data(), static_cast<int>(buffer.size()));
    Require(static_cast<Packet&>(source).SerializeWrite(writer),
        "stunt definition failed to serialize");
    writer.Flush();

    StuntDefinitionAnnounce decoded{};
    serialize::ReadStream reader(writer.GetData(), writer.GetBytesProcessed());
    Require(static_cast<Packet&>(decoded).SerializeRead(reader),
        "wire-quantized stunt definition failed validation");
    Require(decoded.id.fingerprint == source.id.fingerprint,
        "stunt fingerprint changed across wire quantization");
}

struct Options
{
    std::string host = "127.0.0.1";
    uint16_t port = Config::DEFAULT_PORT;
    int cycles = 8;
    int durationMs = 5000;
};

Options ParseOptions(int argc, char** argv)
{
    Options options{};
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        auto requireValue = [&]() -> const char* {
            Require(index + 1 < argc, "missing value after " + argument);
            return argv[++index];
        };
        if (argument == "--host")
        {
            options.host = requireValue();
        }
        else if (argument == "--port")
        {
            options.port = static_cast<uint16_t>(std::stoi(requireValue()));
        }
        else if (argument == "--cycles")
        {
            options.cycles = std::max(0, std::stoi(requireValue()));
        }
        else if (argument == "--duration-ms")
        {
            options.durationMs = std::max(1000, std::stoi(requireValue()));
        }
        else
        {
            throw std::runtime_error("unknown option: " + argument);
        }
    }
    return options;
}

int Run(const Options& options)
{
    RunStuntCodecRegression();
    Require(enet_initialize() == 0, "enet_initialize failed");

    Bot alpha("SoakAlpha");
    Bot bravo("SoakBravo");
    auto pump = [&]() {
        alpha.Pump();
        bravo.Pump();
    };

    alpha.Connect(options.host, options.port, false);
    Require(WaitUntil([&] {
        return alpha.IsAuthenticated() && alpha.HasCredential();
    }, 10000, pump), "first client authentication timed out");
    alpha.SendGameplayFrame(0.0f);

    bravo.Connect(options.host, options.port, false);
    Require(WaitUntil([&] {
        return alpha.IsAuthenticated() && bravo.IsAuthenticated() &&
               alpha.HasCredential() && bravo.HasCredential() &&
               alpha.HasSeen(bravo.Name()) && bravo.HasSeen(alpha.Name());
    }, 10000, pump), "initial two-client authentication/roster exchange timed out");
    Require(alpha.Id() != bravo.Id(), "server assigned duplicate player IDs");

    alpha.SendGameplayFrame(0.0f);
    bravo.SendGameplayFrame(1.0f);
    alpha.SendClothes();
    bravo.SendClothes();
    Require(WaitUntil([&] {
        return alpha.OnFootCount() > 0 && bravo.OnFootCount() > 0 &&
               alpha.CameraCount() > 0 && bravo.CameraCount() > 0 &&
               alpha.KeyCount() > 0 && bravo.KeyCount() > 0 &&
               alpha.ClothesCount() > 0 && bravo.ClothesCount() > 0;
    }, 5000, pump), "initial gameplay/camera/clothes relay timed out");
    Require(alpha.LastOnFootPlayerId() == bravo.Id() &&
            alpha.LastCameraPlayerId() == bravo.Id() &&
            alpha.LastKeyPlayerId() == bravo.Id(),
        "alpha received a relay with an incorrect sender ID");
    Require(bravo.LastOnFootPlayerId() == alpha.Id() &&
            bravo.LastCameraPlayerId() == alpha.Id() &&
            bravo.LastKeyPlayerId() == alpha.Id(),
        "bravo received a relay with an incorrect sender ID");

    Bot* host = alpha.HostId() == alpha.Id() ? &alpha :
        (bravo.HostId() == bravo.Id() ? &bravo : nullptr);
    if (host)
    {
        host->SendStuntCatalog();
        Require(WaitUntil([&] {
            return alpha.StuntStateCount() > 0 && bravo.StuntStateCount() > 0;
        }, 5000, pump), "wire-quantized stunt catalog was not accepted and broadcast");
    }

    for (int cycle = 0; cycle < options.cycles; ++cycle)
    {
        const size_t disconnectsBefore = alpha.DisconnectCount();
        bravo.Disconnect();
        Require(WaitUntil([&] {
            return bravo.WasDisconnected() && alpha.DisconnectCount() > disconnectsBefore;
        }, 5000, pump), "disconnect propagation timed out at cycle " + std::to_string(cycle));

        bravo.Connect(options.host, options.port, true);
        Require(WaitUntil([&] {
            return bravo.IsAuthenticated() && bravo.HasCredential() &&
                   alpha.HasSeen(bravo.Name()) && bravo.HasSeen(alpha.Name());
        }, 10000, pump), "reconnect/roster exchange timed out at cycle " + std::to_string(cycle));

        const size_t alphaOnFootBefore = alpha.OnFootCount();
        const size_t bravoOnFootBefore = bravo.OnFootCount();
        alpha.SendGameplayFrame(static_cast<float>(cycle) * 0.1f);
        bravo.SendGameplayFrame(1.0f + static_cast<float>(cycle) * 0.1f);
        bravo.SendClothes();
        Require(WaitUntil([&] {
            return alpha.OnFootCount() > alphaOnFootBefore &&
                   bravo.OnFootCount() > bravoOnFootBefore;
        }, 5000, pump), "post-reconnect gameplay relay timed out at cycle " + std::to_string(cycle));
    }

    const size_t alphaFramesBefore = alpha.OnFootCount();
    const size_t bravoFramesBefore = bravo.OnFootCount();
    const auto soakDeadline = Clock::now() + std::chrono::milliseconds(options.durationMs);
    int frame = 0;
    while (Clock::now() < soakDeadline)
    {
        alpha.SendGameplayFrame(static_cast<float>(frame % 20) * 0.02f);
        bravo.SendGameplayFrame(1.0f + static_cast<float>(frame % 20) * 0.02f);
        if (frame % 20 == 0)
        {
            alpha.SendClothes();
            bravo.SendClothes();
        }
        const auto frameDeadline = Clock::now() + std::chrono::milliseconds(50);
        while (Clock::now() < frameDeadline)
        {
            pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ++frame;
    }
    pump();

    Require(alpha.OnFootCount() >= alphaFramesBefore + 20,
        "alpha received too few sustained gameplay frames");
    Require(bravo.OnFootCount() >= bravoFramesBefore + 20,
        "bravo received too few sustained gameplay frames");

    std::printf(
        "PASS two-client soak: cycles=%d frames=%d alpha(onfoot=%zu camera=%zu keys=%zu clothes=%zu) "
        "bravo(onfoot=%zu camera=%zu keys=%zu clothes=%zu)\n",
        options.cycles, frame,
        alpha.OnFootCount(), alpha.CameraCount(), alpha.KeyCount(), alpha.ClothesCount(),
        bravo.OnFootCount(), bravo.CameraCount(), bravo.KeyCount(), bravo.ClothesCount());

    alpha.Disconnect();
    bravo.Disconnect();
    WaitUntil([&] {
        return alpha.WasDisconnected() && bravo.WasDisconnected();
    }, 2000, pump);
    alpha.DestroyTransport();
    bravo.DestroyTransport();
    enet_deinitialize();
    return 0;
}
}  // namespace

int main(int argc, char** argv)
{
    try
    {
        return Run(ParseOptions(argc, argv));
    }
    catch (const std::exception& exception)
    {
        std::fprintf(stderr, "FAIL two-client soak: %s\n", exception.what());
        enet_deinitialize();
        return 1;
    }
}
