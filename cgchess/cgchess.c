/*
 * cgchess, chess against GNU Chess
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

/* The board is drawn here, and the rules kept by chess.c; the computer's
 * moves come from a chess engine, GNU Chess by default, spoken to in UCI
 * by engine.c. The whole game is sent with each question, so taking
 * moves back, and hints, need nothing of the engine but an answer. */

#include "config.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include <gtk/gtk.h>

#include "chess.h"
#include "engine.h"
#include "games.h"
#include "games-svg.h"
#include "pieces.h"

#include "icons/cgchess.xpm"

#define APPNAME "cgchess"
#define APPNAME_LONG N_("Chess")

#define ELEMENTS(x) (sizeof (x) / sizeof (x [0]))

/* The table, in the game's pixels: the board's squares, and the margin
 * round it, wide enough for the coordinates, drawn as large as the
 * window allows. */
#define SQ 60
#define MARGIN 22
#define BOARD (8 * SQ)
#define TABLE_SIZE (BOARD + 2 * MARGIN)

/* The boards: their squares and the table round them, and the colours
 * laid over the squares of the last move, the piece chosen and a hint,
 * each to show on that board. */
static const struct
{
  const gchar *id;              /* as stored */
  const gchar *name;            /* as shown */
  guint32 table, light, dark, last, select, hint;
} boards[] = {
  { "wood", N_("Wood"),
    0x3b2f25, 0xf0d9b5, 0xb58863, 0xd8c84a, 0x6f9a3a, 0x4a7fc0 },
  { "green", N_("Green"),
    0x30402a, 0xeeeed2, 0x769656, 0xf0e040, 0x3a7ca5, 0x9b59b6 },
  { "blue", N_("Blue"),
    0x2c3640, 0xdee3e6, 0x8ca2ad, 0xe8d23c, 0x6f9a3a, 0x9b59b6 },
  { "grey", N_("Grey"),
    0x353535, 0xe2e2e2, 0xa8a8a8, 0xe0c83a, 0x6f9a3a, 0x4a7fc0 },
  { "xboard", N_("XBoard"),
    0x2f3a22, 0xc8c365, 0x77a26d, 0xe07a30, 0x3a7ca5, 0x9b59b6 },
};
#define N_BOARDS ((gint) ELEMENTS (boards))
#define BOARD_COLOURS (&boards[opts.board])
#define CHECK_COLOUR 0xd23a2a   /* over a king in check */
#define DOT_COLOUR 0x1e1e1e     /* the squares a piece can go to */

/* The table's size at first, and the least it can be, in the toolkit's
 * pixels; after that it starts at the size it was left at. */
#define FIRST_SIZE 600
#define MIN_SIZE 300

/* How long the window must keep a size before the pieces are drawn
 * again for it, in milliseconds; till then the old ones are shown. */
#define REDRAW_DELAY 150

/* How the computer plays: it searches to a depth, in half-moves, or
 * for a time, and an engine that can play weaker, as Stockfish can,
 * plays at a skill out of 100. Each engine makes its own of a depth, so
 * they are set apart from the times. */
enum { THINK_DEPTH, THINK_TIME };
#define MOST_DEPTH 40
#define MOST_SECONDS 120
#define HINT_LIMITS "movetime 1000"

/* The levels there were before, as the depth, or time, and skill they
 * stand for, for an options file that has one. */
static const struct
{
  gint think, amount, skill;
} old_levels[] = {
  { THINK_DEPTH, 1, 0 },
  { THINK_DEPTH, 2, 15 },
  { THINK_DEPTH, 4, 40 },
  { THINK_TIME, 1, 100 },
  { THINK_TIME, 5, 100 },
  { THINK_TIME, 15, 100 },
};

/* The engines offered by name in Options, and looked for when none is
 * chosen, the first found used: the program, its command, its name,
 * and whether it can be set to play weaker. */
static const struct
{
  const gchar *program, *command, *name;
  gboolean weakens;
} known_engines[] = {
  { "gnuchess", "gnuchess --uci", N_("GNU Chess"), FALSE },
  { "stockfish", "stockfish", N_("Stockfish"), TRUE },
};
#define N_KNOWN ((gint) ELEMENTS (known_engines))

/* The clocks: the minutes each side has for the game, or none. */
static const gint clock_minutes[] = { 0, 5, 10, 15, 30 };
#define N_CLOCKS ((gint) ELEMENTS (clock_minutes))

static GtkWidget *window, *table, *statusbar, *turn_label, *move_label;
static GtkWidget *side_panel, *move_list, *clock_label[2];
static GamesConfig *config;
static Pieces *pieces;

static struct
{
  gint set;                     /* pieces_sets[set] */
  gint board;                   /* boards[board] */
  gint side;                    /* the player's, from the next game */
  gint think;                   /* THINK_DEPTH or THINK_TIME */
  gint depth;                   /* in half-moves */
  gint seconds;                 /* a move */
  gint skill;                   /* out of 100 */
  gboolean coords;              /* the files and ranks round the board */
  gboolean moves;               /* the moves listed beside it */
  gint clock;                   /* clock_minutes[clock], from the next game */
} opts;

/* The game: its positions, the first the start and the last now, and
 * the moves between them. */
#define MOST_PLIES 1024
static ChessPosition history[MOST_PLIES + 1];
static ChessMove played[MOST_PLIES];
static gint plies;
static gchar *start_fen;        /* where the game began, if not the start */
static const gchar *result = "*";       /* as PGN writes it */
static gint human;              /* the player's side in this game */
static gboolean over;
static gboolean flipped;        /* the board turned from the player's view */

static gint selected = -1;      /* the square of the piece chosen */
static ChessMove hint;
static gboolean hint_shown;

static Engine *engine;
static gboolean engine_gone;    /* both sides played by hand */
static enum { ASK_NONE, ASK_MOVE, ASK_HINT } asked;

static struct
{
  gboolean pressed, active;
  gint from;                    /* the square the piece is taken from */
  gint x, y;                    /* the pointer, in the game's pixels */
  gint dx, dy;                  /* from it to the piece's corner */
  gint to_x, to_y;              /* where the pointer has gone since */
  guint idle;
} drag;

/* While a piece is dragged, the board under it, for putting back where
 * it has been. */
static guchar *under;

static void redraw (void);
static void new_game (void);
static void after_move (void);

#define NOW (&history[plies])

static void
message (const gchar *text)
{
  games_flash (GTK_STATUSBAR (statusbar), text);
}

/* The table drawn, as in cgmahjong: the frame is the drawing area's
 * width by height in device pixels, device of them to the toolkit's.
 * The table is laid out in the game's pixels, zoom of the toolkit's and
 * scale of the device's each, zoom the most that fits the table to the
 * window; frame_width by frame_height of them fill it. */
static guchar *frame;
static gint width, height, device = 1;
static gint frame_width = TABLE_SIZE, frame_height = TABLE_SIZE;
static double zoom = 1, scale = 1;
static guint relayout_timer;
#define DEVICE_WIDTH (width * device)
#define DEVICE_HEIGHT (height * device)
#define DEV(v) ((gint) floor ((v) * scale + 0.5))

/* Drawing. */

static void
fill (gint x0, gint y0, gint x1, gint y1, guint32 colour, guint alpha)
{
  gint i, j, k;

  x0 = MAX (x0, 0);
  y0 = MAX (y0, 0);
  x1 = MIN (x1, DEVICE_WIDTH);
  y1 = MIN (y1, DEVICE_HEIGHT);
  for (j = y0; j < y1; j++)
    for (i = x0; i < x1; i++)
      {
        guchar *d = frame + ((gsize) j * DEVICE_WIDTH + i) * 3;

        for (k = 0; k < 3; k++)
          {
            guint c = (colour >> (16 - 8 * k)) & 0xff;

            d[k] = (guchar) ((c * alpha + d[k] * (255 - alpha) + 127) / 255);
          }
      }
}

/* A round disc of radius r about (x, y) in the game's pixels. */
static void
disc (double x, double y, double r, guint32 colour, guint alpha)
{
  double cx = x * scale, cy = y * scale, rr = r * scale;
  gint x0 = MAX ((gint) floor (cx - rr), 0), y0 = MAX ((gint) floor (cy - rr), 0);
  gint x1 = MIN ((gint) ceil (cx + rr), DEVICE_WIDTH);
  gint y1 = MIN ((gint) ceil (cy + rr), DEVICE_HEIGHT);

  gint i, j, k;

  for (j = y0; j < y1; j++)
    for (i = x0; i < x1; i++)
      {
        double dx = i + 0.5 - cx, dy = j + 0.5 - cy;
        double cover = rr + 0.5 - sqrt (dx * dx + dy * dy);
        guchar *d = frame + ((gsize) j * DEVICE_WIDTH + i) * 3;
        guint a;

        if (cover <= 0)
          continue;
        a = (guint) (alpha * MIN (cover, 1.0) + 0.5);
        for (k = 0; k < 3; k++)
          {
            guint c = (colour >> (16 - 8 * k)) & 0xff;

            d[k] = (guchar) ((c * a + d[k] * (255 - a) + 127) / 255);
          }
      }
}

