/*
 * cgfreecell, from GNOME Freecell
 * Copyright (C) 1998 the Free Software Foundation
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

/* FreeCell as Windows plays it: its deals, numbered as it numbers them,
 * its table, its menus and dialogs, and its way of moving the cards, a
 * click on the cards and a click where they go, a run moved a card at
 * a time through the free cells and empty columns. */

#include "config.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "games.h"
#include "games-cards.h"

#include "icons/cgfreecell.xpm"

#define APPNAME "cgfreecell"
#define APPNAME_LONG N_("FreeCell")

#define ELEMENTS(x) (sizeof (x) / sizeof (x [0]))

/* The cards, all face up: a card is its rank, 1 to 13, with its suit
 * above that; 0 is none. */

typedef guint8 Card;

#define CARD(suit, rank) ((Card) ((suit) << 4 | (rank)))
#define RANK(c) ((c) & 0x0f)
#define SUIT(c) (((c) >> 4) & 3)
#define RED(c) GAMES_SUIT_RED (SUIT (c))

/* The places: four free cells at the top left, each holding a card, the
 * four home cells at the top right, built up by suit from the aces, and
 * the eight columns, built down in alternate colours. */
enum
{
  CELL,
  HOME = CELL + 4,
  COLUMN = HOME + 4,
  N_PILES = COLUMN + 8
};

#define IS_CELL(p) ((p) < HOME)
#define IS_HOME(p) ((p) >= HOME && (p) < COLUMN)
#define IS_COLUMN(p) ((p) >= COLUMN)

typedef struct
{
  Card cards[52];
  gint n;
} Pile;

typedef struct
{
  Pile piles[N_PILES];
} State;

/* The deals are numbered as Windows numbers them, from 1 to a million. */
#define MAX_GAME 1000000
/* -1 and -2 are Windows' hidden games, not offered but taken. */
#define HIDDEN_GAME(n) ((n) == -1 || (n) == -2)

static State state;
/* The game before the last move, for Undo, which takes back only that. */
static State before;
static gboolean can_undo;
/* The deal's number, 0 before the first. */
static gint game_number;
/* Whether a move has been made in this deal, and whether it is over:
 * won, lost, or given up. */
static gboolean played, over;

static struct
{
  gboolean warn;                /* says when a move can't be made */
  gboolean quick;               /* moves the cards without showing them go */
  gboolean double_click;        /* a double click sends a card to a cell */
} opts;

/* Games won and lost, in all and since the program started; the longest
 * runs of each; and the run now, wins above 0 and losses below. */
static struct
{
  gint won, lost;
  gint session_won, session_lost;
  gint most_won, most_lost;
  gint streak;
} stats;

static GamesConfig *config;
static GamesCards *cards;
static gint card_width, card_height;

static GtkWidget *window;
static GtkWidget *table;
static GtkWidget *left_label;
static GtkWidget *undo_item;
static GtkWidget *restart_item;
static GtkWidget *options_dialog;
static GtkWidget *select_dialog;
static GtkWidget *stats_dialog;
static GtkWidget *over_dialog;

/* Layout. */

#define TABLE_COLOUR 0x008000
#define ROW_GAP 16
/* The cells are outlined in a card's shape, as Solitaire's empty places
 * are, a card covering the outline; they are this far apart, and from
 * the edges of the table. */
#define CELL_GAP 8
#define BOTTOM 8
#define MIN_GAP 4
#define UP_STEP 30              /* down a column, from card to card */
#define MIN_UP_STEP 15

/* The king between the cells: his head, from the king of spades,
 * HEAD_WIDTH by HEAD_HEIGHT of the card's pixels from (HEAD_X, HEAD_Y),
 * shown twice as large on the table in a cell of its own. */
#define HEAD_X 36
#define HEAD_Y 14
#define HEAD_WIDTH 40
#define HEAD_HEIGHT 40
#define HEAD_ZOOM 2
/* His place, a square: the outline, two pixels wide, round the head. */
#define KING_EDGE 2
#define KING_WIDTH (HEAD_WIDTH * HEAD_ZOOM + 2 * KING_EDGE)
#define KING_HEIGHT (HEAD_HEIGHT * HEAD_ZOOM + 2 * KING_EDGE)

/* The table drawn, as in cgmahjong: the frame is the drawing area's
 * width by height in device pixels, device of them to the toolkit's.
 * The table is laid out in the game's pixels, the cards' own, zoom of
 * the toolkit's and scale of the device's each, zoom what fits the
 * table at its usual size to the window; frame_width by frame_height of
 * them fill it. */
static guchar *frame;
/* While a card moves, the table under it. */
static guchar *under;
static gint table_width, table_height, device = 1;
static gint frame_width, frame_height;
static double zoom = 1, scale = 1;
static guint refit_timer;
#define DEVICE_WIDTH (table_width * device)
#define DEVICE_HEIGHT (table_height * device)

/* A place in the game's pixels, in the device's. */
#define DEV(v) ((gint) floor ((v) * scale + 0.5))

/* How long the window must keep a size before the cards are drawn again
 * for it, in milliseconds; till then the old ones are shown. */
#define REDRAW_DELAY 150

/* The least the table is drawn, of its usual size, and the window's
 * table then. */
#define LEAST_ZOOM 0.75
#define LEAST_WIDTH ((gint) ceil (roomy_width () * LEAST_ZOOM))
#define LEAST_HEIGHT ((gint) ceil (roomy_height () * LEAST_ZOOM))
static gint pile_x[N_PILES], pile_y[N_PILES];
static gint up_step[N_PILES];
static gint king_x, king_y;
/* The king's head, head_size device pixels square, as he is and
 * smiling; whether he looks right; and whether he smiles, as he does at
 * a win. */
static guchar *head, *head_smiling;
static gint head_size;
static gboolean king_right, king_smiles;

/* The place clicked on, whose cards go where the next click is; -1 if
 * none. */
static gint sel_pile = -1;

/* Where the pointer was last seen on the table, in the game's pixels;
 * and the place under it that the cards picked up can go to, shown
 * ringed, or -1. */
static gint pointer_x = -1, pointer_y = -1;
static gint hover_pile = -1;

/* The card the right button shows whole while held down; -1 if none. */
static gint peek_pile = -1, peek_index;

/* The last press of the left button, to tell a double click by. */
static struct
{
  guint32 time;
  gint x, y, pile;
} last_click;

#define DOUBLE_CLICK_MS 500
#define DOUBLE_CLICK_PX 4

/* The moves of single cards still to be made, each from a pile to
 * another, shown one by one; and the card on its way. */
#define MAX_MOVES 512
static gint moves[MAX_MOVES][2];
static gint n_moves, next_move;

static struct
{
  gboolean active;
  Card card;
  gint pile;                    /* where it goes, its top card */
  gint x0, y0, x1, y1;
  gint x, y;
  gint step, steps;
  guint timeout;
} fly;

#define FLY_MS 12               /* between the card's steps */
#define FLY_SPEED 30            /* how far it goes in a step */

static void new_game (gint number);
static void redraw (void);
static void select_game (void);
static void after_moves (void);
static gint target (void);

/* Drawing. */

/* Draws again the part of the window over a rectangle of the table. */
static void
queue_area (gint x, gint y, gint width, gint height)
{
  gint x0 = (gint) floor (x * zoom) - 1, y0 = (gint) floor (y * zoom) - 1;
  gint x1 = (gint) ceil ((x + width) * zoom) + 1;
  gint y1 = (gint) ceil ((y + height) * zoom) + 1;

  gtk_widget_queue_draw_area (table, x0, y0, x1 - x0, y1 - y0);
}

/* Lays a width by height RGBA image, in device pixels, over the frame at
 * (x, y) of the game's: its colours turned round if invert, and the
 * other way round if mirror. */
static void
blit_image (const guchar *src, gint width, gint height, gint x, gint y,
            gboolean invert, gboolean mirror)
{
  gint sw = width, w = sw, h = height, sx = 0, sy = 0, i, j;

  x = DEV (x);
  y = DEV (y);
  if (x < 0) { sx = -x; w += x; x = 0; }
  if (y < 0) { sy = -y; h += y; y = 0; }
  if (x + w > DEVICE_WIDTH)
    w = DEVICE_WIDTH - x;
  if (y + h > DEVICE_HEIGHT)
    h = DEVICE_HEIGHT - y;

  for (j = 0; j < h; j++)
    {
      guchar *d = frame + ((gsize) (y + j) * DEVICE_WIDTH + x) * 3;

      for (i = 0; i < w; i++, d += 3)
        {
          gint col = mirror ? sw - 1 - (sx + i) : sx + i;
          const guchar *s = src + ((gsize) (sy + j) * sw + col) * 4;
          guint a = s[3], r = s[0], g = s[1], b = s[2];

          if (a == 0)
            continue;
          if (invert)
            {
              r = 255 - r;
              g = 255 - g;
              b = 255 - b;
            }
          if (a == 255)
            {
              d[0] = (guchar) r;
              d[1] = (guchar) g;
              d[2] = (guchar) b;
            }
          else
            {
              guint na = 255 - a;

              d[0] = (guchar) ((r * a + d[0] * na + 127) / 255);
              d[1] = (guchar) ((g * a + d[1] * na + 127) / 255);
              d[2] = (guchar) ((b * a + d[2] * na + 127) / 255);
            }
        }
    }
}

