import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "scm" / "scripts"


def read_script(name: str) -> str:
    return (SCRIPTS / name).read_text(encoding="utf-8")


def label_block(text: str, label: str, next_label: str) -> str:
    start = text.index(f":{label}")
    end = text.index(f":{next_label}", start)
    return text[start:end]


class StadiumActivitySyncTests(unittest.TestCase):
    def test_launchers_gate_before_side_effects_and_keep_stock_dispatch(self) -> None:
        blood = read_script("BLOODR.txt")
        hotring = read_script("HOTR.txt")
        kicks = read_script("KICKS.txt")

        for name, text, first_effect in (
            ("BLOODR", blood, "LITCAS_267()"),
            ("HOTR", hotring, "Text.PrintBig('STAD_03'"),
            ("KICKS", kicks, "$weekday = Clock.GetCurrentDayOfWeek()"),
        ):
            with self.subTest(name=name):
                self.assertLess(text.index("Coop.IsHost()"), text.index(first_effect))
                self.assertNotIn("Coop.IsNetworkAuthenticated()", text)

        self.assertIn("Coop.LaunchMissionForCoop(128)", blood)
        self.assertIn("$stat = Stat.GetInt(160)", hotring)
        self.assertIn("$stat > 199", hotring)
        self.assertLess(hotring.index("$race_selection = 25"), hotring.index("Coop.LaunchMissionForCoop(35)"))

        # Dirt Track is the Monday/Wednesday/Friday selector-26 variant.
        for day in (0, 2, 4):
            self.assertIn(f"$weekday == {day}", kicks)
        self.assertIn("$stat = Stat.GetInt(229)", kicks)
        self.assertIn("$stat > 199", kicks)
        self.assertLess(kicks.index("$race_selection = 26"), kicks.index("Coop.LaunchMissionForCoop(35)"))
        # The opposite-day branch remains the distinct Kick Start mission.
        self.assertIn(":KICKS_243", kicks)
        self.assertIn("Text.PrintBig('STAD_02'", kicks)
        self.assertIn("Coop.LaunchMissionForCoop(129)", kicks)

    def test_central_host_condition_preserves_unauthenticated_stock_authority(self) -> None:
        source = (ROOT / "client" / "src" / "Commands" / "Commands" / "CCommandIsHost.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            "!CNetwork::m_bAuthenticated || CLocalPlayer::m_bIsHost",
            source,
        )
        for script in ("BLOODR.txt", "BLOOD.txt", "KICKS.txt", "KICKSTA.txt", "HOTR.txt", "CPRACE.txt"):
            with self.subTest(script=script):
                self.assertNotIn("Coop.IsNetworkAuthenticated()", read_script(script))

    def test_internal_activity_body_gates_precede_stock_mutations(self) -> None:
        for script, mutation, reject in (
            ("BLOOD.txt", "$onmission = 1", ":BLOOD_COOP_REJECT_NON_HOST"),
            ("KICKSTA.txt", "$onmission = 1", ":KICKSTA_COOP_REJECT_NON_HOST"),
        ):
            text = read_script(script)
            with self.subTest(script=script):
                self.assertLess(text.index("Coop.EnableSyncingThisScript()"), text.index("Coop.IsHost()"))
                self.assertLess(text.index("Coop.IsHost()"), text.index(mutation))
                self.assertLess(text.index(reject), text.index(mutation))
                self.assertIn("terminate_this_script", text[text.index(reject) : text.index(mutation)])

        race = read_script("CPRACE.txt")
        prefix = race[: race.index(":CPRACE_STOCK_START")]
        self.assertIn("if or\n  $race_selection == 25\n  $race_selection == 26", prefix)
        self.assertIn("Coop.IsHost()", prefix)
        self.assertIn(":CPRACE_COOP_REJECT_NON_HOST", prefix)
        self.assertLess(race.index("Coop.IsHost()"), race.index("$onmission = 1"))

    def test_frozen_rosters_support_reconnect_without_expanding_membership(self) -> None:
        for script, base in (("BLOOD.txt", "800@"), ("KICKSTA.txt", "500@"), ("CPRACE.txt", "800@")):
            text = read_script(script)
            with self.subTest(script=script):
                self.assertGreaterEqual(text.count("Coop.CollectNetworkPlayersForTheMission()"), 2)
                self.assertIn("Coop.GetNetworkPlayerInternalId", text)
                self.assertIn("COOP_REFRESH_ROSTER", text)
                self.assertIn("COOP_VALIDATE_SLOT", text)
                self.assertIn(f"Coop.TeleportPlayersToHostSafely({base}", text)
                self.assertIn("Coop.IsNetworkPlayerActorValid", text)

    def test_blood_bowl_support_and_stock_rewards_are_host_owned(self) -> None:
        text = read_script("BLOOD.txt")
        self.assertIn("Car.Create(504", text[text.index(":BLOOD_COOP_STAGE_PARTICIPANTS") :])
        self.assertIn(":BLOOD_COOP_FIND_PICKUP_CAR", text)
        self.assertIn(":BLOOD_COOP_FIND_CHECKPOINT_CAR", text)
        self.assertIn(":BLOOD_COOP_CHECK_PARTICIPANT_DAMAGE", text)
        self.assertIn("Coop.UpdateCheckpointForNetworkPlayer(158@, 159@, 160@", text)
        self.assertIn("gosub @BLOOD_COOP_NOTIFY_FAILURE\nText.PrintBig('M_FAIL'", text)
        self.assertIn("gosub @BLOOD_COOP_NOTIFY_PASS\nRestart.CancelOverride", text)

        # No co-op helper duplicates canonical money/progress/save mutations.
        self.assertEqual(text.count("Player.AddScore($player1, 10000)"), 1)
        self.assertEqual(text.count("Stat.PlayerMadeProgress(1)"), 1)
        self.assertEqual(text.count("CarGenerator.Switch($f1_blood_car_gen, 101)"), 1)
        self.assertEqual(text.count("$blood_passed_once = 1"), 1)
        self.assertEqual(text.count("Mission.Finish"), 1)

    def test_kick_start_supports_shared_checkpoints_and_preserves_stock_state(self) -> None:
        text = read_script("KICKSTA.txt")
        helper = text[text.index(":KICKSTA_COOP_INIT") :]
        self.assertIn("Car.Create(468", helper)
        self.assertIn(":KICKSTA_COOP_CHECK_CHECKPOINT_CONTRIBUTORS", helper)
        self.assertIn("Char.IsInModel(500@(516@,3i), 468)", helper)
        self.assertIn("Coop.UpdateCheckpointForNetworkPlayer(50@(522@,33f)", helper)
        self.assertIn("533@ = 1", text[text.index(":KICKSTA_4886") : text.index(":KICKSTA_4964")])
        self.assertIn("533@ == 0", helper)
        self.assertIn("gosub @KICKSTA_COOP_NOTIFY_FAILURE\ngosub @KICKSTA_7448", text)
        self.assertIn(":KICKSTA_7510\ngosub @KICKSTA_COOP_NOTIFY_PASS", text)

        self.assertEqual(text.count("$player_checkpoint_kickstart += 1"), 1)
        self.assertEqual(text.count("$number_of_checkpoints_kickstart -= 1"), 1)
        self.assertEqual(text.count("Stat.PlayerMadeProgress(1)"), 1)
        self.assertEqual(text.count("$flag_kickstart_passed_1stime = 1"), 1)
        self.assertEqual(text.count("CarGenerator.Switch($car_gen_duneride_kickstart, 101)"), 2)
        self.assertEqual(text.count("Player.AddScore($player1, 258@)"), 2)
        self.assertEqual(text.count("Mission.Finish"), 1)

    def test_8track_and_dirt_track_remain_distinct_cprace_variants(self) -> None:
        text = read_script("CPRACE.txt")
        setup_25 = label_block(text, "CPRACE_44980", "CPRACE_45529")
        setup_26 = label_block(text, "CPRACE_45529", "CPRACE_46262")
        reward_25 = label_block(text, "CPRACE_50384", "CPRACE_50460")
        reward_26 = label_block(text, "CPRACE_50467", "CPRACE_50536")

        self.assertIn("224@ = 12", setup_25)
        self.assertIn("256@ = 12", setup_25)
        for model in (503, 502, 494):
            self.assertIn(f"= {model}", setup_25)
        self.assertIn("250@ = 8", setup_25)
        self.assertIn("224@ = 6", setup_26)
        self.assertIn("256@ = 12", setup_26)
        self.assertIn("= 468", setup_26)

        for expected in (
            "start_new_script @CASHWIN 10000 5000",
            "Player.AddScore($player1, 10000)",
            "Stat.PlayerMadeProgress(1)",
            "Stat.RegisterMissionPassed('STAD_03')",
            "CarGenerator.Switch($nascar_reward_cargen1, 101)",
            "CarGenerator.Switch($nascar_reward_cargen2, 101)",
            "$got_race_made_progress[25] = 1",
        ):
            self.assertIn(expected, reward_25)
        for expected in (
            "start_new_script @CASHWIN 25000 5000",
            "Player.AddScore($player1, 25000)",
            "Stat.PlayerMadeProgress(1)",
            "Stat.RegisterMissionPassed('STAD_01')",
            "CarGenerator.Switch($dirtrack_reward_cargen, 101)",
            "$got_race_made_progress[26] = 1",
        ):
            self.assertIn(expected, reward_26)

        helper = text[text.index(":CPRACE_COOP_INIT") :]
        self.assertIn("$race_selection == 25", helper)
        self.assertIn("Car.SetHealth(809@(816@,3i), 2200)", helper)
        self.assertIn("$race_selection == 26", helper)
        self.assertIn("Car.SetHealth(809@(816@,3i), 10000)", helper)
        self.assertIn("independent support checkpoint index", helper)
        self.assertIn("Char.LocateAnyMeans3D(800@(816@,3i), 0, 454@(849@,82f)", helper)
        self.assertIn("Coop.UpdateCheckpointForNetworkPlayer(454@(849@,82f)", helper)
        self.assertIn("unranked support finish latch", helper)
        self.assertNotIn("Player.AddScore", helper)
        self.assertNotIn("Stat.PlayerMadeProgress", helper)

    def test_stadium_cprace_hooks_do_not_capture_street_selectors(self) -> None:
        text = read_script("CPRACE.txt")
        helper_start = text.index(":CPRACE_COOP_INIT")
        body = text[:helper_start]
        for match in re.finditer(r"gosub @CPRACE_COOP_(?:UPDATE|STAGE_PARTICIPANTS|NOTIFY_RESULT|CLEANUP)", body):
            guard = body[max(0, match.start() - 120) : match.start()]
            self.assertIn("$race_selection == 25", guard)
            self.assertIn("$race_selection == 26", guard)
        prefix = body[: body.index(":CPRACE_STOCK_START")]
        for selector in range(25):
            self.assertNotIn(f"$race_selection == {selector}\n", prefix)

    def test_all_coop_labels_are_defined_exactly_once_and_cleanup_is_idempotent(self) -> None:
        for script in ("BLOOD.txt", "KICKSTA.txt", "CPRACE.txt"):
            text = read_script(script)
            refs = set(re.findall(r"(?:gosub|goto|goto_if_false)\s+@([A-Z0-9_]*COOP[A-Z0-9_]*)", text))
            with self.subTest(script=script):
                self.assertTrue(refs)
                for label in refs:
                    self.assertEqual(len(re.findall(rf"(?m)^:{re.escape(label)}\s*$", text)), 1, label)
                cleanup_label = next(label for label in refs if label.endswith("COOP_CLEANUP"))
                cleanup = text[text.index(f":{cleanup_label}") :]
                self.assertIn("idempotent cleanup latch", text)
                self.assertIn("Coop.RemoveCheckpointForNetworkPlayer", cleanup)
                self.assertIn("Coop.ClearAllEntityBlipsForNetworkPlayer", cleanup)
                self.assertIn("Player.SetControl($player1, True)", cleanup)


if __name__ == "__main__":
    unittest.main()
