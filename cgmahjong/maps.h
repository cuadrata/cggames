/*
 * cgmahjong's maps: the layouts the tiles are laid out in
 * Copyright (C) 2026 The CGTK authors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef MAPS_H
#define MAPS_H

#include <glib.h>

#include "cgmahjong.h"

typedef struct {
        gchar *name;            /* as shown */
        gchar *key;             /* as stored, and for the scores */
        tilepos pos[MAX_TILES]; /* bottom left first, top layer first */
} Map;

extern Map *maps;
extern gint n_maps;

/* Easy, which is built in, then the maps in the installed layouts.map
 * that have all their tiles, in the file's order. */
void maps_load (void);

/* The map stored as key; NULL if there is none. */
Map *maps_find (const gchar *key);

#endif
