/* cggames: best scores
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

#include "games.h"

#define MAX_PLACES 10

/* One line per score: category, value, time (seconds since 1970) and
 * player, separated by tabs. Each category keeps its ten best, best
 * first. */

typedef struct
{
  gchar *category;
  gint value;
  long long when;
  gchar *name;
} Score;

static void
score_free (Score *score)
{
  g_free (score->category);
  g_free (score->name);
  g_free (score);
}

static void
scores_free (GSList *scores)
{
  GSList *l;

  for (l = scores; l; l = l->next)
    score_free (l->data);
  g_slist_free (scores);
}

static GSList *
scores_read (const gchar *path)
{
  GSList *scores = NULL;
  FILE *file = fopen (path, "r");
  char *line = NULL;
  size_t size = 0;

  if (!file)
    return NULL;

  while (getline (&line, &size, file) >= 0)
    {
      gchar *field[4];
      gchar *p = line;
      gint i;
      Score *score;

      g_strchomp (line);
      for (i = 0; i < 4 && p; i++)
        {
          field[i] = p;
          p = strchr (p, '\t');
          if (p && i < 3)
            *p++ = '\0';
        }
      if (i < 4 || field[0][0] == '\0')
        continue;

      score = g_new0 (Score, 1);
      score->category = g_strdup (field[0]);
      score->value = atoi (field[1]);
      score->when = atoll (field[2]);
      score->name = g_strdup (field[3]);
      scores = g_slist_append (scores, score);
    }
  free (line);
  fclose (file);
  return scores;
}

static gboolean
scores_write (const gchar *path, GSList *scores)
{
  GString *text = g_string_new (NULL);
  GSList *l;
  gboolean ok;

  for (l = scores; l; l = l->next)
    {
      Score *score = l->data;

      g_string_sprintfa (text, "%s\t%d\t%lld\t%s\n", score->category,
                         score->value, score->when, score->name);
    }
  ok = games_write_file (path, text->str);
  g_string_free (text, TRUE);
  return ok;
}

static gboolean
better (GamesScoreStyle style, gint a, gint b)
{
  return style == GAMES_SCORES_TIME ? a < b : a > b;
}

/* The player's name, without the characters the file uses. */
static gchar *
player_name (void)
{
  gchar *name = g_strdup (g_get_user_name ());
  gchar *p;

  for (p = name; *p; p++)
    if (*p == '\t' || *p == '\n' || *p == '\r')
      *p = ' ';
  return name;
}

gint
games_scores_add (const gchar *game, GamesScoreStyle style,
                  const gchar *category, gint value)
{
  gchar *path = games_data_path (game, "scores");
  GSList *scores = scores_read (path);
  GSList *l, *after = NULL;
  gint place = 1, count;
  Score *score;

  /* Goes after every score in its category that is as good or better. */
  for (l = scores; l; l = l->next)
    {
      Score *s = l->data;

      if (strcmp (s->category, category) != 0)
        continue;
      if (better (style, value, s->value))
        break;
      place++;
      after = l;
    }

  if (place > MAX_PLACES)
    {
      scores_free (scores);
      g_free (path);
      return 0;
    }

  score = g_new0 (Score, 1);
  score->category = g_strdup (category);
  score->value = value;
  score->when = (long long) time (NULL);
  score->name = player_name ();
  if (after)
    after->next = g_slist_prepend (after->next, score);
  else
    {
      /* Before the category's first score, or at the end if it has none. */
      for (l = scores; l; l = l->next)
        if (strcmp (((Score *) l->data)->category, category) == 0)
          break;
      if (l)
        scores = g_slist_insert (scores, score, g_slist_position (scores, l));
      else
        scores = g_slist_append (scores, score);
    }

  /* Keeps the category's ten best. */
  count = 0;
  for (l = scores; l; )
    {
      GSList *next = l->next;
      Score *s = l->data;

      if (strcmp (s->category, category) == 0 && ++count > MAX_PLACES)
        {
          scores = g_slist_remove_link (scores, l);
          score_free (s);
          g_slist_free_1 (l);
        }
      l = next;
    }

  if (!scores_write (path, scores))
    place = 0;
  scores_free (scores);
  g_free (path);
  return place;
}

gchar *
games_format_time (gint seconds)
{
  if (seconds >= 3600)
    return g_strdup_printf ("%d:%02d:%02d", seconds / 3600,
                            seconds / 60 % 60, seconds % 60);
  return g_strdup_printf ("%d:%02d", seconds / 60, seconds % 60);
}

/* The scores window. */

typedef struct
{
  gchar *game;
  GamesScoreStyle style;
  GtkWidget *clist;
  gchar *highlight;             /* category of the place to highlight */
  gint place;
} ScoresView;

