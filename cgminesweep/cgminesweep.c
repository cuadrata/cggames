/*
 * cgminesweep, from GNOME Mines
 * Copyright (C) 1997-1999 the Free Software Foundation
 * Copyright (C) 2026 The CGTK authors
 *
 * Author:        Pista <szekeres@cyberspace.mht.bme.hu>
 * Score support: horape@compendium.com.ar
 * Mine Resizing: djb@redhat.com
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
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

#include "games.h"
#include "minefield.h"

#include "icons/cgminesweep.xpm"

#define APPNAME "cgminesweep"
#define APPNAME_LONG N_("Minesweep")

#define ELEMENTS(x) (sizeof (x) / sizeof (x [0]))

static GtkWidget *mfield;
static GtkWidget *pref_dialog;
static GtkWidget *rbutton;
static GtkWidget *window;
static GtkWidget *xentry;
static GtkWidget *yentry;
static GtkWidget *mentry;
static GtkWidget *sentry;
static GtkWidget *cframe;
static GtkWidget *clk;
static GtkWidget *panel;
static GtkWidget *board;
static GtkWidget *field_box;
static GamesConfig *config;
static gint ysize=-1, xsize=-1;
static gint nmines=-1;
static gint fsize=-1, fsc;
static gint minesize=-1;

/* The field sizes: the stored names, the names shown (Windows'), and
 * the three fixed sizes' width, height and mines. The stored names are
 * GNOME Mines', so scores kept before still count. */
static const GamesScoreCategory fsize2names[] = {
        { "Tiny",   N_("Beginner") },
        { "Medium", N_("Intermediate") },
        { "Biiiig", N_("Expert") },
        { "Custom", N_("Custom") },
};
static const gint size_table[3][3] = {{ 8, 8, 10 }, {16, 16, 40}, {30, 16, 99}};

/* Above the field, as Windows' Minesweeper has it: the mines left and
 * the seconds in red figures, each sunk in a frame, and between them a
 * button with the face on it, which starts a new game. */

#define DIGIT_WIDTH 13
#define DIGIT_HEIGHT 23

enum { FACE_SMILE, FACE_OH, FACE_COOL, FACE_DEAD };

enum {
        INK_BLACK,
        INK_LIT,                /* a figure's segment on */
        INK_UNLIT,              /* and off */
        N_INKS
};

static const guint32 ink_colors[N_INKS] = {
        0x000000, 0xff0000, 0x400000
};

static GdkGC *ink[N_INKS];
static GtkWidget *mines_counter, *time_counter, *face_area;
static gint face = FACE_SMILE;
static gint shown_mines = G_MININT, shown_seconds = -1;

/* The mines not flagged yet; fewer than none when there are too many
 * flags. */
static gint
mines_left (void)
{
        GtkMineField *field = GTK_MINEFIELD (mfield);

        return (gint) field->mcount - (gint) field->flags;
}

static gint
seconds (void)
{
        return MIN (games_clock_get (clk), 999);
}

/* A segment of a figure: a long hexagon from (x0, y0) to (x1, y1),
 * across or down, 3 thick. */
static void
draw_segment (GdkWindow *win, GdkGC *gc, gint x0, gint y0, gint x1, gint y1)
{
        GdkPoint p[6];

        if (y0 == y1) {
                p[0].x = x0;     p[0].y = y0;
                p[1].x = x0 + 1; p[1].y = y0 - 1;
                p[2].x = x1 - 1; p[2].y = y0 - 1;
                p[3].x = x1;     p[3].y = y0;
                p[4].x = x1 - 1; p[4].y = y0 + 1;
                p[5].x = x0 + 1; p[5].y = y0 + 1;
        } else {
                p[0].x = x0;     p[0].y = y0;
                p[1].x = x0 + 1; p[1].y = y0 + 1;
                p[2].x = x0 + 1; p[2].y = y1 - 1;
                p[3].x = x0;     p[3].y = y1;
                p[4].x = x0 - 1; p[4].y = y1 - 1;
                p[5].x = x0 - 1; p[5].y = y0 + 1;
        }
        gdk_draw_polygon (win, gc, TRUE, p, 6);
}

