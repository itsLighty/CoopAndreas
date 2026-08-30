import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class OfflineScriptAuthorityTests(unittest.TestCase):
    def test_is_host_treats_offline_play_as_local_authority(self):
        source = (
            ROOT / "client/src/Commands/Commands/CCommandIsHost.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn('#include "CNetwork.h"', source)
        self.assertIn(
            "!CNetwork::m_bAuthenticated || CLocalPlayer::m_bIsHost", source
        )
        self.assertNotIn(
            "UpdateCompareFlag(CLocalPlayer::m_bIsHost);", source
        )

    def test_every_direct_host_gated_script_inherits_offline_authority(self):
        direct_gate = re.compile(
            r"if\s*Coop\.IsHost\(\)\s*"
            r"goto_if_false\s+@(?P<reject>[A-Z0-9_]+_COOP_REJECT_(?:NON_HOST|PEER_LAUNCH))"
        )
        gated_scripts = []
        for path in (ROOT / "scm/scripts").glob("*.txt"):
            text = path.read_text(encoding="utf-8")
            match = direct_gate.search(text)
            if match is None:
                continue
            self.assertIn("Coop.EnableSyncingThisScript()", text, path.name)
            gated_scripts.append(path.name)
            self.assertIn(f":{match.group('reject')}", text, path.name)
            self.assertIn("terminate_this_script", text[text.index(f":{match.group('reject')}"):], path.name)

        # This guard protects submissions and optional activities added after
        # the story-mission pass; a disappearing match means their authority
        # convention changed and must be reviewed deliberately.
        self.assertGreaterEqual(len(gated_scripts), 10)
        for expected in (
            "TAXIODD.txt",
            "FIRETRU.txt",
            "COPCAR.txt",
            "AMBULAN.txt",
            "PIMP.txt",
            "FREIGHT.txt",
            "STUNT.txt",
            "MTBIKER.txt",
            "BCOUR.txt",
        ):
            self.assertIn(expected, gated_scripts)


if __name__ == "__main__":
    unittest.main()
