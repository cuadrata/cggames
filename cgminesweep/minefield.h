/*
 * cgminesweep: the minefield
 * Copyright (C) 1997-1999 the Free Software Foundation
 *
 * Author: Pista <szekeres@cyberspace.mht.bme.hu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __GTK_MINEFIELD_H__
#define __GTK_MINEFIELD_H__

#include <gdk/gdk.h>
#include <gtk/gtkwidget.h>
#include <glib.h>

#define GTK_MINEFIELD(obj) GTK_CHECK_CAST(obj, gtk_minefield_get_type(), GtkMineField)
#define GTK_MINEFIELD_CLASS(klass) GTK_CHECK_CLASS_CAST(klass, gtk_minefield_get_type(), GtkMineFieldClass)
#define GTK_IS_MINEFIELD(obj) GTK_CHECK_TYPE(obj, gtk_minefield_get_type())

typedef struct _GtkMineField         GtkMineField;
typedef struct _GtkMineFieldClass    GtkMineFieldClass;

struct _Mine {
        guint mined:1;
        guint shown:1;
        guint marked:1;
        guint questioned:1;
        guint down:1;
        guint neighbours;
};

typedef struct _Mine mine;

/* The colours of the flags and mines, besides the numbers'. */
enum {
        PAINT_LIGHT,
        PAINT_BLACK,
        PAINT_RED,
        N_PAINTS
};

struct _GtkMineField {
        GtkWidget widget;
        guint xsize, ysize;
        guint mcount;
        mine *mines;
        guint flags;
        guint shown;
        gint cdown;
        guint cdownx;
        guint cdowny;
        gint bdown[3];
        gint lose;
        gint win;
        gint multi_mode;
        gboolean use_marks;
        GdkGC *paint[N_PAINTS];
        GdkFont   *font;
        struct {
                char text[2];
                gint dx, dy;
                GdkGC *gc;
        } numstr[9];
        guint minesize;
};

struct _GtkMineFieldClass
{
        GtkWidgetClass parent_class;
        void (*marks_changed) (GtkMineField *mfield);
        void (*explode) (GtkMineField *mfield);
        void (*look) (GtkMineField *mfield);
        void (*unlook) (GtkMineField *mfield);
        void (*win) (GtkMineField *mfield);
};


GtkType    gtk_minefield_get_type (void);
GtkWidget* gtk_minefield_new      (void);

void gtk_minefield_set_size(GtkMineField *mfield, guint xsize, guint ysize);
void gtk_minefield_set_mines(GtkMineField *mfield, guint mcount, guint minesize);
void gtk_minefield_restart(GtkMineField *mfield);
void gtk_minefield_set_use_marks(GtkMineField *mfield, gboolean use_marks);
void gtk_minefield_draw(GtkMineField *mfield, GdkRectangle *area);

#endif /* __GTK_MINEFIELD_H__ */
