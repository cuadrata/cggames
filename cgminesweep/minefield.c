/*
 * cgminesweep: the minefield
 * Copyright (C) 1997-1999 the Free Software Foundation
 * Copyright (C) 2026 The CGTK authors
 *
 * Author: Pista <szekeres@cyberspace.mht.bme.hu>
 * Mine resizing: djb@redhat.com
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
#include <gtk/gtk.h>

#include "games.h"
#include "minefield.h"

static const struct {
        gint x;
        gint y;
} neighbour_map[8] = {
        { -1,  1 },
        { 0 ,  1 },
        { 1 ,  1 },
        { 1 ,  0 },
        { 1 , -1 },
        { 0 , -1 },
        { -1, -1 },
        { -1, 0  }
};

/* The numbers' colours, as Windows' Minesweeper has them. */
static const guint32 num_colors[9] = {
        0x000000, /* not used */
        0x0000ff, /* blue */
        0x008000, /* green */
        0xff0000, /* red */
        0x000080, /* navy */
        0x800000, /* maroon */
        0x008080, /* teal */
        0x000000, /* black */
        0x808080  /* grey */
};

/* The colours of the flags and mines; the squares are the theme's. */
static const guint32 paint_colors[N_PAINTS] = {
        0xffffff, /* PAINT_LIGHT */
        0x000000, /* PAINT_BLACK */
        0xff0000  /* PAINT_RED */
};

enum {
        MARKS_CHANGED_SIGNAL,
        EXPLODE_SIGNAL,
        LOOK_SIGNAL,
        UNLOOK_SIGNAL,
        WIN_SIGNAL,
        LAST_SIGNAL
};

static guint minefield_signals[LAST_SIGNAL] = { 0 };

static GtkWidgetClass *parent_class;

/* The cell at (x, y), or -1 off the field. */
static inline gint cell_idx(GtkMineField *mfield, gint x, gint y)
{
        if (x >= 0 && x < (gint) mfield->xsize && y >= 0 && y < (gint) mfield->ysize)
                return x + y * (gint) mfield->xsize;
        return -1;
}

/* The numbers' font and colours, for the mine size; made again when
 * that changes. */
static void gtk_minefield_free_numbers(GtkMineField *mfield)
{
        gint i;

        for (i = 0; i < 9; i++) {
                if (mfield->numstr[i].gc)
                        gdk_gc_unref(mfield->numstr[i].gc);
                mfield->numstr[i].gc = NULL;
        }
        for (i = 0; i < N_PAINTS; i++) {
                if (mfield->paint[i])
                        gdk_gc_unref(mfield->paint[i]);
                mfield->paint[i] = NULL;
        }
        if (mfield->font)
                gdk_font_unref(mfield->font);
        mfield->font = NULL;
}

static void gtk_minefield_setup_numbers(GtkMineField *mfield)
{
        GtkWidget *widget = GTK_WIDGET(mfield);
        gint pxlsz, i;
        char fontname[64];

        gtk_minefield_free_numbers(mfield);

        pxlsz = (gint) mfield->minesize * 3 / 4;
        if (pxlsz > 999) pxlsz = 999;
        if (pxlsz < 2)  pxlsz = 2;

        /* A bold sans, near enough to Windows' blocky numbers. */
        g_snprintf(fontname, sizeof fontname,
                   "-*-helvetica-bold-r-*-*-%d-*-*-*-*-*-*-*", pxlsz);
        mfield->font = gdk_font_load(fontname);
        if (!mfield->font)
                mfield->font = widget->style->font;
        gdk_font_ref(mfield->font);

        for (i=0; i<9; i++) {
                mfield->numstr[i].text[0] = (char) (i+'0');
                mfield->numstr[i].text[1] = '\0';
                mfield->numstr[i].dx =
                        ((gint) mfield->minesize - gdk_string_width(mfield->font,
                                                                    mfield->numstr[i].text))/2;
                mfield->numstr[i].dy = ((gint) mfield->minesize
                                        + mfield->font->ascent - mfield->font->descent) / 2;
                mfield->numstr[i].gc = gdk_gc_new(widget->window);
                gdk_rgb_gc_set_foreground(mfield->numstr[i].gc, num_colors[i]);
        }
        for (i = 0; i < N_PAINTS; i++) {
                mfield->paint[i] = gdk_gc_new(widget->window);
                gdk_rgb_gc_set_foreground(mfield->paint[i], paint_colors[i]);
        }
}

