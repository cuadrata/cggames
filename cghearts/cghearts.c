/*
 * cghearts, the Hearts card game against the computer
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

#include "games.h"
#include "games-cards.h"

#include "icons/cghearts.xpm"

#define APPNAME "cghearts"
#define APPNAME_LONG N_("Hearts")

#define ELEMENTS(x) (sizeof (x) / sizeof (x [0]))

/* The cards: a card is its rank, 1 to 13, with its suit above that; 0
 * is none. Aces are high. */

typedef guint8 Card;

#define CARD(suit, rank) ((Card) ((suit) << 4 | (rank)))
#define RANK(c) ((c) & 0x0f)
#define SUIT(c) (((c) >> 4) & 3)
#define VALUE(c) (RANK (c) == GAMES_ACE ? 14 : RANK (c))

#define TWO_OF_CLUBS CARD (GAMES_CLUBS, 2)
#define QUEEN_OF_SPADES CARD (GAMES_SPADES, GAMES_QUEEN)

/* The players, in the order they play: the player at the bottom, then
 * round the table to the left. */
enum { SOUTH, WEST, NORTH, EAST, N_PLAYERS };

static const gchar *const player_names[N_PLAYERS] = {
  N_("You"), N_("West"), N_("North"), N_("East")
};

/* The passes, in turn from the first hand: to the left, to the right,
 * across, and none. */
enum { PASS_LEFT, PASS_RIGHT, PASS_ACROSS, PASS_NONE };

#define GAME_POINTS 100         /* the game ends once someone has these */
#define MAX_HANDS 64

typedef enum
{
  PASSING,                      /* the player choosing cards to pass */
  PLAYING,
  TRICK_TAKEN,                  /* a full trick shown before it goes */
  HAND_OVER,                    /* the score sheet up */
  GAME_OVER
} Phase;

static struct
{
  Card hand[N_PLAYERS][13];
  gint n[N_PLAYERS];
  Card trick[N_PLAYERS];        /* in the order played */
  gint n_trick;
  gint leader;                  /* who led the trick */
  gint turn;                    /* who plays next */
  gint tricks;                  /* tricks taken this hand */
  gboolean broken;              /* a heart played */
  gboolean queen_gone;          /* the queen of spades played */
  gint taken[N_PLAYERS];        /* points taken this hand */
  gint hand_no;                 /* from 0 */
  Phase phase;
  /* The player's cards to pass, and those passed to the player, raised
   * until the first card is played. */
  gboolean chosen[13];
  gboolean received[13];
  /* The scores after each hand. */
  gint scores[MAX_HANDS][N_PLAYERS];
  gint n_scores;
} game;

static struct
{
  gint back;
} opts;

static GamesConfig *config;
static GamesCards *cards;
static gint card_width, card_height;

static GtkWidget *window;
static GtkWidget *table;
static GtkWidget *statusbar;
static guint prompt_context;
static GtkWidget *score_labels[N_PLAYERS];
static GtkWidget *deck_dialog;
static GtkWidget *score_dialog;
static guint play_timeout;

#define COMPUTER_MS 450         /* between the computer's cards */
#define TRICK_MS 1100           /* a full trick shown */

/* Layout. */

#define TABLE_COLOUR 0x008000
#define ARROW_COLOUR 0xf0f0f0
#define ARROW_DIM 0x006000
#define MARGIN 8
#define HAND_STEP 36            /* across the player's hand */
#define BACK_STEP 20            /* along the others' */
#define RAISE 24                /* a card chosen or passed to the player */
#define TRICK_OFFSET 44         /* each card of a trick from the middle */

/* The table drawn, as in cgmahjong: the frame is the drawing area's
 * width by height in device pixels, device of them to the toolkit's.
 * The table is laid out in the game's pixels, the cards' own; a game's
 * pixel is zoom of the toolkit's and scale of the device's, zoom what
 * fits the table at its usual size to the window, the cards drawn at
 * that size; frame_width by frame_height of them fill it. */
static guchar *frame;
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
/* The middle of the trick. */
static gint mid_x, mid_y;

static void new_game (void);
static void next_hand (void);
static gint roomy_width (void);
static gint roomy_height (void);
static void redraw (void);
static void schedule (gint ms);

/* The status bar. */

static void
prompt (const gchar *text)
{
  gtk_statusbar_pop (GTK_STATUSBAR (statusbar), prompt_context);
  gtk_statusbar_push (GTK_STATUSBAR (statusbar), prompt_context, text);
}

