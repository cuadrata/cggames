/*
 * cghanafuda: Oicho-Kabu at the table
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

#include <string.h>

#include "kabu.h"
#include "table.h"

/* The player's chips at the start, and the most a round can carry. */
#define START_CHIPS 50
#define MOST_BET 10

/* The table, in the game's pixels: the dealer's row, then the lanes,
 * each a column of cards down from the one dealt first with the chips
 * bet on it piled at its right, all of it centred in a larger table. */
#define Y_DEALER (MARGIN + PILE_REACH)
#define DEALER_GAP 8            /* between the dealer's cards */
#define Y_LANES (Y_DEALER + CH + 30)
#define LANE_STEP 44            /* down, between a lane's cards */
#define LANE_H (CH + (KB_MOST - 1) * LANE_STEP)
#define LANE_GAP 44             /* right of each lane, where its chips go */
#define LANES_W (KB_LANES * (CW + LANE_GAP))
#define CHIP_R 16               /* a chip's radius */
#define CHIP_STEP 5             /* up, between the chips piled on a lane */
#define Y_CHIPS (Y_LANES + CH - CHIP_R) /* the lowest chip's middle */
#define KABU_WIDTH (LANES_W + 2 * MARGIN)
#define KABU_HEIGHT (Y_LANES + LANE_H + MARGIN)
#define CHIP_EDGE 0x9b2d20
#define CHIP_FACE 0xf4ecd8

/* How long each card dealt is shown before the next, in milliseconds. */
#define SHOW_MS 650

typedef enum
{
  PHASE_IDLE,                   /* a dialog up, or nothing begun */
  PHASE_BET,                    /* the player to bet on the lanes */
  PHASE_PLAY                    /* the cards being dealt */
} Phase;

static Phase phase = PHASE_IDLE;
static KBDeal deal;
static gint bet[KB_LANES];
static gint won[KB_LANES];      /* each lane's chips, once settled */
static gint chips;
static gint round_number, rounds_this_game;
static gint lane_at;            /* the lane being dealt to, or -1 */
static gboolean choosing;       /* the player to draw or stand for it */
static gboolean revealed;       /* the dealer's first card turned up */
static gboolean settled;
static gboolean over = TRUE;    /* the last round played out */
static guint timer;
/* What a hint marked: a lane to bet on, the stock to draw, or a lane to
 * stand; -1 for nothing, STOCK_MARK for the stock. */
#define STOCK_MARK KB_LANES
static gint marked = -1;

/* What comes after the card shown. */
typedef void (*Step) (void);
static Step next_step;

static void start_round (void);

/* Where things are. */

static gint
top (void)
{
  return (frame_height - KABU_HEIGHT) / 2;
}

static gint
lane_x (gint i)
{
  return (frame_width - LANES_W) / 2 + i * (CW + LANE_GAP);
}

/* The stock, above the first lane. */
static void
stock_xy (gint *x, gint *y)
{
  *x = lane_x (0);
  *y = top () + Y_DEALER;
}

static gint
dealer_x (gint i)
{
  gint n = MAX (deal.dealer.n, 1);
  gint width = n * CW + (n - 1) * DEALER_GAP;

  return (frame_width - width) / 2 + i * (CW + DEALER_GAP);
}

static gboolean
on_stock (gint x, gint y)
{
  gint sx, sy;

  stock_xy (&sx, &sy);
  return x >= sx && x < sx + CW + PILE_REACH
    && y >= sy - PILE_REACH && y < sy + CH;
}

/* The lane whose cards or chips are at (x, y), or -1. */
static gint
lane_at_point (gint x, gint y)
{
  gint i;

  y -= top ();
  if (y < Y_LANES || y >= Y_LANES + LANE_H)
    return -1;
  for (i = 0; i < KB_LANES; i++)
    if (x >= lane_x (i) && x < lane_x (i) + CW + LANE_GAP)
      return i;
  return -1;
}

static gint
total_bet (void)
{
  gint i, n = 0;

  for (i = 0; i < KB_LANES; i++)
    n += bet[i];
  return n;
}

/* Drawing. */

static void
chip (gint x, gint y)
{
  table_disc (x, y, CHIP_R, CHIP_EDGE);
  table_disc (x, y, CHIP_R - 2, CHIP_FACE);
  table_disc (x, y, CHIP_R / 2 + 1, CHIP_EDGE);
  table_disc (x, y, CHIP_R / 2 - 1, CHIP_FACE);
}