static void gtk_minefield_realize(GtkWidget *widget)
{
        GtkMineField *mfield;
        GdkWindowAttr attributes;
        gint attributes_mask;

        g_return_if_fail(widget != NULL);
        g_return_if_fail(GTK_IS_MINEFIELD (widget));

        mfield = GTK_MINEFIELD(widget);
        GTK_WIDGET_SET_FLAGS(widget, GTK_REALIZED);

        attributes.window_type = GDK_WINDOW_CHILD;
        attributes.x = widget->allocation.x;
        attributes.y = widget->allocation.y;
        attributes.width = widget->allocation.width;
        attributes.height = widget->allocation.height;
        attributes.wclass = GDK_INPUT_OUTPUT;
        attributes.visual = gtk_widget_get_visual(widget);
        attributes.colormap = gtk_widget_get_colormap(widget);
        attributes.event_mask = gtk_widget_get_events(widget);
        attributes.event_mask |= GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK |
                                 GDK_BUTTON_RELEASE_MASK |
                                 GDK_POINTER_MOTION_MASK;

        attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;

        widget->window = gdk_window_new(widget->parent->window, &attributes, attributes_mask);
        gdk_window_set_user_data(widget->window, mfield);

        widget->style = gtk_style_attach(widget->style, widget->window);
        gtk_style_set_background(widget->style, widget->window, GTK_STATE_NORMAL);

        gtk_minefield_setup_numbers(mfield);
}

static void gtk_minefield_unrealize (GtkWidget *widget)
{
        GtkMineField *mfield;

        g_return_if_fail (widget != NULL);
        g_return_if_fail (GTK_IS_MINEFIELD (widget));

        mfield = GTK_MINEFIELD (widget);

        gtk_minefield_free_numbers (mfield);

        if (GTK_WIDGET_CLASS (parent_class)->unrealize)
                (* GTK_WIDGET_CLASS (parent_class)->unrealize) (widget);
}

static void gtk_minefield_style_set (GtkWidget *widget, GtkStyle *previous)
{
        (void) previous;
        if (GTK_WIDGET_REALIZED (widget)) {
                gtk_style_set_background(widget->style, widget->window, GTK_STATE_NORMAL);
                gtk_minefield_setup_numbers(GTK_MINEFIELD (widget));
                gtk_widget_queue_draw(widget);
        }
}

