#include "stdafx.h"
#include "CNetworkPedGroupSyncManager.h"
#include "CNetworkPed.h"

#include <CPedGroups.h>

namespace
{
constexpr int NATIVE_GROUP_COUNT = 8;
constexpr int INVALID_NATIVE_GROUP = -1;

struct RemoteMemberState
{
    bool initialized = false;
    Packets::Peds::SPedGroupMembershipSnapshot desired{};
    int appliedGroupId = INVALID_NATIVE_GROUP;
    uint8_t appliedSlot = 0;
};

struct RemoteGroupState
{
    int nativeGroupId = INVALID_NATIVE_GROUP;
    CPlayerPed* leader = nullptr;
};

std::array<RemoteMemberState, Config::MAX_SERVER_PEDS> g_remoteMembers{};
std::array<RemoteGroupState, Config::MAX_SERVER_PLAYERS> g_remoteGroups{};
bool g_groupProcessHookInstalled = false;

bool IsNewerRevision(uint16_t candidate, uint16_t current)
{
    const uint16_t delta = static_cast<uint16_t>(candidate - current);
    return delta != 0 && delta < 0x8000;
}

bool IsValidPed(CPed* ped)
{
    return ped && IsPedPointerValid(ped) && ped->IsVTableValid() && ped->m_fHealth > 0.0f;
}

bool IsManagerOwnedRemoteGroup(CPedGroup* group)
{
    if (!CNetwork::m_bAuthenticated || !group)
        return false;

    const int nativeGroupId = CPedGroups::GetGroupId(group);
    if (nativeGroupId < 0 || nativeGroupId >= NATIVE_GROUP_COUNT ||
        !CPedGroups::ms_activeGroups[nativeGroupId])
    {
        return false;
    }

    CPlayerPed* localPlayer = FindPlayerPed(0);
    if (group->m_groupMembership.GetLeader() == localPlayer)
        return false;

    return std::any_of(g_remoteGroups.begin(), g_remoteGroups.end(),
        [nativeGroupId, localPlayer](const RemoteGroupState& state) {
            return state.nativeGroupId == nativeGroupId && state.leader && state.leader != localPlayer;
        });
}

void __fastcall CPedGroup__Process_GroupSyncHook(CPedGroup* group, SKIP_EDX)
{
    if (!group)
        return;

    // gta-reversed documents CPedGroups::Process (0x5FC800) as dispatching every active group and
    // CPedGroup::Process (0x5FC7E0) as these two calls in order. Replicated remote groups are a
    // membership-only mirror: membership cleanup remains native, but local group intelligence must not
    // allocate tasks that can permanently replace the authoritative network task between revisions.
    const bool suppressRemoteIntelligence = IsManagerOwnedRemoteGroup(group);
    group->m_groupMembership.Process();
    if (!suppressRemoteIntelligence)
        group->m_groupIntelligence.Process();
}

void EnsureGroupProcessHookInstalled()
{
    if (g_groupProcessHookInstalled)
        return;

    patch::RedirectJump(0x5FC7E0, CPedGroup__Process_GroupSyncHook);
    g_groupProcessHookInstalled = true;
}

CPedGroup* GetOwnedGroup(int nativeGroupId)
{
    if (nativeGroupId < 0 || nativeGroupId >= NATIVE_GROUP_COUNT || !CPedGroups::ms_activeGroups[nativeGroupId])
        return nullptr;
    return &CPedGroups::ms_groups[nativeGroupId];
}

void ClearAppliedGroupReferences(int nativeGroupId)
{
    for (auto& member : g_remoteMembers)
    {
        if (member.appliedGroupId == nativeGroupId)
        {
            member.appliedGroupId = INVALID_NATIVE_GROUP;
            member.appliedSlot = 0;
        }
    }
}

void ReleaseGroup(int leaderPlayerId)
{
    if (leaderPlayerId < 0 || leaderPlayerId >= Config::MAX_SERVER_PLAYERS)
        return;

    RemoteGroupState& state = g_remoteGroups[leaderPlayerId];
    if (state.nativeGroupId != INVALID_NATIVE_GROUP)
    {
        const int groupId = state.nativeGroupId;
        if (GetOwnedGroup(groupId))
            CPedGroups::RemoveGroup(groupId);
        ClearAppliedGroupReferences(groupId);
    }
    state = {};
    state.nativeGroupId = INVALID_NATIVE_GROUP;
}

void RemoveAppliedMember(RemoteMemberState& state)
{
    CPedGroup* group = GetOwnedGroup(state.appliedGroupId);
    if (group && state.appliedSlot < Packets::Peds::SPedGroupMembershipSnapshot::MAX_FOLLOWERS)
    {
        group->m_groupMembership.RemoveMember(state.appliedSlot);
    }
    state.appliedGroupId = INVALID_NATIVE_GROUP;
    state.appliedSlot = 0;
}

CPlayerPed* ResolveRemoteLeader(int playerId)
{
    if (playerId < 0 || playerId >= Config::MAX_SERVER_PLAYERS ||
        playerId == CNetworkPlayerManager::m_nMyId)
    {
        return nullptr;
    }

    CNetworkPlayer* player = CNetworkPlayerManager::GetPlayer(playerId);
    return player && IsValidPed(player->m_pPed) ? player->m_pPed : nullptr;
}

CPedGroup* EnsureRemoteGroup(int leaderPlayerId, CPlayerPed* leader)
{
    if (!leader || leaderPlayerId < 0 || leaderPlayerId >= Config::MAX_SERVER_PLAYERS)
        return nullptr;

    RemoteGroupState& state = g_remoteGroups[leaderPlayerId];
    CPedGroup* group = GetOwnedGroup(state.nativeGroupId);
    if (group && state.leader == leader && group->m_groupMembership.GetLeader() == leader)
        return group;

    ReleaseGroup(leaderPlayerId);

    const int nativeGroupId = CPedGroups::AddGroup();
    if (nativeGroupId < 0 || nativeGroupId >= NATIVE_GROUP_COUNT)
        return nullptr;

    state.nativeGroupId = nativeGroupId;
    state.leader = leader;
    group = &CPedGroups::ms_groups[nativeGroupId];
    group->m_bMembersEnterLeadersVehicle = true;
    group->m_groupMembership.SetLeader(leader);
    return group;
}

bool HasDesiredFollowers(int leaderPlayerId)
{
    return std::any_of(g_remoteMembers.begin(), g_remoteMembers.end(), [leaderPlayerId](const auto& member) {
        return member.initialized && member.desired.hasGroup &&
               member.desired.leaderPlayerId.value == leaderPlayerId;
    });
}

void ApplyMember(int pedId)
{
    RemoteMemberState& state = g_remoteMembers[pedId];
    if (!state.initialized || !state.desired.hasGroup)
    {
        RemoveAppliedMember(state);
        return;
    }

    CNetworkPed* networkPed = CNetworkPedManager::GetPed(pedId);
    CPlayerPed* leader = ResolveRemoteLeader(state.desired.leaderPlayerId.value);
    if (!networkPed || networkPed->m_bSyncing || !IsValidPed(networkPed->m_pPed) || !leader)
    {
        RemoveAppliedMember(state);
        return;
    }

    CPedGroup* group = EnsureRemoteGroup(state.desired.leaderPlayerId.value, leader);
    if (!group)
    {
        RemoveAppliedMember(state);
        return;
    }

    const int nativeGroupId = CPedGroups::GetGroupId(group);
    if (nativeGroupId < 0 || nativeGroupId >= NATIVE_GROUP_COUNT)
        return;

    const uint8_t followerSlot = state.desired.followerSlot;
    if (state.appliedGroupId == nativeGroupId && state.appliedSlot == followerSlot &&
        group->m_groupMembership.GetMember(followerSlot) == networkPed->m_pPed)
    {
        return;
    }

    RemoveAppliedMember(state);

    // A replicated ped must never be stolen from a stock or local-player group. Only memberships created by this
    // manager may be replaced; a temporarily conflicting native state remains pending for the next frame.
    if (CPedGroup* currentGroup = CPedGroups::GetPedsGroup(networkPed->m_pPed))
    {
        const int currentGroupId = CPedGroups::GetGroupId(currentGroup);
        const bool isOwnedRemoteGroup = std::any_of(g_remoteGroups.begin(), g_remoteGroups.end(),
            [currentGroupId](const auto& candidate) { return candidate.nativeGroupId == currentGroupId; });
        if (!isOwnedRemoteGroup)
            return;
    }

    CPed* occupant = group->m_groupMembership.GetMember(followerSlot);
    if (occupant && occupant != networkPed->m_pPed)
        group->m_groupMembership.RemoveMember(followerSlot);

    group->m_groupMembership.AddMember(networkPed->m_pPed, followerSlot);
    state.appliedGroupId = nativeGroupId;
    state.appliedSlot = followerSlot;
}

void ReleaseUnusedOrInvalidGroups()
{
    for (int leaderPlayerId = 0; leaderPlayerId < Config::MAX_SERVER_PLAYERS; ++leaderPlayerId)
    {
        RemoteGroupState& state = g_remoteGroups[leaderPlayerId];
        if (state.nativeGroupId == INVALID_NATIVE_GROUP)
            continue;

        CPedGroup* group = GetOwnedGroup(state.nativeGroupId);
        CPlayerPed* leader = ResolveRemoteLeader(leaderPlayerId);
        if (!group || !leader || state.leader != leader || group->m_groupMembership.GetLeader() != leader ||
            !HasDesiredFollowers(leaderPlayerId))
        {
            ReleaseGroup(leaderPlayerId);
        }
    }
}
}  // namespace

