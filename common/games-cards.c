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

/* The faces are drawn from deck.svg, a public domain deck: 54 cards of
 * 750 by 1050 units, nine to a row with nothing between them, spades,
 * hearts, clubs and diamonds from the ace to the king, then the two
 * jokers. The backs are drawn from pictures of their own in backs/,
 * the same size; the cards' black edges are drawn here. */

#include "config.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "games.h"
#include "games-cards.h"
#include "games-svg.h"

/* The cards' width in the toolkit's pixels; the height follows from
 * the deck, 140 for the deck's 5 by 7. */
#define CARD_WIDTH 100

#define DECK_COLUMNS 9
#define DECK_ROWS 6

struct _GamesCards
{
  gint width, height;           /* in the game's pixels */
  gdouble scale;                /* device pixels to each of the game's */
  gint pixel_width, pixel_height;       /* in device pixels */
  guchar *faces[4][13];
  guchar *backs[GAMES_CARDS_N_BACKS];   /* drawn when first wanted */
  guchar *blank, *edge;         /* kept for drawing them */
  guchar *slot;
};

/* Coverage. */

/* The corners are smoothed by taking 4 by 4 samples in every pixel. */
#define SAMPLES 4

/* Whether (x, y) is inside the rectangle from (x0, y0) to (x1, y1) with
 * corners rounded to radius r. */
static gboolean
in_rounded (double x, double y, double x0, double y0, double x1, double y1,
            double r)
{
  double cx, cy;

  if (x < x0 || y < y0 || x >= x1 || y >= y1)
    return FALSE;
  cx = x < x0 + r ? x0 + r : x > x1 - r ? x1 - r : x;
  cy = y < y0 + r ? y0 + r : y > y1 - r ? y1 - r : y;
  return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r;
}

/* The colour of a sample, 0xRRGGBB, or -1 for none. */
typedef glong (*SampleFunc) (gint width, gint height, double x, double y,
                             gint back);

/* An image of a card, width by height of the game's pixels, drawn by
 * func dw by dh device pixels, every device pixel the average of its
 * samples. func is given the game's pixels, so that a design is the
 * same at any size, only finer. */
static guchar *
render (gint width, gint height, gint dw, gint dh, SampleFunc func, gint back)
{
  double sx = (double) dw / width, sy = (double) dh / height;
  guchar *pixels = g_malloc ((gsize) dw * dh * 4);
  guchar *p = pixels;
  gint x, y, i, j;

  for (y = 0; y < dh; y++)
    for (x = 0; x < dw; x++, p += 4)
      {
        guint r = 0, g = 0, b = 0, n = 0;

        for (j = 0; j < SAMPLES; j++)
          for (i = 0; i < SAMPLES; i++)
            {
              glong c = func (width, height,
                              (x + (i + 0.5) / SAMPLES) / sx,
                              (y + (j + 0.5) / SAMPLES) / sy, back);

              if (c < 0)
                continue;
              r += (guint) (c >> 16) & 0xff;
              g += (guint) (c >> 8) & 0xff;
              b += (guint) c & 0xff;
              n++;
            }
        if (n)
          {
            p[0] = (guchar) ((r + n / 2) / n);
            p[1] = (guchar) ((g + n / 2) / n);
            p[2] = (guchar) ((b + n / 2) / n);
          }
        else
          p[0] = p[1] = p[2] = 0;
        p[3] = (guchar) ((n * 255 + SAMPLES * SAMPLES / 2) / (SAMPLES * SAMPLES));
      }
  return pixels;
}

/* The corners' radius: the deck's, 37.5 of 750. */
static gint corner;

/* A blank card: white, edged in black. */
static glong
blank_sample (gint width, gint height, double x, double y, gint back)
{
  (void) back;
  if (!in_rounded (x, y, 0, 0, width, height, corner))
    return -1;
  if (!in_rounded (x, y, 1, 1, width - 1, height - 1, corner - 1))
    return 0x000000;
  return 0xffffff;
}

/* The outline of an empty place: a darker edge over whatever the table
 * is, leaving it showing inside. */
static glong
slot_sample (gint width, gint height, double x, double y, gint back)
{
  (void) back;
  if (!in_rounded (x, y, 0, 0, width, height, corner)
      || in_rounded (x, y, 2, 2, width - 2, height - 2, corner - 2))
    return -1;
  return 0x000000;
}

/* The black edge alone, to lay over a face. */
static glong
edge_sample (gint width, gint height, double x, double y, gint back)
{
  (void) back;
  if (!in_rounded (x, y, 0, 0, width, height, corner)
      || in_rounded (x, y, 1, 1, width - 1, height - 1, corner - 1))
    return -1;
  return 0x000000;
}

