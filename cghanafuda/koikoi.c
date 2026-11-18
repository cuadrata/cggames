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

/* The rules are Nintendo's, as it gives them for its own cards: five
 * brights 10, four without the rain man 8, four with him 7, three
 * without him 5; boar, deer and butterflies 5, and 1 more for each
 * animal beyond them; the three poem ribbons 5, the three blue ribbons
 * 5, and 1 more for each ribbon beyond, or both sets 10, and 1 more for
 * each beyond them; the curtain or the moon with the sake cup 5 each;
 * and five animals, five ribbons or ten plain cards 1, and 1 more for
 * each card beyond, where boar, deer and butterflies or the ribbons'
 * sets don't already count them. */

#include "config.h"

#include <string.h>

#include "games.h"
#include "koikoi.h"

static const KKKind kinds[KK_CARDS] = {
  /* January: crane and sun, poem ribbon */
  KK_BRIGHT, KK_RIBBON, KK_PLAIN, KK_PLAIN,
  /* February: bush warbler, poem ribbon */
  KK_ANIMAL, KK_RIBBON, KK_PLAIN, KK_PLAIN,
  /* March: curtain, poem ribbon */
  KK_BRIGHT, KK_RIBBON, KK_PLAIN, KK_PLAIN,
  /* April: cuckoo, red ribbon */
  KK_ANIMAL, KK_RIBBON, KK_PLAIN, KK_PLAIN,
  /* May: eight-plank bridge, red ribbon */
  KK_ANIMAL, KK_RIBBON, KK_PLAIN, KK_PLAIN,
  /* June: butterflies, blue ribbon */
  KK_ANIMAL, KK_RIBBON, KK_PLAIN, KK_PLAIN,
  /* July: boar, red ribbon */
  KK_ANIMAL, KK_RIBBON, KK_PLAIN, KK_PLAIN,
  /* August: moon, geese */
  KK_BRIGHT, KK_ANIMAL, KK_PLAIN, KK_PLAIN,
  /* September: sake cup, blue ribbon */
  KK_ANIMAL, KK_RIBBON, KK_PLAIN, KK_PLAIN,
  /* October: deer, blue ribbon */
  KK_ANIMAL, KK_RIBBON, KK_PLAIN, KK_PLAIN,
  /* November: rain man, swallow, red ribbon, lightning */
  KK_BRIGHT, KK_ANIMAL, KK_RIBBON, KK_PLAIN,
  /* December: phoenix */
  KK_BRIGHT, KK_PLAIN, KK_PLAIN, KK_PLAIN
};

KKKind
kk_kind (gint card)
{
  return kinds[card];
}

static const gchar *const month_names[12] = {
  N_("January"), N_("February"), N_("March"), N_("April"), N_("May"),
  N_("June"), N_("July"), N_("August"), N_("September"), N_("October"),
  N_("November"), N_("December")
};

static const gchar *const flower_names[12] = {
  N_("pine"), N_("plum"), N_("cherry"), N_("wisteria"), N_("iris"),
  N_("peony"), N_("bush clover"), N_("pampas grass"), N_("chrysanthemum"),
  N_("maple"), N_("willow"), N_("paulownia")
};

/* The cards with names of their own; the rest are plain. */
static const struct
{
  gint card;
  const gchar *name;
} card_names[] = {
  { 0, N_("the crane and the sun") }, { 1, N_("a poem ribbon") },
  { 4, N_("the bush warbler") }, { 5, N_("a poem ribbon") },
  { 8, N_("the curtain") }, { 9, N_("a poem ribbon") },
  { 12, N_("the cuckoo") }, { 13, N_("a red ribbon") },
  { 16, N_("the eight-plank bridge") }, { 17, N_("a red ribbon") },
  { 20, N_("the butterflies") }, { 21, N_("a blue ribbon") },
  { 24, N_("the boar") }, { 25, N_("a red ribbon") },
  { 28, N_("the moon") }, { 29, N_("the geese") },
  { 32, N_("the sake cup") }, { 33, N_("a blue ribbon") },
  { 36, N_("the deer") }, { 37, N_("a blue ribbon") },
  { 40, N_("the rain man") }, { 41, N_("the swallow") },
  { 42, N_("a red ribbon") }, { 43, N_("the lightning") },
  { 44, N_("the phoenix") }
};

const gchar *
kk_month_name (gint month)
{
  return month_names[month];
}

const gchar *
kk_flower_name (gint month)
{
  return flower_names[month];
}

const gchar *
kk_card_name (gint card)
{
  gsize i;

  for (i = 0; i < sizeof card_names / sizeof card_names[0]; i++)
    if (card_names[i].card == card)
      return card_names[i].name;
  return N_("a plain card");
}

