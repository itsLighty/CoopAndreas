import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class LinuxServerBuildSupportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.xmake = (ROOT / "xmake.lua").read_text(encoding="utf-8")
        cls.readme = (ROOT / "README.md").read_text(encoding="utf-8")

    def test_linux_configuration_is_explicitly_32_bit(self):
        self.assertRegex(
            self.xmake,
            r'if is_plat\("linux"\) then\s+.*?set_arch\("i386"\)',
        )
        self.assertIn('add_defines("_GNU_SOURCE")', self.xmake)

    def test_windows_only_targets_are_disabled_outside_windows(self):
        for target in ("client", "proxy", "plugin_sa", "discordrpc", "launcher"):
            target_body = re.search(
                rf'target\("{target}".*?\nend\)', self.xmake, flags=re.DOTALL
            )
            self.assertIsNotNone(target_body, target)
            self.assertIn('if not is_plat("windows") then', target_body.group(0))
            self.assertIn("set_enabled(false)", target_body.group(0))

    def test_windows_server_settings_are_preserved(self):
        self.assertIn('add_files("server/version.rc")', self.xmake)
        self.assertIn('add_syslinks("bcrypt")', self.xmake)
        self.assertIn('"_CRT_SECURE_NO_WARNINGS", "WIN32", "_CONSOLE"', self.xmake)
        self.assertIn('set_pcxxheader("server/src/stdafx.h")', self.xmake)

    def test_linux_server_keeps_header_contract_without_a_pch_artifact(self):
        self.assertIn(
            'add_cxxflags("-include", "server/src/stdafx.h", {force = true})',
            self.xmake,
        )
        self.assertIn('"server/compat/plugin-sdk"', self.xmake)

        plugin_base = (ROOT / "server/compat/plugin-sdk/PluginBase.h").read_text(
            encoding="utf-8"
        )
        self.assertNotIn("windows.h", plugin_base)
        self.assertNotRegex(plugin_base, r'#include\s+[<"]injector')

    def test_packet_registration_does_not_depend_on_msvc_name_lookup(self):
        packet = (ROOT / "shared/network/packet.h").read_text(encoding="utf-8")
        self.assertIn("void RegisterPacketPrototype(Packet* packet);", packet)
        self.assertIn("RegisterPacketPrototype(new PacketT);", packet)

        for factory in ("client/src/CPacketFactory.h", "server/src/CPacketFactory.h"):
            contents = (ROOT / factory).read_text(encoding="utf-8")
            self.assertIn("inline void RegisterPacketPrototype(Packet* packet)", contents)
            self.assertIn("GetPacketFactory().RegisterPacket(packet);", contents)

    def test_server_sources_use_portable_apis(self):
        server_time = (ROOT / "server/src/CServerTime.cpp").read_text(encoding="utf-8")
        network = (ROOT / "server/src/CNetwork.cpp").read_text(encoding="utf-8")
        main = (ROOT / "server/src/main.cpp").read_text(encoding="utf-8")

        self.assertIn("std::chrono::steady_clock", server_time)
        self.assertIn("return GetTickCount();", server_time)
        self.assertNotIn("strcpy_s(", network)
        self.assertIn('readlink("/proc/self/exe"', main)
        banner_source = main.split('printf("[!] : CoopAndreas Server', 1)[1]
        banner = re.search(
            r"#if defined\(_WIN32\)(?P<windows>.*?)#else(?P<linux>.*?)#endif",
            banner_source,
            flags=re.DOTALL,
        )
        self.assertIsNotNone(banner)
        self.assertIn("__DATE__, __TIME__", banner.group("windows"))
        self.assertNotIn("__DATE__", banner.group("linux"))

    def test_server_link_portability_regressions_are_covered(self):
        player = (ROOT / "server/src/CNetworkPlayer.h").read_text(encoding="utf-8")
        pickups = (ROOT / "server/src/CPickupAuthorityManager.cpp").read_text(
            encoding="utf-8"
        )
        config = (ROOT / "server/src/ConfigManager.h").read_text(encoding="utf-8")

        self.assertIn("~CNetworkPlayer() = default;", player)
        self.assertIn("!(pending->id == packet.id)", pickups)
        self.assertIn('#include "INIReader/cpp/INIReader.h"', config)

    def test_readme_has_reproducible_linux_commands(self):
        section = self.readme.split("## Building Server on GNU/Linux", 1)[1].split(
            "## Donate", 1
        )[0]
        self.assertNotIn("TODO", section)
        self.assertIn("gcc-multilib", section)
        self.assertIn("g++-multilib", section)
        self.assertIn("xmake f -c -p linux -a i386 -m release", section)
        self.assertIn("xmake build server", section)
        self.assertIn("build/linux/i386/release/server", section)


if __name__ == "__main__":
    unittest.main()