void CNetworkPedGroupSyncManager::CaptureLocalMembership(
    const CNetworkPed* networkPed, Packets::Peds::SPedGroupMembershipSnapshot& snapshot)
{
    snapshot = {};
    if (!CNetwork::m_bAuthenticated || !networkPed || !networkPed->m_bSyncing ||
        CNetworkPlayerManager::m_nMyId < 0 || CNetworkPlayerManager::m_nMyId >= Config::MAX_SERVER_PLAYERS ||
        !IsValidPed(networkPed->m_pPed))
    {
        return;
    }

    CPlayerPed* leader = FindPlayerPed(0);
    if (!IsValidPed(leader))
        return;

    CPedGroup* group = CPedGroups::GetPedsGroup(networkPed->m_pPed);
    if (!group || group->m_groupMembership.GetLeader() != leader)
        return;

    for (uint8_t slot = 0; slot < Packets::Peds::SPedGroupMembershipSnapshot::MAX_FOLLOWERS; ++slot)
    {
        if (group->m_groupMembership.GetMember(slot) == networkPed->m_pPed)
        {
            snapshot.hasGroup = true;
            snapshot.leaderPlayerId.value = CNetworkPlayerManager::m_nMyId;
            snapshot.followerSlot = slot;
            return;
        }
    }
}

