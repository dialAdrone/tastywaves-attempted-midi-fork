# Sample / table transfer on the FM-1: what the existing firmwares do (research, 2026-10-12)

Question: how do SLOOP, zp12, sloopDX (and Felucca, their parent) get audio data from a computer into an FM-1's
flash, what may VAVEWave reuse, and what should its table-upload protocol look like (WAVETABLE_DESIGN.md sections 9, 10).

Sources read (shallow clones, nothing executed; all on GitHub, fetched 2026-10-12):

| Short | Repo @ commit |
| --- | --- |
| SLOOP | `isod89/sloop-fm1`@8fad1a1 (2026-10-11) |
| zp12 | `zvenson/zp12`@87bbabd (2026-10-12) |
| sloopDX | `zvenson/dxsloop`@7dc1ca1 (2026-10-11) |
| Felucca | `hugelton/Felucca`@ef1f561 (2026-10-11) |

"[fact]" = read in the code/docs; "[inference]" = my reading, not verified on hardware.

## 0. Two corrections to the premise

1. **SLOOP is not commercial.** [fact] It is GPL-3.0-only, public on GitHub (420 stars), and no file in SLOOP, zp12 or
   sloopDX mentions a price, licence key or paid tier. The only money is a tip jar (zp12/sloopDX: `paypal.me/zvenson`;
   the hub: "free and stays free"). The word "non-commercial" that appears in SLOOP's protocol doc is the MIDI
   SysEx manufacturer id `7D` ("non-commercial / educational use"), not a licence (`web/EDITOR_PROTOCOL.md:26`).
