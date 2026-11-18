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

/* The tiles' bodies are drawn here, ivory faces on sides that show the
 * coloured back, as a riichi set's tiles are. The faces' pictures are
 * in tiles.svg: 42 of 300 by 400, 21 across and two rows down, in the
 * game's order (circles; winds, north, west, south, east; red and green
 * dragons; characters; bamboos; flowers; the white dragon; seasons).
 * The sheet is drawn once at the tiles' size and the faces cut from it,
 * over the bodies; the bodies are drawn at that size too, so the tiles
 * are as sharp at any size. */

#include "config.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "games.h"
#include "games-svg.h"
#include "tileset.h"

#define APPNAME "cgmahjong"

#define FACES_ACROSS 21
#define FACES_DOWN 2

struct _Tileset
{
  /* In device pixels: half the face across and down, the side's
   * depth, and the whole tile. */
  gint half_width, half_height, thickness, width, height;
  double corner, line;          /* the face's rounding, its outline */
  guchar *sheet;                /* the faces, FACES_ACROSS by FACES_DOWN */
  gint face_width, face_height; /* one picture */
  guchar *body[2];              /* plain, selected */
  guchar *tiles[2][TILESET_FACES];
};

/* The corners of the face are rounded to this, and its outline and the
 * side's edges are this wide, in a tile's own units. */
#define CORNER 5.0
#define LINE 0.8

/* Each device pixel is the average of 4 by 4 samples. */
#define SAMPLES 4

void
tileset_proportions (gint half_width, gint *half_height, gint *thickness)
{
  *half_height = (half_width * HALF_HEIGHT + HALF_WIDTH / 2) / HALF_WIDTH;
  *thickness = MAX (1, (half_width * THICKNESS + HALF_WIDTH / 2) / HALF_WIDTH);
}

/* How far into the face's rounded rectangle (x, y) is, in device
 * pixels; negative outside. */
static double
inside_face (const Tileset *g, double x, double y)
{
  double x0 = g->thickness, y0 = 0;
  double x1 = g->width, y1 = 2 * g->half_height, r = g->corner;
  double cx = x < x0 + r ? x0 + r : x > x1 - r ? x1 - r : x;
  double cy = y < y0 + r ? y0 + r : y > y1 - r ? y1 - r : y;

  if (x < x0 || y < y0 || x > x1 || y > y1)
    return -1;
  if (cx != x || cy != y)
    return r - sqrt ((x - cx) * (x - cx) + (y - cy) * (y - cy));
  return MIN (MIN (x - x0, x1 - x), MIN (y - y0, y1 - y));
}

/* The colour of a sample of the body, 0xRRGGBB, or -1 for none. The
 * face is lit from the top left; behind it the side, its depth t, is
 * ivory near the face and then the back's colour. */
static glong
body_sample (const Tileset *g, double x, double y, gboolean selected)
{
  double d = inside_face (g, x, y), t, th = g->thickness;
  /* The side is swept in 24 steps, whatever its depth. */
  double step = th / 24;
  gint r, gr, b;

  if (d >= 0)
    {
      /* The face, with a fine darker line round it. */
      double shade = (x - th) / (2 * g->half_width) * 0.4
        + y / (2 * g->half_height) * 0.6;

      if (d < g->line)
        return 0x8a7c6a;
      if (selected)
        {
          r = 205 - (gint) (35 * shade);
          gr = 222 - (gint) (30 * shade);
          b = 250 - (gint) (20 * shade);
        }
      else
        {
          r = 247 - (gint) (14 * shade);
          gr = 243 - (gint) (16 * shade);
          b = 237 - (gint) (20 * shade);
        }
      return ((glong) r << 16) | (gr << 8) | b;
    }
  /* The side: the face moved t down and to the left. */
  for (t = step; t <= th; t += step)
    if ((d = inside_face (g, x + t, y - t)) >= 0)
      {
        double k = t / th;

        if (d < g->line || t > th - g->line)
          return 0x5a3020;
        if (k < 0.45)
          return selected ? 0xa8b8d8 : 0xd8cfc2;
        return 0xc8442e;
      }
  return -1;
}

/* The body, the tile's size, unselected or selected. */
static guchar *
make_body (const Tileset *g, gboolean selected)
{
  gint w = g->width, h = g->height, x, y, i, j;
  guchar *pixels = g_malloc ((gsize) w * h * 4), *p = pixels;

  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++, p += 4)
      {
        guint r = 0, gr = 0, b = 0, n = 0;

        for (j = 0; j < SAMPLES; j++)
          for (i = 0; i < SAMPLES; i++)
            {
              glong c = body_sample (g, x + (i + 0.5) / SAMPLES,
                                     y + (j + 0.5) / SAMPLES, selected);

              if (c < 0)
                continue;
              r += (guint) (c >> 16) & 0xff;
              gr += (guint) (c >> 8) & 0xff;
              b += (guint) c & 0xff;
              n++;
            }
        if (n)
          {
            p[0] = (guchar) ((r + n / 2) / n);
            p[1] = (guchar) ((gr + n / 2) / n);
            p[2] = (guchar) ((b + n / 2) / n);
          }
        else
          p[0] = p[1] = p[2] = 0;
        p[3] = (guchar) ((n * 255 + SAMPLES * SAMPLES / 2)
                         / (SAMPLES * SAMPLES));
      }
  return pixels;
}

