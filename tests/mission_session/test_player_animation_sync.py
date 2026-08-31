import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


def function_body(source: str, signature_pattern: str) -> str:
    signature = re.search(signature_pattern, source)
    if signature is None:
        raise AssertionError(f"missing function matching {signature_pattern}")
    opening_brace = source.find("{", signature.end())
    if opening_brace < 0:
        raise AssertionError(f"missing function body matching {signature_pattern}")
    depth = 0
    for index in range(opening_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening_brace + 1 : index]
    raise AssertionError(f"unterminated function matching {signature_pattern}")


class PlayerAnimationSyncTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.packets = (ROOT / "shared/network/packets/players.h").read_text(encoding="utf-8")
        cls.local_player = (ROOT / "client/src/CLocalPlayer.cpp").read_text(encoding="utf-8")
        cls.remote_player = (ROOT / "client/src/CNetworkPlayer.cpp").read_text(encoding="utf-8")
        cls.remote_header = (ROOT / "client/src/CNetworkPlayer.h").read_text(encoding="utf-8")
        cls.manager = (ROOT / "client/src/CPlayerAnimationSyncManager.cpp").read_text(encoding="utf-8")
        cls.manager_header = (ROOT / "client/src/CPlayerAnimationSyncManager.h").read_text(encoding="utf-8")
        cls.main = (ROOT / "client/src/Main.cpp").read_text(encoding="utf-8")
        cls.client_handler = (ROOT / "client/src/PacketHandlers/players.cpp").read_text(encoding="utf-8")
        cls.server_handler = (ROOT / "server/src/PacketHandlers/players.cpp").read_text(encoding="utf-8")

    def test_existing_task_event_has_a_closed_bounded_semantic_payload(self):
        enum_body = re.search(
            r"enum ePlayerAnimationState\s*:\s*int\s*\{(.*?)\};", self.packets, re.S
        ).group(1)
        states = re.findall(r"PLAYER_ANIMATION_[A-Z_]+", enum_body)
        self.assertEqual(
            states,
            [
                "PLAYER_ANIMATION_NONE",
                "PLAYER_ANIMATION_IDLE_STRETCH",
                "PLAYER_ANIMATION_IDLE_TIME",
                "PLAYER_ANIMATION_IDLE_SHOULDER",
                "PLAYER_ANIMATION_IDLE_STRETCH_LEG",
                "PLAYER_ANIMATION_FUNNY_TURN_LEFT",
                "PLAYER_ANIMATION_FUNNY_TURN_RIGHT",
                "PLAYER_ANIMATION_COUNT",
            ],
        )

        packet = re.search(r"class SetPlayerTask\b.*?^};", self.packets, re.S | re.M).group(0)
        self.assertIn(
            "DEFINE_PACKET_TYPE(SetPlayerTask, ePacketType::SET_PLAYER_TASK, ePacketChannel::EVENT)",
            packet,
        )
        self.assertIn("serialize_bool(stream, hasAnimationState)", packet)
        self.assertIn(
            "serialize_int(stream, animationState, PLAYER_ANIMATION_NONE, PLAYER_ANIMATION_COUNT - 1)",
            packet,
        )
        self.assertIn("serialize_uint16(stream, animationSequence)", packet)
        self.assertIn("serialize_uint8(stream, animationProgress)", packet)
        self.assertIn("Stream::IsWriting && !IsAnimationStateSemanticallyValid()", packet)
        self.assertIn("Stream::IsReading && !IsAnimationStateSemanticallyValid()", packet)
        for invariant in (
            "taskType == TASK_SIMPLE_PLAYER_ON_FOOT",
            "!toggle",
            "animationState >= PLAYER_ANIMATION_NONE",
            "animationState < PLAYER_ANIMATION_COUNT",
            "animationState != PLAYER_ANIMATION_NONE || animationProgress == 0",
        ):
            self.assertIn(invariant, packet)

        legacy_order = [
            "serialize_object(stream, playerid)",
            "serialize_int(stream, taskType, TASK_SIMPLE_PLAYER_ON_FOOT, MAX_NUM_TASK_TYPES - 1)",
            "serialize_object(stream, vecPos)",
            "serialize_object(stream, currentRotation)",
            "serialize_object(stream, aimingRotation)",
            "serialize_bool(stream, toggle)",
            "serialize_bool(stream, hasAnimationState)",
        ]
        offsets = [packet.index(fragment) for fragment in legacy_order]
        self.assertEqual(offsets, sorted(offsets))
        self.assertIn("bool hasAnimationState = false", packet)
        conditional = function_body(packet, r"if \(hasAnimationState\)")
        self.assertIn("serialize_int(stream, animationState", conditional)
        self.assertNotIn("animationState", packet[packet.index("serialize_bool(stream, toggle)") : packet.index("if (hasAnimationState)")])

    def test_server_canonicalizes_identity_rejects_bad_semantics_and_never_echoes(self):
        body = function_body(
            self.server_handler,
            r"PACKET_HANDLER\(ePacketType::SET_PLAYER_TASK\s*,",
        )
        validation = body.index("IsAnimationStateSemanticallyValid")
        identity = body.index("pSetPlayerTask->playerid = pNetworkPlayer->m_iPlayerId")
        relay = body.index("GetPacketFactory().SendToAll(*pSetPlayerTask, pNetworkPlayer)")
        self.assertLess(validation, identity)
        self.assertLess(identity, relay)
        self.assertIn("return;", body[:identity])

        client_body = function_body(
            self.client_handler,
            r"PACKET_HANDLER\(ePacketType::SET_PLAYER_TASK\s*,",
        )
        self.assertIn("IsAnimationStateSemanticallyValid", client_body)
        self.assertIn("pNetworkPlayer->HandleTask(*pSetPlayerTask)", client_body)
        self.assertNotIn("GetPacketFactory", client_body)
        self.assertNotIn("GetPacketFactory", self.remote_player)

    def test_animation_rate_guard_is_bounded_animation_only_and_safe_for_id_reuse(self):
        guard = function_body(self.server_handler, r"CanRelayAnimationEvent\(")
        for evidence in (
            "player->m_iPlayerId < 0",
            "player->m_iPlayerId >= Config::MAX_SERVER_PLAYERS",
            "slot.owner != player",
            "slot.connectId != connectId",
            "now - slot.windowStartedAt >= ANIMATION_RATE_WINDOW_MS",
            "slot.eventCount >= MAX_ANIMATION_EVENTS_PER_WINDOW",
            "++slot.eventCount",
        ):
            self.assertIn(evidence, guard)
        self.assertIn("ANIMATION_RATE_WINDOW_MS = 1000", self.server_handler)
        self.assertIn("MAX_ANIMATION_EVENTS_PER_WINDOW = 20", self.server_handler)
        self.assertIn(
            "AnimationRateSlot g_animationRateSlots[Config::MAX_SERVER_PLAYERS]{}",
            self.server_handler,
        )

        body = function_body(
            self.server_handler,
            r"PACKET_HANDLER\(ePacketType::SET_PLAYER_TASK\s*,",
        )
        self.assertRegex(
            body,
            r"if \(pSetPlayerTask->hasAnimationState &&\s*"
            r"!CanRelayAnimationEvent\(pNetworkPlayer\)\)",
        )
        self.assertEqual(body.count("CanRelayAnimationEvent"), 1)

    def test_local_observer_allowlists_stock_idle_and_action_special_steer_lean(self):
        observe = function_body(
            self.manager,
            r"CPlayerAnimationSyncManager::ObserveLocalAnimation\(",
        )
        for animation, state in (
            ("ANIM_PLAYIDLES_STRETCH", "PLAYER_ANIMATION_IDLE_STRETCH"),
            ("ANIM_PLAYIDLES_TIME", "PLAYER_ANIMATION_IDLE_TIME"),
            ("ANIM_PLAYIDLES_SHLDR", "PLAYER_ANIMATION_IDLE_SHOULDER"),
            ("ANIM_PLAYIDLES_STRLEG", "PLAYER_ANIMATION_IDLE_STRETCH_LEG"),
            ("ANIM_DEFAULT_TURN_L", "PLAYER_ANIMATION_FUNNY_TURN_LEFT"),
            ("ANIM_DEFAULT_TURN_R", "PLAYER_ANIMATION_FUNNY_TURN_RIGHT"),
        ):
            self.assertIn(animation, observe)
            self.assertIn(state, observe)

        self.assertIn("controls.LeftShoulder1 && controls.RightStickX < 0", observe)
        self.assertIn("controls.LeftShoulder1 && controls.RightStickX > 0", observe)
        active = function_body(self.manager, r"FindActiveAssociation\(")
        for condition in (
            "association->m_bPlaying",
            "association->m_fBlendAmount > 0.0f",
            "association->m_fBlendDelta >= 0.0f",
            "association->m_nAnimGroup == groupId",
            "association->m_nAnimId == animationId",
        ):
            self.assertIn(condition, active)

    def test_start_stop_replacement_refresh_and_replay_rejection_are_explicit(self):
        process = function_body(self.manager, r"CPlayerAnimationSyncManager::Process\(")
        self.assertIn("state != ms_lastState", process)
        self.assertIn("state != Packets::Players::PLAYER_ANIMATION_NONE", process)
        self.assertIn("now - ms_lastSentAt >= ANIMATION_HEARTBEAT_MS", process)
        self.assertIn("ANIMATION_HEARTBEAT_MS = 1000", self.manager)
        send = function_body(self.manager, r"CPlayerAnimationSyncManager::SendState\(")
        self.assertIn("++ms_sequence", send)
        self.assertIn("BuildAnimationTaskPacket(state, ms_sequence, QuantizeProgress(association))", send)

        handle = function_body(self.remote_player, r"CNetworkPlayer::HandleSyncedAnimation\(")
        fade = handle.index("FadeSyncedAnimation()")
        replace = handle.index("m_nSyncedAnimationState = packet.animationState")
        self.assertLess(fade, replace)
        self.assertIn("!IsSequenceNewer(packet.animationSequence, m_nSyncedAnimationSequence)", handle)
        self.assertIn("PLAYER_ANIMATION_NONE", handle)
        sequence = function_body(self.remote_player, r"IsSequenceNewer\(")
        self.assertIn("static_cast<int16_t>(incoming - previous) > 0", sequence)

        def is_newer(incoming: int, previous: int) -> bool:
            difference = (incoming - previous) & 0xFFFF
            signed = difference if difference < 0x8000 else difference - 0x10000
            return signed > 0

        self.assertTrue(is_newer(1, 0))
        self.assertTrue(is_newer(0, 0xFFFF))
        self.assertFalse(is_newer(0xFFFF, 0))
        self.assertFalse(is_newer(7, 7))

        quantize = function_body(self.manager, r"CPlayerAnimationSyncManager::QuantizeProgress\(")
        self.assertRegex(quantize, re.compile(r"if \(!association.*?return 0;", re.S))
        self.assertRegex(
            handle,
            re.compile(
                r"m_nSyncedAnimationState = packet\.animationState;.*?"
                r"if \(m_nSyncedAnimationState != Packets::Players::PLAYER_ANIMATION_NONE\).*?"
                r"ApplySyncedAnimation\(\);",
                re.S,
            ),
        )

        task = function_body(self.remote_player, r"CNetworkPlayer::HandleTask\(")
        self.assertIn("++m_nPendingTaskGeneration", task)
        self.assertIn("ApplyTaskPresentation(packet)", task)
        replay = function_body(self.remote_player, r"CNetworkPlayer::ApplyPendingTaskOnce\(")
        self.assertIn("m_nAppliedTaskGeneration == m_nPendingTaskGeneration", replay)
        self.assertEqual(replay.count("ApplyTaskPresentation(m_pendingTask)"), 1)
        presentation = function_body(self.remote_player, r"CNetworkPlayer::ApplyTaskPresentation\(")
        self.assertLess(
            presentation.index("packet.hasAnimationState"), presentation.index("if (!m_pPed)")
        )

    def test_remote_application_uses_loaded_blocks_and_only_the_exact_installed_pair(self):
        definitions = function_body(self.remote_player, r"GetSyncedAnimationDefinition\(")
        self.assertEqual(definitions.count("definition = {"), 6)
        for fixed_metadata in (
            "ANIM_GROUP_PLAYIDLES, ANIM_PLAYIDLES_STRETCH, 8.0f",
            "ANIM_GROUP_PLAYIDLES, ANIM_PLAYIDLES_TIME, 8.0f",
            "ANIM_GROUP_PLAYIDLES, ANIM_PLAYIDLES_SHLDR, 8.0f",
            "ANIM_GROUP_PLAYIDLES, ANIM_PLAYIDLES_STRLEG, 8.0f",
            "ANIM_GROUP_DEFAULT, ANIM_DEFAULT_TURN_L, 16.0f",
            "ANIM_GROUP_DEFAULT, ANIM_DEFAULT_TURN_R, 16.0f",
        ):
            self.assertIn(fixed_metadata, definitions)

        apply = function_body(self.remote_player, r"CNetworkPlayer::ApplySyncedAnimation\(")
        self.assertIn("CPlayerAnimationSyncManager::EnsurePlayIdlesLoaded()", apply)
        self.assertIn("CAnimManager::BlendAnimation", apply)
        self.assertIn("association->SetCurrentTime(targetTime)", apply)
        self.assertIn("association->m_nFlags |= ANIMATION_UNUSED_2", apply)

        exact_find = function_body(self.remote_player, r"FindSyncedAnimationAssociation\(")
        self.assertIn("association->m_nAnimGroup == definition.groupId", exact_find)
        self.assertIn("association->m_nAnimId == definition.animationId", exact_find)
        exact_fade = function_body(self.remote_player, r"CNetworkPlayer::FadeSyncedAnimation\(")
        self.assertIn("FindSyncedAnimationAssociation(m_pPed, definition)", exact_fade)
        self.assertIn("association->m_fBlendDelta = -definition.blendDelta", exact_fade)
        for broad_removal in (
            "RpAnimBlendClumpRemoveAssociations",
            "ANIMATION_PARTIAL)",
            "ANIMATION_IS_PARTIAL",
        ):
            self.assertNotIn(broad_removal, self.remote_player)

        acquire = function_body(self.manager, r"CPlayerAnimationSyncManager::AcquirePlayIdles\(")
        release = function_body(self.manager, r"CPlayerAnimationSyncManager::ReleasePlayIdles\(")
        ensure = function_body(self.manager, r"CPlayerAnimationSyncManager::EnsurePlayIdlesLoaded\(")
        self.assertIn("ms_playIdlesUsers >= Config::MAX_SERVER_PLAYERS", acquire)
        self.assertIn("ms_playIdlesUsers++ == 0", acquire)
        self.assertIn('Command<Commands::REQUEST_ANIMATION>("PLAYIDLES")', acquire)
        self.assertIn("--ms_playIdlesUsers == 0", release)
        self.assertIn('Command<Commands::REMOVE_ANIMATION>("PLAYIDLES")', release)
        self.assertIn("ms_playIdlesUsers == 0", ensure)
        self.assertIn('Command<Commands::HAS_ANIMATION_LOADED>("PLAYIDLES")', ensure)
        self.assertIn('Command<Commands::REQUEST_ANIMATION>("PLAYIDLES")', ensure)

        handle = function_body(self.remote_player, r"CNetworkPlayer::HandleSyncedAnimation\(")
        self.assertIn("CPlayerAnimationSyncManager::AcquirePlayIdles()", handle)
        self.assertIn("CPlayerAnimationSyncManager::ReleasePlayIdles()", handle)
        self.assertIn("if (!wasIdle && isIdle)", handle)
        self.assertIn("else if (wasIdle && !isIdle)", handle)

    def test_offline_disconnect_respawn_and_late_stream_in_lifecycle_is_deterministic(self):
        process = function_body(self.manager, r"CPlayerAnimationSyncManager::Process\(")
        auth = process.index("if (!CNetwork::m_bAuthenticated)")
        reset = process.index("ResetNetworkState()", auth)
        observe = process.index("ObserveLocalAnimation")
        self.assertLess(auth, reset)
        self.assertLess(reset, observe)
        self.assertRegex(process[reset:observe], r"ResetNetworkState\(\);\s*return;")

        local_send = function_body(self.local_player, r"CLocalPlayer::BuildAnimationTaskPacket\(")
        self.assertIn("if (!CNetwork::m_bAuthenticated)", local_send)
        self.assertLess(
            local_send.index("if (!CNetwork::m_bAuthenticated)"),
            local_send.index("FindPlayerPed(0)"),
        )
        self.assertNotIn("BlendAnimation", self.manager)

        main_call = self.main.index("CPlayerAnimationSyncManager::Process()")
        auth_block = self.main.index("if (/*CNetwork::m_bConnected*/ CNetwork::m_bAuthenticated)")
        self.assertLess(main_call, auth_block)

        create = function_body(self.remote_player, r"CNetworkPlayer::CreatePed\(")
        respawn = function_body(self.remote_player, r"CNetworkPlayer::Respawn\(")
        destroy = function_body(self.remote_player, r"CNetworkPlayer::DestroyPed\(")
        clear = function_body(self.remote_player, r"CNetworkPlayer::ClearSyncedAnimationState\(")
        self.assertIn("ApplySyncedAnimation()", create)
        self.assertLess(respawn.index("ClearSyncedAnimationState()"), respawn.index("DestroyPed()"))
        self.assertIn("m_pPed = nullptr", destroy)
        self.assertNotIn("ClearSyncedAnimationState", destroy)
        self.assertNotIn("ReleasePlayIdles", destroy)
        for reset_field in (
            "m_nSyncedAnimationState = Packets::Players::PLAYER_ANIMATION_NONE",
            "m_nSyncedAnimationSequence = 0",
            "m_nSyncedAnimationProgress = 0",
            "m_bHasSyncedAnimationSequence = false",
        ):
            self.assertIn(reset_field, clear)
        destructor = function_body(self.remote_player, r"CNetworkPlayer::~CNetworkPlayer\(")
        self.assertLess(destructor.index("ClearSyncedAnimationState()"), destructor.index("DestroyPed()"))
        self.assertIn("CPlayerAnimationSyncManager::ReleasePlayIdles()", clear)
        self.assertIn("ms_initialized = false", self.manager_header)


if __name__ == "__main__":
    unittest.main()