2. **No firmware samples from the FM-1 itself; "sampling" is done in the browser.** [fact] zp12's "sampling in the
   browser" records a microphone, interface or line-in *on the computer* (`getUserMedia` + an AudioWorklet in
   `web/editor.html:399-437`), converts to 12 bit on the PC, and uploads the bytes (README.md:72-75: "No firmware
   needed for this part"). SLOOP's "PUNCH-IN" (`firmware/src/punch.c:1-9`) is a live *effect* (loop/stutter/reverse
   of the last 0.74 s of the mix, ring in `.pool`, run in the audio ISR); it never writes flash. zp12's `sp_punch.c` is
   the same idea. Resampling the mix into a pad is only a TODO in zp12 ("after the flash write test of sampling in
   the device", `TODO.md:21`). The FM-1's USB "audio input" in these firmwares is the device *as a recording
   source for the computer* (UAC1 IN, `usb.c:11-19`); no host-to-device audio streaming exists. So there is no
   on-device audio-capture code to borrow; only host-to-flash transfer.

## 1. Licences and source availability

### 1.1 Facts

| Project | Licence (exact) | What is open | Evidence |
| --- | --- | --- | --- |
| **SLOOP** (isod89, based on Felucca) | `GPL-3.0-only` (GPL v3 text in `LICENSE`; GitHub reports `gpl-3.0`). | Firmware (`firmware/`), tools (`tools/`), web installer and editor (`web/`), host tests, **protocol doc** `web/EDITOR_PROTOCOL.md`. Build scripts, `.fwsc` releases. | `LICENSING.md:1-30` (titled "Felucca licensing", the file SLOOP inherited): "Its **code** is licensed under ... `GPL-3.0-only`"; every file with an `SPDX-License-Identifier: GPL-3.0-only` header. `firmware/src/editor.c:1-2` has that header. |
| **zp12** (zvenson / Sven Trogus, based on SLOOP + Felucca) | `GPL-3.0-only`. README badge "GPL-3.0". Files carry `SPDX-License-Identifier: GPL-3.0-only (C) 2026 Sven Trogus` (`web/zp12link.js:1`, `web/firmwarehub.html:2`; `firmware/src/sp_link.c:1` has the SPDX line only). | Firmware, `web/` (editor, backup page, `zp12link.js`), tools, site. | `LICENSE`, `LICENSING.md`, `README.md` header. |
| **sloopDX** (zvenson, fork of SLOOP) | `GPL-3.0-only`; `dx7_core.c` is `Apache-2.0 AND GPL-3.0-only` (msfa port). | Same as SLOOP + DX7 engine, `tools/gen_dx7_*`. | `LICENSING.md:1-60`. Issues are disabled on the repo (GitHub API). |
| **Felucca** (Leo Kuroshita / Hügelton, the common ancestor) | `GPL-3.0-only` code. **Assets** (icon atlas `assets/icons.png`, `docs/panel.jpg`, the "Hügelton Sample Pack" from `tools/gen_waves.py`) are "all rights reserved, licence terms will be published later". Section 7 additional permission: you may combine the GPL code with those assets and convey it, following their terms. | Firmware, web installer/editor, tools, protocol doc. | `LICENSING.md:1-45`. |
| **FM-1 Firmware Hub** ("Make Firmware, Not War", `fm1.designburgapps.com`, start page `thefirmwarehub.com`) | `GPL-3.0-only (C) 2026 Sven Trogus` on the page sources. | **Lives in the zp12 repo**: `web/fm1.html`, `web/firmwarehub.html`, `tools/fm1_sync.py`, `tools/make_fm1.py`, doc `FM1-SWITCHER.md`. It installs `.fwsc` packages (using Leo Kuroshita's `fm1ota.js`, also GPL-3.0-only); it has **no** sample/table transfer function. | `web/fm1.html:2`, `FM1-SWITCHER.md:1-32`. It lists only open-source GPL-3.0 music firmwares ("Closed or paid firmwares are not listed", `web/fm1.html:324`); mirrors unchanged release packages under GPL section 4/6 (`FM1-SWITCHER.md:28-32`). |

Third-party items inside: msfa (Apache-2.0), CrispyZebra (GPL-3.0), DaisySP/Rings (MIT), Terminus font (OFL-1.1),
Fukiai font (MIT, web editor), CC0 samples, JieLi SDK blobs (Apache-2.0, read at build time, not in the tree)
(`LICENSING.md` of each). Dinghy's own `LICENSING.md` already credits Felucca + SLOOP + zvenson for the platform.

### 1.2 What we may reuse in a GPL-3.0-only project [inference from the licences above]

- **Code: yes**, from all four, because they are GPL-3.0-only like Dinghy, and Dinghy's platform (`usb.c`, `ota.c`,
  `fm1_flash.h`) is already derived from the same tree. Conditions: keep each file's SPDX/copyright header
  (Leo Kuroshita; isod89 for SLOOP-origin files; Sven Trogus for zp12-origin files), record the origin in the file
  or `LICENSING.md`, and distribute the corresponding source under GPL-3.0.
  Candidates: `editor.c` `ed_unpack7`/`ed_pack7` and the SMP_* handlers (SLOOP), `sp_link.c` (zp12), `tools/fm1_sample_upload.py`
  and `tools/sampleio.py` (WAV reader, resampler, YIN pitch) (SLOOP), `web/zp12link.js` (zp12: `Link` class,
  `crc32`, directory with two copies).
- **Ideas / wire format: yes**, with no obligations (a protocol layout is not copyrightable, and the id `7D` is free
  for anyone). `EDITOR_PROTOCOL.md` is documentation and is not named in the "what is code" list of
  `LICENSING.md`; treat its *text* as not-for-copying, its *facts* as free.
- **Not reusable as is:** Felucca assets (icon atlas, panel image, Hügelton Sample Pack): no licence yet. The
  designburg logo in Dinghy's `assets/` is already marked "all rights reserved, not under the GPL".
- **Commercial restrictions: none found.** The hub's rule "Closed or paid firmwares are not listed" is a
  listing policy, not a licence term. It does mean VAVEWave must stay GPL-3.0 with source to be listed (the hub's
  catalogue is "open source (GPL-3.0) and publish a `.fwsc` release", `FM1-SWITCHER.md:20-22`).

## 2. How data gets from the host to the FM-1

Everything is **SysEx over the USB-MIDI port**; the same port carries the updater. No WebUSB, no UAC out. The
browser tools use Web MIDI with `sysex: true` (`sloop-fm1/web/editor.html:339-341`); the Python tools use
mido + python-rtmidi (`tools/fm1_sample_upload.py:33-60`). [fact]

There are three generations, all on the same low-level path:

| | Generation | Where | Wire | Semantics |
| --- | --- | --- | --- | --- |
| A | **Felucca/SLOOP "SMP" commands** 11..15 | `editor.c:21`, `761-820`; `web/EDITOR_PROTOCOL.md:63-67, 99-111` | `F0 7D 46 4C cmd args F7`, data in SLOOP "pack7" (a top-bits byte per group of 7) | Slot-relative: BEGIN / WRITE(offset) / END(header with CRC) |
| B | **Felucca/SLOOP BACKUP_PUT** (36 in SLOOP, 67 in Felucca) | `EDITOR_PROTOCOL.md:252-265`; Felucca `web/EDITOR_PROTOCOL.md:228` | same | begin(size, CRC32) / data(offset) / commit / abort, staged in RAM, checked at commit, written A/B |
| C | **zp12 raw flash link** | `zp12/firmware/src/sp_link.c:1-139`, `web/zp12link.js` | `F0 pack7(7D 'Z' 'P' cmd addr:3 len:2 data sum) F7`, bit-stream pack7 as the updater's `ota_pack7`, `sum = ~(bytes from cmd on)` | HELLO, READ(addr,n<=512), ERASE(addr), WRITE(addr,data<=512, read back and compared), HOLD, REBOOT, RELOAD, ASSIGN, PADS. All logic (allocation, directory, CRC) in the browser |

### 2.1 Reception (shared platform, identical in Dinghy) [fact]

- **USB poll runs in the timer5 ISR at 2 kHz** (`sloop-fm1/firmware/src/usb.c:4`, `usb_poll` at `:1160`; Dinghy
  `firmware/src/usb.c` same). Each USB-MIDI event packet goes to `sysex_byte()` (`usb.c:675-725`), which collects the
  7-bit bytes between F0 and F7 into **one** 640-byte buffer `sx_frame` (`usb.c:149`), sets `sx_ready` and
  `sx_frame_len`.
- **One frame at a time.** A frame that starts while `sx_ready` is set is dropped whole, no reply (`usb.c:140-146`
  comment, `sx_collect = !sx_ready` at `:694`). More than 640 bytes is dropped as "not ours". Felucca measured it:
  256-byte pieces sent one at a time are all taken (560/560, about 10 ms each); sending the next before the reply
  loses every second piece (`Felucca/web/EDITOR_PROTOCOL.md:439-449`).
- **Handled in the main loop**, not the ISR: `main.c:231, 278` call `ed_service()` (`editor.c:1142-1166`), which
  peeks via `ota_frame_get()`, handles the frame in place (`ed_handle`), frees it with `ota_frame_done()`, and the
  reply is queued by `ota_wire_send()` (`usb.c:815-851`: 64-entry TX ring `SXQ`, blocks up to 200 ms waiting for room).
  Frames with another header are left for `ota_service()` (the updater).
- **Dinghy gap:** Dinghy has no such dispatcher: `ota_take()` consumes **every** pending frame and discards what is
  not a `00 59` updater frame (`firmware/src/ota.c:149-163`, called from `platform_poll()` `platform.c:261`).
  A new service must run before `ota_service()` and only consume frames with its own header, as `ed_service` /
  zp12's `zl_service` do (`sp_link.c:34-47`).

### 2.2 Message format [fact]

SLOOP/Felucca editor frame (`editor.c:3-23`): `F0 7D 46 4C <cmd> <args> F7`. Values: v14 = two 7-bit bytes LSB first
offset by 8192; strings ASCII ended by 0; 3-byte offsets `a|b<<7|c<<14`. Every request gets exactly one reply with
the same cmd; unknown requests get none. Binary payload uses **pack7**: groups of up to 7 bytes preceded by a byte
with their top bits (`editor.c:75-98`; Python twin `tools/fm1_sample_upload.py:28-35`). Expansion 8/7.
There is **no per-frame checksum** in A and B (USB itself checks packets; a truncated frame is detected by the
length/offset checks and by the CRC32 at the end). zp12's C format adds the one-byte `~sum` per frame
(`sp_link.c:34-57`) and reuses the updater's bit-stream pack7.

SMP slot commands (SLOOP 2.x, `EDITOR_PROTOCOL.md:63-67`, `editor.c:761-800`):

| cmd | Request | Action | rc |
| --- | --- | --- | --- |
| 11 SMP_BEGIN | slot 0..3 | erase the slot's header sector (slot invalid from now), mark "open" | 0 ok, 1 erase failed |
| 12 SMP_WRITE | slot, off (3x7 bit, multiple of 256, >= 512), pack7 data <= 256 B | first write into a 4 KiB sector erases it, then program, in increasing order | 0 ok, 1 args, 2 erase, 3 write, 4 slot in use / no BEGIN |
| 13 SMP_END | slot, pack7 480-byte header | check header and zones, then **CRC32 of the data read back from flash through XIP** vs header CRC, then program the header **last**, rescan | 0 ok, 1 size, 2 header, 3 data CRC, 4 flash, 5 zones, 6 no BEGIN |
| 14 SMP_ERASE | slot | erase the whole slot (~1 s) | |
| 15 SMP_INFO | | per slot: zones, name, KiB | |

Slot format (`eng_sample.c:36-56`, `EDITOR_PROTOCOL.md:99-111`): 80 KiB slot, header at 0 (magic `FSMP`, version, zone
count, 8-char name, data length, CRC32 (zlib) of data, 16 zone records), IMA-ADPCM data at +512. Offsets of slots
(`eng_sample.c:42-45`): USR1..3 at `0xA0000 + k*0x14000`, USR4 at `0xE7000`.

BACKUP_PUT (`EDITOR_PROTOCOL.md:252-265`): `begin(id, length u35, CRC32 u35)`, `data(id, offset u35, pack7 <=256 B,
in order)`, `commit(id)`, `abort(id)`; the object is staged in RAM, validated at commit "as a load validates it",
rc 5 if there was no begin or it was more than 15 s ago. This is the best template for commit/abort.

zp12 sample flow (`web/zp12link.js` `upload` at `:352`, `writeDir` at `:242`, directory constants at `:204`):
HOLD (sequencer stops, autosave suppressed for 10 s), then for each 4 KiB sector ERASE + 8 WRITE of 512 B (all-0xFF
chunks skipped), each WRITE read back and compared by the device; finally a **directory** (magic `ZPSM`, generation,
CRC32 of the 24 slot records) is written to the *older* of two sectors (`0xD8000`/`0xD9000`), then RELOAD. The newer
good copy wins on load (`sp_samples.c:36-47`). Power loss during the data phase leaves the old directory valid; the
only loss is a half-written free area.

### 2.3 Speed [fact for Felucca's number, inference for the rest]

About 10 ms per 256-byte piece round trip (Felucca issue measurement above) = ~25 KB/s. An 80 KiB SLOOP slot is
320 pieces + 20 sector erases; the docs say an erase is "~1 s per slot" (`EDITOR_PROTOCOL.md:66`), so a full slot
takes about 5 s [inference]. The Python tool prints elapsed time (`fm1_sample_upload.py:133`). zp12 uses 512-byte
chunks (`ZL_MAX`, `sp_link.c:15`), half the round trips. Our 32,640-byte table is 128 pieces of 256 B: **about
1.5-2 s** including 8 erases [inference].

### 2.4 Flash writes while playing [fact]

- All erase/program is done by RAM-resident functions (`RAMFN`, `fl_erase4k_ram`, `fl_prog_ram`,
  `hal/fm1_flash.h:171-215`) with **interrupts off** for the call: a 4 KiB erase is one `fl_erase4k_quiet`
  (`felucca.c:89-105`), which also **zeroes the audio DMA buffer inside the same IRQ-off window** so the looped
  stale half does not buzz (`audio_silence`, `felucca.c:89-95`). Programs are 256-byte pages, one IRQ-off window each
  (`fl_write`, `fm1_flash.h:244-258`). Watchdog fed per sector (`editor.c:117`).
- SLOOP does **not** stop playback for SMP_* writes: it only zeroes `usr_nz[k]` and the slot's zone table so that
  sounding voices end instead of reading 0xFF (`editor.c:108-121`; `eng_sample.c:80-102`). Felucca (newer) instead
  **stops the transport first** for every flash write from the editor and refuses with a nonzero rc if it does not
  stop within 100 ms (`Felucca/web/EDITOR_PROTOCOL.md:489-495`). zp12 uses HOLD.
- After programming, `fl_inval()` drops cached lines and `ed_smp_inval` re-invalidates under IRQ-off
  (`editor.c:102-106`) so XIP reads see the new data.

### 2.5 Where it lives, how it is validated and used afterwards [fact]

- **Region:** inside Felucca's data region `FL_DATA 0x97000..0xE0000` plus 0xE5000.. / 0xFC000.. pieces
  (`fm1_flash.h:38-50`, SLOOP version `:38-58`). SLOOP USR1..3 `0xA0000..0xDBFFF`, USR4 `0xE7000..0xFAFFF`.
  The app image ends at 0x93000 (`loader.c:20`: loader window `0x4000..0x93000` image, `0x93000..0xFC000` data).
- **Reading:** through the **plain (unencrypted) XIP window** that `fl_plain_window_init()` maps for 0x93000..0x100000
  at boot (`fm1_flash.h:222-231`); `smp_user_xip(k)` returns the XIP pointer and voices decode directly from it
  (`eng_sample.c:61-64`). Data above 0x93000 is written raw by SPI, so reading it through the encrypted window
  would give garbage (matches WAVETABLE_DESIGN section 3 gotcha). Alternative `st_read()` copies in 256-byte IRQ-off
  windows (`felucca.c:63-77`).
- **Validation after boot and after upload:** `smp_user_scan()` (`eng_sample.c:80-102`): magic, version, zone count,
  `data_len`, and `smp_zone_ok` for each zone (offset/length inside the data, rate, loop points, key range),
  checked in a **RAM copy** before publishing `usr_nz[k]` under IRQ-off ("a voice still playing the slot never reads
  a zone that is not valid"). The CRC32 is checked only at SMP_END (not at every boot) [fact: no CRC in scan].
- **Referencing:** a voice holds `0x8000 | slot << 5 | zone`; the slot is bumped by `smp_user_gen` so granular/slice
  engines rebuild their indexes (`eng_sample.c:70, 85`).

### 2.6 sloopDX

sloopDX has **no** sample slots (`dxsloop/web/EDITOR_PROTOCOL.md:85`: "SMP_BEGIN: rc 7 (sloopDX has no sample
slots)"). Its user data are DX7 voice banks (a 32-voice `.syx` bulk dump of 4104 bytes is validated by header
and checksum, `eng_dx7.c:194-203`) and "MY KIT", kept in flash `0xA0000..0xC3FFF` through the same A/B store
(`project.c:690, 720`) and reachable over zp12's raw link (`sp_link.c:3-4`). I did not trace the exact SysEx
receive path of the `.syx` (it is not a research priority here).

## 3. Safety: keeping a transfer away from the app / OTA area [fact]

Four independent layers in SLOOP/Felucca; Dinghy already has the lowest two:

1. **Command-level bounds:** the host never supplies an absolute address in A/B. Slot index < `SMP_USER_SLOTS`,
   `off >= 512`, `off % 256 == 0`, `off + len <= SMP_USER_SIZE` (`editor.c:774-779`); BACKUP object ids 0..9 with a
   fixed length per object. zp12's raw link instead uses an **allow-list** of absolute ranges
   (`zl_writable`, `sp_link.c:23-26`: `0xA0000..0xDC000`, `0xE5000..0xE9000`, `0xEA000..0xFC000`) and a read range
   `0x93000..0x100000` (`:63`).
2. **Storage-hook guard:** `st_erase`/`st_prog` check `FL_STORE_OK` (`felucca.c:106-117`) and the OTA hooks check
   `ota_in_area` (`:134`).
3. **RAM-driver guard (`FL_RANGE_OK`):** the *lowest* erase/program primitives re-check the range and alignment
   themselves and return -1 before touching SPI (`fm1_flash.h:171-192`), so a wrong caller or corrupted pointer
   cannot erase the app. `FL_IN()` is written so that `off + n` cannot wrap (the 1 MiB part ignores high address
   bits, `fm1_flash.h:45-48`).
4. **Separate loader window:** the update loader defines its own `FL_RANGE_OK` (`loader/loader.c:20`).

Dinghy today: `platform.c:70-71` narrows `FL_RANGE_OK` to exactly the OTA staging area plus the one settings sector
`0xFC000`. So **nothing user-data-like can be written until `FL_RANGE_OK` is widened**, and that widening is the
single point where a mistake could hit the app (as WAVETABLE_DESIGN section 9 says).

Write ordering for power-loss safety [fact]: data first, **header/commit record last**, header erased first on BEGIN
so a half-written slot reads as empty (`eng_sample.c` magic check, `editor.c:122-143`); zp12 and the A/B object
store (`storage.c:3-14`) additionally keep two copies with a generation/sequence and CRC so "a write torn at any point
leaves the previous copy in charge". No wear levelling for sample slots (an erase happens only when the user sends
one) [fact: none in code; inference: fine for a user action, typical NOR is rated ~100k cycles, chip not confirmed].

