import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
FREIGHT = ROOT / "scm/scripts/FREIGHT.txt"
LAUNCHER = ROOT / "scm/scripts/R3.txt"


class FreightSubmissionCoopStructureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = FREIGHT.read_text(encoding="utf-8")
        cls.launcher = LAUNCHER.read_text(encoding="utf-8")

    def test_host_authority_and_frozen_identity_roster(self):
        self.assertEqual(self.launcher.count("Coop.LaunchMissionForCoop(126)"), 2)
        self.assertIn("Coop.EnableSyncingThisScript()", self.text)
        self.assertIn("Coop.IsHost()", self.text)
        self.assertIn(
            "200@, 201@, 202@ = Coop.CollectNetworkPlayersForTheMission()",
            self.text,
        )
        self.assertIn(
            "203@(216@,3i) = Coop.GetNetworkPlayerInternalId",
            self.text,
        )

        refresh = self.text.split(":FREIGHT_COOP_REFRESH_ROSTER", 1)[1].split(
            ":FREIGHT_COOP_VALIDATE_SLOT", 1
        )[0]
        self.assertIn(
            "209@, 210@, 211@ = Coop.CollectNetworkPlayersForTheMission()", refresh
        )
        self.assertIn("219@ == 203@(216@,3i)", refresh)
        self.assertIn("200@(216@,3i) = 0", refresh)
        self.assertNotIn(
            "203@(216@,3i) = Coop.GetNetworkPlayerInternalId", refresh
        )

    def test_stock_train_route_timers_levels_and_persistence_remain_host_owned(self):
        for model in (590, 570, 569, 538, 537):
            self.assertEqual(self.text.count(f"Streaming.RequestModel({model})"), 1)
        self.assertEqual(self.text.count("World.DeleteAllTrains"), 1)
        self.assertEqual(self.text.count("Game.SwitchRandomTrains(False)"), 1)
        self.assertEqual(self.text.count("Train.FindDirection($car)"), 1)
        self.assertEqual(self.text.count("Train.HasDerailed($car)"), 1)
        self.assertEqual(self.text.count("130@ += 1"), 1)
        self.assertEqual(self.text.count("130@ == 5"), 1)
        self.assertEqual(self.text.count("Player.AddScore($player1, 128@)"), 1)
        self.assertEqual(self.text.count("Player.AddScore($player1, 50000)"), 1)
        self.assertEqual(self.text.count("Player.AddScore($player1, 10000)"), 1)
        self.assertEqual(self.text.count("$ft_train_level += 1"), 1)
        self.assertEqual(self.text.count("Stat.RegisterOddjobMissionPassed"), 2)
        self.assertEqual(self.text.count("Stat.PlayerMadeProgress(1)"), 1)
        self.assertIn("$flag_freight_passed_0stime = 1", self.text)
        self.assertIn("$flag_freight_passed_1stime = 1", self.text)

        coop = self.text.split(":FREIGHT_COOP_INIT", 1)[1]
        for host_only_effect in (
            "World.DeleteAllTrains",
            "Train.FindDirection",
            "Player.AddScore",
            "$ft_train_level +=",
            "$flag_freight_passed_0stime =",
            "$flag_freight_passed_1stime =",
            "Stat.RegisterOddjobMissionPassed",
            "Stat.PlayerMadeProgress",
        ):
            self.assertNotIn(host_only_effect, coop)

    def test_disconnect_is_dnf_and_connected_death_fails_deterministically(self):
        refresh = self.text.split(":FREIGHT_COOP_REFRESH_ROSTER", 1)[1].split(
            ":FREIGHT_COOP_VALIDATE_SLOT", 1
        )[0]
        self.assertNotIn("212@ = 1", refresh)

        update = self.text.split(":FREIGHT_COOP_UPDATE", 1)[1].split(
            ":FREIGHT_COOP_POLL_TRAIN_REGISTRATION", 1
        )[0]
        self.assertIn("gosub @FREIGHT_COOP_VALIDATE_SLOT", update)
        self.assertIn("Char.IsDead(200@(216@,3i))", update)
        self.assertIn("COOP_PARTICIPANT_DEATH", update)
        self.assertIn("212@ = 1", update)

        failure_bridge = self.text.split("gosub @FREIGHT_COOP_UPDATE", 1)[1].split(
            "is_ps2_keyboard_key_just_pressed 65", 1
        )[0]
        self.assertIn("212@ == 1", failure_bridge)
        self.assertIn("goto @FREIGHT_2819", failure_bridge)

    def test_train_registration_is_bounded_and_nonblocking(self):
        registration = self.text.split(
            ":FREIGHT_COOP_POLL_TRAIN_REGISTRATION", 1
        )[1].split(":FREIGHT_COOP_UPDATE_REGROUP", 1)[0]
        self.assertIn("Coop.GetVehicleNetworkId($car)", registration)
        self.assertIn("> 5000", registration)
        self.assertNotIn("wait ", registration.lower())
        self.assertNotIn("goto @FREIGHT_COOP_POLL_TRAIN_REGISTRATION", registration)

    def test_support_objective_and_safe_vehicle_access(self):
        update = self.text.split(":FREIGHT_COOP_UPDATE", 1)[1].split(
            ":FREIGHT_COOP_POLL_TRAIN_REGISTRATION", 1
        )[0]
        self.assertIn("Car.LockDoors($car, CarLock.Unlocked)", update)

        objectives = self.text.split(":FREIGHT_COOP_UPDATE_OBJECTIVES", 1)[1].split(
            ":FREIGHT_COOP_NOTIFY_FAILURE", 1
        )[0]
        for evidence in (
            "45@(125@,20f)",
            "65@(125@,20f)",
            "85@(125@,20f)",
            "Coop.ClearAllEntityBlipsForNetworkPlayer",
            "Coop.RemoveCheckpointForNetworkPlayer",
            "Coop.UpdateCarBlipForNetworkPlayer",
            "Coop.UpdateCheckpointForNetworkPlayer",
            "Coop.PrintNowForNetworkPlayer('FREI_03'",
        ):
            self.assertIn(evidence, objectives)

        regroup = self.text.split(":FREIGHT_COOP_UPDATE_REGROUP", 1)[1].split(
            ":FREIGHT_COOP_UPDATE_OBJECTIVES", 1
        )[0]
        self.assertIn("Coop.TeleportPlayersToHostSafely(200@, 201@, 202@)", regroup)
        self.assertIn("224@ > 20000", regroup)
        self.assertIn("2.0 > 229@", regroup)

    def test_results_and_cleanup_are_exactly_once_and_idempotent(self):
        failure = self.text.split(":FREIGHT_COOP_NOTIFY_FAILURE", 1)[1].split(
            ":FREIGHT_COOP_NOTIFY_PASS", 1
        )[0]
        passed = self.text.split(":FREIGHT_COOP_NOTIFY_PASS", 1)[1].split(
            ":FREIGHT_COOP_CLEANUP", 1
        )[0]
        self.assertIn("213@ <> 0", failure)
        self.assertIn("213@ = 1", failure)
        self.assertIn("213@ <> 0", passed)
        self.assertIn("213@ = 2", passed)
        self.assertIn("Coop.PrintBigForNetworkPlayer('M_FAIL'", failure)
        self.assertIn("Coop.PrintBigForNetworkPlayer('M_PASSD'", passed)
        self.assertEqual(self.text.count("gosub @FREIGHT_COOP_NOTIFY_PASS"), 1)

        cleanup = self.text.split(":FREIGHT_COOP_CLEANUP", 1)[1]
        self.assertIn("214@ == 1", cleanup)
        self.assertIn("Coop.ClearAllEntityBlipsForNetworkPlayer", cleanup)
        self.assertIn("Coop.RemoveCheckpointForNetworkPlayer", cleanup)
        self.assertIn("Char.FreezePosition(200@(216@,3i), False)", cleanup)
        self.assertIn("Char.SetProofs(200@(216@,3i), False", cleanup)
        self.assertIn("Player.SetControl($player1, True)", cleanup)
        self.assertIn("preserving each", cleanup)
        self.assertEqual(self.text.count("gosub @FREIGHT_COOP_CLEANUP"), 1)
        self.assertEqual(self.text.count("Mission.Finish"), 1)


if __name__ == "__main__":
    unittest.main()
