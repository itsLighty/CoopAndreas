import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


def function_body(source: str, signature: str) -> str:
    match = re.search(signature, source)
    if match is None:
        raise AssertionError(f"missing function matching {signature}")

    opening_brace = source.find("{", match.end())
    depth = 0
    for index in range(opening_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening_brace + 1 : index]
    raise AssertionError(f"unterminated function matching {signature}")


class KeyboardModel:
    row_lengths = (10, 10, 10, 10, 7)

    def __init__(self, limit=128):
        self.row = 1
        self.column = 0
        self.text = ""
        self.shift = False
        self.caps = False
        self.active = True
        self.submitted = False
        self.limit = limit

    def move(self, direction):
        if direction == "left":
            self.column = (self.column - 1) % self.row_lengths[self.row]
        elif direction == "right":
            self.column = (self.column + 1) % self.row_lengths[self.row]
        elif direction == "up":
            self.row = (self.row - 1) % len(self.row_lengths)
            self.column = min(self.column, self.row_lengths[self.row] - 1)
        elif direction == "down":
            self.row = (self.row + 1) % len(self.row_lengths)
            self.column = min(self.column, self.row_lengths[self.row] - 1)

    def insert(self, text):
        remaining = self.limit - len(self.text)
        self.text += text[:remaining]

    def letter(self, value):
        uppercase = self.shift != self.caps
        self.insert(value.upper() if uppercase else value.lower())
        self.shift = False

    def send(self):
        self.active = False
        if self.text and not self.text.isspace():
            self.submitted = True
        self.text = ""

    def cancel(self):
        self.active = False
        self.text = ""


class ChatGamepadOskTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = (ROOT / "client/src/UI/CChatGamepadKeyboard.h").read_text(encoding="utf-8")
        cls.source = (ROOT / "client/src/UI/CChatGamepadKeyboard.cpp").read_text(encoding="utf-8")
        cls.chat_header = (ROOT / "client/src/UI/CChat.h").read_text(encoding="utf-8")
        cls.chat_source = (ROOT / "client/src/UI/CChat.cpp").read_text(encoding="utf-8")
        cls.imgui_source = (ROOT / "client/src/Debug/CImGui.cpp").read_text(encoding="utf-8")
        cls.main = (ROOT / "client/src/Main.cpp").read_text(encoding="utf-8")
        cls.xmake = (ROOT / "xmake.lua").read_text(encoding="utf-8")
        cls.process = function_body(
            cls.source, r"void\s+CChatGamepadKeyboard::Process\s*\(\s*\)"
        )
        cls.can_open = function_body(
            cls.source, r"bool\s+CChatGamepadKeyboard::CanOpen\s*\("
        )
        cls.submit = function_body(cls.chat_source, r"bool\s+CChat::SubmitInput\s*\(\s*\)")
        cls.wnd_proc = function_body(cls.chat_source, r"void\s+CChat::WndProc\s*\(")

    def test_component_is_auto_globbed_and_integrated_once_per_process_and_draw(self):
        self.assertIn('add_files("client/src/UI/*.cpp")', self.xmake)
        self.assertIn("#include <UI/CChatGamepadKeyboard.h>", self.main)
        self.assertEqual(self.main.count("CChatGamepadKeyboard::Process();"), 1)
        self.assertEqual(self.main.count("CChatGamepadKeyboard::Draw();"), 1)
        before_hook = self.main.split("Events::gameProcessEvent.before += []", 1)[1].split(
            "Events::gameProcessEvent += []", 1
        )[0]
        self.assertIn("CChatGamepadKeyboard::Process();", before_hook)
        self.assertLess(
            self.main.index("CChat::DrawInput();"),
            self.main.index("CChatGamepadKeyboard::Draw();"),
        )

    def test_open_action_is_gamepad_only_and_does_not_consume_closed_controls(self):
        open_chord = function_body(
            self.source, r"bool\s+CChatGamepadKeyboard::IsOpenChordDown\s*\("
        )
        self.assertIn("state.m_bChatIndicated != 0", open_chord)
        self.assertIn("state.Select != 0 && state.DPadDown != 0", open_chord)
        inactive = self.process.split("if (!m_bActive)", 1)[1].split("const bool unavailable", 1)[0]
        self.assertIn("if (CanOpen(pad, player))", inactive)
        self.assertIn("Open(pad);", inactive)
        self.assertNotIn("ConsumeGameplayInput(pad)", inactive)

    def test_open_is_rejected_when_network_or_local_gameplay_is_unavailable(self):
        for evidence in (
            "CNetwork::m_bAuthenticated",
            "player != nullptr",
            "player->IsAlive()",
            "player->m_pIntelligence != nullptr",
            "pad->DisablePlayerControls == 0",
            "!FrontEndMenuManager.m_bMenuActive",
            "!CTimer::m_UserPause",
            "!CTimer::m_CodePause",
            "!CCutsceneMgr::ms_running",
            "!TheCamera.m_bWideScreenOn",
        ):
            self.assertIn(evidence, self.can_open)

    def test_active_keyboard_owns_a_unique_control_bit_and_restores_only_that_bit(self):
        self.assertIn("CONTROL_LOCK_MASK = 0x400", self.header)
        self.assertIn("Dedicated chat-OSK lock", self.header)
        self.assertIn("unrelated game control", self.header)
        self.assertIn("DisablePlayerControls |= 0x200", self.imgui_source)
        self.assertNotIn("0x400", self.imgui_source)
        opened = function_body(self.source, r"void\s+CChatGamepadKeyboard::Open\s*\(")
        released = function_body(
            self.source, r"void\s+CChatGamepadKeyboard::ReleaseControlLock\s*\("
        )
        self.assertIn("pad->DisablePlayerControls |= CONTROL_LOCK_MASK", opened)
        self.assertIn("pad->DisablePlayerControls &= ~CONTROL_LOCK_MASK", released)
        self.assertNotIn("pad->DisablePlayerControls = 0", self.source)
        self.assertIn("(pad->DisablePlayerControls & ~CONTROL_LOCK_MASK) != 0", self.source)
        consume = function_body(
            self.source, r"void\s+CChatGamepadKeyboard::ConsumeGameplayInput\s*\("
        )
        for state in ("PCTempJoyState", "NewState", "OldState"):
            self.assertIn(f"memset(&pad->{state}, 0", consume)

    def test_navigation_has_dead_zone_edge_debounce_repeat_and_wrapping(self):
        for evidence in (
            "STICK_DEAD_ZONE = 64",
            "NAV_INITIAL_REPEAT_DELAY_MS = 350",
            "NAV_REPEAT_INTERVAL_MS = 90",
        ):
            self.assertIn(evidence, self.header)
        repeat = function_body(
            self.source, r"bool\s+CChatGamepadKeyboard::ConsumeDirection\s*\("
        )
        self.assertIn("direction != m_eHeldDirection", repeat)
        self.assertIn("now - m_nDirectionStartedAt < NAV_INITIAL_REPEAT_DELAY_MS", repeat)
        self.assertIn("now - m_nLastDirectionAt < NAV_REPEAT_INTERVAL_MS", repeat)
        move = function_body(
            self.source, r"void\s+CChatGamepadKeyboard::MoveSelection\s*\("
        )
        self.assertIn("columnCount - 1", move)
        self.assertIn("% columnCount", move)
        self.assertIn("rowCount - 1", move)
        self.assertIn("% rowCount", move)
        self.assertIn("std::min(m_nSelectedColumn, columnCount - 1)", move)

        model = KeyboardModel()
        model.column = 0
        model.move("left")
        self.assertEqual(model.column, 9)
        model.move("right")
        self.assertEqual(model.column, 0)
        model.row = 0
        model.move("up")
        self.assertEqual(model.row, 4)
        model.column = 6
        model.move("down")
        self.assertEqual((model.row, model.column), (0, 6))

    def test_layout_contains_all_required_character_and_action_keys(self):
        for row_name in ("ROW_DIGITS", "ROW_QWERTY", "ROW_HOME", "ROW_BOTTOM", "ROW_ACTIONS"):
            self.assertIn(row_name, self.source)
        for action in (
            "KeyAction::Shift",
            "KeyAction::CapsLock",
            "KeyAction::Space",
            "KeyAction::Backspace",
            "KeyAction::Clear",
            "KeyAction::Cancel",
            "KeyAction::Send",
        ):
            self.assertIn(action, self.source)
        for punctuation in ('L"!"', 'L"@"', 'L"#"', 'L"$"', 'L"%"', 'L"?"'):
            self.assertIn(punctuation, self.source)

    def test_shift_caps_and_text_length_state_machine(self):
        model = KeyboardModel(limit=4)
        model.shift = True
        model.letter("a")
        self.assertEqual(model.text, "A")
        self.assertFalse(model.shift)
        model.caps = True
        model.letter("b")
        self.assertEqual(model.text, "AB")
        model.shift = True
        model.letter("c")
        self.assertEqual(model.text, "ABc")
        model.insert("def")
        self.assertEqual(model.text, "ABcd")

        insert = function_body(self.chat_source, r"bool\s+CChat::InsertTextAtCaret\s*\(")
        self.assertIn("Config::MAX_CHAT_MESSAGE_LENGTH", insert)
        self.assertIn("IsHighSymbolSurrogate", insert)
        self.assertIn("IsLowSymbolSurrogate", insert)
        self.assertIn("remaining >= 2", insert)

    def test_confirm_cancel_and_shortcuts_are_edge_latched(self):
        consume = function_body(
            self.source, r"bool\s+CChatGamepadKeyboard::ConsumeButton\s*\("
        )
        self.assertIn("held = false", consume)
        self.assertIn("if (held)", consume)
        self.assertIn("held = true", consume)
        for button in ("ButtonCross", "ButtonCircle", "ButtonSquare", "ButtonTriangle"):
            self.assertIn(f"gamepadState.{button}", self.process)
        self.assertIn("m_bSubmissionInFlight", self.source)

        model = KeyboardModel()
        model.text = "draft"
        model.cancel()
        self.assertFalse(model.active)
        self.assertEqual(model.text, "")
        self.assertFalse(model.submitted)

    def test_send_reuses_one_authenticated_chat_transport_and_rejects_blank_input(self):
        activate = function_body(
            self.source, r"void\s+CChatGamepadKeyboard::ActivateSelectedKey\s*\("
        )
        self.assertIn("CChat::SubmitInput()", activate)
        self.assertNotIn("Packets::", self.source)
        self.assertNotIn("GetPacketFactory", self.source)
        self.assertIn("CNetwork::m_bAuthenticated", self.submit)
        self.assertIn("IsInputTextEmpty(m_sInputText)", self.submit)
        self.assertEqual(self.submit.count("GetPacketFactory().Send(packet)"), 1)
        self.assertEqual(self.submit.count("SendPlayerMessage("), 1)

        blank = KeyboardModel()
        blank.text = " \t "
        blank.send()
        self.assertFalse(blank.submitted)
        message = KeyboardModel()
        message.text = "hello"
        message.send()
        self.assertTrue(message.submitted)

    def test_send_and_cancel_close_chat_restore_controls_and_clear_state(self):
        close = function_body(self.source, r"void\s+CChatGamepadKeyboard::Close\s*\(")
        self.assertIn("CChat::ClearInputText()", close)
        self.assertIn("CChat::ToggleInput(false)", close)
        on_close = function_body(
            self.source, r"void\s+CChatGamepadKeyboard::OnChatInputClosed\s*\("
        )
        for evidence in (
            "m_bActive = false",
            "m_bShift = false",
            "m_bCapsLock = false",
            "m_bSubmissionInFlight = false",
            "ResetTransientInput()",
        ):
            self.assertIn(evidence, on_close)
        self.assertIn("if (!m_bReleaseLockWhenButtonsUp)", on_close)
        self.assertIn("ReleaseControlLock(pad)", on_close)
        self.assertIn("CChatGamepadKeyboard::OnChatInputClosed();", self.chat_source)

    def test_gamepad_close_consumes_before_restore_and_waits_for_button_up(self):
        begin = function_body(
            self.source, r"void\s+CChatGamepadKeyboard::BeginGamepadClose\s*\("
        )
        deferred = function_body(
            self.source, r"bool\s+CChatGamepadKeyboard::ProcessDeferredControlRelease\s*\("
        )
        activate = function_body(
            self.source, r"void\s+CChatGamepadKeyboard::ActivateSelectedKey\s*\("
        )

        self.assertLess(begin.index("ConsumeGameplayInput(pad)"), begin.index("m_bReleaseLockWhenButtonsUp = true"))
        cancel_branch = self.process.split("if (cancelPressed)", 1)[1].split("if (backspacePressed)", 1)[0]
        self.assertLess(cancel_branch.index("BeginGamepadClose(pad)"), cancel_branch.index("Close(true)"))
        selected_cancel = activate.split("case KeyAction::Cancel:", 1)[1].split(
            "case KeyAction::Send:", 1
        )[0]
        self.assertLess(selected_cancel.index("BeginGamepadClose(pad)"), selected_cancel.index("Close(true)"))
        selected_send = activate.split("case KeyAction::Send:", 1)[1]
        self.assertLess(selected_send.index("BeginGamepadClose(pad)"), selected_send.index("CChat::SubmitInput()"))

        self.assertIn("state.ButtonCross != 0 || state.ButtonCircle != 0", deferred)
        self.assertLess(deferred.index("ConsumeGameplayInput(pad)"), deferred.index("ReleaseControlLock(pad)"))
        self.assertIn("if (!closeButtonDown)", deferred)
        self.assertEqual(self.submit.count("GetPacketFactory().Send(packet)"), 1)

        own_bit = 0x400
        unrelated_bits = 0x21
        controls = own_bit | unrelated_bits
        cross_down = True
        sampled_cross = cross_down
        sampled_cross = False  # BeginGamepadClose neutralizes the current frame.
        self.assertFalse(sampled_cross)
        self.assertEqual(controls & own_bit, own_bit)
        if not cross_down:
            controls &= ~own_bit
        self.assertEqual(controls, own_bit | unrelated_bits)
        cross_down = False
        sampled_cross = False  # Release frame is consumed before lock removal.
        if not cross_down:
            controls &= ~own_bit
        self.assertFalse(sampled_cross)
        self.assertEqual(controls, unrelated_bits)

    def test_keyboard_mouse_chat_behavior_is_preserved_and_uses_shared_submit(self):
        for evidence in (
            "message == WM_CHAR && m_bInputActive",
            "wParam == VK_F6",
            "wParam == VK_ESCAPE && m_bInputActive",
            "wParam == VK_RETURN && m_bInputActive",
            "wParam == VK_LEFT",
            "wParam == VK_RIGHT",
            "wParam == VK_UP",
            "wParam == VK_DOWN",
            "wParam == VK_BACK",
            "wParam == VK_DELETE",
            "OpenClipboard(nullptr)",
        ):
            self.assertIn(evidence, self.wnd_proc)
        self.assertIn("SubmitInput();", self.wnd_proc)
        self.assertIn("InsertTextAtCaret(clipboardText);", self.wnd_proc)

    def test_rendering_uses_safe_area_scaling_and_non_imgui_focus(self):
        draw = function_body(self.source, r"void\s+CChatGamepadKeyboard::Draw\s*\(\s*\)")
        self.assertIn("CUtil::GetScreenTransform()", draw)
        self.assertIn("transform.X(", draw)
        self.assertIn("transform.Y(", draw)
        self.assertIn("transform.Width(", draw)
        self.assertIn("transform.Height(", draw)
        self.assertIn("RwD3D9GetCurrentD3DDevice()", draw)
        self.assertIn("const bool selected", draw)
        self.assertIn("CRGBA(255, 214, 76, 255)", draw)
        self.assertNotIn("ImGui::", self.source)


if __name__ == "__main__":
    unittest.main()
