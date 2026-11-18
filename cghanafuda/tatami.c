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

#include "config.h"

#include <math.h>

#include "tatami.h"

/* The mat, in the game's pixels: rush stalks about STALK wide, each
 * its own width, lie one above another, bound by threads down across
 * them WARP apart, each stalk passing over one thread and under the
 * next; and the border, TATAMI_BORDER wide, down the left and right
 * sides as on a real mat's long sides. Along each stalk its colour
 * wanders over a length of about WANDER, and over the whole mat it
 * fades in patches about PATCH across. */
#define STALK 2.6
#define WARP 14.0
#define WANDER 90.0
#define PATCH 180.0

#define PI 3.14159265358979323846

/* The rush, between fresh and sunned: greener at a stalk's root and
 * paler at its tip. */
static const double fresh[3] = { 186, 184, 112 };
static const double sunned[3] = { 216, 194, 124 };

/* The border's cloth: its ground, its folded edges, the diamonds woven
 * along it and the lines and dots picked out in pale thread. */
static const double ground[3] = { 30, 48, 98 };
static const double diamond[3] = { 72, 106, 178 };
static const double pale[3] = { 200, 212, 236 };

/* The same pseudo-random number for the same n, every time. */
static guint32
hash (guint32 n)
{
  n ^= n >> 16;
  n *= 0x7feb352dU;
  n ^= n >> 15;
  n *= 0x846ca68bU;
  n ^= n >> 16;
  return n;
}

/* Between -1 and 1. */
static double
noise (guint32 n)
{
  return hash (n) / 2147483647.5 - 1;
}

static double
clamp01 (double v)
{
  return v < 0 ? 0 : v > 1 ? 1 : v;
}

/* c laid over p, cover of it. */
static void
over (double *p, const double *c, double cover)
{
  gint k;

  for (k = 0; k < 3; k++)
    p[k] += (c[k] - p[k]) * cover;
}

/* How far x is from the nearest multiple of period, in pixels. */
static double
from_beat (double x, double period)
{
  double f = fmod (x, period);

  if (f < 0)
    f += period;
  return MIN (f, period - f);
}

/* The border at a pixel, t across it from the outer edge (0) to the
 * mat (1), a along it, b its width in pixels, s pixels to the game's. */
static void
heri (double *p, double t, double a, double b, double s, guint32 n)
{
  double period = b, ha = 0.4 * period, ht = 0.26 * b;
  double ac = fmod (a, period), tc = (t - 0.5) * b, q, d, f;
  gint k;

  if (ac < 0)
    ac += period;
  ac -= period / 2;
  for (k = 0; k < 3; k++)
    p[k] = ground[k];

  /* The diamonds, each with a smaller of the ground in it and a pale
   * dot in the middle, and a pale dot between each two. */
  q = fabs (ac) / ha + fabs (tc) / ht;
  d = MIN (ha, ht) * 0.7;
  over (p, diamond, clamp01 ((1 - q) * d + 0.5));
  over (p, ground, clamp01 ((0.55 - q) * d + 0.5));
  over (p, pale, clamp01 ((0.2 - q) * d + 0.5));
  d = sqrt ((period / 2 - fabs (ac)) * (period / 2 - fabs (ac))
            + tc * tc);
  over (p, pale, clamp01 (0.07 * b - d + 0.5));

  /* A pale line near each edge. */
  d = MIN (fabs (t - 0.15), fabs (t - 0.85)) * b;
  over (p, pale, clamp01 (0.03 * b + 0.5 - d));

  /* The cloth's twill, its weave's grain, and its edges folded under,
   * darker. */
  f = 1 + 0.06 * (from_beat ((t * b + a) / s, 2.0) - 0.5)
    + 0.03 * noise (n);
  if (t < 0.07 || t > 0.93)
    f *= 0.72;
  for (k = 0; k < 3; k++)
    p[k] *= f;
}

/* Smoothly from 0 to 1 as f goes from 0 to 1. */
static double
smooth (double f)
{
  return f * f * (3 - 2 * f);
}

/* The grain over the mat, its own to each pixel of a tile GRAIN wide
 * and high, laid over and over. */
#define GRAIN 256

/* How finely a pixel's row is divided to see how much of each stalk it
 * holds and how the stalks' rounding shades it. */
#define SUBROWS 8

/* What is the same for every stalk at each x across the mat: the
 * threads, for stalks of either parity, and which length between two of
 * them it is; and where it is between the points the stalks' colours
 * wander through. */
typedef struct
{
  float *threads[2];
  gint *segment;
  gint *knot;
  float *between;
  gint knots, segments;
} Across;

/* What a stalk looks like all along its length, before it is shaded
 * round: stalk s, from the mat's left edge to its right, width pixels.
 * Its colour and lightness wander through a value at each knot, and
 * each length between threads is a little lighter or darker. */
