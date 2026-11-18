/*
 * cgsolitaire, Klondike as the classic Solitaire played it
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

#include "config.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "games.h"
#include "games-cards.h"

#include "icons/cgsolitaire.xpm"

#define APPNAME "cgsolitaire"
#define APPNAME_LONG N_("Solitaire")

#define ELEMENTS(x) (sizeof (x) / sizeof (x [0]))

/* The cards. A card is its rank, 1 to 13, its suit above that, and
 * whether it is face up. */

typedef guint8 Card;

#define CARD(suit, rank) ((Card) ((suit) << 4 | (rank)))
#define RANK(c) ((c) & 0x0f)
#define SUIT(c) (((c) >> 4) & 3)
#define FACE_UP 0x80
#define IS_UP(c) (((c) & FACE_UP) != 0)
#define RED(c) GAMES_SUIT_RED (SUIT (c))

/* The piles: the stock, the waste the stock is turned onto, the four
 * foundations built up by suit from the aces, and the seven columns of
 * the tableau built down in alternate colours. */
enum
{
  STOCK,
  WASTE,
  FOUNDATION,
  TABLEAU = FOUNDATION + 4,
  N_PILES = TABLEAU + 7
};

#define IS_FOUNDATION(p) ((p) >= FOUNDATION && (p) < TABLEAU)
#define IS_TABLEAU(p) ((p) >= TABLEAU)

typedef struct
{
  Card cards[52];
  gint n;
} Pile;

enum { SCORING_STANDARD, SCORING_VEGAS, SCORING_NONE };

/* Everything one move can change, so that a move is undone by putting
 * back a copy. */
typedef struct
{
  Pile piles[N_PILES];
  gint fan;             /* cards of the last draw spread on the waste */
  gint redeals;         /* times the waste went back to the stock */
  gint score;           /* points, or dollars in Vegas */
} State;

static State state, undo_state;
static gboolean can_undo;
static gboolean won;
/* The ten-second steps of a timed game already charged. */
static gint time_charged;
/* Whether the last game was played with Vegas scoring, for the
 * cumulative score to go on from. */
static gboolean vegas_played;

static struct
{
  gint draw;                    /* 1 or 3 */
  gint scoring;
  gboolean timed;
  gboolean statusbar;
  gboolean outline;
  gboolean cumulative;
  gint back;
} opts;

static GamesConfig *config;
static GamesCards *cards;
static gint card_width, card_height;

static GtkWidget *window;
static GtkWidget *table;
static GtkWidget *status_box;
static GtkWidget *statusbar;
static GtkWidget *score_label;
static GtkWidget *score_frame;
static GtkWidget *time_frame;
static GtkWidget *clk;
static GtkWidget *undo_item;
static GtkWidget *options_dialog;
static GtkWidget *deck_dialog;
static guint tick_timeout;

/* Layout. */

#define TABLE_COLOUR 0x008000
#define MIN_GAP 4
#define TOP 8
#define ROW_GAP 12
#define BOTTOM 8
#define FAN_STEP 18             /* across, between the waste's spread cards */
#define DOWN_STEP 6             /* down, from a face down card */
#define UP_STEP 30              /* down, from a face up card */
#define MIN_UP_STEP 15
#define STOCK_LAYER 10          /* cards to each layer the stock shows */

/* The table drawn, as in cgmahjong: the frame is the drawing area's
 * width by height in device pixels, device of them to the toolkit's.
 * The table is laid out in the game's pixels, the cards' own, which are
 * positions everywhere else; a game's pixel is zoom of the toolkit's
 * and scale of the device's, zoom what fits the table at its usual size
 * to the window, the cards drawn at that size; frame_width by
 * frame_height of them fill it. */
static guchar *frame;
/* While cards are dragged, the table under them, for putting back where
 * they were. */
static guchar *under;
/* The drawing area's size, in the toolkit's pixels. */
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
static gint up_step[N_PILES], down_step[N_PILES];

/* A card being dragged, or pressed and maybe about to be. */
static struct
{
  gboolean pressed;
  gboolean active;
  gint pile, index;
  gint press_x, press_y;
  gint dx, dy;                  /* the pointer from the card's corner */
  gint x, y;                    /* the card's corner */
  gint to_x, to_y;              /* where the pointer has taken it since */
  guint idle;                   /* to move it there */
} drag;

/* The last press of the left button, to tell a double click by. */
static struct
{
  guint32 time;
  gint x, y, pile;
} last_click;

#define DOUBLE_CLICK_MS 500     /* as long as Windows gives one */
#define DOUBLE_CLICK_PX 4

/* A card clicked on, to be put where the next click is; -1 if none. */
static gint sel_pile = -1, sel_index;

/* The keyboard's place, as Windows Solitaire has one: a pile, and in a
 * column the card the run picked up would start at. It is shown, as a
 * ring, from the first key that moves it until the mouse is used. */
static struct
{
  gboolean shown;
  gint pile, index;
} cur = { FALSE, STOCK, 0 };

/* The cards bouncing off the foundations at a win. */
static struct
{
  gboolean active;
  guint timeout;
  gint left[4];                 /* cards still on each foundation */
  gint next;                    /* the foundation to take one from */
  gboolean flying;
  Card card;
  double x, y, vx, vy;
} cascade;

static void deal (void);
static void redraw (void);
static void stop_cascade (void);
static void drag_cancel (void);

/* Drawing. */

/* Has an area of the table, in the game's pixels, shown again. */
static void
queue_area (gint x, gint y, gint width, gint height)
{
  gint x0 = (gint) floor (x * zoom) - 1, y0 = (gint) floor (y * zoom) - 1;
  gint x1 = (gint) ceil ((x + width) * zoom) + 1;
  gint y1 = (gint) ceil ((y + height) * zoom) + 1;

  gtk_widget_queue_draw_area (table, x0, y0, x1 - x0, y1 - y0);
}

/* Lays a card-sized RGBA image over the frame at (x, y), with its
 * colours turned round if invert. */