/* Figure d, 0 to 9, or 10 for a minus, in a cell DIGIT_WIDTH by 23. */
static void
draw_digit (GdkWindow *win, gint x, gint y, gint d)
{
        /* The segments, a to g: top, top right, bottom right, bottom,
         * bottom left, top left, middle. */
        static const guint8 lit[11] = {
                0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f, 0x40
        };
        const gint l = x + 2, r = x + 10, t = y + 2, m = y + 11, b = y + 20;
        gint i;

        for (i = 0; i < 7; i++) {
                GdkGC *gc = ink[lit[d] & (1 << i) ? INK_LIT : INK_UNLIT];

                switch (i) {
                case 0: draw_segment (win, gc, l + 1, t, r - 1, t); break;
                case 1: draw_segment (win, gc, r, t + 1, r, m - 1); break;
                case 2: draw_segment (win, gc, r, m + 1, r, b - 1); break;
                case 3: draw_segment (win, gc, l + 1, b, r - 1, b); break;
                case 4: draw_segment (win, gc, l, m + 1, l, b - 1); break;
                case 5: draw_segment (win, gc, l, t + 1, l, m - 1); break;
                case 6: draw_segment (win, gc, l + 1, m, r - 1, m); break;
                }
        }
}

/* Three figures on black, as Windows shows them: -99 to 999. */
static gint
counter_expose (GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
        gint value = widget == mines_counter ? mines_left () : seconds ();
        gint digits[3], i;

        (void) event;
        (void) data;
        if (widget == mines_counter)
                shown_mines = value;
        else
                shown_seconds = value;

        value = CLAMP (value, -99, 999);
        if (value < 0) {
                digits[0] = 10;
                value = -value;
        } else
                digits[0] = value / 100;
        digits[1] = value / 10 % 10;
        digits[2] = value % 10;

        gdk_draw_rectangle (widget->window, ink[INK_BLACK], TRUE, 0, 0,
                            widget->allocation.width, widget->allocation.height);
        for (i = 0; i < 3; i++)
                draw_digit (widget->window, i * DIGIT_WIDTH, 0, digits[i]);
        return TRUE;
}

/* A counter: its figures drawn in a drawing area sunk in a frame. */
static GtkWidget *
counter_new (GtkWidget **area)
{
        GtkWidget *frame = gtk_frame_new (NULL);

        gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
        *area = gtk_drawing_area_new ();
        gtk_drawing_area_size (GTK_DRAWING_AREA (*area), 3 * DIGIT_WIDTH,
                               DIGIT_HEIGHT);
        gtk_signal_connect (GTK_OBJECT (*area), "expose_event",
                            GTK_SIGNAL_FUNC (counter_expose), NULL);
        gtk_container_add (GTK_CONTAINER (frame), *area);
        return frame;
}

/* The face's colour at (x, y) of its 20 by 20, sampled at the middle of
 * each device pixel: 0 for none, else 0xRRGGBB | 0xff000000. Its edges
 * are hard, a 17 across circle drawn in whole pixels at scale 1, as
 * Windows' face is, and finer at 2. */
static guint32
face_pixel (gint f, double x, double y)
{
        const guint32 black = 0xff000000, yellow = 0xffffff00;
        double d = hypot (x - 10, y - 10);
        gint k;

#define RING(cx, cy, r0, r1) \
        (hypot (x - (cx), y - (cy)) >= (r0) && hypot (x - (cx), y - (cy)) < (r1))

        if (d >= 8.5)
                return 0;
        if (d >= 7.5)
                return black;

        switch (f) {
        case FACE_SMILE:
        case FACE_OH:
                /* Eyes, wider open in surprise. */
                if (y >= 6 && y < (f == FACE_OH ? 9 : 8)
                    && ((x >= 6 && x < 8) || (x >= 12 && x < 14)))
                        return black;
                if (f == FACE_OH)
                        return RING (10, 13.5, 1.2, 2.2) ? black : yellow;
                /* A smile. */
                if (RING (10, 9.5, 4.5, 5.5) && y > 12 && x > 5.5 && x < 14.5)
                        return black;
                break;
        case FACE_COOL:
                /* Sunglasses, and the smile. */
                if (y >= 6 && y < 7 && x >= 4 && x < 16)
                        return black;
                if (y >= 6 && y < 9 && ((x >= 5 && x < 9) || (x >= 11 && x < 15)))
                        return black;
                if (RING (10, 9.5, 4.5, 5.5) && y > 12 && x > 5.5 && x < 14.5)
                        return black;
                break;
        case FACE_DEAD:
                /* Crosses for eyes, and a frown. */
                for (k = 0; k < 2; k++) {
                        double dx = fabs (x - (k ? 13.5 : 6.5)), dy = fabs (y - 6.5);

                        if (dx < 1.5 && dy < 1.5 && fabs (dx - dy) < 0.5)
                                return black;
                }
                if (RING (10, 17, 4.5, 5.5) && y < 15 && x > 5.5 && x < 14.5)
                        return black;
                break;
        }
#undef RING
        return yellow;
}