static void
stalk_line (float (*line)[3], gint s, gint width, const Across *at,
            float *hue, float *lift, float *seg)
{
  guint32 seed = hash ((guint32) s * 0x9e3779b1U);
  double own = 0.5 + 0.15 * noise (seed);
  double light = 1 + 0.025 * noise (seed ^ 0x51ed270bU);
  const float *threads = at->threads[s & 1];
  float c[3], dc[3];
  gint x, k;

  for (k = 0; k <= at->knots; k++)
    {
      hue[k] = (float) (0.12 * noise (seed ^ hash ((guint32) k)));
      lift[k] = (float) (0.03 * noise (seed ^ hash ((guint32) k
                                                    ^ 0x2545f491U)));
    }
  for (k = 0; k <= at->segments; k++)
    seg[k] = (float) (light * (1 + 0.02 * noise (seed ^ hash ((guint32) k
                                                           ^ 0x68e31da4U))));
  for (k = 0; k < 3; k++)
    {
      c[k] = (float) fresh[k];
      dc[k] = (float) (sunned[k] - fresh[k]);
    }

  for (x = 0; x < width; x++)
    {
      /* Rushes are laid from either side in turn, root first. */
      float along = (x + 0.5f) / width;
      float root = s & 1 ? 1 - along : along;
      gint n = at->knot[x];
      float w = at->between[x];
      float mix = (float) own + 0.22f * (root - 0.5f)
        + hue[n] + (hue[n + 1] - hue[n]) * w;
      float f = (1 + lift[n] + (lift[n + 1] - lift[n]) * w)
        * seg[at->segment[x]] * threads[x];

      for (k = 0; k < 3; k++)
        line[x][k] = (c[k] + dc[k] * mix) * f;
    }
}

