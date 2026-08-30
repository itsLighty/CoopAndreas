import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
ODDVEH = (ROOT / "scm/scripts/ODDVEH.txt").read_text(encoding="utf-8")
STUNT = (ROOT / "scm/scripts/STUNT.txt").read_text(encoding="utf-8")
MTBIKER = (ROOT / "scm/scripts/MTBIKER.txt").read_text(encoding="utf-8")
BCOUR = (ROOT / "scm/scripts/BCOUR.txt").read_text(encoding="utf-8")


class HiddenRacesCourierCoopStructureTests(unittest.TestCase):
    def test_exact_stock_activity_mapping_and_natural_launch_preflight(self):
        self.assertEqual(
            ODDVEH.count("Coop.LaunchMissionForCoop(131) // Courier"), 3
        )
        self.assertEqual(
            ODDVEH.count(
                "Coop.LaunchMissionForCoop(132) // The Chiliad Challenge"
            ),
            1,
        )
        self.assertEqual(
            ODDVEH.count(
                "Coop.LaunchMissionForCoop(133) // BMX / NRG-500 STUNT Mission"
            ),
            2,
        )
        self.assertIn("$stunt_course = 0", ODDVEH)
        self.assertIn("$stunt_course = 1", ODDVEH)
        self.assertIn("// Originally: Courier", BCOUR)
        self.assertIn("// Originally: The Chiliad Challenge", BCOUR)
        self.assertIn("// Originally: BMX / NRG-500 STUNT Mission", MTBIKER)

    def test_host_authority_and_frozen_identity_rosters_cover_all_six(self):
        for text, prefix, base in (
            (STUNT, "STUNT", 200),
            (MTBIKER, "MTBIKER", 800),
            (BCOUR, "BCOUR", 900),
        ):
            prologue = text.split(f":{prefix}_COOP_HOST_START", 1)[0]
            self.assertIn("Coop.EnableSyncingThisScript()", prologue)
            self.assertIn("Coop.IsHost()", prologue)
            self.assertIn(f":{prefix}_COOP_REJECT_NON_HOST", prologue)
            self.assertIn("terminate_this_script", prologue)
            self.assertIn(
                f"{base}@, {base + 1}@, {base + 2}@ = "
                "Coop.CollectNetworkPlayersForTheMission()",
                text,
            )
            self.assertIn(
                f"{base + 3}@({base + 16}@,3i) = "
                f"Coop.GetNetworkPlayerInternalId({base}@({base + 16}@,3i))",
                text,
            )

    def test_reconnect_is_identity_matched_and_disconnect_is_dnf(self):
        for text, prefix, base in (
            (STUNT, "STUNT", 200),
            (MTBIKER, "MTBIKER", 800),
            (BCOUR, "BCOUR", 900),
        ):
            refresh = text.split(f":{prefix}_COOP_REFRESH_ROSTER", 1)[1].split(
                f":{prefix}_COOP_VALIDATE_SLOT", 1
            )[0]
            self.assertIn(
                f"{base + 9}@, {base + 10}@, {base + 11}@ = "
                "Coop.CollectNetworkPlayersForTheMission()",
                refresh,
            )
            self.assertIn(
                f"{base + 19}@ == {base + 3}@({base + 16}@,3i)", refresh
            )
            self.assertIn(f"{base}@({base + 16}@,3i) = 0", refresh)
            self.assertIn(f"{base + 6}@({base + 16}@,3i) = -1", refresh)
            self.assertIn("nonblocking DNF", text)
            self.assertIn("Coop.TeleportPlayersToHostSafely", text)

    def test_connected_death_uses_each_stock_failure_path(self):
        cases = (
            (STUNT, "STUNT", "212@ = 1", "goto @STUNT_1602"),
            (MTBIKER, "MTBIKER", "812@ = 1", "goto @MTBIKER_14393"),
            (BCOUR, "BCOUR", "912@ = 1", "goto @BCOUR_14327"),
        )
        for text, prefix, latch, bridge in cases:
            update = text.split(f":{prefix}_COOP_UPDATE", 1)[1].split(
                f":{prefix}_COOP_POLL_BIKE_REGISTRATION", 1
            )[0]
            self.assertIn("COOP_PARTICIPANT_DEATH", update)
            self.assertIn("Char.IsDead", update)
            self.assertIn(latch, update)
            self.assertIn(bridge, text)

    def test_entity_registration_is_bounded_and_nonblocking(self):
        cases = (
            (STUNT, "STUNT", "Coop.GetVehicleNetworkId(34@)"),
            (MTBIKER, "MTBIKER", "Coop.GetVehicleNetworkId(732@(757@,6i))"),
            (BCOUR, "BCOUR", "Coop.GetVehicleNetworkId(36@)"),
        )
        for text, prefix, registration in cases:
            poll = text.split(f":{prefix}_COOP_POLL_BIKE_REGISTRATION", 1)[
                1
            ].split(f":{prefix}_COOP_UPDATE_REGROUP", 1)[0]
            self.assertIn(registration, poll)
            self.assertIn("> 5000", poll)
            self.assertNotIn("wait ", poll.lower())
            self.assertNotIn(
                f"goto @{prefix}_COOP_POLL_BIKE_REGISTRATION", poll
            )

    def test_supporters_receive_activity_specific_targeted_objectives(self):
        for text, prefix in (
            (STUNT, "STUNT"),
            (MTBIKER, "MTBIKER"),
            (BCOUR, "BCOUR"),
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
        self.assertIn("36@(216@,20i) == 0", STUNT)
        self.assertIn("745@(757@,10i)", MTBIKER)
        self.assertIn("653@(916@,30i) == 0", BCOUR)
        for route in ("MTROUT1", "MTROUT2", "MTROUT3"):
            self.assertIn(f"Coop.PrintNowForNetworkPlayer('{route}'", MTBIKER)

    def test_stock_variants_payouts_progression_and_unlocks_are_preserved(self):
        self.assertEqual(STUNT.count("Player.AddScore($player1, 1000)"), 1)
        self.assertEqual(STUNT.count("Stat.RegisterMissionPassed('BMX')"), 1)
        self.assertEqual(STUNT.count("Stat.RegisterMissionPassed('NRG500')"), 1)
        self.assertEqual(STUNT.count("$done_bmx_stunt_progress = 1"), 1)
        self.assertEqual(STUNT.count("$done_nrg500_stunt_progress = 1"), 1)
        self.assertIn("StatId.BestTimeBmx", STUNT)
        self.assertIn("StatId.BestTimeNrg", STUNT)

        for selection in (1, 2, 3):
            self.assertIn(f"$mtbikerace_selection == {selection}", MTBIKER)
        for payout in (500, 1000, 2000):
            self.assertEqual(
                MTBIKER.count(f"Player.AddScore($player1, {payout})"), 1
            )
        self.assertEqual(MTBIKER.count("$mtbikerace_selection += 1"), 1)
        self.assertEqual(MTBIKER.count("$flag_mtbike_passed_1stime = 1"), 1)
        self.assertEqual(MTBIKER.count("Stat.RegisterOddjobMissionPassed"), 1)

        for model in (481, 463, 462):
            self.assertIn(f"Char.IsInModel($scplayer, {model})", ODDVEH)
        for city in (1, 2, 3):
            self.assertIn(f"872@ == {city}", BCOUR)
        self.assertEqual(BCOUR.count("Player.AddScore($player1, 871@)"), 1)
        self.assertEqual(BCOUR.count("Stat.RegisterOddjobMissionPassed"), 3)
        for city in ("LA", "SF", "LV"):
            self.assertEqual(BCOUR.count(f"$courier{city}_passed_once = 1"), 1)
            self.assertEqual(BCOUR.count(f"$courier{city}_cash_pickup ="), 1)

    def test_results_and_cleanup_are_exactly_once_and_idempotent(self):
        for text, prefix, result, cleanup in (
            (STUNT, "STUNT", 213, 214),
            (MTBIKER, "MTBIKER", 813, 814),
            (BCOUR, "BCOUR", 913, 914),
        ):
            self.assertEqual(text.count(f"gosub @{prefix}_COOP_NOTIFY_FAILURE"), 1)
            self.assertEqual(text.count(f"gosub @{prefix}_COOP_NOTIFY_PASS"), 1)
            self.assertEqual(text.count(f"gosub @{prefix}_COOP_CLEANUP"), 1)
            self.assertIn(f"{result}@ = 1", text)
            self.assertIn(f"{result}@ = 2", text)
            cleanup_body = text.split(f":{prefix}_COOP_CLEANUP", 1)[1]
            self.assertIn(f"{cleanup}@ == 1", cleanup_body)
            self.assertIn(f"{cleanup}@ = 1", cleanup_body)
            self.assertIn("Coop.ClearAllEntityBlipsForNetworkPlayer", cleanup_body)
            self.assertIn("Coop.RemoveCheckpointForNetworkPlayer", cleanup_body)
            self.assertIn("Char.FreezePosition", cleanup_body)
            self.assertIn("deterministic", cleanup_body)
            self.assertIn("restore policy", cleanup_body)
            self.assertEqual(text.count("Mission.Finish"), 1)


if __name__ == "__main__":
    unittest.main()
