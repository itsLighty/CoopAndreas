import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


def packet_handler_body(source: str, packet_type: str) -> str:
    signature = re.search(rf"PACKET_HANDLER\(ePacketType::{packet_type}\s*,", source)
    if signature is None:
        raise AssertionError(f"missing handler for {packet_type}")
    opening = source.find("{", signature.end())
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : index]
    raise AssertionError(f"unterminated {packet_type} handler")


class RemoteClothesSafetyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.player_source = (ROOT / "client/src/CNetworkPlayer.cpp").read_text(encoding="utf-8")
        cls.player_header = (ROOT / "client/src/CNetworkPlayer.h").read_text(encoding="utf-8")
        handlers = (ROOT / "client/src/PacketHandlers/players.cpp").read_text(encoding="utf-8")
        cls.rebuild_handler = packet_handler_body(handlers, "REBUILD_PLAYER")

    def test_remote_player_never_invokes_the_global_native_clothes_builder(self):
        combined = self.player_source + "\n" + self.rebuild_handler
        self.assertNotIn("CClothes::RebuildPlayer", combined)
        self.assertNotIn("CClothes::ConstructPedModel", combined)
        self.assertNotIn("CClothesBuilder::", combined)

    def test_rebuild_packet_still_caches_authoritative_state_and_stats(self):
        self.assertIn("m_pPedClothesDesc = pRebuildPlayer->clothesDesc", self.rebuild_handler)
        self.assertIn("m_stats[STAT_FAT]", self.rebuild_handler)
        self.assertIn("m_stats[STAT_MUSCLE]", self.rebuild_handler)

    def test_remote_clothes_cannot_schedule_a_later_native_rebuild(self):
        combined = self.player_header + "\n" + self.player_source + "\n" + self.rebuild_handler
        self.assertNotIn("QueueClothesRebuild", combined)
        self.assertNotIn("TryRebuildClothes", combined)
        self.assertNotIn("m_bNeedsClothesRebuild", combined)


if __name__ == "__main__":
    unittest.main()
