import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class PlaytestDistributionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.installer = (ROOT / "playtest_launcher/src/main.cpp").read_text(encoding="utf-8")
        cls.resources = (ROOT / "playtest_launcher/assets.rc").read_text(encoding="utf-8")
        cls.launcher = (ROOT / "launcher/src/main.cpp").read_text(encoding="utf-8")
        cls.client_launch = (ROOT / "client/src/CLaunchManager.cpp").read_text(encoding="utf-8")
        cls.build_script = (ROOT / "scripts/build-standalone-playtest.ps1").read_text(encoding="utf-8")
        cls.xmake = (ROOT / "xmake.lua").read_text(encoding="utf-8")

    def test_launcher_and_client_are_key_free(self):
        combined = self.launcher + self.client_launch
        self.assertNotIn("-serial", combined)
        self.assertNotIn("Encrypt(", combined)
        self.assertIn('"\\\"gta_sa.exe\\\" --coop"', self.launcher)

    def test_installer_embeds_every_runtime_asset(self):
        for asset in (
            "proxy.dll",
            "CoopAndreasSA.dll",
            "LaunchCoopAndreas.exe",
            "LaunchCoopAndreas.exe.manifest",
            "server.exe",
            "main.scm",
            "script.img",
        ):
            self.assertIn(asset, self.resources)
        self.assertIn("FindResourceW", self.installer)
        self.assertNotIn("URLDownloadToFileW", self.installer)

    def test_installer_preserves_original_loader_and_requires_game_to_be_closed(self):
        self.assertIn("MoveFileExW(eax.c_str(), original.c_str()", self.installer)
        self.assertIn('L"eax_orig.dll"', self.installer)
        self.assertIn('IsProcessRunning(L"gta_sa.exe")', self.installer)

    def test_installer_supports_host_join_and_install_only(self):
        self.assertIn("Host & Play", self.installer)
        self.assertIn("Join & Play", self.installer)
        self.assertIn("Install only", self.installer)
        self.assertIn("--install-only", self.installer)
        self.assertIn('Launch(JoinPath(gameDirectory, L"server.exe")', self.installer)
        self.assertIn('Launch(JoinPath(gameDirectory, L"LaunchCoopAndreas.exe")', self.installer)

    def test_local_build_produces_one_standalone_executable(self):
        self.assertIn('target("playtest_launcher"', self.xmake)
        self.assertIn('add_files("playtest_launcher/assets.rc")', self.xmake)
        self.assertIn("CoopAndreasPlaytest.exe", self.build_script)
        self.assertIn("validate-scm.ps1", self.build_script)


if __name__ == "__main__":
    unittest.main()