static void
show_scores (void)
{
  gint p;

  for (p = 0; p < N_PLAYERS; p++)
    {
      gchar text[32];
      gint total = game.n_scores ? game.scores[game.n_scores - 1][p] : 0;

      g_snprintf (text, sizeof text, "%s: %d", _(player_names[p]), total);
      gtk_label_set_text (GTK_LABEL (score_labels[p]), text);
    }
}

/* Drawing. */

/* Lays a card-sized RGBA image over the frame at (x, y), turned on its
 * side if sideways, so that it is card_height wide. */
static void
blit (const guchar *src, gint x, gint y, gboolean sideways)
{
  gint cw, ch, w, h, i, j;

  games_cards_pixel_size (cards, &cw, &ch);
  w = sideways ? ch : cw;
  h = sideways ? cw : ch;
  x = DEV (x);
  y = DEV (y);
  for (j = MAX (0, -y); j < h && y + j < DEVICE_HEIGHT; j++)
    for (i = MAX (0, -x); i < w && x + i < DEVICE_WIDTH; i++)
      {
        const guchar *s = sideways ? src + ((gsize) i * cw + (cw - 1 - j)) * 4
          : src + ((gsize) j * cw + i) * 4;
        guchar *d = frame + ((gsize) (y + j) * DEVICE_WIDTH + x + i) * 3;
        guint a = s[3], k;

        if (a == 0)
          continue;
        for (k = 0; k < 3; k++)
          d[k] = (guchar) ((s[k] * a + d[k] * (255 - a) + 127) / 255);
      }
}

static void
draw_face (Card card, gint x, gint y)
{
  blit (games_cards_face (cards, SUIT (card), RANK (card)), x, y, FALSE);
}

static void
draw_back (gint x, gint y, gboolean sideways)
{
  blit (games_cards_back (cards, opts.back), x, y, sideways);
}

/* Where card i of the player's hand is: raised if chosen or passed. */
static void
hand_pos (gint i, gint *x, gint *y)
{
  gint n = game.n[SOUTH];

  *x = (frame_width - ((n - 1) * HAND_STEP + card_width)) / 2 + i * HAND_STEP;
  *y = frame_height - MARGIN - card_height;
  if ((game.phase == PASSING && game.chosen[i]) || game.received[i])
    *y -= RAISE;
}

/* Where each player's card of the trick goes. */
static void
trick_pos (gint p, gint *x, gint *y)
{
  static const gint dx[N_PLAYERS] = { 0, -1, 0, 1 };
  static const gint dy[N_PLAYERS] = { 1, 0, -1, 0 };

  *x = mid_x - card_width / 2 + dx[p] * TRICK_OFFSET * 3 / 2;
  *y = mid_y - card_height / 2 + dy[p] * TRICK_OFFSET;
}

/* The others' hands, face down, half off the edges of the table. */
static void
draw_others (void)
{
  gint i, n;

  n = game.n[NORTH];
  for (i = 0; i < n; i++)
    draw_back ((frame_width - ((n - 1) * BACK_STEP + card_width)) / 2
               + i * BACK_STEP, -card_height / 2, FALSE);
  n = game.n[WEST];
  for (i = 0; i < n; i++)
    draw_back (-card_height / 2, mid_y - ((n - 1) * BACK_STEP + card_width) / 2
               + i * BACK_STEP, TRUE);
  n = game.n[EAST];
  for (i = 0; i < n; i++)
    draw_back (frame_width - card_height / 2,
               mid_y - ((n - 1) * BACK_STEP + card_width) / 2 + i * BACK_STEP,
               TRUE);
}

/* The arrow the cards are passed with, pointing the way they go: lit
 * once three are chosen. */
static gboolean
in_arrow (double u, double v)
{
  /* Pointing right, about the middle. */
  if (u >= -40 && u <= 6)
    return v >= -11 && v <= 11;
  if (u > 6 && u <= 40)
    return v >= -(40 - u) * 30 / 34 && v <= (40 - u) * 30 / 34;
  return FALSE;
}

#define ARROW_SIZE 44           /* from the middle each way */

static void
draw_arrow (gint direction, guint32 colour)
{
  gint x0 = DEV (mid_x - ARROW_SIZE), y0 = DEV (mid_y - ARROW_SIZE);
  gint size = DEV (mid_x + ARROW_SIZE) - x0, x, y, i, j;

  for (y = 0; y < size; y++)
    for (x = 0; x < size; x++)
      {
        gint n = 0, k;
        guchar *d;

        if (x0 + x < 0 || y0 + y < 0 || x0 + x >= DEVICE_WIDTH
            || y0 + y >= DEVICE_HEIGHT)
          continue;
        for (j = 0; j < 4; j++)
          for (i = 0; i < 4; i++)
            {
              double u = (x + (i + 0.5) / 4) / scale - ARROW_SIZE;
              double v = (y + (j + 0.5) / 4) / scale - ARROW_SIZE;

              switch (direction)
                {
                case PASS_LEFT:
                  n += in_arrow (-u, v);
                  break;
                case PASS_RIGHT:
                  n += in_arrow (u, v);
                  break;
                default:
                  n += in_arrow (-v, u);
                  break;
                }
            }
        if (!n)
          continue;
        d = frame + ((gsize) (y0 + y) * DEVICE_WIDTH + x0 + x) * 3;
        for (k = 0; k < 3; k++)
          {
            guint c = (colour >> (16 - 8 * k)) & 0xff;

            d[k] = (guchar) ((c * n + d[k] * (16 - n) + 8) / 16);
          }
      }
}

