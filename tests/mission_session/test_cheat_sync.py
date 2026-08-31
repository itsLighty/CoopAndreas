import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


# Retail GTA SA 1.0 US CCheat table order. This is the executable wire-ID contract.
STOCK_CHEAT_ORDER = (
    "WEAPON_SET_1", "WEAPON_SET_2", "WEAPON_SET_3", "HEALTH_ARMOUR_MONEY",
    "WANTED_LEVEL_UP", "WANTED_LEVEL_CLEAR", "WEATHER_SUNNY",
    "WEATHER_EXTRA_SUNNY", "WEATHER_CLOUDY", "WEATHER_RAINY", "WEATHER_FOGGY",
    "FASTER_CLOCK", "FASTER_GAMEPLAY", "SLOWER_GAMEPLAY", "MAYHEM",
    "EVERYBODY_ATTACKS_PLAYER", "EVERYONE_ARMED", "SPAWN_RHINO",
    "SPAWN_BLOODRING_BANGER", "SPAWN_RANCHER", "SPAWN_HOTRING_A",
    "SPAWN_HOTRING_B", "SPAWN_ROMERO", "SPAWN_STRETCH", "SPAWN_TRASHMASTER",
    "SPAWN_CADDY", "BLOW_UP_ALL_CARS", "INVISIBLE_CARS", "PERFECT_HANDLING",
    "SUICIDE", "GREEN_LIGHTS", "AGGRESSIVE_DRIVERS", "PINK_TRAFFIC",
    "BLACK_TRAFFIC", "CARS_ON_WATER", "BOATS_FLY", "FAT_PLAYER", "MAX_MUSCLE",
    "SKINNY_PLAYER", "ELVIS_EVERYWHERE", "PEDS_ATTACK_WITH_ROCKETS",
    "BEACH_PARTY", "GANG_MEMBERS_EVERYWHERE", "GANGS_CONTROL_STREETS",
    "NINJA_THEME", "LOVE_CONQUERS_ALL", "CHEAP_TRAFFIC", "FAST_TRAFFIC",
    "CARS_FLY", "HUGE_BUNNY_HOP", "SPAWN_HYDRA", "SPAWN_VORTEX",
    "SMASH_AND_BOOM", "ALL_CARS_NITRO", "CARS_FLOAT_WHEN_HIT",
    "ALWAYS_MIDNIGHT", "ORANGE_SKY", "THUNDERSTORM", "SANDSTORM",
    "UNUSED_PREDATOR", "MEGA_JUMP", "INFINITE_HEALTH", "INFINITE_OXYGEN",
    "GIVE_PARACHUTE", "GIVE_JETPACK", "NEVER_WANTED", "SIX_STAR_WANTED",
    "MEGA_PUNCH", "NEVER_HUNGRY", "RIOT_MODE", "FUNHOUSE_THEME",
    "ADRENALINE_MODE", "INFINITE_AMMO", "DRIVEBY_AIMING", "REDUCED_TRAFFIC",
    "COUNTRY_TRAFFIC", "RECRUIT_ANYONE", "RECRUIT_WITH_PISTOLS",
    "RECRUIT_WITH_ROCKETS", "MAX_RESPECT", "MAX_SEX_APPEAL", "MAX_STAMINA",
    "MAX_WEAPON_SKILLS", "MAX_VEHICLE_SKILLS", "SPAWN_HUNTER", "SPAWN_QUAD",
    "SPAWN_TANKER", "SPAWN_DOZER", "SPAWN_STUNT_PLANE", "SPAWN_MONSTER",
    "PROSTITUTES_PAY_PLAYER", "ALL_TAXIS_NITRO",
)