void
tatami_draw (guchar *rgb, gint width, gint height, double scale)
{
  double stalk = STALK * scale, warp = WARP * scale;
  double b = TATAMI_BORDER * scale, patch = PATCH * scale;
  double span = WANDER * scale;
  gint band = MIN ((gint) ceil (b), width / 2);
  gint lip = band + (gint) ceil (3 * scale);
  gint n = (gint) ceil (height / (0.8 * stalk)) + 2;
  double *top = g_malloc (sizeof *top * (n + 1));
  gint *first = g_malloc (sizeof *first * height);
  float *share = g_malloc (sizeof *share * height);
  float *shade = g_malloc (sizeof *shade * height);
  float *grain = g_malloc (sizeof *grain * GRAIN * GRAIN);
  float *hue, *lift, *seg;
  Across at;
  float (*line[2])[3];
  gint held[2] = { -1, -1 };
  gint *cell = g_malloc (sizeof *cell * width);
  float *across = g_malloc (sizeof *across * width);
  gint cols = (gint) (width / patch) + 2, rows = (gint) (height / patch) + 2;
  float *patches = g_malloc (sizeof *patches * cols * rows);
  float *faded = g_malloc (sizeof *faded * cols);
  gint x, y, k, p, s;

  /* Where each stalk lies, each a little wider or narrower. */
  top[0] = 0;
  for (s = 0; s < n; s++)
    top[s + 1] = top[s] + stalk * (0.8 + 0.4 * (noise (hash (s) ^ 7) + 1)
                                   / 2);

  /* Each row of pixels: the stalk at its top, how much of it the row
   * holds, the rest being the next, and the rounding of both, seen
   * through the whole pixel. */
  for (y = 0, s = 0; y < height; y++)
    {
      double sum = 0;
      gint j, mine = 0;

      while (top[s + 1] <= y)
        s++;
      first[y] = s;
      for (j = 0, k = s; j < SUBROWS; j++)
        {
          double py = y + (j + 0.5) / SUBROWS;

          while (k < n - 1 && top[k + 1] <= py)
            k++;
          if (k == s)
            mine++;
          sum += 0.84 + 0.16 * sin (PI * (py - top[k])
                                    / (top[k + 1] - top[k]));
        }
      share[y] = (float) mine / SUBROWS;
      shade[y] = (float) (sum / SUBROWS);
    }

  /* Across the mat, for stalks of either parity: over a thread and
   * lighter, or under it and darker, each length swelling between
   * threads, and the threads themselves, darker still. */
  at.segment = g_malloc (sizeof *at.segment * width);
  at.knot = g_malloc (sizeof *at.knot * width);
  at.between = g_malloc (sizeof *at.between * width);
  at.segments = (gint) (width / warp) + 1;
  at.knots = (gint) (width / span) + 1;
  for (p = 0; p < 2; p++)
    {
      at.threads[p] = g_malloc (sizeof *at.threads[p] * width);
      for (x = 0; x < width; x++)
        {
          gint gap = (gint) floor (x / warp);
          double v = (x + 0.5) / warp - gap;
          double f = ((gap + p) % 2 ? 0.975 : 1.0)
            * (0.985 + 0.015 * sin (PI * v));
          double d = from_beat (x + 0.5, warp);

          f *= 1 - 0.05 * clamp01 (0.5 * scale + 0.5 - d);
          at.threads[p][x] = (float) f;
          at.segment[x] = gap;
        }
    }
  for (x = 0; x < width; x++)
    {
      double f = x / span;

      at.knot[x] = (gint) floor (f);
      at.between[x] = (float) smooth (f - at.knot[x]);
    }
  hue = g_malloc (sizeof *hue * (at.knots + 1));
  lift = g_malloc (sizeof *lift * (at.knots + 1));
  seg = g_malloc (sizeof *seg * (at.segments + 1));

  for (k = 0; k < GRAIN * GRAIN; k++)
    grain[k] = (float) (1 + 0.025 * noise ((guint32) k));
  /* The patches, where the mat has faded more or less, between which
   * it fades smoothly. */
  for (k = 0; k < cols * rows; k++)
    patches[k] = (float) noise (hash ((guint32) k * 0x632be5abU
                                      ^ 0x5bd1e995U));
  line[0] = g_malloc (sizeof *line[0] * width);
  line[1] = g_malloc (sizeof *line[1] * width);
  for (x = 0; x < width; x++)
    {
      double f = x / patch;

      cell[x] = (gint) floor (f);
      across[x] = (float) smooth (f - cell[x]);
    }

  for (y = 0; y < height; y++)
    {
      guchar *row = rgb + (gsize) y * width * 3;
      const float *g = grain + (y % GRAIN) * GRAIN;
      const float (*c0)[3], (*c1)[3];
      float w0 = share[y], w1 = 1 - w0, sh = shade[y];
      double f = y / patch;
      gint j = (gint) floor (f);
      float down = (float) smooth (f - j);

      /* The two stalks this row holds, made once each as the rows go
       * down them. */
      s = first[y];
      if (held[1] == s)
        {
          float (*t)[3] = line[0];

          line[0] = line[1];
          line[1] = t;
          held[0] = s;
          held[1] = -1;
        }
      for (k = 0; k < 2; k++)
        if (held[k] != s + k)
          {
            stalk_line (line[k], s + k, width, &at, hue, lift, seg);
            held[k] = s + k;
          }
      c0 = (const float (*)[3]) line[0];
      c1 = (const float (*)[3]) line[1];
      for (k = 0; k < cols; k++)
        {
          const float *pt = patches + j * cols + k;

          faded[k] = pt[0] + (pt[cols] - pt[0]) * down;
        }

      for (x = 0; x < width; x++)
        {
          guchar *d = row + x * 3;
          const float *pf = faded + cell[x];
          float l = 1, fade = pf[0] + (pf[1] - pf[0]) * across[x];

          if (x < lip || x >= width - lip)
            {
              double edge = MIN (x + 0.5, width - x - 0.5);

              if (edge < b)
                {
                  double cloth[3];

                  heri (cloth, edge / b, y, b, scale,
                        (guint32) x * 7919U + (guint32) y * 104729U);
                  for (k = 0; k < 3; k++)
                    d[k] = (guchar) CLAMP (cloth[k] + 0.5, 0, 255);
                  continue;
                }
              /* The border stands a little proud of the mat. */
              l = (float) (1 - 0.3 * clamp01 (1 - (edge - b)
                                                / (3 * scale)));
            }

          /* The brightest stalk, lit most, stays below 255. */
          l *= sh * g[x % GRAIN] * (1 + 0.03f * fade);
          d[0] = (guchar) CLAMP ((c0[x][0] * w0 + c1[x][0] * w1) * l
                                 * (1 + 0.015f * fade) + 0.5f, 0, 255);
          d[1] = (guchar) CLAMP ((c0[x][1] * w0 + c1[x][1] * w1) * l
                                 + 0.5f, 0, 255);
          d[2] = (guchar) CLAMP ((c0[x][2] * w0 + c1[x][2] * w1) * l
                                 * (1 - 0.03f * fade) + 0.5f, 0, 255);
        }
    }

  g_free (line[0]);
  g_free (line[1]);
  g_free (faded);
  g_free (patches);
  g_free (across);
  g_free (cell);
  g_free (seg);
  g_free (lift);
  g_free (hue);
  g_free (at.threads[0]);
  g_free (at.threads[1]);
  g_free (at.between);
  g_free (at.knot);
  g_free (at.segment);
  g_free (grain);
  g_free (shade);
  g_free (share);
  g_free (first);
  g_free (top);
}
