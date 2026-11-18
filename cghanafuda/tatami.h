/*
 * cghanafuda: a tatami mat to play on
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

#ifndef TATAMI_H
#define TATAMI_H

#include <glib.h>

/* The border's width, in the game's pixels. */
#define TATAMI_BORDER 18

/* Draws a tatami mat over rgb, width by height pixels of three bytes
 * each, scale of them to the game's pixel: woven rush lying across,
 * edged left and right with a blue cloth border. */
void tatami_draw (guchar *rgb, gint width, gint height, double scale);

#endif
