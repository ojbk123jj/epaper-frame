#include "epd_text.h"

#include "epd_fb_pack.h"
#include "font8.h"

#define GLYPH_W 5
#define GLYPH_H 8

void epd_text_draw(uint8_t *fb, int x, int y, const char *s,
                   epd_color_t fg, epd_color_t bg, int scale)
{
    if (scale < 1) {
        scale = 1;
    }
    for (const char *p = s; *p != '\0'; p++) {
        unsigned ch = (unsigned char)*p;
        if (ch < 0x20 || ch > 0x7E) {
            ch = (unsigned)'?';
        }
        const uint8_t *glyph = &epd_font8_table[(ch - 0x20) * GLYPH_H];
        for (int row = 0; row < GLYPH_H; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < GLYPH_W + 1; col++) {
                bool on = (col < GLYPH_W) && ((bits >> (7 - col)) & 0x1);
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        epd_pack_set(fb, x + col * scale + sx,
                                     y + row * scale + sy,
                                     on ? fg : bg);
                    }
                }
            }
        }
        x += (GLYPH_W + 1) * scale;
    }
}

int epd_text_width(const char *s, int scale)
{
    if (scale < 1) {
        scale = 1;
    }
    int n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n * (GLYPH_W + 1) * scale;
}
