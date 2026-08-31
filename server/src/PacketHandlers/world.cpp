#include "network/packet_types.h"
#include "stdafx.h"
#include "network/packet_handler.h"
#include "network/packets/world.h"
#include "CGangZoneWarAuthorityManager.h"
#include "CTagAuthorityManager.h"

PACKET_HANDLER(
    ePacketType::GAME_WEATHER_TIME, Packets::World::GameWeatherTime* pGameWeatherTime, CNetworkPlayer* pNetworkPlayer)
{
	if (!pNetworkPlayer->m_bIsHost)
	{
		return;
	}

	GetPacketFactory().SendToAll(*pGameWeatherTime, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::ADD_EXPLOSION, Packets::World::AddExplosion* pAddExplosion, CNetworkPlayer* pNetworkPlayer)
{
	GetPacketFactory().SendToAll(*pAddExplosion, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::TAG_UPDATE, Packets::World::TagUpdate* pTagUpdate, CNetworkPlayer* pNetworkPlayer)
{
	CTagAuthorityManager::HandleUpdate(pNetworkPlayer, *pTagUpdate);
}

PACKET_HANDLER(ePacketType::UPDATE_ALL_TAGS, Packets::World::UpdateAllTags* pUpdateAllTags, CNetworkPlayer* pNetworkPlayer)
{
	CTagAuthorityManager::HandleSnapshot(pNetworkPlayer, *pUpdateAllTags);
}

PACKET_HANDLER(ePacketType::UPDATE_MOON_SIZE, Packets::World::UpdateMoonSize* pUpdateMoonSize, CNetworkPlayer* pNetworkPlayer)
{
	GetPacketFactory().SendToAll(*pUpdateMoonSize, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::GANG_ZONE_STATE, Packets::World::GangZoneState* pGangZoneState,
    CNetworkPlayer* pNetworkPlayer)
{
    CGangZoneWarAuthorityManager::HandleZoneState(pNetworkPlayer, *pGangZoneState);
}

PACKET_HANDLER(ePacketType::GANG_WAR_STATE, Packets::World::GangWarState* pGangWarState,
    CNetworkPlayer* pNetworkPlayer)
{
    CGangZoneWarAuthorityManager::HandleWarState(pNetworkPlayer, *pGangWarState);
}
