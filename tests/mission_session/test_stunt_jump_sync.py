import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


def function_body(source: str, signature_pattern: str) -> str:
    signature = re.search(signature_pattern, source)
    if signature is None:
        raise AssertionError(f"missing function matching {signature_pattern}")
    opening = source.find("{", signature.end())
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : index]
    raise AssertionError(f"unterminated function matching {signature_pattern}")


class StuntAuthorityModel:
    def __init__(self):
        self.definition = {"start": (0, 10), "finish": (90, 110), "reward": 500}
        self.completed = False
        self.attempts = {}
        self.awards = []

    def start(self, player, now, position, speed, vehicle=1):
        if self.completed or player in self.attempts or not (0 <= position <= 13) or speed < 0.4:
            return False
        self.attempts[player] = (now, position, vehicle)
        return True

    def complete(self, player, now, position, vehicle=1):
        attempt = self.attempts.pop(player, None)
        if self.completed or attempt is None:
            return False
        started, start_position, started_vehicle = attempt
        elapsed = now - started
        maximum_distance = elapsed * 0.25 + 25
        if started_vehicle != vehicle or elapsed < 100 or elapsed > 20000:
            return False
        if not (86 <= position <= 114) or abs(position - start_position) > maximum_distance:
            return False
        self.completed = True
        self.awards.append(player)
        return True


class AttemptHandshakeModel:
    MAX_RETRIES = 3

    def __init__(self):
        self.active = True
        self.pending = "START"
        self.retries = 0

    def result(self, action, accepted, retryable=False):
        if not self.active or action != self.pending:
            return
        if accepted:
            self.pending = None
        elif retryable and self.retries < self.MAX_RETRIES:
            self.retries += 1
        else:
            self.active = False

    def state_replay(self, completed):
        # Canonical state replay is not an acknowledgement for this request.
        _ = completed


class StuntJumpSyncTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.packet_types = (ROOT / "shared/network/packet_types.h").read_text(encoding="utf-8")
        cls.packets = (ROOT / "shared/network/packets/stunts.h").read_text(encoding="utf-8")
        cls.client = (ROOT / "client/src/CStuntJumpSyncManager.cpp").read_text(encoding="utf-8")
        cls.client_header = (ROOT / "client/src/CStuntJumpSyncManager.h").read_text(encoding="utf-8")
        cls.client_handler = (ROOT / "client/src/PacketHandlers/stunts.cpp").read_text(encoding="utf-8")
        cls.client_network = (ROOT / "client/src/CNetwork.cpp").read_text(encoding="utf-8")
        cls.client_system = (ROOT / "client/src/PacketHandlers/system.cpp").read_text(encoding="utf-8")
        cls.game_hooks = (ROOT / "client/src/Hooks/GameHooks.cpp").read_text(encoding="utf-8")
        cls.patch = (ROOT / "client/src/CPatch.cpp").read_text(encoding="utf-8")
        cls.server = (ROOT / "server/src/CStuntJumpAuthorityManager.cpp").read_text(encoding="utf-8")
        cls.server_header = (ROOT / "server/src/CStuntJumpAuthorityManager.h").read_text(encoding="utf-8")
        cls.server_handler = (ROOT / "server/src/PacketHandlers/stunts.cpp").read_text(encoding="utf-8")
        cls.server_network = (ROOT / "server/src/CNetwork.cpp").read_text(encoding="utf-8")
        cls.server_players = (ROOT / "server/src/CNetworkPlayerManager.cpp").read_text(encoding="utf-8")
        cls.server_vehicles = (ROOT / "server/src/PacketHandlers/vehicles.cpp").read_text(encoding="utf-8")
        cls.server_vehicle_header = (ROOT / "server/src/CNetworkVehicle.h").read_text(encoding="utf-8")

    def test_protocol_version_packet_names_and_cardinality_are_exact(self):
        for name in ("STUNT_DEFINITION", "STUNT_ATTEMPT", "STUNT_ATTEMPT_RESULT", "STUNT_STATE"):
            self.assertIn(name, self.packet_types)
            self.assertIn(f'"{name}"', self.packet_types)
        enum = re.search(
            r"enum class ePacketType[^\{]*\{(.*?)PACKET_ID_MAX", self.packet_types, re.S
        ).group(1)
        enum_names = re.findall(r"^\s*([A-Z][A-Z0-9_]*)\s*,", enum, re.M)
        debug = re.search(
            r"static constexpr const char\* array\[\]\s*=\s*\{(.*?)\};", self.packet_types, re.S
        ).group(1)
        self.assertEqual(enum_names, re.findall(r'"([A-Z][A-Z0-9_]*)"', debug))
        self.assertIn(
            'COOPANDREAS_VERSION "0.3.8-alpha"',
            (ROOT / "shared/config.h").read_text(encoding="utf-8"),
        )

    def test_wire_contract_is_pointer_free_closed_and_bounded(self):
        self.assertIn("STUNT_JUMP_CAPACITY = 256", self.packets)
        self.assertIn("slot < STUNT_JUMP_CAPACITY && fingerprint != 0", self.packets)
        self.assertIn("STUNT_MAX_BOX_EXTENT = 512.0f", self.packets)
        self.assertIn("STUNT_MAX_REPORTED_SPEED = 8.0f", self.packets)
        self.assertIn("MAX_SERIALIZED_BYTES = 256", self.packets)
        self.assertIn("FitsSerializedBudget()", self.packets)
        action = re.search(r"enum class eStuntAttemptAction.*?\{(.*?)COUNT", self.packets, re.S).group(1)
        self.assertEqual(
            re.findall(r"^\s*([A-Z_]+)", action, re.M),
            ["START", "HIT_FINISH", "CANCEL", "COMPLETE"],
        )
        reasons = re.search(
            r"enum class eStuntAttemptResultReason.*?\{(.*?)COUNT", self.packets, re.S
        ).group(1)
        self.assertEqual(
            re.findall(r"^\s*([A-Z_]+)", reasons, re.M),
            [
                "NONE",
                "RATE_LIMITED",
                "CATALOG_NOT_READY",
                "INVALID_STUNT",
                "ALREADY_COMPLETED",
                "DRIVER_SNAPSHOT_NOT_READY",
                "OUT_OF_RANGE",
                "INVALID_TRANSITION",
                "TIMEOUT",
            ],
        )
        for evidence in (
            "class StuntAttemptResult",
            "requestId != 0 && clientSessionNonce != 0",
            "retryAfterMs <= MAX_RETRY_AFTER_MS",
            "MAX_RETRY_AFTER_MS = 1000",
        ):
            self.assertIn(evidence, self.packets)
        for forbidden in ("std::vector", "std::deque", "NativeStuntJump*", "CStuntJump*", "void*"):
            self.assertNotIn(forbidden, self.packets)

    def test_stable_identity_hashes_normalized_definition_not_native_address(self):
        for evidence in (
            "HashStuntFloat",
            "std::lround(value * 100.0f)",
            "id.fingerprint == definition.CalculateFingerprint()",
            "AccumulateCatalogHash",
            "catalogCount > 0 && catalogCount <= STUNT_JUMP_CAPACITY",
        ):
            self.assertIn(evidence, self.packets)
        build = function_body(self.client, r"StuntDefinition BuildDefinition\(")
        for field in (
            "jump.start.m_vecMin",
            "jump.start.m_vecMax",
            "jump.finish.m_vecMin",
            "jump.finish.m_vecMax",
            "jump.camera",
            "jump.reward",
        ):
            self.assertIn(field, build)
        self.assertNotIn("reinterpret_cast", build)

    def test_native_hook_preserves_offline_stock_update_and_replaces_only_online_path(self):
        inject = function_body(self.client, r"CStuntJumpSyncManager::InjectHook\(")
        self.assertIn("stuntUpdateCall = 0x53C0C1", inject)
        self.assertIn("destination != 0x49C490", inject)
        self.assertIn("patch::RedirectCall(stuntUpdateCall, ProcessNativeUpdate)", inject)
        process = function_body(self.client, r"CStuntJumpSyncManager::ProcessNativeUpdate\(")
        self.assertIn("if (!CNetwork::m_bAuthenticated)", process)
        self.assertIn("plugin::CallDyn(m_nativeUpdateAddress)", process)
        self.assertIn("ProcessOnlineJump()", process)
        self.assertIn("CStuntJumpSyncManager::InjectHook();", self.game_hooks)
        self.assertNotIn("patch::Nop(0x49C892, 10)", self.patch)

    def test_per_player_slow_motion_saves_applies_and_exactly_restores_local_time_scale(self):
        start = function_body(self.client, r"StartLocalSlowMotionPresentation\(")
        apply = function_body(self.client, r"ApplyLocalSlowMotionPresentation\(")
        stop = function_body(self.client, r"StopLocalSlowMotionPresentation\(")
        self.assertIn("m_previousTimeScale = CTimer::ms_fTimeScale", start)
        self.assertIn("CTimer::ms_fTimeScale = 0.3f", start)
        self.assertIn("CTimer::ms_fTimeScale = 0.3f", apply)
        self.assertIn("CTimer::ms_fTimeScale = m_previousTimeScale", stop)
        self.assertIn("TheCamera.SetCamPositionForFixedMode", start)
        self.assertIn("TheCamera.TakeControl", start)
        self.assertIn("GetTickCount()", apply)
        self.assertIn("TheCamera.m_nMotionBlur", apply)
        self.assertIn("TheCamera.m_nMotionBlurAddAlpha", apply)
        self.assertIn("m_previousMotionBlur", stop)
        self.assertIn("TheCamera.RestoreWithJumpCut()", stop)
        for forbidden in (
            "CTimer::SetTimeScale",
            "SET_TIME_SCALE",
            "m_vecMoveSpeed =",
            "CServerTime",
            "g_serverTime",
        ):
            self.assertNotIn(forbidden, self.client)
        start_attempt = function_body(self.client, r"CStuntJumpSyncManager::StartAttempt\(")
        idle = function_body(self.client, r"CStuntJumpSyncManager::ProcessOnlineJump\(")
        for evidence in ("mission.IsActive()", "ReadMissionFlag()"):
            self.assertIn(evidence, start_attempt)
            self.assertIn(evidence, idle)

    def test_server_seals_one_bounded_catalog_and_rejects_redefinition(self):
        handle = function_body(self.server, r"CStuntJumpAuthorityManager::HandleDefinition\(")
        for evidence in (
            "IsCurrentHost(player)",
            "AcceptRate(player, m_definitionRates, MAX_DEFINITIONS_PER_WINDOW)",
            "packet.catalogCount != m_catalogCount",
            "packet.catalogHash != m_catalogHash",
            "slot.id != packet.id",
            "DefinitionsMatch(slot.definition, packet.definition)",
            "ComputeCatalogHash() != m_catalogHash",
            "m_catalogSealed = true",
            "BroadcastCatalog()",
        ):
            self.assertIn(evidence, handle)
        self.assertIn("MAX_DEFINITIONS_PER_WINDOW = 48", self.server_header)
        self.assertIn("std::array<Slot, Packets::Stunts::STUNT_JUMP_CAPACITY>", self.server_header)

    def test_server_requires_exact_driver_start_then_plausible_finish(self):
        handle = function_body(self.server, r"CStuntJumpAuthorityManager::HandleAttempt\(")
        for evidence in (
            "MAX_ATTEMPT_EVENTS_PER_WINDOW",
            "IsExactDriver(player, packet.vehicleId)",
            "DistanceSquared(packet.position, vehicle->m_vecPosition) > 64.0f",
            "now - vehicle->m_nLastDriverSnapshotAt > DRIVER_SNAPSHOT_MAX_AGE_MS",
            "DistanceSquared(packet.moveSpeed, vehicle->m_vecVelocity) > 0.25f",
            "slot.definition.start.Contains(vehicle->m_vecPosition, 3.0f)",
            "speedSquared < 0.16f",
            "attempt.connectId == player->m_pPeer->connectID",
            "attempt.requestId == packet.requestId",
            "elapsed < MIN_COMPLETION_TIME_MS",
            "elapsed > ATTEMPT_TIMEOUT_MS",
            "slot.definition.finish.Contains(vehicle->m_vecPosition, 4.0f)",
            "attempt.finishHitAt = now",
            "attempt.finishHitAt == 0",
            "now - attempt.finishHitAt > 5000",
            "travelDistanceSquared > maximumTravelDistance * maximumTravelDistance",
        ):
            self.assertIn(evidence, handle)
        self.assertIn("DRIVER_SNAPSHOT_MAX_AGE_MS = 1000", self.server_header)
        exact = function_body(self.server, r"CStuntJumpAuthorityManager::IsExactDriver\(")
        for evidence in (
            "m_bHasOnFootSnapshot",
            "m_bIsAlive",
            "m_nVehicleId != vehicleId",
            "m_nSeatId != 0",
            "vehicle->m_pPlayers[0] == player",
        ):
            self.assertIn(evidence, exact)

    def test_vehicle_snapshots_refresh_canonical_driver_health_weapon_and_velocity(self):
        driver = function_body(self.server_vehicles, r"ePacketType::VEHICLE_DRIVER_UPDATE")
        passenger = function_body(self.server_vehicles, r"ePacketType::VEHICLE_PASSENGER_UPDATE")
        for evidence in (
            "m_vecVelocity = pVehicleDriverUpdate->velocity",
            "m_nLastDriverSnapshotAt = enet_time_get()",
            "m_bHasOnFootSnapshot = true",
            "m_bIsAlive = pVehicleDriverUpdate->playerHealth.iHealth > 0",
            "pVehicleDriverUpdate->playerWeapon.iWeaponType",
        ):
            self.assertIn(evidence, driver)
        for evidence in (
            "m_bHasOnFootSnapshot = true",
            "m_bIsAlive = pVehiclePassengerUpdate->playerHealth.iHealth > 0",
            "pVehiclePassengerUpdate->playerWeapon.iWeaponType",
        ):
            self.assertIn(evidence, passenger)
        self.assertIn("uint32_t m_nLastDriverSnapshotAt = 0", self.server_vehicle_header)

    def test_explicit_attempt_results_bypass_revisions_and_retry_only_bounded_transient_races(self):
        result = function_body(self.client, r"CStuntJumpSyncManager::HandleAttemptResult\(")
        pending = function_body(self.client, r"CStuntJumpSyncManager::ProcessPendingAttempt\(")
        state = function_body(self.client, r"CStuntJumpSyncManager::HandleState\(")
        server = function_body(self.server, r"CStuntJumpAuthorityManager::HandleAttempt\(")
        for evidence in (
            "result.requestId != m_attemptRequestId",
            "result.clientSessionNonce != EnsureClientSessionNonce()",
            "IsRetryableResult(result.reason)",
            "m_pendingActionRetryCount < MAX_RETRIES",
            "FinishAttempt(false, false)",
        ):
            self.assertIn(evidence, result)
        for evidence in ("ACK_TIMEOUT_MS = 750", "MAX_RETRIES = 3", "SendAttempt(m_pendingAction)"):
            self.assertIn(evidence, pending)
        self.assertNotIn("m_attemptActive", state)
        self.assertIn("HandleAttemptResult(*result)", self.client_handler)
        self.assertIn("bypass state revision filtering", self.client_handler)
        for evidence in (
            "eStuntAttemptResultReason::DRIVER_SNAPSHOT_NOT_READY, 150",
            "eStuntAttemptResultReason::RATE_LIMITED, 250",
            "slot.completionRequestId == packet.requestId",
            "RejectCompetingAttempts(slot.id)",
        ):
            self.assertIn(evidence, server)

        model = AttemptHandshakeModel()
        model.state_replay(completed=True)
        self.assertTrue(model.active)
        for retry in range(3):
            model.result("START", accepted=False, retryable=True)
            self.assertTrue(model.active, retry)
        model.result("START", accepted=False, retryable=True)
        self.assertFalse(model.active)

    def test_completion_is_server_canonical_and_reward_applies_once_to_matching_session(self):
        handle = function_body(self.server, r"CStuntJumpAuthorityManager::HandleAttempt\(")
        for evidence in (
            "slot.completed = true",
            "slot.completedByPlayerId",
            "slot.collectorSessionNonce = packet.clientSessionNonce",
            "slot.awardSequence = NextAwardSequence()",
            "completesCatalog ? 10000 : slot.definition.reward",
            "slot.revision = NextRevision()",
            "SendState(slot)",
        ):
            self.assertIn(evidence, handle)
        award = function_body(self.client, r"CStuntJumpSyncManager::ApplyAwardOnce\(")
        for evidence in (
            "state.completedByPlayerId != CNetworkPlayerManager::m_nMyId",
            "state.collectorSessionNonce != EnsureClientSessionNonce()",
            "m_appliedAwardSequences[state.id.slot] == state.awardSequence",
            "m_nMoney += state.rewardAmount",
            "STAT_UNIQUE_JUMPS_DONE",
        ):
            self.assertIn(evidence, award)
        self.assertIn("tried to publish server-authoritative stunt state", self.server_handler)

    def test_late_join_reconnect_and_host_migration_preserve_canonical_state(self):
        snapshot = function_body(self.server, r"CStuntJumpAuthorityManager::SendSnapshot\(")
        self.assertIn("m_catalogSealed", snapshot)
        self.assertIn("SendState(slot, player)", snapshot)
        migrate = function_body(self.server, r"CStuntJumpAuthorityManager::HandleAuthorityChange\(")
        self.assertIn("RejectActiveAttempt(attempt", migrate)
        self.assertIn("slot.revision = NextRevision()", migrate)
        self.assertIn("BroadcastCatalog()", migrate)
        reset = function_body(self.client, r"void CStuntJumpSyncManager::ResetNetworkState\(")
        self.assertIn("RestoreNativeBaseline()", reset)
        self.assertNotIn("m_appliedAwardSequences = {}", reset)
        self.assertIn("m_appliedAwardSequences = {}", function_body(self.client, r"BeginServerRun\("))
        self.assertIn("CStuntJumpAuthorityManager::SendSnapshot(pNewNetworkPlayer);", self.server_network)
        self.assertIn("CStuntJumpAuthorityManager::HandleAuthorityChange(player);", self.server_players)
        self.assertIn("CStuntJumpSyncManager::ResetNetworkState();", self.client_network)
        self.assertEqual(self.client_system.count("CStuntJumpSyncManager::HandleAuthorityChanged();"), 2)

    def test_cancel_death_disconnect_and_mission_transition_restore_presentation(self):
        process = function_body(self.client, r"CStuntJumpSyncManager::ProcessOnlineJump\(")
        for evidence in (
            "!player->IsAlive()",
            "vehicle->GetStatus() == STATUS_WRECKED",
            "vehicle->vehicleFlags.bIsDrowning",
            "vehicle->physicalFlags.bSubmergedInWater",
            "MissionContextChanged()",
            "elapsed > 20000",
            "FinishAttempt(false, true)",
        ):
            self.assertIn(evidence, process)
        self.assertIn(
            "FinishAttempt(false, false)",
            function_body(self.client, r"void CStuntJumpSyncManager::ResetNetworkState\("),
        )
        self.assertIn(
            "FinishAttempt(false, CNetwork::m_bAuthenticated)",
            function_body(self.client, r"CStuntJumpSyncManager::HandleAuthorityChanged\("),
        )
        self.assertIn(
            "CStuntJumpAuthorityManager::HandlePlayerDisconnected(pNetworkPlayer);",
            self.server_network,
        )
        self.assertIn("ATTEMPT_TIMEOUT_MS = 20000", self.server_header)

    def test_catalog_publication_and_attempt_rates_are_strict_and_bounded(self):
        publish = function_body(self.client, r"CStuntJumpSyncManager::PublishCatalog\(")
        self.assertIn("sent < 2", publish)
        self.assertIn("m_nextPublishAt = now + 50", publish)
        self.assertIn("m_nextCatalogRefreshAt = now + 5000", publish)
        rate = function_body(self.server, r"CStuntJumpAuthorityManager::AcceptRate\(")
        for evidence in (
            "rate.owner != player",
            "rate.connectId != connectId",
            "now - rate.windowStartedAt >= RATE_WINDOW_MS",
            "rate.eventCount >= maximumEvents",
            "++rate.eventCount",
        ):
            self.assertIn(evidence, rate)
        self.assertIn("MAX_ATTEMPT_EVENTS_PER_WINDOW = 8", self.server_header)

    def test_executable_authority_model_rejects_spoofed_and_duplicate_completion(self):
        model = StuntAuthorityModel()
        self.assertFalse(model.start(1, 0, 40, 1.0))
        self.assertFalse(model.start(1, 0, 5, 0.1))
        self.assertTrue(model.start(1, 1000, 5, 0.5))
        self.assertFalse(model.complete(1, 1050, 100))
        self.assertTrue(model.start(1, 2000, 5, 0.5))
        self.assertFalse(model.complete(1, 2100, 500))
        self.assertTrue(model.start(1, 3000, 5, 0.5))
        self.assertTrue(model.complete(1, 3500, 100))
        self.assertFalse(model.complete(2, 3600, 100))
        self.assertEqual(model.awards, [1])


if __name__ == "__main__":
    unittest.main()
