/*
 * cghanafuda: the table the games are played on
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
#include <string.h>

#include "chart.h"
#include "table.h"
#include "tatami.h"

#define LIGHT 3                 /* the width of the light round a card */
#define LIGHT_COLOUR 0xffd84a
#define MARK_RADIUS 6           /* a hint's mark, at the card's top edge */
#define MARK_EDGE 0x403000

/* The shadow round a card, so that it stands off the table and off the
 * cards it lies on: its reach, the same on every side, and the darkest
 * it is, at the card's edge. */
#define SHADOW 4
#define SHADOW_DARK 0.45

/* How long the window must keep a size before the cards are drawn again
 * for it, in milliseconds; till then the old ones are shown. */
#define REDRAW_DELAY 150

Hanafuda *cards;

/* The cards drawn smaller, SMALL_W by SMALL_H, made when first wanted
 * at the table's size. */
static Hanafuda *small;

/* The table drawn, as in cgmahjong: the frame is the drawing area's
 * width by height in device pixels, device of them to the toolkit's.
 * The table is laid out in the game's pixels, zoom of the toolkit's and
 * scale of the device's each, zoom the most that fits the game's own
 * table to the window; frame_width by frame_height of them fill it. */
static GtkWidget *area;
static guchar *frame;
static gint width, height, device = 1;
gint frame_width, frame_height;
static double zoom = 1, scale = 1;
static guint relayout_timer;
#define DEVICE_WIDTH (width * device)
#define DEVICE_HEIGHT (height * device)

/* A position in the game's pixels, in the device's. */
#define DEV(v) ((gint) floor ((v) * scale + 0.5))

/* Lays a card of set's RGBA image over the frame at (x, y). */
static void
blit (Hanafuda *set, gint card, gint x, gint y)
{
  const guchar *src = hanafuda_card (set, card);
  gint cw = hanafuda_width (set), w = cw, h = hanafuda_height (set);
  gint sx = 0, sy = 0, i, j;

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
          guint a = s[3];

          if (a == 255)
            memcpy (d, s, 3);
          else if (a)
            {
              d[0] = (guchar) ((s[0] * a + d[0] * (255 - a) + 127) / 255);
              d[1] = (guchar) ((s[1] * a + d[1] * (255 - a) + 127) / 255);
              d[2] = (guchar) ((s[2] * a + d[2] * (255 - a) + 127) / 255);
            }
        }
    }
}

/* A filled rectangle in the device's pixels, x0 to x1 and y0 to y1. */
static void
fill (gint x0, gint y0, gint x1, gint y1, guint32 colour)
{
  gint i, j;

  x0 = MAX (x0, 0);
  y0 = MAX (y0, 0);
  x1 = MIN (x1, DEVICE_WIDTH);
  y1 = MIN (y1, DEVICE_HEIGHT);
  for (j = y0; j < y1; j++)
    for (i = x0; i < x1; i++)
      {
        guchar *d = frame + ((gsize) j * DEVICE_WIDTH + i) * 3;

        d[0] = (guchar) (colour >> 16);
        d[1] = (guchar) (colour >> 8);
        d[2] = (guchar) colour;
      }
}

gint
table_side (void)
{
  return MARGIN + (opts.table == TABLE_TATAMI ? TATAMI_BORDER : 0);
}

/* The tatami, drawn once for the frame's size and scale and kept. */
static guchar *mat;
static gint mat_width, mat_height;
static double mat_scale;

void
table_clear (void)
{
  gsize size = (gsize) DEVICE_WIDTH * DEVICE_HEIGHT * 3;

  if (opts.table != TABLE_TATAMI)
    {
      fill (0, 0, DEVICE_WIDTH, DEVICE_HEIGHT, TABLE_COLOUR);
      return;
    }
  if (!mat || mat_width != DEVICE_WIDTH || mat_height != DEVICE_HEIGHT
      || mat_scale != scale)
    {
      g_free (mat);
      mat = g_malloc (size);
      mat_width = DEVICE_WIDTH;
      mat_height = DEVICE_HEIGHT;
      mat_scale = scale;
      tatami_draw (mat, mat_width, mat_height, scale);
    }
  memcpy (frame, mat, size);
}

/* Darkens what lies round a card of set at (x, y), fading with
 * distance. */
