import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class MapPinLayoutTests(unittest.TestCase):
    def test_map_pin_uses_aspect_safe_scaling_and_restores_focus(self):
        source = (
            ROOT / "client/src/UI/CNetworkPlayerMapPin.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("const auto previousPlayerInFocus = CWorld::PlayerInFocus;", source)
        self.assertIn("player == nullptr || player->m_pPed == nullptr", source)
        self.assertIn("CUtil::SCREEN_SCALE_X(5.0f)", source)
        self.assertIn("CUtil::SCREEN_SCALE_Y(5.0f)", source)
        self.assertIn("CWorld::PlayerInFocus = previousPlayerInFocus;", source)
        self.assertNotIn("maximumHeight / 360", source)


if __name__ == "__main__":
    unittest.main()
