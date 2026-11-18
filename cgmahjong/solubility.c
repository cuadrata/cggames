/*
 * cgmahjong: making games that can be solved
 * (C) 1998 the Free Software Foundation
 * Copyright (C) 2026 The CGTK authors
 *
 * Author: Michael Meeks.
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

#include <assert.h>

#include "games.h"
#include "cgmahjong.h"
#include "solubility.h"

struct _typeinfo {
  int type;
  int placed;
  int image[2];
};
typedef struct _typeinfo typeinfo;

static typeinfo type_info [MAX_TILES/2] = {
        { 0, 0, {0, 0} },
        { 0, 0, {0, 0} },
        { 1, 0, {1, 1} },
        { 1, 0, {1, 1} },
        { 2, 0, {2, 2} },
        { 2, 0, {2, 2} },
        { 3, 0, {3, 3} },
        { 3, 0, {3, 3} },
        { 4, 0, {4, 4} },
        { 4, 0, {4, 4} },
        { 5, 0, {5, 5} },
        { 5, 0, {5, 5} },
        { 6, 0, {6, 6} },
        { 6, 0, {6, 6} },
        { 7, 0, {7, 7} },
        { 7, 0, {7, 7} },
        { 8, 0, {8, 8} },
        { 8, 0, {8, 8} },
        { 9, 0, {9, 9} },
        { 9, 0, {9, 9} },
        { 10, 0, {10, 10} },
        { 10, 0, {10, 10} },
        { 11, 0, {11, 11} },
        { 11, 0, {11, 11} },
        { 12, 0, {12, 12} },
        { 12, 0, {12, 12} },
        { 13, 0, {13, 13} },
        { 13, 0, {13, 13} },
        { 14, 0, {14, 14} },
        { 14, 0, {14, 14} },
        { 15, 0, {15, 15} },
        { 15, 0, {15, 15} },
        { 16, 0, {16, 16} },
        { 16, 0, {16, 16} },
        { 17, 0, {17, 17} },
        { 17, 0, {17, 17} },
        { 18, 0, {18, 18} },
        { 18, 0, {18, 18} },
        { 19, 0, {19, 19} },
        { 19, 0, {19, 19} },
        { 20, 0, {20, 20} },
        { 20, 0, {20, 20} },
        { 21, 0, {21, 21} },
        { 21, 0, {21, 21} },
        { 22, 0, {22, 22} },
        { 22, 0, {22, 22} },
        { 23, 0, {23, 23} },
        { 23, 0, {23, 23} },
        { 24, 0, {24, 24} },
        { 24, 0, {24, 24} },
        { 25, 0, {25, 25} },
        { 25, 0, {25, 25} },
        { 26, 0, {26, 26} },
        { 26, 0, {26, 26} },
        { 27, 0, {27, 27} },
        { 27, 0, {27, 27} },
        { 28, 0, {28, 28} },
        { 28, 0, {28, 28} },
        { 29, 0, {29, 29} },
        { 29, 0, {29, 29} },
        { 30, 0, {30, 30} },
        { 30, 0, {30, 30} },
        { 31, 0, {31, 31} },
        { 31, 0, {31, 31} },
        { 32, 0, {32, 32} },
        { 32, 0, {32, 32} },
        { 33, 0, {33, 34} },
        { 33, 0, {35, 36} },
        { 34, 0, {37, 37} },
        { 34, 0, {37, 37} },
        { 35, 0, {38, 39} },
        { 35, 0, {40, 41} }
};

struct _dep_tree {
  int turn_dep[4] ;     /* Turning dependancies all must be clear */
  int place_dep[4] ;    /* Placing dependancies all must be full */
  int lhs_dep[2] ;      /* Sideways dependancies on this level */
  int rhs_dep[2] ;      /* N.B. Repeats are allowed. */
  int free ;            /* Can we put a tile in it ? */
  int filled ;          /* Does it have a tile in it ? */
};
static struct _dep_tree dep_tree[MAX_TILES] ;

