#include "network/packets/players.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include "network/packet_handler.h"
#include "CMissionSessionServer.h"
#include "CFireAuthorityManager.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr uint32_t ANIMATION_RATE_WINDOW_MS = 1000;
constexpr uint16_t MAX_ANIMATION_EVENTS_PER_WINDOW = 20;
constexpr uint32_t LASER_DOT_RATE_WINDOW_MS = 1000;
constexpr uint16_t MAX_LASER_DOT_UPDATES_PER_WINDOW = 30;
constexpr uint32_t PARACHUTE_RATE_WINDOW_MS = 1000;
constexpr uint16_t MAX_PARACHUTE_UPDATES_PER_WINDOW = 12;

struct AnimationRateSlot
{
    CNetworkPlayer* owner = nullptr;
    uint32_t connectId = 0;
    uint32_t windowStartedAt = 0;
    uint16_t eventCount = 0;
};

AnimationRateSlot g_animationRateSlots[Config::MAX_SERVER_PLAYERS]{};

struct LaserDotRateSlot
{
    CNetworkPlayer* owner = nullptr;
    uint32_t connectId = 0;
    uint32_t windowStartedAt = 0;
    uint16_t updateCount = 0;
};

LaserDotRateSlot g_laserDotRateSlots[Config::MAX_SERVER_PLAYERS]{};

struct ParachuteRateSlot
{
    CNetworkPlayer* owner = nullptr;
    uint32_t connectId = 0;
    uint32_t windowStartedAt = 0;
    uint16_t updateCount = 0;
};

ParachuteRateSlot g_parachuteRateSlots[Config::MAX_SERVER_PLAYERS]{};

bool CanRelayAnimationEvent(CNetworkPlayer* player)
{
    if (!player || player->m_iPlayerId < 0 || player->m_iPlayerId >= Config::MAX_SERVER_PLAYERS ||
        !player->m_pPeer)
    {
        return false;
    }

    AnimationRateSlot& slot = g_animationRateSlots[player->m_iPlayerId];
    const uint32_t now = enet_time_get();
    const uint32_t connectId = player->m_pPeer->connectID;
    if (slot.owner != player || slot.connectId != connectId ||
        now - slot.windowStartedAt >= ANIMATION_RATE_WINDOW_MS)
    {
        slot.owner = player;
        slot.connectId = connectId;
        slot.windowStartedAt = now;
        slot.eventCount = 0;
    }

    if (slot.eventCount >= MAX_ANIMATION_EVENTS_PER_WINDOW)
    {
        return false;
    }

    ++slot.eventCount;
    return true;
}

bool CanRelayLaserScopeDotUpdate(CNetworkPlayer* player)
{
    if (!player || player->m_iPlayerId < 0 || player->m_iPlayerId >= Config::MAX_SERVER_PLAYERS ||
        !player->m_pPeer)
    {
        return false;
    }

    LaserDotRateSlot& slot = g_laserDotRateSlots[player->m_iPlayerId];
    const uint32_t now = enet_time_get();
    const uint32_t connectId = player->m_pPeer->connectID;
    if (slot.owner != player || slot.connectId != connectId ||
        now - slot.windowStartedAt >= LASER_DOT_RATE_WINDOW_MS)
    {
        slot.owner = player;
        slot.connectId = connectId;
        slot.windowStartedAt = now;
        slot.updateCount = 0;
    }

    if (slot.updateCount >= MAX_LASER_DOT_UPDATES_PER_WINDOW)
    {
        return false;
    }

    ++slot.updateCount;
    return true;
}