static gint
n_chosen (void)
{
  gint i, n = 0;

  for (i = 0; i < game.n[SOUTH]; i++)
    n += game.chosen[i];
  return n;
}

static void
redraw (void)
{
  gsize i;
  gint k, x, y;

  if (!frame)
    return;
  for (i = 0; i < (gsize) DEVICE_WIDTH * DEVICE_HEIGHT; i++)
    {
      frame[i * 3] = (TABLE_COLOUR >> 16) & 0xff;
      frame[i * 3 + 1] = (TABLE_COLOUR >> 8) & 0xff;
      frame[i * 3 + 2] = TABLE_COLOUR & 0xff;
    }

  draw_others ();
  for (k = 0; k < game.n[SOUTH]; k++)
    {
      hand_pos (k, &x, &y);
      draw_face (game.hand[SOUTH][k], x, y);
    }
  if (game.phase == PASSING)
    draw_arrow (game.hand_no % 4,
                n_chosen () == 3 ? ARROW_COLOUR : ARROW_DIM);
  for (k = 0; k < game.n_trick; k++)
    {
      trick_pos ((game.leader + k) % N_PLAYERS, &x, &y);
      draw_face (game.trick[k], x, y);
    }
  gtk_widget_queue_draw (table);
}

static void
layout (void)
{
  /* The trick between the north hand's edge and the player's hand. */
  mid_x = frame_width / 2;
  mid_y = (card_height / 2 + frame_height - MARGIN - card_height - RAISE) / 2;
}

/* The table at its usual size, in the game's pixels. */
static gint
roomy_width (void)
{
  return 12 * HAND_STEP + card_width + card_height + 4 * MARGIN;
}