static void
draw_card (Card card, gint x, gint y, gboolean invert)
{
  gint w, h;

  games_cards_pixel_size (cards, &w, &h);
  blit_image (games_cards_face (cards, SUIT (card), RANK (card)), w, h, x, y,
              invert, FALSE);
}

/* Fills a rectangle of the frame with a colour. */
static void
fill (gint x, gint y, gint width, gint height, guint32 colour)
{
  gint x0 = MAX (DEV (x), 0), y0 = MAX (DEV (y), 0);
  gint x1 = MIN (DEV (x + width), DEVICE_WIDTH);
  gint y1 = MIN (DEV (y + height), DEVICE_HEIGHT);
  gint i, j;

  for (j = y0; j < y1; j++)
    for (i = x0; i < x1; i++)
      {
        guchar *d = frame + ((gsize) j * DEVICE_WIDTH + i) * 3;

        d[0] = (guchar) (colour >> 16);
        d[1] = (guchar) (colour >> 8);
        d[2] = (guchar) colour;
      }
}

/* Darkens a rectangle of the table 40%, as the cells' outlines do. */
static void
darken (gint x, gint y, gint width, gint height)
{
  gint x0 = MAX (DEV (x), 0), y0 = MAX (DEV (y), 0);
  gint x1 = MIN (DEV (x + width), DEVICE_WIDTH);
  gint y1 = MIN (DEV (y + height), DEVICE_HEIGHT);
  gint i, j, k;

  for (j = y0; j < y1; j++)
    for (i = x0; i < x1; i++)
      for (k = 0; k < 3; k++)
        {
          guchar *d = frame + ((gsize) j * DEVICE_WIDTH + i) * 3 + k;

          *d = (guchar) (*d * 3 / 5);
        }
}

/* Whether (x, y) is inside the rectangle from (x0, y0) to (x1, y1) with
 * corners rounded to radius r. */
static gboolean
in_rounded (double x, double y, double x0, double y0, double x1, double y1,
            double r)
{
  double cx, cy;

  if (x < x0 || y < y0 || x >= x1 || y >= y1)
    return FALSE;
  cx = CLAMP (x, x0 + r, x1 - r);
  cy = CLAMP (y, y0 + r, y1 - r);
  return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r;
}

/* The ring round a place the cards picked up can go to: RING_WIDTH wide,
 * RING_GAP outside a card there, its corners rounded as a card's. */
#define RING_COLOUR 0xffe040
#define RING_GAP 1
#define RING_WIDTH 3
#define CARD_CORNER (MAX (card_width / 20, 2))

static void
draw_ring (gint x, gint y)
{
  double out = RING_GAP + RING_WIDTH;
  double x0 = x - out, y0 = y - out;
  double x1 = x + card_width + out, y1 = y + card_height + out;
  gint i0 = MAX ((gint) floor (x0 * scale), 0);
  gint j0 = MAX ((gint) floor (y0 * scale), 0);
  gint i1 = MIN ((gint) ceil (x1 * scale), DEVICE_WIDTH);
  gint j1 = MIN ((gint) ceil (y1 * scale), DEVICE_HEIGHT);
  gint i, j, a, b, k;

  for (j = j0; j < j1; j++)
    for (i = i0; i < i1; i++)
      {
        gint n = 0;
        guchar *d = frame + ((gsize) j * DEVICE_WIDTH + i) * 3;

        for (b = 0; b < 4; b++)
          for (a = 0; a < 4; a++)
            {
              double u = (i + (a + 0.5) / 4) / scale;
              double v = (j + (b + 0.5) / 4) / scale;

              n += in_rounded (u, v, x0, y0, x1, y1, CARD_CORNER + out)
                && !in_rounded (u, v, x0 + RING_WIDTH, y0 + RING_WIDTH,
                                x1 - RING_WIDTH, y1 - RING_WIDTH,
                                CARD_CORNER + RING_GAP);
            }
        for (k = 0; k < 3; k++)
          {
            guint c = (RING_COLOUR >> (16 - 8 * k)) & 0xff;

            d[k] = (guchar) ((c * n + d[k] * (16 - n) + 8) / 16);
          }
      }
}

/* The king's place: a square outline as dark as the cells'. */
static void
draw_outline (gint x, gint y, gint width, gint height)
{
  darken (x, y, width, KING_EDGE);
  darken (x, y + height - KING_EDGE, width, KING_EDGE);
  darken (x, y + KING_EDGE, KING_EDGE, height - 2 * KING_EDGE);
  darken (x + width - KING_EDGE, y + KING_EDGE, KING_EDGE,
          height - 2 * KING_EDGE);
}

/* The head's pixels, square. */
#define HEAD_PIXELS_WIDTH head_size
#define HEAD_PIXELS_HEIGHT head_size
/* Device pixels to each of the head's as shown, twice the card's. */
#define HEAD_UNIT ((double) head_size / (HEAD_WIDTH * HEAD_ZOOM))

/* Whether a pixel of the head is the card's white. */
static gboolean
is_white (const guchar *p)
{
  return p[0] >= 235 && p[1] >= 235 && p[2] >= 235;
}

/* How far down the head's sides the white around it is sought, in the
 * head's pixels as shown: below, the edges cut through his hair and his
 * robe, whose white is his own. */
#define WHITE_FROM_SIDES 20

/* Takes the card's white from around the head, so that the table shows
 * there: the white reached from the top and the tops of the sides
 * without crossing a line, and, at its edge, the white the lines are
 * smoothed into. */
static void
clear_white (guchar *pixels)
{
  gint w = HEAD_PIXELS_WIDTH, h = HEAD_PIXELS_HEIGHT, n = 0, i;
  gint *queue = g_new (gint, (gsize) w * h);
  guchar *out = g_malloc0 ((gsize) w * h);

  for (i = 0; i < w * (gint) (WHITE_FROM_SIDES * HEAD_UNIT); i++)
    if ((i / w == 0 || i % w == 0 || i % w == w - 1)
        && is_white (pixels + i * 4))
      {
        out[i] = 1;
        queue[n++] = i;
      }
  while (n > 0)
    {
      gint at = queue[--n], k;
      static const gint dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };

      for (k = 0; k < 4; k++)
        {
          gint x = at % w + dx[k], y = at / w + dy[k], j = y * w + x;

          if (x >= 0 && y >= 0 && x < w && y < h && !out[j]
              && is_white (pixels + j * 4))
            {
              out[j] = 1;
              queue[n++] = j;
            }
        }
    }

  for (i = 0; i < w * h; i++)
    {
      guchar *p = pixels + i * 4;
      gint x = i % w, y = i / w, k;

      if (out[i])
        {
          p[3] = 0;
          continue;
        }
      /* A pixel by the white: as much of it as isn't white, its colour
       * taken from what it would be without. */
      if ((x > 0 && out[i - 1]) || (x < w - 1 && out[i + 1])
          || (y > 0 && out[i - w]) || (y < h - 1 && out[i + w]))
        {
          guint a = 255 - MIN (MIN (p[0], p[1]), p[2]);

          if (a == 0)
            p[3] = 0;
          else
            {
              for (k = 0; k < 3; k++)
                p[k] = (guchar) CLAMP (((gint) p[k] - (gint) (255 - a)) * 255
                                       / (gint) a, 0, 255);
              p[3] = (guchar) (a * p[3] / 255);
            }
        }
    }
  g_free (out);
  g_free (queue);
}

/* Makes the head smile, bending his own lines: the mouth raised under
 * the moustache, more at the corners than in the middle, and a little
 * wider, and the ends of the moustache raised with it. A pixel is drawn
 * from where it was. The places are in the head's pixels as shown,
 * twice the card's; the bending fades out towards the edges of its
 * place, so that nothing else moves. */
#define BEND_LEFT 36.0
#define BEND_RIGHT 64.0
#define BEND_TOP 44.0
#define BEND_BOTTOM 58.0
#define FACE_MIDDLE 50.5
#define MOUTH_HALF 7.5          /* from the middle to a corner */
#define MOUSTACHE_HALF 11.0     /* from the middle to an end */
#define SMILE_RAISE 0.8         /* the whole mouth */
#define SMILE_LIFT 3.0          /* the corners, more */
#define SMILE_WIDEN 1.1
#define MOUSTACHE_LIFT 1.2      /* the ends */

/* 0 at a, 1 at b, and between. */
static double
ramp (double v, double a, double b)
{
  return CLAMP ((v - a) / (b - a), 0, 1);
}

/* The head is drawn this many times finer for smiling, then brought
 * down, so that the bent lines stay sharp. */
#define SMILE_FINE 4

/* Pixel (x, y) of a w by h RGBA image, between pixels as they blend. */
static void
sample (const guchar *src, gint w, gint h, double x, double y, guchar *out)
{
  gint x0, y0, k;
  double fx, fy;

  x = CLAMP (x, 0, w - 1.001);
  y = CLAMP (y, 0, h - 1.001);
  x0 = (gint) x;
  y0 = (gint) y;
  fx = x - x0;
  fy = y - y0;
  for (k = 0; k < 4; k++)
    {
      const guchar *p = src + ((gsize) y0 * w + x0) * 4 + k;
      double top = p[0] * (1 - fx) + p[4] * fx;
      double bottom = p[w * 4] * (1 - fx) + p[w * 4 + 4] * fx;

      out[k] = (guchar) (top * (1 - fy) + bottom * fy + 0.5);
    }
}

