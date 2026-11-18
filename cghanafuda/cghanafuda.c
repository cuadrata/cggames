/*
 * cghanafuda, flower card games against the computer
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

#include "chart.h"
#include "table.h"

#include "icons/cghanafuda.xpm"

#define ELEMENTS(x) (sizeof (x) / sizeof (x [0]))

/* The table's size at first, and the least it can be, in the toolkit's
 * pixels; after that it starts at the size it was left at. */
#define FIRST_WIDTH 800
#define FIRST_HEIGHT 684
#define MIN_WIDTH (FIRST_WIDTH / 2)
#define MIN_HEIGHT (FIRST_HEIGHT / 2)

/* The games, as Options offers them. */
static const Game *const games[] = { &koikoi_game, &kabu_game };
#define N_GAMES ((gint) ELEMENTS (games))

Options opts;
GtkWidget *window;
const Game *game;

static GtkWidget *statusbar, *status_labels[3];

static GamesConfig *config;

void
table_message (const gchar *text)
{
  games_flash (GTK_STATUSBAR (statusbar), text);
}

/* What to do now, and what the pointer is over, each in a context of
 * its own; what is under the pointer is shown over what to do. */
static guint prompt_context, hint_context, prompt_id, hint_id;
static gchar *prompt_text, *hint_text;

static void
show_prompts (void)
{
  GtkStatusbar *bar = GTK_STATUSBAR (statusbar);

  if (!prompt_context)
    {
      prompt_context = gtk_statusbar_get_context_id (bar, "prompt");
      hint_context = gtk_statusbar_get_context_id (bar, "hint");
    }
  if (hint_id)
    gtk_statusbar_remove (bar, hint_context, hint_id);
  if (prompt_id)
    gtk_statusbar_remove (bar, prompt_context, prompt_id);
  hint_id = prompt_id = 0;
  if (prompt_text)
    prompt_id = gtk_statusbar_push (bar, prompt_context, prompt_text);
  if (hint_text)
    hint_id = gtk_statusbar_push (bar, hint_context, hint_text);
}

/* Sets *now to text, and says whether that changed it. */
static gboolean
change (gchar **now, const gchar *text)
{
  if (*now == text || (*now && text && strcmp (*now, text) == 0))
    return FALSE;
  g_free (*now);
  *now = g_strdup (text);
  return TRUE;
}

void
table_prompt (const gchar *text)
{
  if (change (&prompt_text, text))
    show_prompts ();
}

void
table_hint (const gchar *text)
{
  if (change (&hint_text, text))
    show_prompts ();
}

void
table_status (gint field, const gchar *text)
{
  gtk_label_set_text (GTK_LABEL (status_labels[field]), text);
}

static void show_chosen (void);
static GtkWidget *hint_item, *yaku_item;

/* Starts a game of games[i], naming it in the title bar, and marking
 * it in the Game menu. */
static void
play (gint i)
{
  gchar *title;

  if (game)
    game->stop ();
  opts.game = i;
  game = games[i];
  title = g_strdup_printf ("%s - %s", _(APPNAME_LONG), _(game->name));
  gtk_window_set_title (GTK_WINDOW (window), title);
  g_free (title);
  table_status (0, "");
  table_status (1, "");
  table_status (2, "");
  table_prompt (NULL);
  table_hint (NULL);
  gtk_widget_set_sensitive (hint_item, game->hint != NULL);
  gtk_widget_set_sensitive (yaku_item, game->owner != NULL);
  show_chosen ();
  table_refit ();
  game->new_game ();
}

void
table_new_game (void)
{
  play (opts.game);
}

/* Options. */

/* A setting that is on or off. */
static gboolean
flag (const gchar *key, gboolean value)
{
  return games_config_get_int (config, key, value) != 0;
}

