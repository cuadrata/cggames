/*
 * cgmahjong main header, from GNOME Mahjongg
 * (C) 1998-1999 the Free Software Foundation
 *
 * Author: Francisco Bustamante et al.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef CGMAHJONG_H
#define CGMAHJONG_H

#define MAX_TILES 144
#define MAX_TILES_STR "144"

struct _tilepos {
        int x;
        int y;
        int layer;
};
typedef struct _tilepos tilepos;

extern tilepos *pos;

typedef struct _tile tile;

struct _tile {
        int type;
        int image;
        int visible;
        int selected;
        int sequence;
        int number;
};

extern tile tiles[MAX_TILES];

#endif
