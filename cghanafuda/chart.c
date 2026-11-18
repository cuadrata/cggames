/*
 * cghanafuda: charts of the yaku and the cards, for learning
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

#include "chart.h"
#include "table.h"

/* The cards in the charts, in the toolkit's pixels: how high, and the
 * gap between them and round a row of them. */
#define CHART_HEIGHT 56
#define CHART_GAP 4
#define CHART_PAD 4

/* How a card is shown: as it is, faded into the window, or
 * darkened. */
#define FADE 0.55
#define DARK 0.35

/* The players, as the game numbers them. */
#define HUMAN 0
#define COMPUTER 1

/* The cards, drawn for the charts at device times CHART_HEIGHT high. */
static Hanafuda *chart_cards_set;
static gint chart_set = -1, chart_border = -1, chart_device;

static GtkWidget *yaku_window, *cards_window;

/* The yaku, as the chart lists them: their names, their points and
 * what they take, the cards they are made of, and the names kk_yaku
 * gives them when made. */
typedef struct
{
  const gchar *title;
  const gchar *rule;
  gint cards[10];
  gint n_cards;
  gint need;                    /* cards, for those made of any of a kind */
  const gchar *made[4];
} ChartYaku;

static const ChartYaku yaku_rows[] = {
  { N_("Brights"), N_("Sanko 5: three, not the rain man\n"
                      "Shiko 8: four, not him\n"
                      "Ame-shiko 7: four with him\n"
                      "Goko 10: all five"),
    { KK_CRANE, KK_CURTAIN, KK_MOON, KK_RAIN_MAN, KK_PHOENIX }, 5, 0,
    { "Sanko", "Shiko", "Ame-shiko", "Goko" } },
  { N_("Ino-shika-cho 5"), N_("and 1 for each other animal"),
    { KK_BOAR, KK_DEER, KK_BUTTERFLIES }, 3, 0, { "Ino-shika-cho" } },
  { N_("Akatan 5"), N_("and 1 for each other ribbon;\n"
                       "with Aotan, 10 for both"),
    { KK_PINE_POEM, KK_PLUM_POEM, KK_CHERRY_POEM }, 3, 0,
    { "Akatan", "Akatan-aotan" } },
  { N_("Aotan 5"), N_("and 1 for each other ribbon"),
    { KK_PEONY_BLUE, KK_CHRYSANTHEMUM_BLUE, KK_MAPLE_BLUE }, 3, 0,
    { "Aotan", "Akatan-aotan" } },
  { N_("Hanami-zake 5"), N_("cherry blossom viewing"),
    { KK_CURTAIN, KK_SAKE }, 2, 0, { "Hanami-zake" } },
  { N_("Tsukimi-zake 5"), N_("moon viewing"),
    { KK_MOON, KK_SAKE }, 2, 0, { "Tsukimi-zake" } },
  { N_("Tane 1"), N_("any five animals,\nand 1 for each more"),
    { 4, 12, 16, 20, 24, 29, 32, 36, 41 }, 9, 5, { "Tane" } },
  { N_("Tan 1"), N_("any five ribbons,\nand 1 for each more"),
    { 1, 5, 9, 13, 17, 21, 25, 33, 37, 42 }, 10, 5, { "Tan" } },
  { N_("Kasu 1"), N_("any ten plain cards,\nand 1 for each more"),
    { 0 }, 0, 10, { "Kasu" } }
};
#define N_YAKU_ROWS ((gint) (sizeof yaku_rows / sizeof yaku_rows[0]))

static GtkWidget *yaku_status[N_YAKU_ROWS];
static gboolean fresh;          /* a chart opened, to be brought up to date */
static GList *strips;           /* the rows of cards open, to redraw */

/* Who has taken card: HUMAN, COMPUTER or -1, as the game says. */
static gint
owner (gint card)
{
  return game && game->owner ? game->owner (card) : -1;
}

/* The cards for the charts, drawn again if the set, the border or the
 * screen's scale has changed; FALSE if they can't be had. */
static gboolean
have_cards (gint device)
{
  Hanafuda *c;

  if (chart_cards_set && chart_set == opts.set
      && chart_border == opts.border && chart_device == device)
    return TRUE;
  c = hanafuda_new (hanafuda_sets[opts.set], opts.border,
                    CHART_HEIGHT * device);
  if (!c)
    return chart_cards_set != NULL;
  hanafuda_free (chart_cards_set);
  chart_cards_set = c;
  chart_set = opts.set;
  chart_border = opts.border;
  chart_device = device;
  return TRUE;
}