static gint
roomy_height (void)
{
  return card_height / 2 + 2 * TRICK_OFFSET + card_height + RAISE
    + card_height + 4 * MARGIN + 24;
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

      /* If the cards can't be read now, the old ones do. */
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

/* The rules. */

static gint
points (Card c)
{
  return SUIT (c) == GAMES_HEARTS ? 1 : c == QUEEN_OF_SPADES ? 13 : 0;
}

static gint
count_suit (gint p, gint suit)
{
  gint i, n = 0;

  for (i = 0; i < game.n[p]; i++)
    n += SUIT (game.hand[p][i]) == suit;
  return n;
}

static gboolean
holds (gint p, Card c)
{
  gint i;

  for (i = 0; i < game.n[p]; i++)
    if (game.hand[p][i] == c)
      return TRUE;
  return FALSE;
}

/* Why player p can't play card c now, or NULL if it can. */
static const gchar *
not_allowed (gint p, Card c)
{
  gint i;

  if (game.n_trick == 0)
    {
      if (game.tricks == 0 && c != TWO_OF_CLUBS)
        return _("The two of clubs leads.");
      if (SUIT (c) == GAMES_HEARTS && !game.broken
          && count_suit (p, GAMES_HEARTS) < game.n[p])
        return _("Hearts can't be led until one has been played.");
      return NULL;
    }
  if (SUIT (c) != SUIT (game.trick[0]) && count_suit (p, SUIT (game.trick[0])))
    return _("You must follow suit.");
  /* No points on the first trick, if there's anything else. */
  if (game.tricks == 0 && points (c))
    for (i = 0; i < game.n[p]; i++)
      if (!points (game.hand[p][i]))
        return _("No points can be played on the first trick.");
  return NULL;
}

/* The order the player's hand is shown in: clubs, diamonds, spades,
 * hearts, as Windows has it, and low to high. */
static gint
sort_key (Card c)
{
  static const gint order[4] = { 0, 1, 3, 2 };

  return order[SUIT (c)] * 16 + VALUE (c);
}

static void
sort_hand (void)
{
  gint i, j;

  for (i = 1; i < game.n[SOUTH]; i++)
    for (j = i; j > 0 && sort_key (game.hand[SOUTH][j - 1])
         > sort_key (game.hand[SOUTH][j]); j--)
      {
        Card c = game.hand[SOUTH][j];
        gboolean r = game.received[j];

        game.hand[SOUTH][j] = game.hand[SOUTH][j - 1];
        game.hand[SOUTH][j - 1] = c;
        game.received[j] = game.received[j - 1];
        game.received[j - 1] = r;
      }
}

static void
take_card (gint p, gint i)
{
  memmove (game.hand[p] + i, game.hand[p] + i + 1,
           (gsize) (game.n[p] - i - 1));
  if (p == SOUTH)
    memset (game.received, 0, sizeof game.received);
  game.n[p]--;
}

/* The computer. */

/* How much player p wants rid of card c before the hand. */
static gint
pass_worth (gint p, Card c)
{
  gint suit = SUIT (c), v = VALUE (c);

  if (suit == GAMES_SPADES)
    {
      /* The queen, and the cards above it that would catch it, unless
       * there are spades enough to keep them covered. */
      if (v >= GAMES_QUEEN && count_suit (p, GAMES_SPADES) < 5)
        return 100 + v;
      return v - 4;
    }
  if (suit == GAMES_HEARTS)
    return v + 8;
  /* High cards of a short suit, to be rid of it. */
  return v + 2 * (13 - count_suit (p, suit)) - 12;
}

/* Another player than p who has taken every point so far, enough of
 * them to be going for the moon; -1 if no one. */
static gint
moon_shooter (gint p)
{
  gint q, sum = 0;

  for (q = 0; q < N_PLAYERS; q++)
    sum += game.taken[q];
  for (q = 0; q < N_PLAYERS; q++)
    if (q != p && sum >= 10 && game.taken[q] == sum)
      return q;
  return -1;
}

static gint trick_winner (void);

/* The card player p plays: the index in its hand. */
static gint
choose_card (gint p)
{
  gint i, best = -1, best_score = 0;
  gint shooter = moon_shooter (p);
  /* Whether the trick would go to the one shooting the moon, who must
   * be stopped by taking a heart from them. */
  gboolean stop = shooter >= 0 && game.n_trick > 0
    && trick_winner () == shooter;

  for (i = 0; i < game.n[p]; i++)
    {
      Card c = game.hand[p][i];
      gint v = VALUE (c), suit = SUIT (c), score;

      if (not_allowed (p, c))
        continue;
      if (game.n_trick == 0)
        {
          /* Leading: low cards of short suits, keeping hearts and the
           * spades that catch the queen back; low spades draw her out. */
          score = -v - count_suit (p, suit);
          if (suit == GAMES_HEARTS)
            score -= 20;
          if (suit == GAMES_SPADES && !game.queen_gone)
            {
              if (c == QUEEN_OF_SPADES)
                score -= 100;
              else if (v > GAMES_QUEEN)
                score -= 50;
              else if (!holds (p, QUEEN_OF_SPADES))
                score += 5;
            }
        }
      else
        {
          gint led = SUIT (game.trick[0]), high = 0, k;

          for (k = 0; k < game.n_trick; k++)
            if (SUIT (game.trick[k]) == led)
              high = MAX (high, VALUE (game.trick[k]));
          if (suit == led && stop)
            /* Taking the trick from the one shooting the moon. */
            score = v;
          else if (suit == led)
            {
              if (v < high)
                /* The highest card that still loses. */
                score = 100 + v;
              else if (game.tricks == 0
                       || (game.n_trick == N_PLAYERS - 1 && c != QUEEN_OF_SPADES))
                /* Winning anyway, with no points to come: the highest. */
                score = 50 + v;
              else
                /* Winning: the lowest. */
                score = 50 - v - (c == QUEEN_OF_SPADES ? 40 : 0);
            }
          else
            {
              /* Throwing away: the queen, the spades that would catch
               * her, hearts, then high cards of short suits. */
              if (stop && points (c))
                /* Not feeding the one shooting the moon. */
                score = -100 - v;
              else if (c == QUEEN_OF_SPADES)
                score = 300;
              else if (suit == GAMES_SPADES && v > GAMES_QUEEN
                       && !game.queen_gone)
                score = 200 + v;
              else if (suit == GAMES_HEARTS)
                score = 150 + v;
              else
                score = 2 * v - count_suit (p, suit);
            }
        }
      if (best < 0 || score > best_score)
        {
          best = i;
          best_score = score;
        }
    }
  return best;
}

/* Play. */

static void hand_over (void);

/* Who takes the trick, or would if it ended now. */
static gint
trick_winner (void)
{
  gint k, best = 0;

  for (k = 1; k < game.n_trick; k++)
    if (SUIT (game.trick[k]) == SUIT (game.trick[0])
        && VALUE (game.trick[k]) > VALUE (game.trick[best]))
      best = k;
  return (game.leader + best) % N_PLAYERS;
}

static void
whose_turn (void)
{
  if (game.turn == SOUTH)
    prompt (game.tricks == 0 && game.n_trick == 0
            ? _("Your lead: the two of clubs.") : _("Your turn."));
  else
    {
      gchar text[64];

      g_snprintf (text, sizeof text, _("%s to play."),
                  _(player_names[game.turn]));
      prompt (text);
      schedule (COMPUTER_MS);
    }
}

static void
clear_trick (void)
{
  gint p = trick_winner (), k;

  for (k = 0; k < N_PLAYERS; k++)
    game.taken[p] += points (game.trick[k]);
  game.n_trick = 0;
  game.tricks++;
  game.leader = game.turn = p;
  if (game.tricks == 13)
    {
      redraw ();
      hand_over ();
      return;
    }
  game.phase = PLAYING;
  redraw ();
  whose_turn ();
}

static void
play_card (gint p, gint i)
{
  Card c = game.hand[p][i];

  take_card (p, i);
  game.trick[game.n_trick++] = c;
  if (SUIT (c) == GAMES_HEARTS)
    game.broken = TRUE;
  if (c == QUEEN_OF_SPADES)
    game.queen_gone = TRUE;

  if (game.n_trick == N_PLAYERS)
    {
      gchar text[64];
      gint w = trick_winner ();

      game.phase = TRICK_TAKEN;
      g_snprintf (text, sizeof text,
                  w == SOUTH ? _("You take the trick.")
                  : _("%s takes the trick."), _(player_names[w]));
      prompt (text);
      redraw ();
      schedule (TRICK_MS);
      return;
    }
  game.turn = (p + 1) % N_PLAYERS;
  redraw ();
  whose_turn ();
}

static gint
play_step (gpointer data)
{
  (void) data;
  play_timeout = 0;
  if (game.phase == TRICK_TAKEN)
    clear_trick ();
  else if (game.phase == PLAYING && game.turn != SOUTH)
    play_card (game.turn, choose_card (game.turn));
  return FALSE;
}

static void
schedule (gint ms)
{
  if (play_timeout)
    gtk_timeout_remove (play_timeout);
  play_timeout = gtk_timeout_add ((guint32) ms, play_step, NULL);
}

static void
unschedule (void)
{
  if (play_timeout)
    gtk_timeout_remove (play_timeout);
  play_timeout = 0;
}

/* Starts the play of the hand, from the two of clubs. */
static void
start_play (void)
{
  gint p;

  game.phase = PLAYING;
  for (p = 0; p < N_PLAYERS; p++)
    if (holds (p, TWO_OF_CLUBS))
      game.leader = game.turn = p;
  redraw ();
  whose_turn ();
}

/* Passes three cards each the way of this hand. */
static void
pass_cards (void)
{
  static const gint shift[] = { 1, 3, 2 };
  Card passed[N_PLAYERS][3];
  gint p, k, i;

  for (i = 0, k = 0; i < game.n[SOUTH]; i++)
    if (game.chosen[i])
      passed[SOUTH][k++] = game.hand[SOUTH][i];
  memset (game.chosen, 0, sizeof game.chosen);
  for (p = WEST; p < N_PLAYERS; p++)
    for (k = 0; k < 3; k++)
      {
        gint pick = 0;

        for (i = 1; i < game.n[p]; i++)
          if (pass_worth (p, game.hand[p][i])
              > pass_worth (p, game.hand[p][pick]))
            pick = i;
        passed[p][k] = game.hand[p][pick];
        take_card (p, pick);
      }
  for (k = 0; k < 3; k++)
    for (i = 0; i < game.n[SOUTH]; i++)
      if (game.hand[SOUTH][i] == passed[SOUTH][k])
        take_card (SOUTH, i);

  for (p = 0; p < N_PLAYERS; p++)
    {
      gint to = (p + shift[game.hand_no % 4]) % N_PLAYERS;

      for (k = 0; k < 3; k++)
        {
          if (to == SOUTH)
            game.received[game.n[to]] = TRUE;
          game.hand[to][game.n[to]++] = passed[p][k];
        }
    }
  sort_hand ();
  start_play ();
}

static const gchar *const pass_prompts[] = {
  N_("Choose three cards to pass to the left, then click the arrow."),
  N_("Choose three cards to pass to the right, then click the arrow."),
  N_("Choose three cards to pass across, then click the arrow.")
};

static void
deal (void)
{
  Card deck[52];
  gint i, p;

  for (i = 0; i < 52; i++)
    deck[i] = CARD (i / 13, i % 13 + 1);
  for (i = 51; i > 0; i--)
    {
      gint j = games_rand_int (i + 1);
      Card c = deck[i];

      deck[i] = deck[j];
      deck[j] = c;
    }
  for (p = 0; p < N_PLAYERS; p++)
    {
      memcpy (game.hand[p], deck + 13 * p, 13);
      game.n[p] = 13;
      game.taken[p] = 0;
    }
  memset (game.chosen, 0, sizeof game.chosen);
  memset (game.received, 0, sizeof game.received);
  game.n_trick = 0;
  game.tricks = 0;
  game.broken = game.queen_gone = FALSE;
  sort_hand ();

  if (game.hand_no % 4 == PASS_NONE)
    {
      start_play ();
      return;
    }
  game.phase = PASSING;
  prompt (_(pass_prompts[game.hand_no % 4]));
  redraw ();
}

/* The score sheet. */

static void
score_sheet (void)
{
  GtkWidget *grid, *button;
  gint h, p;

  if (score_dialog)
    {
      gdk_window_raise (score_dialog->window);
      return;
    }

  score_dialog = games_dialog_new (GTK_WINDOW (window), _("Score"));
  gtk_signal_connect (GTK_OBJECT (score_dialog), "destroy",
                      GTK_SIGNAL_FUNC (gtk_widget_destroyed), &score_dialog);

  grid = gtk_table_new ((guint) game.n_scores + 1, N_PLAYERS + 1, FALSE);
  gtk_container_set_border_width (GTK_CONTAINER (grid), 8);
  gtk_table_set_col_spacings (GTK_TABLE (grid), 16);
  gtk_table_set_row_spacings (GTK_TABLE (grid), 2);
  for (p = 0; p < N_PLAYERS; p++)
    gtk_table_attach_defaults (GTK_TABLE (grid),
                               gtk_label_new (_(player_names[p])),
                               (guint) p + 1, (guint) p + 2, 0, 1);
  for (h = 0; h < game.n_scores; h++)
    {
      gchar text[16];
      GtkWidget *label;

      g_snprintf (text, sizeof text, "%d", h + 1);
      label = gtk_label_new (text);
      gtk_misc_set_alignment (GTK_MISC (label), 1, 0.5);
      gtk_table_attach_defaults (GTK_TABLE (grid), label, 0, 1,
                                 (guint) h + 1, (guint) h + 2);
      for (p = 0; p < N_PLAYERS; p++)
        {
          g_snprintf (text, sizeof text, "%d", game.scores[h][p]);
          label = gtk_label_new (text);
          gtk_misc_set_alignment (GTK_MISC (label), 1, 0.5);
          gtk_table_attach_defaults (GTK_TABLE (grid), label, (guint) p + 1,
                                     (guint) p + 2, (guint) h + 1,
                                     (guint) h + 2);
        }
    }
  if (!game.n_scores)
    gtk_table_attach_defaults (GTK_TABLE (grid),
                               gtk_label_new (_("No hand played yet.")),
                               0, N_PLAYERS + 1, 1, 2);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (score_dialog)->vbox), grid,
                      TRUE, TRUE, 0);

  button = games_dialog_add_button (score_dialog, _("OK"));
  gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
                             GTK_SIGNAL_FUNC (gtk_widget_destroy),
                             GTK_OBJECT (score_dialog));
  gtk_widget_show_all (score_dialog);
}

