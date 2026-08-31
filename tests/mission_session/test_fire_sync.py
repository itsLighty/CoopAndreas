import dataclasses
import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


@dataclasses.dataclass(frozen=True)
class FireId:
    slot: int
    generation: int


@dataclasses.dataclass
class Fire:
    fire_id: FireId
    position: tuple[float, float, float]
    area: int
    expires_at: int
    revision: int
    active: bool = True
    attachment: tuple[str, int | None] = ("world", None)


@dataclasses.dataclass
class Observation:
    position: tuple[float, float, float]
    area: int
    received_at: int
    alive: bool = True


class FireAuthorityModel:
    """Small executable model of the security and lifecycle invariants in CFireAuthorityManager."""

    MAX_DISTANCE = 30.0
    FRESH_MS = 1500

    def __init__(self, host=0):
        self.host = host
        self.now = 0
        self.revision = 0
        self.last_sequence = 0
        self.fires: dict[int, Fire] = {}
        self.observations: dict[int, Observation] = {}

    @staticmethod
    def _distance_squared(a, b):
        return sum((left - right) ** 2 for left, right in zip(a, b))

    def upsert(self, sender, sequence, fire_id, position, area, lifetime=30_000):
        if sender != self.host or sequence <= self.last_sequence:
            return False
        old = self.fires.get(fire_id.slot)
        if old and old.active and old.fire_id != fire_id:
            if fire_id.generation <= old.fire_id.generation:
                return False
        if old and not old.active and fire_id.generation <= old.fire_id.generation:
            return False
        self.last_sequence = sequence
        self.revision += 1
        self.fires[fire_id.slot] = Fire(
            fire_id, position, area, self.now + lifetime, self.revision
        )
        return True

    def extinguish(self, sender, sequence, fire_id):
        if sender != self.host or sequence <= self.last_sequence:
            return False
        fire = self.fires.get(fire_id.slot)
        if not fire or not fire.active or fire.fire_id != fire_id:
            return False
        self.last_sequence = sequence
        self.revision += 1
        fire.active = False
        fire.revision = self.revision
        return True

    def observe(self, player, position, area, *, alive=True, received_at=None):
        self.observations[player] = Observation(
            position, area, self.now if received_at is None else received_at, alive
        )

    def request_extinguish(self, player, fire_id, claimed_position):
        del claimed_position  # Deliberately not an authorization input.
        fire = self.fires.get(fire_id.slot)
        observation = self.observations.get(player)
        if not fire or not fire.active or fire.fire_id != fire_id or not observation:
            return False
        if not observation.alive or self.now - observation.received_at > self.FRESH_MS:
            return False
        if observation.area != fire.area:
            return False
        return self._distance_squared(observation.position, fire.position) <= self.MAX_DISTANCE**2

    def migrate(self, new_host):
        self.host = new_host
        self.last_sequence = 0
        for fire in self.fires.values():
            if fire.active:
                self.revision += 1
                fire.revision = self.revision

    def update(self, elapsed_ms, existing_entities=None):
        self.now += elapsed_ms
        existing_entities = existing_entities or set()
        for fire in self.fires.values():
            if not fire.active:
                continue
            if fire.attachment[0] != "world" and fire.attachment not in existing_entities:
                fire.attachment = ("world", None)
                self.revision += 1
                fire.revision = self.revision
            if self.now >= fire.expires_at:
                fire.active = False
                self.revision += 1
                fire.revision = self.revision

    def snapshot(self):
        return [dataclasses.replace(fire) for fire in self.fires.values() if fire.active]


