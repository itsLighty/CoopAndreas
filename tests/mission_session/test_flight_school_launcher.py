import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class FlightSchoolLauncherTests(unittest.TestCase):
    def test_only_host_can_launch_coop_flight_school(self):
        launcher = (ROOT / "scm/scripts/PSCH.txt").read_text(encoding="utf-8")
        gate = re.search(
            r"Player\.IsPlaying\(\$player1\).*?Coop\.IsHost\(\).*?"
            r"goto_if_false @PSCH_239.*?\$onmission == 0",
            launcher,
            re.S,
        )
        self.assertIsNotNone(gate)
        self.assertIn("Mission.LoadAndLaunchInternal(83)", launcher)

        body = (ROOT / "scm/scripts/DES3.txt").read_text(encoding="utf-8")
        for evidence in (
            "Coop.EnableSyncingThisScript()",
            "Coop.CollectNetworkPlayersForTheMission()",
            ":DES3_COOP_CHECK_PARTICIPANTS",
            "deterministic cooperative failure requested",
            "M_PASSD",
            "M_FAIL",
        ):
            self.assertIn(evidence, body)


if __name__ == "__main__":
    unittest.main()
