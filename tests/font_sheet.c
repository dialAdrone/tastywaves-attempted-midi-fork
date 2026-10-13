/* SPDX-License-Identifier: GPL-3.0-only */
/* Font comparison harness: draws the UI's strings with the firmware's own text code (gfx.c) and whatever
 * felucca_font.h is first on the include path, and writes a PPM. tests/font_sheet.py builds this once per candidate:
 *   cc -DDINGHY_HOST -Ifirmware/src -I<dir with the candidate felucca_font.h> tests/font_sheet.c -o ...  (that -I first) */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define CV_MAX (640u * 64u)
static uint16_t fb[640 * 320];
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 640u && y + j < 320u) {
                uint16_t c = px[j * w + i];
                fb[(y + j) * 640u + x + i] = (uint16_t)(c >> 8 | c << 8);
            }
}
#include "gfx.c"

#define LILAC RGB(180, 140, 255)
#define GRAY RGB(130, 130, 142)
int main(int argc, char **argv)
{
    static const char *const S1 = "CUTOFF 90  RESO 40  001 BRIGHT PAD  FRM 13/32  V5/12  CPU 27%";
    static const char *const S2 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789 +-./%>#:()";
    static const char *const S3 = "ATK 118MS  REL 1.2S  DETUNE +7  LEGATO  S&H  OSC1 OSC2 SUB NOISE";
    FILE *f;
    uint32_t i;
    if (argc < 2) return 1;
    cv_begin(640, 16, C_BLACK); cv_text(4, 0, &FONT_S, S1, C_WHITE); cv_blit(0, 0);
    cv_begin(640, 16, C_BLACK); cv_text(4, 0, &FONT_S, S2, GRAY); cv_blit(0, 18);
    cv_begin(640, 16, C_BLACK); cv_text(4, 0, &FONT_S, S3, LILAC); cv_blit(0, 36);
    cv_begin(640, 32, C_BLACK); cv_text(4, 0, &FONT_L, "001 BRIGHT PAD", C_WHITE); cv_text(300, 0, &FONT_L, "FRM 13/32", LILAC); cv_blit(0, 56);
    cv_begin(640, 32, C_BLACK); cv_text(4, 0, &FONT_L, "CUTOFF 90  RESO 40", C_WHITE); cv_blit(0, 92);
    cv_begin(640, 32, C_BLACK); cv_text(4, 0, &FONT_L, "70 19 +12 -64 100", LILAC); cv_text(300, 0, &FONT_L, "SAWSQ VOWEL", C_WHITE); cv_blit(0, 128);
    f = fopen(argv[1], "wb");
    fprintf(f, "P6 640 164 255\n");
    for (i = 0; i < 640u * 164u; i++) {
        uint16_t c = fb[i];
        fputc((c >> 11) * 255 / 31, f); fputc((c >> 5 & 63) * 255 / 63, f); fputc((c & 31) * 255 / 31, f);
    }
    fclose(f);
    return 0;
}
