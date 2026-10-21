#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""gen_font.py: make firmware/gen/felucca_font.h (the two screen fonts) from a BDF bitmap font or a TTF/OTF.

The firmware reads a font as felucca_font_t (firmware/src/gfx.c): per glyph an advance, a bitmap width and an offset
into 4-bit-per-pixel data (two pixels a byte, the left one in the high nibble, 0..15 = how much of the colour), rows
of (bw + 1) / 2 bytes, `h` rows, the bitmap starting `pad` pixels left of the pen. Two fonts:

    FONT_S  ASCII 32..126, 16 rows, advance 8 (pad 2, bitmap 12 wide)     text, labels, values
    FONT_L  ASCII 32..95 (capitals, digits, signs), 32 rows, advance 16   big text (effect names, HOME)
    FONT_M  ASCII 32..95, 24 rows, advance 12 (optional, --medium)        knob values, the patch name

The UI lays text out in 8 / 16 px columns, so keep both monospaced at those advances.

    tools/gen_font.py OUT.h --small SPEC --large SPEC [--comment TEXT]
    SPEC = FILE[:key=value[,key=value...]]   keys:
        size   TTF/OTF only: pixel size (float ok) the outline is rendered at (default 16 for S, 32 for L)
        sx sy  integer pixel scale after rendering (default 1): a pixel font drawn at its own size, then doubled
        adv    advance in pixels (S 8, L 16); h cell height (S 16, L 32); pad (adv / 4)
        base   row of the baseline in the cell (BDF: FONT_ASCENT * sy; TTF: the font centred in the cell)
        dx dy  move the glyphs (pixels, after scaling)
        thr    1..255: no grey, a pixel is on at coverage >= thr (default: keep the anti-aliasing, 16 levels)
        fold   1: render a lower case letter with its capital (a font with capitals only)
        center 1: TTF only: centre each glyph in its cell by its own advance (a proportional font in 8 / 16 columns)
    Examples (what the shipped font is, from Terminus 4.49 BDFs):
        tools/gen_font.py firmware/gen/felucca_font.h --small ter-u16n.bdf --large ter-u32n.bdf
    The shipped font (Spleen, assets/fonts):
        tools/gen_font.py firmware/gen/felucca_font.h --small assets/fonts/spleen-8x16.bdf \
            --large assets/fonts/spleen-16x32.bdf --medium assets/fonts/spleen-12x24.bdf
    A TTF (Pillow only; run it as `python -I` on a font you downloaded; it opens the font file and nothing else):
        tools/gen_font.py out.h --small DepartureMono-Regular.otf:size=13.3,thr=110 --large DepartureMono-Regular.otf:size=26.6