static void
add_yaku (KKYaku *yaku, gint *n, const gchar *name, const gchar *meaning,
          gint points, gint extra, KKKind extra_kind)
{
  if (yaku && *n < KK_MAX_YAKU)
    {
      yaku[*n].name = name;
      yaku[*n].meaning = meaning;
      yaku[*n].points = points;
      yaku[*n].extra = extra;
      yaku[*n].extra_kind = extra_kind;
    }
  (*n)++;
}

gint
kk_yaku (const KKRules *rules, const gint8 *taken, gint n_taken,
         KKYaku *yaku, gint *n_yaku)
{
  gboolean has[KK_CARDS];
  gint n = 0, total = 0, i, brights = 0, animals = 0, ribbons = 0;
  gint plain = 0, y;
  gboolean poems, blues, viewing;

  memset (has, 0, sizeof has);
  for (i = 0; i < n_taken; i++)
    {
      has[taken[i]] = TRUE;
      switch (kk_kind (taken[i]))
        {
        case KK_BRIGHT: brights++; break;
        case KK_ANIMAL: animals++; break;
        case KK_RIBBON: ribbons++; break;
        case KK_PLAIN: plain++; break;
        }
    }
  if (rules->sake_is_plain && has[KK_SAKE])
    plain++;

#define YAKU(name, meaning, points, extra, kind) \
  do { y = (points); add_yaku (yaku, &n, name, meaning, y, extra, kind); \
       total += y; } while (0)

  if (brights == 5)
    YAKU ("Goko", N_("Five brights"), 10, 0, KK_BRIGHT);
  else if (brights == 4)
    {
      if (has[KK_RAIN_MAN])
        YAKU ("Ame-shiko", N_("Four brights with rain"), 7, 0, KK_BRIGHT);
      else
        YAKU ("Shiko", N_("Four brights"), 8, 0, KK_BRIGHT);
    }
  else if (brights == 3 && !has[KK_RAIN_MAN])
    YAKU ("Sanko", N_("Three brights"), 5, 0, KK_BRIGHT);

  /* Boar, deer and butterflies count the other animals with them, so
   * five animals are not a yaku of their own too; nor are five ribbons
   * with either set of three. */
  if (has[KK_BOAR] && has[KK_DEER] && has[KK_BUTTERFLIES])
    YAKU ("Ino-shika-cho", N_("Boar, deer and butterflies"),
          5 + animals - 3, animals - 3, KK_ANIMAL);
  else if (animals >= 5)
    YAKU ("Tane", N_("Five animals"), animals - 4, animals - 5, KK_ANIMAL);

  poems = has[KK_PINE_POEM] && has[KK_PLUM_POEM] && has[KK_CHERRY_POEM];
  blues = has[KK_PEONY_BLUE] && has[KK_CHRYSANTHEMUM_BLUE]
    && has[KK_MAPLE_BLUE];
  if (poems && blues)
    YAKU ("Akatan-aotan", N_("Poem and blue ribbons"), 10 + ribbons - 6,
          ribbons - 6, KK_RIBBON);
  else if (poems)
    YAKU ("Akatan", N_("Poem ribbons"), 5 + ribbons - 3, ribbons - 3,
          KK_RIBBON);
  else if (blues)
    YAKU ("Aotan", N_("Blue ribbons"), 5 + ribbons - 3, ribbons - 3,
          KK_RIBBON);
  else if (ribbons >= 5)
    YAKU ("Tan", N_("Five ribbons"), ribbons - 4, ribbons - 5, KK_RIBBON);

  viewing = !(rules->rain_spoils && has[KK_RAIN_MAN]);
  if (viewing && has[KK_SAKE] && has[KK_CURTAIN])
    YAKU ("Hanami-zake", N_("Cherry blossom viewing"), 5, 0, KK_ANIMAL);
  if (viewing && has[KK_SAKE] && has[KK_MOON])
    YAKU ("Tsukimi-zake", N_("Moon viewing"), 5, 0, KK_ANIMAL);

  if (plain >= 10)
    YAKU ("Kasu", N_("Ten plain cards"), plain - 9, plain - 10, KK_PLAIN);
#undef YAKU

  if (n_yaku)
    *n_yaku = MIN (n, KK_MAX_YAKU);
  return total;
}

/* Dealing. */

static void
remove_at (gint8 *cards, gint *n, gint i)
{
  memmove (cards + i, cards + i + 1, (gsize) (*n - i - 1));
  (*n)--;
}

static gint
find (const gint8 *cards, gint n, gint card)
{
  gint i;

  for (i = 0; i < n; i++)
    if (cards[i] == card)
      return i;
  return -1;
}