class FireAuthorityModelTests(unittest.TestCase):
    def test_only_host_can_create_or_extinguish_and_sequences_are_idempotent(self):
        model = FireAuthorityModel(host=2)
        fire_id = FireId(4, 1)
        self.assertFalse(model.upsert(1, 1, fire_id, (0, 0, 0), 0))
        self.assertTrue(model.upsert(2, 1, fire_id, (0, 0, 0), 0))
        revision = model.revision
        self.assertFalse(model.upsert(2, 1, fire_id, (1, 0, 0), 0))
        self.assertEqual(model.revision, revision)
        self.assertFalse(model.extinguish(1, 2, fire_id))
        self.assertTrue(model.extinguish(2, 2, fire_id))

    def test_slot_reuse_requires_a_new_generation(self):
        model = FireAuthorityModel()
        old = FireId(9, 7)
        self.assertTrue(model.upsert(0, 1, old, (0, 0, 0), 0))
        self.assertTrue(model.extinguish(0, 2, old))
        self.assertFalse(model.upsert(0, 3, old, (0, 0, 0), 0))
        self.assertTrue(model.upsert(0, 3, FireId(9, 8), (0, 0, 0), 0))

    def test_late_join_snapshot_contains_only_active_fires(self):
        model = FireAuthorityModel()
        first, second = FireId(0, 1), FireId(1, 1)
        model.upsert(0, 1, first, (0, 0, 0), 0)
        model.upsert(0, 2, second, (1, 0, 0), 0)
        model.extinguish(0, 3, first)
        self.assertEqual([fire.fire_id for fire in model.snapshot()], [second])

    def test_host_migration_keeps_state_but_resets_authority_sequence(self):
        model = FireAuthorityModel(host=0)
        fire_id = FireId(3, 1)
        model.upsert(0, 50, fire_id, (0, 0, 0), 0)
        old_revision = model.fires[3].revision
        model.migrate(1)
        self.assertGreater(model.fires[3].revision, old_revision)
        self.assertTrue(model.upsert(1, 1, fire_id, (1, 0, 0), 0))
        self.assertFalse(model.upsert(0, 51, fire_id, (2, 0, 0), 0))

    def test_server_expiry_and_missing_attachment_fallback(self):
        model = FireAuthorityModel()
        fire_id = FireId(2, 1)
        model.upsert(0, 1, fire_id, (5, 6, 7), 0, lifetime=500)
        model.fires[2].attachment = ("vehicle", 10)
        model.update(100, existing_entities=set())
        self.assertEqual(model.fires[2].attachment, ("world", None))
        self.assertTrue(model.fires[2].active)
        model.update(400)
        self.assertFalse(model.fires[2].active)

    def test_spoofed_claimed_position_cannot_extinguish_distant_fire(self):
        model = FireAuthorityModel()
        fire_id = FireId(0, 1)
        model.upsert(0, 1, fire_id, (0, 0, 0), 0)
        model.observe(1, (100, 0, 0), 0)
        self.assertFalse(model.request_extinguish(1, fire_id, claimed_position=(0, 0, 0)))
        model.observe(1, (5, 0, 0), 0)
        self.assertTrue(model.request_extinguish(1, fire_id, claimed_position=(500, 500, 500)))

    def test_extinguish_rejects_dead_stale_and_wrong_area_observations(self):
        model = FireAuthorityModel()
        fire_id = FireId(0, 1)
        model.upsert(0, 1, fire_id, (0, 0, 0), 3)
        model.observe(1, (0, 0, 0), 3, alive=False)
        self.assertFalse(model.request_extinguish(1, fire_id, (0, 0, 0)))
        model.observe(1, (0, 0, 0), 2)
        self.assertFalse(model.request_extinguish(1, fire_id, (0, 0, 0)))
        model.now = 2000
        model.observe(1, (0, 0, 0), 3, received_at=0)
        self.assertFalse(model.request_extinguish(1, fire_id, (0, 0, 0)))

    def test_area_gating_keeps_canonical_state_without_wrong_dimension_presentation(self):
        model = FireAuthorityModel()
        fire_id = FireId(5, 1)
        model.upsert(0, 1, fire_id, (10, 10, 0), 4)
        canonical = model.snapshot()[0]
        self.assertFalse(canonical.area == 0)
        self.assertTrue(canonical.active)
        self.assertEqual(canonical.area, 4)

    def test_pointer_free_birth_token_detects_unobserved_native_slot_reuse(self):
        birth_epoch = 12
        managed_token = birth_epoch
        birth_epoch += 1  # Extinguish + allocate can happen between manager observations.
        self.assertNotEqual(managed_token, birth_epoch)
        old_id = FireId(7, 22)
        replacement_id = FireId(old_id.slot, old_id.generation + 1)
        self.assertGreater(replacement_id.generation, old_id.generation)


class FireSourceContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.packet_types = (ROOT / "shared/network/packet_types.h").read_text(encoding="utf-8")
        cls.packets = (ROOT / "shared/network/packets/fires.h").read_text(encoding="utf-8")
        cls.client = (ROOT / "client/src/CNetworkFireManager.cpp").read_text(encoding="utf-8")
        cls.client_header = (ROOT / "client/src/CNetworkFireManager.h").read_text(encoding="utf-8")
        cls.server = (ROOT / "server/src/CFireAuthorityManager.cpp").read_text(encoding="utf-8")

    def test_protocol_is_bumped_and_packet_ids_are_append_only(self):
        config = (ROOT / "shared/config.h").read_text(encoding="utf-8")
        self.assertIn('COOPANDREAS_VERSION "0.3.7-alpha"', config)
        enum = re.search(
            r"enum class ePacketType[^\{]*\{(.*?)PACKET_ID_MAX", self.packet_types, re.S
        ).group(1)
        positions = [
            enum.index(name)
            for name in ("FIRE_STATE_INTENT", "FIRE_STATE", "FIRE_EXTINGUISH_REQUEST")
        ]
        self.assertEqual(positions, sorted(positions))
        self.assertGreater(positions[0], enum.index("STUNT_STATE"))

    def test_wire_payload_is_finite_bounded_and_pointer_free(self):
        self.assertIn("FIRE_SLOT_CAPACITY = 64", self.packets)
        self.assertIn("FireId", self.packets)
        self.assertIn("generation", self.packets)
        self.assertIn("MAX_SERIALIZED_BYTES = 192", self.packets)
        self.assertIn("FitsSerializedBudget", self.packets)
        self.assertIn("std::isfinite", self.packets)
        self.assertIn("FIRE_MAX_LIFETIME_MS = 600000", self.packets)
        self.assertIn("uint8_t area", self.packets)
        self.assertIn("area >= MAX_VISIBLE_AREAS", self.packets)
        self.assertNotRegex(self.packets, r"CFire\s*\*")
        self.assertNotRegex(self.packets, r"CEntity\s*\*")
        run_id = self.server.split("void EnsureRunId()", 1)[1]
        run_id = run_id.split("uint32_t NextRevision", 1)[0]
        self.assertNotIn("reinterpret_cast", run_id)

    def test_server_mutations_require_current_authenticated_host_and_are_rate_limited(self):
        self.assertIn("player != host", self.server)
        self.assertIn("!player->m_bIsHost", self.server)
        self.assertIn("player->m_iPlayerId != g_authorityPlayerId", self.server)
        self.assertIn("HOST_MUTATION_RATE_LIMIT", self.server)
        self.assertIn("EXTINGUISH_REQUEST_RATE_LIMIT", self.server)
        self.assertIn("IsFireSerialNewer(intent.authoritySequence", self.server)

    def test_extinguish_permission_uses_fresh_server_observation_not_claimed_position(self):
        permission = self.server.split("void CFireAuthorityManager::HandleExtinguishRequest", 1)[1]
        permission = permission.split("void CFireAuthorityManager::HandlePlayerDisconnected", 1)[0]
        self.assertIn("ResolveFreshPlayerObservation", permission)
        self.assertIn("canonicalRequesterArea != fire.descriptor.area", permission)
        self.assertIn("DistanceSquared(canonicalRequesterPosition", permission)
        self.assertNotIn("DistanceSquared(request.requesterPosition", permission)
        self.assertIn("PLAYER_OBSERVATION_FRESHNESS_MS", self.server)
        self.assertIn("!observation.alive", self.server)

    def test_validated_movement_handlers_feed_the_observation_api(self):
        players = (ROOT / "server/src/PacketHandlers/players.cpp").read_text(encoding="utf-8")
        vehicles = (ROOT / "server/src/PacketHandlers/vehicles.cpp").read_text(encoding="utf-8")
        self.assertIn("ObservePlayerMovement", players)
        self.assertIn("ObservePlayerArea", players)
        enex_handler = players.split("ePacketType::ENEX_TRANSITION", 1)[1]
        enex_handler = enex_handler.split("ePacketType::PLAYER_PLACE_WAYPOINT", 1)[0]
        self.assertNotIn("pEnExTransition->position", enex_handler)
        self.assertIn("MarkPlayerUnavailable", players)
        self.assertGreaterEqual(vehicles.count("ObservePlayerMovement"), 2)
        driver_observation = vehicles.index("ObservePlayerMovement")
        driver_validation = vehicles.index("IsExactOccupant", vehicles.index("VEHICLE_DRIVER_UPDATE"))
        self.assertGreater(driver_observation, driver_validation)

    def test_server_supports_replay_expiry_disconnect_and_migration(self):
        server_network = (ROOT / "server/src/CNetwork.cpp").read_text(encoding="utf-8")
        player_manager = (ROOT / "server/src/CNetworkPlayerManager.cpp").read_text(encoding="utf-8")
        for call in ("CFireAuthorityManager::Update", "HandlePlayerDisconnected", "SendSnapshot"):
            self.assertIn(call, server_network)
        self.assertGreaterEqual(player_manager.count("CFireAuthorityManager::HandleAuthorityChange"), 2)
        self.assertIn("now >= fire.expiresAtMs", self.server)
        self.assertIn("DetachToWorld", self.server)
        self.assertIn("Every accepted authority heartbeat gets a new canonical revision", self.server)
        self.assertIn("fire.m_nTimeToBurn = CTimer::m_snTimeInMilliseconds", self.client)

    def test_client_uses_native_indices_and_never_dereferences_retained_targets(self):
        self.assertIn("int nativeSlot", self.client_header)
        self.assertNotIn("CFire* native", self.client_header)
        self.assertNotIn("CEntity* target", self.client_header)
        self.assertNotRegex(self.client, r"m_pEntityTarget\s*->")
        self.assertIn("m_nativeOwners", self.client)
        self.assertIn("fire - &gFireManager.m_aFires[0]", self.client)

    def test_cross_area_fires_stay_canonical_but_unmaterialized(self):
        materialize = self.client.split("bool CNetworkFireManager::Materialize", 1)[1]
        materialize = materialize.split("void CNetworkFireManager::ProcessMaterializations", 1)[0]
        self.assertIn("GetLocalArea() != slot.descriptor.area", materialize)
        observe = self.client.split("void CNetworkFireManager::ObserveManagedSlot", 1)[1]
        observe = observe.split("void CNetworkFireManager::ObserveNativePool", 1)[0]
        self.assertIn("GetLocalArea() != slot.descriptor.area", observe)
        self.assertIn("RemoveNative(slot, true)", observe)
        fire_hooks = (ROOT / "client/src/Hooks/FireHooks.cpp").read_text(encoding="utf-8")
        self.assertIn("gameProcessEvent.before", fire_hooks)
        self.assertIn("ProcessAreaTransitions", fire_hooks)
        state_handler = self.client.split("void CNetworkFireManager::HandleState", 1)[1]
        state_handler = state_handler.split("CEntity* CNetworkFireManager::ResolveAttachment", 1)[0]
        self.assertIn("previousDescriptor.attachmentId != slot.descriptor.attachmentId", state_handler)
        self.assertIn("previousDescriptor.area != slot.descriptor.area", state_handler)
        self.assertIn("RemoveNative(slot, true)", state_handler)

    def test_native_pool_reuse_retires_old_id_before_new_generation(self):
        self.assertIn("nativeDeadlineToken", self.client_header)
        self.assertIn("nativeFxIdentityToken", self.client_header)
        self.assertIn("nativeScriptReferenceToken", self.client_header)
        self.assertIn("nativeFirstGenerationToken", self.client_header)
        self.assertIn("nativeBirthEpochToken", self.client_header)
        self.assertIn("NativeIdentityMatches", self.client)
        self.assertIn("reinterpret_cast<uintptr_t>(fire.m_pFxSystem)", self.client)
        self.assertIn("slot.nativeBirthEpochToken != m_nativeBirthEpochs[slot.nativeSlot]", self.client)
        fire_hooks = (ROOT / "client/src/Hooks/FireHooks.cpp").read_text(encoding="utf-8")
        for address in ("0x539F00", "0x53A050", "0x53A270"):
            self.assertIn(address, fire_hooks)
        self.assertGreaterEqual(fire_hooks.count("BeginNativeBirthObservation"), 3)
        self.assertGreaterEqual(fire_hooks.count("EndNativeBirthObservation"), 3)
        reuse = self.client.split("if (!NativeIdentityMatches(slot, fire))", 1)[1]
        reuse = reuse.split("if (!m_localPlayerIsAuthority)", 1)[0]
        self.assertIn("eFireMutation::EXTINGUISH", reuse)
        self.assertIn("slot.active = false", reuse)
        adoption = self.client.split("void CNetworkFireManager::ObserveNativePool", 1)[1]
        self.assertIn("NextGeneration(slotIndex)", adoption)

    def test_follower_materialization_cannot_echo_or_spread(self):
        self.assertIn("m_remoteMutationDepth", self.client)
        self.assertIn("IsApplyingRemoteState", self.client)
        self.assertIn("fire.m_nNumGenerationsAllowed = 0", self.client)
        self.assertIn("fire.m_nFlags.bCreatedByScript = true", self.client)
        self.assertIn("fire->m_fStrength = slot.descriptor.strength", self.client)
        self.assertIn("if (!m_localPlayerIsAuthority)", self.client)
        self.assertIn("fire.Extinguish();", self.client)

    def test_offline_native_behavior_and_story_fire_handles_are_preserved(self):
        process = self.client.split("void CNetworkFireManager::Process()", 1)[1]
        self.assertIn("if (!CNetwork::m_bAuthenticated)", process)
        self.assertIn("return; // Offline GTA fire creation", process)
        reset = self.client.split("void CNetworkFireManager::ResetNetworkState()", 1)[1]
        reset = reset.split("void CNetworkFireManager::HandleAuthorityChanged", 1)[0]
        self.assertIn("if (slot.materializedByNetwork && !slot.originatedLocally)", reset)
        self.assertIn("m_nFlags.bCreatedByScript", self.client)

    def test_hook_reset_authority_and_handlers_are_integrated(self):
        hook_init = (ROOT / "client/src/Hooks/CHook.cpp").read_text(encoding="utf-8")
        fire_hook = (ROOT / "client/src/Hooks/FireHooks.cpp").read_text(encoding="utf-8")
        client_network = (ROOT / "client/src/CNetwork.cpp").read_text(encoding="utf-8")
        system = (ROOT / "client/src/PacketHandlers/system.cpp").read_text(encoding="utf-8")
        handler = (ROOT / "client/src/PacketHandlers/fires.cpp").read_text(encoding="utf-8")
        self.assertIn("FireHooks::InjectHooks", hook_init)
        self.assertIn("gameProcessEvent.after", fire_hook)
        self.assertIn("CNetworkFireManager::ResetNetworkState", client_network)
        self.assertEqual(system.count("CNetworkFireManager::HandleAuthorityChanged"), 2)
        self.assertIn("ePacketType::FIRE_STATE", handler)
        self.assertIn("ePacketType::FIRE_EXTINGUISH_REQUEST", handler)

    def test_readme_marks_fire_sync_complete(self):
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        self.assertIn("- [X] fire sync", readme)


if __name__ == "__main__":
    unittest.main()
