import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class MissionSessionProtocolStructureTests(unittest.TestCase):
    def test_packet_enum_and_debug_names_are_in_lockstep(self):
        text = (ROOT / "shared/network/packet_types.h").read_text(encoding="utf-8")
        enum_body = re.search(r"enum class ePacketType[^\{]*\{(.*?)PACKET_ID_MAX", text, re.S).group(1)
        enum_names = re.findall(r"^\s*([A-Z][A-Z0-9_]*)\s*,", enum_body, re.M)
        array_body = re.search(r"static constexpr const char\* array\[\]\s*=\s*\{(.*?)\};", text, re.S).group(1)
        debug_names = re.findall(r'"([A-Z][A-Z0-9_]*)"', array_body)
        self.assertEqual(enum_names, debug_names)

    def test_protocol_mission_bound_matches_scm(self):
        packet_text = (ROOT / "shared/network/packets/scripts.h").read_text(encoding="utf-8")
        scm_text = (ROOT / "scm/main.txt").read_text(encoding="utf-8")
        protocol_count = int(re.search(r"MISSION_ID_COUNT\s*=\s*(\d+)", packet_text).group(1))
        scm_count = int(re.search(r"DEFINE MISSIONS\s+(\d+)", scm_text).group(1))
        self.assertEqual(protocol_count, scm_count)

    def test_wire_uses_one_serializer_for_read_write_and_measure(self):
        packet_header = (ROOT / "shared/network/packet.h").read_text(encoding="utf-8")
        scripts = (ROOT / "shared/network/packets/scripts.h").read_text(encoding="utf-8")
        self.assertIn("Serialize(stream)", packet_header)
        self.assertIn("serialize_bool(stream, acknowledgedRequestAccepted)", scripts)
        self.assertIn("return HasValidParticipantRoster();", scripts)
        self.assertIn("return HasValidPayload();", scripts)

    def test_wrap_comparisons_cover_forward_and_stale_values(self):
        def newer(candidate, reference, bits):
            mask = (1 << bits) - 1
            distance = (candidate - reference) & mask
            return distance != 0 and distance < (1 << (bits - 1))

        for bits in (32, 64):
            maximum = (1 << bits) - 1
            self.assertTrue(newer(1, maximum, bits))
            self.assertFalse(newer(maximum, 1, bits))
            self.assertFalse(newer(7, 7, bits))
            self.assertFalse(newer(1 << (bits - 1), 0, bits))

    def test_authority_and_recipient_guards_are_present(self):
        server = (ROOT / "server/src/CMissionSessionServer.cpp").read_text(encoding="utf-8")
        handlers = (ROOT / "server/src/PacketHandlers/scripts.cpp").read_text(encoding="utf-8")
        client = (ROOT / "client/src/PacketHandlers/scripts.cpp").read_text(encoding="utf-8")
        self.assertIn("CNetworkPlayerManager::GetHost() == pNetworkPlayer", server)
        self.assertIn("acknowledgedRequestAccepted", server)
        self.assertIn("SendToMissionRecipients", handlers)
        self.assertIn("ShouldIgnoreMissionEffect", client)

    def test_new_translation_units_are_in_existing_globs(self):
        xmake = (ROOT / "xmake.lua").read_text(encoding="utf-8")
        self.assertIn('add_files("client/src/*.cpp")', xmake)
        self.assertIn('add_files("server/src/**.cpp")', xmake)

    def test_scm_roster_bridge_uses_frozen_session_slots(self):
        collect = (
            ROOT
            / "client/src/Commands/Commands/CCommandCollectNetworkPlayersForTheMission.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn('CMissionSessionClient::GetState()', collect)
        self.assertIn('missionSession.IsActive()', collect)
        self.assertIn('missionSession.gameplayParticipantCount', collect)
        self.assertIn('missionSession.participantIds[rosterIndex]', collect)
        self.assertIn('participantId == missionSession.hostId', collect)
        self.assertIn('GetFrozenParticipantPedHandle(participantId)', collect)
        self.assertIn('else\n\t{\n\t\t// Legacy scripts', collect)

    def test_player_id_lookup_rejects_spectators_and_invalid_peds(self):
        lookup = (
            ROOT
            / "client/src/Commands/Commands/CCommandGetNetworkPlayerPedHandleTo.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn('playerId < 0 || playerId >= Config::MAX_SERVER_PLAYERS', lookup)
        self.assertIn('!missionSession.ContainsGameplayParticipant(playerId)', lookup)
        self.assertIn('candidate == nullptr', lookup)
        self.assertIn('candidate->m_pPed == nullptr', lookup)
        self.assertIn('CPools::ms_pPedPool == nullptr', lookup)
        self.assertIn('CPools::ms_pPedPool->IsObjectValid', lookup)
        self.assertIn('CPools::GetPed(pedHandle) == candidate->m_pPed', lookup)
        self.assertIn('INVALID_SCM_CHAR_HANDLE', lookup)

    def test_strict_story_audit_and_ci_gate_protocol_evidence(self):
        audit = (ROOT / "scripts/audit-story-missions.ps1").read_text(encoding="utf-8")
        workflow = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
        for evidence in (
            "periodicRecollection",
            "immutableIdentity",
            "participantDeathPolicy",
            "boundedRegistration",
            "resultFanout",
            "idempotentCleanup",
        ):
            self.assertIn(evidence, audit)
        self.assertIn("Get-UnboundedNetworkIdWaitLines", audit)
        self.assertIn("audit-story-missions.ps1 -RequireReady -SummaryOnly", workflow)
        self.assertIn("python -m unittest discover -s tests/mission_session", workflow)

    def test_protocol_contract_documents_bridge_and_runtime_limits(self):
        protocol = (ROOT / "docs/mission-coop-protocol.md").read_text(encoding="utf-8")
        self.assertIn("frozen gameplay IDs", protocol)
        self.assertIn("stable SCM slots", protocol)
        self.assertIn("invalid SCM character handle is `0`", protocol)
        self.assertIn("Runtime limitations", protocol)

    def test_shared_badlands_races_abort_through_stock_cleanup(self):
        wrapper = (ROOT / "scm/scripts/BCESAR4.txt").read_text(encoding="utf-8")
        race = (ROOT / "scm/scripts/CPRACE.txt").read_text(encoding="utf-8")
        self.assertRegex(
            wrapper,
            r"\$failed_cesar_race\s*=\s*0\s+start_new_script\s+@BCESAR4_COOP_RACE",
        )
        self.assertIn("$failed_cesar_race = 1", wrapper)
        self.assertIn(
            "set_var_int_to_lvar_int $flag_bcesar_mission_counter = 41@", wrapper
        )
        for evidence in (
            "$failed_cesar_race == 1",
            "$race_selection == 7",
            "$race_selection == 8",
            "229@ = 1",
            "450@ = 2",
            "49@ = 13",
        ):
            self.assertIn(evidence, race)


if __name__ == "__main__":
    unittest.main()