/* Lays a piece over the frame with its corner at (x, y). */
static void
blit (gint piece, gint x, gint y)
{
  const guchar *src = pieces_get (pieces, piece);
  gint size = pieces_size (pieces), w = size, h = size, sx = 0, sy = 0;
  gint i, j;

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
      const guchar *s = src + ((gsize) (sy + j) * size + sx) * 4;
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

/* Where things are. */

/* Whether the board is drawn with black at the bottom. */
static gboolean
black_below (void)
{
  return (human == CHESS_BLACK) != flipped;
}

static void
square_xy (gint s, gint *x, gint *y)
{
  gint f = CHESS_FILE (s), r = CHESS_RANK (s);
  gint left = (frame_width - BOARD) / 2, top = (frame_height - BOARD) / 2;

  if (black_below ())
    {
      f = 7 - f;
      r = 7 - r;
    }
  *x = left + f * SQ;
  *y = top + (7 - r) * SQ;
}

/* The square at (x, y), or -1 off the board. */
static gint
square_at (gint x, gint y)
{
  gint left = (frame_width - BOARD) / 2, top = (frame_height - BOARD) / 2;
  gint f, r;

  if (x < left || y < top || x >= left + BOARD || y >= top + BOARD)
    return -1;
  f = (x - left) / SQ;
  r = 7 - (y - top) / SQ;
  if (black_below ())
    {
      f = 7 - f;
      r = 7 - r;
    }
  return CHESS_SQUARE (f, r);
}

static void
tint (gint s, guint32 colour, guint alpha)
{
  gint x, y;

  square_xy (s, &x, &y);
  fill (DEV (x), DEV (y), DEV (x + SQ), DEV (y + SQ), colour, alpha);
}

/* The coordinates' letters and figures, a to h and 1 to 8, drawn as
 * strokes in cells GLYPH_W by GLYPH_H of their own units, the baseline
 * at GLYPH_BASE; GLYPH_UNIT game pixels to a unit. */
static const gchar glyph_svg[] =
  "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"192\" height=\"20\"><g fill=\"none\" stroke=\"#000\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
  "<path transform=\"translate(1 1)\" d=\"M2.4 6.3 C3.6 4.6 7.9 4.6 8 7.4 L8 14 M8 9.4 C4.6 9.2 2 9.9 2 11.9 C2 14.6 6.6 14.6 8 12.4\"/>"
  "<path transform=\"translate(13 1)\" d=\"M2 0.5 L2 14 M2 9.5 C2 4.3 8.6 4.3 8.6 9.5 C8.6 14.7 2 14.7 2 9.5\"/>"
  "<path transform=\"translate(25 1)\" d=\"M8.3 6.6 C6.6 4.4 1.9 4.6 1.9 9.5 C1.9 14.4 6.6 14.6 8.3 12.4\"/>"
  "<path transform=\"translate(37 1)\" d=\"M8.4 0.5 L8.4 14 M8.4 9.5 C8.4 4.3 1.8 4.3 1.8 9.5 C1.8 14.7 8.4 14.7 8.4 9.5\"/>"
  "<path transform=\"translate(49 1)\" d=\"M2 9.6 L8.4 9.6 C8.4 4.4 2 4.2 2 9.5 C2 14.6 6.8 14.8 8.2 12.6\"/>"
  "<path transform=\"translate(61 1)\" d=\"M8 1.2 C5.6 -0.1 3.8 0.9 3.8 3.6 L3.8 14 M1.2 5.6 L7.4 5.6\"/>"
  "<path transform=\"translate(73 1)\" d=\"M8.4 5.2 L8.4 14.8 C8.4 18.6 3.4 18.6 2.2 16.4 M8.4 9.3 C8.4 4.2 1.8 4.2 1.8 9.3 C1.8 14.4 8.4 14.4 8.4 9.3\"/>"
  "<path transform=\"translate(85 1)\" d=\"M2 0.5 L2 14 M2 9 C2 4 8.4 4 8.4 8.2 L8.4 14\"/>"
  "<path transform=\"translate(97 1)\" d=\"M2.6 3.2 L5.8 0.8 L5.8 14 M2.8 14 L8.8 14\"/>"
  "<path transform=\"translate(109 1)\" d=\"M1.9 3.6 C2.6 0 8.4 0 8.4 3.9 C8.4 7.3 2.2 10 1.9 14 L8.6 14\"/>"
  "<path transform=\"translate(121 1)\" d=\"M2.2 1 L8.2 1 L4.6 5.7 C9.4 5.5 9.6 14 4.8 14 C3.2 14 2.2 13.4 1.6 12.4\"/>"
  "<path transform=\"translate(133 1)\" d=\"M6.6 14 L6.6 0.8 L1.2 10.1 L9.2 10.1\"/>"
  "<path transform=\"translate(145 1)\" d=\"M8.2 1 L2.8 1 L2.3 6.6 C5.1 4.7 8.8 6 8.8 9.7 C8.8 14.6 3.1 14.9 1.7 12.6\"/>"
  "<path transform=\"translate(157 1)\" d=\"M7.9 1.6 C4.2 -0.6 1.7 3.1 1.7 8.6 C1.7 15 8.6 15 8.6 10.1 C8.6 5.4 2 5.4 1.8 9.7\"/>"
  "<path transform=\"translate(169 1)\" d=\"M1.5 1 L8.7 1 L4.2 14\"/>"
  "<path transform=\"translate(181 1)\" d=\"M5.1 7.2 C1.6 7.2 1.6 0.6 5.1 0.6 C8.6 0.6 8.6 7.2 5.1 7.2 C1.1 7.2 1.1 14.2 5.1 14.2 C9.1 14.2 9.1 7.2 5.1 7.2\"/></g></svg>";
#define GLYPH_W 12
#define GLYPH_H 20
#define GLYPH_BASE 15
#define GLYPH_UNIT 0.8
#define N_GLYPHS 16

static GamesSvg *glyphs;
static guchar *glyph_sheet;     /* all of them, drawn at glyph_scale */
static double glyph_scale;
static gint glyph_sheet_w, glyph_sheet_h;

/* Lays glyph i over the frame, its cell's corner at (x, y) in the
 * game's pixels, in colour. */
static void
draw_glyph (gint i, double x, double y, guint32 colour, guint alpha)
{
  double s = GLYPH_UNIT * scale;
  gint sx0, sx1, dx, dy, a, b, k;

  if (!glyphs)
    glyphs = games_svg_parse (glyph_svg);
  if (!glyphs)
    return;
  if (!glyph_sheet || glyph_scale != s)
    {
      g_free (glyph_sheet);
      glyph_scale = s;
      glyph_sheet_w = (gint) ceil (N_GLYPHS * GLYPH_W * s);
      glyph_sheet_h = (gint) ceil (GLYPH_H * s);
      glyph_sheet = games_svg_render_area (glyphs, 0, 0, s, glyph_sheet_w,
                                           glyph_sheet_h);
    }
  sx0 = (gint) floor (i * GLYPH_W * s + 0.5);
  sx1 = MIN ((gint) floor ((i + 1) * GLYPH_W * s + 0.5), glyph_sheet_w);
  dx = (gint) floor (x * scale + 0.5);
  dy = (gint) floor (y * scale + 0.5);
  for (b = 0; b < glyph_sheet_h; b++)
    for (a = sx0; a < sx1; a++)
      {
        gint fx = dx + a - sx0, fy = dy + b;
        guint al = glyph_sheet[((gsize) b * glyph_sheet_w + a) * 4 + 3];
        guchar *d;

        if (!al || fx < 0 || fy < 0 || fx >= DEVICE_WIDTH || fy >= DEVICE_HEIGHT)
          continue;
        al = al * alpha / 255;
        d = frame + ((gsize) fy * DEVICE_WIDTH + fx) * 3;
        for (k = 0; k < 3; k++)
          {
            guint c = (colour >> (16 - 8 * k)) & 0xff;

            d[k] = (guchar) ((c * al + d[k] * (255 - al) + 127) / 255);
          }
      }
}

/* The files under the board and the ranks at its left, as the board is
 * turned. */
static void
draw_coords (void)
{
  gint left = (frame_width - BOARD) / 2, top = (frame_height - BOARD) / 2;
  double u = GLYPH_UNIT;
  gint i;

  for (i = 0; i < 8; i++)
    {
      gint file = black_below () ? 7 - i : i;
      gint rank = black_below () ? i : 7 - i;

      /* Letters on a baseline in the margin below, figures in the middle
       * of their rank in the margin at the left. */
      draw_glyph (file, left + i * SQ + SQ / 2.0 - GLYPH_W * u / 2,
                  top + BOARD + MARGIN * 0.72 - GLYPH_BASE * u,
                  BOARD_COLOURS->light, 210);
      draw_glyph (8 + rank, left - MARGIN / 2.0 - GLYPH_W * u / 2,
                  top + i * SQ + SQ / 2.0 - 8.4 * u, BOARD_COLOURS->light,
                  210);
    }
}

static void
draw (void)
{
  ChessMove moves[CHESS_MAX_MOVES];
  gint s, x, y, n, i;

  fill (0, 0, DEVICE_WIDTH, DEVICE_HEIGHT, BOARD_COLOURS->table, 255);
  if (opts.coords)
    draw_coords ();

  for (s = 0; s < 64; s++)
    {
      square_xy (s, &x, &y);
      fill (DEV (x), DEV (y), DEV (x + SQ), DEV (y + SQ),
            (CHESS_FILE (s) + CHESS_RANK (s)) & 1 ? BOARD_COLOURS->light
            : BOARD_COLOURS->dark,
            255);
    }
  if (plies > 0)
    {
      tint (played[plies - 1].from, BOARD_COLOURS->last, 110);
      tint (played[plies - 1].to, BOARD_COLOURS->last, 110);
    }
  if (hint_shown)
    {
      tint (hint.from, BOARD_COLOURS->hint, 120);
      tint (hint.to, BOARD_COLOURS->hint, 120);
    }
  if (selected >= 0)
    tint (selected, BOARD_COLOURS->select, 140);
  if (chess_in_check (NOW, NOW->side))
    for (s = 0; s < 64; s++)
      if (NOW->board[s] == (CHESS_KING | (NOW->side ? CHESS_BLACK_PIECE : 0)))
        {
          square_xy (s, &x, &y);
          disc (x + SQ / 2.0, y + SQ / 2.0, SQ * 0.48, CHECK_COLOUR, 150);
        }

  for (s = 0; s < 64; s++)
    if (NOW->board[s] != CHESS_EMPTY && !(drag.active && s == drag.from))
      {
        square_xy (s, &x, &y);
        blit (NOW->board[s], x, y);
      }

  /* Where the piece chosen can go. */
  if (selected >= 0)
    {
      n = chess_moves (NOW, moves);
      for (i = 0; i < n; i++)
        if (moves[i].from == selected && moves[i].promotion <= CHESS_QUEEN
            && (moves[i].promotion == 0 || moves[i].promotion == CHESS_QUEEN))
          {
            square_xy (moves[i].to, &x, &y);
            disc (x + SQ / 2.0, y + SQ / 2.0,
                  NOW->board[moves[i].to] != CHESS_EMPTY ? SQ * 0.42 : SQ * 0.15,
                  DOT_COLOUR, NOW->board[moves[i].to] != CHESS_EMPTY ? 60 : 80);
          }
    }

  /* The board is kept without the piece dragged, to put back under it
   * as it moves. */
  if (drag.active)
    {
      gsize size = (gsize) DEVICE_WIDTH * DEVICE_HEIGHT * 3;

      if (!under)
        under = g_malloc (size);
      memcpy (under, frame, size);
      blit (NOW->board[drag.from], drag.x - drag.dx, drag.y - drag.dy);
    }
}

/* Has an area of the frame, in the game's pixels, shown again. */
static void
queue_area (gint x, gint y, gint w, gint h)
{
  gint x0 = (gint) floor (x * zoom) - 1, y0 = (gint) floor (y * zoom) - 1;
  gint x1 = (gint) ceil ((x + w) * zoom) + 1;
  gint y1 = (gint) ceil ((y + h) * zoom) + 1;

  gtk_widget_queue_draw_area (table, x0, y0, x1 - x0, y1 - y0);
}

/* Moves the piece dragged to where the pointer has gone: only where it
 * was and where it goes are drawn again, and sent to the window. */
static void
move_dragged (void)
{
  gint x = drag.x - drag.dx, y = drag.y - drag.dy, j;
  gint x0 = MAX (DEV (x), 0), y0 = MAX (DEV (y), 0);
  gint x1 = MIN (DEV (x) + pieces_size (pieces), DEVICE_WIDTH);
  gint y1 = MIN (DEV (y) + pieces_size (pieces), DEVICE_HEIGHT);

  if (!under || !frame)
    return;
  for (j = y0; j < y1 && x0 < x1; j++)
    {
      gsize offset = ((gsize) j * DEVICE_WIDTH + x0) * 3;

      memcpy (frame + offset, under + offset, (gsize) (x1 - x0) * 3);
    }
  queue_area (x, y, SQ, SQ);
  drag.x = drag.to_x;
  drag.y = drag.to_y;
  x = drag.x - drag.dx;
  y = drag.y - drag.dy;
  blit (NOW->board[drag.from], x, y);
  queue_area (x, y, SQ, SQ);
}

static gint
drag_idle (gpointer data)
{
  (void) data;
  drag.idle = 0;
  if (drag.active && (drag.to_x != drag.x || drag.to_y != drag.y))
    move_dragged ();
  return FALSE;
}

/* Lets go of a piece dragged. */
static void
drag_end (void)
{
  if (drag.idle)
    gtk_idle_remove (drag.idle);
  drag.idle = 0;
  drag.pressed = drag.active = FALSE;
  g_free (under);
  under = NULL;
}

static void
redraw (void)
{
  if (!frame)
    return;
  draw ();
  gtk_widget_queue_draw (table);
}

/* The window. */

static void
fit_pieces (void)
{
  double z = MIN ((double) width / TABLE_SIZE, (double) height / TABLE_SIZE);
  gint size = (gint) floor (SQ * z * device + 0.5);

  if (size != pieces_size (pieces))
    {
      Pieces *p = pieces_new (pieces_sets[opts.set], size);

      /* If the pieces can't be read now, the old ones do. */
      if (!p)
        return;
      pieces_free (pieces);
      pieces = p;
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
  fit_pieces ();
  redraw ();
  return FALSE;
}

static void
relayout (gboolean at_once)
{
  if (relayout_timer)
    gtk_timeout_remove (relayout_timer);
  relayout_timer = 0;
  if (at_once)
    fit_pieces ();
  else
    {
      frame_width = (gint) (width / zoom);
      frame_height = (gint) (height / zoom);
      relayout_timer = gtk_timeout_add (REDRAW_DELAY, relayout_timeout, NULL);
    }
  redraw ();
}

static void
frame_resize (gint w, gint h)
{
  gint d = MAX (gdk_window_get_scale_factor (table->window), 1);
  gboolean at_once = frame == NULL || d != device;

  if (frame && w == width && h == height && d == device)
    return;
  width = w;
  height = h;
  device = d;
  g_free (frame);
  frame = g_malloc ((gsize) DEVICE_WIDTH * DEVICE_HEIGHT * 3);
  g_free (under);
  under = NULL;
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

/* The moves so far, beside the board: a row to each move, White's and
 * Black's. */
static void
moves_update (void)
{
  gchar san[CHESS_SAN_LEN], number[16];
  gchar *texts[3];
  gint i, row = -1;

  if (!move_list)
    return;
  gtk_clist_freeze (GTK_CLIST (move_list));
  gtk_clist_clear (GTK_CLIST (move_list));
  for (i = 0; i < plies; i++)
    {
      const ChessPosition *pos = &history[i];

      chess_san (pos, played[i], san);
      if (pos->side == CHESS_WHITE || row < 0)
        {
          g_snprintf (number, sizeof number, "%d.", pos->fullmove);
          texts[0] = number;
          texts[1] = pos->side == CHESS_WHITE ? san : "...";
          texts[2] = pos->side == CHESS_WHITE ? "" : san;
          row = gtk_clist_append (GTK_CLIST (move_list), texts);
        }
      else
        gtk_clist_set_text (GTK_CLIST (move_list), row, 2, san);
    }
  gtk_clist_thaw (GTK_CLIST (move_list));
  if (row >= 0)
    gtk_clist_moveto (GTK_CLIST (move_list), row, 0, 1.0, 0.0);
}

/* The clocks: each side's time left, counted down for the side to move
 * from the first move on. */
static gint game_minutes;       /* this game's, or 0 for no clocks */
static gint clock_ms[2];        /* left, when the clock last stopped */
static gint clock_side = -1;    /* the side whose clock runs, or -1 */
static GTimeVal clock_since;    /* when it started */
static gint clock_at[MOST_PLIES + 1][2];        /* both left as each ply began */
static guint clock_timer;

static gint
clock_left (gint side)
{
  GTimeVal now;
  gint ms = clock_ms[side];

  if (side == clock_side)
    {
      g_get_current_time (&now);
      ms -= (gint) ((now.tv_sec - clock_since.tv_sec) * 1000
                    + (now.tv_usec - clock_since.tv_usec) / 1000);
    }
  return ms;
}

static void
clock_show (void)
{
  static const gchar *const names[2] = { N_("White %d:%02d"),
    N_("Black %d:%02d") };
  gchar text[32];
  gint side;

  for (side = 0; side < 2; side++)
    {
      GtkWidget *border = clock_label[side]->parent;
      gint seconds = MAX ((clock_left (side) + 999) / 1000, 0);

      if (!game_minutes)
        {
          gtk_widget_hide (border);
          continue;
        }
      g_snprintf (text, sizeof text, _(names[side]), seconds / 60,
                  seconds % 60);
      gtk_label_set_text (GTK_LABEL (clock_label[side]), text);
      /* The clock waiting is greyed. */
      gtk_widget_set_sensitive (clock_label[side], side == clock_side);
      gtk_widget_show (border);
    }
}

static void
show_status (void)
{
  gchar text[64];

  moves_update ();
  clock_show ();

  gtk_label_set_text (GTK_LABEL (turn_label),
                      over ? _("Game over")
                      : NOW->side == CHESS_WHITE ? _("White to move")
                      : _("Black to move"));
  g_snprintf (text, sizeof text, _("Move %d"), NOW->fullmove);
  gtk_label_set_text (GTK_LABEL (move_label), text);
}

/* The engine. */

static gboolean
computer_to_move (void)
{
  return !over && !engine_gone && NOW->side != human;
}

/* The game so far, as UCI's "position" gives it. */
static gchar *
position_text (void)
{
  GString *s = g_string_new (NULL);
  gchar coord[CHESS_COORD_LEN], *text;
  gint i;

  if (start_fen)
    g_string_sprintfa (s, "fen %s", start_fen);
  else
    g_string_append (s, "startpos");
  if (plies)
    g_string_append (s, " moves");
  for (i = 0; i < plies; i++)
    {
      chess_coord (played[i], coord);
      g_string_sprintfa (s, " %s", coord);
    }
  /* GLib 1.2's g_string_free gives nothing back. */
  text = s->str;
  g_string_free (s, FALSE);
  return text;
}

static void
ask (gint what)
{
  gchar *position = position_text ();
  gchar *limits;

  /* With a clock, the engine is told both sides' time, as in any timed
   * game, and spends its own as it judges; a depth still bounds it, but
   * the seconds a move are for games without a clock. */
  if (game_minutes)
    {
      gint w = MAX (clock_left (CHESS_WHITE), 1);
      gint b = MAX (clock_left (CHESS_BLACK), 1);

      if (opts.think == THINK_DEPTH)
        limits = g_strdup_printf ("depth %d wtime %d btime %d", opts.depth,
                                  w, b);
      else
        limits = g_strdup_printf ("wtime %d btime %d", w, b);
    }
  else if (opts.think == THINK_DEPTH)
    limits = g_strdup_printf ("depth %d", opts.depth);
  else
    limits = g_strdup_printf ("movetime %d", opts.seconds * 1000);
  asked = what;
  engine_set_strength (engine, what == ASK_HINT ? 100 : opts.skill);
  engine_think (engine, position, what == ASK_HINT ? HINT_LIMITS : limits);
  g_free (position);
  g_free (limits);
}

static void play (ChessMove move);

static gint
engine_lost (gpointer data)
{
  (void) data;
  engine_free (engine);
  engine = NULL;
  return FALSE;
}

/* The engine's answer: the computer's move, or a hint. */
static void
answered (const gchar *text, gpointer data)
{
  ChessMove move;
  gint what = asked;

  (void) data;
  asked = ASK_NONE;
  if (!text)
    {
      /* Freed once out of its own callback. */
      engine_gone = TRUE;
      gtk_idle_add (engine_lost, NULL);
      games_message (GTK_WINDOW (window), _(APPNAME_LONG),
                     _("The chess engine could not be started, or has\n"
                       "stopped. Without it, you play both sides.\n\n"
                       "Install GNU Chess or Stockfish, then choose it\n"
                       "in Game > Options, under Engine."));
      message (_("No engine: you play both sides."));
      show_status ();
      return;
    }
  if (!chess_parse_coord (NOW, text, &move))
    {
      gchar *error = g_strdup_printf (_("The engine's move, %s, is not a "
                                        "legal move here."), text);

      games_message (GTK_WINDOW (window), _(APPNAME_LONG), error);
      g_free (error);
      return;
    }
  if (what == ASK_HINT)
    {
      gchar san[CHESS_SAN_LEN], *text2;

      hint = move;
      hint_shown = TRUE;
      chess_san (NOW, move, san);
      text2 = g_strdup_printf (_("Try %s."), san);
      message (text2);
      g_free (text2);
      redraw ();
    }
  else
    play (move);
}

/* Moves. */

static void
game_over_answer (gint answer, gpointer data)
{
  (void) data;
  if (answer == 0)
    new_game ();
}

static void
clock_stop (void)
{
  if (clock_side >= 0)
    clock_ms[clock_side] = clock_left (clock_side);
  clock_side = -1;
  if (clock_timer)
    gtk_timeout_remove (clock_timer);
  clock_timer = 0;
}

static void game_over_dialog (const gchar *text);

/* A side's time has run out: it loses. */
static void
time_up (void)
{
  gint side = clock_side;
  const gchar *text;

  clock_stop ();
  clock_ms[side] = 0;
  if (engine && engine_thinking (engine))
    engine_cancel (engine);
  asked = ASK_NONE;
  over = TRUE;
  result = side == CHESS_WHITE ? "0-1" : "1-0";
  text = engine_gone ? (side == CHESS_WHITE
                        ? _("White's time is up: Black wins.")
                        : _("Black's time is up: White wins."))
    : side == human ? _("Your time is up: the computer wins.")
    : _("The computer's time is up: you win.");
  selected = -1;
  drag_end ();
  show_status ();
  redraw ();
  game_over_dialog (text);
}

static gint
clock_tick (gpointer data)
{
  (void) data;
  if (clock_side >= 0 && clock_left (clock_side) <= 0)
    {
      clock_timer = 0;
      time_up ();
      return FALSE;
    }
  clock_show ();
  return TRUE;
}

/* Runs the clock of the side to move, once the first move is made. */
static void
clock_run (void)
{
  clock_stop ();
  clock_at[plies][CHESS_WHITE] = clock_ms[CHESS_WHITE];
  clock_at[plies][CHESS_BLACK] = clock_ms[CHESS_BLACK];
  if (game_minutes && !over && plies > 0)
    {
      clock_side = NOW->side;
      g_get_current_time (&clock_since);
      clock_timer = gtk_timeout_add (200, clock_tick, NULL);
    }
  clock_show ();
}

static void
game_over_dialog (const gchar *text)
{
  static const gchar *const buttons[] = { N_("Play Again"), N_("Close"),
    NULL };
  const gchar *labels[3];

  labels[0] = _(buttons[0]);
  labels[1] = _(buttons[1]);
  labels[2] = NULL;
  games_choose (GTK_WINDOW (window), _(APPNAME_LONG), text, labels,
                game_over_answer, NULL);
}

/* Says how the game ended, if it has. */
static gboolean
check_over (void)
{
  const gchar *text;

  switch (chess_state (history, plies + 1))
    {
    case CHESS_CHECKMATE:
      result = NOW->side == CHESS_WHITE ? "0-1" : "1-0";
      text = engine_gone ? (NOW->side == CHESS_WHITE
                            ? _("Checkmate: Black wins.")
                            : _("Checkmate: White wins."))
        : NOW->side == human ? _("Checkmate: the computer wins.")
        : _("Checkmate: you win.");
      break;
    case CHESS_STALEMATE:
      text = _("Stalemate: a draw.");
      break;
    case CHESS_FIFTY_MOVES:
      text = _("Fifty moves without a capture or a pawn's move: a draw.");
      break;
    case CHESS_NO_MATERIAL:
      text = _("Neither side can mate: a draw.");
      break;
    case CHESS_REPETITION:
      text = _("The same position three times: a draw.");
      break;
    default:
      return FALSE;
    }
  if (strcmp (result, "*") == 0)
    result = "1/2-1/2";
  over = TRUE;
  clock_stop ();
  show_status ();
  redraw ();
  game_over_dialog (text);
  return TRUE;
}

/* Plays a legal move, by either side. */
static void
play (ChessMove move)
{
  gchar san[CHESS_SAN_LEN], *text;
  gint side = NOW->side;

  if (plies >= MOST_PLIES)
    return;
  chess_san (NOW, move, san);
  history[plies + 1] = history[plies];
  chess_play (&history[plies + 1], move);
  played[plies++] = move;
  selected = -1;
  hint_shown = FALSE;

  text = g_strdup_printf (side == CHESS_WHITE ? _("White plays %s.")
                          : _("Black plays %s."), san);
  message (text);
  g_free (text);
  after_move ();
}

static void
after_move (void)
{
  if (!over)
    clock_run ();
  show_status ();
  redraw ();
  if (over || check_over ())
    return;
  if (computer_to_move ())
    {
      message (_("Thinking..."));
      ask (ASK_MOVE);
    }
}

static ChessMove promoting;

static void
promotion_answer (gint answer, gpointer data)
{
  static const gint kinds[4] = {
    CHESS_QUEEN, CHESS_ROOK, CHESS_BISHOP, CHESS_KNIGHT
  };

  (void) data;
  promoting.promotion = (gint8) kinds[CLAMP (answer, 0, 3)];
  play (promoting);
}

/* The player's move from from to to, if it is one. */
static gboolean
try_move (gint from, gint to)
{
  ChessMove moves[CHESS_MAX_MOVES];
  gint n = chess_moves (NOW, moves), i;

  for (i = 0; i < n; i++)
    if (moves[i].from == from && moves[i].to == to)
      {
        if (moves[i].promotion)
          {
            static const gchar *const buttons[] = {
              N_("Queen"), N_("Rook"), N_("Bishop"), N_("Knight"), NULL
            };
            const gchar *labels[5];
            gint k;

            for (k = 0; k < 4; k++)
              labels[k] = _(buttons[k]);
            labels[4] = NULL;
            promoting = moves[i];
            selected = -1;
            redraw ();
            games_choose (GTK_WINDOW (window), _(APPNAME_LONG),
                          _("What does the pawn become?"), labels,
                          promotion_answer, NULL);
          }
        else
          play (moves[i]);
        return TRUE;
      }
  return FALSE;
}

/* Whether the player may move the piece on s now. */
static gboolean
movable (gint s)
{
  gint p = NOW->board[s];

  return s >= 0 && !over && !computer_to_move () && p != CHESS_EMPTY
    && CHESS_SIDE (p) == NOW->side;
}

static gint
table_press (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
  gint x, y, s;

  (void) widget;
  (void) data;
  if (event->type != GDK_BUTTON_PRESS || event->button != 1)
    return TRUE;
  x = (gint) (event->x / zoom);
  y = (gint) (event->y / zoom);
  s = square_at (x, y);
  if (s < 0)
    return TRUE;
  if (selected >= 0 && s != selected && !movable (s))
    {
      /* A second click: where the piece chosen goes. */
      if (!try_move (selected, s))
        {
          selected = -1;
          redraw ();
        }
      return TRUE;
    }
  if (movable (s))
    {
      gint sx, sy;

      selected = s;
      square_xy (s, &sx, &sy);
      drag.pressed = TRUE;
      drag.active = FALSE;
      drag.from = s;
      drag.x = x;
      drag.y = y;
      drag.dx = x - sx;
      drag.dy = y - sy;
      redraw ();
    }
  return TRUE;
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
       * the piece goes where it is once the events waiting are read. */
      drag.to_x = x;
      drag.to_y = y;
      if (!drag.idle)
        drag.idle = gtk_idle_add_priority (GTK_PRIORITY_REDRAW - 1,
                                           drag_idle, NULL);
      return TRUE;
    }
  /* A drag once the pointer has moved a little; the board is drawn once
   * without the piece, then kept under it. */
  if (ABS (x - drag.x) < 3 && ABS (y - drag.y) < 3)
    return TRUE;
  drag.active = TRUE;
  drag.x = drag.to_x = x;
  drag.y = drag.to_y = y;
  /* The piece is held by its middle, as a hand would hold it. */
  drag.dx = drag.dy = SQ / 2;
  redraw ();
  return TRUE;
}

static gint
table_release (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
  gint s;

  (void) widget;
  (void) data;
  if (event->button != 1 || !drag.pressed)
    return TRUE;
  if (!drag.active)
    {
      drag_end ();
      return TRUE;
    }
  drag_end ();
  s = square_at ((gint) (event->x / zoom), (gint) (event->y / zoom));
  /* Dropped on its own square, it stays chosen for a click. */
  if (s != drag.from && !(s >= 0 && try_move (drag.from, s)))
    selected = -1;
  redraw ();
  return TRUE;
}

/* The game. */

static void
new_game (void)
{
  if (engine)
    engine_cancel (engine);
  asked = ASK_NONE;
  plies = 0;
  chess_start (&history[0]);
  g_free (start_fen);
  start_fen = NULL;
  result = "*";
  clock_stop ();
  game_minutes = clock_minutes[opts.clock];
  clock_ms[CHESS_WHITE] = clock_ms[CHESS_BLACK] = game_minutes * 60000;
  clock_at[0][CHESS_WHITE] = clock_at[0][CHESS_BLACK] = game_minutes * 60000;
  human = opts.side;
  over = FALSE;
  selected = -1;
  hint_shown = FALSE;
  drag_end ();
  message (engine_gone ? _("No engine: you play both sides.")
           : human == CHESS_WHITE ? _("You play White.")
           : _("You play Black."));
  after_move ();
}

/* Takes back the player's last move, and the computer's after it. */
static void
undo (void)
{
  gint back = 1;

  if (plies == 0)
    return;
  if (engine && engine_thinking (engine))
    engine_cancel (engine);
  asked = ASK_NONE;
  if (!engine_gone)
    {
      /* Back to the player's move before. */
      back = NOW->side == human ? 2 : 1;
      if (back > plies)
        back = plies;
    }
  plies -= back;
  over = FALSE;
  result = "*";

  /* The clocks go back with the moves, as they were when the position
   * came about; so a game lost on time can be taken back too. */
  clock_stop ();
  clock_ms[CHESS_WHITE] = clock_at[plies][CHESS_WHITE];
  clock_ms[CHESS_BLACK] = clock_at[plies][CHESS_BLACK];
  selected = -1;
  hint_shown = FALSE;
  message (_("Move taken back."));
  after_move ();
}

static void
ask_hint (void)
{
  if (over || computer_to_move () || engine_gone || !engine)
    return;
  message (_("Thinking of a move for you..."));
  ask (ASK_HINT);
}

/* Games kept as PGN, the files chess programs share. */

static const gchar *
player_name (gint side)
{
  return engine_gone || side == human ? g_get_user_name () : "Computer";
}

/* The game so far in PGN: its tags, then its moves in lines of no more
 * than 79 characters, ending with the result. */
static gchar *
pgn_text (void)
{
  GString *s = g_string_new (NULL);
  gchar date[16], san[CHESS_SAN_LEN], token[32], *text;
  time_t now = time (NULL);
  gint i, column = 0;

  strftime (date, sizeof date, "%Y.%m.%d", localtime (&now));
  g_string_sprintfa (s, "[Event \"Casual game\"]\n[Site \"?\"]\n"
                     "[Date \"%s\"]\n[Round \"-\"]\n[White \"%s\"]\n"
                     "[Black \"%s\"]\n[Result \"%s\"]\n", date,
                     player_name (CHESS_WHITE), player_name (CHESS_BLACK),
                     result);
  if (start_fen)
    g_string_sprintfa (s, "[SetUp \"1\"]\n[FEN \"%s\"]\n", start_fen);
  g_string_append_c (s, '\n');
  for (i = 0; i <= plies; i++)
    {
      gint len;

      if (i < plies)
        {
          const ChessPosition *pos = &history[i];

          chess_san (pos, played[i], san);
          if (pos->side == CHESS_WHITE)
            g_snprintf (token, sizeof token, "%d. %s", pos->fullmove, san);
          else if (i == 0)
            g_snprintf (token, sizeof token, "%d... %s", pos->fullmove, san);
          else
            g_snprintf (token, sizeof token, "%s", san);
        }
      else
        g_snprintf (token, sizeof token, "%s", result);
      len = (gint) strlen (token);
      if (column && column + 1 + len > 79)
        {
          g_string_append_c (s, '\n');
          column = 0;
        }
      else if (column)
        {
          g_string_append_c (s, ' ');
          column++;
        }
      g_string_append (s, token);
      column += len;
    }
  g_string_append_c (s, '\n');
  /* GLib 1.2's g_string_free gives nothing back. */
  text = s->str;
  g_string_free (s, FALSE);
  return text;
}

static gboolean
is_result (const gchar *word)
{
  return strcmp (word, "1-0") == 0 || strcmp (word, "0-1") == 0
    || strcmp (word, "1/2-1/2") == 0 || strcmp (word, "*") == 0;
}

/* The first game in text, played over into the game; NULL, or what is
 * wrong with it. */
static gchar *
pgn_read (const gchar *text)
{
  ChessPosition start, pos;
  ChessMove *moves = g_new (ChessMove, MOST_PLIES);
  gchar *fen = NULL, *error = NULL;
  const gchar *t = text, *res = NULL;
  gint n = 0, i;
  gboolean in_moves = FALSE;

  chess_start (&start);
  pos = start;
  while (*t && !error)
    {
      const gchar *end;
      gchar *word, *w;

      if (isspace ((guchar) *t))
        {
          t++;
          continue;
        }
      if (*t == '[')
        {
          /* A tag, [Name "value"]; another game's once moves are read. */
          if (in_moves)
            break;
          end = strchr (t, ']');
          if (!end)
            break;
          if (strncmp (t, "[FEN", 4) == 0 && isspace ((guchar) t[4]))
            {
              const gchar *q = strchr (t, '"'), *q2 = q ? strchr (q + 1, '"') : NULL;

              if (q2 && q2 < end)
                {
                  g_free (fen);
                  fen = g_strndup (q + 1, (guint) (q2 - q - 1));
                }
            }
          if (strncmp (t, "[Result", 7) == 0 && isspace ((guchar) t[7]))
            {
              static const gchar *const results[] = { "1-0", "0-1",
                "1/2-1/2", "*" };
              const gchar *q = strchr (t, '"');

              for (i = 0; q && q < end && i < 4; i++)
                if (strncmp (q + 1, results[i], strlen (results[i])) == 0
                    && q[1 + strlen (results[i])] == '"')
                  res = results[i];
            }
          t = end + 1;
          continue;
        }
      if (!in_moves)
        {
          in_moves = TRUE;
          if (fen && !chess_parse_fen (&start, fen))
            {
              error = g_strdup (_("Its starting position, in FEN, can't be "
                                  "read."));
              break;
            }
          pos = start;
        }
      /* Comments, variations and annotations are passed over. */
      if (*t == '{')
        {
          end = strchr (t, '}');
          t = end ? end + 1 : t + strlen (t);
          continue;
        }
      if (*t == ';')
        {
          end = strchr (t, '\n');
          t = end ? end + 1 : t + strlen (t);
          continue;
        }
      if (*t == '(')
        {
          gint depth = 0;

          do
            {
              if (*t == '(')
                depth++;
              else if (*t == ')')
                depth--;
              else if (*t == '{')
                {
                  end = strchr (t, '}');
                  t = end ? end : t + strlen (t) - 1;
                }
              t++;
            }
          while (*t && depth);
          continue;
        }
      for (end = t; *end && !isspace ((guchar) *end) && !strchr ("{;()[", *end);
           end++)
        ;
      if (end == t)
        {
          t++;
          continue;
        }
      word = g_strndup (t, (guint) (end - t));
      t = end;
      if (is_result (word))
        {
          res = strcmp (word, "1-0") == 0 ? "1-0" : strcmp (word, "0-1") == 0
            ? "0-1" : strcmp (word, "*") == 0 ? "*" : "1/2-1/2";
          g_free (word);
          break;
        }
      /* A move's number goes before it, "12." or "12...", or joined to
       * it, "12.e4". */
      w = word;
      while (isdigit ((guchar) *w))
        w++;
      if (*w == '.')
        while (*w == '.')
          w++;
      else
        w = word;
      if (*w && *w != '$' && strcmp (w, "e.p.") != 0)
        {
          if (n >= MOST_PLIES)
            error = g_strdup (_("It has too many moves."));
          else if (!chess_parse_san (&pos, w, &moves[n]))
            error = g_strdup_printf (_("Its move %d, %s, is not a legal move "
                                       "there."), pos.fullmove, w);
          else
            chess_play (&pos, moves[n++]);
        }
      g_free (word);
    }
  if (!error && !in_moves && fen && !chess_parse_fen (&start, fen))
    error = g_strdup (_("Its starting position, in FEN, can't be read."));
  if (error)
    {
      g_free (fen);
      g_free (moves);
      return error;
    }

  /* The game read takes the place of the one played. */
  if (engine)
    engine_cancel (engine);
  asked = ASK_NONE;
  clock_stop ();
  game_minutes = clock_minutes[opts.clock];
  clock_ms[CHESS_WHITE] = clock_ms[CHESS_BLACK] = game_minutes * 60000;
  clock_at[0][CHESS_WHITE] = clock_at[0][CHESS_BLACK] = game_minutes * 60000;
  g_free (start_fen);
  start_fen = fen;
  history[0] = start;
  for (i = 0; i < n; i++)
    {
      history[i + 1] = history[i];
      chess_play (&history[i + 1], moves[i]);
      played[i] = moves[i];
    }
  plies = n;
  /* A game read has no times: its clocks start full at every move. */
  for (i = 0; i <= n; i++)
    clock_at[i][CHESS_WHITE] = clock_at[i][CHESS_BLACK]
      = game_minutes * 60000;
  g_free (moves);
  result = res ? res : "*";
  over = strcmp (result, "*") != 0;
  human = opts.side;
  selected = -1;
  hint_shown = FALSE;
  drag_end ();
  return NULL;
}

static GtkWidget *file_dialog;
static gboolean file_saving;

static void
file_ok (GtkWidget *widget, gpointer data)
{
  gchar *name = g_strdup (gtk_file_selection_get_filename
                          (GTK_FILE_SELECTION (file_dialog)));
  gchar *text, *error = NULL;
  FILE *f;

  (void) widget;
  (void) data;
  gtk_widget_destroy (file_dialog);
  if (file_saving)
    {
      text = pgn_text ();
      f = fopen (name, "w");
      if (!f || fputs (text, f) == EOF || fclose (f) != 0)
        error = g_strdup_printf (_("The game could not be saved in %s: %s."),
                                 name, g_strerror (errno));
      else
        {
          gchar *said = g_strdup_printf (_("Game saved in %s."), name);

          message (said);
          g_free (said);
        }
      g_free (text);
    }
  else
    {
      glong size;

      f = fopen (name, "r");
      if (!f)
        error = g_strdup_printf (_("%s could not be opened: %s."), name,
                                 g_strerror (errno));
      else
        {
          fseek (f, 0, SEEK_END);
          size = ftell (f);
          fseek (f, 0, SEEK_SET);
          if (size < 0 || size > 4 * 1024 * 1024)
            size = 4 * 1024 * 1024;
          text = g_malloc ((gsize) size + 1);
          text[fread (text, 1, (gsize) size, f)] = '\0';
          fclose (f);
          error = pgn_read (text);
          g_free (text);
          if (error)
            {
              gchar *e = g_strdup_printf (_("%s is not a game that can be "
                                            "opened. %s"), name, error);

              g_free (error);
              error = e;
            }
          else
            {
              gchar *said = over
                ? g_strdup_printf (_("Opened %s: the game ended %s."), name,
                                   result)
                : g_strdup_printf (_("Opened %s."), name);

              message (said);
              g_free (said);
              after_move ();
            }
        }
    }
  if (error)
    games_message (GTK_WINDOW (window), _(APPNAME_LONG), error);
  g_free (error);
  g_free (name);
}

static void
choose_file (gboolean saving)
{
  if (file_dialog)
    {
      gdk_window_raise (file_dialog->window);
      return;
    }
  file_saving = saving;
  file_dialog = gtk_file_selection_new (saving ? _("Save Game")
                                        : _("Open Game"));
  gtk_window_set_transient_for (GTK_WINDOW (file_dialog), GTK_WINDOW (window));
  gtk_signal_connect (GTK_OBJECT (file_dialog), "destroy",
                      GTK_SIGNAL_FUNC (gtk_widget_destroyed), &file_dialog);
  gtk_signal_connect (GTK_OBJECT (GTK_FILE_SELECTION (file_dialog)->ok_button),
                      "clicked", GTK_SIGNAL_FUNC (file_ok), NULL);
  gtk_signal_connect_object (GTK_OBJECT
                             (GTK_FILE_SELECTION (file_dialog)->cancel_button),
                             "clicked", GTK_SIGNAL_FUNC (gtk_widget_destroy),
                             GTK_OBJECT (file_dialog));
  if (saving)
    gtk_file_selection_set_filename (GTK_FILE_SELECTION (file_dialog),
                                     "game.pgn");
  gtk_widget_show (file_dialog);
}

/* Options. */

static void
load_options (void)
{
  const gchar *set = games_config_get_string (config, "pieces",
                                              pieces_sets[0]);
  gint i;

  opts.set = 0;
  for (i = 0; i < PIECES_N_SETS; i++)
    if (strcmp (set, pieces_sets[i]) == 0)
      opts.set = i;
  set = games_config_get_string (config, "board", boards[0].id);
  opts.board = 0;
  for (i = 0; i < N_BOARDS; i++)
    if (strcmp (set, boards[i].id) == 0)
      opts.board = i;
  opts.side = strcmp (games_config_get_string (config, "side", "white"),
                      "black") == 0 ? CHESS_BLACK : CHESS_WHITE;
  /* A Casual player's, or what the level there was stood for. */
  i = CLAMP (games_config_get_int (config, "level", 1), 0,
             (gint) ELEMENTS (old_levels) - 1);
  opts.think = games_config_get_int (config, "think", old_levels[i].think)
    == THINK_TIME ? THINK_TIME : THINK_DEPTH;
  opts.depth = CLAMP (games_config_get_int (config, "depth",
                                            old_levels[i].think == THINK_DEPTH
                                            ? old_levels[i].amount : 2),
                      1, MOST_DEPTH);
  opts.seconds = CLAMP (games_config_get_int (config, "seconds",
                                              old_levels[i].think == THINK_TIME
                                              ? old_levels[i].amount : 5),
                        1, MOST_SECONDS);
  opts.skill = CLAMP (games_config_get_int (config, "skill",
                                            old_levels[i].skill), 0, 100);
  opts.coords = games_config_get_int (config, "coordinates", 1) != 0;
  opts.moves = games_config_get_int (config, "move_list", 1) != 0;
  opts.clock = CLAMP (games_config_get_int (config, "clock", 0), 0,
                      N_CLOCKS - 1);
}

static void
save_options (void)
{
  games_config_set_string (config, "pieces", pieces_sets[opts.set]);
  games_config_set_string (config, "board", boards[opts.board].id);
  games_config_set_string (config, "side",
                           opts.side == CHESS_BLACK ? "black" : "white");
  games_config_set_int (config, "think", opts.think);
  games_config_set_int (config, "depth", opts.depth);
  games_config_set_int (config, "seconds", opts.seconds);
  games_config_set_int (config, "skill", opts.skill);
  games_config_set_int (config, "coordinates", opts.coords);
  games_config_set_int (config, "move_list", opts.moves);
  games_config_set_int (config, "clock", opts.clock);
  games_config_save (config);
}

static GtkWidget *options_dialog;

static struct
{
  GtkWidget *set[PIECES_N_SETS];
  GtkWidget *board[ELEMENTS (boards)];
  GtkWidget *side[2];
  GtkWidget *think[2], *depth, *seconds, *skill;
  GtkWidget *clock[ELEMENTS (clock_minutes)];
  GtkWidget *known[ELEMENTS (known_engines)], *other, *engine;
  GtkWidget *skill_row;
} od;

static gboolean
active (GtkWidget *toggle)
{
  return GTK_TOGGLE_BUTTON (toggle)->active;
}

/* The engine to use when none is chosen: the first of the known ones
 * installed, or GNU Chess, to say what is missing. */
static const gchar *
default_engine (void)
{
  gsize i;

  for (i = 0; i < (gsize) N_KNOWN; i++)
    if (engine_installed (known_engines[i].program))
      return known_engines[i].command;
  return known_engines[0].command;
}

/* Starts command as the engine, in place of the one there was. */
static void
set_engine (const gchar *command)
{
  gchar *said;

  if (engine)
    engine_free (engine);
  asked = ASK_NONE;
  engine = engine_new (command, answered, NULL);
  engine_gone = engine == NULL;
  if (engine_gone)
    {
      said = g_strdup_printf (_("The engine %s could not be started: you "
                                "play both sides."), command);
      games_message (GTK_WINDOW (window), _(APPNAME_LONG), said);
    }
  else
    said = g_strdup_printf (_("The engine is now %s."), command);
  message (said);
  g_free (said);
  show_status ();
  if (computer_to_move ())
    ask (ASK_MOVE);
}

static void
options_ok (GtkWidget *widget, gpointer data)
{
  gint i, set = opts.set, side = opts.side, clock = opts.clock;
  gchar *command = NULL;

  (void) widget;
  (void) data;
  for (i = 0; i < PIECES_N_SETS; i++)
    if (active (od.set[i]))
      set = i;
  for (i = 0; i < 2; i++)
    if (active (od.side[i]))
      opts.side = i;
  opts.think = active (od.think[THINK_TIME]) ? THINK_TIME : THINK_DEPTH;
  opts.depth = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (od.depth));
  opts.seconds = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON
                                                   (od.seconds));
  opts.skill = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (od.skill));
  for (i = 0; i < N_BOARDS; i++)
    if (active (od.board[i]))
      opts.board = i;
  for (i = 0; i < N_CLOCKS; i++)
    if (active (od.clock[i]))
      opts.clock = i;
  for (i = 0; i < N_KNOWN; i++)
    if (active (od.known[i]))
      command = g_strdup (known_engines[i].command);
  if (!command)
    command = g_strstrip (g_strdup (gtk_entry_get_text
                                    (GTK_ENTRY (od.engine))));
  save_options ();
  redraw ();
  gtk_widget_destroy (options_dialog);

  if (!*command)
    {
      g_free (command);
      command = g_strdup (default_engine ());
    }
  if (strcmp (command, games_config_get_string (config, "engine",
                                                default_engine ())) != 0)
    {
      games_config_set_string (config, "engine", command);
      games_config_save (config);
      set_engine (command);
    }
  g_free (command);
  if (opts.clock != clock && (plies == 0 || over))
    new_game ();
  else if (opts.clock != clock)
    message (_("The clock changes from the next game."));

  if (set != opts.set)
    {
      Pieces *p = pieces_new (pieces_sets[set], pieces_size (pieces));

      if (p)
        {
          pieces_free (pieces);
          pieces = p;
          opts.set = set;
          save_options ();
          redraw ();
        }
      else
        games_message (GTK_WINDOW (window), _(APPNAME_LONG),
                       _("Those pieces could not be read."));
    }
  if (opts.side != side && opts.side != human)
    {
      /* A game not begun starts again with the side chosen. */
      if (plies == 0 || over)
        new_game ();
      else
        message (opts.side == CHESS_WHITE ? _("You play White from the next game.")
                 : _("You play Black from the next game."));
    }
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
radio (GtkWidget *box, GtkWidget *first, const gchar *label)
{
  GtkWidget *button = gtk_radio_button_new_with_label
    (first ? gtk_radio_button_group (GTK_RADIO_BUTTON (first)) : NULL, label);

  gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);
  return button;
}

