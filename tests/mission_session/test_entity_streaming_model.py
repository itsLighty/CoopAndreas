import unittest
from dataclasses import dataclass, field
import pathlib
import re


ROOT = pathlib.Path(__file__).resolve().parents[2]


def function_body(source: str, signature: str) -> str:
    match = re.search(signature, source)
    if match is None:
        raise AssertionError(f"missing function matching {signature}")
    opening = source.find("{", match.end())
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : index]
    raise AssertionError(f"unterminated function matching {signature}")


@dataclass
class LogicalEntity:
    distance: float
    area_matches: bool = True
    hosted: bool = False
    critical: bool = False
    dependency: bool = False
    model_loaded: bool = False
    txd_loaded: bool = False
    special_loaded: bool = True
    presented: bool = False
    request_started: int | None = None
    next_request: int = 0
    attempts: int = 0
    newest_snapshot: int = 0
    applied_snapshot: int | None = None


class StreamingLifecycleModel:
    STREAM_IN = 250.0
    STREAM_OUT = 320.0
    RETRY_MS = 250
    TIMEOUT_MS = 10_000
    COOLDOWN_MS = 5_000
    MAX_REQUESTS_PER_TICK = 8
    MAX_MATERIALIZATIONS_PER_TICK = 4

    def tick(self, now: int, entities: list[LogicalEntity]):
        requests = 0
        materializations = 0
        for entity in entities:
            relevant = entity.hosted or entity.critical or entity.dependency
            should_present = entity.area_matches and (
                relevant or entity.distance <= (self.STREAM_OUT if entity.presented else self.STREAM_IN)
            )

            if entity.presented and not should_present and not entity.hosted:
                entity.presented = False
                entity.applied_snapshot = None
                entity.request_started = None
                entity.attempts = 0
                continue
            if entity.presented or not should_present:
                continue

            if entity.request_started is not None and now - entity.request_started >= self.TIMEOUT_MS:
                entity.request_started = None
                entity.attempts = 0
                entity.next_request = now + self.COOLDOWN_MS
                continue

            ready = entity.model_loaded and entity.txd_loaded and entity.special_loaded
            if ready:
                if materializations < self.MAX_MATERIALIZATIONS_PER_TICK:
                    entity.presented = True
                    entity.applied_snapshot = entity.newest_snapshot
                    materializations += 1
                continue

            if requests < self.MAX_REQUESTS_PER_TICK and now >= entity.next_request:
                if entity.request_started is None:
                    entity.request_started = now
                entity.attempts += 1
                entity.next_request = now + self.RETRY_MS
                requests += 1

        return requests, materializations


