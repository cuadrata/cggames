/* cggames: dialogs, status bar messages and the clock
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

#include <time.h>
#include <gdk/gdkkeysyms.h>

#include "games.h"

#define FLASH_MS 3000

static gint
dialog_key_press (GtkWidget *dialog, GdkEventKey *event, gpointer data)
{
  (void) data;
  if (event->keyval != GDK_Escape)
    return FALSE;
  gtk_widget_destroy (dialog);
  return TRUE;
}

GtkWidget *
games_dialog_new (GtkWindow *parent, const gchar *title)
{
  GtkWidget *dialog = gtk_dialog_new ();
  GtkWidget *box = gtk_hbutton_box_new ();

  gtk_window_set_title (GTK_WINDOW (dialog), title);
  if (parent)
    gtk_window_set_transient_for (GTK_WINDOW (dialog), parent);
  gtk_window_set_policy (GTK_WINDOW (dialog), FALSE, FALSE, TRUE);
  gtk_signal_connect (GTK_OBJECT (dialog), "key_press_event",
                      GTK_SIGNAL_FUNC (dialog_key_press), NULL);

  gtk_button_box_set_layout (GTK_BUTTON_BOX (box), GTK_BUTTONBOX_END);
  gtk_button_box_set_spacing (GTK_BUTTON_BOX (box), 6);
  gtk_container_set_border_width (GTK_CONTAINER (GTK_DIALOG (dialog)->action_area), 6);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->action_area), box,
                      TRUE, TRUE, 0);
  gtk_object_set_data (GTK_OBJECT (dialog), "games-buttons", box);
  return dialog;
}

GtkWidget *
games_dialog_add_button (GtkWidget *dialog, const gchar *label)
{
  GtkWidget *box = gtk_object_get_data (GTK_OBJECT (dialog), "games-buttons");
  GtkWidget *button = gtk_button_new_with_label (label);

  GTK_WIDGET_SET_FLAGS (button, GTK_CAN_DEFAULT);
  gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);
  if (g_list_length (GTK_BOX (box)->children) == 1)
    gtk_widget_grab_default (button);
  return button;
}

/* A dialog showing text, with room round it. */
static GtkWidget *
text_dialog (GtkWindow *parent, const gchar *title, const gchar *text)
{
  GtkWidget *dialog = games_dialog_new (parent, title);
  GtkWidget *label = gtk_label_new (text);

  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_MOUSE);
  gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
  gtk_misc_set_padding (GTK_MISC (label), 16, 12);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), label,
                      TRUE, TRUE, 0);
  return dialog;
}

void
games_message (GtkWindow *parent, const gchar *title, const gchar *text)
{
  GtkWidget *dialog = text_dialog (parent, title, text);
  GtkWidget *button = games_dialog_add_button (dialog, _("OK"));

  gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
                             GTK_SIGNAL_FUNC (gtk_widget_destroy),
                             GTK_OBJECT (dialog));
  gtk_widget_show_all (dialog);
}

typedef struct
{
  GamesAnswerFunc func;
  gpointer data;
  gboolean yes;
} Question;

static void
question_yes (GtkWidget *button, gpointer data)
{
  GtkWidget *dialog = data;
  Question *question = gtk_object_get_data (GTK_OBJECT (dialog),
                                            "games-question");

  (void) button;
  question->yes = TRUE;
  gtk_widget_destroy (dialog);
}

/* Every way of closing the dialog answers once, after it is gone. */
static void
question_destroy (GtkWidget *dialog, gpointer data)
{
  Question *question = data;

  (void) dialog;
  question->func (question->yes, question->data);
  g_free (question);
}

void
games_question (GtkWindow *parent, const gchar *title, const gchar *text,
                const gchar *yes, const gchar *no,
                GamesAnswerFunc func, gpointer data)
{
  GtkWidget *dialog = text_dialog (parent, title, text);
  GtkWidget *yes_button = games_dialog_add_button (dialog, yes);
  GtkWidget *no_button = games_dialog_add_button (dialog, no);
  Question *question = g_new0 (Question, 1);

  question->func = func;
  question->data = data;
  gtk_object_set_data (GTK_OBJECT (dialog), "games-question", question);
  gtk_signal_connect (GTK_OBJECT (dialog), "destroy",
                      GTK_SIGNAL_FUNC (question_destroy), question);
  gtk_signal_connect (GTK_OBJECT (yes_button), "clicked",
                      GTK_SIGNAL_FUNC (question_yes), dialog);
  gtk_signal_connect_object (GTK_OBJECT (no_button), "clicked",
                             GTK_SIGNAL_FUNC (gtk_widget_destroy),
                             GTK_OBJECT (dialog));
  gtk_widget_show_all (dialog);
}

typedef struct
{
  GamesChoiceFunc func;
  gpointer data;
  gint answer;
} Choice;