static void
scores_view_fill (ScoresView *view, const gchar *category)
{
  gchar *path = games_data_path (view->game, "scores");
  GSList *scores = scores_read (path);
  GSList *l;
  gint place = 0;
  GtkCList *clist = GTK_CLIST (view->clist);

  gtk_clist_freeze (clist);
  gtk_clist_clear (clist);
  for (l = scores; l; l = l->next)
    {
      Score *score = l->data;
      gchar rank[16], date[64];
      gchar *text[4];
      time_t when = (time_t) score->when;
      struct tm *tm = localtime (&when);

      if (strcmp (score->category, category) != 0)
        continue;
      if (++place > MAX_PLACES)
        break;
      g_snprintf (rank, sizeof rank, "%d", place);
      if (!tm || strftime (date, sizeof date, "%x", tm) == 0)
        date[0] = '\0';
      text[0] = rank;
      text[1] = score->name;
      text[2] = view->style == GAMES_SCORES_TIME
        ? games_format_time (score->value)
        : g_strdup_printf ("%d", score->value);
      text[3] = date;
      gtk_clist_append (clist, text);
      g_free (text[2]);
    }
  if (view->place && view->highlight
      && strcmp (view->highlight, category) == 0)
    gtk_clist_select_row (clist, view->place - 1, 0);
  gtk_clist_thaw (clist);

  scores_free (scores);
  g_free (path);
}

static void
scores_category_activate (GtkWidget *item, gpointer data)
{
  ScoresView *view = gtk_object_get_user_data (GTK_OBJECT (item));

  scores_view_fill (view, data);
}

static void
scores_view_destroy (GtkWidget *dialog, gpointer data)
{
  ScoresView *view = data;

  (void) dialog;
  g_free (view->game);
  g_free (view->highlight);
  g_free (view);
}

/* Keeps the highlighted row: the list is for reading, not choosing. */
static void
scores_unselect (GtkCList *clist, gint row, gint column, GdkEvent *event,
                 gpointer data)
{
  ScoresView *view = data;

  (void) column;
  (void) event;
  if (view->place && row == view->place - 1)
    gtk_clist_select_row (clist, row, 0);
}

void
games_scores_show (GtkWindow *parent, const gchar *title, const gchar *game,
                   GamesScoreStyle style,
                   const GamesScoreCategory *categories, gint n_categories,
                   const gchar *chooser, const gchar *category, gint place)
{
  ScoresView *view = g_new0 (ScoresView, 1);
  GtkWidget *dialog, *vbox, *button, *scrolled;
  gchar *titles[4];
  gint i, current = 0;

  view->game = g_strdup (game);
  view->style = style;
  view->highlight = g_strdup (category);
  view->place = place;

  dialog = games_dialog_new (parent, title);
  gtk_signal_connect (GTK_OBJECT (dialog), "destroy",
                      GTK_SIGNAL_FUNC (scores_view_destroy), view);
  vbox = GTK_DIALOG (dialog)->vbox;
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 8);
  gtk_box_set_spacing (GTK_BOX (vbox), 8);

  for (i = 0; i < n_categories; i++)
    if (strcmp (categories[i].key, category) == 0)
      current = i;

  if (n_categories > 1)
    {
      GtkWidget *hbox = gtk_hbox_new (FALSE, 6);
      GtkWidget *option = gtk_option_menu_new ();
      GtkWidget *menu = gtk_menu_new ();

      for (i = 0; i < n_categories; i++)
        {
          GtkWidget *item = gtk_menu_item_new_with_label (_(categories[i].name));

          gtk_object_set_user_data (GTK_OBJECT (item), view);
          gtk_signal_connect (GTK_OBJECT (item), "activate",
                              GTK_SIGNAL_FUNC (scores_category_activate),
                              (gpointer) categories[i].key);
          gtk_widget_show (item);
          gtk_menu_append (GTK_MENU (menu), item);
        }
      gtk_option_menu_set_menu (GTK_OPTION_MENU (option), menu);
      gtk_option_menu_set_history (GTK_OPTION_MENU (option), current);
      gtk_box_pack_start (GTK_BOX (hbox), gtk_label_new (chooser),
                          FALSE, FALSE, 0);
      gtk_box_pack_start (GTK_BOX (hbox), option, FALSE, FALSE, 0);
      gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);
    }

  titles[0] = "";
  titles[1] = _("Name");
  titles[2] = style == GAMES_SCORES_TIME ? _("Time") : _("Score");
  titles[3] = _("Date");
  view->clist = gtk_clist_new_with_titles (4, titles);
  gtk_clist_column_titles_passive (GTK_CLIST (view->clist));
  gtk_clist_set_selection_mode (GTK_CLIST (view->clist),
                                GTK_SELECTION_SINGLE);
  for (i = 0; i < 4; i++)
    gtk_clist_set_column_auto_resize (GTK_CLIST (view->clist), i, TRUE);
  gtk_clist_set_column_justification (GTK_CLIST (view->clist), 0,
                                      GTK_JUSTIFY_RIGHT);
  gtk_clist_set_column_justification (GTK_CLIST (view->clist), 2,
                                      GTK_JUSTIFY_RIGHT);
  gtk_signal_connect (GTK_OBJECT (view->clist), "unselect_row",
                      GTK_SIGNAL_FUNC (scores_unselect), view);

  scrolled = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_container_add (GTK_CONTAINER (scrolled), view->clist);
  gtk_widget_set_usize (scrolled, 340, 230);
  gtk_box_pack_start (GTK_BOX (vbox), scrolled, TRUE, TRUE, 0);

  scores_view_fill (view, categories[current].key);

  button = games_dialog_add_button (dialog, _("Close"));
  gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
                             GTK_SIGNAL_FUNC (gtk_widget_destroy),
                             GTK_OBJECT (dialog));
  gtk_widget_show_all (dialog);
}