#define random_UDL games_rand_int (MAX_TILES)
#define random_UDL_half games_rand_int (MAX_TILES/2)
#define RANDOM_LIMIT (MAX_TILES*MAX_TILES)


/* Takes the last tile places, and avoids its dependancies.
   Returns -1 when nowhere is found, and the game is started again. */
static int random_free (int lt)
{
  int t, lp, catch ;
  int goodplace ;

  catch = 0 ;
  do
    {
      do
        {
          t = random_UDL ;
          if (catch++>RANDOM_LIMIT)
            return -1 ;
        }
      while (!dep_tree[t].free) ;

      assert (!dep_tree[t].filled) ;

      goodplace = 1 ;
      if (lt!=-1)
        {
          for (lp=0;lp<4;lp++)
            if (dep_tree[t].place_dep[lp] == lt)
              goodplace = 0 ;
          for (lp=0;lp<2;lp++)
            {
              if ((dep_tree[t].lhs_dep[lp] == lt) ||
                  (dep_tree[t].rhs_dep[lp] == lt))
                goodplace = 0 ;
            }
        }
    }
  while (!goodplace) ;
  assert (!dep_tree[t].filled) ;
  return t ;
}

/* Finds a random block on a layer */
static int random_on_layer (int l)
{
  int t, cnt = 0 ;
  do
    {
      t = random_UDL ;
      if (cnt++>RANDOM_LIMIT)
        return -1 ;
    }
  while (pos[t].layer!=l) ;

  assert (!dep_tree[t].filled) ;
  return t ;
}

/* x,y specify the position of a _quarter_ tile, and layer its layer.
   This returns the offset of the first tile to intersect the x,y _quarter_
   it returns -1 if there is no tile at that position.
   N.B. Each tile occupies 2x2 positions.
 */
static int tile_at (int layer, int x, int y)
{
  int lp ;

  for (lp=0;lp<MAX_TILES;lp++)
    if ((pos[lp].layer == layer) &&
        (pos[lp].x==x || pos[lp].x==x+1) &&
        (pos[lp].y==y || pos[lp].y==y+1))
      return lp ;
  return -1 ;
}

/* This determines whether it is OK to mark this tile as free */
static int ok_free_validate_line (int t)
{
  int i, li, w ;
  int in, out ;
  static int nextl[MAX_TILES] ;
  static int nextr[MAX_TILES] ;

  /* Go left */
  in = 0 ;
  out = 0 ;
  nextl[in++] = t ;
  do
    {
      w = nextl[out++] ;
      if (dep_tree[w].free || dep_tree[w].filled)
        return 0 ;
      if ((i=dep_tree[w].lhs_dep[0])!=-1)
        nextl[in++] = i ;
      li = i ;
      if ((i=dep_tree[w].lhs_dep[1])!=-1 && li != i)
        nextl[in++] = i ;
    }
  while (in>out) ;

  /* Go right */
  in = 0 ;
  out = 0 ;
  nextr[in++] = t ;
  do
    {
      w = nextr[out++] ;
      if (dep_tree[w].free || dep_tree[w].filled)
        return 0 ;
      if ((i=dep_tree[w].rhs_dep[0])!=-1)
        nextr[in++] = i ;
      li = i ;
      if ((i=dep_tree[w].rhs_dep[1])!=-1 && li != i)
        nextr[in++] = i ;
    }
  while (in>out) ;


  return 1 ;
}

/* This simply examines whether the tile should be free or not.
   It does this by examining its dependancies to see if they are
   filled */