static void
play_again (gboolean yes, gpointer data)
{
  (void) data;
  if (yes)
    new_game ();
}

/* The next hand once the score sheet is closed. */
static void
sheet_closed (GtkWidget *widget, gpointer data)
{
  (void) widget;
  (void) data;
  if (game.phase == HAND_OVER)
    next_hand ();
}

static void
hand_over (void)
{
  gint p, moon = -1, best = 0, n_best = 0;
  gint *now, *before;
  gchar text[96];

  game.phase = HAND_OVER;
  if (game.n_scores == MAX_HANDS)
    {
      memmove (game.scores, game.scores + 1,
               sizeof game.scores[0] * (MAX_HANDS - 1));
      game.n_scores--;
    }
  now = game.scores[game.n_scores];
  before = game.n_scores ? game.scores[game.n_scores - 1] : NULL;
  for (p = 0; p < N_PLAYERS; p++)
    if (game.taken[p] == 26)
      moon = p;
  for (p = 0; p < N_PLAYERS; p++)
    {
      gint add = moon < 0 ? game.taken[p] : p == moon ? 0 : 26;

      now[p] = (before ? before[p] : 0) + add;
    }
  game.n_scores++;
  show_scores ();

  if (moon >= 0)
    {
      g_snprintf (text, sizeof text,
                  moon == SOUTH ? _("You shot the moon!")
                  : _("%s shot the moon!"), _(player_names[moon]));
      games_flash (GTK_STATUSBAR (statusbar), text);
    }

  for (p = 0; p < N_PLAYERS; p++)
    if (now[p] >= GAME_POINTS)
      break;
  /* The sheet shows the hand just played. */
  if (score_dialog)
    gtk_widget_destroy (score_dialog);
  if (p == N_PLAYERS)
    {
      prompt (_("The hand is over."));
      score_sheet ();
      gtk_signal_connect (GTK_OBJECT (score_dialog), "destroy",
                          GTK_SIGNAL_FUNC (sheet_closed), NULL);
      return;
    }

  /* The game is over: the fewest points win. */
  game.phase = GAME_OVER;
  for (p = 0; p < N_PLAYERS; p++)
    if (p == 0 || now[p] < best)
      best = now[p];
  for (p = 0; p < N_PLAYERS; p++)
    n_best += now[p] == best;
  score_sheet ();
  if (now[SOUTH] == best && n_best == 1)
    g_snprintf (text, sizeof text, _("You won the game!"));
  else if (now[SOUTH] == best)
    g_snprintf (text, sizeof text, _("You tied for the win."));
  else
    {
      for (p = 1; now[p] != best; p++)
        ;
      g_snprintf (text, sizeof text,
                  n_best == 1 ? _("%s won the game.")
                  : _("%s and others tied for the win."),
                  _(player_names[p]));
    }
  prompt (text);
  games_question (GTK_WINDOW (window), _("Hearts"), text,
                  _("Play Again"), _("Close"), play_again, NULL);
}

