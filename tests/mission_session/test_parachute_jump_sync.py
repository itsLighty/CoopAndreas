import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


def function_body(source: str, signature_pattern: str) -> str:
    signature = re.search(signature_pattern, source)
    if signature is None:
        raise AssertionError(f"missing function matching {signature_pattern}")
    opening = source.find("{", signature.end())
    if opening < 0:
        raise AssertionError(f"missing function body matching {signature_pattern}")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : index]
    raise AssertionError(f"unterminated function matching {signature_pattern}")


class ParachuteRelayModel:
    HEARTBEAT_MS = 250
    STALE_MS = 1500

    FREEFALL = {1, 2, 3, 4}
    OPENING = 5
    DEPLOYED = {6, 7, 8, 9}
    COLLAPSED = 10
    LANDING = {11, 12}

    def __init__(self):
        self.sequence = 0
        self.last_state = 0
        self.last_sent = 0
        self.remote_state = 0
        self.remote_sequence = None
        self.remote_received = None

    def sender_tick(self, now: int, state: int):
        changed = state != self.last_state
        heartbeat = state != 0 and ((now - self.last_sent) & 0xFFFFFFFF) >= self.HEARTBEAT_MS
        if not (changed or heartbeat):
            return None
        self.sequence = (self.sequence + 1) & 0xFFFF
        self.last_state = state
        self.last_sent = now
        return {"state": state, "sequence": self.sequence}

    @staticmethod
    def newer(incoming: int, previous: int) -> bool:
        difference = (incoming - previous) & 0xFFFF
        signed = difference if difference < 0x8000 else difference - 0x10000
        return signed > 0

    @classmethod
    def valid_transition(cls, previous: int, incoming: int) -> bool:
        if incoming == 0 or incoming == previous or previous == 0:
            return True
        if previous in cls.FREEFALL:
            return incoming in cls.FREEFALL or incoming in {cls.OPENING, cls.COLLAPSED}
        if previous == cls.OPENING:
            return incoming in cls.DEPLOYED | cls.LANDING | {cls.COLLAPSED}
        if previous in cls.DEPLOYED:
            return incoming in cls.DEPLOYED | cls.LANDING | {cls.COLLAPSED}
        if previous == cls.COLLAPSED:
            return incoming == cls.COLLAPSED
        return incoming == previous

    def receive(self, now: int, packet):
        if self.remote_sequence is not None and not self.newer(packet["sequence"], self.remote_sequence):
            return False
        if not self.valid_transition(self.remote_state, packet["state"]):
            return False
        self.remote_sequence = packet["sequence"]
        self.remote_state = packet["state"]
        self.remote_received = now if packet["state"] else None
        return True

    def visible_state(self, now: int) -> int:
        if self.remote_received is not None and ((now - self.remote_received) & 0xFFFFFFFF) > self.STALE_MS:
            self.remote_state = 0
            self.remote_received = None
        return self.remote_state


class ParachuteJumpSyncTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.packets = (ROOT / "shared/network/packets/players.h").read_text(encoding="utf-8")
        cls.packet_types = (ROOT / "shared/network/packet_types.h").read_text(encoding="utf-8")
        cls.local_player = (ROOT / "client/src/CLocalPlayer.cpp").read_text(encoding="utf-8")
        cls.manager = (ROOT / "client/src/CPlayerParachuteSyncManager.cpp").read_text(encoding="utf-8")
        cls.manager_header = (ROOT / "client/src/CPlayerParachuteSyncManager.h").read_text(encoding="utf-8")
        cls.remote = (ROOT / "client/src/CNetworkPlayer.cpp").read_text(encoding="utf-8")
        cls.remote_header = (ROOT / "client/src/CNetworkPlayer.h").read_text(encoding="utf-8")
        cls.hooks = (ROOT / "client/src/Hooks/PlayerHooks.cpp").read_text(encoding="utf-8")
        cls.client_handler = (ROOT / "client/src/PacketHandlers/players.cpp").read_text(encoding="utf-8")
        cls.server_handler = (ROOT / "server/src/PacketHandlers/players.cpp").read_text(encoding="utf-8")
        cls.server_player = (ROOT / "server/src/CNetworkPlayer.h").read_text(encoding="utf-8")

    def test_existing_task_packet_has_closed_pointer_free_bounded_state(self):
        enum = re.search(r"enum ePlayerParachuteState\s*:\s*int\s*\{(.*?)\};", self.packets, re.S)
        self.assertIsNotNone(enum)
        self.assertEqual(
            re.findall(r"PLAYER_PARACHUTE_[A-Z_]+", enum.group(1)),
            [
                "PLAYER_PARACHUTE_NONE",
                "PLAYER_PARACHUTE_FREEFALL",
                "PLAYER_PARACHUTE_FREEFALL_LEFT",
                "PLAYER_PARACHUTE_FREEFALL_RIGHT",
                "PLAYER_PARACHUTE_FREEFALL_ACCEL",
                "PLAYER_PARACHUTE_OPENING",
                "PLAYER_PARACHUTE_DEPLOYED",
                "PLAYER_PARACHUTE_DEPLOYED_LEFT",
                "PLAYER_PARACHUTE_DEPLOYED_RIGHT",
                "PLAYER_PARACHUTE_DEPLOYED_FLARE",
                "PLAYER_PARACHUTE_COLLAPSED",
                "PLAYER_PARACHUTE_LANDING",
                "PLAYER_PARACHUTE_LANDING_WATER",
                "PLAYER_PARACHUTE_COUNT",
            ],
        )

        packet = re.search(r"class SetPlayerTask\b.*?^};", self.packets, re.S | re.M).group(0)
        self.assertIn("ePacketType::SET_PLAYER_TASK", packet)
        self.assertIn("serialize_bool(stream, hasParachuteState)", packet)
        self.assertIn(
            "serialize_int(stream, parachuteState, PLAYER_PARACHUTE_NONE, PLAYER_PARACHUTE_COUNT - 1)",
            packet,
        )
        self.assertIn("serialize_uint16(stream, parachuteSequence)", packet)
        self.assertIn("serialize_uint8(stream, parachuteProgress)", packet)
        self.assertEqual(packet.count("serialize_compressed_float(\n                stream, parachute"), 2)
        for invariant in (
            "!hasAnimationState",
            "std::isfinite(parachutePitch)",
            "std::isfinite(parachuteRoll)",
            "PARACHUTE_TILT_LIMIT",
            "stoppedStateIsCanonical",
            "Stream::IsWriting && !IsParachuteStateSemanticallyValid()",
            "Stream::IsReading && !IsParachuteStateSemanticallyValid()",
        ):
            self.assertIn(invariant, packet)
        self.assertNotRegex(packet, r"parachute[A-Za-z]*\s*\*")
        self.assertNotIn("PARACHUTE", self.packet_types)

    def test_local_observer_requires_real_alive_on_foot_parachute_presentation(self):
        observe = function_body(
            self.manager, r"CPlayerParachuteSyncManager::ObserveLocalState\("
        )
        for gate in (
            "!player->IsAlive()",
            "player->m_nPedFlags.bInVehicle",
            "player->m_pVehicle",
            "player->GetWeapon().m_eWeaponType != WEAPON_PARACHUTE",
            'GetAnimationBlockIndex("PARACHUTE")',
            "candidate->m_pHierarchy->m_nAnimBlockId != parachuteBlockId",
        ):
            self.assertIn(gate, observe)
        for animation in (
            "FALL_SKYDIVE",
            "FALL_SKYDIVE_L",
            "FALL_SKYDIVE_R",
            "FALL_SKYDIVE_ACCEL",
            "PARA_OPEN",
            "PARA_FLOAT",
            "PARA_STEERL",
            "PARA_STEERR",
            "PARA_DECEL",
            "PARA_LAND",
            "PARA_LAND_WATER",
        ):
            self.assertIn(animation, self.manager)
        rip = function_body(self.manager, r"FindCanopyRipAssociation\(")
        self.assertIn("MODEL_PARACHUTE", rip)
        self.assertIn("object->m_pAttachedTo != player", rip)
        self.assertIn('GetUppercaseKey("PARA_RIP_LOOP_O")', rip)

    def test_sender_start_heartbeat_stop_wrap_and_dropped_stop_timeout(self):
        process = function_body(self.manager, r"CPlayerParachuteSyncManager::ProcessLocal\(")
        self.assertIn("!CNetwork::m_bAuthenticated", process)
        self.assertIn("CWorld::PlayerInFocus != 0", process)
        self.assertIn("state != ms_lastState", process)
        self.assertIn("now - ms_lastSentAt >= HEARTBEAT_INTERVAL_MS", process)
        self.assertIn("HEARTBEAT_INTERVAL_MS = 250", self.manager_header)
        send = function_body(self.manager, r"CPlayerParachuteSyncManager::SendState\(")
        self.assertIn("++ms_sequence", send)
        self.assertIn("BuildParachuteTaskPacket", send)

        model = ParachuteRelayModel()
        start = model.sender_tick(100, 1)
        self.assertTrue(model.receive(100, start))
        self.assertIsNone(model.sender_tick(200, 1))
        heartbeat = model.sender_tick(350, 1)
        self.assertTrue(model.receive(350, heartbeat))
        stop = model.sender_tick(351, 0)
        self.assertTrue(model.receive(351, stop))
        self.assertEqual(model.visible_state(351), 0)

        model.sequence = 0xFFFF
        wrapped = model.sender_tick(352, 1)
        self.assertEqual(wrapped["sequence"], 0)
        model.remote_sequence = 0xFFFF
        model.remote_state = 0
        self.assertTrue(model.receive(352, wrapped))
        self.assertFalse(model.receive(353, wrapped))
        model.remote_received = 0xFFFFFF00
        self.assertEqual(model.visible_state(0x00000400), 1)
        self.assertEqual(model.visible_state(0x00000600), 0)

    def test_server_validates_authority_transition_sequence_rate_before_identity(self):
        handler = function_body(
            self.server_handler, r"PACKET_HANDLER\(ePacketType::SET_PLAYER_TASK\s*,"
        )
        validation = handler.index("IsParachuteStateSemanticallyValid")
        identity = handler.index("pSetPlayerTask->playerid = pNetworkPlayer->m_iPlayerId")
        relay = handler.index("GetPacketFactory().SendToAll(*pSetPlayerTask, pNetworkPlayer)")
        self.assertLess(validation, identity)
        self.assertLess(identity, relay)
        for evidence in (
            "pSetPlayerTask->hasParachuteState",
            "CanRelayParachuteUpdate(pNetworkPlayer)",
            "IsParachuteSequenceNewer",
            "IsValidParachuteTransition",
            "m_bHasOnFootSnapshot",
            "m_bIsAlive",
            "m_eLastWeaponType != WEAPON_PARACHUTE",
            "m_nVehicleId >= 0",
            "incoming != Packets::Players::PLAYER_PARACHUTE_NONE",
        ):
            self.assertIn(evidence, handler)
        self.assertEqual(handler.count("CanRelayParachuteUpdate"), 1)

        guard = function_body(self.server_handler, r"bool CanRelayParachuteUpdate\(")
        for evidence in (
            "player->m_iPlayerId < 0",
            "player->m_iPlayerId >= Config::MAX_SERVER_PLAYERS",
            "slot.owner != player",
            "slot.connectId != connectId",
            "now - slot.windowStartedAt >= PARACHUTE_RATE_WINDOW_MS",
            "slot.updateCount >= MAX_PARACHUTE_UPDATES_PER_WINDOW",
            "++slot.updateCount",
        ):
            self.assertIn(evidence, guard)
        self.assertIn("MAX_PARACHUTE_UPDATES_PER_WINDOW = 12", self.server_handler)

    def test_transition_model_rejects_impossible_reopen_and_landing_escape(self):
        self.assertTrue(ParachuteRelayModel.valid_transition(1, 5))
        self.assertTrue(ParachuteRelayModel.valid_transition(5, 6))
        self.assertTrue(ParachuteRelayModel.valid_transition(6, 8))
        self.assertTrue(ParachuteRelayModel.valid_transition(8, 11))
        self.assertTrue(ParachuteRelayModel.valid_transition(11, 0))
        self.assertFalse(ParachuteRelayModel.valid_transition(6, 1))
        self.assertFalse(ParachuteRelayModel.valid_transition(11, 6))
        self.assertFalse(ParachuteRelayModel.valid_transition(10, 5))
        transition = function_body(self.server_handler, r"bool IsValidParachuteTransition\(")
        self.assertIn("IsFreefallState(previous)", transition)
        self.assertIn("IsDeployedState(previous)", transition)
        self.assertIn("PLAYER_PARACHUTE_COLLAPSED", transition)
        self.assertIn("PLAYER_PARACHUTE_LANDING_WATER", transition)

    def test_remote_recreates_only_stock_task_and_canopy_presentation(self):
        definitions = function_body(self.remote, r"bool GetSyncedParachuteDefinition\(")
        for animation in (
            '"FALL_SKYDIVE"',
            '"FALL_SKYDIVE_ACCEL"',
            '"PARA_OPEN"',
            '"PARA_FLOAT"',
            '"PARA_STEERL"',
            '"PARA_STEERR"',
            '"PARA_DECEL"',
            '"PARA_RIP_LOOP_O"',
            '"PARA_LAND"',
            '"PARA_LAND_WATER"',
        ):
            self.assertIn(animation, definitions)

        apply = function_body(self.remote, r"void CNetworkPlayer::ApplySyncedParachute\(")
        for evidence in (
            "CPlayerParachuteSyncManager::EnsureResourcesLoaded()",
            "TASK_PLAY_ANIM_NON_INTERRUPTABLE",
            "CREATE_OBJECT",
            "MODEL_PARACHUTE",
            "ATTACH_OBJECT_TO_CHAR",
            "PLAY_OBJECT_ANIM",
            "DETACH_OBJECT",
            "SetOrientation",
            "CorrectParachuteAnimationProgress",
            "missingPedAnimation",
        ):
            self.assertIn(evidence, apply)
        for forbidden in (
            "SetCurrentWeapon",
            "GiveWeapon",
            "REMOVE_WEAPON",
            "TheCamera",
            "CTimer::ms_fTimeScale",
            "SET_TIME_SCALE",
        ):
            self.assertNotIn(forbidden, apply)

    def test_stock_control_runs_then_network_task_is_reasserted(self):
        hook = function_body(self.hooks, r"CPlayerPed__ProcessControl_Hook\(")
        remote_control = hook.rindex("plugin::CallMethod<0x60EA90, CPlayerPed*>(This)")
        reapply = hook.index("player->ProcessSyncedParachute()", remote_control)
        self.assertLess(remote_control, reapply)
        self.assertIn("CPlayerParachuteSyncManager::ProcessLocal()", hook)
        apply = function_body(self.remote, r"void CNetworkPlayer::ApplySyncedParachute\(")
        self.assertIn("stateChanged || missingPedAnimation", apply)

    def test_death_weapon_vehicle_respawn_disconnect_stream_and_stale_cleanup(self):
        process = function_body(self.remote, r"void CNetworkPlayer::ProcessSyncedParachute\(")
        for gate in (
            "!m_pPed",
            "!m_pPed->IsAlive()",
            "m_pPed->m_nPedFlags.bInVehicle",
            "m_pPed->m_pVehicle",
            "STALE_TIMEOUT_MS",
            "ClearSyncedParachuteState()",
        ):
            self.assertIn(gate, process)
        self.assertIn("STALE_TIMEOUT_MS = 1500", self.manager_header)

        weapon = function_body(self.remote, r"void CNetworkPlayer::ApplyWeaponSnapshot\(")
        self.assertIn("weaponSnapshot.iWeaponType != WEAPON_PARACHUTE", weapon)
        self.assertIn("ClearSyncedParachuteState()", weapon)
        for vehicle_function in (
            "WarpIntoVehicleDriver",
            "WarpIntoVehiclePassenger",
            "EnterVehiclePassenger",
        ):
            body = function_body(self.remote, rf"void CNetworkPlayer::{vehicle_function}\(")
            self.assertIn("ClearSyncedParachuteState()", body)

        create = function_body(self.remote, r"void CNetworkPlayer::CreatePed\(")
        destroy = function_body(self.remote, r"void CNetworkPlayer::DestroyPed\(")
        respawn = function_body(self.remote, r"void CNetworkPlayer::Respawn\(")
        destructor = function_body(self.remote, r"CNetworkPlayer::~CNetworkPlayer\(")
        self.assertIn("ApplySyncedParachute()", create)
        self.assertIn("DestroySyncedParachutePresentation()", destroy)
        self.assertNotIn("ClearSyncedParachuteState()", destroy)
        self.assertIn("ClearSyncedParachuteState()", respawn)
        self.assertIn("ClearSyncedParachuteState()", destructor)

        onfoot = function_body(
            self.server_handler,
            r"PACKET_HANDLER\(\s*ePacketType::PLAYER_ONFOOT_UPDATE\s*,",
        )
        self.assertIn("RelayAuthoritativeParachuteStop", onfoot)
        self.assertIn("m_eLastWeaponType != WEAPON_PARACHUTE", onfoot)
        respawn_server = function_body(
            self.server_handler, r"PACKET_HANDLER\(ePacketType::RESPAWN_PLAYER\s*,"
        )
        self.assertIn("RelayAuthoritativeParachuteStop", respawn_server)

    def test_resources_are_reference_counted_and_no_unaudited_raw_native_calls_are_added(self):
        acquire = function_body(self.manager, r"CPlayerParachuteSyncManager::AcquireResources\(")
        ensure = function_body(self.manager, r"CPlayerParachuteSyncManager::EnsureResourcesLoaded\(")
        release = function_body(self.manager, r"CPlayerParachuteSyncManager::ReleaseResources\(")
        self.assertIn("ms_resourceUsers >= Config::MAX_SERVER_PLAYERS", acquire)
        self.assertIn("ms_resourceUsers++ == 0", acquire)
        self.assertIn("CStreaming::RequestModel", acquire)
        self.assertIn("CAnimManager::AddAnimBlockRef", ensure)
        self.assertIn("CAnimManager::RemoveAnimBlockRef", release)
        self.assertIn("CStreaming::SetModelIsDeletable", release)
        self.assertIn("IFP_RESOURCE_BASE = 25575", self.manager)
        self.assertNotRegex(self.manager, r"plugin::(?:Call|CallMethod)<0x[0-9A-Fa-f]+")

    def test_client_handler_checks_both_payloads_and_never_relays(self):
        handler = function_body(
            self.client_handler, r"PACKET_HANDLER\(ePacketType::SET_PLAYER_TASK\s*,"
        )
        self.assertIn("IsAnimationStateSemanticallyValid", handler)
        self.assertIn("IsParachuteStateSemanticallyValid", handler)
        self.assertIn("pNetworkPlayer->HandleTask(*pSetPlayerTask)", handler)
        self.assertNotIn("GetPacketFactory", handler)
        handle = function_body(self.remote, r"void CNetworkPlayer::HandleTask\(")
        self.assertIn("++m_nPendingTaskGeneration", handle)
        self.assertIn("ApplyTaskPresentation(packet)", handle)
        replay = function_body(self.remote, r"CNetworkPlayer::ApplyPendingTaskOnce\(")
        self.assertIn("m_nAppliedTaskGeneration == m_nPendingTaskGeneration", replay)
        self.assertEqual(replay.count("ApplyTaskPresentation(m_pendingTask)"), 1)
        presentation = function_body(self.remote, r"CNetworkPlayer::ApplyTaskPresentation\(")
        self.assertLess(
            presentation.index("packet.hasParachuteState"), presentation.index("if (!m_pPed)")
        )
        self.assertIn("HandleSyncedParachute(packet)", presentation)


if __name__ == "__main__":
    unittest.main()
