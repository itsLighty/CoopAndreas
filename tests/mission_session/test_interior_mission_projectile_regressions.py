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


class InteriorMissionProjectileRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.player_packets = (ROOT / "shared/network/packets/players.h").read_text(encoding="utf-8")
        cls.client_main = (ROOT / "client/src/Main.cpp").read_text(encoding="utf-8")
        cls.client_player = (ROOT / "client/src/CNetworkPlayer.cpp").read_text(encoding="utf-8")
        cls.server_players = (ROOT / "server/src/PacketHandlers/players.cpp").read_text(encoding="utf-8")
        cls.client_projectiles = (ROOT / "client/src/PacketHandlers/players.cpp").read_text(encoding="utf-8")
        cls.projectile_hooks = (ROOT / "client/src/Hooks/ProjectileHooks.cpp").read_text(encoding="utf-8")
        cls.map_pins = (ROOT / "client/src/UI/CNetworkPlayerMapPin.cpp").read_text(encoding="utf-8")
        cls.blips = (ROOT / "client/src/CNetworkEntityBlip.cpp").read_text(encoding="utf-8")
        cls.stream_manager = (ROOT / "client/src/CNetworkEntityStreamManager.cpp").read_text(encoding="utf-8")
        cls.blip_packets = (ROOT / "shared/network/packets/blips.h").read_text(encoding="utf-8")
        cls.server_blips = (ROOT / "server/src/PacketHandlers/blips.cpp").read_text(encoding="utf-8")
        cls.server_network = (ROOT / "server/src/CNetwork.cpp").read_text(encoding="utf-8")
        cls.server_player_header = (ROOT / "server/src/CNetworkPlayer.h").read_text(encoding="utf-8")
        cls.static_blips = (ROOT / "client/src/CNetworkStaticBlip.cpp").read_text(encoding="utf-8")
        cls.opcode_sync = (ROOT / "client/src/COpCodeSync.cpp").read_text(encoding="utf-8")
        cls.cutscene_votes = (ROOT / "client/src/CCutsceneVoteManager.cpp").read_text(encoding="utf-8")
        cls.enex_sync = (ROOT / "client/src/CEntryExitTransitionSync.cpp").read_text(encoding="utf-8")

    def test_onfoot_snapshots_carry_authoritative_area_continuously(self):
        packet = re.search(r"class OnFootUpdate\b.*?^};", self.player_packets, re.S | re.M).group(0)
        self.assertIn("uint8_t areaId = AREA_MAIN_MAP", packet)
        self.assertIn("serialize_int(stream, areaId, AREA_MAIN_MAP, MAX_VISIBLE_AREAS - 1)", packet)
        self.assertIn("onFootUpdate.areaId = static_cast<uint8_t>(localPlayer->m_nAreaCode)", self.client_main)
        cache = function_body(self.client_player, "bool CNetworkPlayer::CacheOnFootSnapshot(")
        self.assertIn("m_nLogicalArea = snapshot.areaId", cache)
        self.assertIn("m_pPed->m_nAreaCode = m_nLogicalArea", cache)
        self.assertIn("m_pPed->UpdateRwMatrix()", cache)
        relay = function_body(self.server_players, "ePacketType::PLAYER_ONFOOT_UPDATE")
        self.assertIn("ObservePlayerArea(pNetworkPlayer, pOnFootUpdate->areaId)", relay)

    def test_player_map_pin_survives_cross_interior_stream_out(self):
        process = function_body(self.map_pins, "void CNetworkPlayerMapPin::Process(")
        self.assertIn("player->GetMapPosition()", process)
        self.assertNotIn("player->m_pPed == nullptr", process)
        marker = function_body(self.map_pins, "bool GetPlayerMarkerPosition(")
        self.assertIn("worldPosition.x", marker)
        self.assertNotIn("FindPlayerCoors", marker)
        map_position = function_body(self.client_player, "CVector CNetworkPlayer::GetMapPosition() const")
        self.assertIn("m_nLogicalArea == AREA_MAIN_MAP", map_position)
        self.assertIn("m_vecMapPosition", map_position)
        onfoot = function_body(self.client_player, "bool CNetworkPlayer::CacheOnFootSnapshot(")
        self.assertIn("m_vecMapPosition = snapshot.vecPos", onfoot)
        self.assertIn("pNetworkPlayer->m_vecMapPosition = packet.position", self.enex_sync)

    def test_late_joiner_receives_existing_host_authority_and_presentation(self):
        self.assertIn("m_lastOnFootSnapshot", self.server_player_header)
        relay = function_body(self.server_players, "ePacketType::PLAYER_ONFOOT_UPDATE")
        self.assertIn("m_lastOnFootSnapshot = *pOnFootUpdate", relay)
        self.assertIn("GetPacketFactory().Send(onFootSnapshot, pNewNetworkPlayer)", self.server_network)
        self.assertIn("GetPacketFactory().Send(playerAssignHost, pNewNetworkPlayer)", self.server_network)

    def test_entity_mission_blips_are_retained_until_native_entity_materializes(self):
        update = function_body(self.blips, "void CNetworkEntityBlip::UpdateEntityBlip(")
        self.assertIn("g_desiredPedBlips", update)
        self.assertIn("g_desiredVehicleBlips", update)
        self.assertIn("ApplyDesiredPedBlip", update)
        self.assertIn("ApplyDesiredVehicleBlip", update)
        self.assertIn("CRadar::ChangeBlipColour", self.blips)
        self.assertIn("CNetworkEntityBlip::HasDesiredPedBlip", self.stream_manager)
        self.assertIn("CNetworkEntityBlip::HasDesiredVehicleBlip", self.stream_manager)

    def test_static_mission_and_interior_icons_replay_to_joiners(self):
        self.assertIn("g_lastStaticBlipsData", self.blip_packets)
        self.assertIn("g_pLastStaticBlipsOwner", self.blip_packets)
        handler = function_body(self.server_blips, "ePacketType::CREATE_STATIC_BLIP")
        self.assertIn("g_lastStaticBlipsData = *pCreateStaticBlip", handler)
        self.assertIn("g_pLastStaticBlipsOwner = pNetworkPlayer", handler)
        self.assertIn("GetPacketFactory().Send(Packets::Blips::g_lastStaticBlipsData, pNewNetworkPlayer)",
                      self.server_network)
        self.assertIn("CNetworkStaticBlip::Process();", self.client_main)
        self.assertIn("HOST_SNAPSHOT_INTERVAL_MS", self.static_blips)
        self.assertIn("SnapshotMatchesRadar", self.static_blips)
        self.assertIn("ApplySnapshot(ms_lastAuthoritativeSnapshot)", self.static_blips)

    def test_cutscenes_are_never_played_and_black_frame_is_restored(self):
        self.assertIn("CCutsceneVoteManager::Process();", self.client_main)
        self.assertIn("header.opcode == 0x02E4", self.opcode_sync)
        self.assertIn("CCutsceneVoteManager::BeginDisabledCutscene();", self.opcode_sync)
        self.assertIn("CCutsceneVoteManager::EndDisabledCutscene();", self.opcode_sync)
        self.assertIn("TheCamera.SetWideScreenOff()", self.cutscene_votes)
        self.assertIn("TheCamera.RestoreWithJumpCut()", self.cutscene_votes)
        self.assertIn("CDraw::FadeValue = 0", self.cutscene_votes)
        self.assertIn("CCutsceneMgr::FinishCutscene()", self.cutscene_votes)

    def test_projectile_optional_direction_is_not_inverted(self):
        packet = re.search(r"class AddProjectile\b.*?^};", self.player_packets, re.S | re.M).group(0)
        self.assertIn("projectileType >= WEAPON_GRENADE", packet)
        self.assertIn("projectileType <= WEAPON_FREEFALL_BOMB", packet)
        handler = function_body(self.client_projectiles, "ePacketType::ADD_PROJECTILE")
        direction_branch = function_body(handler, "if (pAddProjectile->bDir)")
        self.assertIn("&dir", direction_branch)
        self.assertNotIn("nullptr", direction_branch)
        self.assertRegex(handler, re.compile(r"else\s*\{.*?force, nullptr, pTarget", re.S))
        self.assertIn("creator == nullptr", self.projectile_hooks)
        self.assertIn("!creator->IsVTableValid()", self.projectile_hooks)
        server_handler = function_body(self.server_players, "ePacketType::ADD_PROJECTILE")
        self.assertIn("creatorOwnedBySender", server_handler)
        self.assertIn("IsSemanticallyValid", server_handler)


if __name__ == "__main__":
    unittest.main()