static void
next_hand (void)
{
  game.hand_no++;
  deal ();
}

static void
new_game (void)
{
  unschedule ();
  if (score_dialog)
    {
      game.phase = GAME_OVER;
      gtk_widget_destroy (score_dialog);
    }
  game.n_scores = 0;
  game.hand_no = 0;
  show_scores ();
  deal ();
}

/* The mouse. */

static gboolean
in_arrow_box (gint x, gint y)
{
  return ABS (x - mid_x) <= ARROW_SIZE && ABS (y - mid_y) <= ARROW_SIZE;
}

static gint
table_press (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
  gint x = (gint) (event->x / zoom), y = (gint) (event->y / zoom), i;
  gint hit = -1;

  (void) widget;
  (void) data;
  if (event->type != GDK_BUTTON_PRESS || event->button != 1)
    return TRUE;

  /* A click hurries a taken trick off the table. */
  if (game.phase == TRICK_TAKEN)
    {
      unschedule ();
      clear_trick ();
      return TRUE;
    }

  for (i = game.n[SOUTH] - 1; i >= 0 && hit < 0; i--)
    {
      gint cx, cy;

      hand_pos (i, &cx, &cy);
      if (x >= cx && y >= cy && x < cx + card_width && y < cy + card_height)
        hit = i;
    }

  if (game.phase == PASSING)
    {
      if (hit >= 0)
        {
          if (game.chosen[hit] || n_chosen () < 3)
            game.chosen[hit] = !game.chosen[hit];
          redraw ();
        }
      else if (in_arrow_box (x, y) && n_chosen () == 3)
        pass_cards ();
      return TRUE;
    }

  if (game.phase == PLAYING && game.turn == SOUTH && hit >= 0)
    {
      const gchar *why = not_allowed (SOUTH, game.hand[SOUTH][hit]);

      if (why)
        games_flash (GTK_STATUSBAR (statusbar), why);
      else
        play_card (SOUTH, hit);
    }
  return TRUE;
}

