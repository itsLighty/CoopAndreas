import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class BikeSchoolCoopStructureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.launcher = (ROOT / "scm/scripts/BIKES.txt").read_text(encoding="utf-8")
        cls.body = (ROOT / "scm/scripts/BSKOOL.txt").read_text(encoding="utf-8")
        cls.coop = cls.body.split("// Co-op policy for all six Bike School lessons:", 1)[1]

    def test_launcher_and_body_are_host_authoritative(self):
        self.assertIn("Coop.IsHost()", self.launcher)
        self.assertIn("Coop.LaunchMissionForCoop(120)", self.launcher)
        self.assertNotIn("Mission.LoadAndLaunchInternal(120)", self.launcher)
        self.assertIn("Coop.EnableSyncingThisScript()", self.body)
        self.assertRegex(
            self.body,
            r":BSKOOL_COOP_REJECT_NON_HOST\s+terminate_this_script",
        )

    def test_all_six_stock_lessons_share_one_result_pipeline(self):
        for label in (1783, 3477, 4902, 6670, 8446, 10039):
            self.assertIn(f":BSKOOL_{label}", self.body)
        self.assertEqual(
            self.body.count("gosub @BSKOOL_COOP_NOTIFY_LESSON_RESULT"), 6
        )
        for key in ("BS_A_1", "BS_B_1", "BS_C_1", "BS_D_1", "BS_E_1", "BS_F_1"):
            self.assertIn(f"Coop.PrintNowForNetworkPlayer('{key}'", self.coop)

    def test_frozen_roster_is_identity_bound_and_reconnectable(self):
        self.assertIn(
            "500@, 501@, 502@ = Coop.CollectNetworkPlayersForTheMission()",
            self.coop,
        )
        self.assertRegex(
            self.coop,
            r"503@\(520@,3i\)\s*=\s*Coop\.GetNetworkPlayerInternalId",
        )
        refresh = self.coop.split(":BSKOOL_COOP_REFRESH_ROSTER", 1)[1].split(
            ":BSKOOL_COOP_VALIDATE_SLOT", 1
        )[0]
        self.assertIn(
            "509@, 510@, 511@ = Coop.CollectNetworkPlayersForTheMission()",
            refresh,
        )
        self.assertIn("522@ == 503@(520@,3i)", refresh)
        self.assertIn("500@(520@,3i) = 0", refresh)
        self.assertNotRegex(refresh, r"503@\(520@,3i\)\s*=\s*522@")

    def test_disconnect_is_dnf_and_connected_death_uses_stock_failure(self):
        update = self.coop.split(":BSKOOL_COOP_UPDATE", 1)[1].split(
            ":BSKOOL_COOP_STAGE_PARTICIPANTS", 1
        )[0]
        self.assertIn("COOP_PARTICIPANT_DEATH", update)
        self.assertIn("Char.IsDead(500@(520@,3i))", update)
        self.assertIn("48@ = 1", update)
        self.assertIn("Car.SetHealth(38@, 0)", update)
        self.assertIn("nonblocking DNF", self.coop)

    def test_registration_access_regroup_and_objectives_are_bounded(self):
        registration = self.coop.split(
            ":BSKOOL_COOP_POLL_VEHICLE_REGISTRATION", 1
        )[1].split(":BSKOOL_COOP_UPDATE_REGROUP", 1)[0]
        self.assertIn("Coop.GetVehicleNetworkId(38@)", registration)
        self.assertIn("531@ > 5000", registration)
        self.assertNotIn("wait ", registration.lower())
        self.assertNotRegex(registration, r"goto\s+@BSKOOL_COOP_POLL")
        self.assertIn("Car.GetMaximumNumberOfPassengers(38@)", self.coop)
        self.assertIn("Char.WarpIntoCarAsPassenger", self.coop)
        self.assertIn("Coop.TeleportPlayersToHostSafely", self.coop)
        self.assertIn("Coop.UpdateCheckpointForNetworkPlayer", self.coop)
        self.assertIn("Coop.UpdateCarBlipForNetworkPlayer", self.coop)

    def test_progress_rewards_and_cleanup_remain_single_owner(self):
        for evidence in (
            "Stat.RegisterOddjobMissionPassed",
            "$flag_bikeschool_passed_1stime = 1",
            "$bs_gold_rewardgiven = 1",
            "$bs_silver_rewardgiven = 1",
            "$bs_bronze_rewardgiven = 1",
        ):
            self.assertGreaterEqual(self.body.count(evidence), 1)
            self.assertNotIn(evidence, self.coop)
        cleanup = self.coop.split(":BSKOOL_COOP_CLEANUP", 1)[1]
        self.assertIn("518@ == 1", cleanup)
        self.assertIn("Coop.ClearAllEntityBlipsForNetworkPlayer", cleanup)
        self.assertIn("Coop.RemoveCheckpointForNetworkPlayer", cleanup)
        self.assertIn("Player.SetControl($player1, True)", cleanup)


if __name__ == "__main__":
    unittest.main()
