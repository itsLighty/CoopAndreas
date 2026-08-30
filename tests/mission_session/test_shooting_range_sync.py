import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class ShootingRangeCoopStructureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.launcher = (ROOT / "scm/scripts/SHOOT.txt").read_text(encoding="utf-8")
        cls.body = (ROOT / "scm/scripts/SHRANGE.txt").read_text(encoding="utf-8")
        cls.stock, cls.coop = cls.body.split(
            "// Co-op policy for the complete four-weapon, three-round shooting range:",
            1,
        )

    def test_launcher_authority_gate_precedes_every_launch_mutation(self):
        authority = self.launcher.index("Coop.IsHost()")
        onmission = self.launcher.index("$onmission = 1")
        range_selection = self.launcher.index("$SR_range_id = $sr_i")
        launch = self.launcher.index("Coop.LaunchMissionForCoop(113)")
        self.assertLess(authority, onmission)
        self.assertLess(authority, range_selection)
        self.assertLess(authority, launch)
        self.assertNotIn("Mission.LoadAndLaunchInternal(113)", self.launcher)
        self.assertRegex(
            self.launcher,
            r"Coop\.IsHost\(\)\s+goto_if_false @SHOOT_324",
        )

    def test_body_rejects_non_host_before_stock_state_or_save_reads(self):
        self.assertIn("Coop.EnableSyncingThisScript()", self.body)
        reject = self.body.index(":SHRANGE_COOP_REJECT_NON_HOST")
        host_start = self.body.index(":SHRANGE_COOP_HOST_START")
        first_onmission = self.body.index("$onmission = 1")
        first_stat = self.body.index("Stat.GetInt(181)")
        self.assertLess(reject, host_start)
        self.assertLess(host_start, first_onmission)
        self.assertLess(host_start, first_stat)
        self.assertRegex(
            self.body,
            r":SHRANGE_COOP_REJECT_NON_HOST\s+terminate_this_script",
        )

    def test_all_four_weapon_modes_and_twelve_stock_rounds_remain(self):
        expected_player_weapons = {
            "Char.GiveWeapon($scplayer, WeaponType.Pistol, 99999)": 2,
            "Char.GiveWeapon($scplayer, WeaponType.MicroUzi, 99999)": 1,
            "Char.GiveWeapon($scplayer, WeaponType.Shotgun, 99999)": 1,
            "Char.GiveWeapon($scplayer, WeaponType.Ak47, 99999)": 1,
        }
        for instruction, expected in expected_player_weapons.items():
            self.assertEqual(self.stock.count(instruction), expected)
        for round_index in range(12):
            self.assertIn(f"$SR_skill_won == {round_index}", self.stock)
        self.assertEqual(self.stock.count("$SR_skill_won += 1"), 1)
        self.assertEqual(self.stock.count("Object.HasBeenDamaged("), 2)
        self.assertEqual(
            self.stock.count("add_int_var_to_int_var $sr_Score"), 2
        )

    def test_stock_state_machine_shape_and_target_counts_are_unchanged(self):
        expected_state_assignments = {
            "$SR_mission_state = 0": 2,
            "$SR_mission_state = 1": 5,
            "$SR_mission_state = 2": 10,
            "$SR_mission_state = 3": 3,
            "$SR_mission_state = 4": 2,
            "$SR_mission_state = 5": 2,
            "$SR_mission_state = 20": 9,
        }
        for assignment, expected in expected_state_assignments.items():
            self.assertEqual(self.stock.count(assignment), expected)
        self.assertEqual(self.stock.count("Object.Create("), 3)
        self.assertEqual(self.stock.count("Object.CreateNoOffset("), 3)
        self.assertEqual(
            self.stock.count("Hud.DisplayCounterWithString($sr_Score"), 2
        )

    def test_stock_rewards_stats_and_save_progress_remain_single_owner(self):
        expected_mutations = {
            "Stat.IncrementFloat(": 8,
            "Stat.PlayerMadeProgress(1)": 4,
            "Stat.SetInt(StatId.ShootingRangeScore": 1,
            "Stat.SetFloat(StatId.WeapontypePistolSkill, 1000.0)": 1,
            "Player.AddScore($player1, 10000)": 1,
            "$player_has_fast_reload = 1": 1,
            "$SR_skill_award_given += 1": 1,
            "Audio.PlayMissionPassedTune(2)": 1,
        }
        for mutation, expected in expected_mutations.items():
            self.assertEqual(self.stock.count(mutation), expected)
            self.assertNotIn(mutation, self.coop)
        self.assertNotRegex(
            self.coop,
            r"\$(?:SR_mission_state|SR_target_state|SR_range_level|"
            r"SR_skill_won|SR_skill_award_given|sr_Score)\s*(?:=(?!=)|\+=|-=)",
        )

    def test_frozen_roster_is_identity_bound_and_reconnectable(self):
        self.assertIn(
            "600@, 601@, 602@ = Coop.CollectNetworkPlayersForTheMission()",
            self.coop,
        )
        self.assertRegex(
            self.coop,
            r"603@\(620@,3i\)\s*=\s*Coop\.GetNetworkPlayerInternalId",
        )
        refresh = self.coop.split(":SHRANGE_COOP_REFRESH_ROSTER", 1)[1].split(
            ":SHRANGE_COOP_VALIDATE_SLOT", 1
        )[0]
        self.assertIn(
            "609@, 610@, 611@ = Coop.CollectNetworkPlayersForTheMission()",
            refresh,
        )
        self.assertIn("623@ == 603@(620@,3i)", refresh)
        self.assertIn("600@(620@,3i) = 0", refresh)
        self.assertNotRegex(refresh, r"603@\(620@,3i\)\s*=\s*623@")

    def test_spectators_are_staged_without_mutating_their_loadouts(self):
        self.assertIn("Coop.TeleportPlayersToHostSafely", self.coop)
        self.assertIn("Task.StandStill(600@(620@,3i), -1)", self.coop)
        self.assertIn(
            "Char.HideWeaponForScriptedCutscene(600@(620@,3i), True)",
            self.coop,
        )
        self.assertIn("Char.FreezePosition(600@(620@,3i), True)", self.coop)
        self.assertIn("Char.SetCollision(600@(620@,3i), False)", self.coop)
        self.assertIn("Char.SetProofs(600@(620@,3i), True", self.coop)
        self.assertNotIn("Char.GiveWeapon", self.coop)
        self.assertNotIn("Char.RemoveWeapon", self.coop)

    def test_spectator_hold_is_only_applied_on_stage_or_reconnect(self):
        update = self.coop.split(":SHRANGE_COOP_UPDATE", 1)[1].split(
            ":SHRANGE_COOP_STAGE_SPECTATORS", 1
        )[0]
        stage = self.coop.split(":SHRANGE_COOP_STAGE_SPECTATORS", 1)[1].split(
            ":SHRANGE_COOP_HOLD_SPECTATORS", 1
        )[0]
        self.assertNotIn("gosub @SHRANGE_COOP_HOLD_SPECTATORS", update)
        self.assertEqual(stage.count("gosub @SHRANGE_COOP_HOLD_SPECTATORS"), 1)
        self.assertIn("625@ = 0", stage)
        self.assertIn("625@ == 1", update)
        self.assertIn("gosub @SHRANGE_COOP_STAGE_SPECTATORS", update)

    def test_peer_objectives_results_and_cleanup_are_bounded_and_idempotent(self):
        self.assertIn("Coop.UpdateCheckpointForNetworkPlayer", self.coop)
        self.assertIn("Coop.PrintNowForNetworkPlayer('ANR_1'", self.coop)
        self.assertIn("Coop.PrintBigForNetworkPlayer('M_PASSD'", self.coop)
        self.assertIn("Coop.PrintBigForNetworkPlayer('M_FAIL'", self.coop)
        cleanup = self.coop.split(":SHRANGE_COOP_CLEANUP", 1)[1]
        self.assertIn("616@ == 1", cleanup)
        self.assertIn("Coop.RemoveCheckpointForNetworkPlayer", self.coop)
        self.assertIn("Task.StandStill(600@(620@,3i), 0)", self.coop)
        self.assertIn(
            "Char.HideWeaponForScriptedCutscene(600@(620@,3i), False)",
            self.coop,
        )
        self.assertIn("Char.SetCollision(600@(620@,3i), True)", self.coop)
        self.assertIn("Char.SetProofs(600@(620@,3i), False", self.coop)

    def test_lifecycle_hooks_cover_start_tick_round_result_and_area_exit(self):
        self.assertEqual(self.stock.count("gosub @SHRANGE_COOP_INIT"), 1)
        self.assertEqual(self.stock.count("gosub @SHRANGE_COOP_UPDATE"), 1)
        self.assertEqual(self.stock.count("gosub @SHRANGE_COOP_CLEANUP"), 1)
        self.assertIn("$SR_mission_state == 3", self.coop)
        self.assertIn("$SR_mission_state == 20", self.coop)
        self.assertIn("$SR_mission_state == 0", self.coop)
        self.assertRegex(
            self.coop,
            r"\$SR_skill_won > 11\s+then\s+if\s+618@ <> 2",
        )


if __name__ == "__main__":
    unittest.main()