static void
blit (const guchar *src, gint x, gint y, gboolean invert)
{
  gint cw, w, h, sx = 0, sy = 0, i, j;

  games_cards_pixel_size (cards, &cw, &h);
  w = cw;
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
      const guchar *s = src + ((gsize) (sy + j) * cw + sx) * 4;
      guchar *d = frame + ((gsize) (y + j) * DEVICE_WIDTH + x) * 3;

      for (i = 0; i < w; i++, s += 4, d += 3)
        {
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
  if (IS_UP (card))
    blit (games_cards_face (cards, SUIT (card), RANK (card)), x, y, invert);
  else
    blit (games_cards_back (cards, opts.back), x, y, invert);
}

/* The O on an empty stock that can be turned over again, or the X on
 * one that can't, darkening the table like the slot does. */
static void
draw_stock_mark (gint x0, gint y0, gboolean redeal)
{
  double cx = card_width / 2.0, cy = card_height / 2.0;
  double r = card_width * 0.28;
  gint x, y, i, j, w, h;

  games_cards_pixel_size (cards, &w, &h);
  x0 = DEV (x0);
  y0 = DEV (y0);
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      {
        gint n = 0;
        guchar *d;

        if (x0 + x < 0 || y0 + y < 0 || x0 + x >= DEVICE_WIDTH
            || y0 + y >= DEVICE_HEIGHT)
          continue;
        for (j = 0; j < 4; j++)
          for (i = 0; i < 4; i++)
            {
              /* In the game's pixels, so the mark is the same at any
               * scale. */
              double u = (x + (i + 0.5) / 4) / scale - cx;
              double v = (y + (j + 0.5) / 4) / scale - cy;

              if (redeal)
                {
                  double d2 = u * u + v * v;

                  n += d2 <= r * r && d2 >= (r - 5) * (r - 5);
                }
              else
                {
                  double a = u > 0 ? u : -u, b = v > 0 ? v : -v;
                  double off = a - b > 0 ? a - b : b - a;

                  n += off < 3.5 && a < r && b < r;
                }
            }
        if (!n)
          continue;
        d = frame + ((gsize) (y0 + y) * DEVICE_WIDTH + x0 + x) * 3;
        /* As dark as the slot's edge where covered: 40% black. */
        for (i = 0; i < 3; i++)
          d[i] = (guchar) (d[i] - d[i] * 2 * n / (5 * 16));
      }
}

static gboolean
can_redeal (void)
{
  if (opts.scoring != SCORING_VEGAS)
    return TRUE;
  /* Vegas goes through the stock once drawing one, three times drawing
   * three. */
  return opts.draw == 3 && state.redeals < 2;
}

/* Where card i of pile p is drawn. */
static void
card_pos (gint p, gint i, gint *x, gint *y)
{
  const Pile *pile = &state.piles[p];
  gint j;

  *x = pile_x[p];
  *y = pile_y[p];
  if (p == STOCK)
    {
      *x += 2 * (i / STOCK_LAYER);
      *y += 2 * (i / STOCK_LAYER);
    }
  else if (p == WASTE)
    {
      gint first = pile->n - state.fan;

      if (i > first)
        *x += (i - first) * FAN_STEP;
    }
  else if (IS_TABLEAU (p))
    for (j = 0; j < i; j++)
      *y += IS_UP (pile->cards[j]) ? up_step[p] : down_step[p];
}

/* Closes up the columns of the tableau that would run off the bottom. */
static void
fit_columns (void)
{
  gint p;

  for (p = TABLEAU; p < N_PILES; p++)
    {
      const Pile *pile = &state.piles[p];
      gint room = frame_height - BOTTOM - pile_y[p] - card_height;
      gint down = 0, up = 0, i;

      for (i = 0; i + 1 < pile->n; i++)
        {
          if (IS_UP (pile->cards[i]))
            up++;
          else
            down++;
        }
      down_step[p] = DOWN_STEP;
      up_step[p] = UP_STEP;
      if (up && down * DOWN_STEP + up * UP_STEP > room)
        up_step[p] = MAX (MIN_UP_STEP, (room - down * DOWN_STEP) / up);
      if (down && down * down_step[p] + up * up_step[p] > room)
        down_step[p] = MAX (1, (room - up * up_step[p]) / down);
    }
}

static void
layout (void)
{
  gint gap = MAX (MIN_GAP, (frame_width - 7 * card_width) / 8);
  gint left = (frame_width - 7 * card_width - 6 * gap) / 2;
  gint p;

  if (gap > 3 * MIN_GAP && left < gap)
    left = gap;
  for (p = 0; p < 7; p++)
    {
      gint x = left + p * (card_width + gap);

      pile_x[TABLEAU + p] = x;
      pile_y[TABLEAU + p] = TOP + card_height + ROW_GAP;
      if (p < 2)
        pile_x[STOCK + p] = x, pile_y[STOCK + p] = TOP;
      else if (p > 2)
        pile_x[FOUNDATION + p - 3] = x, pile_y[FOUNDATION + p - 3] = TOP;
    }
  fit_columns ();
}

/* Whether card i of pile p is left out of the table being drawn, as it
 * is dragged. */
static gboolean
dragged (gint p, gint i)
{
  return drag.active && !opts.outline && p == drag.pile && i >= drag.index;
}

static void
draw_pile (gint p)
{
  const Pile *pile = &state.piles[p];
  gint n = pile->n, first = 0, i, x, y;

  if (cascade.active && IS_FOUNDATION (p))
    n = cascade.left[p - FOUNDATION];

  if (n == 0 || dragged (p, 0))
    {
      blit (games_cards_slot (cards), pile_x[p], pile_y[p], FALSE);
      if (p == STOCK)
        draw_stock_mark (pile_x[p], pile_y[p], can_redeal ());
    }

  /* Only the cards that can show need drawing: the stock shows the
   * edge of each layer and its top. */
  if (p == STOCK)
    {
      for (i = 0; i < n; i += STOCK_LAYER)
        {
          card_pos (p, i, &x, &y);
          draw_card (pile->cards[i], x, y, FALSE);
        }
      first = MAX (0, n - 1);
    }
  else if (p == WASTE)
    first = MAX (0, n - state.fan - 1);
  else if (IS_FOUNDATION (p))
    first = MAX (0, n - 2);

  for (i = first; i < n; i++)
    {
      if (dragged (p, i))
        break;
      card_pos (p, i, &x, &y);
      draw_card (pile->cards[i], x, y, p == sel_pile && i >= sel_index);
    }
}

/* How far down the dragged cards reach. */
static gint
dragged_height (void)
{
  gint n = state.piles[drag.pile].n - 1 - drag.index;

  return n * (opts.outline ? up_step[drag.pile] : UP_STEP) + card_height;
}

/* Draws the dragged cards over the frame, or with outline dragging the
 * edge of the run, twice over to stand out on the cards. */
static void
draw_dragged (void)
{
  const Pile *pile = &state.piles[drag.pile];
  gint i, k;

  if (opts.outline)
    for (k = 0; k < 2; k++)
      {
        gint h = dragged_height () - card_height;

        blit (games_cards_slot (cards), drag.x, drag.y, FALSE);
        if (h)
          blit (games_cards_slot (cards), drag.x, drag.y + h, FALSE);
      }
  else
    for (i = drag.index; i < pile->n; i++)
      draw_card (pile->cards[i], drag.x, drag.y + (i - drag.index) * UP_STEP,
                 FALSE);
}

/* Whether (x, y) is inside the box (x0, y0)-(x1, y1) with its corners
 * rounded r. */
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

/* The keyboard's ring, as FreeCell's: RING_WIDTH wide, RING_GAP
 * outside the cards it goes round, its corners rounded as a card's. */
#define RING_COLOUR 0xffe040
#define RING_GAP 1
#define RING_WIDTH 3
#define CARD_CORNER (MAX (card_width / 20, 2))

static void
draw_ring (gint x, gint y, gint height)
{
  double out = RING_GAP + RING_WIDTH;
  double x0 = x - out, y0 = y - out;
  double x1 = x + card_width + out, y1 = y + height + out;
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

/* Keeps the keyboard's card where one can be taken: in a column, one
 * of its face up cards, or its top card; elsewhere the top card. */
static void
cursor_fix (void)
{
  const Pile *pile = &state.piles[cur.pile];
  gint first = pile->n - 1;

  if (IS_TABLEAU (cur.pile))
    while (first > 0 && IS_UP (pile->cards[first - 1]))
      first--;
  if (!IS_TABLEAU (cur.pile) || cur.index > pile->n - 1 || cur.index < first)
    cur.index = pile->n - 1;
}

/* The ring round the keyboard's card, and the cards on it. */
static void
draw_cursor (void)
{
  const Pile *pile = &state.piles[cur.pile];
  gint x, y, tx, ty;

  cursor_fix ();
  if (pile->n == 0 || cur.pile == STOCK)
    {
      x = pile_x[cur.pile];
      y = pile_y[cur.pile];
      if (pile->n)
        card_pos (cur.pile, pile->n - 1, &x, &y);
      draw_ring (x, y, card_height);
      return;
    }
  card_pos (cur.pile, cur.index, &x, &y);
  card_pos (cur.pile, pile->n - 1, &tx, &ty);
  draw_ring (x, y, ty + card_height - y);
}

static void
redraw (void)
{
  gsize i;
  gint p;

  if (!frame)
    return;
  for (i = 0; i < (gsize) DEVICE_WIDTH * DEVICE_HEIGHT; i++)
    {
      frame[i * 3] = (TABLE_COLOUR >> 16) & 0xff;
      frame[i * 3 + 1] = (TABLE_COLOUR >> 8) & 0xff;
      frame[i * 3 + 2] = TABLE_COLOUR & 0xff;
    }
  fit_columns ();
  for (p = 0; p < N_PILES; p++)
    draw_pile (p);
  if (cur.shown && !won && !cascade.active)
    draw_cursor ();

  if (drag.active)
    {
      gsize size = (gsize) DEVICE_WIDTH * DEVICE_HEIGHT * 3;

      if (!under)
        under = g_malloc (size);
      memcpy (under, frame, size);
      draw_dragged ();
    }

  gtk_widget_queue_draw (table);
}

/* Moves the dragged cards to (x, y): only where they were and where
 * they go is drawn again, and only that is sent to the window. */
static void
move_dragged (gint x, gint y)
{
  gint h = dragged_height (), j, pw, ph, x0, x1, y0, y1;

  /* The last card's pixels, where blit lays it. */
  games_cards_pixel_size (cards, &pw, &ph);
  x0 = MAX (DEV (drag.x), 0);
  x1 = MIN (DEV (drag.x) + pw, DEVICE_WIDTH);
  y0 = MAX (DEV (drag.y), 0);
  y1 = MIN (DEV (drag.y + h - card_height) + ph, DEVICE_HEIGHT);

  if (!under)
    {
      drag.x = x;
      drag.y = y;
      redraw ();
      return;
    }
  for (j = y0; j < y1 && x0 < x1; j++)
    {
      gsize offset = ((gsize) j * DEVICE_WIDTH + x0) * 3;

      memcpy (frame + offset, under + offset, (gsize) (x1 - x0) * 3);
    }
  queue_area (drag.x, drag.y, card_width, h);

  drag.x = x;
  drag.y = y;
  draw_dragged ();
  queue_area (drag.x, drag.y, card_width, h);
}

/* The table at its usual size, roomy enough for long columns, in the
 * game's pixels; the window starts at this. */
static gint
roomy_width (void)
{
  return 7 * card_width + 8 * 12;
}

static gint
roomy_height (void)
{
  return TOP + 2 * card_height + ROW_GAP + 6 * DOWN_STEP + 9 * UP_STEP
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

      /* If the deck can't be read now, the old cards do. */
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

  /* GdkRectangle's sizes are unsigned: the area is clipped in ints. */
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

/* The status bar. */

static void
show_score (void)
{
  gchar text[64];

  if (opts.scoring == SCORING_NONE)
    {
      gtk_widget_hide (score_frame);
      return;
    }
  if (opts.scoring == SCORING_VEGAS)
    g_snprintf (text, sizeof text, _("Score: %s$%d"),
                state.score < 0 ? "-" : "", ABS (state.score));
  else
    g_snprintf (text, sizeof text, _("Score: %d"), state.score);
  gtk_label_set_text (GTK_LABEL (score_label), text);
  gtk_widget_show (score_frame);
}

static void
show_status (void)
{
  show_score ();
  if (opts.timed)
    gtk_widget_show (time_frame);
  else
    gtk_widget_hide (time_frame);
  if (opts.statusbar)
    gtk_widget_show (status_box);
  else
    gtk_widget_hide (status_box);
}

/* Standard scoring's points, which never go below nothing. */
static void
add_points (State *s, gint points)
{
  if (opts.scoring == SCORING_STANDARD)
    s->score = MAX (0, s->score + points);
}

/* A timed game loses 2 points every 10 seconds. */
static gint
tick (gpointer data)
{
  gint steps;

  (void) data;
  if (!opts.timed || opts.scoring != SCORING_STANDARD || won)
    return TRUE;
  steps = games_clock_get (clk) / 10;
  if (steps > time_charged)
    {
      while (time_charged < steps)
        {
          add_points (&state, -2);
          add_points (&undo_state, -2);
          time_charged++;
        }
      show_score ();
    }
  return TRUE;
}

/* The clock starts with the first move. */
static void
started (void)
{
  if (!won && !games_clock_running (clk))
    games_clock_start (clk);
}

/* The rules. */

static Card
top (gint p)
{
  const Pile *pile = &state.piles[p];

  return pile->n ? pile->cards[pile->n - 1] : 0;
}

/* Whether the cards of pile from from index on can go on pile to. */
static gboolean
can_move (gint from, gint index, gint to)
{
  const Pile *src = &state.piles[from];
  Card card, dest;

  if (from == to || index < 0 || index >= src->n)
    return FALSE;
  card = src->cards[index];
  if (!IS_UP (card))
    return FALSE;
  /* Only a column gives up more than its top card. */
  if (!IS_TABLEAU (from) && index != src->n - 1)
    return FALSE;

  dest = top (to);
  if (IS_FOUNDATION (to))
    {
      if (index != src->n - 1)
        return FALSE;
      if (!dest)
        return RANK (card) == GAMES_ACE;
      return SUIT (dest) == SUIT (card) && RANK (dest) + 1 == RANK (card);
    }
  if (IS_TABLEAU (to))
    {
      if (!dest)
        return RANK (card) == GAMES_KING;
      return IS_UP (dest) && RED (dest) != RED (card)
        && RANK (dest) == RANK (card) + 1;
    }
  return FALSE;
}

static void
save_undo (void)
{
  undo_state = state;
  can_undo = TRUE;
  gtk_widget_set_sensitive (undo_item, TRUE);
}

static void
start_cascade (void);

static void
check_won (void)
{
  gint f, seconds;

  for (f = FOUNDATION; f < TABLEAU; f++)
    if (state.piles[f].n < 13)
      return;

  won = TRUE;
  can_undo = FALSE;
  gtk_widget_set_sensitive (undo_item, FALSE);
  games_clock_stop (clk);

  seconds = games_clock_get (clk);
  if (opts.timed && opts.scoring == SCORING_STANDARD && seconds >= 30)
    {
      gint bonus = 700000 / seconds;
      gchar text[64];

      add_points (&state, bonus);
      g_snprintf (text, sizeof text, _("Bonus: %d"), bonus);
      games_flash (GTK_STATUSBAR (statusbar), text);
    }
  show_score ();
  start_cascade ();
}

static void
move_cards (gint from, gint index, gint to)
{
  Pile *src = &state.piles[from], *dest = &state.piles[to];
  gint n = src->n - index;

  save_undo ();
  if (from == WASTE && IS_TABLEAU (to))
    add_points (&state, 5);
  else if ((from == WASTE || IS_TABLEAU (from)) && IS_FOUNDATION (to))
    add_points (&state, 10);
  else if (IS_FOUNDATION (from) && IS_TABLEAU (to))
    add_points (&state, -15);
  if (opts.scoring == SCORING_VEGAS)
    {
      if (IS_FOUNDATION (to))
        state.score += 5;
      if (IS_FOUNDATION (from))
        state.score -= 5;
    }

  memcpy (dest->cards + dest->n, src->cards + index, (gsize) n);
  dest->n += n;
  src->n = index;
  if (from == WASTE)
    state.fan = src->n ? MAX (state.fan - 1, 1) : 0;

  sel_pile = -1;
  started ();
  show_score ();
  redraw ();
  check_won ();
}

/* Sends the top card of pile p to a foundation, if it can go. */
static gboolean
to_foundation (gint p)
{
  gint f;

  for (f = FOUNDATION; f < TABLEAU; f++)
    if (can_move (p, state.piles[p].n - 1, f))
      {
        move_cards (p, state.piles[p].n - 1, f);
        return TRUE;
      }
  return FALSE;
}

/* Sends every card that can go to the foundations. */
static void
play_all (void)
{
  gboolean moved;

  do
    {
      gint p;

      moved = FALSE;
      for (p = WASTE; p < N_PILES && !won; p++)
        if (!IS_FOUNDATION (p) && state.piles[p].n && to_foundation (p))
          moved = TRUE;
    }
  while (moved && !won);
}

static void
turn_stock (void)
{
  Pile *stock = &state.piles[STOCK], *waste = &state.piles[WASTE];

  if (stock->n)
    {
      gint k = MIN (opts.draw, stock->n), i;

      save_undo ();
      for (i = 0; i < k; i++)
        waste->cards[waste->n++] = stock->cards[--stock->n] | FACE_UP;
      state.fan = k;
    }
  else if (waste->n && can_redeal ())
    {
      save_undo ();
      while (waste->n)
        stock->cards[stock->n++] = waste->cards[--waste->n] & ~FACE_UP;
      state.fan = 0;
      state.redeals++;
      /* Standard scoring's charge for each time through the stock after
       * the first drawing one, after the third drawing three. */
      if (opts.draw == 1)
        add_points (&state, -100);
      else if (state.redeals >= 3)
        add_points (&state, -20);
      show_score ();
    }
  else
    return;
  sel_pile = -1;
  started ();
  redraw ();
}

static void
turn_over (gint p)
{
  Pile *pile = &state.piles[p];

  save_undo ();
  pile->cards[pile->n - 1] |= FACE_UP;
  add_points (&state, 5);
  sel_pile = -1;
  started ();
  show_score ();
  redraw ();
}

static void
undo (void)
{
  if (!can_undo)
    return;
  state = undo_state;
  can_undo = FALSE;
  gtk_widget_set_sensitive (undo_item, FALSE);
  sel_pile = -1;
  show_score ();
  redraw ();
}

static void
deal (void)
{
  Card deck[52];
  gint i, p, bank = 0;

  stop_cascade ();
  for (i = 0; i < 52; i++)
    deck[i] = CARD (i / 13, i % 13 + 1);
  for (i = 51; i > 0; i--)
    {
      gint j = games_rand_int (i + 1);
      Card c = deck[i];

      deck[i] = deck[j];
      deck[j] = c;
    }

  /* Vegas costs $52 a game, going on from the last if the score is
   * cumulative. */
  if (opts.scoring == SCORING_VEGAS && opts.cumulative && vegas_played)
    bank = state.score;
  memset (&state, 0, sizeof state);
  i = 0;
  for (p = 0; p < 7; p++)
    {
      Pile *pile = &state.piles[TABLEAU + p];
      gint k;

      for (k = 0; k <= p; k++)
        pile->cards[pile->n++] = deck[i++];
      pile->cards[pile->n - 1] |= FACE_UP;
    }
  while (i < 52)
    state.piles[STOCK].cards[state.piles[STOCK].n++] = deck[i++];

  if (opts.scoring == SCORING_VEGAS)
    {
      state.score = bank - 52;
      vegas_played = TRUE;
    }

  won = FALSE;
  can_undo = FALSE;
  gtk_widget_set_sensitive (undo_item, FALSE);
  time_charged = 0;
  sel_pile = -1;
  drag_cancel ();
  games_clock_stop (clk);
  games_clock_set (clk, 0);
  show_score ();
  redraw ();
}

/* The win: the cards come off the foundations one by one, kings first,
 * and bounce off across the window, leaving their trail. */

#define CASCADE_MS 20
#define CASCADE_STEPS 3         /* moves drawn each time */
#define GRAVITY 0.75
#define BOUNCE 0.8

static gboolean
cascade_launch (void)
{
  gint k;

  for (k = 0; k < 4; k++)
    {
      gint f = (cascade.next + k) % 4;
      gint x, y;

      if (!cascade.left[f])
        continue;
      cascade.next = (f + 1) % 4;
      cascade.card = state.piles[FOUNDATION + f].cards[--cascade.left[f]];
      x = pile_x[FOUNDATION + f];
      y = pile_y[FOUNDATION + f];
      cascade.x = x;
      cascade.y = y;
      /* Off to one side or the other, rising a little or not at all. */
      cascade.vx = (games_rand_int (2) ? 1 : -1) * (3 + games_rand_int (6));
      cascade.vy = -games_rand_int (10);
      cascade.flying = TRUE;

      /* What the foundation shows under it: the next card down. The
       * last, the ace, leaves its picture where it was, trails and
       * all, as on Windows, rather than baring the table there. */
      if (cascade.left[f])
        draw_card (state.piles[FOUNDATION + f].cards[cascade.left[f] - 1],
                   x, y, FALSE);
      queue_area (x, y, card_width, card_height);
      return TRUE;
    }
  return FALSE;
}

static void
cascade_finished (void);

static gint
cascade_step (gpointer data)
{
  gint k;

  (void) data;
  for (k = 0; k < CASCADE_STEPS; k++)
    {
      gint x, y;

      if (!cascade.flying && !cascade_launch ())
        {
          cascade.timeout = 0;
          cascade_finished ();
          return FALSE;
        }

      cascade.x += cascade.vx;
      cascade.y += cascade.vy;
      cascade.vy += GRAVITY;
      if (cascade.y + card_height > frame_height)
        {
          cascade.y = frame_height - card_height;
          cascade.vy = -cascade.vy * BOUNCE;
        }
      if (cascade.x + card_width < 0 || cascade.x >= frame_width)
        {
          cascade.flying = FALSE;
          continue;
        }

      x = (gint) cascade.x;
      y = (gint) cascade.y;
      draw_card (cascade.card, x, y, FALSE);
      queue_area (x, y, card_width, card_height);
    }
  return TRUE;
}

static void
start_cascade (void)
{
  gint f;

  for (f = 0; f < 4; f++)
    cascade.left[f] = state.piles[FOUNDATION + f].n;
  cascade.next = 0;
  cascade.flying = FALSE;
  cascade.active = TRUE;
  cascade.timeout = gtk_timeout_add (CASCADE_MS, cascade_step, NULL);
}

static void
deal_again (gboolean yes, gpointer data)
{
  (void) data;
  if (yes)
    deal ();
  else
    redraw ();
}

/* The trails stay until the question is answered. */
static void
cascade_finished (void)
{
  if (cascade.timeout)
    gtk_timeout_remove (cascade.timeout);
  cascade.timeout = 0;
  cascade.active = FALSE;
  games_question (GTK_WINDOW (window), _("Solitaire"), _("Deal again?"),
                  _("Yes"), _("No"), deal_again, NULL);
}

static void
stop_cascade (void)
{
  if (!cascade.active)
    return;
  if (cascade.timeout)
    gtk_timeout_remove (cascade.timeout);
  cascade.timeout = 0;
  cascade.active = FALSE;
  redraw ();
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

      /* The top card covers the most, in a column the rest show
       * above it. */
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
          if (!IS_TABLEAU (p))
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

/* Puts the dragged cards on the pile they cover most that takes them. */
static void
drop (void)
{
  gint best = -1, best_area = 0, p;

  for (p = FOUNDATION; p < N_PILES; p++)
    {
      const Pile *pile = &state.piles[p];
      gint x, y, w, h;

      if (!can_move (drag.pile, drag.index, p))
        continue;
      if (pile->n)
        card_pos (p, pile->n - 1, &x, &y);
      else
        x = pile_x[p], y = pile_y[p];
      w = MIN (x, drag.x) + card_width - MAX (x, drag.x);
      h = MIN (y, drag.y) + card_height - MAX (y, drag.y);
      if (w > 0 && h > 0 && w * h > best_area)
        {
          best = p;
          best_area = w * h;
        }
    }
  if (best >= 0)
    move_cards (drag.pile, drag.index, best);
}

static gint
table_press (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
  gint x = (gint) (event->x / zoom), y = (gint) (event->y / zoom), p, i;

  (void) widget;
  (void) data;
  /* GDK's double and triple clicks come as well as the presses; the
   * presses are enough. Taken first, so that the double click a game
   * is won with doesn't stop the cards that win sets bouncing. */
  if (event->type != GDK_BUTTON_PRESS)
    return TRUE;
  if (cascade.active)
    {
      cascade_finished ();
      return TRUE;
    }
  if (won || drag.pressed)
    return TRUE;
  if (cur.shown)
    {
      cur.shown = FALSE;
      redraw ();
    }

  if (event->button == 3)
    {
      sel_pile = -1;
      play_all ();
      redraw ();
      return TRUE;
    }
  if (event->button != 1)
    return TRUE;

  if (!hit (x, y, &p, &i))
    {
      last_click.time = 0;
      if (sel_pile >= 0)
        {
          sel_pile = -1;
          redraw ();
        }
      return TRUE;
    }

  /* A second click on the top card of the waste or a column, soon
   * after the first and close to it, sends the card to its foundation.
   * The double click is timed here, as long as Windows allows for one:
   * GDK's 250 ms is quick for this. */
  if ((p == WASTE || IS_TABLEAU (p)) && i >= 0 && i == state.piles[p].n - 1
      && IS_UP (state.piles[p].cards[i]) && last_click.time
      && p == last_click.pile && event->time - last_click.time <= DOUBLE_CLICK_MS
      && ABS (x - last_click.x) <= DOUBLE_CLICK_PX
      && ABS (y - last_click.y) <= DOUBLE_CLICK_PX)
    {
      last_click.time = 0;
      sel_pile = -1;
      if (!to_foundation (p))
        redraw ();
      return TRUE;
    }
  last_click.time = event->time ? event->time : 1;
  last_click.x = x;
  last_click.y = y;
  last_click.pile = p;

  /* A card clicked on before goes where this click is. */
  if (sel_pile >= 0)
    {
      gint from = sel_pile, index = sel_index;

      sel_pile = -1;
      if (can_move (from, index, p))
        move_cards (from, index, p);
      else
        redraw ();
      return TRUE;
    }

  if (p == STOCK)
    {
      turn_stock ();
      return TRUE;
    }
  if (i < 0)
    return TRUE;
  if (!IS_UP (state.piles[p].cards[i]))
    {
      if (IS_TABLEAU (p) && i == state.piles[p].n - 1)
        turn_over (p);
      return TRUE;
    }

  drag.pressed = TRUE;
  drag.active = FALSE;
  drag.pile = p;
  drag.index = i;
  drag.press_x = x;
  drag.press_y = y;
  card_pos (p, i, &drag.x, &drag.y);
  drag.dx = x - drag.x;
  drag.dy = y - drag.y;
  return TRUE;
}

/* Moves the dragged cards to where the pointer last took them. */
static void
drag_catch_up (void)
{
  if (drag.idle)
    gtk_idle_remove (drag.idle);
  drag.idle = 0;
  if (drag.active && (drag.to_x != drag.x || drag.to_y != drag.y))
    move_dragged (drag.to_x, drag.to_y);
}

static gint
drag_idle (gpointer data)
{
  (void) data;
  drag.idle = 0;
  drag_catch_up ();
  return FALSE;
}

/* Lets go of cards picked up, wherever they are. */
static void
drag_cancel (void)
{
  if (drag.idle)
    gtk_idle_remove (drag.idle);
  drag.idle = 0;
  drag.pressed = drag.active = FALSE;
}

static gint
table_motion (GtkWidget *widget, GdkEventMotion *event, gpointer data)
{
  gint x, y;
  GdkModifierType mask;

  (void) data;
  if (!drag.pressed)
    return TRUE;
  if (event->is_hint)
    gdk_window_get_pointer (widget->window, &x, &y, &mask);
  else
    {
      x = (gint) event->x;
      y = (gint) event->y;
    }
  x = (gint) (x / zoom);
  y = (gint) (y / zoom);
  if (drag.active)
    {
      /* The pointer can report far more often than the screen shows:
       * the cards go where it is once the events waiting are read. */
      drag.to_x = x - drag.dx;
      drag.to_y = y - drag.dy;
      if (!drag.idle)
        drag.idle = gtk_idle_add_priority (GTK_PRIORITY_REDRAW - 1,
                                           drag_idle, NULL);
      return TRUE;
    }
  if (ABS (x - drag.press_x) + ABS (y - drag.press_y) < 4)
    return TRUE;
  /* The table is drawn once without the cards, then kept under them. */
  drag.active = TRUE;
  drag.x = drag.to_x = x - drag.dx;
  drag.y = drag.to_y = y - drag.dy;
  redraw ();
  return TRUE;
}

static gint
table_release (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
  (void) widget;
  (void) data;
  if (event->button != 1 || !drag.pressed)
    return TRUE;
  drag.pressed = FALSE;
  if (drag.active)
    {
      drag_catch_up ();
      drop ();
      drag.active = FALSE;
    }
  else
    {
      /* A click without dragging picks the cards up, to be put down
       * with the next click. */
      sel_pile = drag.pile;
      sel_index = drag.index;
    }
  redraw ();
  return TRUE;
}

/* The keyboard, as in Windows Solitaire: Tab goes from the deck to the
 * suit stacks to the row stacks, the arrows along a row and between the
 * rows, and up a column to the card a run is taken from; Space or Enter
 * turns the stock, turns a column's card over, picks the cards up, or
 * puts the cards picked up down. */

/* The piles of the top row, left to right, and the column under each. */
static const gint top_row[6] = { STOCK, WASTE, FOUNDATION, FOUNDATION + 1,
                                 FOUNDATION + 2, FOUNDATION + 3 };
static const gint under_top[6] = { 0, 1, 3, 4, 5, 6 };

static gint
top_place (gint p)
{
  gint k;

  for (k = 0; k < 6; k++)
    if (top_row[k] == p)
      return k;
  return 0;
}

/* The pile of the top row over column c, or the nearest one. */
static gint
over_column (gint c)
{
  gint k, best = 0;

  for (k = 0; k < 6; k++)
    if (ABS (under_top[k] - c) < ABS (under_top[best] - c))
      best = k;
  return top_row[best];
}

static void
cursor_move (gint pile)
{
  cur.pile = pile;
  cur.index = state.piles[pile].n - 1;
}

/* Space or Enter on the keyboard's card. */
static void
cursor_act (void)
{
  const Pile *pile = &state.piles[cur.pile];

  if (sel_pile >= 0)
    {
      gint from = sel_pile, index = sel_index;

      sel_pile = -1;
      if (from != cur.pile && can_move (from, index, cur.pile))
        {
          /* The keyboard stays on the cards put down, at their top. */
          cursor_move (cur.pile);
          move_cards (from, index, cur.pile);
          cursor_move (cur.pile);
        }
      else
        redraw ();
      return;
    }
  if (cur.pile == STOCK)
    {
      turn_stock ();
      return;
    }
  if (pile->n == 0)
    return;
  if (!IS_UP (pile->cards[pile->n - 1]))
    {
      if (IS_TABLEAU (cur.pile))
        turn_over (cur.pile);
      return;
    }
  if (IS_FOUNDATION (cur.pile) || cur.pile == WASTE || IS_TABLEAU (cur.pile))
    {
      sel_pile = cur.pile;
      sel_index = IS_TABLEAU (cur.pile) ? cur.index : pile->n - 1;
      redraw ();
    }
}

static gboolean
cursor_key (guint keyval, guint state_mask)
{
  gint c = IS_TABLEAU (cur.pile) ? cur.pile - TABLEAU : -1;

  switch (keyval)
    {
    case GDK_Tab:
    case GDK_ISO_Left_Tab:
      {
        /* The deck, the suit stacks, the row stacks, and round again. */
        gint area = IS_TABLEAU (cur.pile) ? 2 : IS_FOUNDATION (cur.pile) ? 1 : 0;

        area = (area + ((state_mask & GDK_SHIFT_MASK)
                        || keyval == GDK_ISO_Left_Tab ? 2 : 1)) % 3;
        cursor_move (area == 0 ? STOCK : area == 1 ? FOUNDATION : TABLEAU);
        break;
      }
    case GDK_Left:
    case GDK_KP_Left:
      if (c >= 0)
        cursor_move (TABLEAU + (c + 6) % 7);
      else
        cursor_move (top_row[(top_place (cur.pile) + 5) % 6]);
      break;
    case GDK_Right:
    case GDK_KP_Right:
      if (c >= 0)
        cursor_move (TABLEAU + (c + 1) % 7);
      else
        cursor_move (top_row[(top_place (cur.pile) + 1) % 6]);
      break;
    case GDK_Up:
    case GDK_KP_Up:
      if (c >= 0)
        {
          const Pile *pile = &state.piles[cur.pile];

          cursor_fix ();
          if (cur.index > 0 && IS_UP (pile->cards[cur.index - 1]))
            cur.index--;
          else
            cursor_move (over_column (c));
        }
      break;
    case GDK_Down:
    case GDK_KP_Down:
      if (c >= 0)
        {
          cursor_fix ();
          if (cur.index < state.piles[cur.pile].n - 1)
            cur.index++;
        }
      else
        cursor_move (TABLEAU + under_top[top_place (cur.pile)]);
      break;
    case GDK_space:
    case GDK_Return:
    case GDK_KP_Enter:
      if (!cur.shown)
        break;
      cursor_act ();
      return TRUE;
    default:
      return FALSE;
    }
  return TRUE;
}

static gint
key_press (GtkWidget *widget, GdkEventKey *event, gpointer data)
{
  (void) widget;
  (void) data;
  if (cascade.active)
    {
      cascade_finished ();
      return TRUE;
    }
  if (event->keyval == GDK_Escape && (drag.pressed || sel_pile >= 0))
    {
      drag_cancel ();
      sel_pile = -1;
      redraw ();
      return TRUE;
    }
  if (won || drag.pressed
      || (event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK)))
    return FALSE;
  if (cursor_key (event->keyval, event->state))
    {
      cur.shown = TRUE;
      redraw ();
      return TRUE;
    }
  return FALSE;
}

/* Minimizing the window stops the clock. */
static gint
window_unmap (GtkWidget *widget, GdkEvent *event, gpointer data)
{
  (void) widget;
  (void) event;
  if (games_clock_running (clk))
    {
      games_clock_stop (clk);
      *(gboolean *) data = TRUE;
    }
  return FALSE;
}

static gint
window_map (GtkWidget *widget, GdkEvent *event, gpointer data)
{
  (void) widget;
  (void) event;
  if (*(gboolean *) data)
    {
      *(gboolean *) data = FALSE;
      games_clock_start (clk);
    }
  return FALSE;
}

/* Settings. */

static void
save_options (void)
{
  games_config_set_int (config, "draw", opts.draw);
  games_config_set_string (config, "scoring",
                           opts.scoring == SCORING_VEGAS ? "vegas"
                           : opts.scoring == SCORING_NONE ? "none"
                           : "standard");
  games_config_set_int (config, "timed", opts.timed);
  games_config_set_int (config, "statusbar", opts.statusbar);
  games_config_set_int (config, "outline", opts.outline);
  games_config_set_int (config, "cumulative", opts.cumulative);
  games_config_set_int (config, "back", opts.back);
  games_config_save (config);
}

static void
load_options (void)
{
  const gchar *scoring = games_config_get_string (config, "scoring",
                                                  "standard");

  opts.draw = games_config_get_int (config, "draw", 3) == 1 ? 1 : 3;
  opts.scoring = !strcmp (scoring, "vegas") ? SCORING_VEGAS
    : !strcmp (scoring, "none") ? SCORING_NONE : SCORING_STANDARD;
  opts.timed = games_config_get_int (config, "timed", 1) != 0;
  opts.statusbar = games_config_get_int (config, "statusbar", 1) != 0;
  opts.outline = games_config_get_int (config, "outline", 0) != 0;
  opts.cumulative = games_config_get_int (config, "cumulative", 0) != 0;
  opts.back = games_config_get_int (config, "back", 0);
  if (opts.back < 0 || opts.back >= GAMES_CARDS_N_BACKS)
    opts.back = 0;
}

/* The options dialog. */

static struct
{
  GtkWidget *draw_one;
  GtkWidget *scoring[3];
  GtkWidget *timed, *statusbar, *outline, *cumulative;
} od;

static gboolean
active (GtkWidget *toggle)
{
  return gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (toggle));
}

static void
scoring_toggled (GtkWidget *widget, gpointer data)
{
  (void) widget;
  (void) data;
  gtk_widget_set_sensitive (od.cumulative, active (od.scoring[SCORING_VEGAS]));
}

static void
options_ok (GtkWidget *widget, gpointer data)
{
  gint draw = active (od.draw_one) ? 1 : 3, scoring = SCORING_STANDARD, i;
  gboolean timed = active (od.timed);
  gboolean redeal;

  (void) widget;
  (void) data;
  for (i = 0; i < 3; i++)
    if (active (od.scoring[i]))
      scoring = i;

  /* Rules that change the game start a new one. */
  redeal = draw != opts.draw || scoring != opts.scoring
    || timed != opts.timed;
  if (scoring != SCORING_VEGAS)
    vegas_played = FALSE;
  opts.draw = draw;
  opts.scoring = scoring;
  opts.timed = timed;
  opts.statusbar = active (od.statusbar);
  opts.outline = active (od.outline);
  opts.cumulative = active (od.cumulative);
  save_options ();

  gtk_widget_destroy (options_dialog);
  show_status ();
  if (redeal)
    deal ();
  else
    redraw ();
}

static GtkWidget *
options_frame (GtkWidget *box, const gchar *title)
{
  GtkWidget *border = gtk_frame_new (title);
  GtkWidget *vbox = gtk_vbox_new (FALSE, 2);

  gtk_container_set_border_width (GTK_CONTAINER (vbox), 6);
  gtk_container_add (GTK_CONTAINER (border), vbox);
  gtk_box_pack_start (GTK_BOX (box), border, TRUE, TRUE, 0);
  return vbox;
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
  static const gchar *const scoring_names[3] = {
    N_("Standard"), N_("Vegas"), N_("None")
  };
  GtkWidget *vbox, *hbox, *fbox, *button, *draw_three;
  gint i;

  if (options_dialog)
    {
      gdk_window_raise (options_dialog->window);
      return;
    }

  options_dialog = games_dialog_new (GTK_WINDOW (window), _("Options"));
  gtk_signal_connect (GTK_OBJECT (options_dialog), "destroy",
                      GTK_SIGNAL_FUNC (gtk_widget_destroyed), &options_dialog);

  vbox = gtk_vbox_new (FALSE, 8);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 8);
  hbox = gtk_hbox_new (TRUE, 8);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, TRUE, TRUE, 0);

  fbox = options_frame (hbox, _("Draw"));
  od.draw_one = gtk_radio_button_new_with_label (NULL, _("Draw One"));
  draw_three = gtk_radio_button_new_with_label
    (gtk_radio_button_group (GTK_RADIO_BUTTON (od.draw_one)), _("Draw Three"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (opts.draw == 1
                                                   ? od.draw_one : draw_three),
                                TRUE);
  gtk_box_pack_start (GTK_BOX (fbox), od.draw_one, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (fbox), draw_three, FALSE, FALSE, 0);

  fbox = options_frame (hbox, _("Scoring"));
  for (i = 0; i < 3; i++)
    {
      od.scoring[i] = gtk_radio_button_new_with_label
        (i ? gtk_radio_button_group (GTK_RADIO_BUTTON (od.scoring[0])) : NULL,
         _(scoring_names[i]));
      gtk_box_pack_start (GTK_BOX (fbox), od.scoring[i], FALSE, FALSE, 0);
    }
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (od.scoring[opts.scoring]),
                                TRUE);

  hbox = gtk_hbox_new (TRUE, 8);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);
  fbox = gtk_vbox_new (FALSE, 2);
  gtk_box_pack_start (GTK_BOX (hbox), fbox, TRUE, TRUE, 0);
  od.timed = check (fbox, _("Timed game"), opts.timed);
  od.statusbar = check (fbox, _("Status bar"), opts.statusbar);
  fbox = gtk_vbox_new (FALSE, 2);
  gtk_box_pack_start (GTK_BOX (hbox), fbox, TRUE, TRUE, 0);
  od.outline = check (fbox, _("Outline dragging"), opts.outline);
  od.cumulative = check (fbox, _("Cumulative score"), opts.cumulative);

  for (i = 0; i < 3; i++)
    gtk_signal_connect (GTK_OBJECT (od.scoring[i]), "toggled",
                        GTK_SIGNAL_FUNC (scoring_toggled), NULL);
  scoring_toggled (NULL, NULL);

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