/* The share of a pixel's colour that is the edge's, out of 255: the
 * edge's coverage of the pixel as a part of the card's, so that where
 * only the edge covers it, round the corners, it is all edge, with
 * none of what lies inside showing through. */
static guint
edge_share (guint edge, guint shape)
{
  if (shape == 0)
    return 0;
  return MIN (255u, (edge * 255u + shape / 2) / shape);
}

/* The backs, each drawn from a picture of its own in backs/, of the
 * deck's 750 by 1050, any white border included; in the order of the
 * deck dialog, six to a row; the first is the default. */
static const gchar *const backs[GAMES_CARDS_N_BACKS] = {
  "argyle.svg", "red-club.svg", "blue-club.svg", "robot.svg",
  "roses.svg", "black-vines.svg", "clownfish.svg", "trumpet-snail.svg",
  "castle.svg", "island.svg", "art-deco.svg", "peacock.svg"
};

/* Back n in device pixels: its picture over white, cut to the card's
 * shape and edged in black; a blank card if the picture can't be
 * read. */
static guchar *
make_back (const GamesCards *cards, const guchar *blank, const guchar *edge,
           gint n)
{
  gint w = cards->pixel_width, h = cards->pixel_height;
  gchar *file = g_strconcat ("backs/", backs[n], NULL);
  gchar *path = games_installed_path ("cards", file);
  GamesSvg *svg = games_svg_load (path);
  guchar *picture = NULL, *back;
  gint i;

  g_free (path);
  g_free (file);
  if (svg)
    {
      picture = games_svg_render (svg, w, h);
      games_svg_free (svg);
    }
  back = g_memdup (blank, (guint) ((gsize) w * h * 4));
  if (!picture)
    return back;
  for (i = 0; i < w * h; i++)
    {
      const guchar *s = picture + (gsize) i * 4;
      const guchar *e = edge + (gsize) i * 4;
      guchar *d = back + (gsize) i * 4;
      guint a = s[3], t = edge_share (e[3], d[3]), k;

      for (k = 0; k < 3; k++)
        {
          guint c = (s[k] * a + 255 * (255 - a) + 127) / 255;

          d[k] = (guchar) ((e[k] * t + c * (255u - t) + 127) / 255);
        }
      /* d[3], the card's shape, is the blank's. */
    }
  g_free (picture);
  return back;
}

/* The faces. */

/* The deck's rows go spades, hearts, clubs, diamonds. */
static const gint deck_row_of_suit[4] = {
  2,                            /* GAMES_CLUBS */
  3,                            /* GAMES_DIAMONDS */
  1,                            /* GAMES_HEARTS */
  0                             /* GAMES_SPADES */
};

/* Card n of the deck, cut from the whole deck drawn deck_height high
 * with its cards row_height apart, to the card's shape, with the edge
 * laid over it; in device pixels. */
static guchar *
make_face (GamesCards *cards, const guchar *deck, gint deck_height,
           double row_height, const guchar *blank, const guchar *edge, gint n)
{
  gint w = cards->pixel_width, h = cards->pixel_height;
  gint deck_width = w * DECK_COLUMNS;
  gint x0 = n % DECK_COLUMNS * w;
  gint y0 = (gint) floor (n / DECK_COLUMNS * row_height + 0.5);
  guchar *card = g_malloc ((gsize) w * h * 4);
  gint x, y;

  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      {
        const guchar *s = deck + ((gsize) MIN (y0 + y, deck_height - 1)
                                  * deck_width + x0 + x) * 4;
        const guchar *e = edge + ((gsize) y * w + x) * 4;
        guchar *d = card + ((gsize) y * w + x) * 4;
        guint shape = blank[((gsize) y * w + x) * 4 + 3];
        guint a = edge_share (e[3], shape), k;

        for (k = 0; k < 3; k++)
          d[k] = (guchar) ((e[k] * a + s[k] * (255 - a) + 127) / 255);
        d[3] = (guchar) shape;
      }
  return card;
}

GamesCards *
games_cards_new (gint scale)
{
  return games_cards_new_scaled (MAX (scale, 1));
}