bool CanRelayParachuteUpdate(CNetworkPlayer* player)
{
    if (!player || player->m_iPlayerId < 0 || player->m_iPlayerId >= Config::MAX_SERVER_PLAYERS ||
        !player->m_pPeer)
    {
        return false;
    }

    ParachuteRateSlot& slot = g_parachuteRateSlots[player->m_iPlayerId];
    const uint32_t now = enet_time_get();
    const uint32_t connectId = player->m_pPeer->connectID;
    if (slot.owner != player || slot.connectId != connectId ||
        now - slot.windowStartedAt >= PARACHUTE_RATE_WINDOW_MS)
    {
        slot.owner = player;
        slot.connectId = connectId;
        slot.windowStartedAt = now;
        slot.updateCount = 0;
    }

    if (slot.updateCount >= MAX_PARACHUTE_UPDATES_PER_WINDOW)
    {
        return false;
    }

    ++slot.updateCount;
    return true;
}

bool IsParachuteSequenceNewer(uint16_t incoming, uint16_t previous)
{
    return static_cast<int16_t>(incoming - previous) > 0;
}

bool IsFreefallState(Packets::Players::ePlayerParachuteState state)
{
    return state >= Packets::Players::PLAYER_PARACHUTE_FREEFALL &&
           state <= Packets::Players::PLAYER_PARACHUTE_FREEFALL_ACCEL;
}

bool IsDeployedState(Packets::Players::ePlayerParachuteState state)
{
    return state >= Packets::Players::PLAYER_PARACHUTE_DEPLOYED &&
           state <= Packets::Players::PLAYER_PARACHUTE_DEPLOYED_FLARE;
}

bool IsValidParachuteTransition(Packets::Players::ePlayerParachuteState previous,
    Packets::Players::ePlayerParachuteState incoming)
{
    if (incoming == Packets::Players::PLAYER_PARACHUTE_NONE || incoming == previous ||
        previous == Packets::Players::PLAYER_PARACHUTE_NONE)
    {
        return true;
    }
    if (IsFreefallState(previous))
    {
        return IsFreefallState(incoming) || incoming == Packets::Players::PLAYER_PARACHUTE_OPENING ||
               incoming == Packets::Players::PLAYER_PARACHUTE_COLLAPSED;
    }
    if (previous == Packets::Players::PLAYER_PARACHUTE_OPENING)
    {
        return incoming == Packets::Players::PLAYER_PARACHUTE_OPENING || IsDeployedState(incoming) ||
               incoming == Packets::Players::PLAYER_PARACHUTE_COLLAPSED ||
               incoming == Packets::Players::PLAYER_PARACHUTE_LANDING ||
               incoming == Packets::Players::PLAYER_PARACHUTE_LANDING_WATER;
    }
    if (IsDeployedState(previous))
    {
        return IsDeployedState(incoming) || incoming == Packets::Players::PLAYER_PARACHUTE_COLLAPSED ||
               incoming == Packets::Players::PLAYER_PARACHUTE_LANDING ||
               incoming == Packets::Players::PLAYER_PARACHUTE_LANDING_WATER;
    }
    if (previous == Packets::Players::PLAYER_PARACHUTE_COLLAPSED)
    {
        return incoming == Packets::Players::PLAYER_PARACHUTE_COLLAPSED;
    }
    return (previous == Packets::Players::PLAYER_PARACHUTE_LANDING &&
               incoming == Packets::Players::PLAYER_PARACHUTE_LANDING) ||
           (previous == Packets::Players::PLAYER_PARACHUTE_LANDING_WATER &&
               incoming == Packets::Players::PLAYER_PARACHUTE_LANDING_WATER);
}

void RelayAuthoritativeParachuteStop(CNetworkPlayer* player)
{
    if (!player || !player->m_bHasAuthoritativeParachuteState)
    {
        return;
    }

    Packets::Players::SetPlayerTask stop{};
    stop.playerid = player->m_iPlayerId;
    stop.taskType = TASK_SIMPLE_PLAYER_ON_FOOT;
    stop.hasParachuteState = true;
    stop.parachuteState = Packets::Players::PLAYER_PARACHUTE_NONE;
    stop.parachuteSequence = static_cast<uint16_t>(player->m_nParachuteSequence + 1);
    GetPacketFactory().SendToAll(stop, player);

    player->m_eParachuteState = Packets::Players::PLAYER_PARACHUTE_NONE;
    player->m_nParachuteSequence = stop.parachuteSequence;
    player->m_bHasParachuteSequence = true;
    player->m_bHasAuthoritativeParachuteState = false;
}