/* The face, over the button's own colour. */
static gint
face_expose (GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
        gint scale = gdk_window_get_scale_factor (widget->window);
        gint size = 20 * scale, x, y;
        gint x0 = (widget->allocation.width - 20) / 2;
        gint y0 = (widget->allocation.height - 20) / 2;
        guchar *buf = g_malloc ((gsize) size * size * 4), *p = buf;

        (void) event;
        (void) data;
        gdk_draw_rectangle (widget->window,
                            widget->style->bg_gc[GTK_WIDGET_STATE (widget)],
                            TRUE, 0, 0, widget->allocation.width,
                            widget->allocation.height);
        for (y = 0; y < size; y++)
                for (x = 0; x < size; x++, p += 4) {
                        guint32 c = face_pixel (face, (x + 0.5) / scale,
                                                (y + 0.5) / scale);

                        /* Premultiplied, as GDK wants it: none is 0, 0, 0, 0. */
                        p[0] = (guchar) (c >> 16);
                        p[1] = (guchar) (c >> 8);
                        p[2] = (guchar) c;
                        p[3] = (guchar) (c >> 24);
                }
        gdk_draw_rgba_image_scaled (widget->window, widget->style->black_gc,
                                    x0, y0, 20, 20, scale, buf, size * 4);
        g_free (buf);
        return TRUE;
}

static void
show_face (gint f)
{
        if (face == f)
                return;
        face = f;
        gtk_widget_queue_draw (face_area);
}

/* The counters are drawn again when they change. */
static gint
counters_tick (gpointer data)
{
        (void) data;
        if (mines_left () != shown_mines)
                gtk_widget_queue_draw (mines_counter);
        if (seconds () != shown_seconds)
                gtk_widget_queue_draw (time_counter);
        return TRUE;
}

static void quit_game(void)
{
        gtk_main_quit();
}

static void set_flabel(GtkMineField *field)
{
        (void) field;
        counters_tick (NULL);
}

static void new_game (void);

static void
face_clicked (GtkWidget *widget, gpointer data)
{
        (void) widget;
        (void) data;
        new_game ();
}

/* The fixed sizes count time, fewest seconds first; Custom counts
 * points, more first, as the original did. */
static GamesScoreStyle
score_style (gint size)
{
        return size < 3 ? GAMES_SCORES_TIME : GAMES_SCORES_POINTS;
}

static void
show_scores (gint size, gint pos)
{
        /* One window for both kinds would list times as points: each
         * size is shown in its own kind, the others alongside it. */
        gint i, n = 0;
        GamesScoreCategory cats[ELEMENTS (fsize2names)];

        for (i = 0; i < (gint) ELEMENTS (fsize2names); i++)
                if (score_style (i) == score_style (size))
                        cats[n++] = fsize2names[i];
        games_scores_show (GTK_WINDOW (window), _("Minesweep Scores"), APPNAME,
                           score_style (size), cats, n, _("Game:"),
                           fsize2names[size].key, pos);
}

static void top_ten(void)
{
        show_scores(fsize, 0);
}

