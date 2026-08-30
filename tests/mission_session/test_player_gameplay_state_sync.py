import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


def handler_body(source: str, packet_type: str) -> str:
    signature = re.search(rf"PACKET_HANDLER\(ePacketType::{packet_type}\s*,", source)
    if signature is None:
        raise AssertionError(f"missing handler for {packet_type}")

    opening_brace = source.find("{", signature.end())
    depth = 0
    for index in range(opening_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening_brace + 1 : index]
    raise AssertionError(f"unterminated handler for {packet_type}")


class PlayerGameplayStateSyncTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.packet_types = (ROOT / "shared/network/packet_types.h").read_text(encoding="utf-8")
        cls.packets = (ROOT / "shared/network/packets/players.h").read_text(encoding="utf-8")
        cls.sender_id = (ROOT / "shared/network/serializable_types.h").read_text(encoding="utf-8")
        cls.server_handler = (ROOT / "server/src/PacketHandlers/players.cpp").read_text(encoding="utf-8")
        cls.server_player = (ROOT / "server/src/CNetworkPlayer.h").read_text(encoding="utf-8")
        cls.server_network = (ROOT / "server/src/CNetwork.cpp").read_text(encoding="utf-8")
        cls.client_handler = (ROOT / "client/src/PacketHandlers/players.cpp").read_text(encoding="utf-8")
        cls.client_player = (ROOT / "client/src/CNetworkPlayer.cpp").read_text(encoding="utf-8")
        cls.client_player_manager = (ROOT / "client/src/CNetworkPlayerManager.cpp").read_text(encoding="utf-8")
        cls.gameplay_sync = (ROOT / "client/src/CPlayerGameplayStateSync.cpp").read_text(encoding="utf-8")
        cls.stats_header = (ROOT / "client/src/CStatsSync.h").read_text(encoding="utf-8")
        cls.stats_sync = (ROOT / "client/src/CStatsSync.cpp").read_text(encoding="utf-8")
        cls.client_network = (ROOT / "client/src/CNetwork.cpp").read_text(encoding="utf-8")

    def test_wire_schema_is_bounded_registered_and_version_gated(self):
        gameplay_state_index = self.packet_types.index("PLAYER_GAMEPLAY_STATE")
        packet_id_max_index = self.packet_types.index("PACKET_ID_MAX", gameplay_state_index)
        self.assertLess(gameplay_state_index, packet_id_max_index)
        self.assertIn('"PLAYER_GAMEPLAY_STATE"', self.packet_types)
        self.assertIn(
            "DEFINE_PACKET_TYPE(PlayerGameplayState, ePacketType::PLAYER_GAMEPLAY_STATE, ePacketChannel::EVENT)",
            self.packets,
        )
        gameplay_packet = re.search(
            r"class PlayerGameplayState\b.*?^};", self.packets, re.S | re.M
        ).group(0)
        self.assertIn("serialize_uint8(stream, wantedLevel)", gameplay_packet)
        self.assertIn("wantedLevel > MAX_WANTED_LEVEL", gameplay_packet)
        self.assertNotIn("serialize_int(stream, wantedLevel", gameplay_packet)
        self.assertIn("serialize_int(stream, money, MIN_MONEY, MAX_MONEY)", gameplay_packet)
        self.assertIn("serialize_compressed_float(stream, breath", gameplay_packet)
        self.assertIn("serialize_compressed_float(stream, maximumHealth", gameplay_packet)
        self.assertIn('COOPANDREAS_VERSION "0.3.3-alpha"',
            (ROOT / "shared/config.h").read_text(encoding="utf-8"))

        client_pch = (ROOT / "client/src/stdafx.h").read_text(encoding="utf-8")
        server_pch = (ROOT / "server/src/stdafx.h").read_text(encoding="utf-8")
        self.assertIn('"network/packets/players.h"', client_pch)
        self.assertIn('"CNetworkPlayer.h"', server_pch)
        self.assertIn("<network/packets/players.h>", self.server_player)

    def test_legacy_stats_wire_shape_has_exactly_eleven_canonical_entries(self):
        self.assertIn("PLAYER_SKILL_STATS_COUNT = 11", self.packets)
        self.assertIn("PLAYER_STATS_WIRE_COUNT = 14", self.packets)
        self.assertIn("float stats[PLAYER_STATS_WIRE_COUNT]", self.packets)
        self.assertIn(
            "SYNCED_STATS_COUNT = Packets::Players::PLAYER_SKILL_STATS_COUNT",
            self.stats_header,
        )
        initializer = re.search(
            r"CStatsSync::m_aeSyncedStats\s*=\s*\{(.*?)\};", self.stats_sync, re.S
        ).group(1)
        self.assertEqual(len(re.findall(r"STAT_[A-Z0-9_]+", initializer)), 11)

        stats_handler = handler_body(self.server_handler, "PLAYER_STATS")
        self.assertIn("ARRAY_SIZE(pPlayerStats->stats)", stats_handler)
        self.assertIn("std::isfinite", stats_handler)
        self.assertIn("ARRAY_SIZE(pNetworkPlayer->m_afStats)", stats_handler)
        self.assertIn("pPlayerStats->playerid = pNetworkPlayer->m_iPlayerId", stats_handler)

    def test_sender_identity_is_server_owned_and_state_is_canonical_cached(self):
        self.assertIn("if (Stream::IsWriting && Config::IsClient)", self.sender_id)
        self.assertIn("if (Stream::IsReading && Config::IsServer)", self.sender_id)

        body = handler_body(self.server_handler, "PLAYER_GAMEPLAY_STATE")
        identity = body.index("pGameplayState->playerid.value = pNetworkPlayer->m_iPlayerId")
        cache = body.index("pNetworkPlayer->m_gameplayState = *pGameplayState")
        relay = body.index("RelayPlayerGameplayState(*pGameplayState, pNetworkPlayer)")
        self.assertLess(identity, cache)
        self.assertLess(cache, relay)
        self.assertIn("bGameplayStateModified = true", body)
        self.assertIn("std::isfinite", body)
        for bound in ("wantedLevel", "money", "breath", "maximumHealth"):
            self.assertIn(f"pGameplayState->{bound} = std::clamp", body)

        relay_helper = re.search(
            r"void RelayPlayerGameplayState\(.*?^}", self.server_handler, re.S | re.M
        ).group(0)
        self.assertIn("pRecipient != pSourcePlayer", relay_helper)
        self.assertIn("CMissionSessionServer::IsGameplayParticipant(pRecipient)", relay_helper)
        self.assertIn("GetPacketFactory().Send(packet, pRecipient)", relay_helper)

    def test_server_rejects_spectators_for_stats_and_gameplay_state(self):
        for packet_type in ("PLAYER_STATS", "PLAYER_GAMEPLAY_STATE"):
            body = handler_body(self.server_handler, packet_type)
            self.assertIn("CMissionSessionServer::GetState().IsActive()", body)
            self.assertIn("!CMissionSessionServer::IsGameplayParticipant(pNetworkPlayer)", body)
            self.assertRegex(
                body,
                r"(?s)!CMissionSessionServer::IsGameplayParticipant\(pNetworkPlayer\)\)\s*"
                r"\{.*?return;\s*\}",
            )

    def test_late_join_uses_cached_authoritative_state_and_frozen_roster_gates(self):
        self.assertIn("Packets::Players::PlayerGameplayState m_gameplayState", self.server_player)
        self.assertIn("bGameplayStateModified", self.server_player)
        for evidence in (
            "missionState.ContainsGameplayParticipant(freeId)",
            "missionState.ContainsGameplayParticipant(i->m_iPlayerId)",
            "i->m_ucSyncFlags.bGameplayStateModified",
            "Packets::Players::PlayerGameplayState gameplayState = i->m_gameplayState",
            "GetPacketFactory().Send(gameplayState, pNewNetworkPlayer)",
        ):
            self.assertIn(evidence, self.server_network)

    def test_client_rejects_wrong_target_and_spectator_mission_state(self):
        helper = re.search(
            r"bool ShouldIgnoreRemoteGameplayState\(.*?^}", self.client_handler, re.S | re.M
        ).group(0)
        self.assertIn("playerId.value == CNetworkPlayerManager::m_nMyId", helper)
        self.assertIn("missionState.IsActive()", helper)
        self.assertIn("CMissionSessionClient::IsSpectator()", helper)
        self.assertIn("!missionState.ContainsGameplayParticipant(playerId.value)", helper)
        for packet_type in ("PLAYER_STATS", "PLAYER_GAMEPLAY_STATE"):
            self.assertIn("ShouldIgnoreRemoteGameplayState", handler_body(self.client_handler, packet_type))
        self.assertIn(
            "CNetworkPlayerManager::ApplyOrQueueGameplayState(*pGameplayState)",
            handler_body(self.client_handler, "PLAYER_GAMEPLAY_STATE"),
        )

        for evidence in (
            "m_pendingStats[playerId] = stats",
            "m_pendingGameplayStates[playerId] = gameplayState",
            "ApplyOrQueueStats(m_pendingStats[playerId])",
            "player->ApplyGameplayState(m_pendingGameplayStates[playerId])",
            "m_hasPendingStats.fill(false)",
            "m_hasPendingGameplayState.fill(false)",
        ):
            self.assertIn(evidence, self.client_player_manager)

    def test_client_applies_all_fields_only_to_matching_remote_player(self):
        apply_body = re.search(
            r"void CNetworkPlayer::ApplyGameplayState\(.*?^}", self.client_player, re.S | re.M
        ).group(0)
        self.assertIn("gameplayState.playerid.value != m_iPlayerId", apply_body)
        self.assertIn("m_pPed->m_fMaxHealth = gameplayState.maximumHealth", apply_body)
        self.assertIn("m_pPlayerData->m_fBreath = gameplayState.breath", apply_body)
        self.assertIn("m_nWantedLevel = gameplayState.wantedLevel", apply_body)
        self.assertIn("m_nMoney = gameplayState.money", apply_body)
        self.assertIn("m_nDisplayMoney = gameplayState.money", apply_body)

    def test_disconnect_and_spectator_transitions_force_initial_resend(self):
        reset = re.search(
            r"void CNetwork::ResetConnectionState\(\).*?^}", self.client_network, re.S | re.M
        ).group(0)
        self.assertIn("CStatsSync::ResetNetworkState()", reset)
        self.assertIn("CPlayerGameplayStateSync::ResetNetworkState()", reset)

        self.assertIn("!CNetwork::m_bAuthenticated", self.gameplay_sync)
        self.assertIn("CMissionSessionClient::IsSpectator()", self.gameplay_sync)
        self.assertIn("m_bHasLastSentState = false", self.gameplay_sync)
        self.assertIn("GetPacketFactory().Send(currentState)", self.gameplay_sync)
        self.assertIn("m_bSentInitialStats = false", self.stats_sync)
        self.assertIn("GetPacketFactory().Send(packet)", self.stats_sync)


if __name__ == "__main__":
    unittest.main()
