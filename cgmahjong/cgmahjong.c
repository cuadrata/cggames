/*
 * cgmahjong, from GNOME Mahjongg
 * (C) 1998-1999 the Free Software Foundation
 * Copyright (C) 2026 The CGTK authors
 *
 * Author: Francisco Bustamante
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

#include "games.h"
#include "cgmahjong.h"
#include "maps.h"
#include "solubility.h"
#include "tileset.h"

#include "icons/cgmahjong.xpm"

#define APPNAME "cgmahjong"
#define APPNAME_LONG N_("Mahjong")

/* The board's size at first, and the least it can be, in the toolkit's
 * pixels; the tiles are drawn to fit it, with MARGIN round them. */
#define AREA_WIDTH 740
#define AREA_HEIGHT 580
#define MIN_WIDTH 320
#define MIN_HEIGHT 240
#define MARGIN 6

/* How long the window must keep a size before the tiles are drawn again
 * for it, in milliseconds; till then the old ones are shown. */
#define REDRAW_DELAY 150

/* #defines for the tile selection code. */
#define SELECTED_FLAG   1
#define HINT_FLAG       16

#define ELEMENTS(x) (sizeof (x) / sizeof (x [0]))

/* The number of half-cycles to blink during a hint (less 1) */
#define HINT_BLINK_NUM 5

tilepos *pos = 0;
static int xpos_offset;
static int ypos_offset;

tile tiles[MAX_TILES];

/* A score category for each map. */
static GamesScoreCategory *score_categories;

static GtkWidget *window, *board, *statusbar, *chrono;
static GtkWidget *toolbar_box, *tiles_label, *moves_label, *pause_button;
static GtkItemFactory *factory;
static GamesConfig *config;

static int selected_tile, visible_tiles;
static int sequence_number;
/* Undo stops here: a shuffle forgets the moves before it. */
static int undo_floor = 1;

/* The tiles, drawn for the window's scale. */
static Tileset *tileset;

/* The map chosen in the preferences, and the one being played. */
static gchar *mapset, *game_map;
static guint32 game_seed;

static guchar backgnd[3];
static gchar *backgnd_name;

static struct {
        gboolean confirm;
} popup_config = { TRUE };

/* The picture of the board, the size of the drawing area in device
 * pixels, scale to each of the toolkit's. The tiles are placed in
 * device pixels, xpos_offset and ypos_offset from the frame's corner. */
static guchar *frame;
static gint frame_width, frame_height;
static gint scale = 1;
static guint relayout_timer;
#define DEVICE_WIDTH (frame_width * scale)
#define DEVICE_HEIGHT (frame_height * scale)

static gint hint_tiles[2];
static guint timer;
static guint timeout_counter = HINT_BLINK_NUM + 1;

static int moves_left = 0;
static gboolean paused = FALSE;
/* Set while a toggle is being brought into line, so that its handler
 * leaves it alone. */
static gboolean syncing = FALSE;

/* What the menus and the toolbar do; the first three ask first. */
typedef enum {
        NEW_GAME, RESTART_GAME, SELECT_GAME, QUIT_GAME,
        UNDO_MOVE, REDO_MOVE, HINT, SCORES, PREFERENCES, ABOUT
} game_action;

static enum { GAME_RUNNING, GAME_WON, GAME_LOST, GAME_DEAD } game_over;

static void new_game (guint32 seed);
static void restart_game (void);
static void select_game (void);
static void you_won (void);
static void check_free (void);
static void set_paused (gboolean pause);

/* Drawing. */

/* Where a tile's corner goes, in device pixels, for tiles of the
 * sizes given; positions are in half tiles. */
static int place_x (int i, int half_width, int thickness)
{
        return pos[i].x * half_width + thickness * pos[i].layer;
}

static int place_y (int i, int half_height, int thickness)
{
        return pos[i].y * half_height - thickness * pos[i].layer;
}

static int canvas_x (int i)
{
        return place_x (i, tileset_half_width (tileset),
                        tileset_thickness (tileset)) + xpos_offset;
}

static int canvas_y (int i)
{
        return place_y (i, tileset_half_height (tileset),
                        tileset_thickness (tileset)) + ypos_offset;
}

/* Lays a tile's image over the frame with its corner at (x, y), in
 * device pixels. */
static void
blit (const guchar *src, gint x, gint y)
{
        gint tw = tileset_width (tileset), w = tw, h = tileset_height (tileset);
        gint sx = 0, sy = 0, i, j;

        if (x < 0) { sx = -x; w += x; x = 0; }
        if (y < 0) { sy = -y; h += y; y = 0; }
        if (x + w > DEVICE_WIDTH)
                w = DEVICE_WIDTH - x;
        if (y + h > DEVICE_HEIGHT)
                h = DEVICE_HEIGHT - y;

        for (j = 0; j < h; j++) {
                const guchar *s = src + ((gsize) (sy + j) * tw + sx) * 4;
                guchar *d = frame + ((gsize) (y + j) * DEVICE_WIDTH + x) * 3;

                for (i = 0; i < w; i++, s += 4, d += 3) {
                        guint a = s[3];

                        if (a == 255) {
                                d[0] = s[0];
                                d[1] = s[1];
                                d[2] = s[2];
                        } else if (a) {
                                d[0] = (guchar) ((s[0] * a + d[0] * (255 - a) + 127) / 255);
                                d[1] = (guchar) ((s[1] * a + d[1] * (255 - a) + 127) / 255);
                                d[2] = (guchar) ((s[2] * a + d[2] * (255 - a) + 127) / 255);
                        }
                }
        }
}

static void
draw_tile (int i)
{
        /* While paused the faces are hidden. */
        blit (tileset_tile (tileset, paused ? -1 : tiles[i].image,
                            tiles[i].selected != 0),
              canvas_x (i), canvas_y (i));
}

/* Draws the board again and has it shown. */
static void
redraw (void)
{
        gsize n;
        int i;

        if (!frame || !pos)
                return;

        for (n = 0; n < (gsize) DEVICE_WIDTH * DEVICE_HEIGHT; n++)
                memcpy (frame + n * 3, backgnd, 3);

        /* Bottom left, top layer, comes first, and is drawn last. */
        for (i = MAX_TILES - 1; i >= 0; i--)
                if (tiles[i].visible)
                        draw_tile (i);

        gtk_widget_queue_draw (board);
}

/* The topmost tile showing at (x, y) in the frame, in device pixels,
 * going by the shape of the tiles; -1 if none. */