/* Whether a hand of eight wins at once: all four of a month, or four
 * pairs. */
static gboolean
lucky_hand (const gint8 *hand, gint n)
{
  gint count[12] = { 0 }, i, pairs = 0;

  for (i = 0; i < n; i++)
    count[KK_MONTH (hand[i])]++;
  for (i = 0; i < 12; i++)
    {
      if (count[i] == 4)
        return TRUE;
      if (count[i] == 2)
        pairs++;
    }
  return pairs == 4;
}

gint
kk_first_dealer (gint *turned)
{
  for (;;)
    {
      gint a = games_rand_int (KK_CARDS), b = games_rand_int (KK_CARDS - 1);

      /* Two cards of the deck, not the same one. */
      if (b >= a)
        b++;
      turned[0] = a;
      turned[1] = b;
      /* Card numbers run by month, and within a month from the card
       * worth most; two plain cards of a month are worth the same. */
      if (KK_MONTH (a) != KK_MONTH (b)
          || kk_kind (a) != kk_kind (b))
        return a < b ? 0 : 1;
    }
}

gint
kk_deal (KKRound *round, gint dealer)
{
  gint8 deck[KK_CARDS];
  gint i, j, count[12];
  gboolean again;

  do
    {
      for (i = 0; i < KK_CARDS; i++)
        deck[i] = (gint8) i;
      for (i = KK_CARDS - 1; i > 0; i--)
        {
          gint8 t;

          j = games_rand_int (i + 1);
          t = deck[i];
          deck[i] = deck[j];
          deck[j] = t;
        }
      memset (round, 0, sizeof *round);
      round->dealer = dealer;
      /* Two at a time, to the field, the other player and the dealer,
       * as Nintendo says to deal them. */
      for (i = 0; i < 4; i++)
        for (j = 0; j < 2; j++)
          {
            round->field[round->n_field++] = deck[i * 6 + j];
            round->hand[1 - dealer][round->n_hand[1 - dealer]++]
              = deck[i * 6 + 2 + j];
            round->hand[dealer][round->n_hand[dealer]++]
              = deck[i * 6 + 4 + j];
          }
      for (i = 24; i < KK_CARDS; i++)
        round->stock[round->n_stock++] = deck[i];

      /* All of a month on the field can't be played: deal again. */
      memset (count, 0, sizeof count);
      again = FALSE;
      for (i = 0; i < round->n_field; i++)
        if (++count[KK_MONTH (round->field[i])] == 4)
          again = TRUE;
    }
  while (again);

  i = lucky_hand (round->hand[dealer], round->n_hand[dealer]);
  j = lucky_hand (round->hand[1 - dealer], round->n_hand[1 - dealer]);
  if (i && j)
    return KK_BOTH;
  if (i)
    return dealer;
  if (j)
    return 1 - dealer;
  return KK_NONE;
}

gint
kk_matches (const KKRound *round, gint card, gint *match)
{
  gint i, n = 0;

  for (i = 0; i < round->n_field; i++)
    if (KK_MONTH (round->field[i]) == KK_MONTH (card))
      match[n++] = round->field[i];
  return n;
}

static void
take (KKRound *round, gint player, gint card)
{
  gint i = find (round->field, round->n_field, card);

  if (i >= 0)
    remove_at (round->field, &round->n_field, i);
  round->taken[player][round->n_taken[player]++] = (gint8) card;
}

gint
kk_play (KKRound *round, gint player, gint card, gint chosen)
{
  gint match[4], n = kk_matches (round, card, match), i;

  if (n == 0)
    {
      round->field[round->n_field++] = (gint8) card;
      return 0;
    }
  round->taken[player][round->n_taken[player]++] = (gint8) card;
  if (n == 2)
    {
      if (chosen != match[1])
        chosen = match[0];
      take (round, player, chosen);
      return 2;
    }
  for (i = 0; i < n; i++)
    take (round, player, match[i]);
  return n + 1;
}

void
kk_from_hand (KKRound *round, gint player, gint card)
{
  gint i = find (round->hand[player], round->n_hand[player], card);

  if (i >= 0)
    remove_at (round->hand[player], &round->n_hand[player], i);
}

gint
kk_draw (KKRound *round)
{
  if (round->n_stock == 0)
    return KK_NONE;
  return round->stock[--round->n_stock];
}

gint
kk_payout (const KKRules *rules, const KKRound *round, gint winner,
           gint points)
{
  if (rules->double_seven && points >= 7)
    points *= 2;
  if (rules->double_koikoi && round->koikoi[1 - winner])
    points *= 2;
  return points;
}

/* The computer. */

