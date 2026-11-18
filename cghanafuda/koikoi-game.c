/*
 * cghanafuda: Koi-Koi at the table
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

#include <stdlib.h>
#include <string.h>

#include "table.h"

#define HUMAN 0
#define COMPUTER 1

/* The table, in the game's pixels, laid out as on a real table, with
 * the cards in play, the field and the player's hand, as large as they
 * can be: the computer's hand across the table, half beyond its edge,
 * the stock at the left of the field, and the cards each player has
 * taken in front of them, drawn small. */
#define HAND_GAP 6              /* between cards in a hand or the field */
#define ROW_GAP 8               /* between the field's rows */
#define FIELD_GAP 16            /* above and below the field */
#define GROUP_GAP 10            /* between the kinds of cards taken */
#define TAKEN_STEP 12           /* across, between cards taken */
#define RAISE 12                /* a picked card stands up this much */

/* The rows, top to bottom. */
#define Y_THEIR_HAND (-CH / 2)
#define Y_THEIR_TAKEN (Y_THEIR_HAND + CH + ROW_GAP)
#define Y_FIELD (Y_THEIR_TAKEN + SMALL_H + FIELD_GAP)
#define Y_FIELD2 (Y_FIELD + CH + ROW_GAP)
#define Y_MY_TAKEN (Y_FIELD2 + CH + FIELD_GAP)
#define Y_MY_HAND (Y_MY_TAKEN + SMALL_H + ROW_GAP + RAISE)
#define COLS 8                  /* the table's width in cards: a hand */
#define KOIKOI_WIDTH (COLS * CW + (COLS - 1) * HAND_GAP + 2 * MARGIN)
#define KOIKOI_HEIGHT (Y_MY_HAND + CH + MARGIN)

/* The field's columns: the first FIELD_COLS in the middle of the
 * table, over the hand, the rest added at the right, away from the
 * stock. */
#define FIELD_COLS 4

/* How long the computer's moves are shown, in milliseconds. */
#define SHOW_MS 650

/* The game. */
static KKRound round_;
static gint round_number;       /* 1 to opts.rounds */
static gint rounds_this_game;
static gint total[2];
static gint next_dealer;
static gint dealt_stock;        /* the stock as the round was dealt */
static gboolean over = TRUE;    /* the last round played out */
static gchar *first_turned;     /* how the first dealer was chosen */

typedef enum
{
  PHASE_OVER,                   /* between rounds, or a dialog up */
  PHASE_PICK,                   /* the player to pick a card from the hand */
  PHASE_PICK_FIELD,             /* to pick one of two field cards for it */
  PHASE_DRAWN_FIELD,            /* to pick one of two for the drawn card */
  PHASE_COMPUTER                /* the computer's move is being shown */
} Phase;

static Phase phase = PHASE_OVER;
static gint picked = KK_NONE;   /* the hand card picked */
static gint drawn = KK_NONE;    /* the card turned from the stock */
static gint shown = KK_NONE;    /* the computer's card, face up */
static gint lit[4];             /* field cards lit as choices */
static gint n_lit;
static gboolean suggested;      /* the card picked is a hint's */
static gint suggest = KK_NONE;  /* the field card a hint would take */
static guint timer;

static void new_game (void);
static void after_turn (gint player);
static void computer_turn (void);

/* Where things are. */

static gint
hand_x (gint i, gint n)
{
  gint width = n * CW + (n - 1) * HAND_GAP;

  return (frame_width - width) / 2 + i * (CW + HAND_GAP);
}

/* Each field card keeps its place, a slot, while it lies there, and a
 * card laid down takes the first slot free. The slots fill columns of
 * two, top first, as many columns as are needed, closer together when
 * they are many. */
static gint slot_of[KK_CARDS];
static gint n_slots;

static void
place_field (void)
{
  gboolean on_field[KK_CARDS], used[KK_CARDS];
  gint i, c;

  memset (on_field, 0, sizeof on_field);
  memset (used, 0, sizeof used);
  for (i = 0; i < round_.n_field; i++)
    on_field[round_.field[i]] = TRUE;
  for (c = 0; c < KK_CARDS; c++)
    if (!on_field[c])
      slot_of[c] = -1;
    else if (slot_of[c] >= 0)
      used[slot_of[c]] = TRUE;
  for (i = 0; i < round_.n_field; i++)
    {
      gint s = 0;

      c = round_.field[i];
      if (slot_of[c] >= 0)
        continue;
      while (used[s])
        s++;
      slot_of[c] = s;
      used[s] = TRUE;
    }
  n_slots = 0;
  for (c = 0; c < KK_CARDS; c++)
    n_slots = MAX (n_slots, slot_of[c] + 1);
}

