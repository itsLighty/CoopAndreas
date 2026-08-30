import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CPRACE = (ROOT / "scm" / "scripts" / "CPRACE.txt").read_text(encoding="utf-8")

STREET_SELECTORS = {*range(1, 7), *range(9, 25)}
STORY_SELECTORS = {0, 7, 8}
STADIUM_SELECTORS = {25, 26}

# These six stock selector branches are the aircraft/heli activities in README
# order: World War Ace, Barnstorming, Military Service, Chopper Checkpoint,
# Whirly Bird Waypoint and Heli Hell.
AIRCRAFT_ROUTES = {
    19: ("CPRACE_36982", "CPRACE_38120", 476),
    20: ("CPRACE_38120", "CPRACE_40068", 513),
    21: ("CPRACE_40068", "CPRACE_42256", 520),
    22: ("CPRACE_42256", "CPRACE_43154", 487),
    23: ("CPRACE_43154", "CPRACE_44052", 488),
    24: ("CPRACE_44052", "CPRACE_44980", 425),
}


def label_block(label: str, next_label: str) -> str:
    start = CPRACE.index(f":{label}")
    end = CPRACE.index(f":{next_label}", start)
    return CPRACE[start:end]


class StreetRacingSyncTests(unittest.TestCase):
    def test_exact_22_street_selectors_enter_host_authority_gate(self) -> None:
        self.assertEqual(
            STREET_SELECTORS,
            {1, 2, 3, 4, 5, 6, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24},
        )
        prefix = CPRACE[: CPRACE.index(":CPRACE_STOCK_START")]
        self.assertIn("$race_selection >= 1\n  6 >= $race_selection", prefix)
        self.assertIn("$race_selection >= 9\n  24 >= $race_selection", prefix)
        self.assertIn(":CPRACE_COOP_AUTHORITY_START", prefix)
        self.assertLess(prefix.index("Coop.IsHost()"), CPRACE.index("$onmission = 1"))
        self.assertRegex(
            prefix,
            r":CPRACE_COOP_REJECT_NON_HOST\s+terminate_this_script",
        )

        # Story selectors never appear as equality gates in the co-op prefix;
        # stadium 25/26 retain their pre-existing explicit dispatch.
        for selector in STORY_SELECTORS:
            self.assertNotIn(f"$race_selection == {selector}\n", prefix)
        for selector in STADIUM_SELECTORS:
            self.assertIn(f"$race_selection == {selector}\n", prefix)

    def test_all_street_route_families_use_shared_host_hooks(self) -> None:
        init = label_block("CPRACE_COOP_INIT", "CPRACE_COOP_REFRESH_ROSTER")
        ranges = {
            (int(first), int(last))
            for first, last in re.findall(
                r"\$race_selection >= (\d+)\s+(\d+) >= \$race_selection"
                r"\s+then\s+850@ = 1",
                init,
            )
        }
        actual_selectors = {
            selector
            for first, last in ranges
            for selector in range(first, last + 1)
        }
        self.assertEqual(ranges, {(1, 6), (9, 24)})
        self.assertEqual(actual_selectors, STREET_SELECTORS)
        self.assertTrue(actual_selectors.isdisjoint(STORY_SELECTORS | STADIUM_SELECTORS))

        for hook in (
            "STAGE_PARTICIPANTS",
            "UPDATE",
            "NOTIFY_RESULT",
            "CLEANUP",
        ):
            with self.subTest(hook=hook):
                self.assertEqual(CPRACE.count(f"gosub @CPRACE_COOP_STREET_{hook}"), 1)
                self.assertIn(
                    f":CPRACE_COOP_STREET_{hook}\ngoto @CPRACE_COOP_{hook}",
                    CPRACE,
                )

        helper = CPRACE[CPRACE.index(":CPRACE_COOP_INIT") :]
        self.assertIn("Car.Create(716@(442@,16i)", helper)
        self.assertIn("Char.WarpIntoCar(800@(816@,3i), 809@(816@,3i))", helper)
        self.assertIn("independent support checkpoint index", helper)
        self.assertIn("Char.LocateAnyMeans3D(800@(816@,3i), 0, 454@(849@,82f)", helper)
        self.assertIn("Coop.UpdateCheckpointForNetworkPlayer(454@(849@,82f)", helper)
        self.assertIn("unranked support finish latch", helper)

        body = CPRACE[: CPRACE.index(":CPRACE_COOP_STREET_STAGE_PARTICIPANTS")]
        for match in re.finditer(
            r"gosub @CPRACE_COOP_STREET_(?:STAGE_PARTICIPANTS|UPDATE|NOTIFY_RESULT|CLEANUP)",
            body,
        ):
            with self.subTest(call=match.group(0)):
                self.assertIn("850@ == 1", body[max(0, match.start() - 80) : match.start()])

        # The already-completed stadium paths retain their explicit 25/26
        # guards and established hooks; they are not relabelled as street races.
        for match in re.finditer(
            r"gosub @CPRACE_COOP_(?:STAGE_PARTICIPANTS|UPDATE|NOTIFY_RESULT|CLEANUP)",
            body,
        ):
            guard = body[max(0, match.start() - 120) : match.start()]
            self.assertIn("$race_selection == 25", guard)
            self.assertIn("$race_selection == 26", guard)

    def test_aircraft_and_heli_routes_keep_stock_selected_models_and_checkpoints(self) -> None:
        self.assertEqual(set(AIRCRAFT_ROUTES), set(range(19, 25)))
        for selector, (label, next_label, model) in AIRCRAFT_ROUTES.items():
            route = label_block(label, next_label)
            with self.subTest(selector=selector):
                self.assertRegex(route, r"(?m)^250@ = [1-9][0-9]*$")
                self.assertIn(f"set_lvar_int_to_constant 716@ = {model}", route)
                self.assertIn("set_lvar_int_to_constant 205@ = 3", route)
                self.assertIn("goto @CPRACE_46262", route)

        helper = CPRACE[CPRACE.index(":CPRACE_COOP_STAGE_PARTICIPANTS") :]
        # Support vehicles and checkpoints follow the stock-selected model and
        # route arrays. No street-only car model or substitute route is invented.
        self.assertIn("Car.Create(716@(442@,16i)", helper)
        self.assertIn("454@(849@,82f)", helper)
        for _selector, (_label, _next_label, model) in AIRCRAFT_ROUTES.items():
            self.assertNotIn(f"Car.Create({model},", helper)

    def test_roster_is_frozen_and_disconnect_reconnect_does_not_expand_it(self) -> None:
        init = label_block("CPRACE_COOP_INIT", "CPRACE_COOP_REFRESH_ROSTER")
        refresh = label_block("CPRACE_COOP_REFRESH_ROSTER", "CPRACE_COOP_VALIDATE_SLOT")
        validate = label_block("CPRACE_COOP_VALIDATE_SLOT", "CPRACE_COOP_STAGE_PARTICIPANTS")

        self.assertIn("800@, 801@, 802@ = Coop.CollectNetworkPlayersForTheMission()", init)
        self.assertIn("803@(816@,3i) = Coop.GetNetworkPlayerInternalId", init)
        self.assertIn("806@, 807@, 808@ = Coop.CollectNetworkPlayersForTheMission()", refresh)
        self.assertIn("800@(816@,3i) = 0", refresh)
        self.assertIn("818@ == 803@(816@,3i)", refresh)
        self.assertNotIn("803@(816@,3i) = 818@", refresh)
        self.assertIn("818@ == 803@(816@,3i)", validate)
        self.assertIn("Coop.TeleportPlayersToHostSafely(800@, 801@, 802@)", CPRACE)

    def test_connected_peer_death_is_a_rewardless_stock_teardown(self) -> None:
        update = label_block("CPRACE_COOP_UPDATE", "CPRACE_COOP_NOTIFY_RESULT")
        result = label_block("CPRACE_19581", "CPRACE_20088")
        reward_dispatch = label_block("CPRACE_48585", "CPRACE_48872")

        self.assertIn("gosub @CPRACE_COOP_REFRESH_ROSTER", update)
        self.assertLess(update.index("gosub @CPRACE_COOP_VALIDATE_SLOT"), update.index("Char.IsDead"))
        self.assertIn("COOP_PARTICIPANT_DEATH", CPRACE)
        self.assertIn("851@ = 1", update)
        self.assertIn("850@ == 1", result)
        self.assertIn("851@ == 1", result)
        self.assertIn("goto @CPRACE_19931", result)
        self.assertIn("goto @CPRACE_50543", reward_dispatch)
        self.assertLess(reward_dispatch.index("goto @CPRACE_50543"), reward_dispatch.index("Audio.PlayMissionPassedTune"))

        notify = label_block("CPRACE_COOP_NOTIFY_RESULT", "CPRACE_COOP_CLEANUP")
        self.assertLess(notify.index("851@ == 0"), notify.index("256@ == 1"))

    def test_results_rewards_and_cleanup_have_one_authoritative_owner(self) -> None:
        helper = CPRACE[CPRACE.index(":CPRACE_COOP_INIT") :]
        for forbidden in (
            "Player.AddScore",
            "Stat.PlayerMadeProgress",
            "$total_races_completed += 1",
            "$got_race_made_progress",
            "Mission.Finish",
        ):
            self.assertNotIn(forbidden, helper)

        cleanup = CPRACE[CPRACE.index(":CPRACE_COOP_CLEANUP") :]
        self.assertIn("idempotent cleanup latch", CPRACE)
        self.assertIn("Coop.RemoveCheckpointForNetworkPlayer", cleanup)
        self.assertIn("Coop.ClearAllEntityBlipsForNetworkPlayer", cleanup)
        self.assertIn("Car.Delete(809@(816@,3i))", cleanup)
        self.assertIn("Player.SetControl($player1, True)", cleanup)

        refs = set(
            re.findall(
                r"(?:gosub|goto|goto_if_false)\s+@([A-Z0-9_]*COOP[A-Z0-9_]*)",
                CPRACE,
            )
        )
        for label in refs:
            with self.subTest(label=label):
                self.assertEqual(
                    len(re.findall(rf"(?m)^:{re.escape(label)}\s*$", CPRACE)),
                    1,
                )


if __name__ == "__main__":
    unittest.main()
