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


class FollowerCreationModel:
    """Executable model for follower request allocation, validation, and adoption identity."""

    CAPACITY = 2
    MAX_DISTANCE = 30.0

    def __init__(self):
        self.generations = [0] * self.CAPACITY
        self.active: dict[int, tuple[FireId, int, int, tuple[str, int | None]]] = {}
        self.last_request: dict[int, tuple[int, FireId]] = {}
        self.authoritative_scripts: dict[tuple, FireId] = {}

    def add_host_script(self, *, fire_position=(0.0, 0.0, 0.0), fire_area=0, attachment=("world", None)):
        slot = next(slot for slot in range(self.CAPACITY) if slot not in self.active)
        self.generations[slot] += 1
        fire_id = FireId(slot, self.generations[slot])
        self.active[slot] = (fire_id, 0, 0, attachment)
        self.authoritative_scripts[(fire_position, fire_area, attachment)] = fire_id
        return fire_id

    def request(
        self,
        *,
        player: int,
        request_id: int,
        player_position=(0.0, 0.0, 0.0),
        player_area=0,
        fire_position=(0.0, 0.0, 0.0),
        fire_area=0,
        attachment=("world", None),
        weapon="molotov",
        vehicle=None,
        vehicle_syncer=None,
        ped_syncer=None,
        created_by_script=False,
    ):
        previous = self.last_request.get(player)
        if previous and previous[0] == request_id:
            return previous[1]
        if previous and request_id <= previous[0]:
            return None
        if created_by_script:
            matched = self.authoritative_scripts.get((fire_position, fire_area, attachment))
            if matched is not None:
                self.last_request[player] = (request_id, matched)
            return matched
        if fire_area != player_area:
            return None
        if sum((a - b) ** 2 for a, b in zip(player_position, fire_position)) > self.MAX_DISTANCE**2:
            return None
        kind, target = attachment
        if kind == "world" and weapon not in {"molotov", "flamethrower"}:
            return None
        if kind == "player" and target != player:
            return None
        if kind == "vehicle" and target != vehicle and player != vehicle_syncer:
            return None
        if kind == "ped" and (target is None or ped_syncer != player or weapon not in {"molotov", "flamethrower"}):
            return None

        for _, (_, _, _, existing_attachment) in self.active.items():
            if kind != "world" and existing_attachment == attachment:
                fire_id = next(value[0] for value in self.active.values() if value[3] == attachment)
                self.last_request[player] = (request_id, fire_id)
                return fire_id
        for slot in range(self.CAPACITY):
            if slot in self.active:
                continue
            self.generations[slot] += 1
            fire_id = FireId(slot, self.generations[slot])
            self.active[slot] = (fire_id, player, request_id, attachment)
            self.last_request[player] = (request_id, fire_id)
            return fire_id
        return None

    def extinguish(self, fire_id):
        if fire_id.slot in self.active and self.active[fire_id.slot][0] == fire_id:
            del self.active[fire_id.slot]


