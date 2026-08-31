import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class PlaytestDistributionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.updater = (ROOT / "playtest_launcher/src/main.cpp").read_text(encoding="utf-8")
        cls.resources = (ROOT / "playtest_launcher/assets.rc").read_text(encoding="utf-8")
        cls.launcher = (ROOT / "launcher/src/main.cpp").read_text(encoding="utf-8")
        cls.client_launch = (ROOT / "client/src/CLaunchManager.cpp").read_text(encoding="utf-8")
        cls.workflow = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
        cls.stage = (ROOT / "scripts/stage-playtest.ps1").read_text(encoding="utf-8")
        cls.build_updater = (ROOT / "scripts/build-playtest-updater.ps1").read_text(encoding="utf-8")
        cls.build_compatibility = (ROOT / "scripts/build-standalone-playtest.ps1").read_text(encoding="utf-8")
        cls.xmake = (ROOT / "xmake.lua").read_text(encoding="utf-8")

    def test_launcher_and_client_are_key_free(self):
        combined = self.launcher + self.client_launch
        self.assertNotIn("-serial", combined)
        self.assertNotIn("Encrypt(", combined)
        self.assertIn('"\\\"gta_sa.exe\\\" --coop"', self.launcher)

    def test_updater_uses_one_versioned_package_from_fixed_rolling_release(self):
        self.assertIn(
            "https://github.com/itsLighty/CoopAndreas/releases/download/playtest-latest/",
            self.updater,
        )
        self.assertIn('MANIFEST_NAME[] = L"playtest-manifest.txt"', self.updater)
        self.assertIn('"CoopAndreas-playtest-" + manifest.commit + ".zip"', self.updater)
        self.assertIn("DownloadVerifiedPackage", self.updater)
        self.assertNotIn("RCDATA", self.resources)

    def test_updater_rejects_untrusted_or_incoherent_downloads(self):
        self.assertIn("IsLowerHex(manifest.commit, 40)", self.updater)
        self.assertIn("IsLowerHex(manifest.sha256, 64)", self.updater)
        self.assertIn("HashFileSha256", self.updater)
        self.assertIn("actualHash != manifest.sha256", self.updater)
        self.assertIn("actualSize != manifest.size", self.updater)
        self.assertIn("ManifestsMatch(manifest, confirmation)", self.updater)
        self.assertIn("DOWNLOAD_ATTEMPTS = 3", self.updater)

    def test_updater_preserves_loader_and_rolls_back_partial_install(self):
        self.assertIn("CopyFileW(eax.c_str(), original.c_str(), TRUE)", self.updater)
        self.assertIn('L"eax_orig.dll"', self.updater)
        self.assertIn("FILE_ATTRIBUTE_REPARSE_POINT", self.updater)
        self.assertIn('IsProcessRunning(L"gta_sa.exe")', self.updater)
        self.assertIn('IsProcessRunning(L"server.exe")', self.updater)
        self.assertIn("RollBack(gameDirectory", self.updater)
        self.assertIn("The previous CoopAndreas build was restored", self.updater)

    def test_updater_extracts_with_system_powershell_not_path_search(self):
        self.assertIn("GetSystemDirectoryW", self.updater)
        self.assertIn('L"WindowsPowerShell\\\\v1.0\\\\powershell.exe"', self.updater)
        self.assertIn("CreateProcessW(powershell.c_str()", self.updater)
        self.assertNotIn("CreateProcessW(nullptr, command.data()", self.updater)

    def test_updater_supports_host_join_and_update_only(self):
        self.assertIn("Host & Play", self.updater)
        self.assertIn("Join & Play", self.updater)
        self.assertIn("Update only", self.updater)
        self.assertIn("--update-only", self.updater)
        self.assertIn('Launch(JoinPath(gameDirectory, L"server.exe")', self.updater)
        self.assertIn('Launch(JoinPath(gameDirectory, L"LaunchCoopAndreas.exe")', self.updater)

    def test_stage_creates_one_commit_named_package_and_manifest(self):
        self.assertIn('"CoopAndreas-playtest-$CommitSha.zip"', self.stage)
        self.assertIn("Compress-Archive", self.stage)
        self.assertIn("playtest-manifest.txt", self.stage)
        self.assertIn("Get-FileHash", self.stage)
        self.assertIn("sha256=$packageHash", self.stage)
        self.assertIn("size=$($packageFile.Length)", self.stage)
        self.assertIn("playtest-build.txt", self.stage)

    def test_local_build_outputs_a_small_permanent_updater(self):
        self.assertIn('target("playtest_launcher"', self.xmake)
        self.assertIn("xmake build -r playtest_launcher", self.build_updater)
        self.assertIn("CoopAndreasPlaytest.exe", self.build_updater)
        self.assertIn("build-playtest-updater.ps1", self.build_compatibility)

    def test_ci_uses_verified_pinned_sanny_download(self):
        self.assertIn("runs-on: windows-2022", self.workflow)
        self.assertNotIn("runs-on: windows-latest", self.workflow)
        self.assertIn(
            "https://github.com/sannybuilder/dev/releases/download/v4.2.0/SannyBuilder-v4.2.0.zip",
            self.workflow,
        )
        self.assertIn("4aceb1c1e430e57bc57216fbe95a3eac1829f2bd982ba19d99bb147caa4b33e6", self.workflow)
        self.assertIn("Get-FileHash", self.workflow)
        self.assertNotIn("api.github.com/repos/sannybuilder", self.workflow)

    def test_ci_keeps_validation_and_publishes_manifest_last_on_main(self):
        self.assertIn("python -m unittest discover", self.workflow)
        self.assertIn("audit-story-missions.ps1 -RequireReady", self.workflow)
        self.assertIn("actions/upload-artifact@v4", self.workflow)
        self.assertIn("github.ref == 'refs/heads/main'", self.workflow)
        self.assertIn('commits/main" --jq', self.workflow)
        self.assertIn("git/refs/tags/$tag", self.workflow)
        self.assertIn("gh release upload $tag $package.FullName", self.workflow)
        self.assertIn("gh release upload $tag $manifest", self.workflow)
        self.assertLess(
            self.workflow.index("gh release upload $tag $package.FullName"),
            self.workflow.index("gh release upload $tag $manifest"),
        )


if __name__ == "__main__":
    unittest.main()