/* The left of the table, over the hand's first card, where the stock
 * lies. */
static gint
stock_x (void)
{
  return hand_x (0, COLS);
}

/* The field's slots fill columns of two, top first, left to right:
 * the first FIELD_COLS columns in the middle, over the hand, with a
 * card's width clear of the stock, and the rest further right, all
 * closer together when there is no more room. */
static void
field_xy (gint card, gint *x, gint *y)
{
  gint s = slot_of[card], col = s / 2;
  gint cols = MAX (FIELD_COLS, (n_slots + 1) / 2);
  gint first = hand_x ((COLS - FIELD_COLS) / 2, COLS);
  gint step = CW + HAND_GAP;

  if (cols > 1)
    step = MIN (step, (hand_x (COLS - 1, COLS) - first) / (cols - 1));
  *x = first + col * step;
  *y = s % 2 ? Y_FIELD2 : Y_FIELD;
}

static void
stock_xy (gint *x, gint *y)
{
  *x = stock_x ();
  *y = Y_FIELD;
}

static void
drawn_xy (gint *x, gint *y)
{
  *x = stock_x ();
  *y = Y_FIELD2;
}

/* The cards player has taken, by kind, a month at a time. */
static gint
taken_sorted (gint player, gint8 *sorted, gint *counts)
{
  gint n = 0, k, c, i;

  for (k = 0; k < 4; k++)
    {
      counts[k] = 0;
      for (c = 0; c < KK_CARDS; c++)
        if ((gint) kk_kind (c) == k)
          for (i = 0; i < round_.n_taken[player]; i++)
            if (round_.taken[player][i] == c)
              {
                sorted[n++] = (gint8) c;
                counts[k]++;
              }
    }
  return n;
}

/* The step between the cards taken, close enough for all to fit in
 * room. */
static gint
taken_step (const gint *counts, gint room)
{
  gint k, groups = 0, extra = 0, step;

  for (k = 0; k < 4; k++)
    if (counts[k])
      {
        groups++;
        extra += counts[k] - 1;
      }
  if (!extra)
    return TAKEN_STEP;
  step = (room - groups * SMALL_W - (groups - 1) * GROUP_GAP) / extra;
  return CLAMP (step, 2, TAKEN_STEP);
}

/* The cards player has taken, small, from x in a row at y. */
static void
draw_taken (gint player, gint x, gint y)
{
  gint8 sorted[KK_CARDS];
  gint counts[4], n = taken_sorted (player, sorted, counts);
  gint step = taken_step (counts, frame_width - table_side () - x), k, i, j = 0;

  (void) n;
  for (k = 0; k < 4; k++)
    {
      for (i = 0; i < counts[k]; i++)
        table_small_card (sorted[j++], x + i * step, y);
      if (counts[k])
        x += SMALL_W + (counts[k] - 1) * step + GROUP_GAP;
    }
}

static gboolean
is_lit (gint card)
{
  gint i;

  for (i = 0; i < n_lit; i++)
    if (lit[i] == card)
      return TRUE;
  return FALSE;
}

/* Whether any card in the player's hand can take one from the
 * field. */
static gboolean
any_match (void)
{
  gint i, match[4];

  for (i = 0; i < round_.n_hand[HUMAN]; i++)
    if (kk_matches (&round_, round_.hand[HUMAN][i], match))
      return TRUE;
  return FALSE;
}

/* What the player is to do now, in the status bar; and that, or
 * NULL. */
static const gchar *
show_prompt (void)
{
  const gchar *text = NULL;
  gint match[4], n;

  switch (phase)
    {
    case PHASE_PICK:
    case PHASE_PICK_FIELD:
      if (picked == KK_NONE)
        {
          if (!any_match ())
            text = _("Your turn: no card matches, so lay one down.");
          else if (opts.learning)
            text = _("Your turn: the lit cards can take one from the field.");
          else
            text = _("Your turn: play a card from your hand.");
          break;
        }
      n = kk_matches (&round_, picked, match);
      if (suggested)
        text = n == 0 ? _("Hint: lay the raised card down: click it again.")
          : n == 2 ? _("Hint: play the raised card, and take the marked one.")
          : _("Hint: play the raised card: click it again, or its match.");
      else
        text = n == 0 ? _("No match: click it again to lay it down.")
          : n == 2 ? _("Choose which of the two it takes.")
          : _("Click it again, or its match, to take.");
      break;
    case PHASE_DRAWN_FIELD:
      text = suggest != KK_NONE ? _("Hint: the drawn card takes the marked "
                                    "one.")
        : _("Choose which card the drawn one takes.");
      break;
    case PHASE_COMPUTER:
      text = _("The computer's turn.");
      break;
    default:
      break;
    }
  table_prompt (text);
  return text;
}

