import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
TAXI = ROOT / "scm/scripts/TAXIODD.txt"


class TaxiSubmissionCoopStructureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = TAXI.read_text(encoding="utf-8")

    def test_host_authority_and_frozen_identity_roster(self):
        self.assertIn("Coop.EnableSyncingThisScript()", self.text)
        self.assertIn("Coop.IsHost()", self.text)
        self.assertIn(
            "200@, 201@, 202@ = Coop.CollectNetworkPlayersForTheMission()",
            self.text,
        )
        self.assertRegex(
            self.text,
            r"203@\(216@,3i\)\s*=\s*Coop\.GetNetworkPlayerInternalId",
        )
        refresh = self.text.split(":TAXIODD_COOP_REFRESH_ROSTER", 1)[1].split(
            ":TAXIODD_COOP_VALIDATE_SLOT", 1
        )[0]
        self.assertIn("209@, 210@, 211@ = Coop.CollectNetworkPlayersForTheMission()", refresh)
        self.assertIn("219@ == 203@(216@,3i)", refresh)
        self.assertIn("200@(216@,3i) = 0", refresh)

    def test_host_stock_fares_and_fifty_fare_unlock_stay_single_owner(self):
        self.assertEqual(self.text.count("$g_Taxiodd_faresKM += 1"), 1)
        self.assertEqual(self.text.count("$taxi_passed += 1"), 1)
        self.assertEqual(self.text.count("Stat.RegisterOddjobMissionPassed"), 1)
        self.assertEqual(self.text.count("Game.SetAllTaxisHaveNitro(True)"), 1)
        self.assertIn("$taxi_passed > 49", self.text)
        self.assertIn("$done_taxiodd_progress = 1", self.text)
        self.assertNotRegex(
            self.text.split(":TAXIODD_COOP_INIT", 1)[1],
            r"(?:Player\.AddScore|\$g_Taxiodd_faresKM\s*\+=|\$taxi_passed\s*\+=)",
        )

    def test_disconnect_is_dnf_but_connected_death_fails_canonically(self):
        update = self.text.split(":TAXIODD_COOP_UPDATE", 1)[1].split(
            ":TAXIODD_COOP_POLL_ENTITY_REGISTRATION", 1
        )[0]
        self.assertIn("COOP_PARTICIPANT_DEATH", update)
        self.assertIn("gosub @TAXIODD_COOP_VALIDATE_SLOT", update)
        self.assertIn("Char.IsDead(200@(216@,3i))", update)
        self.assertIn("124@ = 1", update)
        self.assertIn("108@ = 3", update)
        refresh = self.text.split(":TAXIODD_COOP_REFRESH_ROSTER", 1)[1].split(
            ":TAXIODD_COOP_VALIDATE_SLOT", 1
        )[0]
        self.assertNotIn("124@ = 1", refresh)
        self.assertNotIn("212@ = 1", refresh)

    def test_registration_is_bounded_and_never_blocks_stock_progression(self):
        registration = self.text.split(
            ":TAXIODD_COOP_POLL_ENTITY_REGISTRATION", 1
        )[1].split(":TAXIODD_COOP_UPDATE_REGROUP", 1)[0]
        self.assertIn("Coop.GetVehicleNetworkId(89@)", registration)
        self.assertIn("Coop.GetPedNetworkId(88@)", registration)
        self.assertGreaterEqual(registration.count("> 5000"), 2)
        self.assertNotIn("wait ", registration.lower())
        self.assertNotRegex(registration, r"goto\s+@TAXIODD_COOP_POLL_ENTITY_REGISTRATION")

    def test_support_objectives_regroup_and_vehicle_access_are_shared(self):
        for evidence in (
            "Car.LockDoors(89@, CarLock.Unlocked)",
            "Coop.TeleportPlayersToHostSafely(200@, 201@, 202@)",
            "Coop.UpdateCharBlipForNetworkPlayer",
            "Coop.UpdateCarBlipForNetworkPlayer",
            "Coop.UpdateCheckpointForNetworkPlayer",
            "Coop.PrintNowForNetworkPlayer('TX_PKUP'",
            "Coop.PrintNowForNetworkPlayer('TX_TIME'",
        ):
            self.assertIn(evidence, self.text)
        self.assertIn("237@(216@,3i) == 0", self.text)
        self.assertIn("237@(216@,3i) = 1", self.text)
        self.assertIn("223@ <> -1", self.text)
        self.assertIn("224@ <> -1", self.text)

    def test_results_and_cleanup_are_exactly_once_and_keep_stock_labels(self):
        self.assertIn(":TAXIODD_18818", self.text)
        self.assertIn(":TAXIODD_18991", self.text)
        self.assertEqual(self.text.count("gosub @TAXIODD_COOP_NOTIFY_FAILURE"), 1)
        failure_label = self.text.index(":TAXIODD_18818")
        stock_end = self.text.index("Text.PrintBig('TX_END'", failure_label)
        shared_failure = self.text.index("gosub @TAXIODD_COOP_NOTIFY_FAILURE", stock_end)
        self.assertGreater(shared_failure, stock_end)
        self.assertEqual(self.text.count("gosub @TAXIODD_COOP_NOTIFY_PASS"), 1)
        self.assertIn("213@ = 1", self.text)
        self.assertIn("213@ = 2", self.text)
        self.assertIn("Coop.PrintBigForNetworkPlayer('M_FAIL'", self.text)
        self.assertIn("Coop.PrintBigForNetworkPlayer('M_PASSD'", self.text)
        cleanup = self.text.split(":TAXIODD_COOP_CLEANUP", 1)[1].split(
            "//-------------Mission 122", 1
        )[0]
        self.assertIn("214@ == 1", cleanup)
        self.assertIn("Coop.ClearAllEntityBlipsForNetworkPlayer", cleanup)
        self.assertIn("Coop.RemoveCheckpointForNetworkPlayer", cleanup)
        self.assertIn("Char.FreezePosition(200@(216@,3i), False)", cleanup)
        self.assertIn("Char.SetProofs(200@(216@,3i), False", cleanup)
        self.assertIn("Player.SetControl($player1, True)", cleanup)
        self.assertIn("preserving that", cleanup)


if __name__ == "__main__":
    unittest.main()
