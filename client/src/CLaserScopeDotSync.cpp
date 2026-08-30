#include "stdafx.h"
#include "CLaserScopeDotSync.h"

#include <CCoronas.h>
#include <cmath>

namespace
{
bool g_localDotActive = false;
WorldPositionCompressed g_localDotPosition{};
float g_localDotSize = 0.0f;

bool IsSniperCameraMode(uint16_t cameraMode)
{
    return cameraMode == MODE_SNIPER || cameraMode == MODE_SNIPER_RUNABOUT;
}

bool IsFiniteNativeResult(const CVector& position, float size)
{
    return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z) &&
           position.x >= -3000.0f && position.x <= 3000.0f && position.y >= -3000.0f &&
           position.y <= 3000.0f && position.z >= -120.0f && position.z <= 1000.0f &&
           std::isfinite(size) && size >= Packets::Players::PlayerCameraSync::LASER_DOT_MIN_SIZE &&
           size <= Packets::Players::PlayerCameraSync::LASER_DOT_MAX_SIZE;
}

void ClearLocalState()
{
    g_localDotActive = false;
    g_localDotPosition = {};
    g_localDotSize = 0.0f;
}

void CaptureLocalDot()
{
    ClearLocalState();

    CPlayerPed* player = FindPlayerPed(0);
    if (!CNetwork::m_bAuthenticated || CWorld::PlayerInFocus != 0 || player == nullptr || !player->IsAlive() ||
        player->m_nPedFlags.bInVehicle)
    {
        return;
    }

    const uint16_t cameraMode = TheCamera.m_aCams[TheCamera.m_nActiveCam].m_nMode;
    CWeapon& weapon = player->GetWeapon();
    if (weapon.m_eWeaponType != WEAPON_SNIPERRIFLE || !IsSniperCameraMode(cameraMode))
    {
        return;
    }

    CVector position{};
    float size = 0.0f;
    // GTA SA 1.0 US 0x73A8D0 performs the native camera raycast and registers the local red corona.
    if (!weapon.LaserScopeDot(&position, &size) || !IsFiniteNativeResult(position, size))
    {
        return;
    }

    g_localDotActive = true;
    g_localDotPosition = position;
    g_localDotSize = size;
}

void RenderRemoteDot(CNetworkPlayer* player, uint32_t now)
{
    if (player == nullptr || !player->m_cameraSnapshot.bLaserScopeDotActive ||
        player->m_nLaserScopeDotReceivedAt == 0)
    {
        return;
    }

    if (now - player->m_nLaserScopeDotReceivedAt > CLaserScopeDotSync::STALE_TIMEOUT_MS)
    {
        player->m_cameraSnapshot.bLaserScopeDotActive = false;
        player->m_cameraSnapshot.laserScopeDotPosition = {};
        player->m_cameraSnapshot.laserScopeDotSize = 0.0f;
        player->m_nLaserScopeDotReceivedAt = 0;
        return;
    }

    CPlayerPed* ped = player->m_pPed;
    if (ped == nullptr || !ped->IsAlive() || ped->m_nPedFlags.bInVehicle ||
        ped->GetWeapon().m_eWeaponType != WEAPON_SNIPERRIFLE)
    {
        return;
    }

    const Packets::Players::PlayerCameraSync& state = player->m_cameraSnapshot;
    if (!state.IsLaserScopeDotSemanticallyValid())
    {
        return;
    }

    // Mirror the constants and unique weapon-address ID used by native CWeapon::LaserScopeDot. The projected
    // sender-side size is validated on the wire but is intentionally not used as a world radius: each observer's
    // corona renderer projects the fixed native 1.2f radius from its own camera.
    const unsigned int coronaId = static_cast<unsigned int>(
        reinterpret_cast<uintptr_t>(&ped->GetWeapon()) + 7u);
    CCoronas::RegisterCorona(coronaId, nullptr, 128, 0, 0, 255, state.laserScopeDotPosition, 1.2f, 50.0f,
        CORONATYPE_SHINYSTAR, FLARETYPE_NONE, true, false, 1, 0.0f, false, 1.5f, 0, 15.0f, false, false);
}
}  // namespace

void CLaserScopeDotSync::Process()
{
    CaptureLocalDot();
    if (!CNetwork::m_bAuthenticated)
    {
        return;
    }

    const uint32_t now = GetTickCount();
    for (CNetworkPlayer* player : CNetworkPlayerManager::m_pPlayers)
    {
        RenderRemoteDot(player, now);
    }
}

void CLaserScopeDotSync::AppendLocalState(Packets::Players::PlayerCameraSync& packet)
{
    packet.bLaserScopeDotActive = CNetwork::m_bAuthenticated && CWorld::PlayerInFocus == 0 && g_localDotActive;
    if (packet.bLaserScopeDotActive)
    {
        packet.laserScopeDotPosition = g_localDotPosition;
        packet.laserScopeDotSize = g_localDotSize;
    }
    else
    {
        packet.laserScopeDotPosition = {};
        packet.laserScopeDotSize = 0.0f;
    }
}

void CLaserScopeDotSync::HandleRemoteState(
    CNetworkPlayer* player, const Packets::Players::PlayerCameraSync& packet)
{
    if (player == nullptr)
    {
        return;
    }

    player->m_cameraSnapshot.bLaserScopeDotActive = packet.bLaserScopeDotActive;
    player->m_cameraSnapshot.laserScopeDotPosition = packet.laserScopeDotPosition;
    player->m_cameraSnapshot.laserScopeDotSize = packet.laserScopeDotSize;
    player->m_nLaserScopeDotReceivedAt = packet.bLaserScopeDotActive ? GetTickCount() : 0;
}

bool CLaserScopeDotSync::ShouldSendHeartbeat(
    const Packets::Players::PlayerCameraSync& packet, uint32_t now, uint32_t lastSentAt)
{
    return packet.bLaserScopeDotActive && now - lastSentAt >= HEARTBEAT_INTERVAL_MS;
}
