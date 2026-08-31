#include "stdafx.h"
#include "CNetworkTransformInterpolator.h"
#include "CServerTime.h"

namespace
{
constexpr float PI = 3.14159265358979323846f;
constexpr float TWO_PI = PI * 2.0f;
constexpr float VECTOR_EPSILON = 0.000001f;

struct Quaternion
{
    float w = 1.0f;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

float Clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

float Dot(const CVector& left, const CVector& right)
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

CVector Cross(const CVector& left, const CVector& right)
{
    return CVector(left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x);
}

CVector Normalized(const CVector& value, const CVector& fallback)
{
    const float lengthSquared = Dot(value, value);
    if (!std::isfinite(lengthSquared) || lengthSquared <= VECTOR_EPSILON)
        return fallback;
    const float inverseLength = 1.0f / std::sqrt(lengthSquared);
    return value * inverseLength;
}

CVector Lerp(const CVector& from, const CVector& to, float alpha)
{
    return from + (to - from) * alpha;
}

float NormalizeAngle(float angle)
{
    if (!std::isfinite(angle))
        return 0.0f;
    angle = std::fmod(angle + PI, TWO_PI);
    if (angle < 0.0f)
        angle += TWO_PI;
    return angle - PI;
}

float LerpAngleShortest(float from, float to, float alpha)
{
    return NormalizeAngle(from + NormalizeAngle(to - from) * alpha);
}

void Orthonormalize(const CVector& sourceRight, const CVector& sourceForward,
    CVector& right, CVector& forward, CVector& up)
{
    right = Normalized(sourceRight, CVector(1.0f, 0.0f, 0.0f));
    CVector projectedForward = sourceForward - right * Dot(sourceForward, right);
    forward = Normalized(projectedForward, CVector(0.0f, 1.0f, 0.0f));
    up = Normalized(Cross(right, forward), CVector(0.0f, 0.0f, 1.0f));
    forward = Normalized(Cross(up, right), forward);
}

Quaternion NormalizeQuaternion(Quaternion value)
{
    const float lengthSquared = value.w * value.w + value.x * value.x + value.y * value.y + value.z * value.z;
    if (!std::isfinite(lengthSquared) || lengthSquared <= VECTOR_EPSILON)
        return {};
    const float inverseLength = 1.0f / std::sqrt(lengthSquared);
    value.w *= inverseLength;
    value.x *= inverseLength;
    value.y *= inverseLength;
    value.z *= inverseLength;
    return value;
}

Quaternion BasisToQuaternion(const CVector& sourceRight, const CVector& sourceForward)
{
    CVector right{}, forward{}, up{};
    Orthonormalize(sourceRight, sourceForward, right, forward, up);

    // GTA's CMatrix basis is right/forward/up in the right/up/at fields respectively.
    const float m00 = right.x;
    const float m01 = forward.x;
    const float m02 = up.x;
    const float m10 = right.y;
    const float m11 = forward.y;
    const float m12 = up.y;
    const float m20 = right.z;
    const float m21 = forward.z;
    const float m22 = up.z;

    Quaternion result{};
    const float trace = m00 + m11 + m22;
    if (trace > 0.0f)
    {
        const float scale = std::sqrt(trace + 1.0f) * 2.0f;
        result.w = 0.25f * scale;
        result.x = (m21 - m12) / scale;
        result.y = (m02 - m20) / scale;
        result.z = (m10 - m01) / scale;
    }
    else if (m00 > m11 && m00 > m22)
    {
        const float scale = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        result.w = (m21 - m12) / scale;
        result.x = 0.25f * scale;
        result.y = (m01 + m10) / scale;
        result.z = (m02 + m20) / scale;
    }
    else if (m11 > m22)
    {
        const float scale = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        result.w = (m02 - m20) / scale;
        result.x = (m01 + m10) / scale;
        result.y = 0.25f * scale;
        result.z = (m12 + m21) / scale;
    }
    else
    {
        const float scale = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        result.w = (m10 - m01) / scale;
        result.x = (m02 + m20) / scale;
        result.y = (m12 + m21) / scale;
        result.z = 0.25f * scale;
    }
    return NormalizeQuaternion(result);
}

Quaternion SlerpShortest(Quaternion from, Quaternion to, float alpha)
{
    from = NormalizeQuaternion(from);
    to = NormalizeQuaternion(to);
    float dot = from.w * to.w + from.x * to.x + from.y * to.y + from.z * to.z;
    if (dot < 0.0f)
    {
        dot = -dot;
        to.w = -to.w;
        to.x = -to.x;
        to.y = -to.y;
        to.z = -to.z;
    }

    if (dot > 0.9995f)
    {
        Quaternion result{};
        result.w = from.w + (to.w - from.w) * alpha;
        result.x = from.x + (to.x - from.x) * alpha;
        result.y = from.y + (to.y - from.y) * alpha;
        result.z = from.z + (to.z - from.z) * alpha;
        return NormalizeQuaternion(result);
    }

    const float theta = std::acos(std::clamp(dot, -1.0f, 1.0f));
    const float sinTheta = std::sin(theta);
    if (std::abs(sinTheta) <= VECTOR_EPSILON)
        return from;
    const float fromWeight = std::sin((1.0f - alpha) * theta) / sinTheta;
    const float toWeight = std::sin(alpha * theta) / sinTheta;
    Quaternion result{};
    result.w = from.w * fromWeight + to.w * toWeight;
    result.x = from.x * fromWeight + to.x * toWeight;
    result.y = from.y * fromWeight + to.y * toWeight;
    result.z = from.z * fromWeight + to.z * toWeight;
    return NormalizeQuaternion(result);
}

void QuaternionToBasis(const Quaternion& source, CVector& right, CVector& forward, CVector& up)
{
    const Quaternion q = NormalizeQuaternion(source);
    const float xx = q.x * q.x;
    const float yy = q.y * q.y;
    const float zz = q.z * q.z;
    const float xy = q.x * q.y;
    const float xz = q.x * q.z;
    const float yz = q.y * q.z;
    const float wx = q.w * q.x;
    const float wy = q.w * q.y;
    const float wz = q.w * q.z;

    right = CVector(1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz), 2.0f * (xz - wy));
    forward = CVector(2.0f * (xy - wz), 1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx));
    up = CVector(2.0f * (xz + wy), 2.0f * (yz - wx), 1.0f - 2.0f * (xx + yy));
    Orthonormalize(right, forward, right, forward, up);
}