class EntityStreamingLifecycleModelTests(unittest.TestCase):
    def test_never_materializes_until_model_txd_and_special_resources_are_ready(self):
        model = StreamingLifecycleModel()
        entity = LogicalEntity(distance=20.0, special_loaded=False)

        self.assertEqual(model.tick(0, [entity]), (1, 0))
        entity.model_loaded = True
        self.assertEqual(model.tick(250, [entity]), (1, 0))
        entity.txd_loaded = True
        self.assertEqual(model.tick(500, [entity]), (1, 0))
        entity.special_loaded = True
        self.assertEqual(model.tick(750, [entity]), (0, 1))
        self.assertTrue(entity.presented)

    def test_logical_snapshots_continue_advancing_while_presentation_is_absent(self):
        model = StreamingLifecycleModel()
        entity = LogicalEntity(distance=600.0, model_loaded=True, txd_loaded=True)
        for revision in range(1, 7):
            entity.newest_snapshot = revision
            model.tick(revision * 20, [entity])
        self.assertFalse(entity.presented)

        entity.distance = 100.0
        model.tick(200, [entity])
        self.assertTrue(entity.presented)
        self.assertEqual(entity.applied_snapshot, 6)

    def test_hysteresis_and_dependencies_prevent_incorrect_stream_out(self):
        model = StreamingLifecycleModel()
        entity = LogicalEntity(distance=100.0, model_loaded=True, txd_loaded=True)
        model.tick(0, [entity])
        self.assertTrue(entity.presented)

        entity.distance = 300.0
        model.tick(100, [entity])
        self.assertTrue(entity.presented)
        entity.distance = 500.0
        entity.dependency = True
        model.tick(200, [entity])
        self.assertTrue(entity.presented)
        entity.dependency = False
        model.tick(300, [entity])
        self.assertFalse(entity.presented)

    def test_queue_work_is_bounded_and_timeout_enters_cooldown(self):
        model = StreamingLifecycleModel()
        waiting = [LogicalEntity(distance=10.0) for _ in range(20)]
        self.assertEqual(model.tick(0, waiting), (8, 0))

        ready = [LogicalEntity(distance=10.0, model_loaded=True, txd_loaded=True) for _ in range(10)]
        self.assertEqual(model.tick(100, ready), (0, 4))
        self.assertEqual(sum(entity.presented for entity in ready), 4)

        timed_out = waiting[0]
        timed_out.request_started = 1
        timed_out.next_request = 1
        model.tick(10_001, [timed_out])
        self.assertIsNone(timed_out.request_started)
        self.assertEqual(timed_out.next_request, 15_001)
        self.assertEqual(timed_out.attempts, 0)

    def test_vehicle_remove_detaches_local_and_remote_occupants_before_deletion(self):
        vehicle = object()
        local = {"vehicle": vehicle, "in_vehicle": True}
        remote_driver = {"vehicle": vehicle, "in_vehicle": True}
        remote_passenger = {"vehicle": vehicle, "in_vehicle": True}
        unrelated = {"vehicle": object(), "in_vehicle": True}
        occupants = [local, remote_driver, remote_passenger, unrelated]

        deletion_order = []
        for occupant in occupants:
            if occupant["vehicle"] is vehicle:
                occupant["vehicle"] = None
                occupant["in_vehicle"] = False
                deletion_order.append("detach")
        deletion_order.append("delete_vehicle")

        self.assertEqual(deletion_order, ["detach", "detach", "detach", "delete_vehicle"])
        self.assertFalse(local["in_vehicle"])
        self.assertFalse(remote_driver["in_vehicle"])
        self.assertFalse(remote_passenger["in_vehicle"])
        self.assertTrue(unrelated["in_vehicle"])


class EntityStreamingSourceContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manager = (ROOT / "client/src/CNetworkEntityStreamManager.cpp").read_text(encoding="utf-8")
        cls.player = (ROOT / "client/src/CNetworkPlayer.cpp").read_text(encoding="utf-8")
        cls.transition = (ROOT / "client/src/CEntryExitTransitionSync.cpp").read_text(encoding="utf-8")
        cls.vehicle = (ROOT / "client/src/CNetworkVehicle.cpp").read_text(encoding="utf-8")
        cls.vehicle_manager = (ROOT / "client/src/CNetworkVehicleManager.cpp").read_text(encoding="utf-8")
        cls.vehicle_handler = (ROOT / "client/src/PacketHandlers/vehicles.cpp").read_text(encoding="utf-8")

    def test_player_tasks_and_enex_are_consumed_once_per_packet_or_materialization(self):
        apply_cached = function_body(self.player, r"CNetworkPlayer::ApplyCachedPresentation\(")
        self.assertIn("ProcessPendingPresentation()", apply_cached)
        self.assertNotIn("HandleTask(m_pendingTask)", apply_cached)
        pending = function_body(self.player, r"CNetworkPlayer::ProcessPendingPresentation\(")
        self.assertIn("ApplyPendingTaskOnce()", pending)
        self.assertNotIn("ApplyTaskPresentation(m_pendingTask)", pending)

        replay = function_body(self.player, r"CNetworkPlayer::ApplyPendingTaskOnce\(")
        self.assertIn("m_nAppliedTaskGeneration == m_nPendingTaskGeneration", replay)
        self.assertIn("ApplyTaskPresentation(m_pendingTask)", replay)
        self.assertEqual(replay.count("ApplyTaskPresentation"), 1)

        receive = function_body(self.transition, r"CEntryExitTransitionSync::Receive\(")
        self.assertIn("++pNetworkPlayer->m_nPendingEnExTransitionGeneration", receive)
        self.assertIn("ReplayPending(pNetworkPlayer)", receive)
        enex_replay = function_body(self.transition, r"CEntryExitTransitionSync::ReplayPending\(")
        self.assertIn(
            "m_nAppliedEnExTransitionGeneration == pNetworkPlayer->m_nPendingEnExTransitionGeneration",
            enex_replay,
        )
        respawn = function_body(self.player, r"CNetworkPlayer::Respawn\(")
        self.assertIn("m_bHasPendingTask = false", respawn)
        self.assertIn("m_bHasPendingEnExTransition = false", respawn)

    def test_player_readiness_gate_does_not_claim_a_remote_model_lease(self):
        self.assertNotIn("player->m_bModelLeaseHeld", self.manager)
        forget = function_body(self.manager, r"CNetworkEntityStreamManager::Forget\(CNetworkPlayer")
        self.assertNotIn("ReleaseModelLease", forget)

    def test_production_model_gate_is_async_bounded_and_checks_rw_and_txd_readiness(self):
        self.assertNotIn("LoadAllRequestedModels", self.manager)
        self.assertNotIn("Sleep(", self.manager)
        self.assertIn("MAX_MODEL_REQUESTS_PER_FRAME = 8", self.manager)
        self.assertIn("MAX_MATERIALIZATIONS_PER_FRAME = 4", self.manager)
        self.assertIn("MODEL_LOAD_TIMEOUT_MS = 10000", self.manager)
        ready = function_body(self.manager, r"bool IsModelReady\(")
        self.assertIn("m_nLoadState != LOADSTATE_LOADED", ready)
        self.assertIn("modelInfo->m_pRwObject", ready)
        self.assertIn("HasReadyTxd(modelInfo)", ready)
        process = function_body(self.manager, r"CNetworkEntityStreamManager::Process\(")
        self.assertNotIn("ApplyCachedPresentation()", process)
        self.assertIn("ProcessPendingPresentation()", process)

    def test_vehicle_remove_detaches_every_native_occupant_before_native_delete(self):
        remove = function_body(
            self.vehicle_handler,
            r"PACKET_HANDLER\(ePacketType::VEHICLE_REMOVE\s*,",
        )
        self.assertIn("CNetworkVehicleManager::Remove(pNetworkVehicle)", remove)
        manager_remove = function_body(self.vehicle_manager, r"CNetworkVehicleManager::Remove\(")
        self.assertIn("ClearVehicleRelations(vehicle)", manager_remove)
        clear = function_body(self.vehicle_manager, r"CNetworkVehicleManager::ClearVehicleRelations\(")
        self.assertIn("vehicle->StreamOut()", clear)

        stream_out = function_body(self.vehicle, r"CNetworkVehicle::StreamOut\(")
        for evidence in (
            "for (CPed* ped : CPools::ms_pPedPool)",
            "ped->m_pVehicle != vehicle",
            "WARP_CHAR_FROM_CAR_TO_COORD",
            "vehicle->m_pDriver = nullptr",
            "passenger = nullptr",
            "ped->m_pVehicle = nullptr",
            "ped->m_nPedFlags.bInVehicle = false",
        ):
            self.assertIn(evidence, stream_out)
        self.assertLess(stream_out.index("for (CPed* ped"), stream_out.index("delete vehicle;"))
        self.assertLess(stream_out.index("DetachTrailerLinks();"), stream_out.index("delete vehicle;"))


if __name__ == "__main__":
    unittest.main()
