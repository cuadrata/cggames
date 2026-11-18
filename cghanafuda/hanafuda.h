/*
 * cghanafuda: the cards' pictures
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

#ifndef HANAFUDA_H
#define HANAFUDA_H

#include <glib.h>

/* A card, in the game's pixels: the table is laid out in these, and
 * drawn at whatever size fits the window. The cards' own shape, 960 by
 * 1600. */
#define HANAFUDA_WIDTH 60
#define HANAFUDA_HEIGHT 100

/* The sets of pictures installed: their names as stored, and as
 * shown. */
#define HANAFUDA_N_SETS 2
extern const gchar *const hanafuda_sets[HANAFUDA_N_SETS];
extern const gchar *const hanafuda_set_names[HANAFUDA_N_SETS];

/* The colour of the cards' frames and backs: as the sets have them, or
 * black. */
enum { HANAFUDA_RED, HANAFUDA_BLACK, HANAFUDA_N_BORDERS };
extern const gchar *const hanafuda_borders[HANAFUDA_N_BORDERS];
extern const gchar *const hanafuda_border_names[HANAFUDA_N_BORDERS];

typedef struct _Hanafuda Hanafuda;

/* The cards of set with border's frames and back, drawn height device
 * pixels high, and wide in proportion; NULL, with a message on stderr,
 * if the set can't be read. */
Hanafuda *hanafuda_new (const gchar *set, gint border, gint height);

/* Whether the cards of set with border's frames can be read: the sheet
 * is read now, not drawn, and kept for hanafuda_new(). */
gboolean hanafuda_readable (const gchar *set, gint border);

/* Lets go of the sheet kept since the last hanafuda_new() or
 * hanafuda_readable(). */
void hanafuda_forget (void);
void hanafuda_free (Hanafuda *cards);
gint hanafuda_width (Hanafuda *cards);
gint hanafuda_height (Hanafuda *cards);

/* A card's face, 0 to 47 as koikoi.h numbers them, or the back for -1:
 * hanafuda_width by hanafuda_height of RGBA, not
 * premultiplied, transparent outside the card. */
const guchar *hanafuda_card (Hanafuda *cards, gint card);

#endif