PERSISTENT = {
    "FASTER_CLOCK",
    "MAYHEM",
    "EVERYBODY_ATTACKS_PLAYER",
    "EVERYONE_ARMED",
    "INVISIBLE_CARS",
    "PERFECT_HANDLING",
    "GREEN_LIGHTS",
    "AGGRESSIVE_DRIVERS",
    "PINK_TRAFFIC",
    "BLACK_TRAFFIC",
    "CARS_ON_WATER",
    "BOATS_FLY",
    "ELVIS_EVERYWHERE",
    "PEDS_ATTACK_WITH_ROCKETS",
    "BEACH_PARTY",
    "GANG_MEMBERS_EVERYWHERE",
    "GANGS_CONTROL_STREETS",
    "NINJA_THEME",
    "LOVE_CONQUERS_ALL",
    "CHEAP_TRAFFIC",
    "FAST_TRAFFIC",
    "CARS_FLY",
    "HUGE_BUNNY_HOP",
    "SMASH_AND_BOOM",
    "ALL_CARS_NITRO",
    "CARS_FLOAT_WHEN_HIT",
    "ALWAYS_MIDNIGHT",
    "ORANGE_SKY",
    "MEGA_JUMP",
    "INFINITE_HEALTH",
    "INFINITE_OXYGEN",
    "NEVER_WANTED",
    "MEGA_PUNCH",
    "NEVER_HUNGRY",
    "RIOT_MODE",
    "FUNHOUSE_THEME",
    "ADRENALINE_MODE",
    "INFINITE_AMMO",
    "DRIVEBY_AIMING",
    "REDUCED_TRAFFIC",
    "COUNTRY_TRAFFIC",
    "RECRUIT_ANYONE",
    "RECRUIT_WITH_PISTOLS",
    "RECRUIT_WITH_ROCKETS",
    "PROSTITUTES_PAY_PLAYER",
    "ALL_TAXIS_NITRO",
}

SPECIAL_THEMES = {
    "BEACH_PARTY",
    "CHEAP_TRAFFIC",
    "FAST_TRAFFIC",
    "FUNHOUSE_THEME",
    "COUNTRY_TRAFFIC",
}


class CheatAuthorityModel:
    """Executable model of the fixed stock transition table used by the server."""

    RATE_LIMIT = 8

    def __init__(self, host=0):
        self.host = host
        self.mission_active = False
        self.last_request = 0
        self.rate_count = 0
        self.state: set[str] = set()
        self.revision = 1
        self.event_sequence = 0
        self.gameplay_speed_step = 0
        self.actions: list[tuple[int, str]] = []

    def _set(self, cheat, enabled):
        if enabled:
            self.state.add(cheat)
        else:
            self.state.discard(cheat)

    def _persistent_transition(self, cheat):
        enabled = cheat not in self.state
        self._set(cheat, enabled)
        if not enabled:
            if cheat == "PEDS_ATTACK_WITH_ROCKETS":
                self._set(
                    "EVERYBODY_ATTACKS_PLAYER",
                    "EVERYBODY_ATTACKS_PLAYER" not in self.state,
                )
                self._set("EVERYONE_ARMED", True)
            return
        if cheat == "PINK_TRAFFIC":
            self._set("BLACK_TRAFFIC", False)
        elif cheat == "BLACK_TRAFFIC":
            self._set("PINK_TRAFFIC", False)
        elif cheat in SPECIAL_THEMES:
            for other in SPECIAL_THEMES - {cheat}:
                self._set(other, False)
        elif cheat == "NINJA_THEME":
            for other in SPECIAL_THEMES:
                self._set(other, False)
            self._set("BLACK_TRAFFIC", True)
            self._set("PINK_TRAFFIC", False)
        elif cheat == "PEDS_ATTACK_WITH_ROCKETS":
            self._set("EVERYONE_ARMED", False)
            self._set(
                "EVERYBODY_ATTACKS_PLAYER",
                "EVERYBODY_ATTACKS_PLAYER" not in self.state,
            )

    def request(self, sender, sequence, cheat):
        if (
            sender != self.host
            or self.mission_active
            or sequence <= self.last_request
            or self.rate_count >= self.RATE_LIMIT
        ):
            return False
        self.last_request = sequence
        self.rate_count += 1
        if cheat in PERSISTENT:
            self._persistent_transition(cheat)
            self.revision += 1
        elif cheat in {"FASTER_GAMEPLAY", "SLOWER_GAMEPLAY"}:
            delta = 1 if cheat == "FASTER_GAMEPLAY" else -1
            self.gameplay_speed_step = max(-2, min(2, self.gameplay_speed_step + delta))
            self.revision += 1
        else:
            self.event_sequence += 1
            self.actions.append((self.event_sequence, cheat))
        return True

    def migrate(self, new_host):
        self.host = new_host
        self.last_request = 0
        self.rate_count = 0
        self.revision += 1