template <typename PacketT>
void RelayPlayerGameplayState(PacketT& packet, CNetworkPlayer* pSourcePlayer)
{
    if (!CMissionSessionServer::GetState().IsActive())
    {
        GetPacketFactory().SendToAll(packet, pSourcePlayer);
        return;
    }

    for (CNetworkPlayer* pRecipient : CNetworkPlayerManager::m_pPlayers)
    {
        if (pRecipient != pSourcePlayer && CMissionSessionServer::IsGameplayParticipant(pRecipient))
        {
            GetPacketFactory().Send(packet, pRecipient);
        }
    }
}
}  // namespace

PACKET_HANDLER(
    ePacketType::PLAYER_ONFOOT_UPDATE, Packets::Players::OnFootUpdate* pOnFootUpdate, CNetworkPlayer* pNetworkPlayer)
{
    pNetworkPlayer->m_bHasOnFootSnapshot = true;
    pNetworkPlayer->m_bIsAlive = pOnFootUpdate->healthSnapshot.iHealth > 0;
    pNetworkPlayer->m_eLastWeaponType = static_cast<eWeaponType>(pOnFootUpdate->weaponSnapshot.iWeaponType);
    CFireAuthorityManager::ObservePlayerMovement(
        pNetworkPlayer, pOnFootUpdate->vecPos, pNetworkPlayer->m_bIsAlive);
    if (pNetworkPlayer->m_bHasAuthoritativeParachuteState &&
        (!pNetworkPlayer->m_bIsAlive || pNetworkPlayer->m_eLastWeaponType != WEAPON_PARACHUTE ||
            pNetworkPlayer->m_nVehicleId >= 0))
    {
        RelayAuthoritativeParachuteStop(pNetworkPlayer);
    }
    pOnFootUpdate->playerid.value = pNetworkPlayer->m_iPlayerId;
    GetPacketFactory().SendToAll(*pOnFootUpdate, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PLAYER_KEY_SYNC, Packets::Players::KeyPressed* pKeyPressed, CNetworkPlayer* pNetworkPlayer)
{
    pKeyPressed->playerid = pNetworkPlayer->m_iPlayerId;
    GetPacketFactory().SendToAll(*pKeyPressed, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PLAYER_CAMERA_SYNC, Packets::Players::PlayerCameraSync* pPlayerCameraSync, CNetworkPlayer* pNetworkPlayer)
{
    if (!pPlayerCameraSync->IsLaserScopeDotSemanticallyValid())
    {
        return;
    }
    if (pPlayerCameraSync->bLaserScopeDotActive &&
        (!pNetworkPlayer->m_bHasOnFootSnapshot || !pNetworkPlayer->m_bIsAlive ||
            pNetworkPlayer->m_eLastWeaponType != WEAPON_SNIPERRIFLE || pNetworkPlayer->m_nVehicleId >= 0 ||
            !CanRelayLaserScopeDotUpdate(pNetworkPlayer)))
    {
        return;
    }

    pPlayerCameraSync->playerid = pNetworkPlayer->m_iPlayerId;
    GetPacketFactory().SendToAll(*pPlayerCameraSync, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::SET_PLAYER_TASK, Packets::Players::SetPlayerTask* pSetPlayerTask, CNetworkPlayer* pNetworkPlayer)
{
    if (!pSetPlayerTask->IsAnimationStateSemanticallyValid() ||
        !pSetPlayerTask->IsParachuteStateSemanticallyValid())
    {
        return;
    }
    if (pSetPlayerTask->hasAnimationState && !CanRelayAnimationEvent(pNetworkPlayer))
    {
        return;
    }
    if (pSetPlayerTask->hasParachuteState)
    {
        const auto incoming = static_cast<Packets::Players::ePlayerParachuteState>(
            pSetPlayerTask->parachuteState);
        if (!CanRelayParachuteUpdate(pNetworkPlayer) ||
            (pNetworkPlayer->m_bHasParachuteSequence &&
                !IsParachuteSequenceNewer(
                    pSetPlayerTask->parachuteSequence, pNetworkPlayer->m_nParachuteSequence)) ||
            !IsValidParachuteTransition(pNetworkPlayer->m_eParachuteState, incoming))
        {
            return;
        }

        if (incoming != Packets::Players::PLAYER_PARACHUTE_NONE &&
            (!pNetworkPlayer->m_bHasOnFootSnapshot || !pNetworkPlayer->m_bIsAlive ||
                pNetworkPlayer->m_eLastWeaponType != WEAPON_PARACHUTE || pNetworkPlayer->m_nVehicleId >= 0))
        {
            return;
        }

        pNetworkPlayer->m_eParachuteState = incoming;
        pNetworkPlayer->m_nParachuteSequence = pSetPlayerTask->parachuteSequence;
        pNetworkPlayer->m_bHasParachuteSequence = true;
        pNetworkPlayer->m_bHasAuthoritativeParachuteState =
            incoming != Packets::Players::PLAYER_PARACHUTE_NONE;
    }

    pSetPlayerTask->playerid = pNetworkPlayer->m_iPlayerId;
    GetPacketFactory().SendToAll(*pSetPlayerTask, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::ENEX_TRANSITION, Packets::Players::EnExTransition* pEnExTransition,
    CNetworkPlayer* pNetworkPlayer)
{
    CFireAuthorityManager::ObservePlayerArea(pNetworkPlayer, pEnExTransition->playerAreaId);
    pEnExTransition->playerid = pNetworkPlayer->m_iPlayerId;
    GetPacketFactory().SendToAll(*pEnExTransition, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PLAYER_PLACE_WAYPOINT, Packets::Players::PlayerPlaceWaypoint* pPlayerPlaceWaypoint, CNetworkPlayer* pNetworkPlayer)
{
    pNetworkPlayer->m_waypointState = *pPlayerPlaceWaypoint;

    pPlayerPlaceWaypoint->playerid = pNetworkPlayer->m_iPlayerId;
    GetPacketFactory().SendToAll(*pPlayerPlaceWaypoint, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::RESPAWN_PLAYER, Packets::Players::RespawnPlayer* pRespawnPlayer, CNetworkPlayer* pNetworkPlayer)
{
    RelayAuthoritativeParachuteStop(pNetworkPlayer);
    pNetworkPlayer->m_bHasOnFootSnapshot = false;
    pNetworkPlayer->m_bIsAlive = false;
    pNetworkPlayer->m_eLastWeaponType = WEAPON_UNARMED;
    CFireAuthorityManager::MarkPlayerUnavailable(pNetworkPlayer);
    pRespawnPlayer->playerid = pNetworkPlayer->m_iPlayerId;
    GetPacketFactory().SendToAll(*pRespawnPlayer, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PLAYER_BULLET_SHOT, Packets::Players::PlayerBulletShot* pPlayerBulletShot, CNetworkPlayer* pNetworkPlayer)
{
    pPlayerBulletShot->playerid = pNetworkPlayer->m_iPlayerId;
    GetPacketFactory().SendToAll(*pPlayerBulletShot, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::ADD_PROJECTILE, Packets::Players::AddProjectile* pAddProjectile, CNetworkPlayer* pNetworkPlayer)
{
    GetPacketFactory().SendToAll(*pAddProjectile, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PLAYER_STATS, Packets::Players::PlayerStats* pPlayerStats, CNetworkPlayer* pNetworkPlayer)
{
    if (CMissionSessionServer::GetState().IsActive() &&
        !CMissionSessionServer::IsGameplayParticipant(pNetworkPlayer))
    {
        logger::warn("%s tried to publish player stats as a mission spectator",
            pNetworkPlayer->GetName().c_str());
        return;
    }

    for (size_t i = 0; i < ARRAY_SIZE(pPlayerStats->stats); i++)
    {
        if (!std::isfinite(pPlayerStats->stats[i]))
        {
            logger::warn("%s sent a non-finite player stat", pNetworkPlayer->GetName().c_str());
            return;
        }
        pPlayerStats->stats[i] = std::clamp(pPlayerStats->stats[i],
            Packets::Players::PlayerStats::MIN_STAT_VALUE,
            Packets::Players::PlayerStats::MAX_STAT_VALUE);
    }

    for (size_t i = 0; i < ARRAY_SIZE(pNetworkPlayer->m_afStats); i++)
    {
        pNetworkPlayer->m_afStats[i] = pPlayerStats->stats[i];
    }

    pNetworkPlayer->m_ucSyncFlags.bStatsModified = true;
    pPlayerStats->playerid = pNetworkPlayer->m_iPlayerId;
    RelayPlayerGameplayState(*pPlayerStats, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PLAYER_GAMEPLAY_STATE,
    Packets::Players::PlayerGameplayState* pGameplayState, CNetworkPlayer* pNetworkPlayer)
{
    if (CMissionSessionServer::GetState().IsActive() &&
        !CMissionSessionServer::IsGameplayParticipant(pNetworkPlayer))
    {
        logger::warn("%s tried to publish gameplay state as a mission spectator",
            pNetworkPlayer->GetName().c_str());
        return;
    }

    if (!std::isfinite(pGameplayState->breath) || !std::isfinite(pGameplayState->maximumHealth))
    {
        logger::warn("%s sent non-finite gameplay state", pNetworkPlayer->GetName().c_str());
        return;
    }

    pGameplayState->wantedLevel = std::clamp(pGameplayState->wantedLevel,
        Packets::Players::PlayerGameplayState::MIN_WANTED_LEVEL,
        Packets::Players::PlayerGameplayState::MAX_WANTED_LEVEL);
    pGameplayState->money = std::clamp(pGameplayState->money,
        Packets::Players::PlayerGameplayState::MIN_MONEY,
        Packets::Players::PlayerGameplayState::MAX_MONEY);
    pGameplayState->breath = std::clamp(pGameplayState->breath,
        Packets::Players::PlayerGameplayState::MIN_BREATH,
        Packets::Players::PlayerGameplayState::MAX_BREATH);
    pGameplayState->maximumHealth = std::clamp(pGameplayState->maximumHealth,
        Packets::Players::PlayerGameplayState::MIN_MAX_HEALTH,
        Packets::Players::PlayerGameplayState::MAX_MAX_HEALTH);

    // SenderPlayerId omits the client-provided ID on C2S; the authenticated peer is always canonical.
    pGameplayState->playerid.value = pNetworkPlayer->m_iPlayerId;
    pNetworkPlayer->m_gameplayState = *pGameplayState;
    pNetworkPlayer->m_ucSyncFlags.bGameplayStateModified = true;
    RelayPlayerGameplayState(*pGameplayState, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::REBUILD_PLAYER, Packets::Players::RebuildPlayer* pRebuildPlayer, CNetworkPlayer* pNetworkPlayer)
{
    pNetworkPlayer->m_pPedClothesDesc = pRebuildPlayer->clothesDesc;
    pNetworkPlayer->m_ucSyncFlags.bClothesModified = true;
    pRebuildPlayer->playerid = pNetworkPlayer->m_iPlayerId;
    GetPacketFactory().SendToAll(*pRebuildPlayer, pNetworkPlayer);
}