static void
draw (void)
{
  gint i, x, y, n;

  table_clear ();

  /* The computer's hand, face down across the table, and its cards
   * taken. */
  n = round_.n_hand[COMPUTER];
  for (i = 0; i < n; i++)
    if (round_.hand[COMPUTER][i] != shown)
      table_card (KK_NONE, hand_x (i, n), Y_THEIR_HAND);
  draw_taken (COMPUTER, table_side (), Y_THEIR_TAKEN);

  /* The stock, and the card turned from it. */
  stock_xy (&x, &y);
  table_pile (x, y, round_.n_stock);
  if (drawn != KK_NONE)
    {
      drawn_xy (&x, &y);
      table_card (drawn, x, y);
    }

  place_field ();
  for (i = 0; i < round_.n_field; i++)
    {
      field_xy (round_.field[i], &x, &y);
      table_card (round_.field[i], x, y);
      if (is_lit (round_.field[i]))
        table_light (x, y);
    }

  if (suggest != KK_NONE && slot_of[suggest] >= 0)
    {
      field_xy (suggest, &x, &y);
      table_mark (x, y);
    }

  draw_taken (HUMAN, table_side (), Y_MY_TAKEN);
  n = round_.n_hand[HUMAN];
  for (i = 0; i < n; i++)
    {
      gint c = round_.hand[HUMAN][i], match[4];

      table_card (c, hand_x (i, n),
            Y_MY_HAND - (c == picked ? RAISE : 0));
      /* For a player learning, the cards that can take one are lit
       * while one is to be picked. */
      if (opts.learning && phase == PHASE_PICK && picked == KK_NONE
          && kk_matches (&round_, c, match))
        table_light (hand_x (i, n), Y_MY_HAND);
    }

  /* The computer's card being played, face up and out of its hand,
   * over what lies under it. */
  n = round_.n_hand[COMPUTER];
  for (i = 0; i < n; i++)
    if (round_.hand[COMPUTER][i] == shown)
      table_card (shown, hand_x (i, n), Y_THEIR_HAND + CH / 2 + RAISE / 2);
  show_prompt ();
}

/* The status bar. */

static void
show_status (void)
{
  gchar text[64];

  g_snprintf (text, sizeof text, _("You: %d"), total[HUMAN]);
  table_status (2, text);
  g_snprintf (text, sizeof text, _("Computer: %d"), total[COMPUTER]);
  table_status (1, text);
  g_snprintf (text, sizeof text, _("Round %d of %d"), round_number,
              rounds_this_game);
  table_status (0, text);
}

/* The rounds. */

/* The yaku in player's cards, a line each, and their points. */
static gchar *
yaku_text (gint player, gint *points)
{
  KKYaku yaku[KK_MAX_YAKU];
  gint n, i;
  GString *s = g_string_new (NULL);
  gchar *text;

  *points = kk_yaku (&opts.rules, round_.taken[player],
                     round_.n_taken[player], yaku, &n);
  for (i = 0; i < n; i++)
    {
      g_string_sprintfa (s, "%s%s (%s", i ? "\n" : "", yaku[i].name,
                         _(yaku[i].meaning));
      if (yaku[i].extra > 0)
        g_string_sprintfa (s, _(", and %d more"), yaku[i].extra);
      g_string_sprintfa (s, "): %d", yaku[i].points);
    }
  /* GLib 1.2's g_string_free gives nothing back. */
  text = s->str;
  g_string_free (s, FALSE);
  return text;
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
  gchar *text;

  phase = PHASE_OVER;
  over = TRUE;
  if (total[HUMAN] > total[COMPUTER])
    text = g_strdup_printf (_("You win, %d to %d."), total[HUMAN],
                            total[COMPUTER]);
  else if (total[HUMAN] < total[COMPUTER])
    text = g_strdup_printf (_("The computer wins, %d to %d."),
                            total[COMPUTER], total[HUMAN]);
  else
    text = g_strdup_printf (_("A draw, %d all."), total[HUMAN]);
  labels[0] = _(buttons[0]);
  labels[1] = _(buttons[1]);
  labels[2] = NULL;
  games_choose (GTK_WINDOW (window), _(APPNAME_LONG), text, labels,
                game_over_answer, NULL);
  g_free (text);
}

