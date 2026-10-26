# tools/

Scripts to set up, build, install and release TastyWaves, and to make the data the firmware is built from. Run them
from anywhere: each one changes to the repository first. The Python ones use `.venv/` (made by `install.sh`).

## Making a release

A release is a version number, a tested `.fwsc` package, a git tag, a GitHub release with the package attached, and
the same package bundled with the web tool on GitHub Pages (its Install button).

### Version numbers and the FM-1 identity

Versions are `X.Y.Z`, one digit each (`build.py` enforces it); the screen and the release name show it. Every
package also carries an FM-1 identity, which the FM-1 reports over USB and the installers check after writing.
TastyWaves' identity is **`FM-1_937`**, the same for every release (`PRODUCT` in `tools/build.py`). It is ours in the
[FM-1 Firmware Hub](https://thefirmwarehub.com/) catalogue: do not change it without telling the hub. Dev builds
(no `--release`) carry it too, with the version text `0.0`; they are for testing only, never published.

### Checklist

1. **Everything is committed** on `main`, and `docs/DEVICE-TEST-PLAN.md` has a section for what changed.
2. **Run the test suites:**

   ```sh
   for t in arp arp_panel latch panel patches seqstore ui fx engine host patchlog wtlink webtool wavetable_tool; do
       tests/run_$t.sh > /dev/null 2>&1 && echo "pass $t" || echo "FAIL $t"; done
   tests/web_smoke.sh          # the web tool in headless Chrome (skipped without Chrome)
   ```

3. **Build the release:**

   ```sh
   ./build.sh --release 1.1.0          # -> build/tastywaves-1.1.0.fwsc, identity FM-1_937
   ```

   The build fails on what would break the FM-1 (size, RAM, ROM calls, registers outside `hal/`); note the free
   flash it prints (`image N B of 581564`).

4. **Install it and test it on a real FM-1:**

   ```sh
   tools/install_fw.sh build/tastywaves-1.1.0.fwsc
   ```

   GLO > INFO must show `TASTYWAVES 1.1.0`. Run the new test plan section, check that saved patches survived, and
   switch off and on once (the splash, then HOME). Untested builds are not released: the FM-1 Firmware Hub lists
   only firmwares tested on the device.

5. **Bundle it with the web tool**, then commit and push:

   ```sh
   tools/web_release.sh 1.1.0          # -> tools/web/firmware/tastywaves.fwsc + release.json
   git add -A && git commit -m "TastyWaves 1.1.0" && git push
   ```

   The push publishes the web tool (`.github/workflows/pages.yml`); in a minute or two
   https://marksweiss.github.io/tastywaves/ offers **Install TastyWaves 1.1.0**.

6. **Tag and publish the GitHub release** with the package attached:

   ```sh
   git tag -a v1.1.0 -m "TastyWaves 1.1.0" && git push origin v1.1.0
   gh release create v1.1.0 build/tastywaves-1.1.0.fwsc --title "TastyWaves 1.1.0" --notes-file NOTES.md
   ```

   Notes: how to install (the web page, or the file), and what changed.

7. **Update the README:** the version in the status line and the download link in Install
   (`releases/download/vX.Y.Z/tastywaves-X.Y.Z.fwsc`).

8. **Check:** the page, the download and the bundled file are the same package:

   ```sh
   shasum -a 256 build/tastywaves-1.1.0.fwsc
   curl -sL https://github.com/marksweiss/tastywaves/releases/download/v1.1.0/tastywaves-1.1.0.fwsc | shasum -a 256
   curl -s https://marksweiss.github.io/tastywaves/firmware/tastywaves.fwsc | shasum -a 256
   ```

Once TastyWaves is listed on the [FM-1 Firmware Hub](https://thefirmwarehub.com/), the hub picks up each new GitHub
release by itself (nightly).

## The tools

### Set up, build, install

| Tool | What it does |
| --- | --- |
| `install.sh` | Once per checkout: a uv venv with Pillow, mido, python-rtmidi, numpy; the JieLi toolchain; three files of the JieLi AC79 SDK; then a build and a host test |
| `get_toolchain.sh` | Downloads JieLi's clang for the chip to `~/.jieli/toolchain` (`install.sh` calls it) |
| `build.py` | The build (`../build.sh` runs it, in Docker on macOS): the app, the update loader, the `.fwsc` package, and the checks. `--release X.Y.Z` |
| `build_fw.sh` | `build.sh` with its arguments, from the `tools/` folder |
| `fm1pkg_make.py`, `lz4blk.py` | Package the app and the loader into a `.fwsc` (Felucca's format); used by `build.py` |
| `install_fw.sh` | Installs a `.fwsc` on the connected FM-1 over USB (default `build/tastywaves.fwsc`); `--info` shows what runs on it |
| `fm1_install.py` | The installer behind `install_fw.sh` (Felucca's USB update protocol); exit codes in its header |

### The web tool

| Tool | What it does |
| --- | --- |
| `web/` | The browser tool: install the firmware, send wavetables, manage patches. See [web/README.md](web/README.md) |
| `web_tool.sh` | Serves `web/` at http://localhost:8000 and opens it |
| `web_serve.py` | The server behind it (caching off, so a reload always gets the current scripts) |
| `web_release.sh` | Bundles a release build with the web tool, for its Install button and GitHub Pages |

### Data and testing

| Tool | What it does |
| --- | --- |
| `fm1_wavetable.py` | Wavetable WAVs (Serum, WaveEdit, Ableton) and Surge `.wt` files to a user table (`.vwt`), and puts, gets and erases tables on the FM-1 (`put`, `get`, `erase`, `info`); the same converter as the web tool |
| `gen_wavetables.py` | Makes the 12 factory wavetables (`firmware/gen/wavetables.h`) |
| `gen_engine_tables.py` | The engine's constant tables (`firmware/gen/engine_tables.h`): everything that needs floating point, computed here |
| `gen_bench_tables.py` | From those, the guarded tables the engine plays (`firmware/gen/wt_bench.h`, with the table names) and the sinc taps (`sinc8_taps.h`) |
| `gen_font.py` | The screen fonts (`firmware/gen/felucca_font.h`) from BDF files in `assets/fonts/` |
| `send_clock.py` | Sends MIDI clock to the FM-1, to try the arpeggiator and sequencers' sync without a DAW |

The generated headers in `firmware/gen/` are committed: run a `gen_*` tool only when you change what it makes.