class FollowerCreationModelTests(unittest.TestCase):
    def test_molotov_and_flamethrower_requests_allocate_once_and_deduplicate(self):
        model = FollowerCreationModel()
        molotov = model.request(player=2, request_id=10, fire_position=(5, 0, 0), weapon="molotov")
        self.assertIsNotNone(molotov)
        self.assertEqual(
            model.request(player=2, request_id=10, fire_position=(99, 0, 0), weapon="pistol"),
            molotov,
        )
        flame = model.request(
            player=3,
            request_id=1,
            attachment=("ped", 14),
            ped_syncer=3,
            weapon="flamethrower",
        )
        self.assertIsNotNone(flame)

    def test_vehicle_birth_requires_the_requesters_authoritative_vehicle(self):
        model = FollowerCreationModel()
        self.assertIsNone(
            model.request(player=1, request_id=1, attachment=("vehicle", 9), vehicle=7)
        )
        accepted = model.request(player=1, request_id=2, attachment=("vehicle", 7), vehicle=7)
        self.assertIsNotNone(accepted)
        self.assertEqual(
            model.request(player=4, request_id=1, attachment=("vehicle", 7), vehicle=7),
            accepted,
        )
        synced_empty_vehicle = FollowerCreationModel().request(
            player=3,
            request_id=1,
            attachment=("vehicle", 11),
            vehicle_syncer=3,
        )
        self.assertIsNotNone(synced_empty_vehicle)

    def test_malicious_script_distance_area_and_attachment_claims_are_rejected(self):
        cases = (
            dict(created_by_script=True),
            dict(fire_position=(31, 0, 0)),
            dict(fire_area=2),
            dict(attachment=("player", 5)),
            dict(attachment=("world", None), weapon="pistol"),
            dict(attachment=("ped", 8), ped_syncer=9),
        )
        for index, kwargs in enumerate(cases, start=1):
            with self.subTest(kwargs=kwargs):
                self.assertIsNone(FollowerCreationModel().request(player=1, request_id=index, **kwargs))

    def test_server_owned_slot_reuse_advances_generation(self):
        model = FollowerCreationModel()
        first = model.request(player=1, request_id=1)
        self.assertIsNotNone(first)
        model.extinguish(first)
        replacement = model.request(player=1, request_id=2)
        self.assertEqual(replacement.slot, first.slot)
        self.assertGreater(replacement.generation, first.generation)

    def test_script_candidate_can_only_adopt_an_existing_host_canonical_fire(self):
        model = FollowerCreationModel()
        self.assertIsNone(model.request(player=2, request_id=1, created_by_script=True))
        canonical = model.add_host_script(fire_position=(12, 5, 0), fire_area=3)
        adopted = model.request(
            player=2,
            request_id=2,
            fire_position=(12, 5, 0),
            fire_area=3,
            player_area=3,
            created_by_script=True,
        )
        self.assertEqual(adopted, canonical)
        self.assertEqual(len(model.active), 1)


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
        self.assertIn('COOPANDREAS_VERSION "0.3.10-alpha"', config)
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
        self.assertNotIn("ThiscallEvent", fire_hooks)
        self.assertNotIn("0x53A270", fire_hooks)
        self.assertIn("gameProcessEvent.before", fire_hooks)
        self.assertIn("gameProcessEvent.after", fire_hooks)
        self.assertIn("BeginNativeBirthObservation(nullptr, true)", fire_hooks)
        self.assertIn("EndNativeBirthObservation()", fire_hooks)
        materialize = self.client.split("bool CNetworkFireManager::Materialize", 1)[1]
        materialize = materialize.split("void CNetworkFireManager::ClearPendingBirth", 1)[0]
        self.assertIn("BeginNativeBirthObservation(target, true)", materialize)
        self.assertIn("EndNativeBirthObservation()", materialize)
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

    def test_remote_attached_replay_uses_the_native_script_path_without_null_creator(self):
        materialize = self.client.split("bool CNetworkFireManager::Materialize", 1)[1]
        materialize = materialize.split("void CNetworkFireManager::ClearPendingBirth", 1)[0]
        self.assertIn("StartScriptFire", materialize)
        self.assertNotRegex(materialize, r"StartFire\s*\(\s*target\s*,\s*nullptr")
        self.assertIn("m_pEntityTarget == target", materialize)
        self.assertIn("replayScriptHandle & 0xFFFF", materialize)
        self.assertIn("m_nScriptReferenceIndex == scriptToken", materialize)
        self.assertIn("birthEpochsBefore[nativeIndex] != m_nativeBirthEpochs[nativeIndex]", materialize)
        self.assertIn("fire->m_nTimeToBurn = CTimer::m_snTimeInMilliseconds", materialize)
        self.assertIn("crime reporting", materialize)

    def test_follower_births_are_requested_adopted_and_timed_out_without_wire_changes(self):
        observe = self.client.split("void CNetworkFireManager::ObserveNativePool", 1)[1]
        observe = observe.split("void CNetworkFireManager::Process()", 1)[0]
        self.assertIn("PendingBirth& pending", observe)
        self.assertIn("SendPendingBirthIntent", observe)
        self.assertIn("FOLLOWER_BIRTH_TIMEOUT_MS", observe)
        self.assertIn("fire.m_nFlags.bCreatedByScript", observe)
        self.assertIn("continue;", observe)
        adoption = self.client.split("bool CNetworkFireManager::TryAdoptPendingBirth", 1)[1]
        adoption = adoption.split("void CNetworkFireManager::ProcessMaterializations", 1)[0]
        self.assertIn("slot.nativeSlot = nativeSlot", adoption)
        self.assertIn("slot.scriptReferenceIndex = pending.scriptReferenceIndex", adoption)
        self.assertIn("m_nativeOwners[nativeSlot] = slot.id.slot", adoption)
        self.assertIn("localOriginalGenerationsAllowed", adoption)
        send = self.client.split("void CNetworkFireManager::SendPendingBirthIntent", 1)[1]
        send = send.split("bool CNetworkFireManager::TryAdoptPendingBirth", 1)[0]
        self.assertIn("FireStateIntent", send)
        self.assertIn("intent.authoritySequence = pending.requestId", send)
        self.assertIn("scriptCandidate", self.client_header)
        self.assertIn("An unmatched SCM fire remains local", self.client)
        self.assertIn("PendingMatchesDescriptor(pending, canonicalSlot.descriptor)", observe)
        self.assertIn("RemoveNative(canonicalSlot, true)", observe)
        birth = self.client.split("void CNetworkFireManager::EndNativeBirthObservation", 1)[1]
        birth = birth.split("void CNetworkFireManager::RemoveNative", 1)[0]
        self.assertIn("m_nativeBirthOriginalGenerations", birth)
        self.assertIn("fire.m_nNumGenerationsAllowed = 0", birth)
        self.assertIn("CNetwork::m_bAuthenticated", birth)
        self.assertIn("pending.descriptor.generationsAllowed", observe)

    def test_server_allocates_follower_ids_and_rejects_spoofed_births(self):
        follower = self.server.split("void HandleFollowerCreation", 1)[1]
        follower = follower.split("}  // namespace", 1)[0]
        self.assertIn("ValidateFollowerCreation", follower)
        self.assertIn("AllocateFollowerFire", follower)
        self.assertNotIn("intent.id.slot", follower)
        self.assertIn("g_followerRequests", self.server)
        self.assertIn("FOLLOWER_CREATION_RATE_LIMIT", self.server)
        self.assertIn("FOLLOWER_ACTIVE_FIRE_LIMIT", self.server)
        validation = self.server.split("bool ValidateFollowerCreation", 1)[1]
        validation = validation.split("CanonicalFire* FindActiveAttachment", 1)[0]
        self.assertIn("requested.createdByScript", validation)
        self.assertIn("ResolveFreshPlayerObservation", validation)
        self.assertIn("requested.area != requesterArea", validation)
        self.assertIn("WEAPON_MOLOTOV", self.server)
        self.assertIn("WEAPON_FTHROWER", self.server)
        self.assertIn("ped->m_pSyncer != player", validation)
        self.assertIn("player->m_nVehicleId == canonical.attachmentId", validation)
        self.assertIn("vehicle->m_pPlayers[player->m_nSeatId] == player", validation)
        self.assertIn("vehicle->m_pSyncer == player", validation)
        script_match = self.server.split("CanonicalFire* FindMatchingAuthoritativeScript", 1)[1]
        script_match = script_match.split("CanonicalFire* FindFire", 1)[0]
        self.assertIn("fire.descriptor.createdByScript", script_match)
        follower = self.server.split("void HandleFollowerCreation", 1)[1]
        follower = follower.split("}  // namespace", 1)[0]
        script_branch = follower.split("if (intent.descriptor.createdByScript)", 1)[1]
        script_branch = script_branch.split("FireDescriptor descriptor", 1)[0]
        self.assertIn("FindMatchingAuthoritativeScript", script_branch)
        self.assertNotIn("AllocateFollowerFire", script_branch)

    def test_attached_follower_fire_keeps_native_target_motion(self):
        managed = self.client.split("void CNetworkFireManager::ObserveManagedSlot", 1)[1]
        managed = managed.split("void CNetworkFireManager::ObserveNativePool", 1)[0]
        position_write = "fire.m_vecPosition = slot.descriptor.fallbackPosition"
        self.assertEqual(managed.count(position_write), 1)
        guard = managed.split(position_write, 1)[0].rsplit("if", 1)[1]
        self.assertIn("eFireAttachmentType::WORLD", guard)

    def test_offline_native_behavior_and_story_fire_handles_are_preserved(self):
        process = self.client.split("void CNetworkFireManager::Process()", 1)[1]
        self.assertIn("if (!CNetwork::m_bAuthenticated)", process)
        self.assertIn("return; // Offline GTA fire creation", process)
        reset = self.client.split("void CNetworkFireManager::ResetNetworkState()", 1)[1]
        reset = reset.split("void CNetworkFireManager::HandleAuthorityChanged", 1)[0]
        self.assertIn("if (slot.materializedByNetwork && !slot.originatedLocally)", reset)
        self.assertIn("m_nFlags.bCreatedByScript", self.client)
        follower_scan = self.client.split("void CNetworkFireManager::ObserveNativePool", 1)[1]
        self.assertIn("pending.scriptCandidate = fire.m_nFlags.bCreatedByScript", follower_scan)
        script_timeout = follower_scan.split("if (pending.scriptCandidate)", 1)[1]
        script_timeout = script_timeout.split("++m_remoteMutationDepth", 1)[0]
        self.assertNotIn("fire.Extinguish()", script_timeout)
        self.assertIn("slot.scriptReferenceIndex = pending.scriptReferenceIndex", self.client)
        reset = self.client.split("void CNetworkFireManager::ResetNetworkState()", 1)[1]
        reset = reset.split("void CNetworkFireManager::HandleAuthorityChanged", 1)[0]
        self.assertIn("localOriginalCreatedByScript", reset)
        self.assertIn("localOriginalGenerationsAllowed", reset)
        remove = self.client.split("void CNetworkFireManager::RemoveNative", 1)[1]
        remove = remove.split("void CNetworkFireManager::RecordNativeIdentity", 1)[0]
        self.assertIn("fire.m_nFlags.bCreatedByScript = false", remove)

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
