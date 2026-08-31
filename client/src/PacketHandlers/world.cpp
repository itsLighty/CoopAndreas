#include "network/packets/world.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include <CWeatherSync.h>
#include <CMoonSync.h>
#include <CGangZoneWarSyncManager.h>
#include <game_sa/CTagManager.h>

PACKET_HANDLER(ePacketType::GAME_WEATHER_TIME, Packets::World::GameWeatherTime* pGameWeatherTime)
{
	CWeatherSync::HandlePacket(pGameWeatherTime);
}

PACKET_HANDLER(ePacketType::ADD_EXPLOSION, Packets::World::AddExplosion* pAddExplosion)
{
	//unused for now
	//CExplosion::AddExplosion(nullptr, nullptr, pAddExplosion->type, pAddExplosion->pos, pAddExplosion->time, pAddExplosion->usesSound, pAddExplosion->cameraShake, pAddExplosion->isVisible);
}

PACKET_HANDLER(ePacketType::TAG_UPDATE, Packets::World::TagUpdate* pTagUpdate)
{
    for (auto& tagDesc : CTagManager::ms_tagDesc)
    {
		if (!tagDesc.m_pEntity)
		{
			continue;
		}

		auto& pos = tagDesc.m_pEntity->GetPosition();

		if (static_cast<int16_t>(floor(pos.x)) == pTagUpdate->payload.pos_x &&
			static_cast<int16_t>(floor(pos.y)) == pTagUpdate->payload.pos_y &&
			static_cast<int16_t>(floor(pos.z)) == pTagUpdate->payload.pos_z)
		{
			// TAG_UPDATE is visual-only online. Pickup authority is the only path allowed to set completion alpha.
			CTagManager::SetAlpha(tagDesc.m_pEntity, std::min<uint8_t>(pTagUpdate->payload.alpha, 254));
		}
	}
}

PACKET_HANDLER(ePacketType::UPDATE_ALL_TAGS, Packets::World::UpdateAllTags* pUpdateAllTags)
{
	for (auto& tag : pUpdateAllTags->tags)
	{
		for (auto& tagDesc : CTagManager::ms_tagDesc)
		{
			if (!tagDesc.m_pEntity)
			{
				continue;
			}

			auto& pos = tagDesc.m_pEntity->GetPosition();

			if (static_cast<int16_t>(floor(pos.x)) == tag.pos_x &&
				static_cast<int16_t>(floor(pos.y)) == tag.pos_y &&
				static_cast<int16_t>(floor(pos.z)) == tag.pos_z)
			{
				CTagManager::SetAlpha(tagDesc.m_pEntity, std::min<uint8_t>(tag.alpha, 254));
			}
		}
	}
}

PACKET_HANDLER(ePacketType::UPDATE_MOON_SIZE, Packets::World::UpdateMoonSize* pUpdateMoonSize)
{
	CMoonSync::HandlePacket(pUpdateMoonSize);
}

PACKET_HANDLER(ePacketType::GANG_ZONE_STATE, Packets::World::GangZoneState* pGangZoneState)
{
    CGangZoneWarSyncManager::HandleZoneState(*pGangZoneState);
}

PACKET_HANDLER(ePacketType::GANG_WAR_STATE, Packets::World::GangWarState* pGangWarState)
{
    CGangZoneWarSyncManager::HandleWarState(*pGangWarState);
}