/* The deck dialog: the backs to pick from, each in a little drawing
 * area, framed when picked. */

#define DECK_PAD 4

static GtkWidget *deck_areas[GAMES_CARDS_N_BACKS];
static gint deck_choice;
/* The cards the dialog shows the backs of, at the usual size whatever
 * the table's. */
static GamesCards *deck_cards;

static gint
deck_expose (GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
  gint back = GPOINTER_TO_INT (data);
  /* At the scale the dialog's cards are drawn for, the window's; GDK
   * makes up the difference if the dialog is on a screen of another. */
  gint s = (gint) games_cards_scale (deck_cards), cw, ch;
  gint w = (card_width + 2 * DECK_PAD) * s, h = (card_height + 2 * DECK_PAD) * s;
  GdkColor *bg = &widget->style->bg[back == deck_choice ? GTK_STATE_SELECTED
                                    : GTK_STATE_NORMAL];
  const guchar *src = games_cards_back (deck_cards, back);
  guchar *buf = g_malloc ((gsize) w * h * 3);
  gint x, y;

  (void) event;
  games_cards_pixel_size (deck_cards, &cw, &ch);
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      {
        guchar *d = buf + ((gsize) y * w + x) * 3;
        gint cx = x - DECK_PAD * s, cy = y - DECK_PAD * s;

        d[0] = (guchar) (bg->red >> 8);
        d[1] = (guchar) (bg->green >> 8);
        d[2] = (guchar) (bg->blue >> 8);
        if (cx >= 0 && cy >= 0 && cx < cw && cy < ch)
          {
            const guchar *p = src + ((gsize) cy * cw + cx) * 4;
            guint a = p[3], k;

            for (k = 0; k < 3; k++)
              d[k] = (guchar) ((p[k] * a + d[k] * (255 - a) + 127) / 255);
          }
      }
  gdk_draw_rgb_image_scaled (widget->window, widget->style->black_gc, 0, 0,
                             card_width + 2 * DECK_PAD,
                             card_height + 2 * DECK_PAD, s,
                             GDK_RGB_DITHER_NONE, buf, w * 3);
  g_free (buf);
  return TRUE;
}

