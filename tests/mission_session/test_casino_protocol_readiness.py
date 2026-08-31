import hashlib
import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT_ROOT = ROOT / "scm" / "scripts"

MISSIONS = {
    "CASINO1": {
        "id": 84,
        "title": "Fender Ketchup",
        "story_label_count": 292,
        "story_label_digest": "3e89c71c0ab0adf84ada691905022c2877f5f1391b3fc024336535fba595bf1b",
        "reconnect_label": "CASINO1_COOP_REGROUP_RECONNECTED",
        "result_label": "CASINO1_COOP_NOTIFY_OUTCOME",
    },
    "CASINO2": {
        "id": 85,
        "title": "Explosive Situation",
        "story_label_count": 363,
        "story_label_digest": "1dff7d243a5d4f6e4c58bc0f3a7c3184bd07051e93971ea1c580bd7f77690022",
        "reconnect_label": "CASINO2_COOP_TELEPORT_CURRENT",
        "result_label": "CASINO2_COOP_NOTIFY_RESULT",
    },
    "CASINO3": {
        "id": 86,
        "title": "You've Had Your Chips",
        "story_label_count": 205,
        "story_label_digest": "03b8e48e163f3a1dd93149e4b72e5aad080d168958d58f94e903097a9e2f7b8a",
        "reconnect_label": "CASINO3_COOP_STAGE_RECONNECTS",
        "result_label": "CASINO3_COOP_NOTIFY_RESULT",
    },
    "CASINO4": {
        "id": 88,
        "title": "Don Peyote",
        "story_label_count": 884,
        "story_label_digest": "15769a9839127a37ef133573cf713ff66bb756fd0cc0fc6bdf20f292bfb1129c",
        "reconnect_label": "CASINO4_COOP_FREEZE_RECONNECTED",
        "result_label": "CASINO4_COOP_NOTIFY_RESULT",
    },
}


def read_script(name):
    return (SCRIPT_ROOT / f"{name}.txt").read_text(encoding="utf-8")


def label_body(script, label):
    match = re.search(
        rf"(?ms)^:{re.escape(label)}\s*$\n(?P<body>.*?)(?=^:[A-Z0-9_]+\s*$|\Z)",
        script,
    )
    if match is None:
        raise AssertionError(f"missing label {label}")
    return match.group("body")


class CasinoProtocolReadinessTests(unittest.TestCase):
    def test_original_story_stage_label_sequence_is_preserved(self):
        for name, mission in MISSIONS.items():
            with self.subTest(mission=mission["id"], script=name):
                script = read_script(name)
                labels = re.findall(rf"(?m)^:({name}_[0-9]+)\s*$", script)
                digest = hashlib.sha256("\n".join(labels).encode()).hexdigest()
                self.assertEqual(mission["story_label_count"], len(labels))
                self.assertEqual(mission["story_label_digest"], digest)

    def test_protocol_lifecycle_is_reachable_and_reconnect_safe(self):
        for name, mission in MISSIONS.items():
            with self.subTest(mission=mission["id"], script=name):
                script = read_script(name)
                roster_label = f"{name}_COOP_UPDATE_ROSTER"
                cleanup_label = f"{name}_COOP_CLEANUP"
                roster = label_body(script, roster_label)
                result = label_body(script, mission["result_label"])
                cleanup = label_body(script, cleanup_label)

                self.assertIn("Coop.EnableSyncingThisScript()", script)
                self.assertGreaterEqual(
                    script.count("Coop.CollectNetworkPlayersForTheMission()"), 2
                )
                self.assertIn("Coop.CollectNetworkPlayersForTheMission()", roster)
                self.assertIn("Coop.IsNetworkPlayerActorValid", roster)
                self.assertIn("Coop.GetNetworkPlayerInternalId", roster)
                self.assertRegex(roster, r"is_int_lvar_equal_to_int_lvar[^\n]+==")
                self.assertRegex(script, rf"(?m)^\s*gosub @{roster_label}\s*$")
                self.assertRegex(
                    script,
                    rf"(?m)^\s*gosub @{mission['reconnect_label']}\s*$",
                )

                self.assertIn("'M_FAIL'", result)
                self.assertIn("'M_PASSD'", result)
                self.assertRegex(
                    script,
                    rf"(?m)^\s*gosub @{mission['result_label']}\s*$",
                )
                self.assertRegex(cleanup, r"(?ms)^if\s+.*?==\s*1\s+then\s+return")
                self.assertIn(f"gosub @{roster_label}", cleanup)
                self.assertRegex(script, rf"(?m)^\s*gosub @{cleanup_label}\s*$")
                self.assertNotRegex(
                    script.lower(),
                    r"currently unsupported|not recommended to play|may cause desyncs",
                )


if __name__ == "__main__":
    unittest.main()
