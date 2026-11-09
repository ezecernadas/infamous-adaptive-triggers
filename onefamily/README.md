# OneFamily daemon: game trigger request file

The plugin does not talk to the controller itself. It writes the effects it wants to
`/data/onefamily/game_triggers.bin`, and the [OneFamily](https://github.com/ChiricoG/OneFamily)
daemon sends them to the DualSense. Official OneFamily builds do not read that file yet. The
proposal is [ChiricoG/OneFamily#66](https://github.com/ChiricoG/OneFamily/issues/66).

Until it lands upstream, this folder holds the change on top of **OneFamily v1.2.2-beta16**:

- `onefamily-game-trigger-beta16.patch`: the daemon change (10 files, mostly the protocol header
  and its host-side tests). Nothing else from beta16 is touched.
- The release page also ships `onefamily.bin`, built from beta16 with this patch.
  OneFamily is MIT licensed, © its contributors.

## Building it yourself

```sh
git clone https://github.com/ChiricoG/OneFamily.git
cd OneFamily
git checkout v1.2.2-beta16
git apply /path/to/onefamily-game-trigger-beta16.patch
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