/* A row of cards: which, and whether they are shown by who has them,
 * as in the yaku chart, or only dimmed once taken, as in the cards
 * chart. */
typedef struct
{
  gint cards[10];
  gint n;
  gboolean by_player;
} Strip;

/* How a card of the strip is shown: 1 as it is, else the part of its
 * colour kept, toward the table's for faded. */
static double
card_shade (const Strip *strip, gint card, gboolean *fade)
{
  gint who = owner (card);

  *fade = FALSE;
  if (!strip->by_player)
    return who >= 0 ? DARK + 0.25 : 1;
  if (who == HUMAN)
    return 1;
  if (who == COMPUTER)
    return DARK;
  *fade = TRUE;
  return FADE;
}

static gint
strip_expose (GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
  Strip *strip = data;
  gint device = MAX (gdk_window_get_scale_factor (widget->window), 1);
  gint w = widget->allocation.width * device;
  gint h = widget->allocation.height * device;
  guchar bg[3], *frame;
  gint cw, ch, i, x, y, k;

  (void) event;
  if (!have_cards (device))
    return TRUE;
  /* The cards lie on the window's own colour, and fade into it. */
  bg[0] = (guchar) (widget->style->bg[GTK_STATE_NORMAL].red >> 8);
  bg[1] = (guchar) (widget->style->bg[GTK_STATE_NORMAL].green >> 8);
  bg[2] = (guchar) (widget->style->bg[GTK_STATE_NORMAL].blue >> 8);
  frame = g_malloc ((gsize) w * h * 3);
  for (i = 0; i < w * h; i++)
    memcpy (frame + (gsize) i * 3, bg, 3);

  cw = hanafuda_width (chart_cards_set);
  ch = hanafuda_height (chart_cards_set);
  for (k = 0; k < strip->n; k++)
    {
      const guchar *src = hanafuda_card (chart_cards_set, strip->cards[k]);
      gboolean fade;
      double keep = card_shade (strip, strip->cards[k], &fade);
      gint x0 = (CHART_PAD + k * CHART_GAP) * device + k * cw;
      gint y0 = CHART_PAD * device;

      for (y = 0; y < ch && y0 + y < h; y++)
        for (x = 0; x < cw && x0 + x < w; x++)
          {
            const guchar *s = src + ((gsize) y * cw + x) * 4;
            guchar *d = frame + ((gsize) (y0 + y) * w + x0 + x) * 3;
            guint a = s[3], c;

            if (!a)
              continue;
            for (i = 0; i < 3; i++)
              {
                /* Faded toward the table, or darkened. */
                double v = fade ? s[i] * keep + bg[i] * (1 - keep)
                  : s[i] * keep;

                c = (guint) (v + 0.5);
                d[i] = (guchar) ((c * a + d[i] * (255 - a) + 127) / 255);
              }
          }
    }
  gdk_draw_rgb_image_scaled (widget->window, widget->style->black_gc,
                             0, 0, widget->allocation.width,
                             widget->allocation.height, device,
                             GDK_RGB_DITHER_NONE, frame, w * 3);
  g_free (frame);
  return TRUE;
}

static void
strip_destroyed (GtkWidget *widget, gpointer data)
{
  strips = g_list_remove (strips, widget);
  g_free (data);
}

/* A row of n cards, shown as by_player says. */
static GtkWidget *
strip_new (const gint *row, gint n, gboolean by_player)
{
  GtkWidget *area = gtk_drawing_area_new ();
  Strip *strip = g_new0 (Strip, 1);
  /* At least as wide as the cards are drawn, whichever way they round. */
  gint cw = (CHART_HEIGHT * HANAFUDA_WIDTH + HANAFUDA_HEIGHT - 1)
    / HANAFUDA_HEIGHT;

  memcpy (strip->cards, row, sizeof (gint) * (gsize) n);
  strip->n = n;
  strip->by_player = by_player;
  gtk_widget_set_usize (area, n * cw + (n - 1) * CHART_GAP + 2 * CHART_PAD,
                        CHART_HEIGHT + 2 * CHART_PAD);
  gtk_signal_connect (GTK_OBJECT (area), "expose_event",
                      GTK_SIGNAL_FUNC (strip_expose), strip);
  gtk_signal_connect (GTK_OBJECT (area), "destroy",
                      GTK_SIGNAL_FUNC (strip_destroyed), strip);
  strips = g_list_prepend (strips, area);
  return area;
}