static void
choice_clicked (GtkWidget *button, gpointer data)
{
  GtkWidget *dialog = data;
  Choice *choice = gtk_object_get_data (GTK_OBJECT (dialog), "games-choice");

  choice->answer = GPOINTER_TO_INT (gtk_object_get_user_data
                                    (GTK_OBJECT (button)));
  gtk_widget_destroy (dialog);
}

static void
choice_destroy (GtkWidget *dialog, gpointer data)
{
  Choice *choice = data;

  (void) dialog;
  choice->func (choice->answer, choice->data);
  g_free (choice);
}

void
games_choose (GtkWindow *parent, const gchar *title, const gchar *text,
              const gchar *const *buttons, GamesChoiceFunc func, gpointer data)
{
  GtkWidget *dialog = text_dialog (parent, title, text);
  Choice *choice = g_new0 (Choice, 1);
  gint i;

  choice->func = func;
  choice->data = data;
  choice->answer = -1;
  gtk_object_set_data (GTK_OBJECT (dialog), "games-choice", choice);
  gtk_signal_connect (GTK_OBJECT (dialog), "destroy",
                      GTK_SIGNAL_FUNC (choice_destroy), choice);
  for (i = 0; buttons[i]; i++)
    {
      GtkWidget *button = games_dialog_add_button (dialog, buttons[i]);

      gtk_object_set_user_data (GTK_OBJECT (button), GINT_TO_POINTER (i));
      gtk_signal_connect (GTK_OBJECT (button), "clicked",
                          GTK_SIGNAL_FUNC (choice_clicked), dialog);
    }
  gtk_widget_show_all (dialog);
}

/* The game's icon in the About dialog: its pixels, premultiplied. */
typedef struct
{
  guchar *pixels;
  gint width;
  gint height;
} Icon;

static gint
icon_expose (GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
  Icon *icon = data;

  (void) event;
  gdk_draw_rgba_image_scaled (widget->window, widget->style->black_gc,
                              (widget->allocation.width - icon->width) / 2,
                              (widget->allocation.height - icon->height) / 2,
                              icon->width, icon->height, 1, icon->pixels,
                              icon->width * 4);
  return TRUE;
}

static void
icon_destroy (gpointer data)
{
  Icon *icon = data;

  g_free (icon->pixels);
  g_free (icon);
}

/* A widget showing the game's installed icon, or NULL without one. */
static GtkWidget *
icon_new (const gchar *game)
{
  gchar *path = g_strconcat (GAMES_ICONDIR "/", game, ".png", NULL);
  GtkWidget *area;
  Icon *icon = g_new0 (Icon, 1);

  /* as the toolkit reads it: premultiplied, which is how it is drawn */
  icon->pixels = gdk_png_load (path, &icon->width, &icon->height, NULL);
  g_free (path);
  if (!icon->pixels)
    {
      g_free (icon);
      return NULL;
    }
  area = gtk_drawing_area_new ();
  gtk_drawing_area_size (GTK_DRAWING_AREA (area), icon->width, icon->height);
  gtk_signal_connect (GTK_OBJECT (area), "expose_event",
                      GTK_SIGNAL_FUNC (icon_expose), icon);
  gtk_object_set_data_full (GTK_OBJECT (area), "games-icon", icon,
                            icon_destroy);
  return area;
}

void
games_window_set_icon (GtkWidget *window, const gchar *game, gchar **xpm)
{
  GdkPixmap *icon;
  GdkBitmap *mask = NULL;

  g_return_if_fail (GTK_IS_WINDOW (window));
  g_return_if_fail (game != NULL);
  g_return_if_fail (xpm != NULL);

  gtk_window_set_default_icon_name (game);
  gtk_widget_realize (window);
  /* The window manager is told of them by number: they are kept for as
   * long as the game runs. */
  icon = gdk_pixmap_create_from_xpm_d (window->window, &mask, NULL, xpm);
  if (icon)
    gdk_window_set_icon (window->window, NULL, icon, mask);
}

void
games_about (GtkWindow *parent, const gchar *game, const gchar *name,
             const gchar *copyright, const gchar *const *credits,
             const gchar *comments)
{
  GtkWidget *dialog, *vbox, *label, *button, *icon;
  gchar *title = g_strdup_printf (_("About %s"), name);
  gchar *heading = g_strdup_printf ("%s %s", name, VERSION);

  dialog = games_dialog_new (parent, title);
  vbox = GTK_DIALOG (dialog)->vbox;
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 16);
  gtk_box_set_spacing (GTK_BOX (vbox), 10);

  icon = icon_new (game);
  if (icon)
    gtk_box_pack_start (GTK_BOX (vbox), icon, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), gtk_label_new (heading), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), gtk_label_new (copyright),
                      FALSE, FALSE, 0);
  if (credits)
    {
      GString *text = g_string_new (NULL);

      for (; *credits; credits++)
        {
          if (text->len)
            g_string_append_c (text, '\n');
          g_string_append (text, *credits);
        }
      label = gtk_label_new (text->str);
      gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_LEFT);
      gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);
      g_string_free (text, TRUE);
    }
  if (comments)
    gtk_box_pack_start (GTK_BOX (vbox), gtk_label_new (comments),
                        FALSE, FALSE, 0);

  button = games_dialog_add_button (dialog, _("Close"));
  gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
                             GTK_SIGNAL_FUNC (gtk_widget_destroy),
                             GTK_OBJECT (dialog));
  gtk_widget_show_all (dialog);
  g_free (heading);
  g_free (title);
}

