import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class ReconnectCredentialAndTeardownTests(unittest.TestCase):
    def test_reconnect_wire_protocol_is_separate_from_legacy_connect(self):
        packets = (ROOT / "shared/network/packets/system.h").read_text(encoding="utf-8")
        packet_types = (ROOT / "shared/network/packet_types.h").read_text(encoding="utf-8")
        config = (ROOT / "shared/config.h").read_text(encoding="utf-8")

        legacy_connect = re.search(
            r"class PlayerConnected\b.*?^};", packets, re.S | re.M
        ).group(0)
        self.assertNotIn("ReconnectCredential", legacy_connect)
        self.assertIn("RECONNECT_CREDENTIAL_SIZE = 32", packets)
        self.assertIn("class PlayerReconnectRequest", packets)
        self.assertIn("requestedPlayerId", packets)
        self.assertIn("class PlayerReconnectCredential", packets)
        self.assertIn("class PlayerReconnectCredentialAck", packets)
        self.assertIn("PLAYER_RECONNECT_REQUEST", packet_types)
        self.assertIn("PLAYER_RECONNECT_CREDENTIAL", packet_types)
        self.assertIn("PLAYER_RECONNECT_CREDENTIAL_ACK", packet_types)
        self.assertIn('COOPANDREAS_VERSION "0.3.5-alpha"', config)

    def test_server_uses_os_csprng_and_constant_time_token_validation(self):
        server = (ROOT / "server/src/CNetwork.cpp").read_text(encoding="utf-8")
        xmake = (ROOT / "xmake.lua").read_text(encoding="utf-8")

        self.assertIn("BCryptGenRandom", server)
        self.assertIn("BCRYPT_USE_SYSTEM_PREFERRED_RNG", server)
        self.assertIn("getrandom", server)
        self.assertIn('add_syslinks("bcrypt")', xmake)
        self.assertIsNotNone(
            re.search(
                r"for \(size_t i = 0; i < left\.size\(\); \+\+i\).*?difference.*?left\[i\] \^ right\[i\]",
                server,
                re.S,
            )
        )

    def test_only_valid_frozen_identity_can_choose_reserved_id(self):
        server = (ROOT / "server/src/CNetwork.cpp").read_text(encoding="utf-8")

        reclaim = re.search(
            r"bool CanReclaimFrozenIdentity\(.*?^}", server, re.S | re.M
        ).group(0)
        self.assertIn("missionState.IsActive()", reclaim)
        self.assertIn("missionState.ContainsParticipant(playerId)", reclaim)
        self.assertIn("identity.valid", reclaim)
        self.assertIn("identity.playerName", reclaim)
        self.assertIn("CredentialsEqual", reclaim)

        ordinary = re.search(r"int FindOrdinaryPlayerId\(\).*?^}", server, re.S | re.M).group(0)
        self.assertIn("!missionState.ContainsParticipant(playerId)", ordinary)
        self.assertIn(
            "reclaimedIdentity ? reconnectRequest.requestedPlayerId : FindOrdinaryPlayerId()",
            server,
        )
        self.assertIn("GenerateReconnectCredential(nextCredential)", server)
        self.assertIn("identity.credential = nextCredential", server)

    def test_rotated_token_keeps_only_delivery_grace(self):
        server = (ROOT / "server/src/CNetwork.cpp").read_text(encoding="utf-8")
        client = (ROOT / "client/src/CNetwork.cpp").read_text(encoding="utf-8")
        handler = (ROOT / "server/src/PacketHandlers/system.cpp").read_text(encoding="utf-8")

        self.assertIn("previousCredentialValid", server)
        self.assertIn("previousCredentialMatches", server)
        self.assertIn("acceptedReconnectCredential", server)
        self.assertIn("PlayerReconnectCredentialAck acknowledgement", client)
        self.assertIn("CNetwork::ConfirmReconnectCredential(pNetworkPlayer, *pAcknowledgement)", handler)
        confirmation = re.search(
            r"void CNetwork::ConfirmReconnectCredential\(.*?^}", server, re.S | re.M
        ).group(0)
        self.assertIn("CredentialsEqual(identity.credential, acknowledgement.credential)", confirmation)
        self.assertIn("previousCredentialValid = false", confirmation)
        self.assertIn("previousCredential.fill(0)", confirmation)

    def test_client_retains_only_endpoint_bound_reconnect_identity(self):
        client = (ROOT / "client/src/CNetwork.cpp").read_text(encoding="utf-8")

        self.assertIn("HasReconnectCredentialForCurrentIdentity()", client)
        self.assertIn("PlayerReconnectRequest reconnectRequest", client)
        self.assertIn("reconnectRequest.credential = ms_reconnectCredential", client)
        self.assertIn("ms_reconnectIpAddress", client)
        self.assertIn("ms_nReconnectPort", client)
        self.assertIn("ms_reconnectPlayerName", client)
        self.assertIn("ClearReconnectCredential();", client)

        reset = re.search(r"void CNetwork::ResetConnectionState\(\).*?^}", client, re.S | re.M).group(0)
        self.assertNotIn("ClearReconnectCredential", reset)

    def test_disconnect_teardown_clears_entities_and_replayable_queues(self):
        client = (ROOT / "client/src/CNetwork.cpp").read_text(encoding="utf-8")
        packet_buffer = (ROOT / "client/src/CPacketBuffer.cpp").read_text(encoding="utf-8")

        reset = re.search(r"void CNetwork::ResetConnectionState\(\).*?^}", client, re.S | re.M).group(0)
        for cleanup in (
            "CEntryExitTransitionSync::Reset()",
            "GetPacketBuffer().Clear()",
            "CNetworkAnimQueue::Clear()",
            "CNetworkPedManager::Clear()",
            "CNetworkPlayerManager::Clear()",
            "CNetworkVehicleManager::Clear()",
            "GetPacketFactory().ClearRecords()",
            "CServerTime::Reset()",
            "CMissionSessionClient::Reset()",
        ):
            self.assertIn(cleanup, reset)
        self.assertIn("delete packet", packet_buffer)
        self.assertIn(
            "while (m_bConnected && enet_host_service(m_pENetHost, &eNetEvent, 0) > 0)",
            client,
        )

    def test_mission_snapshot_precedes_and_gates_cached_enex(self):
        server = (ROOT / "server/src/CNetwork.cpp").read_text(encoding="utf-8")
        snapshot = server.index("CMissionSessionServer::SendSnapshot(pNewNetworkPlayer)")
        cached_enex = server.index(
            "GetPacketFactory().Send(Packets::Scripts::g_lastEnExData, pNewNetworkPlayer)"
        )
        self.assertLess(snapshot, cached_enex)
        self.assertIn("missionState.ContainsGameplayParticipant(freeId)", server)


if __name__ == "__main__":
    unittest.main()
