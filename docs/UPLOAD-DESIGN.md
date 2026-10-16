# User wavetable upload: design lock (T19) - DRAFT for the user and `rules-reviewer`

Date 2026-10-15. Status: **proposal, nothing built**. Facts about other firmwares and the protocol options are in
`docs/research/sample-transfer.md` (cited there); this file is the VAVEWave decision set. Anything marked **[verify]**
is an assumption to be measured on the unit before it is relied on.

## 1. Goal and scope

- Single-cycle wavetables (32 frames x 256 samples) made on a computer and loaded into **user slots** on the FM-1,
  shown in the ALGORITHM list after the 12 factory tables.
- Converter inputs: **Serum, WaveEdit, Ableton, Surge** files (all WAV; Surge `.wt` later). Not recordings: a sampled
  sound is out of scope for v1 (needs slicing/pitch detection, see research 4.7).
- First tool: a **command-line tool on the computer** (`tools/fm1_wavetable.py`). Web page later, after this is proven.
- Out of scope: one-shot sample playback, on-device recording/bounce (reuses the same write path later).

## 2. The one rule that matters: nothing outside the slot region can be written

The brick risk of the whole project is the code that erases and programs flash. Today `FL_RANGE_OK`
(`platform.c:71`) allows exactly: the OTA staging area `0xE0000..0xE4FFF` and the settings sector `0xFC000`.
Upload adds **exactly the slot region and nothing else**. The host never sends an address: it sends slot number and
offset, the firmware computes the address (slot * 0x9000 + offset), range-checks it, and the lowest-level erase and
program functions re-check (`FL_RANGE_OK`) before touching the SPI bus.

## 3. Flash layout (decision)

Free flash per research: `0x97000..0xDFFFF` (73 sectors, 292 KB). The app image must end below `0x93000`
(it currently ends well below; `./build.sh` already fails if the image is too big).

| item | address | size |
| --- | --- | --- |
| user slot 0..7 | `0x97000 + k * 0x9000` | 9 sectors: 1 header + 8 data |
| spare (test sector) | `0xDF000` | 1 sector (first write test only, section 8) |
| OTA staging (not ours) | `0xE0000..0xE4FFF` | 5 sectors |
| patch log (T17, later) | `0xE5000` | 1 sector |
| slots 8, 9 (optional) | `0xE6000..0xF7FFF` | 2 x 9 sectors; **not in v1** |
| settings | `0xFC000` | 1 sector (exists) |

- **8 slots, full-quality tables** (6 mip levels, 32,640 B per table, as `tools/gen_wavetables.py`). Slot 8, 9 only
  if 8 prove too few; the fmt byte in the header allows a top-level-only format (16 KB, mips built on load) later.
- Header (sector 0 of the slot, little endian, 64 B): magic `VWT1`, u16 version, u8 fmt, u8 frames, u8 levels,
  u8 reserved, char name[8], u32 data bytes, u32 CRC32 of the data, u32 CRC32 of the header bytes before it.
