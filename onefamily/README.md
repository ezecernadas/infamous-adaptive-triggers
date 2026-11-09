# OneFamily daemon: game trigger request file

The plugin does not talk to the controller itself. It writes the effects it wants to
`/data/onefamily/game_triggers.bin`, and the [OneFamily](https://github.com/ChiricoG/OneFamily)
daemon sends them to the DualSense. Official OneFamily builds do not read that file yet. The
proposal is [ChiricoG/OneFamily#66](https://github.com/ChiricoG/OneFamily/issues/66).

Until it lands upstream, this folder holds the change on top of **OneFamily v1.2.2-beta14**:

- `onefamily-game-trigger-beta14.patch`: the daemon change (12 files): the game trigger request
  file, a trigger strength fix and their host-side tests.
- The release page also ships `onefamily.bin`, built from beta14 with this patch.
  OneFamily is MIT licensed, © its contributors.

### Why beta14 and not a newer OneFamily

From v1.2.2-beta15 on (including the v1.2.2 stable release), the daemon no longer requests
feature report `0x05`, so a DualSense over Bluetooth stays in the simple `0x01` input mode. In that
mode the controller ignores the adaptive trigger effects in the output report, so no effect is
felt, neither this plugin's nor OneFamily's own trigger presets. beta14 still switches the
controller to the extended `0x31` mode. The tradeoff: beta15 removed that request to fix an input
freeze over Bluetooth, which beta14 can still hit.

## Building it yourself

```sh
git clone https://github.com/ChiricoG/OneFamily.git
cd OneFamily
git checkout v1.2.2-beta14
git apply /path/to/onefamily-game-trigger-beta14.patch
make -C daemon/tests          # host-side protocol tests
# then build the daemon as usual (build_daemon.ps1, OpenOrbis toolchain)
```

## Protocol

`/data/onefamily/game_triggers.bin`, 38 bytes, little endian:

| Offset | Field |
|---|---|
| 0 | `u32` magic `'OFGT'` |
| 4 | `u8` version (1) |
| 5 | `u8` flags (bit 0 = active) |
| 6 | `u16` reserved |
| 8 | `u32` seq (changes whenever the effects change) |
| 12 | `u8 r2[11]`: raw DualSense effect block for R2 |
| 23 | `u8 l2[11]`: raw DualSense effect block for L2 |
| 34 | `u32` heartbeat |

- While the active flag is set, the blocks replace the user's `dualsense_trigger_mode` preset.
  When the flag is clear, the preset applies as usual.
- A new `seq` is sent to the controller at once. The writer rewrites the file (temp file +
  rename) on every change and at least every 0.5 s.
- If neither `seq` nor `heartbeat` changes for 2 s, the daemon goes back to the user's preset. A
  stale file left behind by a closed game is never applied again.
- Messages with a bad magic, version, size or an unknown effect mode are ignored.

The header `plugin/source/game_trigger_protocol.h` is the same file the daemon uses.
