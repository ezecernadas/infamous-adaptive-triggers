# inFAMOUS Second Son: DualSense adaptive triggers

[![tests](https://github.com/ezecernadas/infamous-adaptive-triggers/actions/workflows/tests.yml/badge.svg)](https://github.com/ezecernadas/infamous-adaptive-triggers/actions/workflows/tests.yml)
[![license: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blue.svg)](LICENSE)
![platform: PS4 GoldHEN](https://img.shields.io/badge/platform-PS4%20%C2%B7%20GoldHEN-informational)

A GoldHEN plugin that gives **inFAMOUS Second Son** on PS4 adaptive trigger effects on a
**DualSense connected over Bluetooth**. Each power gets its own R2 feel, and the effects follow
what the game is doing: shots, charges, dashes, empty energy and the pause menu.

The game has no adaptive-trigger support of its own. The plugin reads the game's state, decides
the effect, and hands it to the [OneFamily](https://github.com/ChiricoG/OneFamily) daemon, which
drives the DualSense.

> **Status: pre-release.** Developed and tested on one console and controller. Feedback is welcome.

## What it feels like

| Power | Resting | While shooting |
|---|---|---|
| **Neon** | A crisp click mid-travel, like a semi-automatic | A short, sharp kick on every shot. The trigger goes free while a shot or the recovery after a burst is running. |
| **Smoke** | **Two-stage trigger:** free for the first part of the travel, then a hard wall | A light press fires a normal shot. Pushing into the wall charges, a "ready" pulse tells you the charge is full, and backing out of the wall releases the charged shot. |
| **Video** | Light resistance in the second half of the travel | A buzz that speeds up the longer the burst runs |
| **Concrete** | Your OneFamily setting (not mapped yet) | — |

For every mapped power:

- **No energy:** the trigger jams (maximum resistance) until you absorb a source.
- **Dash:** the trigger goes free, since the game does not let you fire.
- **Pause menu and loading screens:** the trigger goes back to your own OneFamily setting.
- **Game closed:** within 2 seconds, the same.

## Requirements

- PS4 with **GoldHEN** (plugin support enabled).
- **inFAMOUS Second Son, CUSA00223, version 01.00** (no update installed). On any other version or
  region, the plugin detects the mismatch and stays idle.
- A **DualSense** paired over Bluetooth through **OneFamily**, running a daemon that reads the game
  trigger request file. Official OneFamily builds don't read it yet
  ([proposal: ChiricoG/OneFamily#66](https://github.com/ChiricoG/OneFamily/issues/66)). The release
  ships a build of OneFamily v1.2.2-beta16 with that change. See [`onefamily/`](onefamily/).

## Installation

1. **OneFamily:** install the official v1.2.2-beta16 companion app, then replace the daemon
   payload that GoldHEN's AutoRun loads with the `onefamily.bin` from this release. If the
   companion offers to install its own daemon, decline, because it would overwrite this one.
2. **Plugin:** copy `infamous_triggers.prx` to `/data/GoldHEN/plugins/` and add it under the
   game's section of `/data/GoldHEN/plugins.ini`:

   ```ini
   [CUSA00223]
   /data/GoldHEN/plugins/infamous_triggers.prx
   ```

3. Reboot the console, start the game, and look for the notification
   *"inFAMOUS adaptive triggers v3.1 active"*.

The plugin writes a log to `/data/GoldHEN/infamous_triggers.log`, useful when reporting issues.

## How it works

```
inFAMOUS ──hooks──▶ infamous_triggers.prx ──/data/onefamily/game_triggers.bin──▶ OneFamily daemon ──Bluetooth──▶ DualSense
```

- **Game state:** the active power, the hero's energy and current action, the game's own "can use
  powers" mask (dashes) and its time scale (pause and loading) are read straight from memory. Every
  address is checked against the expected code before it is used.
- **Events:** shots, charges and bursts come from hooks on the game's rumble functions. Only
  rumbles started by the game's scripts count, so landings or absorbing a source don't trigger
  anything.
- **Effects:** a small pure-C state machine maps state and events to the official DualSense effects
  (Feedback, Weapon, Vibration). For smoke, the plugin also rewrites the R2 input the game sees, to
  implement the two-stage trigger.
- **Hand-off:** the plugin writes a 38-byte request file. The daemon applies it instead of your
  trigger preset while it is fresh, and drops it after 2 seconds of silence.

The full write-up, with addresses, rumble ids and how each piece was found, is in
[docs/how-it-works.md](docs/how-it-works.md).

## Building

You need the [OpenOrbis PS4 toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain) and the
[GoldHEN Plugins SDK](https://github.com/GoldHEN/GoldHEN_Plugins_SDK).

```sh
export OO_PS4_TOOLCHAIN=/path/to/PS4Toolchain
export GOLDHEN_SDK=/path/to/GoldHEN_Plugins_SDK
make -C plugin clean
make -C plugin          # → bin/prx_final/infamous_triggers.prx
```

Always run `clean` first: the Makefile does not track header changes.

The game logic is plain C with host-side tests, so no console is needed to run them:

```sh
make -C plugin/tests
```

## Limitations

- Only v01.00 of the game is supported.
- Concrete is not mapped, and L2 has no effects yet.
- The effects were tuned by hand on one DualSense. The official effects' strengths don't feel the
  same on every controller.
- Requests reach the daemon through a polled file, so an effect can arrive up to ~30 ms late.

## Credits

- **[OneFamily](https://github.com/ChiricoG/OneFamily)** by ChiricoG: the daemon that makes the
  DualSense work on PS4 and applies the effects (MIT).
- **[HayatoG/iss_pc](https://github.com/HayatoG/iss_pc)** and
  **[Liondarkz/issn_pc](https://github.com/Liondarkz/issn_pc)**: their PC ports of the game
  documented the active power, the hero object and the game clock. Only those findings were used,
  no code.
- **[GoldHEN Plugins Repository](https://github.com/GoldHEN/GoldHEN_Plugins_Repository)**: the
  plugin build template, and `pad.h` plus the pad-read hook approach from jocover's `gamepad_helper`
  (GPL-3.0).
- Nielk1's [DualSense trigger effect notes](https://gist.github.com/Nielk1/6d54cc2c00d2201ccb8c2720ad7538db)
  for the official effect encodings.

Built with the help of Claude Code.

## License

[GPL-3.0](LICENSE). The OneFamily build on the release page is MIT, © its contributors.

This is an unofficial fan project. It is not affiliated with or endorsed by Sony Interactive
Entertainment or Sucker Punch Productions. No game code or assets are included.
