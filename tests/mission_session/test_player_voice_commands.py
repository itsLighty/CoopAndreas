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


class VoiceCommandDebounceModel:
    DEBOUNCE_MS = 250

    def __init__(self):
        self.session = None
        self.last_context = None
        self.last_at = 0

    def accept(self, session, now: int, context: str, authenticated=True, alive=True):
        if not authenticated or not alive:
            self.session = self.last_context = None
            return False
        if session != self.session:
            self.session = session
            self.last_context = None
        if self.last_context == context and ((now - self.last_at) & 0xFFFFFFFF) < self.DEBOUNCE_MS:
            return False
        self.last_context = context
        self.last_at = now
        return True


class VoiceCommandRateModel:
    LIMIT = 8

    def __init__(self):
        self.session = None
        self.started = 0
        self.count = 0

    def accept(self, session, now: int, is_command: bool):
        if not is_command:
            return True
        if session != self.session or ((now - self.started) & 0xFFFFFFFF) >= 2000:
            self.session = session
            self.started = now
            self.count = 0
        if self.count >= self.LIMIT:
            return False
        self.count += 1
        return True


class PlayerVoiceCommandTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.packet = (ROOT / "shared/network/packets/peds.h").read_text(encoding="utf-8")
        cls.hook = (ROOT / "client/src/Hooks/PedHooks.cpp").read_text(encoding="utf-8")
        cls.client = (ROOT / "client/src/PacketHandlers/peds.cpp").read_text(encoding="utf-8")
        cls.server = (ROOT / "server/src/PacketHandlers/peds.cpp").read_text(encoding="utf-8")
        cls.packet_types = (ROOT / "shared/network/packet_types.h").read_text(encoding="utf-8")

    def test_reuses_ped_say_and_has_the_exact_deliberate_stock_allowlist(self):
        allowlist = function_body(self.packet, r"IsDeliberatePlayerVoiceCommand\(")
        actual = set(re.findall(r"case (CONTEXT_GLOBAL_[A-Z0-9_]+):", allowlist))
        expected = {
            "CONTEXT_GLOBAL_JOIN_ME_ASK",
            "CONTEXT_GLOBAL_JOIN_ME_REJECTED",
            "CONTEXT_GLOBAL_ORDER_ATTACK_MANY",
            "CONTEXT_GLOBAL_ORDER_ATTACK_SINGLE",
            "CONTEXT_GLOBAL_ORDER_DISBAND_MANY",
            "CONTEXT_GLOBAL_ORDER_DISBAND_ONE",
            "CONTEXT_GLOBAL_ORDER_FOLLOW_FAR_MANY",
            "CONTEXT_GLOBAL_ORDER_FOLLOW_FAR_ONE",
            "CONTEXT_GLOBAL_ORDER_FOLLOW_NEAR_MANY",
            "CONTEXT_GLOBAL_ORDER_FOLLOW_NEAR_ONE",
            "CONTEXT_GLOBAL_ORDER_FOLLOW_VNEAR_MANY",
            "CONTEXT_GLOBAL_ORDER_FOLLOW_VNEAR_ONE",
            "CONTEXT_GLOBAL_ORDER_WAIT_MANY",
            "CONTEXT_GLOBAL_ORDER_WAIT_ONE",
        }
        self.assertEqual(actual, expected)
        self.assertNotIn("CONTEXT_GLOBAL_ORDER_KEEP_UP_ONE", allowlist)
        self.assertNotIn("CONTEXT_GLOBAL_ORDER_KEEP_UP_MANY", allowlist)
        self.assertIn("DEFINE_PACKET_TYPE(PedSay, ePacketType::PED_SAY", self.packet)
        self.assertNotIn("VOICE_COMMAND", self.packet_types)

    def test_command_arguments_are_closed_while_ordinary_ped_say_stays_general(self):
        arguments = function_body(self.packet, r"HasStockPlayerVoiceCommandArguments\(")
        for invariant in (
            "IsDeliberatePlayerVoiceCommand(context)",
            "startTimeDelay == 0",
            "!overrideSilence",
            "!isForceAudible",
            "!isFrontEnd",
        ):
            self.assertIn(invariant, arguments)

        packet = re.search(r"class PedSay\b.*?^};", self.packet, re.S | re.M).group(0)
        self.assertIn("serialize_uint32(stream, startTimeDelay)", packet)
        self.assertIn("serialize_bool(stream, overrideSilence)", packet)
        self.assertNotIn("HasStockPlayerVoiceCommandArguments", packet)

    def test_native_accepted_call_is_the_single_keyboard_and_gamepad_capture_point(self):
        hook = function_body(self.hook, r"CAEPedSpeechAudioEntity__AddSayEvent_Hook\(")
        native = hook.index("plugin::CallMethodAndReturn<int16_t, 0x4E6550>")
        accepted = hook.index("if (result == -1)")
        send = hook.index("GetPacketFactory().Send(packet)")
        self.assertLess(native, accepted)
        self.assertLess(accepted, send)
        self.assertIn("patch::RedirectCall(0x5F000B", self.hook)
        self.assertIn("keyboard and gamepad group controls converge", self.hook)
        self.assertNotIn("GroupControlForwardJustDown", self.hook)
        self.assertNotIn("GroupControlBackJustDown", self.hook)
        self.assertIn("pPed != FindPlayerPed(0)", hook)
        self.assertIn("!CNetwork::m_bAuthenticated", hook)

    def test_client_debounce_is_command_only_wrap_safe_and_reconnect_safe(self):
        guard = function_body(self.hook, r"ShouldRelayPlayerVoiceCommand\(")
        for evidence in (
            "ped != FindPlayerPed(0)",
            "!ped->IsAlive()",
            "state.peer != CNetwork::m_pPeer",
            "state.connectId != connectId",
            "state.playerId != CNetworkPlayerManager::m_nMyId",
            "now - state.acceptedAt < PLAYER_VOICE_COMMAND_DEBOUNCE_MS",
        ):
            self.assertIn(evidence, guard)
        self.assertIn("PLAYER_VOICE_COMMAND_DEBOUNCE_MS = 250", self.hook)

        hook = function_body(self.hook, r"CAEPedSpeechAudioEntity__AddSayEvent_Hook\(")
        self.assertRegex(
            hook,
            re.compile(
                r"isPlayerCommand = Packets::Peds::IsDeliberatePlayerVoiceCommand\(context\).*?"
                r"HasStockPlayerVoiceCommandArguments.*?"
                r"if \(isPlayerCommand\)\s*return result;.*?"
                r"isPlayerCommand && !ShouldRelayPlayerVoiceCommand\(pPed, context\)",
                re.S,
            ),
        )

        model = VoiceCommandDebounceModel()
        self.assertTrue(model.accept(("peer-a", 1), 0xFFFFFFF0, "WAIT"))
        self.assertFalse(model.accept(("peer-a", 1), 0x00000020, "WAIT"))
        self.assertTrue(model.accept(("peer-a", 1), 0x00000021, "FOLLOW"))
        self.assertTrue(model.accept(("peer-b", 2), 0x00000022, "FOLLOW"))
        self.assertFalse(model.accept(("peer-b", 2), 0x00000023, "WAIT", alive=False))

    def test_server_canonicalizes_and_validates_live_player_before_command_relay(self):
        handler = function_body(self.server, r"PACKET_HANDLER\(ePacketType::PED_SAY\s*,")
        canonical = handler.index("pPedSay->entity.entityId = pNetworkPlayer->m_iPlayerId")
        command = handler.index("IsDeliberatePlayerVoiceCommand")
        relay = handler.index("GetPacketFactory().SendToAll(*pPedSay, pNetworkPlayer)")
        self.assertLess(canonical, command)
        self.assertLess(command, relay)
        self.assertIn("HasStockPlayerVoiceCommandArguments", handler)
        self.assertIn("CanRelayPlayerVoiceCommand(pNetworkPlayer)", handler)

        live = function_body(self.server, r"IsAuthenticatedLivePlayer\(")
        for evidence in (
            "player->m_pPeer != nullptr",
            "CNetworkPlayerManager::GetPlayer(player->m_iPlayerId) == player",
            "player->m_bHasOnFootSnapshot",
            "player->m_bIsAlive",
            "player->m_nVehicleId < 0",
        ):
            self.assertIn(evidence, live)

    def test_server_rate_limit_is_command_only_and_safe_for_player_id_reuse(self):
        guard = function_body(self.server, r"CanRelayPlayerVoiceCommand\(")
        for evidence in (
            "slot.owner != player",
            "slot.connectId != connectId",
            "now - slot.windowStartedAt >= PLAYER_VOICE_COMMAND_RATE_WINDOW_MS",
            "slot.commandCount >= MAX_PLAYER_VOICE_COMMANDS_PER_WINDOW",
            "++slot.commandCount",
        ):
            self.assertIn(evidence, guard)
        self.assertIn("PLAYER_VOICE_COMMAND_RATE_WINDOW_MS = 2000", self.server)
        self.assertIn("MAX_PLAYER_VOICE_COMMANDS_PER_WINDOW = 8", self.server)

        handler = function_body(self.server, r"PACKET_HANDLER\(ePacketType::PED_SAY\s*,")
        command_branch = handler[: handler.index("else if (pPedSay->entity.entityType")]
        npc_branch = handler[handler.index("else if (pPedSay->entity.entityType") :]
        self.assertEqual(command_branch.count("CanRelayPlayerVoiceCommand"), 1)
        self.assertNotIn("CanRelayPlayerVoiceCommand", npc_branch)

        model = VoiceCommandRateModel()
        for index in range(8):
            self.assertTrue(model.accept(("peer-a", 1), index, True))
        self.assertFalse(model.accept(("peer-a", 1), 8, True))
        self.assertTrue(model.accept(("peer-a", 1), 9, False))
        self.assertTrue(model.accept(("peer-b", 2), 10, True))

    def test_remote_playback_is_native_spatial_non_recursive_and_lifecycle_safe(self):
        handler = function_body(self.client, r"PACKET_HANDLER\(ePacketType::PED_SAY\s*,")
        for evidence in (
            "pPed == nullptr",
            "!pPed->IsVTableValid()",
            "pPedSay->entity.entityType == NETWORK_ENTITY_TYPE_PLAYER",
            "!pPed->IsPlayer()",
            "!pPed->IsAlive()",
            "HasStockPlayerVoiceCommandArguments",
            "&pPed->m_pedSpeech",
            "plugin::CallMethodAndReturn<int16_t, 0x4E6550",
        ):
            self.assertIn(evidence, handler)
        self.assertNotIn("pPed->Say", handler)
        self.assertNotIn("GetPacketFactory", handler)


if __name__ == "__main__":
    unittest.main()
