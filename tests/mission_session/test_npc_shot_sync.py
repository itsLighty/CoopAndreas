import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class NpcShotSyncTests(unittest.TestCase):
    def test_npc_shots_have_sender_authority_and_remote_application(self):
        packet = (ROOT / "shared/network/packets/peds.h").read_text(encoding="utf-8")
        sender = (ROOT / "client/src/Hooks/PedHooks.cpp").read_text(encoding="utf-8")
        server = (ROOT / "server/src/PacketHandlers/peds.cpp").read_text(encoding="utf-8")
        receiver = (ROOT / "client/src/PacketHandlers/peds.cpp").read_text(encoding="utf-8")

        self.assertIn("class PedShotSync", packet)
        self.assertIn("ePacketChannel::EVENT", packet)
        self.assertIn("Packets::Peds::PedShotSync packet", sender)
        self.assertIn("if (pNetworkPed->m_bSyncing)", sender)
        self.assertIn("pNetworkPed->m_pSyncer != pNetworkPlayer", server)
        self.assertIn("GetPacketFactory().SendToAll(*pPedShotSync", server)
        self.assertIn("PACKET_HANDLER(ePacketType::PED_SHOT_SYNC", receiver)
        self.assertIn("pNetworkPed->m_pPed->GetWeapon().Fire", receiver)


if __name__ == "__main__":
    unittest.main()