### Incidents (GitHub issues, 2026-10-05..12) [fact]

None involve a sample/table upload. They are all about the firmware image or the updater:
- Felucca #61: after updating 0.9-beta -> 1.0.1 the unit stuck in the mask-ROM boot mode (WL80UBOOT). The image had been
  written correctly; the cause was a **divide-by-zero at startup while loading an old stored project** (fixed in
  1.0.3.1, which also turned the divide trap off). Recovered with `jl-uboot-tool` / FM-1 Transporter.
  **Lesson for us: stored user data must be validated before use at boot, and a bad record must degrade to "empty",
  never to a crash.** Dinghy has `bootguard.h` + rescue, but table headers must not be trusted.
- SLOOP #92: unbootable after a disconnect during a firmware update; community recovery via a Flipper Zero tool
  (`merthsoft/flipper-fm1-recovery`, built on FM-1 Transporter + jl-uboot-tool).
- SLOOP #93: after Baud Girl -> SLOOP 2.5, Windows no longer enumerates the device; unresolved in the thread.
- Felucca #64: BACKUP_PUT pieces refused "often" from a CoreMIDI/JUCE sender; cause on the sender side: JUCE's
  `MidiOutput` splits a SysEx over 192 bytes across two `MIDISendEventList` calls and the FM-1 then drops it (19/20
  failed); Python/Web MIDI fine, 128-byte pieces fine. Keep this in mind for any native uploader.