static void
shadow (Hanafuda *set, gint x, gint y)
{
  /* The card's own edges, in whole pixels, as blit lays it. */
  double x0 = DEV (x), y0 = DEV (y);
  double x1 = x0 + hanafuda_width (set), y1 = y0 + hanafuda_height (set);
  double reach = MAX (SHADOW * scale, 2);
  gint i0 = MAX ((gint) floor (x0 - reach), 0);
  gint j0 = MAX ((gint) floor (y0 - reach), 0);
  gint i1 = MIN ((gint) ceil (x1 + reach), DEVICE_WIDTH);
  gint j1 = MIN ((gint) ceil (y1 + reach), DEVICE_HEIGHT);
  gint i, j, k;

  for (j = j0; j < j1; j++)
    for (i = i0; i < i1; i++)
      {
        double px = i + 0.5, py = j + 0.5;
        double dx = px < x0 ? x0 - px : px > x1 ? px - x1 : 0;
        double dy = py < y0 ? y0 - py : py > y1 ? py - y1 : 0;
        double d = sqrt (dx * dx + dy * dy), fade;
        guchar *p;

        /* Under the card, it hides the shadow itself. */
        if (d == 0 || d >= reach)
          continue;
        fade = 1 - d / reach;
        fade = 1 - SHADOW_DARK * fade * fade;
        p = frame + ((gsize) j * DEVICE_WIDTH + i) * 3;
        for (k = 0; k < 3; k++)
          p[k] = (guchar) (p[k] * fade + 0.5);
      }
}

void
table_card (gint card, gint x, gint y)
{
  if (!cards)
    return;
  shadow (cards, x, y);
  blit (cards, card, x, y);
}

void
table_small_card (gint card, gint x, gint y)
{
  gint h = DEV (SMALL_H);

  if (!small || hanafuda_height (small) != h)
    {
      Hanafuda *s = hanafuda_new (hanafuda_sets[opts.set], opts.border, h);

      /* If they can't be read now, the old ones do, or none. */
      if (s)
        {
          hanafuda_free (small);
          small = s;
        }
      if (!small)
        return;
    }
  shadow (small, x, y);
  blit (small, card, x, y);
}

gboolean
table_set_cards (gint set, gint border)
{
  Hanafuda *c = hanafuda_new (hanafuda_sets[set], border,
                              cards ? hanafuda_height (cards) : DEV (CH));

  if (!c)
    return FALSE;
  hanafuda_free (cards);
  cards = c;
  hanafuda_free (small);
  small = NULL;
  opts.set = set;
  opts.border = border;
  table_redraw ();
  return TRUE;
}

/* One card in the pile for each eight, up to four, PILE_REACH / 3
 * apart. */
void
table_pile (gint x, gint y, gint n)
{
  gint depth = MIN (4, (n + 7) / 8), i;

  for (i = depth - 1; i >= 0; i--)
    table_card (KK_NONE, x + i * PILE_REACH / 3, y - i * PILE_REACH / 3);
}

void
table_light (gint x, gint y)
{
  table_light_cards (x, y, x, y);
}

void
table_light_cards (gint x, gint y, gint x_last, gint y_last)
{
  gint x0 = DEV (x), y0 = DEV (y), x1, y1;
  gint l = MAX (DEV (LIGHT), 1);

  if (!cards)
    return;
  x1 = DEV (x_last) + hanafuda_width (cards);
  y1 = DEV (y_last) + hanafuda_height (cards);

  fill (x0 - l, y0 - l, x1 + l, y0, LIGHT_COLOUR);
  fill (x0 - l, y1, x1 + l, y1 + l, LIGHT_COLOUR);
  fill (x0 - l, y0, x0, y1, LIGHT_COLOUR);
  fill (x1, y0, x1 + l, y1, LIGHT_COLOUR);
}

void
table_mark (gint x, gint y)
{
  table_disc (x + CW / 2, y, MARK_RADIUS + 1, MARK_EDGE);
  table_disc (x + CW / 2, y, MARK_RADIUS, LIGHT_COLOUR);
}

void
table_disc (gint x, gint y, gint r, guint32 colour)
{
  double cx = x * scale, cy = y * scale, rr = r * scale;
  gint x0 = MAX ((gint) floor (cx - rr), 0);
  gint y0 = MAX ((gint) floor (cy - rr), 0);
  gint x1 = MIN ((gint) ceil (cx + rr), DEVICE_WIDTH);
  gint y1 = MIN ((gint) ceil (cy + rr), DEVICE_HEIGHT);
  guint c[3];
  gint i, j, k;

  c[0] = (colour >> 16) & 0xff;
  c[1] = (colour >> 8) & 0xff;
  c[2] = colour & 0xff;
  for (j = y0; j < y1; j++)
    for (i = x0; i < x1; i++)
      {
        double dx = i + 0.5 - cx, dy = j + 0.5 - cy;
        double cover = rr + 0.5 - sqrt (dx * dx + dy * dy);
        guchar *d = frame + ((gsize) j * DEVICE_WIDTH + i) * 3;
        guint a;

        if (cover <= 0)
          continue;
        a = cover >= 1 ? 255 : (guint) (cover * 255 + 0.5);
        for (k = 0; k < 3; k++)
          d[k] = (guchar) ((c[k] * a + d[k] * (255 - a) + 127) / 255);
      }
}

