import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class NetworkSoakHarnessTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.harness = (ROOT / "tests/network_soak/main.cpp").read_text(encoding="utf-8")
        cls.runner = (ROOT / "tests/network_soak/run.ps1").read_text(encoding="utf-8")
        cls.xmake = (ROOT / "xmake.lua").read_text(encoding="utf-8")
        cls.server_factory = (ROOT / "server/src/CPacketFactory.cpp").read_text(encoding="utf-8")

    def test_harness_uses_production_enet_packets_channels_and_serializers(self):
        for evidence in (
            '#include "enet/enet.h"',
            '#include "network/packets/players.h"',
            '#include "network/packets/system.h"',
            "serialize::WriteStream",
            "serialize::ReadStream",
            "GetChannelReliability",
            "enet_host_create",
            "enet_host_connect",
        ):
            self.assertIn(evidence, self.harness)

    def test_harness_covers_late_join_relay_disconnect_and_reconnect(self):
        for evidence in (
            'Bot alpha("SoakAlpha")',
            'Bot bravo("SoakBravo")',
            "alpha.SendGameplayFrame",
            "bravo.Connect",
            "PLAYER_ONFOOT_UPDATE",
            "PLAYER_CAMERA_SYNC",
            "PLAYER_KEY_SYNC",
            "PLAYER_DISCONNECTED",
            "PlayerReconnectRequest",
            "StuntDefinitionAnnounce",
        ):
            self.assertIn(evidence, self.harness)

    def test_runner_stages_real_server_and_always_stops_only_its_child(self):
        for evidence in (
            "Copy-Item -LiteralPath $serverBinary",
            "Start-Process @processOptions",
            "Get-NetUDPEndpoint",
            "Stop-Process -Id $serverProcess.Id -Force",
        ):
            self.assertIn(evidence, self.runner)

    def test_failed_server_serialization_cannot_send_a_partial_packet(self):
        self.assertIn("static bool BuildPacketStream", self.server_factory)
        self.assertEqual(self.server_factory.count("if (!BuildPacketStream("), 3)
        self.assertIn("ePacketType_ToString", self.server_factory)

    def test_xmake_builds_a_dedicated_32_bit_soak_target(self):
        self.assertIn('target("network_soak"', self.xmake)
        self.assertIn('add_files("tests/network_soak/main.cpp")', self.xmake)
        self.assertIn('set_arch("x86")', self.xmake)


if __name__ == "__main__":
    unittest.main()
