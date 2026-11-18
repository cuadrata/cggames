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

#ifndef TABLE_H
#define TABLE_H

#include <gtk/gtk.h>

#include "games.h"
#include "hanafuda.h"
#include "koikoi.h"

#define APPNAME "cghanafuda"
#define APPNAME_LONG N_("Hanafuda")

/* The table, in the game's pixels: a card, and the margin round the
 * table. */
#define CW HANAFUDA_WIDTH
#define CH HANAFUDA_HEIGHT
#define MARGIN 10

#define TABLE_COLOUR 0x1e5a32

/* What the cards are played on. */
enum { TABLE_FELT, TABLE_TATAMI, N_TABLES };

/* How far the cards keep from the table's left and right edges, in the
 * game's pixels: MARGIN, and on the tatami the border's width too. The
 * table is drawn small enough that the game fits between. */
gint table_side (void);

/* The options, kept in cghanafuda.conf. */
typedef struct
{
  gint game;                    /* games[game] */
  gint set;                     /* hanafuda_sets[set] */
  gint border;                  /* HANAFUDA_RED or HANAFUDA_BLACK */
  gint table;                   /* TABLE_FELT or TABLE_TATAMI */
  gint rounds;                  /* 3, 6 or 12 */
  KKRules rules;                /* Koi-Koi's scoring */
  gboolean ties_to_dealer;      /* Oicho-Kabu's ties */
  gboolean learning;            /* help for a player learning the game */
} Options;

extern Options opts;
extern GtkWidget *window;

/* What each game does at the table: laid out in width by height of the
 * game's pixels, which are drawn as large as the window allows; started
 * anew, stopped when another takes its place, drawn, clicked at (x, y)
 * with button, and its rules told; and, if it can, told the pointer is
 * at (x, y), or off the table for -1, asked for a hint, and asked which
 * player has taken a card, for the charts. */
typedef struct
{
  const gchar *id;              /* as stored */
  const gchar *name;            /* as shown */
  gint width, height;
  void (*new_game) (void);
  void (*stop) (void);
  gboolean (*in_play) (void);   /* begun and not yet over */
  void (*draw) (void);
  void (*press) (gint x, gint y, gint button);
  void (*help) (void);
  void (*motion) (gint x, gint y);
  void (*hint) (void);
  gint (*owner) (gint card);    /* who has taken card, or -1 */
} Game;

/* The games there are, and the one being played. */
extern const Game koikoi_game, kabu_game;
extern const Game *game;

/* The width and height of the table in the game's pixels: at least the
 * game's own once it is laid out to fit. */
extern gint frame_width, frame_height;

/* The cards, drawn to the table's size. */
extern Hanafuda *cards;

/* The game's drawing, in the game's pixels: the table bare, a card, or
 * the back for KK_NONE, a light round the card at (x, y) to show it can
 * be chosen, and a rectangle filled with colour. */
void table_clear (void);
void table_card (gint card, gint x, gint y);

/* A card drawn smaller, SMALL_W by SMALL_H, for cards that are only to
 * be told apart, as those taken in Koi-Koi. */
#define SMALL_W (CW * 7 / 10)
#define SMALL_H (CH * 7 / 10)
void table_small_card (gint card, gint x, gint y);

/* The cards of hanafuda_sets[set] from now on; FALSE, the cards as they
 * were, if they can't be read. */
gboolean table_set_cards (gint set, gint border);
void table_light (gint x, gint y);

/* A mark on the card at (x, y), the one a hint suggests. */
void table_mark (gint x, gint y);

/* The stock: n cards face down, piled a few deep as it is fuller, the
 * top one at (x, y) and those under it showing above and to its right;
 * and how far up and across the pile reaches past the top card. */
void table_pile (gint x, gint y, gint n);
#define PILE_REACH 6

/* A light round cards laid from the one at (x, y) to the one at
 * (x_last, y_last). */
void table_light_cards (gint x, gint y, gint x_last, gint y_last);

/* A round disc of radius r about (x, y), smoothed at its edge. */
void table_disc (gint x, gint y, gint r, guint32 colour);

/* Draws the game again. */
void table_redraw (void);

/* Lays the table out again for a game of another size. */
void table_refit (void);

/* The status bar: a message for a moment, over what to do now, which
 * stays, and under both what is under the pointer while it is there;
 * NULL takes those away. Then three fields from the right. */
void table_message (const gchar *text);
void table_prompt (const gchar *text);
void table_hint (const gchar *text);
void table_status (gint field, const gchar *text);

/* A new game of the game the options name, which may not be the one
 * being played. */
void table_new_game (void);

/* The drawing area's part. */
GtkWidget *table_new (gint width, gint height);
gint table_width (void);
gint table_height (void);
void table_free (void);

/* Converts a press on the drawing area to the game's pixels. */
void table_point (gdouble x, gdouble y, gint *gx, gint *gy);

#endif
