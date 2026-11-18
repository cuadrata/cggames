/* cggames: what the games share
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

#ifndef GAMES_H
#define GAMES_H

#include <stdint.h>
#include <time.h>
#include <gtk/gtk.h>

/* Translatable text, in the cggames text domain. */
#ifdef ENABLE_NLS
#include <libintl.h>
#define _(s) gettext (s)
#else
#define _(s) (s)
#define ngettext(s, p, n) ((n) == 1 ? (s) : (p))
#endif
#define N_(s) (s)

/* Binds the games' text domain; called after gtk_set_locale. */
void games_init_translation (void);

/* Files. A game named "foo" keeps its settings in
 * $XDG_CONFIG_HOME/cggames/foo.conf and its other files (scores) in
 * $XDG_DATA_HOME/cggames/foo.EXT, with ~/.config and ~/.local/share
 * when the variables are unset, empty or not absolute. The installed
 * images and such are in DATADIR/cggames/foo. */
gchar *games_config_path (const gchar *game, const gchar *ext);
gchar *games_data_path (const gchar *game, const gchar *ext);
gchar *games_installed_path (const gchar *game, const gchar *file);

/* Writes contents to path through a temporary file and a rename,
 * creating the directory first. Reports failure on stderr. */
gboolean games_write_file (const gchar *path, const gchar *contents);

/* Settings: key = value lines, read once and written whole. */
typedef struct _GamesConfig GamesConfig;

GamesConfig *games_config_load (const gchar *game);
void games_config_free (GamesConfig *config);
const gchar *games_config_get_string (GamesConfig *config, const gchar *key,
                                      const gchar *fallback);
gint games_config_get_int (GamesConfig *config, const gchar *key,
                           gint fallback);
void games_config_set_string (GamesConfig *config, const gchar *key,
                              const gchar *value);
void games_config_set_int (GamesConfig *config, const gchar *key,
                           gint value);
gboolean games_config_save (GamesConfig *config);

/* Ten best scores per category, in $XDG_DATA_HOME/cggames/GAME.scores. */
typedef enum
{
  GAMES_SCORES_TIME,            /* seconds, fewest first */
  GAMES_SCORES_POINTS           /* points, most first */
} GamesScoreStyle;

typedef struct
{
  const gchar *key;             /* as stored */
  const gchar *name;            /* as shown, translatable */
} GamesScoreCategory;

/* Records a score and returns its place, 1 to 10, or 0 if it is not
 * among the ten best. */
gint games_scores_add (const gchar *game, GamesScoreStyle style,
                       const gchar *category, gint value);

/* Shows the scores, for category at first, with the place given (if not
 * 0) highlighted. With more than one category, the others can be
 * chosen beside the label chooser ("Map:", say). */
void games_scores_show (GtkWindow *parent, const gchar *title,
                        const gchar *game, GamesScoreStyle style,
                        const GamesScoreCategory *categories, gint n_categories,
                        const gchar *chooser, const gchar *category, gint place);

/* "m:ss", or "h:mm:ss" from an hour. */
gchar *games_format_time (gint seconds);

/* Dialogs, modal over parent. */
typedef void (*GamesAnswerFunc) (gboolean yes, gpointer data);

void games_message (GtkWindow *parent, const gchar *title,
                    const gchar *text);
void games_question (GtkWindow *parent, const gchar *title,
                     const gchar *text, const gchar *yes, const gchar *no,
                     GamesAnswerFunc func, gpointer data);
/* Asks with a button for each of the NULL-ended buttons, the first the
 * default; func gets the index of the one clicked, or -1 if the
 * dialog was closed otherwise, once it is gone. */
typedef void (*GamesChoiceFunc) (gint answer, gpointer data);
void games_choose (GtkWindow *parent, const gchar *title, const gchar *text,
                   const gchar *const *buttons, GamesChoiceFunc func,
                   gpointer data);
/* The About dialog, with the icon installed for game, as "cgchess". */
void games_about (GtkWindow *parent, const gchar *game, const gchar *name,
                  const gchar *copyright, const gchar *const *credits,
                  const gchar *comments);

/* Gives a game's windows their icon: the one it is installed with, by
 * its name, in colour and in the size the window manager wants, for
 * this window and every one after it; and from the XPM in the game's
 * icons directory, for this window, where that one is not to be found
 * or the window manager knows only a pixmap. The window is realized
 * for it. */
void games_window_set_icon (GtkWidget *window, const gchar *game,
                            gchar **xpm);

/* A dialog with its buttons in a button box at the bottom right; the
 * first button is the default. */
GtkWidget *games_dialog_new (GtkWindow *parent, const gchar *title);
GtkWidget *games_dialog_add_button (GtkWidget *dialog, const gchar *label);

/* A text shown in the status bar for a few seconds. */
void games_flash (GtkStatusbar *statusbar, const gchar *text);

/* A label counting up seconds while running. */
GtkWidget *games_clock_new (void);
void games_clock_start (GtkWidget *clock);
void games_clock_stop (GtkWidget *clock);
void games_clock_set (GtkWidget *clock, gint seconds);
gint games_clock_get (GtkWidget *clock);
gboolean games_clock_running (GtkWidget *clock);

/* The same numbers for the same seed with any C library, so that a game
 * number means the same game everywhere. */
void games_rand_seed (uint32_t seed);
/* 0 to n - 1, n at least 1. */
gint games_rand_int (gint n);
/* A seed taken from the time and process. */
uint32_t games_rand_new_seed (void);

#endif