static void
smile (guchar *pixels, gint w, gint h, double fine)
{
  gsize size = (gsize) w * h * 4;
  guchar *src = g_memdup (pixels, (guint) size);
  double k = fine;              /* pixels to each of the head's */
  gint x, y;

  for (y = (gint) (BEND_TOP * k); y < (gint) (BEND_BOTTOM * k); y++)
    for (x = (gint) (BEND_LEFT * k); x < (gint) (BEND_RIGHT * k); x++)
      {
        double u = (x + 0.5) / k, v = (y + 0.5) / k;
        /* How much of the bending each place takes: the sides fading
         * out, the mouth's rows and the moustache's meeting between. */
        double sides = MIN (ramp (u, BEND_LEFT, BEND_LEFT + 3),
                            ramp (u, BEND_RIGHT, BEND_RIGHT - 3));
        double mouth = MIN (ramp (v, 50, 51.5), ramp (v, BEND_BOTTOM, 56.5));
        double moustache = MIN (ramp (v, BEND_TOP + 0.5, 46.5),
                                ramp (v, 51.5, 50));
        double t = CLAMP ((u - FACE_MIDDLE) / MOUTH_HALF, -1, 1);
        double tm = CLAMP ((u - FACE_MIDDLE) / MOUSTACHE_HALF, -1, 1);
        double up = (mouth * (SMILE_RAISE + SMILE_LIFT * t * t)
                     + moustache * MOUSTACHE_LIFT * tm * tm) * sides;
        double su = FACE_MIDDLE
          + (u - FACE_MIDDLE) / (1 + (SMILE_WIDEN - 1) * mouth * sides);

        sample (src, w, h, su * k - 0.5, (v + up) * k - 0.5,
                pixels + ((gsize) y * w + x) * 4);
      }
  g_free (src);
}

/* An image fine times as large brought down to its size, each pixel
 * the average of those it covers; NULL if there is none. */
static guchar *
shrink (const guchar *big, gint w, gint h, gint fine)
{
  guchar *out;
  gint x, y, i, j, k;

  if (!big)
    return NULL;
  out = g_malloc ((gsize) w * h * 4);
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      for (k = 0; k < 4; k++)
        {
          guint sum = 0;

          for (j = 0; j < fine; j++)
            for (i = 0; i < fine; i++)
              sum += big[(((gsize) y * fine + j) * w * fine + x * fine + i) * 4
                         + k];
          out[((gsize) y * w + x) * 4 + k]
            = (guchar) ((sum + fine * fine / 2) / (fine * fine));
        }
  return out;
}

static void
draw_king (void)
{
  /* His head is as wide as his place inside the edge. */
  gint size = DEV (king_x + KING_EDGE + HEAD_WIDTH * HEAD_ZOOM)
    - DEV (king_x + KING_EDGE);

  if (head_size != size)
    {
      guchar *big;

      g_free (head);
      g_free (head_smiling);
      head = head_smiling = NULL;
      head_size = size;
      big = games_cards_detail (cards, GAMES_SPADES, GAMES_KING, HEAD_X,
                                HEAD_Y, HEAD_WIDTH, HEAD_HEIGHT,
                                (double) size * SMILE_FINE / HEAD_WIDTH);
      /* Both drawn fine and brought down alike, so that they differ
       * only in the smile. */
      head = shrink (big, HEAD_PIXELS_WIDTH, HEAD_PIXELS_HEIGHT, SMILE_FINE);
      if (big)
        {
          smile (big, HEAD_PIXELS_WIDTH * SMILE_FINE,
                 HEAD_PIXELS_HEIGHT * SMILE_FINE, HEAD_UNIT * SMILE_FINE);
          head_smiling = shrink (big, HEAD_PIXELS_WIDTH, HEAD_PIXELS_HEIGHT,
                                 SMILE_FINE);
          g_free (big);
          clear_white (head);
          clear_white (head_smiling);
        }
    }
  fill (king_x, king_y, KING_WIDTH, KING_HEIGHT, TABLE_COLOUR);
  draw_outline (king_x, king_y, KING_WIDTH, KING_HEIGHT);
  /* He looks right as drawn, and left turned round. */
  if (head)
    blit_image (king_smiles ? head_smiling : head, head_size, head_size,
                king_x + KING_EDGE, king_y + KING_EDGE, FALSE, !king_right);
}

/* Where card i of pile p is drawn. */
static void
card_pos (gint p, gint i, gint *x, gint *y)
{
  *x = pile_x[p];
  *y = pile_y[p];
  if (IS_COLUMN (p))
    *y += i * up_step[p];
}

/* Closes up the columns that would run off the bottom. */
static void
fit_columns (void)
{
  gint p;

  for (p = COLUMN; p < N_PILES; p++)
    {
      gint room = frame_height - BOTTOM - pile_y[p] - card_height;
      gint n = state.piles[p].n - 1;

      up_step[p] = UP_STEP;
      if (n > 0 && n * UP_STEP > room)
        up_step[p] = MAX (MIN_UP_STEP, room / n);
    }
}

/* The free cells side by side in the top left corner, the home cells in
 * the top right, the king between them, and the columns spread across
 * the table below. */
static void
layout (void)
{
  gint gap = MAX (MIN_GAP, (frame_width - 8 * card_width) / 9);
  gint left = (frame_width - 8 * card_width - 7 * gap) / 2;
  gint p;

  for (p = 0; p < 4; p++)
    {
      pile_x[CELL + p] = CELL_GAP + p * (card_width + CELL_GAP);
      pile_y[CELL + p] = CELL_GAP;
      pile_x[HOME + p] = frame_width - (4 - p) * (card_width + CELL_GAP);
      pile_y[HOME + p] = CELL_GAP;
    }
  for (p = 0; p < 8; p++)
    {
      pile_x[COLUMN + p] = left + p * (card_width + gap);
      pile_y[COLUMN + p] = CELL_GAP + card_height + ROW_GAP;
    }
  king_x = (frame_width - KING_WIDTH) / 2;
  king_y = CELL_GAP + (card_height - KING_HEIGHT) / 2;
  fit_columns ();
}

static void
draw_pile (gint p)
{
  const Pile *pile = &state.piles[p];
  gint first = 0, last = pile->n, i, x, y;

  /* The card on its way is drawn where it is, not where it goes. */
  if (fly.active && p == fly.pile)
    last--;
  if (!IS_COLUMN (p))
    first = MAX (0, last - 1);
  for (i = first; i < last; i++)
    {
      card_pos (p, i, &x, &y);
      draw_card (pile->cards[i], x, y, p == sel_pile && i == pile->n - 1);
    }
}

static void
redraw (void)
{
  gsize i;
  gint p, w, h;

  if (!frame)
    return;
  for (i = 0; i < (gsize) DEVICE_WIDTH * DEVICE_HEIGHT; i++)
    {
      frame[i * 3] = (TABLE_COLOUR >> 16) & 0xff;
      frame[i * 3 + 1] = (TABLE_COLOUR >> 8) & 0xff;
      frame[i * 3 + 2] = TABLE_COLOUR & 0xff;
    }
  fit_columns ();
  games_cards_pixel_size (cards, &w, &h);
  for (p = CELL; p < COLUMN; p++)
    blit_image (games_cards_slot (cards), w, h, pile_x[p], pile_y[p], FALSE,
                FALSE);
  draw_king ();
  for (p = 0; p < N_PILES; p++)
    draw_pile (p);

  /* The place under the pointer that the cards picked up can go to,
   * ringed, round its top card or where one would lie. */
  hover_pile = target ();
  if (hover_pile >= 0)
    {
      gint x, y, n = state.piles[hover_pile].n;

      card_pos (hover_pile, IS_COLUMN (hover_pile) ? MAX (n - 1, 0) : 0,
                &x, &y);
      draw_ring (x, y);
    }

  /* The card the right button holds shows whole, over those below it. */
  if (peek_pile >= 0)
    {
      gint x, y;

      card_pos (peek_pile, peek_index, &x, &y);
      draw_card (state.piles[peek_pile].cards[peek_index], x, y, FALSE);
    }

  if (fly.active)
    {
      gsize size = (gsize) DEVICE_WIDTH * DEVICE_HEIGHT * 3;

      if (!under)
        under = g_malloc (size);
      memcpy (under, frame, size);
      draw_card (fly.card, fly.x, fly.y, FALSE);
    }

  gtk_widget_queue_draw (table);
}

/* Moves the card on its way to (x, y): only where it was and where it
 * goes is drawn again. */
static void
move_flying (gint x, gint y)
{
  gint j, w, h, x0, x1, y0, y1;

  games_cards_pixel_size (cards, &w, &h);
  x0 = MAX (DEV (fly.x), 0);
  x1 = MIN (DEV (fly.x) + w, DEVICE_WIDTH);
  y0 = MAX (DEV (fly.y), 0);
  y1 = MIN (DEV (fly.y) + h, DEVICE_HEIGHT);

  if (!under)
    {
      fly.x = x;
      fly.y = y;
      redraw ();
      return;
    }
  for (j = y0; j < y1 && x0 < x1; j++)
    {
      gsize offset = ((gsize) j * DEVICE_WIDTH + x0) * 3;

      memcpy (frame + offset, under + offset, (gsize) (x1 - x0) * 3);
    }
  queue_area (fly.x, fly.y, card_width, card_height);
  fly.x = x;
  fly.y = y;
  draw_card (fly.card, fly.x, fly.y, FALSE);
  queue_area (fly.x, fly.y, card_width, card_height);
}

