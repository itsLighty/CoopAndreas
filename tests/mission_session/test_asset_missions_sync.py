import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "scm/scripts"

TRUCKS = (SCRIPTS / "TRUCKS.txt").read_text(encoding="utf-8")
TRUCK = (SCRIPTS / "TRUCK.txt").read_text(encoding="utf-8")
VALET_L = (SCRIPTS / "VALET_L.txt").read_text(encoding="utf-8")
VALET = (SCRIPTS / "VALET.txt").read_text(encoding="utf-8")
QUARRYS = (SCRIPTS / "QUARRYS.txt").read_text(encoding="utf-8")
QUARRY = (SCRIPTS / "QUARRY.txt").read_text(encoding="utf-8")


class AssetMissionCoopStructureTests(unittest.TestCase):
    def test_launchers_and_selectors_map_exactly_to_stock_assets(self):
        self.assertEqual(TRUCKS.count("Coop.LaunchMissionForCoop(117)"), 1)
        self.assertEqual(QUARRYS.count("Coop.LaunchMissionForCoop(118)"), 1)
        self.assertEqual(VALET_L.count("StreamedScript.StartNew(72)"), 1)
        for launcher in (TRUCKS, VALET_L, QUARRYS):
            self.assertIn("Coop.IsHost()", launcher)
            self.assertNotIn("Coop.IsNetworkAuthenticated()", launcher)

        self.assertRegex(
            TRUCK,
            r"switch_start \$g_nTruckMissionsPassed total_jumps 8 .*?"
            r"jumps 0 @TRUCK_8451 1 @TRUCK_8465 2 @TRUCK_8479 "
            r"3 @TRUCK_8493 4 @TRUCK_8507 5 @TRUCK_8521 6 @TRUCK_8552",
        )
        self.assertRegex(
            QUARRY,
            r"switch_start 198@ total_jumps 7 .*?jumps 1 @QUARRY_21698 "
            r"2 @QUARRY_21712 3 @QUARRY_21782 4 @QUARRY_21726 "
            r"5 @QUARRY_21740 6 @QUARRY_21754 7 @QUARRY_21768",
        )
        self.assertIn("$valet_cars_to_park = 3", VALET)
        self.assertIn("18@ = 1", VALET)
        self.assertIn("18@ += 1", VALET)
        self.assertIn("18@ == 6", VALET)

    def test_all_three_bodies_reject_independent_peer_instances(self):
        for text, prefix in (
            (TRUCK, "TRUCK"),
            (VALET, "VALET"),
            (QUARRY, "QUARRY"),
        ):
            prologue = text.split(f":{prefix}_COOP_HOST_START", 1)[0]
            self.assertIn("Coop.EnableSyncingThisScript()", prologue)
            self.assertIn("Coop.IsHost()", prologue)
            self.assertRegex(
                prologue,
                rf":{prefix}_COOP_REJECT_NON_HOST\s+terminate_this_script",
            )
            self.assertNotIn("Coop.IsNetworkAuthenticated()", text)

    def test_frozen_rosters_are_identity_bound_and_reconnectable(self):
        for text, prefix, base in (
            (TRUCK, "TRUCK", 200),
            (QUARRY, "QUARRY", 300),
        ):
            coop = text.split(f":{prefix}_COOP_INIT", 1)[1]
            self.assertIn(
                f"{base}@, {base + 1}@, {base + 2}@ = "
                "Coop.CollectNetworkPlayersForTheMission()",
                coop,
            )
            self.assertIn(
                f"{base + 3}@({base + 16}@,3i) = "
                f"Coop.GetNetworkPlayerInternalId({base}@({base + 16}@,3i))",
                coop,
            )
            refresh = coop.split(f":{prefix}_COOP_REFRESH_ROSTER", 1)[1].split(
                f":{prefix}_COOP_VALIDATE_SLOT", 1
            )[0]
            self.assertIn(
                f"{base + 9}@, {base + 10}@, {base + 11}@ = "
                "Coop.CollectNetworkPlayersForTheMission()",
                refresh,
            )
            self.assertIn(
                f"{base}@({base + 16}@,3i) = 0", refresh
            )
            self.assertIn(
                f"{base + 19}@ == {base + 3}@({base + 16}@,3i)", refresh
            )
            self.assertNotRegex(
                refresh,
                rf"{base + 3}@\({base + 16}@,3i\)\s*=\s*{base + 19}@",
            )
            self.assertIn("nonblocking DNF", text)

    def test_disconnect_death_reconnect_and_regroup_are_deterministic(self):
        cases = (
            (TRUCK, "TRUCK", 200, 212, "goto @TRUCK_896"),
            (QUARRY, "QUARRY", 300, 312, "goto @QUARRY_2224"),
        )
        for text, prefix, base, latch, bridge in cases:
            update = text.split(f":{prefix}_COOP_UPDATE", 1)[1].split(
                f":{prefix}_COOP_POLL_VEHICLE_REGISTRATION", 1
            )[0]
            self.assertIn("COOP_PARTICIPANT_DEATH", update)
            self.assertIn(f"Char.IsDead({base}@({base + 16}@,3i))", update)
            self.assertIn(f"{latch}@ = 1", update)
            self.assertIn(bridge, text)
            regroup = text.split(f":{prefix}_COOP_UPDATE_REGROUP", 1)[1].split(
                f":{prefix}_COOP_UPDATE_OBJECTIVES", 1
            )[0]
            self.assertIn("Coop.TeleportPlayersToHostSafely", regroup)
            self.assertIn("> 20000", regroup)

    def test_vehicle_registration_is_bounded_and_shared_policy_is_explicit(self):
        expectations = (
            (TRUCK, "TRUCK", "Coop.GetVehicleNetworkId(74@)"),
            (QUARRY, "QUARRY", "Coop.GetVehicleNetworkId(326@)"),
        )
        for text, prefix, handshake in expectations:
            poll = text.split(
                f":{prefix}_COOP_POLL_VEHICLE_REGISTRATION", 1
            )[1].split(f":{prefix}_COOP_UPDATE_REGROUP", 1)[0]
            self.assertIn(handshake, poll)
            self.assertIn("> 5000", poll)
            self.assertNotIn("wait ", poll.lower())
            self.assertNotRegex(
                poll, rf"goto\s+@{prefix}_COOP_POLL_VEHICLE_REGISTRATION"
            )
            self.assertIn("shared", text)

        self.assertIn("single driver seat", TRUCK)
        self.assertIn("host-operated shared objectives", QUARRY)
        self.assertIn("operator seat", QUARRY)

        selector = QUARRY.split(":QUARRY_COOP_SELECT_VEHICLE", 1)[1].split(
            ":QUARRY_COOP_POLL_VEHICLE_REGISTRATION", 1
        )[0]
        self.assertIn("switch_start 198@ total_jumps 7", selector)
        self.assertEqual(selector.count("set_lvar_int_to_lvar_int 326@ = 87@"), 4)
        self.assertEqual(selector.count("set_lvar_int_to_lvar_int 326@ = 88@"), 3)

    def test_targeted_objectives_cover_truck_valet_and_quarry_state(self):
        for text, prefix in (
            (TRUCK, "TRUCK"),
            (QUARRY, "QUARRY"),
        ):
            objectives = text.split(f":{prefix}_COOP_UPDATE_OBJECTIVES", 1)[
                1
            ].split(f":{prefix}_COOP_NOTIFY_FAILURE", 1)[0]
            for evidence in (
                "Coop.ClearAllEntityBlipsForNetworkPlayer",
                "Coop.RemoveCheckpointForNetworkPlayer",
                "Coop.UpdateCarBlipForNetworkPlayer",
                "Coop.UpdateCheckpointForNetworkPlayer",
                "Coop.PrintNowForNetworkPlayer",
            ):
                self.assertIn(evidence, objectives)
        self.assertIn("102@, 103@, 104@", TRUCK)
        self.assertIn("212@, 213@, 214@", QUARRY)

    def test_valet_namespace_freezes_ids_with_only_safe_transient_locals(self):
        local_indices = [int(value) for value in re.findall(r"\b(\d+)@", VALET)]
        self.assertTrue(local_indices)
        self.assertLessEqual(max(local_indices), 33)
        coop = VALET.split("// Co-op policy for all five Valet rounds", 1)[1]
        coop_code = re.sub(r"(?m)//.*$", "", coop)
        coop_locals = {int(value) for value in re.findall(r"\b(\d+)@", coop_code)}
        self.assertEqual(coop_locals, {16, 21, 22, 23, 31, 32, 33})
        self.assertGreaterEqual(
            coop.count(
                "31@, 32@, 33@ = Coop.CollectNetworkPlayersForTheMission()"
            ),
            2,
        )
        for slot in range(3):
            self.assertIn(
                f"$COOP_VALET_ID_{slot} = "
                f"Coop.GetNetworkPlayerInternalId({31 + slot}@)",
                coop,
            )
            self.assertIn(f"$COOP_VALET_ID_{slot} = -1", coop)
            self.assertIn(f"$COOP_VALET_PLAYER_{slot} = 0", coop)
        self.assertEqual(coop.count("set_var_int_to_lvar_int $COOP_VALET_PLAYER_"), 12)
        self.assertNotIn("set_lvar_int_to_var_int $COOP_VALET_PLAYER_", coop)

        refresh = coop.split(":VALET_COOP_REFRESH_ROSTER", 1)[1].split(
            ":VALET_COOP_UPDATE", 1
        )[0]
        self.assertEqual(refresh.count("$COOP_VALET_PLAYER_0 = 0"), 1)
        self.assertEqual(refresh.count("$COOP_VALET_PLAYER_1 = 0"), 1)
        self.assertEqual(refresh.count("$COOP_VALET_PLAYER_2 = 0"), 1)
        for slot in range(3):
            self.assertIn(
                "$COOP_VALET_CANDIDATE_ID == " f"$COOP_VALET_ID_{slot}",
                refresh,
            )
            self.assertNotIn(f"$COOP_VALET_ID_{slot} = 31@", refresh)
            self.assertNotIn(f"$COOP_VALET_ID_{slot} = 32@", refresh)
            self.assertNotIn(f"$COOP_VALET_ID_{slot} = 33@", refresh)

        namespace_globals = set(re.findall(r"\$(COOP_[A-Za-z0-9_]+)", coop))
        self.assertTrue(namespace_globals)
        self.assertTrue(
            all(name.startswith("COOP_VALET_") for name in namespace_globals)
        )
        reset = coop.split(":VALET_COOP_RESET_NAMESPACE", 1)[1].split(
            ":VALET_COOP_INIT", 1
        )[0]
        for name in namespace_globals:
            self.assertIn(f"${name} =", reset)
        cleanup = coop.split(":VALET_COOP_CLEANUP", 1)[1]
        self.assertIn("gosub @VALET_COOP_RESET_NAMESPACE", cleanup)

    def test_valet_single_instance_death_objectives_results_and_cleanup(self):
        self.assertIn("StreamedScript.GetNumberOfInstances(72)", VALET_L)
        self.assertIn("$number_of_instances_of_streamed_script == 0", VALET_L)
        self.assertIn("Coop.IsHost()", VALET_L)
        self.assertIn("$player_on_valet_mission == 0", VALET)
        self.assertIn("$onmission <> 1", VALET)

        update = VALET.split(":VALET_COOP_UPDATE", 1)[1].split(
            ":VALET_COOP_POLL_VEHICLE_REGISTRATION", 1
        )[0]
        self.assertIn("COOP_PARTICIPANT_DEATH", update)
        for slot in range(3):
            self.assertIn(f"Char.IsDead($COOP_VALET_PLAYER_{slot})", update)
        self.assertIn("$valet_mission_flag = 8", update)

        registration = VALET.split(
            ":VALET_COOP_POLL_VEHICLE_REGISTRATION", 1
        )[1].split(":VALET_COOP_UPDATE_REGROUP", 1)[0]
        self.assertIn(
            "$COOP_VALET_VEHICLE_NET_ID = "
            "Coop.GetVehicleNetworkId($valet_mission_car)",
            registration,
        )
        self.assertIn("$COOP_VALET_REG_NOW > 5000", registration)
        self.assertNotIn("wait ", registration.lower())

        objectives = VALET.split(":VALET_COOP_UPDATE_OBJECTIVES", 1)[1].split(
            ":VALET_COOP_NOTIFY_FAILURE", 1
        )[0]
        self.assertIn("Coop.UpdateCarBlipForNetworkPlayer", objectives)
        self.assertIn("Coop.UpdateCheckpointForNetworkPlayer", objectives)
        self.assertIn("21@, 22@, 23@", objectives)
        self.assertIn("$drop_off_point_x($val_Area,4f)", objectives)

        failure = VALET.split(":VALET_COOP_NOTIFY_FAILURE", 1)[1].split(
            ":VALET_COOP_NOTIFY_PASS", 1
        )[0]
        passed = VALET.split(":VALET_COOP_NOTIFY_PASS", 1)[1].split(
            ":VALET_COOP_CLEANUP", 1
        )[0]
        cleanup = VALET.split(":VALET_COOP_CLEANUP", 1)[1]
        self.assertIn("$COOP_VALET_RESULT = 1", failure)
        self.assertIn("$COOP_VALET_RESULT = 2", passed)
        self.assertIn("Coop.PrintBigForNetworkPlayer('M_FAIL'", failure)
        self.assertIn("Coop.PrintBigForNetworkPlayer('M_PASSD'", passed)
        self.assertIn("$COOP_VALET_CLEANED == 1", cleanup)
        self.assertIn("$COOP_VALET_CLEANED = 1", cleanup)
        self.assertIn("deterministic restore policy", cleanup)

    def test_stock_rewards_stats_unlocks_and_save_mutations_remain_single_owner(self):
        truck_coop = TRUCK.split("// Co-op policy for all eight", 1)[1]
        quarry_coop = QUARRY.split("// Co-op policy for all seven", 1)[1]

        self.assertEqual(TRUCK.count("Player.AddScore($player1, 83@)"), 1)
        self.assertEqual(TRUCK.count("Stat.RegisterOddjobMissionPassed"), 1)
        self.assertEqual(TRUCK.count("Stat.PlayerMadeProgress(1)"), 1)
        self.assertEqual(TRUCK.count("$done_truck_progress = 1"), 1)
        self.assertEqual(TRUCK.count("Mission.Finish"), 1)

        quarry_rewards = QUARRY.split(":QUARRY_26830", 1)[1].split(
            ":QUARRY_27023", 1
        )[0]
        for reward in (500, 1000, 2000, 3000, 5000, 7500, 10000):
            self.assertEqual(
                len(
                    re.findall(
                        rf"^\s*50@\s*=\s*{reward}\s*$",
                        quarry_rewards,
                        re.MULTILINE,
                    )
                ),
                1,
            )
        self.assertEqual(QUARRY.count("Player.AddScore($player1, 50@)"), 1)
        self.assertEqual(QUARRY.count("Stat.RegisterOddjobMissionPassed"), 1)
        self.assertEqual(QUARRY.count("$done_quarry_progress = 1"), 1)
        self.assertEqual(QUARRY.count("Mission.Finish"), 1)

        self.assertEqual(VALET.count("Player.AddScore($player1, $a_int)"), 1)
        self.assertEqual(VALET.count("Stat.PlayerMadeProgress(1)"), 1)
        self.assertEqual(VALET.count("$valet_mission_completed = 1"), 1)

        for coop in (truck_coop, quarry_coop):
            self.assertNotIn("Player.AddScore", coop)
            self.assertNotIn("Stat.RegisterOddjobMissionPassed", coop)
            self.assertNotIn("Stat.PlayerMadeProgress", coop)
            self.assertNotIn("Hud.DisplayTimer", coop)

    def test_results_and_cleanup_are_exactly_once_and_idempotent(self):
        for text, prefix, result, cleanup, base in (
            (TRUCK, "TRUCK", 213, 214, 200),
            (QUARRY, "QUARRY", 313, 314, 300),
        ):
            failure = text.split(f":{prefix}_COOP_NOTIFY_FAILURE", 1)[1].split(
                f":{prefix}_COOP_NOTIFY_PASS", 1
            )[0]
            passed = text.split(f":{prefix}_COOP_NOTIFY_PASS", 1)[1].split(
                f":{prefix}_COOP_CLEANUP", 1
            )[0]
            cleanup_body = text.split(f":{prefix}_COOP_CLEANUP", 1)[1]
            self.assertIn(f"{result}@ = 1", failure)
            self.assertIn(f"{result}@ = 2", passed)
            self.assertIn("Coop.PrintBigForNetworkPlayer('M_FAIL'", failure)
            self.assertIn("Coop.PrintBigForNetworkPlayer('M_PASSD'", passed)
            self.assertIn(f"{cleanup}@ == 1", cleanup_body)
            self.assertIn(f"{cleanup}@ = 1", cleanup_body)
            self.assertIn("Coop.ClearAllEntityBlipsForNetworkPlayer", cleanup_body)
            self.assertIn("Coop.RemoveCheckpointForNetworkPlayer", cleanup_body)
            self.assertIn(
                f"Char.FreezePosition({base}@({base + 16}@,3i), False)",
                cleanup_body,
            )
            self.assertIn("deterministic restore policy", cleanup_body)
            self.assertIn("Player.SetControl($player1, True)", cleanup_body)


if __name__ == "__main__":
    unittest.main()
