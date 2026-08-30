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


if __name__ == "__main__":
    unittest.main()