static int
tile_at_point (int x, int y)
{
        int i;

        for (i = 0; i < MAX_TILES; i++)
                if (tiles[i].visible
                    && tileset_hit (tileset, x - canvas_x (i), y - canvas_y (i)))
                        return i;
        return -1;
}

/* The bounds of the tiles laid out at a size, from the corner of the
 * first position. */
static void
extent (gint half_width, gint *x0, gint *y0, gint *x1, gint *y1)
{
        gint hh, th, i;

        tileset_proportions (half_width, &hh, &th);
        *x0 = *y0 = G_MAXINT;
        *x1 = *y1 = G_MININT;
        for (i = 0; i < MAX_TILES; i++) {
                gint x = place_x (i, half_width, th), y = place_y (i, hh, th);

                *x0 = MIN (*x0, x);
                *y0 = MIN (*y0, y);
                *x1 = MAX (*x1, x + 2 * half_width + th);
                *y1 = MAX (*y1, y + 2 * hh + th);
        }
}

/* The largest tiles that fit the frame, by their half width. */
static gint
fitting_half_width (void)
{
        gint room_w = DEVICE_WIDTH - 2 * MARGIN * scale;
        gint room_h = DEVICE_HEIGHT - 2 * MARGIN * scale;
        gint lo = 2, hi = MAX (room_w, 2);

        /* The extent grows with the size, so the range can be halved. */
        while (lo < hi) {
                gint mid = (lo + hi + 1) / 2, x0, y0, x1, y1;

                extent (mid, &x0, &y0, &x1, &y1);
                if (x1 - x0 <= room_w && y1 - y0 <= room_h)
                        lo = mid;
                else
                        hi = mid - 1;
        }
        return lo;
}

/* Centres the tiles, at the size they are drawn, in the frame. */
static void
centre (void)
{
        gint x0, y0, x1, y1;

        extent (tileset_half_width (tileset), &x0, &y0, &x1, &y1);
        xpos_offset = (DEVICE_WIDTH - (x1 - x0)) / 2 - x0;
        ypos_offset = (DEVICE_HEIGHT - (y1 - y0)) / 2 - y0;
}

/* Draws the tiles again at the size that fits, if it has changed. */
static void
fit_tiles (void)
{
        gint want = fitting_half_width ();

        if (want != tileset_half_width (tileset)) {
                Tileset *t = tileset_new (want);

                /* If the faces can't be read now, the old tiles do. */
                if (t) {
                        tileset_free (tileset);
                        tileset = t;
                }
        }
        centre ();
}

static gint
relayout_timeout (gpointer data)
{
        (void) data;
        relayout_timer = 0;
        fit_tiles ();
        redraw ();
        return FALSE;
}

/* Fits the tiles to the frame: at once, or once the window has kept its
 * size a moment, showing the tiles as they are till then. */
static void
relayout (gboolean at_once)
{
        if (!frame || !pos)
                return;
        if (relayout_timer)
                gtk_timeout_remove (relayout_timer);
        relayout_timer = 0;
        if (at_once)
                fit_tiles ();
        else {
                centre ();
                relayout_timer = gtk_timeout_add (REDRAW_DELAY, relayout_timeout,
                                                  NULL);
        }
        redraw ();
}

/* Brings the frame into line with the drawing area's size and the
 * window's scale, and the tiles with the frame. */
static void
frame_resize (gint width, gint height)
{
        gint s = MAX (gdk_window_get_scale_factor (board->window), 1);
        gboolean at_once = frame == NULL || s != scale;

        if (frame && width == frame_width && height == frame_height
            && s == scale)
                return;
        frame_width = width;
        frame_height = height;
        scale = s;
        g_free (frame);
        frame = g_malloc ((gsize) DEVICE_WIDTH * DEVICE_HEIGHT * 3);
        relayout (at_once);
}

static gint
board_configure (GtkWidget *widget, GdkEventConfigure *event, gpointer data)
{
        (void) widget;
        (void) data;

        frame_resize (event->width, event->height);
        return TRUE;
}

static gint
board_expose (GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
        gint x0, y0, x1, y1;

        (void) data;
        if (!frame)
                return TRUE;
        /* The window may have gone to a screen of another scale. */
        frame_resize (frame_width, frame_height);

        /* GdkRectangle's sizes are unsigned: the area is clipped in ints. */
        x0 = MAX (event->area.x, 0);
        y0 = MAX (event->area.y, 0);
        x1 = MIN (event->area.x + (gint) event->area.width, frame_width);
        y1 = MIN (event->area.y + (gint) event->area.height, frame_height);
        if (x1 <= x0 || y1 <= y0)
                return TRUE;

        gdk_draw_rgb_image_scaled (widget->window, widget->style->black_gc,
                                   x0, y0, x1 - x0, y1 - y0, scale,
                                   GDK_RGB_DITHER_NONE,
                                   frame + ((gsize) y0 * scale * DEVICE_WIDTH
                                            + x0 * scale) * 3,
                                   DEVICE_WIDTH * 3);
        return TRUE;
}

/* Labels. */

static void
set_label_number (GtkWidget *label, int number)
{
        gchar text[16];

        g_snprintf (text, sizeof text, "%d", number);
        gtk_label_set_text (GTK_LABEL (label), text);
}

static void
message (const gchar *text)
{
        games_flash (GTK_STATUSBAR (statusbar), text);
}

static void
chrono_start (void)
{
        games_clock_stop (chrono);
        games_clock_set (chrono, 0);
        games_clock_start (chrono);
}

static void
chrono_add (int seconds)
{
        games_clock_set (chrono, games_clock_get (chrono) + seconds);
}

/* Counts the pairs that could be taken now. */
static int
count_moves (void)
{
        int i;
        int tile_count[MAX_TILES];

        moves_left = 0;

        for (i = 0; i < MAX_TILES; i++)
                tile_count[i] = 0;

        for (i = 0; i < MAX_TILES; i++) {
                if (tile_free (i))
                        tile_count[tiles[i].type]++;
        }

        for (i = 0; i < MAX_TILES; i++)
                moves_left += tile_count[i] >> 1;

        set_label_number (moves_label, moves_left);
        return moves_left;
}

static void
update_labels (void)
{
        set_label_number (tiles_label, visible_tiles);
        count_moves ();
}

/* Selection and hints. */

static void
hint_stop (void)
{
        if (timeout_counter <= HINT_BLINK_NUM) {
                gtk_timeout_remove (timer);
                timeout_counter = HINT_BLINK_NUM + 1;
                tiles[hint_tiles[0]].selected &= ~HINT_FLAG;
                tiles[hint_tiles[1]].selected &= ~HINT_FLAG;
        }
}

