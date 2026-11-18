/*
 * cghanafuda: the rules of Koi-Koi
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

#ifndef KOIKOI_H
#define KOIKOI_H

#include <glib.h>

/* The 48 cards, four to a month, January first; within a month the
 * bright, the animal, the ribbon and the plain cards, in that order,
 * as far as the month has them. */
#define KK_CARDS 48
#define KK_MONTH(c) ((c) / 4)
#define KK_NONE (-1)

typedef enum
{
  KK_BRIGHT, KK_ANIMAL, KK_RIBBON, KK_PLAIN
} KKKind;

/* The cards the yaku are made of. */
enum
{
  KK_CRANE = 0, KK_PINE_POEM = 1,
  KK_WARBLER = 4, KK_PLUM_POEM = 5,
  KK_CURTAIN = 8, KK_CHERRY_POEM = 9,
  KK_BUTTERFLIES = 20, KK_PEONY_BLUE = 21,
  KK_BOAR = 24,
  KK_MOON = 28,
  KK_SAKE = 32, KK_CHRYSANTHEMUM_BLUE = 33,
  KK_DEER = 36, KK_MAPLE_BLUE = 37,
  KK_RAIN_MAN = 40,
  KK_PHOENIX = 44
};

KKKind kk_kind (gint card);

/* Names, in English: a month's, its flower's, and a card's own, as "the
 * crane" or "a plain card". */
const gchar *kk_month_name (gint month);
const gchar *kk_flower_name (gint month);
const gchar *kk_card_name (gint card);

/* The house rules. */
typedef struct
{
  gboolean sake_is_plain;       /* the sake cup counts as a plain card too */
  gboolean rain_spoils;         /* the rain man spoils the viewing yaku */
  gboolean double_seven;        /* seven points or more count double */
  gboolean double_koikoi;       /* stopping after the other's koi-koi doubles */
  gboolean koikoi_again;        /* koi-koi may be called again on more points */
  gboolean dealer_privilege;    /* a round with no yaku gives the dealer 6 */
} KKRules;

/* A yaku made, for showing. */
typedef struct
{
  const gchar *name;            /* romanized */
  const gchar *meaning;         /* in English */
  gint points;
  gint extra;                   /* cards of extra_kind beyond those it needs */
  KKKind extra_kind;
} KKYaku;

#define KK_MAX_YAKU 16

/* The yaku in the cards taken, into yaku (if not NULL), and their
 * points. */
gint kk_yaku (const KKRules *rules, const gint8 *taken, gint n_taken,
              KKYaku *yaku, gint *n_yaku);

/* A round. Player 0 is the one at the table, 1 the computer. */
typedef struct
{
  gint8 hand[2][8];
  gint n_hand[2];
  gint8 field[KK_CARDS];
  gint n_field;
  gint8 stock[KK_CARDS];
  gint n_stock;
  gint8 taken[2][KK_CARDS];
  gint n_taken[2];
  gint dealer;                  /* plays first */
  gint koikoi[2];               /* times each has called koi-koi */
  gint points[2];               /* yaku points when each last decided */
} KKRound;

/* Who deals first: each player turns a card, and the earlier month
 * deals, or in the same month the card worth more; they turn again if
 * that settles nothing. The cards turned go into turned[2]. */
gint kk_first_dealer (gint *turned);

/* Deals with games_rand_int's numbers, again if the field has all of
 * a month. Returns the player whose hand is all four of a month or
 * four pairs, a hand that wins at once, KK_BOTH if both hands are, or
 * KK_NONE. */
#define KK_BOTH 2
gint kk_deal (KKRound *round, gint dealer);

/* The field cards of card's month, into match; their number. */
gint kk_matches (const KKRound *round, gint card, gint *match);

/* Plays card for player, from the hand or just drawn: with no match it
 * goes to the field; with one, or three, the player takes them all;
 * with two, the one chosen. Returns the number of cards taken. */
gint kk_play (KKRound *round, gint player, gint card, gint chosen);

/* Takes card from player's hand, for playing. */
void kk_from_hand (KKRound *round, gint player, gint card);

/* The top card of the stock, taken off it; KK_NONE when it is empty. */
gint kk_draw (KKRound *round);

/* The points a round won by stopping with points pays, under the
 * rules. */
gint kk_payout (const KKRules *rules, const KKRound *round, gint winner,
                gint points);

/* The computer's choices: the hand card to play, the field card to take
 * from two, and whether to call koi-koi on points. */
gint kk_ai_card (const KKRules *rules, const KKRound *round, gint player,
                 gint *chosen);
gint kk_ai_choose (const KKRules *rules, const KKRound *round, gint player,
                   gint card);
gboolean kk_ai_koikoi (const KKRules *rules, const KKRound *round,
                       gint player, gint points);

#endif