static void
load_options (void)
{
  gint i;
  const gchar *set = games_config_get_string (config, "cards",
                                              hanafuda_sets[0]);
  const gchar *border = games_config_get_string (config, "border",
                                                 hanafuda_borders[0]);
  const gchar *name = games_config_get_string (config, "game", games[0]->id);

  opts.game = 0;
  for (i = 0; i < N_GAMES; i++)
    if (strcmp (name, games[i]->id) == 0)
      opts.game = i;
  opts.set = 0;
  for (i = 0; i < HANAFUDA_N_SETS; i++)
    if (strcmp (set, hanafuda_sets[i]) == 0)
      opts.set = i;
  opts.border = HANAFUDA_RED;
  for (i = 0; i < HANAFUDA_N_BORDERS; i++)
    if (strcmp (border, hanafuda_borders[i]) == 0)
      opts.border = i;
  opts.table = strcmp (games_config_get_string (config, "table", "felt"),
                       "tatami") == 0 ? TABLE_TATAMI : TABLE_FELT;
  opts.rounds = games_config_get_int (config, "rounds", 12);
  if (opts.rounds != 3 && opts.rounds != 6)
    opts.rounds = 12;
  opts.rules.sake_is_plain = flag ("sake_plain", TRUE);
  opts.rules.rain_spoils = flag ("rain_spoils", FALSE);
  opts.rules.double_seven = flag ("double_seven", TRUE);
  opts.rules.double_koikoi = flag ("double_koikoi", TRUE);
  opts.rules.koikoi_again = flag ("koikoi_again", FALSE);
  opts.rules.dealer_privilege = flag ("dealer_privilege", FALSE);
  opts.ties_to_dealer = flag ("ties_to_dealer", FALSE);
  opts.learning = flag ("learning", TRUE);
}

static void
save_options (void)
{
  games_config_set_string (config, "game", games[opts.game]->id);
  games_config_set_string (config, "cards", hanafuda_sets[opts.set]);
  games_config_set_string (config, "border", hanafuda_borders[opts.border]);
  games_config_set_string (config, "table",
                           opts.table == TABLE_TATAMI ? "tatami" : "felt");
  games_config_set_int (config, "rounds", opts.rounds);
  games_config_set_int (config, "sake_plain", opts.rules.sake_is_plain);
  games_config_set_int (config, "rain_spoils", opts.rules.rain_spoils);
  games_config_set_int (config, "double_seven", opts.rules.double_seven);
  games_config_set_int (config, "double_koikoi", opts.rules.double_koikoi);
  games_config_set_int (config, "koikoi_again", opts.rules.koikoi_again);
  games_config_set_int (config, "dealer_privilege",
                        opts.rules.dealer_privilege);
  games_config_set_int (config, "ties_to_dealer", opts.ties_to_dealer);
  games_config_set_int (config, "learning", opts.learning);
  games_config_save (config);
}

static GtkWidget *options_dialog;

static struct
{
  GtkWidget *set[HANAFUDA_N_SETS];
  GtkWidget *border[HANAFUDA_N_BORDERS];
  GtkWidget *table[N_TABLES];
  GtkWidget *rounds[3];
  GtkWidget *sake, *rain, *seven, *koikoi, *again, *privilege, *ties;
  GtkWidget *learning;
} od;

static gboolean
active (GtkWidget *toggle)
{
  return GTK_TOGGLE_BUTTON (toggle)->active;
}

static void
switch_answer (gint answer, gpointer data)
{
  if (answer == 0)
    play (GPOINTER_TO_INT (data));
  else
    table_message (_("The next game will be the one chosen."));
}