/* Whether a click on the stock deals, or draws a card. */
static gboolean
stock_live (void)
{
  return (phase == PHASE_BET && total_bet ()) || choosing;
}

static void
draw (void)
{
  gint y0 = top (), i, k, x, y;

  table_clear ();

  /* The stock, lit when a click on it deals or draws. */
  stock_xy (&x, &y);
  table_pile (x, y, deal.n_stock);
  if (stock_live ())
    table_light_cards (x, y - PILE_REACH, x + PILE_REACH, y);
  for (i = 0; i < deal.dealer.n; i++)
    table_card (i == 0 && !revealed ? KK_NONE : deal.dealer.cards[i],
                dealer_x (i), y0 + Y_DEALER);

  for (i = 0; i < KB_LANES; i++)
    {
      const KBHand *lane = &deal.lane[i];
      gint lx = lane_x (i), ly = y0 + Y_LANES;

      for (k = 0; k < lane->n; k++)
        table_card (lane->cards[k], lx, ly + k * LANE_STEP);
      if ((choosing && i == lane_at) || (settled && won[i] > 0))
        table_light_cards (lx, ly, lx, ly + (lane->n - 1) * LANE_STEP);
      for (k = 0; k < bet[i]; k++)
        chip (lx + CW + LANE_GAP / 2, y0 + Y_CHIPS - k * CHIP_STEP);
      if (marked == i)
        table_mark (lx, ly + (lane->n - 1) * LANE_STEP);
    }
  if (marked == STOCK_MARK)
    {
      stock_xy (&x, &y);
      table_mark (x, y);
    }
}

static void
show_status (void)
{
  gchar text[64];

  g_snprintf (text, sizeof text, _("Chips: %d"), chips);
  table_status (2, text);
  g_snprintf (text, sizeof text, _("Bet: %d"), total_bet ());
  table_status (1, text);
  g_snprintf (text, sizeof text, _("Round %d of %d"), round_number,
              rounds_this_game);
  table_status (0, text);
}

/* Some chips, in words. */
static gchar *
chips_text (gint n)
{
  return g_strdup_printf (ngettext ("%d chip", "%d chips", n), n);
}

/* The steps of a round, each card shown a moment. */

static gint
step_timeout (gpointer data)
{
  (void) data;
  timer = 0;
  next_step ();
  return FALSE;
}

static void
later (Step step)
{
  next_step = step;
  timer = gtk_timeout_add (SHOW_MS, step_timeout, NULL);
}

static void
game_over_answer (gint answer, gpointer data)
{
  (void) data;
  if (answer == 0)
    table_new_game ();
}

static void
game_over (void)
{
  static const gchar *const buttons[] = { N_("Play Again"), N_("Close"),
    NULL };
  const gchar *labels[3];
  gchar *text, *n = chips_text (ABS (chips - START_CHIPS));

  over = TRUE;
  phase = PHASE_IDLE;
  if (chips == 0)
    text = g_strdup (_("You have lost all your chips."));
  else if (chips > START_CHIPS)
    text = g_strdup_printf (_("You finish with %d chips, %s more than "
                              "you began with."), chips, n);
  else if (chips < START_CHIPS)
    text = g_strdup_printf (_("You finish with %d chips, %s fewer than "
                              "you began with."), chips, n);
  else
    text = g_strdup_printf (_("You finish with %d chips, as many as you "
                              "began with."), chips);
  labels[0] = _(buttons[0]);
  labels[1] = _(buttons[1]);
  labels[2] = NULL;
  games_choose (GTK_WINDOW (window), _(APPNAME_LONG), text, labels,
                game_over_answer, NULL);
  g_free (text);
  g_free (n);
}

static void
round_answer (gint answer, gpointer data)
{
  (void) answer;
  (void) data;
  if (chips == 0 || round_number >= rounds_this_game)
    game_over ();
  else
    {
      round_number++;
      start_round ();
    }
}

