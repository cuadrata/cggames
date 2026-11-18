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

/* Each set is one SVG, pieces/SET.svg: six cells across, king, queen,
 * rook, bishop, knight and pawn, the white pieces above the black. The
 * sheet is drawn once, at the size the pieces are wanted, and they are
 * cut from it. */

#include "config.h"

#include <string.h>

#include "chess.h"
#include "games.h"
#include "games-svg.h"
#include "pieces.h"

#define APPNAME "cgchess"

const gchar *const pieces_sets[PIECES_N_SETS] = { "merida", "cburnett" };
const gchar *const pieces_set_names[PIECES_N_SETS] = {
  N_("Merida"), N_("Burnett")
};

struct _Pieces
{
  gint size;
  guchar *pieces[16];           /* by chess.h's numbers */
};

Pieces *
pieces_new (const gchar *set, gint size)
{
  static const gint columns[7] = { -1, 5, 4, 3, 2, 1, 0 };
  gchar *file = g_strconcat ("pieces/", set, ".svg", NULL);
  gchar *path = games_installed_path (APPNAME, file);
  GamesSvg *svg = games_svg_load (path);
  Pieces *pieces;
  guchar *sheet;
  gint kind, side, y;

  g_free (file);
  g_free (path);
  if (!svg)
    return NULL;
  size = MAX (size, 1);
  sheet = games_svg_render (svg, 6 * size, 2 * size);
  games_svg_free (svg);
  if (!sheet)
    return NULL;

  pieces = g_new0 (Pieces, 1);
  pieces->size = size;
  for (side = 0; side < 2; side++)
    for (kind = CHESS_PAWN; kind <= CHESS_KING; kind++)
      {
        guchar *piece = g_malloc ((gsize) size * size * 4);

        for (y = 0; y < size; y++)
          memcpy (piece + (gsize) y * size * 4,
                  sheet + (((gsize) side * size + y) * 6 * size
                           + (gsize) columns[kind] * size) * 4,
                  (gsize) size * 4);
        pieces->pieces[kind | (side ? CHESS_BLACK_PIECE : 0)] = piece;
      }
  g_free (sheet);
  return pieces;
}

void
pieces_free (Pieces *pieces)
{
  gint i;

  if (!pieces)
    return;
  for (i = 0; i < 16; i++)
    g_free (pieces->pieces[i]);
  g_free (pieces);
}

gint
pieces_size (Pieces *pieces)
{
  return pieces->size;
}

const guchar *
pieces_get (Pieces *pieces, gint piece)
{
  return pieces->pieces[piece];
}