- No report found of an upload corrupting the app/OTA area. [fact: `gh search issues` for sample/slot/flash/brick in
  SLOOP and Felucca returned nothing relevant; absence is not proof.]

## 4. What to borrow for VAVEWave

### 4.1 Decision: slot-relative SLOOP semantics + zp12's frame details + Felucca's commit/abort

- **Why not zp12's raw link:** it puts allocation and integrity in the browser and gives the host absolute flash
  addresses; one bug in a tool then writes anywhere in the allow-list. We want the firmware to own a **slot table**
  and expose only slot + offset.
- **Why not SLOOP framing verbatim:** the pack7 variants differ; Dinghy's platform already contains
  `ota_pack7/ota_unpack7` (bit stream, `ota.c`) and `tools/fm1_install.py` has its Python twin (`pack7`/`unpack7`),
  which is exactly what zp12 reuses. So: use the platform's bit-stream pack7 and zp12's per-frame `~sum`.

### 4.2 Wire format ("VW link", version 1)

```
F0  pack7( 7D 'V' 'W' cmd  seq  len_lo len_hi  payload[len]  sum )  F7
sum = ~(sum of bytes from cmd to the last payload byte) & 0xFF        (as zp12 sp_link.c)
reply: same header and cmd, then  rc  seq  len  payload  sum
```

