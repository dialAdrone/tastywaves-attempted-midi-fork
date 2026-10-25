#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Font comparison sheet (does not touch the shipped font).

    python -I tests/font_sheet.py OUTDIR NAME|SMALLSPEC|LARGESPEC|LICENCE [...]   (SPEC as tools/gen_font.py)

For every candidate: tools/gen_font.py makes a felucca_font.h, tests/font_sheet.c draws the UI's strings with the
firmware's own gfx.c (-> OUTDIR/text-NAME.png), and the real app (tastywaves.c, host mode) is built with that header and
run on tests/scripts/panel_ui.txt for three screens. The current font (firmware/gen/felucca_font.h) is the first row.
Writes OUTDIR/font-sheet-text.png and OUTDIR/font-sheet-screens.png. Font files are only read by gen_font.py (Pillow /
a BDF parser); nothing from them is executed."""
import os
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
SHOTS = ["home2_spectrum", "edit_bend", "fx_filter"]


def run(*a, **k):
    subprocess.run([str(x) for x in a], check=True, **k)


def make(out: Path, name, cand_dir: Path):
    out.mkdir(parents=True, exist_ok=True)
    # text strips
    exe = out / f"fs-{name}"
    run("cc", "-DDINGHY_HOST", f"-I{cand_dir}", f"-I{ROOT / 'firmware/src'}", "-o", exe, ROOT / "tests/font_sheet.c")
    ppm = out / f"fs-{name}.ppm"
    run(exe, ppm)
    Image.open(ppm).resize((1280, 328), Image.NEAREST).save(out / f"text-{name}.png")
    ppm.unlink()
    exe.unlink()
    # real screens
    app = out / f"app-{name}"
    run("cc", "-O1", "-Wno-unused-function", "-DDINGHY_HOST", '-DAPP_VER="font"', f"-I{cand_dir}", f"-I{ROOT / 'firmware/src'}",
        f"-I{ROOT / 'firmware/gen'}", "-o", app, ROOT / "firmware/src/tastywaves.c")
    with open(out / f"app-{name}.log", "w") as log:
        run(app, out / f"scr-{name}", ROOT / "tests/scripts/panel_ui.txt", stdout=log)
    app.unlink()
    for p in out.glob(f"scr-{name}-*.ppm"):
        if p.stem.split(f"scr-{name}-")[1] in SHOTS:
            Image.open(p).save(p.with_suffix(".png"))
        p.unlink()
    (out / f"app-{name}.log").unlink()


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    out = Path(sys.argv[1])
    cands = [("CURRENT", None, None, "Terminus 4.49 8x16 (SIL OFL 1.1), as shipped")]
    for a in sys.argv[2:]:
        n, s, l, lic = a.split("|", 3)
        cands.append((n, s, l, lic))
    font = ImageFont.load_default(14)
    for name, s, l, lic in cands:
        d = out / f"cand-{name}"
        d.mkdir(parents=True, exist_ok=True)
        if s is None:
            (d / "felucca_font.h").write_text((ROOT / "firmware/gen/felucca_font.h").read_text())
        else:
            run(sys.executable, "-I", ROOT / "tools/gen_font.py", d / "felucca_font.h", "--small", s, "--large", l,
                "--comment", f"candidate {name}")
        make(out, name, d)
    # sheets
    W = 1280
    rows = [Image.open(out / f"text-{c[0]}.png") for c in cands]
    sheet = Image.new("RGB", (W, sum(r.height + 26 for r in rows)), (20, 20, 24))
    dr = ImageDraw.Draw(sheet)
    y = 0
    for c, im in zip(cands, rows):
        dr.text((6, y + 4), f"{c[0]}   {c[3]}", font=font, fill=(255, 255, 255))
        sheet.paste(im, (0, y + 26))
        y += im.height + 26
    sheet.save(out / "font-sheet-text.png")
    sc = Image.new("RGB", (len(SHOTS) * 244 + 6, len(cands) * 262), (20, 20, 24))
    dr = ImageDraw.Draw(sc)
    for i, c in enumerate(cands):
        dr.text((6, i * 262 + 3), f"{c[0]}   {c[3]}", font=font, fill=(255, 255, 255))
        for j, sh in enumerate(SHOTS):
            sc.paste(Image.open(out / f"scr-{c[0]}-{sh}.png"), (6 + j * 244, i * 262 + 20))
    sc.save(out / "font-sheet-screens.png")
    print("wrote", out / "font-sheet-text.png", out / "font-sheet-screens.png")


if __name__ == "__main__":
    main()
