#include "CTagAuthorityManager.h"

#include "CNetworkPlayer.h"
#include "CNetworkPlayerManager.h"
#include "CPacketFactory.h"
#include "CServerTime.h"
#include "stdafx.h"

#include <array>

namespace
{
constexpr uint8_t MAX_TAG_UPDATES_PER_SECOND = 12;
constexpr uint32_t TAG_RATE_WINDOW_MS = 1000;
constexpr uint8_t MAX_NON_HOST_ALPHA_STEP = 32;
}

bool CTagAuthorityManager::IsCurrentHost(const CNetworkPlayer* sender)
{
    return sender != nullptr && sender->m_bIsHost && CNetworkPlayerManager::GetHost() == sender;
}

int CTagAuthorityManager::FindTag(const Packets::World::TagUpdate::Payload& tag)
{
    if (!ms_hasSnapshot)
        return -1;
    for (size_t index = 0; index < ARRAY_SIZE(ms_snapshot.tags); ++index)
    {
        if (!ms_validTags[index])
            continue;
        const auto& canonical = ms_snapshot.tags[index];
        if (canonical.pos_x == tag.pos_x && canonical.pos_y == tag.pos_y && canonical.pos_z == tag.pos_z)
            return static_cast<int>(index);
    }
    return -1;
}

bool CTagAuthorityManager::ConsumeRate(int playerId)
{
    if (playerId < 0 || playerId >= Config::MAX_SERVER_PLAYERS)
        return false;
    RateState& rate = ms_rates[playerId];
    if (g_serverTime - rate.windowStartedAt >= TAG_RATE_WINDOW_MS)
    {
        rate.windowStartedAt = g_serverTime;
        rate.count = 0;
    }
    if (rate.count >= MAX_TAG_UPDATES_PER_SECOND)
        return false;
    ++rate.count;
    return true;
}

bool CTagAuthorityManager::HandleUpdate(CNetworkPlayer* sender, const Packets::World::TagUpdate& packet)
{
    if (sender == nullptr || !ConsumeRate(sender->m_iPlayerId))
        return false;
    int index = FindTag(packet.payload);
    if (index < 0)
    {
        if (!IsCurrentHost(sender) || packet.payload.alpha == 0)
            return false;

        // Only the current authority may establish canonical tag coordinates.
        // Participant updates are accepted only after matching that bounded table.
        for (size_t empty = 0; empty < ms_validTags.size(); ++empty)
        {
            if (!ms_validTags[empty])
            {
                index = static_cast<int>(empty);
                ms_snapshot.tags[empty] = packet.payload;
                ms_snapshot.tags[empty].alpha = 0;
                ms_snapshot.tags[empty].bFullySprayed = false;
                ms_validTags[empty] = true;
                ms_hasSnapshot = true;
                break;
            }
        }
        if (index < 0)
            return false;
    }

    const auto& canonical = ms_snapshot.tags[index];
    if (packet.payload.alpha <= canonical.alpha)
        return false;
    if (!IsCurrentHost(sender))
    {
        const uint8_t delta = static_cast<uint8_t>(packet.payload.alpha - canonical.alpha);
        if ((packet.payload.bFullySprayed && canonical.alpha < 224) ||
            (!packet.payload.bFullySprayed && delta > MAX_NON_HOST_ALPHA_STEP))
            return false;
    }

    Packets::World::TagUpdate update = packet;
    update.payload.alpha = packet.payload.bFullySprayed ? 255 : packet.payload.alpha;
    ms_snapshot.tags[index] = update.payload;
    GetPacketFactory().SendToAll(update, sender);
    return true;
}

bool CTagAuthorityManager::HandleSnapshot(CNetworkPlayer* sender, const Packets::World::UpdateAllTags& packet)
{
    if (!IsCurrentHost(sender))
        return false;

    Packets::World::UpdateAllTags merged = ms_hasSnapshot ? ms_snapshot : packet;
    std::array<bool, 100> mergedValid = ms_validTags;
    for (size_t index = 0; index < ARRAY_SIZE(packet.tags); ++index)
    {
        const auto& incoming = packet.tags[index];
        const bool incomingPresent = incoming.alpha != 0 || incoming.pos_x != 0 ||
                                     incoming.pos_y != 0 || incoming.pos_z != 0;
        if (!incomingPresent)
            continue;
        for (size_t other = index + 1; other < ARRAY_SIZE(packet.tags); ++other)
        {
            const bool otherPresent = packet.tags[other].alpha != 0 || packet.tags[other].pos_x != 0 ||
                                      packet.tags[other].pos_y != 0 || packet.tags[other].pos_z != 0;
            if (!otherPresent)
                continue;
            if (incoming.pos_x == packet.tags[other].pos_x && incoming.pos_y == packet.tags[other].pos_y &&
                incoming.pos_z == packet.tags[other].pos_z)
            {
                return false;
            }
        }

        if (!ms_hasSnapshot)
        {
            mergedValid[index] = true;
            continue;
        }
        int canonicalIndex = FindTag(incoming);
        if (canonicalIndex >= 0)
        {
            if (incoming.alpha < ms_snapshot.tags[canonicalIndex].alpha)
                return false;
            merged.tags[canonicalIndex] = incoming;
            continue;
        }

        canonicalIndex = -1;
        for (size_t empty = 0; empty < mergedValid.size(); ++empty)
        {
            if (!mergedValid[empty])
            {
                canonicalIndex = static_cast<int>(empty);
                break;
            }
        }
        if (canonicalIndex < 0)
            return false;
        merged.tags[canonicalIndex] = incoming;
        mergedValid[canonicalIndex] = true;
    }

    ms_snapshot = merged;
    ms_validTags = mergedValid;
    ms_hasSnapshot = true;
    Packets::World::UpdateAllTags broadcast = ms_snapshot;
    GetPacketFactory().SendToAll(broadcast, sender);
    return true;
}

void CTagAuthorityManager::SendSnapshot(CNetworkPlayer* recipient)
{
    if (recipient && ms_hasSnapshot)
        GetPacketFactory().Send(ms_snapshot, recipient);
}

void CTagAuthorityManager::HandlePlayerDisconnected(int playerId)
{
    if (playerId >= 0 && playerId < Config::MAX_SERVER_PLAYERS)
        ms_rates[playerId] = {};
}

void CTagAuthorityManager::ResetSession()
{
    ms_hasSnapshot = false;
    ms_snapshot = {};
    ms_validTags.fill(false);
    ms_rates.fill(RateState{});
}