static void
deck_ok (GtkWidget *widget, gpointer data)
{
  (void) widget;
  (void) data;
  opts.back = deck_choice;
  save_options ();
  gtk_widget_destroy (deck_dialog);
  redraw ();
}

static gint
deck_press (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
  gint i;

  (void) widget;
  if (event->button != 1)
    return TRUE;
  deck_choice = GPOINTER_TO_INT (data);
  for (i = 0; i < GAMES_CARDS_N_BACKS; i++)
    gtk_widget_queue_draw (deck_areas[i]);
  /* A double click picks the back and closes the dialog. */
  if (event->type == GDK_2BUTTON_PRESS)
    deck_ok (NULL, NULL);
  return TRUE;
}

static void
deck_destroyed (GtkWidget *widget, gpointer data)
{
  (void) widget;
  (void) data;
  games_cards_free (deck_cards);
  deck_cards = NULL;
}

static void
deck (void)
{
  GtkWidget *grid, *button;
  gint i;

  if (deck_dialog)
    {
      gdk_window_raise (deck_dialog->window);
      return;
    }

  deck_cards = games_cards_new (device);
  if (!deck_cards)
    return;
  deck_choice = opts.back;
  deck_dialog = games_dialog_new (GTK_WINDOW (window), _("Select Card Back"));
  gtk_signal_connect (GTK_OBJECT (deck_dialog), "destroy",
                      GTK_SIGNAL_FUNC (gtk_widget_destroyed), &deck_dialog);
  gtk_signal_connect (GTK_OBJECT (deck_dialog), "destroy",
                      GTK_SIGNAL_FUNC (deck_destroyed), NULL);

  /* Six to a row, in two rows, as Windows' were. */
  grid = gtk_table_new (GAMES_CARDS_N_BACKS / GAMES_CARDS_BACKS_PER_ROW,
                        GAMES_CARDS_BACKS_PER_ROW, TRUE);
  gtk_container_set_border_width (GTK_CONTAINER (grid), 8);
  gtk_table_set_row_spacings (GTK_TABLE (grid), 4);
  gtk_table_set_col_spacings (GTK_TABLE (grid), 4);
  for (i = 0; i < GAMES_CARDS_N_BACKS; i++)
    {
      GtkWidget *area = gtk_drawing_area_new ();

      deck_areas[i] = area;
      gtk_drawing_area_size (GTK_DRAWING_AREA (area),
                             card_width + 2 * DECK_PAD,
                             card_height + 2 * DECK_PAD);
      gtk_widget_set_events (area, GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK);
      gtk_signal_connect (GTK_OBJECT (area), "expose_event",
                          GTK_SIGNAL_FUNC (deck_expose), GINT_TO_POINTER (i));
      gtk_signal_connect (GTK_OBJECT (area), "button_press_event",
                          GTK_SIGNAL_FUNC (deck_press), GINT_TO_POINTER (i));
      gtk_table_attach (GTK_TABLE (grid), area,
                        (guint) (i % GAMES_CARDS_BACKS_PER_ROW),
                        (guint) (i % GAMES_CARDS_BACKS_PER_ROW + 1),
                        (guint) (i / GAMES_CARDS_BACKS_PER_ROW),
                        (guint) (i / GAMES_CARDS_BACKS_PER_ROW + 1), 0, 0, 0, 0);
    }
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (deck_dialog)->vbox), grid,
                      TRUE, TRUE, 0);

  button = games_dialog_add_button (deck_dialog, _("OK"));
  gtk_signal_connect (GTK_OBJECT (button), "clicked",
                      GTK_SIGNAL_FUNC (deck_ok), NULL);
  button = games_dialog_add_button (deck_dialog, _("Cancel"));
  gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
                             GTK_SIGNAL_FUNC (gtk_widget_destroy),
                             GTK_OBJECT (deck_dialog));

  gtk_widget_show_all (deck_dialog);
}

