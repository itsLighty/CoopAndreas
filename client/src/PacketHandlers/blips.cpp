#include "network/packets/blips.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include <CNetworkEntityBlip.h>
#include <CNetworkCheckpoint.h>
#include <CNetworkStaticBlip.h>
#include <CMissionSessionClient.h>

namespace
{
bool ShouldIgnoreMissionEffect()
{
	const auto& state = CMissionSessionClient::GetState();
	if (!state.IsActive())
	{
		return false;
	}

	const int localPlayerId = CNetworkPlayerManager::m_nMyId;
	return !state.HasValidParticipantRoster() || CMissionSessionClient::IsSpectator() ||
		CLocalPlayer::m_bIsHost || state.hostId == localPlayerId;
}

bool ShouldIgnoreTargetedMissionEffect(int forWhoPlayerId)
{
	const auto& state = CMissionSessionClient::GetState();
	return ShouldIgnoreMissionEffect() ||
		(state.IsActive() && forWhoPlayerId != CNetworkPlayerManager::m_nMyId);
}
}

PACKET_HANDLER(ePacketType::UPDATE_ENTITY_BLIP, Packets::Blips::UpdateEntityBlip* pUpdateEntityBlip)
{
	if (CLocalPlayer::m_bIsHost ||
		ShouldIgnoreTargetedMissionEffect(pUpdateEntityBlip->forWhoPlayerId))
		return;

	CNetworkEntityBlip::UpdateEntityBlip(pUpdateEntityBlip);
}

PACKET_HANDLER(ePacketType::REMOVE_ENTITY_BLIP, Packets::Blips::RemoveEntityBlip* pRemoveEntityBlip)
{
	if (CLocalPlayer::m_bIsHost ||
		ShouldIgnoreTargetedMissionEffect(pRemoveEntityBlip->forWhoPlayerId))
		return;

	CNetworkEntityBlip::RemoveEntityBlip(pRemoveEntityBlip);
}

PACKET_HANDLER(ePacketType::CLEAR_ENTITY_BLIPS, Packets::Blips::ClearEntityBlips* pClearEntityBlips)
{
	if (ShouldIgnoreTargetedMissionEffect(pClearEntityBlips->forWhoPlayerId))
		return;

	CNetworkEntityBlip::ClearEntityBlips();
}

PACKET_HANDLER(ePacketType::UPDATE_CHECKPOINT, Packets::Blips::UpdateCheckpoint* pUpdateCheckpoint)
{
	if (CLocalPlayer::m_bIsHost ||
		ShouldIgnoreTargetedMissionEffect(pUpdateCheckpoint->forWhoPlayerId))
		return;

	CNetworkCheckpoint::Update(pUpdateCheckpoint->position, pUpdateCheckpoint->radius);
}

PACKET_HANDLER(ePacketType::REMOVE_CHECKPOINT, Packets::Blips::RemoveCheckpoint* pRemoveCheckpoint)
{
	if (CLocalPlayer::m_bIsHost ||
		ShouldIgnoreTargetedMissionEffect(pRemoveCheckpoint->forWhoPlayerId))
		return;

	CNetworkCheckpoint::Remove();
}

PACKET_HANDLER(ePacketType::CREATE_STATIC_BLIP, Packets::Blips::StaticBlipsSnapshot* pCreateStaticBlip)
{
	if (CLocalPlayer::m_bIsHost || ShouldIgnoreMissionEffect())
	{
		return;
	}

	CNetworkStaticBlip::Create(*pCreateStaticBlip);
}
