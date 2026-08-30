import math
import pathlib
import re
import unittest


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


def class_body(source: str, name: str) -> str:
    return function_body(source, rf"class\s+{name}\b[^{{]*")


def bits_for_values(value_count: int) -> int:
    return math.ceil(math.log2(value_count))


class TrailerAuthorityModel:
    TRAILER_MODELS = {435, 450, 584, 591, 606, 607, 608, 610, 611}
    TOW_MODELS = {403, 485, 514, 515, 531, 552, 583, 591, 606, 607}

    def __init__(self):
        self.models = {}
        self.owners = {}
        self.links = {}
        self.pending = {}

    def spawn(self, vehicle_id, model, owner="host"):
        self.models[vehicle_id] = model
        self.owners[vehicle_id] = owner
        self.links.setdefault(vehicle_id, -1)
        pending = list(self.pending.items())
        for tractor, (trailer, deadline) in pending:
            self.publish(tractor, trailer, deadline - 1, self.owners[tractor])

    def remove(self, vehicle_id):
        self.models.pop(vehicle_id, None)
        self.owners.pop(vehicle_id, None)
        self.links.pop(vehicle_id, None)
        self.pending.pop(vehicle_id, None)
        for tractor, trailer in list(self.links.items()):
            if trailer == vehicle_id:
                self.links[tractor] = -1
        for tractor, (trailer, _) in list(self.pending.items()):
            if trailer == vehicle_id:
                self.pending.pop(tractor)

    def publish(self, tractor, trailer, now, sender="host"):
        if trailer == -1:
            self.links[tractor] = -1
            self.pending.pop(tractor, None)
            return True
        if self.models.get(tractor) not in self.TOW_MODELS or trailer == tractor:
            return False
        if trailer not in self.models:
            self.links[tractor] = -1
            self.pending.setdefault(tractor, (trailer, now + 5000))
            if now >= self.pending[tractor][1]:
                self.pending.pop(tractor)
            return False
        if self.models[trailer] not in self.TRAILER_MODELS:
            return False
        compatible = {
            403: {435, 450, 584, 591},
            514: {435, 450, 584, 591},
            515: {435, 450, 584, 591},
            591: {435, 450, 584, 591},
            531: {610},
            552: {611},
            485: {606, 607, 608},
            583: {606, 607, 608},
            606: {606, 607, 608},
            607: {606, 607, 608},
        }
        if self.models[trailer] not in compatible[self.models[tractor]]:
            return False
        if self.owners[trailer] != sender:
            return False
        if any(owner != tractor and owned == trailer for owner, owned in self.links.items()):
            return False
        cursor = trailer
        for _ in range(255):
            if cursor == tractor:
                return False
            cursor = self.links.get(cursor, -1)
            if cursor == -1:
                break
        self.links[tractor] = trailer
        self.pending.pop(tractor, None)
        return True


class VehicleAuxStateSyncTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.packets = (ROOT / "shared/network/packets/vehicles.h").read_text(encoding="utf-8")
        cls.config = (ROOT / "shared/config.h").read_text(encoding="utf-8")
        cls.client_vehicle_h = (ROOT / "client/src/CNetworkVehicle.h").read_text(encoding="utf-8")
        cls.client_vehicle = (ROOT / "client/src/CNetworkVehicle.cpp").read_text(encoding="utf-8")
        cls.client_manager_h = (ROOT / "client/src/CNetworkVehicleManager.h").read_text(encoding="utf-8")
        cls.client_manager = (ROOT / "client/src/CNetworkVehicleManager.cpp").read_text(encoding="utf-8")
        cls.client_handler = (ROOT / "client/src/PacketHandlers/vehicles.cpp").read_text(encoding="utf-8")
        cls.server_vehicle_h = (ROOT / "server/src/CNetworkVehicle.h").read_text(encoding="utf-8")
        cls.server_manager_h = (ROOT / "server/src/CNetworkVehicleManager.h").read_text(encoding="utf-8")
        cls.server_manager = (ROOT / "server/src/CNetworkVehicleManager.cpp").read_text(encoding="utf-8")
        cls.server_handler = (ROOT / "server/src/PacketHandlers/vehicles.cpp").read_text(encoding="utf-8")

    def test_existing_sync_packets_carry_one_bounded_aux_snapshot(self):
        aux = function_body(self.packets, r"struct\s+VehicleAuxState\b")
        for evidence in (
            "serialize_bool(stream, hydraulicsActive)",
            "serialize_int(stream, hydraulicControlAngle, 0, VEHICLE_HYDRAULIC_ANGLE_MAX)",
            "serialize_compressed_float(stream, wheel, 0.0f, 1.0f, 0.01f)",
            "serialize_int(stream, trailerId, VEHICLE_TRAILER_NONE, Config::MAX_SERVER_VEHICLES - 1)",
            "serialize_int(stream, radioStation, 0, VEHICLE_RADIO_OFF - 1)",
            "serialize_int(stream, radioTrackId, VEHICLE_RADIO_TRACK_NONE, VEHICLE_RADIO_TRACK_MAX)",
            "serialize_int(stream, radioPlayTimeMs, 0, VEHICLE_RADIO_PLAY_TIME_MAX_MS)",
        ):
            self.assertIn(evidence, aux)

        idle = class_body(self.packets, "VehicleIdleUpdate")
        driver = class_body(self.packets, "VehicleDriverUpdate")
        passenger = class_body(self.packets, "VehiclePassengerUpdate")
        self.assertIn("VehicleAuxState auxState", idle)
        self.assertIn("VehicleAuxState auxState", driver)
        self.assertNotIn("VehicleAuxState auxState", passenger)
        self.assertIn("ePacketChannel::SYNC", idle)
        self.assertIn("ePacketChannel::SYNC", driver)
        self.assertNotIn("DEFINE_PACKET_TYPE(VehicleAuxState", self.packets)

        max_vehicles = int(re.search(r"MAX_SERVER_VEHICLES\s*=\s*(\d+)", self.config).group(1))
        maximum_bits = (
            1
            + bits_for_values(505)
            + 4 * bits_for_values(101)
            + bits_for_values(max_vehicles + 1)
            + 1
            + bits_for_values(13)
            + bits_for_values(4097)
            + bits_for_values(1_800_001)
        )
        self.assertEqual(maximum_bits, 85)
        self.assertLessEqual(math.ceil(maximum_bits / 8), 12)

    def test_model_allowlists_are_explicit_and_hydraulics_are_runtime_gated(self):
        hydraulic_allowlist = function_body(self.packets, r"IsHydraulicSyncModel\s*\(")
        for model in (
            "MODEL_VOODOO",
            "MODEL_REMINGTN",
            "MODEL_SLAMVAN",
            "MODEL_BLADE",
            "MODEL_TAHOMA",
            "MODEL_SAVANNA",
            "MODEL_BROADWAY",
            "MODEL_TORNADO",
        ):
            self.assertIn(model, hydraulic_allowlist)
        self.assertEqual(hydraulic_allowlist.count("return true"), 1)

        capture = function_body(self.client_manager, r"CNetworkVehicleManager::CaptureAuxState\s*\(")
        apply = function_body(self.client_vehicle, r"CNetworkVehicle::ApplyAuxState\s*\(")
        for body in (capture, apply):
            gate = body.index("IsHydraulicSyncModel")
            native = body.index("wheelsDistancesToGround1")
            self.assertLess(gate, native)
            self.assertIn("VEHICLE_AUTOMOBILE", body[gate:native])
            self.assertIn("bHydraulicInst", body[gate:native])
        self.assertIn("state.hydraulicsActive", apply[: apply.index("wheelsDistancesToGround1")])
        self.assertIn("!Packets::Vehicles::IsHydraulicSyncModel", self.server_handler)

    def test_server_enforces_driver_syncer_and_passenger_authority(self):
        exact = function_body(self.server_handler, r"IsExactOccupant\s*\(")
        for evidence in ("m_pPlayers[seat] == player", "player->m_nVehicleId", "player->m_nSeatId"):
            self.assertIn(evidence, exact)

        driver = function_body(self.server_handler, r"VEHICLE_DRIVER_UPDATE\s*,")
        self.assertLess(driver.index("IsExactOccupant"), driver.index("SanitizeAuxState"))
        self.assertLess(driver.index("SanitizeAuxState"), driver.index("SendToAll"))
        self.assertNotIn("SetOccupant(0, pNetworkPlayer)", driver)

        idle = function_body(self.server_handler, r"VEHICLE_IDLE_UPDATE\s*,")
        self.assertIn("vehicle->m_pSyncer == pNetworkPlayer", idle)
        self.assertIn("vehicle->m_pPlayers[0] == nullptr", idle)
        self.assertIn("senderIsPassenger", idle)
        self.assertIn(
            "SanitizeAuxState(vehicle, pNetworkPlayer, pVehicleIdleUpdate->auxState, !senderIsPassenger, false)", idle
        )

        sanitize = function_body(self.server_handler, r"SanitizeAuxState\s*\(")
        cached = sanitize.index("state = vehicle->m_auxState")
        trailer_validation = sanitize.index("SanitizeTrailerState")
        radio_off = sanitize.index("SetRadioOff(state)")
        self.assertLess(cached, trailer_validation)
        self.assertLess(trailer_validation, radio_off)
        self.assertIn("if (!allowRadioAuthority)", sanitize)
        self.assertIn(
            "SanitizeAuxState(pNetworkVehicle, pNetworkPlayer, pVehicleDriverUpdate->auxState, true, true)",
            driver,
        )

        passenger = function_body(self.server_handler, r"VEHICLE_PASSENGER_UPDATE\s*,")
        self.assertLess(passenger.index("IsExactOccupant"), passenger.index("SendToAll"))
        self.assertNotIn("SetOccupant", passenger)
        self.assertNotIn("auxState", class_body(self.packets, "VehiclePassengerUpdate"))

    def test_server_canonicalizes_live_trailer_ids_models_cycles_and_duplicates(self):
        sanitize = function_body(self.server_handler, r"SanitizeTrailerState\s*\(")
        for evidence in (
            "requestedTrailerId == vehicle->m_nVehicleId",
            "TrailerAlreadyOwnedByAnotherTractor",
            "WouldCreateTrailerCycle",
            "CNetworkVehicleManager::GetVehicle(requestedTrailerId)",
            "IsTrailerModel(trailer->m_nModelId)",
            "CanAttachTrailerModel(vehicle->m_nModelId, trailer->m_nModelId)",
            "trailer->m_pSyncer != sender",
            "vehicle->m_auxState.trailerId = requestedTrailerId",
        ):
            self.assertIn(evidence, sanitize)
        self.assertLess(sanitize.index("GetVehicle(requestedTrailerId)"), sanitize.index("m_auxState.trailerId = requestedTrailerId"))
        self.assertLess(sanitize.index("trailer->m_pSyncer != sender"), sanitize.index("m_auxState.trailerId = requestedTrailerId"))
        self.assertIn("Config::MAX_SERVER_VEHICLES", function_body(self.server_handler, r"WouldCreateTrailerCycle\s*\("))

        model = TrailerAuthorityModel()
        model.spawn(1, 403)
        model.spawn(2, 435)
        model.spawn(3, 514)
        self.assertFalse(model.publish(1, 1, 0))
        self.assertTrue(model.publish(1, 2, 0))
        self.assertFalse(model.publish(3, 2, 0))
        model.links[2] = 1
        model.links[1] = -1
        self.assertFalse(model.publish(1, 2, 0))
        model.spawn(4, 400)
        self.assertFalse(model.publish(1, 4, 0))
        model.spawn(5, 435, owner="other-syncer")
        self.assertFalse(model.publish(1, 5, 0, sender="host"))
        model.spawn(6, 525)
        model.spawn(7, 435)
        self.assertFalse(model.publish(6, 7, 0))

    def test_only_validated_driver_can_publish_radio(self):
        idle = function_body(self.server_handler, r"VEHICLE_IDLE_UPDATE\s*,")
        driver = function_body(self.server_handler, r"VEHICLE_DRIVER_UPDATE\s*,")
        passenger = function_body(self.server_handler, r"VEHICLE_PASSENGER_UPDATE\s*,")
        sanitize = function_body(self.server_handler, r"SanitizeAuxState\s*\(")
        radio_off = function_body(self.server_handler, r"SetRadioOff\s*\(")

        self.assertIn("!senderIsPassenger, false", idle)
        self.assertNotIn("true, true", idle)
        self.assertLess(driver.index("IsExactOccupant"), driver.index("true, true"))
        self.assertNotIn("radio", passenger.lower())
        self.assertIn("if (!allowRadioAuthority)", sanitize)
        self.assertIn("SetRadioOff(state)", sanitize)
        for evidence in (
            "state.radioActive = false",
            "state.radioStation = Packets::Vehicles::VEHICLE_RADIO_OFF",
            "state.radioTrackId = Packets::Vehicles::VEHICLE_RADIO_TRACK_NONE",
            "state.radioPlayTimeMs = 0",
        ):
            self.assertIn(evidence, radio_off)

    def test_attach_before_and_after_stream_order_timeout_and_removal_cleanup(self):
        model = TrailerAuthorityModel()
        model.spawn(10, 403)
        self.assertFalse(model.publish(10, 11, 100))
        self.assertEqual(model.pending[10][0], 11)
        model.spawn(11, 435)
        self.assertEqual(model.links[10], 11)

        second = TrailerAuthorityModel()
        second.spawn(21, 435)
        second.spawn(20, 403)
        self.assertTrue(second.publish(20, 21, 0))
        second.remove(21)
        self.assertEqual(second.links[20], -1)
        second.spawn(21, 435)
        self.assertEqual(second.links[20], -1)
        self.assertTrue(second.publish(20, 21, 1))

        timed = TrailerAuthorityModel()
        timed.spawn(30, 403)
        self.assertFalse(timed.publish(30, 31, 0))
        self.assertFalse(timed.publish(30, 31, 5000))
        self.assertNotIn(30, timed.pending)

        server_pending = function_body(self.server_handler, r"SanitizeTrailerState\s*\(")
        self.assertIn("TRAILER_PENDING_TIMEOUT_MS", server_pending)
        self.assertIn("m_nPendingTrailerSinceMs", server_pending)
        client_pending = function_body(self.client_vehicle, r"CNetworkVehicle::ResolvePendingTrailer\s*\(")
        self.assertIn("TRAILER_STREAM_TIMEOUT_MS", client_pending)
        self.assertIn("CNetworkVehicleManager::GetVehicle(m_nPendingTrailerId)", client_pending)
        self.assertIn("SetTowLink(m_pVehicle, false)", client_pending)
        self.assertIn("ResolvePendingVehicleState();", function_body(self.client_manager, r"CNetworkVehicleManager::Add\s*\("))

        clear_server = function_body(self.server_manager, r"CNetworkVehicleManager::ClearVehicleRelations\s*\(")
        self.assertIn("m_auxState.trailerId == vehicleid", clear_server)
        self.assertIn("m_nPendingTrailerId == vehicleid", clear_server)
        self.assertIn("ClearVehicleRelations((*it)->m_nVehicleId)", self.server_manager)
        self.assertIn("DetachTrailerLinks();", function_body(self.client_vehicle, r"CNetworkVehicle::~CNetworkVehicle\s*\("))
        remove_client = function_body(self.client_manager, r"CNetworkVehicleManager::ClearVehicleRelations\s*\(")
        self.assertIn("other->m_nPendingTrailerId == vehicle->m_nVehicleId", remove_client)
        self.assertIn("other->m_nAppliedTrailerId == vehicle->m_nVehicleId", remove_client)
        self.assertIn("other->m_lastAuxState.trailerId = Packets::Vehicles::VEHICLE_TRAILER_NONE", remove_client)

    def test_radio_off_user_tracks_drift_and_audio_unavailable_paths(self):
        capture = function_body(self.client_manager, r"CNetworkVehicleManager::CaptureAuxState\s*\(")
        self.assertIn("localPlayer->m_pVehicle == vehicle", capture)
        self.assertIn("AudioEngine.IsVehicleRadioActive()", capture)
        self.assertIn("AudioEngine.IsRadioOn()", capture)
        self.assertIn("GetRadioTrackPlayTime()", capture)

        apply = function_body(self.client_vehicle, r"CNetworkVehicle::ApplyAuxState\s*\(")
        self.assertIn("const bool localPassenger", apply)
        self.assertIn("AudioEngine.IsVehicleRadioActive()", apply)
        self.assertIn("!state.radioActive", apply)
        self.assertIn("AudioEngine.RetuneRadio", apply)
        self.assertIn("state.radioStation != RADIO_USER_TRACKS_ID", apply)
        self.assertIn("RADIO_DRIFT_TOLERANCE_MS", apply)
        self.assertIn("RADIO_CORRECTION_INTERVAL_MS", apply)
        self.assertIn("ForceRadioTrack", apply)
        self.assertNotIn("AudioEngine", function_body(self.client_vehicle, r"CNetworkVehicle::DetachTrailerLinks\s*\("))

        def needs_correction(local_track, remote_track, local_time, remote_time):
            return local_track != remote_track or abs(local_time - remote_time) > 2000

        self.assertFalse(needs_correction(42, 42, 10_000, 11_999))
        self.assertTrue(needs_correction(42, 42, 10_000, 12_001))
        self.assertTrue(needs_correction(41, 42, 10_000, 10_000))

    def test_cleanup_and_compile_hygiene_are_declared_symmetrically(self):
        for declaration, definition in (
            ("ApplyAuxState", "CNetworkVehicle::ApplyAuxState"),
            ("ResolvePendingTrailer", "CNetworkVehicle::ResolvePendingTrailer"),
            ("DetachTrailerLinks", "CNetworkVehicle::DetachTrailerLinks"),
        ):
            self.assertIn(declaration, self.client_vehicle_h)
            self.assertIn(definition, self.client_vehicle)
        for declaration, definition in (
            ("CaptureAuxState", "CNetworkVehicleManager::CaptureAuxState"),
            ("ResolvePendingVehicleState", "CNetworkVehicleManager::ResolvePendingVehicleState"),
            ("ClearVehicleRelations", "CNetworkVehicleManager::ClearVehicleRelations"),
        ):
            self.assertIn(declaration, self.client_manager_h)
            self.assertIn(definition, self.client_manager)
        self.assertIn("VehicleAuxState m_auxState", self.server_vehicle_h)
        self.assertIn("ClearVehicleRelations", self.server_manager_h)
        self.assertIn("ClearVehicleRelations", self.server_manager)
        self.assertIn("ApplyAuxState(pVehicleIdleUpdate->auxState)", self.client_handler)
        self.assertIn("ApplyAuxState(pVehicleDriverUpdate->auxState)", self.client_handler)


if __name__ == "__main__":
    unittest.main()