/* The table at its usual size, in the game's pixels: the cells and the
 * king along the top with room either side of him, and room for a dozen
 * cards down a column. */
static gint
roomy_width (void)
{
  return 8 * (card_width + CELL_GAP) + KING_WIDTH + 2 * 12;
}

static gint
roomy_height (void)
{
  return CELL_GAP + 2 * card_height + ROW_GAP + 12 * UP_STEP
    + BOTTOM;
}

/* Lays the table out at the zoom that fits the window, with the cards
 * drawn again for it if their size has changed. */
static void
fit_cards (void)
{
  double z = MIN ((double) table_width / roomy_width (),
                  (double) table_height / roomy_height ());

  if (fabs (z * device - games_cards_scale (cards)) * card_width >= 0.5)
    {
      GamesCards *c = games_cards_new_scaled (z * device);

      /* If the cards can't be read now, the old ones do. */
      if (c)
        {
          games_cards_free (cards);
          cards = c;
        }
    }
  zoom = z;
  scale = z * device;
  frame_width = (gint) (table_width / zoom);
  frame_height = (gint) (table_height / zoom);
  layout ();
}

static gint
refit_timeout (gpointer data)
{
  (void) data;
  refit_timer = 0;
  fit_cards ();
  redraw ();
  return FALSE;
}

/* Brings the frame into line with the drawing area's size and the
 * window's scale, and the table with the frame: at once the first time
 * or for a new scale, otherwise once the window has kept its size a
 * moment, the cards spread as they are till then. */
static void
frame_resize (gint width, gint height)
{
  gint d = MAX (gdk_window_get_scale_factor (table->window), 1);
  gboolean at_once = frame == NULL || d != device;

  if (frame && width == table_width && height == table_height && d == device)
    return;
  table_width = width;
  table_height = height;
  device = d;
  g_free (frame);
  frame = g_malloc ((gsize) DEVICE_WIDTH * DEVICE_HEIGHT * 3);
  g_free (under);
  under = NULL;
  if (refit_timer)
    gtk_timeout_remove (refit_timer);
  refit_timer = 0;
  if (at_once)
    fit_cards ();
  else
    {
      scale = zoom * device;
      frame_width = (gint) (table_width / zoom);
      frame_height = (gint) (table_height / zoom);
      layout ();
      refit_timer = gtk_timeout_add (REDRAW_DELAY, refit_timeout, NULL);
    }
  redraw ();
}

static gint
table_configure (GtkWidget *widget, GdkEventConfigure *event, gpointer data)
{
  (void) widget;
  (void) data;

  frame_resize (event->width, event->height);
  return TRUE;
}

static gint
table_expose (GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
  gint x0, y0, x1, y1;

  (void) data;
  if (!frame)
    return TRUE;
  /* The window may have gone to a screen of another scale. */
  frame_resize (table_width, table_height);

  x0 = MAX (event->area.x, 0);
  y0 = MAX (event->area.y, 0);
  x1 = MIN (event->area.x + (gint) event->area.width, table_width);
  y1 = MIN (event->area.y + (gint) event->area.height, table_height);
  if (x1 <= x0 || y1 <= y0)
    return TRUE;

  gdk_draw_rgb_image_scaled (widget->window, widget->style->black_gc,
                             x0, y0, x1 - x0, y1 - y0, device,
                             GDK_RGB_DITHER_NONE,
                             frame + ((gsize) y0 * device * DEVICE_WIDTH
                                      + x0 * device) * 3,
                             DEVICE_WIDTH * 3);
  return TRUE;
}

/* The menu bar's count and the title. */

static void
show_left (void)
{
  gchar text[64];
  gint p, home = 0;

  for (p = HOME; p < COLUMN; p++)
    home += state.piles[p].n;
  if (game_number)
    g_snprintf (text, sizeof text, _("Cards Left: %d"), 52 - home);
  else
    text[0] = '\0';
  gtk_label_set_text (GTK_LABEL (left_label), text);
}

static void
show_title (void)
{
  gchar text[64];

  if (game_number)
    g_snprintf (text, sizeof text, _("FreeCell Game #%d"), game_number);
  else
    g_snprintf (text, sizeof text, "%s", _(APPNAME_LONG));
  gtk_window_set_title (GTK_WINDOW (window), text);
}

static gboolean
busy (void)
{
  return fly.active || next_move < n_moves;
}

static void
set_menus (void)
{
  gtk_widget_set_sensitive (undo_item, can_undo && !over);
  gtk_widget_set_sensitive (restart_item, game_number != 0);
}

/* The statistics. */

static void
save_stats (void)
{
  games_config_set_int (config, "won", stats.won);
  games_config_set_int (config, "lost", stats.lost);
  games_config_set_int (config, "most_won", stats.most_won);
  games_config_set_int (config, "most_lost", stats.most_lost);
  games_config_set_int (config, "streak", stats.streak);
  games_config_save (config);
}

static void
load_stats (void)
{
  stats.won = MAX (0, games_config_get_int (config, "won", 0));
  stats.lost = MAX (0, games_config_get_int (config, "lost", 0));
  stats.most_won = MAX (0, games_config_get_int (config, "most_won", 0));
  stats.most_lost = MAX (0, games_config_get_int (config, "most_lost", 0));
  stats.streak = games_config_get_int (config, "streak", 0);
}

static void update_stats_dialog (void);

static void
count_game (gboolean win)
{
  over = TRUE;
  if (win)
    {
      stats.won++;
      stats.session_won++;
      stats.streak = stats.streak > 0 ? stats.streak + 1 : 1;
      stats.most_won = MAX (stats.most_won, stats.streak);
    }
  else
    {
      stats.lost++;
      stats.session_lost++;
      stats.streak = stats.streak < 0 ? stats.streak - 1 : -1;
      stats.most_lost = MAX (stats.most_lost, -stats.streak);
    }
  save_stats ();
  update_stats_dialog ();
  set_menus ();
}

/* Whether a game is being played that leaving would lose. */
static gboolean
in_play (void)
{
  return game_number && played && !over;
}

/* The rules. */

static Card
top (gint p)
{
  const Pile *pile = &state.piles[p];

  return pile->n ? pile->cards[pile->n - 1] : 0;
}

/* Whether card a can go on card b in a column. */
static gboolean
goes_on (Card a, Card b)
{
  return RED (a) != RED (b) && RANK (a) + 1 == RANK (b);
}

/* The first card of the run down in alternate colours at the top of
 * pile p, which can be moved together. */
static gint
run_start (gint p)
{
  const Pile *pile = &state.piles[p];
  gint i = pile->n - 1;

  if (IS_COLUMN (p))
    while (i > 0 && goes_on (pile->cards[i], pile->cards[i - 1]))
      i--;
  return i;
}

/* The most cards that can be moved together, a card at a time through
 * the free cells and the empty columns: twice as many for each empty
 * column, one fewer of those if the cards go to one. */
static gint
most_movable (gboolean to_empty)
{
  gint cells = 0, empty = 0, p;

  for (p = CELL; p < HOME; p++)
    cells += !state.piles[p].n;
  for (p = COLUMN; p < N_PILES; p++)
    empty += !state.piles[p].n;
  if (to_empty)
    empty--;
  return (cells + 1) << MAX (empty, 0);
}

/* Whether the top card of pile from can go on pile to by itself. */
static gboolean
card_goes (gint from, gint to)
{
  Card card = top (from), dest = top (to);

  if (!card || from == to || IS_HOME (from))
    return FALSE;
  if (IS_CELL (to))
    return !dest;
  if (IS_HOME (to))
    return dest ? SUIT (dest) == SUIT (card) && RANK (dest) + 1 == RANK (card)
      : RANK (card) == GAMES_ACE;
  return !dest || goes_on (card, dest);
}

/* Whether a card must go home now: twos and aces always, a higher card
 * once the cards of the other colour a rank below it are home, so that
 * nothing could want to go on it. */
static gboolean
safe_home (Card card)
{
  gint home[4] = { 0, 0, 0, 0 }, p, s;

  if (RANK (card) <= 2)
    return TRUE;
  for (p = HOME; p < COLUMN; p++)
    if (state.piles[p].n)
      home[SUIT (top (p))] = RANK (top (p));
  for (s = 0; s < 4; s++)
    if (GAMES_SUIT_RED (s) != RED (card) && home[s] < RANK (card) - 1)
      return FALSE;
  return TRUE;
}

/* Puts the top card of pile from onto pile to. */
static void
shift (State *st, gint from, gint to)
{
  Pile *src = &st->piles[from], *dest = &st->piles[to];

  dest->cards[dest->n++] = src->cards[--src->n];
}

/* A move of a card that must go home, set to be made; FALSE if there's
 * none. */
static gboolean
queue_home (void)
{
  gint p, h;

  for (p = CELL; p < N_PILES; p++)
    {
      if (IS_HOME (p) || !state.piles[p].n || !safe_home (top (p)))
        continue;
      for (h = HOME; h < COLUMN; h++)
        if (card_goes (p, h))
          {
            n_moves = next_move = 0;
            moves[n_moves][0] = p;
            moves[n_moves++][1] = h;
            return TRUE;
          }
    }
  return FALSE;
}