/* Plays games[i] now if the game in play is hardly begun, or asks. */
static void
switch_game (gint i)
{
  static const gchar *const answers[] = {
    N_("Play %s"), N_("Continue Playing"), NULL
  };
  const gchar *labels[3];
  gchar *text, *first;

  opts.game = i;
  save_options ();
  if (!game->in_play ())
    {
      play (i);
      return;
    }
  text = g_strdup_printf (_("Do you want to start a game of %s now?\n\n"
                            "If you continue playing, the next game will\n"
                            "be %s."), _(games[i]->name), _(games[i]->name));
  first = g_strdup_printf (_(answers[0]), _(games[i]->name));
  labels[0] = first;
  labels[1] = _(answers[1]);
  labels[2] = NULL;
  games_choose (GTK_WINDOW (window), _(APPNAME_LONG), text, labels,
                switch_answer, GINT_TO_POINTER (i));
  g_free (first);
  g_free (text);
}

static void
options_ok (GtkWidget *widget, gpointer data)
{
  static const gint rounds[3] = { 3, 6, 12 };
  gint i, set = opts.set, border = opts.border;
  gint old_rounds = opts.rounds, old_table = opts.table;

  (void) widget;
  (void) data;
  for (i = 0; i < HANAFUDA_N_SETS; i++)
    if (active (od.set[i]))
      set = i;
  for (i = 0; i < HANAFUDA_N_BORDERS; i++)
    if (active (od.border[i]))
      border = i;
  for (i = 0; i < 3; i++)
    if (active (od.rounds[i]))
      opts.rounds = rounds[i];
  for (i = 0; i < N_TABLES; i++)
    if (active (od.table[i]))
      opts.table = i;
  opts.rules.sake_is_plain = active (od.sake);
  opts.rules.rain_spoils = active (od.rain);
  opts.rules.double_seven = active (od.seven);
  opts.rules.double_koikoi = active (od.koikoi);
  opts.rules.koikoi_again = active (od.again);
  opts.rules.dealer_privilege = active (od.privilege);
  opts.ties_to_dealer = active (od.ties);
  opts.learning = active (od.learning);
  save_options ();
  /* The tatami's border takes room at the sides. */
  if (opts.table != old_table)
    table_refit ();
  table_redraw ();
  gtk_widget_destroy (options_dialog);

  if (set != opts.set || border != opts.border)
    {
      if (table_set_cards (set, border))
        save_options ();
      else
        games_message (GTK_WINDOW (window), _(APPNAME_LONG),
                       _("Those cards could not be read."));
    }
  if (opts.rounds != old_rounds && game->in_play ())
    table_message (_("The number of rounds holds from the next game."));
}

static GtkWidget *
options_frame (GtkWidget *box, const gchar *title)
{
  GtkWidget *frame = gtk_frame_new (title);
  GtkWidget *vbox = gtk_vbox_new (FALSE, 2);

  gtk_container_set_border_width (GTK_CONTAINER (vbox), 6);
  gtk_container_add (GTK_CONTAINER (frame), vbox);
  gtk_box_pack_start (GTK_BOX (box), frame, TRUE, TRUE, 0);
  return vbox;
}

static GtkWidget *
check (GtkWidget *box, const gchar *label, gboolean value)
{
  GtkWidget *button = gtk_check_button_new_with_label (label);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), value);
  gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);
  return button;
}