static GtkWidget *
left_label (const gchar *text)
{
  GtkWidget *label = gtk_label_new (text);

  gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_LEFT);
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
  return label;
}

/* A chart's window: a dialog with a Close button, its contents in a
 * box, and *window set to it while it is open. */
static GtkWidget *
chart_window (GtkWidget **window_ptr, const gchar *title,
              const gchar *legend, GtkWidget *contents)
{
  GtkWidget *dialog = games_dialog_new (GTK_WINDOW (window), title);
  GtkWidget *vbox = gtk_vbox_new (FALSE, 8), *button;

  *window_ptr = dialog;
  gtk_signal_connect (GTK_OBJECT (dialog), "destroy",
                      GTK_SIGNAL_FUNC (gtk_widget_destroyed), window_ptr);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 8);
  gtk_box_pack_start (GTK_BOX (vbox), left_label (legend), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), contents, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), vbox, TRUE, TRUE,
                      0);
  button = games_dialog_add_button (dialog, _("Close"));
  gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
                             GTK_SIGNAL_FUNC (gtk_widget_destroy),
                             GTK_OBJECT (dialog));
  return dialog;
}

/* The yaku chart. */

/* How near player is to row's yaku, as "You: 2 of 3", or "made". */
static gchar *
yaku_progress (gint row, gint player, const KKYaku *made, gint n_made)
{
  const ChartYaku *y = &yaku_rows[row];
  const gchar *who = player == HUMAN ? _("You") : _("The computer");
  gint have = 0, i, j, c;

  for (i = 0; i < n_made; i++)
    for (j = 0; j < 4 && y->made[j]; j++)
      if (strcmp (made[i].name, y->made[j]) == 0)
        return g_strdup_printf (ngettext ("%s: made, %d point",
                                          "%s: made, %d points",
                                          made[i].points), who,
                                made[i].points);
  if (y->n_cards)
    {
      for (i = 0; i < y->n_cards; i++)
        if (owner (y->cards[i]) == player)
          have++;
    }
  else
    for (c = 0; c < KK_CARDS; c++)
      if (owner (c) == player
          && (kk_kind (c) == KK_PLAIN
              || (c == KK_SAKE && opts.rules.sake_is_plain)))
        have++;
  return g_strdup_printf (_("%s: %d of %d"), who, have,
                          y->need ? y->need : y->n_cards);
}

static void
yaku_update (void)
{
  KKYaku made[2][KK_MAX_YAKU];
  gint n_made[2], p, row, c;

  if (!yaku_window)
    return;
  for (p = 0; p < 2; p++)
    {
      gint8 taken[KK_CARDS];
      gint n = 0;

      for (c = 0; c < KK_CARDS; c++)
        if (owner (c) == p)
          taken[n++] = (gint8) c;
      kk_yaku (&opts.rules, taken, n, made[p], &n_made[p]);
    }
  for (row = 0; row < N_YAKU_ROWS; row++)
    {
      gchar *mine = yaku_progress (row, HUMAN, made[HUMAN], n_made[HUMAN]);
      gchar *theirs = yaku_progress (row, COMPUTER, made[COMPUTER],
                                     n_made[COMPUTER]);
      gchar *text = g_strdup_printf ("%s\n%s", mine, theirs);

      gtk_label_set_text (GTK_LABEL (yaku_status[row]), text);
      g_free (text);
      g_free (mine);
      g_free (theirs);
    }
}

void
chart_yaku (void)
{
  GtkWidget *table, *label;
  gchar *text;
  gint row;

  if (yaku_window)
    {
      gdk_window_raise (yaku_window->window);
      return;
    }
  table = gtk_table_new (N_YAKU_ROWS, 3, FALSE);
  gtk_table_set_row_spacings (GTK_TABLE (table), 6);
  gtk_table_set_col_spacings (GTK_TABLE (table), 12);
  for (row = 0; row < N_YAKU_ROWS; row++)
    {
      const ChartYaku *y = &yaku_rows[row];

      text = g_strdup_printf ("%s\n%s", _(y->title), _(y->rule));
      label = left_label (text);
      g_free (text);
      gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                        GTK_FILL, GTK_FILL, 0, 0);
      if (y->n_cards)
        gtk_table_attach (GTK_TABLE (table),
                          strip_new (y->cards, y->n_cards, TRUE),
                          1, 2, row, row + 1, GTK_FILL, 0, 0, 0);
      else
        gtk_table_attach (GTK_TABLE (table),
                          left_label (opts.rules.sake_is_plain
                                      ? _("The plain cards: two in most "
                                          "months, and the sake cup too.")
                                      : _("The plain cards: two in most "
                                          "months.")),
                          1, 2, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
      yaku_status[row] = left_label ("");
      gtk_table_attach (GTK_TABLE (table), yaku_status[row], 2, 3, row,
                        row + 1, GTK_FILL, GTK_FILL, 0, 0);
    }
  fresh = TRUE;
  chart_window (&yaku_window, _("Yaku"),
                _("Your cards are shown as they are, the computer's dark, "
                  "and those still to be won faded.\n"
                  "A card one player has is lost to the other: the yaku "
                  "the other needed it for can't be made."), table);
  yaku_update ();
  gtk_widget_show_all (yaku_window);
}