/* Whether any card can be moved at all. */
static gboolean
can_play (void)
{
  gint from, to;

  for (from = CELL; from < N_PILES; from++)
    for (to = CELL; to < N_PILES; to++)
      if (card_goes (from, to)
          /* A card moved from a free cell to another, or a column's
           * only card to an empty column, changes nothing. */
          && !(IS_CELL (from) && IS_CELL (to))
          && !(IS_COLUMN (from) && IS_COLUMN (to) && !state.piles[to].n
               && state.piles[from].n == 1))
        return TRUE;
  return FALSE;
}

/* Whether what a click on pile from picked up can go to pile to, as
 * move_to would move it, without asking. */
static gboolean
can_go (gint from, gint to)
{
  const Pile *src = &state.piles[from];
  gint i;

  if (from == to)
    return FALSE;
  if (IS_CELL (to) || IS_HOME (to) || !IS_COLUMN (from))
    return card_goes (from, to);
  if (!state.piles[to].n)
    return src->n > 0;
  for (i = run_start (from); i < src->n; i++)
    if (goes_on (src->cards[i], top (to)))
      return src->n - i <= most_movable (FALSE);
  return FALSE;
}

/* Planning a run's move: the single moves that make it, worked out on
 * a copy of the game. */

static State plan;

static void
plan_card (gint from, gint to)
{
  if (n_moves < MAX_MOVES)
    {
      moves[n_moves][0] = from;
      moves[n_moves++][1] = to;
    }
  shift (&plan, from, to);
}

/* Moves the top n cards of pile src to pile dst, using the free cells
 * and the n_cols empty columns in cols: n-1 cards into the cells and
 * back if there are cells enough, or else half of them into an empty
 * column, the rest across, and the half after them. */
static void
plan_run (gint n, gint src, gint dst, const gint *cols, gint n_cols)
{
  gint cells = 0, used[4], p, k, half;

  for (p = CELL; p < HOME; p++)
    cells += !plan.piles[p].n;
  if (n <= cells + 1 || n_cols == 0)
    {
      for (k = 0, p = CELL; k < n - 1 && p < HOME; p++)
        if (!plan.piles[p].n)
          {
            used[k++] = p;
            plan_card (src, p);
          }
      plan_card (src, dst);
      while (k-- > 0)
        plan_card (used[k], dst);
      return;
    }
  half = (cells + 1) << (n_cols - 1);
  if (n <= half)
    {
      plan_run (n, src, dst, cols + 1, n_cols - 1);
      return;
    }
  k = n - half;
  plan_run (k, src, cols[0], cols + 1, n_cols - 1);
  plan_run (n - k, src, dst, cols + 1, n_cols - 1);
  plan_run (k, cols[0], dst, cols + 1, n_cols - 1);
}

/* Moving the cards. */

static gint
fly_step (gpointer data)
{
  (void) data;
  fly.step++;
  if (fly.step >= fly.steps)
    {
      fly.timeout = 0;
      fly.active = FALSE;
      after_moves ();
      return FALSE;
    }
  move_flying (fly.x0 + (fly.x1 - fly.x0) * fly.step / fly.steps,
               fly.y0 + (fly.y1 - fly.y0) * fly.step / fly.steps);
  return TRUE;
}

/* Makes the next move set, showing the card go unless the moves are to
 * be quick. */
static void
make_move (void)
{
  gint from = moves[next_move][0], to = moves[next_move][1];
  gint dx, dy, distance;

  next_move++;
  /* The king looks at the cells a card goes to. */
  if (IS_CELL (to))
    king_right = FALSE;
  else if (IS_HOME (to))
    king_right = TRUE;
  card_pos (from, state.piles[from].n - 1, &fly.x0, &fly.y0);
  fly.card = top (from);
  shift (&state, from, to);
  if (opts.quick || !frame)
    return;
  card_pos (to, state.piles[to].n - 1, &fly.x1, &fly.y1);
  dx = fly.x1 - fly.x0;
  dy = fly.y1 - fly.y0;
  distance = ABS (dx) > ABS (dy) ? ABS (dx) + ABS (dy) / 2
    : ABS (dy) + ABS (dx) / 2;
  fly.pile = to;
  fly.x = fly.x0;
  fly.y = fly.y0;
  fly.step = 0;
  fly.steps = MAX (2, distance / FLY_SPEED);
  fly.active = TRUE;
  redraw ();
  fly.timeout = gtk_timeout_add (FLY_MS, fly_step, NULL);
}

static void game_over (gboolean win);

/* Goes on with the moves set, then sends home the cards that must go,
 * then sees whether the game is won or lost. */
static void
after_moves (void)
{
  while (next_move < n_moves || queue_home ())
    {
      make_move ();
      if (fly.active)
        return;
    }
  n_moves = next_move = 0;
  show_left ();
  redraw ();
  set_menus ();
  if (!game_number || over)
    return;
  if (state.piles[HOME].n + state.piles[HOME + 1].n + state.piles[HOME + 2].n
      + state.piles[HOME + 3].n == 52)
    game_over (TRUE);
  else if (!can_play ())
    game_over (FALSE);
}

/* Makes at once the moves still to be made, as when a new game is
 * dealt before they have all been shown. */
static void
finish_moves (void)
{
  if (fly.timeout)
    gtk_timeout_remove (fly.timeout);
  fly.timeout = 0;
  fly.active = FALSE;
  while (next_move < n_moves || queue_home ())
    {
      next_move++;
      shift (&state, moves[next_move - 1][0], moves[next_move - 1][1]);
    }
  n_moves = next_move = 0;
}

/* Moves the top n cards of pile from to pile to, a card at a time. */
static void
move_cards (gint from, gint n, gint to)
{
  gint cols[8], n_cols = 0, p;

  before = state;
  can_undo = TRUE;
  played = TRUE;
  sel_pile = -1;
  plan = state;
  n_moves = next_move = 0;
  for (p = COLUMN; p < N_PILES; p++)
    if (p != to && !state.piles[p].n)
      cols[n_cols++] = p;
  plan_run (n, from, to, cols, n_cols);
  after_moves ();
}

static void
not_allowed (const gchar *text)
{
  redraw ();
  if (opts.warn)
    games_message (GTK_WINDOW (window), _("FreeCell"),
                   text ? text : _("That move is not allowed."));
}

/* Moving a run into an empty column: the whole of what can go, or a
 * card. */

static gint empty_from, empty_to, empty_n;

static void
empty_answer (gint answer, gpointer data)
{
  (void) data;
  if (answer == 0)
    move_cards (empty_from, empty_n, empty_to);
  else if (answer == 1)
    move_cards (empty_from, 1, empty_to);
  else
    redraw ();
}

/* Moves what can go from pile from to pile to, or says why not. */
static void
move_to (gint from, gint to)
{
  const Pile *src = &state.piles[from];
  gint start = run_start (from), i;

  if (IS_CELL (to) || IS_HOME (to) || !IS_COLUMN (from))
    {
      if (card_goes (from, to))
        move_cards (from, 1, to);
      else
        not_allowed (NULL);
      return;
    }

  if (!state.piles[to].n)
    {
      gint n = MIN (src->n - start, most_movable (TRUE));

      if (n > 1)
        {
          static const gchar *const buttons[] = {
            N_("Move column"), N_("Move single card"), NULL
          };

          empty_from = from;
          empty_to = to;
          empty_n = n;
          games_choose (GTK_WINDOW (window), _("Move to Empty Column"),
                        _("Move the column, or a single card?"), buttons,
                        empty_answer, NULL);
        }
      else
        move_cards (from, 1, to);
      return;
    }

  /* The card of the run that goes on the column's. */
  for (i = start; i < src->n; i++)
    if (goes_on (src->cards[i], top (to)))
      {
        gint n = src->n - i, most = most_movable (FALSE);

        if (n > most)
          {
            gchar text[160];

            g_snprintf (text, sizeof text,
                        _("That move requires moving %d cards. You only have "
                          "enough free space to move %d cards."), n, most);
            not_allowed (text);
          }
        else
          move_cards (from, n, to);
        return;
      }
  not_allowed (NULL);
}

/* Sends the top card of column p to the first empty free cell. */
static void
to_cell (gint p)
{
  gint c;

  for (c = CELL; c < HOME; c++)
    if (!state.piles[c].n)
      {
        move_cards (p, 1, c);
        return;
      }
  redraw ();
}

static void
undo (void)
{
  if (!can_undo || over)
    return;
  finish_moves ();
  state = before;
  can_undo = FALSE;
  sel_pile = -1;
  peek_pile = -1;
  show_left ();
  set_menus ();
  redraw ();
}

/*
 * Windows' two hidden games, which can't be won: in -1 the aces and the
 * low odd cards lie under kings, and in -2 every column runs from its
 * ace or seven upwards, so nothing can go home. Column c holds suit
 * c % 4, the bottom card first.
 */