static void
select_tile (tile *tile_inf)
{
        tile_inf->selected |= SELECTED_FLAG;
        selected_tile = tile_inf->number;
}

static void
unselect_tile (tile *tile_inf)
{
        selected_tile = MAX_TILES + 1;
        tile_inf->selected &= ~SELECTED_FLAG;
}

static void
clear_selection (void)
{
        if (selected_tile < MAX_TILES) {
                tiles[selected_tile].selected = 0;
                selected_tile = MAX_TILES + 1;
        }
}

/* You lose your redo queue when you make a move */
static void
clear_undo_queue (void)
{
        int lp;

        for (lp = 0; lp < MAX_TILES; lp++)
                if (tiles[lp].sequence >= sequence_number)
                        tiles[lp].sequence = 0;
}

static gint
board_button_press (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
        int number;
        tile *tile_inf;

        (void) widget;
        (void) data;

        if (paused || event->type != GDK_BUTTON_PRESS)
                return TRUE;
        number = tile_at_point ((int) (event->x * scale),
                               (int) (event->y * scale));
        if (number < 0 || !tile_free (number))
                return TRUE;
        tile_inf = &tiles[number];

        /* A click ends a hint's blinking. */
        hint_stop ();

        switch (event->button) {
        case 1:
                if (tile_inf->selected & SELECTED_FLAG)
                        unselect_tile (tile_inf);
                else if (selected_tile < MAX_TILES) {
                        if (tiles[selected_tile].type == tile_inf->type) {
                                tiles[selected_tile].visible = 0;
                                tile_inf->visible = 0;
                                tiles[selected_tile].selected &= ~SELECTED_FLAG;
                                clear_undo_queue ();
                                tiles[selected_tile].sequence = tile_inf->sequence = sequence_number;
                                sequence_number++;
                                selected_tile = MAX_TILES + 1;
                                visible_tiles -= 2;
                                update_labels ();
                                redraw ();

                                if (visible_tiles <= 0) {
                                        games_clock_stop (chrono);
                                        you_won ();
                                } else
                                        check_free ();
                                return TRUE;
                        }
                        /* Not a match: the player is starting another
                         * pair. */
                        unselect_tile (&tiles[selected_tile]);
                        select_tile (tile_inf);
                } else
                        select_tile (tile_inf);
                break;

        case 3:
                if (selected_tile < MAX_TILES)
                        unselect_tile (&tiles[selected_tile]);
                select_tile (tile_inf);
                break;

        default:
                return TRUE;
        }
        redraw ();
        return TRUE;
}

static void shuffle_tiles (void);
static void undo_tile (void);

enum { STUCK_SHUFFLE, STUCK_NEW_GAME, STUCK_UNDO };

static void
stuck_answer (gint answer, gpointer data)
{
        gint *choices = data;

        /* Closing the dialog takes the move back, as GNOME's did. */
        if (answer < 0) {
                undo_tile ();
        } else {
                switch (choices[answer]) {
                case STUCK_SHUFFLE:
                        shuffle_tiles ();
                        break;
                case STUCK_NEW_GAME:
                        new_game (games_rand_new_seed ());
                        break;
                case STUCK_UNDO:
                        undo_tile ();
                        break;
                }
        }
        g_free (choices);
}

/* When no pair can be taken, offers once to shuffle the tiles, start
 * again, or take the last move back. */
static void
check_free (void)
{
        const gchar *labels[4];
        gint *choices, n = 0;

        if (count_moves () != 0 || visible_tiles <= 0
            || game_over != GAME_RUNNING)
                return;
        game_over = GAME_LOST;

        choices = g_new (gint, 3);
        labels[n] = _("Shuffle");
        choices[n++] = STUCK_SHUFFLE;
        labels[n] = _("New Game");
        choices[n++] = STUCK_NEW_GAME;
        if (sequence_number > undo_floor) {
                labels[n] = _("Undo");
                choices[n++] = STUCK_UNDO;
        }
        labels[n] = NULL;
        games_choose (GTK_WINDOW (window), _(APPNAME_LONG),
                      _("There are no more moves."), labels,
                      stuck_answer, choices);
}

static void
new_game_reply_callback (gboolean yes, gpointer data)
{
        (void) data;
        if (yes)
                new_game (games_rand_new_seed ());
}

static void
you_won (void)
{
        gint place, seconds;
        gchar *text, *time;

        game_over = GAME_WON;

        seconds = games_clock_get (chrono);
        time = games_format_time (seconds);
        place = games_scores_add (APPNAME, GAMES_SCORES_TIME, game_map, seconds);
        if (place) {
                games_scores_show (GTK_WINDOW (window), _("Mahjong Scores"),
                                   APPNAME, GAMES_SCORES_TIME,
                                   score_categories, n_maps, _("Map:"),
                                   game_map, place);
                text = g_strdup_printf (_("Fantastic! %s!\n"
                                          "You have reached #%d in the Top Ten.\n\n"
                                          "Another game?"), time, place);
        } else
                text = g_strdup_printf (_("Great!\nYou made it in %s.\n\n"
                                          "Another game?"), time);
        games_question (GTK_WINDOW (window), _(APPNAME_LONG), text,
                        _("Yes"), _("No"), new_game_reply_callback, NULL);
        g_free (text);
        g_free (time);
}

static gint
hint_timeout (gpointer data)
{
        (void) data;
        timeout_counter++;

        if (timeout_counter > HINT_BLINK_NUM)
                return FALSE;

        tiles[hint_tiles[0]].selected ^= HINT_FLAG;
        tiles[hint_tiles[1]].selected ^= HINT_FLAG;
        redraw ();

        return TRUE;
}

static void
hint (void)
{
        int i, j, found = 0;

        if (paused || game_over)
                return;

        /* This prevents the flashing speeding up if the hint button is
         * pressed multiple times. */
        if (timeout_counter <= HINT_BLINK_NUM)
                return;

        /* The selected tile's match, if it has one free. */
        if (selected_tile < MAX_TILES)
                for (j = 0; j < MAX_TILES && !found; j++)
                        if (tiles[j].type == tiles[selected_tile].type
                            && j != selected_tile && tile_free (j)) {
                                hint_tiles[0] = selected_tile;
                                hint_tiles[1] = j;
                                found = 1;
                        }

        for (i = 0; i < MAX_TILES && !found; i++)
                if (tile_free (i))
                        for (j = 0; j < MAX_TILES && !found; j++)
                                if (tiles[j].type == tiles[i].type && i != j
                                    && tile_free (j)) {
                                        hint_tiles[0] = i;
                                        hint_tiles[1] = j;
                                        found = 1;
                                }
        if (!found)
                return;

        /* The selection stays if it is half of the hint. */
        if (hint_tiles[0] != selected_tile)
                clear_selection ();

        tiles[hint_tiles[0]].selected ^= HINT_FLAG;
        tiles[hint_tiles[1]].selected ^= HINT_FLAG;
        redraw ();

        timeout_counter = 0;
        timer = gtk_timeout_add (250, hint_timeout, NULL);

        /* 30s penalty */
        chrono_add (30);
}

