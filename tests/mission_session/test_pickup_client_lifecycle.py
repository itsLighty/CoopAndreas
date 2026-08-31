import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class PickupClientModel:
    """Small state model for client authority/grant and lifecycle invariants."""

    def __init__(self):
        self.authenticated = False
        self.is_host = False
        self.authority_ready = False
        self.epoch = 0
        self.generations = [0] * 620
        self.pickups = {}
        self.grants = [0] * 8
        self.script_collected = set()

    def process(self):
        if not self.authenticated:
            return "stock"
        if self.is_host:
            return "authority" if self.authority_ready else "waiting"
        return "peer-masked"

    def snapshot(self, epoch, authority, entries, local_player):
        self.epoch = epoch
        retained = set()
        for entry in entries:
            slot, generation = entry["identity"]
            self.generations[slot] = generation
            if entry["lifecycle"] == "removed":
                continue
            retained.add(entry["identity"])
            self.pickups[entry["identity"]] = dict(entry)
        self.pickups = {identity: value for identity, value in self.pickups.items() if identity in retained}
        self.authority_ready = self.is_host and authority == local_player

    def result(self, claimant, local_player, identity, revision, lifecycle):
        item = self.pickups.get(identity)
        if item is None or revision <= item["revision"]:
            return False
        if claimant == local_player and not self.is_host:
            self.grants[local_player] += 1
        elif claimant != local_player and not self.is_host:
            self.script_collected.add(identity)
        item["revision"] = revision
        item["lifecycle"] = lifecycle
        if lifecycle == "removed":
            del self.pickups[identity]
        return True


class TagAuthorityModel:
    def __init__(self):
        self.tags = {}

    def update(self, host, position, alpha, fully=False):
        if not host and position not in self.tags:
            return False
        old = self.tags.get(position, 0)
        if alpha <= old:
            return False
        if not host:
            if fully and old < 224:
                return False
            if not fully and alpha - old > 32:
                return False
        self.tags[position] = 255 if fully else alpha
        return True

    def full_snapshot(self, host, values):
        if not host:
            return False
        for position, alpha in values.items():
            if alpha < self.tags.get(position, 0):
                return False
        self.tags.update(values)
        return True


def descriptor(identity=(4, 7), revision=1, lifecycle="active", pickup_type=2, model=1242, area=0):
    return {
        "identity": identity,
        "revision": revision,
        "lifecycle": lifecycle,
        "type": pickup_type,
        "model": model,
        "area": area,
    }


class PickupClientLifecycleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.client = (ROOT / "client/src/CNetworkPickupManager.cpp").read_text(encoding="utf-8")
        cls.client_header = (ROOT / "client/src/CNetworkPickupManager.h").read_text(encoding="utf-8")
        cls.client_handlers = (ROOT / "client/src/PacketHandlers/pickups.cpp").read_text(encoding="utf-8")
        cls.world_hooks = (ROOT / "client/src/Hooks/WorldHooks.cpp").read_text(encoding="utf-8")
        cls.network = (ROOT / "client/src/CNetwork.cpp").read_text(encoding="utf-8")
        cls.system = (ROOT / "client/src/PacketHandlers/system.cpp").read_text(encoding="utf-8")
        cls.util = (ROOT / "client/src/CUtil.cpp").read_text(encoding="utf-8")
        cls.packets = (ROOT / "shared/network/packets/pickups.h").read_text(encoding="utf-8")
        cls.server = (ROOT / "server/src/CPickupAuthorityManager.cpp").read_text(encoding="utf-8")
        cls.tags = (ROOT / "server/src/CTagAuthorityManager.cpp").read_text(encoding="utf-8")
        cls.server_network = (ROOT / "server/src/CNetwork.cpp").read_text(encoding="utf-8")

    def test_offline_path_does_not_mask_or_replace_stock_pickups(self):
        process = re.search(r"void CNetworkPickupManager::Process\(\).*?^}", self.client, re.S | re.M).group(0)
        self.assertRegex(process, r"if \(!CNetwork::m_bAuthenticated\)\s+return;")
        self.assertNotIn("CPickups::", process.split("return;", 1)[0])
        model = PickupClientModel()
        self.assertEqual(model.process(), "stock")

    def test_host_is_the_only_native_authority_and_waits_for_migration_snapshot(self):
        process = re.search(r"void CNetworkPickupManager::Process\(\).*?^}", self.client, re.S | re.M).group(0)
        self.assertIn("if (CLocalPlayer::m_bIsHost)", process)
        self.assertIn("if (ms_authorityReady)\n            ScanAuthorityPool();\n        return;", process)
        self.assertIn("if (localIsAuthority && ms_authorityReady)", self.client)
        model = PickupClientModel()
        model.authenticated = True
        model.is_host = True
        self.assertEqual(model.process(), "waiting")
        model.snapshot(3, authority=2, entries=[], local_player=2)
        self.assertEqual(model.process(), "authority")

    def test_peer_pickups_are_masked_before_stock_and_same_frame_script_updates(self):
        self.assertIn("PrepareForNativePickupUpdate", self.client_header)
        self.assertIn("ms_authorityEpoch == 0 ? 1 : ms_authorityEpoch", self.client)
        self.assertIn("Events::gameProcessEvent.before", self.world_hooks)
        self.assertIn("Events::processScriptsEvent.after", self.world_hooks)
        self.assertIn("CNetwork::m_bAuthenticated && !CLocalPlayer::m_bIsHost", self.client)

    def test_native_grant_runs_only_for_authority_validation_or_the_approved_claimant(self):
        update_calls = re.findall(r"\bnative\.Update\(", self.client)
        self.assertEqual(len(update_calls), 2)
        self.assertIn("ValidateAndCollectForPlayer", self.client)
        self.assertIn("ExecuteLocalGrant", self.client)
        grant_body = re.search(r"bool CNetworkPickupManager::ExecuteLocalGrant.*?^}", self.client,
                               re.S | re.M).group(0)
        for forbidden in ("GiveWeapon", "AddMoney", "m_nMoney +=", "m_nMoney ="):
            self.assertNotIn(forbidden, grant_body)

        model = PickupClientModel()
        model.authenticated = True
        identity = (9, 3)
        model.pickups[identity] = descriptor(identity=identity)
        self.assertTrue(model.result(4, 4, identity, 2, "removed"))
        self.assertFalse(model.result(4, 4, identity, 2, "removed"))
        self.assertEqual(model.grants[4], 1)

    def test_nonclaiming_peers_never_grant_but_receive_idempotent_script_progress(self):
        model = PickupClientModel()
        model.authenticated = True
        identity = (11, 5)
        model.pickups[identity] = descriptor(identity=identity)
        self.assertTrue(model.result(3, 6, identity, 2, "disabled"))
        self.assertEqual(model.grants[6], 0)
        self.assertEqual(model.script_collected, {identity})
        self.assertFalse(model.result(3, 6, identity, 2, "disabled"))

    def test_expiry_respawn_static_drop_interior_and_death_guards_use_stock_state(self):
        for evidence in (
            "GetRegenerationRemaining(native)",
            "ePickupLifecycle::DISABLED",
            "ePickupLifecycle::ACTIVE && !native.m_pObject",
            "native.GiveUsAPickUpObject",
            "native.m_pObject->m_nAreaCode = pickup.areaCode",
            "player->m_fHealth <= 0.0f",
            "ped->m_fHealth <= 0.0f",
            "ped->m_nAreaCode != localPickup.pickup.areaCode",
            "native.m_nPickupType == PICKUP_NONE",
        ):
            self.assertIn(evidence, self.client)
        self.assertIn("ePickupRemovalReason::SCRIPT", self.client)
        self.assertIn("regenerationRemainingMs", self.server)

    def test_late_join_tombstones_preserve_wrapping_slot_generation(self):
        for evidence in (
            "Removed records are bounded tombstones",
            "if (record.occupied)",
            "pickup.lifecycle == ePickupLifecycle::REMOVED",
            "ms_generations[pickup.identity.slot] = pickup.identity.generation",
            "NextGeneration(ms_generations[slot])",
        ):
            self.assertIn(evidence, self.server + self.client)
        model = PickupClientModel()
        model.is_host = True
        tombstone = descriptor(identity=(17, 65535), revision=9, lifecycle="removed")
        model.snapshot(8, authority=1, entries=[tombstone], local_player=1)
        self.assertNotIn((17, 65535), model.pickups)
        self.assertEqual(model.generations[17], 65535)
        self.assertEqual((model.generations[17] + 1) & 0xFFFF, 0)
        self.assertIn("return generation == 0 ? 1 : generation;", self.client)

    def test_snapshot_cross_chunk_data_and_total_bytes_are_bounded(self):
        for evidence in (
            "MAX_SNAPSHOT_ENTRIES = 64",
            "MAX_SNAPSHOT_CHUNKS =",
            "MAX_SNAPSHOT_BYTES = 8 * 1024",
            "MAX_SNAPSHOT_TOTAL_BYTES = 24 * 1024",
            "packet.authorityPlayerId != ms_snapshotAuthorityPlayerId",
            "ms_collectibleProgress != packet.collectibleProgress",
            "identity.slot == packet.entries[incoming].identity.slot",
            "totalSnapshotBytes > MAX_SNAPSHOT_TOTAL_BYTES",
        ):
            self.assertIn(evidence, self.packets + self.client + self.server)

    def test_disconnect_full_reset_and_all_eight_client_handlers_are_wired(self):
        self.assertIn("CNetworkPickupManager::ResetNetworkState();", self.network)
        self.assertIn("CNetworkPickupManager::Process();", self.world_hooks)
        self.assertEqual(self.client_handlers.count("PACKET_HANDLER("), 8)
        self.assertEqual(self.system.count("CNetworkPickupManager::HandleAuthorityChanged"), 2)
        self.assertIn("CPickupAuthorityManager::HandlePlayerDisconnected", self.server_network)
        self.assertIn("CPickupAuthorityManager::HandleAuthorityChanged(nullptr)",
                      (ROOT / "server/src/CNetworkPlayerManager.cpp").read_text(encoding="utf-8"))
        for evidence in ("networkGenerated", "native.m_nPickupType = local.pickup.pickupType", "ms_authorityEpoch = 0"):
            self.assertIn(evidence, self.client)

    def test_jetpack_drop_has_one_authoritative_pickup_and_no_manual_reward_path(self):
        jetpack_body = re.search(r"void CUtil::SetPlayerJetpack\(.*?^}", self.util, re.S | re.M).group(0)
        self.assertIn("CNetwork::m_bAuthenticated && !CLocalPlayer::m_bIsHost", jetpack_body)
        self.assertIn("task->m_bIsFinished = true", jetpack_body)
        self.assertIn("CTaskSimpleJetPack::DropJetPack", jetpack_body)
        self.assertNotIn("GiveWeapon", jetpack_body)
        self.assertNotIn("m_nMoney", jetpack_body)
        self.assertIn("pickup.modelId", self.client)
        self.assertIn("native.Update", self.client)

    def test_remote_validation_rolls_back_player_zero_globals_and_result_uses_full_state(self):
        for evidence in (
            "ScopedLocalRewardRollback",
            "CStats::StatTypesInt",
            "CStats::StatTypesFloat",
            "info.m_nMoney = m_money",
            "std::memcpy(local->GetWanted()",
            "decision.resolvedPickup",
            "local.pickup = packet.pickup",
            "CPickups::AddToCollectedPickupsArray(localSlot)",
        ):
            self.assertIn(evidence, self.client)
        self.assertNotIn("CPickups::AddToCollectedPickupsArray(local.nativeHandle)", self.client)

    def test_snapshot_reward_uses_stock_picture_path_only_after_authority_result(self):
        execute = re.search(r"bool CNetworkPickupManager::ExecuteLocalGrant.*?^}", self.client, re.S | re.M).group(0)
        validate = re.search(r"bool CNetworkPickupManager::ValidateAndCollectForPlayer.*?^}", self.client,
                             re.S | re.M).group(0)
        self.assertIn("CPickups::PictureTaken();", execute)
        self.assertIn("native.Remove();", validate)
        self.assertIn("claimantIsLocal && !authorityAlreadyApplied", self.client)

    def test_collectible_progress_is_imported_from_host_applied_and_revisioned(self):
        for evidence in (
            "MergeLocalCollectibleProgress", "ApplyCollectibleProgress", "PublishCollectibleProgress",
            "STAT_HORSESHOES_COLLECTED", "STAT_SNAPSHOTS_TAKEN", "STAT_OYSTERS_COLLECTED",
            "ms_progressPublishedEpoch == ms_authorityEpoch", "progress.snapshotRevision = ms_snapshotRevision",
        ):
            self.assertIn(evidence, self.client)

    def test_participant_tag_progress_is_monotonic_and_cannot_overwrite_full_table(self):
        for evidence in (
            "MAX_NON_HOST_ALPHA_STEP = 32",
            "packet.payload.bFullySprayed && canonical.alpha < 224",
            "if (!IsCurrentHost(sender) || packet.payload.alpha == 0)",
            "ms_validTags[empty]",
            "ms_snapshot.tags[empty].alpha = 0",
            "if (!IsCurrentHost(sender))",
            "if (!IsCurrentHost(sender))"  # both update and full snapshot paths
        ):
            self.assertIn(evidence, self.tags)
        self.assertGreaterEqual(self.tags.count("IsCurrentHost(sender)"), 3)

        model = TagAuthorityModel()
        tag = (100, 200, 20)
        spoof = (999, 999, 999)
        self.assertFalse(model.update(False, spoof, 24))
        self.assertTrue(model.full_snapshot(True, {tag: 0}))
        self.assertTrue(model.update(False, tag, 24))
        self.assertFalse(model.update(False, tag, 80))
        self.assertFalse(model.update(False, tag, 32, fully=True))
        for alpha in (48, 80, 112, 144, 176, 208, 224):
            self.assertTrue(model.update(False, tag, alpha))
        self.assertTrue(model.update(False, tag, 255, fully=True))
        self.assertFalse(model.full_snapshot(False, {tag: 0}))
        self.assertFalse(model.full_snapshot(True, {tag: 0}))
        self.assertTrue(model.full_snapshot(True, {tag: 255}))


if __name__ == "__main__":
    unittest.main()
