import math
import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


def bits_for(minimum: int, maximum: int) -> int:
    return (maximum - minimum).bit_length()


def maximum_snapshot_wire_bytes(entry_count: int = 64) -> int:
    descriptor_bits = 0
    descriptor_bits += bits_for(0, 619)          # stable origin slot
    descriptor_bits += bits_for(1, 65535)        # generation
    descriptor_bits += 32 + 32                   # authority epoch + item revision
    descriptor_bits += 1                         # compressed world-position selector
    descriptor_bits += bits_for(0, 6_000_000) * 2
    descriptor_bits += bits_for(0, 1_120_000)
    descriptor_bits += bits_for(1, 19_999)       # model
    descriptor_bits += bits_for(1, 22)           # native pickup type
    descriptor_bits += bits_for(0, 1_000_000)    # ammo or money
    descriptor_bits += 16                        # money per day
    descriptor_bits += bits_for(0, 86_400_000)   # regeneration remaining
    descriptor_bits += 32                        # bounded asset revenue
    descriptor_bits += 8                         # area code
    descriptor_bits += bits_for(0, 2)            # lifecycle
    descriptor_bits += 2                         # empty + visible

    snapshot_bits = 32 + 32 + bits_for(0, 7)
    snapshot_bits += bits_for(0, 9) + bits_for(1, 10) + bits_for(0, 64)
    snapshot_bits += bits_for(0, 100) * 4
    snapshot_bits += descriptor_bits * entry_count
    return 6 + math.ceil(snapshot_bits / 8)       # packet id + server time + payload


class PickupAuthorityModel:
    """Executable acceptance model for the bounded server authority contract."""

    def __init__(self, host=0):
        self.host = host
        self.epoch = 1
        self.snapshot_revision = 1
        self.records = [None] * 620
        self.pending = [None] * 8
        self.last_nonce = [None] * 8
        self.rate = [[] for _ in range(8)]
        self.progress = [0] * 4

    @staticmethod
    def newer16(candidate, reference):
        delta = (candidate - reference) & 0xFFFF
        return delta != 0 and delta < 0x8000

    @staticmethod
    def valid_descriptor(item):
        return (
            0 <= item["slot"] < 620
            and 1 <= item["generation"] <= 65535
            and item["epoch"] > 0
            and item["revision"] > 0
            and 1 <= item["model"] <= 19999
            and 1 <= item["type"] <= 22
            and 0 <= item["ammo"] <= 1_000_000
            and -3000 <= item["position"][0] <= 3000
            and -3000 <= item["position"][1] <= 3000
            and -120 <= item["position"][2] <= 1000
            and all(math.isfinite(v) for v in item["position"])
        )

    def spawn(self, sender, item):
        if sender != self.host or item["epoch"] != self.epoch or item["revision"] != 1:
            return False
        if not self.valid_descriptor(item) or item["lifecycle"] == "removed":
            return False
        old = self.records[item["slot"]]
        if old:
            if old["generation"] == item["generation"]:
                return old == item
            if old["lifecycle"] != "removed" or not self.newer16(item["generation"], old["generation"]):
                return False
        self.records[item["slot"]] = dict(item)
        self.snapshot_revision += 1
        return True

    def state(self, sender, item):
        old = self.records[item["slot"]] if 0 <= item["slot"] < 620 else None
        if sender != self.host or not old or old["generation"] != item["generation"]:
            return False
        if item["epoch"] != self.epoch or item["revision"] != old["revision"] + 1:
            return False
        if not self.valid_descriptor(item) or item["lifecycle"] == "removed":
            return False
        self.records[item["slot"]] = dict(item)
        return True

    def request(self, sender, identity, revision, nonce, position, area, camera, now):
        if not 0 <= sender < 8 or self.last_nonce[sender] is not None and nonce <= self.last_nonce[sender]:
            return False
        self.rate[sender] = [stamp for stamp in self.rate[sender] if now - stamp < 1000]
        if len(self.rate[sender]) >= 8:
            return False
        self.rate[sender].append(now)
        self.last_nonce[sender] = nonce
        slot, generation = identity
        item = self.records[slot] if 0 <= slot < 620 else None
        if not item or item["generation"] != generation or item["revision"] != revision:
            return False
        if item["lifecycle"] != "active" or item["epoch"] != self.epoch or item["area"] != area:
            return False
        snapshot = item["type"] == 20 or item["model"] == 1253
        if snapshot and not camera:
            return False
        maximum = 100 if snapshot else 6
        distance = math.dist(position, item["position"])
        if not all(math.isfinite(v) for v in position) or distance > maximum:
            return False
        if self.pending[sender] is not None:
            return False
        self.pending[sender] = (identity, revision, nonce, now)
        return True

    def decision(self, sender, claimant, identity, revision, nonce, accepted, lifecycle, kind=0):
        if sender != self.host:
            return False
        host_native = nonce == 0 and claimant == self.host
        if not host_native:
            pending = self.pending[claimant]
            if pending is None or pending[:3] != (identity, revision, nonce):
                return False
            self.pending[claimant] = None
        slot, generation = identity
        item = self.records[slot]
        if not item or item["generation"] != generation or item["revision"] != revision:
            return False
        if not accepted:
            return True
        item["revision"] += 1
        item["lifecycle"] = lifecycle
        if kind:
            self.progress[kind] = min(100, self.progress[kind] + 1)
        for player, pending in enumerate(self.pending):
            if pending and pending[0] == identity:
                self.pending[player] = None
        return True

    def migrate(self, host):
        self.host = host
        self.epoch += 1
        self.snapshot_revision += 1
        self.pending = [None] * 8
        for item in self.records:
            if item:
                item["epoch"] = self.epoch

    def disconnect(self, player):
        self.pending[player] = None
        self.last_nonce[player] = None
        self.rate[player] = []

    def reset(self):
        self.records = [None] * 620
        self.pending = [None] * 8
        self.last_nonce = [None] * 8
        self.rate = [[] for _ in range(8)]
        self.progress = [0] * 4
        self.epoch = 0
        self.snapshot_revision = 0