/* What a hand has: arashi, kuppin or shippin, or its worth. */
static gchar *
hand_text (const gchar *who, const KBHand *hand, gboolean dealer)
{
  if (kb_arashi (hand))
    return g_strdup_printf (_("%s has arashi, three of a month, making %d"),
                            who, kb_total (hand));
  if (dealer && kb_kuppin (hand))
    return g_strdup_printf (_("%s has kuppin, nine and one"), who);
  if (!dealer && kb_shippin (hand))
    return g_strdup_printf (_("%s has shippin, four and one"), who);
  return g_strdup_printf (_("%s makes %d"), who, kb_total (hand));
}

/* Settles the bets, and says how they went. */
static void
settle (void)
{
  static const gchar *const next[] = { N_("Next Round"), NULL };
  static const gchar *const last[] = { N_("See the Score"), NULL };
  const gchar *labels[2];
  GString *s = g_string_new (NULL);
  gchar *line, *text;
  gint i, net = 0;

  line = hand_text (_("The dealer"), &deal.dealer, TRUE);
  g_string_sprintfa (s, "%s.\n\n", line);
  g_free (line);
  for (i = 0; i < KB_LANES; i++)
    {
      gchar *lane, *what, *n;

      if (!bet[i])
        continue;
      won[i] = bet[i] * kb_settle (&deal.lane[i], &deal.dealer,
                                   opts.ties_to_dealer);
      net += won[i];
      lane = g_strdup_printf (_("Lane %d"), i + 1);
      what = hand_text (lane, &deal.lane[i], FALSE);
      n = chips_text (ABS (won[i]));
      if (won[i] > 0)
        g_string_sprintfa (s, _("%s: you win %s.\n"), what, n);
      else if (won[i] < 0)
        g_string_sprintfa (s, _("%s: you lose %s.\n"), what, n);
      else
        g_string_sprintfa (s, _("%s, the same: your bet stands.\n"), what);
      g_free (lane);
      g_free (what);
      g_free (n);
    }
  chips = MAX (chips + net, 0);
  if (net)
    {
      gchar *n = chips_text (ABS (net));

      g_string_sprintfa (s, net > 0 ? _("\nYou win %s.") : _("\nYou lose %s."),
                         n);
      g_free (n);
    }
  else
    g_string_append (s, _("\nYou come out even."));

  settled = TRUE;
  phase = PHASE_IDLE;
  lane_at = -1;
  show_status ();
  table_redraw ();

  /* GLib 1.2's g_string_free gives nothing back. */
  text = s->str;
  g_string_free (s, FALSE);
  labels[0] = _(chips == 0 || round_number >= rounds_this_game
                ? last[0] : next[0]);
  labels[1] = NULL;
  games_choose (GTK_WINDOW (window), _(APPNAME_LONG), text, labels,
                round_answer, NULL);
  g_free (text);
}

static void
dealer_third (void)
{
  kb_hit (&deal, &deal.dealer);
  table_redraw ();
  later (settle);
}

static void
dealer_second (void)
{
  kb_hit (&deal, &deal.dealer);
  table_redraw ();
  later (kb_dealer_draws (&deal.dealer) ? dealer_third : settle);
}

/* The lanes played: the dealer turns its card and plays its hand. */
static void
dealer_turn (void)
{
  revealed = TRUE;
  lane_at = -1;
  table_message (_("The dealer turns up its card."));
  table_redraw ();
  later (dealer_second);
}

static void next_lane (void);

static void
hit_lane (void)
{
  kb_hit (&deal, &deal.lane[lane_at]);
  table_redraw ();
  later (next_lane);
}

/* On to the next lane bet on: it draws, stands, or the player says. */
static void
next_lane (void)
{
  const KBHand *lane;
  gchar *text;

  do
    lane_at++;
  while (lane_at < KB_LANES && !bet[lane_at]);
  if (lane_at >= KB_LANES)
    {
      dealer_turn ();
      return;
    }
  lane = &deal.lane[lane_at];
  switch (kb_choice (lane))
    {
    case KB_MUST_DRAW:
      text = g_strdup_printf (_("Lane %d makes %d, and takes a card."),
                              lane_at + 1, kb_total (lane));
      table_message (text);
      later (hit_lane);
      break;
    case KB_MAY_DRAW:
      text = g_strdup_printf (_("Lane %d makes %d: click the stock to draw "
                                "a card, or the lane to stand."),
                              lane_at + 1, kb_total (lane));
      table_message (text);
      choosing = TRUE;
          table_redraw ();
      break;
    default:
      text = kb_shippin (lane)
        ? g_strdup_printf (_("Lane %d has shippin, and stands."), lane_at + 1)
        : g_strdup_printf (_("Lane %d makes %d, and stands."), lane_at + 1,
                           kb_total (lane));
      table_message (text);
      later (next_lane);
      break;
    }
  g_free (text);
}

