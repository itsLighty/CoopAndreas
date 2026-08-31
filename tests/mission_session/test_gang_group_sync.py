import re
import unittest
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


@dataclass(frozen=True)
class Membership:
    leader: int | None = None
    slot: int = 0


class GroupAuthorityModel:
    """Semantic model of owner binding, slot canonicalization, and per-ID revisions."""

    def __init__(self):
        self.revisions = [0] * 255
        self.memberships: dict[int, tuple[int, Membership]] = {}

    def accept(self, ped_id, sender, owner, member_alive, leader_alive, requested):
        if sender != owner:
            return None

        canonical = Membership()
        if member_alive and requested.leader is not None:
            canonical = Membership(sender, requested.slot)
            if not leader_alive or not 0 <= canonical.slot < 7:
                return None
            followers = [
                (other_id, other_owner, membership)
                for other_id, (other_owner, membership) in self.memberships.items()
                if other_id != ped_id and membership.leader == sender
            ]
            if any(other_owner != sender for _, other_owner, _ in followers):
                return None
            if any(membership.slot == canonical.slot for _, _, membership in followers):
                return None
            if len(followers) >= 7:
                return None

        previous = self.memberships.get(ped_id)
        if previous is None or previous[1] != canonical:
            self.revisions[ped_id] = (self.revisions[ped_id] + 1) & 0xFFFF
            if self.revisions[ped_id] == 0:
                self.revisions[ped_id] = 1
        self.memberships[ped_id] = (sender, canonical)
        return self.revisions[ped_id], canonical

    def remove(self, ped_id):
        self.memberships.pop(ped_id, None)


class PendingReceiverModel:
    def __init__(self):
        self.revisions = {}
        self.desired = {}
        self.players = set()
        self.peds = set()
        self.applied = set()

    def receive(self, ped_id, revision, membership):
        current = self.revisions.get(ped_id)
        if current is not None:
            delta = (revision - current) & 0xFFFF
            if delta == 0 or delta >= 0x8000:
                return False
        self.revisions[ped_id] = revision
        self.desired[ped_id] = membership
        self.process()
        return True

    def process(self):
        self.applied.clear()
        for ped_id, membership in self.desired.items():
            if membership.leader is not None and ped_id in self.peds and membership.leader in self.players:
                self.applied.add((membership.leader, membership.slot, ped_id))


class GangGroupSyncTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.packet = (ROOT / "shared/network/packets/peds.h").read_text(encoding="utf-8")
        cls.client_manager = (ROOT / "client/src/CNetworkPedGroupSyncManager.cpp").read_text(encoding="utf-8")
        cls.client_ped_manager = (ROOT / "client/src/CNetworkPedManager.cpp").read_text(encoding="utf-8")
        cls.client_handlers = (ROOT / "client/src/PacketHandlers/peds.cpp").read_text(encoding="utf-8")
        cls.server_handlers = (ROOT / "server/src/PacketHandlers/peds.cpp").read_text(encoding="utf-8")
        cls.server_manager = (ROOT / "server/src/CNetworkPedManager.cpp").read_text(encoding="utf-8")
        cls.server_manager_h = (ROOT / "server/src/CNetworkPedManager.h").read_text(encoding="utf-8")
        cls.server_ped_h = (ROOT / "server/src/CNetworkPed.h").read_text(encoding="utf-8")
        cls.group_membership_h = (
            ROOT / "third_party/plugin-sdk/plugin_sa/game_sa/CPedGroupMembership.h"
        ).read_text(encoding="utf-8")
        cls.groups_h = (ROOT / "third_party/plugin-sdk/plugin_sa/game_sa/CPedGroups.h").read_text(encoding="utf-8")
        cls.config = (ROOT / "shared/config.h").read_text(encoding="utf-8")

    def test_reuses_ped_streams_and_preserves_npc_snapshot_contract_order(self):
        self.assertNotIn("PED_GROUP_UPDATE", self.packet)
        self.assertLess(self.packet.index("struct SPedTaskSnapshot"), self.packet.index("struct SPedGroupMembershipSnapshot"))
        first_budget = re.search(r"MAX_SERIALIZED_BYTES\s*=\s*(\d+)", self.packet)
        self.assertEqual(first_budget.group(1), "32")
        for packet_name in ("PedOnFoot", "PedDriverUpdate", "PedPassengerSync"):
            body = re.search(rf"class {packet_name}.*?\n\}};", self.packet, re.S).group(0)
            self.assertIn("SPedGroupMembershipSnapshot group{}", body)

    def test_wire_state_is_tiny_stable_id_only_and_c2s_owner_bound(self):
        body = re.search(r"struct SPedGroupMembershipSnapshot\s*\{(.*?)\n\};", self.packet, re.S).group(1)
        self.assertNotRegex(body, r"\b(?:CPed|CPlayerPed|CPedGroup|CEntity)\s*\*")
        self.assertNotIn("uintptr", body)
        self.assertIn("SenderPlayerId leaderPlayerId", body)
        self.assertIn("MAX_GROUP_SERIALIZED_BYTES = 4", body)
        self.assertIn("followerSlot < MAX_FOLLOWERS", body)
        self.assertIn("serialize_object(stream, leaderPlayerId)", body)
        self.assertIn("SenderPlayerId deliberately omits this field C2S", body)
        # revision + present bit + three-bit player ID + three-bit follower slot fits in three bytes S2C.
        self.assertLessEqual((16 + 1 + 3 + 3 + 7) // 8, 4)

    def test_stock_pool_and_membership_limits_are_exact(self):
        self.assertIn("CPed *m_apMembers[8]", self.group_membership_h)
        self.assertIn("m_apMembers[7] is a leader", self.group_membership_h)
        self.assertIn("CPedGroup(&ms_groups)[8]", self.groups_h)
        self.assertIn("MAX_SERVER_PLAYERS = 8", self.config)
        self.assertIn("MAX_FOLLOWERS = 7", self.packet)
        self.assertIn("LEADER_MEMBER_SLOT = 7", self.packet)
        self.assertIn("NATIVE_GROUP_COUNT = 8", self.client_manager)

    def test_server_binds_leader_to_authenticated_live_owner_before_relay(self):
        canonical = self.server_handlers.index("incoming.leaderPlayerId.value = owner->m_iPlayerId")
        relay = self.server_handlers.index("GetPacketFactory().SendToAll(*pPedOnFoot")
        self.assertLess(canonical, relay)
        for marker in (
            "member->m_pSyncer != owner",
            "!owner->m_pPeer",
            "CNetworkPlayerManager::GetPlayer(owner->m_iPlayerId) != owner",
            "memberHealth == 0",
            "other->m_pSyncer != owner",
            "other->m_groupSnapshot.followerSlot == group.followerSlot",
            "followerCount < Packets::Peds::SPedGroupMembershipSnapshot::MAX_FOLLOWERS",
        ):
            self.assertIn(marker, self.server_handlers)

    def test_authority_model_rejects_spoof_duplicate_cross_owner_and_overflow(self):
        model = GroupAuthorityModel()
        self.assertIsNone(model.accept(1, sender=2, owner=3, member_alive=True, leader_alive=True,
                                       requested=Membership(2, 0)))
        self.assertIsNone(model.accept(1, sender=2, owner=2, member_alive=True, leader_alive=False,
                                       requested=Membership(2, 0)))
        self.assertIsNotNone(model.accept(1, 2, 2, True, True, Membership(99, 0)))
        self.assertIsNone(model.accept(2, 2, 2, True, True, Membership(2, 0)))

        cross_owner = GroupAuthorityModel()
        cross_owner.memberships[9] = (3, Membership(2, 1))
        self.assertIsNone(cross_owner.accept(2, 2, 2, True, True, Membership(2, 2)))

        full = GroupAuthorityModel()
        for slot in range(7):
            full.memberships[slot + 10] = (2, Membership(2, slot))
        self.assertIsNone(full.accept(50, 2, 2, True, True, Membership(2, 0)))

    def test_revisions_reject_replay_wrap_and_survive_ped_id_reuse(self):
        model = GroupAuthorityModel()
        first = model.accept(4, 1, 1, True, True, Membership(1, 0))
        self.assertEqual(first[0], 1)
        self.assertEqual(model.accept(4, 1, 1, True, True, Membership(1, 0))[0], 1)
        model.revisions[4] = 0xFFFF
        wrapped = model.accept(4, 1, 1, True, True, Membership(1, 1))
        self.assertEqual(wrapped[0], 1)
        model.remove(4)
        replacement = model.accept(4, 1, 1, True, True, Membership(1, 2))
        self.assertEqual(replacement[0], 2)

        receiver = PendingReceiverModel()
        self.assertTrue(receiver.receive(4, 0xFFFF, Membership(1, 1)))
        self.assertTrue(receiver.receive(4, 1, Membership(1, 2)))
        self.assertFalse(receiver.receive(4, 0xFFFF, Membership(1, 1)))
        self.assertIn("m_anGroupRevisions", self.server_manager_h)
        self.assertIn("AdvanceGroupRevision", self.server_manager)
        self.assertIn("retaining this tombstone", self.client_manager)

    def test_pending_receiver_tolerates_member_and_leader_stream_order(self):
        receiver = PendingReceiverModel()
        self.assertTrue(receiver.receive(12, 7, Membership(3, 2)))
        self.assertFalse(receiver.applied)
        receiver.peds.add(12)
        receiver.process()
        self.assertFalse(receiver.applied)
        receiver.players.add(3)
        receiver.process()
        self.assertEqual(receiver.applied, {(3, 2, 12)})

        observe = self.client_handlers.index("ObserveRemoteMembership(pPedOnFoot->pedid")
        resolve = self.client_handlers.index("CNetworkPedManager::GetPed(pPedOnFoot->pedid)")
        self.assertLess(observe, resolve)
        self.assertIn("valid canonical update can arrive first", self.client_handlers)

    def test_client_uses_only_owned_native_groups_and_stable_slots(self):
        for marker in (
            "CPedGroups::AddGroup()",
            "m_groupMembership.SetLeader(leader)",
            "m_groupMembership.AddMember(networkPed->m_pPed, followerSlot)",
            "m_groupMembership.RemoveMember",
            "CPedGroups::RemoveGroup(groupId)",
            "CPedGroups::GetGroupId(group)",
        ):
            self.assertIn(marker, self.client_manager)
        self.assertIn("if (!isOwnedRemoteGroup)", self.client_manager)
        self.assertIn("must never be stolen from a stock or local-player group", self.client_manager)
        self.assertIn("playerId == CNetworkPlayerManager::m_nMyId", self.client_manager)

    def test_membership_covers_onfoot_driver_and_passenger_late_join_updates(self):
        self.assertEqual(self.client_ped_manager.count("CaptureLocalMembership(pNetworkPed, packet.group)"), 3)
        self.assertIn("pPedDriverUpdate->group", self.server_handlers)
        self.assertIn("pPedPassengerSync->group", self.server_handlers)
        self.assertIn("ObserveRemoteMembership(pPedDriverUpdate->pedid", self.client_handlers)
        self.assertIn("ObserveRemoteMembership(pPedPassengerSync->pedid", self.client_handlers)

    def test_release_death_disconnect_and_owner_migration_clear_membership(self):
        self.assertIn("if (memberHealth == 0)", self.server_handlers)
        self.assertGreaterEqual(self.server_handlers.count("ClearGroupMembership(pNetworkPed)"), 3)
        self.assertIn("ClearGroupMembership(ped);", self.server_manager)
        self.assertIn("ped->m_groupSnapshot = {}", self.server_manager)
        self.assertIn("clear.toggleOwnership = false", self.server_manager)
        self.assertIn("GetPacketFactory().SendToAll(clear)", self.server_manager)
        self.assertIn("if (!pAssignPedSyncer->toggleOwnership)", self.client_handlers)
        self.assertIn("OnPedRemoved(ped->m_nPedId)", self.client_ped_manager)
        self.assertIn("CNetworkPedGroupSyncManager::Reset()", self.client_ped_manager)

    def test_remote_replication_cannot_feed_back_and_offline_stock_is_preserved(self):
        self.assertIn("!networkPed->m_bSyncing", self.client_manager)
        self.assertIn("if (!CNetwork::m_bAuthenticated)", self.client_manager)
        self.assertIn("!CNetwork::m_bAuthenticated || !networkPed", self.client_manager)
        self.assertIn("if (!pNetworkPed->m_bSyncing)", self.client_ped_manager)

    def test_remote_group_process_keeps_membership_but_never_runs_local_group_ai(self):
        # gta-reversed: CPedGroups::Process (0x5FC800) calls CPedGroup::Process for every active group;
        # CPedGroup::Process (0x5FC7E0) calls membership.Process then groupIntelligence.Process.
        self.assertIn("CPedGroups::Process (0x5FC800)", self.client_manager)
        self.assertIn("CPedGroup::Process (0x5FC7E0)", self.client_manager)
        self.assertIn("patch::RedirectJump(0x5FC7E0, CPedGroup__Process_GroupSyncHook)", self.client_manager)
        hook = re.search(
            r"void __fastcall CPedGroup__Process_GroupSyncHook\(.*?\n\}",
            self.client_manager,
            re.S,
        ).group(0)
        membership = hook.index("group->m_groupMembership.Process()")
        validation = hook.index("groupIntelligence->m_pPedGroup == group")
        intelligence = hook.index("groupIntelligence->Process()")
        self.assertLess(membership, intelligence)
        self.assertLess(validation, intelligence)
        self.assertIn("if (!suppressRemoteIntelligence && groupIntelligence->m_pPedGroup == group)", hook)
        self.assertIn("const bool suppressRemoteIntelligence = IsManagerOwnedRemoteGroup(group)", hook)
        self.assertIn("!CNetwork::m_bAuthenticated || !group", self.client_manager)
        self.assertIn("group->m_groupMembership.GetLeader() == localPlayer", self.client_manager)
        self.assertIn("state.leader != localPlayer", self.client_manager)
        self.assertIn("membership-only mirror", self.client_manager)
        self.assertLess(
            self.client_manager.index("EnsureGroupProcessHookInstalled();"),
            self.client_manager.index("ApplyMember(pedId);", self.client_manager.index("ObserveRemoteMembership")),
        )

    def test_group_intelligence_uses_native_game_offset_not_shifted_sdk_member(self):
        self.assertIn("NATIVE_GROUP_INTELLIGENCE_OFFSET = 0x30", self.client_manager)
        self.assertIn("offsetof(CPedGroup, m_groupIntelligence) == 0x34", self.client_manager)
        self.assertIn("GetNativeGroupIntelligence(group)", self.client_manager)
        self.assertIn(
            "reinterpret_cast<unsigned char*>(group) + NATIVE_GROUP_INTELLIGENCE_OFFSET",
            self.client_manager,
        )
        self.assertNotIn("group->m_groupIntelligence.Process()", self.client_manager)


if __name__ == "__main__":
    unittest.main()
