import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1 : index]
    raise AssertionError(f"unterminated function: {signature}")


class CutsceneVoteSyncTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.packets = (ROOT / "shared/network/packets/scripts.h").read_text(encoding="utf-8")
        cls.packet_types = (ROOT / "shared/network/packet_types.h").read_text(encoding="utf-8")
        cls.server = (ROOT / "server/src/CCutsceneVoteManager.cpp").read_text(encoding="utf-8")
        cls.server_header = (ROOT / "server/src/CCutsceneVoteManager.h").read_text(encoding="utf-8")
        cls.server_handlers = (ROOT / "server/src/PacketHandlers/scripts.cpp").read_text(encoding="utf-8")
        cls.client = (ROOT / "client/src/CCutsceneVoteManager.cpp").read_text(encoding="utf-8")
        cls.client_handlers = (ROOT / "client/src/PacketHandlers/scripts.cpp").read_text(encoding="utf-8")
        cls.opcodes = (ROOT / "client/src/COpCodeSync.cpp").read_text(encoding="utf-8")
        cls.hooks = (ROOT / "client/src/Hooks/GameHooks.cpp").read_text(encoding="utf-8")
        cls.server_network = (ROOT / "server/src/CNetwork.cpp").read_text(encoding="utf-8")
        cls.server_mission = (ROOT / "server/src/CMissionSessionServer.cpp").read_text(encoding="utf-8")
        cls.client_mission = (ROOT / "client/src/CMissionSessionClient.cpp").read_text(encoding="utf-8")

    def test_protocol_is_narrow_versioned_and_wire_bounded(self):
        for packet_type in (
            "CUTSCENE_START_REQUEST",
            "CUTSCENE_VOTE_REQUEST",
            "CUTSCENE_END_REQUEST",
            "CUTSCENE_VOTE_STATE",
        ):
            self.assertIn(packet_type, self.packet_types)
        config = (ROOT / "shared/config.h").read_text(encoding="utf-8")
        self.assertIn('COOPANDREAS_VERSION "0.3.4-alpha"', config)
        state = re.search(r"class CutsceneVoteState\b.*?^};", self.packets, re.S | re.M).group(0)
        self.assertIn("sessionId", state)
        self.assertIn("missionEpoch", state)
        self.assertIn("cutsceneEpoch", state)
        self.assertIn("startRequestId", state)
        self.assertIn("eligibleCount > MISSION_SCM_GAMEPLAY_PLAYER_CAP", state)
        self.assertIn("voteCount > eligibleCount", state)
        self.assertIn("eligibleCount / 2 + 1", state)
        self.assertIn("HasValidVoteState()", state)

    def test_vote_sender_identity_is_never_client_supplied(self):
        vote = re.search(r"class CutsceneVoteRequest\b.*?^};", self.packets, re.S | re.M).group(0)
        self.assertNotIn("SenderPlayerId", vote)
        self.assertNotRegex(vote, r"\bplayer(?:id|Id)\b")
        self.assertIn("pNetworkPlayer->m_iPlayerId", self.server)
        self.assertIn("CNetworkPlayerManager::GetPlayer(pNetworkPlayer->m_iPlayerId) != pNetworkPlayer", self.server)

    def test_scm_remains_the_only_visual_start_path(self):
        self.assertIn("GetPacketFactory().Send(packet);", self.opcodes)
        send = function_body(self.opcodes, "void BuildAndSendOpcode()")
        self.assertLess(send.index("GetPacketFactory().Send(packet)"), send.index("NotifySynchronizedCutsceneStarted"))
        self.assertIn("lastOpCodeProcessed == 0x02E7", send)
        self.assertIn("lastOpCodeProcessed == 0x02EA", send)
        self.assertNotIn("CCutsceneMgr__StartCutscene_Hook", self.hooks)
        self.assertIn("#if 0 // controlled with SCM", self.client_handlers)
        self.assertIn("#if 0  // controlled with SCM", self.server_handlers)

    def test_server_start_requires_host_and_snapshots_frozen_gameplay_roster(self):
        start = function_body(self.server, "bool CCutsceneVoteManager::HandleStartRequest(")
        self.assertIn("CMissionSessionServer::IsAuthoritativeHost(pNetworkPlayer)", start)
        self.assertIn("CMissionSessionServer::IsGameplayParticipant(pNetworkPlayer)", start)
        self.assertIn("request.sessionId != mission.sessionId", start)
        self.assertIn("request.missionEpoch != mission.epoch", start)
        self.assertIn("ClearState();", start)
        self.assertIn("NextCutsceneEpoch()", start)
        self.assertIn("mission.gameplayParticipantCount", start)
        self.assertIn("mission.participantIds[rosterIndex]", start)
        self.assertIn("m_State.eligibleCount / 2 + 1", start)

    def test_only_connected_frozen_gameplay_participants_vote_once(self):
        eligibility = function_body(self.server, "bool CCutsceneVoteManager::IsEligibleConnectedPlayer(")
        self.assertIn("CNetworkPlayerManager::GetPlayer", eligibility)
        self.assertIn("m_abEligiblePlayers", eligibility)
        self.assertIn("CMissionSessionServer::IsGameplayParticipant", eligibility)
        vote = function_body(self.server, "bool CCutsceneVoteManager::HandleVoteRequest(")
        self.assertIn("m_abPlayerVotes[playerId]", vote)
        self.assertIn("Rejected a duplicate cutscene vote", vote)
        self.assertIn("++m_State.voteCount", vote)
        self.assertNotIn("ClearState()", vote)

    def test_threshold_skip_and_end_are_server_owned_and_exactly_once(self):
        vote = function_body(self.server, "bool CCutsceneVoteManager::HandleVoteRequest(")
        self.assertIn("m_State.voteCount >= m_State.requiredVotes", vote)
        self.assertIn("m_State.lifecycle = eCutsceneVoteLifecycle::SKIPPED", vote)
        self.assertIn("if (m_bSkipBroadcast)", vote)
        self.assertIn("m_bSkipBroadcast = true", vote)
        self.assertEqual(vote.count("BroadcastState();"), 1)
        end = function_body(self.server, "bool CCutsceneVoteManager::HandleEndRequest(")
        self.assertIn("CMissionSessionServer::IsAuthoritativeHost", end)
        self.assertIn("MatchesCurrentCutscene", end)
        self.assertLess(end.index("BroadcastState();"), end.index("ClearState();"))
        self.assertIn("server-owned cutscene vote state", self.server_handlers)

    def test_stale_replay_and_spectator_paths_are_rejected(self):
        match = function_body(self.server, "bool CCutsceneVoteManager::MatchesCurrentCutscene(")
        for identity in ("sessionId", "missionEpoch", "cutsceneEpoch"):
            self.assertIn(identity, match)
        self.assertIn("m_State.lifecycle != eCutsceneVoteLifecycle::ACTIVE", self.server)
        self.assertIn("IsSequenceNumberNewer(state.cutsceneEpoch, m_nLastCutsceneEpoch)", self.client)
        self.assertIn("state.voteCount < m_State.voteCount", self.client)
        self.assertIn("static_cast<int>(state.lifecycle) < static_cast<int>(m_State.lifecycle)", self.client)
        self.assertIn("CMissionSessionClient::IsSpectator()", self.client)

    def test_client_vote_never_performs_local_skip(self):
        input_handler = function_body(self.client, "bool CCutsceneVoteManager::HandleSkipButton(")
        self.assertIn("if (!CNetwork::m_bAuthenticated || !mission.IsActive())", input_handler)
        self.assertIn("return true;", input_handler)
        self.assertIn("CutsceneVoteRequest request", input_handler)
        self.assertIn("m_bLocalVoteSent = true", input_handler)
        self.assertTrue(input_handler.rstrip().endswith("return false;"))
        self.assertNotIn("FinishCutscene", input_handler)
        apply_skip = function_body(self.client, "void CCutsceneVoteManager::ApplyAuthoritativeSkip(")
        self.assertIn("if (m_bSkipApplied)", apply_skip)
        self.assertIn("CCutsceneMgr::FinishCutscene();", apply_skip)
        self.assertIn("eCutsceneVoteLifecycle::SKIPPED", self.client)
        for callsite in ("0x5B1947", "0x469F0E", "0x475459"):
            self.assertIn(f"patch::RedirectCall({callsite}, CCutsceneMgr__IsCutsceneSkipButtonBeingPressed_Hook)", self.hooks)

    def test_reconnect_snapshot_preserves_server_votes_and_orders_after_mission(self):
        mission_snapshot = self.server_network.index("CMissionSessionServer::SendSnapshot(pNewNetworkPlayer)")
        cutscene_snapshot = self.server_network.index("CCutsceneVoteManager::SendSnapshot(pNewNetworkPlayer)")
        self.assertLess(mission_snapshot, cutscene_snapshot)
        snapshot = function_body(self.server, "void CCutsceneVoteManager::SendSnapshot(")
        self.assertIn("IsEligibleConnectedPlayer", snapshot)
        self.assertIn("GetPacketFactory().Send(m_State, pNetworkPlayer)", snapshot)
        self.assertNotIn("m_abPlayerVotes", snapshot)
        self.assertNotIn("ClearState", snapshot)

    def test_mission_lifecycle_and_disconnect_reset_cutscene_state(self):
        self.assertGreaterEqual(self.server_mission.count("CCutsceneVoteManager::ResetForMissionSession();"), 3)
        self.assertIn("AbortSession(eMissionSessionResult::HOST_DISCONNECTED)", self.server_mission)
        self.assertIn("CCutsceneVoteManager::Reset();", self.client_mission)
        self.assertIn("CCutsceneVoteManager::HandleMissionSessionReset();", self.client_mission)
        reset = function_body(self.client, "void CCutsceneVoteManager::HandleMissionSessionReset(")
        self.assertIn("m_bEndWhenAcknowledged = false", reset)
        self.assertIn("m_bLocalVoteSent = false", reset)

    def test_new_start_cannot_inherit_end_before_ack_latch(self):
        start = function_body(self.client, "void CCutsceneVoteManager::NotifySynchronizedCutsceneStarted(")
        clear_latch = start.index("ClearActiveState();")
        install_request = start.index("m_nPendingStartRequestId = NextStartRequestId()")
        self.assertLess(clear_latch, install_request)
        clear = function_body(self.client, "void CCutsceneVoteManager::ClearActiveState(")
        for stale_field in (
            "m_nPendingStartRequestId = 0",
            "m_nPendingSessionId = 0",
            "m_nPendingMissionEpoch = 0",
            "m_bEndWhenAcknowledged = false",
            "m_bEndRequestSent = false",
        ):
            self.assertIn(stale_field, clear)
        end = function_body(self.client, "void CCutsceneVoteManager::NotifySynchronizedCutsceneEnded(")
        self.assertIn("m_bEndWhenAcknowledged = true", end)
        handle = function_body(self.client, "void CCutsceneVoteManager::HandleState(")
        self.assertIn("bStateAcknowledgesPendingStart", handle)
        self.assertIn("bEndAcknowledgedCutscene", handle)
        self.assertIn("if (bEndAcknowledgedCutscene", handle)
        self.assertLess(handle.index("bEndAcknowledgedCutscene"), handle.index("ClearActiveState();"))


if __name__ == "__main__":
    unittest.main()