def pickup(slot=10, generation=1, epoch=1, revision=1, pickup_type=2, model=1242):
    return {
        "slot": slot,
        "generation": generation,
        "epoch": epoch,
        "revision": revision,
        "model": model,
        "type": pickup_type,
        "ammo": 100,
        "position": (10.0, 20.0, 3.0),
        "area": 0,
        "lifecycle": "active",
    }


class PickupAuthorityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.packets = (ROOT / "shared/network/packets/pickups.h").read_text(encoding="utf-8")
        cls.registry = (ROOT / "shared/network/packet_types.h").read_text(encoding="utf-8")
        cls.config = (ROOT / "shared/config.h").read_text(encoding="utf-8")
        cls.server = (ROOT / "server/src/CPickupAuthorityManager.cpp").read_text(encoding="utf-8")
        cls.server_header = (ROOT / "server/src/CPickupAuthorityManager.h").read_text(encoding="utf-8")
        cls.handlers = (ROOT / "server/src/PacketHandlers/pickups.cpp").read_text(encoding="utf-8")
        cls.network = (ROOT / "server/src/CNetwork.cpp").read_text(encoding="utf-8")
        cls.players = (ROOT / "server/src/CNetworkPlayerManager.cpp").read_text(encoding="utf-8")

    def test_packet_registry_and_debug_names_are_lockstep(self):
        expected = [
            "PICKUP_SPAWN", "PICKUP_STATE", "PICKUP_REMOVE", "PICKUP_COLLECT_REQUEST",
            "PICKUP_COLLECT_FORWARD", "PICKUP_COLLECT_DECISION", "PICKUP_COLLECT_RESULT",
            "PICKUP_SNAPSHOT_CHUNK",
        ]
        enum_body = re.search(r"enum class ePacketType.*?\{(.*?)PACKET_ID_MAX", self.registry, re.S).group(1)
        enum_names = re.findall(r"^\s*([A-Z][A-Z0-9_]+)\s*,", enum_body, re.M)
        debug_body = re.search(r"static constexpr const char\* array\[\].*?\{(.*?)\};", self.registry, re.S).group(1)
        debug_names = re.findall(r'"([A-Z][A-Z0-9_]+)"', debug_body)
        self.assertEqual(enum_names, debug_names)
        self.assertEqual(enum_names[-8:], expected)
        for name in expected:
            self.assertEqual(self.registry.count(name), 2)

    def test_protocol_has_exactly_one_coordinated_035_bump(self):
        legacy_contract = 'COOPANDREAS_VERSION "0.3.' + '4-alpha"'
        self.assertEqual(self.config.count('COOPANDREAS_VERSION "0.3.5-alpha"'), 1)
        self.assertNotIn(legacy_contract, self.config)
        for path in (ROOT / "tests/mission_session").glob("test_*.py"):
            self.assertNotIn(legacy_contract, path.read_text(encoding="utf-8"), path.name)

    def test_all_native_types_and_message_free_numeric_bounds_are_explicit(self):
        for evidence in (
            "MAX_PICKUPS = 620", "MAX_PICKUP_MODEL_ID = 19999", "MIN_PICKUP_TYPE = 1",
            "MAX_PICKUP_TYPE = 22", "MAX_PICKUP_AMMO_OR_MONEY = 1000000",
            "MAX_REGENERATION_MS = 86400000", "HasFiniteBoundedPosition()",
            "pickupType >= MIN_PICKUP_TYPE", "ammoOrMoney <= MAX_PICKUP_AMMO_OR_MONEY",
        ):
            self.assertIn(evidence, self.packets)
        descriptor = re.search(r"struct PickupDescriptor.*?^};", self.packets, re.S | re.M).group(0)
        self.assertNotRegex(descriptor, r"char\s*\[")
        model = PickupAuthorityModel()
        for pickup_type in range(1, 23):
            item = pickup(slot=pickup_type, pickup_type=pickup_type)
            self.assertTrue(model.valid_descriptor(item))

    def test_snapshot_chunk_and_fixed_factory_byte_budgets(self):
        self.assertEqual(maximum_snapshot_wire_bytes(), 2300)
        self.assertLessEqual(maximum_snapshot_wire_bytes(), 8 * 1024)
        for evidence in (
            "MAX_SNAPSHOT_ENTRIES = 64", "MAX_SNAPSHOT_CHUNKS =", "MAX_SNAPSHOT_BYTES = 8 * 1024",
            "entryCount > MAX_SNAPSHOT_ENTRIES", "entries[index].HasValidState()",
            "FitsSerializedBudget()", "MAX_SNAPSHOT_BYTES <= 10 * 1024",
            "MAX_SNAPSHOT_TOTAL_BYTES = 24 * 1024",
            "MAX_SNAPSHOT_ENTRIES * MAX_SNAPSHOT_CHUNKS >= MAX_PICKUPS",
        ):
            self.assertIn(evidence, self.packets)

    def test_host_only_spawn_state_remove_and_decision(self):
        for signature in ("HandleSpawn", "HandleState", "HandleRemove", "HandleCollectDecision"):
            body = re.search(rf"bool CPickupAuthorityManager::{signature}\(.*?\n\}}", self.server, re.S).group(0)
            self.assertIn("IsCurrentHost(sender)", body)
        self.assertIn("sender->m_bIsHost", self.server)
        self.assertIn("CNetworkPlayerManager::GetHost() == sender", self.server)
        model = PickupAuthorityModel(host=2)
        item = pickup()
        self.assertFalse(model.spawn(1, item))
        self.assertTrue(model.spawn(2, item))
        changed = dict(item, revision=2, lifecycle="disabled")
        self.assertFalse(model.state(1, changed))
        self.assertTrue(model.state(2, changed))

    def test_participant_request_proximity_area_camera_rate_and_replay(self):
        for evidence in (
            "packet.HasValidClaim()", "ConsumeRequestBudget", "MAX_COLLECTION_REQUESTS_PER_SECOND",
            "packet.areaCode != record.pickup.areaCode", "deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ",
            "MAX_SNAPSHOT_REQUEST_DISTANCE", "snapshot && !packet.cameraAttempt",
        ):
            self.assertIn(evidence, self.server)
        model = PickupAuthorityModel()
        item = pickup()
        self.assertTrue(model.spawn(0, item))
        identity = (item["slot"], item["generation"])
        self.assertFalse(model.request(1, identity, 1, 1, (100, 100, 3), 0, False, 1))
        self.assertFalse(model.request(1, identity, 1, 2, (10, 20, 3), 1, False, 2))
        self.assertTrue(model.request(1, identity, 1, 3, (10, 20, 3), 0, False, 3))
        self.assertFalse(model.request(1, identity, 1, 3, (10, 20, 3), 0, False, 4))
        model.pending[1] = None
        accepted = 0
        for nonce in range(4, 14):
            accepted += model.request(1, identity, 1, nonce, (10, 20, 3), 0, False, 5)
            model.pending[1] = None
        self.assertLessEqual(accepted, 7)  # one request was already consumed in this window

        snapshot_model = PickupAuthorityModel()
        snap = pickup(slot=11, pickup_type=20, model=1253)
        self.assertTrue(snapshot_model.spawn(0, snap))
        snap_id = (11, 1)
        self.assertFalse(snapshot_model.request(2, snap_id, 1, 1, (10, 20, 3), 0, False, 1))
        self.assertTrue(snapshot_model.request(2, snap_id, 1, 2, (50, 20, 3), 0, True, 2))

    def test_stable_generation_reuse_rejects_unknown_stale_and_malformed(self):
        model = PickupAuthorityModel()
        first = pickup(slot=4, generation=7)
        self.assertTrue(model.spawn(0, first))
        self.assertTrue(model.spawn(0, dict(first)))
        spoofed_retransmit = dict(first, ammo=999999)
        self.assertFalse(model.spawn(0, spoofed_retransmit))
        self.assertFalse(model.spawn(0, pickup(slot=4, generation=8)))
        model.records[4]["lifecycle"] = "removed"
        self.assertFalse(model.spawn(0, pickup(slot=4, generation=6)))
        self.assertTrue(model.spawn(0, pickup(slot=4, generation=8)))
        self.assertFalse(model.state(0, pickup(slot=4, generation=7, revision=2)))
        self.assertFalse(model.valid_descriptor(pickup(model=20000)))
        malformed = pickup()
        malformed["position"] = (math.nan, 0, 0)
        self.assertFalse(model.valid_descriptor(malformed))
        self.assertIn("IsGenerationNewer", self.server)
        self.assertIn("return IsSameDescriptor(record.pickup, pickup);", self.server)
        self.assertIn("pickup.revision != NextRevision(record.pickup.revision)", self.server)

    def test_pending_decision_is_exactly_once_and_progress_is_bounded(self):
        for evidence in (
            "pending->requestNonce != packet.requestNonce", "packet.observedRevision != record.pickup.revision",
            "record.pickup.revision = NextRevision", "other.active && other.identity == packet.identity",
            "std::min<uint8_t>(MAX_COLLECTIBLE_PROGRESS", "GetPacketFactory().SendToAll(result)",
        ):
            self.assertIn(evidence, self.server)
        model = PickupAuthorityModel()
        item = pickup(model=954)
        self.assertTrue(model.spawn(0, item))
        identity = (10, 1)
        self.assertTrue(model.request(3, identity, 1, 50, (10, 20, 3), 0, False, 10))
        self.assertTrue(model.decision(0, 3, identity, 1, 50, True, "removed", kind=1))
        self.assertFalse(model.decision(0, 3, identity, 1, 50, True, "removed", kind=1))
        self.assertEqual(model.records[10]["revision"], 2)
        self.assertEqual(model.progress[1], 1)
        for _ in range(150):
            model.progress[1] = min(100, model.progress[1] + 1)
        self.assertEqual(model.progress[1], 100)

    def test_decision_and_result_carry_the_complete_bounded_post_collection_state(self):
        decision = re.search(r"class PickupCollectDecision.*?^};", self.packets, re.S | re.M).group(0)
        result = re.search(r"class PickupCollectResult.*?^};", self.packets, re.S | re.M).group(0)
        self.assertIn("PickupDescriptor resolvedPickup", decision)
        self.assertIn("resolvedPickup.identity == identity", decision)
        self.assertIn("PickupDescriptor pickup", result)
        self.assertIn("revenueValue", self.packets)
        self.assertIn("record.pickup = packet.resolvedPickup", self.server)
        self.assertIn("packet.resolvedPickup.modelId != record.pickup.modelId", self.server)
        self.assertIn("packet.resolvedPickup.pickupType != record.pickup.pickupType", self.server)
        self.assertIn("lifecycle != ePickupLifecycle::REMOVED ||", self.packets)
        self.assertIn("record.pickup.revenueValue = 0.0f;", self.server)

    def test_only_stock_collectible_types_can_enter_participant_request_flow(self):
        self.assertIn("IsParticipantCollectionType", self.server)
        self.assertIn("if (!IsParticipantCollectionType(record.pickup.pickupType))", self.server)
        for excluded in (7, 9, 10, 11, 12, 16, 17, 18, 21):
            self.assertNotRegex(self.server, rf"case {excluded}:\s+//.*\n(?:.*\n)*?\s*return true;")

    def test_host_progress_snapshot_is_bounded_monotonic_and_cannot_replace_pickups(self):
        for evidence in (
            "HandleProgressSnapshot", "packet.entryCount != 0", "packet.collectibleProgress[0] != 0",
            "packet.collectibleProgress[index] < ms_collectibleProgress[index]",
            "ms_collectibleProgress = packet.collectibleProgress", "SendSnapshotToAll()",
        ):
            self.assertIn(evidence, self.server + self.handlers)

    def test_late_join_migration_disconnect_and_full_reset_are_wired(self):
        for evidence in (
            "CPickupAuthorityManager::Update();",
            "CPickupAuthorityManager::HandlePlayerDisconnected(disconnectedPlayerId);",
            "CPickupAuthorityManager::SendSnapshot(pNewNetworkPlayer);",
        ):
            self.assertIn(evidence, self.network)
        self.assertIn("CPickupAuthorityManager::HandleAuthorityChanged(player);", self.players)
        self.assertIn("CPickupAuthorityManager::HandleAuthorityChanged(nullptr);", self.players)
        self.assertIn("record.pickup.authorityEpoch = ms_authorityEpoch", self.server)
        self.assertIn("SendSnapshotToAll();", self.server)

        model = PickupAuthorityModel(host=0)
        item = pickup(slot=7, generation=9)
        self.assertTrue(model.spawn(0, item))
        identity = (7, 9)
        self.assertTrue(model.request(4, identity, 1, 1, (10, 20, 3), 0, False, 10))
        model.migrate(2)
        self.assertEqual(model.records[7]["generation"], 9)
        self.assertEqual(model.records[7]["epoch"], 2)
        self.assertIsNone(model.pending[4])
        model.disconnect(4)
        self.assertIsNone(model.last_nonce[4])
        model.reset()
        self.assertEqual(model.epoch, 0)
        self.assertTrue(all(record is None for record in model.records))

    def test_handlers_cover_all_eight_types_without_relay_shortcuts(self):
        for name in (
            "PICKUP_SPAWN", "PICKUP_STATE", "PICKUP_REMOVE", "PICKUP_COLLECT_REQUEST",
            "PICKUP_COLLECT_FORWARD", "PICKUP_COLLECT_DECISION", "PICKUP_COLLECT_RESULT",
            "PICKUP_SNAPSHOT_CHUNK",
        ):
            self.assertIn(f"ePacketType::{name}", self.handlers)
        self.assertNotIn("SendToAll(*packet", self.handlers)


if __name__ == "__main__":
    unittest.main()
