#pragma once

class CNetworkPed;

class CNetworkPedGroupSyncManager
{
public:
    static void CaptureLocalMembership(
        const CNetworkPed* networkPed, Packets::Peds::SPedGroupMembershipSnapshot& snapshot);
    static void ObserveRemoteMembership(int pedId, const Packets::Peds::SPedGroupMembershipSnapshot& snapshot);
    static void OnPedAvailable(int pedId);
    static void OnPedPresentationUnavailable(int pedId);
    static void OnPedRemoved(int pedId);
    static bool IsPedPresentationRequired(int pedId);
    static bool IsPlayerPresentationRequired(int playerId);
    static void Process();
    static void Reset();
};