void
table_redraw (void)
{
  /* Nothing till the drawing area and the game are both there. */
  if (!frame || !game)
    return;
  game->draw ();
  gtk_widget_queue_draw (area);
  chart_update ();
}

/* Lays the table out at the zoom that fits the window, with the cards
 * drawn again for it if their size has changed. */
static void
fit_cards (void)
{
  double z;
  gint h;

  /* Laid out once there is a game to lay out. */
  if (!game)
    return;
  z = MIN ((double) width / (game->width + 2 * (table_side () - MARGIN)),
           (double) height / game->height);
  h = (gint) floor (CH * z * device + 0.5);

  if (!cards || h != hanafuda_height (cards))
    {
      Hanafuda *c = hanafuda_new (hanafuda_sets[opts.set], opts.border, h);

      /* If the cards can't be read now, the old ones do. */
      if (!c)
        return;
      hanafuda_free (cards);
      cards = c;
    }
  zoom = z;
  scale = z * device;
  frame_width = (gint) (width / zoom);
  frame_height = (gint) (height / zoom);
}

static gint
relayout_timeout (gpointer data)
{
  (void) data;
  relayout_timer = 0;
  fit_cards ();
  table_redraw ();
  return FALSE;
}

void
table_refit (void)
{
  if (!frame)
    return;
  if (relayout_timer)
    gtk_timeout_remove (relayout_timer);
  relayout_timer = 0;
  fit_cards ();
}

/* Fits the table to the frame: at once, or once the window has kept its
 * size a moment, with the cards as they are till then. */
static void
relayout (gboolean at_once)
{
  if (relayout_timer)
    gtk_timeout_remove (relayout_timer);
  relayout_timer = 0;
  if (at_once)
    fit_cards ();
  else
    {
      frame_width = (gint) (width / zoom);
      frame_height = (gint) (height / zoom);
      relayout_timer = gtk_timeout_add (REDRAW_DELAY, relayout_timeout, NULL);
    }
  table_redraw ();
}

/* Brings the frame into line with the drawing area's size and the
 * window's scale, and the table with the frame. */
static void
frame_resize (gint w, gint h)
{
  gint d = MAX (gdk_window_get_scale_factor (area->window), 1);
  gboolean at_once = frame == NULL || d != device;

  if (frame && w == width && h == height && d == device)
    return;
  width = w;
  height = h;
  device = d;
  g_free (frame);
  frame = g_malloc ((gsize) DEVICE_WIDTH * DEVICE_HEIGHT * 3);
  relayout (at_once);
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
  frame_resize (width, height);

  /* GdkRectangle's sizes are unsigned: the area is clipped in ints. */
  x0 = MAX (event->area.x, 0);
  y0 = MAX (event->area.y, 0);
  x1 = MIN (event->area.x + (gint) event->area.width, width);
  y1 = MIN (event->area.y + (gint) event->area.height, height);
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

GtkWidget *
table_new (gint w, gint h)
{
  area = gtk_drawing_area_new ();
  gtk_widget_set_usize (area, w, h);
  gtk_widget_set_events (area, GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK
                         | GDK_POINTER_MOTION_MASK | GDK_LEAVE_NOTIFY_MASK);
  gtk_signal_connect (GTK_OBJECT (area), "configure_event",
                      GTK_SIGNAL_FUNC (table_configure), NULL);
  gtk_signal_connect (GTK_OBJECT (area), "expose_event",
                      GTK_SIGNAL_FUNC (table_expose), NULL);
  return area;
}

gint
table_width (void)
{
  return frame ? width : 0;
}

gint
table_height (void)
{
  return frame ? height : 0;
}

void
table_point (gdouble x, gdouble y, gint *gx, gint *gy)
{
  *gx = (gint) (x / zoom);
  *gy = (gint) (y / zoom);
}

void
table_free (void)
{
  if (relayout_timer)
    gtk_timeout_remove (relayout_timer);
  relayout_timer = 0;
  g_free (frame);
  frame = NULL;
  g_free (mat);
  mat = NULL;
  hanafuda_free (cards);
  cards = NULL;
  hanafuda_free (small);
  small = NULL;
  hanafuda_forget ();
}
