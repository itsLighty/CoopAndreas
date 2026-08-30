import json
import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCM = ROOT / "scm/scripts"
PROPERTY_FILES = {
    name: (SCM / name).read_text(encoding="utf-8")
    for name in (
        "BUY_PRO.txt",
        "BUYPRO1.txt",
        "BUY1.txt",
        "BUY2.txt",
        "BUY3.txt",
        "INITIAL.txt",
        "OPENUP.txt",
    )
}


class PropertyPurchaseSyncTests(unittest.TestCase):
    def test_authentication_condition_uses_network_state_not_streamed_actors(self):
        source = (
            ROOT
            / "client/src/Commands/Commands/CCommandIsNetworkAuthenticated.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("script->UpdateCompareFlag(CNetwork::m_bAuthenticated);", source)
        self.assertNotIn("CNetworkPlayer", source)
        self.assertNotIn("m_pPed", source)

        for name, text in PROPERTY_FILES.items():
            if name == "INITIAL.txt":
                continue
            with self.subTest(script=name):
                self.assertIn("Coop.IsNetworkAuthenticated()", text)
                self.assertNotIn("Coop.IsNetworkPlayerActorValid", text)
                self.assertNotIn("$NETWORK_PLAYER", text)

    def test_opcode_1d1e_is_registered_and_all_sanny_definitions_match(self):
        registrar = (
            ROOT / "client/src/Commands/CCustomCommandRegistrar.h"
        ).read_text(encoding="utf-8")
        self.assertEqual(
            registrar.count(
                "RegisterCommand(0x1D1E, new CCommandIsNetworkAuthenticated())"
            ),
            1,
        )
        self.assertIn(
            '#include "Commands/CCommandIsNetworkAuthenticated.h"', registrar
        )

        sanny = ROOT / "sdk/Sanny Builder 4/data/sa_sbl_coopandreas"
        self.assertIn(
            "1D1E=0,coop_is_network_authenticated",
            (sanny / "SASCM.INI").read_text(encoding="utf-8"),
        )
        for filename in ("classes.db", "sa_coop.db"):
            self.assertIn(
                "IsNetworkAuthenticated,1D1E,1,0,()",
                (sanny / filename).read_text(encoding="utf-8"),
            )

        definitions = json.loads((sanny / "sa_coop.json").read_text(encoding="utf-8"))
        commands = [
            command
            for extension in definitions["extensions"]
            for command in extension.get("commands", [])
            if command.get("id") == "1D1E"
        ]
        self.assertEqual(len(commands), 1)
        command = commands[0]
        self.assertEqual(command["member"], "IsNetworkAuthenticated")
        self.assertEqual(command["num_params"], 0)
        self.assertTrue(command["attrs"]["is_condition"])

    def test_ambient_authority_preserves_offline_and_rejects_authenticated_peers(self):
        for name in ("BUY_PRO.txt", "BUY1.txt", "BUY2.txt", "BUY3.txt", "OPENUP.txt"):
            text = PROPERTY_FILES[name]
            helper = text.split("COOP_CAN_OWN_PROPERTY_STATE", 1)[1]
            with self.subTest(script=name):
                self.assertRegex(helper, r"0@\s*=\s*1")
                auth = helper.index("Coop.IsNetworkAuthenticated()")
                host = helper.index("Coop.IsHost()")
                peer = helper.index("COOP_AUTHENTICATED_PEER")
                deny = helper.index("0@ = 0", peer)
                self.assertLess(auth, host)
                self.assertLess(host, peer)
                self.assertLess(peer, deny)
                self.assertIn("goto_if_false", helper[auth:host])

    def test_peer_pickups_are_suppressed_before_engine_deduction(self):
        generic = PROPERTY_FILES["BUY_PRO.txt"]
        self.assertLess(
            generic.index("gosub @BUY_PRO_COOP_CAN_OWN_PROPERTY_STATE"),
            generic.index("Pickup.HasBeenCollected"),
        )
        suppression = generic.split(":BUY_PRO_COOP_SUPPRESS_PEER_PICKUP", 1)[1]
        self.assertIn(
            "Pickup.Remove($save_housepickup($save_house_index,32i))", suppression
        )

        for index, name in enumerate(("BUY1.txt", "BUY2.txt", "BUY3.txt")):
            text = PROPERTY_FILES[name]
            with self.subTest(script=name):
                first_auth = text.index("COOP_CAN_OWN_PROPERTY_STATE")
                first_for_sale = text.index("Pickup.CreateForSaleProperty")
                self.assertLess(first_auth, first_for_sale)
                suppression = text.split("COOP_SUPPRESS_PEER_PICKUP", 1)[1]
                self.assertIn(f"Pickup.Remove($save_housepickup[{index}])", suppression)
                self.assertIn("COOP_WAIT_FOR_AUTHORITY", suppression)
                self.assertLess(
                    text.index(f"gosub @{name.removesuffix('.txt')}_COOP_CAN_OWN_PROPERTY_STATE"),
                    text.index("Pickup.HasBeenCollected"),
                )

    def test_purchase_mission_is_frozen_session_host_authoritative(self):
        text = PROPERTY_FILES["BUYPRO1.txt"]
        first_mutation = text.index("$onmission = 1")
        self.assertLess(text.index("Coop.EnableSyncingThisScript()"), first_mutation)
        self.assertLess(text.index("Coop.IsNetworkAuthenticated()"), first_mutation)
        self.assertLess(text.index("Coop.IsHost()"), first_mutation)
        self.assertIn(
            "35@, 36@, 37@ = Coop.CollectNetworkPlayersForTheMission()", text
        )
        self.assertIn("@BUYPRO1_COOP_REJECT_PEER_LAUNCH", text[:first_mutation])

        reject = text.split(":BUYPRO1_COOP_REJECT_PEER_LAUNCH", 1)[1]
        for forbidden in (
            "Stat.IncrementInt",
            "Stat.PlayerMadeProgress",
            "Garage.Activate",
            "Blip.Add",
            "start_new_script",
        ):
            self.assertNotIn(forbidden, reject)

    def test_stock_progress_prices_and_save_layout_remain_single_copy(self):
        mission = PROPERTY_FILES["BUYPRO1.txt"]
        self.assertEqual(mission.count("Stat.IncrementInt("), 32)
        self.assertEqual(mission.count("Stat.PlayerMadeProgress(1)"), 32)
        self.assertEqual(mission.count("start_new_script @STEAL"), 1)
        self.assertEqual(mission.count("start_new_script @PSCH"), 1)
        self.assertNotIn("Player.AddScore", mission)

        initial = PROPERTY_FILES["INITIAL.txt"]
        pickup_creations = re.findall(
            r"Pickup\.Create(?:ForSale|Locked)Property", initial
        )
        self.assertEqual(len(pickup_creations), 32)
        for index in range(32):
            self.assertIn(f"$prorerty_switch[{index}] = {index}", initial)
            self.assertIn(f"$propertyX[{index}]", initial)
            self.assertIn(f"$propertyY[{index}]", initial)
            self.assertIn(f"$propertyZ[{index}]", initial)

        openup = PROPERTY_FILES["OPENUP.txt"]
        self.assertLess(
            openup.index("gosub @OPENUP_COOP_CAN_OWN_PROPERTY_STATE"),
            openup.index("$Return_cities_passed"),
        )
        self.assertEqual(openup.count("Pickup.CreateForSaleProperty"), 23)

    def test_existing_relays_publish_host_markers_and_entry_exit_unlocks(self):
        radar = (ROOT / "client/src/Hooks/RadarHooks.cpp").read_text(
            encoding="utf-8"
        )
        static_blips = (ROOT / "client/src/CNetworkStaticBlip.cpp").read_text(
            encoding="utf-8"
        )
        server_blips = (ROOT / "server/src/PacketHandlers/blips.cpp").read_text(
            encoding="utf-8"
        )
        opcode_sync = (ROOT / "client/src/COpCodeSync.cpp").read_text(
            encoding="utf-8"
        )

        self.assertIn("if (!CLocalPlayer::m_bIsHost)", radar)
        self.assertIn("ms_bNeedToSendAfterThisFrame = true", radar)
        self.assertIn("StaticBlipsSnapshot packet", static_blips)
        self.assertIn("HasMissionUiAuthority", server_blips)
        self.assertIn("SendToMissionRecipients", server_blips)
        self.assertIn("0x7FB", opcode_sync)
        self.assertIn("0x9B4", opcode_sync)
        self.assertGreaterEqual(
            PROPERTY_FILES["BUYPRO1.txt"].count("World.SetClosestEntryExitFlag"),
            30,
        )


if __name__ == "__main__":
    unittest.main()