static void validate_tile (int t)
{
  int lfilled, rfilled, lp, i ;

  if (t==-1||dep_tree[t].filled)
    return ;    /* No point. */

  /* Check below */
  for (lp=0;lp<4;lp++)
    if ((i = dep_tree[t].place_dep[lp]) != -1)
      if (!dep_tree[i].filled) return ;

  if (ok_free_validate_line(t)) /* First in this layer on this line */
    {
      assert (pos[t].layer!=0) ; /* Intended */
      dep_tree[t].free = 1 ;
      return ;
    }

  /* LHS */
  if ((dep_tree[t].lhs_dep[0] == -1) &&
      (dep_tree[t].lhs_dep[1] == -1))
    lfilled = 0 ;  /* ie. so being on the edge != can put a tile there */
  else
    {
      lfilled = 1 ;
      for (lp=0;lp<2;lp++)
        if ((i = dep_tree[t].lhs_dep[lp]) != -1)
          if (!dep_tree[i].filled) lfilled = 0 ;
    }

  /* RHS */
  if ((dep_tree[t].rhs_dep[0] == -1) &&
      (dep_tree[t].rhs_dep[1] == -1))
    rfilled = 0 ;
  else
    {
      rfilled = 1 ;
      for (lp=0;lp<2;lp++)
        if ((i = dep_tree[t].rhs_dep[lp]) != -1)
          if (!dep_tree[i].filled) rfilled = 0 ;
    }

  if ((lfilled) || (rfilled))
    dep_tree[t].free = 1 ;
}
/* Place tile in map at position f, with pic & type from t */
static void place_tile (int f, int t, int idx)
{
  tiles[f].visible = 1;
  tiles[f].selected = 0;

  /*  if (tiles[f].layer>0)
      tiles[f].visible = 0 ;
      tiles[f].sequence = 0 ; */

  tiles[f].type = type_info[t].type;
  tiles[f].image = type_info[t].image[idx] ;

  assert (dep_tree[f].free) ;
  assert (!dep_tree[f].filled) ;
  dep_tree[f].filled = 1 ; /* Lord */
  dep_tree[f].free = 0 ;

  /* Now let the tiles near this one see if this makes them 'free'
   * N.B. You shall know the truth and the truth will set you free */
  {
    int lp, i ;
    for (lp=0;lp<4;lp++)
      if ((i=dep_tree[f].turn_dep[lp])!=-1)
        validate_tile(i) ;
    for (lp=0;lp<2;lp++)
      {
        if ((i=dep_tree[f].lhs_dep[lp])!=-1)
          validate_tile(i) ;
        if ((i=dep_tree[f].rhs_dep[lp])!=-1)
          validate_tile(i) ;
      }
  }
}

int tile_free (int tile_num)
{
  int lp, valid, t, lfree, rfree ;

  if (tile_num>=MAX_TILES)
    return 0 ;

  if (tiles[tile_num].visible == 0)
    return 0;

  valid = 1 ;
  /* Check above */
  for (lp=0;lp<4;lp++)
    if ((t = dep_tree[tile_num].turn_dep[lp]) != -1)
      if (tiles[t].visible) valid = 0 ;
  if (valid==0)
    return 0 ;

  lfree = 1 ;
  /* LHS */
  for (lp=0;lp<2;lp++)
    if ((t = dep_tree[tile_num].lhs_dep[lp]) != -1)
      if (tiles[t].visible) lfree = 0 ;
  rfree = 1 ;
  /* LHS */
  for (lp=0;lp<2;lp++)
    if ((t = dep_tree[tile_num].rhs_dep[lp]) != -1)
      if (tiles[t].visible) rfree = 0 ;
  if ((lfree) || (rfree)) return 1;
  else return 0 ;
}

static int random_tile_type (void)
{
  int o ;
  do
    o = random_UDL_half ;
  while (type_info[o].placed) ;
  return o ;
}

/* Remove duplicate entries */
static void unique (int *a, int l)
{
  int lp, sc ;
  if (l==2)
    {
      if (a[1] == a[0])
        a[1] = -1 ;
      return ;
    }
  for (lp=0;lp<l-1;lp++)
    {
      for (sc=lp+1;sc<l;sc++)
        {
          assert (lp!=sc) ;
          assert (sc<l) ;
          assert (lp<l) ;
          if (a[lp] == a[sc])
            a[sc] = -1 ;
        }
    }
}

