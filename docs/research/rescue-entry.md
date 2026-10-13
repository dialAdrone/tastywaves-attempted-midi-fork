# USB rescue entry on the FM-1: why holding OCT- may not have opened "USB RESCUE"

Question: a user held OCT- (button id 0, alone) while switching on or while plugging in USB-C, and our build
started the normal app instead of the USB RESCUE screen. What is the right gesture, do others report the same, has
the entry code ever had bugs, and can anything in our code make `recovery_key()` fail while OCT- is held?

Legend: **[V]** verified from source/doc (cited), **[I]** my inference, **[?]** unknown.

## 1. Sources checked

| Source | License | Last push | Stars | Relevant content |
| --- | --- | --- | --- | --- |
| [isod89/sloop-fm1](https://github.com/isod89/sloop-fm1) @8fad1a1 | GPL-3.0 | 2026-10-12 | 437 | origin of `recovery.c`, `bootguard.h`, OCT- rescue; OCT-+OCT+ calibration |
| [zvenson/dxsloop](https://github.com/zvenson/dxsloop) (sloopDX) @7dc1ca1 | GPL-3.0 | 2026-10-11 | 20 | same platform, rescue docs |
| [zvenson/zp12](https://github.com/zvenson/zp12) @ca8f0c0 | GPL-3.0 | 2026-10-12 | - | same platform |
| [zvenson/dinghy](https://github.com/zvenson/dinghy) @d44df7d | GPL-3.0 | 2026-10-12 | - | our upstream |
| [hugelton/Felucca](https://github.com/hugelton/Felucca) @9f6a885 | GPL-3.0 | 2026-10-13 | 435 | input HAL origin; **has no OCT- rescue**; OCT-+OCT+ calibration; panel picture |
| [aroum/fm1-custom-fw](https://github.com/aroum/fm1-custom-fw) (hub/directory) @6464b8f | none stated | 2026-10-12 | - | recovery vectors, "no recovery buttons" |
| [FM-1 Firmware Hub](https://fm1.designburgapps.com/) | web | - | - | rescue wording |
| [kurogedelic/FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter) | MIT | 2026-10-01 | 40 | battery, power switch, ROM UBOOT entry |
| [jvitkauskas/fm1-bootloader](https://github.com/jvitkauskas/fm1-bootloader) | Apache-2.0 | 2026-10-12 | 1 | rebuilt stock SPL (uboot.boot) |
| [AL-255/FM-1-RE](https://github.com/AL-255/FM-1-RE) @ec832f2 | WTFPL | 2026-09-08 | 106 | stock firmware reverse engineering |

All the code compared below is GPL-3.0 (same as ours). Nothing needs to be reused: our files already match upstream.

## 2. What the correct gesture is

- **Docs say: hold OCT- *alone*, then switch the FM-1 on.** [V] SLOOP README "Rescue": "hold **OCT−** alone while
  switching it on (*SLOOP USB RESCUE*)" ([README](https://github.com/isod89/sloop-fm1#rescue)); sloopDX README line 174
  and SLOOP.md line 523 say the same; the Hub says "Hold OCT− while switching the FM-1 on (USB rescue)"
  ([fm1.designburgapps.com](https://fm1.designburgapps.com/)). Our `docs/RULES.md:53` and Dinghy's README:31 agree.
- **Which button.** [V] OCT- is the left one of the two small buttons under PRESETS / ALGORITHM, left of the screen;
  OCT+ is to its right (Felucca `docs/controls.jpg`, group 5). The input HAL maps matrix column 0 / row PA8 to id 0
  and column 1 / row PA8 to id 1 (`firmware/hal/fm1_input.h` `FM1_KEYMAP`), and SLOOP's default panel table maps the
  label OCT- to matrix id 0, OCT+ to 1 (`sloop-fm1/firmware/src/panel.c` `PANEL_DEFAULT`).
- **When.** [V] from code: the check runs once, in `fm1_cstart`, in the first ~50 ms of the app
  (`recovery_key`: a 50 ms polled scan, then one decision). [I] With ROM + SPL before it, that window ends well under
  half a second after the switch. OCT- must therefore be **held before** the switch goes on and kept held until the
  RESCUE screen shows. Pressing it after switching on cannot work. There is no hold-duration requirement beyond that.
- **Power switch, not USB insertion.** [V] The FM-1 has a rear ON/OFF slide switch and a 2000 mAh battery
  ([Synth Anatomy review](https://synthanatomy.com/2026/07/m-vave-fm-1-review-low-budget-pocket-fm-ynthesizer-with-iconic-sounds.html));
  "The FM-1 runs on its battery", "switch the FM-1 ON when prompted", "power-cycle it"
  ([Transporter README](https://github.com/kurogedelic/FM-1-transporter), `docs/DEVLOG.md:85`). So **plugging USB-C into
  a unit that is already on does not restart the MCU**, and the rescue check never runs. [?] Whether plugging USB into
  a unit that is switched OFF starts the MCU (or only charges) is not documented anywhere I found; treat USB insertion
  as *not* a boot event. [?] Whether the switch cuts the MCU rail when USB is connected (VBUS-powered system rail) is
  also undocumented; if it does not, toggling the switch with USB attached may not reboot at all [I].
- **OCT- + OCT+ matters.** [V] `bootguard_manual` is `(buttons & 3u) == 1u` "physical OCT- alone; both keep
  calibration". Holding both (or brushing OCT+, which sits right next to it) skips the rescue silently. In SLOOP and
  Felucca, both held at power-on is HARDWARE CALIBRATION (`sloop-fm1/GUIDE.md:173`, `Felucca/firmware/src/main.c:188`,
  [Felucca #123](https://github.com/hugelton/Felucca/issues/123)); SLOOP also has OCT-+OCT+ held 5 s while stopped =
  UBOOT (`GUIDE.md:174`). Our build has neither: both held just boots normally.

## 3. Did others report the same? Did it have bugs?

- **No report found of OCT- failing to enter USB RESCUE on a working image.** Searched issues for rescue, OCT-,
  recovery, power on, calibration, uboot, power switch, battery in all repos above (2026-10-12).
- Related, not the same:
  - [sloop-fm1 #92](https://github.com/isod89/sloop-fm1/issues/92): "the normal rescue steps didn't work" after a
    USB disconnect **during an update**. Expected: the rescue is application-level; a half-written app cannot run it
    (`recovery.c` header, `bootguard.h` "cannot rescue an image that fails before reaching the application").
  - [sloop-fm1 #93](https://github.com/isod89/sloop-fm1/issues/93): a user tried "oct-/oct+ held before USB
    insertion" as update mode. Shows users do mix up the gestures and use USB insertion as the trigger.
  - [Felucca #61](https://github.com/hugelton/Felucca/issues/61): image would not start, unit sat in ROM UBOOT; fixed
    with jl-uboot-tool. Felucca never had an OCT- rescue.
  - [sloop-fm1 #26](https://github.com/isod89/sloop-fm1/issues/26), [#110](https://github.com/isod89/sloop-fm1/issues/110):
    knob direction / button table mismatches fixed by calibration. Owner attributes #110 to software (another
    firmware's table), not hardware. [?] No evidence of a hardware revision where matrix id 0 is not OCT-.
- **No fixes to the entry code since it was written.** [V] `firmware/src/recovery.c` and `bootguard.h` have one
  commit each in SLOOP (a3f9fd04, "SLOOP 2.0", 2026-10-04); sloopDX/zp12 only changed the screen text. Our copies are
  byte-identical to Dinghy's and differ from SLOOP/sloopDX/zp12 only in the title string. Our `fm1_cstart` sequence
  matches SLOOP's `main.c:285-315`. `fm1_input.h` is identical to SLOOP/sloopDX/zp12/Dinghy; it changed in SLOOP 2.1
  and 2.3 (d691ba7b): SLOOP 2.0 used `FM1_PRESS 3` with a counting debouncer, now `FM1_DEB_PRESS 2` with a
  state-compare debouncer. Neither change alters the outcome over a 50 ms window (~80 frames).
- **Hardware verification is thin.** [V] SLOOP's `tests/recovery_test.c` mocks `fm1_input_scan` (sets `buttons`
  directly), so the real matrix path at that early point is not covered by any test. [I] Nobody has published a
  hardware confirmation either way; absence of complaints is the only evidence it works.

## 4. Stock firmware / JieLi boot and held keys

- [V] The SPL (`uboot.boot`) reads no panel keys: boot flow is isd_config, flash mount, `UPDATA_PARM` check, run app,
  else ROM USB download ([fm1-bootloader README "Boot flow"](https://github.com/jvitkauskas/fm1-bootloader)). It
  supports `BOOT_DELAY` and `PORT_INPUT` from isd_config ([arch.c](https://github.com/jvitkauskas/fm1-bootloader/blob/main/src/arch.c)); [?] stock values unknown.
- [V] The mask-ROM UBOOT is entered by a USB_KEY pulse train on D+/D- at power-on or after a watchdog reset, or
  automatically when no app boots; there are no recovery buttons on the board
  ([Transporter README](https://github.com/kurogedelic/FM-1-transporter), `docs/DEVLOG.md:155`;
  [hub REVERSE_ENGINEERING.md](https://github.com/aroum/fm1-custom-fw/blob/main/REVERSE_ENGINEERING.md) sec. 2;
  [jielie USB key](https://kagaimiq.github.io/jielie/isp/usb/usb-key.html)).
- [V] Stock V15 has no documented boot-time key combo; OCT- + OCT+ while running resets octave/transpose (manual,
  via search summary of [manuals.plus](https://manuals.plus/ae/1005012692306518), page itself 403). Stock enables USB only
  after a cold power-on (Transporter `DEVLOG.md:149`). [?] AL-255's RE does not mention any boot key check.
- So **every OCT- rescue is our app's own code**, not the ROM/SPL, and it only exists in SLOOP-lineage builds
  (SLOOP, sloopDX, zp12, Dinghy, ours). On a unit actually running Felucca or a Felucca fork, OCT- at power-on does
  nothing.

## 5. Our code: can `recovery_key()` fail with OCT- held?

Read: `firmware/src/platform.c:305-344` (`fm1_cstart`), `recovery.c:41-50`, `bootguard.h`, `hal/fm1_input.h`,
`hal/fm1_time.h`, `hal/fm1_sys.h:97-107`.

Order at power-on: `fm1_time_init` (TIMER4, 24 MHz OSC) -> `fm1_reset_reason` -> WDT arm -> clear guard on a plain
power-on -> `bootguard_begin` -> (BOOT_ROM: UBOOT) -> IRQ init -> .bss/.data/pool init -> guard -> **`recovery_key()`**
(own `fm1_input_init`, then `fm1_input_scan` in a loop for 50 ms) -> `bootguard_manual(fm1_in.buttons)`.

| Suspect | Finding |
| --- | --- |
| Pull-ups not ready | `fm1_input_init` enables row DIE/input/pull-ups and drives all columns high before the first scan; each column gets 10 us settle before sampling. The first frames come >50 us after init. Not a failure path [V code, I electrically]. |
| `fm1_input_init` after the check | The second `fm1_input_init` at `platform.c:333` is for the normal path; `recovery_key` calls its own first (`recovery.c:44`). Not a bug. |
| First-frame debounce | `fm1_in` is .bss, zeroed just before; a closed key sets bit 0 after 2 consecutive closed frames (~1-2 ms); the loop runs ~80 frames (11 x ~55 us). A press held through the window always registers. |
| Timer wrong | If TIMER4 ran at another rate the window would shrink or grow, but even 10x faster leaves ~8 frames, more than the 2 needed. A stopped TIMER4 would hang until the WDT, not boot the app. Not a failure path. |
| Reset reason / `bootguard_clear` | Only `BOOT_ROM` skips the check, and it jumps to UBOOT (not the app). `BOOT_RECOVERY` enters rescue without a key. Power-on, WDT, soft resets all reach `recovery_key`. Not a failure path. |
| Polarity | Rows are active low with pull-ups; `fm1__rows` inverts, 1 = closed; the same code drives the app, where OCT- works. Not a failure path. |
| Mask | `(buttons & 3u) == 1u`: **fails if OCT+ (id 1) is also closed**. Only real code-level way to miss it. |
| Short-circuit | `boot_mode == BOOT_RECOVERY \|\| recovery_key()`: fine. |
| LED / 595 order | Columns use the same 16-bit word as the app; LEDs dark (`fm1_led` zero). No dependency on `lcd_init`/ADC/audio found (LCD init only touches PA2 and PC7-10). |

Verdict: **[I] no condition in the code makes `recovery_key()` return false while matrix id 0 is held alone through
the first ~50 ms of the app.** The plausible causes are all outside the code: (a) the MCU did not cold-start
(USB plugged into a unit already on battery, or the switch toggled with USB attached and the rail stayed up);
(b) OCT- pressed after switching on; (c) OCT+ brushed too; (d) the unit was not actually running our build (check
`fm1_install.py --info` reports FM-1_950).

## Recommendation

1. Tell the user the exact gesture: **unit fully OFF (rear switch), USB unplugged; hold OCT- (left of OCT+) alone;
   slide the switch ON; keep holding until "USB RESCUE" shows; then connect USB.** USB insertion is not a trigger.
2. If that still starts the app, use the BOOT KEYS diagnostic already in our build (`docs/DEVICE-TEST-PLAN.md` 1.6):
   `app_main` copies `fm1_in.buttons` / `frames` / `raw[]` left by `recovery_key`'s scan (no scans run between it and
   `app_main`). `BTN 0x0001` with no rescue would mean a logic bug (none found); `0x0003` = OCT+ also held;
   `0x0000` with FR around 80 = the key was not closed during the window (timing or power, not code); FR near 0 or
   huge = the boot path was not what we think.
3. Code changes are not justified by this evidence, and CODEMAP marks `recovery.c` as "never break, do not touch". If the diagnostic
   shows a too-early window on real hardware, the low-risk fix (upstream-style, discussed with SLOOP/Dinghy first)
   would be a longer window, e.g. 300-500 ms, or waiting for the key to be released first. Either way it needs a
   hardware test before release.
4. Docs: add "switch it on, not plug it in; OCT- alone, held before switching on" to `docs/RULES.md` / README.

Open questions [?]: does USB insertion boot the MCU when the switch is OFF? Does the switch cut the MCU rail with
USB attached? Stock isd_config `BOOT_DELAY` value (sets how early the window starts)? No published hardware
confirmation of the OCT- rescue from SLOOP/sloopDX users.
