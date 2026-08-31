import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


def function_body(source: str, signature: str) -> str:
    match = re.search(signature, source)
    if match is None:
        raise AssertionError(f"missing function matching {signature}")
    opening = source.find("{", match.end())
    if opening < 0:
        raise AssertionError(f"missing function body matching {signature}")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : index]
    raise AssertionError(f"unterminated function matching {signature}")


class CameraBaselineModel:
    KEYFRAME_MS = 1000

    def __init__(self):
        self.sent_initial = False
        self.last_full = 0
        self.has_baseline = False
        self.old_time = None
        self.current_time = None

    def sender_tick(self, now: int) -> bool:
        full = not self.sent_initial or ((now - self.last_full) & 0xFFFFFFFF) >= self.KEYFRAME_MS
        if full:
            self.sent_initial = True
            self.last_full = now
        return full

    def receive(self, server_time: int, full: bool):
        if not self.has_baseline:
            if not full:
                return
            self.old_time = self.current_time = server_time
            self.has_baseline = True
            return
        self.old_time = self.current_time
        self.current_time = server_time

    def reconnect(self):
        self.has_baseline = False
        self.old_time = self.current_time = None


class CameraJoinContextTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.aim = (ROOT / "client/src/CAimSync.cpp").read_text(encoding="utf-8")
        cls.aim_header = (ROOT / "client/src/CAimSync.h").read_text(encoding="utf-8")
        cls.player = (ROOT / "client/src/CNetworkPlayer.h").read_text(encoding="utf-8")
        cls.handler = (ROOT / "client/src/PacketHandlers/players.cpp").read_text(encoding="utf-8")
        cls.network = (ROOT / "client/src/CNetwork.cpp").read_text(encoding="utf-8")
        cls.keys = (ROOT / "client/src/CKeySync.cpp").read_text(encoding="utf-8")
        cls.player_hooks = (ROOT / "client/src/Hooks/PlayerHooks.cpp").read_text(encoding="utf-8")
        cls.vehicle_hooks = (ROOT / "client/src/Hooks/VehicleHooks.cpp").read_text(encoding="utf-8")
        cls.task_hooks = (ROOT / "client/src/Hooks/TaskHooks.cpp").read_text(encoding="utf-8")

    def test_camera_application_uses_the_explicit_validated_ped(self):
        apply_packet = function_body(self.aim, r"bool ApplyPacketToGame\(")
        validation = apply_packet.index("const bool hasValidContextPlayer")
        first_camera_write = apply_packet.index("cam.m_fFOV =")
        pitch_write = apply_packet.index("contextPlayer->m_pPlayerData->m_fLookPitch")
        self.assertLess(validation, first_camera_write)
        self.assertLess(first_camera_write, pitch_write)
        self.assertIn("contextPlayer->IsVTableValid()", apply_packet)
        self.assertIn("contextPlayer->m_pPlayerData != nullptr", apply_packet)
        self.assertNotIn("FindPlayerPed()", apply_packet)

        interpolated = function_body(self.aim, r"bool ApplyPacketInterpolated\(")
        self.assertIn("ApplyPacketToGame(interp, pNetworkPlayer->m_pPed, true)", interpolated)
        local = function_body(self.aim, r"void CAimSync::ApplyLocalContext\(")
        self.assertIn("ApplyPacketToGame(frame.cameraState, frame.previousContextPlayer", local)
        self.assertIn("allowMissingContextPlayer", self.aim)

    def test_remote_context_waits_for_a_complete_camera_baseline(self):
        network_context = function_body(self.aim, r"void CAimSync::ApplyNetworkPlayerContext\(")
        self.assertIn("!player->m_bHasCameraSnapshot", network_context)
        self.assertLess(
            network_context.index("!player->m_bHasCameraSnapshot"),
            network_context.index("ApplyPacketInterpolated"),
        )
        self.assertIn("bool m_bHasCameraSnapshot = false", self.player)

        handler = function_body(
            self.handler, r"PACKET_HANDLER\(ePacketType::PLAYER_CAMERA_SYNC\s*,"
        )
        baseline = handler.index("if (!pNetworkPlayer->m_bHasCameraSnapshot)")
        full_gate = handler.index("if (pPlayerCameraSync->bFullUpdate)", baseline)
        old_init = handler.index("m_cameraSnapshotOld = *pPlayerCameraSync", full_gate)
        new_init = handler.index("m_cameraSnapshot = *pPlayerCameraSync", old_init)
        valid = handler.index("m_bHasCameraSnapshot = true", new_init)
        early_return = handler.index("return;", valid)
        partial_update = handler.index("m_cameraSnapshot.cameraMode", early_return)
        self.assertLess(baseline, full_gate)
        self.assertLess(full_gate, old_init)
        self.assertLess(old_init, new_init)
        self.assertLess(new_init, valid)
        self.assertLess(valid, early_return)
        self.assertLess(early_return, partial_update)
        self.assertIn(
            "m_cameraSnapshot.serverTime = pPlayerCameraSync->serverTime", handler
        )

    def test_every_observer_eventually_receives_a_fresh_full_baseline(self):
        process = function_body(self.aim, r"void CAimSync::ProcessSyncing\(")
        self.assertIn("FULL_CAMERA_KEYFRAME_INTERVAL_MS = 1000", self.aim)
        self.assertIn("!hasSentInitialCameraState ||", process)
        self.assertIn("tickCount - lastFullCameraSyncTick >= FULL_CAMERA_KEYFRAME_INTERVAL_MS", process)
        self.assertIn("if (fullKeyframeDue || cameraState != lastSentCameraState", process)
        self.assertIn("hasSentInitialCameraState = true", process)
        self.assertIn("lastFullCameraSyncTick = tickCount", process)

        reset = function_body(self.aim, r"void CAimSync::ResetNetworkState\(")
        for evidence in (
            "cameraContextDepth = 0",
            "activeCameraContextPlayer = nullptr",
            "lastPlayerCameraSyncTick = 0",
            "lastFullCameraSyncTick = 0",
            "lastSentCameraState = {}",
            "hasSentInitialCameraState = false",
        ):
            self.assertIn(evidence, reset)
        self.assertIn("static void ResetNetworkState();", self.aim_header)

        connection_reset = function_body(self.network, r"void CNetwork::ResetConnectionState\(")
        self.assertIn("CAimSync::ResetNetworkState();", connection_reset)

        model = CameraBaselineModel()
        self.assertTrue(model.sender_tick(0))  # dropped initial unreliable packet
        model.receive(500, False)  # late observer ignores an incomplete state
        self.assertFalse(model.has_baseline)
        self.assertTrue(model.sender_tick(1000))
        model.receive(1000, True)
        self.assertEqual((model.old_time, model.current_time), (1000, 1000))
        model.receive(1100, False)
        self.assertEqual((model.old_time, model.current_time), (1000, 1100))
        model.reconnect()
        model.receive(1200, False)
        self.assertFalse(model.has_baseline)
        model.receive(2000, True)
        self.assertTrue(model.has_baseline)

    def test_nested_remote_contexts_restore_camera_keys_and_player_focus(self):
        camera_apply = function_body(self.aim, r"void CAimSync::ApplyNetworkPlayerContext\(")
        camera_restore = function_body(self.aim, r"void CAimSync::ApplyLocalContext\(")
        self.assertIn("cameraContextStack[cameraContextDepth++]", camera_apply)
        self.assertIn("cameraContextStack[--cameraContextDepth]", camera_restore)
        self.assertIn("previousContextPlayer", camera_apply)
        self.assertIn("previousContextWasRemote", camera_restore)

        key_apply = function_body(self.keys, r"void CKeySync::ApplyNetworkPlayerContext\(")
        key_restore = function_body(self.keys, r"void CKeySync::ApplyLocalContext\(")
        self.assertIn("keyContextStack[keyContextDepth++]", key_apply)
        self.assertIn("keyContextStack[--keyContextDepth]", key_restore)

        for hook in (self.player_hooks, self.vehicle_hooks, self.task_hooks):
            self.assertIn("const int previousPlayerInFocus = CWorld::PlayerInFocus", hook)
            self.assertIn("CWorld::PlayerInFocus = previousPlayerInFocus", hook)


if __name__ == "__main__":
    unittest.main()
