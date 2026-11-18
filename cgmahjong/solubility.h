/*
 * cgmahjong: making games that can be solved
 * (C) 1998 the Free Software Foundation
 *
 * Author: Michael Meeks.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef SOLUBILITY_H
#define SOLUBILITY_H

/* Places the tiles of a new game, with games_rand_int's numbers, so
 * that the game can be finished. */
void generate_game (void);

/* Works out which positions cover or neighbour which, for pos. */
void generate_dependancies (void);

/* Whether a tile can be taken: nothing on it, and nothing beside it on
 * at least one side. */
int tile_free (int tile_num);

#endif