/* A spin button for whole numbers from low to high, in a row after a
 * radio button or label, with a word after it. */
static GtkWidget *
spin (GtkWidget *row, gint value, gint low, gint high, gint step,
      const gchar *after)
{
  GtkObject *adj = gtk_adjustment_new (value, low, high, step, step * 5, 0);
  GtkWidget *button = gtk_spin_button_new (GTK_ADJUSTMENT (adj), 1, 0);

  gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (button), TRUE);
  gtk_box_pack_start (GTK_BOX (row), button, FALSE, FALSE, 0);
  if (after)
    gtk_box_pack_start (GTK_BOX (row), gtk_label_new (after), FALSE, FALSE, 0);
  return button;
}

static GtkWidget *
spin_row (GtkWidget *box)
{
  GtkWidget *row = gtk_hbox_new (FALSE, 6);

  gtk_box_pack_start (GTK_BOX (box), row, FALSE, FALSE, 0);
  return row;
}

/* Only the way of thinking chosen can be set. */
static void
think_toggled (GtkWidget *widget, gpointer data)
{
  (void) widget;
  (void) data;
  gtk_widget_set_sensitive (od.depth, active (od.think[THINK_DEPTH]));
  gtk_widget_set_sensitive (od.seconds, active (od.think[THINK_TIME]));
}