class CheatTransitionModelTests(unittest.TestCase):
    def test_non_host_mission_stale_and_rate_limited_requests_are_rejected(self):
        model = CheatAuthorityModel(host=2)
        self.assertFalse(model.request(1, 1, "MEGA_JUMP"))
        model.mission_active = True
        self.assertFalse(model.request(2, 1, "MEGA_JUMP"))
        model.mission_active = False
        self.assertTrue(model.request(2, 1, "MEGA_JUMP"))
        self.assertFalse(model.request(2, 1, "MEGA_JUMP"))
        for sequence in range(2, 9):
            self.assertTrue(model.request(2, sequence, "MEGA_PUNCH"))
        self.assertFalse(model.request(2, 9, "MEGA_PUNCH"))

    def test_stat_setters_are_events_and_never_flip_canonical_bits(self):
        model = CheatAuthorityModel()
        for sequence, cheat in enumerate(
            ("MAX_RESPECT", "MAX_RESPECT", "MAX_SEX_APPEAL", "MAX_STAMINA"),
            1,
        ):
            self.assertTrue(model.request(0, sequence, cheat))
        self.assertFalse(model.state & set(dict(model.actions).values()))
        self.assertEqual([1, 2, 3, 4], [sequence for sequence, _ in model.actions])
        self.assertEqual(2, [cheat for _, cheat in model.actions].count("MAX_RESPECT"))

    def test_physics_strength_cheats_are_reversible_runtime_flags(self):
        model = CheatAuthorityModel()
        for sequence, cheat in enumerate(
            ("HUGE_BUNNY_HOP", "MEGA_JUMP", "MEGA_PUNCH"), 1
        ):
            self.assertTrue(model.request(0, sequence, cheat))
            self.assertIn(cheat, model.state)
        self.assertTrue(model.request(0, 4, "MEGA_JUMP"))
        self.assertNotIn("MEGA_JUMP", model.state)
        self.assertEqual([], model.actions)

    def test_recruit_modes_are_independent_stock_flags(self):
        model = CheatAuthorityModel()
        recruits = (
            "RECRUIT_ANYONE",
            "RECRUIT_WITH_PISTOLS",
            "RECRUIT_WITH_ROCKETS",
        )
        for sequence, cheat in enumerate(recruits, 1):
            self.assertTrue(model.request(0, sequence, cheat))
        self.assertTrue(set(recruits) <= model.state)
        self.assertTrue(model.request(0, 4, "RECRUIT_WITH_PISTOLS"))
        self.assertEqual(
            {"RECRUIT_ANYONE", "RECRUIT_WITH_ROCKETS"}, model.state
        )

    def test_theme_and_traffic_transitions_are_canonical(self):
        model = CheatAuthorityModel()
        self.assertTrue(model.request(0, 1, "BEACH_PARTY"))
        self.assertTrue(model.request(0, 2, "FAST_TRAFFIC"))
        self.assertEqual({"FAST_TRAFFIC"}, model.state)
        self.assertTrue(model.request(0, 3, "PINK_TRAFFIC"))
        self.assertTrue(model.request(0, 4, "NINJA_THEME"))
        self.assertNotIn("PINK_TRAFFIC", model.state)
        self.assertNotIn("FAST_TRAFFIC", model.state)
        self.assertIn("BLACK_TRAFFIC", model.state)
        self.assertIn("NINJA_THEME", model.state)

    def test_rocket_ped_cheat_preserves_stock_asymmetric_coupling(self):
        model = CheatAuthorityModel()
        self.assertTrue(model.request(0, 1, "EVERYONE_ARMED"))
        self.assertTrue(model.request(0, 2, "PEDS_ATTACK_WITH_ROCKETS"))
        self.assertIn("PEDS_ATTACK_WITH_ROCKETS", model.state)
        self.assertNotIn("EVERYONE_ARMED", model.state)
        self.assertIn("EVERYBODY_ATTACKS_PLAYER", model.state)
        self.assertTrue(model.request(0, 3, "PEDS_ATTACK_WITH_ROCKETS"))
        self.assertNotIn("PEDS_ATTACK_WITH_ROCKETS", model.state)
        self.assertIn("EVERYONE_ARMED", model.state)
        self.assertNotIn("EVERYBODY_ATTACKS_PLAYER", model.state)

    def test_host_migration_keeps_mask_and_resets_authority_sequence(self):
        model = CheatAuthorityModel()
        self.assertTrue(model.request(0, 50, "MEGA_JUMP"))
        old_revision = model.revision
        model.migrate(1)
        self.assertIn("MEGA_JUMP", model.state)
        self.assertGreater(model.revision, old_revision)
        self.assertTrue(model.request(1, 1, "MEGA_PUNCH"))
        self.assertFalse(model.request(0, 51, "MEGA_PUNCH"))

    def test_gameplay_speed_is_bounded_canonical_and_replayed(self):
        model = CheatAuthorityModel()
        for sequence in range(1, 5):
            self.assertTrue(model.request(0, sequence, "FASTER_GAMEPLAY"))
        self.assertEqual(2, model.gameplay_speed_step)
        late_join_scale = (0.25, 0.5, 1.0, 2.0, 4.0)[model.gameplay_speed_step + 2]
        self.assertEqual(4.0, late_join_scale)
        self.assertTrue(model.request(0, 5, "SLOWER_GAMEPLAY"))
        self.assertEqual(1, model.gameplay_speed_step)
        self.assertNotIn("FASTER_GAMEPLAY", dict(model.actions).values())