static void
options (void)
{
  static const gchar *const round_names[3] = {
    N_("3 rounds"), N_("6 rounds"), N_("12 rounds")
  };
  static const gchar *const table_names[N_TABLES] = {
    N_("Green felt"), N_("Tatami")
  };
  GtkWidget *vbox, *hbox, *fbox, *button;
  gint i;

  if (options_dialog)
    {
      gdk_window_raise (options_dialog->window);
      return;
    }
  options_dialog = games_dialog_new (GTK_WINDOW (window), _("Options"));
  gtk_signal_connect (GTK_OBJECT (options_dialog), "destroy",
                      GTK_SIGNAL_FUNC (gtk_widget_destroyed), &options_dialog);

  vbox = gtk_vbox_new (FALSE, 8);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 8);
  hbox = gtk_hbox_new (TRUE, 8);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, TRUE, TRUE, 0);

  fbox = options_frame (hbox, _("Cards"));
  for (i = 0; i < HANAFUDA_N_SETS; i++)
    {
      od.set[i] = gtk_radio_button_new_with_label
        (i ? gtk_radio_button_group (GTK_RADIO_BUTTON (od.set[0])) : NULL,
         _(hanafuda_set_names[i]));
      gtk_box_pack_start (GTK_BOX (fbox), od.set[i], FALSE, FALSE, 0);
    }
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (od.set[opts.set]), TRUE);
  gtk_box_pack_start (GTK_BOX (fbox), gtk_hseparator_new (), FALSE, FALSE, 4);
  for (i = 0; i < HANAFUDA_N_BORDERS; i++)
    {
      od.border[i] = gtk_radio_button_new_with_label
        (i ? gtk_radio_button_group (GTK_RADIO_BUTTON (od.border[0])) : NULL,
         _(hanafuda_border_names[i]));
      gtk_box_pack_start (GTK_BOX (fbox), od.border[i], FALSE, FALSE, 0);
    }
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (od.border[opts.border]),
                                TRUE);

  fbox = options_frame (hbox, _("Length"));
  for (i = 0; i < 3; i++)
    {
      od.rounds[i] = gtk_radio_button_new_with_label
        (i ? gtk_radio_button_group (GTK_RADIO_BUTTON (od.rounds[0])) : NULL,
         _(round_names[i]));
      gtk_box_pack_start (GTK_BOX (fbox), od.rounds[i], FALSE, FALSE, 0);
    }
  gtk_toggle_button_set_active
    (GTK_TOGGLE_BUTTON (od.rounds[opts.rounds == 3 ? 0
                                  : opts.rounds == 6 ? 1 : 2]), TRUE);

  fbox = options_frame (hbox, _("Table"));
  for (i = 0; i < N_TABLES; i++)
    {
      od.table[i] = gtk_radio_button_new_with_label
        (i ? gtk_radio_button_group (GTK_RADIO_BUTTON (od.table[0])) : NULL,
         _(table_names[i]));
      gtk_box_pack_start (GTK_BOX (fbox), od.table[i], FALSE, FALSE, 0);
    }
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (od.table[opts.table]),
                                TRUE);

  fbox = options_frame (vbox, _("Koi-Koi"));
  od.sake = check (fbox, _("The sake cup is a plain card too"),
                   opts.rules.sake_is_plain);
  od.rain = check (fbox, _("The rain man spoils the viewing yaku"),
                   opts.rules.rain_spoils);
  od.seven = check (fbox, _("Seven points or more count double"),
                    opts.rules.double_seven);
  od.koikoi = check (fbox, _("Winning after the other's koi-koi doubles"),
                     opts.rules.double_koikoi);
  od.again = check (fbox, _("Koi-koi may be called again on more points"),
                    opts.rules.koikoi_again);
  od.privilege = check (fbox, _("With no yaku, the dealer takes 6 points"),
                        opts.rules.dealer_privilege);

  fbox = options_frame (vbox, _("Oicho-Kabu"));
  od.ties = check (fbox, _("A tie goes to the dealer"), opts.ties_to_dealer);

  fbox = options_frame (vbox, _("Learning"));
  od.learning = check (fbox, _("Light the cards that match, name the cards "
                               "under the pointer,\nand say what the "
                               "computer would do"), opts.learning);

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
  games_about (GTK_WINDOW (window), "cghanafuda",
               _("Classic GUI Hanafuda"),
               "Copyright (C) 2026 The CGTK authors", NULL, NULL);
}

static gint
table_press (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
  gint x, y;

  (void) widget;
  (void) data;
  if (event->type == GDK_BUTTON_PRESS)
    {
      table_point (event->x, event->y, &x, &y);
      game->press (x, y, (gint) event->button);
    }
  return TRUE;
}

static gint
table_motion (GtkWidget *widget, GdkEventMotion *event, gpointer data)
{
  gint x, y;

  (void) widget;
  (void) data;
  if (game->motion)
    {
      table_point (event->x, event->y, &x, &y);
      game->motion (x, y);
    }
  return TRUE;
}