float DistanceSquared(const CVector& left, const CVector& right)
{
    const CVector delta = left - right;
    return Dot(delta, delta);
}
}  // namespace

bool CNetworkTransformInterpolator::IsNewer(server_time_t candidate, server_time_t reference)
{
    return TimeDelta(candidate, reference) > 0;
}

int32_t CNetworkTransformInterpolator::TimeDelta(server_time_t candidate, server_time_t reference)
{
    return static_cast<int32_t>(candidate - reference);
}

uint32_t CNetworkTransformInterpolator::CalculateRenderDelay(uint32_t sourceRtt)
{
    const uint32_t localRtt = std::min<uint32_t>(CNetwork::GetRTT(), 1000);
    sourceRtt = std::min<uint32_t>(sourceRtt, 1000);
    const uint32_t pathAllowance = std::min<uint32_t>((localRtt + sourceRtt) / 4, 100);
    return std::clamp<uint32_t>(MIN_RENDER_DELAY_MS + pathAllowance,
        MIN_RENDER_DELAY_MS, MAX_RENDER_DELAY_MS);
}

bool CNetworkTransformInterpolator::Push(const CNetworkTransformSnapshot& incoming, float teleportDistance)
{
    CNetworkTransformSnapshot snapshot = incoming;
    if (snapshot.serverTime == 0)
        snapshot.serverTime = g_serverTime;

    const int32_t acceptedDelta = m_hasAcceptedServerTime
        ? TimeDelta(snapshot.serverTime, m_lastAcceptedServerTime)
        : 1;
    if (m_hasAcceptedServerTime && (acceptedDelta < 0 || (acceptedDelta == 0 && !m_snapshots.empty())))
    {
        ++m_rejectedStaleCount;
        return false;
    }

    bool resetBoundary = false;
    if (!m_snapshots.empty())
    {
        const CNetworkTransformSnapshot& previous = m_snapshots.back();
        const int32_t packetGap = TimeDelta(snapshot.serverTime, previous.serverTime);
        const bool sourceChanged = previous.source != snapshot.source || previous.sourceId != snapshot.sourceId;
        const bool areaChanged = previous.area != snapshot.area;
        const bool resumedAfterStalePresentation = m_lastReceiveServerTime != 0 &&
            TimeDelta(g_serverTime, m_lastReceiveServerTime) > static_cast<int32_t>(STALE_PRESENTATION_MS);
        const float teleportDistanceSquared = teleportDistance * teleportDistance;
        const bool teleported = teleportDistance > 0.0f &&
            DistanceSquared(previous.position, snapshot.position) > teleportDistanceSquared;
        resetBoundary = sourceChanged || areaChanged || resumedAfterStalePresentation ||
            packetGap > static_cast<int32_t>(MAX_PACKET_GAP_MS) || teleported;
    }

    if (resetBoundary)
        m_snapshots.clear();

    m_snapshots.push_back(snapshot);
    while (m_snapshots.size() > MAX_SNAPSHOTS)
        m_snapshots.pop_front();
    m_lastAcceptedServerTime = snapshot.serverTime;
    if (m_lastAngleServerTime == 0 || IsNewer(snapshot.serverTime, m_lastAngleServerTime))
        m_lastAngleServerTime = snapshot.serverTime;
    m_lastReceiveServerTime = g_serverTime;
    m_hasAcceptedServerTime = true;
    return true;
}

bool CNetworkTransformInterpolator::UpdateNewestAngles(
    server_time_t serverTime, float currentRotation, float aimingRotation)
{
    if (m_snapshots.empty())
        return false;
    if (serverTime != 0 && m_lastAngleServerTime != 0 &&
        TimeDelta(serverTime, m_lastAngleServerTime) < 0)
        return false;
    m_snapshots.back().currentRotation = currentRotation;
    m_snapshots.back().aimingRotation = aimingRotation;
    if (serverTime != 0)
        m_lastAngleServerTime = serverTime;
    return true;
}

