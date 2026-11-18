/* cggames: random numbers
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

#include "config.h"

#include <time.h>
#include <unistd.h>

#include "games.h"

/* PCG32 (pcg-random.org, XSH RR), whose output depends only on the
 * seed, unlike rand(). */

static uint64_t state;
static const uint64_t increment = 1442695040888963407u;

static uint32_t
next (void)
{
  uint64_t old = state;
  uint32_t xorshifted = (uint32_t) (((old >> 18) ^ old) >> 27);
  uint32_t rot = (uint32_t) (old >> 59);

  state = old * 6364136223846793005u + increment;
  return (xorshifted >> rot) | (xorshifted << ((32 - rot) & 31));
}

void
games_rand_seed (uint32_t seed)
{
  state = 0;
  next ();
  state += seed;
  next ();
}

gint
games_rand_int (gint n)
{
  /* Rejects the top of the range that would favour low numbers. */
  uint32_t bound = (uint32_t) n;
  uint32_t threshold = -bound % bound;
  uint32_t r;

  g_return_val_if_fail (n > 0, 0);
  do
    r = next ();
  while (r < threshold);
  return (gint) (r % bound);
}

uint32_t
games_rand_new_seed (void)
{
  struct timespec now;
  uint64_t saved = state;
  uint32_t seed;

  clock_gettime (CLOCK_REALTIME, &now);
  games_rand_seed ((uint32_t) now.tv_sec ^ (uint32_t) now.tv_nsec
                   ^ ((uint32_t) getpid () << 16));
  /* Kept to what fits in a gint, for typing back in. */
  seed = next () & 0x7fffffff;
  state = saved;
  return seed;
}