static void new_game(void)
{
        games_clock_stop(clk);
        games_clock_set(clk, 0);
        show_face(FACE_SMILE);
        gtk_minefield_restart(GTK_MINEFIELD(mfield));
        gtk_widget_queue_draw(mfield);
        set_flabel(GTK_MINEFIELD(mfield));

        gtk_widget_hide (rbutton);
        gtk_widget_show (mfield);
}

/* While the game runs, losing the focus hides the field and stops the
 * clock, until the player comes back. */
static gint focus_out_cb (GtkWidget *widget, GdkEventFocus *event, gpointer data)
{
        (void) widget;
        (void) event;
        (void) data;
        if (!games_clock_running(clk))
                return FALSE;

        gtk_widget_hide (mfield);
        gtk_widget_show (rbutton);
        games_clock_stop(clk);
        return FALSE;
}

static void resume_game_cb (GtkWidget *widget, gpointer data)
{
        (void) widget;
        (void) data;
        gtk_widget_hide (rbutton);
        gtk_widget_show (mfield);
        games_clock_start(clk);
}

static void marks_changed(GtkWidget *widget, gpointer data)
{
        (void) data;
        set_flabel(GTK_MINEFIELD(widget));
        games_clock_start(clk);
}

static void lose_game(GtkWidget *widget, gpointer data)
{
        (void) widget;
        (void) data;
        show_face(FACE_DEAD);
        games_clock_stop(clk);
}

static void win_game(GtkWidget *widget, gpointer data)
{
        gint score, pos, seconds;

        (void) widget;
        (void) data;
        games_clock_stop(clk);
        show_face(FACE_COOL);
        set_flabel(GTK_MINEFIELD(mfield));

        /* Even a game won before the clock showed a second took one. */
        seconds = MAX (games_clock_get(clk), 1);
        if (fsize < 3)
                score = seconds;
        else
                /* The share of the field that was mines, in per cent, for
                 * each second; the original kept two decimals of that. */
                score = (gint) (100.0 * ((nmines * 100) / (xsize * ysize))
                                / seconds + 0.5);

        pos = games_scores_add(APPNAME, score_style (fsize),
                               fsize2names[fsize].key, score);
        show_scores(fsize, pos);
}

static void look_cell(GtkWidget *widget, gpointer data)
{
        (void) widget;
        (void) data;
        show_face(FACE_OH);
        games_clock_start(clk);
}

static void unlook_cell(GtkWidget *widget, gpointer data)
{
        (void) widget;
        (void) data;
        show_face(FACE_SMILE);
}

static void setup_mode(gint mode)
{
        gint x,y,m;

        if (mode == 3) {
                x = xsize;
                y = ysize;
                m = nmines;
        } else {
                x = size_table[mode][0];
                y = size_table[mode][1];
                m = size_table[mode][2];
        }
        gtk_minefield_set_size(GTK_MINEFIELD(mfield), (guint) x, (guint) y);
        gtk_minefield_set_mines(GTK_MINEFIELD(mfield), (guint) m, (guint) minesize);
}

static int range (int val, int min, int max)
{
        if (val < min)
                val = min;
        if (val > max)
                val = max;
        return val;
}

static void verify_ranges (void)
{
        minesize = range (minesize, 2, 100);
        xsize    = range (xsize, 4, 100);
        ysize    = range (ysize, 4, 100);
        /* At least one square without a mine, or there is nothing to do. */
        nmines   = range (nmines, 1, xsize * ysize - 1);
        fsize    = range (fsize, 0, 3);
}

static void about(void)
{
        static const gchar *const credits[] = {
                "Code: Pista",
                "Score: HoraPe",
                NULL
        };

        games_about (GTK_WINDOW (window), "cgminesweep",
                     _("Classic GUI Minesweep"),
                     "Copyright (C) 1997-1999 The Free Software Foundation\n"
                     "Copyright (C) 2026 The CGTK authors",
                     credits, NULL);
}

static void size_radio_callback(GtkWidget *widget, gpointer data)
{
        (void) widget;
        if (!pref_dialog)
                return;

        fsc = GPOINTER_TO_INT (data);

        gtk_widget_set_sensitive(cframe, fsc == 3);
}

