# Production two-client soak

`network_soak` uses the repository's real ENet transport, packet channels, packet classes, and serializers.
It connects two independent client hosts, validates roster and credential handshakes, relays movement,
camera and clothes snapshots, exercises the stunt catalog codec, then repeatedly disconnects and reconnects
one client before a sustained sync run.

Build `server` and `network_soak`, start a dedicated server, then run:

    build\windows\x86\release\network_soak.exe --host 127.0.0.1 --port 6767 --cycles 8 --duration-ms 5000

The harness can also join a live server as two bot peers to stress an installed GTA client without needing a
second person.
