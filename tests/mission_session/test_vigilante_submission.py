import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
VIGILANTE = ROOT / "scm/scripts/COPCAR.txt"
LAUNCHER = ROOT / "scm/scripts/R3.txt"


class VigilanteSubmissionCoopStructureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = VIGILANTE.read_text(encoding="utf-8")
        cls.launcher = LAUNCHER.read_text(encoding="utf-8")

    def test_natural_launch_preflight_and_host_authority(self):
        self.assertGreaterEqual(
            self.launcher.count("Coop.LaunchMissionForCoop(124)"), 2
        )
        self.assertNotIn("Mission.LoadAndLaunchInternal(124)", self.launcher)
        self.assertIn("Coop.EnableSyncingThisScript()", self.text)
        self.assertIn("Coop.IsHost()", self.text)
        self.assertIn(":COPCAR_COOP_REJECT_NON_HOST", self.text)
        self.assertRegex(
            self.text,
            r":COPCAR_COOP_REJECT_NON_HOST\s+terminate_this_script",
        )

    def test_frozen_roster_is_identity_bound_and_reconnectable(self):
        self.assertIn(
            "277@, 278@, 279@ = Coop.CollectNetworkPlayersForTheMission()",
            self.text,
        )
        self.assertRegex(
            self.text,
            r"280@\(290@,3i\)\s*=\s*Coop\.GetNetworkPlayerInternalId",
        )
        refresh = self.text.split(":COPCAR_COOP_REFRESH_ROSTER", 1)[1].split(
            ":COPCAR_COOP_VALIDATE_SLOT", 1
        )[0]
        self.assertIn(
            "286@, 287@, 288@ = Coop.CollectNetworkPlayersForTheMission()",
            refresh,
        )
        self.assertIn("292@ == 280@(290@,3i)", refresh)
        self.assertIn("277@(290@,3i) = 0", refresh)
        self.assertIn("283@(290@,3i) = -1", refresh)
        self.assertNotIn("294@ = 1", refresh)

    def test_disconnect_is_dnf_and_connected_death_uses_stock_failure_exit(self):
        update = self.text.split(":COPCAR_COOP_UPDATE", 1)[1].split(
            ":COPCAR_COOP_POLL_ENTITY_REGISTRATION", 1
        )[0]
        self.assertIn("COOP_PARTICIPANT_DEATH", update)
        self.assertIn("gosub @COPCAR_COOP_VALIDATE_SLOT", update)
        self.assertIn("Char.IsDead(277@(290@,3i))", update)
        self.assertIn("294@ = 1", update)
        self.assertIn("58@ = 1", update)
        main_loop = self.text.split(":COPCAR_5818", 1)[1].split(
            ":COPCAR_5873", 1
        )[0]
        self.assertIn("gosub @COPCAR_COOP_UPDATE", main_loop)
        self.assertIn("goto @COPCAR_14808", main_loop)

    def test_stock_spawning_police_timer_progression_rewards_are_single_owner(self):
        self.assertEqual(self.text.count("Char.CreateInsideCar"), 3)
        self.assertEqual(self.text.count("Char.CreateAsPassenger"), 9)
        self.assertIn("police_radio_message", self.text)
        self.assertIn("Task.KillCharOnFoot", self.text)
        self.assertEqual(self.text.count("$cop_time_limit += 30000"), 1)
        self.assertEqual(self.text.count("51@ += 1"), 1)
        self.assertEqual(self.text.count("$total_criminals_killed += 1"), 12)
        self.assertEqual(self.text.count("Player.AddScore($player1, 54@)"), 1)
        self.assertEqual(self.text.count("Stat.RegisterOddjobMissionPassed"), 1)
        self.assertEqual(self.text.count("Player.IncreaseMaxArmor($player1, 50)"), 1)
        self.assertEqual(self.text.count("$done_copcar_progress = 1"), 1)
        coop = self.text.split("// Co-op policy for Vigilante:", 1)[1]
        self.assertNotRegex(
            coop,
            r"(?:Player\.AddScore|Stat\.RegisterOddjobMissionPassed|"
            r"Player\.IncreaseMaxArmor|\$done_copcar_progress\s*=)",
        )

    def test_entity_registration_is_bounded_and_does_not_stall_stock(self):
        registration = self.text.split(
            ":COPCAR_COOP_POLL_ENTITY_REGISTRATION", 1
        )[1].split(":COPCAR_COOP_UPDATE_REGROUP", 1)[0]
        self.assertIn("Coop.GetVehicleNetworkId(362@)", registration)
        self.assertIn("Coop.GetPedNetworkId(362@)", registration)
        self.assertIn("363@ > 5000", registration)
        self.assertIn("346@(361@,15i) = 1", registration)
        self.assertNotIn("wait ", registration.lower())
        self.assertNotRegex(
            registration,
            r"goto\s+@COPCAR_COOP_POLL_ENTITY_REGISTRATION",
        )

    def test_shared_suspect_vehicle_objectives_and_regrouping(self):
        objectives = self.text.split(":COPCAR_COOP_UPDATE_OBJECTIVES", 1)[1].split(
            ":COPCAR_COOP_NOTIFY_FAILURE", 1
        )[0]
        for evidence in (
            "Coop.UpdateCarBlipForNetworkPlayer",
            "Coop.UpdateCharBlipForNetworkPlayer",
            "Coop.UpdateCheckpointForNetworkPlayer",
            "Coop.PrintNowForNetworkPlayer('KILLS'",
            "BlipColor.Purple",
            "BlipColor.Red",
        ):
            self.assertIn(evidence, objectives)
        self.assertIn("not Car.IsDead(362@)", objectives)
        self.assertIn("not Char.IsDead(362@)", objectives)
        regroup = self.text.split(":COPCAR_COOP_UPDATE_REGROUP", 1)[1].split(
            ":COPCAR_COOP_UPDATE_OBJECTIVES", 1
        )[0]
        self.assertIn("Coop.TeleportPlayersToHostSafely(277@, 278@, 279@)", regroup)
        self.assertIn("363@ > 20000", regroup)

    def test_result_and_cleanup_are_exactly_once_and_idempotent(self):
        self.assertEqual(self.text.count("gosub @COPCAR_COOP_NOTIFY_FAILURE"), 1)
        self.assertEqual(self.text.count("gosub @COPCAR_COOP_NOTIFY_PASS"), 1)
        self.assertIn("51@ == 12", self.text)
        self.assertIn("295@ = 1", self.text)
        self.assertIn("295@ = 2", self.text)
        self.assertIn("Coop.PrintBigForNetworkPlayer('M_FAIL'", self.text)
        self.assertIn("Coop.PrintBigForNetworkPlayer('M_PASSD'", self.text)
        self.assertEqual(self.text.count("gosub @COPCAR_COOP_CLEANUP"), 1)
        cleanup = self.text.split(":COPCAR_COOP_CLEANUP", 1)[1]
        self.assertIn("296@ == 1", cleanup)
        self.assertIn("Coop.ClearAllEntityBlipsForNetworkPlayer", cleanup)
        self.assertIn("Coop.RemoveCheckpointForNetworkPlayer", cleanup)
        self.assertIn("Coop.ClearThisPrintForNetworkPlayer('KILLS'", cleanup)
        self.assertIn("Char.FreezePosition(277@(290@,3i), False)", cleanup)
        self.assertIn("Char.SetProofs(277@(290@,3i), False", cleanup)
        self.assertIn("Player.SetControl($player1, True)", cleanup)
        self.assertIn("preserving each remote", cleanup)


if __name__ == "__main__":
    unittest.main()