/* The cards chart. */

static const gchar *const kind_names[] = {
  N_("bright"), N_("animal"), N_("ribbon"), N_("plain")
};

void
chart_cards (void)
{
  GtkWidget *table, *box;
  gint month, i;

  if (cards_window)
    {
      gdk_window_raise (cards_window->window);
      return;
    }
  /* Two columns of six months, January to June, then July to
   * December. */
  table = gtk_table_new (6, 2, FALSE);
  gtk_table_set_row_spacings (GTK_TABLE (table), 6);
  gtk_table_set_col_spacings (GTK_TABLE (table), 16);
  for (month = 0; month < 12; month++)
    {
      gint four[4];
      GString *s = g_string_new (NULL);

      for (i = 0; i < 4; i++)
        four[i] = month * 4 + i;
      g_string_sprintfa (s, "%s, %s:", _(kk_month_name (month)),
                         _(kk_flower_name (month)));
      /* The cards with names of their own, and what the brights, the
       * animals and the lightning are. */
      for (i = 0; i < 4; i++)
        {
          const gchar *name = kk_card_name (four[i]);
          KKKind kind = kk_kind (four[i]);

          if (kind == KK_RIBBON)
            g_string_sprintfa (s, " %s,", _(name));
          else if (kind != KK_PLAIN || name[0] == 't')
            g_string_sprintfa (s, " %s (%s),", _(name), _(kind_names[kind]));
        }
      if (s->str[s->len - 1] == ',')
        g_string_truncate (s, s->len - 1);
      box = gtk_vbox_new (FALSE, 2);
      gtk_box_pack_start (GTK_BOX (box), left_label (s->str), FALSE, FALSE,
                          0);
      g_string_free (s, TRUE);
      gtk_box_pack_start (GTK_BOX (box), strip_new (four, 4, FALSE), FALSE,
                          FALSE, 0);
      gtk_table_attach (GTK_TABLE (table), box, month / 6, month / 6 + 1,
                        month % 6, month % 6 + 1, GTK_FILL, GTK_FILL, 0, 0);
    }
  fresh = TRUE;
  chart_window (&cards_window, _("Cards"),
                _("The cards a month at a time, those worth most first; "
                  "the rest are plain.\n"
                  "A card matches the others of its month. Those taken "
                  "are shown darker."), table);
  gtk_widget_show_all (cards_window);
}

void
chart_update (void)
{
  static gint seen[KK_CARDS + 3];
  gint now[KK_CARDS + 3], c;
  GList *l;

  if (!yaku_window && !cards_window)
    return;
  /* The yaku are Koi-Koi's. */
  if (yaku_window && !(game && game->owner))
    gtk_widget_destroy (yaku_window);

  /* Nothing to do unless who has what, the cards, or the rules that
   * the chart shows have changed. */
  for (c = 0; c < KK_CARDS; c++)
    now[c] = owner (c);
  now[KK_CARDS] = opts.set * HANAFUDA_N_BORDERS + opts.border;
  now[KK_CARDS + 1] = opts.rules.sake_is_plain;
  now[KK_CARDS + 2] = opts.rules.rain_spoils;
  if (memcmp (now, seen, sizeof now) == 0 && !fresh)
    return;
  memcpy (seen, now, sizeof now);
  fresh = FALSE;
  yaku_update ();
  for (l = strips; l; l = l->next)
    gtk_widget_queue_draw (GTK_WIDGET (l->data));
}

void
chart_close (void)
{
  if (yaku_window)
    gtk_widget_destroy (yaku_window);
  if (cards_window)
    gtk_widget_destroy (cards_window);
  hanafuda_free (chart_cards_set);
  chart_cards_set = NULL;
  chart_set = -1;
  chart_border = -1;
}