/* Status bar messages. */

typedef struct
{
  guint context;
  guint message;
  guint timeout;
} Flash;

static gint
flash_timeout (gpointer data)
{
  GtkStatusbar *statusbar = data;
  Flash *flash = gtk_object_get_data (GTK_OBJECT (statusbar), "games-flash");

  gtk_statusbar_remove (statusbar, flash->context, flash->message);
  flash->timeout = 0;
  return FALSE;
}

static void
flash_destroy (gpointer data)
{
  Flash *flash = data;

  if (flash->timeout)
    gtk_timeout_remove (flash->timeout);
  g_free (flash);
}

void
games_flash (GtkStatusbar *statusbar, const gchar *text)
{
  Flash *flash = gtk_object_get_data (GTK_OBJECT (statusbar), "games-flash");

  if (!flash)
    {
      flash = g_new0 (Flash, 1);
      flash->context = gtk_statusbar_get_context_id (statusbar, "flash");
      gtk_object_set_data_full (GTK_OBJECT (statusbar), "games-flash",
                                flash, flash_destroy);
    }
  if (flash->timeout)
    {
      gtk_timeout_remove (flash->timeout);
      gtk_statusbar_remove (statusbar, flash->context, flash->message);
    }
  flash->message = gtk_statusbar_push (statusbar, flash->context, text);
  flash->timeout = gtk_timeout_add (FLASH_MS, flash_timeout, statusbar);
}

/* The clock. It counts with the monotonic clock, so that changing the
 * system time doesn't change a game's. */

typedef struct
{
  gint base;                    /* seconds before the last start */
  gboolean running;
  struct timespec start;
  guint timeout;
  gint shown;
} Clock;

static gint
clock_seconds (Clock *clock)
{
  struct timespec now;

  if (!clock->running)
    return clock->base;
  clock_gettime (CLOCK_MONOTONIC, &now);
  return clock->base + (gint) (now.tv_sec - clock->start.tv_sec
                               - (now.tv_nsec < clock->start.tv_nsec));
}

static void
clock_show (GtkWidget *label, Clock *clock)
{
  gint seconds = clock_seconds (clock);
  gchar *text;

  if (seconds == clock->shown)
    return;
  clock->shown = seconds;
  text = games_format_time (seconds);
  gtk_label_set_text (GTK_LABEL (label), text);
  g_free (text);
}

static gint
clock_tick (gpointer data)
{
  GtkWidget *label = data;

  clock_show (label, gtk_object_get_data (GTK_OBJECT (label), "games-clock"));
  return TRUE;
}

static void
clock_destroy (gpointer data)
{
  Clock *clock = data;

  if (clock->timeout)
    gtk_timeout_remove (clock->timeout);
  g_free (clock);
}

GtkWidget *
games_clock_new (void)
{
  GtkWidget *label = gtk_label_new ("0:00");
  Clock *clock = g_new0 (Clock, 1);

  gtk_object_set_data_full (GTK_OBJECT (label), "games-clock", clock,
                            clock_destroy);
  return label;
}

void
games_clock_start (GtkWidget *label)
{
  Clock *clock = gtk_object_get_data (GTK_OBJECT (label), "games-clock");

  if (clock->running)
    return;
  clock->running = TRUE;
  clock_gettime (CLOCK_MONOTONIC, &clock->start);
  /* Often enough that the display never skips a second. */
  clock->timeout = gtk_timeout_add (250, clock_tick, label);
}

void
games_clock_stop (GtkWidget *label)
{
  Clock *clock = gtk_object_get_data (GTK_OBJECT (label), "games-clock");

  if (!clock->running)
    return;
  clock->base = clock_seconds (clock);
  clock->running = FALSE;
  gtk_timeout_remove (clock->timeout);
  clock->timeout = 0;
  clock_show (label, clock);
}

void
games_clock_set (GtkWidget *label, gint seconds)
{
  Clock *clock = gtk_object_get_data (GTK_OBJECT (label), "games-clock");

  clock->base = seconds;
  if (clock->running)
    clock_gettime (CLOCK_MONOTONIC, &clock->start);
  clock_show (label, clock);
}

gint
games_clock_get (GtkWidget *label)
{
  return clock_seconds (gtk_object_get_data (GTK_OBJECT (label),
                                             "games-clock"));
}

gboolean
games_clock_running (GtkWidget *label)
{
  Clock *clock = gtk_object_get_data (GTK_OBJECT (label), "games-clock");

  return clock->running;
}
