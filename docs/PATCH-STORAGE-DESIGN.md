<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Patch storage (T17): design note

Status: **proposal, waiting for the user's decisions A-F (section 9)**. No code yet. Written 2026-10-16.

## 1. Today

- 8 factory patches in the firmware (`PRESET[]`, `panel.h`); PRESETS encoder browses them, a patch loads after 400 ms.
- SAVE keeps **one** edited patch, as an overlay on the factory patch it came from, inside the settings sector
  (`app_set_t.uval[]` / `uslot`, 96 B of the 256 B). SAVE on another slot replaces it. Octave, look and the last slot
  live in the same sector (`SETTINGS_AT 0xFC000`, `settings_save`: erase, write, header last).
- A patch is `patch.p[P_COUNT]` (48 parameters, ranges in `PDESC`). `P_TABLE1/2` 12..19 point at user wavetable slots.

## 2. Goals

1. Keep many user patches, each with its own save, surviving power-off and firmware updates by our loader.
2. A power cut or a cable pull during a save never loses a patch that was already saved (the old version stays).
3. Parameters added later (effects, arp, sequencer) never invalidate saved patches.
4. The same store can later hold other record types (sequencer patterns, T27) without a format change.
5. Backup and naming from the computer (the web tool) in a later stage.

Non-goals now: sequencer patterns (own design, T27), patch import from other synths, MIDI patch dump.

## 3. Flash layout

Region **`0xE5000..0xE8FFF`: 4 sectors** (16 KiB), directly above the OTA staging area `0xE0000..0xE4FFF`, which
stays untouched. That leaves `0xE9000..0xFBFFF` (19 sectors) free, enough for two more table slots later
(`UPLOAD-DESIGN.md` section 3: slots 8 and 9, 18 sectors).

`FL_RANGE_OK` gains exactly this window. It is a flash-write path change: user agreement, `rules-reviewer`,
device test, same as the upload stages. Everything else about the rescue path stays as it is.

## 4. Record format

Append-only **log** over the 4 sectors (a ring). A record is written once and never modified; a newer record with the
same id replaces it. All integers little endian, records start on a 4-byte boundary.

| offset | size | field |
| --- | --- | --- |
| 0 | 2 | length of the whole record in bytes (a multiple of 4; 0xFFFF = erased flash = end of log in this sector) |
| 2 | 1 | type: 1 patch, 2 tombstone (deleted id), 3.. reserved (patterns, T27) |
| 3 | 1 | id: patch number 0..31 |
| 4 | 4 | sequence number, increasing by one per record written (the newest wins) |
| 8 | 1 | format version (1) |
| 9 | 1 | parameter count N (P_COUNT when written) |
| 10 | 12 | name, ASCII, zero padded (default `U01`..`U32` plus the base name) |
| 22 | 2 | reserved, 0 |
| 24 | 2N | parameters, int16, in `P_*` order |
| 24+2N | pad | zeros up to a multiple of 4 |
| end-4 | 4 | CRC32 over every byte before it |

48 parameters give a 124 B record, 32 per sector; 32 live patches fill one sector. A record is written in two steps:
everything from byte 2 on (including the CRC), then the 2 length bytes last. Until then the position still reads as
erased (0xFFFF), so a power cut at any point leaves either nothing or a complete record, the "header last" rule of the
settings and the table slots. A record is also checked on reading: sane length, inside its sector, CRC matches; anything
else is skipped.

**Compatibility rule (goal 3):** the parameter enum is append-only; no parameter is ever reordered, removed or
reinterpreted. Loading a record with N < P_COUNT fills the rest from `PDESC[].def`; N > P_COUNT (a newer firmware's
patch) ignores the extra values. Every value is clamped with `pclamp` on load, so a damaged record cannot put the
engine out of range (the engine clamps again anyway).

## 5. Operations

All run in the main loop, never in the ISR, and (like the settings save) only when `quiet()` (an erase stops the
audio interrupt for about 17 ms; programming a record is 1-2 pages, about 2-4 ms) and not while an upload is running.

- **Boot scan**: read the 4 sectors with SPI reads (about 16 KiB, a few ms), keep in RAM an index `id -> flash offset,
  sequence` (32 x 8 B) plus the write head (first erased position) and the highest sequence. Records that fail their
  checks are skipped. Newest valid record per id wins; a tombstone newer than every patch record removes the id.
- **Load**: read the record into a RAM buffer (<= 256 B), validate, `patch_set` each parameter. Same path for factory
  patches except that those come from `PRESET[]`.
- **Save**: append a record (sequence + 1) at the head; update the index only after the write returned success and a
  read-back compare matched. A failed write leaves the index and the patch "modified"; press SAVE again (as today).
