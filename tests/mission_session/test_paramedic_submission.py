import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
PARAMEDIC = ROOT / "scm/scripts/AMBULAN.txt"


class ParamedicSubmissionCoopStructureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = PARAMEDIC.read_text(encoding="utf-8")

    def test_host_authority_and_frozen_identity_roster(self):
        self.assertIn("Coop.EnableSyncingThisScript()", self.text)
        self.assertIn("Coop.IsHost()", self.text)
        self.assertIn(
            "0@, 1@, 2@ = Coop.CollectNetworkPlayersForTheMission()",
            self.text,
        )
        self.assertRegex(
            self.text,
            r"3@\(13@,3i\)\s*=\s*Coop\.GetNetworkPlayerInternalId",
        )
        refresh = self.text.split(":AMBULAN_COOP_REFRESH_ROSTER", 1)[1].split(
            ":AMBULAN_COOP_VALIDATE_SLOT", 1
        )[0]
        self.assertIn(
            "14@, 15@, 16@ = Coop.CollectNetworkPlayersForTheMission()", refresh
        )
        self.assertIn("29@ == 3@(13@,3i)", refresh)
        self.assertIn("0@(13@,3i) = 0", refresh)
        self.assertNotIn("3@(13@,3i) = Coop.GetNetworkPlayerInternalId", refresh)

    def test_stock_patient_rules_progression_and_persistence_remain_host_owned(self):
        self.assertEqual(self.text.count("Char.CreateRandom("), 12)
        self.assertEqual(self.text.count("Task.EnterCarAsPassenger(77@, 34@"), 2)
        self.assertEqual(self.text.count("Stat.RegisterInt(StatId.AmbulanceLevel"), 1)
        self.assertEqual(self.text.count("$db_ambulance_level += 1"), 1)
        self.assertEqual(self.text.count("Stat.RegisterOddjobMissionPassed"), 1)
        self.assertEqual(self.text.count("$done_ambulance_progress = 1"), 1)
        self.assertEqual(
            self.text.count("Stat.IncrementFloatNoMessage(StatId.MaxHealth"), 1
        )
        self.assertIn("41@ *= 50", self.text)
        self.assertIn("sub_int_lvar_from_int_var $ped_time_limit -= 41@", self.text)
        self.assertIn("Car.SetHealth(34@, 54@)", self.text)

        coop = self.text.split(":AMBULAN_COOP_INIT", 1)[1]
        for host_only_effect in (
            "Char.CreateRandom(",
            "Player.AddScore",
            "$db_ambulance_level +=",
            "$done_ambulance_progress =",
            "Stat.RegisterOddjobMissionPassed",
            "Stat.IncrementFloatNoMessage",
        ):
            self.assertNotIn(host_only_effect, coop)

    def test_disconnect_is_dnf_and_connected_death_fails_deterministically(self):
        refresh = self.text.split(":AMBULAN_COOP_REFRESH_ROSTER", 1)[1].split(
            ":AMBULAN_COOP_VALIDATE_SLOT", 1
        )[0]
        self.assertNotIn("12@ = 1", refresh)
        self.assertNotIn("79@ = 1", refresh)

        update = self.text.split(":AMBULAN_COOP_UPDATE", 1)[1].split(
            ":AMBULAN_COOP_SELECT_OBJECTIVE", 1
        )[0]
        self.assertIn("gosub @AMBULAN_COOP_VALIDATE_SLOT", update)
        self.assertIn("Char.IsDead(0@(13@,3i))", update)
        self.assertIn("12@ = 1", update)
        failure_bridge = self.text.split(":AMBULAN_11334", 1)[1].split(
            ":AMBULAN_11380", 1
        )[0]
        self.assertIn("COOP_PARTICIPANT_DEATH", failure_bridge)
        self.assertIn("79@ = 1", failure_bridge)
        self.assertIn("goto @AMBULAN_13159", failure_bridge)

    def test_all_twelve_stock_patient_slots_feed_stable_support_objectives(self):
        selector = self.text.split(":AMBULAN_COOP_SELECT_OBJECTIVE", 1)[1].split(
            ":AMBULAN_COOP_POLL_ENTITY_REGISTRATION", 1
        )[0]
        expected_pairs = (
            (81, 83),
            (84, 86),
            (87, 89),
            (90, 92),
            (93, 95),
            (56, 58),
            (59, 61),
            (62, 64),
            (65, 67),
            (68, 70),
            (71, 73),
            (74, 76),
        )
        for actor, state in expected_pairs:
            self.assertIn(f"set_lvar_int_to_lvar_int 19@ = {actor}@", selector)
            self.assertIn(f"set_lvar_int_to_lvar_int 28@ = {state}@", selector)
        self.assertIn("28@ >= 3", selector)
        self.assertIn("18@ = 2", selector)
        self.assertIn("18@ = 1", selector)

    def test_entity_registration_is_bounded_and_nonblocking(self):
        registration = self.text.split(
            ":AMBULAN_COOP_POLL_ENTITY_REGISTRATION", 1
        )[1].split(":AMBULAN_COOP_UPDATE_REGROUP", 1)[0]
        self.assertIn("Coop.GetVehicleNetworkId(34@)", registration)
        self.assertIn("Coop.GetPedNetworkId(19@)", registration)
        self.assertGreaterEqual(registration.count("> 5000"), 2)
        self.assertNotIn("wait ", registration.lower())
        self.assertNotRegex(
            registration, r"goto\s+@AMBULAN_COOP_POLL_ENTITY_REGISTRATION"
        )

    def test_connected_supporters_receive_targeted_pickup_and_delivery_guidance(self):
        objectives = self.text.split(":AMBULAN_COOP_UPDATE_OBJECTIVES", 1)[1].split(
            ":AMBULAN_COOP_NOTIFY_FAILURE", 1
        )[0]
        for evidence in (
            "Coop.ClearAllEntityBlipsForNetworkPlayer",
            "Coop.RemoveCheckpointForNetworkPlayer",
            "Coop.UpdateCharBlipForNetworkPlayer",
            "Coop.UpdateCarBlipForNetworkPlayer",
            "Coop.UpdateCheckpointForNetworkPlayer(29@, 30@, 31@",
            "Coop.UpdateCheckpointForNetworkPlayer(167@, 168@, 169@",
            "Coop.PrintNowForNetworkPlayer('ATUTOR2'",
        ):
            self.assertIn(evidence, objectives)
        self.assertIn("20@ <> -1", objectives)
        self.assertIn("23@ <> -1", objectives)
        self.assertIn("Coop.TeleportPlayersToHostSafely(0@, 1@, 2@)", self.text)

    def test_results_and_cleanup_are_exactly_once_and_idempotent(self):
        self.assertEqual(self.text.count("gosub @AMBULAN_COOP_NOTIFY_FAILURE"), 1)
        self.assertEqual(self.text.count("gosub @AMBULAN_COOP_NOTIFY_PASS"), 1)
        self.assertEqual(self.text.count("gosub @AMBULAN_COOP_CLEANUP"), 1)
        self.assertIn("10@ = 1", self.text)
        self.assertIn("10@ = 2", self.text)
        self.assertIn("Coop.PrintBigForNetworkPlayer('M_FAIL'", self.text)
        self.assertIn("Coop.PrintBigForNetworkPlayer('M_PASSD'", self.text)

        cleanup = self.text.split(":AMBULAN_COOP_CLEANUP", 1)[1].split(
            "//-------------Mission 123", 1
        )[0]
        self.assertIn("11@ == 1", cleanup)
        self.assertIn("Coop.ClearAllEntityBlipsForNetworkPlayer", cleanup)
        self.assertIn("Coop.RemoveCheckpointForNetworkPlayer", cleanup)
        self.assertIn("Char.FreezePosition(0@(13@,3i), False)", cleanup)
        self.assertIn("Char.SetProofs(0@(13@,3i), False", cleanup)
        self.assertIn("Player.SetControl($player1, True)", cleanup)
        self.assertIn("preserving each", cleanup)
        self.assertIn("Mission.Finish", self.text)


if __name__ == "__main__":
    unittest.main()