`7D` is the non-commercial id like SLOOP (`46 4C`) and zp12 (`5A 50`); the pair `56 57` ("VW") keeps it apart, so
SLOOP's editor and Dinghy ignore each other's frames. One request at a time; the host retries a timed-out request
unchanged (after 1.5 s, 4 tries, as `zp12link.js` `Link`). `seq` echoes back so a late reply to a retried request can
be discarded. Frame limit: wire <= 640 B (`sx_frame`), so payload <= 512 (`ZL_MAX` precedent, 605 B on the wire);
**start with 256-byte DATA chunks** (flash-page aligned, proven at full speed in Felucca).

### 4.3 Commands

| cmd | Request payload | Reply payload | Notes |
| --- | --- | --- | --- |
| 1 HELLO | | proto, version string, nslots, slot bytes, frames, levels, flags | identity; a tool refuses unknown `proto` |
| 2 DIR | start, count | per slot: state (0 empty, 1 valid), name[8], frames, fmt, bytes, crc32 | read from RAM mirror built at boot |
| 3 BEGIN | slot, fmt, frames, levels, total bytes u32, crc32 u32, name[8] | rc | refuse if upload locked / slot >= N / size mismatch for fmt. Marks slot invalid (RAM, voices released), erases the **header sector**, starts the 15 s timeout, mutes audio, "UPLOADING" screen |
| 4 DATA | slot, off u24 (multiple of 256), bytes (<= 256, or 512 later) | rc, off | first chunk of each 4 KiB sector erases it (`fl_erase4k_quiet`), program, **read back via XIP and compare** (as zp12 `WRITE`); strictly increasing offsets; idempotent on retry |
| 5 COMMIT | slot | rc, crc32 | device recomputes CRC32 over the data (XIP), compares with BEGIN's; validates structure; writes the **header last** (magic `VWT1`, version, name, frames, fmt, bytes, crc32, header CRC); rescans; releases the lock |
| 6 ABORT | slot | rc | invalidate (header stays erased), release lock; also automatic on timeout, USB reset or unplug |
| 7 ERASE | slot | rc | erase header sector only (fast, ~45 ms); data sectors are erased lazily by the next BEGIN/DATA |
| 8 READ | slot, off, len <= 256 | bytes | export / backup of a user table (through `st_read` or XIP) |
| 9 AUDITION | slot, osc | rc | optional: select the table in oscillator 1/2 so the user hears it at once (zp12's ASSIGN) |

rc: 0 ok, 1 bad args, 2 out of range / alignment, 3 erase failed, 4 program failed, 5 verify failed, 6 no BEGIN or
timed out, 7 busy or refused (e.g. the on-device bounce is running), 8 CRC mismatch, 9 no flash (`flash_ok` false).

### 4.4 Flash layout (proposal, to be fixed with the engine task)

Use `0x97000..0xE0000` = 73 sectors (292 KB):

- **Slot = 9 sectors (36 KB): sector 0 header, sectors 1..8 data**, so the full 6-level 32-frame table of
  `tools/gen_wavetables.py` (**32,640 B** = 32 frames x 510 int16) fits in the 8 data sectors (32,768 B). That gives
  **8 slots at `0x97000 + k*0x9000`**, 1 spare sector at `0xDF000`. A second region `0xE6000..0xFC000` (22 sectors,
  after the preset sector 0xE5000 of the design) fits 2 more slots. Total 10; the design doc's "about 9" is correct.
- **Top-level-only variant (fmt 1):** 32 x 256 x int16 = 16,384 B = 4 data sectors; mips derived in `.pool` on load
  (WAVETABLE_DESIGN section 9/10). Same protocol, different `fmt`. Choose after measuring mip generation time.
- **Header (in sector 0, 64 B, little endian):** `magic 'VWT1'`, `u16 version`, `u8 fmt`, `u8 frames`, `u8 levels`,
  `u8 rsv`, `char name[8]`, `u32 bytes`, `u32 crc32 (zlib) of data`, `u32 hcrc (zlib) of the header bytes before it`.
  A header that fails magic/version/hcrc or has `frames` = 0 or > 64, `bytes` != frames * frame_len(fmt) is an empty
  slot. **Do not CRC the data at every boot** (30 KB x 10 slots would cost startup time) [fact: SLOOP does not
  either]; check CRC at COMMIT and on demand; bounds-check everything else at scan, as `smp_user_scan` does.
- **Replace semantics:** SLOOP overwrites in place (old content is lost at BEGIN). Optional upgrade [inference]:
  keep one spare slot and upload to it, then ERASE the old, so a failed transfer never costs the user's previous
  table; costs 36 KB and one extra "which slot is spare" bit.
- **Guard:** add `FL_IN(off, n, WT_USER_LO, WT_USER_HI)` to `platform.c:71` for exactly these ranges (never
  `FL_DATA_*` wholesale, never up to 0xE5000 where the OTA loader lives at `0xE0000..0xE4FFF`).
  Per-command bounds as in section 3 layer 1: slot * 0x9000 + offset computed in firmware, never an address from
  the host. Note: Felucca documents that the stock firmware's own updater stages around `0xE5000`, so a stock
  re-flash may wipe that sector and anything above it ("only ever while the stock firmware runs", `storage.c:20-27`);
  the main region `0x97000..0xDFFFF` is not in that margin [fact for Felucca's usage, inference for stock behaviour].

### 4.5 Device behaviour rules

1. BEGIN stops/mutes: release all voices on tables of that slot, mute output, show UPLOADING; refuse if the
   transport/recording engine says busy. Prefer **Felucca's approach (stop playing first)** over SLOOP's
   (keep playing): 8-9 erases at up to ~50 ms each with IRQs off cannot be played through.
2. Use the existing `audio_silence` + erase in one IRQ-off window pattern (`felucca.c:89-105`); Dinghy's
   `settings_save` already has the comment about the sound dropping out (`platform.c:134-136`).
3. The engine resolves a user table through a small RAM directory `{const int16_t *base; u8 frames, fmt, valid}`;
   BEGIN sets `valid = 0` (a single aligned write) before touching flash; voices fall back to table 0 when
   `valid == 0`. Reading user tables through XIP needs `fl_plain_window_init()` at boot; **Dinghy's `platform.c`
   currently never calls it** (grep: only the definition in `fm1_flash.h:222`) [fact] so add it, or read with
   `st_read` into `.pool`.
4. The ISR never touches flash, never waits; DATA/COMMIT run in the main loop, between `platform_poll()` calls
   (as `ed_service`). CRC32 over 32 KB may be sliced across polls (Dinghy's `platform.c:139` bitwise CRC is
   zlib-compatible; SLOOP uses a 4-bit table step, `storage.c:37`).
5. The flash writer is one internal function `wt_user_write(slot, off, src, n)` so the **on-device resample/bounce**
   of WAVETABLE_DESIGN section 10 stores into a slot through the same guarded path (no second path to audit).
6. Validate stored headers at boot defensively (Felucca #61): a corrupt slot is "empty", never a crash; run the scan
   under the watchdog.

### 4.6 PC-side tool plan

Phase 1, **Python + mido** (already a dependency of `tools/fm1_install.py`; numpy is already used by
`tools/gen_wavetables.py`): `tools/fm1_wavetable.py`

```
fm1_wavetable.py info                         HELLO + DIR
fm1_wavetable.py put SLOT NAME input.wav [--mode cycles|pitch|serum] [--frames 32] [--top-only]
fm1_wavetable.py get SLOT out.bin|out.wav     READ (backup / audit)
fm1_wavetable.py erase SLOT
fm1_wavetable.py build NAME OUT.bin input...  no device: the same bytes the firmware would store
```

Port discovery and retries copy `fm1_sample_upload.py:33-70` (open output+input by name regex, drain input, 3 hello
tries because "the first frame after opening the port can be lost") and `fm1_install.py:50` `PORT_RE` (never probe
other gear). Reuse `fm1_install.py`'s `pack7/unpack7` verbatim.

Phase 2 (optional), **browser via Web MIDI**: one HTML file in `web/` like SLOOP's editor, using
`requestMIDIAccess({sysex: true})`, `AudioContext.decodeAudioData` for any audio format, and a small JS FFT for the
same band-limiting. Pros: no install, drag-and-drop WAV, preview. Cons: Chrome/Edge only, needs a secure context
(https or localhost; behaviour for `file://` not verified), a JS port of the DSP that must be tested against the
Python reference (SLOOP does this: "checked byte for byte by `web/test_web.mjs`", `EDITOR_PROTOCOL.md:101`).
The hub is hosted by a third party (zvenson); a VAVEWave tool page hosted there would need his agreement and is
not required, since the page can be served from the repo.

### 4.7 Sharing with sample import (slice a sample into 256-sample cycles)

The device protocol is identical; **all import logic is PC-side** and shares `gen_wavetables.py`'s mip generator
(`mip_frame`, harmonic limit 64 >> L, per-table normalisation). Three importers:

1. **cycles**: file is already single cycles (AKWF: 600 samples; WaveEdit: 64 x 256; Serum/Vital: N x 2048).
   Split into frames of the chosen length, resample each to 256 by FFT truncation / zero-padding (exact for a
   periodic frame; discards harmonics > 64), pick or interpolate to 32 frames.
2. **pitch** (a sampled sound): estimate the period with YIN (`sloop-fm1/tools/sampleio.py:162 yin`, `:182 detect_hz`,
   GPL-3.0, pure Python + optional numpy), take 32 evenly spaced analysis points across the sustained part, each
   one period (or an average of K periods) resampled to 256 points, harmonic-1 phase set to zero so adjacent frames
   do not flip sign [inference: standard practice], then the mip generator. Needs a user-visible fundamental
   override.
3. **bounce**: a render with exact period 256 (WAVETABLE_DESIGN section 10): no pitch detection, no resampling,
   straight to frames.
The same `build` output works for all three; the WAV reader/resampler (`sampleio.py:27-129`, handles extensible
and 24-bit WAV) can be lifted with its header.

## 5. Recommendation

1. **Build the upload as a slot-relative SysEx protocol** (section 4): BEGIN / DATA(256 B, increasing, erase-on-sector
   start, read-back verify) / COMMIT(CRC32, header last) / ABORT, plus HELLO, DIR, ERASE, READ. Frame it with the
   platform's bit-stream pack7 and zp12's `~sum`, id `7D 'V' 'W'`. This is what SLOOP's SMP commands + Felucca's
   BACKUP_PUT + zp12's frame already prove on this chip; nothing new is invented in the transport.
2. **Reuse code, not just ideas:** `editor.c` SMP handlers and `ed_service` pattern, `fm1_sample_upload.py`,
   `sampleio.py` (WAV, resample, YIN), `zp12link.js` `Link` retry logic; keep SPDX headers and add the origins to
   `LICENSING.md`. Everything is GPL-3.0-only, so it is compatible; avoid Felucca's assets.
3. **Land in this order (matches WAVETABLE_DESIGN section 9):** factory tables in the image first; then widen
   `FL_RANGE_OK` to exactly the slot region; then the writer + `rules-reviewer`; then the Python tool; then a
   device-side bounce reusing `wt_user_write`.
4. **No on-device recording path exists to copy.** Sample-to-wavetable import is PC-side; "punch-in" in SLOOP is an
   FX, not sampling.

## 6. Open questions / verify on hardware

1. **Flash part and erase time.** JEDEC `0x856014` read at boot (`platform.c:329`); datasheet figures (sector erase
   typ/max, page program, number of partial page programs, endurance) are not in the repo. Measure `took_us` of
   `fl_erase4k_quiet` over 100 sectors; it sets whether 8 erases per table are ~0.4 s or more. The driver
   comment says "generic NOR max 400 ms" (`fm1_flash.h:182`).
2. **Plain XIP window for the user region:** confirm on a Dinghy build that a table written through SPI reads back
   correctly through `fm1_xip` only after `fl_plain_window_init()`, and that factory tables (in the encrypted image)
   are unaffected.
3. **Free flash really free:** confirm `0x97000..0xDFFFF` is untouched by the Dinghy image (size check: image must
   end below 0x93000) and by the stock bootloader/"device data" (SLOOP and zp12 use `0xA0000..0xDBFFF` without
   reported damage, but they too leave `0x93000..0x97000` alone for an unknown reason).
4. **Throughput and frame loss** with mido/python-rtmidi on macOS at 256 and 512 B (Felucca: 10 ms/256 B).
5. **IRQ-off budget:** does the USB poll (timer5 ISR, 2 kHz) lose a SOF/packet during a 45 ms erase? Felucca's
   measurements say frames are retried by USB (NAK), but measure with an upload during playback and with an upload
   with the transport stopped.
6. **Native senders:** if a VAVEWave plugin or app is ever wanted, test with JUCE/CoreMIDI (Felucca #64: splitting a
   SysEx over 192 B drops it).
7. **Header and slot policy decisions for the user:** in-place replace vs spare-slot swap; top-level-only vs full
   mips (depends on a measured mip build time in `.pool`); name length (8 chars like SLOOP) and how the 12 factory
   tables and up to 10 user tables are presented on the 240x240 screen and in MIDI program change.
8. **Hub:** listing of a VAVEWave package on the hub requires GPL-3.0 + a `.fwsc` release (release-engineer task);
   whether the hub could also host a table-upload page is a question for its maintainer, not a technical need.
9. **Licence hygiene:** `EDITOR_PROTOCOL.md` text, and `sp_link.c` (SPDX only, no copyright line) have no explicit
   copyright statement; if their wording is copied, ask or paraphrase (the wire formats are free).
