import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


def braced_body(source: str, opening_brace: int, description: str) -> str:
    depth = 0
    for index in range(opening_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening_brace + 1 : index]
    raise AssertionError(f"unterminated {description}")


def network_player_methods(source: str) -> dict[str, str]:
    methods = {}
    signatures = re.finditer(
        r"(?:^|\n)[\w:<>&*\s]+\bCNetworkPlayer::(\w+)\s*\([^;]*?\)\s*(?:const\s*)?\{",
        source,
        re.M,
    )
    for signature in signatures:
        opening_brace = source.find("{", signature.start())
        methods[signature.group(1)] = braced_body(
            source, opening_brace, f"CNetworkPlayer::{signature.group(1)}"
        )
    return methods


def packet_handler_body(source: str, packet_type: str) -> str:
    signature = re.search(
        rf"PACKET_HANDLER\(ePacketType::{packet_type}\s*,", source
    )
    if signature is None:
        raise AssertionError(f"missing handler for {packet_type}")
    opening_brace = source.find("{", signature.end())
    return braced_body(source, opening_brace, f"{packet_type} packet handler")


def called_network_player_methods(body: str, known_methods: set[str]) -> set[str]:
    calls = set(re.findall(r"(?:->|\.|\b)([A-Za-z_]\w*)\s*\(", body))
    return calls & known_methods


class ReconnectClothesRebuildTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = (ROOT / "client/src/CNetworkPlayer.h").read_text(
            encoding="utf-8"
        )
        cls.source = (ROOT / "client/src/CNetworkPlayer.cpp").read_text(
            encoding="utf-8"
        )
        cls.handlers = (ROOT / "client/src/PacketHandlers/players.cpp").read_text(
            encoding="utf-8"
        )
        cls.methods = network_player_methods(cls.source)
        cls.create_ped = cls.methods["CreatePed"]
        cls.process_pending = cls.methods["ProcessPendingPresentation"]
        cls.rebuild_packet = packet_handler_body(cls.handlers, "REBUILD_PLAYER")

    def queue_methods_reached_from(self, body: str) -> list[str]:
        queue_methods = []
        if "m_bNeedsClothesRebuild = true" in body:
            queue_methods.append("<inline>")
        for name in called_network_player_methods(body, set(self.methods)):
            candidate = self.methods[name]
            if "m_bNeedsClothesRebuild = true" in candidate:
                queue_methods.append(name)
        return queue_methods

    def rebuild_methods_reached_from(self, root_name: str) -> set[str]:
        reached = set()
        work = [root_name]
        while work:
            name = work.pop()
            if name in reached:
                continue
            reached.add(name)
            work.extend(
                called_network_player_methods(self.methods[name], set(self.methods))
                - reached
            )
        return {
            name
            for name in reached
            if "CClothes::RebuildPlayer" in self.methods[name]
        }

    def test_create_ped_queues_instead_of_rebuilding_synchronously(self):
        self.assertNotIn("CClothes::RebuildPlayer", self.create_ped)
        self.assertNotIn("m_bNeedsClothesRebuild = false", self.create_ped)
        self.assertTrue(
            self.queue_methods_reached_from(self.create_ped),
            "CreatePed must queue a deferred clothes rebuild",
        )

    def test_rebuild_packet_only_updates_state_and_queues_work(self):
        self.assertIn("m_pPedClothesDesc =", self.rebuild_packet)
        self.assertNotIn("CClothes::RebuildPlayer", self.rebuild_packet)
        self.assertNotIn("m_pPlayerData", self.rebuild_packet)
        self.assertNotIn("m_bNeedsClothesRebuild = false", self.rebuild_packet)
        self.assertTrue(
            self.queue_methods_reached_from(self.rebuild_packet),
            "REBUILD_PLAYER must leave rendering work to the deferred player path",
        )

    def test_pending_presentation_retries_the_single_native_rebuild_path(self):
        rebuild_methods = {
            name
            for name, body in self.methods.items()
            if "CClothes::RebuildPlayer" in body
        }
        self.assertEqual(
            len(rebuild_methods),
            1,
            "the native clothes rebuild must have one guarded call site",
        )
        self.assertEqual(
            self.rebuild_methods_reached_from("ProcessPendingPresentation"),
            rebuild_methods,
            "ProcessPendingPresentation must retry the deferred rebuild",
        )

    def test_native_rebuild_is_guarded_against_the_reconnect_crash_inputs(self):
        rebuild_name = next(
            name
            for name, body in self.methods.items()
            if "CClothes::RebuildPlayer" in body
        )
        rebuild = self.methods[rebuild_name]

        required_preconditions = (
            "m_bNeedsClothesRebuild",
            "m_pPed",
            "IsVTableValid",
            "m_pPlayerData",
            "m_pPlayerData->m_pPedClothesDesc",
            "m_pRwClump",
            "CTxdStore::ms_pTxdPool",
            'FindTxdSlot("player")',
            "m_pRwDictionary",
        )
        call_at = rebuild.index("CClothes::RebuildPlayer")
        before_call = rebuild[:call_at]
        for precondition in required_preconditions:
            self.assertIn(
                precondition,
                before_call,
                f"{rebuild_name} must check {precondition} before rebuilding",
            )

    def test_rebuild_waits_for_a_nonzero_readiness_delay(self):
        rebuild_name = next(
            name
            for name, body in self.methods.items()
            if "CClothes::RebuildPlayer" in body
        )
        rebuild = self.methods[rebuild_name]
        queue_bodies = [
            body
            for body in self.methods.values()
            if "m_bNeedsClothesRebuild = true" in body
        ]
        timing_code = "\n".join(queue_bodies + [rebuild, self.header, self.source])

        self.assertIn(
            "GetTickCount()",
            "\n".join(queue_bodies + [rebuild]),
            "queue/retry logic must use a monotonic readiness time",
        )
        named_delays = re.findall(
            r"\b(\w*(?:CLOTHES|Clothes)\w*(?:DELAY|Delay|READY|Ready|RETRY|Retry)\w*)"
            r"\s*=\s*([1-9]\d*)[uUlL]*\b",
            timing_code,
        )
        inline_delay = re.search(
            r"GetTickCount\(\)[^;\n]{0,160}[+\-<>=][^;\n]{0,160}\b[1-9]\d*[uUlL]*\b",
            "\n".join(queue_bodies + [rebuild]),
        )
        self.assertTrue(
            named_delays or inline_delay,
            "deferred rebuilding must enforce a named or inline nonzero delay",
        )

    def test_pending_flag_is_cleared_only_after_a_successful_attempt(self):
        rebuild_name = next(
            name
            for name, body in self.methods.items()
            if "CClothes::RebuildPlayer" in body
        )
        rebuild = self.methods[rebuild_name]
        call_at = rebuild.index("CClothes::RebuildPlayer")
        clear_positions = [
            match.start()
            for match in re.finditer(
                r"m_bNeedsClothesRebuild\s*=\s*false", rebuild
            )
        ]
        self.assertEqual(
            len(clear_positions),
            1,
            "the retry path must have one success-only pending clear",
        )
        self.assertGreater(
            clear_positions[0],
            call_at,
            "failed readiness checks must leave the rebuild pending",
        )


if __name__ == "__main__":
    unittest.main()
