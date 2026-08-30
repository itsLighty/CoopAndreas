#include "stdafx.h"

void CLocalPlayer::BuildTaskPacket(eTaskType type, bool toggle)
{
	CPlayerPed* pPlayerPed = FindPlayerPed(0);
	Packets::Players::SetPlayerTask packet{};
	packet.taskType = type;
	packet.vecPos = pPlayerPed->GetPosition();
	packet.currentRotation = pPlayerPed->m_fCurrentRotation;
	packet.aimingRotation = pPlayerPed->m_fAimingRotation;
	packet.toggle = toggle;
	GetPacketFactory().Send(packet);
}

void CLocalPlayer::BuildAnimationTaskPacket(Packets::Players::ePlayerAnimationState state, uint16_t sequence,
	uint8_t progress)
{
	if (!CNetwork::m_bAuthenticated)
	{
		return;
	}

	CPlayerPed* pPlayerPed = FindPlayerPed(0);
	if (!pPlayerPed)
	{
		return;
	}

	Packets::Players::SetPlayerTask packet{};
	packet.taskType = TASK_SIMPLE_PLAYER_ON_FOOT;
	packet.vecPos = pPlayerPed->GetPosition();
	packet.currentRotation = pPlayerPed->m_fCurrentRotation;
	packet.aimingRotation = pPlayerPed->m_fAimingRotation;
	packet.hasAnimationState = true;
	packet.animationState = state;
	packet.animationSequence = sequence;
	packet.animationProgress = progress;
	GetPacketFactory().Send(packet);
}

bool CLocalPlayer::GetIsHostingEntity(CEntity* pEntity)
{
    if (pEntity->m_nType == ENTITY_TYPE_PED)
    {
        CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pEntity);
        if (pNetworkPlayer)
        {
            return false;
        }

        CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pEntity);
        if (!pNetworkPed)
        {
            return true;
        }
        if (pNetworkPed->m_bSyncing)
        {
            return true;
        }
    }
    else if (pEntity->m_nType == ENTITY_TYPE_VEHICLE)
    {
        CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pEntity);
        if (!pNetworkVehicle)
        {
            return true;
        }
        if (pNetworkVehicle && pNetworkVehicle->m_bSyncing)
        {
            return true;
        }
    }
    else
    {
        return true;
    }

    return false;
}