/* Generate a single tiles dependancy tree entries */
/* N.B. Layer increases as you go up tiles BRC is x,y x+right y+down*/
static void tile_deps (int tn)
{
  int lp, layer, x, y, lpx, lpy ;

  /* Side dependancies */
  x = pos[tn].x ;
  y = pos[tn].y ;
  layer = pos[tn].layer ;

  dep_tree[tn].lhs_dep[0] = tile_at(layer, x-2, y) ;
  dep_tree[tn].lhs_dep[1] = tile_at(layer, x-2, y-1) ;

  dep_tree[tn].rhs_dep[0] = tile_at(layer, x+1, y) ;
  dep_tree[tn].rhs_dep[1] = tile_at(layer, x+1, y-1) ;

  /* Place / take dependancies */
  lp=0 ;
  for (lpx=0;lpx<=1;lpx++)      /* RHS -> LHS */
    for (lpy=0;lpy<=1;lpy++)    /* BOT -> TOP */
      {
        dep_tree[tn].turn_dep[lp] = tile_at(layer+1, x-lpx, y-lpy) ;
        dep_tree[tn].place_dep[lp] = tile_at(layer-1, x-lpx, y-lpy) ;
        lp++ ;
      }

  dep_tree[tn].filled = 0 ; /* Lord */
  dep_tree[tn].free = 0 ;

  unique(dep_tree[tn].turn_dep, 4) ;
  unique(dep_tree[tn].place_dep, 4) ;
  unique(dep_tree[tn].lhs_dep, 2) ;
  unique(dep_tree[tn].rhs_dep, 2) ;
}

/* This calculates the tree of up and down dependancies that is used
   to determine which tiles can be turned / how to place the tiles */
void generate_dependancies (void)
{
  int lp ;

  for (lp=0;lp<MAX_TILES;lp++)
    {
      tile_deps(lp) ;   /* Do the thrunging */
    }
}

/* One attempt at placing the tiles; FALSE if it got stuck. */
static int try_generate (void)
{
  int lp;

  for (lp = 0; lp < MAX_TILES; lp++)
    {
      tiles[lp].visible = 0;
      dep_tree[lp].free = 0 ;
      dep_tree[lp].filled = 0 ; /* Lord */
      type_info[lp/2].placed = 0 ;
    }

  /* OK ! we want only one tile per apparent horizontal line ! */
  for (lp=0;lp<MAX_TILES;lp++)
    {
      int t = random_on_layer(0) ;
      if (t == -1)
        return 0 ;
      if (ok_free_validate_line (t))
        dep_tree[t].free = 1 ;
    }
  /* Check we really got them all ? : very important for algorithem ! */
  for (lp=0;lp<MAX_TILES;lp++)
    {
        if (pos[lp].layer==0 &&
            ok_free_validate_line (lp))
            dep_tree[lp].free = 1 ;
    }

  {
    int last_place, type, offset, offset2;

    last_place = type = offset = -1;

    /* Insert */
    for (lp=0;lp<MAX_TILES;lp++)
      {
        /* Find somewhere to put it: every other tile depends on the last */
        if ((lp&0x1)==0)
          {
            type = random_tile_type() ;
            type_info[type].placed = 1 ;
            last_place = -1 ;
          }

        offset = random_free (last_place) ;
        if (offset == -1)
          return 0 ;
        if (lp<MAX_TILES-1) /* There is another tile */
          {
            offset2 = random_free (last_place) ;
            if (offset2 == -1)
              return 0 ;
            if (pos[offset2].layer > pos[offset].layer)
              offset = offset2 ;  /* Higher = better... */
          }
        place_tile (offset, type, (lp&1)) ;

        tiles[offset].sequence = 0 ;
        last_place = offset ;
      }
  }
  return 1 ;
}

/* Do the tile placement for a soluable game */
void generate_game (void)
{
  while (!try_generate ())
    ;
}