static gint
table_leave (GtkWidget *widget, GdkEventCrossing *event, gpointer data)
{
  (void) widget;
  (void) event;
  (void) data;
  if (game->motion)
    game->motion (-1, -1);
  return TRUE;
}

/* The menus. */

enum {
  NEW_GAME, HINT, OPTIONS, QUIT_GAME, RULES, YAKU, CARDS, ABOUT, PLAY_GAME
};

/* The Game menu's items for the games, set without playing them. */
static GtkWidget *game_items[ELEMENTS (games)];
static gboolean syncing;

static void
show_chosen (void)
{
  if (!game_items[opts.game])
    return;
  syncing = TRUE;
  gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (game_items[opts.game]),
                                  TRUE);
  syncing = FALSE;
}

/* A game chosen in the Game menu: played now, or after the one in
 * play, or the one in play kept after all. */
static void
choose_game (gint i)
{
  gint playing = (gint) (game - games[0]);

  if (i != playing)
    switch_game (i);
  else if (opts.game != playing)
    {
      opts.game = playing;
      save_options ();
      table_message (_("The game being played will be played next too."));
    }
}

static void
quit_game (void)
{
  /* The table's size is kept for next time. */
  if (table_width ())
    {
      games_config_set_int (config, "width", table_width ());
      games_config_set_int (config, "height", table_height ());
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
      table_new_game ();
      break;
    case HINT:
      if (game->hint)
        game->hint ();
      break;
    case OPTIONS:
      options ();
      break;
    case QUIT_GAME:
      quit_game ();
      break;
    case RULES:
      game->help ();
      break;
    case YAKU:
      chart_yaku ();
      break;
    case CARDS:
      chart_cards ();
      break;
    case ABOUT:
      about ();
      break;
    default:
      if (!syncing && GTK_CHECK_MENU_ITEM (widget)->active)
        choose_game ((gint) action - PLAY_GAME);
      break;
    }
}

