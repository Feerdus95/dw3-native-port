#ifndef MENU_SKIN_H
#define MENU_SKIN_H

#include <SDL3/SDL.h>

/* The options overlay's look: the game's panels (the Digilog's colours and frame,
   item_list.c.patch), so the overlay reads as one of its menus. All in 320x240. */

/* A panel: the game's navy fill and its frame, the bottom-left corner cut */
void skin_panel(SDL_Renderer *r, const SDL_FRect *box);

/* A thin rule across a panel, from x0 to x1 */
void skin_rule(SDL_Renderer *r, float x0, float x1, float y);

/* The chosen row's bar, with the accent mark down its left side */
void skin_row(SDL_Renderer *r, const SDL_FRect *bar);

/* The chosen tab's plate */
void skin_tab(SDL_Renderer *r, const SDL_FRect *plate);

/* The game's scroll bar (updateScrollBar): a white thumb, no track */
void skin_thumb(SDL_Renderer *r, const SDL_FRect *thumb);

/* A line of text in the game's font at x, y (the debug font's top), in the ink set last; its
   width. The debug font stands in until the game's sheets are in VRAM. */
void skin_text(SDL_Renderer *r, float x, float y, const char *s);
float skin_text_width(const char *s);

/* Text colours: the label, the dimmed label, the value, the header */
void skin_ink(SDL_Renderer *r, int which);
enum { INK_LABEL, INK_DIM, INK_HEAD, INK_VALUE, INK_NOTE };

#endif
