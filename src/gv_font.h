// gv_font.h - 5x7 bitmap font for the HUD and the debug overlay.
#ifndef GV_FONT_H
#define GV_FONT_H

#include "gv_common.h"

#define GV_GLYPH_W   5
#define GV_GLYPH_H   7
#define GV_GLYPH_ADV 6    // pixels between glyph origins
#define GV_LINE_ADV  8    // pixels between baselines

bool gv_font_init(SDL_Renderer *ren);
void gv_font_quit(void);

void gv_font_draw(SDL_Renderer *ren, int x, int y, SDL_Color c, const char *text);
void gv_font_printf(SDL_Renderer *ren, int x, int y, SDL_Color c,
                    SDL_PRINTF_FORMAT_STRING const char *fmt, ...) SDL_PRINTF_VARARG_FUNC(5);
int  gv_font_width(const char *text);

// Draws centred on the logical playfield's horizontal midpoint.
void gv_font_center(SDL_Renderer *ren, int y, SDL_Color c, const char *text);

#endif // GV_FONT_H