Licence: the font you convert keeps its own licence (OFL / MIT / BSD files go in docs/ or the repo's licence notes).
This script is GPL-3.0-only like the rest of the repo."""
import argparse
import sys

S_DEF = dict(first=32, last=126, h=16, adv=8, size=16.0)
L_DEF = dict(first=32, last=95, h=32, adv=16, size=32.0)
M_DEF = dict(first=32, last=95, h=24, adv=12, size=24.0)


def parse_spec(spec, defaults):
    path, _, opts = spec.partition(":")
    # a Windows drive letter or a path with a colon is not supported on purpose: keep file names simple
    o = dict(defaults)
    for kv in filter(None, opts.split(",")):
        k, _, v = kv.partition("=")
        if k in ("size",):
            o[k] = float(v)
        elif k in ("sx", "sy", "adv", "h", "pad", "base", "dx", "dy", "thr", "fold", "center"):
            o[k] = int(v)
        else:
            sys.exit(f"gen_font: unknown option {k!r} in {spec!r}")
    o.setdefault("sx", 1)
    o.setdefault("sy", 1)
    o.setdefault("dx", 0)
    o.setdefault("dy", 0)
    o.setdefault("thr", 0)
    o.setdefault("fold", 0)
    o.setdefault("center", 0)
    o["path"] = path
    o.setdefault("pad", o["adv"] // 4)
    return o


# ---- BDF: a text format. We read STARTCHAR blocks: ENCODING, DWIDTH, BBX, BITMAP
def read_bdf(path):
    glyphs, ascent, cur = {}, None, None
    with open(path, encoding="latin-1") as f:
        in_bitmap = False
        for line in f:
            t = line.split()
            if not t:
                continue
            if in_bitmap:
                if t[0] == "ENDCHAR":
                    in_bitmap = False
                    if cur["enc"] >= 0:
                        glyphs[cur["enc"]] = cur
                else:
                    cur["rows"].append(t[0])          # hex digits, MSB = leftmost pixel
                continue
            k = t[0]
            if k == "FONT_ASCENT":
                ascent = int(t[1])
            elif k == "STARTCHAR":
                cur = {"enc": -1, "dw": 0, "bbx": (0, 0, 0, 0), "rows": []}
            elif k == "ENCODING":
                cur["enc"] = int(t[1])
            elif k == "DWIDTH":
                cur["dw"] = int(t[1])
            elif k == "BBX":
                cur["bbx"] = tuple(int(x) for x in t[1:5])
            elif k == "BITMAP":
                in_bitmap = True
    return glyphs, ascent


def bdf_pixels(g):
    """(x, y) of the set pixels of a BDF glyph: x from the pen, y = rows above the baseline (0 = the row just above)."""
    w, h, xo, yo = g["bbx"]
    out = []
    for r, hexrow in enumerate(g["rows"]):
        v = int(hexrow, 16)
        nbits = len(hexrow) * 4
        for c in range(w):
            if v >> (nbits - 1 - c) & 1:
                out.append((xo + c, yo + (h - 1 - r)))
    return out


class Source:
    """One font file as a function char -> list of (x, y_up, alpha 0..255) at native size, plus the baseline default."""

    def __init__(self, o):
        self.o = o
        p = o["path"].lower()
        self.bdf = p.endswith(".bdf")
        if self.bdf:
            self.glyphs, self.ascent = read_bdf(o["path"])
            if self.ascent is None:
                sys.exit("gen_font: no FONT_ASCENT in " + o["path"])
        else:
            from PIL import ImageFont
            self.font = ImageFont.truetype(o["path"], o["size"])
            self.ascent, self.descent = self.font.getmetrics()

    def render(self, code):
        """Pixels of one character at native size: dict (x, y_up) -> alpha, pen at x = 0, baseline at y = 0."""
        o = self.o
        ch = chr(code)
        if o["fold"] and "a" <= ch <= "z":
            ch = ch.upper()
            code = ord(ch)
        if self.bdf:
            g = self.glyphs.get(code)
            return {(x, y): 255 for x, y in bdf_pixels(g)} if g else {}
        from PIL import Image, ImageDraw
        size = int(o["size"] * 3) + 8
        im = Image.new("L", (size, size), 0)
        d = ImageDraw.Draw(im)
        d.fontmode = "1" if o["thr"] else "L"
        bx, by = size // 3, size * 2 // 3          # pen and baseline on the canvas
        if o["center"]:                            # centre by the advance: the cell is adv / sx native pixels wide
            bx += int((o["adv"] / o["sx"] - self.font.getlength(ch)) / 2)
        d.text((bx, by), ch, font=self.font, fill=255, anchor="ls")
        px = im.load()
        out = {}
        for yy in range(size):
            for xx in range(size):
                if px[xx, yy]:
                    out[(xx - bx, by - 1 - yy)] = px[xx, yy]      # y_up 0 = the row just above the baseline
        return out


def build(o):
    """-> (data bytes, off list, adv list, bw list, cell h, pad, first, last)"""
    src = Source(o)
    adv, h, pad, sx, sy = o["adv"], o["h"], o["pad"], o["sx"], o["sy"]
    bw = adv + 2 * pad
    if o.get("base") is not None:
        base = o["base"]
    elif src.bdf:
        base = src.ascent * sy
    else:
        asc, desc = src.ascent * sy, src.descent * sy
        base = (h - (asc + desc)) // 2 + asc
    data, off = bytearray(), []
    rb = (bw + 1) // 2
    for code in range(o["first"], o["last"] + 1):
        off.append(len(data))
        cell = [[0] * bw for _ in range(h)]
        for (x, yu), a in src.render(code).items():
            if o["thr"]:
                a = 255 if a >= o["thr"] else 0
            v = (a * 15 + 127) // 255
            if not v:
                continue
            for j in range(sy):                     # integer scale
                row = base - (yu + 1) * sy + j + o["dy"]
                for i in range(sx):
                    col = pad + x * sx + i + o["dx"]
                    if 0 <= row < h and 0 <= col < bw:
                        cell[row][col] = max(cell[row][col], v)
        for row in cell:
            for c in range(0, bw, 2):
                lo = row[c + 1] if c + 1 < bw else 0
                data.append(row[c] << 4 | lo)
        assert len(data) - off[-1] == rb * h
    n = o["last"] - o["first"] + 1
    return data, off, [adv] * n, [bw] * n, h, pad, o["first"], o["last"]


def emit(f, tag, data, off, adv, bw, h, pad, first, last):
    f.write(f"static const uint8_t FONT_{tag}_DATA[{len(data)}] = {{\n")
    for i in range(0, len(data), 24):
        chunk = data[i:i + 24]
        end = i + 24 >= len(data)
        f.write("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ("};\n" if end else ",\n"))
    n = len(off)
    f.write(f"static const uint16_t FONT_{tag}_OFF[{n}] = {{" + ", ".join(map(str, off)) + "};\n")
    f.write(f"static const uint8_t FONT_{tag}_ADV[{n}] = {{" + ", ".join(map(str, adv)) + "};\n")
    f.write(f"static const uint8_t FONT_{tag}_BW[{n}] = {{" + ", ".join(map(str, bw)) + "};\n")
    f.write(f"static const felucca_font_t FONT_{tag} = {{ {h}, {pad}, {first}, {last}, FONT_{tag}_ADV, FONT_{tag}_BW, "
            f"FONT_{tag}_OFF, FONT_{tag}_DATA }};\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("out")
    ap.add_argument("--small", required=True, help="FONT_S spec")
    ap.add_argument("--large", required=True, help="FONT_L spec")
    ap.add_argument("--medium", default=None, help="FONT_M spec (optional)")
    ap.add_argument("--comment", default=None, help="text of the first line of the header")
    a = ap.parse_args()
    s, l = parse_spec(a.small, S_DEF), parse_spec(a.large, L_DEF)
    if s["adv"] != 8 or l["adv"] != 16 or s["h"] != 16 or l["h"] != 32:
        print("gen_font: warning: the UI is laid out for FONT_S 8x16 and FONT_L 16x32 cells", file=sys.stderr)
    import os
    comment = a.comment or ("generated by tools/gen_font.py from %s (FONT_S) and %s (FONT_L)"
                            % (os.path.basename(s["path"]), os.path.basename(l["path"])))
    with open(a.out, "w") as f:
        f.write(f"/* {comment} */\n#pragma once\n#include <stdint.h>\n#define FONT_PAD {s['pad']}  /* x scale for L, see below */\n\n")
        emit(f, "S", *build(s))
        f.write("\n")
        emit(f, "L", *build(l))
        if a.medium:
            f.write("\n")
            emit(f, "M", *build(parse_spec(a.medium, M_DEF)))
    print("wrote", a.out)


if __name__ == "__main__":
    main()