/* The bets placed: a second card to each lane, and a third face up to
 * those no one bet on; then the lanes bet on are played in turn. */
static void
deal_cards (void)
{
  gint i;

  phase = PHASE_PLAY;
  for (i = 0; i < KB_LANES; i++)
    kb_hit (&deal, &deal.lane[i]);
  for (i = 0; i < KB_LANES; i++)
    if (!bet[i])
      kb_hit (&deal, &deal.lane[i]);
  lane_at = -1;
  table_redraw ();
  later (next_lane);
}

static void
draw_card (void)
{
  choosing = FALSE;
  hit_lane ();
}

static void
stand (void)
{
  choosing = FALSE;
  table_redraw ();
  next_lane ();
}

/* Betting, a click puts a chip on a lane, the right button takes one
 * back, and a click on the stock deals; a lane that may draw draws at a
 * click on the stock, and stands at one on the lane. */
static void
press (gint x, gint y, gint mouse)
{
  gint i = lane_at_point (x, y);

  marked = -1;

  if (mouse == 1 && choosing)
    {
      if (on_stock (x, y))
        draw_card ();
      else if (i == lane_at)
        stand ();
      return;
    }
  if (phase == PHASE_BET && mouse == 1 && on_stock (x, y))
    {
      if (total_bet ())
        deal_cards ();
      else
        table_message (_("Bet a chip on a lane or more first."));
      return;
    }
  if (phase != PHASE_BET || i < 0)
    return;
  if (mouse == 1)
    {
      if (total_bet () >= MOST_BET)
        {
          gchar *text = g_strdup_printf (_("No more than %d chips a round."),
                                         MOST_BET);

          table_message (text);
          g_free (text);
          return;
        }
      if (total_bet () >= chips)
        {
          table_message (_("You have no more chips to bet."));
          return;
        }
      bet[i]++;
    }
  else if (mouse == 3 && bet[i] > 0)
    bet[i]--;
  else
    return;
  show_status ();
  table_redraw ();
}

static void
start_round (void)
{
  kb_deal (&deal);
  memset (bet, 0, sizeof bet);
  memset (won, 0, sizeof won);
  lane_at = -1;
  marked = -1;
  choosing = revealed = settled = FALSE;
  phase = PHASE_BET;
  table_message (_("Click a lane to bet a chip on it, right-click to "
                   "take one back; then click the stock to deal."));
  show_status ();
  table_redraw ();
}

static void
stop (void)
{
  if (timer)
    gtk_timeout_remove (timer);
  timer = 0;
  choosing = FALSE;
  phase = PHASE_IDLE;
}

static gboolean
in_play (void)
{
  return !over && (round_number > 1 || phase != PHASE_BET || total_bet ());
}

static void
new_game (void)
{
  stop ();
  over = FALSE;
  chips = START_CHIPS;
  round_number = 1;
  rounds_this_game = opts.rounds;
  start_round ();
}

/* For a player learning the game: what is under the pointer. */

/* A card, by its month and its worth. */
static gchar *
card_text (gint card)
{
  gint month = KK_MONTH (card);

  return g_strdup_printf (_("%s, %s: worth %d."), _(kk_month_name (month)),
                          _(kk_flower_name (month)), KB_VALUE (card));
}

/* A hand's cards and what they make. */
static gchar *
lane_text (gint i)
{
  const KBHand *lane = &deal.lane[i];
  gchar *text, *bets = NULL;

  if (bet[i])
    bets = chips_text (bet[i]);
  if (lane->n == 1)
    text = bet[i]
      ? g_strdup_printf (_("Lane %d: %d so far, with %s bet."), i + 1,
                         kb_total (lane), bets)
      : g_strdup_printf (_("Lane %d: %d so far."), i + 1, kb_total (lane));
  else if (kb_arashi (lane))
    text = g_strdup_printf (_("Lane %d: arashi, three of a month."), i + 1);
  else if (kb_shippin (lane))
    text = g_strdup_printf (_("Lane %d: shippin, four and one."), i + 1);
  else
    text = bet[i]
      ? g_strdup_printf (_("Lane %d makes %d, with %s bet."), i + 1,
                         kb_total (lane), bets)
      : g_strdup_printf (_("Lane %d makes %d."), i + 1, kb_total (lane));
  g_free (bets);
  return text;
}

