import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


def handler_body(source: str, packet_type: str) -> str:
    signature = re.search(
        rf"PACKET_HANDLER\(ePacketType::{packet_type}\s*,", source
    )
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


class BlipMissionGuardTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = (ROOT / "shared/network/packets/blips.h").read_text(
            encoding="utf-8"
        )
        cls.server = (ROOT / "server/src/PacketHandlers/blips.cpp").read_text(
            encoding="utf-8"
        )
        cls.client = (ROOT / "client/src/PacketHandlers/blips.cpp").read_text(
            encoding="utf-8"
        )

    def test_every_blip_packet_has_server_and_client_handlers(self):
        packet_types = re.findall(r"DEFINE_PACKET_TYPE\([^,]+,\s*ePacketType::(\w+)", self.header)
        self.assertEqual(
            packet_types,
            [
                "UPDATE_ENTITY_BLIP",
                "REMOVE_ENTITY_BLIP",
                "CLEAR_ENTITY_BLIPS",
                "UPDATE_CHECKPOINT",
                "REMOVE_CHECKPOINT",
                "CREATE_STATIC_BLIP",
            ],
        )
        for packet_type in packet_types:
            handler_body(self.server, packet_type)
            handler_body(self.client, packet_type)

    def test_server_requires_authoritative_host_during_missions(self):
        self.assertRegex(
            self.server,
            r"if \(CMissionSessionServer::GetState\(\)\.IsActive\(\)\)\s*\{\s*"
            r"return CMissionSessionServer::IsAuthoritativeHost\(pNetworkPlayer\);",
        )
        self.assertIn(
            "pNetworkPlayer != nullptr && pNetworkPlayer->m_bIsHost", self.server
        )
        self.assertIn("CMissionSessionServer::IsGameplayParticipant", self.server)

    def test_all_targeted_packets_use_frozen_roster_forwarder(self):
        expected_calls = {
            "UPDATE_ENTITY_BLIP": "ForwardTargetedMissionEffect(*pUpdateEntityBlip, pNetworkPlayer);",
            "REMOVE_ENTITY_BLIP": "ForwardTargetedMissionEffect(*pRemoveEntityBlip, pNetworkPlayer);",
            "CLEAR_ENTITY_BLIPS": "ForwardTargetedMissionEffect(*pClearEntityBlips, pNetworkPlayer);",
            "UPDATE_CHECKPOINT": "ForwardTargetedMissionEffect(*pUpdateCheckpoint, pNetworkPlayer);",
            "REMOVE_CHECKPOINT": "ForwardTargetedMissionEffect(*pRemoveCheckpoint, pNetworkPlayer);",
        }
        for packet_type, call in expected_calls.items():
            self.assertIn(call, handler_body(self.server, packet_type))

        self.assertIn("CNetworkPlayerManager::GetPlayer(packet.forWhoPlayerId)", self.server)
        self.assertIn("IsAllowedMissionTarget(pTargetPlayer)", self.server)

    def test_static_snapshot_is_limited_to_gameplay_recipients(self):
        body = handler_body(self.server, "CREATE_STATIC_BLIP")
        self.assertIn("HasMissionUiAuthority(pNetworkPlayer)", body)
        self.assertIn("SendToMissionRecipients(*pCreateStaticBlip, pNetworkPlayer);", body)
        self.assertIn("GetPacketFactory().SendToAll(packet, pNetworkPlayerToIgnore)", self.server)
        self.assertIn("CMissionSessionServer::IsGameplayParticipant(pNetworkPlayer)", self.server)

    def test_client_rejects_spectators_invalid_context_and_wrong_target(self):
        for evidence in (
            "state.HasValidParticipantRoster()",
            "CMissionSessionClient::IsSpectator()",
            "CLocalPlayer::m_bIsHost",
            "state.hostId == localPlayerId",
            "forWhoPlayerId != CNetworkPlayerManager::m_nMyId",
        ):
            self.assertIn(evidence, self.client)

        targeted = (
            "UPDATE_ENTITY_BLIP",
            "REMOVE_ENTITY_BLIP",
            "CLEAR_ENTITY_BLIPS",
            "UPDATE_CHECKPOINT",
            "REMOVE_CHECKPOINT",
        )
        for packet_type in targeted:
            self.assertIn(
                "ShouldIgnoreTargetedMissionEffect(",
                handler_body(self.client, packet_type),
            )
        self.assertIn(
            "ShouldIgnoreMissionEffect()",
            handler_body(self.client, "CREATE_STATIC_BLIP"),
        )

    def test_inactive_sessions_keep_legacy_paths(self):
        self.assertRegex(
            self.client,
            r"if \(!state\.IsActive\(\)\)\s*\{\s*return false;",
        )
        self.assertRegex(
            self.server,
            r"if \(!CMissionSessionServer::GetState\(\)\.IsActive\(\)\)\s*\{\s*"
            r"GetPacketFactory\(\)\.SendToAll\(packet, pNetworkPlayerToIgnore\);",
        )


if __name__ == "__main__":
    unittest.main()
