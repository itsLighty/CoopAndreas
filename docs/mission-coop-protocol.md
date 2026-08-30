# Mission co-op protocol

CoopAndreas story missions use an authoritative mission session to keep canonical story state on the host while
allowing a frozen set of remote players to participate. The session protocol and SCM layer must agree on player
identity, lifecycle, failure, and cleanup. A mission is not ready merely because its script enables opcode syncing.

## Authoritative session and roster

The server creates one mission session for an approved host launch. Its participant IDs are frozen for the lifetime
of that session. The first `gameplayParticipantCount` IDs are gameplay participants; any remaining players, including
late joiners, are spectators until the next mission. The current SCM interface supports the host and at most three
remote gameplay participants even though the wire roster can contain every connected player.

`Coop.CollectNetworkPlayersForTheMission()` has two modes:

- During an active authoritative session it resolves the frozen gameplay IDs, omits the host, and writes the remote
  participants into stable SCM slots. A disconnected or not-yet-streamed participant leaves an invalid character
  handle in that participant's slot. Current player-vector order is never consulted for slot ownership, so a late
  spectator cannot replace a disconnected participant.
- Outside an active session it retains the legacy player-vector ordering used by non-session scripts. Null players,
  missing peds, and invalid ped-pool references produce invalid handles instead of being dereferenced.

`Coop.GetNetworkPlayerChar(id)` also returns an invalid handle for an out-of-range or unknown ID, a null player or
ped, an invalid ped-pool reference, or a spectator ID while a mission session is active. SCM code must still validate
every returned handle with `Coop.IsNetworkPlayerActorValid()` before dereferencing it.

The invalid SCM character handle is `0`. Network entity IDs use their existing `-1` invalid sentinel.

## Mission-script obligations

Every story mission in `MissionRunner.cpp` must implement the following behavior:

1. Enable synchronization and collect the remote roster at entry.
2. Store each accepted participant's internal network ID exactly once as the immutable mission identity.
3. Recollect the frozen roster periodically from an active co-op update path. Accept a new ped handle only after its
   internal ID matches the immutable slot ID. A missing handle is disconnected state, not a usable actor.
4. Treat late and non-roster players as spectators. Reconnecting participants resume the current canonical phase and
   are restaged without restarting host objectives, rewards, cutscenes, or mission variables.
5. Fail deterministically when a currently connected frozen participant dies. Route that failure through the normal
   mission failure and cleanup path and notify connected peers. A disconnected slot is not itself a death failure.
6. Keep story progression, rewards, statistics, save flags, mission pass/fail, and destructive world mutations owned
   by the host. Peer actions may feed the existing host transition, but cannot award progression independently.
7. Freeze, protect, regroup, and release valid participants around host cinematics and interior transitions. Never
   invoke actor opcodes on disconnected handles.
8. Bound every ped/vehicle network-ID registration attempt by a deadline or retry cap. On timeout, continue with a
   documented coordinate, host-AI, or no-blip fallback rather than blocking the mission thread.
9. Fan objectives and pass/fail results out to connected participants. Cleanup must be idempotent and restore any
   temporary weapons, controls, proofs, vehicles, checkpoints, blips, and text owned by the co-op layer.

The static audit recognizes explicit co-op roster/reconnect, participant-death, result, and cleanup labels or markers.
These names are part of the maintainability contract: they make protocol-critical paths reviewable and prevent stock
enemy or escort checks from being mistaken for participant policy.

## Lifecycle and authority

Only the authoritative host may launch, advance, end, or abort a session. Session and request sequence numbers use
wrap-aware comparisons. State updates are accepted only when they are newer or repeat the same authoritative state;
an acknowledgement cannot invent a state transition. Spectators do not receive mission effects, and non-host clients
cannot publish authoritative script effects.

Production SCM launchers call `Coop.LaunchMissionForCoop(missionId)` (`1D1D`) instead of opcode `0417` directly.
While authenticated, the command suppresses peer launches and asks the server to freeze the authoritative roster;
only an accepted host request executes the native internal-mission opcode. Duplicate launcher ticks cannot create a
second request, and a rejected SCM request rolls back its speculative mission flag. Before authentication, the command
uses the native opcode directly so `MAIN` missions 0 and 1 and offline startup cannot deadlock on a network response.

The host normally ends the session as completed, succeeded, or failed after the SCM path reaches its terminal state.
A host abort or host disconnect terminates the session without transferring story authority to another player.

## Validation

Run the protocol structure tests and the strict story audit from the repository root:

```powershell
python -m unittest discover -s tests/mission_session -p 'test_*.py'
./scripts/audit-story-missions.ps1 -RequireReady -SummaryOnly
```

Compile the full SCM with pinned Sanny Builder 4.2.0:

```powershell
./scripts/validate-scm.ps1 -SannyBuilderPath <path-to-Sanny-Builder-4.2.0>
```

CI runs all three checks before compiling the native targets.

## Runtime limitations

- Static checks and a successful Sanny compile do not prove multiplayer timing, streaming, collision, or balance.
- SCM currently exposes only three remote participant slots. Additional frozen roster members remain spectators.
- Reconnect recovery depends on the server preserving the participant's session identity and on the replacement ped
  becoming available to the host. Until then the stable slot remains invalid.
- Missions poll for reconnects; recovery latency is bounded by each mission's update cadence.
- Entity registration can time out under streaming or packet loss. Missions must use their declared fallback, so a
  peer may temporarily lose an entity blip or support role without blocking canonical progression.
- Health-ledger friendly-fire mitigation cannot always restore a lethal hit before the engine resolves death. The
  deterministic connected-participant death policy then takes the normal failure path.
- Live validation should cover disconnect/reconnect, late spectators, four-player sessions, cutscene skips, interiors,
  death and retry, cleanup after failure, and successful completion without duplicate rewards.
