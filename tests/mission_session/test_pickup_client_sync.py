import re
import unittest
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


@dataclass
class ClientSlot:
    generation: int = 0
    revision: int = 0
    active: bool = False
    pending: bool = False
    last_granted: int = 0
    grants: int = 0
    retry_after: int = 0

    @staticmethod
    def newer(candidate: int, reference: int, bits: int) -> bool:
        mask = (1 << bits) - 1
        distance = (candidate - reference) & mask
        return distance != 0 and distance < (1 << (bits - 1))

    def state(self, generation: int, revision: int, active: bool) -> bool:
        if self.generation:
            if generation != self.generation and not self.newer(generation, self.generation, 16):
                return False
            if generation == self.generation and not self.newer(revision, self.revision, 32):
                return False
        self.generation = generation
        self.revision = revision
        self.active = active
        self.pending = False
        return True

    def request(self, now: int = 0) -> bool:
        if not self.active or self.pending or now < self.retry_after:
            return False
        self.pending = True
        return True

    def result(self, generation: int, approved: bool, local_collector: bool, now: int = 0) -> bool:
        if generation != self.generation:
            return False
        if not approved:
            self.pending = False
            self.retry_after = now + 1000
            return True
        if self.last_granted == generation:
            return False
        self.last_granted = generation
        self.pending = False
        self.active = False
        if local_collector:
            self.grants += 1
        return True


class NativePickupPool:
    """Small model of SA's slot/reference packed pickup handle contract."""

    CAPACITY = 620

    def __init__(self):
        self.references = [0] * self.CAPACITY
        self.collected = []

    def get_unique(self, slot: int) -> int:
        return self.references[slot] * self.CAPACITY + slot

    def get_new_unique(self, slot: int) -> int:
        self.references[slot] = (self.references[slot] + 1) & 0x7FFF
        if self.references[slot] == 0:
            self.references[slot] = 1
        return self.get_unique(slot)

    def actual(self, handle: int) -> int:
        slot = handle % self.CAPACITY
        return slot if self.get_unique(slot) == handle else -1

    def add_to_collected(self, native_slot: int):
        # 0x455240 performs GetUniquePickupIndex(native_slot) internally.
        self.collected.append(self.get_unique(native_slot))


class PickupClientSyncTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = (ROOT / "client/src/CNetworkPickupManager.h").read_text(encoding="utf-8")
        cls.manager = (ROOT / "client/src/CNetworkPickupManager.cpp").read_text(encoding="utf-8")
        cls.hooks = (ROOT / "client/src/Hooks/PickupHooks.cpp").read_text(encoding="utf-8")
        cls.handlers = (ROOT / "client/src/PacketHandlers/pickups.cpp").read_text(encoding="utf-8")
        cls.world_hooks = (ROOT / "client/src/Hooks/WorldHooks.cpp").read_text(encoding="utf-8")
        cls.client_world_handlers = (ROOT / "client/src/PacketHandlers/world.cpp").read_text(encoding="utf-8")
        cls.server_world_handlers = (ROOT / "server/src/PacketHandlers/world.cpp").read_text(encoding="utf-8")
        cls.util = (ROOT / "client/src/CUtil.cpp").read_text(encoding="utf-8")
        cls.packets = (ROOT / "shared/network/packets/pickups.h").read_text(encoding="utf-8")
        cls.native_pickups_header = (
            ROOT / "third_party/plugin-sdk/plugin_sa/game_sa/CPickups.h").read_text(encoding="utf-8")
        cls.hook_registry = (ROOT / "client/src/Hooks/CHook.cpp").read_text(encoding="utf-8")
        cls.network = (ROOT / "client/src/CNetwork.cpp").read_text(encoding="utf-8")
        cls.system_handlers = (ROOT / "client/src/PacketHandlers/system.cpp").read_text(encoding="utf-8")

    def test_all_pickup_kinds_have_native_or_tag_paths(self):
        for kind in (
            "TAG", "HORSESHOE", "SNAPSHOT", "OYSTER", "STATIC_WEAPON", "STATIC_ARMOUR",
            "STATIC_BRIBE", "DROPPED_MONEY", "DROPPED_WEAPON", "JETPACK",
        ):
            self.assertIn(f"ePickupKind::{kind}", self.manager)
        for model in ("MODEL_CJ_HORSE_SHOE", "MODEL_CJ_OYSTER", "MODEL_BODYARMOUR", "MODEL_BRIBE",
                      "MODEL_MONEY", "MODEL_JETPACK"):
            self.assertIn(model, self.manager)

    def test_registry_is_bounded_and_wire_identity_is_not_a_pointer(self):
        self.assertIn("std::array<Slot, Packets::Pickups::PICKUP_POOL_CAPACITY>", self.header)
        self.assertIn("std::array<int16_t, Packets::Pickups::PICKUP_POOL_CAPACITY>", self.header)
        self.assertIn("TAG_NETWORK_SLOT_COUNT = 100", self.header)
        self.assertNotRegex(self.packets, r"CPickup\s*\*")
        self.assertNotRegex(self.packets, r"CObject\s*\*")

    def test_host_observes_state_and_nonhost_sends_only_bounded_intents(self):
        self.assertIn("ObserveNativePool();", self.manager)
        self.assertIn("PublishNewState(networkSlot, nativeSlot, metadata", self.manager)
        self.assertIn("IsCreationIntentPickupKind(metadata.kind)", self.manager)
        self.assertIn("PickupCreateIntent intent{}", self.manager)
        self.assertIn("PROVISIONAL_NETWORK_SLOT", self.manager)
        self.assertIn("provisional.creationIntentSent = true", self.manager)
        nonhost = self.manager[self.manager.index("else\n        {", self.manager.index("if (m_localPlayerIsAuthority)",
                                                                 self.manager.index("void CNetworkPickupManager::ObserveNativePool"))):]
        self.assertNotIn("NeutralizeNativePickup(nativeSlot);", nonhost[:nonhost.index("}\n    }\n}")])
        self.assertNotIn("std::vector<", self.header)

    def test_host_uses_actual_synchronized_requester_state(self):
        for evidence in (
            "CNetworkPlayerManager::GetPlayer(packet.requesterPlayerId.value)",
            "requesterPed->GetPosition()",
            "packet.interior != requesterPed->m_nAreaCode",
            "REQUEST_POSITION_TOLERANCE * REQUEST_POSITION_TOLERANCE",
            "IsPickupEligibleForPed(m_slots[networkSlot], requesterPed",
        ):
            self.assertIn(evidence, self.manager)

    def test_native_awards_are_suppressed_until_approved_result(self):
        self.assertIn("CNetworkPickupManager::IsManagedNativeSlot(i)", self.hooks)
        update_loop = self.hooks[self.hooks.index("for (int i = start; i < end; ++i)", 200):]
        self.assertIn("continue;", update_loop)
        self.assertIn("slot.lastGrantedGeneration == packet.id.generation", self.manager)
        self.assertIn("packet.collectorPlayerId == CNetworkPlayerManager::m_nMyId", self.manager)
        self.assertIn("GrantApprovedPickup(packet.grantedState);", self.manager)

    def test_approved_collection_notifies_scripts_and_handles_respawn(self):
        self.assertIn("CPickups::AddToCollectedPickupsArray(nativeSlot)", self.manager)
        self.assertNotRegex(self.manager, r"AddToCollectedPickupsArray\(\s*pickupHandle\s*\)")
        self.assertIn("UsesCollectedPickupArray(slot.state.metadata.kind)", self.manager)
        self.assertIn("metadata.respawnsAfterMs", self.manager)
        self.assertNotIn("ProcessRespawns();", self.manager)
        self.assertIn("incoming.respawnRemainingMs > 0", self.manager)

    def test_snapshot_and_tag_collection_use_event_specific_hooks(self):
        self.assertIn("RequestLocalSnapshotCapture", self.hooks)
        for callsite in ("0x456B1B", "0x456B90", "0x456BB6"):
            self.assertIn(callsite, self.hooks)
        self.assertIn("NotifyLocalTagSprayed(LastTagEntity, previousTagStat", self.world_hooks)
        self.assertIn("CTagManager::ms_tagDesc[index].m_nAlpha == 255", self.manager)
        self.assertIn("CompleteTagVisual(packet.grantedState, localCollector)", self.manager)
        self.assertIn("TheCamera.IsSphereVisible", self.manager)

    def test_remote_tag_progress_cannot_bypass_pickup_authority(self):
        self.assertIn("std::min<uint8_t>(LastTagAlpha, 254)", self.world_hooks)
        self.assertIn("Never send legacy bFullySprayed online", self.world_hooks)
        online_completion = self.world_hooks[self.world_hooks.index("if (result == 2 && LastTagEntity != nullptr)"):
                                             self.world_hooks.index("return result;", self.world_hooks.index(
                                                 "if (result == 2 && LastTagEntity != nullptr)"))]
        self.assertNotIn("TagUpdate packet", online_completion)
        self.assertIn("pTagUpdate->payload.bFullySprayed", self.server_world_handlers)
        self.assertIn("std::min<uint8_t>(pTagUpdate->payload.alpha, 254)", self.client_world_handlers)
        self.assertIn("tag.bFullySprayed = false", self.server_world_handlers)
        self.assertIn("std::min<uint8_t>(tag.alpha, 254)", self.client_world_handlers)
        self.assertNotIn("TheCamera.m_bWideScreenOn", self.client_world_handlers)
        self.assertIn("requesterIsLocalHost", self.manager)
        self.assertIn("return !requireLocalTagEvidence || IsTagReady(slot) || slot.localTagCompletionEvidence;",
                      self.manager)
        self.assertIn("m_slots[tagIndex].localTagCompletionEvidence = true", self.manager)
        self.assertIn("m_slots[networkSlot].localTagCompletionEvidence = false", self.manager)
        self.assertIn("CStats::SetStatValue(STAT_TAGS_SPRAYED, previousTagStat)", self.manager)

    def test_model_loading_is_retry_bounded_and_released(self):
        self.assertIn("MODEL_REQUEST_RETRY_MS = 250", self.header)
        self.assertIn("MAX_MODEL_REQUESTS_PER_TICK = 4", self.header)
        self.assertIn("MAX_MATERIALIZATIONS_PER_TICK = 4", self.header)
        self.assertIn("CStreaming::RequestModel", self.manager)
        self.assertNotIn("LoadAllRequestedModels", self.manager)
        self.assertIn("CStreaming::SetModelIsDeletable(modelId)", self.manager)
        self.assertIn("CStreaming::SetModelTxdIsDeletable(modelId)", self.manager)
        self.assertIn("slot.active && slot.materialized", self.manager)

    def test_interior_expiry_and_pool_reuse_are_explicit(self):
        self.assertIn("metadata.interior", self.manager)
        self.assertIn("RemainingNativeLifetime", self.manager)
        self.assertIn("CPickups::GenerateNewOne(", self.manager)
        self.assertIn("CPickups::GetActualPickupIndex(pickupHandle)", self.manager)
        self.assertNotRegex(self.manager, r"m_nReferenceIndex\s*=")
        self.assertIn("FindMatchingProvisionalNativeSlot", self.manager)
        self.assertIn("FindFreeNetworkSlot()", self.manager)
        self.assertIn("IsPickupGenerationNewer", self.manager)
        self.assertIn("IsPickupRevisionNewer", self.manager)

    def test_jetpack_remote_task_drop_cannot_create_duplicate_intents(self):
        self.assertIn("SuppressSyntheticJetpackDrop(player->m_pPed->GetPosition())", self.util)
        self.assertIn("IsSyntheticJetpackDrop(metadata)", self.manager)
        self.assertIn("SYNTHETIC_DROP_SUPPRESSION_MS = 2000", self.header)

    def test_handlers_cover_server_to_client_pickup_flow(self):
        expected = {
            "PICKUP_STATE": "HandleState",
            "PICKUP_COLLECT_REQUEST": "HandleCollectRequest",
            "PICKUP_COLLECT_RESULT": "HandleCollectResult",
            "PICKUP_CREATE_INTENT": "HandleCreateIntent",
        }
        for packet, handler in expected.items():
            self.assertRegex(self.handlers, re.compile(
                rf"PACKET_HANDLER\(ePacketType::{packet}.*?{handler}", re.S))
        self.assertNotIn("PICKUP_COLLECT_DECISION", self.handlers)

    def test_lifecycle_apis_are_exposed_for_parent_integration(self):
        self.assertIn("static void ResetNetworkState();", self.header)
        self.assertIn("static void HandleAuthorityChanged(uint8_t authorityPlayerId, bool localPlayerIsAuthority);",
                      self.header)
        self.assertIn('#include "PickupHooks.h"', self.hook_registry)
        self.assertIn("PickupHooks::InjectHooks();", self.hook_registry)
        reset = self.network[self.network.index("void CNetwork::ResetConnectionState()"):
                             self.network.index("void CNetwork::", self.network.index(
                                 "void CNetwork::ResetConnectionState()") + 1)
                             if "void CNetwork::" in self.network[self.network.index(
                                 "void CNetwork::ResetConnectionState()") + 1:] else len(self.network)]
        self.assertIn("CNetworkPickupManager::ResetNetworkState();", reset)
        self.assertLess(reset.index("CNetworkPickupManager::ResetNetworkState();"),
                        reset.index("CNetworkPedManager::Clear();"))
        assign_host = self.system_handlers[self.system_handlers.index("PLAYER_ASSIGN_HOST"):
                                           self.system_handlers.index("PLAYER_CHAT_MESSAGE")]
        self.assertIn("HandleAuthorityChanged(pPlayerAssignHost->playerid, true)", assign_host)
        self.assertIn("HandleAuthorityChanged(pPlayerAssignHost->playerid, false)", assign_host)

    def test_executable_model_rejects_replays_and_grants_local_once(self):
        slot = ClientSlot()
        self.assertTrue(slot.state(7, 1, True))
        self.assertFalse(slot.state(7, 1, True))
        self.assertTrue(slot.request())
        self.assertFalse(slot.request())
        self.assertTrue(slot.result(7, True, True))
        self.assertFalse(slot.result(7, True, True))
        self.assertEqual(slot.grants, 1)
        self.assertFalse(slot.active)
        self.assertTrue(slot.state(8, 1, True))
        self.assertTrue(slot.request())

    def test_executable_model_never_rewards_noncollector(self):
        slot = ClientSlot()
        self.assertTrue(slot.state(12, 3, True))
        self.assertTrue(slot.result(12, True, False))
        self.assertEqual(slot.grants, 0)
        self.assertFalse(slot.result(12, True, True))

    def test_executable_model_denial_reopens_only_the_same_generation(self):
        slot = ClientSlot()
        self.assertTrue(slot.state(2, 9, True))
        self.assertTrue(slot.request())
        self.assertTrue(slot.result(2, False, True, now=50))
        self.assertFalse(slot.request(now=50))
        self.assertTrue(slot.request(now=1050))
        self.assertFalse(slot.result(1, True, True))
        self.assertEqual(slot.grants, 0)

    def test_existing_native_handle_is_preserved_and_reused(self):
        for evidence in (
            "SCM variables keep the handle returned by their original creation opcode",
            "BindNativePickup(slot, matchingNativeSlot)",
            "slot.ownsNativePickup = false",
            "GenerateNewOne above establishes a fresh reference generation",
        ):
            self.assertIn(evidence, self.manager)

    def test_pool_reuse_invalidates_stale_handles_and_collects_current_generation(self):
        self.assertIn("GetNewUniquePickupIndex(int pickupIndex)", self.native_pickups_header)
        pool = NativePickupPool()
        slot = 619
        first = pool.get_new_unique(slot)
        self.assertEqual(pool.actual(first), slot)
        pool.add_to_collected(slot)
        self.assertEqual(pool.collected, [first])

        reused = pool.get_new_unique(slot)
        self.assertNotEqual(first, reused)
        self.assertEqual(pool.actual(first), -1)
        self.assertEqual(pool.actual(reused), slot)
        pool.add_to_collected(slot)
        self.assertEqual(pool.collected[-1], reused)

    def test_completion_tombstones_apply_without_reward_and_preserve_respawn_handles(self):
        self.assertIn("incoming.hasCompletionState", self.manager)
        self.assertIn("CompleteTagVisual(incoming, false)", self.manager)
        self.assertIn("RemoveNativePickup(slot, notifyScripts, incoming.respawnRemainingMs > 0)", self.manager)
        completion_path = self.manager[self.manager.index("void CNetworkPickupManager::HandleState"):
                                       self.manager.index("bool CNetworkPickupManager::IsTagReady")]
        self.assertNotIn("GrantApprovedPickup", completion_path)


if __name__ == "__main__":
    unittest.main()