static void start_round (void);

static void
round_answer (gint answer, gpointer data)
{
  (void) answer;
  (void) data;
  if (round_number >= rounds_this_game)
    game_over ();
  else
    {
      round_number++;
      start_round ();
    }
}

/* Ends the round: won by winner with points before doubling, or by no
 * one for KK_NONE; text says how. */
static void
end_round (gint winner, gint points, const gchar *how)
{
  static const gchar *const next[] = { N_("Next Round"), NULL };
  static const gchar *const last[] = { N_("See the Score"), NULL };
  const gchar *labels[2];
  gchar *text;

  if (timer)
    gtk_timeout_remove (timer);
  timer = 0;
  phase = PHASE_OVER;
  picked = drawn = shown = suggest = KK_NONE;
  suggested = FALSE;
  n_lit = 0;
  table_redraw ();

  if (winner == KK_NONE)
    {
      gboolean called = round_.koikoi[HUMAN] || round_.koikoi[COMPUTER];

      if (how)
        text = g_strdup (how);
      else if (called)
        text = g_strdup (_("The cards ran out after a koi-koi, before "
                           "another yaku:\nno one scores."));
      else if (opts.rules.dealer_privilege)
        {
          /* The dealer's privilege: the dealer takes 6 and deals
           * again. */
          winner = round_.dealer;
          total[winner] += 6;
          text = g_strdup (winner == HUMAN
                           ? _("No one made a yaku. You dealt, so you "
                               "take 6 points.")
                           : _("No one made a yaku. The computer dealt, "
                               "so it takes 6 points."));
        }
      else
        text = g_strdup (_("No one made a yaku: no one scores."));
      /* With no winner, the deal passes. */
      next_dealer = winner == KK_NONE ? 1 - round_.dealer : winner;
    }
  else
    {
      gint pay = kk_payout (&opts.rules, &round_, winner, points);

      total[winner] += pay;
      next_dealer = winner;
      if (pay != points)
        text = g_strdup_printf (ngettext ("%s\n\n%s\n\n%d point, doubled to "
                                          "%d.",
                                          "%s\n\n%s\n\n%d points, doubled to "
                                          "%d.", points),
                                winner == HUMAN ? _("You win the round.")
                                : _("The computer wins the round."),
                                how, points, pay);
      else
        text = g_strdup_printf (ngettext ("%s\n\n%s\n\n%d point.",
                                          "%s\n\n%s\n\n%d points.", points),
                                winner == HUMAN ? _("You win the round.")
                                : _("The computer wins the round."),
                                how, points);
    }
  show_status ();
  labels[0] = _(round_number >= rounds_this_game ? last[0] : next[0]);
  labels[1] = NULL;
  games_choose (GTK_WINDOW (window), _(APPNAME_LONG), text, labels,
                round_answer, NULL);
  g_free (text);
}

static void
start_turn (gint player)
{
  if (round_.n_hand[HUMAN] == 0 && round_.n_hand[COMPUTER] == 0)
    {
      end_round (KK_NONE, 0, NULL);
      return;
    }
  if (round_.n_hand[player] == 0)
    player = 1 - player;
  if (player == HUMAN)
    {
      phase = PHASE_PICK;
      picked = KK_NONE;
      n_lit = 0;
      table_redraw ();
    }
  else
    computer_turn ();
}

static gint
by_card (const void *a, const void *b)
{
  return *(const gint8 *) a - *(const gint8 *) b;
}

static void
start_round (void)
{
  gint lucky = kk_deal (&round_, next_dealer), c;

  /* The player's hand in order of the months; the field's cards each
   * in a place of their own. */
  qsort (round_.hand[HUMAN], (gsize) round_.n_hand[HUMAN], 1, by_card);
  dealt_stock = round_.n_stock;
  for (c = 0; c < KK_CARDS; c++)
    slot_of[c] = -1;

  picked = drawn = shown = KK_NONE;
  n_lit = 0;
  show_status ();
  table_redraw ();
  if (lucky == KK_BOTH)
    {
      end_round (KK_NONE, 0,
                 _("Both hands as dealt are four of a month or four pairs:"
                   "\nno one scores."));
      return;
    }
  if (lucky != KK_NONE)
    {
      /* A hand that wins as dealt: 6 points. */
      end_round (lucky, 6, lucky == HUMAN
                 ? _("Your hand as dealt wins: four of a month, or four pairs.")
                 : _("The computer's hand as dealt wins: four of a month, "
                     "or four pairs."));
      return;
    }
  if (first_turned)
    {
      table_message (first_turned);
      g_free (first_turned);
      first_turned = NULL;
    }
  else
    table_message (round_.dealer == HUMAN ? _("You deal, and lead.")
                   : _("The computer deals, and leads."));
  start_turn (round_.dealer);
}

