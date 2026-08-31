import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]

MISSIONS = {
    "MUSIC1": {
        "frozen_id": "372@",
        "connected": "375@",
        "recollect": "MUSIC1_COOP_REFRESH_ROSTER",
        "update": "MUSIC1_COOP_UPDATE",
        "cleanup_guard": "384@",
        "passed": "STRAP_1",
    },
    "MUSIC2": {
        "frozen_id": "278@",
        "connected": "281@",
        "recollect": "MUSIC2_COOP_REFRESH_ROSTER",
        "update": "MUSIC2_COOP_UPDATE",
        "cleanup_guard": "290@",
        "passed": "STRAP_2",
    },
    "MUSIC3": {
        "frozen_id": "289@",
        "connected": "292@",
        "recollect": "MUSIC3_COOP_REFRESH_ROSTER",
        "update": "MUSIC3_COOP_UPDATE",
        "cleanup_guard": "301@",
        "passed": "STRAP_3",
    },
    "MUSIC5": {
        "frozen_id": "168@",
        "connected": "171@",
        "recollect": "MUSIC5_COOP_RECOLLECT_ROSTER",
        "update": "MUSIC5_COOP_UPDATE_ROSTER",
        "cleanup_guard": "153@",
        "passed": "STRAP_4",
    },
}


def label_block(script: str, label: str) -> str:
    start = script.index(f":{label}")
    prefix = label.split("_COOP_", 1)[0]
    following = re.search(rf"(?m)^:{prefix}_COOP_[A-Z0-9_]+\s*$", script[start + 1 :])
    if following is None:
        return script[start:]
    return script[start : start + 1 + following.start()]


class MusicStoryMissionProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.scripts = {
            name: (ROOT / "scm" / "scripts" / f"{name}.txt").read_text(
                encoding="utf-8"
            )
            for name in MISSIONS
        }

    def test_frozen_rosters_are_bounded_and_reconnect_safe(self):
        for name, contract in MISSIONS.items():
            with self.subTest(mission=name):
                script = self.scripts[name]
                recollect = label_block(script, contract["recollect"])

                self.assertIn("Coop.EnableSyncingThisScript()", script)
                self.assertGreaterEqual(
                    script.count("Coop.CollectNetworkPlayersForTheMission()"), 2
                )
                self.assertIn("for $temp_int = 0 to 2", recollect)
                self.assertRegex(recollect, r"for \d+@ = 0 to 2")
                self.assertIn("$NETWORK_PLAYER[$temp_int] = -1", recollect)
                self.assertIn("Coop.IsNetworkPlayerActorValid(", recollect)
                self.assertIn("Coop.GetNetworkPlayerInternalId(", recollect)
                self.assertRegex(
                    recollect,
                    rf"is_int_lvar_equal_to_int_lvar\s+\d+@\s*==\s*"
                    rf"{re.escape(contract['frozen_id'])}\(\$temp_int,3i\)",
                )
                self.assertNotRegex(
                    recollect,
                    rf"{re.escape(contract['frozen_id'])}\(\$temp_int,3i\)\s*=\s*"
                    r"Coop\.GetNetworkPlayerInternalId",
                )

    def test_identity_safe_readiness_is_on_the_live_update_path(self):
        for name, contract in MISSIONS.items():
            with self.subTest(mission=name):
                script = self.scripts[name]
                update = label_block(script, contract["update"])
                recollect = label_block(script, contract["recollect"])

                if name == "MUSIC5":
                    self.assertIn(
                        "gosub @MUSIC5_COOP_RECOLLECT_ROSTER", update
                    )
                    readiness = update
                else:
                    self.assertIn(f"gosub @{contract['recollect']}", update)
                    readiness = recollect

                self.assertRegex(
                    readiness,
                    rf"{re.escape(contract['connected'])}\(\$temp_int,3i\)\s*=\s*1",
                )
                self.assertRegex(
                    script,
                    rf"{re.escape(contract['connected'])}\(\$temp_int,3i\)\s*==\s*1",
                )
                self.assertIn("Coop.IsNetworkPlayerActorValid($NETWORK_PLAYER[$temp_int])", script)

    def test_network_id_waits_are_bounded(self):
        for name, script in self.scripts.items():
            lines = script.splitlines()
            waits = 0
            for index, line in enumerate(lines):
                match = re.match(r"^(?P<indent>\s*)while\s+.+==\s*-1\s*$", line)
                if match is None:
                    continue
                waits += 1
                indent = match.group("indent")
                body = []
                for candidate in lines[index + 1 :]:
                    if candidate == f"{indent}end":
                        break
                    body.append(candidate)
                body_text = "\n".join(body)
                with self.subTest(mission=name, line=index + 1):
                    self.assertIn("Clock.GetGameTimer", body_text)
                    self.assertRegex(body_text, r"(?:>=|>|greater(?:_or_equal)?_to)")
                    self.assertRegex(body_text, r"(?m)^\s*(?:return|goto(?:_if_false)?)\b")

            if name == "MUSIC2":
                self.assertEqual(waits, 2)
            else:
                self.assertEqual(waits, 0)

    def test_result_fanout_and_cleanup_are_reachable_and_idempotent(self):
        for name, contract in MISSIONS.items():
            with self.subTest(mission=name):
                script = self.scripts[name]
                failure_label = f"{name}_COOP_NOTIFY_FAILURE"
                result_label = f"{name}_COOP_NOTIFY_RESULT"
                cleanup_label = f"{name}_COOP_CLEANUP"
                failure = label_block(script, failure_label)
                result = label_block(script, result_label)
                cleanup = label_block(script, cleanup_label)

                self.assertGreaterEqual(script.count(f"@{failure_label}"), 1)
                self.assertGreaterEqual(script.count(f"@{result_label}"), 1)
                self.assertGreaterEqual(script.count(f"@{cleanup_label}"), 1)
                self.assertIn("PrintBigForNetworkPlayer('M_FAIL'", failure)
                self.assertIn("PrintBigForNetworkPlayer('M_PASSD'", result)
                self.assertIn(
                    f"Stat.RegisterMissionPassed('{contract['passed']}')", script
                )
                self.assertRegex(
                    cleanup,
                    rf"(?s)^:{cleanup_label}\s+if\s+"
                    rf"{re.escape(contract['cleanup_guard'])}\s*==\s*1\s+"
                    r"then\s+return\s+end\s+"
                    rf"{re.escape(contract['cleanup_guard'])}\s*=\s*1",
                )

    def test_each_mission_exposes_all_story_audit_ready_prerequisites(self):
        unsupported = re.compile(
            r"currently unsupported|not recommended to play|may cause desyncs", re.I
        )
        for name, script in self.scripts.items():
            with self.subTest(mission=name):
                self.assertIsNone(unsupported.search(script))
                for marker in (
                    f":{name}_COOP_PARTICIPANT_DEATH",
                    f":{name}_COOP_NOTIFY_FAILURE",
                    f":{name}_COOP_NOTIFY_RESULT",
                    f":{name}_COOP_CLEANUP",
                ):
                    self.assertIn(marker, script)


if __name__ == "__main__":
    unittest.main()
