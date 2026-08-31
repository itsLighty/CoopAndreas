#pragma once

#include <deque>

enum class eNetworkTransformSource : uint8_t
{
    NONE = 0,
    PLAYER_ON_FOOT,
    PED_ON_FOOT,
    VEHICLE_IDLE,
    VEHICLE_PLAYER_DRIVER,
    VEHICLE_PED_DRIVER
};

struct CNetworkTransformSnapshot
{
    server_time_t serverTime = 0;
    CVector position{};
    CVector velocity{};
    CVector turnSpeed{};
    CVector right{1.0f, 0.0f, 0.0f};
    CVector forward{0.0f, 1.0f, 0.0f};
    float currentRotation = 0.0f;
    float aimingRotation = 0.0f;
    float lookDirection = 0.0f;
    uint32_t sourceId = 0;
    uint8_t area = AREA_MAIN_MAP;
    eNetworkTransformSource source = eNetworkTransformSource::NONE;
    bool hasVehicleOrientation = false;
};

struct CNetworkTransformSample
{
    CVector position{};
    CVector velocity{};
    CVector turnSpeed{};
    CVector right{1.0f, 0.0f, 0.0f};
    CVector forward{0.0f, 1.0f, 0.0f};
    CVector up{0.0f, 0.0f, 1.0f};
    float currentRotation = 0.0f;
    float aimingRotation = 0.0f;
    float lookDirection = 0.0f;
    bool hasVehicleOrientation = false;
    bool heldAtBoundary = false;
};

class CNetworkTransformInterpolator
{
public:
    static constexpr size_t MAX_SNAPSHOTS = 12;
    static constexpr uint32_t MIN_RENDER_DELAY_MS = Config::INTERP_BUFFER_DELAY_MS + 50;
    static constexpr uint32_t MAX_RENDER_DELAY_MS = 200;
    static constexpr uint32_t MAX_PACKET_GAP_MS = 1000;
    static constexpr uint32_t STALE_PRESENTATION_MS = 750;

    bool Push(const CNetworkTransformSnapshot& snapshot, float teleportDistance);
    bool Sample(server_time_t serverNow, uint32_t sourceRtt, CNetworkTransformSample& output);
    bool UpdateNewestAngles(server_time_t serverTime, float currentRotation, float aimingRotation);
    void Reset();
    bool ResetAt(server_time_t boundaryTime);
    bool ResetForCrossChannelBoundary(server_time_t boundaryTime);
    void ClearSnapshots();

    bool Empty() const { return m_snapshots.empty(); }
    size_t Size() const { return m_snapshots.size(); }
    uint32_t GetRejectedStaleCount() const { return m_rejectedStaleCount; }

private:
    static bool IsNewer(server_time_t candidate, server_time_t reference);
    static int32_t TimeDelta(server_time_t candidate, server_time_t reference);
    static uint32_t CalculateRenderDelay(uint32_t sourceRtt);
    static CNetworkTransformSample Interpolate(
        const CNetworkTransformSnapshot& from, const CNetworkTransformSnapshot& to, float alpha);

    std::deque<CNetworkTransformSnapshot> m_snapshots;
    server_time_t m_lastAcceptedServerTime = 0;
    server_time_t m_lastAngleServerTime = 0;
    server_time_t m_lastReceiveServerTime = 0;
    uint32_t m_rejectedStaleCount = 0;
    bool m_hasAcceptedServerTime = false;
};
