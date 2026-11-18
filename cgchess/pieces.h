/*
 * cgchess: the pieces' pictures
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

#ifndef PIECES_H
#define PIECES_H

#include <glib.h>

/* The sets installed: their names as stored, and as shown. */
#define PIECES_N_SETS 2
extern const gchar *const pieces_sets[PIECES_N_SETS];
extern const gchar *const pieces_set_names[PIECES_N_SETS];

typedef struct _Pieces Pieces;

/* The pieces of set, each drawn size device pixels square; NULL if the
 * set can't be read. */
Pieces *pieces_new (const gchar *set, gint size);
void pieces_free (Pieces *pieces);
gint pieces_size (Pieces *pieces);

/* A piece, as chess.h numbers them: size by size of RGBA, not
 * premultiplied. */
const guchar *pieces_get (Pieces *pieces, gint piece);

#endif