static void
stop (void)
{
  if (timer)
    gtk_timeout_remove (timer);
  timer = 0;
  phase = PHASE_OVER;
}

static gboolean
in_play (void)
{
  return !over && (round_number > 1 || round_.n_stock < dealt_stock);
}

/* Each turns a card for the first deal, and the earlier month deals:
 * said in the status bar. */
static void
first_dealer (void)
{
  gint turned[2];
  gchar *text;

  next_dealer = kk_first_dealer (turned);
  text = g_strdup_printf
    (next_dealer == HUMAN
     ? _("You turned %s (%s), the computer %s (%s): you deal.")
     : _("You turned %s (%s), the computer %s (%s): the computer deals."),
     _(kk_card_name (turned[HUMAN])),
     _(kk_month_name (KK_MONTH (turned[HUMAN]))),
     _(kk_card_name (turned[COMPUTER])),
     _(kk_month_name (KK_MONTH (turned[COMPUTER]))));
  first_turned = text;
}

static void
new_game (void)
{
  stop ();
  over = FALSE;
  total[HUMAN] = total[COMPUTER] = 0;
  round_number = 1;
  rounds_this_game = opts.rounds;
  first_dealer ();
  start_round ();
}

/* Koi-koi or stop. */

static void
decide_answer (gint answer, gpointer data)
{
  gint points = GPOINTER_TO_INT (data);

  if (answer == 1)
    {
      round_.koikoi[HUMAN]++;
      round_.points[HUMAN] = points;
      table_message (opts.rules.koikoi_again ? _("Koi-koi! Play on.")
                     : _("Koi-koi! The next yaku ends the round."));
      start_turn (COMPUTER);
    }
  else
    {
      gint p;
      gchar *how = yaku_text (HUMAN, &p);

      end_round (HUMAN, points, how);
      g_free (how);
    }
}

/* After player's turn: stop or koi-koi on new points, or on to the
 * other. */
static void
after_turn (gint player)
{
  gint points;
  gchar *how = yaku_text (player, &points);

  drawn = KK_NONE;
  n_lit = 0;
  table_redraw ();
  if (points > round_.points[player])
    {
      /* With no cards left to play on, or after a koi-koi when it may
       * be called only once, the first new yaku ends the round. */
      gboolean last = round_.n_hand[player] == 0 || round_.n_stock == 0
        || (!opts.rules.koikoi_again
            && (round_.koikoi[HUMAN] || round_.koikoi[COMPUTER]));

      if (player == HUMAN && !last)
        {
          static const gchar *const buttons[] = { N_("Stop"), N_("Koi-Koi"),
            NULL };
          const gchar *labels[3];
          gchar *text = g_strdup_printf (ngettext ("%s\n\n%d point. Stop and "
                                                   "take it, or call koi-koi "
                                                   "and play on for more?",
                                                   "%s\n\n%d points. Stop and "
                                                   "take them, or call koi-koi "
                                                   "and play on for more?",
                                                   points), how, points);

          if (opts.learning)
            {
              gchar *more = g_strdup_printf
                ("%s\n\n%s\n%s", text,
                 opts.rules.double_koikoi
                 ? _("If you play on and the computer makes a yaku first, "
                     "it wins\nthe round with its points doubled, and yours "
                     "count for nothing.")
                 : _("If you play on and the computer makes a yaku first, "
                     "it wins\nthe round, and yours count for nothing."),
                 kk_ai_koikoi (&opts.rules, &round_, HUMAN, points)
                 ? _("In your place, the computer would call koi-koi.")
                 : _("In your place, the computer would stop."));

              g_free (text);
              text = more;
            }

          phase = PHASE_OVER;
          labels[0] = _(buttons[0]);
          labels[1] = _(buttons[1]);
          labels[2] = NULL;
          games_choose (GTK_WINDOW (window), _(APPNAME_LONG), text, labels,
                        decide_answer, GINT_TO_POINTER (points));
          g_free (text);
          g_free (how);
          return;
        }
      if (player == COMPUTER && !last
          && kk_ai_koikoi (&opts.rules, &round_, player, points))
        {
          round_.koikoi[COMPUTER]++;
          round_.points[COMPUTER] = points;
          table_message (opts.rules.double_koikoi
                         ? _("The computer calls koi-koi: a yaku of yours "
                             "now wins, doubled.")
                         : _("The computer calls koi-koi: a yaku of yours "
                             "now wins."));
          g_free (how);
          start_turn (HUMAN);
          return;
        }
      end_round (player, points, how);
      g_free (how);
      return;
    }
  g_free (how);
  start_turn (1 - player);
}