/* What a card is worth to have, the pieces of the larger yaku more. */
static gint
worth (gint card)
{
  switch (card)
    {
    case KK_SAKE:
      return 16;
    case KK_BOAR: case KK_DEER: case KK_BUTTERFLIES:
      return 13;
    case KK_PINE_POEM: case KK_PLUM_POEM: case KK_CHERRY_POEM:
    case KK_PEONY_BLUE: case KK_CHRYSANTHEMUM_BLUE: case KK_MAPLE_BLUE:
      return 9;
    case KK_RAIN_MAN:
      return 12;
    }
  switch (kk_kind (card))
    {
    case KK_BRIGHT: return 20;
    case KK_ANIMAL: return 8;
    case KK_RIBBON: return 5;
    default: return 1;
    }
}

/* What taking cards adds to player: their worth, and much more for the
 * points they make. */
static gint
gain (const KKRules *rules, const KKRound *round, gint player,
      const gint *cards, gint n)
{
  gint8 taken[KK_CARDS];
  gint m = round->n_taken[player], i, value = 0, before, after;

  memcpy (taken, round->taken[player], (gsize) m);
  before = kk_yaku (rules, taken, m, NULL, NULL);
  for (i = 0; i < n; i++)
    {
      taken[m++] = (gint8) cards[i];
      value += worth (cards[i]);
    }
  after = kk_yaku (rules, taken, m, NULL, NULL);
  return value + 30 * (after - before);
}

/* What playing card with chosen as the field card to take is worth. */
static gint
play_value (const KKRules *rules, const KKRound *round, gint player,
            gint card, gint chosen)
{
  gint match[4], n = kk_matches (round, card, match), cards[5], k = 0, i;

  if (n == 0)
    {
      /* Left on the field, it is the other's to take: worse the more
       * it is worth, and worse still if it pairs with one there for
       * them. */
      gint other[1] = { card };

      return -gain (rules, round, 1 - player, other, 1) / 2 - 2;
    }
  cards[k++] = card;
  if (n == 2)
    cards[k++] = chosen;
  else
    for (i = 0; i < n; i++)
      cards[k++] = match[i];
  return gain (rules, round, player, cards, k);
}

gint
kk_ai_choose (const KKRules *rules, const KKRound *round, gint player,
              gint card)
{
  gint match[4], n = kk_matches (round, card, match);

  if (n != 2)
    return n ? match[0] : KK_NONE;
  return play_value (rules, round, player, card, match[0])
    >= play_value (rules, round, player, card, match[1])
    ? match[0] : match[1];
}

gint
kk_ai_card (const KKRules *rules, const KKRound *round, gint player,
            gint *chosen)
{
  gint best = KK_NONE, best_value = G_MININT, ties = 0, i;

  for (i = 0; i < round->n_hand[player]; i++)
    {
      gint card = round->hand[player][i];
      gint pick = kk_ai_choose (rules, round, player, card);
      gint value = play_value (rules, round, player, card, pick);

      /* Equal choices are taken at random, so play varies. */
      if (value > best_value)
        {
          best_value = value;
          best = card;
          *chosen = pick;
          ties = 1;
        }
      else if (value == best_value && games_rand_int (++ties) == 0)
        {
          best = card;
          *chosen = pick;
        }
    }
  return best;
}

/* Whether player is a card away from more points, with that card still
 * to be had. */
static gboolean
close_to_points (const KKRules *rules, const KKRound *round, gint player)
{
  gint8 taken[KK_CARDS];
  gint m = round->n_taken[player], card, now;
  gboolean gone[KK_CARDS];
  gint p, i;

  memset (gone, 0, sizeof gone);
  for (p = 0; p < 2; p++)
    for (i = 0; i < round->n_taken[p]; i++)
      gone[round->taken[p][i]] = TRUE;
  memcpy (taken, round->taken[player], (gsize) m);
  now = kk_yaku (rules, taken, m, NULL, NULL);
  for (card = 0; card < KK_CARDS; card++)
    {
      if (gone[card])
        continue;
      taken[m] = (gint8) card;
      if (kk_yaku (rules, taken, m + 1, NULL, NULL) > now)
        return TRUE;
    }
  return FALSE;
}

gboolean
kk_ai_koikoi (const KKRules *rules, const KKRound *round, gint player,
              gint points)
{
  /* Nothing left to play, or the other near points, or enough already:
   * stop. Otherwise press on while there are cards to do it with. */
  if (round->n_hand[player] == 0 || round->n_stock == 0)
    return FALSE;
  if (close_to_points (rules, round, 1 - player))
    return FALSE;
  if (points >= 7)
    return FALSE;
  return round->n_hand[player] >= 4 && points < 5;
}
