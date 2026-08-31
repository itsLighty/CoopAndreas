import math
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
BASE_WIDTH = 640.0
BASE_HEIGHT = 448.0
SAFE_ASPECT = 4.0 / 3.0


def build_transform(width: float, height: float, render_aspect: float):
    """Pure mirror of CUtil::BuildScreenTransform for ratio regression tests."""
    if not all(math.isfinite(value) for value in (width, height)):
        return None
    if width <= 0.0 or height <= 0.0:
        return None

    framebuffer_aspect = width / height
    render_aspect_usable = math.isfinite(render_aspect) and render_aspect > 0.0
    if (
        render_aspect_usable
        and abs(render_aspect - framebuffer_aspect) <= framebuffer_aspect * 0.05
    ):
        current_aspect = render_aspect
    else:
        current_aspect = framebuffer_aspect

    if current_aspect >= SAFE_ASPECT:
        safe_height = height
        safe_width = min(width, height * SAFE_ASPECT)
        safe_left = (width - safe_width) * 0.5
        safe_top = 0.0
    else:
        safe_width = width
        safe_height = min(height, width / SAFE_ASPECT)
        safe_left = 0.0
        safe_top = (height - safe_height) * 0.5

    return {
        "left": safe_left,
        "top": safe_top,
        "width": safe_width,
        "height": safe_height,
        "scale_x": safe_width / BASE_WIDTH,
        "scale_y": safe_height / BASE_HEIGHT,
    }


class WidescreenLayoutMathTests(unittest.TestCase):
    CASES = {
        "4:3": (1600.0, 1200.0, 0.0, 0.0, 1600.0, 1200.0),
        "16:9": (1920.0, 1080.0, 240.0, 0.0, 1440.0, 1080.0),
        "16:10": (1920.0, 1200.0, 160.0, 0.0, 1600.0, 1200.0),
        "21:9": (2560.0, 1080.0, 560.0, 0.0, 1440.0, 1080.0),
        "5:4": (1280.0, 1024.0, 0.0, 32.0, 1280.0, 960.0),
    }

    def test_safe_canvas_ratio_matrix(self):
        for label, (width, height, left, top, safe_width, safe_height) in self.CASES.items():
            with self.subTest(label=label):
                transform = build_transform(width, height, width / height)
                self.assertIsNotNone(transform)
                self.assertAlmostEqual(transform["left"], left)
                self.assertAlmostEqual(transform["top"], top)
                self.assertAlmostEqual(transform["width"], safe_width)
                self.assertAlmostEqual(transform["height"], safe_height)
                self.assertAlmostEqual(
                    transform["width"] / transform["height"], SAFE_ASPECT
                )

    def test_virtual_canvas_center_stays_at_physical_center(self):
        for label, (width, height, *_rest) in self.CASES.items():
            with self.subTest(label=label):
                transform = build_transform(width, height, width / height)
                x = transform["left"] + 320.0 * transform["scale_x"]
                y = transform["top"] + 224.0 * transform["scale_y"]
                self.assertAlmostEqual(x, width / 2.0)
                self.assertAlmostEqual(y, height / 2.0)

    def test_four_by_three_preserves_legacy_hud_scale(self):
        transform = build_transform(1600.0, 1200.0, 4.0 / 3.0)
        self.assertAlmostEqual(transform["scale_x"], 1600.0 / BASE_WIDTH)
        self.assertAlmostEqual(transform["scale_y"], 1200.0 / BASE_HEIGHT)

    def test_invalid_or_stale_render_state_cannot_escape_framebuffer(self):
        self.assertIsNone(build_transform(0.0, 1080.0, 16.0 / 9.0))
        self.assertIsNone(build_transform(float("nan"), 1080.0, 16.0 / 9.0))
        transform = build_transform(1280.0, 1024.0, 16.0 / 9.0)
        self.assertGreaterEqual(transform["left"], 0.0)
        self.assertGreaterEqual(transform["top"], 0.0)
        self.assertLessEqual(transform["left"] + transform["width"], 1280.0)
        self.assertLessEqual(transform["top"] + transform["height"], 1024.0)


class WidescreenLayoutStructureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.util_header = (ROOT / "client/src/CUtil.h").read_text(encoding="utf-8")
        cls.util_source = (ROOT / "client/src/CUtil.cpp").read_text(encoding="utf-8")
        cls.ui_sources = {
            path.name: path.read_text(encoding="utf-8")
            for path in (ROOT / "client/src/UI").glob("*.cpp")
        }

    def test_live_transform_reads_render_state_without_startup_cache(self):
        self.assertIn("CScreenTransform CUtil::GetScreenTransform()", self.util_source)
        self.assertIn("RsGlobal.maximumWidth", self.util_source)
        self.assertIn("RsGlobal.maximumHeight", self.util_source)
        self.assertIn("CDraw::ms_fAspectRatio", self.util_source)
        self.assertNotIn("static CScreenTransform", self.util_source)

    def test_all_custom_overlays_use_the_shared_transform(self):
        for filename in (
            "CChat.cpp",
            "CDXFont.cpp",
            "CNetworkPlayerList.cpp",
            "CNetworkPlayerMapPin.cpp",
            "CNetworkPlayerNameTag.cpp",
            "CNetworkPlayerWaypoint.cpp",
        ):
            with self.subTest(filename=filename):
                self.assertIn("CUtil::GetScreenTransform()", self.ui_sources[filename])

    def test_old_ad_hoc_position_scaling_is_removed(self):
        combined = "\n".join(self.ui_sources.values())
        self.assertNotIn("#define PROPORION_X", combined)
        self.assertNotIn("#define PROPORION_Y", combined)
        self.assertNotIn("maximumHeight / 360", combined)
        self.assertNotIn("CUtil::HUD_X(screen.x)", combined)
        self.assertNotIn("CUtil::HUD_Y(screen.y)", combined)
        for filename, source in self.ui_sources.items():
            with self.subTest(filename=filename):
                self.assertNotIn("RsGlobal.maximumWidth", source)
                self.assertNotIn("RsGlobal.maximumHeight", source)

    def test_font_and_render_globals_are_restored(self):
        player_list = self.ui_sources["CNetworkPlayerList.cpp"]
        name_tag = self.ui_sources["CNetworkPlayerNameTag.cpp"]
        map_pin = self.ui_sources["CNetworkPlayerMapPin.cpp"]
        self.assertIn("class ScopedFontState", player_list)
        self.assertIn("class ScopedRenderState", player_list)
        self.assertIn("class ScopedFontState", name_tag)
        self.assertIn("class ScopedRenderState", name_tag)
        self.assertNotIn("CWorld::PlayerInFocus =", map_pin)

    def test_dynamic_font_tracks_live_safe_canvas(self):
        font = self.ui_sources["CDXFont.cpp"]
        self.assertIn("EnsureFontForCurrentLayout", font)
        self.assertIn("transform.safeWidth", font)
        self.assertIn("transform.safeHeight", font)
        self.assertIn("transform.Right()", font)
        self.assertIn("transform.Bottom()", font)


if __name__ == "__main__":
    unittest.main()
