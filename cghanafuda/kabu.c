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

/* The rules are the common ones: a hand is worth the last digit of its
 * sum, nine best; a lane draws a third card at 3 or less, may at 4 to 6
 * or at 9, and stands at 7 or 8; a tie, 0 and 0 too, goes to the
 * dealer, or, as the computer always deals here, may stand off, the
 * bet kept. Arashi, three of a month, wins three times the bet whatever
 * the dealer holds, or loses it three times over to the dealer's, the
 * higher worth winning if both have one. The dealer's kuppin, nine and
 * one, beats every other lane, and a lane's shippin, four and one,
 * every other dealer's hand. */

#include "config.h"

#include <string.h>

#include "games.h"
#include "kabu.h"

gint
kb_total (const KBHand *hand)
{
  gint i, sum = 0;

  for (i = 0; i < hand->n; i++)
    sum += KB_VALUE (hand->cards[i]);
  return sum % 10;
}

gboolean
kb_arashi (const KBHand *hand)
{
  return hand->n == 3
    && KB_VALUE (hand->cards[0]) == KB_VALUE (hand->cards[1])
    && KB_VALUE (hand->cards[1]) == KB_VALUE (hand->cards[2]);
}

static gboolean
pair_of (const KBHand *hand, gint a, gint b)
{
  gint x, y;

  if (hand->n != 2)
    return FALSE;
  x = KB_VALUE (hand->cards[0]);
  y = KB_VALUE (hand->cards[1]);
  return (x == a && y == b) || (x == b && y == a);
}

gboolean
kb_kuppin (const KBHand *hand)
{
  return pair_of (hand, 9, 1);
}

gboolean
kb_shippin (const KBHand *hand)
{
  return pair_of (hand, 4, 1);
}

static gint
take (KBDeal *deal)
{
  return deal->stock[--deal->n_stock];
}

void
kb_deal (KBDeal *deal)
{
  gint i, j, same;

  do
    {
      for (i = 0; i < KB_CARDS; i++)
        deal->stock[i] = (gint8) i;
      for (i = KB_CARDS - 1; i > 0; i--)
        {
          gint k = games_rand_int (i + 1);
          gint8 t = deal->stock[i];

          deal->stock[i] = deal->stock[k];
          deal->stock[k] = t;
        }
      deal->n_stock = KB_CARDS;
      for (i = 0; i < KB_LANES; i++)
        {
          deal->lane[i].cards[0] = (gint8) take (deal);
          deal->lane[i].n = 1;
        }
      deal->dealer.cards[0] = (gint8) take (deal);
      deal->dealer.n = 1;

      /* Three lanes or more of one month: dealt again. */
      same = 0;
      for (i = 0; i < KB_LANES; i++)
        {
          gint n = 0;

          for (j = 0; j < KB_LANES; j++)
            if (KB_VALUE (deal->lane[j].cards[0])
                == KB_VALUE (deal->lane[i].cards[0]))
              n++;
          same = MAX (same, n);
        }
    }
  while (same >= 3);
}

void
kb_hit (KBDeal *deal, KBHand *hand)
{
  if (hand->n < KB_MOST && deal->n_stock > 0)
    hand->cards[hand->n++] = (gint8) take (deal);
}

KBChoice
kb_choice (const KBHand *lane)
{
  gint t = kb_total (lane);

  if (lane->n >= KB_MOST || kb_shippin (lane))
    return KB_STAND;
  if (t <= 3)
    return KB_MUST_DRAW;
  if (t <= 6 || t == 9)
    return KB_MAY_DRAW;
  return KB_STAND;
}

gboolean
kb_dealer_draws (const KBHand *dealer)
{
  return dealer->n < KB_MOST && !kb_kuppin (dealer)
    && kb_total (dealer) <= 5;
}

gint
kb_settle (const KBHand *lane, const KBHand *dealer, gboolean ties_to_dealer)
{
  gboolean mine = kb_arashi (lane), theirs = kb_arashi (dealer);

  if (mine && theirs)
    return kb_total (lane) > kb_total (dealer) ? 3 : -3;
  if (mine)
    return 3;
  if (theirs)
    return -3;
  if (kb_kuppin (dealer))
    return -1;
  if (kb_shippin (lane))
    return 1;
  if (kb_total (lane) == kb_total (dealer))
    return ties_to_dealer ? -1 : 0;
  return kb_total (lane) > kb_total (dealer) ? 1 : -1;
}