static void
motion (gint x, gint y)
{
  gchar *text = NULL;
  gint i, k, y0 = top ();

  if (!opts.learning || x < 0 || over)
    {
      table_hint (NULL);
      return;
    }
  /* A lane's cards, the top one first, then its chips. */
  i = lane_at_point (x, y);
  if (i >= 0)
    {
      const KBHand *lane = &deal.lane[i];
      gint lx = lane_x (i);

      for (k = lane->n - 1; k >= 0 && !text; k--)
        {
          gint cy = y0 + Y_LANES + k * LANE_STEP;

          if (x >= lx && x < lx + CW && y >= cy && y < cy + CH)
            {
              gchar *card = card_text (lane->cards[k]);
              gchar *hand = lane_text (i);

              text = g_strdup_printf ("%s %s", card, hand);
              g_free (card);
              g_free (hand);
            }
        }
      if (!text)
        text = lane_text (i);
    }
  for (k = deal.dealer.n - 1; k >= 0 && !text; k--)
    if (x >= dealer_x (k) && x < dealer_x (k) + CW
        && y >= y0 + Y_DEALER && y < y0 + Y_DEALER + CH)
      {
        if (k == 0 && !revealed)
          text = g_strdup (_("The dealer's first card, face down until the "
                             "lanes are played."));
        else
          {
            gchar *card = card_text (deal.dealer.cards[k]);

            text = revealed
              ? g_strdup_printf (_("%s The dealer makes %d."), card,
                                 kb_total (&deal.dealer))
              : g_strdup (card);
            g_free (card);
          }
      }
  if (!text && on_stock (x, y))
    text = g_strdup_printf (ngettext ("The stock: %d card.",
                                      "The stock: %d cards.", deal.n_stock),
                            deal.n_stock);
  table_hint (text);
  g_free (text);
}

/* Hints: deals played out many times over from the cards the player
 * can't see, the stock and the dealer's first card. */

#define TRIALS 4000

static guint32 hint_seed = 0x9e3779b9;

static gint
hint_rand (gint n)
{
  hint_seed ^= hint_seed << 13;
  hint_seed ^= hint_seed >> 17;
  hint_seed ^= hint_seed << 5;
  return (gint) (hint_seed % (guint32) n);
}

/* The cards unseen, and the dealer's hand with its first card taken out
 * to be drawn from them. */
static gint
unseen (gint8 *pool)
{
  gint n = 0, i;

  for (i = 0; i < deal.n_stock; i++)
    pool[n++] = deal.stock[i];
  if (!revealed && deal.dealer.n)
    pool[n++] = deal.dealer.cards[0];
  return n;
}

/* Plays a lane out against the dealer from pool, shuffled as it goes;
 * draws a third card at may_draw if it may. Returns what the bet wins. */
static gint
play_out (KBHand lane, gint8 *pool, gint n, gboolean may_draw)
{
  KBHand dealer;
  gint left = n;

#define DRAW(hand) do { gint r = hint_rand (left); gint8 t = pool[r]; \
    pool[r] = pool[--left]; pool[left] = t; \
    (hand).cards[(hand).n++] = t; } while (0)

  if (lane.n < 2)
    DRAW (lane);
  switch (kb_choice (&lane))
    {
    case KB_MUST_DRAW:
      DRAW (lane);
      break;
    case KB_MAY_DRAW:
      if (may_draw)
        DRAW (lane);
      break;
    default:
      break;
    }
  dealer.n = 0;
  DRAW (dealer);
  DRAW (dealer);
  if (kb_dealer_draws (&dealer))
    DRAW (dealer);
#undef DRAW
  return kb_settle (&lane, &dealer, opts.ties_to_dealer);
}

/* How often a lane wins and loses, in per cent, and what it wins on
 * average in hundredths of a bet, drawing at may_draw. */