static const gint hidden_ranks[2][8][7] = {
  {
    { 1, 3, 5, 7, 9, 11, 13 }, { 1, 3, 5, 7, 9, 11, 13 },
    { 1, 3, 5, 7, 9, 11, 13 }, { 1, 3, 5, 7, 9, 11, 13 },
    { 12, 10, 8, 6, 4, 2 }, { 12, 10, 8, 6, 4, 2 },
    { 12, 10, 8, 6, 4, 2 }, { 12, 10, 8, 6, 4, 2 }
  },
  {
    { 1, 13, 12, 11, 10, 9, 8 }, { 1, 13, 12, 11, 10, 9, 8 },
    { 1, 13, 12, 11, 10, 9, 8 }, { 1, 13, 12, 11, 10, 9, 8 },
    { 7, 6, 5, 4, 3, 2 }, { 7, 6, 5, 4, 3, 2 },
    { 7, 6, 5, 4, 3, 2 }, { 7, 6, 5, 4, 3, 2 }
  }
};

static void
deal_hidden (gint which)
{
  gint c, i;

  for (c = 0; c < 8; c++)
    {
      Pile *column = &state.piles[COLUMN + c];

      for (i = 0; i < 7 && hidden_ranks[which][c][i]; i++)
        column->cards[column->n++] = CARD (c % 4, hidden_ranks[which][c][i]);
    }
}

/* Deals game number, as Windows does from its number. */
static void
deal (gint number)
{
  Card deck[52];
  guint32 seed = (guint32) number;
  gint left, i;

  memset (&state, 0, sizeof state);
  if (number < 0)
    {
      deal_hidden (-number - 1);
      return;
    }
  for (i = 0; i < 52; i++)
    deck[i] = CARD (i % 4, i / 4 + 1);
  for (i = 0, left = 52; i < 52; i++, left--)
    {
      gint r;
      Pile *column = &state.piles[COLUMN + i % 8];

      seed = (seed * 214013 + 2531011) & 0x7fffffff;
      r = (gint) ((seed >> 16) % (guint32) left);
      column->cards[column->n++] = deck[r];
      deck[r] = deck[left - 1];
    }
}

/* Deals game number, or one at random if it is 0. */
static void
new_game (gint number)
{
  finish_moves ();
  if (in_play ())
    count_game (FALSE);
  if (over_dialog)
    gtk_widget_destroy (over_dialog);
  if ((number < 1 || number > MAX_GAME) && !HIDDEN_GAME (number))
    number = games_rand_int (MAX_GAME) + 1;
  game_number = number;
  king_smiles = FALSE;
  deal (number);
  can_undo = FALSE;
  played = over = FALSE;
  sel_pile = -1;
  peek_pile = -1;
  set_menus ();
  show_left ();
  show_title ();
  redraw ();
}

/* Giving up a game in play: Windows asks first. */

static gint resign_number;

static void
resign_answer (gboolean yes, gpointer data)
{
  (void) data;
  if (yes)
    new_game (resign_number);
}

/* Deals game number, 0 for one at random, once the game in play is
 * given up. */
static void
leave_for (gint number)
{
  if (busy ())
    {
      finish_moves ();
      after_moves ();
    }
  if (!in_play ())
    {
      new_game (number);
      return;
    }
  resign_number = number;
  games_question (GTK_WINDOW (window), _("FreeCell"),
                  _("Do you want to resign this game?"), _("Yes"), _("No"),
                  resign_answer, NULL);
}

/* The end of a game: won, or with no move left. */

static GtkWidget *over_check;
static gboolean over_won;

static void
over_yes (GtkWidget *widget, gpointer data)
{
  gboolean checked
    = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (over_check));
  gboolean won = over_won;

  (void) widget;
  (void) data;
  gtk_widget_destroy (over_dialog);
  if (won && checked)
    select_game ();
  else
    new_game (!won && checked ? game_number : 0);
}

static void
game_over (gboolean win)
{
  GtkWidget *vbox, *label, *button;

  count_game (win);
  over_won = win;
  /* The king smiles at a win. */
  if (win)
    {
      king_smiles = TRUE;
      redraw ();
    }
  if (over_dialog)
    gtk_widget_destroy (over_dialog);
  over_dialog = games_dialog_new (GTK_WINDOW (window), _("Game Over"));
  gtk_signal_connect (GTK_OBJECT (over_dialog), "destroy",
                      GTK_SIGNAL_FUNC (gtk_widget_destroyed), &over_dialog);

  vbox = gtk_vbox_new (FALSE, 8);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 12);
  label = gtk_label_new (win ? _("Congratulations, you win!")
                         : _("Sorry, you lose."));
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
  gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);
  label = gtk_label_new (_("Do you want to play again?"));
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
  gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);
  over_check = gtk_check_button_new_with_label (win ? _("Select game")
                                                : _("Same game"));
  gtk_box_pack_start (GTK_BOX (vbox), over_check, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (over_dialog)->vbox), vbox,
                      TRUE, TRUE, 0);

  button = games_dialog_add_button (over_dialog, _("Yes"));
  gtk_signal_connect (GTK_OBJECT (button), "clicked",
                      GTK_SIGNAL_FUNC (over_yes), NULL);
  button = games_dialog_add_button (over_dialog, _("No"));
  gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
                             GTK_SIGNAL_FUNC (gtk_widget_destroy),
                             GTK_OBJECT (over_dialog));
  gtk_widget_show_all (over_dialog);
}

/* The mouse. */

/* The pile under (x, y), and the card in it, or -1 for an empty place;
 * FALSE if there's none. */
static gboolean
hit (gint x, gint y, gint *pile, gint *index)
{
  gint p, i, cx, cy;

  for (p = 0; p < N_PILES; p++)
    {
      const Pile *pl = &state.piles[p];

      for (i = pl->n - 1; i >= 0; i--)
        {
          card_pos (p, i, &cx, &cy);
          if (x >= cx && y >= cy && x < cx + card_width
              && y < cy + card_height)
            {
              *pile = p;
              *index = i;
              return TRUE;
            }
          if (!IS_COLUMN (p))
            break;
        }
      if (x >= pile_x[p] && y >= pile_y[p] && x < pile_x[p] + card_width
          && y < pile_y[p] + card_height)
        {
          *pile = p;
          *index = pl->n ? pl->n - 1 : -1;
          return TRUE;
        }
    }
  return FALSE;
}

/* The place under the pointer that the cards picked up can go to, or
 * -1: shown, as Windows shows it with the pointer, by a ring. */
static gint
target (void)
{
  gint p, i;

  if (sel_pile < 0 || busy () || over || peek_pile >= 0
      || !hit (pointer_x, pointer_y, &p, &i) || !can_go (sel_pile, p))
    return -1;
  return p;
}

static gint
table_press (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
  gint x = (gint) (event->x / zoom), y = (gint) (event->y / zoom), p, i;
  gboolean twice;

  (void) widget;
  (void) data;
  pointer_x = x;
  pointer_y = y;
  if (event->type != GDK_BUTTON_PRESS || !game_number || over || busy ()
      || peek_pile >= 0)
    return TRUE;

  /* The right button shows a card the ones below it hide. */
  if (event->button == 3)
    {
      if (hit (x, y, &p, &i) && IS_COLUMN (p) && i >= 0
          && i < state.piles[p].n - 1)
        {
          peek_pile = p;
          peek_index = i;
          redraw ();
        }
      return TRUE;
    }
  if (event->button != 1)
    return TRUE;

  if (!hit (x, y, &p, &i))
    {
      last_click.time = 0;
      return TRUE;
    }
  twice = last_click.time && p == last_click.pile
    && event->time - last_click.time <= DOUBLE_CLICK_MS
    && ABS (x - last_click.x) <= DOUBLE_CLICK_PX
    && ABS (y - last_click.y) <= DOUBLE_CLICK_PX;
  last_click.time = 0;

  /* A double click on a column sends its card to a free cell. */
  if (twice && opts.double_click && IS_COLUMN (p) && i >= 0)
    {
      sel_pile = -1;
      to_cell (p);
      return TRUE;
    }

  /* A place clicked on before gives what it can to this one; a second
   * click on it lets it go. */
  if (sel_pile >= 0)
    {
      gint from = sel_pile;

      sel_pile = -1;
      if (from == p)
        redraw ();
      else
        move_to (from, p);
      return TRUE;
    }

  if (i < 0 || IS_HOME (p))
    return TRUE;
  sel_pile = p;
  last_click.time = event->time ? event->time : 1;
  last_click.x = x;
  last_click.y = y;
  last_click.pile = p;
  redraw ();
  return TRUE;
}

static gint
table_release (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
  (void) widget;
  (void) data;
  if (event->button == 3 && peek_pile >= 0)
    {
      peek_pile = -1;
      redraw ();
    }
  return TRUE;
}

/* The king looks towards the pointer while it is level with him, along
 * the cells, and stays looking that way when it goes elsewhere. */
static gint
table_motion (GtkWidget *widget, GdkEventMotion *event, gpointer data)
{
  gint x = (gint) (event->x / zoom), y = (gint) (event->y / zoom);
  gboolean right = x >= king_x + KING_WIDTH / 2;

  (void) widget;
  (void) data;
  pointer_x = x;
  pointer_y = y;
  if (frame && target () != hover_pile)
    redraw ();
  if (y < CELL_GAP || y >= CELL_GAP + card_height)
    return TRUE;
  if (right != king_right && frame && !fly.active)
    {
      king_right = right;
      draw_king ();
      queue_area (king_x, king_y, KING_WIDTH, KING_HEIGHT);
    }
  return TRUE;
}

static gint
table_leave (GtkWidget *widget, GdkEventCrossing *event, gpointer data)
{
  (void) widget;
  (void) event;
  (void) data;
  pointer_x = pointer_y = -1;
  if (hover_pile >= 0)
    redraw ();
  return TRUE;
}