static void
about (void)
{
  games_about (GTK_WINDOW (window), "cgsolitaire",
               _("Classic GUI Solitaire"),
               "Copyright (C) 2026 The CGTK authors", NULL, NULL);
}

/* The window. */

enum { DEAL, UNDO, DECK, OPTIONS, QUIT_GAME, ABOUT };

static void
menu_callback (gpointer data, guint action, GtkWidget *widget)
{
  (void) data;
  (void) widget;
  switch (action)
    {
    case DEAL:
      deal ();
      break;
    case UNDO:
      undo ();
      break;
    case DECK:
      deck ();
      break;
    case OPTIONS:
      options ();
      break;
    case QUIT_GAME:
      gtk_main_quit ();
      break;
    case ABOUT:
      about ();
      break;
    }
}

static GtkItemFactoryEntry menu_items[] = {
  { N_("/_Game"), NULL, NULL, 0, "<Branch>" },
  { N_("/Game/_Deal"), "F2", GTK_SIGNAL_FUNC (menu_callback), DEAL, NULL },
  { "/Game/sep1", NULL, NULL, 0, "<Separator>" },
  { N_("/Game/_Undo"), "<control>Z", GTK_SIGNAL_FUNC (menu_callback), UNDO,
    NULL },
  { N_("/Game/De_ck..."), NULL, GTK_SIGNAL_FUNC (menu_callback), DECK, NULL },
  { N_("/Game/O_ptions..."), NULL, GTK_SIGNAL_FUNC (menu_callback), OPTIONS,
    NULL },
  { "/Game/sep2", NULL, NULL, 0, "<Separator>" },
  { N_("/Game/E_xit"), "<control>Q", GTK_SIGNAL_FUNC (menu_callback),
    QUIT_GAME, NULL },
  { N_("/_Help"), NULL, NULL, 0, "<Branch>" },
  { N_("/Help/_About Solitaire..."), NULL, GTK_SIGNAL_FUNC (menu_callback),
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
  gtk_main_quit ();
  return TRUE;
}

int
main (int argc, char *argv[])
{
  static gboolean paused;
  GtkWidget *vbox, *hbox, *label;
  GtkAccelGroup *accel_group;
  GtkItemFactory *factory;

  gtk_set_locale ();
  games_init_translation ();
  gtk_init (&argc, &argv);

  config = games_config_load (APPNAME);
  games_rand_seed (games_rand_new_seed ());
  load_options ();

  /* For scale 1 until the window is shown, when frame_resize sees the
   * scale it is drawn at; the card size is the same at any. */
  cards = games_cards_new (1);
  if (!cards)
    {
      fprintf (stderr, _("%s: the card images could not be read\n"), argv[0]);
      return 1;
    }
  games_cards_size (cards, &card_width, &card_height);

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title (GTK_WINDOW (window), _(APPNAME_LONG));
  gtk_window_set_wmclass (GTK_WINDOW (window), APPNAME, "Cgsolitaire");
  gtk_window_set_policy (GTK_WINDOW (window), FALSE, TRUE, FALSE);
  gtk_signal_connect (GTK_OBJECT (window), "delete_event",
                      GTK_SIGNAL_FUNC (delete_event_callback), NULL);
  gtk_signal_connect (GTK_OBJECT (window), "key_press_event",
                      GTK_SIGNAL_FUNC (key_press), NULL);
  gtk_signal_connect (GTK_OBJECT (window), "unmap_event",
                      GTK_SIGNAL_FUNC (window_unmap), &paused);
  gtk_signal_connect (GTK_OBJECT (window), "map_event",
                      GTK_SIGNAL_FUNC (window_map), &paused);

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (window), vbox);

  accel_group = gtk_accel_group_new ();
  factory = gtk_item_factory_new (GTK_TYPE_MENU_BAR, "<main>", accel_group);
  gtk_item_factory_set_translate_func (factory, translate, NULL, NULL);
  gtk_item_factory_create_items (factory, ELEMENTS (menu_items), menu_items,
                                 NULL);
  gtk_window_add_accel_group (GTK_WINDOW (window), accel_group);
  gtk_box_pack_start (GTK_BOX (vbox),
                      gtk_item_factory_get_widget (factory, "<main>"),
                      FALSE, FALSE, 0);
  undo_item = gtk_item_factory_get_widget (factory, "/Game/Undo");

  table = gtk_drawing_area_new ();
  /* The table drawn as small as LEAST_ZOOM of its usual size. */
  gtk_widget_set_usize (table, LEAST_WIDTH, LEAST_HEIGHT);
  gtk_widget_set_events (table, GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK
                         | GDK_BUTTON_RELEASE_MASK | GDK_BUTTON1_MOTION_MASK
                         | GDK_POINTER_MOTION_HINT_MASK);
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
  gtk_box_pack_start (GTK_BOX (vbox), table, TRUE, TRUE, 0);

  /* The status bar: messages, then the score and the time. */
  status_box = gtk_hbox_new (FALSE, 2);
  statusbar = gtk_statusbar_new ();
  gtk_box_pack_start (GTK_BOX (status_box), statusbar, TRUE, TRUE, 0);

  time_frame = gtk_frame_new (NULL);
  gtk_frame_set_shadow_type (GTK_FRAME (time_frame), GTK_SHADOW_IN);
  hbox = gtk_hbox_new (FALSE, 0);
  label = gtk_label_new (_("Time: "));
  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 4);
  clk = games_clock_new ();
  gtk_box_pack_start (GTK_BOX (hbox), clk, FALSE, FALSE, 4);
  gtk_container_add (GTK_CONTAINER (time_frame), hbox);
  gtk_box_pack_end (GTK_BOX (status_box), time_frame, FALSE, FALSE, 0);

  score_frame = gtk_frame_new (NULL);
  gtk_frame_set_shadow_type (GTK_FRAME (score_frame), GTK_SHADOW_IN);
  score_label = gtk_label_new ("");
  gtk_misc_set_padding (GTK_MISC (score_label), 4, 0);
  gtk_container_add (GTK_CONTAINER (score_frame), score_label);
  gtk_box_pack_end (GTK_BOX (status_box), score_frame, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), status_box, FALSE, FALSE, 0);

  gtk_window_set_default_size (GTK_WINDOW (window),
                               roomy_width (), roomy_height ());

  gtk_widget_show_all (vbox);
  show_status ();
  deal ();
  tick_timeout = gtk_timeout_add (500, tick, NULL);

  games_window_set_icon (window, "cgsolitaire", cgsolitaire_xpm);
  gtk_widget_show (window);
  gtk_main ();

  gtk_timeout_remove (tick_timeout);
  if (cascade.timeout)
    gtk_timeout_remove (cascade.timeout);
  g_free (frame);
  g_free (under);
  games_cards_free (cards);
  games_config_free (config);
  return 0;
}