static void
odds (const KBHand *lane, gboolean may_draw, gint *wins, gint *losses,
      gint *gain)
{
  gint8 pool[KB_CARDS];
  gint n = unseen (pool), t, w = 0, l = 0, g = 0;

  for (t = 0; t < TRIALS; t++)
    {
      gint r = play_out (*lane, pool, n, may_draw);

      w += r > 0;
      l += r < 0;
      g += r;
    }
  *wins = (w * 100 + TRIALS / 2) / TRIALS;
  *losses = (l * 100 + TRIALS / 2) / TRIALS;
  *gain = (g * 100) / TRIALS;
}

/* Drawing at 4 to 6 and standing at 9, as a careful player does: the
 * choice the bet hint plays each lane out with. */
static gboolean
would_draw (const KBHand *lane)
{
  return kb_total (lane) <= 6;
}

static void
hint (void)
{
  gchar *text;

  if (choosing)
    {
      const KBHand *lane = &deal.lane[lane_at];
      gint dw, dl, dg, sw, sl, sg;

      odds (lane, TRUE, &dw, &dl, &dg);
      odds (lane, FALSE, &sw, &sl, &sg);
      marked = dg > sg ? STOCK_MARK : lane_at;
      text = g_strdup_printf
        (dg > sg ? _("Draw: lane %d wins %d%% of the time drawing and %d%% "
                     "standing, and loses %d%% and %d%%.")
         : _("Stand: lane %d wins %d%% of the time standing and %d%% "
             "drawing, and loses %d%% and %d%%."),
         lane_at + 1, dg > sg ? dw : sw, dg > sg ? sw : dw,
         dg > sg ? dl : sl, dg > sg ? sl : dl);
    }
  else if (phase == PHASE_BET)
    {
      gint i, best = 0, w[KB_LANES], l[KB_LANES], g[KB_LANES];

      for (i = 0; i < KB_LANES; i++)
        {
          odds (&deal.lane[i], would_draw (&deal.lane[i]), &w[i], &l[i],
                &g[i]);
          if (g[i] > g[best])
            best = i;
        }
      marked = best;
      text = g_strdup_printf (_("Lane %d is the best bet: it wins %d%% of "
                                "the time and loses %d%%."), best + 1,
                              w[best], l[best]);
    }
  else
    {
      table_message (_("A hint is for when it is your turn."));
      return;
    }
  table_redraw ();
  table_message (text);
  g_free (text);
}

static void
help (void)
{
  gchar *text = g_strdup_printf
    (_("Oicho-Kabu is played with the forty cards of January to October,\n"
       "each worth its month, January 1 to October 10. A hand is worth\n"
       "the last digit of its sum: 9 is best, and 10 or 20 is nothing.\n\n"
       "The dealer lays a card face up on each of four lanes, and one\n"
       "face down for itself. Click a lane to bet a chip on it, up to %d\n"
       "a round, right-click to take one back, then click the stock to\n"
       "deal. Each lane gets a second card, and those not bet on a third.\n"
       "A lane bet on that makes 3 or less takes a third too; one that\n"
       "makes 4 to 6, or 9, may: click the stock to draw, or the lane to\n"
       "stand. One that makes 7 or 8 stands. Then the dealer turns its\n"
       "card, takes a second, and a third if it makes 5 or less.\n\n"
       "A lane worth more than the dealer wins its bet, and one worth\n"
       "less loses it; a tie stands off, or goes to the dealer if the\n"
       "options say so.\n\n"
       "Arashi: three of a month: wins three times the bet, or the\n"
       "dealer's takes three times; if both have it, the higher wins.\n"
       "Kuppin: the dealer's 9 and 1: beats every lane but arashi.\n"
       "Shippin: a lane's 4 and 1: beats every hand of the dealer's\n"
       "but arashi and kuppin.\n\n"
       "Game, Hint plays the deal out many times from the cards you\n"
       "can't see, and marks the best lane to bet on, or whether to\n"
       "draw or stand."), MOST_BET);

  games_message (GTK_WINDOW (window), _("Oicho-Kabu"), text);
  g_free (text);
}

const Game kabu_game = {
  "kabu", N_("Oicho-Kabu"), KABU_WIDTH, KABU_HEIGHT,
  new_game, stop, in_play, draw, press, help, motion, hint, NULL
};
