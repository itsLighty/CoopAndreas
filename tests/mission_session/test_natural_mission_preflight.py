import json
import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening_brace = source.index("{", start)
    depth = 0
    for index in range(opening_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening_brace : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


def executable_scm(text: str) -> str:
    return "\n".join(line.split("//", 1)[0] for line in text.splitlines())


class NaturalMissionPreflightTests(unittest.TestCase):
    def setUp(self):
        self.client = (ROOT / "client/src/CMissionSessionClient.cpp").read_text(encoding="utf-8")

    def test_every_production_internal_launcher_uses_coop_preflight(self):
        launchers = sorted((ROOT / "scm/scripts").glob("*.txt"))
        active_preflight_calls = []
        legacy_bypasses = []

        for launcher in launchers:
            active_text = executable_scm(launcher.read_text(encoding="utf-8"))
            active_preflight_calls.extend(
                (launcher, int(match.group(1)))
                for match in re.finditer(r"Coop\.LaunchMissionForCoop\((\d+)\)", active_text)
            )
            if "Mission.LoadAndLaunchInternal(" in active_text:
                legacy_bypasses.append(launcher.relative_to(ROOT).as_posix())

        self.assertEqual([], legacy_bypasses)
        self.assertEqual(167, len(active_preflight_calls))
        self.assertEqual(45, len({launcher for launcher, _ in active_preflight_calls}))

    def test_unauthenticated_bootstrap_falls_back_to_native_launch(self):
        main = executable_scm((ROOT / "scm/scripts/MAIN.txt").read_text(encoding="utf-8"))
        self.assertIn("Coop.LaunchMissionForCoop(0)", main)
        self.assertIn("Coop.LaunchMissionForCoop(1)", main)

        method = function_body(self.client, "bool CMissionSessionClient::RequestScmLaunch")
        fallback = function_body(method, "if (!CNetwork::m_bAuthenticated)")
        self.assertIn("Command<Commands::LOAD_AND_LAUNCH_MISSION_INTERNAL>(validatedMissionId);", fallback)
        self.assertIn("return true;", fallback)

    def test_authenticated_peer_is_rejected_before_requesting_or_launching(self):
        method = function_body(self.client, "bool CMissionSessionClient::RequestScmLaunch")
        peer_guard = function_body(method, "if (!CLocalPlayer::m_bIsHost)")
        self.assertIn("RollbackScmMissionLaunch();", peer_guard)
        self.assertIn("return false;", peer_guard)
        self.assertNotIn("LOAD_AND_LAUNCH_MISSION_INTERNAL", peer_guard)
        self.assertLess(method.index("if (!CLocalPlayer::m_bIsHost)"),
                        method.index("RequestLaunch(validatedMissionId, true)"))

    def test_authenticated_host_requests_local_launch_only_after_approval(self):
        preflight = function_body(self.client, "bool CMissionSessionClient::RequestScmLaunch")
        self.assertIn("RequestLaunch(validatedMissionId, true)", preflight)

        approved = function_body(self.client, "void CMissionSessionClient::ProcessApprovedLocalLaunch")
        self.assertEqual(1, approved.count("Command<Commands::LOAD_AND_LAUNCH_MISSION_INTERNAL>(missionId);"))
        self.assertNotIn("RequestScmLaunch", approved)

    def test_rejection_rolls_back_only_the_scm_preflight(self):
        apply_state = function_body(self.client, "void CMissionSessionClient::ApplyState")
        self.assertIn("const bool bRejectedScmLaunch = m_bScmLaunchRequestPending;", apply_state)
        self.assertRegex(
            apply_state,
            r"if \(bRejectedScmLaunch && !state\.IsActive\(\)\)\s*\{\s*RollbackScmMissionLaunch\(\);",
        )

    def test_rejected_peer_bootstrap_recovers_from_main_fade_out(self):
        main = executable_scm((ROOT / "scm/scripts/MAIN.txt").read_text(encoding="utf-8"))
        intro_launch = main.index("Coop.LaunchMissionForCoop(2)")
        peer_recovery = main.index(":MAIN_RECOVER_PEER_STARTUP", intro_launch)
        recovery_done = main.index(":MAIN_PEER_STARTUP_READY", peer_recovery)
        recovery = main[peer_recovery:recovery_done]

        self.assertIn("Coop.IsHost()", main[intro_launch:peer_recovery])
        self.assertIn("goto_if_false @MAIN_RECOVER_PEER_STARTUP", main[intro_launch:peer_recovery])
        self.assertIn("Camera.DoFade(1, 1000)", recovery)
        self.assertIn("Hud.SwitchWidescreen(False)", recovery)
        self.assertIn("Player.SetControl($player1, True)", recovery)

        rollback = function_body(self.client, "void CMissionSessionClient::RollbackScmMissionLaunch")
        self.assertNotIn("TheCamera.Fade", rollback)

    def test_opcode_is_registered_in_native_and_sanny_definitions(self):
        registrar = (ROOT / "client/src/Commands/CCustomCommandRegistrar.h").read_text(encoding="utf-8")
        command = (ROOT / "client/src/Commands/Commands/CCommandLaunchMissionForCoop.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("RegisterCommand(0x1D1D, new CCommandLaunchMissionForCoop())", registrar)
        self.assertIn("CMissionSessionClient::RequestScmLaunch(ScriptParams[0]);", command)

        sanny_root = ROOT / "sdk/Sanny Builder 4/data/sa_sbl_coopandreas"
        definitions = json.loads((sanny_root / "sa_coop.json").read_text(encoding="utf-8"))
        serialized = json.dumps(definitions)
        self.assertIn('"id": "1D1D"', serialized)
        self.assertIn('"member": "LaunchMissionForCoop"', serialized)
        for filename in ("sa_coop.db", "classes.db", "SASCM.INI"):
            self.assertIn("1D1D", (sanny_root / filename).read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