/* The player's moves. */

static gint
light_matches (gint card)
{
  n_lit = kk_matches (&round_, card, lit);
  return n_lit;
}

static gint
draw_timeout (gpointer data)
{
  gint player = GPOINTER_TO_INT (data);

  timer = 0;
  if (player == HUMAN && light_matches (drawn) == 2)
    {
      /* Two to choose from: the player says which. */
      phase = PHASE_DRAWN_FIELD;
      table_redraw ();
      return FALSE;
    }
  kk_play (&round_, player, drawn,
           player == HUMAN ? KK_NONE
           : kk_ai_choose (&opts.rules, &round_, player, drawn));
  after_turn (player);
  return FALSE;
}

/* Turns the top card of the stock, and plays it after a moment. */
static void
draw_card (gint player)
{
  drawn = kk_draw (&round_);
  n_lit = 0;
  if (drawn == KK_NONE)
    {
      after_turn (player);
      return;
    }
  light_matches (drawn);
  phase = player == HUMAN ? PHASE_OVER : PHASE_COMPUTER;
  table_redraw ();
  timer = gtk_timeout_add (SHOW_MS, draw_timeout, GINT_TO_POINTER (player));
}

static void
play_picked (gint chosen)
{
  gint card = picked;

  kk_from_hand (&round_, HUMAN, card);
  kk_play (&round_, HUMAN, card, chosen);
  picked = KK_NONE;
  suggested = FALSE;
  suggest = KK_NONE;
  n_lit = 0;
  draw_card (HUMAN);
}

/* Which card of player's hand, the field or the drawn card is at
 * (x, y); KK_NONE if none. *where is 0 for the hand, 1 the field. */
static gint
card_at (gint x, gint y, gint *where)
{
  gint i, n, cx, cy;

  n = round_.n_hand[HUMAN];
  for (i = n - 1; i >= 0; i--)
    {
      gint c = round_.hand[HUMAN][i];

      cx = hand_x (i, n);
      cy = Y_MY_HAND - (c == picked ? RAISE : 0);
      if (x >= cx && x < cx + CW && y >= cy && y < cy + CH)
        {
          *where = 0;
          return c;
        }
    }
  for (i = round_.n_field - 1; i >= 0; i--)
    {
      field_xy (round_.field[i], &cx, &cy);
      if (x >= cx && x < cx + CW && y >= cy && y < cy + CH)
        {
          *where = 1;
          return round_.field[i];
        }
    }
  return KK_NONE;
}

static void
press (gint x, gint y, gint button)
{
  gint where, card = card_at (x, y, &where);

  if (button != 1 || card == KK_NONE)
    return;

  switch (phase)
    {
    case PHASE_PICK:
    case PHASE_PICK_FIELD:
      if (where == 0 && card != picked)
        {
          /* A card picked: its matches light up; a second click plays
           * it, or a click on a match. */
          gint n;

          picked = card;
          suggested = FALSE;
          suggest = KK_NONE;
          n = light_matches (card);
          phase = n == 2 ? PHASE_PICK_FIELD : PHASE_PICK;
          table_redraw ();
        }
      else if (where == 0 && phase == PHASE_PICK)
        play_picked (KK_NONE);
      else if (where == 1 && picked != KK_NONE && is_lit (card))
        play_picked (card);
      break;

    case PHASE_DRAWN_FIELD:
      if (where == 1 && is_lit (card))
        {
          suggest = KK_NONE;
          kk_play (&round_, HUMAN, drawn, card);
          after_turn (HUMAN);
        }
      break;

    default:
      break;
    }
}

/* The computer's moves, shown a step at a time. */

static gint computer_chosen;

static gint
computer_play_timeout (gpointer data)
{
  gint card = shown;

  (void) data;
  timer = 0;
  shown = KK_NONE;
  kk_from_hand (&round_, COMPUTER, card);
  kk_play (&round_, COMPUTER, card, computer_chosen);
  n_lit = 0;
  draw_card (COMPUTER);
  return FALSE;
}

