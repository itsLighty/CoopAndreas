import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
IMPEXPM = (ROOT / "scm/scripts/IMPEXPM.txt").read_text(encoding="utf-8")
IMPEXPC = (ROOT / "scm/scripts/IMPEXPC.txt").read_text(encoding="utf-8")
CRANES = (ROOT / "scm/scripts/CRANES.txt").read_text(encoding="utf-8")
CRANE2 = (ROOT / "scm/scripts/CRANE2.txt").read_text(encoding="utf-8")
IS_HOST = (
    ROOT / "client/src/Commands/Commands/CCommandIsHost.cpp"
).read_text(encoding="utf-8")


class ImportExportCoopStructureTests(unittest.TestCase):
    def test_exact_stock_activity_to_script_mapping(self):
        self.assertIn("script_name 'IMPEXPM'", IMPEXPM)
        self.assertIn("script_name 'IMPEXPC'", IMPEXPC)
        self.assertIn("script_name 'CRANES'", CRANES)
        self.assertIn("script_name 'CRANE2'", CRANE2)

        # IMPEXPM owns the ship delivery/purchase bodies and their exact bays.
        self.assertIn("World.GetRandomCarInSphereNoSave(-1577.942, 52.6333", IMPEXPM)
        self.assertIn("IMPEXPM_9397()", IMPEXPM)
        self.assertIn("$imported_car = Car.Create(", IMPEXPM)
        self.assertIn("-1572.168, 63.2853, 16.3281", IMPEXPM)

        # IMPEXPC is only the wanted-car hint companion.
        self.assertIn("$car = Char.StoreCarIsInNoSave($scplayer)", IMPEXPC)
        self.assertIn("Text.PrintHelp('IE22')", IMPEXPC)
        self.assertNotIn("Player.AddScore", IMPEXPC)
        self.assertNotIn("Stat.", IMPEXPC)

        # CRANES starts stock streamed body 68 at the magnetic crane; CRANE2
        # contains controls/rope/camera only, not an invented model/reward list.
        self.assertIn("StreamedScript.StartNew(68, $magno_base)", CRANES)
        self.assertIn("Crane.PlayerEnteredDockCrane", CRANE2)
        self.assertIn("Object.GetRopeHeight($magno_arm)", CRANE2)
        self.assertNotIn("Player.AddScore", CRANE2)
        self.assertNotIn("Stat.PlayerMadeProgress", CRANE2)
        self.assertIn("$player_is_in_crane == 2", IMPEXPM)
        self.assertIn("$COOP_IMPEXP_KIND = 3", IMPEXPM)
        crane_session = IMPEXPM.split(":IMPEXPM_COOP_UPDATE", 1)[1]
        self.assertIn("Object.GrabEntityOnRope($magno_arm)", crane_session)

    def test_offline_authority_contract_and_gates_precede_side_effects(self):
        self.assertIn(
            "!CNetwork::m_bAuthenticated || CLocalPlayer::m_bIsHost", IS_HOST
        )

        manager_prologue = IMPEXPM.split(":IMPEXPM_484", 1)[0]
        self.assertLess(manager_prologue.index("Coop.IsHost()"), manager_prologue.index("Object.CreateNoOffset"))
        self.assertLess(manager_prologue.index("Coop.IsHost()"), manager_prologue.index("$current_wanted_list = 0"))
        self.assertLess(manager_prologue.index("Coop.IsHost()"), manager_prologue.index("start_new_script @IMPEXPC"))

        idle_gate = IMPEXPM.split(":IMPEXPM_484", 1)[1].split(":IMPEXPM_563", 1)[0]
        active_gate = IMPEXPM.split(":IMPEXPM_577", 1)[1].split(":IMPEXPM_627", 1)[0]
        self.assertIn("Coop.IsHost()", idle_gate)
        self.assertIn("Coop.IsHost()", active_gate)
        observer = IMPEXPM.split(":IMPEXPM_COOP_OBSERVER", 1)[1].split(
            ":IMPEXPM_COOP_AUTHORITY_LOST", 1
        )[0]
        for forbidden in (
            "$current_wanted_car_status",
            "$unlocked_import_cars",
            "$imported_cars",
            "Player.AddScore",
            "Stat.",
            "Car.Create",
            "Object.Create",
        ):
            self.assertNotIn(forbidden, observer)

        hint_loop = IMPEXPC.split(":IMPEXPC_11", 1)[1]
        self.assertLess(hint_loop.index("Coop.IsHost()"), hint_loop.index("Player.IsPlaying"))
        self.assertLess(hint_loop.index("Coop.IsHost()"), hint_loop.index("$car ="))

        launcher_loop = CRANES.split(":CRANES_40", 1)[1]
        self.assertLess(launcher_loop.index("Coop.IsHost()"), launcher_loop.index("Player.IsPlaying"))
        self.assertLess(launcher_loop.index("Coop.IsHost()"), launcher_loop.index("StreamedScript.Stream"))
        self.assertLess(launcher_loop.index("Coop.IsHost()"), launcher_loop.index("StreamedScript.StartNew"))

        crane_prologue = CRANE2.split(":CRANE2_102", 1)[0]
        self.assertIn("Coop.EnableSyncingThisScript()", crane_prologue)
        self.assertLess(crane_prologue.index("Coop.IsHost()"), crane_prologue.index("0@ = 0"))
        self.assertIn(":CRANE2_COOP_REJECT_NON_HOST", CRANE2)
        crane_loop = CRANE2.split(":CRANE2_150", 1)[1].split(":CRANE2_260", 1)[0]
        self.assertLess(crane_loop.index("Coop.IsHost()"), crane_loop.index("Object.DoesExist"))
        self.assertIn(":CRANE2_COOP_AUTHORITY_LOST", CRANE2)

    def test_stock_export_lists_statuses_prices_and_milestones_are_exact(self):
        lists = (
            (402, 405, 411, 483, 445, 470, 468, 409, 533, 534),
            (415, 489, 439, 514, 480, 535, 496, 580, 475, 521),
            (429, 506, 508, 579, 424, 536, 463, 500, 477, 587),
        )
        for wanted_list in lists:
            for index, model in enumerate(wanted_list):
                self.assertEqual(
                    IMPEXPM.count(
                        f"set_var_int_to_constant $current_wanted_car_list[{index}] = {model}"
                    ),
                    1,
                )
        self.assertIn("$current_wanted_car_status(4@,10i) = 1", IMPEXPM)
        self.assertIn("$current_wanted_list += 1", IMPEXPM)
        self.assertIn("$export_price_multiplier = 1.0", IMPEXPM)
        self.assertIn("12@ *= $export_damage_multiplier", IMPEXPM)
        self.assertEqual(IMPEXPM.count("Stat.IncrementInt(StatId.TotalProgress, 213)"), 1)
        for count, bonus in ((10, 50000), (20, 100000), (30, 200000)):
            self.assertIn(f"{count} @IMPEXPM_", IMPEXPM)
            self.assertEqual(IMPEXPM.count(f"29@ += {bonus}"), 1)
        self.assertEqual(IMPEXPM.count("Player.AddScore($player1, 29@)"), 1)
        self.assertEqual(IMPEXPM.count("$impexp_is_complete = 1"), 2)

    def test_stock_import_matrix_prices_slots_unlocks_and_save_layout_are_exact(self):
        for index, model in enumerate((589, 404, 559, 589, 404, 559)):
            self.assertIn(
                f"set_var_int_to_constant $unlocked_import_cars[{index}] = {model}",
                IMPEXPM,
            )
        self.assertIn("42 > 4@", IMPEXPM)
        self.assertIn("$unlocked_import_cars(4@,42i) = -1", IMPEXPM)
        for day in range(7):
            expected = (day, day + 7, day + 14, day + 21, day + 28, day + 35)
            for row, source in enumerate(expected):
                self.assertIn(
                    f"$current_import_car_list[{row}] = $unlocked_import_cars[{source}]",
                    IMPEXPM,
                )

        self.assertIn("$import_price_multiplier = 0.8", IMPEXPM)
        self.assertEqual(IMPEXPM.count("Player.AddScore($player1, 4@)"), 1)
        self.assertEqual(IMPEXPM.count("Stat.IncrementInt(StatId.TotalProgress, 214)"), 1)
        self.assertEqual(IMPEXPM.count("$imported_cars[0] = -1"), 1)
        self.assertIn("5 > 4@", IMPEXPM)
        self.assertIn("$imported_cars(4@,5i) = $imported_car", IMPEXPM)

        for slot, bonus_model in ((10, 444), (16, 555), (22, 568), (28, 451), (34, 539), (40, 541)):
            self.assertIn(f"{slot} @IMPEXPM_", IMPEXPM)
            self.assertEqual(
                IMPEXPM.count(
                    f"set_var_int_to_constant $unlocked_import_cars(5@,42i) = {bonus_model}"
                ),
                1,
            )
        self.assertEqual(IMPEXPM.count("Stat.PlayerMadeProgress(1)"), 3)
        self.assertEqual(IMPEXPM.count("Audio.PlayMissionPassedTune(2)"), 3)
        self.assertNotRegex(IMPEXPM, re.compile(r"\$coop_[A-Za-z0-9_]+"))

    def test_stock_plate_selector_model_routes_and_fallback_are_exact(self):
        selector = IMPEXPM.split(":IMPEXPM_13123", 1)[1].split(
            ":IMPEXPM_14460", 1
        )[0]
        branches = (
            (589, 13339, None, ("N13_LLF_",)),
            (587, 13365, None, ("_DS3MP__",)),
            (506, 13391, None, ("_CMACD1_",)),
            (555, 13417, (0, 3), ("__C0S___",)),
            (559, 13470, (0, 3), ("_X2_GAV_", "__G3PO__")),
            (405, 13560, (0, 3), ("D0N_D0N_", "_D0_NNY_", "TH3_D0N_")),
            (483, 13687, None, ("SJM1985",)),
            (533, 13712, (0, 5), ("433_ADF_", "DR_F_MBE", "ANN_F3RG")),
            (475, 13839, None, ("__FR4Z__",)),
            (415, 13865, None, ("_IMY_AK_",)),
            (480, 13891, None, ("_L0LLY__",)),
            (411, 13917, None, ("_J_L33S_",)),
            (489, 13943, None, ("S4_LIJON",)),
            (496, 13969, (0, 2), ("DI5CO5TU", "SM53_NUV")),
            (429, 14048, (0, 3), ("J3NYTAL5", "_J3_NCF_", "DD0_N4LD")),
            (424, 14175, None, ("LA5H_L3Y",)),
            (470, 14201, (0, 4), ("AL3X_RES", "R_F3RG1E", "H4_NNAHF", "CL41_RES")),
            (536, 14365, None, ("R055_MCL",)),
            (541, 14391, (0, 4), ("T00_FAST",)),
        )
        self.assertIn("default_jump 1 @IMPEXPM_14444", selector)
        self.assertIn(":IMPEXPM_14444\nIMPEXPM_14460()", selector)

        labels = [branch[1] for branch in branches] + [14444]
        for index, (model, label, random_range, expected_plates) in enumerate(branches):
            self.assertRegex(selector, rf"\b{model} @IMPEXPM_{label}\b")
            body = IMPEXPM.split(f":IMPEXPM_{label}", 1)[1].split(
                f":IMPEXPM_{labels[index + 1]}", 1
            )[0]
            actual_plates = tuple(
                re.findall(
                    r'Car\.CustomPlateForNextCar\([^\n]+, "([^"]+)"\)', body
                )
            )
            self.assertEqual(actual_plates, expected_plates, f"model {model}")
            if random_range is None:
                self.assertNotIn("Math.RandomIntInRange", body)
            else:
                self.assertIn(
                    f"Math.RandomIntInRange({random_range[0]}, {random_range[1]})",
                    body,
                )

        fallback = IMPEXPM.split(":IMPEXPM_14460", 1)[1].split(
            ":IMPEXPM_14909", 1
        )[0]
        fallback_routes = (
            (0, 14597, "R4N_G3RS"),
            (1, 14623, "GL4S_G0W"),
            (2, 14649, "_ARRAN__"),
            (3, 14675, "AM0_RUS0"),
            (4, 14701, "_AMAT0__"),
            (5, 14727, "_GA_ZZA_"),
            (6, 14753, "ZID_ANE_"),
            (7, 14779, "MC_C01ST"),
            (8, 14805, "BAW_BAG_"),
            (9, 14831, "BR0_D1E_"),
            (10, 14857, "MR_J0BBY"),
            (11, 14883, "BR0_DICK"),
        )
        self.assertIn("Math.RandomIntInRange(0, 100)", fallback)
        for value, label, plate in fallback_routes:
            self.assertRegex(fallback, rf"\b{value} @IMPEXPM_{label}\b")
            branch = fallback.split(f":IMPEXPM_{label}", 1)[1]
            self.assertIn(f'"{plate}"', branch.split("goto @IMPEXPM_14909", 1)[0])
        self.assertEqual(
            tuple(
                re.findall(
                    r'Car\.CustomPlateForNextCar\([^\n]+, "([^"]+)"\)', fallback
                )
            ),
            tuple(route[2] for route in fallback_routes),
        )

    def test_stock_crane_launchers_locations_and_lifetime_are_preserved(self):
        for script_id, argument in (
            (67, "$sf_crane1_base"),
            (67, "$lv_base"),
            (68, "$magno_base"),
            (69, "$quarry_base, $quarry_stand, $quarry_arm"),
        ):
            self.assertEqual(
                CRANES.count(f"StreamedScript.StartNew({script_id}, {argument})"),
                1,
            )
        for coordinate in (
            "-2080.441, 256.015",
            "2399.202, 1879.139",
            "709.45, 915.93",
        ):
            self.assertIn(coordinate, CRANES)
        self.assertIn("Char.LocateAnyMeansObject2D($scplayer, $magno_base, 50.0, 50.0", CRANES)
        self.assertEqual(CRANE2.count("Crane.PlayerEnteredDockCrane"), 1)
        normal_exit = CRANE2.split(":CRANE2_4430", 1)[1].split(
            ":CRANE2_4652", 1
        )[0]
        for evidence in (
            "0@ == 4",
            "$remove_player_from_crane == 1",
            "Crane.PlayerLeftCrane",
            "Game.SetMinigameInProgress(False)",
            "Object.GetOffsetInWorldCoords($y, $z, $magno_base, 2.0, -4.0, 0.0)",
            "Char.FreezePosition($scplayer, False)",
            "Player.SetControl($player1, True)",
            "$player_is_in_crane = 0",
            "$remove_player_from_crane = 0",
        ):
            self.assertIn(evidence, normal_exit)

        authority_exit = CRANE2.split(":CRANE2_COOP_AUTHORITY_LOST", 1)[1]
        self.assertIn("$player_is_in_crane == 2", authority_exit)
        for evidence in (
            "Crane.PlayerLeftCrane",
            "Game.SetMinigameInProgress(False)",
            "Char.DetachFromCar($scplayer)",
            "Char.FreezePosition($scplayer, False)",
            "Char.SetVisible($scplayer, True)",
            "Char.SetCollision($scplayer, True)",
            "Char.SetProofs($scplayer, False",
            "Player.SetControl($player1, True)",
            "Camera.RestoreJumpcut",
            "$player_is_in_crane = 0",
            "$remove_player_from_crane = 0",
            "terminate_this_script",
        ):
            self.assertIn(evidence, authority_exit)
        self.assertEqual(CRANE2.count("Crane.PlayerLeftCrane"), 2)

    def test_frozen_identity_reconnect_disconnect_and_death_protocols(self):
        self.assertIn(
            "$COOP_IMPEXP_PLAYER[0], $COOP_IMPEXP_PLAYER[1], "
            "$COOP_IMPEXP_PLAYER[2] = Coop.CollectNetworkPlayersForTheMission()",
            IMPEXPM,
        )
        self.assertIn(
            "$COOP_IMPEXP_PLAYER_ID(32@,3i) = "
            "Coop.GetNetworkPlayerInternalId($COOP_IMPEXP_PLAYER(32@,3i))",
            IMPEXPM,
        )
        self.assertIn("nonblocking DNF", IMPEXPM)
        self.assertIn("COOP_PARTICIPANT_DEATH", IMPEXPM)
        self.assertIn("$COOP_IMPEXP_DEATH = 1", IMPEXPM)
        self.assertIn("Coop.TeleportPlayersToHostSafely", IMPEXPM)
        self.assertIn("$COOP_IMPEXP_KIND == 3", IMPEXPM)

        manager_refresh = IMPEXPM.split(":IMPEXPM_COOP_REFRESH_ROSTER", 1)[1].split(
            ":IMPEXPM_COOP_VALIDATE_SLOT", 1
        )[0]
        self.assertIn(
            "$COOP_IMPEXP_CURRENT_ID == $COOP_IMPEXP_PLAYER_ID(32@,3i)",
            manager_refresh,
        )
        self.assertIn("$COOP_IMPEXP_PLAYER(32@,3i) = 0", manager_refresh)
        self.assertNotIn(
            "$COOP_IMPEXP_PLAYER_ID(32@,3i) = "
            "Coop.GetNetworkPlayerInternalId",
            manager_refresh,
        )

    def test_namespaced_state_is_initialized_reset_and_abi_bounded(self):
        stock_manager_locals = {
            0, 1, 4, 5, 6, 7, 8, 9, 12, 13, 14, 15, 16, 17, 18,
            19, 20, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
        }
        manager_locals = {int(value) for value in re.findall(r"\b(\d+)@", IMPEXPM)}
        self.assertLessEqual(max(manager_locals), 33)
        self.assertEqual(manager_locals - stock_manager_locals, {32, 33})
        self.assertLessEqual(
            max(int(value) for value in re.findall(r"\b(\d+)@", CRANE2)), 29
        )
        self.assertLessEqual(
            max(int(value) for value in re.findall(r"\b(\d+)@", IMPEXPC)), 3
        )
        self.assertLessEqual(
            max(int(value) for value in re.findall(r"\b(\d+)@", CRANES)), 0
        )

        namespace = set(re.findall(r"\$(COOP_IMPEXP_[A-Z0-9_]+)", IMPEXPM))
        expected = {
            "COOP_IMPEXP_PLAYER", "COOP_IMPEXP_PLAYER_ID",
            "COOP_IMPEXP_OBJECTIVE", "COOP_IMPEXP_REFRESH",
            "COOP_IMPEXP_SESSION_ACTIVE", "COOP_IMPEXP_KIND",
            "COOP_IMPEXP_DEATH", "COOP_IMPEXP_RESULT",
            "COOP_IMPEXP_CLEANUP", "COOP_IMPEXP_PLAYER_COUNT",
            "COOP_IMPEXP_VALID", "COOP_IMPEXP_CURRENT_ID",
            "COOP_IMPEXP_OLD_HANDLE", "COOP_IMPEXP_TARGET_HANDLE",
            "COOP_IMPEXP_VEHICLE_HANDLE", "COOP_IMPEXP_VEHICLE_NETWORK_ID",
            "COOP_IMPEXP_REGISTRATION_START",
            "COOP_IMPEXP_REGISTRATION_TIMEOUT", "COOP_IMPEXP_REGROUP_START",
            "COOP_IMPEXP_OBJECTIVE_TOKEN", "COOP_IMPEXP_REGROUP_NEEDED",
            "COOP_IMPEXP_TEMP_TIMER", "COOP_IMPEXP_COMMITTED",
            "COOP_IMPEXP_IMPORT_REFUND", "COOP_IMPEXP_GRAB_AUX_1",
            "COOP_IMPEXP_GRAB_AUX_2",
        }
        self.assertEqual(namespace, expected)
        reset = IMPEXPM.split(":IMPEXPM_COOP_RESET_ALL", 1)[1].split(
            ":IMPEXPM_COOP_INIT", 1
        )[0]
        cleanup = IMPEXPM.split(":IMPEXPM_COOP_CLEANUP", 1)[1]
        for name in expected:
            self.assertIn(f"${name}", reset)
            self.assertIn(f"${name}", cleanup)
        self.assertIn("gosub @IMPEXPM_COOP_RESET_ALL", IMPEXPM.split(":IMPEXPM_484", 1)[0])
        self.assertIn("gosub @IMPEXPM_COOP_RESET_ALL", IMPEXPM.split(":IMPEXPM_COOP_AUTHORITY_LOST", 1)[1].split(":IMPEXPM_COOP_RESET_ALL", 1)[0])
        self.assertIn("$COOP_IMPEXP_CLEANUP == 1", cleanup)

    def test_registration_objectives_results_and_cleanup_are_bounded(self):
        manager_registration = IMPEXPM.split(
            ":IMPEXPM_COOP_POLL_VEHICLE_REGISTRATION", 1
        )[1].split(":IMPEXPM_COOP_UPDATE_REGROUP", 1)[0]
        self.assertIn(
            "Coop.GetVehicleNetworkId($COOP_IMPEXP_VEHICLE_HANDLE)",
            manager_registration,
        )
        self.assertIn("> 5000", manager_registration)
        self.assertNotIn("wait ", manager_registration.lower())

        objectives = IMPEXPM.split(":IMPEXPM_COOP_UPDATE_OBJECTIVES", 1)[1].split(
            ":IMPEXPM_COOP_NOTIFY_FAILURE", 1
        )[0]
        for evidence in (
            "Coop.ClearAllEntityBlipsForNetworkPlayer",
            "Coop.RemoveCheckpointForNetworkPlayer",
            "Coop.UpdateCarBlipForNetworkPlayer",
            "Coop.UpdateCheckpointForNetworkPlayer",
            "Coop.PrintNowForNetworkPlayer",
        ):
            self.assertIn(evidence, objectives)
        self.assertIn("-1577.942, 52.6333, 16.3281", objectives)
        cleanup = IMPEXPM.split(":IMPEXPM_COOP_CLEANUP", 1)[1]
        self.assertIn("$COOP_IMPEXP_CLEANUP == 1", cleanup)
        self.assertIn("Coop.ClearAllEntityBlipsForNetworkPlayer", cleanup)
        self.assertIn("Coop.RemoveCheckpointForNetworkPlayer", cleanup)
        self.assertIn("Char.FreezePosition", cleanup)
        self.assertIn("deterministic restore policy", cleanup)

        self.assertIn("$COOP_IMPEXP_IMPORT_REFUND > 0", IMPEXPM)
        self.assertIn(
            "Player.AddScore($player1, $COOP_IMPEXP_IMPORT_REFUND)", IMPEXPM
        )
        self.assertIn("$COOP_IMPEXP_COMMITTED = 1", IMPEXPM)
        self.assertIn("$remove_player_from_crane = 1", IMPEXPM)


if __name__ == "__main__":
    unittest.main()
