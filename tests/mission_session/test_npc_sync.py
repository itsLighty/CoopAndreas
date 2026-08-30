import re
import unittest
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


@dataclass(frozen=True)
class TaskPayload:
    task_type: str
    target_type: str = "nothing"
    target_id: int = 0


class TaskAuthorityModel:
    """Small semantic model of the server-canonical transition stream."""

    def __init__(self):
        self.revision = 0
        self.payload = None

    def accept(self, is_owner: bool, valid: bool, payload: TaskPayload):
        if not is_owner or not valid:
            return None
        if payload != self.payload:
            self.revision = (self.revision + 1) & 0xFFFF
            if self.revision == 0:
                self.revision = 1
            self.payload = payload
        return self.revision, self.payload


class TaskReceiverModel:
    def __init__(self):
        self.revision = None
        self.applied = []

    def receive(self, revision: int, payload: TaskPayload):
        if self.revision is not None:
            delta = (revision - self.revision) & 0xFFFF
            if delta == 0 or delta >= 0x8000:
                return False
        self.revision = revision
        self.applied.append(payload)
        return True


def bits_required(minimum: int, maximum: int) -> int:
    return (maximum - minimum).bit_length()


def measured_task_bytes(task_type: str, target_type: str = "ped") -> int:
    """Mirrors serialize::MeasureStream for every branch of SPedTaskSnapshot."""
    bits = 16 + bits_required(0, 5)  # revision + closed task type
    if task_type in {"NONE", "CLIMB"}:
        pass
    elif task_type == "STAND_STILL":
        bits += bits_required(-1, 600000) + 1 + 1
    elif task_type == "WANDER":
        bits += bits_required(0, 3) + bits_required(0, 7) + 1
        bits += bits_required(0, 499)  # compressed [0.1, 50.0] at 0.1 resolution
    elif task_type == "KILL_PED_ON_FOOT":
        bits += bits_required(0, 7)  # eNetworkEntityType
        bits += bits_required(0, 254 if target_type == "ped" else 7)
        bits += bits_required(-1, 600000)
        bits += 8
        bits += bits_required(0, 60000)
        bits += bits_required(0, 100)
        bits += 1
    elif task_type == "JUMP":
        bits += bits_required(0, 1)
    else:
        raise AssertionError(f"unmeasured task variant: {task_type}")
    return (bits + 7) // 8


class RemotePedLifecycleModel:
    def __init__(self):
        self.task = True
        self.aim = True
        self.horn = True
        self.siren = True

    def clear(self):
        self.task = self.aim = self.horn = self.siren = False


class NpcSyncTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.packet = (ROOT / "shared/network/packets/peds.h").read_text(encoding="utf-8")
        cls.client_ped = (ROOT / "client/src/CNetworkPed.cpp").read_text(encoding="utf-8")
        cls.client_ped_h = (ROOT / "client/src/CNetworkPed.h").read_text(encoding="utf-8")
        cls.client_manager = (ROOT / "client/src/CNetworkPedManager.cpp").read_text(encoding="utf-8")
        cls.client_handlers = (ROOT / "client/src/PacketHandlers/peds.cpp").read_text(encoding="utf-8")
        cls.client_network = (ROOT / "client/src/CNetwork.cpp").read_text(encoding="utf-8")
        cls.server_handlers = (ROOT / "server/src/PacketHandlers/peds.cpp").read_text(encoding="utf-8")
        cls.server_manager = (ROOT / "server/src/CNetworkPedManager.cpp").read_text(encoding="utf-8")

    def test_reuses_existing_sync_packet_types(self):
        self.assertIn("ePacketType::PED_ONFOOT, ePacketChannel::SYNC", self.packet)
        self.assertIn("ePacketType::PED_DRIVER_UPDATE, ePacketChannel::SYNC", self.packet)
        self.assertNotIn("PED_TASK_UPDATE", self.packet)
        self.assertNotIn("PED_AIM_UPDATE", self.packet)
        self.assertNotIn("PED_HORN_UPDATE", self.packet)

    def test_task_wire_format_is_allowlisted_bounded_and_pointer_free(self):
        enum_body = re.search(r"enum class ePedTaskSyncType\s*\{(.*?)\};", self.packet, re.S).group(1)
        self.assertEqual(
            set(re.findall(r"\b(NONE|STAND_STILL|WANDER|KILL_PED_ON_FOOT|JUMP|CLIMB)\b", enum_body)),
            {"NONE", "STAND_STILL", "WANDER", "KILL_PED_ON_FOOT", "JUMP", "CLIMB"},
        )
        snapshot = re.search(r"struct SPedTaskSnapshot\s*\{(.*?)\n\};", self.packet, re.S).group(1)
        self.assertNotRegex(snapshot, r"\b(?:CTask|CPed|CEntity|CVehicle)\s*\*")
        self.assertNotIn("uintptr", snapshot)
        self.assertNotIn("serialize_bytes", snapshot)
        self.assertIn("MAX_SERIALIZED_BYTES = 32", snapshot)
        self.assertIn("FitsSerializedBudget", snapshot)
        self.assertIn("static_assert(SPedTaskSnapshot::MAX_SERIALIZED_BYTES <= 64", self.packet)

    def test_every_task_variant_is_measured_against_declared_budget(self):
        declared = int(re.search(r"MAX_SERIALIZED_BYTES = (\d+)", self.packet).group(1))
        variants = {"NONE", "STAND_STILL", "WANDER", "KILL_PED_ON_FOOT", "JUMP", "CLIMB"}
        measured = {variant: measured_task_bytes(variant) for variant in variants}
        measured["KILL_PED_ON_FOOT_PLAYER"] = measured_task_bytes("KILL_PED_ON_FOOT", "player")
        self.assertEqual(set(measured) - {"KILL_PED_ON_FOOT_PLAYER"}, variants)
        for variant, byte_count in measured.items():
            with self.subTest(variant=variant):
                self.assertGreater(byte_count, 0)
                self.assertLessEqual(byte_count, declared)
        self.assertEqual(measured["KILL_PED_ON_FOOT"], 11)
        self.assertIn("serialize::MeasureStream stream", self.packet)
        self.assertIn("measured.Serialize(stream)", self.packet)

    def test_task_semantic_bounds_cover_every_parameter(self):
        required = (
            "standTime >= -1 && standTime <= 600000",
            "wanderDirection <= 7",
            "std::isfinite(wanderRadius)",
            "wanderRadius >= 0.1f && wanderRadius <= 50.0f",
            "killTime >= -1 && killTime <= 600000",
            "killActionDelay >= 0 && killActionDelay <= 60000",
            "killActionChance <= 100",
            "jumpType <= 1",
        )
        for marker in required:
            self.assertIn(marker, self.packet)
        self.assertIn("NETWORK_ENTITY_TYPE_PLAYER || target.entityType == NETWORK_ENTITY_TYPE_PED", self.packet)

    def test_aim_rejects_non_finite_out_of_world_dead_and_non_weapon_states(self):
        self.assertIn("std::isfinite(position.x)", self.packet)
        self.assertIn("position.x >= -3000.0f && position.x <= 3000.0f", self.packet)
        self.assertIn("position.z >= -120.0f && position.z <= 1000.0f", self.packet)
        self.assertIn("healthSnapshot.iHealth > 0", self.packet)
        self.assertIn("IsAimCapableWeapon(weaponSnapshot.iWeaponType)", self.packet)
        self.assertIn("!pPedOnFoot->HasValidAimState()", self.server_handlers)
        self.assertIn("ApplyAimSnapshot", self.client_handlers)
        self.assertIn("ClearRemoteAim", self.client_handlers)

    def test_server_enforces_owner_entity_and_vehicle_semantics_before_relay(self):
        owner_check = self.server_handlers.index("pPed->m_pSyncer != pNetworkPlayer")
        onfoot_relay = self.server_handlers.index("GetPacketFactory().SendToAll(*pPedOnFoot")
        self.assertLess(owner_check, onfoot_relay)
        self.assertIn("HasValidTaskTarget", self.server_handlers)
        self.assertIn("CNetworkPlayerManager::GetPlayer(task.target.entityId)", self.server_handlers)
        self.assertIn("CNetworkPedManager::GetPed(task.target.entityId)", self.server_handlers)
        self.assertIn("target != nullptr && target != owner", self.server_handlers)
        self.assertIn("vehicle->m_pSyncer != player", self.server_handlers)
        self.assertIn("vehicle->m_pPlayers[0] != nullptr", self.server_handlers)
        self.assertIn("other->m_nVehicleId == vehicle->m_nVehicleId", self.server_handlers)

    def test_horn_and_siren_are_continuous_bools_with_capability_validation(self):
        self.assertIn("bool bHorn = false", self.packet)
        self.assertIn("bool bSiren = false", self.packet)
        self.assertIn("serialize_bool(stream, bHorn)", self.packet)
        self.assertIn("serialize_bool(stream, bSiren)", self.packet)
        self.assertIn("IsSirenCapableVehicleModel", self.server_handlers)
        self.assertIn("pVehicle->m_nHornCounter != 0", self.client_manager)
        self.assertIn("pVehicle->UsesSiren()", self.client_manager)
        self.assertIn("vehicle->m_nHornCounter = horn ?", self.client_ped)
        self.assertIn("vehicle->m_nVehicleFlags.bSirenOrAlarm = siren && vehicle->UsesSiren()", self.client_ped)

    def test_siren_allowlist_exactly_matches_sa_uses_siren(self):
        helper = re.search(
            r"inline bool IsSirenCapableVehicleModel\(.*?\n\}(?=\n\nstruct SPedTaskSnapshot)",
            self.packet,
            re.S,
        ).group(0)
        actual = set(re.findall(r"case (MODEL_[A-Z0-9_]+):", helper))
        expected = {
            "MODEL_FIRETRUK",
            "MODEL_AMBULAN",
            "MODEL_MRWHOOP",
            "MODEL_ENFORCER",
            "MODEL_PREDATOR",
            "MODEL_BARRACKS",
            "MODEL_FBIRANCH",
            "MODEL_COPBIKE",
            "MODEL_FBITRUCK",
            "MODEL_COPCARLA",
            "MODEL_COPCARSF",
            "MODEL_COPCARVG",
            "MODEL_COPCARRU",
            "MODEL_SWATVAN",
        }
        self.assertEqual(actual, expected)
        self.assertNotIn("MODEL_RHINO", actual)
        self.assertNotIn("MODEL_POLMAV", actual)

    def test_task_transitions_are_server_canonical_and_not_replayed(self):
        authority = TaskAuthorityModel()
        receiver = TaskReceiverModel()
        stand = TaskPayload("stand")
        kill = TaskPayload("kill", "ped", 4)

        first = authority.accept(True, True, stand)
        self.assertTrue(receiver.receive(*first))
        repeated = authority.accept(True, True, stand)
        self.assertFalse(receiver.receive(*repeated))
        changed = authority.accept(True, True, kill)
        self.assertTrue(receiver.receive(*changed))
        self.assertEqual(receiver.applied, [stand, kill])

        self.assertIsNone(authority.accept(False, True, TaskPayload("jump")))
        self.assertIsNone(authority.accept(True, False, TaskPayload("jump")))
        self.assertIn("CanonicalizeTaskSnapshot", self.server_handlers)
        self.assertIn("incoming.revision = 0", self.server_handlers)
        self.assertIn("!IsNewerRevision(snapshot.revision, m_lastRemoteTask.revision)", self.client_ped)

    def test_revision_wrap_replay_and_owner_migration_model(self):
        authority = TaskAuthorityModel()
        authority.revision = 0xFFFF
        authority.payload = TaskPayload("stand")
        receiver = TaskReceiverModel()
        receiver.revision = 0xFFFF

        wrapped = authority.accept(True, True, TaskPayload("jump"))
        self.assertEqual(wrapped[0], 1)
        self.assertTrue(receiver.receive(*wrapped))
        self.assertFalse(receiver.receive(0xFFFF, TaskPayload("stand")))

        # A replacement owner reporting the same canonical payload does not replay it; a real transition advances it.
        same_after_migration = authority.accept(True, True, TaskPayload("jump"))
        self.assertEqual(same_after_migration[0], 1)
        changed_after_migration = authority.accept(True, True, TaskPayload("climb"))
        self.assertEqual(changed_after_migration[0], 2)

    def test_sender_captures_only_safe_task_data_and_receiver_constructs_fresh_tasks(self):
        self.assertIn("CaptureTaskSnapshot(packet.task)", self.client_manager)
        for task_type in (
            "TASK_SIMPLE_STAND_STILL",
            "TASK_COMPLEX_WANDER",
            "TASK_COMPLEX_KILL_PED_ON_FOOT",
            "TASK_COMPLEX_JUMP",
            "TASK_COMPLEX_CLIMB",
        ):
            self.assertIn(task_type, self.client_ped)
        self.assertIn("snapshot.target.SetEntity", self.client_ped)
        self.assertIn("snapshot.target.GetEntity() != kill->m_pTarget", self.client_ped)
        self.assertIn("new CTaskComplexKillPedOnFoot", self.client_ped)
        self.assertIn("SetTask(newTask, TASK_PRIMARY_PRIMARY, false)", self.client_ped)

    def test_climb_is_explicitly_zero_parameter_and_recreated_locally(self):
        climb_header = (
            ROOT / "third_party/plugin-sdk/plugin_sa/game_sa/CTaskComplexClimb.h"
        ).read_text(encoding="utf-8")
        self.assertRegex(climb_header, r"CTaskComplexClimb\(\s*\)\s*;")
        serializer = self.packet[self.packet.index("template <typename Stream>\n    bool Serialize(Stream& stream)") :]
        climb_wire = re.search(
            r"case ePedTaskSyncType::NONE:\s*case ePedTaskSyncType::CLIMB:(.*?)break;",
            serializer,
            re.S,
        ).group(1)
        self.assertNotIn("serialize_", climb_wire)
        self.assertIn("new CTaskComplexClimb()", self.client_ped)
        self.assertIn("zero-argument constructor", self.client_ped)

    def test_remote_application_cannot_feed_back_during_normal_ownership(self):
        apply_body = re.search(
            r"void CNetworkPed::ApplyTaskSnapshot\(.*?\n\}(?=\n\nvoid CNetworkPed::ApplyAimSnapshot)",
            self.client_ped,
            re.S,
        ).group(0)
        self.assertIn("if (m_bSyncing", apply_body)
        update_body = re.search(
            r"void CNetworkPedManager::Update\(\).*?\n\}(?=\n\nvoid CNetworkPedManager::Process)",
            self.client_manager,
            re.S,
        ).group(0)
        self.assertIn("if (!pNetworkPed->m_bSyncing)", update_body)

    def test_vehicle_exit_death_reassignment_and_stream_out_clear_state(self):
        for event in ("death", "vehicle_exit", "disconnect", "reconnect", "stream_out", "reassignment"):
            with self.subTest(event=event):
                state = RemotePedLifecycleModel()
                state.clear()
                self.assertFalse(any((state.task, state.aim, state.horn, state.siren)))
        self.assertIn("ResetRemoteSyncState(true);", self.client_ped)
        self.assertIn("ClearDriverSignals();", self.client_ped)
        self.assertIn("ClearRemoteAim();", self.client_ped)
        self.assertIn("ClearRemoteTask();", self.client_ped)
        self.assertGreaterEqual(self.client_handlers.count("ResetRemoteSyncState(false)"), 2)
        self.assertIn("pPed->m_fHealth <= 0.0f", self.client_handlers)
        self.assertIn("pNetworkPed->ClearDriverSignals()", self.client_handlers)
        self.assertIn("ValidateRemoteTaskTarget", self.client_manager)
        self.assertIn("CNetworkPedManager::Clear()", self.client_network)
        self.assertIn("m_bRemoteTaskInitialized = false", self.client_ped_h)

    def test_disconnect_and_host_migration_reassign_or_remove_cleanly(self):
        self.assertIn("candidate != player", self.server_manager)
        self.assertIn("ped->m_pSyncer = replacement", self.server_manager)
        self.assertIn("vehicle->ReassignSyncer(replacement)", self.server_manager)
        self.assertIn("ReleaseVehicleUsage(ped)", self.server_manager)
        self.assertIn("vehicle->ReassignSyncer(pNetworkPlayer)", self.server_handlers)
        self.assertIn("vehicle->ReassignSyncer(p)", self.server_handlers)

    def test_offline_gameplay_is_not_hooked_or_suppressed(self):
        owned_sources = "\n".join(
            [self.client_ped, self.client_manager, self.client_handlers]
        )
        self.assertNotIn("CNetwork::m_bAuthenticated =", owned_sources)
        self.assertNotIn("patch::RedirectCall", owned_sources)
        self.assertNotIn("patch::RedirectJump", owned_sources)
        self.assertNotIn("GameHooks", owned_sources)


if __name__ == "__main__":
    unittest.main()