static void
about (void)
{
        static const gchar *const credits[] = {
                "Code: Francisco Bustamante, Max Watson, Heinz Hempe,",
                "Michael Meeks, Philippe Chavin",
                "Maps: Rexford Newbould, Krzysztof Foltman",
                NULL
        };

        games_about (GTK_WINDOW (window), "cgmahjong",
                     _("Classic GUI Mahjong"),
                     "Copyright (C) 1998 The Free Software Foundation\n"
                     "Copyright (C) 2026 The CGTK authors",
                     credits, NULL);
}

static void
quit_game (void)
{
        /* The board's size is kept for next time. */
        if (frame) {
                games_config_set_int (config, "width", frame_width);
                games_config_set_int (config, "height", frame_height);
                games_config_save (config);
        }
        gtk_main_quit ();
}

/* Pausing hides the faces and stops the clock. */
static void
set_paused (gboolean pause)
{
        GtkWidget *item = gtk_item_factory_get_widget (factory, "/Game/Pause");

        if (game_over)
                pause = FALSE;
        if (pause != paused) {
                paused = pause;
                if (paused) {
                        hint_stop ();
                        games_clock_stop (chrono);
                        gtk_statusbar_push (GTK_STATUSBAR (statusbar),
                                            gtk_statusbar_get_context_id (GTK_STATUSBAR (statusbar), "pause"),
                                            _("... Game paused ..."));
                } else {
                        gtk_statusbar_pop (GTK_STATUSBAR (statusbar),
                                           gtk_statusbar_get_context_id (GTK_STATUSBAR (statusbar), "pause"));
                        games_clock_start (chrono);
                }
                redraw ();
        }

        syncing = TRUE;
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pause_button), paused);
        gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), paused);
        syncing = FALSE;
}

static void
pause_button_callback (GtkWidget *widget, gpointer data)
{
        (void) data;
        if (!syncing)
                set_paused (GTK_TOGGLE_BUTTON (widget)->active);
}

static void
pause_item_callback (gpointer data, guint action, GtkWidget *widget)
{
        (void) data;
        (void) action;
        if (!syncing)
                set_paused (GTK_CHECK_MENU_ITEM (widget)->active);
}

static void
show_scores (void)
{
        games_scores_show (GTK_WINDOW (window), _("Mahjong Scores"),
                           APPNAME, GAMES_SCORES_TIME,
                           score_categories, n_maps, _("Map:"),
                           game_map, 0);
}

static void
init_game (void)
{
        game_over = GAME_RUNNING;
        sequence_number = 1;
        undo_floor = 1;
        visible_tiles = MAX_TILES;
        selected_tile = MAX_TILES + 1;
        update_labels ();
        redraw ();

        chrono_start ();
}

static void
confirm_callback (gboolean yes, gpointer data)
{
        if (!yes)
                return;
        switch ((game_action) GPOINTER_TO_INT (data)) {
        case NEW_GAME:
                new_game (games_rand_new_seed ());
                break;
        case RESTART_GAME:
                restart_game ();
                break;
        case SELECT_GAME:
                select_game ();
                break;
        default:
                break;
        }
}

/* Asks first, if the player has a game going and wants to be asked. */
static void
confirm_action (game_action action)
{
        const char *confirm_text;

        if (popup_config.confirm
            && game_over != GAME_WON
            && game_over != GAME_DEAD
            && sequence_number > 1) {
                switch (action) {
                case RESTART_GAME:
                        confirm_text = _("Really restart this game?");
                        break;
                default:
                        confirm_text = _("Really start a new game?");
                        break;
                }
                games_question (GTK_WINDOW (window), _(APPNAME_LONG),
                                confirm_text, _("OK"), _("Cancel"),
                                confirm_callback, GINT_TO_POINTER (action));
        } else
                confirm_callback (TRUE, GINT_TO_POINTER (action));
}


static void
restart_game (void)
{
        int i;

        set_paused (FALSE);
        hint_stop ();
        for (i = 0; i < MAX_TILES; i++) {
                tiles[i].visible = 1;
                tiles[i].selected = 0;
                tiles[i].sequence = 0;
        }
        init_game ();
}

static void
redo_tile (void)
{
        int i, change;

        if (paused || game_over == GAME_WON || game_over == GAME_DEAD)
                return;
        if (sequence_number > (MAX_TILES / 2))
                return;

        hint_stop ();
        clear_selection ();
        change = 0;
        for (i = 0; i < MAX_TILES; i++)
                if (tiles[i].sequence == sequence_number) {
                        tiles[i].selected = 0;
                        tiles[i].visible = 0;
                        visible_tiles--;
                        change = 1;
                }
        if (change) {
                if (sequence_number < MAX_TILES)
                        sequence_number++;
        } else
                message (_("No more redo!"));

        update_labels ();
        redraw ();
        if (change)
                check_free ();
}

static void
undo_tile (void)
{
        int i;

        if (paused || game_over == GAME_WON || game_over == GAME_DEAD)
                return;
        if (sequence_number <= undo_floor)
                return;
        sequence_number--;
        if (game_over == GAME_LOST)
                game_over = GAME_RUNNING;

        hint_stop ();
        clear_selection ();

        for (i = 0; i < MAX_TILES; i++)
                if (tiles[i].sequence == sequence_number) {
                        tiles[i].selected = 0;
                        tiles[i].visible = 1;
                        visible_tiles++;
                }

        update_labels ();
        redraw ();
}

/* The game number dialog. */

static void
seed_ok (GtkWidget *widget, gpointer data)
{
        GtkWidget *dialog = data;
        GtkWidget *entry = gtk_object_get_user_data (GTK_OBJECT (dialog));
        const gchar *text = gtk_entry_get_text (GTK_ENTRY (entry));
        char *end;
        unsigned long seed;

        (void) widget;
        seed = strtoul (text, &end, 10);
        while (*end == ' ')
                end++;
        if (end == text || *end != '\0' || seed > 0x7fffffffUL) {
                gdk_beep ();
                return;
        }
        gtk_widget_destroy (dialog);
        new_game ((guint32) seed);
}

