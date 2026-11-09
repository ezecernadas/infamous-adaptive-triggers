# How it works

All addresses are for **inFAMOUS Second Son CUSA00223 v01.00**. They are Ghidra addresses with
image base `0x400000`, so runtime = `base_address + (addr - 0x400000)`. The plugin checks the bytes
around every hook and read site at load. On any other version or region it stays idle and shows
a notification.

## Signals read from the game

| Signal | Where | Used for |
|---|---|---|
| Active power | `int` at `0x0128d22c` (player state `0x0128d148` + `0xe4`): neon 0, smoke 1, video 3, concrete 4 | which power's effects apply |
| Hero object | pointer at `0x01e97b78` (checked through the reader at `0x609f83`) | "in game" gate, energy, action state |
| Power energy | `float` at hero + `0x1a1e8`, maximum at + `0x1a1f0` | jammed trigger when empty (`<= 0.5`) |
| Hero busy | hero + `0x19c0` (action type at + `0x18f8`: 1 shot, 3 burst recovery) | free trigger while a shot or recovery runs (neon, video) |
| Usable mask | return value of `0x5c2500` (the function behind the `CAN_USE_POWERS` script native) | dash: bits `0x00c00000` clear means the powers are locked |
| Time scale | `float` at `0x011bea30` (game clock `0x011bea10` + `0x20`): 1.0 playing, 0.0 paused or loading | hands the trigger back to the user |
| Rumble events | hooks on `start_rumble` `0x7804d0` and `stop_rumble` `0x780860`; only calls from the `StartRumble` script native (return address `0x63f23e`) | shots, charges and bursts per power |

Rumble ids from the script native (the pattern table at `0x107a4d0`, stride `0x104`):

| Power | Event | Rumble |
|---|---|---|
| Neon | shot | id 9, finite |
| Smoke | tap shot | id 8 |
| Smoke | charging | id 1, infinite, stopped on release |
| Smoke | charged shot | id 7, finite |
| Video | burst | id 2, infinite, stopped when it ends |
| any | dry fire (no energy) | id 2, finite 0.2 s |

## Two-stage R2 (smoke)

The game tells a tap from a charge by how long R2 is held: a tap starts a charge and cancels it
within ~70 ms. The plugin hooks `scePadRead` / `scePadReadState` (the same way GoldHEN's
`gamepad_helper` does) and rewrites what the game sees from how deep R2 is pressed:

- Pressed past 45 % but not to the wall: after 80 ms (or at once if released earlier), a 50 ms
  press is sent. That is a normal shot.
- Into the wall (78 % of the analog range, felt at the zone-7 Feedback wall): R2 is held for the
  game, so it charges. Backing out of the wall fires the charged shot.

The 80 ms wait means pressing straight through to the wall never fires a normal shot first.

## How the pieces were found

- The active power and the hero layout come from the open-source PC ports
  [HayatoG/iss_pc](https://github.com/HayatoG/iss_pc) and
  [Liondarkz/issn_pc](https://github.com/Liondarkz/issn_pc) (US v1.07). They were mapped to v01.00
  with their byte patterns and checked against memory diffs. Only facts were used, no code.
- Rumble ids were decoded with a research plugin that logged every rumble with its caller while
  each power was used in a known order.
- The pause flag was found by diffing three paused and three playing dumps of the RW segment.
  v01.00 has no pause counter like the one the PC port uses on v1.07.

## Plugin source map

| File | Role |
|---|---|
| `plugin/source/main.c` | hooks, game reads, publisher thread, log |
| `plugin/source/trigger_map.h` | state + events → R2 effect per power (pure C, tested) |
| `plugin/source/two_stage.h` | two-stage R2 state machine for smoke (pure C, tested) |
| `plugin/source/game_state.h` | address constants and small checks (tested) |
| `plugin/source/game_trigger_protocol.h` | the OneFamily request file format |
| `plugin/source/hero_capture.h`, `pause_dump.h` | research helpers, compiled out (`ENABLE_HERO_CAPTURE`, `ENABLE_PAUSE_DUMPS`) |

The plugin logs to `/data/GoldHEN/infamous_triggers.log`: power changes, mapped rumbles, gates
(pause, dash) and every effect it requests.
