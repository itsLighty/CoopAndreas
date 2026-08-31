import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


def function_body(source: str, signature: str) -> str:
    match = re.search(signature, source)
    if match is None:
        raise AssertionError(f"missing function matching {signature}")
    opening = source.find("{", match.end())
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : index]
    raise AssertionError(f"unterminated function matching {signature}")


class OccupancyAuthorityModel:
    def __init__(self):
        self.seats = {}
        self.players = {}

    def intent_enter(self, player, vehicle, seat):
        return self.seats.get((vehicle, seat)) in (None, player) and player not in self.players

    def confirm_enter(self, player, vehicle, seat):
        occupant = self.seats.get((vehicle, seat))
        if occupant not in (None, player):
            return False
        if self.players.get(player) == (vehicle, seat) and occupant == player:
            return True
        self.confirm_exit(player)
        self.seats[(vehicle, seat)] = player
        self.players[player] = (vehicle, seat)
        return True

    def intent_exit(self, player):
        return player in self.players

    def confirm_exit(self, player):
        removed = self.players.pop(player, None)
        for key, occupant in list(self.seats.items()):
            if occupant == player:
                self.seats.pop(key)
                removed = removed or key
        return removed is not None

    def can_update(self, player, vehicle, seat):
        return self.players.get(player) == (vehicle, seat) and self.seats.get((vehicle, seat)) == player

    def remove_vehicle(self, vehicle):
        removed = []
        for player, occupancy in list(self.players.items()):
            if occupancy[0] == vehicle:
                self.confirm_exit(player)
                removed.append(player)
        return removed

    def disconnect(self, player):
        return self.confirm_exit(player)


class LocalOccupancyObserverModel:
    RETRY_MS = 500
    MAX_CONFIRMATIONS = 3

    def __init__(self):
        self.occupancy = None
        self.confirmations = 0
        self.last_confirmation_at = 0

    @staticmethod
    def native_seat(vehicle, driver=False, passenger_index=None, alive=True, streamed=True):
        if not alive or not streamed:
            return None
        if driver:
            return vehicle, 0
        if passenger_index is not None and 0 <= passenger_index < 7:
            return vehicle, passenger_index + 1
        return None

    def process(self, native_occupancy, now, authenticated=True):
        events = []
        if not authenticated:
            self.occupancy = None
            self.confirmations = 0
            self.last_confirmation_at = 0
            return events
        if native_occupancy != self.occupancy:
            if self.occupancy is not None:
                events.append(("exit", self.occupancy))
            self.occupancy = native_occupancy
            self.confirmations = 0
            self.last_confirmation_at = now
            if self.occupancy is not None:
                events.append(("enter", self.occupancy))
                self.confirmations = 1
            return events
        if (
            self.occupancy is not None
            and self.confirmations < self.MAX_CONFIRMATIONS
            and now - self.last_confirmation_at >= self.RETRY_MS
        ):
            events.append(("enter", self.occupancy))
            self.confirmations += 1
            self.last_confirmation_at = now
        return events


class TransitionRateModel:
    WINDOW_MS = 1000
    MAX_TRANSITIONS = 8

    def __init__(self):
        self.window_start = 0
        self.count = 0

    def allow(self, now):
        if now - self.window_start >= self.WINDOW_MS:
            self.window_start = now
            self.count = 0
        if self.count >= self.MAX_TRANSITIONS:
            return False
        self.count += 1
        return True


class VehicleOccupancyTransitionSyncTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.task_hooks = (ROOT / "client/src/Hooks/TaskHooks.cpp").read_text(encoding="utf-8")
        cls.passenger = (ROOT / "client/src/CPassengerEnter.cpp").read_text(encoding="utf-8")
        cls.observer_h = (ROOT / "client/src/CLocalVehicleOccupancySync.h").read_text(encoding="utf-8")
        cls.observer = (ROOT / "client/src/CLocalVehicleOccupancySync.cpp").read_text(encoding="utf-8")
        cls.server_player = (ROOT / "server/src/CNetworkPlayer.h").read_text(encoding="utf-8")
        cls.server = (ROOT / "server/src/PacketHandlers/vehicles.cpp").read_text(encoding="utf-8")

    def test_constructor_packets_are_visual_intent_only(self):
        driver_ctor = function_body(self.task_hooks, r"CTaskComplexEnterCarAsDriver__Ctor_Hook\s*\(")
        exit_ctor = function_body(self.task_hooks, r"CTaskComplexLeaveCar__Ctor_Hook\s*\(")
        passenger_process = function_body(self.passenger, r"CPassengerEnter::Process\s*\(")
        for body in (driver_ctor, exit_ctor, passenger_process):
            self.assertIn("bForce = false", body)
            self.assertNotIn("bForce = true", body)
        self.assertIn(
            "Events::gameProcessEvent.after += [] { CLocalVehicleOccupancySync::Process(); }",
            self.task_hooks,
        )

    def test_observer_uses_committed_native_vehicle_and_seat_state(self):
        capture = function_body(self.observer, r"CLocalVehicleOccupancySync::CaptureNativeOccupancy\s*\(")
        for evidence in (
            "player->IsAlive()",
            "player->m_nPedFlags.bInVehicle",
            "player->m_pVehicle",
            "CPools::ms_pVehiclePool->IsObjectValid(player->m_pVehicle)",
            "CNetworkVehicleManager::GetVehicle(vehicle)",
            "vehicle->m_pDriver == player",
            "vehicle->m_apPassengers[passengerSeat] == player",
            "passengerSeat + 1",
        ):
            self.assertIn(evidence, capture)
        self.assertIn("return {};", capture)

        enter = function_body(self.observer, r"CLocalVehicleOccupancySync::SendEnterConfirmation\s*\(")
        leave = function_body(self.observer, r"CLocalVehicleOccupancySync::SendExitConfirmation\s*\(")
        self.assertIn("packet.bForce = true", enter)
        self.assertIn("packet.bForce = true", leave)
        self.assertIn("packet.bPassenger = occupancy.serverSeat > 0", enter)
        self.assertIn("occupancy.serverSeat - 1", enter)

    def test_actual_changes_exit_old_seat_then_confirm_new_seat(self):
        process = function_body(self.observer, r"CLocalVehicleOccupancySync::Process\s*\(")
        transition = process[process.index("nativeOccupancy != ms_confirmedOccupancy") :]
        self.assertLess(transition.index("SendExitConfirmation()"), transition.index("ms_confirmedOccupancy ="))
        self.assertLess(transition.index("ms_confirmedOccupancy ="), transition.index("SendEnterConfirmation("))
        self.assertIn("if (!CNetwork::m_bAuthenticated)", process)
        self.assertIn("Reset();", process)
        self.assertIn("OCCUPANCY_CONFIRM_RETRY_MS = 500", self.observer)
        self.assertIn("MAX_OCCUPANCY_CONFIRMATIONS = 3", self.observer)
        self.assertIn("ms_nConfirmationCount < MAX_OCCUPANCY_CONFIRMATIONS", process)

    def test_server_demotes_intent_and_canonicalizes_confirmations(self):
        enter = function_body(self.server, r"VEHICLE_ENTER\s*,")
        occupied_guard = enter.index("m_pPlayers[seat] &&")
        idempotent = enter.index("IsExactOccupant")
        intent = enter.index("if (!pVehicleEnter->bForce)")
        clear = enter.index("RemovePlayerFromAllVehicleSeats")
        claim = enter.index("SetOccupant")
        relay = enter.rindex("SendToAll")
        self.assertLess(occupied_guard, idempotent)
        self.assertLess(idempotent, intent)
        self.assertLess(intent, clear)
        self.assertLess(clear, claim)
        self.assertLess(claim, relay)
        intent_body = function_body(enter, r"if\s*\(!pVehicleEnter->bForce\)")
        self.assertNotIn("SetOccupant", intent_body)
        self.assertNotIn("RemovePlayerFromAllVehicleSeats", intent_body)
        for evidence in (
            "pVehicleEnter->playerid = pNetworkPlayer->m_iPlayerId",
            "pVehicleEnter->bPassenger = seat > 0",
            "pVehicleEnter->seatid = seat > 0",
        ):
            self.assertIn(evidence, enter)

    def test_server_exit_intent_does_not_clear_and_forced_exit_repairs_stale_state(self):
        leave = function_body(self.server, r"VEHICLE_EXIT\s*,")
        intent = function_body(leave, r"if\s*\(!pVehicleExit->bForce\)")
        self.assertNotIn("RemovePlayerFromAllVehicleSeats", intent)
        self.assertIn("exactOccupancy", intent)
        self.assertIn("HasPlayerVehicleSeatReference", leave)
        self.assertIn("RemovePlayerFromAllVehicleSeats(pNetworkPlayer)", leave)
        self.assertIn("pVehicleExit->playerid = pNetworkPlayer->m_iPlayerId", leave)

        remove_vehicle = function_body(self.server, r"VEHICLE_REMOVE\s*,")
        self.assertLess(
            remove_vehicle.index("RemovePlayerFromAllVehicleSeats"),
            remove_vehicle.index("CNetworkVehicleManager::Remove(vehicle)"),
        )
        self.assertIn("exit.bForce = true", remove_vehicle)

    def test_transition_rate_is_bounded_but_idempotency_precedes_it(self):
        rate = function_body(self.server, r"CanApplyVehicleTransition\s*\(")
        self.assertIn("VEHICLE_TRANSITION_RATE_WINDOW_MS", rate)
        self.assertIn("MAX_VEHICLE_TRANSITIONS_PER_WINDOW", rate)
        self.assertIn("m_nVehicleTransitionWindowStartMs", self.server_player)
        self.assertIn("m_nVehicleTransitionCount", self.server_player)
        enter = function_body(self.server, r"VEHICLE_ENTER\s*,")
        self.assertLess(enter.index("IsExactOccupant"), enter.index("CanApplyVehicleTransition"))

        limiter = TransitionRateModel()
        self.assertEqual([limiter.allow(10) for _ in range(9)], [True] * 8 + [False])
        self.assertTrue(limiter.allow(1000))

    def test_authority_model_covers_cancel_warp_race_seat_change_and_spoof(self):
        model = OccupancyAuthorityModel()

        # Entry intent can animate remotely, but canceling it leaves the seat unreserved.
        self.assertTrue(model.intent_enter("alice", 10, 0))
        self.assertNotIn("alice", model.players)
        self.assertFalse(model.can_update("alice", 10, 0))

        # A completed normal entry or direct SCM/native warp uses the same forced confirmation.
        self.assertTrue(model.confirm_enter("alice", 10, 0))
        self.assertTrue(model.can_update("alice", 10, 0))
        self.assertTrue(model.confirm_enter("alice", 10, 0))  # bounded retry is idempotent

        # Another peer cannot steal an occupied canonical seat.
        self.assertFalse(model.confirm_enter("mallory", 10, 0))
        self.assertNotIn("mallory", model.players)

        # A canceled exit remains occupied; an actual seat change clears the old seat first.
        self.assertTrue(model.intent_exit("alice"))
        self.assertTrue(model.can_update("alice", 10, 0))
        self.assertTrue(model.confirm_enter("alice", 10, 2))
        self.assertNotIn((10, 0), model.seats)
        self.assertTrue(model.can_update("alice", 10, 2))
        self.assertTrue(model.confirm_exit("alice"))
        self.assertFalse(model.can_update("alice", 10, 2))

    def test_canceled_enter_and_direct_scm_warp_are_distinct(self):
        observer = LocalOccupancyObserverModel()
        authority = OccupancyAuthorityModel()

        # The constructor may send visual intent, but native state remains on foot when entry is canceled.
        self.assertTrue(authority.intent_enter("alice", 10, 0))
        self.assertEqual(observer.process(None, 0), [])
        self.assertNotIn("alice", authority.players)

        # A direct 036A/native warp has no constructor event; actual driver state is sufficient.
        warp = observer.native_seat(10, driver=True)
        self.assertEqual(observer.process(warp, 1), [("enter", (10, 0))])
        self.assertTrue(authority.confirm_enter("alice", 10, 0))
        self.assertTrue(authority.can_update("alice", 10, 0))

    def test_canceled_exit_death_and_actual_exit_only_clear_after_native_change(self):
        observer = LocalOccupancyObserverModel()
        authority = OccupancyAuthorityModel()
        driver = observer.native_seat(10, driver=True)
        observer.process(driver, 0)
        authority.confirm_enter("alice", 10, 0)

        self.assertTrue(authority.intent_exit("alice"))
        self.assertEqual(observer.process(driver, 100), [])  # canceled leave task
        self.assertTrue(authority.can_update("alice", 10, 0))

        dead_state = observer.native_seat(10, driver=True, alive=False)
        self.assertEqual(observer.process(dead_state, 101), [("exit", (10, 0))])
        self.assertTrue(authority.confirm_exit("alice"))
        self.assertFalse(authority.can_update("alice", 10, 0))

    def test_a_to_b_switch_orders_exit_before_enter_and_maps_passenger_seats(self):
        observer = LocalOccupancyObserverModel()
        driver_a = observer.native_seat(10, driver=True)
        passenger_b = observer.native_seat(20, passenger_index=2)
        self.assertEqual(passenger_b, (20, 3))  # native passenger index 2 is canonical server seat 3
        observer.process(driver_a, 0)
        self.assertEqual(
            observer.process(passenger_b, 20),
            [("exit", (10, 0)), ("enter", (20, 3))],
        )

        self.assertEqual(observer.native_seat(20, passenger_index=0), (20, 1))
        self.assertEqual(observer.native_seat(20, passenger_index=6), (20, 7))
        self.assertIsNone(observer.native_seat(20, passenger_index=7))

    def test_event_vs_sync_race_recovers_and_retries_are_bounded(self):
        observer = LocalOccupancyObserverModel()
        authority = OccupancyAuthorityModel()
        driver = observer.native_seat(10, driver=True)

        # Main's unreliable SYNC callback can run before the post-native EVENT confirmation.
        self.assertFalse(authority.can_update("alice", 10, 0))
        self.assertEqual(observer.process(driver, 10), [("enter", (10, 0))])
        authority.confirm_enter("alice", 10, 0)
        self.assertTrue(authority.can_update("alice", 10, 0))

        self.assertEqual(observer.process(driver, 509), [])
        self.assertEqual(observer.process(driver, 510), [("enter", (10, 0))])
        self.assertTrue(authority.confirm_enter("alice", 10, 0))  # server idempotence
        self.assertEqual(observer.process(driver, 1010), [("enter", (10, 0))])
        self.assertEqual(observer.process(driver, 1510), [])

    def test_vehicle_deletion_and_disconnect_release_canonical_occupancy(self):
        observer = LocalOccupancyObserverModel()
        authority = OccupancyAuthorityModel()
        driver = observer.native_seat(10, driver=True)
        observer.process(driver, 0)
        authority.confirm_enter("alice", 10, 0)

        # Server removal clears occupants before deleting the vehicle object.
        self.assertEqual(authority.remove_vehicle(10), ["alice"])
        self.assertNotIn("alice", authority.players)
        # Once the network mapping disappears, capture returns no occupancy and emits a forced exit.
        self.assertEqual(observer.process(observer.native_seat(10, driver=True, streamed=False), 1),
                         [("exit", (10, 0))])

        observer.process(observer.native_seat(20, passenger_index=0), 2)
        authority.confirm_enter("alice", 20, 1)
        # Disconnect disables sending; local state resets and server lifecycle cleanup owns release.
        self.assertEqual(observer.process((20, 1), 3, authenticated=False), [])
        self.assertTrue(authority.disconnect("alice"))
        self.assertNotIn("alice", authority.players)


if __name__ == "__main__":
    unittest.main()