static gint
key_press (GtkWidget *widget, GdkEventKey *event, gpointer data)
{
  (void) widget;
  (void) data;
  if (event->keyval == GDK_Escape && sel_pile >= 0)
    {
      sel_pile = -1;
      redraw ();
      return TRUE;
    }
  return FALSE;
}

/* Settings. */

static void
save_options (void)
{
  games_config_set_int (config, "warn", opts.warn);
  games_config_set_int (config, "quick", opts.quick);
  games_config_set_int (config, "double_click", opts.double_click);
  games_config_save (config);
}

static void
load_options (void)
{
  opts.warn = games_config_get_int (config, "warn", 1) != 0;
  opts.quick = games_config_get_int (config, "quick", 0) != 0;
  opts.double_click = games_config_get_int (config, "double_click", 1) != 0;
}

/* The options dialog. */

static struct
{
  GtkWidget *warn, *quick, *double_click;
} od;

static gboolean
active (GtkWidget *toggle)
{
  return gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (toggle));
}

static void
options_ok (GtkWidget *widget, gpointer data)
{
  (void) widget;
  (void) data;
  opts.warn = active (od.warn);
  opts.quick = active (od.quick);
  opts.double_click = active (od.double_click);
  save_options ();
  gtk_widget_destroy (options_dialog);
}

static GtkWidget *
check (GtkWidget *box, const gchar *label, gboolean value)
{
  GtkWidget *button = gtk_check_button_new_with_label (label);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), value);
  gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);
  return button;
}

static void
options (void)
{
  GtkWidget *vbox, *button;

  if (options_dialog)
    {
      gdk_window_raise (options_dialog->window);
      return;
    }

  options_dialog = games_dialog_new (GTK_WINDOW (window), _("Options"));
  gtk_signal_connect (GTK_OBJECT (options_dialog), "destroy",
                      GTK_SIGNAL_FUNC (gtk_widget_destroyed), &options_dialog);

  vbox = gtk_vbox_new (FALSE, 2);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 8);
  od.warn = check (vbox, _("Display messages on illegal moves"), opts.warn);
  od.quick = check (vbox, _("Quick play (no animation)"), opts.quick);
  od.double_click = check (vbox, _("Double-click moves card to free cell"),
                           opts.double_click);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (options_dialog)->vbox), vbox,
                      TRUE, TRUE, 0);

  button = games_dialog_add_button (options_dialog, _("OK"));
  gtk_signal_connect (GTK_OBJECT (button), "clicked",
                      GTK_SIGNAL_FUNC (options_ok), NULL);
  button = games_dialog_add_button (options_dialog, _("Cancel"));
  gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
                             GTK_SIGNAL_FUNC (gtk_widget_destroy),
                             GTK_OBJECT (options_dialog));

  gtk_widget_show_all (options_dialog);
}

/* Choosing a game by its number. */

static GtkWidget *select_entry;

static void
select_ok (GtkWidget *widget, gpointer data)
{
  const gchar *text = gtk_entry_get_text (GTK_ENTRY (select_entry));
  gchar *end;
  long number = strtol (text, &end, 10);

  (void) widget;
  (void) data;
  while (*end == ' ')
    end++;
  if (end == text || *end
      || ((number < 1 || number > MAX_GAME) && !HIDDEN_GAME (number)))
    {
      gchar message[64];

      g_snprintf (message, sizeof message,
                  _("Select a game number from 1 to %d."), MAX_GAME);
      games_message (GTK_WINDOW (select_dialog), _("Game Number"), message);
      return;
    }
  gtk_widget_destroy (select_dialog);
  leave_for ((gint) number);
}

static void
select_game (void)
{
  GtkWidget *vbox, *label, *button;
  gchar text[64];

  if (select_dialog)
    {
      gdk_window_raise (select_dialog->window);
      return;
    }

  select_dialog = games_dialog_new (GTK_WINDOW (window), _("Game Number"));
  gtk_signal_connect (GTK_OBJECT (select_dialog), "destroy",
                      GTK_SIGNAL_FUNC (gtk_widget_destroyed), &select_dialog);

  vbox = gtk_vbox_new (FALSE, 8);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 8);
  g_snprintf (text, sizeof text, _("Select a game number from 1 to %d:"),
              MAX_GAME);
  label = gtk_label_new (text);
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
  gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);
  select_entry = gtk_entry_new ();
  g_snprintf (text, sizeof text, "%d", games_rand_int (MAX_GAME) + 1);
  gtk_entry_set_text (GTK_ENTRY (select_entry), text);
  gtk_entry_select_region (GTK_ENTRY (select_entry), 0, -1);
  gtk_signal_connect (GTK_OBJECT (select_entry), "activate",
                      GTK_SIGNAL_FUNC (select_ok), NULL);
  gtk_box_pack_start (GTK_BOX (vbox), select_entry, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (select_dialog)->vbox), vbox,
                      TRUE, TRUE, 0);

  button = games_dialog_add_button (select_dialog, _("OK"));
  gtk_signal_connect (GTK_OBJECT (button), "clicked",
                      GTK_SIGNAL_FUNC (select_ok), NULL);
  button = games_dialog_add_button (select_dialog, _("Cancel"));
  gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
                             GTK_SIGNAL_FUNC (gtk_widget_destroy),
                             GTK_OBJECT (select_dialog));

  gtk_widget_show_all (select_dialog);
  gtk_widget_grab_focus (select_entry);
}

/* The statistics dialog: a heading and its figures to each row. */

enum
{
  ST_SESSION, ST_SESSION_WON, ST_SESSION_LOST,
  ST_TOTAL, ST_TOTAL_WON, ST_TOTAL_LOST,
  ST_STREAKS, ST_WINS, ST_LOSSES, ST_CURRENT,
  N_ST
};

static GtkWidget *stats_labels[N_ST];

static gint
percent (gint won, gint lost)
{
  return won + lost ? won * 100 / (won + lost) : 0;
}

static void
update_stats_dialog (void)
{
  gchar text[N_ST][32];
  gint i;

  if (!stats_dialog)
    return;
  g_snprintf (text[ST_SESSION], 32, "%d%%",
              percent (stats.session_won, stats.session_lost));
  g_snprintf (text[ST_SESSION_WON], 32, "%d", stats.session_won);
  g_snprintf (text[ST_SESSION_LOST], 32, "%d", stats.session_lost);
  g_snprintf (text[ST_TOTAL], 32, "%d%%", percent (stats.won, stats.lost));
  g_snprintf (text[ST_TOTAL_WON], 32, "%d", stats.won);
  g_snprintf (text[ST_TOTAL_LOST], 32, "%d", stats.lost);
  text[ST_STREAKS][0] = '\0';
  g_snprintf (text[ST_WINS], 32, "%d", stats.most_won);
  g_snprintf (text[ST_LOSSES], 32, "%d", stats.most_lost);
  if (stats.streak >= 0)
    g_snprintf (text[ST_CURRENT], 32, ngettext ("%d win", "%d wins",
                                                    stats.streak), stats.streak);
  else
    g_snprintf (text[ST_CURRENT], 32, ngettext ("%d loss", "%d losses",
                                                    -stats.streak),
                -stats.streak);
  for (i = 0; i < N_ST; i++)
    gtk_label_set_text (GTK_LABEL (stats_labels[i]), text[i]);
}

static void
stats_clear (GtkWidget *widget, gpointer data)
{
  (void) widget;
  (void) data;
  memset (&stats, 0, sizeof stats);
  save_stats ();
  update_stats_dialog ();
}

static void
statistics (void)
{
  static const gchar *const headings[N_ST] = {
    N_("This session:"), N_("won:"), N_("lost:"),
    N_("Total:"), N_("won:"), N_("lost:"),
    N_("Streaks:"), N_("wins:"), N_("losses:"), N_("current:")
  };
  GtkWidget *grid, *button;
  gint i;

  if (stats_dialog)
    {
      gdk_window_raise (stats_dialog->window);
      return;
    }

  stats_dialog = games_dialog_new (GTK_WINDOW (window),
                                   _("FreeCell Statistics"));
  gtk_signal_connect (GTK_OBJECT (stats_dialog), "destroy",
                      GTK_SIGNAL_FUNC (gtk_widget_destroyed), &stats_dialog);

  grid = gtk_table_new (N_ST, 2, FALSE);
  gtk_container_set_border_width (GTK_CONTAINER (grid), 8);
  gtk_table_set_col_spacings (GTK_TABLE (grid), 12);
  for (i = 0; i < N_ST; i++)
    {
      GtkWidget *label = gtk_label_new (_(headings[i]));
      /* The figures under each heading are set in. */
      gboolean sub = i != ST_SESSION && i != ST_TOTAL && i != ST_STREAKS;

      gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
      gtk_misc_set_padding (GTK_MISC (label), sub ? 16 : 0, 0);
      gtk_table_attach (GTK_TABLE (grid), label, 0, 1, (guint) i,
                        (guint) i + 1, GTK_FILL, 0, 0, 0);
      if (i == ST_TOTAL || i == ST_STREAKS)
        gtk_table_set_row_spacing (GTK_TABLE (grid), (guint) i - 1, 8);
      stats_labels[i] = gtk_label_new ("");
      gtk_misc_set_alignment (GTK_MISC (stats_labels[i]), 0, 0.5);
      gtk_table_attach (GTK_TABLE (grid), stats_labels[i], 1, 2, (guint) i,
                        (guint) i + 1, GTK_FILL, 0, 0, 0);
    }
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (stats_dialog)->vbox), grid,
                      TRUE, TRUE, 0);
  update_stats_dialog ();

  button = games_dialog_add_button (stats_dialog, _("OK"));
  gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
                             GTK_SIGNAL_FUNC (gtk_widget_destroy),
                             GTK_OBJECT (stats_dialog));
  button = games_dialog_add_button (stats_dialog, _("Clear"));
  gtk_signal_connect (GTK_OBJECT (button), "clicked",
                      GTK_SIGNAL_FUNC (stats_clear), NULL);

  gtk_widget_show_all (stats_dialog);
}