static void apply_cb (GtkWidget *widget, gpointer data)
{
        gint oldxsize, oldysize, oldnmines, oldfsize;

        (void) widget;
        (void) data;

        oldxsize = xsize;
        oldysize = ysize;
        oldnmines = nmines;
        oldfsize = fsize;

        xsize  = atoi(gtk_entry_get_text(GTK_ENTRY(xentry)));
        ysize  = atoi(gtk_entry_get_text(GTK_ENTRY(yentry)));
        nmines = atoi(gtk_entry_get_text(GTK_ENTRY(mentry)));
        minesize = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(sentry));
        fsize  = fsc;

        verify_ranges ();
        setup_mode(fsize);

        /* A custom size's changes start a game only when it is in use. */
        if (oldfsize != fsize
            || (fsize == 3 && (oldxsize != xsize || oldysize != ysize
                               || oldnmines != nmines))) {
                new_game();
        }

        games_config_set_int(config, "xsize",  xsize);
        games_config_set_int(config, "ysize",  ysize);
        games_config_set_int(config, "nmines", nmines);
        games_config_set_int(config, "minesize", minesize);
        games_config_set_int(config, "mode",   fsize);
        games_config_save(config);

        gtk_widget_destroy(pref_dialog);
}

static GtkWidget *
number_entry (gint value)
{
        GtkWidget *entry = gtk_entry_new ();
        gchar numstr[16];

        gtk_widget_set_usize (entry, 50, -1);
        g_snprintf(numstr, sizeof numstr, "%d", value);
        gtk_entry_set_text(GTK_ENTRY(entry), numstr);
        return entry;
}