void CNetworkPedGroupSyncManager::ObserveRemoteMembership(
    int pedId, const Packets::Peds::SPedGroupMembershipSnapshot& snapshot)
{
    if (!CNetwork::m_bAuthenticated || pedId < 0 || pedId >= Config::MAX_SERVER_PEDS || snapshot.revision == 0 ||
        !snapshot.HasValidSemantics() || !snapshot.FitsSerializedBudget())
    {
        return;
    }

    // Install before ApplyMember can activate a remote group. This prevents even its first native process
    // from generating local AI tasks, while an offline session never needs to install the hook at all.
    EnsureGroupProcessHookInstalled();

    RemoteMemberState& state = g_remoteMembers[pedId];
    if (state.initialized && !IsNewerRevision(snapshot.revision, state.desired.revision))
        return;

    state.initialized = true;
    state.desired = snapshot;
    ApplyMember(pedId);
    ReleaseUnusedOrInvalidGroups();
}

void CNetworkPedGroupSyncManager::OnPedAvailable(int pedId)
{
    if (pedId >= 0 && pedId < Config::MAX_SERVER_PEDS)
        ApplyMember(pedId);
}

void CNetworkPedGroupSyncManager::OnPedRemoved(int pedId)
{
    if (pedId < 0 || pedId >= Config::MAX_SERVER_PEDS)
        return;

    RemoteMemberState& state = g_remoteMembers[pedId];
    const bool preserveRevision = state.initialized;
    const uint16_t lastRevision = state.desired.revision;
    RemoveAppliedMember(state);
    state = {};
    state.appliedGroupId = INVALID_NATIVE_GROUP;
    if (preserveRevision)
    {
        // Ped IDs are reused. The server keeps a per-ID canonical revision stream, so retaining this tombstone
        // rejects delayed SYNC packets from the removed incarnation while accepting the replacement's next state.
        state.initialized = true;
        state.desired.revision = lastRevision;
    }
    ReleaseUnusedOrInvalidGroups();
}

void CNetworkPedGroupSyncManager::Process()
{
    if (!CNetwork::m_bAuthenticated)
        return;

    for (int pedId = 0; pedId < Config::MAX_SERVER_PEDS; ++pedId)
    {
        if (g_remoteMembers[pedId].initialized)
            ApplyMember(pedId);
    }
    ReleaseUnusedOrInvalidGroups();
}

void CNetworkPedGroupSyncManager::Reset()
{
    for (int leaderPlayerId = 0; leaderPlayerId < Config::MAX_SERVER_PLAYERS; ++leaderPlayerId)
        ReleaseGroup(leaderPlayerId);

    g_remoteMembers.fill(RemoteMemberState{});
    g_remoteGroups.fill(RemoteGroupState{});
    for (auto& group : g_remoteGroups)
        group.nativeGroupId = INVALID_NATIVE_GROUP;
    for (auto& member : g_remoteMembers)
        member.appliedGroupId = INVALID_NATIVE_GROUP;
}