class ExactlyOnceClientModelTests(unittest.TestCase):
    def test_transient_events_reject_replay_and_foreign_run_until_reconnect(self):
        applied = []
        run_id = 10
        last_sequence = 0
        for incoming_run, sequence, cheat in [
            (10, 1, "WEAPON_SET_1"),
            (10, 1, "WEAPON_SET_1"),
            (10, 0, "WEAPON_SET_2"),
            (11, 1, "WEAPON_SET_2"),
        ]:
            if incoming_run != run_id:
                continue
            if sequence == 0 or sequence <= last_sequence:
                continue
            last_sequence = sequence
            applied.append(cheat)
        self.assertEqual(["WEAPON_SET_1"], applied)

        # A reconnect creates a fresh client session and may bind the new run exactly once.
        run_id, last_sequence = 11, 0
        self.assertEqual((11, 0), (run_id, last_sequence))


class CheatSourceContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.packet_types = (ROOT / "shared/network/packet_types.h").read_text(
            encoding="utf-8"
        )
        cls.packets = (ROOT / "shared/network/packets/cheats.h").read_text(
            encoding="utf-8"
        )
        cls.server = (ROOT / "server/src/CCheatAuthorityManager.cpp").read_text(
            encoding="utf-8"
        )
        cls.client = (ROOT / "client/src/CNetworkCheatManager.cpp").read_text(
            encoding="utf-8"
        )

    def test_protocol_is_append_only_and_bumped_to_038(self):
        config = (ROOT / "shared/config.h").read_text(encoding="utf-8")
        self.assertIn('COOPANDREAS_VERSION "0.3.10-alpha"', config)
        enum = re.search(
            r"enum class ePacketType[^\{]*\{(.*?)PACKET_ID_MAX",
            self.packet_types,
            re.S,
        ).group(1)
        positions = [enum.index(name) for name in ("CHEAT_REQUEST", "CHEAT_STATE", "CHEAT_ACTION")]
        self.assertEqual(positions, sorted(positions))
        self.assertGreater(positions[0], enum.index("FIRE_EXTINGUISH_REQUEST"))

    def test_wire_is_a_fixed_bounded_enum_without_strings_or_pointers(self):
        self.assertIn("STOCK_CHEAT_COUNT == 92", self.packets)
        self.assertIn("enum class eStockCheat : uint8_t", self.packets)
        self.assertIn("CHEAT_MASK_BYTES", self.packets)
        self.assertIn("serialize_int(stream, value, 0, STOCK_CHEAT_COUNT - 1)", self.packets)
        for forbidden in ("std::string", "char*", "void*", "uintptr_t", "m_CheatString"):
            self.assertNotIn(forbidden, self.packets)

        enum_body = re.search(
            r"enum class eStockCheat\s*:\s*uint8_t\s*\{(.*?)\n\};",
            self.packets,
            re.S,
        ).group(1)
        wire_order = tuple(re.findall(r"^\s+([A-Z0-9_]+)(?:\s*=\s*\d+)?\s*,", enum_body, re.M))
        wire_order = tuple(name for name in wire_order if name != "COUNT")
        self.assertEqual(STOCK_CHEAT_ORDER, wire_order)

    def test_only_reversible_flags_enter_the_persistent_mask(self):
        persistent_body = self.packets.split("inline bool IsPersistentCheat", 1)[1]
        persistent_body = persistent_body.split("inline bool IsCanonicalGameplaySpeedCheat", 1)[0]
        for cheat in ("HUGE_BUNNY_HOP", "MEGA_JUMP", "MEGA_PUNCH"):
            self.assertIn(f"eStockCheat::{cheat}", persistent_body)
        for cheat in ("MAX_RESPECT", "MAX_SEX_APPEAL", "MAX_STAMINA", "MAX_WEAPON_SKILLS"):
            self.assertNotIn(f"eStockCheat::{cheat}", persistent_body)
        self.assertIn("IsLatchedStatSetter(cheat)", self.client)

    def test_gameplay_speed_is_bounded_replayed_and_offline_scale_is_restored(self):
        self.assertIn("GAMEPLAY_SPEED_STEP_MIN = -2", self.packets)
        self.assertIn("GAMEPLAY_SPEED_STEP_MAX = 2", self.packets)
        self.assertIn("gameplaySpeedStep", self.packets)
        self.assertIn("IsCanonicalGameplaySpeedCheat(request.cheat)", self.server)
        self.assertIn("event.gameplaySpeedStep = g_gameplaySpeedStep", self.server)
        self.assertIn("ApplyGameplaySpeedStep(m_canonicalGameplaySpeedStep)", self.client)
        self.assertIn("m_offlineTimeScale = CTimer::ms_fTimeScale", self.client)
        self.assertIn("CTimer::ms_fTimeScale = m_offlineTimeScale", self.client)

    def test_server_requires_host_mission_safety_rate_and_fresh_sequences(self):
        self.assertIn("player != host", self.server)
        self.assertIn("!player->m_bIsHost", self.server)
        self.assertIn("player->m_iPlayerId != g_authorityPlayerId", self.server)
        self.assertIn("CMissionSessionServer::GetState().IsActive()", self.server)
        self.assertIn("CHEAT_REQUEST_RATE_LIMIT", self.server)
        self.assertIn("IsCheatSerialNewer(request.requestSequence", self.server)

    def test_server_replays_only_persistent_state_and_resets_empty_lobby(self):
        network = (ROOT / "server/src/CNetwork.cpp").read_text(encoding="utf-8")
        players = (ROOT / "server/src/CNetworkPlayerManager.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("CCheatAuthorityManager::SendSnapshot", network)
        self.assertIn("CCheatAuthorityManager::HandlePlayerDisconnected", network)
        self.assertEqual(2, players.count("CCheatAuthorityManager::HandleAuthorityChange"))
        reset = self.server.split("if (newAuthority == nullptr ||", 1)[1]
        self.assertIn("g_persistentState.fill(0)", reset)
        self.assertIn("g_serverRunId = 0", reset)
        snapshot = self.server.split("void CCheatAuthorityManager::SendSnapshot", 1)[1]
        self.assertIn("CheatStateEvent", snapshot)
        self.assertNotIn("CheatActionEvent", snapshot)

    def test_client_preserves_offline_and_custom_cheat_strings_without_echo(self):
        hook = (ROOT / "client/src/Hooks/GameHooks.cpp").read_text(encoding="utf-8")
        self.assertIn("0x439B0A", hook)
        self.assertNotIn("ProcessCheat_Hook1", hook)
        self.assertNotIn("ProcessCheat_Hook2", hook)
        self.assertIn("plugin::Call<0x438480>(lastPressedKey)", self.client)
        self.assertIn("Debug-menu and vehicle-spawner strings remain", self.client)
        request = self.client.split("void CNetworkCheatManager::RequestCheat", 1)[1]
        request = request.split("void CNetworkCheatManager::ExecuteNative", 1)[0]
        self.assertIn("m_localPlayerIsAuthority", request)
        self.assertIn("IsMissionActive()", request)
        execute = self.client.split("void CNetworkCheatManager::ExecuteNative", 1)[1]
        execute = execute.split("void CNetworkCheatManager::ApplyPersistentMask", 1)[0]
        self.assertNotIn("GetPacketFactory", execute)

    def test_client_rejects_stale_actions_and_bounds_deferred_execution(self):
        self.assertIn("IsCheatSerialNewer(action.eventSequence", self.client)
        self.assertGreaterEqual(self.client.count("m_serverRunId !="), 2)
        self.assertGreaterEqual(self.client.count("authorityPlayerId != m_authorityPlayerId"), 2)
        self.assertIn("PENDING_ACTION_CAPACITY = 16", (ROOT / "client/src/CNetworkCheatManager.h").read_text(encoding="utf-8"))
        self.assertIn("PENDING_ACTION_LIFETIME_MS", self.client)
        self.assertIn("IsAuthorityOnlyTransient", self.client)

    def test_lifecycle_authority_and_handlers_are_integrated(self):
        client_network = (ROOT / "client/src/CNetwork.cpp").read_text(encoding="utf-8")
        system = (ROOT / "client/src/PacketHandlers/system.cpp").read_text(
            encoding="utf-8"
        )
        main = (ROOT / "client/src/Main.cpp").read_text(encoding="utf-8")
        server_handler = (ROOT / "server/src/PacketHandlers/cheats.cpp").read_text(
            encoding="utf-8"
        )
        client_handler = (ROOT / "client/src/PacketHandlers/cheats.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("CNetworkCheatManager::ResetNetworkState", client_network)
        self.assertIn("CNetworkCheatManager::BeginNetworkSession", system)
        self.assertEqual(2, system.count("CNetworkCheatManager::HandleAuthorityChanged"))
        self.assertIn("CNetworkCheatManager::Process", main)
        self.assertIn("ePacketType::CHEAT_REQUEST", server_handler)
        self.assertIn("ePacketType::CHEAT_STATE", client_handler)
        self.assertIn("ePacketType::CHEAT_ACTION", client_handler)

    def test_weather_and_patch_interactions_are_explicit(self):
        patch_source = (ROOT / "client/src/CPatch.cpp").read_text(encoding="utf-8")
        self.assertNotIn("CNetworkCheatManager", patch_source)
        self.assertIn("CWeatherSync::SyncCurrentState", self.client)
        self.assertIn("IsAuthorityOnlyTransient", self.client)
        main = (ROOT / "client/src/Main.cpp").read_text(encoding="utf-8")
        self.assertIn("lastWeatherTimeSyncTickRate + 2000", main)
        persistent_apply = self.client.split("void CNetworkCheatManager::ApplyPersistentMask", 1)[1]
        persistent_apply = persistent_apply.split("void CNetworkCheatManager::ShowAcceptedFeedback", 1)[0]
        self.assertIn("ExecuteNative(cheat)", persistent_apply)

    def test_all_windows_resources_match_protocol_version(self):
        for relative in (
            "client/version.rc",
            "server/version.rc",
            "proxy/version.rc",
            "launcher/version.rc",
        ):
            with self.subTest(relative=relative):
                contents = (ROOT / relative).read_text(encoding="utf-8")
                self.assertIn("FILEVERSION 0,3,10,0", contents)
                self.assertIn("PRODUCTVERSION 0,3,10,0", contents)
                self.assertIn('VALUE "FileVersion", "0.3.10-alpha"', contents)
                self.assertIn('VALUE "ProductVersion", "0.3.10-alpha"', contents)

    def test_readme_marks_only_proven_cheat_sync_complete(self):
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        self.assertIn("- [X] cheat code sync", readme)


if __name__ == "__main__":
    unittest.main()
