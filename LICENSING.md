# Licensing

TastyWaves is free software under the GNU General Public License, version 3 only (`GPL-3.0-only`, full text in
`LICENSE`). You may use, study, change and share it. If you give someone a firmware built from it, give them its
complete source under the same licence too.

TastyWaves derives from [Dinghy](https://github.com/zvenson/dinghy), which derives from
[Felucca](https://github.com/hugelton/Felucca) through [SLOOP](https://github.com/isod89/sloop-fm1). All three are
GPL-3.0, and so is everything here.

## Who made what

| | |
| --- | --- |
| The platform: `firmware/hal/`, `firmware/loader/`, `crt0.S`, `app.ld`, `firmware/src/` (`usb.c`, `ota.c`, `recovery.c`, `lcd.c`, `gfx.c`, `libc.c`, `bootguard.h`, `uac_fir.h`), `tools/build.py`, `tools/fm1_install.py`, `tools/fm1pkg_make.py`, `tools/get_toolchain.sh`, `tools/lz4blk.py` | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments ([Felucca](https://github.com/hugelton/Felucca)), with the changes of [SLOOP](https://github.com/isod89/sloop-fm1) (isod89) |
| Dinghy: the wiring `firmware/src/platform.c`, the host mode `tests/host.c` and `tests/run_host.sh`, `docs/API.md`, `docs/RULES.md`, the build checks | zvenson ([Dinghy](https://github.com/zvenson/dinghy)) |
| TastyWaves: the synth (`firmware/src/tastywaves.c`, `synth.h`, `engine.h`, `arp.h`, `fx.h`, `patch.h`, `panel.h`, `patchlog.h`, `patchlink.h`, `wtlink.h`, `usertab.h`, `ui.h`, `ui_screens.h`, `wtbench.h`), the generated tables in `firmware/gen/` (but `felucca_font.h`, see below), the wavetable and web tools (`tools/fm1_wavetable.py`, `tools/gen_*.py`, `tools/web/`) and the other scripts in `tools/`, the other tests, the other docs | Copyright (C) 2026 Mark Weiss (@marksweiss), written with Claude (Anthropic) |

Each builds on the one before it; the changes TastyWaves made to the platform and Dinghy files are in the git history.

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Spleen fonts 8x16, 12x24 and 16x32 (Frederic Cambus), rendered into `firmware/gen/felucca_font.h` by `tools/gen_font.py` | BSD 2-Clause | `assets/fonts/Spleen-LICENSE.txt` (BDF sources beside it) |
| Terminus font 8x16 (ter-u16n): the font Felucca shipped, no longer in the build | SIL OFL 1.1 | `assets/fonts/Terminus-LICENSE.txt` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

Dinghy's logo (the designburg signet) is not part of TastyWaves: it is not under the GPL and marks Dinghy itself.

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments; "M-VAVE" and "FM-1" belong to their
owners. TastyWaves is not made or endorsed by them, nor by the authors of Dinghy or SLOOP.