static void
about (void)
{
  static const gchar *const credits[] = {
    "Code: Changwoo Ryu",
    NULL
  };

  games_about (GTK_WINDOW (window), "cgfreecell",
               _("Classic GUI FreeCell"),
               "Copyright (C) 1998 The Free Software Foundation\n"
               "Copyright (C) 2026 The CGTK authors",
               credits, NULL);
}

/* The window. */

static void
leave (void)
{
  /* The table's size is kept for next time. */
  if (frame)
    {
      games_config_set_int (config, "width", table_width);
      games_config_set_int (config, "height", table_height);
      games_config_save (config);
    }
  gtk_main_quit ();
}

static void
resign_and_leave (gboolean yes, gpointer data)
{
  (void) data;
  if (!yes)
    return;
  count_game (FALSE);
  leave ();
}

/* Leaving with a game in play gives it up, as Windows asks first. */
static void
quit_game (void)
{
  if (busy ())
    {
      finish_moves ();
      after_moves ();
    }
  if (!in_play ())
    {
      leave ();
      return;
    }
  games_question (GTK_WINDOW (window), _("FreeCell"),
                  _("Do you want to resign this game?"), _("Yes"), _("No"),
                  resign_and_leave, NULL);
}

enum { NEW_GAME, SELECT, RESTART, STATISTICS, OPTIONS, UNDO, QUIT_GAME,
       ABOUT };

static void
menu_callback (gpointer data, guint action, GtkWidget *widget)
{
  (void) data;
  (void) widget;
  switch (action)
    {
    case NEW_GAME:
      leave_for (0);
      break;
    case SELECT:
      select_game ();
      break;
    case RESTART:
      if (game_number)
        leave_for (game_number);
      break;
    case STATISTICS:
      statistics ();
      break;
    case OPTIONS:
      options ();
      break;
    case UNDO:
      undo ();
      break;
    case QUIT_GAME:
      quit_game ();
      break;
    case ABOUT:
      about ();
      break;
    }
}

static GtkItemFactoryEntry menu_items[] = {
  { N_("/_Game"), NULL, NULL, 0, "<Branch>" },
  { N_("/Game/_New Game"), "F2", GTK_SIGNAL_FUNC (menu_callback), NEW_GAME,
    NULL },
  { N_("/Game/_Select Game..."), "F3", GTK_SIGNAL_FUNC (menu_callback),
    SELECT, NULL },
  { N_("/Game/_Restart Game"), NULL, GTK_SIGNAL_FUNC (menu_callback),
    RESTART, NULL },
  { "/Game/sep1", NULL, NULL, 0, "<Separator>" },
  { N_("/Game/S_tatistics..."), "F4", GTK_SIGNAL_FUNC (menu_callback),
    STATISTICS, NULL },
  { N_("/Game/O_ptions..."), "F5", GTK_SIGNAL_FUNC (menu_callback), OPTIONS,
    NULL },
  { "/Game/sep2", NULL, NULL, 0, "<Separator>" },
  { N_("/Game/_Undo"), "F10", GTK_SIGNAL_FUNC (menu_callback), UNDO, NULL },
  { "/Game/sep3", NULL, NULL, 0, "<Separator>" },
  { N_("/Game/E_xit"), "<control>Q", GTK_SIGNAL_FUNC (menu_callback),
    QUIT_GAME, NULL },
  { N_("/_Help"), NULL, NULL, 0, "<Branch>" },
  { N_("/Help/_About FreeCell..."), NULL, GTK_SIGNAL_FUNC (menu_callback),
    ABOUT, NULL },
};

static gchar *
translate (const gchar *path, gpointer data)
{
  (void) data;
  return (gchar *) _(path);
}

static gint
delete_event_callback (GtkWidget *widget, GdkEvent *event, gpointer data)
{
  (void) widget;
  (void) event;
  (void) data;
  quit_game ();
  return TRUE;
}

/* The count of cards left sits at the right of the menu bar, as an item
 * that can't be picked. */
static void
no_select (GtkWidget *item, gpointer data)
{
  (void) data;
  gtk_signal_emit_stop_by_name (GTK_OBJECT (item), "select");
}

int
main (int argc, char *argv[])
{
  GtkWidget *vbox, *menubar, *left_item;
  GtkAccelGroup *accel_group;
  GtkItemFactory *factory;

  gtk_set_locale ();
  games_init_translation ();
  gtk_init (&argc, &argv);

  config = games_config_load (APPNAME);
  games_rand_seed (games_rand_new_seed ());
  load_options ();
  load_stats ();

  cards = games_cards_new (1);
  if (!cards)
    {
      fprintf (stderr, _("%s: the card images could not be read\n"), argv[0]);
      return 1;
    }
  games_cards_size (cards, &card_width, &card_height);

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_wmclass (GTK_WINDOW (window), APPNAME, "Cgfreecell");
  gtk_window_set_policy (GTK_WINDOW (window), FALSE, TRUE, FALSE);
  gtk_signal_connect (GTK_OBJECT (window), "delete_event",
                      GTK_SIGNAL_FUNC (delete_event_callback), NULL);
  gtk_signal_connect (GTK_OBJECT (window), "key_press_event",
                      GTK_SIGNAL_FUNC (key_press), NULL);

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (window), vbox);

  accel_group = gtk_accel_group_new ();
  factory = gtk_item_factory_new (GTK_TYPE_MENU_BAR, "<main>", accel_group);
  gtk_item_factory_set_translate_func (factory, translate, NULL, NULL);
  gtk_item_factory_create_items (factory, ELEMENTS (menu_items), menu_items,
                                 NULL);
  gtk_window_add_accel_group (GTK_WINDOW (window), accel_group);
  menubar = gtk_item_factory_get_widget (factory, "<main>");
  left_label = gtk_label_new ("");
  left_item = gtk_menu_item_new ();
  gtk_container_add (GTK_CONTAINER (left_item), left_label);
  gtk_menu_item_right_justify (GTK_MENU_ITEM (left_item));
  gtk_signal_connect (GTK_OBJECT (left_item), "select",
                      GTK_SIGNAL_FUNC (no_select), NULL);
  gtk_menu_bar_append (GTK_MENU_BAR (menubar), left_item);
  gtk_box_pack_start (GTK_BOX (vbox), menubar, FALSE, FALSE, 0);
  undo_item = gtk_item_factory_get_widget (factory, "/Game/Undo");
  restart_item = gtk_item_factory_get_widget (factory, "/Game/Restart Game");

  table = gtk_drawing_area_new ();
  /* The table starts at the size it was left at, roomy the first time;
   * once shown, it can be made as small as LEAST_ZOOM of that. */
  gtk_widget_set_usize (table,
                        MAX (games_config_get_int (config, "width",
                                                   roomy_width ()),
                             LEAST_WIDTH),
                        MAX (games_config_get_int (config, "height",
                                                   roomy_height ()),
                             LEAST_HEIGHT));
  gtk_widget_set_events (table, GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK
                         | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK
                         | GDK_LEAVE_NOTIFY_MASK);
  gtk_signal_connect (GTK_OBJECT (table), "configure_event",
                      GTK_SIGNAL_FUNC (table_configure), NULL);
  gtk_signal_connect (GTK_OBJECT (table), "expose_event",
                      GTK_SIGNAL_FUNC (table_expose), NULL);
  gtk_signal_connect (GTK_OBJECT (table), "button_press_event",
                      GTK_SIGNAL_FUNC (table_press), NULL);
  gtk_signal_connect (GTK_OBJECT (table), "button_release_event",
                      GTK_SIGNAL_FUNC (table_release), NULL);
  gtk_signal_connect (GTK_OBJECT (table), "motion_notify_event",
                      GTK_SIGNAL_FUNC (table_motion), NULL);
  gtk_signal_connect (GTK_OBJECT (table), "leave_notify_event",
                      GTK_SIGNAL_FUNC (table_leave), NULL);
  gtk_box_pack_start (GTK_BOX (vbox), table, TRUE, TRUE, 0);

  /* As in Windows, the table is empty until a game is dealt. */
  show_title ();
  show_left ();
  set_menus ();
  gtk_widget_show_all (vbox);
  games_window_set_icon (window, "cgfreecell", cgfreecell_xpm);
  gtk_widget_show (window);
  gtk_widget_set_usize (table, LEAST_WIDTH, LEAST_HEIGHT);
  gtk_main ();

  finish_moves ();
  if (refit_timer)
    gtk_timeout_remove (refit_timer);
  g_free (frame);
  g_free (under);
  g_free (head);
  g_free (head_smiling);
  games_cards_free (cards);
  games_config_free (config);
  return 0;
}
