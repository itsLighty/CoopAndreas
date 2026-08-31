import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class PlaytestDistributionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.updater = (ROOT / "playtest_launcher/src/main.cpp").read_text(encoding="utf-8")
        cls.launcher = (ROOT / "launcher/src/main.cpp").read_text(encoding="utf-8")
        cls.client_launch = (ROOT / "client/src/CLaunchManager.cpp").read_text(encoding="utf-8")
        cls.workflow = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
        cls.stage = (ROOT / "scripts/stage-playtest.ps1").read_text(encoding="utf-8")
        cls.xmake = (ROOT / "xmake.lua").read_text(encoding="utf-8")

    def test_launcher_and_client_are_key_free(self):
        combined = self.launcher + self.client_launch
        self.assertNotIn("-serial", combined)
        self.assertNotIn("Encrypt(", combined)
        self.assertIn('"\\\"gta_sa.exe\\\" --coop"', self.launcher)

    def test_updater_targets_public_rolling_release(self):
        self.assertIn(
            "https://github.com/itsLighty/CoopAndreas/releases/download/playtest-latest/",
            self.updater,
        )
        for asset in (
            "eax.dll",
            "CoopAndreasSA.dll",
            "LaunchCoopAndreas.exe",
            "LaunchCoopAndreas.exe.manifest",
            "server.exe",
            "main.scm",
            "script.img",
        ):
            self.assertIn(f'L"{asset}"', self.updater)

    def test_updater_preserves_original_loader_and_requires_game_to_be_closed(self):
        self.assertIn('MoveFileExW(eax.c_str(), original.c_str()', self.updater)
        self.assertIn('L"eax_orig.dll"', self.updater)
        self.assertIn('IsProcessRunning(L"gta_sa.exe")', self.updater)

    def test_updater_supports_host_join_and_update_only(self):
        self.assertIn("Host & Play", self.updater)
        self.assertIn("Join & Play", self.updater)
        self.assertIn("Update only", self.updater)
        self.assertIn('Launch(JoinPath(gameDirectory, L"server.exe")', self.updater)
        self.assertIn('Launch(JoinPath(gameDirectory, L"LaunchCoopAndreas.exe")', self.updater)

    def test_build_and_stage_include_updater(self):
        self.assertIn('target("playtest_launcher"', self.xmake)
        self.assertIn("CoopAndreasPlaytest.exe", self.stage)
        self.assertIn("CoopAndreas-playtest.zip", self.stage)
        self.assertIn("SHA256SUMS.txt", self.stage)

    def test_ci_uploads_artifact_and_publishes_main(self):
        self.assertIn("actions/upload-artifact@v4", self.workflow)
        self.assertIn("gh release create playtest-latest", self.workflow)
        self.assertIn("github.ref == 'refs/heads/main'", self.workflow)
        self.assertIn("github.repository == 'itsLighty/CoopAndreas'", self.workflow)


if __name__ == "__main__":
    unittest.main()