/* The skill, greyed for GNU Chess, which can't play weaker; any other
 * engine is left to do with it what it can. */
static void
skill_update (void)
{
  gint i;

  if (!od.skill_row)
    return;
  for (i = 0; i < N_KNOWN; i++)
    if (active (od.known[i]))
      {
        gtk_widget_set_sensitive (od.skill_row, known_engines[i].weakens);
        return;
      }
  gtk_widget_set_sensitive (od.skill_row, TRUE);
}

static void
engine_toggled (GtkWidget *widget, gpointer data)
{
  (void) widget;
  (void) data;
  skill_update ();
}

/* The command entry is for the other engine alone. */
static void
other_toggled (GtkWidget *widget, gpointer data)
{
  (void) data;
  gtk_widget_set_sensitive (od.engine, active (widget));
  if (active (widget))
    gtk_widget_grab_focus (od.engine);
}

static void
options (void)
{
  GtkWidget *vbox, *hbox, *fbox, *button;
  gint i;

  if (options_dialog)
    {
      gdk_window_raise (options_dialog->window);
      return;
    }
  memset (&od, 0, sizeof od);
  options_dialog = games_dialog_new (GTK_WINDOW (window), _("Options"));
  gtk_signal_connect (GTK_OBJECT (options_dialog), "destroy",
                      GTK_SIGNAL_FUNC (gtk_widget_destroyed), &options_dialog);

  vbox = gtk_vbox_new (FALSE, 8);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 8);
  hbox = gtk_hbox_new (TRUE, 8);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, TRUE, TRUE, 0);

  fbox = options_frame (hbox, _("You play"));
  od.side[0] = radio (fbox, NULL, _("White"));
  od.side[1] = radio (fbox, od.side[0], _("Black"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (od.side[opts.side]), TRUE);

  fbox = options_frame (hbox, _("Pieces"));
  for (i = 0; i < PIECES_N_SETS; i++)
    od.set[i] = radio (fbox, i ? od.set[0] : NULL, _(pieces_set_names[i]));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (od.set[opts.set]), TRUE);

  fbox = options_frame (hbox, _("Board"));
  for (i = 0; i < N_BOARDS; i++)
    od.board[i] = radio (fbox, i ? od.board[0] : NULL, _(boards[i].name));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (od.board[opts.board]),
                                TRUE);

  hbox = gtk_hbox_new (FALSE, 8);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, TRUE, TRUE, 0);
  fbox = options_frame (hbox, _("The computer thinks"));
  {
    GtkWidget *row = spin_row (fbox), *label;

    od.think[THINK_DEPTH] = radio (row, NULL, _("To a depth of"));
    od.depth = spin (row, opts.depth, 1, MOST_DEPTH, 1, _("half-moves"));
    row = spin_row (fbox);
    od.think[THINK_TIME] = radio (row, od.think[THINK_DEPTH], _("For"));
    od.seconds = spin (row, opts.seconds, 1, MOST_SECONDS, 1,
                       _("seconds a move"));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (od.think[opts.think]),
                                  TRUE);
    think_toggled (NULL, NULL);
    for (i = 0; i < 2; i++)
      gtk_signal_connect (GTK_OBJECT (od.think[i]), "toggled",
                          GTK_SIGNAL_FUNC (think_toggled), NULL);

    /* The skill is greyed for GNU Chess, as the engine chosen below
     * changes. */
    od.skill_row = row = spin_row (fbox);
    label = gtk_label_new (_("At a skill of"));
    gtk_box_pack_start (GTK_BOX (row), label, FALSE, FALSE, 0);
    od.skill = spin (row, opts.skill, 0, 100, 5, _("out of 100"));
  }

  fbox = options_frame (hbox, _("Clock"));
  for (i = 0; i < N_CLOCKS; i++)
    {
      gchar text[32];

      if (clock_minutes[i])
        g_snprintf (text, sizeof text, _("%d minutes a side"),
                    clock_minutes[i]);
      else
        g_snprintf (text, sizeof text, "%s", _("None"));
      od.clock[i] = radio (fbox, i ? od.clock[0] : NULL, text);
    }
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (od.clock[opts.clock]),
                                TRUE);

  fbox = options_frame (vbox, _("Engine"));
  {
    const gchar *command = games_config_get_string (config, "engine",
                                                    default_engine ());
    GtkWidget *row, *chosen = NULL;

    /* GNU Chess and Stockfish by name, greyed if not installed; any
     * other UCI engine by its command. */
    for (i = 0; i < N_KNOWN; i++)
      {
        gboolean installed = engine_installed (known_engines[i].program);
        gchar *label = installed ? g_strdup (_(known_engines[i].name))
          : g_strdup_printf (_("%s (not installed)"),
                             _(known_engines[i].name));

        od.known[i] = radio (fbox, i ? od.known[0] : NULL, label);
        g_free (label);
        gtk_widget_set_sensitive (od.known[i], installed);
        if (strcmp (command, known_engines[i].command) == 0)
          chosen = od.known[i];
      }
    row = gtk_hbox_new (FALSE, 6);
    gtk_box_pack_start (GTK_BOX (fbox), row, FALSE, FALSE, 0);
    od.other = radio (row, od.known[0], _("Another UCI engine:"));
    od.engine = gtk_entry_new ();
    gtk_box_pack_start (GTK_BOX (row), od.engine, TRUE, TRUE, 0);
    if (!chosen)
      {
        chosen = od.other;
        gtk_entry_set_text (GTK_ENTRY (od.engine), command);
      }
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (chosen), TRUE);
    gtk_widget_set_sensitive (od.engine, chosen == od.other);
    gtk_signal_connect (GTK_OBJECT (od.other), "toggled",
                        GTK_SIGNAL_FUNC (other_toggled), NULL);
    for (i = 0; i < N_KNOWN; i++)
      gtk_signal_connect (GTK_OBJECT (od.known[i]), "toggled",
                          GTK_SIGNAL_FUNC (engine_toggled), NULL);
    skill_update ();
  }

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