static void
computer_turn (void)
{
  phase = PHASE_COMPUTER;
  picked = KK_NONE;
  shown = kk_ai_card (&opts.rules, &round_, COMPUTER, &computer_chosen);
  /* Its card is turned up, and what it takes lit, for a moment. */
  n_lit = 0;
  if (computer_chosen != KK_NONE)
    {
      gint match[4], n = kk_matches (&round_, shown, match), i;

      if (n == 2)
        {
          lit[0] = computer_chosen;
          n_lit = 1;
        }
      else
        for (i = 0; i < n; i++)
          lit[n_lit++] = match[i];
    }
  table_redraw ();
  timer = gtk_timeout_add (SHOW_MS, computer_play_timeout, NULL);
}

/* For a player learning: what the card under the pointer is, and a
 * hint. */

/* The yaku a card is part of. */
static const gchar *
card_yaku (gint card)
{
  switch (card)
    {
    case KK_RAIN_MAN:
      return _("For Goko and Ame-shiko only.");
    case KK_CURTAIN:
      return _("For the bright yaku and Hanami-zake.");
    case KK_MOON:
      return _("For the bright yaku and Tsukimi-zake.");
    case KK_SAKE:
      return opts.rules.sake_is_plain
        ? _("For both viewing yaku, Tane and Kasu.")
        : _("For both viewing yaku and Tane.");
    case KK_BOAR: case KK_DEER: case KK_BUTTERFLIES:
      return _("For Ino-shika-cho and Tane.");
    case KK_PINE_POEM: case KK_PLUM_POEM: case KK_CHERRY_POEM:
      return _("For Akatan and Tan.");
    case KK_PEONY_BLUE: case KK_CHRYSANTHEMUM_BLUE: case KK_MAPLE_BLUE:
      return _("For Aotan and Tan.");
    }
  switch (kk_kind (card))
    {
    case KK_BRIGHT: return _("For the bright yaku.");
    case KK_ANIMAL: return _("For Tane.");
    case KK_RIBBON: return _("For Tan.");
    default: return _("For Kasu.");
    }
}

/* "March, cherry: the curtain, a bright. For ...", newly allocated. */
static gchar *
describe (gint card)
{
  static const gchar *const kinds[] = {
    N_("a bright"), N_("an animal"), N_("a ribbon"), N_("a plain card")
  };
  const gchar *name = _(kk_card_name (card));
  gint month = KK_MONTH (card);

  if (kk_kind (card) == KK_PLAIN && name[0] == 'a')
    return g_strdup_printf ("%s, %s: %s. %s", _(kk_month_name (month)),
                            _(kk_flower_name (month)), name,
                            card_yaku (card));
  return g_strdup_printf ("%s, %s: %s, %s. %s", _(kk_month_name (month)),
                          _(kk_flower_name (month)), name,
                          _(kinds[kk_kind (card)]), card_yaku (card));
}

/* The card player has taken that is at (x, y), as draw_taken lays them
 * from x0 at y0; KK_NONE if none. */
static gint
taken_at (gint player, gint x0, gint y0, gint x, gint y)
{
  gint8 sorted[KK_CARDS];
  gint counts[4], n = taken_sorted (player, sorted, counts);
  gint step = taken_step (counts, frame_width - table_side () - x0), k, i, j = 0;
  gint found = KK_NONE;

  (void) n;
  if (y < y0 || y >= y0 + SMALL_H)
    return KK_NONE;
  for (k = 0; k < 4; k++)
    {
      /* Later cards lie over earlier ones: the last under the pointer
       * is the one seen. */
      for (i = 0; i < counts[k]; i++, j++)
        if (x >= x0 + i * step && x < x0 + i * step + SMALL_W)
          found = sorted[j];
      if (counts[k])
        x0 += SMALL_W + (counts[k] - 1) * step + GROUP_GAP;
    }
  return found;
}

