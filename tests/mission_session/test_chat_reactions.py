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


class ReactionModel:
    commands = {
        "/react good": "goodcha",
        "/r good": "goodcha",
        "/react bad": "badchat",
        "/r bad": "badchat",
        "/react up": "thumbup",
        "/r up": "thumbup",
        "/react down": "thumbdn",
        "/r down": "thumbdn",
    }

    def __init__(self, player_count=8, limit_ms=1200, queue_limit=6):
        self.player_count = player_count
        self.limit_ms = limit_ms
        self.queue_limit = queue_limit
        self.last = [None] * player_count
        self.queue = []

    def receive(self, player_id, message, now):
        texture = self.commands.get(message)
        if texture is None or not 0 <= player_id < self.player_count:
            return False, False
        previous = self.last[player_id]
        if previous is not None and ((now - previous) & 0xFFFFFFFF) < self.limit_ms:
            return True, False
        self.last[player_id] = now
        self.queue.append(texture)
        self.queue = self.queue[-self.queue_limit :]
        return True, True


class ChatReactionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = (ROOT / "client/src/UI/CChatReactions.h").read_text(encoding="utf-8")
        cls.source = (ROOT / "client/src/UI/CChatReactions.cpp").read_text(encoding="utf-8")
        cls.chat = (ROOT / "client/src/UI/CChat.cpp").read_text(encoding="utf-8")
        cls.main = (ROOT / "client/src/Main.cpp").read_text(encoding="utf-8")
        cls.gamepad = (ROOT / "client/src/UI/CChatGamepadKeyboard.cpp").read_text(encoding="utf-8")
        cls.xmake = (ROOT / "xmake.lua").read_text(encoding="utf-8")

    def test_exact_ld_chat_reaction_assets_and_commands(self):
        expected = {
            "/react good": "goodcha",
            "/react bad": "badchat",
            "/react up": "thumbup",
            "/react down": "thumbdn",
        }
        for command, texture in expected.items():
            self.assertIn(f'L"{command}"', self.source)
            self.assertIn(f'"{texture}"', self.source)
            self.assertIn(f'L"{command.replace("/react", "/r")}"', self.source)
        self.assertNotIn('"dpad_64"', self.source)
        self.assertNotIn('"dpad_lr"', self.source)
        self.assertIn('"models\\\\txd\\\\LD_CHAT.txd"', self.source)
        self.assertIn("RwTexDictionaryFindNamedTexture", self.source)

    def test_parser_is_bounded_exact_and_does_not_create_a_protocol(self):
        parser = function_body(self.source, r"FindReactionDefinition\s*\(")
        self.assertIn("length <= Config::MAX_CHAT_MESSAGE_LENGTH", parser)
        self.assertIn("length > Config::MAX_CHAT_MESSAGE_LENGTH", parser)
        self.assertIn("command == definition.command || command == definition.shortCommand", parser)
        for malformed in ("/react", "/react UP", "/react up ", " /react up", "/react unknown"):
            self.assertNotIn(malformed, ReactionModel.commands)
        self.assertNotIn("Packets::", self.source)
        self.assertNotIn("GetPacketFactory", self.source)
        submit = function_body(self.chat, r"bool\s+CChat::SubmitInput\s*\(")
        self.assertEqual(submit.count("GetPacketFactory().Send(packet)"), 1)
        self.assertEqual(submit.count("SendPlayerMessage("), 1)

    def test_per_sender_rate_limit_queue_bound_and_wrap_safe_math(self):
        self.assertIn("REACTION_RATE_LIMIT_MS = 1200", self.header)
        self.assertIn("MAX_QUEUED_REACTIONS = 6", self.header)
        self.assertIn("MAX_VISIBLE_REACTIONS = 4", self.header)
        handler = function_body(
            self.source, r"CChatReactions::CommandResult\s+CChatReactions::HandleAuthenticatedMessage\s*\("
        )
        self.assertIn("playerId < 0 || playerId >= Config::MAX_SERVER_PLAYERS", handler)
        self.assertIn("now - rateLimit.lastAcceptedAt < REACTION_RATE_LIMIT_MS", handler)
        self.assertIn("m_aToasts.pop_front()", handler)

        model = ReactionModel()
        self.assertEqual(model.receive(2, "/r up", 100), (True, True))
        self.assertEqual(model.receive(2, "/r down", 1299), (True, False))
        self.assertEqual(model.receive(3, "/r down", 1299), (True, True))
        self.assertEqual(model.receive(2, "/r down", 1300), (True, True))
        self.assertEqual(model.receive(8, "/r up", 5000), (False, False))
        for index in range(10):
            model.receive(index % 8, "/react good", 10000 + index * 1300)
        self.assertLessEqual(len(model.queue), 6)

        wrap = ReactionModel()
        self.assertEqual(wrap.receive(1, "/r good", 0xFFFFFF00), (True, True))
        self.assertEqual(wrap.receive(1, "/r bad", 0x00000050), (True, False))

    def test_authenticated_session_reset_and_texture_teardown(self):
        ensure = function_body(
            self.source, r"bool\s+CChatReactions::EnsureAuthenticatedSession\s*\("
        )
        reset = function_body(self.source, r"void\s+CChatReactions::Reset\s*\(")
        release = function_body(self.source, r"void\s+CChatReactions::ReleaseTextures\s*\(")
        self.assertIn("!CNetwork::m_bAuthenticated", ensure)
        self.assertIn("Reset();", ensure)
        self.assertIn("m_aToasts.clear()", reset)
        self.assertIn("m_aSenderRateLimits.fill(SenderRateLimit{})", reset)
        self.assertIn("RwTexDictionaryDestroy", release)
        self.assertIn("m_pTextureDictionary = nullptr", release)
        self.assertIn("CChatReactions::Draw();", function_body(self.chat, r"void\s+CChat::Draw\s*\("))

    def test_resolution_safe_rendering_and_bounded_fade(self):
        draw = function_body(self.source, r"void\s+CChatReactions::Draw\s*\(")
        for evidence in (
            "CUtil::GetScreenTransform()",
            "transform.valid",
            "transform.X(",
            "transform.Y(",
            "MAX_VISIBLE_REACTIONS",
            "REACTION_VISIBLE_TIME_MS",
            "REACTION_FADE_TIME_MS",
            "RwD3D9GetCurrentD3DDevice()",
        ):
            self.assertIn(evidence, draw)
        self.assertIn('add_files("client/src/UI/*.cpp")', self.xmake)

    def test_reaction_pass_restores_renderware_state(self):
        guard = function_body(self.source, r"class\s+ScopedReactionRenderState")
        for state in (
            "rwRENDERSTATETEXTUREFILTER",
            "rwRENDERSTATEZTESTENABLE",
            "rwRENDERSTATEZWRITEENABLE",
            "rwRENDERSTATETEXTURERASTER",
        ):
            self.assertIn(state, guard)
        self.assertIn("plugin::GetRenderState", guard)
        self.assertIn("plugin::GetRenderRaster", guard)
        self.assertIn("plugin::SetRenderState", guard)
        self.assertIn("plugin::SetRenderRaster(raster)", guard)

        draw = function_body(self.source, r"void\s+CChatReactions::Draw\s*\(")
        self.assertEqual(draw.count("ScopedReactionRenderState renderState;"), 1)
        self.assertIn("plugin::SetRenderState(rwRENDERSTATETEXTUREFILTER, rwFILTERLINEAR)", draw)
        self.assertIn("plugin::SetRenderState(rwRENDERSTATEZTESTENABLE, FALSE)", draw)
        self.assertIn("plugin::SetRenderState(rwRENDERSTATEZWRITEENABLE, FALSE)", draw)
        self.assertIn("plugin::SetRenderRaster(RwTextureGetRaster(texture))", draw)
        self.assertNotIn("RwRenderStateSet", draw)

    def test_ordinary_keyboard_and_gamepad_paths_are_reused_not_reimplemented(self):
        send = function_body(self.chat, r"void\s+CChat::SendPlayerMessage\s*\(")
        self.assertIn("HandleAuthenticatedMessage", send)
        self.assertIn("if (reaction.recognized)", send)
        self.assertIn("reacted: %s", send)
        self.assertIn("CChat::AddMessage(true", send)
        self.assertNotIn("CChatReactions", self.main)
        self.assertNotIn("CChatReactions", self.gamepad)
        self.assertIn("CChat::SubmitInput()", self.gamepad)
        for command in ReactionModel.commands:
            recognized, accepted = ReactionModel().receive(0, command, 1)
            self.assertTrue(recognized)
            self.assertTrue(accepted)


if __name__ == "__main__":
    unittest.main()