static void
select_game (void)
{
        GtkWidget *dialog, *entry, *label, *hbox, *button;
        gchar text[16];

        set_paused (FALSE);
        dialog = games_dialog_new (GTK_WINDOW (window), _("Select Game"));
        gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
        gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_MOUSE);

        hbox = gtk_hbox_new (FALSE, 6);
        gtk_container_set_border_width (GTK_CONTAINER (hbox), 12);
        label = gtk_label_new (_("Game Number:"));
        gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);

        entry = gtk_entry_new ();
        g_snprintf (text, sizeof text, "%lu", (unsigned long) game_seed);
        gtk_entry_set_text (GTK_ENTRY (entry), text);
        gtk_editable_select_region (GTK_EDITABLE (entry), 0, -1);
        gtk_box_pack_start (GTK_BOX (hbox), entry, TRUE, TRUE, 0);
        gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), hbox,
                            TRUE, TRUE, 0);
        gtk_object_set_user_data (GTK_OBJECT (dialog), entry);
        gtk_signal_connect (GTK_OBJECT (entry), "activate",
                            GTK_SIGNAL_FUNC (seed_ok), dialog);

        button = games_dialog_add_button (dialog, _("OK"));
        gtk_signal_connect (GTK_OBJECT (button), "clicked",
                            GTK_SIGNAL_FUNC (seed_ok), dialog);
        button = games_dialog_add_button (dialog, _("Cancel"));
        gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
                                   GTK_SIGNAL_FUNC (gtk_widget_destroy),
                                   GTK_OBJECT (dialog));

        gtk_widget_show_all (dialog);
        gtk_widget_grab_focus (entry);
}

static void
show_tb_callback (gpointer data, guint action, GtkWidget *widget)
{
        gboolean show = GTK_CHECK_MENU_ITEM (widget)->active;

        (void) data;
        (void) action;
        if (show)
                gtk_widget_show (toolbar_box);
        else
                gtk_widget_hide (toolbar_box);
        if (!syncing) {
                games_config_set_int (config, "toolbar", show);
                games_config_save (config);
        }
}

/* Maps and images. */

static void
set_map (const char *name)
{
        const Map *m = maps_find (name);

        /* Only a known map; the first otherwise. */
        g_free (mapset);
        mapset = g_strdup (m ? m->key : maps[0].key);
}

static void
load_map (void)
{
        Map *m = maps_find (mapset);
        gchar *title;

        pos = m->pos;
        /* Another map may want other sizes of tiles. */
        relayout (TRUE);

        generate_dependancies ();
        g_free (game_map);
        game_map = g_strdup (m->key);

        /* The window says which map is being played. */
        title = g_strdup_printf ("%s - %s", _(APPNAME_LONG), m->name);
        gtk_window_set_title (GTK_WINDOW (window), title);
        g_free (title);
}

static void
set_backgnd_colour (const char *str)
{
        GdkColor colour;

        if (!gdk_color_parse (str, &colour))
                gdk_color_parse ("#003010", &colour);
        backgnd[0] = colour.red >> 8;
        backgnd[1] = colour.green >> 8;
        backgnd[2] = colour.blue >> 8;
        g_free (backgnd_name);
        backgnd_name = g_strdup_printf ("#%02x%02x%02x",
                                        backgnd[0], backgnd[1], backgnd[2]);
        redraw ();
}

static void
new_game (guint32 seed)
{
        int i;

        set_paused (FALSE);
        hint_stop ();
        game_seed = seed;
        games_rand_seed (seed);
        load_map ();
        generate_game ();

        for (i = 0; i < MAX_TILES; i++)
                tiles[i].number = i;

        init_game ();
}

static void
shuffle_tiles (void)
{
        int i, previous = 0, first = 1, num_shuffle = 0;
        tile temp = { 0, 0, 0, 0, 0, 0 };

        if (paused || game_over == GAME_DEAD || game_over == GAME_WON)
                return;

        hint_stop ();
        clear_selection ();

        do {
                num_shuffle++;

                /* We do a circular permutation */
                for (i = 0; i < MAX_TILES; i++) {
                        if (tiles[i].visible) {
                                if (first) {
                                        temp = tiles[i];
                                        first--;
                                } else {
                                        tiles[previous].type = tiles[i].type;
                                        tiles[previous].image = tiles[i].image;
                                }
                                previous = i;
                        }
                }
                tiles[previous].type = temp.type;
                tiles[previous].image = temp.image;
                first = 1;
        } while (!count_moves () && num_shuffle < visible_tiles);

        if (num_shuffle >= visible_tiles && !moves_left) {
                game_over = GAME_DEAD;
                games_clock_stop (chrono);
                redraw ();
                games_message (GTK_WINDOW (window), _(APPNAME_LONG),
                               _("Sorry, I can't find\na playable configuration."));
        } else {
                for (i = 0; i < MAX_TILES; i++)
                        tiles[i].sequence = 0;
                undo_floor = sequence_number;

                game_over = GAME_RUNNING;
                redraw ();

                /* 60s penalty */
                chrono_add (60);
        }
}

/* Preferences. */

static struct {
        GtkWidget *dialog;
        gchar *mapset;
        GdkColor colour;
        GtkWidget *swatch;
        GtkWidget *confirm;
} prefs;

static void
choose_string (GtkWidget *item, gpointer data)
{
        gchar **choice = data;

        g_free (*choice);
        *choice = g_strdup (gtk_object_get_user_data (GTK_OBJECT (item)));
}

static void
free_item_name (GtkWidget *item, gpointer data)
{
        (void) data;
        g_free (gtk_object_get_user_data (GTK_OBJECT (item)));
}

/* An option menu of the maps, setting prefs.mapset. */
static GtkWidget *
map_menu (void)
{
        GtkWidget *option = gtk_option_menu_new ();
        GtkWidget *menu = gtk_menu_new ();
        gint lp, active = 0;

        for (lp = 0; lp < n_maps; lp++) {
                GtkWidget *item = gtk_menu_item_new_with_label (maps[lp].name);

                gtk_object_set_user_data (GTK_OBJECT (item), g_strdup (maps[lp].key));
                gtk_signal_connect (GTK_OBJECT (item), "activate",
                                    GTK_SIGNAL_FUNC (choose_string), &prefs.mapset);
                gtk_signal_connect (GTK_OBJECT (item), "destroy",
                                    GTK_SIGNAL_FUNC (free_item_name), NULL);
                gtk_widget_show (item);
                gtk_menu_append (GTK_MENU (menu), item);
                if (strcmp (maps[lp].key, mapset) == 0)
                        active = lp;
        }
        gtk_option_menu_set_menu (GTK_OPTION_MENU (option), menu);
        gtk_option_menu_set_history (GTK_OPTION_MENU (option), active);
        return option;
}

