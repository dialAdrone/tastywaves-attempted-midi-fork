<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Effects chain (T26): design note

Status: direction accepted by the user (2026-10-18): a user-ordered chain of 4 slots; reverb may be used once. E1 built (framework, CHORUS, FLANGER; fx.h). CPU with the sequencer and lane running was 5 % (device).

## 1. Goal

CHORUS, FLANGER, PHASER, DELAY, REVERB in a chain the user orders: 4 slots in series, after the voices and the master drive (the soft clip) or before it (decision P).

## 2. Slots

- Each slot: TYPE (OFF, CHORUS, FLANGER, PHASER, DELAY, REVERB) and 3 generic parameters P1, P2, MIX (0..127). The knob labels follow the type:

| type | P1 | P2 | cost |
| --- | --- | --- | --- |
| CHORUS | RATE | DEPTH | low |
| FLANGER | RATE | FEEDBACK | low |
| PHASER | RATE | FEEDBACK | low |
| DELAY | TIME (ms or tempo rate) | FEEDBACK | low CPU, big RAM |
| REVERB | SIZE | DAMP | highest |

- Page "FX2": one knob set per slot (TYPE, P1, P2, MIX); the existing FX page (filter) stays.
- Patch: 16 parameters appended (P_FXn_TYPE, P_FXn_A, P_FXn_B, P_FXn_MIX, n = 1..4), P_COUNT 57 to 73 (limit PL_MAXP 100). Trailing defaults are not stored, so a patch with no effects costs nothing.

## 3. Resource rule (the reverb and delay limit)

Pool use is per effect type, not per slot, so the budget does not depend on the order:

| effect | instances | pool |
| --- | --- | --- |
| CHORUS / FLANGER / PHASER | any number (one per slot) | about 4 KB each |
| DELAY | once | about 96 KB (0.55 s stereo at 44.1 kHz, 16 bit) |
| REVERB | once | about 40 KB |

Once DELAY or REVERB is in a slot, the type list of the other slots shows it grayed (cannot be chosen); the slot that owns it can keep or change it. (The user asked for this for reverb; delay has the same reason: its buffer is the largest.) Worst case 96 + 40 + 16 = about 150 KB of the roughly 200 KB spare pool (T25 will enforce the map at build time; the user table slots share the rest).

## 4. Engine

- Runs in the audio path after the voice mix, in slot order, on the stereo block (or mono if the output is mono: check in E0). Per-effect state in `.pool`; the code is const tables and integer maths (Q15), no float, no 64-bit divide.
- Changing a slot's type: the new effect's buffer is cleared in the main loop (sliced), the slot's mix is held at 0 until it is ready, then ramped in. No click, no ISR stall.
- Parameters go through the one-pole smoother like the knobs. Tempo-sync for DELAY TIME uses P_TEMPO / the external clock (later).
- A bypassed slot (OFF, or MIX 0) costs one branch.

## 5. Stages

| stage | content | check |
| --- | --- | --- |
| E0 | CPU and pool measurement: a dummy effect pass, confirm output channel layout; fix the budget | host + device number |
| E1 | slot framework (4 slots, page, patch params, type change handling) with CHORUS | device |
| E2 | FLANGER, PHASER | device |
| E3 | DELAY | device |
| E4 | REVERB (once rule) | device |

## 6. Decisions for the user

- **O.** 4 slots (accepted).
- **P.** Settled by the existing hook (synth.h): the chain runs on the stereo bus before the drive and the soft clip, so the clip is also the last limiter. DRIVE therefore distorts the effects too.
- **Q.** DELAY once as well as REVERB (proposal: yes, the 96 KB buffer).
- **R.** The effect list as in section 1, nothing else for v1.

## 7. Built (E1)

- fx.h: slot framework on the existing `fx_chain` hooks; types 1 CHORUS, 2 FLANGER (one 2048-sample delay line a slot, 4 KB, in `.pool`; two taps, quarter-cycle LFO apart). Types 3..5 are in the type list (TXT) but PDESC allows 0..2 until they exist.
- Patch: 16 parameters P_FX1_TYPE.. (P_COUNT 73), defaults OFF, A 64, B 64, MIX 64. The panel: FX page sets 3..6 (press FX again): TYPE / A / B / MIX; the labels follow the type.
- Slot type changes run in the main loop (`fx_sync`): `on` = 0, clear the line, new type, `on` = 1; the mix ramps in from 0.
- Grayed delay / reverb: with the types.
- Test: tests/run_fx.sh.