Tileset *
tileset_new (gint half_width)
{
  gchar *path = games_installed_path (APPNAME, "tiles/tiles.svg");
  GamesSvg *svg = games_svg_load (path);
  Tileset *tiles;
  double zoom;

  g_free (path);
  if (!svg)
    return NULL;
  tiles = g_new0 (Tileset, 1);
  tiles->half_width = MAX (half_width, 2);
  tileset_proportions (tiles->half_width, &tiles->half_height,
                       &tiles->thickness);
  tiles->width = 2 * tiles->half_width + tiles->thickness;
  tiles->height = 2 * tiles->half_height + tiles->thickness;
  zoom = (double) tiles->half_width / HALF_WIDTH;
  tiles->corner = CORNER * zoom;
  tiles->line = MAX (LINE * zoom, 0.6);

  /* A picture fills the tile's face across; down, it is 4 to 3 of
   * that, a little short of the face, and centred on it. Its height is
   * rounded up, so that the sheet is drawn to the width. */
  tiles->face_width = 2 * tiles->half_width;
  tiles->face_height = (tiles->face_width * 4 + 2) / 3;
  tiles->sheet = games_svg_render (svg, tiles->face_width * FACES_ACROSS,
                                   tiles->face_height * FACES_DOWN);
  games_svg_free (svg);
  if (!tiles->sheet)
    {
      g_free (tiles);
      return NULL;
    }
  tiles->body[0] = make_body (tiles, FALSE);
  tiles->body[1] = make_body (tiles, TRUE);
  return tiles;
}

void
tileset_free (Tileset *tiles)
{
  gint i, j;

  if (!tiles)
    return;
  for (i = 0; i < TILESET_FACES; i++)
    for (j = 0; j < 2; j++)
      g_free (tiles->tiles[j][i]);
  g_free (tiles->sheet);
  g_free (tiles->body[0]);
  g_free (tiles->body[1]);
  g_free (tiles);
}

gint tileset_half_width (Tileset *tiles) { return tiles->half_width; }
gint tileset_half_height (Tileset *tiles) { return tiles->half_height; }
gint tileset_thickness (Tileset *tiles) { return tiles->thickness; }
gint tileset_width (Tileset *tiles) { return tiles->width; }
gint tileset_height (Tileset *tiles) { return tiles->height; }

/* The face's picture, cut from the sheet, over the body. */
static guchar *
make_tile (Tileset *tiles, gint face, gboolean selected)
{
  gint w = tiles->width, h = tiles->height;
  gint pw = tiles->face_width, ph = tiles->face_height;
  gint sheet_width = pw * FACES_ACROSS;
  /* The sheet is drawn pw / 300 to its unit, so its rows are 400 of
   * those apart, which needn't be a whole number of pixels. */
  gint sx = face % FACES_ACROSS * pw;
  gint sy = (gint) floor (face / FACES_ACROSS * 400.0 * pw / 300 + 0.5);
  gint ox = tiles->thickness, oy = (2 * tiles->half_height - ph) / 2;
  guchar *tile = g_memdup (tiles->body[selected], (guint) ((gsize) w * h * 4));
  gint x, y, k;

  for (y = 0; y < ph; y++)
    {
      if (oy + y < 0 || oy + y >= h || sy + y >= ph * FACES_DOWN)
        continue;
      for (x = 0; x < pw; x++)
        {
          const guchar *p = tiles->sheet + ((gsize) (sy + y) * sheet_width
                                            + sx + x) * 4;
          guchar *d = tile + ((gsize) (oy + y) * w + ox + x) * 4;
          guint a = p[3];

          if (!a || !d[3])
            continue;
          for (k = 0; k < 3; k++)
            d[k] = (guchar) ((p[k] * a + d[k] * (255 - a) + 127) / 255);
        }
    }
  return tile;
}

const guchar *
tileset_tile (Tileset *tiles, gint face, gboolean selected)
{
  selected = selected != 0;
  if (face < 0 || face >= TILESET_FACES)
    return tiles->body[selected];
  if (!tiles->tiles[selected][face])
    tiles->tiles[selected][face] = make_tile (tiles, face, selected);
  return tiles->tiles[selected][face];
}

gboolean
tileset_hit (Tileset *tiles, gint x, gint y)
{
  if (x < 0 || y < 0 || x >= tiles->width || y >= tiles->height)
    return FALSE;
  return tiles->body[0][((gsize) y * tiles->width + x) * 4 + 3] >= 128;
}
