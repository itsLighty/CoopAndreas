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


class PassengerEntryInputTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = (ROOT / "client/src/CPassengerEnter.h").read_text(encoding="utf-8")
        cls.source = (ROOT / "client/src/CPassengerEnter.cpp").read_text(encoding="utf-8")
        cls.consume = function_body(
            cls.source, r"CPassengerEnter::InputSource\s+CPassengerEnter::ConsumePassengerAction\s*\("
        )
        cls.process = function_body(cls.source, r"void\s+CPassengerEnter::Process\s*\(\s*\)")

    def test_semantic_action_combines_keyboard_and_gamepad_with_one_latch(self):
        self.assertIn("keyboardDown || gamepadDPadUp", self.consume)
        self.assertIn("actionHeld = false", self.consume)
        self.assertIn("if (actionHeld)", self.consume)
        self.assertIn("actionHeld = true", self.consume)
        self.assertIn("InputSource::Gamepad : InputSource::Keyboard", self.consume)

        def consume(keyboard_down, gamepad_down, held):
            action_down = keyboard_down or gamepad_down
            if not action_down:
                return "none", False
            if held:
                return "none", True
            return ("gamepad" if gamepad_down else "keyboard"), True

        source, held = consume(True, True, False)
        self.assertEqual((source, held), ("gamepad", True))
        self.assertEqual(consume(True, True, held), ("none", True))
        self.assertEqual(consume(False, False, held), ("none", False))
        self.assertEqual(consume(True, False, False), ("keyboard", True))
        self.assertEqual(consume(False, True, False), ("gamepad", True))

    def test_process_reads_separate_keyboard_and_gamepad_semantics_once(self):
        self.assertIn("pPad->PCTempKeyState.DPadUp != 0", self.process)
        self.assertIn("pPad->PCTempJoyState.DPadUp != 0", self.process)
        self.assertEqual(self.process.count("ConsumePassengerAction("), 1)
        self.assertNotIn("OldState.DPadUp", self.process)
        self.assertNotIn("NewState.DPadUp", self.process)
        self.assertNotIn("ButtonTriangle", self.process)

    def test_prompt_describes_both_supported_devices(self):
        self.assertIn("Press G or D-pad Up", self.source)
        self.assertNotIn('"Press G to enter', self.source)

    def test_dead_players_and_unavailable_vehicles_are_rejected(self):
        for evidence in (
            "pPlayerPed == nullptr",
            "pPad == nullptr",
            "!pPlayerPed->IsAlive()",
            "pPlayerPed->m_pIntelligence == nullptr",
            "pNetworkVehicle == nullptr",
            "CPools::ms_pVehiclePool != nullptr",
            "CPools::ms_pVehiclePool->IsObjectValid(pVehicle)",
            "pVehicle->m_matrix != nullptr",
            "pVehicle->m_fHealth > 0.0f",
            "pVehicle->m_nStatus != STATUS_WRECKED",
            "!pVehicle->m_nPhysicalFlags.bDestroyed",
            "pVehicle->m_nNumPassengers < pVehicle->m_nMaxPassengers",
        ):
            self.assertIn(evidence, self.source)

    def test_selected_passenger_seat_is_validated_and_reused_for_packet(self):
        self.assertIn("ComputePassengerIndexFromCarDoor(pVehicle, doorId)", self.source)
        self.assertIn("seatId >= 0", self.source)
        self.assertIn("seatId < pVehicle->m_nMaxPassengers", self.source)
        self.assertIn("pVehicle->m_apPassengers[seatId] == nullptr", self.source)
        self.assertIn("new CTaskComplexEnterCarAsPassenger(pVehicle, passengerSeat.doorId, false)", self.process)
        self.assertIn("packet.seatid = passengerSeat.seatId", self.process)
        self.assertIn("packet.bPassenger = true", self.process)
        self.assertEqual(self.process.count("GetPacketFactory().Send(packet)"), 1)


if __name__ == "__main__":
    unittest.main()