static gint
swatch_expose (GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
        GdkGC *gc = gdk_gc_new (widget->window);

        (void) event;
        (void) data;
        gdk_rgb_gc_set_foreground (gc, ((guint32) (prefs.colour.red >> 8) << 16)
                                   | ((guint32) (prefs.colour.green >> 8) << 8)
                                   | (prefs.colour.blue >> 8));
        gdk_draw_rectangle (widget->window, gc, TRUE, 0, 0,
                            widget->allocation.width, widget->allocation.height);
        gdk_draw_rectangle (widget->window, widget->style->black_gc, FALSE, 0, 0,
                            widget->allocation.width - 1,
                            widget->allocation.height - 1);
        gdk_gc_unref (gc);
        return TRUE;
}

static void
colour_ok (GtkWidget *widget, gpointer data)
{
        GtkColorSelectionDialog *csd = data;
        gdouble c[4];

        (void) widget;
        gtk_color_selection_get_color (GTK_COLOR_SELECTION (csd->colorsel), c);
        prefs.colour.red = (gushort) (c[0] * 65535.0 + 0.5);
        prefs.colour.green = (gushort) (c[1] * 65535.0 + 0.5);
        prefs.colour.blue = (gushort) (c[2] * 65535.0 + 0.5);
        if (prefs.swatch)
                gtk_widget_queue_draw (prefs.swatch);
        gtk_widget_destroy (GTK_WIDGET (csd));
}

static void
colour_clicked (GtkWidget *widget, gpointer data)
{
        GtkWidget *csd = gtk_color_selection_dialog_new (_("Background Colour"));
        GtkColorSelectionDialog *d = GTK_COLOR_SELECTION_DIALOG (csd);
        gdouble c[4];

        (void) widget;
        (void) data;
        c[0] = prefs.colour.red / 65535.0;
        c[1] = prefs.colour.green / 65535.0;
        c[2] = prefs.colour.blue / 65535.0;
        c[3] = 1.0;
        gtk_color_selection_set_color (GTK_COLOR_SELECTION (d->colorsel), c);
        gtk_window_set_transient_for (GTK_WINDOW (csd), GTK_WINDOW (prefs.dialog));
        gtk_window_set_modal (GTK_WINDOW (csd), TRUE);
        gtk_widget_hide (d->help_button);
        gtk_signal_connect (GTK_OBJECT (d->ok_button), "clicked",
                            GTK_SIGNAL_FUNC (colour_ok), csd);
        gtk_signal_connect_object (GTK_OBJECT (d->cancel_button), "clicked",
                                   GTK_SIGNAL_FUNC (gtk_widget_destroy),
                                   GTK_OBJECT (csd));
        gtk_widget_show (csd);
}

static void
prefs_destroy (GtkWidget *widget, gpointer data)
{
        (void) widget;
        (void) data;
        g_free (prefs.mapset);
        memset (&prefs, 0, sizeof prefs);
}

static void
apply_preferences (GtkWidget *widget, gpointer data)
{
        gchar colour[8];
        gboolean ask_newgame = FALSE;

        (void) widget;
        (void) data;

        if (strcmp (prefs.mapset, mapset) != 0) {
                set_map (prefs.mapset);
                games_config_set_string (config, "map", mapset);
                ask_newgame = strcmp (mapset, game_map) != 0;
        }

        g_snprintf (colour, sizeof colour, "#%02x%02x%02x",
                    prefs.colour.red >> 8, prefs.colour.green >> 8,
                    prefs.colour.blue >> 8);
        set_backgnd_colour (colour);
        games_config_set_string (config, "colour", backgnd_name);

        popup_config.confirm = GTK_TOGGLE_BUTTON (prefs.confirm)->active;
        games_config_set_int (config, "confirm", popup_config.confirm);

        games_config_save (config);
        gtk_widget_destroy (prefs.dialog);

        /* A game hardly begun just starts again on the new map. */
        if (ask_newgame && (game_over != GAME_RUNNING || sequence_number <= 1))
                new_game (games_rand_new_seed ());
        else if (ask_newgame)
                games_question (GTK_WINDOW (window), _(APPNAME_LONG),
                                _("Do you want to start a new game with this map?\n\n"
                                  "If you continue playing, the next game will\n"
                                  "use the new map."),
                                _("Use New Map"), _("Continue Playing"),
                                new_game_reply_callback, NULL);
}

/* A frame holding a vbox, packed into box. */
static GtkWidget *
prefs_frame (GtkWidget *box, const gchar *title)
{
        GtkWidget *f = gtk_frame_new (title);
        GtkWidget *vbox = gtk_vbox_new (FALSE, 5);

        gtk_container_set_border_width (GTK_CONTAINER (f), 5);
        gtk_container_set_border_width (GTK_CONTAINER (vbox), 5);
        gtk_container_add (GTK_CONTAINER (f), vbox);
        gtk_box_pack_start (GTK_BOX (box), f, FALSE, FALSE, 0);
        return vbox;
}

/* A label and a widget side by side. */
static void
prefs_row (GtkWidget *box, const gchar *text, GtkWidget *widget)
{
        GtkWidget *hbox = gtk_hbox_new (FALSE, 6);
        GtkWidget *label = gtk_label_new (text);

        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 0);
        gtk_box_pack_start (GTK_BOX (hbox), widget, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (box), hbox, FALSE, FALSE, 0);
}

