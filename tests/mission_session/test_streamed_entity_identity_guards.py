import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening_brace = source.index("{", start)
    depth = 0
    for index in range(opening_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening_brace : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


class StreamedEntityIdentityGuardTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.player_manager = (ROOT / "client/src/CNetworkPlayerManager.cpp").read_text(encoding="utf-8")
        cls.vehicle_manager = (ROOT / "client/src/CNetworkVehicleManager.cpp").read_text(encoding="utf-8")
        cls.player = (ROOT / "client/src/CNetworkPlayer.cpp").read_text(encoding="utf-8")
        cls.vehicle_hooks = (ROOT / "client/src/Hooks/VehicleHooks.cpp").read_text(encoding="utf-8")

    def test_null_native_entities_never_match_streamed_out_wrappers(self):
        player_lookup = function_body(
            self.player_manager, "CNetworkPlayer* CNetworkPlayerManager::GetPlayer(CEntity* entity)"
        )
        vehicle_lookup = function_body(
            self.vehicle_manager, "CNetworkVehicle* CNetworkVehicleManager::GetVehicle(CEntity* vehicle)"
        )

        for lookup in (player_lookup, vehicle_lookup):
            guard = re.search(r"if \([^\n]+ == nullptr\)\s*return nullptr;", lookup)
            self.assertIsNotNone(guard)
            self.assertLess(guard.start(), lookup.index("for ("))

    def test_streamed_out_player_cannot_match_an_unused_gta_player_slot(self):
        get_internal_id = function_body(self.player, "int CNetworkPlayer::GetInternalId()")
        guard = get_internal_id.index("if (m_pPed == nullptr)")
        self.assertLess(guard, get_internal_id.index("for ("))
        self.assertIn("return -1;", get_internal_id[guard : get_internal_id.index("for (")])

    def test_empty_vehicle_uses_native_control_without_remote_player_context(self):
        process = function_body(self.vehicle_hooks, "void __fastcall CVehicle__ProcessControl_Hook()")
        driver = process.index("CPed* driver = vehicle->m_pDriver;")
        null_guard = process.index("if (driver == nullptr)", driver)
        lookup = process.index("CNetworkPlayerManager::GetPlayer(driver)", null_guard)
        first_remote_dereference = process.index("player->m_pPed->m_fHealth", lookup)

        self.assertLess(null_guard, lookup)
        self.assertIn("player->m_pPed != driver", process[lookup:first_remote_dereference])
        self.assertIn("!driver->IsVTableValid()", process[lookup:first_remote_dereference])


if __name__ == "__main__":
    unittest.main()
