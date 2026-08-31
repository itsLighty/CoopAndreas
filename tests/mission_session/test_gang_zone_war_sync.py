import pathlib
import math
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class MeasureStreamModel:
    """Mirrors third_party/serialize.h MeasureStream, including its conservative 7-bit byte alignment."""

    EVENT_HEADER_BYTES = 6

    def __init__(self):
        self.bits = 0

    def integer(self, minimum: int, maximum: int):
        self.bits += (maximum - minimum).bit_length()

    def raw_bits(self, count: int):
        self.bits += count

    def byte_array(self, count: int):
        self.bits += 7 + count * 8

    def compressed_float(self, minimum: float, maximum: float, resolution: float):
        max_integer = math.ceil((maximum - minimum) / resolution)
        self.integer(0, max_integer)

    def world_position(self):
        self.raw_bits(1)
        self.compressed_float(-3000.0, 3000.0, 0.001)
        self.compressed_float(-3000.0, 3000.0, 0.001)
        self.compressed_float(-120.0, 1000.0, 0.001)

    @property
    def wire_bytes(self):
        return self.EVENT_HEADER_BYTES + (self.bits + 7) // 8


def measure_max_gang_zone_wire_bytes():
    stream = MeasureStreamModel()
    stream.raw_bits(32)
    stream.integer(0, 7)
    stream.integer(1, 380)
    stream.byte_array(380 * 10)
    return stream.wire_bytes


def measure_max_gang_war_wire_bytes():
    stream = MeasureStreamModel()
    stream.raw_bits(32)
    stream.integer(0, 7)
    stream.integer(0, 6)
    stream.integer(0, 2)
    stream.integer(-1, 9)
    stream.integer(-1, 9)
    stream.integer(0, 5)
    stream.integer(-1, 379)
    stream.integer(-1, 379)
    stream.integer(-1, 379)
    stream.raw_bits(5)
    stream.integer(0, 6)
    for _ in range(6):
        stream.integer(0, 379)
    for _ in range(3):
        stream.integer(0, 2)
        stream.integer(0, 380)
    stream.integer(0, 2_000_000)
    stream.raw_bits(32 * 2)
    stream.raw_bits(32)
    stream.compressed_float(0.0, 255.0, 0.01)
    stream.compressed_float(0.0, 1.0, 0.001)
    stream.compressed_float(0.0, 1.0, 0.001)
    stream.world_position()
    stream.world_position()
    return stream.wire_bytes


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start + len(signature))
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : index]
    raise AssertionError(f"unterminated function: {signature}")


class ServerCacheModel:
    """Small executable acceptance model for the C++ authority/revision contract."""

    def __init__(self, host_id: int):
        self.host_id = host_id
        self.zone_revision = None
        self.war_revision = None
        self.zone_snapshot = None
        self.war_snapshot = None

    @staticmethod
    def newer(candidate: int, reference: int) -> bool:
        distance = (candidate - reference) & 0xFFFFFFFF
        return distance != 0 and distance < 0x80000000

    def accept(self, stream: str, sender: int, claimed_host: int, revision: int, valid: bool = True):
        if sender != self.host_id or claimed_host != sender or not valid or revision == 0:
            return False
        previous = getattr(self, f"{stream}_revision")
        if previous is not None and not self.newer(revision, previous):
            return False
        setattr(self, f"{stream}_revision", revision)
        setattr(self, f"{stream}_snapshot", (sender, revision))
        return True

    def migrate(self, host_id: int):
        self.host_id = host_id
        self.zone_revision = None
        self.war_revision = None
        self.zone_snapshot = None
        self.war_snapshot = None

    def reconnect_snapshot(self):
        return self.zone_snapshot, self.war_snapshot


class GangZoneWarSyncTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.packet_types = (ROOT / "shared/network/packet_types.h").read_text(encoding="utf-8")
        cls.world_packets = (ROOT / "shared/network/packets/world.h").read_text(encoding="utf-8")
        cls.client = (ROOT / "client/src/CGangZoneWarSyncManager.cpp").read_text(encoding="utf-8")
        cls.client_header = (ROOT / "client/src/CGangZoneWarSyncManager.h").read_text(encoding="utf-8")
        cls.server = (ROOT / "server/src/CGangZoneWarAuthorityManager.cpp").read_text(encoding="utf-8")
        cls.server_header = (ROOT / "server/src/CGangZoneWarAuthorityManager.h").read_text(encoding="utf-8")
        cls.client_world = (ROOT / "client/src/PacketHandlers/world.cpp").read_text(encoding="utf-8")
        cls.server_world = (ROOT / "server/src/PacketHandlers/world.cpp").read_text(encoding="utf-8")
        cls.game_hooks = (ROOT / "client/src/Hooks/GameHooks.cpp").read_text(encoding="utf-8")
        cls.client_system = (ROOT / "client/src/PacketHandlers/system.cpp").read_text(encoding="utf-8")
        cls.client_network = (ROOT / "client/src/CNetwork.cpp").read_text(encoding="utf-8")
        cls.server_network = (ROOT / "server/src/CNetwork.cpp").read_text(encoding="utf-8")
        cls.server_players = (ROOT / "server/src/CNetworkPlayerManager.cpp").read_text(encoding="utf-8")

    def test_actual_measure_stream_sizes_fit_declared_packet_budgets(self):
        self.assertIn("MAX_GANG_ZONE_INFOS = 380", self.world_packets)
        self.assertIn("MAX_GANG_NAVIGATION_ZONES = 380", self.world_packets)
        self.assertIn("GANG_DENSITY_COUNT = 10", self.world_packets)
        self.assertIn("GANG_ZONE_DENSITY_BYTES = MAX_GANG_ZONE_INFOS * GANG_DENSITY_COUNT", self.world_packets)
        zone_packet = re.search(r"class GangZoneState\b.*?^};", self.world_packets, re.S | re.M).group(0)
        war_packet = re.search(r"class GangWarState\b.*?^};", self.world_packets, re.S | re.M).group(0)
        self.assertIn("zoneInfoCount > 0", zone_packet)
        self.assertIn("zoneInfoCount <= MAX_GANG_ZONE_INFOS", zone_packet)
        self.assertIn("zoneIndex < zoneInfoCount", zone_packet)
        self.assertIn("serialize_bytes(stream, gangDensities[zoneIndex].data(), GANG_DENSITY_COUNT)", zone_packet)
        for packet in (zone_packet, war_packet):
            self.assertIn("serialize::MeasureStream stream", packet)
            self.assertIn("stream.GetBytesProcessed()", packet)
            self.assertIn("FitsSerializedBudget()", packet)
        zone_budget = int(re.search(r"MAX_SERIALIZED_BYTES = (\d+)", zone_packet).group(1))
        war_budget = int(re.search(r"MAX_SERIALIZED_BYTES = (\d+)", war_packet).group(1))
        self.assertEqual(measure_max_gang_zone_wire_bytes(), 3813)
        self.assertEqual(measure_max_gang_war_wire_bytes(), 64)
        self.assertLessEqual(measure_max_gang_zone_wire_bytes(), zone_budget)
        self.assertLessEqual(measure_max_gang_war_wire_bytes(), war_budget)
        self.assertIn("MAX_SERIALIZED_BYTES <= 10 * 1024", self.world_packets)

    def test_semantic_bounds_match_audited_game_fields(self):
        for evidence in (
            "MAX_GANG_DENSITY = UINT8_MAX",
            "warFerocity < 0 || warFerocity > 5",
            "MAX_FIGHT_TIMER_MS = 2000000",
            "MAX_REPLICATED_ELAPSED_MS = 86400000",
            "MAX_TIME_TILL_NEXT_ATTACK_MS = 1620000.0f",
            "difficulty < 0.0f || difficulty > 1.0f",
            "territoryControl < 0.0f || territoryControl > 1.0f",
            "gangRatingStrength[index] > MAX_GANG_NAVIGATION_ZONES",
        ):
            self.assertIn(evidence, self.world_packets)
        self.assertIn("complete byte is data", self.world_packets)
        self.assertIn("GangRatings stores each of the three turf gangs' rank", self.world_packets)
        self.assertIn("groveZones / allGangZones", self.client)
        self.assertNotIn("MAX_GANG_DENSITY = 100", self.world_packets)

    def test_protocol_names_cardinality_and_version_are_exact(self):
        for packet_name in ("GANG_ZONE_STATE", "GANG_WAR_STATE"):
            self.assertIn(packet_name, self.packet_types)
            self.assertIn(f'"{packet_name}"', self.packet_types)
        enum_body = re.search(
            r"enum class ePacketType[^\{]*\{(.*?)PACKET_ID_MAX", self.packet_types, re.S
        ).group(1)
        enum_names = re.findall(r"^\s*([A-Z][A-Z0-9_]+)\s*,", enum_body, re.M)
        debug_array = re.search(r"static constexpr const char\* array\[\]\s*=\s*\{(.*?)\};", self.packet_types, re.S).group(1)
        debug_names = re.findall(r'"([A-Z][A-Z0-9_]+)"', debug_array)
        self.assertEqual(enum_names, debug_names)
        config = (ROOT / "shared/config.h").read_text(encoding="utf-8")
        self.assertIn('COOPANDREAS_VERSION "0.3.10-alpha"', config)

    def test_all_wire_enums_counts_indices_and_floats_are_validated(self):
        war_packet = re.search(r"class GangWarState\b.*?^};", self.world_packets, re.S | re.M).group(0)
        for evidence in (
            "lifecycleValue > static_cast<int>(eGangWarLifecycle::THIRD_WAVE)",
            "attackValue > static_cast<int>(eGangAttackLifecycle::PLAYER_CAME_TO_WAR)",
            "specificZoneCount > MAX_SPECIFIC_GANG_ZONES",
            "fightZoneInfoIndex >= MAX_GANG_ZONE_INFOS",
            "fightNavigationZoneIndex >= MAX_GANG_NAVIGATION_ZONES",
            "trainingZoneInfoIndex >= MAX_GANG_ZONE_INFOS",
            "specificNavigationZoneIndices[index] >= MAX_GANG_NAVIGATION_ZONES",
            "fightTimerRemainingMs > MAX_FIGHT_TIMER_MS",
            "waveElapsedMs > MAX_REPLICATED_ELAPSED_MS",
            "timeOutsideFightAreaMs > MAX_REPLICATED_ELAPSED_MS",
            "std::isfinite(timeTillNextAttackMs)",
            "std::isfinite(provocation)",
            "HasFiniteWorldPositions()",
            "lifecycleNeedsFightZone",
            "inactiveTimersAreCanonical",
        ):
            self.assertIn(evidence, war_packet)
        self.assertIn("return !Stream::IsReading || HasValidState()", war_packet)

    def test_audited_layout_avoids_bundled_plugin_sdk_zone_misdefinition(self):
        for evidence in (
            "ZONE_INFO_COUNT_ADDRESS = 0xBA1DE8",
            "ZONE_INFO_ARRAY_ADDRESS = 0xBA1DF0",
            "NAVIGATION_ZONE_COUNT_ADDRESS = 0xBA3794",
            "NAVIGATION_ZONE_ARRAY_ADDRESS = 0xBA3798",
            "GANG_WAR_RADAR_BLIP_ADDRESS = 0x96AB98",
            "NAVIGATION_ZONE_STRIDE = 0x20",
            'sizeof(CZoneInfo) == 0x11',
            "offset % stride != 0",
        ):
            self.assertIn(evidence, self.client)
        self.assertNotIn("CTheZones::ZoneInfoArray", self.client)
        self.assertNotRegex(self.client, r"pZoneToFightOver\s*-\s*&")

    def test_offline_update_is_exact_and_only_authenticated_host_advances_online(self):
        process = function_body(self.client, "void CGangZoneWarSyncManager::ProcessGangWars()")
        self.assertIn("if (!CNetwork::m_bAuthenticated)", process)
        self.assertIn("ResetNetworkState();", process)
        self.assertIn("if (CLocalPlayer::m_bIsHost)", process)
        self.assertEqual(process.count("CGangWars::Update();"), 2)
        self.assertIn("PublishAuthoritativeState();", process)
        self.assertIn("ApplyAuthoritativeState(ApplyPendingState());", process)
        self.assertIn("patch::RedirectCall(0x53C122, CGangWars__Update_Hook)", self.game_hooks)
        hook = function_body(self.game_hooks, "static void __cdecl CGangWars__Update_Hook()")
        self.assertIn("CGangZoneWarSyncManager::ProcessGangWars();", hook)
        end_hook = function_body(self.game_hooks, "static void __cdecl CGangWars__EndGangWar_Hook(")
        self.assertIn("CNetwork::m_bAuthenticated && !CLocalPlayer::m_bIsHost", end_hook)
        self.assertIn("CGangWars::EndGangWar(bEnd);", end_hook)
        for address in ("0x4465D4", "0x446631", "0x56E5C3", "0x56E5FE", "0x618F87"):
            self.assertIn(address, self.game_hooks)

    def test_server_rejects_non_host_spoofed_invalid_and_stale_state(self):
        for signature in (
            "bool CGangZoneWarAuthorityManager::HandleZoneState(",
            "bool CGangZoneWarAuthorityManager::HandleWarState(",
        ):
            body = function_body(self.server, signature)
            self.assertIn("IsCurrentHost(pNetworkPlayer)", body)
            self.assertIn("state.HasValidState()", body)
            self.assertIn("state.FitsSerializedBudget()", body)
            self.assertIn("state.authorityPlayerId != pNetworkPlayer->m_iPlayerId", body)
            self.assertIn("IsGangWorldRevisionNewer", body)
            self.assertIn("SendToAll", body)
        current_host = function_body(self.server, "bool CGangZoneWarAuthorityManager::IsCurrentHost(")
        self.assertIn("pNetworkPlayer->m_bIsHost", current_host)
        self.assertIn("CNetworkPlayerManager::GetHost() == pNetworkPlayer", current_host)

        model = ServerCacheModel(host_id=2)
        self.assertFalse(model.accept("zone", sender=1, claimed_host=1, revision=1))
        self.assertFalse(model.accept("zone", sender=2, claimed_host=7, revision=1))
        self.assertFalse(model.accept("zone", sender=2, claimed_host=2, revision=1, valid=False))
        self.assertTrue(model.accept("zone", sender=2, claimed_host=2, revision=1))
        self.assertFalse(model.accept("zone", sender=2, claimed_host=2, revision=1))
        self.assertFalse(model.accept("zone", sender=2, claimed_host=2, revision=0))

    def test_reconnect_snapshot_and_host_migration_reset_both_streams(self):
        snapshot = function_body(self.server, "void CGangZoneWarAuthorityManager::SendSnapshot(")
        self.assertIn("GetPacketFactory().Send(m_ZoneState, pNetworkPlayer)", snapshot)
        self.assertIn("GetPacketFactory().Send(m_WarState, pNetworkPlayer)", snapshot)
        self.assertIn("CGangZoneWarAuthorityManager::SendSnapshot(pNewNetworkPlayer)", self.server_network)
        reset = function_body(self.server, "void CGangZoneWarAuthorityManager::ResetForAuthorityChange()")
        self.assertIn("m_bHasZoneState = false", reset)
        self.assertIn("m_bHasWarState = false", reset)
        assign = function_body(self.server_players, "void CNetworkPlayerManager::AssignHostToFirstPlayer()")
        self.assertGreaterEqual(assign.count("CGangZoneWarAuthorityManager::ResetForAuthorityChange();"), 2)
        self.assertIn("HandleAuthorityChanged(pPlayerAssignHost->playerid, true)", self.client_system)
        self.assertIn("HandleAuthorityChanged(pPlayerAssignHost->playerid, false)", self.client_system)

        model = ServerCacheModel(host_id=0)
        self.assertTrue(model.accept("zone", 0, 0, 15))
        self.assertTrue(model.accept("war", 0, 0, 40))
        self.assertEqual(model.reconnect_snapshot(), ((0, 15), (0, 40)))
        model.migrate(3)
        self.assertEqual(model.reconnect_snapshot(), (None, None))
        self.assertFalse(model.accept("war", 0, 0, 41))
        self.assertTrue(model.accept("war", 3, 3, 1))

    def test_peer_application_is_observational_and_never_replays_war_side_effects(self):
        apply = function_body(self.client, "void CGangZoneWarSyncManager::ApplyAuthoritativeState(")
        for assignment in (
            "CGangWars::State =",
            "CGangWars::State2 =",
            "CGangWars::FightTimer =",
            "CGangWars::pZoneInfoToFightOver =",
            "CGangWars::pZoneToFightOver =",
            "CGangWars::TerritoryUnderControlPercentage =",
        ):
            self.assertIn(assignment, apply)
        for forbidden_call in (
            "CGangWars::StartOffensiveGangWar(",
            "CGangWars::StartDefensiveGangWar(",
            "CGangWars::EndGangWar(",
            "CGangWars::CreateAttackWave(",
            "CGangWars::ReleasePedsInAttackWave(",
            "CGangWars::ReleaseCarsInAttackWave(",
        ):
            self.assertNotIn(forbidden_call, apply)
        self.assertIn("CTheZones::FillZonesWithGangColours(!state.gangWarsActive)", apply)
        self.assertIn("CTheZones::FillZonesWithGangColours(!CGangWars::bGangWarsActive)", apply)
        self.assertIn("RADAR_MODE_MASK", apply)
        self.assertNotRegex(apply, r"CGangWars::RadarBlip\s*=")
        self.assertIn("UpdateReplicatedRadarBlip(state, displayedFightTimerMs)", apply)

        radar = function_body(self.client, "void CGangZoneWarSyncManager::UpdateReplicatedRadarBlip(")
        self.assertIn("state.attackLifecycle != eGangAttackLifecycle::WAR_NOTIFIED", radar)
        self.assertIn("CRadar::SetCoordBlip", radar)
        self.assertIn("GetStockGangBlipSprite", radar)
        self.assertIn("CRadar::SetBlipSprite", radar)
        self.assertIn("CRadar::ChangeBlipDisplay", radar)
        self.assertIn("displayedFightTimerMs > 120000", radar)
        clear = function_body(self.client, "void CGangZoneWarSyncManager::ClearReplicatedRadarBlip()")
        self.assertIn("CRadar::ClearBlip(m_nReplicatedRadarBlip)", clear)
        self.assertIn("IsMatchingDefensiveWarBlip", clear)
        self.assertIn("CRadar::ClearActualBlip(index)", clear)
        authority = function_body(self.client, "void CGangZoneWarSyncManager::HandleAuthorityChanged(")
        self.assertIn("ClearReplicatedRadarBlip();", authority)
        self.assertIn("GetNativeGangWarRadarBlipHandle() = m_nReplicatedRadarBlip", authority)
        self.assertIn("CRadar::ClearBlip(nativeBlip)", authority)

    def test_pending_state_waits_for_matching_runtime_counts_and_reapplies_authority(self):
        pending = function_body(self.client, "bool CGangZoneWarSyncManager::ApplyPendingState()")
        self.assertIn("GetZoneInfoCount() == m_PendingZoneState.zoneInfoCount", pending)
        self.assertIn("CanApplyWarState(m_PendingWarState)", pending)
        self.assertIn("m_nWarStateAppliedAt = CTimer::m_snTimeInMilliseconds", pending)
        apply = function_body(self.client, "void CGangZoneWarSyncManager::ApplyAuthoritativeState(")
        self.assertIn("std::memcmp", apply)
        self.assertIn("std::memcpy", apply)
        self.assertIn("sinceSnapshot", apply)
        self.assertIn("state.fightTimerRemainingMs > sinceSnapshot", apply)

    def test_handlers_reset_wiring_and_compile_hygiene_are_present(self):
        for packet_name in ("GANG_ZONE_STATE", "GANG_WAR_STATE"):
            self.assertIn(f"ePacketType::{packet_name}", self.client_world)
            self.assertIn(f"ePacketType::{packet_name}", self.server_world)
        self.assertIn('#include "network/packets/world.h"', self.client_header)
        self.assertIn('#include "network/packets/world.h"', self.server_header)
        self.assertIn("CGangZoneWarSyncManager::ResetNetworkState();", self.client_network)
        self.assertIn("static_assert(sizeof(CZoneInfo) == 0x11", self.client)
        xmake = (ROOT / "xmake.lua").read_text(encoding="utf-8")
        self.assertIn('add_files("client/src/*.cpp")', xmake)
        self.assertIn('add_files("server/src/**.cpp")', xmake)


if __name__ == "__main__":
    unittest.main()