static void
properties (void)
{
        GtkWidget *cols, *col1, *col2, *fv, *button;

        if (prefs.dialog) {
                gdk_window_raise (prefs.dialog->window);
                return;
        }

        prefs.mapset = g_strdup (mapset);
        gdk_color_parse (backgnd_name, &prefs.colour);

        prefs.dialog = games_dialog_new (GTK_WINDOW (window), _("Preferences"));
        gtk_signal_connect (GTK_OBJECT (prefs.dialog), "destroy",
                            GTK_SIGNAL_FUNC (prefs_destroy), NULL);

        cols = gtk_hbox_new (FALSE, 0);
        col1 = gtk_vbox_new (FALSE, 0);
        col2 = gtk_vbox_new (FALSE, 0);
        gtk_container_set_border_width (GTK_CONTAINER (cols), 5);

        fv = prefs_frame (col1, _("Maps"));
        prefs_row (fv, _("Select Map:"), map_menu ());

        fv = prefs_frame (col2, _("Colours"));
        button = gtk_button_new ();
        prefs.swatch = gtk_drawing_area_new ();
        gtk_drawing_area_size (GTK_DRAWING_AREA (prefs.swatch), 32, 14);
        gtk_signal_connect (GTK_OBJECT (prefs.swatch), "expose_event",
                            GTK_SIGNAL_FUNC (swatch_expose), NULL);
        gtk_container_add (GTK_CONTAINER (button), prefs.swatch);
        gtk_signal_connect (GTK_OBJECT (button), "clicked",
                            GTK_SIGNAL_FUNC (colour_clicked), NULL);
        prefs_row (fv, _("Background:"), button);

        fv = prefs_frame (col2, _("Warnings"));
        prefs.confirm = gtk_check_button_new_with_label (_("Confirm before starting over"));
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (prefs.confirm),
                                      popup_config.confirm);
        gtk_box_pack_start (GTK_BOX (fv), prefs.confirm, FALSE, FALSE, 0);

        gtk_box_pack_start (GTK_BOX (cols), col1, TRUE, TRUE, 0);
        gtk_box_pack_start (GTK_BOX (cols), col2, TRUE, TRUE, 0);
        gtk_box_pack_start (GTK_BOX (GTK_DIALOG (prefs.dialog)->vbox), cols,
                            TRUE, TRUE, 0);

        button = games_dialog_add_button (prefs.dialog, _("OK"));
        gtk_signal_connect (GTK_OBJECT (button), "clicked",
                            GTK_SIGNAL_FUNC (apply_preferences), NULL);
        button = games_dialog_add_button (prefs.dialog, _("Cancel"));
        gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
                                   GTK_SIGNAL_FUNC (gtk_widget_destroy),
                                   GTK_OBJECT (prefs.dialog));

        gtk_widget_show_all (prefs.dialog);
}

/* The window. */

static void
do_action (game_action action)
{
        switch (action) {
        case NEW_GAME:
        case RESTART_GAME:
        case SELECT_GAME:
                confirm_action (action);
                break;
        case QUIT_GAME:
                quit_game ();
                break;
        case UNDO_MOVE:
                undo_tile ();
                break;
        case REDO_MOVE:
                redo_tile ();
                break;
        case HINT:
                hint ();
                break;
        case SCORES:
                show_scores ();
                break;
        case PREFERENCES:
                properties ();
                break;
        case ABOUT:
                about ();
                break;
        }
}

static void
menu_callback (gpointer data, guint action, GtkWidget *widget)
{
        (void) data;
        (void) widget;
        do_action ((game_action) action);
}

static void
toolbar_callback (GtkWidget *widget, gpointer data)
{
        (void) widget;
        do_action ((game_action) GPOINTER_TO_UINT (data));
}

static gint
delete_event_callback (GtkWidget *widget, GdkEvent *event, gpointer data)
{
        (void) widget;
        (void) event;
        (void) data;
        do_action (QUIT_GAME);
        return TRUE;
}

static GtkItemFactoryEntry menu_items[] = {
        { N_("/_Game"), NULL, NULL, 0, "<Branch>" },
        { N_("/Game/_New Game"), "<control>N",
          GTK_SIGNAL_FUNC (menu_callback), NEW_GAME, NULL },
        { N_("/Game/New Game with _Seed..."), NULL,
          GTK_SIGNAL_FUNC (menu_callback), SELECT_GAME, NULL },
        { N_("/Game/_Restart Game"), NULL,
          GTK_SIGNAL_FUNC (menu_callback), RESTART_GAME, NULL },
        { "/Game/sep1", NULL, NULL, 0, "<Separator>" },
        { N_("/Game/_Undo Move"), "<control>Z",
          GTK_SIGNAL_FUNC (menu_callback), UNDO_MOVE, NULL },
        { N_("/Game/Re_do Move"), "<shift><control>Z",
          GTK_SIGNAL_FUNC (menu_callback), REDO_MOVE, NULL },
        { N_("/Game/_Hint"), "<control>H",
          GTK_SIGNAL_FUNC (menu_callback), HINT, NULL },
        { N_("/Game/_Pause"), "Pause",
          GTK_SIGNAL_FUNC (pause_item_callback), 0, "<CheckItem>" },
        { "/Game/sep2", NULL, NULL, 0, "<Separator>" },
        { N_("/Game/S_cores..."), NULL,
          GTK_SIGNAL_FUNC (menu_callback), SCORES, NULL },
        { "/Game/sep3", NULL, NULL, 0, "<Separator>" },
        { N_("/Game/E_xit"), "<control>Q",
          GTK_SIGNAL_FUNC (menu_callback), QUIT_GAME, NULL },
        { N_("/_Settings"), NULL, NULL, 0, "<Branch>" },
        { N_("/Settings/Show _Tool Bar"), NULL,
          GTK_SIGNAL_FUNC (show_tb_callback), 0, "<CheckItem>" },
        { "/Settings/sep1", NULL, NULL, 0, "<Separator>" },
        { N_("/Settings/_Preferences..."), NULL,
          GTK_SIGNAL_FUNC (menu_callback), PREFERENCES, NULL },
        { N_("/_Help"), NULL, NULL, 0, "<Branch>" },
        { N_("/Help/_About Mahjong..."), NULL,
          GTK_SIGNAL_FUNC (menu_callback), ABOUT, NULL },
};

static gchar *
translate (const gchar *path, gpointer data)
{
        (void) data;
        return (gchar *) _(path);
}

