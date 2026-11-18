/*
 * cgmahjong: the tiles
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

#ifndef TILESET_H
#define TILESET_H

#include <glib.h>

/* A tile's proportions: its face, 2 * HALF_WIDTH by 2 * HALF_HEIGHT, at
 * the top right, and its side, THICKNESS deep, showing to the left and
 * below, since each layer stands THICKNESS up and to the right of the
 * one under it. The tiles are drawn at any size in these proportions,
 * as near as whole pixels allow. */
#define HALF_WIDTH 22
#define HALF_HEIGHT 30
#define THICKNESS 6

/* The faces, as the game numbers them. */
#define TILESET_FACES 42

/* A tile's sizes in device pixels, for a half width of half_width. */
void tileset_proportions (gint half_width, gint *half_height,
                          gint *thickness);

typedef struct _Tileset Tileset;

/* The tiles drawn from the faces installed with the game, half_width
 * device pixels to half a face across; NULL, with a message on stderr,
 * if the faces can't be read. */
Tileset *tileset_new (gint half_width);
void tileset_free (Tileset *tiles);

/* The sizes the tiles were drawn at, in device pixels. */
gint tileset_half_width (Tileset *tiles);
gint tileset_half_height (Tileset *tiles);
gint tileset_thickness (Tileset *tiles);
gint tileset_width (Tileset *tiles);
gint tileset_height (Tileset *tiles);

/* A tile, tileset_width by tileset_height of RGBA, not premultiplied,
 * transparent outside it; face -1 for a tile without one. It belongs
 * to tiles, and is drawn the first time it is asked for. */
const guchar *tileset_tile (Tileset *tiles, gint face, gboolean selected);

/* Whether (x, y), in device pixels from the tile's corner, is on the
 * tile. */
gboolean tileset_hit (Tileset *tiles, gint x, gint y);

#endif