static GtkItemFactoryEntry menu_items[] = {
  { N_("/_Game"), NULL, NULL, 0, "<Branch>" },
  { N_("/Game/_New Game"), "F2", GTK_SIGNAL_FUNC (menu_callback), NEW_GAME,
    NULL },
  { N_("/Game/_Hint"), "<control>H", GTK_SIGNAL_FUNC (menu_callback), HINT,
    NULL },
  { "/Game/sep1", NULL, NULL, 0, "<Separator>" },
  { N_("/Game/_Koi-Koi"), NULL, GTK_SIGNAL_FUNC (menu_callback), PLAY_GAME,
    "<RadioItem>" },
  { N_("/Game/Oi_cho-Kabu"), NULL, GTK_SIGNAL_FUNC (menu_callback),
    PLAY_GAME + 1, "/Game/Koi-Koi" },
  { "/Game/sep3", NULL, NULL, 0, "<Separator>" },
  { N_("/Game/O_ptions..."), NULL, GTK_SIGNAL_FUNC (menu_callback), OPTIONS,
    NULL },
  { "/Game/sep2", NULL, NULL, 0, "<Separator>" },
  { N_("/Game/E_xit"), "<control>Q", GTK_SIGNAL_FUNC (menu_callback),
    QUIT_GAME, NULL },
  { N_("/_Help"), NULL, NULL, 0, "<Branch>" },
  { N_("/Help/_Rules..."), NULL, GTK_SIGNAL_FUNC (menu_callback), RULES,
    NULL },
  { N_("/Help/_Yaku..."), NULL, GTK_SIGNAL_FUNC (menu_callback), YAKU,
    NULL },
  { N_("/Help/_Cards..."), NULL, GTK_SIGNAL_FUNC (menu_callback), CARDS,
    NULL },
  { "/Help/sep1", NULL, NULL, 0, "<Separator>" },
  { N_("/Help/_About Hanafuda..."), NULL, GTK_SIGNAL_FUNC (menu_callback),
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
  GtkWidget *frame = gtk_frame_new (NULL);
  GtkWidget *label = gtk_label_new ("");

  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
  gtk_misc_set_padding (GTK_MISC (label), 4, 0);
  gtk_container_add (GTK_CONTAINER (frame), label);
  gtk_box_pack_end (GTK_BOX (box), frame, FALSE, FALSE, 0);
  return label;
}

int
main (int argc, char *argv[])
{
  GtkWidget *vbox, *status_box, *table;
  GtkAccelGroup *accel_group;
  gint i;
  GtkItemFactory *factory;

  gtk_set_locale ();
  games_init_translation ();
  gtk_init (&argc, &argv);

  config = games_config_load (APPNAME);
  games_rand_seed (games_rand_new_seed ());
  load_options ();

  /* Only read here: the cards are drawn once the table has a size. */
  if (!hanafuda_readable (hanafuda_sets[opts.set], opts.border)
      && (opts.set == 0
          || !hanafuda_readable (hanafuda_sets[opts.set = 0], opts.border)))
    {
      fprintf (stderr, _("%s: the card images could not be read\n"), argv[0]);
      return 1;
    }

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title (GTK_WINDOW (window), _(APPNAME_LONG));
  gtk_window_set_wmclass (GTK_WINDOW (window), APPNAME, "Cghanafuda");
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
  game_items[0] = gtk_item_factory_get_widget_by_action (factory, PLAY_GAME);
  game_items[1] = gtk_item_factory_get_widget_by_action (factory,
                                                         PLAY_GAME + 1);
  hint_item = gtk_item_factory_get_widget_by_action (factory, HINT);
  yaku_item = gtk_item_factory_get_widget_by_action (factory, YAKU);
  show_chosen ();
  gtk_window_add_accel_group (GTK_WINDOW (window), accel_group);
  gtk_box_pack_start (GTK_BOX (vbox),
                      gtk_item_factory_get_widget (factory, "<main>"),
                      FALSE, FALSE, 0);

  /* The table starts at the size it was left at; once shown, it can be
   * made as small as MIN_WIDTH by MIN_HEIGHT. */
  table = table_new (MAX (games_config_get_int (config, "width",
                                                 FIRST_WIDTH), MIN_WIDTH),
                     MAX (games_config_get_int (config, "height",
                                                 FIRST_HEIGHT), MIN_HEIGHT));
  gtk_signal_connect (GTK_OBJECT (table), "button_press_event",
                      GTK_SIGNAL_FUNC (table_press), NULL);
  gtk_signal_connect (GTK_OBJECT (table), "motion_notify_event",
                      GTK_SIGNAL_FUNC (table_motion), NULL);
  gtk_signal_connect (GTK_OBJECT (table), "leave_notify_event",
                      GTK_SIGNAL_FUNC (table_leave), NULL);
  gtk_box_pack_start (GTK_BOX (vbox), table, TRUE, TRUE, 0);

  /* The status bar: messages, then the round and the scores. */
  status_box = gtk_hbox_new (FALSE, 2);
  statusbar = gtk_statusbar_new ();
  gtk_box_pack_start (GTK_BOX (status_box), statusbar, TRUE, TRUE, 0);
  for (i = 0; i < 3; i++)
    status_labels[i] = status_field (status_box);
  gtk_box_pack_start (GTK_BOX (vbox), status_box, FALSE, FALSE, 0);

  gtk_widget_show_all (vbox);
  games_window_set_icon (window, "cghanafuda", cghanafuda_xpm);
  gtk_widget_show (window);
  gtk_widget_set_usize (table, MIN_WIDTH, MIN_HEIGHT);
  play (opts.game);
  gtk_main ();

  game->stop ();
  chart_close ();
  table_free ();
  g_free (prompt_text);
  g_free (hint_text);
  games_config_free (config);
  return 0;
}