static GtkWidget *
create_toolbar (void)
{
        GtkWidget *toolbar = gtk_toolbar_new (GTK_ORIENTATION_HORIZONTAL,
                                              GTK_TOOLBAR_TEXT);
        GtkToolbar *tb = GTK_TOOLBAR (toolbar);

        gtk_toolbar_set_button_relief (tb, GTK_RELIEF_NONE);
        gtk_toolbar_set_space_size (tb, 25);

        gtk_toolbar_append_item (tb, _("New"), _("Start a new game"), NULL, NULL,
                                 GTK_SIGNAL_FUNC (toolbar_callback),
                                 GUINT_TO_POINTER (NEW_GAME));
        gtk_toolbar_append_item (tb, _("Undo"), _("Undo the last move"),
                                 NULL, NULL, GTK_SIGNAL_FUNC (toolbar_callback),
                                 GUINT_TO_POINTER (UNDO_MOVE));
        gtk_toolbar_append_item (tb, _("Hint"), _("Show a pair that can be taken"),
                                 NULL, NULL, GTK_SIGNAL_FUNC (toolbar_callback),
                                 GUINT_TO_POINTER (HINT));
        pause_button = gtk_toolbar_append_element (tb, GTK_TOOLBAR_CHILD_TOGGLEBUTTON,
                                                   NULL, _("Pause"),
                                                   _("Pause the game"), NULL, NULL,
                                                   GTK_SIGNAL_FUNC (pause_button_callback),
                                                   NULL);
        return toolbar;
}

/* A sunken box at the end of the status bar: text, then a value. */
static GtkWidget *
status_field (GtkWidget *box, const gchar *text, GtkWidget *value)
{
        GtkWidget *border = gtk_frame_new (NULL);
        GtkWidget *hbox = gtk_hbox_new (FALSE, 0);

        gtk_frame_set_shadow_type (GTK_FRAME (border), GTK_SHADOW_IN);
        gtk_box_pack_start (GTK_BOX (hbox), gtk_label_new (text), FALSE, FALSE, 4);
        gtk_box_pack_start (GTK_BOX (hbox), value, FALSE, FALSE, 4);
        gtk_container_add (GTK_CONTAINER (border), hbox);
        gtk_box_pack_end (GTK_BOX (box), border, FALSE, FALSE, 0);
        return value;
}

int
main (int argc, char *argv[])
{
        GtkWidget *vbox, *item, *status_box;
        GtkAccelGroup *accel_group;
        gint i;

        gtk_set_locale ();
        games_init_translation ();
        gtk_init (&argc, &argv);

        config = games_config_load (APPNAME);

        window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title (GTK_WINDOW (window), _(APPNAME_LONG));
        gtk_window_set_wmclass (GTK_WINDOW (window), APPNAME, "Cgmahjong");
        gtk_window_set_policy (GTK_WINDOW (window), FALSE, TRUE, FALSE);
        gtk_signal_connect (GTK_OBJECT (window), "delete_event",
                            GTK_SIGNAL_FUNC (delete_event_callback), NULL);
        gtk_signal_connect (GTK_OBJECT (window), "destroy",
                            GTK_SIGNAL_FUNC (gtk_main_quit), NULL);

        vbox = gtk_vbox_new (FALSE, 0);
        gtk_container_add (GTK_CONTAINER (window), vbox);

        accel_group = gtk_accel_group_new ();
        factory = gtk_item_factory_new (GTK_TYPE_MENU_BAR, "<main>", accel_group);
        gtk_item_factory_set_translate_func (factory, translate, NULL, NULL);
        gtk_item_factory_create_items (factory, ELEMENTS (menu_items),
                                       menu_items, NULL);
        gtk_window_add_accel_group (GTK_WINDOW (window), accel_group);
        gtk_box_pack_start (GTK_BOX (vbox),
                            gtk_item_factory_get_widget (factory, "<main>"),
                            FALSE, FALSE, 0);

        toolbar_box = gtk_handle_box_new ();
        gtk_container_add (GTK_CONTAINER (toolbar_box), create_toolbar ());
        gtk_box_pack_start (GTK_BOX (vbox), toolbar_box, FALSE, FALSE, 0);

        board = gtk_drawing_area_new ();
        /* The board starts at the size it was left at; once shown, it
         * can be made as small as MIN_WIDTH by MIN_HEIGHT. */
        gtk_widget_set_usize (board,
                              MAX (games_config_get_int (config, "width", AREA_WIDTH),
                                   MIN_WIDTH),
                              MAX (games_config_get_int (config, "height", AREA_HEIGHT),
                                   MIN_HEIGHT));
        gtk_widget_set_events (board, GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK);
        gtk_signal_connect (GTK_OBJECT (board), "configure_event",
                            GTK_SIGNAL_FUNC (board_configure), NULL);
        gtk_signal_connect (GTK_OBJECT (board), "expose_event",
                            GTK_SIGNAL_FUNC (board_expose), NULL);
        gtk_signal_connect (GTK_OBJECT (board), "button_press_event",
                            GTK_SIGNAL_FUNC (board_button_press), NULL);
        gtk_box_pack_start (GTK_BOX (vbox), board, TRUE, TRUE, 0);

        /* The status bar: messages, then the tiles and moves left and the
         * time, as in GNOME Mahjongg's. */
        status_box = gtk_hbox_new (FALSE, 2);
        statusbar = gtk_statusbar_new ();
        gtk_box_pack_start (GTK_BOX (status_box), statusbar, TRUE, TRUE, 0);
        chrono = status_field (status_box, _("Time:"), games_clock_new ());
        moves_label = status_field (status_box, _("Moves Left:"),
                                    gtk_label_new (MAX_TILES_STR));
        tiles_label = status_field (status_box, _("Tiles Left:"),
                                    gtk_label_new (MAX_TILES_STR));
        gtk_box_pack_start (GTK_BOX (vbox), status_box, FALSE, FALSE, 0);

        maps_load ();
        score_categories = g_new (GamesScoreCategory, n_maps);
        for (i = 0; i < n_maps; i++) {
                score_categories[i].key = maps[i].key;
                score_categories[i].name = maps[i].name;
        }
        set_map (games_config_get_string (config, "map", "easy"));
        set_backgnd_colour (games_config_get_string (config, "colour", "#003010"));
        popup_config.confirm = games_config_get_int (config, "confirm", 1) != 0;

        /* Drawn again to fit the board once it is shown. */
        tileset = tileset_new (HALF_WIDTH);
        if (!tileset) {
                fprintf (stderr, _("%s: the tile images could not be read\n"),
                         argv[0]);
                return 1;
        }

        new_game (games_rand_new_seed ());

        games_window_set_icon (window, "cgmahjong", cgmahjong_xpm);
        gtk_widget_show_all (window);
        gtk_widget_set_usize (board, MIN_WIDTH, MIN_HEIGHT);

        item = gtk_item_factory_get_widget (factory, "/Settings/Show Tool Bar");
        syncing = TRUE;
        gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), TRUE);
        gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item),
                                        games_config_get_int (config, "toolbar", 1) != 0);
        syncing = FALSE;

        message (_("Welcome to Mahjong!"));

        gtk_main ();

        games_config_free (config);
        return 0;
}
