<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Patch storage (T17): design note

Status: decisions A-F accepted as recommended (2026-10-16). P0 (the log) and P1 (the panel) are written; see section 10 for the stages.
Details settled while building P1: holding SAVE and turning PRESETS starts from the id a short press would use (the patch's own id, or the first free one) and moves one id per click, clamped to U01..U32; `*` marks an occupied id; a short press with nothing changed since load shows the confirmation only; the saved record is named after the patch it was made from (factory name, or the loaded user patch's name); a deleted patch loads the patch before it in the list. Messages: SAVED Unn, DELETED Unn, STORE FULL, SAVE FAILED, DEL FAILED.

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
| 22 | 1 | pattern words PW (0..33; 0 in the first records), the note pattern and mod lane of the sequencer (section 11) |
| 23 | 1 | reserved, 0 |
| 24 | 2N | parameters, int16, in `P_*` order |
| 24+2N | 2PW | pattern words, int16 |
| 24+2N+2PW | pad | zeros up to a multiple of 4 |
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
- **Space**: when the head sector cannot hold the next record, move on to an erased sector. A new head is taken only
  while at least 3 erased sectors remain; otherwise **compact** first: copy every live record (newest per id, not
  deleted, with a fresh sequence number) into an erased sector, verify each copy by read-back, and only then erase the
  sectors that hold nothing live, **strictly oldest first, stopping at the first sector that still holds live data or
  whose erase failed** (so a tombstone is never erased before an older record of its id). Live data is capped at one
  sector (3,968 B for 32 patches of 124 B), so the copy always fits, and two erased sectors stay in reserve so that one
  power cut inside a compaction still leaves one. If no erased sector is left anyway (cuts in two successive
  compactions), the oldest sector's live records are copied into the head's free space and the sector erased; when the
  head has no room they go through RAM (erase, then rewrite), the one step where a power cut or a program fault can
  cost the records of that one sector.
- **Power cut anywhere**: a torn record fails its CRC; a cut during compaction leaves the old records in place and at
  worst duplicates (same id, same bytes, newer sequence), which resolve to the same patch; the old sectors are erased
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

## 11. Sequencer pattern in the patch (T28 stage S4)

A record may carry the note pattern and the mod lane: PW words after the parameters. Word 0 is the length (1..32), then one
word per step: note (bits 0-6), sounds (bit 7; clear = rest), tied (bit 8), lane value + 64 (bits 9-15). A 16 step pattern is
17 words (34 B), the most is 33 words (66 B). The record limit stays 256 B, so PL_MAXP dropped from 100 to 80 (P_COUNT is 73): the
largest record is 80 parameters + 33 words = 254 B. A firmware that does not know PW (older than S4) finds a length that does not
match its parameter count and skips the record, so a downgrade hides patches that have a pattern.

- **Save**: the pattern goes in only when the sequencer is on (SEQ) and a pattern exists. A patch without it is as small as before.
- **Load**: a patch with a pattern replaces the pattern in RAM (the ISR plays nothing while the arrays change). A patch without
  one leaves the pattern as it is: the sequencer is a performance switch, like the arp, so changing the sound does not stop the tune.
- **Modified**: a changed pattern or lane makes SAVE blink, like a parameter edit.
- **Capacity**: live data is capped at one sector (4,096 B). A record with a 16 step pattern is about 200 B, so about 20 such patches fit
  (STORE FULL after that); plain patches are not affected.
- **Downgrade**: going back to a firmware older than S4 hides patches saved with a pattern, and that firmware's next compaction erases them for good. Back up with the web library (section 12) before downgrading.
- **Known, accepted**: loading a pattern while keys are held for transposition can leave one held key sounding as a plain note until released (a 1-2 microsecond window per block); SAVE blinks for a pattern edit made with SEQ off although such a pattern is not stored.

## 12. Patch library over the VW link (T17 stage P2)

VW link stage 5 (HELLO reports stage 5) adds four commands, handled by `patchlink.h` and answered only when no table upload runs
(rc 7 busy). The firmware writes only through `pl_put` / `pl_del`, so every guarantee of sections 4 and 5 holds (record written
whole or not at all, old version kept on a cut). A patch body (what PGET returns and PPUT sends after the id) is
`name[12] | n | pw | int16 v[n] | int16 pattern[pw]`, little endian; values are not interpreted (the panel clamps on load).

| cmd | name | payload | reply |
| --- | --- | --- | --- |
| 12 | PDIR | start, count (1..16) | per id 16 B: id, present, n, pw, name[12] |
| 13 | PGET | id | the body (rc 10: none) |
| 14 | PPUT | id, body | rc 0, 11 store full, 4 flash error, 1 bad shape (n > 80, pw > 33, wrong length, non-printable name, id > 31) |
| 15 | PDEL | id | rc 0 (also when there was none) |

The panel learns of a change through `pl_link_dirty` and refreshes the names it shows; a patch already loaded stays as it is in RAM.
PPUT and PDEL are refused with rc 7 while keys are down or sound plays (like the table link), a PPUT of a body the id already holds writes nothing (a retry after a lost reply is free), rc 12 means the stored record is damaged. If the loaded patch is replaced or deleted from the computer the panel reloads it from the log, so SAVE cannot name or replace the wrong patch.

The web tool (`tools/web`, section "Patches on the FM-1"): list with the user tables and pattern each patch uses; **rename** (get, new
name, put); **reorder** (the list order is the id order: swap two ids; through a free id as a parking place, read fresh from the FM-1, so no patch exists only in the browser's memory; with all 32 ids in use the move is refused; if a write fails the parked copy is kept and named); **delete**; **backup** (JSON:
format, version, per patch id, name and body in hex); **restore** (each patch back under its own id, confirm first, patches not in
the file stay, every write read back). Tests: `tests/wtlink_check.c` (the commands on the real log), `tests/webtool_check.mjs`
(the library against the firmware logic), `tests/web_smoke.sh` (the page).