/* Settings. */

static void
save_options (void)
{
  games_config_set_int (config, "back", opts.back);
  games_config_save (config);
}

static void
load_options (void)
{
  opts.back = games_config_get_int (config, "back", 0);
  if (opts.back < 0 || opts.back >= GAMES_CARDS_N_BACKS)
    opts.back = 0;
}

/* The deck dialog: the backs to pick from, each in a little drawing
 * area, framed when picked, as in Solitaire. */

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
  gint s = (gint) games_cards_scale (deck_cards), cw, ch;
  gint w = (card_width + 2 * DECK_PAD) * s;
  gint h = (card_height + 2 * DECK_PAD) * s;
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
  games_about (GTK_WINDOW (window), "cghearts",
               _("Classic GUI Hearts"),
               "Copyright (C) 2026 The CGTK authors", NULL, NULL);
}

/* The window. */

static void
quit_game (void)
{
  /* The table's size is kept for next time. */
  if (frame)
    {
      games_config_set_int (config, "width", table_width);
      games_config_set_int (config, "height", table_height);
      games_config_save (config);
    }
  gtk_main_quit ();
}

enum { NEW_GAME, SCORE, DECK, QUIT_GAME, ABOUT };

static void
menu_callback (gpointer data, guint action, GtkWidget *widget)
{
  (void) data;
  (void) widget;
  switch (action)
    {
    case NEW_GAME:
      new_game ();
      break;
    case SCORE:
      score_sheet ();
      break;
    case DECK:
      deck ();
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
  { "/Game/sep1", NULL, NULL, 0, "<Separator>" },
  { N_("/Game/_Score..."), "F4", GTK_SIGNAL_FUNC (menu_callback), SCORE,
    NULL },
  { N_("/Game/De_ck..."), NULL, GTK_SIGNAL_FUNC (menu_callback), DECK, NULL },
  { "/Game/sep2", NULL, NULL, 0, "<Separator>" },
  { N_("/Game/E_xit"), "<control>Q", GTK_SIGNAL_FUNC (menu_callback),
    QUIT_GAME, NULL },
  { N_("/_Help"), NULL, NULL, 0, "<Branch>" },
  { N_("/Help/_About Hearts..."), NULL, GTK_SIGNAL_FUNC (menu_callback),
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

static GtkWidget *
status_field (GtkWidget *box)
{
  GtkWidget *border = gtk_frame_new (NULL);
  GtkWidget *label = gtk_label_new ("");

  gtk_frame_set_shadow_type (GTK_FRAME (border), GTK_SHADOW_IN);
  gtk_misc_set_padding (GTK_MISC (label), 4, 0);
  gtk_container_add (GTK_CONTAINER (border), label);
  gtk_box_pack_start (GTK_BOX (box), border, FALSE, FALSE, 0);
  return label;
}

int
main (int argc, char *argv[])
{
  GtkWidget *vbox, *status_box;
  GtkAccelGroup *accel_group;
  GtkItemFactory *factory;
  gint p;

  gtk_set_locale ();
  games_init_translation ();
  gtk_init (&argc, &argv);

  config = games_config_load (APPNAME);
  games_rand_seed (games_rand_new_seed ());
  load_options ();

  cards = games_cards_new (1);
  if (!cards)
    {
      fprintf (stderr, _("%s: the card images could not be read\n"), argv[0]);
      return 1;
    }
  games_cards_size (cards, &card_width, &card_height);

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title (GTK_WINDOW (window), _(APPNAME_LONG));
  gtk_window_set_wmclass (GTK_WINDOW (window), APPNAME, "Cghearts");
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
  /* The table starts at the size it was left at, roomy the first time;
   * once shown, it can be made as small as LEAST_ZOOM of that. */
  gtk_widget_set_usize (table,
                        MAX (games_config_get_int (config, "width",
                                                   roomy_width ()),
                             LEAST_WIDTH),
                        MAX (games_config_get_int (config, "height",
                                                   roomy_height ()),
                             LEAST_HEIGHT));
  gtk_widget_set_events (table, GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK);
  gtk_signal_connect (GTK_OBJECT (table), "configure_event",
                      GTK_SIGNAL_FUNC (table_configure), NULL);
  gtk_signal_connect (GTK_OBJECT (table), "expose_event",
                      GTK_SIGNAL_FUNC (table_expose), NULL);
  gtk_signal_connect (GTK_OBJECT (table), "button_press_event",
                      GTK_SIGNAL_FUNC (table_press), NULL);
  gtk_box_pack_start (GTK_BOX (vbox), table, TRUE, TRUE, 0);

  /* The status bar: each player's points, then what is happening. */
  status_box = gtk_hbox_new (FALSE, 2);
  for (p = 0; p < N_PLAYERS; p++)
    score_labels[p] = status_field (status_box);
  statusbar = gtk_statusbar_new ();
  prompt_context = gtk_statusbar_get_context_id (GTK_STATUSBAR (statusbar),
                                                 "prompt");
  gtk_box_pack_start (GTK_BOX (status_box), statusbar, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), status_box, FALSE, FALSE, 0);

  gtk_widget_show_all (vbox);
  new_game ();
  games_window_set_icon (window, "cghearts", cghearts_xpm);
  gtk_widget_show (window);
  gtk_widget_set_usize (table, LEAST_WIDTH, LEAST_HEIGHT);
  gtk_main ();

  unschedule ();
  if (refit_timer)
    gtk_timeout_remove (refit_timer);
  g_free (frame);
  games_cards_free (cards);
  games_config_free (config);
  return 0;
}
