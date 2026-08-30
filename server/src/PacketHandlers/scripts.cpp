#include "network/packets/scripts.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include "CMissionSessionServer.h"

namespace
{
bool IsAllowedMissionTarget(const CNetworkPlayer* pNetworkPlayer)
{
    return pNetworkPlayer != nullptr &&
           (!CMissionSessionServer::GetState().IsActive() ||
               CMissionSessionServer::IsGameplayParticipant(pNetworkPlayer));
}

template <typename PacketType>
void SendToMissionRecipients(PacketType& packet, CNetworkPlayer* pNetworkPlayerToIgnore)
{
    if (!CMissionSessionServer::GetState().IsActive())
    {
        GetPacketFactory().SendToAll(packet, pNetworkPlayerToIgnore);
        return;
    }

    for (CNetworkPlayer* pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (pNetworkPlayer != pNetworkPlayerToIgnore &&
            CMissionSessionServer::IsGameplayParticipant(pNetworkPlayer))
        {
            GetPacketFactory().Send(packet, pNetworkPlayer);
        }
    }
}
}

PACKET_HANDLER(ePacketType::MISSION_SESSION_REQUEST,
    Packets::Scripts::MissionSessionRequest* pMissionSessionRequest, CNetworkPlayer* pNetworkPlayer)
{
    CMissionSessionServer::HandleRequest(pNetworkPlayer, *pMissionSessionRequest);
}

PACKET_HANDLER(ePacketType::MISSION_SESSION_STATE,
    Packets::Scripts::MissionSessionState*, CNetworkPlayer* pNetworkPlayer)
{
    logger::warn("%s tried to send server-owned mission-session state", pNetworkPlayer->GetName().c_str());
}

PACKET_HANDLER(ePacketType::ON_MISSION_FLAG_SYNC, Packets::Scripts::OnMissionFlagSync* pOnMissionFlagSync,
    CNetworkPlayer* pNetworkPlayer)
{
    CMissionSessionServer::HandleLegacyMissionFlag(pNetworkPlayer, pOnMissionFlagSync->bOnMission);
}

PACKET_HANDLER(ePacketType::ENEX_SYNC, Packets::Scripts::EnExSync* pEnExSync, CNetworkPlayer* pNetworkPlayer)
{
    if (CMissionSessionServer::IsAuthoritativeHost(pNetworkPlayer))
    {
        Packets::Scripts::g_lastEnExData = *pEnExSync;
        Packets::Scripts::g_lastEnExData.serverTime = 0;  // force recalculation

        Packets::Scripts::g_pLastEnExPlayerOwner = pNetworkPlayer;

        GetPacketFactory().SendToAll(*pEnExSync, pNetworkPlayer);
    }
}

PACKET_HANDLER(
    ePacketType::ADD_MESSAGE_GXT, Packets::Scripts::AddMessageGXT* pAddMessageGXT, CNetworkPlayer* pNetworkPlayer)
{
    if (CMissionSessionServer::IsAuthoritativeHost(pNetworkPlayer))
    {
        if (auto pTargetPlayer = CNetworkPlayerManager::GetPlayer(pAddMessageGXT->forWhoPlayerId);
            IsAllowedMissionTarget(pTargetPlayer))
        {
            GetPacketFactory().Send(*pAddMessageGXT, pTargetPlayer);
        }
    }
}

PACKET_HANDLER(ePacketType::REMOVE_MESSAGE_GXT, Packets::Scripts::RemoveMessageGXT* pRemoveMessageGXT,
    CNetworkPlayer* pNetworkPlayer)
{
    if (CMissionSessionServer::IsAuthoritativeHost(pNetworkPlayer))
    {
        if (auto pTargetPlayer = CNetworkPlayerManager::GetPlayer(pRemoveMessageGXT->forWhoPlayerId);
            IsAllowedMissionTarget(pTargetPlayer))
        {
            GetPacketFactory().Send(*pRemoveMessageGXT, pTargetPlayer);
        }
    }
}

#if 0  // controlled with SCM
PACKET_HANDLER(ePacketType::START_CUTSCENE, Packets::Scripts::StartCutscene* pStartCutscene, CNetworkPlayer* pNetworkPlayer)
{
    if (CMissionSessionServer::IsAuthoritativeHost(pNetworkPlayer))
    {
        GetPacketFactory().SendToAll(*pStartCutscene, pNetworkPlayer);
    }
}

PACKET_HANDLER(ePacketType::SKIP_CUTSCENE, Packets::Scripts::SkipCutscene* pSkipCutscene, CNetworkPlayer* pNetworkPlayer)
{
    pSkipCutscene->playerid = pNetworkPlayer->m_iPlayerId;
    GetPacketFactory().SendToAll(*pSkipCutscene, pNetworkPlayer);
}
#endif

PACKET_HANDLER(ePacketType::PLAY_MISSION_AUDIO, Packets::Scripts::PlayMissionAudio* pPlayMissionAudio,
    CNetworkPlayer* pNetworkPlayer)
{
    if (CMissionSessionServer::IsAuthoritativeHost(pNetworkPlayer))
    {
        SendToMissionRecipients(*pPlayMissionAudio, pNetworkPlayer);
    }
}

PACKET_HANDLER(ePacketType::TELEPORT_PLAYER_SCRIPTED, Packets::Scripts::TeleportPlayerScripted* pTeleportPlayerScripted,
    CNetworkPlayer* pNetworkPlayer)
{
    if (CMissionSessionServer::IsAuthoritativeHost(pNetworkPlayer))
    {
        if (auto pTargetPlayer = CNetworkPlayerManager::GetPlayer(pTeleportPlayerScripted->playerid);
            IsAllowedMissionTarget(pTargetPlayer))
        {
            GetPacketFactory().Send(*pTeleportPlayerScripted, pTargetPlayer);
        }
    }
}

PACKET_HANDLER(ePacketType::OPCODE_SYNC, Packets::Scripts::OpCodeSync* pOpCodeSync, CNetworkPlayer* pNetworkPlayer)
{
    if (!CMissionSessionServer::IsAuthoritativeHost(pNetworkPlayer))
    {
        logger::warn("%s tried to synchronize a script opcode without host authority",
            pNetworkPlayer->GetName().c_str());
        return;
    }
    SendToMissionRecipients(*pOpCodeSync, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PERFORM_TASK_SEQUENCE, Packets::Scripts::PerformTaskSequence* pPerformTaskSequence, CNetworkPlayer* pNetworkPlayer)
{
    if (CMissionSessionServer::GetState().IsActive() &&
        !CMissionSessionServer::IsGameplayParticipant(pNetworkPlayer))
    {
        logger::warn("%s tried to send a mission task sequence as a spectator",
            pNetworkPlayer->GetName().c_str());
        return;
    }
    *(int*)&pPerformTaskSequence->buffer[0] = pNetworkPlayer->m_iPlayerId;
    SendToMissionRecipients(*pPerformTaskSequence, pNetworkPlayer);
}
