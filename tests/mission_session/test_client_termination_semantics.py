import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class ClientTerminationSemanticsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.session = (ROOT / "client/src/CMissionSessionClient.cpp").read_text(encoding="utf-8")
        cls.opcodes = (ROOT / "client/src/COpCodeSync.cpp").read_text(encoding="utf-8")
        cls.main = (ROOT / "client/src/Main.cpp").read_text(encoding="utf-8")
        cls.script_packets = (ROOT / "client/src/PacketHandlers/scripts.cpp").read_text(encoding="utf-8")
        cls.styled_text = (
            ROOT / "client/src/Commands/Commands/CCommandShowTextStyledForNetworkPlayer.cpp"
        ).read_text(encoding="utf-8")

    def test_terminal_result_uses_scm_signal_with_completed_fallback(self):
        self.assertIn("RequestEnd(ResolveScmMissionResult())", self.session)
        self.assertIn("m_PendingEndAfterLaunchResult = ResolveScmMissionResult()", self.session)
        self.assertIn("return eMissionSessionResult::COMPLETED;", self.session)
        self.assertIn("COMMAND_REGISTER_MISSION_PASSED", self.opcodes)
        self.assertIn('"M_FAIL"', self.opcodes)
        self.assertIn('"M_PASS"', self.opcodes)
        self.assertIn("ReportScmMissionResult", self.styled_text)
        self.assertIn(
            "m_ObservedScmMissionResult != eMissionSessionResult::NONE",
            self.session,
        )
        build = re.search(
            r"void BuildAndSendOpcode\(\)\s*\{(.*?)\n\}", self.opcodes, re.S
        ).group(1)
        self.assertLess(
            build.index("ObserveMissionResultFromSynchronizedOpcode()"),
            build.index("if (!COpCodeSync::IsOpcodeSyncable"),
        )
        self.assertRegex(
            build,
            r"(?s)if \(ObserveMissionResultFromSynchronizedOpcode\(\)\).*?"
            r"ResetCollectedOpcodeParameters\(\);\s*return;",
        )
        observer = re.search(
            r"bool ObserveMissionResultFromSynchronizedOpcode\(\)\s*\{(.*?)\n\}",
            self.opcodes,
            re.S,
        ).group(1)
        self.assertIn("IsCurrentOpcodeFromSynchronizedMission()", observer)
        self.assertIn("COMMAND_REGISTER_MISSION_PASSED", observer)
        self.assertIn("IsMissionResultTextOpcode", observer)
        self.assertNotIn("IsOpcodeSyncable", observer)
        registered_script = re.search(
            r"bool IsRegisteredSynchronizedScript\(.*?\)\s*\{(.*?)\n\}",
            self.opcodes,
            re.S,
        ).group(1)
        self.assertIn("ms_aszSyncedScripts", registered_script)
        self.assertIn("strnicmp", registered_script)
        collector = re.search(
            r"void CollectTextParameters\(\)\s*\{(.*?)\n\}", self.opcodes, re.S
        ).group(1)
        self.assertIn("IsMissionResultTextOpcode", collector)
        self.assertIn("IsCurrentOpcodeFromSynchronizedMission", collector)
        whitelist = re.search(
            r"const SSyncedOpCode syncedOpcodes\[\]\s*=\s*\{(.*?)\n\};",
            self.opcodes,
            re.S,
        ).group(1)
        self.assertNotIn("0x0318", whitelist)
        self.assertIn("script->m_bIsMission", self.styled_text)

    def test_cleanup_cancels_and_untags_all_deferred_media(self):
        cleanup = re.search(
            r"void CMissionSessionClient::CancelPendingMissionMedia\(\)\s*\{(.*?)\n\}",
            self.session,
            re.S,
        ).group(1)
        self.assertIn("Command<Commands::CLEAR_MISSION_AUDIO>(slot + 1)", cleanup)
        self.assertIn("Command<Commands::CLEAR_CUTSCENE>()", cleanup)
        self.assertIn("ms_anLoadingMissionAudioSessionIds[slot] = 0", cleanup)
        self.assertIn("ms_nLoadingCutsceneSessionId = 0", cleanup)
        self.assertGreaterEqual(self.session.count("CancelPendingMissionMedia();"), 2)

    def test_deferred_playback_requires_the_same_active_session(self):
        self.assertIn("ms_anLoadingMissionAudioSessionIds", self.script_packets)
        self.assertIn("missionSession.sessionId", self.script_packets)
        self.assertIn("ms_nLoadingCutsceneSessionId = sessionId", self.opcodes)
        self.assertIn("IsDeferredMediaSessionCurrent", self.main)
        self.assertLess(
            self.main.index("CMissionSessionClient::Process();"),
            self.main.index("bHasStaleDeferredMedia"),
        )

    def test_debug_launch_waits_only_for_gameplay_participants(self):
        required = re.search(
            r"for \(size_t rosterIndex = 1; rosterIndex < state\.gameplayParticipantCount;.*?\n    \}",
            self.session,
            re.S,
        ).group(0)
        optional = re.search(
            r"for \(size_t rosterIndex = state\.gameplayParticipantCount;.*?\n    \}",
            self.session,
            re.S,
        ).group(0)
        self.assertIn("participant == nullptr || participant->m_pPed == nullptr", required)
        self.assertIn("if (CNetworkPlayer* spectator", optional)
        self.assertNotIn("return false", optional)


if __name__ == "__main__":
    unittest.main()