- **Delete**: append a tombstone for the id.
- **Space**: when the head sector cannot hold the next record, move on to the next sector of the ring, which must be
  erased. After a move, if fewer than 2 sectors are erased, **compact**: copy every live record (newest per id, not
  deleted), with its original sequence number, into erased sectors at the head; verify each copy by read-back; only
  then erase the sectors that now hold nothing live. Live data is at most 4 KiB (32 patches), so a ring of 4 always
  has room: 1 sector live + the head + 2 erased.
- **Power cut anywhere**: a torn record fails its CRC; a cut during compaction leaves the old records in place and at
  worst duplicates (same id, same sequence, same bytes), which resolve to the same patch; the old sectors are erased
  only after every copy verified; a cut during that erase leaves a sector of garbage that fails CRC record by record
  (an erased-then-half-programmed sector is not a risk: nothing is programmed into a sector before its erase finished).
  In every case the user sees the last patch version that was completely written.
- **Wear**: one sector erase per about 32 saves per sector, spread over 4 sectors; the part is rated for about
  100,000 erases per sector. Not a concern.

## 6. What a patch contains, and the table question

A patch stores the 48 parameters as they are, including `P_TABLE1/2`. A user table is referred to **by slot number**:
if the slot is later replaced by another table, the patch plays the new one; if the slot is empty or damaged it plays
table 0 and the screen says EMPTY / BAD (stage 4b behaviour, device-tested). The patch does not store table contents or
a table CRC (decision F offers a CRC check; the web library, section 8, can show a mismatch without firmware help).

## 7. Panel behaviour (the part the user feels)

- **List**: PRESETS steps through the 8 factory patches `F1..F8` and then the existing user patches `U01..U32` in
  numeric order (empty ids skipped, as empty table slots are in ALGORITHM). The browse and 400 ms load behaviour
  stays. Name line: `U03 NAME`.
- **SAVE on a user patch**: replaces it in place (a new record with the same id). On a factory patch: goes to the lowest
  free user id and the screen shows `SAVED U05`. Holding SAVE and turning PRESETS chooses another target id before
  release (existing id: the screen shows `REPLACE U07?` and a second press confirms). No change since load: only the
  confirmation, as now.
- **Delete**: hold SAVE for 2 s while browsing a user patch: `DELETE U05?`, press SAVE again to confirm. Factory patches
  cannot be deleted. (Decision C: which gesture.)
- **Names**: no text entry on the panel. Default `U05` + the name of the patch it was edited from, 12 characters.
  Renaming comes with the web library (section 8).
- **Settings sector**: keeps octave, look and the last patch (now 0..39: F1..F8 = 0..7, U01.. = 8..39). The old
  overlay (`uval`, `uslot`) is no longer written. **Migration (decision E)**: at boot, if the old overlay is present and
  the log has no patch, it is written once as U01 and the overlay is cleared.

## 8. Computer side (later stage, separate rollout)

VW link v2 commands `PDIR`, `PGET`, `PPUT`, `PDEL` (patch directory, read, write, delete), the same pattern as the
table slots: the firmware validates and writes records only through the log code above. The web tool gets a patch
library: list, rename, reorder, export all to a file and restore (the backup story), and show which table slots each
patch refers to. This needs its own staged rollout and reviewer pass; it is not part of the first firmware stage.

## 9. Decisions for the user

- **A.** 32 user patches (4 banks of 8 in the head, shown as one list U01..U32). Enough, or more? (Each 32 more needs
  one more live sector and a larger ring; layout above has room for 64 if the ring grows to 6 sectors, which then
  conflicts with the two optional table slots.)
- **B.** SAVE on a factory patch goes to the first free user id; hold SAVE + PRESETS to pick the target. OK?
- **C.** Delete by holding SAVE 2 s on a user patch, then confirm. OK, or another gesture / no delete on the panel
  (delete only from the web library)?
- **D.** Names: default `U05` + base name now, rename from the web library later. OK?
- **E.** Migrate the single overlay patch in the settings sector to U01 on first boot. Do you have a patch there you
  want kept? (If not, skip the migration code.)
- **F.** A patch refers to user tables by slot only (no CRC check). OK, or check the table CRC and show "TABLE CHANGED"?

## 10. Staged rollout

| stage | content | device check |
| --- | --- | --- |
| P0 | the log code (`patchlog.h`) on a NOR mock on the computer: all operations, fault injection (failed erase / program, power cut at every write step incl. mid-compaction), index rebuild from any state; no firmware wiring | none (host only) |
| P1 | wire into the app: save, list, load, delete, migration, `FL_RANGE_OK` window; reviewer | save 3 patches, power-cycle, they are back; save over; delete; fill 32 (compaction runs); cable/power cut during a save: previous version intact; firmware update keeps them |
| P2 | VW link v2 patch commands + web library (rename, backup, restore) | backup, wipe, restore, compare |

Tests as for the table link: `tests/run_patchlog.sh` with the NOR mock and fault injection, panel script tests
(`tests/scripts/panel_ui.txt`) for the list / SAVE / delete screens, and a section 10 in `docs/DEVICE-TEST-PLAN.md`.
