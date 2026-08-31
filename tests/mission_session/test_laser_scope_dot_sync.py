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


class LaserRelayModel:
    """Executable model of active heartbeat, explicit stop, and stale-loss behavior."""

    HEARTBEAT_MS = 250
    STALE_MS = 1000

    def __init__(self):
        self.last_sent = 0
        self.last_state = False
        self.remote_active = False
        self.remote_received = None

    def sender_tick(self, now: int, active: bool):
        changed = active != self.last_state
        heartbeat = active and ((now - self.last_sent) & 0xFFFFFFFF) >= self.HEARTBEAT_MS
        if changed or heartbeat:
            self.last_sent = now
            self.last_state = active
            return {"active": active, "full": active}
        return None

    def receive(self, now: int, packet):
        self.remote_active = packet["active"]
        self.remote_received = now if packet["active"] else None

    def visible(self, now: int):
        if self.remote_received is not None and ((now - self.remote_received) & 0xFFFFFFFF) > self.STALE_MS:
            self.remote_active = False
            self.remote_received = None
        return self.remote_active


class LaserScopeDotSyncTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.packets = (ROOT / "shared/network/packets/players.h").read_text(encoding="utf-8")
        cls.aim = (ROOT / "client/src/CAimSync.cpp").read_text(encoding="utf-8")
        cls.manager = (ROOT / "client/src/CLaserScopeDotSync.cpp").read_text(encoding="utf-8")
        cls.manager_header = (ROOT / "client/src/CLaserScopeDotSync.h").read_text(encoding="utf-8")
        cls.client_handler = (ROOT / "client/src/PacketHandlers/players.cpp").read_text(encoding="utf-8")
        cls.server_handler = (ROOT / "server/src/PacketHandlers/players.cpp").read_text(encoding="utf-8")
        cls.server_player = (ROOT / "server/src/CNetworkPlayer.h").read_text(encoding="utf-8")
        cls.remote_player = (ROOT / "client/src/CNetworkPlayer.h").read_text(encoding="utf-8")
        cls.remote_player_source = (ROOT / "client/src/CNetworkPlayer.cpp").read_text(encoding="utf-8")
        cls.packet_types = (ROOT / "shared/network/packet_types.h").read_text(encoding="utf-8")
        cls.game_hooks = (ROOT / "client/src/Hooks/GameHooks.cpp").read_text(encoding="utf-8")

    def test_existing_camera_packet_has_a_bounded_closed_laser_payload(self):
        packet = re.search(r"class PlayerCameraSync\b.*?^};", self.packets, re.S | re.M).group(0)
        self.assertIn(
            "DEFINE_PACKET_TYPE(PlayerCameraSync, ePacketType::PLAYER_CAMERA_SYNC, ePacketChannel::SYNC)",
            packet,
        )
        self.assertIn("serialize_bool(stream, bLaserScopeDotActive)", packet)
        self.assertIn("serialize_object(stream, laserScopeDotPosition)", packet)
        self.assertIn("LASER_DOT_MIN_SIZE = 0.001f", packet)
        self.assertIn("LASER_DOT_MAX_SIZE = 64.0f", packet)
        self.assertIn("serialize_compressed_float", packet)
        for invariant in (
            "cameraMode == MODE_SNIPER || cameraMode == MODE_SNIPER_RUNABOUT",
            "bFullUpdate && sniperCamera",
            "std::isfinite(laserScopeDotPosition.x)",
            "laserScopeDotPosition.x >= -3000.0f",
            "laserScopeDotPosition.z <= 1000.0f",
            "std::isfinite(laserScopeDotSize)",
            "laserScopeDotSize >= LASER_DOT_MIN_SIZE",
            "laserScopeDotSize <= LASER_DOT_MAX_SIZE",
        ):
            self.assertIn(invariant, packet)
        self.assertNotIn("LASER", self.packet_types)

    def test_local_capture_is_the_real_alive_on_foot_sniper_and_uses_native_path(self):
        capture = function_body(self.manager, r"void CaptureLocalDot\(")
        for evidence in (
            "CNetwork::m_bAuthenticated",
            "CWorld::PlayerInFocus != 0",
            "player->IsAlive()",
            "player->m_nPedFlags.bInVehicle",
            "weapon.m_eWeaponType != WEAPON_SNIPERRIFLE",
            "IsSniperCameraMode(cameraMode)",
            "weapon.LaserScopeDot(&position, &size)",
            "IsFiniteNativeResult(position, size)",
        ):
            self.assertIn(evidence, capture)
        camera_modes = function_body(self.manager, r"bool IsSniperCameraMode\(")
        self.assertIn("MODE_SNIPER", camera_modes)
        self.assertIn("MODE_SNIPER_RUNABOUT", camera_modes)
        self.assertNotIn("LaserScopeDot", self.game_hooks)

    def test_first_active_update_is_forced_full_heartbeats_and_stop_is_distinct(self):
        process = function_body(self.aim, r"void CAimSync::ProcessSyncing\(")
        collect = process.index("CollectState(&cameraState, pPlayerPed)")
        force_full = process.index(
            "fullKeyframeDue || requireFullUpdate || cameraState.bLaserScopeDotActive"
        )
        compare = process.index("cameraState != lastSentCameraState")
        self.assertLess(collect, force_full)
        self.assertLess(force_full, compare)
        self.assertIn("ShouldSendHeartbeat(cameraState, tickCount, lastPlayerCameraSyncTick)", process)
        self.assertIn("HEARTBEAT_INTERVAL_MS = 250", self.manager_header)

        model = LaserRelayModel()
        start = model.sender_tick(100, True)
        self.assertEqual(start, {"active": True, "full": True})
        model.receive(100, start)
        self.assertTrue(model.visible(100))
        self.assertIsNone(model.sender_tick(200, True))
        heartbeat = model.sender_tick(350, True)
        self.assertEqual(heartbeat, {"active": True, "full": True})
        stop = model.sender_tick(351, False)
        self.assertEqual(stop, {"active": False, "full": False})
        model.receive(351, stop)
        self.assertFalse(model.visible(351))

    def test_dropped_stop_is_bounded_by_remote_stale_timeout(self):
        model = LaserRelayModel()
        packet = model.sender_tick(0xFFFFFF00, True)
        model.receive(0xFFFFFF00, packet)
        self.assertTrue(model.visible(0x00000050))
        self.assertFalse(model.visible(0x00000350))

        render = function_body(self.manager, r"void RenderRemoteDot\(")
        self.assertIn("now - player->m_nLaserScopeDotReceivedAt > CLaserScopeDotSync::STALE_TIMEOUT_MS", render)
        self.assertIn("m_cameraSnapshot.bLaserScopeDotActive = false", render)
        self.assertIn("m_nLaserScopeDotReceivedAt = 0", render)
        self.assertIn("STALE_TIMEOUT_MS = 1000", self.manager_header)

    def test_server_validates_before_identity_and_rate_limits_only_active_laser(self):
        handler = function_body(
            self.server_handler, r"PACKET_HANDLER\(ePacketType::PLAYER_CAMERA_SYNC\s*,"
        )
        validation = handler.index("IsLaserScopeDotSemanticallyValid")
        identity = handler.index("pPlayerCameraSync->playerid = pNetworkPlayer->m_iPlayerId")
        relay = handler.index("GetPacketFactory().SendToAll(*pPlayerCameraSync, pNetworkPlayer)")
        self.assertLess(validation, identity)
        self.assertLess(identity, relay)
        for authority in (
            "pPlayerCameraSync->bLaserScopeDotActive",
            "m_bHasOnFootSnapshot",
            "m_bIsAlive",
            "m_eLastWeaponType != WEAPON_SNIPERRIFLE",
            "m_nVehicleId >= 0",
            "CanRelayLaserScopeDotUpdate(pNetworkPlayer)",
        ):
            self.assertIn(authority, handler)
        self.assertEqual(handler.count("CanRelayLaserScopeDotUpdate"), 1)
        self.assertRegex(
            handler,
            re.compile(
                r"if \(pPlayerCameraSync->bLaserScopeDotActive &&.*?"
                r"!CanRelayLaserScopeDotUpdate\(pNetworkPlayer\)\)\)",
                re.S,
            ),
        )

        guard = function_body(self.server_handler, r"bool CanRelayLaserScopeDotUpdate\(")
        for evidence in (
            "player->m_iPlayerId < 0",
            "player->m_iPlayerId >= Config::MAX_SERVER_PLAYERS",
            "slot.owner != player",
            "slot.connectId != connectId",
            "now - slot.windowStartedAt >= LASER_DOT_RATE_WINDOW_MS",
            "slot.updateCount >= MAX_LASER_DOT_UPDATES_PER_WINDOW",
        ):
            self.assertIn(evidence, guard)
        self.assertIn("MAX_LASER_DOT_UPDATES_PER_WINDOW = 30", self.server_handler)

    def test_server_tracks_sender_weapon_life_and_canonical_identity_from_onfoot(self):
        onfoot = function_body(
            self.server_handler, r"PACKET_HANDLER\(\s*ePacketType::PLAYER_ONFOOT_UPDATE\s*,"
        )
        for evidence in (
            "m_bHasOnFootSnapshot = true",
            "healthSnapshot.iHealth > 0",
            "m_eLastWeaponType",
            "weaponSnapshot.iWeaponType",
        ):
            self.assertIn(evidence, onfoot)
        for field in (
            "bool m_bHasOnFootSnapshot = false",
            "bool m_bIsAlive = false",
            "eWeaponType m_eLastWeaponType = WEAPON_UNARMED",
        ):
            self.assertIn(field, self.server_player)

    def test_remote_render_is_observer_safe_and_preserves_moon_behavior(self):
        render = function_body(self.manager, r"void RenderRemoteDot\(")
        for gate in (
            "ped == nullptr",
            "!ped->IsAlive()",
            "ped->m_nPedFlags.bInVehicle",
            "ped->GetWeapon().m_eWeaponType != WEAPON_SNIPERRIFLE",
            "IsLaserScopeDotSemanticallyValid()",
        ):
            self.assertIn(gate, render)
        for native_constant in (
            "128, 0, 0, 255",
            "1.2f, 50.0f",
            "CORONATYPE_SHINYSTAR",
            "FLARETYPE_NONE",
            "1.5f, 0, 15.0f",
        ):
            self.assertIn(native_constant, render)
        for forbidden in (
            "ApplyNetworkPlayerContext",
            "ApplyLocalContext",
            "FireSniper",
            "MoonSize",
            "SetCurrentWeapon",
        ):
            self.assertNotIn(forbidden, self.manager)

    def test_client_handler_rejects_bad_state_and_handles_stream_lifecycle(self):
        handler = function_body(
            self.client_handler, r"PACKET_HANDLER\(ePacketType::PLAYER_CAMERA_SYNC\s*,"
        )
        self.assertLess(
            handler.index("IsLaserScopeDotSemanticallyValid"),
            handler.index("CNetworkPlayerManager::GetPlayer"),
        )
        self.assertIn("CLaserScopeDotSync::HandleRemoteState", handler)
        remote = function_body(self.manager, r"void CLaserScopeDotSync::HandleRemoteState\(")
        self.assertIn("packet.bLaserScopeDotActive", remote)
        self.assertIn("packet.bLaserScopeDotActive ? GetTickCount() : 0", remote)
        self.assertIn("uint32_t m_nLaserScopeDotReceivedAt = 0", self.remote_player)
        process = function_body(self.manager, r"void CLaserScopeDotSync::Process\(")
        self.assertIn("CNetworkPlayerManager::m_pPlayers", process)
        self.assertIn("!CNetwork::m_bAuthenticated", process)

        destroy = function_body(self.remote_player_source, r"void CNetworkPlayer::DestroyPed\(")
        self.assertIn("ClearLaserScopeDotState()", destroy)
        clear = function_body(self.remote_player_source, r"void CNetworkPlayer::ClearLaserScopeDotState\(")
        self.assertIn("m_cameraSnapshotOld.bLaserScopeDotActive = false", clear)
        self.assertIn("m_cameraSnapshot.bLaserScopeDotActive = false", clear)
        self.assertIn("m_nLaserScopeDotReceivedAt = 0", clear)
        weapon = function_body(self.remote_player_source, r"void CNetworkPlayer::ApplyWeaponSnapshot\(")
        self.assertIn("weaponSnapshot.iWeaponType != WEAPON_SNIPERRIFLE", weapon)
        self.assertIn("ClearLaserScopeDotState()", weapon)

        respawn = function_body(
            self.server_handler, r"PACKET_HANDLER\(ePacketType::RESPAWN_PLAYER\s*,"
        )
        self.assertIn("m_bHasOnFootSnapshot = false", respawn)
        self.assertIn("m_bIsAlive = false", respawn)
        self.assertIn("m_eLastWeaponType = WEAPON_UNARMED", respawn)


if __name__ == "__main__":
    unittest.main()
