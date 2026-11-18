/*
 * cghanafuda: the rules of Oicho-Kabu
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

#ifndef KABU_H
#define KABU_H

#include <glib.h>

/* The forty cards of January to October, numbered as koikoi.h numbers
 * them, each worth its month: 1 to 10. */
#define KB_CARDS 40
#define KB_VALUE(c) ((c) / 4 + 1)
#define KB_LANES 4
#define KB_MOST 3               /* cards in a hand, at most */

typedef struct
{
  gint8 cards[KB_MOST];
  gint n;
} KBHand;

typedef struct
{
  gint8 stock[KB_CARDS];
  gint n_stock;
  KBHand lane[KB_LANES];        /* the tableaus the player bets on */
  KBHand dealer;
} KBDeal;

/* A hand's worth: its cards' sum, but for the tens. */
gint kb_total (const KBHand *hand);

/* Three of one month, arashi. */
gboolean kb_arashi (const KBHand *hand);

/* The first two cards, and no third: nine and one, kuppin, the
 * dealer's; four and one, shippin, the player's. */
gboolean kb_kuppin (const KBHand *hand);
gboolean kb_shippin (const KBHand *hand);

/* Shuffles and deals a card face up to each lane, and the dealer's
 * first, face down; dealt again while three lanes show one month. */
void kb_deal (KBDeal *deal);

/* A card from the stock onto hand. */
void kb_hit (KBDeal *deal, KBHand *hand);

/* Whether a lane of two cards must take a third, may, or can't. */
typedef enum { KB_MUST_DRAW, KB_MAY_DRAW, KB_STAND } KBChoice;
KBChoice kb_choice (const KBHand *lane);

/* Whether the dealer, with two cards, takes a third. */
gboolean kb_dealer_draws (const KBHand *dealer);

/* What a lane wins, in bets: 1 or 3, or loses, -1 or -3; 0 for a tie
 * that stands off, unless ties go to the dealer. */
gint kb_settle (const KBHand *lane, const KBHand *dealer,
                gboolean ties_to_dealer);

#endif