- A header that fails magic, version, header CRC, or has frames = 0 or > 32, or bytes != frames * 510 * 2 (fmt 0), is
  **an empty slot**. Never a crash, never trusted (Felucca issue #61: a bad stored project crashed the boot).
- The data CRC is checked at COMMIT and on demand, **not at every boot** (10 x 32 KB would slow the start).

## 4. Wire protocol ("VW link" v1, SysEx on the USB-MIDI port)

```
request  F0  pack7( 7D 56 57 cmd seq len_lo len_hi payload[len] sum )         F7
reply    F0  pack7( 7D 56 57 cmd rc seq len_lo len_hi payload[len] sum )      F7
sum = ~(sum of every byte from cmd up to the one before sum) & 0xFF; a request with a bad sum or length gets no reply
HELLO reply payload (16 B): proto 1, stage, slots, header length, u32 slot bytes, identity[8]
DIR request: start, count. Reply: 16 B per slot: slot, valid, frames, levels, name[8], u32 data CRC
READ request (5 B): slot, u16 offset, u16 length (<= 256), offset + length within the slot (0x9000); data sector at +0x1000
```

`pack7` is the platform's bit-stream packer (`ota.c`, twin in `tools/fm1_install.py`), id pair `56 57` keeps it apart
from the stock updater, SLOOP and zp12. One request at a time, 256-byte DATA chunks (proven at full speed on this
platform), the tool retries a timed-out request unchanged (4 tries, 1.5 s).

| cmd | request | what the firmware does |
| --- | --- | --- |
| 1 HELLO | | proto, firmware identity, slot count and size |
| 2 DIR | start, count | per slot: valid?, name, frames, bytes, CRC (from a RAM mirror made at boot) |
| 3 BEGIN | slot, fmt, frames, levels, bytes, crc32, name | refuse unless idle and quiet; releases voices on that slot, mutes, "UPLOADING" screen, erases the header sector, 15 s timeout |
| 4 DATA | slot, offset (x256), <= 256 B | first chunk of a sector erases it, programs, **reads back and compares** |
| 5 COMMIT | slot | recomputes the CRC32 over the data, compares with BEGIN's, **writes the header last**, rescans |
| 6 ABORT | slot | slot stays empty (header erased); also automatic on timeout |
| 7 ERASE | slot | erases the header sector only |
| 8 READ | slot, offset, <= 256 | returns the stored bytes (backup / audit) |
| 9 AUDITION | slot, osc | selects the table in oscillator 1 or 2 (optional) |
| 10 SELFTEST | | stage 1 only: refused unless keys up and quiet; erase `0xDF000`, check 0xFF, program 16 pages of a pattern, read back, erase, check. Reply (always rc 0): result (0 pass, 3 erase, 4 program, 5 verify, 7 refused), driver code, erase time, program time, second erase time, wrong bytes (u32 each, microseconds) |

Stage 2 payloads: BEGIN (20 B) slot, fmt 0, frames, levels 6, u32 bytes, u32 data CRC32, name[8]; DATA slot, u16 chunk index,
data (256 B, the last chunk the rest; in order; the previous chunk again is only compared; reply: u16 next chunk wanted);
COMMIT slot (reply: u32 CRC of what is stored); ABORT; ERASE slot (header sector only). Slot 0 only at stage 2, slots 0..7 at stage 3.
rc: 0 ok, 1 bad args, 2 range/alignment, 3 erase failed, 4 program failed, 5 verify failed, 6 no BEGIN/timeout,
7 busy/refused, 8 CRC mismatch, 9 no flash. Frame limit: 640 B on the wire (`sx_frame`).

## 5. Firmware behaviour rules

1. **Stop first:** BEGIN refuses unless no key is down and no voice sounds (`quiet()`); it mutes output. A sector
   erase runs with interrupts off (tens of ms), so playing through an upload is not attempted.
2. **Main loop only:** the audio ISR never touches flash, never waits. Receive path is `usb.c` -> a new
   `wt_service()` called from `platform_poll()` **before** `ota_service()`, consuming only frames with its own
   header (today `ota_take()` discards everything that is not an updater frame, `ota.c:149`).
3. **One writer function**, `wt_user_write(slot, off, src, n)`, is the only code that programs user flash. Later
   on-device bounce uses the same function (one path to audit).
4. **Engine contract:** user tables are read through the plain XIP window (needs `fl_plain_window_init()` at boot,
   not called today; it changes SFC registers, so it is **moved to stage 4**, when the engine first reads a slot.
   Stage 0 to 3 read flash only through `st_read` (the RAM driver, as settings do), in pieces of 64 B). The engine keeps a RAM directory {base, frames, valid}; BEGIN sets valid = 0
   *before* flash is touched, an invalid or missing table plays table 0 (never silence, never a crash). A patch that
   refers to user table k stores k; a patch that loads while k is empty falls back to table 0.
5. **Boot:** header scan under the watchdog; every field bounds-checked before use (frames, bytes, name).
6. **Power cut / cable pull / crash mid-upload:** the header is erased at BEGIN and written last at COMMIT, so a
   half-written slot reads as empty at the next boot. Verified in the test plan (section 8, test U6).

## 6. UI

- ALGORITHM list: 12 factory names, then the user names, empty slots skipped. Table name is 5 chars on screen
  today (status line); user names are truncated to 5 characters in the status line, 8 elsewhere **[decision A]**.
- UPLOADING screen while a transfer runs: name, percent, "DO NOT POWER OFF"; keys and knobs ignored; ends with
  `OK` or the error code for 1.5 s.
- No on-device delete in v1 (the tool's `erase` does it). HELP page 2 gets one line.

## 7. The computer tool: `tools/fm1_wavetable.py`

```
info                                   HELLO + DIR
put SLOT NAME input.wav [--format auto|serum|waveedit|ableton|surge] [--frame N] [--frames 32]
get SLOT out.wav                       READ (backup)
erase SLOT
build NAME out.vwt input.wav           no device: the exact bytes the firmware would store, plus a preview
```

- **Format detection:** Serum reads the frame length from the WAV `clm ` chunk **[verify with a real file]**;
  WaveEdit is 64 frames x 256 samples; Ableton and Surge exports are WAVs of 2048-sample frames; `--frame N`
  overrides. Without a chunk or a format the usual 2048 (or 256 for exactly 16384 samples) is assumed and printed as
  ASSUMED: statistics cannot tell a cycle from a piece of one. Mono or stereo (mixed), 8/16/24/32-bit PCM, float 32/64.
  **Surge `.wt`** (documented in Surge XT's "WT fileformat.txt": `vawt`, wave_size, wave_count, flags, int16 or
  float32) is read natively; the `is sample` flag is refused. **Ableton:** a `.alp` pack holds Wavetable *presets*
  (XML) that name Live's built-in tables, not audio, so it is not a source; Ableton-made tables are WAV of 2048-sample
  frames. Real files checked: Serum (`Prologue FatA`), WaveEdit (`E.GUITAR`), Surge (`.wt` and `HQ.wav`).
  Implemented as `build` and `inspect` (T21, no device commands yet); `tests/run_wavetable_tool.sh` covers it.
- **Conversion:** per frame, FFT truncation or zero padding to 256 samples (exact for periodic frames, discards
  harmonics above 64; with the 32-frame limit, 64/128/256-frame tables are picked evenly or interpolated), per-table
  normalisation, the **same mip generator** as the factory tables (`gen_wavetables.py`), so a user table has the same
  alias behaviour as a factory table (tested with the same spectrum test, `tests/wavetable_spectrum.py`).
- Port discovery as `fm1_install.py` (`PORT_RE`, never probe other gear); `build` works without a device so the
  converter is testable on its own.
- Licensing: all reused sources are GPL-3.0-only (`LICENSING.md` gets the origins). We ship **no** third-party
  wavetables; the tool converts files the user supplies.

## 8. Safety and recovery verification (the user's stated priority)

### 8.1 Recovery ladder, from least to most drastic

| # | Situation | Way back | Status today |
| --- | --- | --- | --- |
| R1 | A user slot is corrupt or half written | Slot reads as empty; table 0 plays; `erase` or a new `put` | design (section 5) |
| R2 | App boots but misbehaves | `fm1_install.py` over the running app (OTA stays enabled) | works |
| R3 | App fails to start twice | automatic rescue after 2 failed boots (`bootguard`) | exists, never exercised on the unit |
| R4 | App never reaches the main loop | manual USB rescue, OCT- at power-on | **does not enter on this unit (T30)** |
| R5 | Image destroyed | mask-ROM USB boot mode with the vendor tool (`jl-uboot-tool` / FM-1 Transporter, Felucca #61, SLOOP #92) and the stock `FM-1_093` package | **never rehearsed; package not yet downloaded** |

R4 and R5 are the net under a bug in the flash writer. **Before the first write test (stage 1) I want R4 either
working (T30 option A, then B) or R5 rehearsed once by you with the stock package in hand.**

### 8.2 Staged rollout (each stage: reviewer sign-off, then you flash, then a device test)

| stage | firmware change | what we learn | write range |
| --- | --- | --- | --- |
| 0 | read-only HELLO/DIR/READ, header scan on the first main-loop pass (`firmware/src/wtlink.h`) | the link works, boot unaffected | unchanged |
| 1 | writer enabled for **only the spare sector `0xDF000`**, a self-test command (erase, program, read back, verify) with timing | erase time of this flash part, program/verify path, IRQ-off effect on USB, behaviour of a power cut mid-erase | `0xDF000..0xDFFFF` |
| 2 | slot 0 only (9 sectors), full protocol, tool `put` | whole upload on one slot, cable pull test, reload | slot 0 |
| 3 | slots 0..7 | all slots, playing a user table, patch referencing it | slots 0..7 |
| 4 | engine uses user tables with LFO/BEND/mip levels; spectrum test of an uploaded table | audio quality vs factory | unchanged |

### 8.3 Tests (host first, then device)

- Host: protocol round trip against the firmware code on the computer (extend `tests/host.c`), fuzz of DIR/BEGIN/DATA
  (bad slot, offset, size, sum, order, repeat), a range test that every address the writer can produce lies inside
  the allow list (property test over all slot * offset values), converter golden files and the spectrum test.
- Device U1 identify; U2 read empty slots; U3 spare-sector self-test, 100 erase cycles timed; U4 put one table, play
  it, compare with `get`; U5 power-cycle, still there; U6 pull the USB cable mid-transfer (3 times) then power-cycle:
  slot empty, app boots, other slots intact; U7 power off mid-transfer; U8 upload while holding keys (refused);
  U9 corrupt a header on purpose (tool command `--corrupt-test`, stage 2 only): boots, slot empty; U10 15 minute soak
  playing a user table with an upload to another slot afterwards; U11 firmware update over the top (R2): do the user
  tables survive? **[verify: does `loader.c` erase `0x93000..`? its range allows it]**

## 9. Decisions (user, 2026-10-15)

- **A.** 8 characters stored, 5 shown in the status line: yes.
- **B.** Replace in place (old table gone at BEGIN; `get` is the backup): yes.
- **C.** 8 slots in v1: yes.
- **D.** Way back: the user has Baud Girl from the Hub and the M-VAVE firmware installer saved locally (R5 package in
  hand). T30 option A is therefore not a gate for stage 1; it stays queued.
- **E.** Stage 1 may erase and program the one sector at `0xDF000`: yes (still needs `rules-reviewer` and the user
  flashing).

## 10. Task split (replaces T19-T21 text in TASKS.md)

T19 this document (+ your decisions). T20a stage 0 firmware. T20b stage 1 self-test. T20c stages 2-3. T21 the tool
(`build` first, no device needed, can start now). T30 option A before stage 1.

## 11. Notes from the stage 3 review (2026-10-16)

- **Updates keep the slots only with the Felucca loader.** By the code, `firmware/loader` erases/programs `[0x4000, 0x93000)` only (plus
  `ldr_records_drop` after success, which erases a sector above 0x93000 only if its last 256 bytes form a valid update record: about 2^-32
  per slot data sector). The vendor loader `usb_hid_ota.bin` (accepted by `ota_stage`) and the stock FM-1_093 package are unknown and
  should be assumed to lose the slots: back slots up with `get` before such an install (recovery R5).
- **Stage 4 must check the data CRC32** (header offset 22) when the engine first loads a table: `wt_parse` trusts the header alone, so a
  valid header over damaged data would otherwise play.

## 12. Stage 4: layout v2 (2026-10-16)

The engine reads tables in place in its own layout (`firmware/gen/wt_bench.h`: 7 mip levels, each `[3 pre guards][256 >> L samples][4 post guards]`,
557 int16 per frame, 32 frames = 35,648 B), which does not fit the 8 data sectors of layout v1 (32,640 B, 6 levels, 1 guard). So the slot
changed, keeping 8 slots of 9 sectors at the same addresses:

- **data** at slot offset 0 (35,648 B, sectors 0..8, tail in sector 8 up to 0x8B40);
- **header** (64 B, same fields, version 2, fmt 1, frames 32, levels 7, bytes 35,648) at slot offset 0x8F00, the last page of sector 8,
  a page no data chunk touches;
- **BEGIN** erases sector 8 (data tail + header): the slot reads empty once that erase has completed (about 45 ms; a power cut INSIDE it, on a slot that already holds a table, can leave the header page intact over a damaged data tail: the data CRC32 check before first use, stage 4b, is therefore mandatory, not optional; a possible extra hardening is to program a zero byte over the header magic before the erase, which needs its own review); sectors 0..7 are erased as their first chunk arrives;
  COMMIT writes the header last. Header absent or damaged = empty slot, as before.
- Layout v1 slots (the first test tables) read as empty (their bytes at 0x8F00 are sample data) and must be uploaded again.
- READ / DIR unchanged; the tool reads the header from 0x8F00 and the data from 0. `.vwt` files are header + data as before, 35,712 B.

Stage 4 is split: **4a** (this commit) = layout v2 + `XIPCHECK` (cmd 11), which sets up the plain flash window on its first call (not at boot)
and compares the slot read through XIP with the SPI read; the engine does not use the slots yet. **4b** = the engine and the ALGORITHM list use
valid user tables (data CRC32 checked first, fallback to table 0), then `fl_plain_window_init()` moves to boot if the 4a device test is clean.

## 13. Stage 4b: the engine plays user tables (2026-10-16)

- **Table index** 0..11 factory, 12..19 = user slot 0..7 (`usertab.h`; `P_TABLE1/2` range 0..19). The engine finds a table with
  `eng_tbl(t)` (synth.h): a user table only if `eng_user_base[k]` is set, else table 0: an empty, unchecked or damaged slot never
  silences a voice and never makes the ISR read flash that is not a table.
- **`eng_user_base[k]` is set only after the data CRC32 matched the header's**: by `wt_verify` (main loop, `wt_user_poll`, one slot a pass, about
  0.15 s over SPI, for the tables the patch refers to) or by COMMIT (which just checked it). **BEGIN and ERASE clear it before the slot is
  touched.** A mismatch marks the slot BAD (shown as BAD, skipped by the encoder, plays table 0).
- **The plain flash window** (`fl_plain_window_init`) is now set up on the first main-loop pass after boot (proven by XIPCHECK, stage 4a), so
  the engine can read slots in place through the cache (24 ns/byte measured, same as other XIP reads).
- **UI**: ALGORITHM steps through the 12 factory tables, then the user tables that exist (empty and BAD slots skipped); the user name is shown
  in full (8 characters) on the big views and as 5 characters on the status line (decision A); an unknown index shows EMPTY.
- Host test: `DINGHY_USERTAB` + `patch` script lines (tests/host.c) and tests/scripts/engine_user.txt: a user table at the exact pitch,
  fallbacks for BAD / unchecked / empty slots, encoder stepping and skipping, under UBSan.
- **Device checks (docs/DEVICE-TEST-PLAN.md section 9)**: the CPU cost of a user table (OSC1, OSC2, both; worst case) against the factory numbers.
