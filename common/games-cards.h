/* cggames: playing cards
 * Copyright (C) 2026 The CGTK authors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef GAMES_CARDS_H
#define GAMES_CARDS_H

#include <glib.h>

/* The suits in bridge order. */
typedef enum
{
  GAMES_CLUBS,
  GAMES_DIAMONDS,
  GAMES_HEARTS,
  GAMES_SPADES
} GamesSuit;

#define GAMES_SUIT_RED(suit) ((suit) == GAMES_DIAMONDS || (suit) == GAMES_HEARTS)

#define GAMES_ACE   1
#define GAMES_JACK  11
#define GAMES_QUEEN 12
#define GAMES_KING  13

/* The designs on the backs, shown six to a row. */
#define GAMES_CARDS_N_BACKS 12
#define GAMES_CARDS_BACKS_PER_ROW 6

typedef struct _GamesCards GamesCards;

/* Draws the faces from the deck installed with the games, and the
 * backs, scale device pixels to each of the game's: a whole scale for a
 * window of that many device pixels to each of the toolkit's
 * (gdk_window_get_scale_factor), or any, for cards drawn larger or
 * smaller; NULL, with a message on stderr, if the deck can't be read. */
GamesCards *games_cards_new (gint scale);
GamesCards *games_cards_new_scaled (gdouble scale);
void games_cards_free (GamesCards *cards);

/* Every card is this size, in the game's pixels, the same at any
 * scale, which a game lays its table out in. */
void games_cards_size (GamesCards *cards, gint *width, gint *height);
/* The scale the cards were drawn at: near the one asked for, so that
 * the cards are whole device pixels. */
gdouble games_cards_scale (GamesCards *cards);
/* Every card's images are this size, in device pixels. */
void games_cards_pixel_size (GamesCards *cards, gint *width, gint *height);

/* Images of games_cards_pixel_size: 8-bit RGBA, not premultiplied, in
 * rows of width * 4 bytes, with the rounded corners transparent. They
 * belong to cards.
 * A back is drawn the first time it is asked for.
 * The slot is the outline of an empty place, to lay over the table. */
const guchar *games_cards_face (GamesCards *cards, GamesSuit suit, gint rank);
const guchar *games_cards_back (GamesCards *cards, gint back);
const guchar *games_cards_slot (GamesCards *cards);

/* A part of a card's face, width by height of the game's pixels from
 * (x, y) on it, drawn pixels device pixels to each of those: RGBA,
 * width * pixels by height * pixels, each rounded to the nearest, to be
 * freed with g_free; NULL, with a message on stderr, if the deck can't
 * be read. */
guchar *games_cards_detail (GamesCards *cards, GamesSuit suit, gint rank,
                            gint x, gint y, gint width, gint height,
                            gdouble pixels);

#endif