static void
about (void)
{
  const gchar *name = engine ? engine_name (engine) : NULL;
  gchar *line = g_strdup_printf ("Engine: %s, run as a separate program",
                                 name ? name : "none");
  const gchar *credits[] = {
    "Pieces: Colin M. L. Burnett, and",
    "Armando Hernandez Marroquin, GPL",
    line,
    NULL
  };

  games_about (GTK_WINDOW (window), "cgchess",
               _("Classic GUI Chess"),
               "Copyright (C) 2026 The CGTK authors", credits, NULL);
  g_free (line);
}

/* The menus. */

enum { NEW_GAME, OPEN_GAME, SAVE_GAME, UNDO, HINT, FLIP, OPTIONS, QUIT_GAME,
  ABOUT, COORDS, MOVES };

static void
quit_game (void)
{
  /* The board's size is kept for next time. */
  if (frame)
    {
      games_config_set_int (config, "width", width);
      games_config_set_int (config, "height", height);
      games_config_save (config);
    }
  gtk_main_quit ();
}

static void
menu_callback (gpointer data, guint action, GtkWidget *widget)
{
  (void) data;
  switch (action)
    {
    case NEW_GAME:
      new_game ();
      break;
    case OPEN_GAME:
      choose_file (FALSE);
      break;
    case SAVE_GAME:
      choose_file (TRUE);
      break;
    case UNDO:
      undo ();
      break;
    case HINT:
      ask_hint ();
      break;
    case FLIP:
      flipped = !flipped;
      redraw ();
      break;
    case COORDS:
      opts.coords = GTK_CHECK_MENU_ITEM (widget)->active;
      save_options ();
      redraw ();
      break;
    case MOVES:
      opts.moves = GTK_CHECK_MENU_ITEM (widget)->active;
      save_options ();
      if (opts.moves)
        gtk_widget_show (side_panel);
      else
        gtk_widget_hide (side_panel);
      break;
    case OPTIONS:
      options ();
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
  { N_("/Game/_Open Game..."), "<control>O", GTK_SIGNAL_FUNC (menu_callback),
    OPEN_GAME, NULL },
  { N_("/Game/_Save Game..."), "<control>S", GTK_SIGNAL_FUNC (menu_callback),
    SAVE_GAME, NULL },
  { "/Game/sep1", NULL, NULL, 0, "<Separator>" },
  { N_("/Game/_Undo Move"), "<control>Z", GTK_SIGNAL_FUNC (menu_callback),
    UNDO, NULL },
  { N_("/Game/_Hint"), "<control>H", GTK_SIGNAL_FUNC (menu_callback), HINT,
    NULL },
  { "/Game/sep2", NULL, NULL, 0, "<Separator>" },
  { N_("/Game/O_ptions..."), NULL, GTK_SIGNAL_FUNC (menu_callback), OPTIONS,
    NULL },
  { "/Game/sep3", NULL, NULL, 0, "<Separator>" },
  { N_("/Game/E_xit"), "<control>Q", GTK_SIGNAL_FUNC (menu_callback),
    QUIT_GAME, NULL },
  { N_("/_View"), NULL, NULL, 0, "<Branch>" },
  { N_("/View/_Coordinates"), NULL, GTK_SIGNAL_FUNC (menu_callback), COORDS,
    "<CheckItem>" },
  { N_("/View/_Move List"), NULL, GTK_SIGNAL_FUNC (menu_callback), MOVES,
    "<CheckItem>" },
  { "/View/sep1", NULL, NULL, 0, "<Separator>" },
  { N_("/View/_Flip Board"), "<control>F", GTK_SIGNAL_FUNC (menu_callback),
    FLIP, NULL },
  { N_("/_Help"), NULL, NULL, 0, "<Branch>" },
  { N_("/Help/_About Chess..."), NULL, GTK_SIGNAL_FUNC (menu_callback),
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

/* A sunken box at the end of the status bar. */
static GtkWidget *
status_field (GtkWidget *box)
{
  GtkWidget *border = gtk_frame_new (NULL);
  GtkWidget *label = gtk_label_new ("");

  gtk_frame_set_shadow_type (GTK_FRAME (border), GTK_SHADOW_IN);
  gtk_misc_set_padding (GTK_MISC (label), 4, 0);
  gtk_container_add (GTK_CONTAINER (border), label);
  gtk_box_pack_end (GTK_BOX (box), border, FALSE, FALSE, 0);
  return label;
}

int
main (int argc, char *argv[])
{
  GtkWidget *vbox, *status_box;
  GtkAccelGroup *accel_group;
  GtkItemFactory *factory;

  gtk_set_locale ();
  games_init_translation ();
  gtk_init (&argc, &argv);

  config = games_config_load (APPNAME);
  load_options ();

  /* Drawn again to fit the board once it is shown. */
  pieces = pieces_new (pieces_sets[opts.set], SQ);
  if (!pieces && opts.set != 0)
    pieces = pieces_new (pieces_sets[opts.set = 0], SQ);
  if (!pieces)
    {
      fprintf (stderr, _("%s: the pieces' pictures could not be read\n"),
               argv[0]);
      return 1;
    }

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title (GTK_WINDOW (window), _(APPNAME_LONG));
  gtk_window_set_wmclass (GTK_WINDOW (window), APPNAME, "Cgchess");
  gtk_window_set_policy (GTK_WINDOW (window), FALSE, TRUE, FALSE);
  gtk_signal_connect (GTK_OBJECT (window), "delete_event",
                      GTK_SIGNAL_FUNC (delete_event_callback), NULL);

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

  table = gtk_drawing_area_new ();
  /* The board starts at the size it was left at; once shown, it can be
   * made as small as MIN_SIZE square. */
  gtk_widget_set_usize (table,
                        MAX (games_config_get_int (config, "width", FIRST_SIZE),
                             MIN_SIZE),
                        MAX (games_config_get_int (config, "height", FIRST_SIZE),
                             MIN_SIZE));
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
  {
    static const gchar *titles[3] = { "", N_("White"), N_("Black") };
    const gchar *shown[3];
    GtkWidget *hbox = gtk_hbox_new (FALSE, 0), *scrolled;
    gint i;

    gtk_box_pack_start (GTK_BOX (hbox), table, TRUE, TRUE, 0);
    /* The moves, a row to each, beside the board. */
    for (i = 0; i < 3; i++)
      shown[i] = i ? _(titles[i]) : titles[i];
    move_list = gtk_clist_new_with_titles (3, (gchar **) shown);
    gtk_clist_column_titles_passive (GTK_CLIST (move_list));
    gtk_clist_set_column_width (GTK_CLIST (move_list), 0, 30);
    gtk_clist_set_column_width (GTK_CLIST (move_list), 1, 60);
    gtk_clist_set_column_width (GTK_CLIST (move_list), 2, 60);
    gtk_clist_set_column_justification (GTK_CLIST (move_list), 0,
                                        GTK_JUSTIFY_RIGHT);
    gtk_clist_set_selection_mode (GTK_CLIST (move_list),
                                  GTK_SELECTION_SINGLE);
    scrolled = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                    GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
    gtk_container_add (GTK_CONTAINER (scrolled), move_list);
    gtk_widget_set_usize (scrolled, 190, -1);
    side_panel = scrolled;
    gtk_box_pack_start (GTK_BOX (hbox), side_panel, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (vbox), hbox, TRUE, TRUE, 0);
  }

  status_box = gtk_hbox_new (FALSE, 2);
  statusbar = gtk_statusbar_new ();
  gtk_box_pack_start (GTK_BOX (status_box), statusbar, TRUE, TRUE, 0);
  move_label = status_field (status_box);
  turn_label = status_field (status_box);
  clock_label[CHESS_BLACK] = status_field (status_box);
  clock_label[CHESS_WHITE] = status_field (status_box);
  gtk_box_pack_start (GTK_BOX (vbox), status_box, FALSE, FALSE, 0);

  engine = engine_new (games_config_get_string (config, "engine",
                                                default_engine ()),
                       answered, NULL);
  if (!engine)
    engine_gone = TRUE;

  gtk_widget_show_all (vbox);
  if (!opts.moves)
    gtk_widget_hide (side_panel);
  gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM
                                  (gtk_item_factory_get_widget
                                   (factory, "<main>/View/Coordinates")),
                                  opts.coords);
  gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM
                                  (gtk_item_factory_get_widget
                                   (factory, "<main>/View/Move List")),
                                  opts.moves);
  games_window_set_icon (window, "cgchess", cgchess_xpm);
  gtk_widget_show (window);
  gtk_widget_set_usize (table, MIN_SIZE, MIN_SIZE);
  new_game ();
  gtk_main ();

  engine_free (engine);
  clock_stop ();
  if (relayout_timer)
    gtk_timeout_remove (relayout_timer);
  drag_end ();
  g_free (frame);
  pieces_free (pieces);
  games_config_free (config);
  return 0;
}
