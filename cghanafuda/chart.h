/*
 * cghanafuda: charts of the yaku and the cards, for learning
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

#ifndef CHART_H
#define CHART_H

/* Opens a chart, or raises it if it is open: the yaku, each with its
 * cards and how near each player is to it, or the cards, a month at a
 * time. Both follow the game as it is played. */
void chart_yaku (void);
void chart_cards (void);

/* Brings the charts open up to date with the game. */
void chart_update (void);

/* Closes them. */
void chart_close (void);

#endif