CNetworkTransformSample CNetworkTransformInterpolator::Interpolate(
    const CNetworkTransformSnapshot& from, const CNetworkTransformSnapshot& to, float alpha)
{
    alpha = Clamp01(alpha);
    CNetworkTransformSample output{};
    output.position = Lerp(from.position, to.position, alpha);
    output.velocity = Lerp(from.velocity, to.velocity, alpha);
    output.turnSpeed = Lerp(from.turnSpeed, to.turnSpeed, alpha);
    output.currentRotation = LerpAngleShortest(from.currentRotation, to.currentRotation, alpha);
    output.aimingRotation = LerpAngleShortest(from.aimingRotation, to.aimingRotation, alpha);
    output.lookDirection = LerpAngleShortest(from.lookDirection, to.lookDirection, alpha);
    output.hasVehicleOrientation = from.hasVehicleOrientation && to.hasVehicleOrientation;
    if (output.hasVehicleOrientation)
    {
        const Quaternion orientation = SlerpShortest(
            BasisToQuaternion(from.right, from.forward), BasisToQuaternion(to.right, to.forward), alpha);
        QuaternionToBasis(orientation, output.right, output.forward, output.up);
    }
    return output;
}

bool CNetworkTransformInterpolator::Sample(
    server_time_t serverNow, uint32_t sourceRtt, CNetworkTransformSample& output)
{
    if (m_snapshots.empty())
        return false;

    const uint32_t delay = CalculateRenderDelay(sourceRtt);
    const server_time_t renderTime = serverNow - delay;

    while (m_snapshots.size() > 2 && TimeDelta(renderTime, m_snapshots[1].serverTime) >= 0)
        m_snapshots.pop_front();

    if (m_snapshots.size() == 1 || TimeDelta(renderTime, m_snapshots.front().serverTime) <= 0)
    {
        output = Interpolate(m_snapshots.front(), m_snapshots.front(), 0.0f);
        output.heldAtBoundary = true;
        return true;
    }

    if (TimeDelta(renderTime, m_snapshots.back().serverTime) >= 0)
    {
        output = Interpolate(m_snapshots.back(), m_snapshots.back(), 0.0f);
        output.heldAtBoundary = true;
        // Holding the newest authoritative transform deliberately avoids unbounded dead reckoning.
        if (TimeDelta(serverNow, m_lastReceiveServerTime) > static_cast<int32_t>(STALE_PRESENTATION_MS))
        {
            const CNetworkTransformSnapshot newest = m_snapshots.back();
            m_snapshots.clear();
            m_snapshots.push_back(newest);
        }
        return true;
    }

    const CNetworkTransformSnapshot& from = m_snapshots.front();
    const CNetworkTransformSnapshot& to = m_snapshots[1];
    const int32_t span = std::max<int32_t>(1, TimeDelta(to.serverTime, from.serverTime));
    const int32_t elapsed = std::clamp<int32_t>(TimeDelta(renderTime, from.serverTime), 0, span);
    output = Interpolate(from, to, static_cast<float>(elapsed) / static_cast<float>(span));
    return true;
}

void CNetworkTransformInterpolator::Reset()
{
    m_snapshots.clear();
    m_lastAcceptedServerTime = 0;
    m_lastAngleServerTime = 0;
    m_lastReceiveServerTime = 0;
    m_hasAcceptedServerTime = false;
}

bool CNetworkTransformInterpolator::ResetAt(server_time_t boundaryTime)
{
    if (boundaryTime != 0 && m_hasAcceptedServerTime &&
        TimeDelta(boundaryTime, m_lastAcceptedServerTime) < 0)
    {
        ++m_rejectedStaleCount;
        return false;
    }
    Reset();
    if (boundaryTime == 0)
        return true;
    m_lastAcceptedServerTime = boundaryTime;
    m_lastAngleServerTime = boundaryTime;
    m_lastReceiveServerTime = g_serverTime;
    m_hasAcceptedServerTime = true;
    return true;
}

bool CNetworkTransformInterpolator::ResetForCrossChannelBoundary(server_time_t boundaryTime)
{
    if (boundaryTime == 0)
    {
        Reset();
        return true;
    }

    if (m_hasAcceptedServerTime && TimeDelta(boundaryTime, m_lastAcceptedServerTime) < 0)
    {
        // A reliable lifecycle event may cross an unreliable transform update in transit. The
        // event still invalidates the buffered presentation, but its older pose must not move
        // the authoritative transform watermark backwards.
        m_snapshots.clear();
        m_lastReceiveServerTime = g_serverTime;
        ++m_rejectedStaleCount;
        return false;
    }

    return ResetAt(boundaryTime);
}

void CNetworkTransformInterpolator::ClearSnapshots()
{
    m_snapshots.clear();
    m_lastReceiveServerTime = g_serverTime;
}
