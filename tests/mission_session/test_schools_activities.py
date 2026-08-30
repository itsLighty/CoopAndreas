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
        self.assertEqual(self.body.count("gosub @BSKOOL_COOP_BEGIN_LESSON"), 6)
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
        stock = self.body.split("// Co-op policy for all six Bike School lessons:", 1)[0]
        # Stock intentionally cascades higher awards into lower rewards: the
        # gold branch grants all three, silver grants silver+bronze, and the
        # bronze branch grants bronze. These are host-only stock mutations.
        expected_stock_counts = {
            "Stat.RegisterOddjobMissionPassed": 1,
            "$flag_bikeschool_passed_1stime = 1": 1,
            "$bs_gold_rewardgiven = 1": 1,
            "$bs_silver_rewardgiven = 1": 2,
            "$bs_bronze_rewardgiven = 1": 3,
        }
        for evidence, expected_count in expected_stock_counts.items():
            self.assertEqual(stock.count(evidence), expected_count)
            self.assertNotIn(evidence, self.coop)
        cleanup = self.coop.split(":BSKOOL_COOP_CLEANUP", 1)[1]
        self.assertIn("518@ == 1", cleanup)
        self.assertIn("Coop.ClearAllEntityBlipsForNetworkPlayer", cleanup)
        self.assertIn("Coop.RemoveCheckpointForNetworkPlayer", cleanup)
        self.assertIn("Player.SetControl($player1, True)", cleanup)


class DrivingSchoolCoopStructureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.launcher = (ROOT / "scm/scripts/TRACE.txt").read_text(encoding="utf-8")
        cls.body = (ROOT / "scm/scripts/DSKOOL.txt").read_text(encoding="utf-8")
        cls.coop = cls.body.split(
            "// Co-op policy for all twelve Driving School lessons.", 1
        )[1]

    def test_launcher_and_body_are_host_authoritative(self):
        self.assertEqual(self.launcher.count("Coop.LaunchMissionForCoop(71)"), 2)
        self.assertGreaterEqual(self.launcher.count("Coop.IsHost()"), 2)
        self.assertNotIn("Mission.LoadAndLaunchInternal(71)", self.launcher)
        for start_label in ("@TRACE_263", "@TRACE_395"):
            self.assertRegex(
                self.launcher,
                rf"(?s)Player\.CanStartMission\(\$player1\).*?Coop\.IsHost\(\).*?"
                rf"goto_if_false {start_label}.*?\$onmission = 1",
            )
        self.assertIn("Coop.EnableSyncingThisScript()", self.body)
        self.assertRegex(
            self.body,
            r":DSKOOL_COOP_REJECT_NON_HOST\s+terminate_this_script",
        )

    def test_all_twelve_stock_lessons_share_begin_and_result_pipelines(self):
        lesson_labels = (3843, 5416, 6742, 8347, 9857, 11959, 13448, 15346, 16957, 18359, 20066, 22718)
        for label in lesson_labels:
            self.assertIn(f":DSKOOL_{label}", self.body)
        self.assertEqual(self.body.count("gosub @DSKOOL_COOP_BEGIN_LESSON"), 12)
        self.assertEqual(
            self.body.count("gosub @DSKOOL_COOP_NOTIFY_LESSON_RESULT"), 12
        )
        for key in (
            "DS1_44", "DS1_45", "DS1_32", "DS1_30", "DS1_29", "DS1_36",
            "DS1_1", "DS1_34", "DS1_28", "DS1_35", "DS1_33", "DS1_31",
        ):
            self.assertIn(f"Coop.PrintNowForNetworkPlayer('{key}'", self.coop)

    def test_frozen_roster_disconnect_and_reconnect_are_identity_bound(self):
        self.assertIn(
            "500@, 501@, 502@ = Coop.CollectNetworkPlayersForTheMission()",
            self.coop,
        )
        self.assertIn(
            "503@(520@,3i) = Coop.GetNetworkPlayerInternalId", self.coop
        )
        refresh = self.coop.split(":DSKOOL_COOP_REFRESH_ROSTER", 1)[1].split(
            ":DSKOOL_COOP_VALIDATE_SLOT", 1
        )[0]
        self.assertIn("522@ == 503@(520@,3i)", refresh)
        self.assertIn("500@(520@,3i) = 0", refresh)
        self.assertIn("disconnect is DNF", self.coop)

    def test_connected_death_uses_stock_failure_and_registration_is_bounded(self):
        update = self.coop.split(":DSKOOL_COOP_UPDATE", 1)[1].split(
            ":DSKOOL_COOP_STAGE_PARTICIPANTS", 1
        )[0]
        self.assertIn("COOP_PARTICIPANT_DEATH", update)
        self.assertIn("$instructor_car_dead_flag = 1", update)
        self.assertIn("Car.SetHealth($instructor_car, 0)", update)
        self.assertIn("gosub @DSKOOL_COOP_UPDATE", self.body.split(":DSKOOL_31109", 1)[1])
        registration = self.coop.split(
            ":DSKOOL_COOP_POLL_VEHICLE_REGISTRATION", 1
        )[1].split(":DSKOOL_COOP_UPDATE_REGROUP", 1)[0]
        self.assertIn("Coop.GetVehicleNetworkId($instructor_car)", registration)
        self.assertIn("531@ > 5000", registration)
        self.assertNotIn("wait ", registration.lower())

    def test_supporters_receive_safe_access_regrouping_and_objectives(self):
        for evidence in (
            "Car.GetMaximumNumberOfPassengers($instructor_car)",
            "Char.WarpIntoCarAsPassenger",
            "Coop.TeleportPlayersToHostSafely",
            "Char.LocateAnyMeansChar3D",
            "Coop.UpdateCheckpointForNetworkPlayer",
            "Coop.UpdateCarBlipForNetworkPlayer",
        ):
            self.assertIn(evidence, self.coop)

    def test_stock_progress_awards_and_cleanup_remain_single_owner(self):
        stock = self.body.split(
            "// Co-op policy for all twelve Driving School lessons.", 1
        )[0]
        expected_stock_counts = {
            "Stat.RegisterMissionPassed('FAR_1')": 1,
            "Stat.PlayerMadeProgress(1)": 1,
            "$driving_test_passed = 1": 2,  # debug unlock plus canonical completion
            "$f1_bronze_award = 1": 1,
            "$f1_silver_award = 1": 1,
            "$f1_gold_award = 1": 1,
        }
        for evidence, expected_count in expected_stock_counts.items():
            self.assertEqual(stock.count(evidence), expected_count)
            self.assertNotIn(evidence, self.coop)
        cleanup = self.coop.split(":DSKOOL_COOP_CLEANUP", 1)[1]
        self.assertIn("518@ == 1", cleanup)
        self.assertIn("Coop.ClearAllEntityBlipsForNetworkPlayer", cleanup)
        self.assertIn("Coop.RemoveCheckpointForNetworkPlayer", cleanup)
        self.assertIn("Player.SetControl($player1, True)", cleanup)


if __name__ == "__main__":
    unittest.main()