static void preferences_callback (void)
{
        GtkWidget *table;
        GtkWidget *frame;
        GtkWidget *vbox;
        GtkWidget *hbox;
        GtkWidget *button = NULL;
        GtkWidget *table2;
        GtkWidget *label2;
        GtkObject *adj;
        gint i;

        if (pref_dialog) {
                gdk_window_raise (pref_dialog->window);
                return;
        }

        fsc = fsize;

        pref_dialog = games_dialog_new (GTK_WINDOW (window), _("Minesweep Preferences"));
        gtk_signal_connect (GTK_OBJECT (pref_dialog), "destroy",
                        GTK_SIGNAL_FUNC (gtk_widget_destroyed), &pref_dialog);

        table = gtk_table_new (2, 2, FALSE);
        gtk_container_set_border_width (GTK_CONTAINER (table), 8);
        gtk_table_set_row_spacings (GTK_TABLE (table), 8);
        gtk_table_set_col_spacings (GTK_TABLE (table), 8);

        frame = gtk_frame_new (_("Field size"));

        vbox = gtk_vbox_new (TRUE, 0);
        gtk_container_set_border_width (GTK_CONTAINER (vbox), 8);

        for (i = 0; i < (gint) ELEMENTS (fsize2names); i++) {
                button = gtk_radio_button_new_with_label
                        (button ? gtk_radio_button_group (GTK_RADIO_BUTTON (button)) : NULL,
                         _(fsize2names[i].name));
                if (fsize == i)
                        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);
                gtk_signal_connect (GTK_OBJECT (button), "clicked",
                                GTK_SIGNAL_FUNC (size_radio_callback), GINT_TO_POINTER (i));
                gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, FALSE, 0);
        }

        gtk_container_add (GTK_CONTAINER (frame), vbox);

        gtk_table_attach (GTK_TABLE (table), frame, 0, 1, 0, 1, GTK_EXPAND |
                        GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

        cframe = gtk_frame_new (_("Custom size"));
        gtk_widget_set_sensitive (cframe, fsize == 3);

        table2 = gtk_table_new (3, 2, FALSE);
        gtk_container_set_border_width (GTK_CONTAINER (table2), 8);
        gtk_table_set_row_spacings (GTK_TABLE (table2), 8);
        gtk_table_set_col_spacings (GTK_TABLE (table2), 6);

        label2 = gtk_label_new (_("Horizontal:"));
        gtk_misc_set_alignment (GTK_MISC (label2), 0, 0.5);
        gtk_table_attach (GTK_TABLE (table2), label2, 0, 1, 0, 1,
                        GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

        xentry = number_entry (xsize);
        gtk_table_attach (GTK_TABLE (table2), xentry, 1, 2, 0, 1, 0, GTK_EXPAND
                        | GTK_FILL, 0, 0);

        label2 = gtk_label_new (_("Vertical:"));
        gtk_misc_set_alignment (GTK_MISC (label2), 0, 0.5);
        gtk_table_attach (GTK_TABLE (table2), label2, 0, 1, 1, 2,
                        GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

        yentry = number_entry (ysize);
        gtk_table_attach (GTK_TABLE (table2), yentry, 1, 2, 1, 2, 0, GTK_EXPAND
                        | GTK_FILL, 0, 0);

        label2 = gtk_label_new (_("Number of mines:"));
        gtk_misc_set_alignment (GTK_MISC (label2), 0, 0.5);
        gtk_table_attach (GTK_TABLE (table2), label2, 0, 1, 2, 3,
                        GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

        mentry = number_entry (nmines);
        gtk_table_attach (GTK_TABLE (table2), mentry, 1, 2, 2, 3, GTK_FILL, GTK_EXPAND
                        | GTK_FILL, 0, 0);

        gtk_container_add (GTK_CONTAINER (cframe), table2);

        gtk_table_attach (GTK_TABLE (table), cframe, 1, 2, 0, 1, GTK_EXPAND |
                        GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

        hbox = gtk_hbox_new (FALSE, 8);

        label2 = gtk_label_new(_("Mine size:"));
        gtk_box_pack_start (GTK_BOX (hbox), label2, FALSE, FALSE, 0);

        adj = gtk_adjustment_new(minesize, 2, 99, 1, 5, 10);
        sentry = gtk_spin_button_new(GTK_ADJUSTMENT(adj), 10, 0);
        gtk_spin_button_set_update_policy(GTK_SPIN_BUTTON(sentry), GTK_UPDATE_ALWAYS);
        gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(sentry), TRUE);
        gtk_box_pack_start (GTK_BOX (hbox), sentry, TRUE, TRUE, 0);

        gtk_table_attach (GTK_TABLE (table), hbox, 0, 1, 1, 2, GTK_EXPAND |
                        GTK_FILL, 0, 0, 0);

        gtk_box_pack_start (GTK_BOX (GTK_DIALOG (pref_dialog)->vbox), table,
                            TRUE, TRUE, 0);

        button = games_dialog_add_button (pref_dialog, _("OK"));
        gtk_signal_connect (GTK_OBJECT (button), "clicked",
                            GTK_SIGNAL_FUNC (apply_cb), NULL);
        button = games_dialog_add_button (pref_dialog, _("Cancel"));
        gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
                                   GTK_SIGNAL_FUNC (gtk_widget_destroy),
                                   GTK_OBJECT (pref_dialog));

        gtk_widget_show_all (pref_dialog);
}

/* The window. */

enum { NEW_GAME, SCORES, QUIT_GAME, PREFERENCES, MARKS, ABOUT };

static gboolean use_marks = TRUE;

static void
menu_callback (gpointer data, guint action, GtkWidget *widget)
{
        (void) data;
        switch (action) {
        case NEW_GAME:
                new_game ();
                break;
        case SCORES:
                top_ten ();
                break;
        case QUIT_GAME:
                quit_game ();
                break;
        case PREFERENCES:
                preferences_callback ();
                break;
        case MARKS:
                use_marks = GTK_CHECK_MENU_ITEM (widget)->active;
                gtk_minefield_set_use_marks (GTK_MINEFIELD (mfield), use_marks);
                games_config_set_int (config, "marks", use_marks);
                games_config_save (config);
                break;
        case ABOUT:
                about ();
                break;
        }
}

static GtkItemFactoryEntry menu_items[] = {
        { N_("/_Game"), NULL, NULL, 0, "<Branch>" },
        { N_("/Game/_New Game"), "<control>N",
          GTK_SIGNAL_FUNC (menu_callback), NEW_GAME, NULL },
        { "/Game/sep1", NULL, NULL, 0, "<Separator>" },
        { N_("/Game/S_cores..."), NULL,
          GTK_SIGNAL_FUNC (menu_callback), SCORES, NULL },
        { "/Game/sep2", NULL, NULL, 0, "<Separator>" },
        { N_("/Game/E_xit"), "<control>Q",
          GTK_SIGNAL_FUNC (menu_callback), QUIT_GAME, NULL },
        { N_("/_Settings"), NULL, NULL, 0, "<Branch>" },
        { N_("/Settings/_Preferences..."), NULL,
          GTK_SIGNAL_FUNC (menu_callback), PREFERENCES, NULL },
        { N_("/Settings/_Marks (?)"), NULL,
          GTK_SIGNAL_FUNC (menu_callback), MARKS, "<ToggleItem>" },
        { N_("/_Help"), NULL, NULL, 0, "<Branch>" },
        { N_("/Help/_About Minesweep..."), NULL,
          GTK_SIGNAL_FUNC (menu_callback), ABOUT, NULL },
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

int
main (int argc, char *argv[])
{
        GtkWidget *all_boxes;
        GtkWidget *box;
        GtkAccelGroup *accel_group;
        GtkItemFactory *factory;

        gtk_set_locale ();
        games_init_translation ();
        gtk_init (&argc, &argv);

        config = games_config_load (APPNAME);
        games_rand_seed (games_rand_new_seed ());

        xsize    = games_config_get_int (config, "xsize", 16);
        ysize    = games_config_get_int (config, "ysize", 16);
        nmines   = games_config_get_int (config, "nmines", 40);
        minesize = games_config_get_int (config, "minesize", 16);
        fsize    = games_config_get_int (config, "mode", 0);
        use_marks = games_config_get_int (config, "marks", 1) != 0;

        verify_ranges ();

        window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title (GTK_WINDOW (window), _(APPNAME_LONG));
        gtk_window_set_wmclass (GTK_WINDOW (window), APPNAME, "Cgminesweep");
        gtk_window_set_policy (GTK_WINDOW (window), FALSE, FALSE, TRUE);
        gtk_widget_realize (window);
        games_window_set_icon (window, "cgminesweep", cgminesweep_xpm);
        {
                gint i;

                for (i = 0; i < N_INKS; i++) {
                        ink[i] = gdk_gc_new (window->window);
                        gdk_rgb_gc_set_foreground (ink[i], ink_colors[i]);
                }
        }

        gtk_signal_connect (GTK_OBJECT (window), "delete_event",
                            GTK_SIGNAL_FUNC (delete_event_callback), NULL);
        gtk_signal_connect (GTK_OBJECT (window), "focus_out_event",
                            GTK_SIGNAL_FUNC (focus_out_cb), NULL);

        all_boxes = gtk_vbox_new (FALSE, 0);
        gtk_container_add (GTK_CONTAINER (window), all_boxes);

        accel_group = gtk_accel_group_new ();
        factory = gtk_item_factory_new (GTK_TYPE_MENU_BAR, "<main>", accel_group);
        gtk_item_factory_set_translate_func (factory, translate, NULL, NULL);
        gtk_item_factory_create_items (factory, ELEMENTS (menu_items),
                                       menu_items, NULL);
        gtk_window_add_accel_group (GTK_WINDOW (window), accel_group);
        gtk_box_pack_start (GTK_BOX (all_boxes),
                            gtk_item_factory_get_widget (factory, "<main>"),
                            FALSE, FALSE, 0);

        /* The counters and the face, sunk in a frame, and the field sunk
         * in another, both in a raised one. */
        board = gtk_frame_new (NULL);
        gtk_frame_set_shadow_type (GTK_FRAME (board), GTK_SHADOW_OUT);
        gtk_box_pack_start (GTK_BOX (all_boxes), board, TRUE, TRUE, 0);

        box = gtk_vbox_new (FALSE, 6);
        gtk_container_set_border_width (GTK_CONTAINER (box), 6);
        gtk_container_add (GTK_CONTAINER (board), box);

        panel = gtk_frame_new (NULL);
        gtk_frame_set_shadow_type (GTK_FRAME (panel), GTK_SHADOW_IN);
        gtk_box_pack_start (GTK_BOX (box), panel, FALSE, FALSE, 0);
        {
                GtkWidget *hbox = gtk_hbox_new (FALSE, 0);
                GtkWidget *align, *button;

                gtk_container_set_border_width (GTK_CONTAINER (hbox), 4);
                gtk_container_add (GTK_CONTAINER (panel), hbox);

                gtk_box_pack_start (GTK_BOX (hbox), counter_new (&mines_counter),
                                    FALSE, FALSE, 0);
                gtk_box_pack_end (GTK_BOX (hbox), counter_new (&time_counter),
                                  FALSE, FALSE, 0);

                button = gtk_button_new ();
                gtk_signal_connect (GTK_OBJECT (button), "clicked",
                                    GTK_SIGNAL_FUNC (face_clicked), NULL);
                face_area = gtk_drawing_area_new ();
                gtk_drawing_area_size (GTK_DRAWING_AREA (face_area), 20, 20);
                gtk_signal_connect (GTK_OBJECT (face_area), "expose_event",
                                    GTK_SIGNAL_FUNC (face_expose), NULL);
                gtk_container_add (GTK_CONTAINER (button), face_area);
                align = gtk_alignment_new (0.5, 0.5, 0.0, 0.0);
                gtk_container_add (GTK_CONTAINER (align), button);
                gtk_box_pack_start (GTK_BOX (hbox), align, TRUE, TRUE, 0);
        }

        /* The field, sunk in a frame its own size, in the middle when the
         * counters and the face are the wider. */
        {
                GtkWidget *frame = gtk_frame_new (NULL);
                GtkWidget *align = gtk_alignment_new (0.5, 0.0, 0.0, 0.0);

                gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
                gtk_container_add (GTK_CONTAINER (align), frame);
                gtk_box_pack_start (GTK_BOX (box), align, FALSE, FALSE, 0);
                field_box = gtk_vbox_new (FALSE, 0);
                gtk_container_add (GTK_CONTAINER (frame), field_box);
        }

        mfield = gtk_minefield_new ();
        gtk_minefield_set_use_marks (GTK_MINEFIELD (mfield), use_marks);
        gtk_box_pack_start (GTK_BOX (field_box), mfield, FALSE, FALSE, 0);
        gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (
                gtk_item_factory_get_widget (factory, "/Settings/Marks (?)")),
                use_marks);

        rbutton = gtk_button_new_with_label (_("Press to resume"));
        gtk_signal_connect (GTK_OBJECT (rbutton), "clicked",
                            GTK_SIGNAL_FUNC (resume_game_cb), NULL);
        gtk_box_pack_start (GTK_BOX (field_box), rbutton, TRUE, FALSE, 0);

        setup_mode (fsize);

        gtk_signal_connect (GTK_OBJECT (mfield), "marks_changed",
                            GTK_SIGNAL_FUNC (marks_changed), NULL);
        gtk_signal_connect (GTK_OBJECT (mfield), "explode",
                            GTK_SIGNAL_FUNC (lose_game), NULL);
        gtk_signal_connect (GTK_OBJECT (mfield), "win",
                            GTK_SIGNAL_FUNC (win_game), NULL);
        gtk_signal_connect (GTK_OBJECT (mfield), "look",
                            GTK_SIGNAL_FUNC (look_cell), NULL);
        gtk_signal_connect (GTK_OBJECT (mfield), "unlook",
                            GTK_SIGNAL_FUNC (unlook_cell), NULL);

        /* The clock counts for the panel, never shown itself. */
        clk = games_clock_new ();
        gtk_widget_ref (clk);
        gtk_object_sink (GTK_OBJECT (clk));
        gtk_timeout_add (200, counters_tick, NULL);

        gtk_widget_show_all (board);
        gtk_widget_hide (rbutton);
        gtk_widget_show_all (gtk_item_factory_get_widget (factory, "<main>"));
        gtk_widget_show (all_boxes);

        new_game ();

        gtk_widget_show (window);

        gtk_main ();

        games_config_free (config);
        return 0;
}
