import math
import pathlib
import unittest
from dataclasses import dataclass


ROOT = pathlib.Path(__file__).resolve().parents[2]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def time_delta(candidate: int, reference: int) -> int:
    value = (candidate - reference) & 0xFFFFFFFF
    return value - 0x100000000 if value & 0x80000000 else value


def shortest_angle(from_angle: float, to_angle: float, alpha: float) -> float:
    def normalize(value: float) -> float:
        return (value + math.pi) % (2.0 * math.pi) - math.pi

    return normalize(from_angle + normalize(to_angle - from_angle) * alpha)


@dataclass
class Snapshot:
    server_time: int
    position: float
    angle: float = 0.0
    source: int = 1
    area: int = 0


class TransformBufferModel:
    MAX_SNAPSHOTS = 12
    MIN_RENDER_DELAY_MS = 100
    MAX_RENDER_DELAY_MS = 200
    MAX_PACKET_GAP_MS = 1000

    def __init__(self) -> None:
        self.snapshots: list[Snapshot] = []
        self.last_time: int | None = None
        self.rejected_stale = 0

    @classmethod
    def render_delay(cls, local_rtt: int, source_rtt: int) -> int:
        allowance = min((min(local_rtt, 1000) + min(source_rtt, 1000)) // 4, 100)
        return max(cls.MIN_RENDER_DELAY_MS, min(cls.MIN_RENDER_DELAY_MS + allowance,
                                                cls.MAX_RENDER_DELAY_MS))

    def reset(self) -> None:
        self.snapshots.clear()
        self.last_time = None

    def reset_at(self, boundary_time: int) -> bool:
        if self.last_time is not None and time_delta(boundary_time, self.last_time) < 0:
            self.rejected_stale += 1
            return False
        self.snapshots.clear()
        self.last_time = boundary_time
        return True

    def push(self, snapshot: Snapshot, teleport_distance: float = 30.0) -> bool:
        accepted_delta = time_delta(snapshot.server_time, self.last_time) if self.last_time is not None else 1
        if self.last_time is not None and (accepted_delta < 0 or
                                           (accepted_delta == 0 and self.snapshots)):
            self.rejected_stale += 1
            return False
        if self.snapshots:
            previous = self.snapshots[-1]
            boundary = (
                previous.source != snapshot.source
                or previous.area != snapshot.area
                or time_delta(snapshot.server_time, previous.server_time) > self.MAX_PACKET_GAP_MS
                or abs(previous.position - snapshot.position) > teleport_distance
            )
            if boundary:
                self.snapshots.clear()
        self.snapshots.append(snapshot)
        self.snapshots = self.snapshots[-self.MAX_SNAPSHOTS:]
        self.last_time = snapshot.server_time
        return True

    def sample(self, server_now: int, local_rtt: int = 0, source_rtt: int = 0) -> tuple[float, float]:
        render_time = (server_now - self.render_delay(local_rtt, source_rtt)) & 0xFFFFFFFF
        while len(self.snapshots) > 2 and time_delta(render_time, self.snapshots[1].server_time) >= 0:
            self.snapshots.pop(0)
        if len(self.snapshots) == 1 or time_delta(render_time, self.snapshots[0].server_time) <= 0:
            current = self.snapshots[0]
            return current.position, current.angle
        if time_delta(render_time, self.snapshots[-1].server_time) >= 0:
            current = self.snapshots[-1]
            return current.position, current.angle
        before, after = self.snapshots[0], self.snapshots[1]
        span = max(1, time_delta(after.server_time, before.server_time))
        alpha = max(0.0, min(time_delta(render_time, before.server_time) / span, 1.0))
        return (
            before.position + (after.position - before.position) * alpha,
            shortest_angle(before.angle, after.angle, alpha),
        )


class SmoothInterpolationModelTests(unittest.TestCase):
    def test_buffer_is_bounded_and_rejects_stale_packets(self) -> None:
        buffer = TransformBufferModel()
        for index in range(20):
            self.assertTrue(buffer.push(Snapshot(1000 + index * 50, float(index))))
        self.assertEqual(12, len(buffer.snapshots))
        self.assertFalse(buffer.push(Snapshot(1500, -1.0)))
        self.assertEqual(1, buffer.rejected_stale)

    def test_server_time_and_rtt_select_a_bracketed_midpoint(self) -> None:
        buffer = TransformBufferModel()
        buffer.push(Snapshot(1000, 0.0))
        buffer.push(Snapshot(1100, 10.0))
        # 150 ms adaptive delay: render time 1050, exactly between the snapshots.
        position, _ = buffer.sample(1200, local_rtt=100, source_rtt=100)
        self.assertAlmostEqual(5.0, position)
        self.assertEqual(100, buffer.render_delay(0, 0))
        self.assertEqual(200, buffer.render_delay(1000, 1000))

    def test_rotation_uses_the_shortest_path_across_pi(self) -> None:
        buffer = TransformBufferModel()
        buffer.push(Snapshot(1000, 0.0, math.radians(179.0)))
        buffer.push(Snapshot(1100, 0.0, math.radians(-179.0)))
        _, angle = buffer.sample(1150)
        self.assertAlmostEqual(math.pi, abs(angle), places=5)

    def test_teleport_area_source_and_long_gap_are_snap_boundaries(self) -> None:
        cases = [
            Snapshot(1100, 100.0),
            Snapshot(1100, 1.0, source=2),
            Snapshot(1100, 1.0, area=1),
            Snapshot(2501, 1.0),
        ]
        for boundary in cases:
            with self.subTest(boundary=boundary):
                buffer = TransformBufferModel()
                buffer.push(Snapshot(1000, 0.0))
                self.assertTrue(buffer.push(boundary))
                self.assertEqual([boundary], buffer.snapshots)

    def test_newest_snapshot_is_held_without_indefinite_extrapolation(self) -> None:
        buffer = TransformBufferModel()
        buffer.push(Snapshot(1000, 3.0))
        buffer.push(Snapshot(1100, 7.0))
        self.assertEqual((7.0, 0.0), buffer.sample(10_000))

    def test_uint32_server_time_wrap_is_newer(self) -> None:
        buffer = TransformBufferModel()
        self.assertTrue(buffer.push(Snapshot(0xFFFFFFF0, 1.0)))
        self.assertTrue(buffer.push(Snapshot(0x00000020, 2.0)))

    def test_stale_lifecycle_boundary_cannot_roll_back_the_watermark(self) -> None:
        buffer = TransformBufferModel()
        buffer.push(Snapshot(1200, 3.0))
        self.assertFalse(buffer.reset_at(1100))
        self.assertEqual(1200, buffer.last_time)
        self.assertTrue(buffer.reset_at(1300))
        self.assertEqual([], buffer.snapshots)
        self.assertTrue(buffer.push(Snapshot(1300, 4.0)))


class SmoothInterpolationSourceContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.interpolator_h = read("client/src/CNetworkTransformInterpolator.h")
        cls.interpolator_cpp = read("client/src/CNetworkTransformInterpolator.cpp")
        cls.player_cpp = read("client/src/CNetworkPlayer.cpp")
        cls.ped_cpp = read("client/src/CNetworkPed.cpp")
        cls.vehicle_cpp = read("client/src/CNetworkVehicle.cpp")
        cls.vehicle_manager_cpp = read("client/src/CNetworkVehicleManager.cpp")
        cls.player_handler = read("client/src/PacketHandlers/players.cpp")
        cls.ped_handler = read("client/src/PacketHandlers/peds.cpp")
        cls.vehicle_handler = read("client/src/PacketHandlers/vehicles.cpp")
        cls.enex = read("client/src/CEntryExitTransitionSync.cpp")

    def test_bounded_server_time_rtt_buffer_and_stale_hold_exist(self) -> None:
        self.assertIn("MAX_SNAPSHOTS = 12", self.interpolator_h)
        self.assertIn("MIN_RENDER_DELAY_MS", self.interpolator_h)
        self.assertIn("MAX_RENDER_DELAY_MS = 200", self.interpolator_h)
        self.assertIn("CNetwork::GetRTT()", self.interpolator_cpp)
        self.assertIn("m_rejectedStaleCount", self.interpolator_cpp)
        self.assertIn("Holding the newest authoritative transform", self.interpolator_cpp)
        self.assertNotIn("extrapolat", self.interpolator_cpp.lower().replace("unbounded dead reckoning", ""))

    def test_shortest_path_angles_and_vehicle_orientation_are_explicit(self) -> None:
        self.assertIn("LerpAngleShortest", self.interpolator_cpp)
        self.assertIn("SlerpShortest", self.interpolator_cpp)
        self.assertIn("if (dot < 0.0f)", self.interpolator_cpp)
        self.assertIn("m_pVehicle->m_matrix->at = sample.up", self.vehicle_cpp)

    def test_remote_entity_classes_sample_every_presentation_frame(self) -> None:
        self.assertIn("m_transformInterpolator.Sample(g_serverTime, m_nRTT, sample)", self.player_cpp)
        self.assertIn("m_transformInterpolator.Sample(g_serverTime, 0, sample)", self.ped_cpp)
        self.assertIn("m_transformInterpolator.Sample(g_serverTime, sourceRtt, sample)", self.vehicle_cpp)
        self.assertIn("m_iPlayerId == CNetworkPlayerManager::m_nMyId", self.player_cpp)
        self.assertIn("m_bSyncing", self.ped_cpp)
        self.assertIn("m_bSyncing", self.vehicle_cpp)

    def test_packet_handlers_do_not_apply_authoritative_transform_steps_directly(self) -> None:
        player_onfoot = self.player_handler.split("PACKET_HANDLER(ePacketType::PLAYER_ONFOOT_UPDATE", 1)[1]
        player_onfoot = player_onfoot.split("PACKET_HANDLER(ePacketType::PLAYER_KEY_SYNC", 1)[0]
        self.assertNotIn("SetPosn(pOnFootUpdate", player_onfoot)
        self.assertNotIn("m_fCurrentRotation = pOnFootUpdate", player_onfoot)

        ped_onfoot = self.ped_handler.split("PACKET_HANDLER(ePacketType::PED_ONFOOT", 1)[1]
        ped_onfoot = ped_onfoot.split("PACKET_HANDLER(ePacketType::PED_DRIVER_UPDATE", 1)[0]
        self.assertNotIn("SetPosn(pPedOnFoot", ped_onfoot)

        vehicle_idle = self.vehicle_handler.split("PACKET_HANDLER(ePacketType::VEHICLE_IDLE_UPDATE", 1)[1]
        vehicle_idle = vehicle_idle.split("PACKET_HANDLER(ePacketType::VEHICLE_DRIVER_UPDATE", 1)[0]
        self.assertNotIn("m_matrix->pos = pVehicleIdleUpdate", vehicle_idle)

        vehicle_driver = self.vehicle_handler.split("PACKET_HANDLER(ePacketType::VEHICLE_DRIVER_UPDATE", 1)[1]
        vehicle_driver = vehicle_driver.split("PACKET_HANDLER(ePacketType::VEHICLE_ENTER", 1)[0]
        self.assertNotIn("m_matrix->pos = pVehicleDriverUpdate", vehicle_driver)

    def test_vehicle_is_the_only_driver_transform_owner(self) -> None:
        ped_apply = self.ped_cpp.split("void CNetworkPed::ApplyCachedPresentation", 1)[1]
        ped_apply = ped_apply.split("void CNetworkPed::ProcessPendingPresentation", 1)[0]
        self.assertNotIn("vehicle->m_matrix", ped_apply)
        self.assertIn("ProcessTransformInterpolation();", self.vehicle_cpp)

    def test_all_required_snap_reset_boundaries_are_wired(self) -> None:
        self.assertIn("teleported", self.interpolator_cpp)
        self.assertIn("areaChanged", self.interpolator_cpp)
        self.assertIn("sourceChanged", self.interpolator_cpp)
        self.assertIn("resumedAfterStalePresentation", self.interpolator_cpp)
        self.assertIn("m_transformInterpolator.ClearSnapshots();\n    ApplyCachedPresentation();", self.player_cpp)
        self.assertIn("m_transformInterpolator.ClearSnapshots();\n    ApplyCachedPresentation();", self.ped_cpp)
        self.assertIn("m_transformInterpolator.ClearSnapshots();\n    ApplyCachedPresentation();", self.vehicle_cpp)
        self.assertIn("void CNetworkPlayer::Respawn(server_time_t boundaryTime)\n{\n"
                      "    if (!ResetTransformInterpolation(boundaryTime))", self.player_cpp)
        self.assertIn("SnapOnFootTransform(\n            packet.vecPos", self.player_cpp)
        self.assertIn("!pNetworkPlayer->ResetTransformInterpolation(packet.serverTime)", self.enex)
        self.assertIn("pNetworkVehicle->ResetTransformInterpolation(pVehicleEnter->serverTime)",
                      self.vehicle_handler)
        self.assertIn("pNetworkPed->ResetTransformInterpolation(pAssignPedSyncer->serverTime)",
                      self.ped_handler)
        self.assertIn("TimeDelta(boundaryTime, m_lastAcceptedServerTime) < 0", self.interpolator_cpp)
        self.assertIn("ClearVehicleRelations(vehicle, boundaryTime)", self.vehicle_manager_cpp)
        self.assertIn("player->ResetTransformInterpolation(boundaryTime)", self.vehicle_manager_cpp)


if __name__ == "__main__":
    unittest.main()