GamesCards *
games_cards_new_scaled (gdouble scale)
{
  gchar *path = games_installed_path ("cards", "deck.svg");
  GamesSvg *svg = games_svg_load (path);
  GamesCards *cards;
  guchar *deck, *edge, *blank;
  gdouble deck_w, deck_h, unit;
  gint i, suit, rank, deck_height;

  g_free (path);
  if (!svg)
    return NULL;
  games_svg_size (svg, &deck_w, &deck_h);

  cards = g_new0 (GamesCards, 1);
  cards->width = CARD_WIDTH;
  cards->height = (gint) floor (CARD_WIDTH * (deck_h / DECK_ROWS)
                                / (deck_w / DECK_COLUMNS) + 0.5);
  /* Whole device pixels each way, in the deck's proportions. */
  cards->pixel_width = MAX ((gint) floor (CARD_WIDTH * scale + 0.5), 1);
  cards->pixel_height = MAX ((gint) floor (cards->pixel_width
                                           * (deck_h / DECK_ROWS)
                                           / (deck_w / DECK_COLUMNS) + 0.5), 1);
  cards->scale = (gdouble) cards->pixel_width / CARD_WIDTH;
  corner = MAX (cards->width / 20, 2);

  /* The deck drawn so that each card is a whole number of pixels wide,
   * the rows falling where they fall. */
  unit = cards->pixel_width / (deck_w / DECK_COLUMNS);
  deck_height = (gint) ceil (deck_h * unit);
  deck = games_svg_render_area (svg, 0, 0, unit,
                                cards->pixel_width * DECK_COLUMNS, deck_height);
  games_svg_free (svg);
  if (!deck)
    {
      g_free (cards);
      return NULL;
    }
  edge = render (cards->width, cards->height, cards->pixel_width,
                 cards->pixel_height, edge_sample, 0);
  blank = render (cards->width, cards->height, cards->pixel_width,
                  cards->pixel_height, blank_sample, 0);
  for (suit = GAMES_CLUBS; suit <= GAMES_SPADES; suit++)
    for (rank = GAMES_ACE; rank <= GAMES_KING; rank++)
      cards->faces[suit][rank - 1] =
        make_face (cards, deck, deck_height, deck_h / DECK_ROWS * unit,
                   blank, edge, deck_row_of_suit[suit] * 13 + rank - 1);
  g_free (deck);

  /* The backs are drawn as they're wanted: one in play, the rest when
   * the deck dialog shows them. */
  cards->blank = blank;
  cards->edge = edge;
  cards->slot = render (cards->width, cards->height, cards->pixel_width,
                        cards->pixel_height, slot_sample, 0);
  /* The edge is 40% black, darkening what it lies on. */
  for (i = 0; i < cards->pixel_width * cards->pixel_height; i++)
    cards->slot[i * 4 + 3] = (guchar) (cards->slot[i * 4 + 3] * 2 / 5);
  return cards;
}

void
games_cards_free (GamesCards *cards)
{
  gint i, j;

  if (!cards)
    return;
  for (i = 0; i < 4; i++)
    for (j = 0; j < 13; j++)
      g_free (cards->faces[i][j]);
  for (i = 0; i < GAMES_CARDS_N_BACKS; i++)
    g_free (cards->backs[i]);
  g_free (cards->blank);
  g_free (cards->edge);
  g_free (cards->slot);
  g_free (cards);
}

void
games_cards_size (GamesCards *cards, gint *width, gint *height)
{
  *width = cards->width;
  *height = cards->height;
}

gdouble
games_cards_scale (GamesCards *cards)
{
  return cards->scale;
}

void
games_cards_pixel_size (GamesCards *cards, gint *width, gint *height)
{
  *width = cards->pixel_width;
  *height = cards->pixel_height;
}

guchar *
games_cards_detail (GamesCards *cards, GamesSuit suit, gint rank, gint x,
                    gint y, gint width, gint height, gdouble pixels)
{
  gchar *path = games_installed_path ("cards", "deck.svg");
  GamesSvg *svg = games_svg_load (path);
  gint n = deck_row_of_suit[suit] * 13 + rank - 1;
  gdouble deck_w, deck_h, unit;
  guchar *image;

  g_free (path);
  if (!svg)
    return NULL;
  /* The deck's units to each of the game's pixels on a card. */
  games_svg_size (svg, &deck_w, &deck_h);
  unit = deck_w / DECK_COLUMNS / cards->width;
  image = games_svg_render_area (svg,
                                 (n % DECK_COLUMNS * cards->width + x) * unit,
                                 (n / DECK_COLUMNS * cards->height + y) * unit,
                                 pixels / unit,
                                 (gint) floor (width * pixels + 0.5),
                                 (gint) floor (height * pixels + 0.5));
  games_svg_free (svg);
  return image;
}

const guchar *
games_cards_face (GamesCards *cards, GamesSuit suit, gint rank)
{
  g_return_val_if_fail (suit <= GAMES_SPADES, NULL);
  g_return_val_if_fail (rank >= GAMES_ACE && rank <= GAMES_KING, NULL);
  return cards->faces[suit][rank - 1];
}

const guchar *
games_cards_back (GamesCards *cards, gint back)
{
  g_return_val_if_fail (back >= 0 && back < GAMES_CARDS_N_BACKS, NULL);
  if (!cards->backs[back])
    cards->backs[back] = make_back (cards, cards->blank, cards->edge, back);
  return cards->backs[back];
}

const guchar *
games_cards_slot (GamesCards *cards)
{
  return cards->slot;
}
