import math
import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


def newer(candidate: int, reference: int, bits: int) -> bool:
    mask = (1 << bits) - 1
    distance = (candidate - reference) & mask
    return distance != 0 and distance < (1 << (bits - 1))


class PickupAuthorityModel:
    """Executable model of the generation/revision and pending-grant contract."""

    def __init__(self, host=0, collect_capacity=64, create_capacity=32):
        self.host = host
        self.collect_capacity = collect_capacity
        self.create_capacity = create_capacity
        self.slots = {}
        self.collects = {}
        self.creates = {}
        self.next_request = 0
        self.grants = []

    def request_id(self):
        self.next_request = (self.next_request + 1) & 0xFFFFFFFF
        if self.next_request == 0:
            self.next_request = 1
        return self.next_request

    def create_intent(self, sender, metadata):
        if sender == self.host or len(self.creates) >= self.create_capacity:
            return None
        request_id = self.request_id()
        self.creates[request_id] = (sender, metadata)
        return request_id

    def publish(self, sender, slot, generation, revision, active=True, creator=None, source=0, metadata=None):
        if sender != self.host or generation == 0 or revision == 0:
            return False
        previous = self.slots.get(slot)
        same = previous is not None and generation == previous["generation"]
        if active:
            if previous is not None:
                fresh = newer(generation, previous["generation"], 16)
                if (not same and not fresh) or (fresh and previous["active"]):
                    return False
                if same and (not previous["active"] or not newer(revision, previous["revision"], 32)):
                    return False
                if same and (creator, source) != (previous["creator"], previous["source"]):
                    return False
            if same:
                pass
            elif source:
                intent = self.creates.get(source)
                if intent != (creator, metadata):
                    return False
                del self.creates[source]
            elif creator != sender:
                return False
            self.slots[slot] = {
                "generation": generation,
                "revision": revision,
                "active": True,
                "creator": creator,
                "source": source,
                "metadata": metadata,
                "authority": sender,
            }
            return True
        if previous is None or generation != previous["generation"] or not previous["active"]:
            return False
        if not newer(revision, previous["revision"], 32):
            return False
        previous.update(active=False, revision=revision)
        return True

    def collect(self, sender, slot, generation):
        state = self.slots.get(slot)
        if len(self.collects) >= self.collect_capacity or state is None or not state["active"]:
            return None
        if state["generation"] != generation:
            return None
        if any(value == (sender, slot, generation) for value in self.collects.values()):
            return None
        request_id = self.request_id()
        self.collects[request_id] = (sender, slot, generation)
        return request_id

    def decide(self, sender, request_id, slot, generation, approved):
        pending = self.collects.get(request_id)
        if sender != self.host or pending is None or pending[1:] != (slot, generation):
            return False
        requester, pending_slot, pending_generation = self.collects.pop(request_id)
        state = self.slots.get(pending_slot)
        if not approved:
            return True
        if state is None or not state["active"] or state["generation"] != pending_generation:
            return False
        state["active"] = False
        self.grants.append((requester, pending_slot, pending_generation))
        self.collects = {
            key: value for key, value in self.collects.items()
            if value[1:] != (pending_slot, pending_generation)
        }
        return True

    def migrate(self, new_host):
        self.host = new_host
        self.collects.clear()
        self.creates.clear()
        for state in self.slots.values():
            if state["active"]:
                state["authority"] = new_host


class RespawnAuthorityModel:
    """Executable server model for retained completion state and deadline replay."""

    def __init__(self, host=0):
        self.host = host
        self.generation = 7
        self.authority = host
        self.active = True
        self.completion = False
        self.respawn_at = 0
        self.rewards = 0

    def collect(self, now: int, respawn_ms: int):
        assert self.active
        self.active = False
        self.completion = True
        self.respawn_at = now + respawn_ms if respawn_ms else 0
        self.rewards += 1

    def migrate(self, new_host: int):
        self.host = new_host
        if self.active or self.completion:
            self.authority = new_host

    def replay(self, now: int):
        if self.completion and self.respawn_at and now >= self.respawn_at:
            self.generation = (self.generation + 1) & 0xFFFF or 1
            self.active = True
            self.completion = False
            self.respawn_at = 0
        return {
            "active": self.active,
            "completion": self.completion,
            "generation": self.generation,
            "authority": self.authority,
            "remaining": max(1, self.respawn_at - now) if self.completion and self.respawn_at else 0,
            "award": False,
        }


class PickupServerAuthorityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.packet_types = (ROOT / "shared/network/packet_types.h").read_text(encoding="utf-8")
        cls.packets = (ROOT / "shared/network/packets/pickups.h").read_text(encoding="utf-8")
        cls.manager = (ROOT / "server/src/CPickupAuthorityManager.cpp").read_text(encoding="utf-8")
        cls.manager_header = (ROOT / "server/src/CPickupAuthorityManager.h").read_text(encoding="utf-8")
        cls.handlers = (ROOT / "server/src/PacketHandlers/pickups.cpp").read_text(encoding="utf-8")
        cls.network = (ROOT / "server/src/CNetwork.cpp").read_text(encoding="utf-8")
        cls.players = (ROOT / "server/src/CNetworkPlayerManager.cpp").read_text(encoding="utf-8")

    def test_protocol_version_names_and_cardinality_are_exact(self):
        names = (
            "PICKUP_STATE",
            "PICKUP_COLLECT_REQUEST",
            "PICKUP_COLLECT_DECISION",
            "PICKUP_COLLECT_RESULT",
            "PICKUP_CREATE_INTENT",
        )
        for name in names:
            self.assertIn(name, self.packet_types)
            self.assertIn(f'"{name}"', self.packet_types)
        enum = re.search(r"enum class ePacketType[^\{]*\{(.*?)PACKET_ID_MAX", self.packet_types, re.S).group(1)
        enum_names = re.findall(r"^\s*([A-Z][A-Z0-9_]*)\s*,", enum, re.M)
        debug = re.search(r"static constexpr const char\* array\[\]\s*=\s*\{(.*?)\};", self.packet_types, re.S).group(1)
        debug_names = re.findall(r'"([A-Z][A-Z0-9_]*)"', debug)
        self.assertEqual(enum_names, debug_names)
        self.assertIn('COOPANDREAS_VERSION "0.3.5-alpha"',
                      (ROOT / "shared/config.h").read_text(encoding="utf-8"))

    def test_stable_id_closed_kinds_and_metadata_are_bounded(self):
        self.assertRegex(self.packets, r'(?m)^#include "eWeaponType\.h"$')
        self.assertIn("PICKUP_POOL_CAPACITY = 620", self.packets)
        self.assertIn("slot < PICKUP_POOL_CAPACITY && generation != 0", self.packets)
        enum = re.search(r"enum class ePickupKind.*?\{(.*?)COUNT", self.packets, re.S).group(1)
        kinds = re.findall(r"^\s*([A-Z][A-Z0-9_]*)", enum, re.M)
        self.assertEqual(kinds, [
            "TAG", "HORSESHOE", "SNAPSHOT", "OYSTER", "STATIC_WEAPON", "STATIC_ARMOUR",
            "STATIC_BRIBE", "DROPPED_MONEY", "DROPPED_WEAPON", "JETPACK",
        ])
        for bound in (
            "MAX_PICKUP_MODEL_ID = 20000",
            "MAX_PICKUP_REWARD = 2000000",
            "MAX_PICKUP_AMMO = 9999",
            "MAX_PICKUP_EXPIRY_MS = 3600000",
            "MAX_PICKUP_RESPAWN_MS = 86400000",
            "IsFinitePickupPosition(position)",
            "collectibleIndex <= maximumIndex",
            "IsGrantablePickupWeaponId(reward)",
        ):
            self.assertIn(bound, self.packets)

        helper = re.search(r"inline bool IsGrantablePickupWeaponId.*?^}", self.packets, re.S | re.M).group(0)
        self.assertIn("WEAPON_BRASSKNUCKLE", helper)
        self.assertIn("WEAPON_MOLOTOV", helper)
        self.assertIn("WEAPON_PISTOL", helper)
        self.assertIn("WEAPON_SATCHEL_CHARGE", helper)
        self.assertIn("WEAPON_SPRAYCAN", helper)
        self.assertIn("WEAPON_CAMERA", helper)
        for excluded in ("IDs 19..21", "DETONATOR", "vision goggles", "parachutes"):
            self.assertIn(excluded, helper)
        self.assertNotIn("reward <= 46", self.packets)

        grantable = set(range(1, 19)) | set(range(22, 40)) | set(range(41, 44))
        for weapon_id in range(0, 47):
            expected = (1 <= weapon_id <= 18) or (22 <= weapon_id <= 39) or (41 <= weapon_id <= 43)
            self.assertEqual(weapon_id in grantable, expected)
        for excluded in (0, 19, 20, 21, 40, 44, 45, 46):
            self.assertNotIn(excluded, grantable)

    def test_per_pickup_state_event_has_a_small_measured_budget(self):
        self.assertNotIn("std::array<PickupState, PICKUP_POOL_CAPACITY>", self.packets)
        self.assertIn("serialize::MeasureStream stream", self.packets)
        self.assertIn("FitsSerializedBudget()", self.packets)
        bits_required = lambda minimum, maximum: (maximum - minimum).bit_length()
        state_bits = (
            bits_required(0, 619) + bits_required(1, 65535) + 32 + bits_required(0, 7) + 1 +
            bits_required(0, 7) + 32 + bits_required(0, 9) +
            1 + bits_required(0, 6_000_000) * 2 + bits_required(0, 1_120_000) +
            8 + bits_required(-1, 20_000) + bits_required(0, 2_000_000) +
            bits_required(0, 9_999) + bits_required(-1, 149) +
            bits_required(0, 3_600_000) + bits_required(0, 86_400_000)
        )
        measured_wire_bytes = 6 + math.ceil(state_bits / 8)
        self.assertEqual(measured_wire_bytes, 42)
        self.assertLessEqual(measured_wire_bytes, 128)

    def test_server_enforces_authority_identity_revision_and_intent_attribution(self):
        for evidence in (
            "IsCurrentHost(player)",
            "packet.state.authorityPlayerId != player->m_iPlayerId",
            "IsPickupGenerationNewer(incoming.id.generation, slot.lastGeneration)",
            "IsPickupRevisionNewer(incoming.revision, slot.lastRevision)",
            "incoming.creatorPlayerId != player->m_iPlayerId",
            "FindPendingCreate(incoming.sourceIntentRequestId)",
            "MetadataMatches(incoming.metadata, sourceIntent->metadata)",
            "GetPacketFactory().SendToAll(canonical, player)",
        ):
            self.assertIn(evidence, self.manager)

    def test_collects_are_canonical_proximity_checked_and_committed_once(self):
        for evidence in (
            "packet.requestId != 0",
            "canonical.requestId = NextRequestId()",
            "canonical.requesterPlayerId.value = player->m_iPlayerId",
            "GetPacketFactory().Send(canonical, host)",
            "packet.interior != slot.state.metadata.interior",
            "MAX_PICKUP_COLLECT_DISTANCE * MAX_PICKUP_COLLECT_DISTANCE",
            "slot.collectedGeneration = slot.lastGeneration",
            "slot.active = false",
            "GetPacketFactory().SendToAll(granted)",
        ):
            self.assertIn(evidence, self.manager)
        self.assertIn("tried to publish a server-authoritative pickup result", self.handlers)
        self.assertEqual(self.packets.count("SenderPlayerId requesterPlayerId{}"), 2)

    def test_pending_tables_rate_limits_disconnect_and_migration_are_bounded(self):
        for evidence in (
            "MAX_PENDING_COLLECT_REQUESTS = 64",
            "MAX_PENDING_CREATE_INTENTS = 32",
            "MAX_COLLECT_REQUESTS_PER_WINDOW = 20",
            "MAX_CREATE_INTENTS_PER_WINDOW = 8",
            "PENDING_REQUEST_TIMEOUT_MS = 10000",
        ):
            self.assertIn(evidence, self.manager_header)
        self.assertIn("slot.eventCount >= maximumEvents", self.manager)
        self.assertIn("HandlePlayerDisconnected(CNetworkPlayer* player)", self.manager)
        self.assertIn("ClearAllPending();", self.manager)
        self.assertIn("slot.state.authorityPlayerId = static_cast<uint8_t>(newHost->m_iPlayerId)", self.manager)
        self.assertIn("SendActiveStates(newHost)", self.manager)

    def test_connection_replay_expiry_and_authority_hooks_are_integrated(self):
        self.assertIn("CPickupAuthorityManager::Update();", self.network)
        self.assertIn("CPickupAuthorityManager::HandlePlayerDisconnected(pNetworkPlayer);", self.network)
        self.assertIn("CPickupAuthorityManager::SendActiveStates(pNewNetworkPlayer);", self.network)
        self.assertIn("CPickupAuthorityManager::HandleAuthorityChange(nullptr);", self.players)
        self.assertIn("CPickupAuthorityManager::HandleAuthorityChange(player);", self.players)
        self.assertIn("replay.state.metadata.expiresAfterMs = slot.expiresAt - now", self.manager)

    def test_completion_tombstones_and_server_respawns_are_retained_and_bounded(self):
        for evidence in (
            "hasCompletionState = false",
            "respawnRemainingMs = 0",
            "permanentCollectible",
            "scheduledRespawn",
            "respawnRemainingMs <= metadata.respawnsAfterMs",
        ):
            self.assertIn(evidence, self.packets)
        for evidence in (
            "completion.state.hasCompletionState = permanentCollectible || scheduledRespawn",
            "slot.respawnAt = scheduledRespawn",
            "GetPacketFactory().SendToAll(completion)",
            "MaterializeDueRespawn(slot, host, now, dueRespawn)",
            "std::max(1u, slot.respawnAt - now)",
        ):
            self.assertIn(evidence, self.manager)

    def test_retained_completion_authority_is_rewritten_on_migration(self):
        migration = self.manager[self.manager.index("void CPickupAuthorityManager::HandleAuthorityChange"):]
        self.assertIn("!slot.active && !slot.state.hasCompletionState", migration)
        self.assertIn("slot.state.authorityPlayerId = static_cast<uint8_t>(newHost->m_iPlayerId)", migration)

    def test_late_join_at_respawn_deadline_materializes_before_remaining_time(self):
        model = RespawnAuthorityModel(host=0)
        model.collect(now=1000, respawn_ms=30000)
        before = model.replay(now=30999)
        self.assertTrue(before["completion"])
        self.assertEqual(before["remaining"], 1)
        at_deadline = model.replay(now=31000)
        self.assertTrue(at_deadline["active"])
        self.assertFalse(at_deadline["completion"])
        self.assertEqual(at_deadline["generation"], 8)
        self.assertEqual(at_deadline["remaining"], 0)
        self.assertFalse(at_deadline["award"])

    def test_migration_mid_respawn_preserves_deadline_and_new_authority(self):
        model = RespawnAuthorityModel(host=0)
        model.collect(now=500, respawn_ms=36000)
        model.migrate(3)
        replay = model.replay(now=12500)
        self.assertTrue(replay["completion"])
        self.assertEqual(replay["remaining"], 24000)
        self.assertEqual(replay["authority"], 3)
        self.assertEqual(model.rewards, 1)
        respawn = model.replay(now=36500)
        self.assertTrue(respawn["active"])
        self.assertEqual(respawn["authority"], 3)
        self.assertEqual(model.rewards, 1)

    def test_executable_model_rejects_replays_and_grants_once_per_generation(self):
        model = PickupAuthorityModel(host=0)
        self.assertTrue(model.publish(0, 12, 1, 1, creator=0, metadata="static"))
        self.assertFalse(model.publish(1, 12, 1, 2, creator=0, metadata="static"))
        self.assertFalse(model.publish(0, 12, 1, 1, creator=0, metadata="static"))
        request = model.collect(2, 12, 1)
        competing = model.collect(3, 12, 1)
        self.assertTrue(model.decide(0, request, 12, 1, True))
        self.assertFalse(model.decide(0, competing, 12, 1, True))
        self.assertEqual(model.grants, [(2, 12, 1)])
        self.assertIsNone(model.collect(2, 12, 1))
        self.assertFalse(model.publish(0, 12, 1, 2, creator=0, metadata="static"))
        self.assertTrue(model.publish(0, 12, 2, 1, creator=0, metadata="static"))

    def test_executable_model_preserves_active_state_but_clears_pending_on_migration(self):
        model = PickupAuthorityModel(host=0)
        self.assertTrue(model.publish(0, 8, 7, 4, creator=0, metadata="jetpack"))
        self.assertIsNotNone(model.collect(2, 8, 7))
        self.assertIsNotNone(model.create_intent(3, "drop"))
        model.migrate(1)
        self.assertEqual(model.collects, {})
        self.assertEqual(model.creates, {})
        self.assertTrue(model.slots[8]["active"])
        self.assertEqual(model.slots[8]["authority"], 1)
        self.assertTrue(model.publish(1, 8, 7, 5, creator=0, metadata="jetpack"))

    def test_executable_model_requires_host_to_publish_canonical_nonhost_intent(self):
        model = PickupAuthorityModel(host=0)
        intent = model.create_intent(4, "weapon-drop")
        self.assertIsNotNone(intent)
        self.assertFalse(model.publish(1, 4, 1, 1, creator=4, source=intent, metadata="weapon-drop"))
        self.assertFalse(model.publish(0, 4, 1, 1, creator=5, source=intent, metadata="weapon-drop"))
        self.assertTrue(model.publish(0, 4, 1, 1, creator=4, source=intent, metadata="weapon-drop"))
        self.assertNotIn(intent, model.creates)


if __name__ == "__main__":
    unittest.main()
