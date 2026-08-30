import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
FIREFIGHTER = ROOT / "scm/scripts/FIRETRU.txt"


class FirefighterSubmissionCoopStructureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = FIREFIGHTER.read_text(encoding="utf-8")

    def test_natural_launch_body_is_host_authoritative_and_uses_frozen_roster(self):
        prologue = self.text.split(":FIRETRU_16", 1)[0]
        self.assertIn("Coop.EnableSyncingThisScript()", prologue)
        self.assertIn("Coop.IsHost()", prologue)
        self.assertIn(":FIRETRU_COOP_REJECT_NON_HOST", prologue)
        self.assertIn("terminate_this_script", prologue)
        self.assertIn(
            "200@, 201@, 202@ = Coop.CollectNetworkPlayersForTheMission()",
            self.text,
        )
        self.assertIn(
            "203@(216@,3i) = Coop.GetNetworkPlayerInternalId",
            self.text,
        )

    def test_reconnect_requires_frozen_identity_and_disconnect_is_nonblocking_dnf(self):
        refresh = self.text.split(":FIRETRU_COOP_REFRESH_ROSTER", 1)[1].split(
            ":FIRETRU_COOP_VALIDATE_SLOT", 1
        )[0]
        self.assertIn(
            "209@, 210@, 211@ = Coop.CollectNetworkPlayersForTheMission()",
            refresh,
        )
        self.assertIn("220@ == 203@(216@,3i)", refresh)
        self.assertIn("200@(216@,3i) = 0", refresh)
        self.assertIn("206@(216@,3i) = -1", refresh)
        self.assertIn("Coop.TeleportPlayersToHostSafely", refresh)
        self.assertNotIn("212@ = 1", refresh)
        self.assertIn("disconnecting a frozen participant is a DNF", self.text)

    def test_connected_frozen_participant_death_fails_through_stock_path(self):
        update = self.text.split(":FIRETRU_COOP_UPDATE", 1)[1].split(
            ":FIRETRU_COOP_RESET_LEVEL_REGISTRATION", 1
        )[0]
        self.assertIn("gosub @FIRETRU_COOP_VALIDATE_SLOT", update)
        self.assertIn("Char.IsDead(200@(216@,3i))", update)
        self.assertIn("COOP_PARTICIPANT_DEATH", update)
        self.assertIn("212@ = 1", update)
        self.assertIn("77@ = 1", update)
        abort = self.text.split(":FIRETRU_8348", 1)[1].split(
            ":FIRETRU_8411", 1
        )[0]
        self.assertIn("gosub @FIRETRU_COOP_UPDATE", abort)
        self.assertIn("212@ == 1", abort)

    def test_all_incident_registration_is_bounded_and_never_blocks_stock(self):
        registration = self.text.split(
            ":FIRETRU_COOP_POLL_ENTITY_REGISTRATION", 1
        )[1].split(":FIRETRU_COOP_UPDATE_REGROUP", 1)[0]
        self.assertIn("for 267@ = 0 to 2", registration)
        self.assertIn("for 267@ = 0 to 8", registration)
        self.assertIn("Coop.GetVehicleNetworkId(268@)", registration)
        self.assertIn("Coop.GetPedNetworkId(268@)", registration)
        self.assertGreaterEqual(registration.count("> 5000"), 2)
        self.assertGreaterEqual(registration.count("206@(216@,3i) = -1"), 2)
        self.assertNotIn("wait ", registration.lower())
        self.assertNotIn("goto @FIRETRU_COOP_POLL_ENTITY_REGISTRATION", registration)
        for handle in range(35, 38):
            self.assertIn(f"268@ = {handle}@", registration)
        for handle in range(40, 49):
            self.assertIn(f"268@ = {handle}@", registration)

    def test_previous_level_handles_cannot_be_registered_during_spawn_search(self):
        self.assertIn(
            ":FIRETRU_789\n272@ = 0 // current level entities are not ready",
            self.text,
        )
        self.assertIn(
            ":FIRETRU_2800\n272@ = 1 // all active vehicles and victims",
            self.text,
        )
        update = self.text.split(":FIRETRU_COOP_UPDATE", 1)[1].split(
            ":FIRETRU_COOP_RESET_LEVEL_REGISTRATION", 1
        )[0]
        readiness = update.index("272@ == 0")
        registration = update.index("gosub @FIRETRU_COOP_RESET_LEVEL_REGISTRATION")
        self.assertLess(readiness, registration)
        self.assertIn("Never register or target them as the next incident", update)

    def test_support_players_receive_current_fire_or_victim_objective(self):
        objective = self.text.split(":FIRETRU_COOP_UPDATE_OBJECTIVE", 1)[1].split(
            ":FIRETRU_COOP_NOTIFY_FAILURE", 1
        )[0]
        for stock_state in ("92@ <> 10", "93@ <> 10", "94@ <> 10"):
            self.assertIn(stock_state, objective)
        for stock_state in range(95, 104):
            self.assertIn(f"{stock_state}@ < 2", objective)
        for evidence in (
            "Coop.UpdateCarBlipForNetworkPlayer",
            "Coop.UpdateCharBlipForNetworkPlayer",
            "Coop.UpdateCheckpointForNetworkPlayer",
            "Coop.PrintNowForNetworkPlayer('SPRAY_1'",
            "Coop.ClearAllEntityBlipsForNetworkPlayer",
            "Coop.RemoveCheckpointForNetworkPlayer",
        ):
            self.assertIn(evidence, objective)
        self.assertIn("206@(216@,3i) = 221@", objective)

    def test_stock_progression_rewards_and_persistent_unlock_remain_single_owner(self):
        self.assertEqual(self.text.count("$db_firetruck_level += 1"), 1)
        self.assertEqual(self.text.count("Player.MakeFireProof($player1, True)"), 1)
        self.assertEqual(self.text.count("Stat.RegisterOddjobMissionPassed"), 1)
        self.assertEqual(self.text.count("$done_firetruck_progress = 1"), 1)
        self.assertIn("Hud.DisplayTimerWithString($fire_time_limit", self.text)
        self.assertIn("$fire_time_limit = 60000", self.text)
        self.assertIn("ScriptFire.CreateCarFire", self.text)
        self.assertIn("ScriptFire.CreateCharFire", self.text)
        coop = self.text.split("// Co-op policy for Firefighter:", 1)[1]
        self.assertNotIn("Player.AddScore", coop)
        self.assertNotIn("Stat.RegisterOddjobMissionPassed", coop)
        self.assertNotIn("Player.MakeFireProof", coop)

    def test_pass_fail_and_cleanup_are_exactly_once_and_idempotent(self):
        self.assertEqual(self.text.count("gosub @FIRETRU_COOP_NOTIFY_PASS"), 1)
        self.assertEqual(self.text.count("gosub @FIRETRU_COOP_NOTIFY_FAILURE"), 1)
        self.assertEqual(self.text.count("gosub @FIRETRU_COOP_CLEANUP"), 1)
        self.assertIn("213@ = 1", self.text)
        self.assertIn("213@ = 2", self.text)
        self.assertIn("Coop.PrintBigForNetworkPlayer('M_FAIL'", self.text)
        self.assertIn("Coop.PrintBigForNetworkPlayer('M_PASSD'", self.text)
        cleanup = self.text.split(":FIRETRU_COOP_CLEANUP", 1)[1].split(
            "//-------------Mission 124", 1
        )[0]
        self.assertIn("214@ == 1", cleanup)
        self.assertIn("214@ = 1", cleanup)
        self.assertIn("Coop.ClearAllEntityBlipsForNetworkPlayer", cleanup)
        self.assertIn("Coop.RemoveCheckpointForNetworkPlayer", cleanup)
        self.assertIn("Coop.ClearThisPrintForNetworkPlayer('SPRAY_1'", cleanup)
        self.assertIn("Char.FreezePosition(200@(216@,3i), False)", cleanup)
        self.assertIn("Player.SetControl($player1, True)", cleanup)
        self.assertIn("deterministic weapon-restore policy", cleanup)


if __name__ == "__main__":
    unittest.main()