static void gtk_minefield_destroy (GtkObject *object)
{
        GtkMineField *mfield = GTK_MINEFIELD (object);

        g_free (mfield->mines);
        mfield->mines = NULL;

        if (GTK_OBJECT_CLASS (parent_class)->destroy)
                (* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}

static void gtk_minefield_size_allocate(GtkWidget *widget,
                                        GtkAllocation *allocation)
{
        g_return_if_fail(widget != NULL);
        g_return_if_fail(GTK_IS_MINEFIELD (widget));
        g_return_if_fail(allocation != NULL);

        widget->allocation = *allocation;

        if (GTK_WIDGET_REALIZED(widget)) {
                gdk_window_move_resize(widget->window,
                                       allocation->x, allocation->y,
                                       GTK_MINEFIELD(widget)->xsize *
                                       GTK_MINEFIELD(widget)->minesize,
                                       GTK_MINEFIELD(widget)->ysize *
                                       GTK_MINEFIELD(widget)->minesize);
        }
}

static void gtk_minefield_size_request(GtkWidget *widget,
                                       GtkRequisition *requisition)
{
        requisition->width  = GTK_MINEFIELD(widget)->xsize *
                              GTK_MINEFIELD(widget)->minesize;
        requisition->height = GTK_MINEFIELD(widget)->ysize *
                              GTK_MINEFIELD(widget)->minesize;
}

/* The squares are drawn in the theme's colours and shadows, which a
 * Windows-like theme makes Windows' Minesweeper's; the pictures on them
 * as Windows draws them, in shapes worked out from a square of 16 so
 * that they grow with the mine size. GDK draws all of it as sharp as
 * the window's scale allows. */

/* x or y of 16 in a square of size s. */
#define AT(v, s) (((v) * (s) + 8) / 16)

static void fill_rect (GtkMineField *mfield, gint paint, gint x, gint y,
                       gint w, gint h)
{
        if (w > 0 && h > 0)
                gdk_draw_rectangle(GTK_WIDGET(mfield)->window,
                                   mfield->paint[paint], TRUE, x, y, w, h);
}

/* A square not yet opened: raised, as the theme raises a button. */
static void draw_raised (GtkMineField *mfield, gint px, gint py, gint s)
{
        GtkWidget *widget = GTK_WIDGET(mfield);

        gdk_draw_rectangle(widget->window, widget->style->bg_gc[GTK_STATE_NORMAL],
                           TRUE, px, py, s, s);
        gtk_draw_shadow(widget->style, widget->window, GTK_STATE_NORMAL,
                        GTK_SHADOW_OUT, px, py, s, s);
}

/* An open square, or one held down: flat, on gc, with the grid's line
 * above and to the left in the theme's dark colour. */
static void draw_flat (GtkMineField *mfield, GdkGC *gc, gint px, gint py,
                       gint s)
{
        GtkWidget *widget = GTK_WIDGET(mfield);
        GdkGC *dark = widget->style->dark_gc[GTK_STATE_NORMAL];

        gdk_draw_rectangle(widget->window, gc, TRUE, px, py, s, s);
        gdk_draw_rectangle(widget->window, dark, TRUE, px, py, s, 1);
        gdk_draw_rectangle(widget->window, dark, TRUE, px, py, 1, s);
}

static void draw_mine (GtkMineField *mfield, gint px, gint py, gint s)
{
        GdkWindow *window = GTK_WIDGET(mfield)->window;
        gint t = MAX (AT(1, s), 1);
        gint c = AT(8, s), r = AT(5, s) - t / 2;
        gint d = AT(2, s), k;

        /* The spikes, straight and slanted. */
        fill_rect(mfield, PAINT_BLACK, px + AT(2, s), py + c - t / 2,
                  AT(14, s) - AT(2, s), t);
        fill_rect(mfield, PAINT_BLACK, px + c - t / 2, py + AT(2, s),
                  t, AT(14, s) - AT(2, s));
        for (k = 0; k < 4; k++) {
                gint dx = k & 1 ? d : -d, dy = k & 2 ? d : -d;

                fill_rect(mfield, PAINT_BLACK,
                          px + c + dx * 2 - t, py + c + dy * 2 - t, 2 * t, 2 * t);
        }
        /* The ball, and its shine. */
        gdk_draw_arc(window, mfield->paint[PAINT_BLACK], TRUE,
                     px + c - r, py + c - r, 2 * r, 2 * r, 0, 360 * 64);
        fill_rect(mfield, PAINT_LIGHT, px + c - r / 2 - t, py + c - r / 2 - t,
                  MAX (AT(2, s), 1), MAX (AT(2, s), 1));
}

static void draw_flag (GtkMineField *mfield, gint px, gint py, gint s)
{
        GdkWindow *window = GTK_WIDGET(mfield)->window;
        GdkPoint flag[3];
        gint t = MAX (AT(1, s), 1);

        flag[0].x = px + AT(9, s);  flag[0].y = py + AT(3, s);
        flag[1].x = px + AT(9, s);  flag[1].y = py + AT(8, s);
        flag[2].x = px + AT(4, s);  flag[2].y = py + (AT(3, s) + AT(8, s)) / 2;
        gdk_draw_polygon(window, mfield->paint[PAINT_RED], TRUE, flag, 3);
        /* The pole, on its stand. */
        fill_rect(mfield, PAINT_BLACK, px + AT(9, s), py + AT(3, s), t,
                  AT(11, s) - AT(3, s));
        fill_rect(mfield, PAINT_BLACK, px + AT(6, s), py + AT(11, s),
                  AT(11, s) - AT(6, s) + t - 1, AT(12, s) - AT(11, s));
        fill_rect(mfield, PAINT_BLACK, px + AT(4, s), py + AT(12, s),
                  AT(13, s) - AT(4, s) + t - 1, AT(13, s) - AT(12, s) + t - 1);
}

/* The question mark, in black on the raised square. */
static void draw_question (GtkMineField *mfield, gint px, gint py, gint s)
{
        gint w = gdk_string_width(mfield->font, "?");

        gdk_draw_string(GTK_WIDGET(mfield)->window, mfield->font,
                        mfield->paint[PAINT_BLACK], px + (s - w) / 2,
                        py + (s + mfield->font->ascent - mfield->font->descent) / 2,
                        "?");
}

/* The cross over a flag that was wrong. */
static void draw_cross (GtkMineField *mfield, gint px, gint py, gint s)
{
        GdkWindow *window = GTK_WIDGET(mfield)->window;
        gint t = MAX (AT(1, s), 1), a = AT(2, s), b = AT(14, s);
        GdkPoint p[4];
        gint k;

        for (k = 0; k < 2; k++) {
                gint y0 = k ? b : a, y1 = k ? a : b;

                p[0].x = px + a;     p[0].y = py + y0;
                p[1].x = px + a + t; p[1].y = py + y0;
                p[2].x = px + b + t; p[2].y = py + y1;
                p[3].x = px + b;     p[3].y = py + y1;
                gdk_draw_polygon(window, mfield->paint[PAINT_RED], TRUE, p, 4);
        }
}

static void gtk_mine_draw(GtkMineField *mfield, guint x, guint y)
{
        int c = cell_idx(mfield, (gint) x, (gint) y);
        int n;
        gint s, px, py;
        gboolean open, over;
        GdkGC *bg;
        mine *m;

        GtkWidget *widget = GTK_WIDGET(mfield);

        if (c < 0 || !GTK_WIDGET_DRAWABLE(widget) || !mfield->paint[0])
                return;

        m = &mfield->mines[c];
        /* Open squares in the colour of a pressed button, as GNOME Mines
         * had them: the same grey as the rest in a Windows-like theme, a
         * shade darker in one whose bevels are fainter. */
        bg = widget->style->bg_gc[GTK_STATE_ACTIVE];
        s = (gint) mfield->minesize;
        px = (gint) x * s;
        py = (gint) y * s;
        over = mfield->lose || mfield->win;
        open = m->shown || (!over && m->down);

        if (!open) {
                /* At a loss, the mines not found show, and the flags that
                 * were wrong are crossed out. */
                if (mfield->lose && m->mined && !m->marked) {
                        draw_flat(mfield, bg, px, py, s);
                        draw_mine(mfield, px, py, s);
                } else if (mfield->lose && m->marked && !m->mined) {
                        draw_flat(mfield, bg, px, py, s);
                        draw_mine(mfield, px, py, s);
                        draw_cross(mfield, px, py, s);
                } else {
                        draw_raised(mfield, px, py, s);
                        if (m->marked)
                                draw_flag(mfield, px, py, s);
                        else if (m->questioned)
                                draw_question(mfield, px, py, s);
                }
                return;
        }

        if (m->shown && m->mined) {
                /* The mine that went off, on red. */
                draw_flat(mfield, mfield->paint[PAINT_RED], px, py, s);
                draw_mine(mfield, px, py, s);
                return;
        }
        draw_flat(mfield, bg, px, py, s);
        if (m->shown && (n = (int) m->neighbours) != 0)
                gdk_draw_string(widget->window, mfield->font,
                                mfield->numstr[n].gc,
                                px + mfield->numstr[n].dx,
                                py + mfield->numstr[n].dy,
                                mfield->numstr[n].text);
}

void gtk_minefield_draw(GtkMineField *mfield, GdkRectangle *area)
{
        gint x1, y1, x2, y2, x, y, minesize;

        minesize = (gint) mfield->minesize;
        if (mfield->xsize == 0 || mfield->ysize == 0)
                return;

        if (area) {
                x1 = area->x/minesize;
                y1 = area->y/minesize;
                x2 = (area->x + area->width - 1) / minesize;
                y2 = (area->y + area->height - 1) / minesize;
        } else {
                x1 = 0; y1 = 0;
                x2 = (gint) mfield->xsize - 1;
                y2 = (gint) mfield->ysize - 1;
        }
        if (x2 >= (gint) mfield->xsize) x2 = (gint) mfield->xsize - 1;
        if (y2 >= (gint) mfield->ysize) y2 = (gint) mfield->ysize - 1;

        for (x = x1; x <= x2; x++)
                for (y = y1; y <= y2; y++)
                        gtk_mine_draw(mfield, (guint) x, (guint) y);
}

static gint gtk_minefield_expose(GtkWidget *widget,
                                 GdkEventExpose *event)
{
        g_return_val_if_fail(widget != NULL, FALSE);
        g_return_val_if_fail(GTK_IS_MINEFIELD(widget), FALSE);
        g_return_val_if_fail(event != NULL, FALSE);

        if (!GTK_WIDGET_DRAWABLE(widget))
                return FALSE;

        gtk_minefield_draw(GTK_MINEFIELD(widget), &event->area);

        return FALSE;
}

static inline int gtk_minefield_check_cell(GtkMineField *mfield, gint x, gint y)
{
        guint changed;
        gint c;
        guint i;
        gint nx, ny;

        changed = 0;

        for (i=0; i<8; i++) {
                nx = x+neighbour_map[i].x;
                ny = y+neighbour_map[i].y;
                if ((c = cell_idx(mfield, nx, ny)) != -1) {
                        if (mfield->mines[c].shown == 0 &&
                            mfield->mines[c].marked == 0) {
                                mfield->mines[c].shown = 1;
                                mfield->shown++;
                                gtk_mine_draw(mfield, (guint) nx, (guint) ny);
                                changed = 1;
                        }
                }
        }
        return changed;
}


static void gtk_minefield_check_field(GtkMineField *mfield, gint x, gint y)
{
        gint c;
        guint changed;

        gint x1, y1, x2, y2;
        gint cx1,cy1,cx2,cy2;

        cx1 = x-2;
        cy1 = y-2;
        cx2 = x+2;
        cy2 = y+2;

        do {
                x1 = cx1-1;
                y1 = cy1-1;
                x2 = cx2+1;
                y2 = cy2+1;

                if (x1 < 0) x1 = 0;
                if (y1 < 0) y1 = 0;
                if (x2 >= (gint) mfield->xsize) x2 = (gint) mfield->xsize-1;
                if (y2 >= (gint) mfield->ysize) y2 = (gint) mfield->ysize-1;

                changed = 0;
                for (x=x1; x<=x2; x++) {
                        for (y=y1; y<=y2; y++) {
                                c = cell_idx(mfield, x, y);
                                if (mfield->mines[c].neighbours == 0 &&
                                    mfield->mines[c].shown == 1) {
                                        changed |= gtk_minefield_check_cell(mfield, x, y);
                                        if (changed) {
                                                if (x < cx1) cx1 = x;
                                                if (x > cx2) cx2 = x;
                                                if (y < cy1) cy1 = y;
                                                if (y > cy2) cy2 = y;
                                        }
                                }
                        }
                }
        } while (changed);
}

static void gtk_minefield_loose(GtkMineField *mfield)
{
        gtk_signal_emit(GTK_OBJECT(mfield),
                        minefield_signals[EXPLODE_SIGNAL]);
        mfield->lose = 1;
        gtk_minefield_draw(mfield, NULL);
}

static void gtk_minefield_win(GtkMineField *mfield)
{
        guint x, y, c;

        for (x = 0; x < mfield->xsize; x++)
                for (y = 0; y < mfield->ysize; y++)
                {
                        c = x+y*mfield->xsize;
                        if (mfield->mines[c].shown == 0 && mfield->mines[c].marked == 0)
                        {
                                mfield->mines[c].marked = 1;
                                gtk_mine_draw(mfield, x, y);
                        }
                }

        /* The mines left are flagged, and counted. */
        mfield->flags = mfield->mcount;
        mfield->win = 1;
        gtk_signal_emit(GTK_OBJECT(mfield),
                        minefield_signals[WIN_SIGNAL]);
}

/* Counts each square's mined neighbours. */
static void count_neighbours(GtkMineField *mfield)
{
        guint x, y, i, n;
        gint cidx;

        for (x=0; x<mfield->xsize; x++) {
                for (y=0; y<mfield->ysize; y++) {
                        n = 0;
                        for (i=0; i<8; i++) {
                                if ((cidx = cell_idx(mfield, (gint) x+neighbour_map[i].x,
                                                     (gint) y+neighbour_map[i].y)) != -1) {
                                        if (mfield->mines[cidx].mined) n++;
                                }
                        }
                        mfield->mines[x+mfield->xsize*y].neighbours = n;
                }
        }
}

/* The first square uncovered never holds a mine: as in Windows'
 * Minesweeper, a mine there moves to the first free square from the
 * top left. */
static void move_first_mine(GtkMineField *mfield, guint c)
{
        guint i, cells = mfield->xsize*mfield->ysize;

        for (i = 0; i < cells; i++) {
                if (i != c && !mfield->mines[i].mined) {
                        mfield->mines[i].mined = 1;
                        mfield->mines[c].mined = 0;
                        count_neighbours(mfield);
                        return;
                }
        }
}

static void gtk_minefield_show(GtkMineField *mfield, guint x, guint y)
{
        guint c = x+mfield->xsize*y;

        if (mfield->mines[c].marked != 1 && mfield->mines[c].shown != 1) {
                if (mfield->shown == 0 && mfield->mines[c].mined)
                        move_first_mine(mfield, c);
                mfield->mines[c].shown = 1;
                mfield->shown++;
                gtk_mine_draw(mfield, mfield->cdownx, mfield->cdowny);
                if(mfield->mines[c].mined == 1) {
                        gtk_minefield_loose(mfield);
                } else {
                        gtk_minefield_check_field(mfield, (gint) x, (gint) y);
                        if (mfield->shown == mfield->xsize*mfield->ysize-mfield->mcount) {
                                gtk_minefield_win(mfield);
                        }
                }
        }
}

static void gtk_minefield_toggle_mark(GtkMineField *mfield, guint x, guint y)
{
        int c = cell_idx(mfield, (gint) x, (gint) y);
        if (mfield->mines[c].shown == 0) {
                mine *m = &mfield->mines[c];

                /* Blank, flag, question mark (when marks are on), blank. */
                if (m->marked) {
                        m->marked = 0;
                        m->questioned = mfield->use_marks;
                        mfield->flags--;
                } else if (m->questioned) {
                        m->questioned = 0;
                } else {
                        m->marked = 1;
                        mfield->flags++;
                }
                gtk_signal_emit(GTK_OBJECT(mfield),
                                minefield_signals[MARKS_CHANGED_SIGNAL]);
                if (mfield->shown == mfield->xsize*mfield->ysize-mfield->mcount) {
                        gtk_minefield_win(mfield);
                }
        }
}

static inline void gtk_minefield_multi_press(GtkMineField *mfield, guint x, guint y)
{
        guint i;
        gint nx, ny, c2;

        for (i=0; i<8; i++) {
                nx = (gint) x+neighbour_map[i].x;
                ny = (gint) y+neighbour_map[i].y;
                if ((c2 = cell_idx(mfield, nx, ny)) == -1)
                        continue;
                if (!mfield->mines[c2].marked && !mfield->mines[c2].shown) {
                        mfield->mines[c2].down = 1;
                        gtk_mine_draw(mfield, (guint) nx, (guint) ny);
                }
        }
        mfield->multi_mode = 1;
}

static void gtk_minefield_multi_release (GtkMineField *mfield, guint x, guint y, guint c, guint really)
{
        gint n, nx, ny, i, c2;
        guint loose = 0;

        mfield->multi_mode = 0;

        n = 0;
        for (i=0; i<8; i++) {
                nx = (gint) x+neighbour_map[i].x;
                ny = (gint) y+neighbour_map[i].y;
                if ((c2 = cell_idx(mfield, nx, ny)) == -1)
                        continue;
                if (mfield->mines[c2].marked) n++;
        }
        if ((gint) mfield->mines[c].neighbours != n ||
            mfield->mines[c].marked ||
            !mfield->mines[c].shown) really = 0;

        for (i=0; i<8; i++) {
                nx = (gint) x+neighbour_map[i].x;
                ny = (gint) y+neighbour_map[i].y;
                if ((c2 = cell_idx(mfield, nx, ny)) == -1)
                        continue;
                if (mfield->mines[c2].down) {
                        mfield->mines[c2].down = 0;
                        if (really) {
                                mfield->mines[c2].shown = 1;
                                mfield->shown++;
                                if (mfield->mines[c2].mined == 1) {
                                        loose = 1;
                                }
                        }
                        gtk_mine_draw(mfield, (guint) nx, (guint) ny);
                }
        }
        if (loose) {
                gtk_minefield_loose(mfield);
        } else if (really) {
                gtk_minefield_check_field(mfield, (gint) x, (gint) y);
                if (mfield->shown == mfield->xsize*mfield->ysize-mfield->mcount) {
                        gtk_minefield_win(mfield);
                }
        }
}

/* The cell under (ex, ey), FALSE if that is off the field. */
static gboolean cell_at (GtkMineField *mfield, gdouble ex, gdouble ey,
                         guint *x, guint *y)
{
        gint cx, cy;

        if (ex < 0 || ey < 0)
                return FALSE;
        cx = (gint) ex / (gint) mfield->minesize;
        cy = (gint) ey / (gint) mfield->minesize;
        if (cx >= (gint) mfield->xsize || cy >= (gint) mfield->ysize)
                return FALSE;
        *x = (guint) cx;
        *y = (guint) cy;
        return TRUE;
}

static gint gtk_minefield_motion_notify(GtkWidget *widget, GdkEventMotion *event)
{
        GtkMineField *mfield;
        guint x, y;
        guint c;
        guint multi;

        g_return_val_if_fail(widget != NULL, 0);
        g_return_val_if_fail(GTK_IS_MINEFIELD(widget), 0);
        g_return_val_if_fail(event != NULL, 0);

        mfield = GTK_MINEFIELD(widget);

        if (mfield->lose || mfield->win) return FALSE;

        if (mfield->bdown[0] || mfield->bdown[1]) {
                if (!cell_at (mfield, event->x, event->y, &x, &y))
                        return 0;

                c = x+y*(mfield->xsize);

                if ((gint) c != mfield->cdown) {
                        mfield->mines[mfield->cdown].down = 0;
                        gtk_mine_draw(mfield, mfield->cdownx, mfield->cdowny);

                        multi = (guint) mfield->multi_mode;
                        if (multi) gtk_minefield_multi_release(mfield, mfield->cdownx, mfield->cdowny, (guint) mfield->cdown, 0);
                        mfield->cdownx = x;
                        mfield->cdowny = y;
                        mfield->cdown = (gint) c;
                        mfield->mines[c].down = 1;
                        gtk_mine_draw(mfield, x, y);

                        if (multi) gtk_minefield_multi_press(mfield, x, y);
                }
        }
        return FALSE;
}

static gint gtk_minefield_button_press(GtkWidget *widget, GdkEventButton *event)
{
        GtkMineField *mfield;
        guint x, y;
        guint c;

        g_return_val_if_fail(widget != NULL, 0);
        g_return_val_if_fail(GTK_IS_MINEFIELD(widget), 0);
        g_return_val_if_fail(event != NULL, 0);

        mfield = GTK_MINEFIELD(widget);

        if (mfield->lose || mfield->win) return FALSE;
        /* A double click comes as two presses as well; this is the third
         * event, which has no release of its own. */
        if (event->type != GDK_BUTTON_PRESS) return FALSE;

        if (event->button >= 1 && event->button <= 3 && !mfield->bdown[1]) {
                if (!cell_at (mfield, event->x, event->y, &x, &y))
                        return FALSE;
                c = x+y*(mfield->xsize);
                if (!mfield->bdown[0] && !mfield->bdown[1] && !mfield->bdown[2]) {
                        mfield->cdownx = x;
                        mfield->cdowny = y;
                        mfield->cdown = (gint) c;
                        mfield->mines[c].down = 1;
                }
                mfield->bdown[event->button-1]++;
                gtk_mine_draw(mfield, x, y);
                if (((event->button == 2) || (event->button == 1 && event->state & GDK_SHIFT_MASK)) ||
                   (mfield->bdown[0] && mfield->bdown[2]) ) { /* multi show */
                        gtk_minefield_multi_press(mfield, x, y);
                }
                else if (event->button == 3 && mfield->bdown[2] == 1)
                {
                        gtk_minefield_toggle_mark(mfield, x, y);
                        gtk_mine_draw(mfield, x, y);
                }
                if (event->button == 1 || event->button == 2) {
                        gtk_signal_emit(GTK_OBJECT(mfield),
                                        minefield_signals[LOOK_SIGNAL]);
                }
        }
        return FALSE;
}

static gint gtk_minefield_button_release(GtkWidget *widget, GdkEventButton *event)
{
        GtkMineField *mfield;
        guint button;

        g_return_val_if_fail(widget != NULL, FALSE);
        g_return_val_if_fail(GTK_IS_MINEFIELD(widget), FALSE);
        g_return_val_if_fail(event != NULL, FALSE);

        mfield = GTK_MINEFIELD(widget);

        if (mfield->lose || mfield->win) return FALSE;

        button = event->button;
        if (button >= 1 && button <= 3 && mfield->bdown[button-1] && mfield->cdown >= 0) {
                if (mfield->bdown[0] && mfield->bdown[2] && button != 2) {
                    /* left+right click = multi show */
                    mfield->bdown[0] = 0;
                    mfield->bdown[1] = 1;
                    mfield->bdown[2] = 0;
                    button = 2;
                }
                switch (button) {
                case 1:
                        if (event->state & GDK_SHIFT_MASK) gtk_minefield_multi_release(mfield, mfield->cdownx, mfield->cdowny, (guint) mfield->cdown, 1);
                        else gtk_minefield_show(mfield, mfield->cdownx, mfield->cdowny);
                        break;
                case 2: if (mfield->multi_mode) gtk_minefield_multi_release(mfield, mfield->cdownx, mfield->cdowny, (guint) mfield->cdown, 1);
                        break;
                }
                if (!mfield->lose && !mfield->win) {
                        gtk_signal_emit(GTK_OBJECT(mfield),
                                        minefield_signals[UNLOOK_SIGNAL]);
                }
                mfield->mines[mfield->cdown].down = 0;
                mfield->cdown = -1;
                mfield->bdown[button-1] = 0;
                gtk_mine_draw(mfield, mfield->cdownx, mfield->cdowny);
        }
        return FALSE;
}


static void gtk_minefield_class_init (gpointer klass)
{
        GtkMineFieldClass *class = klass;
        GtkWidgetClass *widget_class;
        GtkObjectClass *object_class;

        widget_class = (GtkWidgetClass *)class;
        object_class = (GtkObjectClass *)class;

        parent_class = gtk_type_class (gtk_widget_get_type ());

        object_class->destroy = gtk_minefield_destroy;
        widget_class->realize = gtk_minefield_realize;
        widget_class->unrealize = gtk_minefield_unrealize;
        widget_class->style_set = gtk_minefield_style_set;
        widget_class->size_allocate = gtk_minefield_size_allocate;
        widget_class->size_request = gtk_minefield_size_request;
        widget_class->expose_event = gtk_minefield_expose;
        widget_class->button_press_event = gtk_minefield_button_press;
        widget_class->button_release_event = gtk_minefield_button_release;
        widget_class->motion_notify_event = gtk_minefield_motion_notify;

        class->marks_changed = NULL;
        class->explode = NULL;
        class->look = NULL;
        class->unlook = NULL;
        class->win = NULL;

        minefield_signals[MARKS_CHANGED_SIGNAL] =
                gtk_signal_new("marks_changed",
                               GTK_RUN_FIRST,
                               object_class->type,
                               GTK_SIGNAL_OFFSET(GtkMineFieldClass, marks_changed),
                               gtk_signal_default_marshaller,
                               GTK_TYPE_NONE,
                               0);
        minefield_signals[EXPLODE_SIGNAL] =
                gtk_signal_new("explode",
                               GTK_RUN_FIRST,
                               object_class->type,
                               GTK_SIGNAL_OFFSET(GtkMineFieldClass, explode),
                               gtk_signal_default_marshaller,
                               GTK_TYPE_NONE,
                               0);
        minefield_signals[LOOK_SIGNAL] =
                gtk_signal_new("look",
                               GTK_RUN_FIRST,
                               object_class->type,
                               GTK_SIGNAL_OFFSET(GtkMineFieldClass, look),
                               gtk_signal_default_marshaller,
                               GTK_TYPE_NONE,
                               0);
        minefield_signals[UNLOOK_SIGNAL] =
                gtk_signal_new("unlook",
                               GTK_RUN_FIRST,
                               object_class->type,
                               GTK_SIGNAL_OFFSET(GtkMineFieldClass, unlook),
                               gtk_signal_default_marshaller,
                               GTK_TYPE_NONE,
                               0);
        minefield_signals[WIN_SIGNAL] =
                gtk_signal_new("win",
                               GTK_RUN_FIRST,
                               object_class->type,
                               GTK_SIGNAL_OFFSET(GtkMineFieldClass, win),
                               gtk_signal_default_marshaller,
                               GTK_TYPE_NONE,
                               0);

        gtk_object_class_add_signals(object_class, minefield_signals, LAST_SIGNAL);
}

static void gtk_minefield_init (gpointer object, gpointer klass)
{
        GtkMineField *mfield = object;

        (void) klass;
        mfield->xsize = 0;
        mfield->ysize = 0;
        mfield->mines = NULL;
        mfield->cdown = -1;
        mfield->minesize = 16;
        mfield->use_marks = TRUE;
}

void gtk_minefield_set_size(GtkMineField *mfield, guint xsize, guint ysize)
{
        g_return_if_fail(mfield != NULL);
        g_return_if_fail(GTK_IS_MINEFIELD(mfield));

        if (mfield->xsize*mfield->ysize != xsize*ysize || !mfield->mines) {
                mfield->mines = g_realloc(mfield->mines,
                                          sizeof(mine)*MAX (xsize*ysize, 1));
        }
        mfield->xsize = xsize;
        mfield->ysize = ysize;
}

GtkWidget* gtk_minefield_new(void)
{
        return GTK_WIDGET(gtk_type_new(gtk_minefield_get_type()));
}

GtkType gtk_minefield_get_type (void)
{
        static GtkType minefield_type = 0;

        if (!minefield_type)
        {
                static const GtkTypeInfo minefield_info =
                        {
                                "GtkMineField",
                                sizeof (GtkMineField),
                                sizeof (GtkMineFieldClass),
                                gtk_minefield_class_init,
                                gtk_minefield_init,
                                NULL,
                                NULL,
                                NULL
                        };

                minefield_type = gtk_type_unique (gtk_widget_get_type (), &minefield_info);
        }

        return minefield_type;
}

void gtk_minefield_set_mines(GtkMineField *mfield, guint mcount, guint minesize)
{
        g_return_if_fail(mfield != NULL);
        g_return_if_fail(GTK_IS_MINEFIELD(mfield));
        g_return_if_fail(minesize>0);

        mfield->mcount = mcount;
        if (minesize != mfield->minesize) {
                mfield->minesize = minesize;
                if (GTK_WIDGET_REALIZED(mfield))
                        gtk_minefield_setup_numbers(mfield);
        }

        if (GTK_WIDGET_VISIBLE(mfield)) {
                gtk_widget_queue_resize(GTK_WIDGET(mfield));
        }
}

void gtk_minefield_restart(GtkMineField *mfield)
{
        guint i, cells;

        g_return_if_fail(mfield != NULL);
        g_return_if_fail(GTK_IS_MINEFIELD(mfield));

        mfield->flags = 0;
        mfield->shown = 0;
        mfield->lose  = 0;
        mfield->win   = 0;
        mfield->bdown[0] = 0;
        mfield->bdown[1] = 0;
        mfield->bdown[2] = 0;
        mfield->cdown = -1;
        mfield->multi_mode = 0;

        cells = mfield->xsize*mfield->ysize;
        for (i=0; i<cells; i++) {
                mfield->mines[i].mined  = i < mfield->mcount;
                mfield->mines[i].marked = 0;
                mfield->mines[i].questioned = 0;
                mfield->mines[i].shown  = 0;
                mfield->mines[i].down   = 0;
        }

        /* The mines go in random places (Fisher-Yates). */
        for (i = cells; i > 1; i--) {
                guint j = (guint) games_rand_int ((gint) i);
                guint tmp = mfield->mines[i-1].mined;

                mfield->mines[i-1].mined = mfield->mines[j].mined;
                mfield->mines[j].mined = tmp;
        }

        count_neighbours(mfield);
}

void gtk_minefield_set_use_marks(GtkMineField *mfield, gboolean use_marks)
{
        guint i, cells;

        g_return_if_fail(mfield != NULL);
        g_return_if_fail(GTK_IS_MINEFIELD(mfield));

        mfield->use_marks = use_marks;
        /* Turning them off takes the marks already made away. */
        if (!use_marks && mfield->mines) {
                cells = mfield->xsize*mfield->ysize;
                for (i = 0; i < cells; i++)
                        mfield->mines[i].questioned = 0;
                gtk_minefield_draw(mfield, NULL);
        }
}
