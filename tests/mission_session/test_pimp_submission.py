import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
PIMP = ROOT / "scm/scripts/PIMP.txt"


class PimpSubmissionCoopStructureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = PIMP.read_text(encoding="utf-8")

    def test_host_owns_body_and_frozen_identity_roster(self):
        prologue = self.text.split(":PIMP_47", 1)[0]
        self.assertIn("Coop.EnableSyncingThisScript()", prologue)
        self.assertIn("Coop.IsHost()", prologue)
        self.assertIn(":PIMP_COOP_REJECT_NON_HOST", prologue)
        self.assertIn("terminate_this_script", prologue)
        self.assertIn(
            "542@, 543@, 544@ = Coop.CollectNetworkPlayersForTheMission()",
            self.text,
        )
        self.assertIn(
            "545@(554@,3i) = Coop.GetNetworkPlayerInternalId",
            self.text,
        )

    def test_stock_spawns_progression_payouts_and_persistence_remain_single_owner(self):
        self.assertGreaterEqual(self.text.count("35@(37@,2i) = Char.Create("), 3)
        self.assertGreaterEqual(self.text.count("91@(50@,28i) = Char.Create("), 1)
        self.assertEqual(self.text.count("49@ += 1"), 1)
        self.assertEqual(self.text.count("Player.AddScore($player1, 526@)"), 2)
        self.assertEqual(self.text.count("Player.AddScore($player1, 1000)"), 1)
        self.assertEqual(self.text.count("Stat.RegisterOddjobMissionPassed"), 1)
        self.assertEqual(self.text.count("Game.ActivatePimpCheat(True)"), 1)
        self.assertEqual(self.text.count("$pimp_passed_once = 1"), 1)
        self.assertIn("$pimp_runtimer = 180000", self.text)

        coop = self.text.split("// Co-op policy for Pimping:", 1)[1]
        for host_only_effect in (
            "Char.Create(",
            "Player.AddScore",
            "49@ += 1",
            "Stat.RegisterOddjobMissionPassed",
            "Game.ActivatePimpCheat",
            "$pimp_passed_once =",
        ):
            self.assertNotIn(host_only_effect, coop)

    def test_reconnect_matches_immutable_identity_and_disconnect_is_dnf(self):
        refresh = self.text.split(":PIMP_COOP_REFRESH_ROSTER", 1)[1].split(
            ":PIMP_COOP_VALIDATE_SLOT", 1
        )[0]
        self.assertIn(
            "551@, 552@, 553@ = Coop.CollectNetworkPlayersForTheMission()",
            refresh,
        )
        self.assertIn("557@ == 545@(554@,3i)", refresh)
        self.assertIn("542@(554@,3i) = 0", refresh)
        self.assertIn("548@(554@,3i) = -1", refresh)
        self.assertIn("Coop.TeleportPlayersToHostSafely", refresh)
        self.assertNotIn("560@ = 1", refresh)
        self.assertIn("disconnect", self.text.lower())
        self.assertIn("DNF", self.text)

    def test_connected_participant_death_fails_deterministically(self):
        update = self.text.split(":PIMP_COOP_UPDATE", 1)[1].split(
            ":PIMP_COOP_SELECT_OBJECTIVE", 1
        )[0]
        self.assertIn("gosub @PIMP_COOP_VALIDATE_SLOT", update)
        self.assertIn("Char.IsDead(542@(554@,3i))", update)
        self.assertIn("COOP_PARTICIPANT_DEATH", update)
        self.assertIn("560@ = 1", update)
        bridge = self.text.split(":PIMP_3765", 1)[1].split(
            ":PIMP_COOP_CONTINUE_STOCK", 1
        )[0]
        self.assertIn("gosub @PIMP_COOP_UPDATE", bridge)
        self.assertIn("goto @PIMP_37455", bridge)

    def test_entity_registration_is_bounded_and_nonblocking(self):
        registration = self.text.split(
            ":PIMP_COOP_POLL_ENTITY_REGISTRATION", 1
        )[1].split(":PIMP_COOP_UPDATE_REGROUP", 1)[0]
        self.assertIn("Coop.GetPedNetworkId(564@)", registration)
        self.assertIn("Coop.GetVehicleNetworkId(40@)", registration)
        self.assertGreaterEqual(registration.count("> 5000"), 2)
        self.assertGreaterEqual(registration.count("548@(554@,3i) = -1"), 3)
        self.assertNotIn("wait ", registration.lower())
        self.assertNotIn("goto @PIMP_COOP_POLL_ENTITY_REGISTRATION", registration)

    def test_supporters_receive_targeted_worker_client_and_vehicle_guidance(self):
        selector = self.text.split(":PIMP_COOP_SELECT_OBJECTIVE", 1)[1].split(
            ":PIMP_COOP_POLL_ENTITY_REGISTRATION", 1
        )[0]
        self.assertIn("35@(37@,2i)", selector)
        self.assertIn("91@(511@,28i)", selector)
        self.assertIn("91@(512@,28i)", selector)
        objectives = self.text.split(":PIMP_COOP_UPDATE_OBJECTIVES", 1)[1].split(
            ":PIMP_COOP_NOTIFY_FAILURE", 1
        )[0]
        for evidence in (
            "Coop.ClearAllEntityBlipsForNetworkPlayer",
            "Coop.RemoveCheckpointForNetworkPlayer",
            "Coop.UpdateCheckpointForNetworkPlayer",
            "Coop.UpdateCharBlipForNetworkPlayer",
            "Coop.UpdateCarBlipForNetworkPlayer",
            "Coop.PrintNowForNetworkPlayer('PIMP_55'",
            "Coop.PrintNowForNetworkPlayer('PIMP_2'",
        ):
            self.assertIn(evidence, objectives)

    def test_results_and_cleanup_are_exactly_once_and_idempotent(self):
        self.assertEqual(self.text.count("gosub @PIMP_COOP_NOTIFY_FAILURE"), 1)
        self.assertEqual(self.text.count("gosub @PIMP_COOP_NOTIFY_PASS"), 1)
        self.assertEqual(self.text.count("gosub @PIMP_COOP_CLEANUP"), 1)
        self.assertIn("561@ = 1", self.text)
        self.assertIn("561@ = 2", self.text)
        self.assertIn("Coop.PrintBigForNetworkPlayer('M_FAIL'", self.text)
        self.assertIn("Coop.PrintBigForNetworkPlayer('M_PASSD'", self.text)
        cleanup = self.text.split(":PIMP_COOP_CLEANUP", 1)[1]
        self.assertIn("562@ == 1", cleanup)
        self.assertIn("562@ = 1", cleanup)
        self.assertIn("Coop.ClearAllEntityBlipsForNetworkPlayer", cleanup)
        self.assertIn("Coop.RemoveCheckpointForNetworkPlayer", cleanup)
        self.assertIn("Char.FreezePosition(542@(554@,3i), False)", cleanup)
        self.assertIn("Player.SetControl($player1, True)", cleanup)
        self.assertIn("deterministic weapon-restore policy", cleanup)
        self.assertEqual(self.text.count("Mission.Finish"), 1)


if __name__ == "__main__":
    unittest.main()