static void
motion (gint x, gint y)
{
  gint where, card, sx, sy, n = round_.n_hand[COMPUTER];
  gchar *text = NULL;

  if (!opts.learning || x < 0 || over)
    {
      table_hint (NULL);
      return;
    }
  card = card_at (x, y, &where);
  if (card == KK_NONE && drawn != KK_NONE)
    {
      drawn_xy (&sx, &sy);
      if (x >= sx && x < sx + CW && y >= sy && y < sy + CH)
        card = drawn;
    }
  if (card == KK_NONE)
    card = taken_at (HUMAN, table_side (), Y_MY_TAKEN, x, y);
  if (card == KK_NONE)
    card = taken_at (COMPUTER, table_side (), Y_THEIR_TAKEN, x, y);
  if (card != KK_NONE)
    text = describe (card);
  else
    {
      stock_xy (&sx, &sy);
      if (round_.n_stock && x >= sx && x < sx + CW + PILE_REACH
          && y >= sy - PILE_REACH && y < sy + CH)
        text = g_strdup_printf (ngettext ("The stock: %d card.",
                                          "The stock: %d cards.",
                                          round_.n_stock), round_.n_stock);
      else if (n && y < Y_THEIR_HAND + CH && x >= hand_x (0, n)
               && x < hand_x (n - 1, n) + CW)
        text = g_strdup_printf (ngettext ("The computer's hand: %d card.",
                                          "The computer's hand: %d cards.", n),
                                n);
    }
  table_hint (text);
  g_free (text);
}

/* The computer's choice in the player's place, shown: the card it would
 * play raised, as if picked, and the field card it would take marked
 * when there are two. */
static void
hint (void)
{
  gint chosen;

  switch (phase)
    {
    case PHASE_PICK:
    case PHASE_PICK_FIELD:
      picked = kk_ai_card (&opts.rules, &round_, HUMAN, &chosen);
      phase = light_matches (picked) == 2 ? PHASE_PICK_FIELD : PHASE_PICK;
      suggest = phase == PHASE_PICK_FIELD ? chosen : KK_NONE;
      suggested = TRUE;
      table_redraw ();
      /* Said over the name of the card under the pointer too. */
      table_message (show_prompt ());
      break;
    case PHASE_DRAWN_FIELD:
      suggest = kk_ai_choose (&opts.rules, &round_, HUMAN, drawn);
      table_redraw ();
      table_message (show_prompt ());
      break;
    default:
      table_message (_("A hint is for when it is your turn."));
      break;
    }
}

/* Who has taken card, for the charts. */
static gint
owner (gint card)
{
  gint p, i;

  for (p = 0; p < 2; p++)
    for (i = 0; i < round_.n_taken[p]; i++)
      if (round_.taken[p][i] == card)
        return p;
  return -1;
}

/* Help. */

static void
help (void)
{
  GString *s = g_string_new (NULL);

  g_string_append (s, _("Match a card from your hand to one of the same "
                        "month on the field,\n"
                        "then turn a card from the stock and do the same. "
                        "Cards matched are yours.\n"
                        "With a yaku, stop and take its points, or call "
                        "koi-koi and play on.\n"));
  g_string_append (s, opts.rules.koikoi_again
                   ? _("If the other makes a yaku first, they win the "
                       "round.\n\n")
                   : _("After a koi-koi, the next yaku ends the round, "
                       "whoever makes it.\n\n"));
  g_string_append (s, _("Goko: the five brights: 10\n"
                        "Shiko: four brights without the rain man: 8\n"
                        "Ame-shiko: four brights with the rain man: 7\n"
                        "Sanko: three brights without the rain man: 5\n"
                        "Ino-shika-cho: the boar, the deer and the "
                        "butterflies: 5, and 1 for each other animal\n"
                        "Akatan: the three poem ribbons: 5, and 1 for each "
                        "other ribbon\n"
                        "Aotan: the three blue ribbons: 5, and 1 for each "
                        "other ribbon\n"
                        "Akatan-aotan: both: 10, and 1 for each other "
                        "ribbon\n"
                        "Hanami-zake: the curtain and the sake cup: 5\n"
                        "Tsukimi-zake: the moon and the sake cup: 5\n"
                        "Tane: five animals: 1, and 1 for each more\n"
                        "Tan: five ribbons: 1, and 1 for each more\n"
                        "Kasu: ten plain cards: 1, and 1 for each more\n"));
  if (opts.rules.double_seven)
    g_string_append (s, _("\nSeven points or more count double."));
  if (opts.rules.double_koikoi)
    g_string_append (s, _("\nWinning after the other's koi-koi doubles "
                          "the points."));
  g_string_append (s, _("\n\nHelp, Yaku shows each yaku's cards, and "
                        "who has them; Help, Cards\n"
                        "shows the months. Game, Hint shows what the "
                        "computer would play."));
  games_message (GTK_WINDOW (window), _("Koi-Koi"), s->str);
  g_string_free (s, TRUE);
}

const Game koikoi_game = {
  "koikoi", N_("Koi-Koi"), KOIKOI_WIDTH, KOIKOI_HEIGHT,
  new_game, stop, in_play, draw, press, help, motion, hint, owner
};
