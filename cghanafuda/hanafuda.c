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

/* Each set is one SVG, cards/SET.svg: cells of CELL_W by CELL_H, with a
 * card of CARD_W by CARD_H at CARD_X, CARD_Y in each, January to
 * December across and the month's four cards down in koikoi.h's order,
 * the back first in the fifth row. The card's edge is its frame's outer
 * edge, with square corners; anything past it is cut off here. The
 * sheet is drawn once, at the size the cards are wanted, and the cards
 * cut from it. */

#include "config.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "games.h"
#include "games-svg.h"
#include "hanafuda.h"

#define APPNAME "cghanafuda"

#define CELL_W 1120.0
#define CELL_H 1744.0
#define CARD_X 72.0             /* the card in its cell */
#define CARD_Y 72.0
#define CARD_W 960.0
#define CARD_H 1600.0

const gchar *const hanafuda_sets[HANAFUDA_N_SETS] = {
  "traditional", "light"
};

const gchar *const hanafuda_set_names[HANAFUDA_N_SETS] = {
  N_("Traditional"), N_("Light")
};

const gchar *const hanafuda_borders[HANAFUDA_N_BORDERS] = {
  "red", "black"
};

const gchar *const hanafuda_border_names[HANAFUDA_N_BORDERS] = {
  N_("Red border"), N_("Black border")
};

/* The colours every set gives its frames and back, and the keyline
 * inside each frame, and what they become for each border: used
 * nowhere else in the sheets, so they can be swapped in the text. The
 * black frame's keyline is grey, so black leaves that meet the frame
 * stay apart from it. */
#define FRAME 0
#define KEYLINE 1
static const gchar *const colours[HANAFUDA_N_BORDERS][2] = {
  { "#8f2a1c", "#6d1e13" },
  { "#0b0b0c", "#55575c" }
};

/* set's sheet, its frames and back in border's colours. */
static GamesSvg *
load (const gchar *path, gint border)
{
  GamesSvg *svg;
  GError *error = NULL;
  gchar *text;
  gint k;

  if (border == HANAFUDA_RED)
    return games_svg_load (path);
  if (!g_file_get_contents (path, &text, NULL, &error))
    {
      fprintf (stderr, "%s\n", error->message);
      g_error_free (error);
      return NULL;
    }
  for (k = FRAME; k <= KEYLINE; k++)
    {
      const gchar *from = colours[HANAFUDA_RED][k];
      gsize n = strlen (from);
      gchar *p = text;

      while ((p = strstr (p, from)) != NULL)
        {
          memcpy (p, colours[border][k], n);
          p += n;
        }
    }
  svg = games_svg_parse (text);
  if (!svg)
    fprintf (stderr, "%s: the cards could not be drawn\n", path);
  g_free (text);
  return svg;
}

struct _Hanafuda
{
  gint width, height;
  guchar *cards[49];            /* the faces, then the back */
};

/* The last sheet read, kept so that drawing the cards again at another
 * size, small or for the charts does not read it again. */
static GamesSvg *held;
static gchar *held_set;
static gint held_border;

void
hanafuda_forget (void)
{
  if (held)
    games_svg_free (held);
  held = NULL;
  g_free (held_set);
  held_set = NULL;
}

/* set's sheet with border's frames, read now or kept from before. */
static GamesSvg *
sheet_of (const gchar *set, gint border)
{
  gchar *file, *path;

  if (held && held_border == border && strcmp (held_set, set) == 0)
    return held;
  hanafuda_forget ();
  file = g_strconcat ("cards/", set, ".svg", NULL);
  path = games_installed_path (APPNAME, file);
  held = load (path, border);
  g_free (file);
  g_free (path);
  if (held)
    {
      held_set = g_strdup (set);
      held_border = border;
    }
  return held;
}

gboolean
hanafuda_readable (const gchar *set, gint border)
{
  return sheet_of (set, border) != NULL;
}

Hanafuda *
hanafuda_new (const gchar *set, gint border, gint height)
{
  GamesSvg *svg = sheet_of (set, border);
  gint h = MAX (height, 2);
  gint w = MAX ((h * HANAFUDA_WIDTH + HANAFUDA_HEIGHT / 2) / HANAFUDA_HEIGHT, 1);
  double k = h / CARD_H;        /* device pixels to the sheet's unit */
  gint sheet_w = (gint) ceil (12 * CELL_W * k);
  gint sheet_h = (gint) ceil (5 * CELL_H * k);
  guchar *sheet;
  Hanafuda *cards;
  gint c, x, y;

  if (!svg)
    return NULL;
  /* Drawn to the height; the width, in the same proportion, fits. */
  sheet = games_svg_render (svg, sheet_w, sheet_h);
  if (!sheet)
    return NULL;

  cards = g_new0 (Hanafuda, 1);
  cards->width = w;
  cards->height = h;
  for (c = 0; c < 49; c++)
    {
      /* The back is first in the fifth row. */
      gint col = c < 48 ? c / 4 : 0, row = c < 48 ? c % 4 : 4;
      gint x0 = (gint) floor ((col * CELL_W + CARD_X + (CARD_W - w / k) / 2)
                              * k + 0.5);
      gint y0 = (gint) floor ((row * CELL_H + CARD_Y) * k + 0.5);
      guchar *card = g_malloc ((gsize) w * h * 4);

      for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
          {
            guchar *d = card + ((gsize) y * w + x) * 4;
            gint sx = x0 + x, sy = y0 + y;

            if (sx < sheet_w && sy < sheet_h)
              memcpy (d, sheet + ((gsize) sy * sheet_w + sx) * 4, 4);
            else
              memset (d, 0, 4);
          }
      cards->cards[c] = card;
    }
  g_free (sheet);
  return cards;
}

void
hanafuda_free (Hanafuda *cards)
{
  gint c;

  if (!cards)
    return;
  for (c = 0; c < 49; c++)
    g_free (cards->cards[c]);
  g_free (cards);
}

gint
hanafuda_width (Hanafuda *cards)
{
  return cards->width;
}

gint
hanafuda_height (Hanafuda *cards)
{
  return cards->height;
}

const guchar *
hanafuda_card (Hanafuda *cards, gint card)
{
  return cards->cards[card < 0 || card >= 48 ? 48 : card];
}
